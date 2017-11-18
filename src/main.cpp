#include <iostream>
#include <stdio.h>
#include <wiringPi.h>
#include <boost/program_options.hpp>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <libcec/cec.h>
#include <libcec/cecloader.h>
#include <signal.h>

namespace po = boost::program_options;

using namespace CEC;

std::atomic_flag detected = ATOMIC_FLAG_INIT;
std::atomic_flag interrupted = ATOMIC_FLAG_INIT;
libcec_configuration g_config;
ICECAdapter *g_parser;
ICECCallbacks        g_callbacks;

void handle_signal(int) {
	interrupted.test_and_set();
}

void on_interrupt() {
	detected.test_and_set();
	std::cout << "detected\n";
}

void CecLogMessage(void *cbParam, const cec_log_message* message) {
	//std::cerr << message->message << std::endl;
}
void CecAlert(void *cbParam, const libcec_alert type, const libcec_parameter param) {}

int main(int ac, char * av[]) {
	using namespace std::chrono_literals;
	detected.clear();
	interrupted.clear();

	po::options_description desc("allowed options");
	desc.add_options()
		("help", "produce help message")
		("pin", po::value<int>()->default_value(17), "ir sensor pin")
		("standby", po::value<double>()->default_value(5.), "standby timeout")
	;
	po::variables_map vm;
	po::store(po::parse_command_line(ac, av, desc), vm);
	po::notify(vm);

	if (vm.count("help")) {
		std::cout << desc << std::endl;
		return 1;
	}

	signal(SIGINT, handle_signal);	
	
	g_config.Clear();
	snprintf(g_config.strDeviceName, 13, "CECTester");
	g_config.clientVersion      = LIBCEC_VERSION_CURRENT;
	g_config.bActivateSource    = 0;
	g_callbacks.logMessage      = &CecLogMessage;
	//g_callbacks.keyPress        = &CecKeyPress;
	//g_callbacks.commandReceived = &CecCommand;
	g_callbacks.alert           = &CecAlert;
	g_config.callbacks = &g_callbacks;
	g_config.deviceTypes.Add(CEC_DEVICE_TYPE_RECORDING_DEVICE);
	//g_config.deviceTypes.Add(CEC_DEVICE_TYPE_TV);

	g_parser = LibCecInitialise(&g_config);

	if (!g_parser) {
		std::cerr << "could not initialize libcec\n";
		return 2;
	}

	g_parser->InitVideoStandalone();
	cec_adapter_descriptor devices[10];
	uint8_t iDevicesFound = g_parser->DetectAdapters(devices, 10, NULL, true);
	if (iDevicesFound <= 0) {
		std::cerr << "no devices found\n";
		UnloadLibCec(g_parser);
		return 2;
	}
	std::cout << "devices found:\n";
	for(int i = 0; i < iDevicesFound; i++) {
		std::cout << devices[i].strComName << std::endl;
	}
	if (!g_parser->Open(devices[0].strComName)) {
		std::cerr << "could not open device: " << devices[0].strComName << "\n";
		UnloadLibCec(g_parser);
		return 1;
	}

	int ir_pin = vm["pin"].as<int>();
	wiringPiSetupGpio();
	pinMode(ir_pin, INPUT);
	wiringPiISR(ir_pin, INT_EDGE_RISING, on_interrupt);

	auto standby = std::chrono::duration<double>(vm["standby"].as<double>());
	auto standby_time = std::chrono::system_clock::now() + std::chrono::duration<double>(standby);

	bool power_on = true;
	auto addr = (cec_logical_address)0;
	std::cout << "powering on\n";
	g_parser->PowerOnDevices(addr);

	while(!interrupted.test_and_set()) {
		interrupted.clear();

		bool sensed = detected.test_and_set();
		detected.clear();

		auto t = std::chrono::system_clock::now();
		if(sensed) {
			std::cout << "sensed\n";
			if (!power_on) {
				std::cout << "powering on\n";
				g_parser->PowerOnDevices(addr);
				power_on = true;
			}
			standby_time = t + standby;
			continue;
		}

		if (t > standby_time && power_on) {
			std::cout << "entering standby\n";
			g_parser->StandbyDevices(addr);
			power_on = false;
		}

		std::this_thread::sleep_for(500ms);
	}

	UnloadLibCec(g_parser);
	return 0;
}

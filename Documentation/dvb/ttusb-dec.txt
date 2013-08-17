TechnoTrend/Hauppauge DEC USB Driver
====================================

Driver Status
-------------

Supported:
	DEC2000-t
	DEC2450-t
	DEC3000-s
	Linux Kernels 2.4 and 2.6
	Video Streaming
	Audio Streaming
	Section Filters
	Channel Zapping
	Hotplug firmware loader under 2.6 kernels

To Do:
	Tuner status information
	DVB network interface
	Streaming video PC->DEC
	Conax support for 2450-t

Getting the Firmware
--------------------
To download the firmware, use the following commands:
"get_dvb_firmware dec2000t"
"get_dvb_firmware dec2540t"
"get_dvb_firmware dec3000s"


Compilation Notes for 2.4 kernels
---------------------------------
For 2.4 kernels the firmware for the DECs is compiled into the driver itself.

Copy the three files downloaded above into the build-2.4 directory.


Hotplug Firmware Loading for 2.6 kernels
----------------------------------------
For 2.6 kernels the firmware is loaded at the point that the driver module is
loaded.  See linux/Documentation/dvb/firmware.txt for more information.

Copy the three files downloaded above into the /usr/lib/hotplug/firmware or
/lib/firmware directory (depending on configuration of firmware hotplug).

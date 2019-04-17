Kernel driver menf21bmc_hwmon
=============================

Supported chips:

	* MEN 14F021P00

	  Prefix: 'menf21bmc_hwmon'

	  Adresses scanned: -

Author: Andreas Werner <andreas.werner@men.de>

Description
-----------

The menf21bmc is a Board Management Controller (BMC) which provides an I2C
interface to the host to access the features implemented in the BMC.

This driver gives access to the voltage monitoring feature of the main
voltages of the board.
The voltage sensors are connected to the ADC inputs of the BMC which is
a PIC16F917 Mikrocontroller.

Usage Notes
-----------

This driver is part of the MFD driver named "menf21bmc" and does
not auto-detect devices.
You will have to instantiate the MFD driver explicitly.
Please see Documentation/i2c/instantiating-devices for
details.

Sysfs entries
-------------

The following attributes are supported. All attributes are read only
The Limits are read once by the driver.

=============== ==========================
in0_input	+3.3V input voltage
in1_input	+5.0V input voltage
in2_input	+12.0V input voltage
in3_input	+5V Standby input voltage
in4_input	VBAT (on board battery)

in[0-4]_min	Minimum voltage limit
in[0-4]_max	Maximum voltage limit

in0_label	"MON_3_3V"
in1_label	"MON_5V"
in2_label	"MON_12V"
in3_label	"5V_STANDBY"
in4_label	"VBAT"
=============== ==========================

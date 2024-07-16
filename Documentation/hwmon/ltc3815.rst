Kernel driver ltc3815
=====================

Supported chips:

  * Linear Technology LTC3815

    Prefix: 'ltc3815'

    Addresses scanned: -

    Datasheet: http://www.linear.com/product/ltc3815

Author: Guenter Roeck <linux@roeck-us.net>


Description
-----------

LTC3815 is a Monolithic Synchronous DC/DC Step-Down Converter.


Usage Notes
-----------

This driver does not probe for PMBus devices. You will have to instantiate
devices explicitly.

Example: the following commands will load the driver for an LTC3815
at address 0x20 on I2C bus #1::

	# modprobe ltc3815
	# echo ltc3815 0x20 > /sys/bus/i2c/devices/i2c-1/new_device


Sysfs attributes
----------------

======================= =======================================================
in1_label		"vin"
in1_input		Measured input voltage.
in1_alarm		Input voltage alarm.
in1_highest		Highest input voltage.
in1_reset_history	Reset input voltage history.

in2_label		"vout1".
in2_input		Measured output voltage.
in2_alarm		Output voltage alarm.
in2_highest		Highest output voltage.
in2_reset_history	Reset output voltage history.

temp1_input		Measured chip temperature.
temp1_alarm		Temperature alarm.
temp1_highest		Highest measured temperature.
temp1_reset_history	Reset temperature history.

curr1_label		"iin".
curr1_input		Measured input current.
curr1_highest		Highest input current.
curr1_reset_history	Reset input current history.

curr2_label		"iout1".
curr2_input		Measured output current.
curr2_alarm		Output current alarm.
curr2_highest		Highest output current.
curr2_reset_history	Reset output current history.
======================= =======================================================

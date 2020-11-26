Kernel driver ltc2945
=====================

Supported chips:

  * Linear Technology LTC2945

    Prefix: 'ltc2945'

    Addresses scanned: -

    Datasheet:

	https://www.analog.com/media/en/technical-documentation/data-sheets/2945fb.pdf

Author: Guenter Roeck <linux@roeck-us.net>


Description
-----------

The LTC2945  is a rail-to-rail system monitor that measures current, voltage,
and power consumption.


Usage Notes
-----------

This driver does not probe for LTC2945 devices, since there is no register
which can be safely used to identify the chip. You will have to instantiate
the devices explicitly.

Example: the following will load the driver for an LTC2945 at address 0x10
on I2C bus #1::

	$ modprobe ltc2945
	$ echo ltc2945 0x10 > /sys/bus/i2c/devices/i2c-1/new_device


Sysfs entries
-------------

Voltage readings provided by this driver are reported as obtained from the ADC
registers. If a set of voltage divider resistors is installed, calculate the
real voltage by multiplying the reported value with (R1+R2)/R2, where R1 is the
value of the divider resistor against the measured voltage and R2 is the value
of the divider resistor against Ground.

Current reading provided by this driver is reported as obtained from the ADC
Current Sense register. The reported value assumes that a 1 mOhm sense resistor
is installed. If a different sense resistor is installed, calculate the real
current by dividing the reported value by the sense resistor value in mOhm.

======================= ========================================================
in1_input		VIN voltage (mV). Voltage is measured either at
			SENSE+ or VDD pin depending on chip configuration.
in1_min			Undervoltage threshold
in1_max			Overvoltage threshold
in1_lowest		Lowest measured voltage
in1_highest		Highest measured voltage
in1_reset_history	Write 1 to reset in1 history
in1_min_alarm		Undervoltage alarm
in1_max_alarm		Overvoltage alarm

in2_input		ADIN voltage (mV)
in2_min			Undervoltage threshold
in2_max			Overvoltage threshold
in2_lowest		Lowest measured voltage
in2_highest		Highest measured voltage
in2_reset_history	Write 1 to reset in2 history
in2_min_alarm		Undervoltage alarm
in2_max_alarm		Overvoltage alarm

curr1_input		SENSE current (mA)
curr1_min		Undercurrent threshold
curr1_max		Overcurrent threshold
curr1_lowest		Lowest measured current
curr1_highest		Highest measured current
curr1_reset_history	Write 1 to reset curr1 history
curr1_min_alarm		Undercurrent alarm
curr1_max_alarm		Overcurrent alarm

power1_input		Power (in uW). Power is calculated based on SENSE+/VDD
			voltage or ADIN voltage depending on chip configuration.
power1_min		Low lower threshold
power1_max		High power threshold
power1_input_lowest	Historical minimum power use
power1_input_highest	Historical maximum power use
power1_reset_history	Write 1 to reset power1 history
power1_min_alarm	Low power alarm
power1_max_alarm	High power alarm
======================= ========================================================

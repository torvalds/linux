Kernel driver inspur-ipsps1
===========================

Supported chips:

  * Inspur Power System power supply unit

Author: John Wang <wangzqbj@inspur.com>

Description
-----------

This driver supports Inspur Power System power supplies. This driver
is a client to the core PMBus driver.

Usage Notes
-----------

This driver does not auto-detect devices. You will have to instantiate the
devices explicitly. Please see Documentation/i2c/instantiating-devices.rst for
details.

Sysfs entries
-------------

The following attributes are supported:

======================= ======================================================
curr1_input		Measured input current
curr1_label		"iin"
curr1_max		Maximum current
curr1_max_alarm		Current high alarm
curr2_input		Measured output current in mA.
curr2_label		"iout1"
curr2_crit		Critical maximum current
curr2_crit_alarm	Current critical high alarm
curr2_max		Maximum current
curr2_max_alarm		Current high alarm

fan1_alarm		Fan 1 warning.
fan1_fault		Fan 1 fault.
fan1_input		Fan 1 speed in RPM.

in1_alarm		Input voltage under-voltage alarm.
in1_input		Measured input voltage in mV.
in1_label		"vin"
in2_input		Measured output voltage in mV.
in2_label		"vout1"
in2_lcrit		Critical minimum output voltage
in2_lcrit_alarm		Output voltage critical low alarm
in2_max			Maximum output voltage
in2_max_alarm		Output voltage high alarm
in2_min			Minimum output voltage
in2_min_alarm		Output voltage low alarm

power1_alarm		Input fault or alarm.
power1_input		Measured input power in uW.
power1_label		"pin"
power1_max		Input power limit
power2_max_alarm	Output power high alarm
power2_max		Output power limit
power2_input		Measured output power in uW.
power2_label		"pout"

temp[1-3]_input		Measured temperature
temp[1-2]_max		Maximum temperature
temp[1-3]_max_alarm	Temperature high alarm

vendor			Manufacturer name
model			Product model
part_number		Product part number
serial_number		Product serial number
fw_version		Firmware version
hw_version		Hardware version
mode			Work mode. Can be set to active or
			standby, when set to standby, PSU will
			automatically switch between standby
			and redundancy mode.
======================= ======================================================

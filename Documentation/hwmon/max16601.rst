.. SPDX-License-Identifier: GPL-2.0

Kernel driver max16601
======================

Supported chips:

  * Maxim MAX16508

    Prefix: 'max16508'

    Addresses scanned: -

    Datasheet: Not published

  * Maxim MAX16601

    Prefix: 'max16601'

    Addresses scanned: -

    Datasheet: Not published

Author: Guenter Roeck <linux@roeck-us.net>


Description
-----------

This driver supports the MAX16508 VR13 Dual-Output Voltage Regulator
as well as the MAX16601 VR13.HC Dual-Output Voltage Regulator chipsets.

The driver is a client driver to the core PMBus driver.
Please see Documentation/hwmon/pmbus.rst for details on PMBus client drivers.


Usage Notes
-----------

This driver does not auto-detect devices. You will have to instantiate the
devices explicitly. Please see Documentation/i2c/instantiating-devices.rst for
details.


Platform data support
---------------------

The driver supports standard PMBus driver platform data.


Sysfs entries
-------------

The following attributes are supported.

=============================== ===============================================
in1_label			"vin1"
in1_input			VCORE input voltage.
in1_alarm			Input voltage alarm.

in2_label			"vout1"
in2_input			VCORE output voltage.
in2_alarm			Output voltage alarm.

curr1_label			"iin1"
curr1_input			VCORE input current, derived from duty cycle
				and output current.
curr1_max			Maximum input current.
curr1_max_alarm			Current high alarm.

curr[P+2]_label			"iin1.P"
curr[P+2]_input			VCORE phase P input current.

curr[N+2]_label			"iin2"
curr[N+2]_input			VCORE input current, derived from sensor
				element.
				'N' is the number of enabled/populated phases.

curr[N+3]_label			"iin3"
curr[N+3]_input			VSA input current.

curr[N+4]_label			"iout1"
curr[N+4]_input			VCORE output current.
curr[N+4]_crit			Critical output current.
curr[N+4]_crit_alarm		Output current critical alarm.
curr[N+4]_max			Maximum output current.
curr[N+4]_max_alarm		Output current high alarm.

curr[N+P+5]_label		"iout1.P"
curr[N+P+5]_input		VCORE phase P output current.

curr[2*N+5]_label		"iout3"
curr[2*N+5]_input		VSA output current.
curr[2*N+5]_highest		Historical maximum VSA output current.
curr[2*N+5]_reset_history	Write any value to reset curr21_highest.
curr[2*N+5]_crit		Critical output current.
curr[2*N+5]_crit_alarm		Output current critical alarm.
curr[2*N+5]_max			Maximum output current.
curr[2*N+5]_max_alarm		Output current high alarm.

power1_label			"pin1"
power1_input			Input power, derived from duty cycle and output
				current.
power1_alarm			Input power alarm.

power2_label			"pin2"
power2_input			Input power, derived from input current sensor.

power3_label			"pout"
power3_input			Output power.

temp1_input			VCORE temperature.
temp1_crit			Critical high temperature.
temp1_crit_alarm		Chip temperature critical high alarm.
temp1_max			Maximum temperature.
temp1_max_alarm			Chip temperature high alarm.

temp2_input			TSENSE_0 temperature
temp3_input			TSENSE_1 temperature
temp4_input			TSENSE_2 temperature
temp5_input			TSENSE_3 temperature

temp6_input			VSA temperature.
temp6_crit			Critical high temperature.
temp6_crit_alarm		Chip temperature critical high alarm.
temp6_max			Maximum temperature.
temp6_max_alarm			Chip temperature high alarm.
=============================== ===============================================

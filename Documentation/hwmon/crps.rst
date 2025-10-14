.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver crps
==================

Supported chips:

  * Intel CRPS185

    Prefix: 'crps185'

    Addresses scanned: -

    Datasheet: Only available under NDA.

Authors:
    Ninad Palsule <ninad@linux.ibm.com>


Description
-----------

This driver implements support for Intel Common Redundant Power supply with
PMBus support.

The driver is a client driver to the core PMBus driver.
Please see Documentation/hwmon/pmbus.rst for details on PMBus client drivers.


Usage Notes
-----------

This driver does not auto-detect devices. You will have to instantiate the
devices explicitly. Please see Documentation/i2c/instantiating-devices.rst for
details.


Sysfs entries
-------------

======================= ======================================================
curr1_label		"iin"
curr1_input		Measured input current
curr1_max		Maximum input current
curr1_max_alarm		Input maximum current high alarm
curr1_crit		Critical high input current
curr1_crit_alarm	Input critical current high alarm
curr1_rated_max		Maximum rated input current

curr2_label		"iout1"
curr2_input		Measured output current
curr2_max		Maximum output current
curr2_max_alarm		Output maximum current high alarm
curr2_crit		Critical high output current
curr2_crit_alarm	Output critical current high alarm
curr2_rated_max		Maximum rated output current

in1_label		"vin"
in1_input		Measured input voltage
in1_crit		Critical input over voltage
in1_crit_alarm		Critical input over voltage alarm
in1_max			Maximum input over voltage
in1_max_alarm		Maximum input over voltage alarm
in1_rated_min		Minimum rated input voltage
in1_rated_max		Maximum rated input voltage

in2_label		"vout1"
in2_input		Measured input voltage
in2_crit		Critical input over voltage
in2_crit_alarm		Critical input over voltage alarm
in2_lcrit		Critical input under voltage fault
in2_lcrit_alarm		Critical input under voltage fault alarm
in2_max			Maximum input over voltage
in2_max_alarm		Maximum input over voltage alarm
in2_min			Minimum input under voltage warning
in2_min_alarm		Minimum input under voltage warning alarm
in2_rated_min		Minimum rated input voltage
in2_rated_max		Maximum rated input voltage

power1_label		"pin"
power1_input		Measured input power
power1_alarm		Input power high alarm
power1_max  		Maximum input power
power1_rated_max	Maximum rated input power

temp[1-2]_input		Measured temperature
temp[1-2]_crit 		Critical temperature
temp[1-2]_crit_alarm	Critical temperature alarm
temp[1-2]_max		Maximum temperature
temp[1-2]_max_alarm	Maximum temperature alarm
temp[1-2]_rated_max	Maximum rated temperature

fan1_alarm		Fan 1 warning.
fan1_fault		Fan 1 fault.
fan1_input		Fan 1 speed in RPM.
fan1_target		Fan 1 target.
======================= ======================================================

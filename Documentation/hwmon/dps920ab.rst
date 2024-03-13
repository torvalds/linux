.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver dps920ab
========================

Supported chips:

  * Delta DPS920AB

    Prefix: 'dps920ab'

    Addresses scanned: -

Authors:
    Robert Marko <robert.marko@sartura.hr>


Description
-----------

This driver implements support for Delta DPS920AB 920W 54V DC single output
power supply with PMBus support.

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
curr1_alarm		Input current high alarm

curr2_label		"iout1"
curr2_input		Measured output current
curr2_max		Maximum output current
curr2_rated_max		Maximum rated output current

in1_label		"vin"
in1_input		Measured input voltage
in1_alarm		Input voltage alarm

in2_label		"vout1"
in2_input		Measured output voltage
in2_rated_min		Minimum rated output voltage
in2_rated_max		Maximum rated output voltage
in2_alarm		Output voltage alarm

power1_label		"pin"
power1_input		Measured input power
power1_alarm		Input power high alarm

power2_label		"pout1"
power2_input		Measured output power
power2_rated_max	Maximum rated output power

temp[1-3]_input		Measured temperature
temp[1-3]_alarm		Temperature alarm

fan1_alarm		Fan 1 warning.
fan1_fault		Fan 1 fault.
fan1_input		Fan 1 speed in RPM.
======================= ======================================================

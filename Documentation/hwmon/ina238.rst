.. SPDX-License-Identifier: GPL-2.0-only

Kernel driver ina238
====================

Supported chips:

  * Texas Instruments INA238

    Prefix: 'ina238'

    Addresses: I2C 0x40 - 0x4f

    Datasheet:
	https://www.ti.com/lit/gpn/ina238

Author: Nathan Rossi <nathan.rossi@digi.com>

Description
-----------

The INA238 is a current shunt, power and temperature monitor with an I2C
interface. It includes a number of programmable functions including alerts,
conversion rate, sample averaging and selectable shunt voltage accuracy.

The shunt value in micro-ohms can be set via platform data or device tree at
compile-time or via the shunt_resistor attribute in sysfs at run-time. Please
refer to the Documentation/devicetree/bindings/hwmon/ti,ina2xx.yaml for bindings
if the device tree is used.

Sysfs entries
-------------

======================= =======================================================
in0_input		Shunt voltage (mV)
in0_min			Minimum shunt voltage threshold (mV)
in0_min_alarm		Minimum shunt voltage alarm
in0_max			Maximum shunt voltage threshold (mV)
in0_max_alarm		Maximum shunt voltage alarm

in1_input		Bus voltage (mV)
in1_min			Minimum bus voltage threshold (mV)
in1_min_alarm		Minimum shunt voltage alarm
in1_max			Maximum bus voltage threshold (mV)
in1_max_alarm		Maximum shunt voltage alarm

power1_input		Power measurement (uW)
power1_max		Maximum power threshold (uW)
power1_max_alarm	Maximum power alarm

curr1_input		Current measurement (mA)

temp1_input		Die temperature measurement (mC)
temp1_max		Maximum die temperature threshold (mC)
temp1_max_alarm		Maximum die temperature alarm
======================= =======================================================

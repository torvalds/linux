.. SPDX-License-Identifier: GPL-2.0

Kernel driver bpa-rs600
=======================

Supported chips:

  * BPA-RS600-120

    Datasheet: Publicly available at the BluTek website
       http://blutekpower.com/wp-content/uploads/2019/01/BPA-RS600-120-07-19-2018.pdf

Authors:
      - Chris Packham <chris.packham@alliedtelesis.co.nz>

Description
-----------

The BPA-RS600 is a compact 600W removable power supply module.

Usage Notes
-----------

This driver does not probe for PMBus devices. You will have to instantiate
devices explicitly.

Sysfs attributes
----------------

======================= ============================================
curr1_label             "iin"
curr1_input		Measured input current
curr1_max		Maximum input current
curr1_max_alarm		Input current high alarm

curr2_label		"iout1"
curr2_input		Measured output current
curr2_max		Maximum output current
curr2_max_alarm		Output current high alarm

fan1_input		Measured fan speed
fan1_alarm		Fan warning
fan1_fault		Fan fault

in1_label		"vin"
in1_input		Measured input voltage
in1_max			Maximum input voltage
in1_max_alarm		Input voltage high alarm
in1_min			Minimum input voltage
in1_min_alarm		Input voltage low alarm

in2_label		"vout1"
in2_input		Measured output voltage
in2_max			Maximum output voltage
in2_max_alarm		Output voltage high alarm
in2_min			Maximum output voltage
in2_min_alarm		Output voltage low alarm

power1_label		"pin"
power1_input		Measured input power
power1_alarm		Input power alarm
power1_max		Maximum input power

power2_label		"pout1"
power2_input		Measured output power
power2_max		Maximum output power
power2_max_alarm	Output power high alarm

temp1_input		Measured temperature around input connector
temp1_alarm		Temperature alarm

temp2_input		Measured temperature around output connector
temp2_alarm		Temperature alarm
======================= ============================================

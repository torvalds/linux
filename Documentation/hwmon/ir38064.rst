Kernel driver ir38064
=====================

Supported chips:

  * Infineon IR38060

    Prefix: 'IR38060'
    Addresses scanned: -

    Datasheet: Publicly available at the Infineon website
      https://www.infineon.com/dgdl/Infineon-IR38060M-DS-v03_16-EN.pdf?fileId=5546d4625c167129015c3291ea9a4cee

  * Infineon IR38064

    Prefix: 'ir38064'
    Addresses scanned: -

    Datasheet: Publicly available at the Infineon website
      https://www.infineon.com/dgdl/Infineon-IR38064MTRPBF-DS-v03_07-EN.pdf?fileId=5546d462584d1d4a0158db0d9efb67ca

  * Infineon IR38164

    Prefix: 'ir38164'
    Addresses scanned: -

    Datasheet: Publicly available at the Infineon website
      https://www.infineon.com/dgdl/Infineon-IR38164M-DS-v02_02-EN.pdf?fileId=5546d462636cc8fb01640046efea1248

  * Infineon ir38263

    Prefix: 'ir38263'
    Addresses scanned: -

    Datasheet:  Publicly available at the Infineon website
      https://www.infineon.com/dgdl/Infineon-IR38263M-DataSheet-v03_05-EN.pdf?fileId=5546d4625b62cd8a015bcf81f90a6e52

Authors:
      - Maxim Sloyko <maxims@google.com>
      - Patrick Venture <venture@google.com>

Description
-----------

IR38x6x are a Single-input Voltage, Synchronous Buck Regulator, DC-DC Converter.

Usage Notes
-----------

This driver does not probe for PMBus devices. You will have to instantiate
devices explicitly.

Sysfs attributes
----------------

======================= ===========================
curr1_label		"iout1"
curr1_input		Measured output current
curr1_crit		Critical maximum current
curr1_crit_alarm	Current critical high alarm
curr1_max		Maximum current
curr1_max_alarm		Current high alarm

in1_label		"vin"
in1_input		Measured input voltage
in1_crit		Critical maximum input voltage
in1_crit_alarm		Input voltage critical high alarm
in1_min			Minimum input voltage
in1_min_alarm		Input voltage low alarm

in2_label		"vout1"
in2_input		Measured output voltage
in2_lcrit		Critical minimum output voltage
in2_lcrit_alarm		Output voltage critical low alarm
in2_crit		Critical maximum output voltage
in2_crit_alarm		Output voltage critical high alarm
in2_max			Maximum output voltage
in2_max_alarm		Output voltage high alarm
in2_min			Minimum output voltage
in2_min_alarm		Output voltage low alarm

power1_label		"pout1"
power1_input		Measured output power

temp1_input		Measured temperature
temp1_crit		Critical high temperature
temp1_crit_alarm	Chip temperature critical high alarm
temp1_max		Maximum temperature
temp1_max_alarm		Chip temperature high alarm
======================= ===========================

Kernel driver raspberrypi-hwmon
===============================

Supported boards:

  * Raspberry Pi A+ (via GPIO on SoC)
  * Raspberry Pi B+ (via GPIO on SoC)
  * Raspberry Pi 2 B (via GPIO on SoC)
  * Raspberry Pi 3 B (via GPIO on port expander)
  * Raspberry Pi 3 B+ (via PMIC)

Author: Stefan Wahren <stefan.wahren@i2se.com>

Description
-----------

This driver periodically polls a mailbox property of the VC4 firmware to detect
undervoltage conditions.

Sysfs entries
-------------

======================= ======================================================
in0_input		Core voltage in millivolts
in1_input		SDRAM controller voltage in millivolts
in2_input		SDRAM I/O voltage in millivolts
in3_input		SDRAM PHY voltage in millivolts
in0_label		"core"
in1_label		"sdram_c"
in2_label		"sdram_i"
in3_label		"sdram_p"
in0_lcrit_alarm		Undervoltage alarm
======================= ======================================================

The voltage inputs and labels are only exposed if the firmware reports support
for the corresponding voltage ID.

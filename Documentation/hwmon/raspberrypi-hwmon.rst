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

======================= ==================
in0_lcrit_alarm		Undervoltage alarm
======================= ==================

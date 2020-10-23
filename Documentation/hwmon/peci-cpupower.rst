.. SPDX-License-Identifier: GPL-2.0

Kernel driver peci-cpupower
==========================

:Copyright: |copy| 2018-2020 Intel Corporation

Supported chips:
	One of Intel server CPUs listed below which is connected to a PECI bus.
		* Intel Xeon E5/E7 v3 server processors
			Intel Xeon E5-14xx v3 family
			Intel Xeon E5-24xx v3 family
			Intel Xeon E5-16xx v3 family
			Intel Xeon E5-26xx v3 family
			Intel Xeon E5-46xx v3 family
			Intel Xeon E7-48xx v3 family
			Intel Xeon E7-88xx v3 family
		* Intel Xeon E5/E7 v4 server processors
			Intel Xeon E5-16xx v4 family
			Intel Xeon E5-26xx v4 family
			Intel Xeon E5-46xx v4 family
			Intel Xeon E7-48xx v4 family
			Intel Xeon E7-88xx v4 family
		* Intel Xeon Scalable server processors
			Intel Xeon D family
			Intel Xeon Bronze family
			Intel Xeon Silver family
			Intel Xeon Gold family
			Intel Xeon Platinum family

	Addresses scanned: PECI client address 0x30 - 0x37
	Datasheet: Available from http://www.intel.com/design/literature.htm

Author:
	Zhikui Ren <zhikui.ren@.intel.com>

Description
-----------

This driver implements a generic PECI hwmon feature which provides
average power and energy consumption readings of the CPU package based on
energy counter.

Power values are average power since last measure given in milli Watt and
will be measurable only when the target CPU is powered on.

Energy values are energy consumption in micro Joules.

Driver provides current package power limit, maximal (TDP) and minimal power
setting as well.

All needed processor registers are accessible using the PECI Client Command
Suite via the processor PECI client.

``sysfs`` interface
-------------------
======================= =======================================================
power1_label		Provides string "cpu power".
power1_average		Provides average power since last read in milli Watt.
power1_cap		Provides current package power limit 1 (PPL1).
power1_cap_max		Provides maximal (TDP) package power setting.
power1_cap_min		Provides minimal package power setting.
energy1_label		Provides string "cpu energy".
energy1_input		Provides energy consumption in micro Joules.
======================= =======================================================

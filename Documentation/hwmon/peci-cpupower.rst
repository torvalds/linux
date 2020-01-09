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
average power consumption readings of the CPU package based on energy counter
accessible using the PECI Client Command Suite via the processor PECI client.

Power values are average power since last measure given in milli Watt and
will be measurable only when the target CPU is powered on.

``sysfs`` interface
-------------------
======================= =======================================================
power1_average		Provides average power since last read in milli Watt.
power1_label		Provides string "Average Power".
======================= =======================================================

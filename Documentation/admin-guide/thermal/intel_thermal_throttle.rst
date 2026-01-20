.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

=======================================
Intel thermal throttle events reporting
=======================================

:Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>

Introduction
------------

Intel processors have built in automatic and adaptive thermal monitoring
mechanisms that force the processor to reduce its power consumption in order
to operate within predetermined temperature limits.

Refer to section "THERMAL MONITORING AND PROTECTION" in the "Intel® 64 and
IA-32 Architectures Software Developer’s Manual Volume 3 (3A, 3B, 3C, & 3D):
System Programming Guide" for more details.

In general, there are two mechanisms to control the core temperature of the
processor. They are called "Thermal Monitor 1 (TM1) and Thermal Monitor 2 (TM2)".

The status of the temperature sensor that triggers the thermal monitor (TM1/TM2)
is indicated through the "thermal status flag" and "thermal status log flag" in
MSR_IA32_THERM_STATUS for core level and MSR_IA32_PACKAGE_THERM_STATUS for
package level.

Thermal Status flag, bit 0 — When set, indicates that the processor core
temperature is currently at the trip temperature of the thermal monitor and that
the processor power consumption is being reduced via either TM1 or TM2, depending
on which is enabled. When clear, the flag indicates that the core temperature is
below the thermal monitor trip temperature. This flag is read only.

Thermal Status Log flag, bit 1 — When set, indicates that the thermal sensor has
tripped since the last power-up or reset or since the last time that software
cleared this flag. This flag is a sticky bit; once set it remains set until
cleared by software or until a power-up or reset of the processor. The default
state is clear.

It is possible that when user reads MSR_IA32_THERM_STATUS or
MSR_IA32_PACKAGE_THERM_STATUS, TM1/TM2 is not active. In this case,
"Thermal Status flag" will read "0" and the "Thermal Status Log flag" will be set
to show any previous "TM1/TM2" activation. But since it needs to be cleared by
the software, it can't show the number of occurrences of "TM1/TM2" activations.

Hence, Linux provides counters of how many times the "Thermal Status flag" was
set. Also presents how long the "Thermal Status flag" was active in milliseconds.
Using these counters, users can check if the performance was limited because of
thermal events. It is recommended to read from sysfs instead of directly reading
MSRs as the "Thermal Status Log flag" is reset by the driver to implement rate
control.

Sysfs Interface
---------------

Thermal throttling events are presented for each CPU under
"/sys/devices/system/cpu/cpuX/thermal_throttle/", where "X" is the CPU number.

All these counters are read-only. They can't be reset to 0. So, they can potentially
overflow after reaching the maximum 64 bit unsigned integer.

``core_throttle_count``
	Shows the number of times "Thermal Status flag" changed from 0 to 1 for this
	CPU since OS boot and thermal vector is initialized. This is a 64 bit counter.

``package_throttle_count``
	Shows the number of times "Thermal Status flag" changed from 0 to 1 for the
	package containing this CPU since OS boot and thermal vector is initialized.
	Package status is broadcast to all CPUs; all CPUs in the package increment
	this count. This is a 64-bit counter.

``core_throttle_max_time_ms``
	Shows the maximum amount of time for which "Thermal Status flag" has been
	set to 1 for this CPU at the core level since OS boot and thermal vector
	is initialized.

``package_throttle_max_time_ms``
	Shows the maximum amount of time for which "Thermal Status flag" has been
	set to 1 for the package containing this CPU since OS boot and thermal
	vector is initialized.

``core_throttle_total_time_ms``
	Shows the cumulative time for which "Thermal Status flag" has been
	set to 1 for this CPU for core level since OS boot and thermal vector
	is initialized.

``package_throttle_total_time_ms``
	Shows the cumulative time for which "Thermal Status flag" has been set
	to 1 for the package containing this CPU since OS boot and thermal vector
	is initialized.

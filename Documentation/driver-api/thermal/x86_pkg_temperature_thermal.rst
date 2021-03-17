===================================
Kernel driver: x86_pkg_temp_thermal
===================================

Supported chips:

* x86: with package level thermal management

(Verify using: CPUID.06H:EAX[bit 6] =1)

Authors: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>

Reference
---------

Intel® 64 and IA-32 Architectures Software Developer’s Manual (Jan, 2013):
Chapter 14.6: PACKAGE LEVEL THERMAL MANAGEMENT

Description
-----------

This driver register CPU digital temperature package level sensor as a thermal
zone with maximum two user mode configurable trip points. Number of trip points
depends on the capability of the package. Once the trip point is violated,
user mode can receive notification via thermal notification mechanism and can
take any action to control temperature.


Threshold management
--------------------
Each package will register as a thermal zone under /sys/class/thermal.

Example::

	/sys/class/thermal/thermal_zone1

This contains two trip points:

- trip_point_0_temp
- trip_point_1_temp

User can set any temperature between 0 to TJ-Max temperature. Temperature units
are in milli-degree Celsius. Refer to "Documentation/driver-api/thermal/sysfs-api.rst" for
thermal sys-fs details.

Any value other than 0 in these trip points, can trigger thermal notifications.
Setting 0, stops sending thermal notifications.

Thermal notifications:
To get kobject-uevent notifications, set the thermal zone
policy to "user_space".

For example::

	echo -n "user_space" > policy

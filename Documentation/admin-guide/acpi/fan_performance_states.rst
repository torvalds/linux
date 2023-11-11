.. SPDX-License-Identifier: GPL-2.0

===========================
ACPI Fan Performance States
===========================

When the optional _FPS object is present under an ACPI device representing a
fan (for example, PNP0C0B or INT3404), the ACPI fan driver creates additional
"state*" attributes in the sysfs directory of the ACPI device in question.
These attributes list properties of fan performance states.

For more information on _FPS refer to the ACPI specification at:

http://uefi.org/specifications

For instance, the contents of the INT3404 ACPI device sysfs directory
may look as follows::

 $ ls -l /sys/bus/acpi/devices/INT3404:00/
 total 0
 ...
 -r--r--r-- 1 root root 4096 Dec 13 20:38 state0
 -r--r--r-- 1 root root 4096 Dec 13 20:38 state1
 -r--r--r-- 1 root root 4096 Dec 13 20:38 state10
 -r--r--r-- 1 root root 4096 Dec 13 20:38 state11
 -r--r--r-- 1 root root 4096 Dec 13 20:38 state2
 -r--r--r-- 1 root root 4096 Dec 13 20:38 state3
 -r--r--r-- 1 root root 4096 Dec 13 20:38 state4
 -r--r--r-- 1 root root 4096 Dec 13 20:38 state5
 -r--r--r-- 1 root root 4096 Dec 13 20:38 state6
 -r--r--r-- 1 root root 4096 Dec 13 20:38 state7
 -r--r--r-- 1 root root 4096 Dec 13 20:38 state8
 -r--r--r-- 1 root root 4096 Dec 13 20:38 state9
 -r--r--r-- 1 root root 4096 Dec 13 01:00 status
 ...

where each of the "state*" files represents one performance state of the fan
and contains a colon-separated list of 5 integer numbers (fields) with the
following interpretation::

  control_percent:trip_point_index:speed_rpm:noise_level_mdb:power_mw

* ``control_percent``: The percent value to be used to set the fan speed to a
  specific level using the _FSL object (0-100).

* ``trip_point_index``: The active cooling trip point number that corresponds
  to this performance state (0-9).

* ``speed_rpm``: Speed of the fan in rotations per minute.

* ``noise_level_mdb``: Audible noise emitted by the fan in this state in
  millidecibels.

* ``power_mw``: Power draw of the fan in this state in milliwatts.

For example::

 $cat /sys/bus/acpi/devices/INT3404:00/state1
 25:0:3200:12500:1250

When a given field is not populated or its value provided by the platform
firmware is invalid, the "not-defined" string is shown instead of the value.

ACPI Fan Fine Grain Control
=============================

When _FIF object specifies support for fine grain control, then fan speed
can be set from 0 to 100% with the recommended minimum "step size" via
_FSL object. User can adjust fan speed using thermal sysfs cooling device.

Here use can look at fan performance states for a reference speed (speed_rpm)
and set it by changing cooling device cur_state. If the fine grain control
is supported then user can also adjust to some other speeds which are
not defined in the performance states.

The support of fine grain control is presented via sysfs attribute
"fine_grain_control". If fine grain control is present, this attribute
will show "1" otherwise "0".

This sysfs attribute is presented in the same directory as performance states.

ACPI Fan Performance Feedback
=============================

The optional _FST object provides status information for the fan device.
This includes field to provide current fan speed in revolutions per minute
at which the fan is rotating.

This speed is presented in the sysfs using the attribute "fan_speed_rpm",
in the same directory as performance states.

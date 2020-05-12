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

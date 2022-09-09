.. SPDX-License-Identifier: GPL-2.0

===============================================================
Configurable sysfs parameters for the x86-64 machine check code
===============================================================

Machine checks report internal hardware error conditions detected
by the CPU. Uncorrected errors typically cause a machine check
(often with panic), corrected ones cause a machine check log entry.

Machine checks are organized in banks (normally associated with
a hardware subsystem) and subevents in a bank. The exact meaning
of the banks and subevent is CPU specific.

mcelog knows how to decode them.

When you see the "Machine check errors logged" message in the system
log then mcelog should run to collect and decode machine check entries
from /dev/mcelog. Normally mcelog should be run regularly from a cronjob.

Each CPU has a directory in /sys/devices/system/machinecheck/machinecheckN
(N = CPU number).

The directory contains some configurable entries. See
Documentation/ABI/testing/sysfs-mce for more details.

TBD document entries for AMD threshold interrupt configuration

For more details about the x86 machine check architecture
see the Intel and AMD architecture manuals from their developer websites.

For more details about the architecture
see http://one.firstfloor.org/~andi/mce.pdf

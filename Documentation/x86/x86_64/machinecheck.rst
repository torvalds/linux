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

The directory contains some configurable entries:

bankNctl
	(N bank number)

	64bit Hex bitmask enabling/disabling specific subevents for bank N
	When a bit in the bitmask is zero then the respective
	subevent will not be reported.
	By default all events are enabled.
	Note that BIOS maintain another mask to disable specific events
	per bank.  This is not visible here

The following entries appear for each CPU, but they are truly shared
between all CPUs.

check_interval
	How often to poll for corrected machine check errors, in seconds
	(Note output is hexadecimal). Default 5 minutes.  When the poller
	finds MCEs it triggers an exponential speedup (poll more often) on
	the polling interval.  When the poller stops finding MCEs, it
	triggers an exponential backoff (poll less often) on the polling
	interval. The check_interval variable is both the initial and
	maximum polling interval. 0 means no polling for corrected machine
	check errors (but some corrected errors might be still reported
	in other ways)

tolerant
	Tolerance level. When a machine check exception occurs for a non
	corrected machine check the kernel can take different actions.
	Since machine check exceptions can happen any time it is sometimes
	risky for the kernel to kill a process because it defies
	normal kernel locking rules. The tolerance level configures
	how hard the kernel tries to recover even at some risk of
	deadlock.  Higher tolerant values trade potentially better uptime
	with the risk of a crash or even corruption (for tolerant >= 3).

	0: always panic on uncorrected errors, log corrected errors
	1: panic or SIGBUS on uncorrected errors, log corrected errors
	2: SIGBUS or log uncorrected errors, log corrected errors
	3: never panic or SIGBUS, log all errors (for testing only)

	Default: 1

	Note this only makes a difference if the CPU allows recovery
	from a machine check exception. Current x86 CPUs generally do not.

trigger
	Program to run when a machine check event is detected.
	This is an alternative to running mcelog regularly from cron
	and allows to detect events faster.
monarch_timeout
	How long to wait for the other CPUs to machine check too on a
	exception. 0 to disable waiting for other CPUs.
	Unit: us

TBD document entries for AMD threshold interrupt configuration

For more details about the x86 machine check architecture
see the Intel and AMD architecture manuals from their developer websites.

For more details about the architecture see
see http://one.firstfloor.org/~andi/mce.pdf

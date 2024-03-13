.. SPDX-License-Identifier: GPL-2.0

===========================
Low Power Idle Table (LPIT)
===========================

To enumerate platform Low Power Idle states, Intel platforms are using
“Low Power Idle Table” (LPIT). More details about this table can be
downloaded from:
https://www.uefi.org/sites/default/files/resources/Intel_ACPI_Low_Power_S0_Idle.pdf

Residencies for each low power state can be read via FFH
(Function fixed hardware) or a memory mapped interface.

On platforms supporting S0ix sleep states, there can be two types of
residencies:

  - CPU PKG C10 (Read via FFH interface)
  - Platform Controller Hub (PCH) SLP_S0 (Read via memory mapped interface)

The following attributes are added dynamically to the cpuidle
sysfs attribute group::

  /sys/devices/system/cpu/cpuidle/low_power_idle_cpu_residency_us
  /sys/devices/system/cpu/cpuidle/low_power_idle_system_residency_us

The "low_power_idle_cpu_residency_us" attribute shows time spent
by the CPU package in PKG C10

The "low_power_idle_system_residency_us" attribute shows SLP_S0
residency, or system time spent with the SLP_S0# signal asserted.
This is the lowest possible system power state, achieved only when CPU is in
PKG C10 and all functional blocks in PCH are in a low power state.

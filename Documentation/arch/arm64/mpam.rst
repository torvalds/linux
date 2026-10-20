.. SPDX-License-Identifier: GPL-2.0

====
MPAM
====

What is MPAM
============
MPAM (Memory Partitioning and Monitoring) is a feature in the CPUs and memory
system components such as the caches or memory controllers that allow memory
traffic to be labelled, partitioned and monitored.

Traffic is labelled by the CPU, based on the control or monitor group the
current task is assigned to using resctrl.  Partitioning policy can be set
using the schemata file in resctrl, and monitor values read via resctrl.
See Documentation/filesystems/resctrl.rst for more details.

This allows tasks that share memory system resources, such as caches, to be
isolated from each other according to the partitioning policy (so called noisy
neighbours).

Supported Platforms
===================
Use of this feature requires CPU support, support in the memory system
components, and a description from firmware of where the MPAM device controls
are in the MMIO address space. (e.g. the 'MPAM' ACPI table).

The MMIO device that provides MPAM controls/monitors for a memory system
component is called a memory system component. (MSC).

Because the user interface to MPAM is via resctrl, only MPAM features that are
compatible with resctrl can be exposed to user-space.

MSC are considered as a group based on the topology. MSC that correspond with
the L3 cache are considered together, it is not possible to mix MSC between L2
and L3 to 'cover' a resctrl schema.

The supported features are:

* Cache portion bitmap controls (CPOR) on the L2 or L3 caches.  To expose
  CPOR at L2 or L3, every CPU must have a corresponding CPU cache at this
  level that also supports the feature.  Mismatched big/little platforms are
  not supported as resctrl's controls would then also depend on task
  placement.

* Memory bandwidth maximum controls (MBW_MAX) on or after the L3 cache.
  resctrl uses the L3 cache-id to identify where the memory bandwidth
  control is applied. For this reason the platform must have an L3 cache
  with cache-id's supplied by firmware. (It doesn't need to support MPAM.)

  To be exported as the 'MB' schema, the topology of the group of MSC chosen
  must match the topology of the L3 cache so that the cache-id's can be
  repainted. For example: Platforms with Memory bandwidth maximum controls
  on CPU-less NUMA nodes cannot expose the 'MB' schema to resctrl as these
  nodes do not have a corresponding L3 cache. If the memory bandwidth
  control is on the memory rather than the L3 then there must be a single
  global L3 as otherwise it is unknown which L3 the traffic came from. There
  must be no caches between the L3 and the memory so that the two ends of
  the path have equivalent traffic.

  When the MPAM driver finds multiple groups of MSC it can use for the 'MB'
  schema, it prefers the group closest to the L3 cache.

* Cache Storage Usage (CSU) counters can expose the 'llc_occupancy' provided
  there is at least one CSU monitor on each MSC that makes up the L3 group.
  Exposing CSU counters from other caches or devices is not supported.

* Memory Bandwidth Usage (MBWU) on or after the L3 cache.  resctrl uses the
  L3 cache-id to identify where the memory bandwidth is measured. For this
  reason the platform must have an L3 cache with cache-id's supplied by
  firmware. (The platform doesn't need to support MPAM.)

  Memory bandwidth monitoring makes use of MBWU monitors in each MSC that
  makes up the L3 group. If the memory bandwidth monitoring is on the memory
  rather than the L3 then there must be a single global L3 as otherwise it
  is unknown which L3 the traffic came from.

  To expose 'mbm_total_bytes', the topology of the group of MSC chosen must
  match the topology of the L3 cache so that the cache-id's can be
  repainted. For example: Platforms with Memory bandwidth monitors on
  CPU-less NUMA nodes cannot expose 'mbm_total_bytes' as these nodes do not
  have a corresponding L3 cache. 'mbm_local_bytes' is not exposed as MPAM
  cannot distinguish local traffic from global traffic.

  All these restrictions based on L3 cache are due to resctrl, currently, only
  supporting monitoring at the L3 scope. It is expected that going forward more
  MBWU monitors can be exposed to the user after support for more monitoring
  scopes is added to resctrl.

Command line parameters
=======================

arm64.nompam
------------
Firmware controls MPAM through two bits of MPAM3_EL3. MPAMEN enables
it: while set, the PARTID and PMG in the MPAMn_ELx registers label the
CPU's memory requests. TRAPLOWER, which resets to 1, traps accesses to
the MPAM system registers from the lower exception levels to EL3.
Firmware must clear it, or handle the trap and emulate MPAM as disabled.
Where it does neither, the CPUs still advertise MPAM in the ID registers,
the kernel's MPAM register accesses trap to EL3, and the boot fails.
``arm64.nompam`` exists for that firmware: it makes the kernel treat the
CPUs as not implementing MPAM, so no MPAM system register is accessed.
Set it only on a machine that does not boot without it.

It is not a way to turn MPAM off. Where firmware has cleared TRAPLOWER,
the option leaves the trap controls in MPAM2_EL2 and MPAMHCR_EL2
unwritten, and their reset values are UNKNOWN. KVM still hides MPAM from
guests but no longer enables the traps that stop a guest from using it,
so a guest may be able to read and write MPAM0_EL1, MPAM1_EL1 and
MPAMSM_EL1. KVM does not save or restore them, so what one guest writes
is still there when the next guest runs on that CPU, and when the host
does. With MPAMEN set, EL0 and EL1 requests then carry that PARTID and
PMG.

Reporting Bugs
==============
If you are not seeing the counters or controls you expect please share the
debug messages produced when enabling dynamic debug and booting with:
dyndbg="file mpam_resctrl.c +pl"

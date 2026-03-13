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

Reporting Bugs
==============
If you are not seeing the counters or controls you expect please share the
debug messages produced when enabling dynamic debug and booting with:
dyndbg="file mpam_resctrl.c +pl"

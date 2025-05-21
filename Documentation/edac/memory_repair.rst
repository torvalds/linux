.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.2-no-invariants-or-later

==========================
EDAC Memory Repair Control
==========================

Copyright (c) 2024-2025 HiSilicon Limited.

:Author:   Shiju Jose <shiju.jose@huawei.com>
:License:  The GNU Free Documentation License, Version 1.2 without
           Invariant Sections, Front-Cover Texts nor Back-Cover Texts.
           (dual licensed under the GPL v2)
:Original Reviewers:

- Written for: 6.15

Introduction
------------

Some memory devices support repair operations to address issues in their
memory media. Post Package Repair (PPR) and memory sparing are examples of
such features.

Post Package Repair (PPR)
~~~~~~~~~~~~~~~~~~~~~~~~~

Post Package Repair is a maintenance operation which requests the memory
device to perform repair operation on its media. It is a memory self-healing
feature that fixes a failing memory location by replacing it with a spare row
in a DRAM device.

For example, a CXL memory device with DRAM components that support PPR
features implements maintenance operations. DRAM components support those
types of PPR functions:

 - hard PPR, for a permanent row repair, and
 - soft PPR, for a temporary row repair.

Soft PPR is much faster than hard PPR, but the repair is lost after a power
cycle.

The data may not be retained and memory requests may not be correctly
processed during a repair operation. In such case, the repair operation should
not be executed at runtime.

For example, for CXL memory devices, see CXL spec rev 3.1 [1]_ sections
8.2.9.7.1.1 PPR Maintenance Operations, 8.2.9.7.1.2 sPPR Maintenance Operation
and 8.2.9.7.1.3 hPPR Maintenance Operation for more details.

Memory Sparing
~~~~~~~~~~~~~~

Memory sparing is a repair function that replaces a portion of memory with
a portion of functional memory at a particular granularity. Memory
sparing has cacheline/row/bank/rank sparing granularities. For example, in
rank memory-sparing mode, one memory rank serves as a spare for other ranks on
the same channel in case they fail.

The spare rank is held in reserve and not used as active memory until
a failure is indicated, with reserved capacity subtracted from the total
available memory in the system.

After an error threshold is surpassed in a system protected by memory sparing,
the content of a failing rank of DIMMs is copied to the spare rank. The
failing rank is then taken offline and the spare rank placed online for use as
active memory in place of the failed rank.

For example, CXL memory devices can support various subclasses for sparing
operation vary in terms of the scope of the sparing being performed.

Cacheline sparing subclass refers to a sparing action that can replace a full
cacheline. Row sparing is provided as an alternative to PPR sparing functions
and its scope is that of a single DDR row. Bank sparing allows an entire bank
to be replaced. Rank sparing is defined as an operation in which an entire DDR
rank is replaced.

See CXL spec 3.1 [1]_ section 8.2.9.7.1.4 Memory Sparing Maintenance
Operations for more details.

.. [1] https://computeexpresslink.org/cxl-specification/

Use cases of generic memory repair features control
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1. The soft PPR, hard PPR and memory-sparing features share similar control
   attributes. Therefore, there is a need for a standardized, generic sysfs
   repair control that is exposed to userspace and used by administrators,
   scripts and tools.

2. When a CXL device detects an error in a memory component, it informs the
   host of the need for a repair maintenance operation by using an event
   record where the "maintenance needed" flag is set. The event record
   specifies the device physical address (DPA) and attributes of the memory
   that requires repair. The kernel reports the corresponding CXL general
   media or DRAM trace event to userspace, and userspace tools (e.g.
   rasdaemon) initiate a repair maintenance operation in response to the
   device request using the sysfs repair control.

3. Userspace tools, such as rasdaemon, request a repair operation on a memory
   region when maintenance need flag set or an uncorrected memory error or
   excess of corrected memory errors above a threshold value is reported or an
   exceed corrected errors threshold flag set for that memory.

4. Multiple PPR/sparing instances may be present per memory device.

5. Drivers should enforce that live repair is safe. In systems where memory
   mapping functions can change between boots, one approach to this is to log
   memory errors seen on this boot against which to check live memory repair
   requests.

The File System
---------------

The control attributes of a registered memory repair instance could be
accessed in the /sys/bus/edac/devices/<dev-name>/mem_repairX/

sysfs
-----

Sysfs files are documented in
`Documentation/ABI/testing/sysfs-edac-memory-repair`.

Examples
--------

The memory repair usage takes the form shown in this example:

1. CXL memory sparing

Memory sparing is defined as a repair function that replaces a portion of
memory with a portion of functional memory at that same DPA. The subclass
for this operation, cacheline/row/bank/rank sparing, vary in terms of the
scope of the sparing being performed.

Memory sparing maintenance operations may be supported by CXL devices that
implement CXL.mem protocol. A sparing maintenance operation requests the
CXL device to perform a repair operation on its media. For example, a CXL
device with DRAM components that support memory sparing features may
implement sparing maintenance operations.

2. CXL memory Soft Post Package Repair (sPPR)

Post Package Repair (PPR) maintenance operations may be supported by CXL
devices that implement CXL.mem protocol. A PPR maintenance operation
requests the CXL device to perform a repair operation on its media.
For example, a CXL device with DRAM components that support PPR features
may implement PPR Maintenance operations. Soft PPR (sPPR) is a temporary
row repair. Soft PPR may be faster, but the repair is lost with a power
cycle.

Sysfs files for memory repair are documented in
`Documentation/ABI/testing/sysfs-edac-memory-repair`

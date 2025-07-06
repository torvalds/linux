.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.2-no-invariants-or-later

=============
Scrub Control
=============

Copyright (c) 2024-2025 HiSilicon Limited.

:Author:   Shiju Jose <shiju.jose@huawei.com>
:License:  The GNU Free Documentation License, Version 1.2 without
           Invariant Sections, Front-Cover Texts nor Back-Cover Texts.
           (dual licensed under the GPL v2)

- Written for: 6.15

Introduction
------------

Increasing DRAM size and cost have made memory subsystem reliability an
important concern. These modules are used where potentially corrupted data
could cause expensive or fatal issues. Memory errors are among the top
hardware failures that cause server and workload crashes.

Memory scrubbing is a feature where an ECC (Error-Correcting Code) engine
reads data from each memory media location, corrects if necessary and writes
the corrected data back to the same memory media location.

DIMMs can be scrubbed at a configurable rate to detect uncorrected memory
errors and attempt recovery from detected errors, providing the following
benefits:

1. Proactively scrubbing DIMMs reduces the chance of a correctable error
   becoming uncorrectable.

2. When detected, uncorrected errors caught in unallocated memory pages are
   isolated and prevented from being allocated to an application or the OS.

3. This reduces the likelihood of software or hardware products encountering
   memory errors.

4. The additional data on failures in memory may be used to build up
   statistics that are later used to decide whether to use memory repair
   technologies such as Post Package Repair or Sparing.

There are 2 types of memory scrubbing:

1. Background (patrol) scrubbing while the DRAM is otherwise idle.

2. On-demand scrubbing for a specific address range or region of memory.

Several types of interfaces to hardware memory scrubbers have been
identified, such as CXL memory device patrol scrub, CXL DDR5 ECS, ACPI
RAS2 memory scrubbing, and ACPI NVDIMM ARS (Address Range Scrub).

The control mechanisms vary across different memory scrubbers. To enable
standardized userspace tooling, there is a need to present these controls
through a standardized ABI.

A generic memory EDAC scrub control allows users to manage underlying
scrubbers in the system through a standardized sysfs control interface.  It
abstracts the management of various scrubbing functionalities into a unified
set of functions.

Use cases of common scrub control feature
-----------------------------------------

1. Several types of interfaces for hardware memory scrubbers have been
   identified, including the CXL memory device patrol scrub, CXL DDR5 ECS,
   ACPI RAS2 memory scrubbing features, ACPI NVDIMM ARS (Address Range Scrub),
   and software-based memory scrubbers.

   Of the identified interfaces to hardware memory scrubbers some support
   control over patrol (background) scrubbing (e.g., ACPI RAS2, CXL) and/or
   on-demand scrubbing (e.g., ACPI RAS2, ACPI ARS). However, the scrub control
   interfaces vary between memory scrubbers, highlighting the need for
   a standardized, generic sysfs scrub control interface that is accessible to
   userspace for administration and use by scripts/tools.

2. User-space scrub controls allow users to disable scrubbing if necessary,
   for example, to disable background patrol scrubbing or adjust the scrub
   rate for performance-aware operations where background activities need to
   be minimized or disabled.

3. User-space tools enable on-demand scrubbing for specific address ranges,
   provided that the scrubber supports this functionality.

4. User-space tools can also control memory DIMM scrubbing at a configurable
   scrub rate via sysfs scrub controls. This approach offers several benefits:

   4.1. Detects uncorrectable memory errors early, before user access to affected
        memory, helping facilitate recovery.

   4.2. Reduces the likelihood of correctable errors developing into uncorrectable
        errors.

5. Policy control for hotplugged memory is necessary because there may not
   be a system-wide BIOS or similar control to manage scrub settings for a CXL
   device added after boot. Determining these settings is a policy decision,
   balancing reliability against performance, so userspace should control it.
   Therefore, a unified interface is recommended for handling this function in
   a way that aligns with other similar interfaces, rather than creating a
   separate one.

Scrubbing features
------------------

CXL Memory Scrubbing features
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

CXL spec r3.1 [1]_ section 8.2.9.9.11.1 describes the memory device patrol
scrub control feature. The device patrol scrub proactively locates and makes
corrections to errors in regular cycle. The patrol scrub control allows the
userspace request to change CXL patrol scrubber's configurations.

The patrol scrub control allows the requester to specify the number of
hours in which the patrol scrub cycles must be completed, provided that
the requested scrub rate must be within the supported range of the
scrub rate that the device is capable of. In the CXL driver, the
number of seconds per scrub cycles, which user requests via sysfs, is
rescaled to hours per scrub cycles.

In addition, they allow the host to disable the feature in case it interferes
with performance-aware operations which require the background operations to
be turned off.

Error Check Scrub (ECS)
~~~~~~~~~~~~~~~~~~~~~~~

CXL spec r3.1 [1]_ section 8.2.9.9.11.2 describes Error Check Scrub (ECS)
- a feature defined in the JEDEC DDR5 SDRAM Specification (JESD79-5) and
allowing DRAM to internally read, correct single-bit errors, and write back
corrected data bits to the DRAM array while providing transparency to error
counts.

The DDR5 device contains number of memory media Field Replaceable Units (FRU)
per device. The DDR5 ECS feature and thus the ECS control driver supports
configuring the ECS parameters per FRU.

ACPI RAS2 Hardware-based Memory Scrubbing
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

ACPI spec 6.5 [2]_ section 5.2.21 ACPI RAS2 describes an ACPI RAS2 table
which provides interfaces for platform RAS features and supports independent
RAS controls and capabilities for a given RAS feature for multiple instances
of the same component in a given system.

Memory RAS features apply to RAS capabilities, controls and operations that
are specific to memory. RAS2 PCC sub-spaces for memory-specific RAS features
have a Feature Type of 0x00 (Memory).

The platform can use the hardware-based memory scrubbing feature to expose
controls and capabilities associated with hardware-based memory scrub
engines. The RAS2 memory scrubbing feature supports as per spec,

1. Independent memory scrubbing controls for each NUMA domain, identified
   using its proximity domain.

2. Provision for background (patrol) scrubbing of the entire memory system,
   as well as on-demand scrubbing for a specific region of memory.

ACPI Address Range Scrubbing (ARS)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

ACPI spec 6.5 [2]_ section 9.19.7.2 describes Address Range Scrubbing (ARS).
ARS allows the platform to communicate memory errors to system software.
This capability allows system software to prevent accesses to addresses with
uncorrectable errors in memory. ARS functions manage all NVDIMMs present in
the system. Only one scrub can be in progress system wide at any given time.

The following functions are supported as per the specification:

1. Query ARS Capabilities for a given address range, indicates platform
   supports the ACPI NVDIMM Root Device Unconsumed Error Notification.

2. Start ARS triggers an Address Range Scrub for the given memory range.
   Address scrubbing can be done for volatile or persistent memory, or both.

3. Query ARS Status command allows software to get the status of ARS,
   including the progress of ARS and ARS error record.

4. Clear Uncorrectable Error.

5. Translate SPA

6. ARS Error Inject etc.

The kernel supports an existing control for ARS and ARS is currently not
supported in EDAC.

.. [1] https://computeexpresslink.org/cxl-specification/

.. [2] https://uefi.org/specs/ACPI/6.5/

Comparison of various scrubbing features
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

 +--------------+-----------+-----------+-----------+-----------+
 |              |   ACPI    | CXL patrol|  CXL ECS  |  ARS      |
 |  Name        |   RAS2    | scrub     |           |           |
 +--------------+-----------+-----------+-----------+-----------+
 |              |           |           |           |           |
 | On-demand    | Supported | No        | No        | Supported |
 | Scrubbing    |           |           |           |           |
 |              |           |           |           |           |
 +--------------+-----------+-----------+-----------+-----------+
 |              |           |           |           |           |
 | Background   | Supported | Supported | Supported | No        |
 | scrubbing    |           |           |           |           |
 |              |           |           |           |           |
 +--------------+-----------+-----------+-----------+-----------+
 |              |           |           |           |           |
 | Mode of      | Scrub ctrl| per device| per memory|  Unknown  |
 | scrubbing    | per NUMA  |           | media     |           |
 |              | domain.   |           |           |           |
 +--------------+-----------+-----------+-----------+-----------+
 |              |           |           |           |           |
 | Query scrub  | Supported | Supported | Supported | Supported |
 | capabilities |           |           |           |           |
 |              |           |           |           |           |
 +--------------+-----------+-----------+-----------+-----------+
 |              |           |           |           |           |
 | Setting      | Supported | No        | No        | Supported |
 | address range|           |           |           |           |
 |              |           |           |           |           |
 +--------------+-----------+-----------+-----------+-----------+
 |              |           |           |           |           |
 | Setting      | Supported | Supported | No        | No        |
 | scrub rate   |           |           |           |           |
 |              |           |           |           |           |
 +--------------+-----------+-----------+-----------+-----------+
 |              |           |           |           |           |
 | Unit for     | Not       | in hours  | No        | No        |
 | scrub rate   | Defined   |           |           |           |
 |              |           |           |           |           |
 +--------------+-----------+-----------+-----------+-----------+
 |              | Supported |           |           |           |
 | Scrub        | on-demand | No        | No        | Supported |
 | status/      | scrubbing |           |           |           |
 | Completion   | only      |           |           |           |
 +--------------+-----------+-----------+-----------+-----------+
 | UC error     |           |CXL general|CXL general| ACPI UCE  |
 | reporting    | Exception |media/DRAM |media/DRAM | notify and|
 |              |           |event/media|event/media| query     |
 |              |           |scan?      |scan?      | ARS status|
 +--------------+-----------+-----------+-----------+-----------+
 |              |           |           |           |           |
 | Support for  | Supported | Supported | Supported | No        |
 | EDAC control |           |           |           |           |
 |              |           |           |           |           |
 +--------------+-----------+-----------+-----------+-----------+

The File System
---------------

The control attributes of a registered scrubber instance could be
accessed in:

/sys/bus/edac/devices/<dev-name>/scrubX/

sysfs
-----

Sysfs files are documented in
`Documentation/ABI/testing/sysfs-edac-scrub`

`Documentation/ABI/testing/sysfs-edac-ecs`

Examples
--------

The usage takes the form shown in these examples:

1. CXL memory Patrol Scrub

The following are the use cases identified why we might increase the scrub rate.

- Scrubbing is needed at device granularity because a device is showing
  unexpectedly high errors.

- Scrubbing may apply to memory that isn't online at all yet. Likely this
  is a system wide default setting on boot.

- Scrubbing at a higher rate because the monitor software has determined that
  more reliability is necessary for a particular data set. This is called
  Differentiated Reliability.

1.1. Device based scrubbing

CXL memory is exposed to memory management subsystem and ultimately userspace
via CXL devices. Device-based scrubbing is used for the first use case
described in "Section 1 CXL Memory Patrol Scrub".

When combining control via the device interfaces and region interfaces,
"see Section 1.2 Region based scrubbing".

Sysfs files for scrubbing are documented in
`Documentation/ABI/testing/sysfs-edac-scrub`

1.2. Region based scrubbing

CXL memory is exposed to memory management subsystem and ultimately userspace
via CXL regions. CXL Regions represent mapped memory capacity in system
physical address space. These can incorporate one or more parts of multiple CXL
memory devices with traffic interleaved across them. The user may want to control
the scrub rate via this more abstract region instead of having to figure out the
constituent devices and program them separately. The scrub rate for each device
covers the whole device. Thus if multiple regions use parts of that device then
requests for scrubbing of other regions may result in a higher scrub rate than
requested for this specific region.

Region-based scrubbing is used for the third use case described in
"Section 1 CXL Memory Patrol Scrub".

Userspace must follow below set of rules on how to set the scrub rates for any
mixture of requirements.

1. Taking each region in turn from lowest desired scrub rate to highest and set
   their scrub rates. Later regions may override the scrub rate on individual
   devices (and hence potentially whole regions).

2. Take each device for which enhanced scrubbing is required (higher rate) and
   set those scrub rates. This will override the scrub rates of individual devices,
   setting them to the maximum rate required for any of the regions they help back,
   unless a specific rate is already defined.

Sysfs files for scrubbing are documented in
`Documentation/ABI/testing/sysfs-edac-scrub`

2. CXL memory Error Check Scrub (ECS)

The Error Check Scrub (ECS) feature enables a memory device to perform error
checking and correction (ECC) and count single-bit errors. The associated
memory controller sets the ECS mode with a trigger sent to the memory
device. CXL ECS control allows the host, thus the userspace, to change the
attributes for error count mode, threshold number of errors per segment
(indicating how many segments have at least that number of errors) for
reporting errors, and reset the ECS counter. Thus the responsibility for
initiating Error Check Scrub on a memory device may lie with the memory
controller or platform when unexpectedly high error rates are detected.

Sysfs files for scrubbing are documented in
`Documentation/ABI/testing/sysfs-edac-ecs`

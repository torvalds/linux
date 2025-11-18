.. SPDX-License-Identifier: GPL-2.0

=====================
Devices and Protocols
=====================

The type of CXL device (Memory, Accelerator, etc) dictates many configuration steps. This section
covers some basic background on device types and on-device resources used by the platform and OS
which impact configuration.

Protocols
=========

There are three core protocols to CXL.  For the purpose of this documentation,
we will only discuss very high level definitions as the specific hardware
details are largely abstracted away from Linux.  See the CXL specification
for more details.

CXL.io
------
The basic interaction protocol, similar to PCIe configuration mechanisms.
Typically used for initialization, configuration, and I/O access for anything
other than memory (CXL.mem) or cache (CXL.cache) operations.

The Linux CXL driver exposes access to .io functionality via the various sysfs
interfaces and /dev/cxl/ devices (which exposes direct access to device
mailboxes).

CXL.cache
---------
The mechanism by which a device may coherently access and cache host memory.

Largely transparent to Linux once configured.

CXL.mem
---------
The mechanism by which the CPU may coherently access and cache device memory.

Largely transparent to Linux once configured.


Device Types
============

Type-1
------

A Type-1 CXL device:

* Supports cxl.io and cxl.cache protocols
* Implements a fully coherent cache
* Allows Device-to-Host coherence and Host-to-Device snoops.
* Does NOT have host-managed device memory (HDM)

Typical examples of type-1 devices is a Smart NIC - which may want to
directly operate on host-memory (DMA) to store incoming packets. These
devices largely rely on CPU-attached memory.

Type-2
------

A Type-2 CXL Device:

* Supports cxl.io, cxl.cache, and cxl.mem protocols
* Optionally implements coherent cache and Host-Managed Device Memory
* Is typically an accelerator device with high bandwidth memory.

The primary difference between a type-1 and type-2 device is the presence
of host-managed device memory, which allows the device to operate on a
local memory bank - while the CPU still has coherent DMA to the same memory.

This allows things like GPUs to expose their memory via DAX devices or file
descriptors, allows drivers and programs direct access to device memory
rather than use block-transfer semantics.

Type-3
------

A Type-3 CXL Device

* Supports cxl.io and cxl.mem
* Implements Host-Managed Device Memory
* May provide either Volatile or Persistent memory capacity (or both).

A basic example of a type-3 device is a simple memory expander, whose
local memory capacity is exposed to the CPU for access directly via
basic coherent DMA.

Switch
------

A CXL switch is a device capable of routing any CXL (and by extension, PCIe)
protocol between an upstream, downstream, or peer devices.  Many devices, such
as Multi-Logical Devices, imply the presence of switching in some manner.

Logical Devices and Heads
-------------------------

A CXL device may present one or more "Logical Devices" to one or more hosts
(via physical "Heads").

A Single-Logical Device (SLD) is a device which presents a single device to
one or more heads.

A Multi-Logical Device (MLD) is a device which may present multiple devices
to one or more upstream devices.

A Single-Headed Device exposes only a single physical connection.

A Multi-Headed Device exposes multiple physical connections.

MHSLD
~~~~~
A Multi-Headed Single-Logical Device (MHSLD) exposes a single logical
device to multiple heads which may be connected to one or more discrete
hosts.  An example of this would be a simple memory-pool which may be
statically configured (prior to boot) to expose portions of its memory
to Linux via :doc:`CEDT <../platform/acpi/cedt>`.

MHMLD
~~~~~
A Multi-Headed Multi-Logical Device (MHMLD) exposes multiple logical
devices to multiple heads which may be connected to one or more discrete
hosts.  An example of this would be a Dynamic Capacity Device or which
may be configured at runtime to expose portions of its memory to Linux.

Example Devices
===============

Memory Expander
---------------
The simplest form of Type-3 device is a memory expander.  A memory expander
exposes Host-Managed Device Memory (HDM) to Linux.  This memory may be
Volatile or Non-Volatile (Persistent).

Memory Expanders will typically be considered a form of Single-Headed,
Single-Logical Device - as its form factor will typically be an add-in-card
(AIC) or some other similar form-factor.

The Linux CXL driver provides support for static or dynamic configuration of
basic memory expanders.  The platform may program decoders prior to OS init
(e.g. auto-decoders), or the user may program the fabric if the platform
defers these operations to the OS.

Multiple Memory Expanders may be added to an external chassis and exposed to
a host via a head attached to a CXL switch.  This is a "memory pool", and
would be considered an MHSLD or MHMLD depending on the management capabilities
provided by the switch platform.

As of v6.14, Linux does not provide a formalized interface to manage non-DCD
MHSLD or MHMLD devices.

Dynamic Capacity Device (DCD)
-----------------------------

A Dynamic Capacity Device is a Type-3 device which provides dynamic management
of memory capacity. The basic premise of a DCD to provide an allocator-like
interface for physical memory capacity to a "Fabric Manager" (an external,
privileged host with privileges to change configurations for other hosts).

A DCD manages "Memory Extents", which may be volatile or persistent. Extents
may also be exclusive to a single host or shared across multiple hosts.

As of v6.14, Linux does not provide a formalized interface to manage DCD
devices, however there is active work on LKML targeting future release.

.. SPDX-License-Identifier: GPL-2.0

==================
CXL Device Hotplug
==================

Device hotplug refers to *physical* hotplug of a device (addition or removal
of a physical device from the machine).

BIOS/EFI software is expected to configure sufficient resources **at boot
time** to allow hotplugged devices to be configured by software (such as
proximity domains, HPA regions, and host-bridge configurations).

BIOS/EFI is not expected (**nor suggested**) to configure hotplugged
devices at hotplug time (i.e. HDM decoders should be left unprogrammed).

This document covers some examples of those resources, but should not
be considered exhaustive.

Hot-Remove
==========
Hot removal of a device typically requires careful removal of software
constructs (memory regions, associated drivers) which manage these devices.

Hard-removing a CXL.mem device without carefully tearing down driver stacks
is likely to cause the system to machine-check (or at least SIGBUS if memory
access is limited to user space).

Memory Device Hot-Add
=====================
A device present at boot may be associated with a CXL Fixed Memory Window
reported in :doc:`CEDT<acpi/cedt>`.  That CFMWS may match the size of the
device, but the construction of the CEDT CFMWS is platform-defined.

Hot-adding a memory device requires this pre-defined, **static** CFMWS to
have sufficient HPA space to describe that device.

There are a few common scenarios to consider.

Single-Endpoint Memory Device Present at Boot
---------------------------------------------
A device present at boot likely had its capacity reported in the
:doc:`CEDT<acpi/cedt>`.  If a device is removed and a new device hotplugged,
the capacity of the new device will be limited to the original CFMWS capacity.

Adding capacity larger than the original device will cause memory region
creation to fail if the region size is greater than the CFMWS size.

The CFMWS is **static** and cannot be adjusted.  Platforms which may expect
different sized devices to be hotplugged must allocate sufficient CFMWS space
**at boot time** to cover all future expected devices.

Multi-Endpoint Memory Device Present at Boot
--------------------------------------------
Non-switch-based Multi-Endpoint devices are outside the scope of what the
CXL specification describes, but they are technically possible. We describe
them here for instructive reasons only - this does not imply Linux support.

A hot-plug capable CXL memory device, such as one which presents multiple
expanders as a single large-capacity device, should report the **maximum
possible capacity** for the device at boot. ::

                  HB0
                  RP0
                   |
     [Multi-Endpoint Memory Device]
              _____|_____
             |          |
        [Endpoint0]   [Empty]


Limiting the size to the capacity preset at boot will limit hot-add support
to replacing capacity that was present at boot.

No CXL Device Present at Boot
-----------------------------
When no CXL memory device is present on boot, some platforms omit the CFMWS
in the :doc:`CEDT<acpi/cedt>`.  When this occurs, hot-add is not possible.

This describes the base case for any given device not being present at boot.
If a future possible device is not described in the CEDT at boot, hot-add
of that device is either limited or not possible.

For a platform to support hot-add of a full memory device, it must allocate
a CEDT CFMWS region with sufficient memory capacity to cover all future
potentially added capacity (along with any relevant CEDT CHBS entry).

To support memory hotplug directly on the host bridge/root port, or on a switch
downstream of the host bridge, a platform must construct a CEDT CFMWS at boot
with sufficient resources to support the max possible (or expected) hotplug
memory capacity. ::

         HB0                 HB1
      RP0    RP1             RP2
       |      |               |
     Empty  Empty            USP
                      ________|________
                      |    |    |     |
                     DSP  DSP  DSP   DSP
                      |    |    |    |
                         All  Empty

For example, a BIOS/EFI may expose an option to configure a CEDT CFMWS with
a pre-configured amount of memory capacity (per host bridge, or host bridge
interleave set), even if no device is attached to Root Ports or Downstream
Ports at boot (as depicted in the figure above).


Interleave Sets
===============

Host Bridge Interleave
----------------------
Host-bridge interleaved memory regions are defined **statically** in the
:doc:`CEDT<acpi/cedt>`.  To apply cross-host-bridge interleave, a CFMWS entry
describing that interleave must have been provided **at boot**.  Hotplugged
devices cannot add host-bridge interleave capabilities at hotplug time.

See the :doc:`Flexible CEDT Configuration<example-configurations/flexible>`
example to see how a platform can provide this kind of flexibility regarding
hotplugged memory devices.  BIOS/EFI software should consider options to
present flexible CEDT configurations with hotplug support.

HDM Interleave
--------------
Decoder-applied interleave can flexibly handle hotplugged devices, as decoders
can be re-programmed after hotplug.

To add or remove a device to/from an existing HDM-applied interleaved region,
that region must be torn down an re-created.

.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

=========================================
Why using ACPI drivers is not a good idea
=========================================

:Copyright: |copy| 2026, Intel Corporation

:Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>

Even though binding drivers directly to struct acpi_device objects, also
referred to as "ACPI device nodes", allows basic functionality to be provided
at least in some cases, there are problems with it, related to general
consistency, sysfs layout, power management operation ordering, and code
cleanliness.

First of all, ACPI device nodes represent firmware entities rather than
hardware and in many cases they provide auxiliary information on devices
enumerated independently (like PCI devices or CPUs).  It is therefore generally
questionable to assign resources to them because the entities represented by
them do not decode addresses in the memory or I/O address spaces and do not
generate interrupts or similar (all of that is done by hardware).

Second, as a general rule, a struct acpi_device can only be a parent of another
struct acpi_device.  If that is not the case, the location of the child device
in the device hierarchy is at least confusing and it may not be straightforward
to identify the piece of hardware providing functionality represented by it.
However, binding a driver directly to an ACPI device node may cause that to
happen if the given driver registers input devices or wakeup sources under it,
for example.

Next, using system suspend and resume callbacks directly on ACPI device nodes
is also questionable because it may cause ordering problems to appear.  Namely,
ACPI device nodes are registered before enumerating hardware corresponding to
them and they land on the PM list in front of the majority of other device
objects.  Consequently, the execution ordering of their PM callbacks may be
different from what is generally expected.  Also, in general, dependencies
returned by _DEP objects do not affect ACPI device nodes themselves, but the
"physical" devices associated with them, which potentially is one more source
of inconsistency related to treating ACPI device nodes as "real" device
representation.

All of the above means that binding drivers to ACPI device nodes should
generally be avoided and so struct acpi_driver objects should not be used.

Moreover, a device ID is necessary to bind a driver directly to an ACPI device
node, but device IDs are not generally associated with all of them.  Some of
them contain alternative information allowing the corresponding pieces of
hardware to be identified, for example represeted by an _ADR object return
value, and device IDs are not used in those cases.  In consequence, confusingly
enough, binding an ACPI driver to an ACPI device node may even be impossible.

When that happens, the piece of hardware corresponding to the given ACPI device
node is represented by another device object, like a struct pci_dev, and the
ACPI device node is the "ACPI companion" of that device, accessible through its
fwnode pointer used by the ACPI_COMPANION() macro.  The ACPI companion holds
additional information on the device configuration and possibly some "recipes"
on device manipulation in the form of AML (ACPI Machine Language) bytecode
provided by the platform firmware.  Thus the role of the ACPI device node is
similar to the role of a struct device_node on a system where Device Tree is
used for platform description.

For consistency, this approach has been extended to the cases in which ACPI
device IDs are used.  Namely, in those cases, an additional device object is
created to represent the piece of hardware corresponding to a given ACPI device
node.  By default, it is a platform device, but it may also be a PNP device, a
CPU device, or another type of device, depending on what the given piece of
hardware actually is.  There are even cases in which multiple devices are
"backed" or "accompanied" by one ACPI device node (e.g. ACPI device nodes
corresponding to GPUs that may provide firmware interfaces for backlight
brightness control in addition to GPU configuration information).

This means that it really should never be necessary to bind a driver directly to
an ACPI device node because there is a "proper" device object representing the
corresponding piece of hardware that can be bound to by a "proper" driver using
the given ACPI device node as the device's ACPI companion.  Thus, in principle,
there is no reason to use ACPI drivers and if they all were replaced with other
driver types (for example, platform drivers), some code could be dropped and
some complexity would go away.

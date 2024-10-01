.. SPDX-License-Identifier: GPL-2.0
.. _cpuhp_index:

====================
CPU Hotplug and ACPI
====================

CPU hotplug in the arm64 world is commonly used to describe the kernel taking
CPUs online/offline using PSCI. This document is about ACPI firmware allowing
CPUs that were not available during boot to be added to the system later.

``possible`` and ``present`` refer to the state of the CPU as seen by linux.


CPU Hotplug on physical systems - CPUs not present at boot
----------------------------------------------------------

Physical systems need to mark a CPU that is ``possible`` but not ``present`` as
being ``present``. An example would be a dual socket machine, where the package
in one of the sockets can be replaced while the system is running.

This is not supported.

In the arm64 world CPUs are not a single device but a slice of the system.
There are no systems that support the physical addition (or removal) of CPUs
while the system is running, and ACPI is not able to sufficiently describe
them.

e.g. New CPUs come with new caches, but the platform's cache topology is
described in a static table, the PPTT. How caches are shared between CPUs is
not discoverable, and must be described by firmware.

e.g. The GIC redistributor for each CPU must be accessed by the driver during
boot to discover the system wide supported features. ACPI's MADT GICC
structures can describe a redistributor associated with a disabled CPU, but
can't describe whether the redistributor is accessible, only that it is not
'always on'.

arm64's ACPI tables assume that everything described is ``present``.


CPU Hotplug on virtual systems - CPUs not enabled at boot
---------------------------------------------------------

Virtual systems have the advantage that all the properties the system will
ever have can be described at boot. There are no power-domain considerations
as such devices are emulated.

CPU Hotplug on virtual systems is supported. It is distinct from physical
CPU Hotplug as all resources are described as ``present``, but CPUs may be
marked as disabled by firmware. Only the CPU's online/offline behaviour is
influenced by firmware. An example is where a virtual machine boots with a
single CPU, and additional CPUs are added once a cloud orchestrator deploys
the workload.

For a virtual machine, the VMM (e.g. Qemu) plays the part of firmware.

Virtual hotplug is implemented as a firmware policy affecting which CPUs can be
brought online. Firmware can enforce its policy via PSCI's return codes. e.g.
``DENIED``.

The ACPI tables must describe all the resources of the virtual machine. CPUs
that firmware wishes to disable either from boot (or later) should not be
``enabled`` in the MADT GICC structures, but should have the ``online capable``
bit set, to indicate they can be enabled later. The boot CPU must be marked as
``enabled``.  The 'always on' GICR structure must be used to describe the
redistributors.

CPUs described as ``online capable`` but not ``enabled`` can be set to enabled
by the DSDT's Processor object's _STA method. On virtual systems the _STA method
must always report the CPU as ``present``. Changes to the firmware policy can
be notified to the OS via device-check or eject-request.

CPUs described as ``enabled`` in the static table, should not have their _STA
modified dynamically by firmware. Soft-restart features such as kexec will
re-read the static properties of the system from these static tables, and
may malfunction if these no longer describe the running system. Linux will
re-discover the dynamic properties of the system from the _STA method later
during boot.

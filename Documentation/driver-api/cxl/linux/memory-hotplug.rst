.. SPDX-License-Identifier: GPL-2.0

==============
Memory Hotplug
==============
The final phase of surfacing CXL memory to the kernel page allocator is for
the `DAX` driver to surface a `Driver Managed` memory region via the
memory-hotplug component.

There are four major configurations to consider:

1) Default Online Behavior (on/off and zone)
2) Hotplug Memory Block size
3) Memory Map Resource location
4) Driver-Managed Memory Designation

Default Online Behavior
=======================
The default-online behavior of hotplug memory is dictated by the following,
in order of precedence:

- :code:`CONFIG_MHP_DEFAULT_ONLINE_TYPE` Build Configuration
- :code:`memhp_default_state` Boot parameter
- :code:`/sys/devices/system/memory/auto_online_blocks` value

These dictate whether hotplugged memory blocks arrive in one of three states:

1) Offline
2) Online in :code:`ZONE_NORMAL`
3) Online in :code:`ZONE_MOVABLE`

:code:`ZONE_NORMAL` implies this capacity may be used for almost any allocation,
while :code:`ZONE_MOVABLE` implies this capacity should only be used for
migratable allocations.

:code:`ZONE_MOVABLE` attempts to retain the hotplug-ability of a memory block
so that it the entire region may be hot-unplugged at a later time.  Any capacity
onlined into :code:`ZONE_NORMAL` should be considered permanently attached to
the page allocator.

Hotplug Memory Block Size
=========================
By default, on most architectures, the Hotplug Memory Block Size is either
128MB or 256MB.  On x86, the block size increases up to 2GB as total memory
capacity exceeds 64GB.  As of v6.15, Linux does not take into account the
size and alignment of the ACPI CEDT CFMWS regions (see Early Boot docs) when
deciding the Hotplug Memory Block Size.

Memory Map
==========
The location of :code:`struct folio` allocations to represent the hotplugged
memory capacity are dictated by the following system settings:

- :code:`/sys_module/memory_hotplug/parameters/memmap_on_memory`
- :code:`/sys/bus/dax/devices/daxN.Y/memmap_on_memory`

If both of these parameters are set to true, :code:`struct folio` for this
capacity will be carved out of the memory block being onlined.  This has
performance implications if the memory is particularly high-latency and
its :code:`struct folio` becomes hotly contended.

If either parameter is set to false, :code:`struct folio` for this capacity
will be allocated from the local node of the processor running the hotplug
procedure.  This capacity will be allocated from :code:`ZONE_NORMAL` on
that node, as it is a :code:`GFP_KERNEL` allocation.

Systems with extremely large amounts of :code:`ZONE_MOVABLE` memory (e.g.
CXL memory pools) must ensure that there is sufficient local
:code:`ZONE_NORMAL` capacity to host the memory map for the hotplugged capacity.

Driver Managed Memory
=====================
The DAX driver surfaces this memory to memory-hotplug as "Driver Managed". This
is not a configurable setting, but it's important to note that driver managed
memory is explicitly excluded from use during kexec.  This is required to ensure
any reset or out-of-band operations that the CXL device may be subject to during
a functional system-reboot (such as a reset-on-probe) will not cause portions of
the kexec kernel to be overwritten.

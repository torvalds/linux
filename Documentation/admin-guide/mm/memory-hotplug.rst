.. _admin_guide_memory_hotplug:

==============
Memory Hotplug
==============

:Created:							Jul 28 2007
:Updated: Add some details about locking internals:		Aug 20 2018

This document is about memory hotplug including how-to-use and current status.
Because Memory Hotplug is still under development, contents of this text will
be changed often.

.. contents:: :local:

.. note::

    (1) x86_64's has special implementation for memory hotplug.
        This text does not describe it.
    (2) This text assumes that sysfs is mounted at ``/sys``.


Introduction
============

Purpose of memory hotplug
-------------------------

Memory Hotplug allows users to increase/decrease the amount of memory.
Generally, there are two purposes.

(A) For changing the amount of memory.
    This is to allow a feature like capacity on demand.
(B) For installing/removing DIMMs or NUMA-nodes physically.
    This is to exchange DIMMs/NUMA-nodes, reduce power consumption, etc.

(A) is required by highly virtualized environments and (B) is required by
hardware which supports memory power management.

Linux memory hotplug is designed for both purpose.

Phases of memory hotplug
------------------------

There are 2 phases in Memory Hotplug:

  1) Physical Memory Hotplug phase
  2) Logical Memory Hotplug phase.

The First phase is to communicate hardware/firmware and make/erase
environment for hotplugged memory. Basically, this phase is necessary
for the purpose (B), but this is good phase for communication between
highly virtualized environments too.

When memory is hotplugged, the kernel recognizes new memory, makes new memory
management tables, and makes sysfs files for new memory's operation.

If firmware supports notification of connection of new memory to OS,
this phase is triggered automatically. ACPI can notify this event. If not,
"probe" operation by system administration is used instead.
(see :ref:`memory_hotplug_physical_mem`).

Logical Memory Hotplug phase is to change memory state into
available/unavailable for users. Amount of memory from user's view is
changed by this phase. The kernel makes all memory in it as free pages
when a memory range is available.

In this document, this phase is described as online/offline.

Logical Memory Hotplug phase is triggered by write of sysfs file by system
administrator. For the hot-add case, it must be executed after Physical Hotplug
phase by hand.
(However, if you writes udev's hotplug scripts for memory hotplug, these
phases can be execute in seamless way.)

Unit of Memory online/offline operation
---------------------------------------

Memory hotplug uses SPARSEMEM memory model which allows memory to be divided
into chunks of the same size. These chunks are called "sections". The size of
a memory section is architecture dependent. For example, power uses 16MiB, ia64
uses 1GiB.

Memory sections are combined into chunks referred to as "memory blocks". The
size of a memory block is architecture dependent and represents the logical
unit upon which memory online/offline operations are to be performed. The
default size of a memory block is the same as memory section size unless an
architecture specifies otherwise. (see :ref:`memory_hotplug_sysfs_files`.)

To determine the size (in bytes) of a memory block please read this file::

  /sys/devices/system/memory/block_size_bytes

Kernel Configuration
====================

To use memory hotplug feature, kernel must be compiled with following
config options.

- For all memory hotplug:
    - Memory model -> Sparse Memory  (``CONFIG_SPARSEMEM``)
    - Allow for memory hot-add       (``CONFIG_MEMORY_HOTPLUG``)

- To enable memory removal, the following are also necessary:
    - Allow for memory hot remove    (``CONFIG_MEMORY_HOTREMOVE``)
    - Page Migration                 (``CONFIG_MIGRATION``)

- For ACPI memory hotplug, the following are also necessary:
    - Memory hotplug (under ACPI Support menu) (``CONFIG_ACPI_HOTPLUG_MEMORY``)
    - This option can be kernel module.

- As a related configuration, if your box has a feature of NUMA-node hotplug
  via ACPI, then this option is necessary too.

    - ACPI0004,PNP0A05 and PNP0A06 Container Driver (under ACPI Support menu)
      (``CONFIG_ACPI_CONTAINER``).

     This option can be kernel module too.


.. _memory_hotplug_sysfs_files:

sysfs files for memory hotplug
==============================

All memory blocks have their device information in sysfs.  Each memory block
is described under ``/sys/devices/system/memory`` as::

	/sys/devices/system/memory/memoryXXX

where XXX is the memory block id.

For the memory block covered by the sysfs directory.  It is expected that all
memory sections in this range are present and no memory holes exist in the
range. Currently there is no way to determine if there is a memory hole, but
the existence of one should not affect the hotplug capabilities of the memory
block.

For example, assume 1GiB memory block size. A device for a memory starting at
0x100000000 is ``/sys/device/system/memory/memory4``::

	(0x100000000 / 1Gib = 4)

This device covers address range [0x100000000 ... 0x140000000)

Under each memory block, you can see 5 files:

- ``/sys/devices/system/memory/memoryXXX/phys_index``
- ``/sys/devices/system/memory/memoryXXX/phys_device``
- ``/sys/devices/system/memory/memoryXXX/state``
- ``/sys/devices/system/memory/memoryXXX/removable``
- ``/sys/devices/system/memory/memoryXXX/valid_zones``

=================== ============================================================
``phys_index``      read-only and contains memory block id, same as XXX.
``state``           read-write

                    - at read:  contains online/offline state of memory.
                    - at write: user can specify "online_kernel",

                    "online_movable", "online", "offline" command
                    which will be performed on all sections in the block.
``phys_device``     read-only: designed to show the name of physical memory
                    device.  This is not well implemented now.
``removable``       read-only: contains an integer value indicating
                    whether the memory block is removable or not
                    removable.  A value of 1 indicates that the memory
                    block is removable and a value of 0 indicates that
                    it is not removable. A memory block is removable only if
                    every section in the block is removable.
``valid_zones``     read-only: designed to show which zones this memory block
		    can be onlined to.

		    The first column shows it`s default zone.

		    "memory6/valid_zones: Normal Movable" shows this memoryblock
		    can be onlined to ZONE_NORMAL by default and to ZONE_MOVABLE
		    by online_movable.

		    "memory7/valid_zones: Movable Normal" shows this memoryblock
		    can be onlined to ZONE_MOVABLE by default and to ZONE_NORMAL
		    by online_kernel.
=================== ============================================================

.. note::

  These directories/files appear after physical memory hotplug phase.

If CONFIG_NUMA is enabled the memoryXXX/ directories can also be accessed
via symbolic links located in the ``/sys/devices/system/node/node*`` directories.

For example::

	/sys/devices/system/node/node0/memory9 -> ../../memory/memory9

A backlink will also be created::

	/sys/devices/system/memory/memory9/node0 -> ../../node/node0

.. _memory_hotplug_physical_mem:

Physical memory hot-add phase
=============================

Hardware(Firmware) Support
--------------------------

On x86_64/ia64 platform, memory hotplug by ACPI is supported.

In general, the firmware (ACPI) which supports memory hotplug defines
memory class object of _HID "PNP0C80". When a notify is asserted to PNP0C80,
Linux's ACPI handler does hot-add memory to the system and calls a hotplug udev
script. This will be done automatically.

But scripts for memory hotplug are not contained in generic udev package(now).
You may have to write it by yourself or online/offline memory by hand.
Please see :ref:`memory_hotplug_how_to_online_memory` and
:ref:`memory_hotplug_how_to_offline_memory`.

If firmware supports NUMA-node hotplug, and defines an object _HID "ACPI0004",
"PNP0A05", or "PNP0A06", notification is asserted to it, and ACPI handler
calls hotplug code for all of objects which are defined in it.
If memory device is found, memory hotplug code will be called.

Notify memory hot-add event by hand
-----------------------------------

On some architectures, the firmware may not notify the kernel of a memory
hotplug event.  Therefore, the memory "probe" interface is supported to
explicitly notify the kernel.  This interface depends on
CONFIG_ARCH_MEMORY_PROBE and can be configured on powerpc, sh, and x86
if hotplug is supported, although for x86 this should be handled by ACPI
notification.

Probe interface is located at::

	/sys/devices/system/memory/probe

You can tell the physical address of new memory to the kernel by::

	% echo start_address_of_new_memory > /sys/devices/system/memory/probe

Then, [start_address_of_new_memory, start_address_of_new_memory +
memory_block_size] memory range is hot-added. In this case, hotplug script is
not called (in current implementation). You'll have to online memory by
yourself.  Please see :ref:`memory_hotplug_how_to_online_memory`.

Logical Memory hot-add phase
============================

State of memory
---------------

To see (online/offline) state of a memory block, read 'state' file::

	% cat /sys/device/system/memory/memoryXXX/state


- If the memory block is online, you'll read "online".
- If the memory block is offline, you'll read "offline".


.. _memory_hotplug_how_to_online_memory:

How to online memory
--------------------

When the memory is hot-added, the kernel decides whether or not to "online"
it according to the policy which can be read from "auto_online_blocks" file::

	% cat /sys/devices/system/memory/auto_online_blocks

The default depends on the CONFIG_MEMORY_HOTPLUG_DEFAULT_ONLINE kernel config
option. If it is disabled the default is "offline" which means the newly added
memory is not in a ready-to-use state and you have to "online" the newly added
memory blocks manually. Automatic onlining can be requested by writing "online"
to "auto_online_blocks" file::

	% echo online > /sys/devices/system/memory/auto_online_blocks

This sets a global policy and impacts all memory blocks that will subsequently
be hotplugged. Currently offline blocks keep their state. It is possible, under
certain circumstances, that some memory blocks will be added but will fail to
online. User space tools can check their "state" files
(``/sys/devices/system/memory/memoryXXX/state``) and try to online them manually.

If the automatic onlining wasn't requested, failed, or some memory block was
offlined it is possible to change the individual block's state by writing to the
"state" file::

	% echo online > /sys/devices/system/memory/memoryXXX/state

This onlining will not change the ZONE type of the target memory block,
If the memory block doesn't belong to any zone an appropriate kernel zone
(usually ZONE_NORMAL) will be used unless movable_node kernel command line
option is specified when ZONE_MOVABLE will be used.

You can explicitly request to associate it with ZONE_MOVABLE by::

	% echo online_movable > /sys/devices/system/memory/memoryXXX/state

.. note:: current limit: this memory block must be adjacent to ZONE_MOVABLE

Or you can explicitly request a kernel zone (usually ZONE_NORMAL) by::

	% echo online_kernel > /sys/devices/system/memory/memoryXXX/state

.. note:: current limit: this memory block must be adjacent to ZONE_NORMAL

An explicit zone onlining can fail (e.g. when the range is already within
and existing and incompatible zone already).

After this, memory block XXX's state will be 'online' and the amount of
available memory will be increased.

This may be changed in future.

Logical memory remove
=====================

Memory offline and ZONE_MOVABLE
-------------------------------

Memory offlining is more complicated than memory online. Because memory offline
has to make the whole memory block be unused, memory offline can fail if
the memory block includes memory which cannot be freed.

In general, memory offline can use 2 techniques.

(1) reclaim and free all memory in the memory block.
(2) migrate all pages in the memory block.

In the current implementation, Linux's memory offline uses method (2), freeing
all  pages in the memory block by page migration. But not all pages are
migratable. Under current Linux, migratable pages are anonymous pages and
page caches. For offlining a memory block by migration, the kernel has to
guarantee that the memory block contains only migratable pages.

Now, a boot option for making a memory block which consists of migratable pages
is supported. By specifying "kernelcore=" or "movablecore=" boot option, you can
create ZONE_MOVABLE...a zone which is just used for movable pages.
(See also Documentation/admin-guide/kernel-parameters.rst)

Assume the system has "TOTAL" amount of memory at boot time, this boot option
creates ZONE_MOVABLE as following.

1) When kernelcore=YYYY boot option is used,
   Size of memory not for movable pages (not for offline) is YYYY.
   Size of memory for movable pages (for offline) is TOTAL-YYYY.

2) When movablecore=ZZZZ boot option is used,
   Size of memory not for movable pages (not for offline) is TOTAL - ZZZZ.
   Size of memory for movable pages (for offline) is ZZZZ.

.. note::

   Unfortunately, there is no information to show which memory block belongs
   to ZONE_MOVABLE. This is TBD.

.. _memory_hotplug_how_to_offline_memory:

How to offline memory
---------------------

You can offline a memory block by using the same sysfs interface that was used
in memory onlining::

	% echo offline > /sys/devices/system/memory/memoryXXX/state

If offline succeeds, the state of the memory block is changed to be "offline".
If it fails, some error core (like -EBUSY) will be returned by the kernel.
Even if a memory block does not belong to ZONE_MOVABLE, you can try to offline
it.  If it doesn't contain 'unmovable' memory, you'll get success.

A memory block under ZONE_MOVABLE is considered to be able to be offlined
easily.  But under some busy state, it may return -EBUSY. Even if a memory
block cannot be offlined due to -EBUSY, you can retry offlining it and may be
able to offline it (or not). (For example, a page is referred to by some kernel
internal call and released soon.)

Consideration:
  Memory hotplug's design direction is to make the possibility of memory
  offlining higher and to guarantee unplugging memory under any situation. But
  it needs more work. Returning -EBUSY under some situation may be good because
  the user can decide to retry more or not by himself. Currently, memory
  offlining code does some amount of retry with 120 seconds timeout.

Physical memory remove
======================

Need more implementation yet....
 - Notification completion of remove works by OS to firmware.
 - Guard from remove if not yet.


Locking Internals
=================

When adding/removing memory that uses memory block devices (i.e. ordinary RAM),
the device_hotplug_lock should be held to:

- synchronize against online/offline requests (e.g. via sysfs). This way, memory
  block devices can only be accessed (.online/.state attributes) by user
  space once memory has been fully added. And when removing memory, we
  know nobody is in critical sections.
- synchronize against CPU hotplug and similar (e.g. relevant for ACPI and PPC)

Especially, there is a possible lock inversion that is avoided using
device_hotplug_lock when adding memory and user space tries to online that
memory faster than expected:

- device_online() will first take the device_lock(), followed by
  mem_hotplug_lock
- add_memory_resource() will first take the mem_hotplug_lock, followed by
  the device_lock() (while creating the devices, during bus_add_device()).

As the device is visible to user space before taking the device_lock(), this
can result in a lock inversion.

onlining/offlining of memory should be done via device_online()/
device_offline() - to make sure it is properly synchronized to actions
via sysfs. Holding device_hotplug_lock is advised (to e.g. protect online_type)

When adding/removing/onlining/offlining memory or adding/removing
heterogeneous/device memory, we should always hold the mem_hotplug_lock in
write mode to serialise memory hotplug (e.g. access to global/zone
variables).

In addition, mem_hotplug_lock (in contrast to device_hotplug_lock) in read
mode allows for a quite efficient get_online_mems/put_online_mems
implementation, so code accessing memory can protect from that memory
vanishing.


Future Work
===========

  - allowing memory hot-add to ZONE_MOVABLE. maybe we need some switch like
    sysctl or new control file.
  - showing memory block and physical device relationship.
  - test and make it better memory offlining.
  - support HugeTLB page migration and offlining.
  - memmap removing at memory offline.
  - physical remove memory.

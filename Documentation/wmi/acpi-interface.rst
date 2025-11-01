.. SPDX-License-Identifier: GPL-2.0-or-later

==================
ACPI WMI interface
==================

The ACPI WMI interface is a proprietary extension of the ACPI specification made
by Microsoft to allow hardware vendors to embed WMI (Windows Management Instrumentation)
objects inside their ACPI firmware. Typical functions implemented over ACPI WMI
are hotkey events on modern notebooks and configuration of BIOS options.

PNP0C14 ACPI device
-------------------

Discovery of WMI objects is handled by defining ACPI devices with a PNP ID
of ``PNP0C14``. These devices will contain a set of ACPI buffers and methods
used for mapping and execution of WMI methods and/or queries. If there exist
multiple of such devices, then each device is required to have a
unique ACPI UID.

_WDG buffer
-----------

The ``_WDG`` buffer is used to discover WMI objects and is required to be
static. Its internal structure consists of data blocks with a size of 20 bytes,
containing the following data:

======= =============== =====================================================
Offset  Size (in bytes) Content
======= =============== =====================================================
0x00    16              128 bit Variant 2 object GUID.
0x10    2               2 character method ID or single byte notification ID.
0x12    1               Object instance count.
0x13    1               Object flags.
======= =============== =====================================================

The WMI object flags control whether the method or notification ID is used:

- 0x1: Data block is expensive to collect.
- 0x2: Data block contains WMI methods.
- 0x4: Data block contains ASCIZ string.
- 0x8: Data block describes a WMI event, use notification ID instead
  of method ID.

Each WMI object GUID can appear multiple times inside a system.
The method/notification ID is used to construct the ACPI method names used for
interacting with the WMI object.

WQxx ACPI methods
-----------------

If a data block does not contain WMI methods, then its content can be retrieved
by this required ACPI method. The last two characters of the ACPI method name
are the method ID of the data block to query. Their single parameter is an
integer describing the instance which should be queried. This parameter can be
omitted if the data block contains only a single instance.

WSxx ACPI methods
-----------------

Similar to the ``WQxx`` ACPI methods, except that it is optional and takes an
additional buffer as its second argument. The instance argument also cannot
be omitted.

WMxx ACPI methods
-----------------

Used for executing WMI methods associated with a data block. The last two
characters of the ACPI method name are the method ID of the data block
containing the WMI methods. Their first parameter is a integer describing the
instance which methods should be executed. The second parameter is an integer
describing the WMI method ID to execute, and the third parameter is a buffer
containing the WMI method parameters. If the data block is marked as containing
an ASCIZ string, then this buffer should contain an ASCIZ string. The ACPI
method will return the result of the executed WMI method.

WExx ACPI methods
-----------------

Used for optionally enabling/disabling WMI events, the last two characters of
the ACPI method are the notification ID of the data block describing the WMI
event as hexadecimal value. Their first parameter is an integer with a value
of 0 if the WMI event should be disabled, other values will enable
the WMI event.

Those ACPI methods are always called even for WMI events not registered as
being expensive to collect to match the behavior of the Windows driver.

WCxx ACPI methods
-----------------
Similar to the ``WExx`` ACPI methods, except that instead of WMI events it controls
data collection of data blocks registered as being expensive to collect. Thus the
last two characters of the ACPI method name are the method ID of the data block
to enable/disable.

Those ACPI methods are also called before setting data blocks to match the
behavior of the Windows driver.

_WED ACPI method
----------------

Used to retrieve additional WMI event data, its single parameter is a integer
holding the notification ID of the event. This method should be evaluated every
time an ACPI notification is received, since some ACPI implementations use a
queue to store WMI event data items. This queue will overflow after a couple
of WMI events are received without retrieving the associated WMI event data.

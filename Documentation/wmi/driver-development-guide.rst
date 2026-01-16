.. SPDX-License-Identifier: GPL-2.0-or-later

============================
WMI driver development guide
============================

The WMI subsystem provides a rich driver API for implementing WMI drivers,
documented at Documentation/driver-api/wmi.rst. This document will serve
as an introductory guide for WMI driver writers using this API. It is supposed
to be a successor to the original LWN article [1]_ which deals with WMI drivers
using the deprecated GUID-based WMI interface.

Obtaining WMI device information
--------------------------------

Before developing an WMI driver, information about the WMI device in question
must be obtained. The `lswmi <https://pypi.org/project/lswmi>`_ utility can be
used to extract detailed WMI device information using the following command:

::

  lswmi -V

The resulting output will contain information about all WMI devices available on
a given machine, plus some extra information.

In order to find out more about the interface used to communicate with a WMI device,
the `bmfdec <https://github.com/pali/bmfdec>`_ utilities can be used to decode
the Binary MOF (Managed Object Format) information used to describe WMI devices.
The ``wmi-bmof`` driver exposes this information to userspace, see
Documentation/wmi/devices/wmi-bmof.rst.

In order to retrieve the decoded Binary MOF information, use the following command (requires root):

::

  ./bmf2mof /sys/bus/wmi/devices/05901221-D566-11D1-B2F0-00A0C9062910[-X]/bmof

Sometimes, looking at the disassembled ACPI tables used to describe the WMI device
helps in understanding how the WMI device is supposed to work. The path of the ACPI
method associated with a given WMI device can be retrieved using the ``lswmi`` utility
as mentioned above.

If you are attempting to port a driver to Linux and are working on a Windows
system, `WMIExplorer <https://github.com/vinaypamnani/wmie2>`_ can be useful
for inspecting available WMI methods and invoking them directly.

Basic WMI driver structure
--------------------------

The basic WMI driver is build around the struct wmi_driver, which is then bound
to matching WMI devices using a struct wmi_device_id table:

::

  static const struct wmi_device_id foo_id_table[] = {
         /* Only use uppercase letters! */
         { "936DA01F-9ABD-4D9D-80C7-02AF85C822A8", NULL },
         { }
  };
  MODULE_DEVICE_TABLE(wmi, foo_id_table);

  static struct wmi_driver foo_driver = {
        .driver = {
                .name = "foo",
                .probe_type = PROBE_PREFER_ASYNCHRONOUS,        /* recommended */
                .pm = pm_sleep_ptr(&foo_dev_pm_ops),            /* optional */
        },
        .id_table = foo_id_table,
        .probe = foo_probe,
        .remove = foo_remove,         /* optional, devres is preferred */
        .shutdown = foo_shutdown,     /* optional, called during shutdown */
        .notify_new = foo_notify,     /* optional, for event handling */
        .no_notify_data = true,       /* optional, enables events containing no additional data */
        .no_singleton = true,         /* required for new WMI drivers */
  };
  module_wmi_driver(foo_driver);

The probe() callback is called when the WMI driver is bound to a matching WMI device. Allocating
driver-specific data structures and initialising interfaces to other kernel subsystems should
normally be done in this function.

The remove() callback is then called when the WMI driver is unbound from a WMI device. In order
to unregister interfaces to other kernel subsystems and release resources, devres should be used.
This simplifies error handling during probe and often allows to omit this callback entirely, see
Documentation/driver-api/driver-model/devres.rst for details.

The shutdown() callback is called during shutdown, reboot or kexec. Its sole purpose is to disable
the WMI device and put it in a well-known state for the WMI driver to pick up later after reboot
or kexec. Most WMI drivers need no special shutdown handling and can thus omit this callback.

Please note that new WMI drivers are required to be able to be instantiated multiple times,
and are forbidden from using any deprecated GUID-based or ACPI-based WMI functions. This means
that the WMI driver should be prepared for the scenario that multiple matching WMI devices are
present on a given machine.

Because of this, WMI drivers should use the state container design pattern as described in
Documentation/driver-api/driver-model/design-patterns.rst.

.. warning:: Using both GUID-based and non-GUID-based functions for querying WMI data blocks and
             handling WMI events simultaneously on the same device is guaranteed to corrupt the
             WMI device state and might lead to erratic behaviour.

WMI method drivers
------------------

WMI drivers can call WMI device methods using wmidev_invoke_method(). For each WMI method
invocation the WMI driver needs to provide the instance number and the method ID, as well as
a buffer with the method arguments and optionally a buffer for the results.

The layout of said buffers is device-specific and described by the Binary MOF data associated
with a given WMI device. Said Binary MOF data also describes the method ID of a given WMI method
with the ``WmiMethodId`` qualifier. WMI devices exposing WMI methods usually expose only a single
instance (instance number 0), but in theory can expose multiple instances as well. In such a case
the number of instances can be retrieved using wmidev_instance_count().

Take a look at drivers/platform/x86/intel/wmi/thunderbolt.c for an example WMI method driver.

WMI data block drivers
----------------------

WMI drivers can query WMI data blocks using wmidev_query_block(), the layout of the returned
buffer is again device-specific and described by the Binary MOF data. Some WMI data blocks are
also writeable and can be set using wmidev_set_block(). The number of data block instances can
again be retrieved using wmidev_instance_count().

Take a look at drivers/platform/x86/intel/wmi/sbl-fw-update.c for an example WMI data block driver.

WMI event drivers
-----------------

WMI drivers can receive WMI events via the notify_new() callback inside the struct wmi_driver.
The WMI subsystem will then take care of setting up the WMI event accordingly. Please note that
the layout of the buffer passed to this callback is device-specific, and freeing of the buffer
is done by the WMI subsystem itself, not the driver.

The WMI driver core will take care that the notify_new() callback will only be called after
the probe() callback has been called, and that no events are being received by the driver
right before and after calling its remove() or shutdown() callback.

However WMI driver developers should be aware that multiple WMI events can be received concurrently,
so any locking (if necessary) needs to be provided by the WMI driver itself.

In order to be able to receive WMI events containing no additional event data,
the ``no_notify_data`` flag inside struct wmi_driver should be set to ``true``.

Take a look at drivers/platform/x86/xiaomi-wmi.c for an example WMI event driver.

Exchanging data with the WMI driver core
----------------------------------------

WMI drivers can exchange data with the WMI driver core using struct wmi_buffer. The internal
structure of those buffers is device-specific and only known by the WMI driver. Because of this
the WMI driver itself is responsible for parsing and validating the data received from its
WMI device.

The structure of said buffers is described by the MOF data associated with the WMI device in
question. When such a buffer contains multiple data items it usually makes sense to define a
C structure and use it during parsing. Since the WMI driver core guarantees that all buffers
received from a WMI device are aligned on an 8-byte boundary, WMI drivers can simply perform
a cast between the WMI buffer data and this C structure.

This however should only be done after the size of the buffer was verified to be large enough
to hold the whole C structure. WMI drivers should reject undersized buffers as they are usually
sent by the WMI device to signal an internal error. Oversized buffers however should be accepted
to emulate the behavior of the Windows WMI implementation.

When defining a C structure for parsing WMI buffers the alignment of the data items should be
respected. This is especially important for 64-bit integers as those have different alignments
on 64-bit (8-byte alignment) and 32-bit (4-byte alignment) architectures. It is thus a good idea
to manually specify the alignment of such data items or mark the whole structure as packed when
appropriate. Integer data items in general are little-endian integers and should be marked as
such using ``__le64`` and friends. When parsing WMI string data items the struct wmi_string should
be used as WMI strings have a different layout than C strings.

See Documentation/wmi/acpi-interface.rst for more information regarding the binary format
of WMI data items.

Handling multiple WMI devices at once
-------------------------------------

There are many cases of firmware vendors using multiple WMI devices to control different aspects
of a single physical device. This can make developing WMI drivers complicated, as those drivers
might need to communicate with each other to present a unified interface to userspace.

On such case involves a WMI event device which needs to talk to a WMI data block device or WMI
method device upon receiving an WMI event. In such a case, two WMI drivers should be developed,
one for the WMI event device and one for the other WMI device.

The WMI event device driver has only one purpose: to receive WMI events, validate any additional
event data and invoke a notifier chain. The other WMI driver adds itself to this notifier chain
during probing and thus gets notified every time a WMI event is received. This WMI driver might
then process the event further for example by using an input device.

For other WMI device constellations, similar mechanisms can be used.

Things to avoid
---------------

When developing WMI drivers, there are a couple of things which should be avoided:

- usage of the deprecated GUID-based WMI interface which uses GUIDs instead of WMI device structs
- usage of the deprecated ACPI-based WMI interface which uses ACPI objects instead of plain buffers
- bypassing of the WMI subsystem when talking to WMI devices
- WMI drivers which cannot be instantiated multiple times.

Many older WMI drivers violate one or more points from this list. The reason for
this is that the WMI subsystem evolved significantly over the last two decades,
so there is a lot of legacy cruft inside older WMI drivers.

New WMI drivers are also required to conform to the linux kernel coding style as specified in
Documentation/process/coding-style.rst. The checkpatch utility can catch many common coding style
violations, you can invoke it with the following command:

::

  ./scripts/checkpatch.pl --strict <path to driver file>

References
==========

.. [1] https://lwn.net/Articles/391230/

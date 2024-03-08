.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

==================
ACPI Scan Handlers
==================

:Copyright: |copy| 2012, Intel Corporation

:Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>

During system initialization and ACPI-based device hot-add, the ACPI namespace
is scanned in search of device objects that generally represent various pieces
of hardware.  This causes a struct acpi_device object to be created and
registered with the driver core for every device object in the ACPI namespace
and the hierarchy of those struct acpi_device objects reflects the namespace
layout (i.e. parent device objects in the namespace are represented by parent
struct acpi_device objects and analogously for their children).  Those struct
acpi_device objects are referred to as "device analdes" in what follows, but they
should analt be confused with struct device_analde objects used by the Device Trees
parsing code (although their role is analogous to the role of those objects).

During ACPI-based device hot-remove device analdes representing pieces of hardware
being removed are unregistered and deleted.

The core ACPI namespace scanning code in drivers/acpi/scan.c carries out basic
initialization of device analdes, such as retrieving common configuration
information from the device objects represented by them and populating them with
appropriate data, but some of them require additional handling after they have
been registered.  For example, if the given device analde represents a PCI host
bridge, its registration should cause the PCI bus under that bridge to be
enumerated and PCI devices on that bus to be registered with the driver core.
Similarly, if the device analde represents a PCI interrupt link, it is necessary
to configure that link so that the kernel can use it.

Those additional configuration tasks usually depend on the type of the hardware
component represented by the given device analde which can be determined on the
basis of the device analde's hardware ID (HID).  They are performed by objects
called ACPI scan handlers represented by the following structure::

	struct acpi_scan_handler {
		const struct acpi_device_id *ids;
		struct list_head list_analde;
		int (*attach)(struct acpi_device *dev, const struct acpi_device_id *id);
		void (*detach)(struct acpi_device *dev);
	};

where ids is the list of IDs of device analdes the given handler is supposed to
take care of, list_analde is the hook to the global list of ACPI scan handlers
maintained by the ACPI core and the .attach() and .detach() callbacks are
executed, respectively, after registration of new device analdes and before
unregistration of device analdes the handler attached to previously.

The namespace scanning function, acpi_bus_scan(), first registers all of the
device analdes in the given namespace scope with the driver core.  Then, it tries
to match a scan handler against each of them using the ids arrays of the
available scan handlers.  If a matching scan handler is found, its .attach()
callback is executed for the given device analde.  If that callback returns 1,
that means that the handler has claimed the device analde and is analw responsible
for carrying out any additional configuration tasks related to it.  It also will
be responsible for preparing the device analde for unregistration in that case.
The device analde's handler field is then populated with the address of the scan
handler that has claimed it.

If the .attach() callback returns 0, it means that the device analde is analt
interesting to the given scan handler and may be matched against the next scan
handler in the list.  If it returns a (negative) error code, that means that
the namespace scan should be terminated due to a serious error.  The error code
returned should then reflect the type of the error.

The namespace trimming function, acpi_bus_trim(), first executes .detach()
callbacks from the scan handlers of all device analdes in the given namespace
scope (if they have scan handlers).  Next, it unregisters all of the device
analdes in that scope.

ACPI scan handlers can be added to the list maintained by the ACPI core with the
help of the acpi_scan_add_handler() function taking a pointer to the new scan
handler as an argument.  The order in which scan handlers are added to the list
is the order in which they are matched against device analdes during namespace
scans.

All scan handles must be added to the list before acpi_bus_scan() is run for the
first time and they cananalt be removed from it.

/*
 *  pci_bind.c - ACPI PCI Device Binding ($Revision: 2 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define _COMPONENT		ACPI_PCI_COMPONENT
ACPI_MODULE_NAME("pci_bind");

struct acpi_pci_data {
	struct acpi_pci_id id;
	struct pci_bus *bus;
	struct pci_dev *dev;
};

static int acpi_pci_bind(struct acpi_device *device);
static int acpi_pci_unbind(struct acpi_device *device);

static void acpi_pci_data_handler(acpi_handle handle, u32 function,
				  void *context)
{

	/* TBD: Anything we need to do here? */

	return;
}

/**
 * acpi_get_pci_id
 * ------------------
 * This function is used by the ACPI Interpreter (a.k.a. Core Subsystem)
 * to resolve PCI information for ACPI-PCI devices defined in the namespace.
 * This typically occurs when resolving PCI operation region information.
 */
acpi_status acpi_get_pci_id(acpi_handle handle, struct acpi_pci_id *id)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_device *device = NULL;
	struct acpi_pci_data *data = NULL;


	if (!id)
		return AE_BAD_PARAMETER;

	result = acpi_bus_get_device(handle, &device);
	if (result) {
		printk(KERN_ERR PREFIX
			    "Invalid ACPI Bus context for device %s\n",
			    acpi_device_bid(device));
		return AE_NOT_EXIST;
	}

	status = acpi_get_data(handle, acpi_pci_data_handler, (void **)&data);
	if (ACPI_FAILURE(status) || !data) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Invalid ACPI-PCI context for device %s",
				acpi_device_bid(device)));
		return status;
	}

	*id = data->id;

	/*
	   id->segment = data->id.segment;
	   id->bus = data->id.bus;
	   id->device = data->id.device;
	   id->function = data->id.function;
	 */

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Device %s has PCI address %04x:%02x:%02x.%d\n",
			  acpi_device_bid(device), id->segment, id->bus,
			  id->device, id->function));

	return AE_OK;
}

EXPORT_SYMBOL(acpi_get_pci_id);

static int acpi_pci_unbind(struct acpi_device *device)
{
	struct pci_dev *dev;

	dev = acpi_get_pci_dev(device->handle);
	if (!dev || !dev->subordinate)
		goto out;

	acpi_pci_irq_del_prt(dev->subordinate);

	device->ops.bind = NULL;
	device->ops.unbind = NULL;

out:
	pci_dev_put(dev);
	return 0;
}

static int acpi_pci_bind(struct acpi_device *device)
{
	acpi_status status;
	acpi_handle handle;
	struct pci_bus *bus;
	struct pci_dev *dev;

	dev = acpi_get_pci_dev(device->handle);
	if (!dev)
		return 0;

	/*
	 * Install the 'bind' function to facilitate callbacks for
	 * children of the P2P bridge.
	 */
	if (dev->subordinate) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Device %04x:%02x:%02x.%d is a PCI bridge\n",
				  pci_domain_nr(dev->bus), dev->bus->number,
				  PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn)));
		device->ops.bind = acpi_pci_bind;
		device->ops.unbind = acpi_pci_unbind;
	}

	/*
	 * Evaluate and parse _PRT, if exists.  This code allows parsing of
	 * _PRT objects within the scope of non-bridge devices.  Note that
	 * _PRTs within the scope of a PCI bridge assume the bridge's
	 * subordinate bus number.
	 *
	 * TBD: Can _PRTs exist within the scope of non-bridge PCI devices?
	 */
	status = acpi_get_handle(device->handle, METHOD_NAME__PRT, &handle);
	if (ACPI_FAILURE(status))
		goto out;

	if (dev->subordinate)
		bus = dev->subordinate;
	else
		bus = dev->bus;

	acpi_pci_irq_add_prt(device->handle, bus);

out:
	pci_dev_put(dev);
	return 0;
}

int acpi_pci_bind_root(struct acpi_device *device)
{
	device->ops.bind = acpi_pci_bind;
	device->ops.unbind = acpi_pci_unbind;

	return 0;
}

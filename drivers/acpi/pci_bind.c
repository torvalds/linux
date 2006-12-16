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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/pm.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define _COMPONENT		ACPI_PCI_COMPONENT
ACPI_MODULE_NAME("pci_bind")

struct acpi_pci_data {
	struct acpi_pci_id id;
	struct pci_bus *bus;
	struct pci_dev *dev;
};

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
			  "Device %s has PCI address %02x:%02x:%02x.%02x\n",
			  acpi_device_bid(device), id->segment, id->bus,
			  id->device, id->function));

	return AE_OK;
}

EXPORT_SYMBOL(acpi_get_pci_id);

int acpi_pci_bind(struct acpi_device *device)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_pci_data *data = NULL;
	struct acpi_pci_data *pdata = NULL;
	char *pathname = NULL;
	struct acpi_buffer buffer = { 0, NULL };
	acpi_handle handle = NULL;
	struct pci_dev *dev;
	struct pci_bus *bus;


	if (!device || !device->parent)
		return -EINVAL;

	pathname = kmalloc(ACPI_PATHNAME_MAX, GFP_KERNEL);
	if (!pathname)
		return -ENOMEM;
	memset(pathname, 0, ACPI_PATHNAME_MAX);
	buffer.length = ACPI_PATHNAME_MAX;
	buffer.pointer = pathname;

	data = kmalloc(sizeof(struct acpi_pci_data), GFP_KERNEL);
	if (!data) {
		kfree(pathname);
		return -ENOMEM;
	}
	memset(data, 0, sizeof(struct acpi_pci_data));

	acpi_get_name(device->handle, ACPI_FULL_PATHNAME, &buffer);
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Binding PCI device [%s]...\n",
			  pathname));

	/* 
	 * Segment & Bus
	 * -------------
	 * These are obtained via the parent device's ACPI-PCI context.
	 */
	status = acpi_get_data(device->parent->handle, acpi_pci_data_handler,
			       (void **)&pdata);
	if (ACPI_FAILURE(status) || !pdata || !pdata->bus) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Invalid ACPI-PCI context for parent device %s",
				acpi_device_bid(device->parent)));
		result = -ENODEV;
		goto end;
	}
	data->id.segment = pdata->id.segment;
	data->id.bus = pdata->bus->number;

	/*
	 * Device & Function
	 * -----------------
	 * These are simply obtained from the device's _ADR method.  Note
	 * that a value of zero is valid.
	 */
	data->id.device = device->pnp.bus_address >> 16;
	data->id.function = device->pnp.bus_address & 0xFFFF;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "...to %02x:%02x:%02x.%02x\n",
			  data->id.segment, data->id.bus, data->id.device,
			  data->id.function));

	/*
	 * TBD: Support slot devices (e.g. function=0xFFFF).
	 */

	/* 
	 * Locate PCI Device
	 * -----------------
	 * Locate matching device in PCI namespace.  If it doesn't exist
	 * this typically means that the device isn't currently inserted
	 * (e.g. docking station, port replicator, etc.).
	 * We cannot simply search the global pci device list, since
	 * PCI devices are added to the global pci list when the root
	 * bridge start ops are run, which may not have happened yet.
	 */
	bus = pci_find_bus(data->id.segment, data->id.bus);
	if (bus) {
		list_for_each_entry(dev, &bus->devices, bus_list) {
			if (dev->devfn == PCI_DEVFN(data->id.device,
						    data->id.function)) {
				data->dev = dev;
				break;
			}
		}
	}
	if (!data->dev) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Device %02x:%02x:%02x.%02x not present in PCI namespace\n",
				  data->id.segment, data->id.bus,
				  data->id.device, data->id.function));
		result = -ENODEV;
		goto end;
	}
	if (!data->dev->bus) {
		printk(KERN_ERR PREFIX
			    "Device %02x:%02x:%02x.%02x has invalid 'bus' field\n",
			    data->id.segment, data->id.bus,
			    data->id.device, data->id.function);
		result = -ENODEV;
		goto end;
	}

	/*
	 * PCI Bridge?
	 * -----------
	 * If so, set the 'bus' field and install the 'bind' function to 
	 * facilitate callbacks for all of its children.
	 */
	if (data->dev->subordinate) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Device %02x:%02x:%02x.%02x is a PCI bridge\n",
				  data->id.segment, data->id.bus,
				  data->id.device, data->id.function));
		data->bus = data->dev->subordinate;
		device->ops.bind = acpi_pci_bind;
		device->ops.unbind = acpi_pci_unbind;
	}

	/*
	 * Attach ACPI-PCI Context
	 * -----------------------
	 * Thus binding the ACPI and PCI devices.
	 */
	status = acpi_attach_data(device->handle, acpi_pci_data_handler, data);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Unable to attach ACPI-PCI context to device %s",
				acpi_device_bid(device)));
		result = -ENODEV;
		goto end;
	}

	/*
	 * PCI Routing Table
	 * -----------------
	 * Evaluate and parse _PRT, if exists.  This code is independent of 
	 * PCI bridges (above) to allow parsing of _PRT objects within the
	 * scope of non-bridge devices.  Note that _PRTs within the scope of
	 * a PCI bridge assume the bridge's subordinate bus number.
	 *
	 * TBD: Can _PRTs exist within the scope of non-bridge PCI devices?
	 */
	status = acpi_get_handle(device->handle, METHOD_NAME__PRT, &handle);
	if (ACPI_SUCCESS(status)) {
		if (data->bus)	/* PCI-PCI bridge */
			acpi_pci_irq_add_prt(device->handle, data->id.segment,
					     data->bus->number);
		else		/* non-bridge PCI device */
			acpi_pci_irq_add_prt(device->handle, data->id.segment,
					     data->id.bus);
	}

      end:
	kfree(pathname);
	if (result)
		kfree(data);

	return result;
}

int acpi_pci_unbind(struct acpi_device *device)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_pci_data *data = NULL;
	char *pathname = NULL;
	struct acpi_buffer buffer = { 0, NULL };


	if (!device || !device->parent)
		return -EINVAL;

	pathname = kmalloc(ACPI_PATHNAME_MAX, GFP_KERNEL);
	if (!pathname)
		return -ENOMEM;
	memset(pathname, 0, ACPI_PATHNAME_MAX);

	buffer.length = ACPI_PATHNAME_MAX;
	buffer.pointer = pathname;
	acpi_get_name(device->handle, ACPI_FULL_PATHNAME, &buffer);
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Unbinding PCI device [%s]...\n",
			  pathname));
	kfree(pathname);

	status =
	    acpi_get_data(device->handle, acpi_pci_data_handler,
			  (void **)&data);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Unable to get data from device %s",
				acpi_device_bid(device)));
		result = -ENODEV;
		goto end;
	}

	status = acpi_detach_data(device->handle, acpi_pci_data_handler);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Unable to detach data from device %s",
				acpi_device_bid(device)));
		result = -ENODEV;
		goto end;
	}
	if (data->dev->subordinate) {
		acpi_pci_irq_del_prt(data->id.segment, data->bus->number);
	}
	kfree(data);

      end:
	return result;
}

int
acpi_pci_bind_root(struct acpi_device *device,
		   struct acpi_pci_id *id, struct pci_bus *bus)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_pci_data *data = NULL;
	char *pathname = NULL;
	struct acpi_buffer buffer = { 0, NULL };


	pathname = kmalloc(ACPI_PATHNAME_MAX, GFP_KERNEL);
	if (!pathname)
		return -ENOMEM;
	memset(pathname, 0, ACPI_PATHNAME_MAX);

	buffer.length = ACPI_PATHNAME_MAX;
	buffer.pointer = pathname;

	if (!device || !id || !bus) {
		kfree(pathname);
		return -EINVAL;
	}

	data = kmalloc(sizeof(struct acpi_pci_data), GFP_KERNEL);
	if (!data) {
		kfree(pathname);
		return -ENOMEM;
	}
	memset(data, 0, sizeof(struct acpi_pci_data));

	data->id = *id;
	data->bus = bus;
	device->ops.bind = acpi_pci_bind;
	device->ops.unbind = acpi_pci_unbind;

	acpi_get_name(device->handle, ACPI_FULL_PATHNAME, &buffer);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Binding PCI root bridge [%s] to "
			  "%02x:%02x\n", pathname, id->segment, id->bus));

	status = acpi_attach_data(device->handle, acpi_pci_data_handler, data);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Unable to attach ACPI-PCI context to device %s",
				pathname));
		result = -ENODEV;
		goto end;
	}

      end:
	kfree(pathname);
	if (result != 0)
		kfree(data);

	return result;
}

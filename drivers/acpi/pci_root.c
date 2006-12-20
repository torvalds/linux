/*
 *  pci_root.c - ACPI PCI Root Bridge Driver ($Revision: 40 $)
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
ACPI_MODULE_NAME("pci_root")
#define ACPI_PCI_ROOT_CLASS		"pci_bridge"
#define ACPI_PCI_ROOT_HID		"PNP0A03"
#define ACPI_PCI_ROOT_DRIVER_NAME	"ACPI PCI Root Bridge Driver"
#define ACPI_PCI_ROOT_DEVICE_NAME	"PCI Root Bridge"
static int acpi_pci_root_add(struct acpi_device *device);
static int acpi_pci_root_remove(struct acpi_device *device, int type);
static int acpi_pci_root_start(struct acpi_device *device);

static struct acpi_driver acpi_pci_root_driver = {
	.name = ACPI_PCI_ROOT_DRIVER_NAME,
	.class = ACPI_PCI_ROOT_CLASS,
	.ids = ACPI_PCI_ROOT_HID,
	.ops = {
		.add = acpi_pci_root_add,
		.remove = acpi_pci_root_remove,
		.start = acpi_pci_root_start,
		},
};

struct acpi_pci_root {
	struct list_head node;
	struct acpi_device * device;
	struct acpi_pci_id id;
	struct pci_bus *bus;
};

static LIST_HEAD(acpi_pci_roots);

static struct acpi_pci_driver *sub_driver;

int acpi_pci_register_driver(struct acpi_pci_driver *driver)
{
	int n = 0;
	struct list_head *entry;

	struct acpi_pci_driver **pptr = &sub_driver;
	while (*pptr)
		pptr = &(*pptr)->next;
	*pptr = driver;

	if (!driver->add)
		return 0;

	list_for_each(entry, &acpi_pci_roots) {
		struct acpi_pci_root *root;
		root = list_entry(entry, struct acpi_pci_root, node);
		driver->add(root->device->handle);
		n++;
	}

	return n;
}

EXPORT_SYMBOL(acpi_pci_register_driver);

void acpi_pci_unregister_driver(struct acpi_pci_driver *driver)
{
	struct list_head *entry;

	struct acpi_pci_driver **pptr = &sub_driver;
	while (*pptr) {
		if (*pptr == driver)
			break;
		pptr = &(*pptr)->next;
	}
	BUG_ON(!*pptr);
	*pptr = (*pptr)->next;

	if (!driver->remove)
		return;

	list_for_each(entry, &acpi_pci_roots) {
		struct acpi_pci_root *root;
		root = list_entry(entry, struct acpi_pci_root, node);
		driver->remove(root->device->handle);
	}
}

EXPORT_SYMBOL(acpi_pci_unregister_driver);

static acpi_status
get_root_bridge_busnr_callback(struct acpi_resource *resource, void *data)
{
	int *busnr = data;
	struct acpi_resource_address64 address;

	if (resource->type != ACPI_RESOURCE_TYPE_ADDRESS16 &&
	    resource->type != ACPI_RESOURCE_TYPE_ADDRESS32 &&
	    resource->type != ACPI_RESOURCE_TYPE_ADDRESS64)
		return AE_OK;

	acpi_resource_to_address64(resource, &address);
	if ((address.address_length > 0) &&
	    (address.resource_type == ACPI_BUS_NUMBER_RANGE))
		*busnr = address.minimum;

	return AE_OK;
}

static acpi_status try_get_root_bridge_busnr(acpi_handle handle, int *busnum)
{
	acpi_status status;

	*busnum = -1;
	status =
	    acpi_walk_resources(handle, METHOD_NAME__CRS,
				get_root_bridge_busnr_callback, busnum);
	if (ACPI_FAILURE(status))
		return status;
	/* Check if we really get a bus number from _CRS */
	if (*busnum == -1)
		return AE_ERROR;
	return AE_OK;
}

static int acpi_pci_root_add(struct acpi_device *device)
{
	int result = 0;
	struct acpi_pci_root *root = NULL;
	struct acpi_pci_root *tmp;
	acpi_status status = AE_OK;
	unsigned long value = 0;
	acpi_handle handle = NULL;


	if (!device)
		return -EINVAL;

	root = kmalloc(sizeof(struct acpi_pci_root), GFP_KERNEL);
	if (!root)
		return -ENOMEM;
	memset(root, 0, sizeof(struct acpi_pci_root));
	INIT_LIST_HEAD(&root->node);

	root->device = device;
	strcpy(acpi_device_name(device), ACPI_PCI_ROOT_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_PCI_ROOT_CLASS);
	acpi_driver_data(device) = root;

	/*
	 * TBD: Doesn't the bus driver automatically set this?
	 */
	device->ops.bind = acpi_pci_bind;

	/* 
	 * Segment
	 * -------
	 * Obtained via _SEG, if exists, otherwise assumed to be zero (0).
	 */
	status = acpi_evaluate_integer(device->handle, METHOD_NAME__SEG, NULL,
				       &value);
	switch (status) {
	case AE_OK:
		root->id.segment = (u16) value;
		break;
	case AE_NOT_FOUND:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Assuming segment 0 (no _SEG)\n"));
		root->id.segment = 0;
		break;
	default:
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _SEG"));
		result = -ENODEV;
		goto end;
	}

	/* 
	 * Bus
	 * ---
	 * Obtained via _BBN, if exists, otherwise assumed to be zero (0).
	 */
	status = acpi_evaluate_integer(device->handle, METHOD_NAME__BBN, NULL,
				       &value);
	switch (status) {
	case AE_OK:
		root->id.bus = (u16) value;
		break;
	case AE_NOT_FOUND:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Assuming bus 0 (no _BBN)\n"));
		root->id.bus = 0;
		break;
	default:
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _BBN"));
		result = -ENODEV;
		goto end;
	}

	/* Some systems have wrong _BBN */
	list_for_each_entry(tmp, &acpi_pci_roots, node) {
		if ((tmp->id.segment == root->id.segment)
		    && (tmp->id.bus == root->id.bus)) {
			int bus = 0;
			acpi_status status;

			printk(KERN_ERR PREFIX
				    "Wrong _BBN value, reboot"
				    " and use option 'pci=noacpi'\n");

			status = try_get_root_bridge_busnr(device->handle, &bus);
			if (ACPI_FAILURE(status))
				break;
			if (bus != root->id.bus) {
				printk(KERN_INFO PREFIX
				       "PCI _CRS %d overrides _BBN 0\n", bus);
				root->id.bus = bus;
			}
			break;
		}
	}
	/*
	 * Device & Function
	 * -----------------
	 * Obtained from _ADR (which has already been evaluated for us).
	 */
	root->id.device = device->pnp.bus_address >> 16;
	root->id.function = device->pnp.bus_address & 0xFFFF;

	/*
	 * TBD: Need PCI interface for enumeration/configuration of roots.
	 */

	/* TBD: Locking */
	list_add_tail(&root->node, &acpi_pci_roots);

	printk(KERN_INFO PREFIX "%s [%s] (%04x:%02x)\n",
	       acpi_device_name(device), acpi_device_bid(device),
	       root->id.segment, root->id.bus);

	/*
	 * Scan the Root Bridge
	 * --------------------
	 * Must do this prior to any attempt to bind the root device, as the
	 * PCI namespace does not get created until this call is made (and 
	 * thus the root bridge's pci_dev does not exist).
	 */
	root->bus = pci_acpi_scan_root(device, root->id.segment, root->id.bus);
	if (!root->bus) {
		printk(KERN_ERR PREFIX
			    "Bus %04x:%02x not present in PCI namespace\n",
			    root->id.segment, root->id.bus);
		result = -ENODEV;
		goto end;
	}

	/*
	 * Attach ACPI-PCI Context
	 * -----------------------
	 * Thus binding the ACPI and PCI devices.
	 */
	result = acpi_pci_bind_root(device, &root->id, root->bus);
	if (result)
		goto end;

	/*
	 * PCI Routing Table
	 * -----------------
	 * Evaluate and parse _PRT, if exists.
	 */
	status = acpi_get_handle(device->handle, METHOD_NAME__PRT, &handle);
	if (ACPI_SUCCESS(status))
		result = acpi_pci_irq_add_prt(device->handle, root->id.segment,
					      root->id.bus);

      end:
	if (result) {
		if (!list_empty(&root->node))
			list_del(&root->node);
		kfree(root);
	}

	return result;
}

static int acpi_pci_root_start(struct acpi_device *device)
{
	struct acpi_pci_root *root;


	list_for_each_entry(root, &acpi_pci_roots, node) {
		if (root->device == device) {
			pci_bus_add_devices(root->bus);
			return 0;
		}
	}
	return -ENODEV;
}

static int acpi_pci_root_remove(struct acpi_device *device, int type)
{
	struct acpi_pci_root *root = NULL;


	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	root = acpi_driver_data(device);

	kfree(root);

	return 0;
}

static int __init acpi_pci_root_init(void)
{

	if (acpi_pci_disabled)
		return 0;

	/* DEBUG:
	   acpi_dbg_layer = ACPI_PCI_COMPONENT;
	   acpi_dbg_level = 0xFFFFFFFF;
	 */

	if (acpi_bus_register_driver(&acpi_pci_root_driver) < 0)
		return -ENODEV;

	return 0;
}

subsys_initcall(acpi_pci_root_init);

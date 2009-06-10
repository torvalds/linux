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
#include <linux/pci-acpi.h>
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define _COMPONENT		ACPI_PCI_COMPONENT
ACPI_MODULE_NAME("pci_root");
#define ACPI_PCI_ROOT_CLASS		"pci_bridge"
#define ACPI_PCI_ROOT_DEVICE_NAME	"PCI Root Bridge"
static int acpi_pci_root_add(struct acpi_device *device);
static int acpi_pci_root_remove(struct acpi_device *device, int type);
static int acpi_pci_root_start(struct acpi_device *device);

static struct acpi_device_id root_device_ids[] = {
	{"PNP0A03", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, root_device_ids);

static struct acpi_driver acpi_pci_root_driver = {
	.name = "pci_root",
	.class = ACPI_PCI_ROOT_CLASS,
	.ids = root_device_ids,
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

	u32 osc_support_set;	/* _OSC state of support bits */
	u32 osc_control_set;	/* _OSC state of control bits */
	u32 osc_control_qry;	/* the latest _OSC query result */

	u32 osc_queried:1;	/* has _OSC control been queried? */
};

static LIST_HEAD(acpi_pci_roots);

static struct acpi_pci_driver *sub_driver;
static DEFINE_MUTEX(osc_lock);

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

acpi_handle acpi_get_pci_rootbridge_handle(unsigned int seg, unsigned int bus)
{
	struct acpi_pci_root *tmp;
	
	list_for_each_entry(tmp, &acpi_pci_roots, node) {
		if ((tmp->id.segment == (u16) seg) && (tmp->id.bus == (u16) bus))
			return tmp->device->handle;
	}
	return NULL;		
}

EXPORT_SYMBOL_GPL(acpi_get_pci_rootbridge_handle);

/**
 * acpi_is_root_bridge - determine whether an ACPI CA node is a PCI root bridge
 * @handle - the ACPI CA node in question.
 *
 * Note: we could make this API take a struct acpi_device * instead, but
 * for now, it's more convenient to operate on an acpi_handle.
 */
int acpi_is_root_bridge(acpi_handle handle)
{
	int ret;
	struct acpi_device *device;

	ret = acpi_bus_get_device(handle, &device);
	if (ret)
		return 0;

	ret = acpi_match_device_ids(device, root_device_ids);
	if (ret)
		return 0;
	else
		return 1;
}
EXPORT_SYMBOL_GPL(acpi_is_root_bridge);

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

static void acpi_pci_bridge_scan(struct acpi_device *device)
{
	int status;
	struct acpi_device *child = NULL;

	if (device->flags.bus_address)
		if (device->parent && device->parent->ops.bind) {
			status = device->parent->ops.bind(device);
			if (!status) {
				list_for_each_entry(child, &device->children, node)
					acpi_pci_bridge_scan(child);
			}
		}
}

static u8 OSC_UUID[16] = {0x5B, 0x4D, 0xDB, 0x33, 0xF7, 0x1F, 0x1C, 0x40,
			  0x96, 0x57, 0x74, 0x41, 0xC0, 0x3D, 0xD7, 0x66};

static acpi_status acpi_pci_run_osc(acpi_handle handle,
				    const u32 *capbuf, u32 *retval)
{
	acpi_status status;
	struct acpi_object_list input;
	union acpi_object in_params[4];
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *out_obj;
	u32 errors;

	/* Setting up input parameters */
	input.count = 4;
	input.pointer = in_params;
	in_params[0].type 		= ACPI_TYPE_BUFFER;
	in_params[0].buffer.length 	= 16;
	in_params[0].buffer.pointer	= OSC_UUID;
	in_params[1].type 		= ACPI_TYPE_INTEGER;
	in_params[1].integer.value 	= 1;
	in_params[2].type 		= ACPI_TYPE_INTEGER;
	in_params[2].integer.value	= 3;
	in_params[3].type		= ACPI_TYPE_BUFFER;
	in_params[3].buffer.length 	= 12;
	in_params[3].buffer.pointer 	= (u8 *)capbuf;

	status = acpi_evaluate_object(handle, "_OSC", &input, &output);
	if (ACPI_FAILURE(status))
		return status;

	if (!output.length)
		return AE_NULL_OBJECT;

	out_obj = output.pointer;
	if (out_obj->type != ACPI_TYPE_BUFFER) {
		printk(KERN_DEBUG "_OSC evaluation returned wrong type\n");
		status = AE_TYPE;
		goto out_kfree;
	}
	/* Need to ignore the bit0 in result code */
	errors = *((u32 *)out_obj->buffer.pointer) & ~(1 << 0);
	if (errors) {
		if (errors & OSC_REQUEST_ERROR)
			printk(KERN_DEBUG "_OSC request failed\n");
		if (errors & OSC_INVALID_UUID_ERROR)
			printk(KERN_DEBUG "_OSC invalid UUID\n");
		if (errors & OSC_INVALID_REVISION_ERROR)
			printk(KERN_DEBUG "_OSC invalid revision\n");
		if (errors & OSC_CAPABILITIES_MASK_ERROR) {
			if (capbuf[OSC_QUERY_TYPE] & OSC_QUERY_ENABLE)
				goto out_success;
			printk(KERN_DEBUG
			       "Firmware did not grant requested _OSC control\n");
			status = AE_SUPPORT;
			goto out_kfree;
		}
		status = AE_ERROR;
		goto out_kfree;
	}
out_success:
	*retval = *((u32 *)(out_obj->buffer.pointer + 8));
	status = AE_OK;

out_kfree:
	kfree(output.pointer);
	return status;
}

static acpi_status acpi_pci_query_osc(struct acpi_pci_root *root, u32 flags)
{
	acpi_status status;
	u32 support_set, result, capbuf[3];

	/* do _OSC query for all possible controls */
	support_set = root->osc_support_set | (flags & OSC_SUPPORT_MASKS);
	capbuf[OSC_QUERY_TYPE] = OSC_QUERY_ENABLE;
	capbuf[OSC_SUPPORT_TYPE] = support_set;
	capbuf[OSC_CONTROL_TYPE] = OSC_CONTROL_MASKS;

	status = acpi_pci_run_osc(root->device->handle, capbuf, &result);
	if (ACPI_SUCCESS(status)) {
		root->osc_support_set = support_set;
		root->osc_control_qry = result;
		root->osc_queried = 1;
	}
	return status;
}

static acpi_status acpi_pci_osc_support(struct acpi_pci_root *root, u32 flags)
{
	acpi_status status;
	acpi_handle tmp;

	status = acpi_get_handle(root->device->handle, "_OSC", &tmp);
	if (ACPI_FAILURE(status))
		return status;
	mutex_lock(&osc_lock);
	status = acpi_pci_query_osc(root, flags);
	mutex_unlock(&osc_lock);
	return status;
}

static struct acpi_pci_root *acpi_pci_find_root(acpi_handle handle)
{
	struct acpi_pci_root *root;
	list_for_each_entry(root, &acpi_pci_roots, node) {
		if (root->device->handle == handle)
			return root;
	}
	return NULL;
}

struct acpi_handle_node {
	struct list_head node;
	acpi_handle handle;
};

/**
 * acpi_get_pci_dev - convert ACPI CA handle to struct pci_dev
 * @handle: the handle in question
 *
 * Given an ACPI CA handle, the desired PCI device is located in the
 * list of PCI devices.
 *
 * If the device is found, its reference count is increased and this
 * function returns a pointer to its data structure.  The caller must
 * decrement the reference count by calling pci_dev_put().
 * If no device is found, %NULL is returned.
 */
struct pci_dev *acpi_get_pci_dev(acpi_handle handle)
{
	int dev, fn;
	unsigned long long adr;
	acpi_status status;
	acpi_handle phandle;
	struct pci_bus *pbus;
	struct pci_dev *pdev = NULL;
	struct acpi_handle_node *node, *tmp;
	struct acpi_pci_root *root;
	LIST_HEAD(device_list);

	/*
	 * Walk up the ACPI CA namespace until we reach a PCI root bridge.
	 */
	phandle = handle;
	while (!acpi_is_root_bridge(phandle)) {
		node = kzalloc(sizeof(struct acpi_handle_node), GFP_KERNEL);
		if (!node)
			goto out;

		INIT_LIST_HEAD(&node->node);
		node->handle = phandle;
		list_add(&node->node, &device_list);

		status = acpi_get_parent(phandle, &phandle);
		if (ACPI_FAILURE(status))
			goto out;
	}

	root = acpi_pci_find_root(phandle);
	if (!root)
		goto out;

	pbus = root->bus;

	/*
	 * Now, walk back down the PCI device tree until we return to our
	 * original handle. Assumes that everything between the PCI root
	 * bridge and the device we're looking for must be a P2P bridge.
	 */
	list_for_each_entry(node, &device_list, node) {
		acpi_handle hnd = node->handle;
		status = acpi_evaluate_integer(hnd, "_ADR", NULL, &adr);
		if (ACPI_FAILURE(status))
			goto out;
		dev = (adr >> 16) & 0xffff;
		fn  = adr & 0xffff;

		pdev = pci_get_slot(pbus, PCI_DEVFN(dev, fn));
		if (hnd == handle)
			break;

		pbus = pdev->subordinate;
		pci_dev_put(pdev);
	}
out:
	list_for_each_entry_safe(node, tmp, &device_list, node)
		kfree(node);

	return pdev;
}
EXPORT_SYMBOL_GPL(acpi_get_pci_dev);

/**
 * acpi_pci_osc_control_set - commit requested control to Firmware
 * @handle: acpi_handle for the target ACPI object
 * @flags: driver's requested control bits
 *
 * Attempt to take control from Firmware on requested control bits.
 **/
acpi_status acpi_pci_osc_control_set(acpi_handle handle, u32 flags)
{
	acpi_status status;
	u32 control_req, result, capbuf[3];
	acpi_handle tmp;
	struct acpi_pci_root *root;

	status = acpi_get_handle(handle, "_OSC", &tmp);
	if (ACPI_FAILURE(status))
		return status;

	control_req = (flags & OSC_CONTROL_MASKS);
	if (!control_req)
		return AE_TYPE;

	root = acpi_pci_find_root(handle);
	if (!root)
		return AE_NOT_EXIST;

	mutex_lock(&osc_lock);
	/* No need to evaluate _OSC if the control was already granted. */
	if ((root->osc_control_set & control_req) == control_req)
		goto out;

	/* Need to query controls first before requesting them */
	if (!root->osc_queried) {
		status = acpi_pci_query_osc(root, root->osc_support_set);
		if (ACPI_FAILURE(status))
			goto out;
	}
	if ((root->osc_control_qry & control_req) != control_req) {
		printk(KERN_DEBUG
		       "Firmware did not grant requested _OSC control\n");
		status = AE_SUPPORT;
		goto out;
	}

	capbuf[OSC_QUERY_TYPE] = 0;
	capbuf[OSC_SUPPORT_TYPE] = root->osc_support_set;
	capbuf[OSC_CONTROL_TYPE] = root->osc_control_set | control_req;
	status = acpi_pci_run_osc(handle, capbuf, &result);
	if (ACPI_SUCCESS(status))
		root->osc_control_set = result;
out:
	mutex_unlock(&osc_lock);
	return status;
}
EXPORT_SYMBOL(acpi_pci_osc_control_set);

static int __devinit acpi_pci_root_add(struct acpi_device *device)
{
	int result = 0;
	struct acpi_pci_root *root = NULL;
	struct acpi_pci_root *tmp;
	acpi_status status = AE_OK;
	unsigned long long value = 0;
	acpi_handle handle = NULL;
	struct acpi_device *child;
	u32 flags, base_flags;


	if (!device)
		return -EINVAL;

	root = kzalloc(sizeof(struct acpi_pci_root), GFP_KERNEL);
	if (!root)
		return -ENOMEM;
	INIT_LIST_HEAD(&root->node);

	root->device = device;
	strcpy(acpi_device_name(device), ACPI_PCI_ROOT_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_PCI_ROOT_CLASS);
	device->driver_data = root;

	/*
	 * All supported architectures that use ACPI have support for
	 * PCI domains, so we indicate this in _OSC support capabilities.
	 */
	flags = base_flags = OSC_PCI_SEGMENT_GROUPS_SUPPORT;
	acpi_pci_osc_support(root, flags);

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
	result = acpi_pci_bind_root(device);
	if (result)
		goto end;

	/*
	 * PCI Routing Table
	 * -----------------
	 * Evaluate and parse _PRT, if exists.
	 */
	status = acpi_get_handle(device->handle, METHOD_NAME__PRT, &handle);
	if (ACPI_SUCCESS(status))
		result = acpi_pci_irq_add_prt(device->handle, root->bus);

	/*
	 * Scan and bind all _ADR-Based Devices
	 */
	list_for_each_entry(child, &device->children, node)
		acpi_pci_bridge_scan(child);

	/* Indicate support for various _OSC capabilities. */
	if (pci_ext_cfg_avail(root->bus->self))
		flags |= OSC_EXT_PCI_CONFIG_SUPPORT;
	if (pcie_aspm_enabled())
		flags |= OSC_ACTIVE_STATE_PWR_SUPPORT |
			OSC_CLOCK_PWR_CAPABILITY_SUPPORT;
	if (pci_msi_enabled())
		flags |= OSC_MSI_SUPPORT;
	if (flags != base_flags)
		acpi_pci_osc_support(root, flags);

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

	if (acpi_bus_register_driver(&acpi_pci_root_driver) < 0)
		return -ENODEV;

	return 0;
}

subsys_initcall(acpi_pci_root_init);

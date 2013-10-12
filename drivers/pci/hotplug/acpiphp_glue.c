/*
 * ACPI PCI HotPlug glue functions to ACPI CA subsystem
 *
 * Copyright (C) 2002,2003 Takayoshi Kochi (t-kochi@bq.jp.nec.com)
 * Copyright (C) 2002 Hiroshi Aono (h-aono@ap.jp.nec.com)
 * Copyright (C) 2002,2003 NEC Corporation
 * Copyright (C) 2003-2005 Matthew Wilcox (matthew.wilcox@hp.com)
 * Copyright (C) 2003-2005 Hewlett Packard
 * Copyright (C) 2005 Rajesh Shah (rajesh.shah@intel.com)
 * Copyright (C) 2005 Intel Corporation
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <kristen.c.accardi@intel.com>
 *
 */

/*
 * Lifetime rules for pci_dev:
 *  - The one in acpiphp_bridge has its refcount elevated by pci_get_slot()
 *    when the bridge is scanned and it loses a refcount when the bridge
 *    is removed.
 *  - When a P2P bridge is present, we elevate the refcount on the subordinate
 *    bus. It loses the refcount when the the driver unloads.
 */

#include <linux/init.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/pci-acpi.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/acpi.h>

#include "../pci.h"
#include "acpiphp.h"

static LIST_HEAD(bridge_list);
static DEFINE_MUTEX(bridge_mutex);
static DEFINE_MUTEX(acpiphp_context_lock);

#define MY_NAME "acpiphp_glue"

static void handle_hotplug_event(acpi_handle handle, u32 type, void *data);
static void acpiphp_sanitize_bus(struct pci_bus *bus);
static void acpiphp_set_hpp_values(struct pci_bus *bus);
static void hotplug_event(acpi_handle handle, u32 type, void *data);
static void free_bridge(struct kref *kref);

static void acpiphp_context_handler(acpi_handle handle, void *context)
{
	/* Intentionally empty. */
}

/**
 * acpiphp_init_context - Create hotplug context and grab a reference to it.
 * @handle: ACPI object handle to create the context for.
 *
 * Call under acpiphp_context_lock.
 */
static struct acpiphp_context *acpiphp_init_context(acpi_handle handle)
{
	struct acpiphp_context *context;
	acpi_status status;

	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return NULL;

	context->handle = handle;
	context->refcount = 1;
	status = acpi_attach_data(handle, acpiphp_context_handler, context);
	if (ACPI_FAILURE(status)) {
		kfree(context);
		return NULL;
	}
	return context;
}

/**
 * acpiphp_get_context - Get hotplug context and grab a reference to it.
 * @handle: ACPI object handle to get the context for.
 *
 * Call under acpiphp_context_lock.
 */
static struct acpiphp_context *acpiphp_get_context(acpi_handle handle)
{
	struct acpiphp_context *context = NULL;
	acpi_status status;
	void *data;

	status = acpi_get_data(handle, acpiphp_context_handler, &data);
	if (ACPI_SUCCESS(status)) {
		context = data;
		context->refcount++;
	}
	return context;
}

/**
 * acpiphp_put_context - Drop a reference to ACPI hotplug context.
 * @handle: ACPI object handle to put the context for.
 *
 * The context object is removed if there are no more references to it.
 *
 * Call under acpiphp_context_lock.
 */
static void acpiphp_put_context(struct acpiphp_context *context)
{
	if (--context->refcount)
		return;

	WARN_ON(context->bridge);
	acpi_detach_data(context->handle, acpiphp_context_handler);
	kfree(context);
}

static inline void get_bridge(struct acpiphp_bridge *bridge)
{
	kref_get(&bridge->ref);
}

static inline void put_bridge(struct acpiphp_bridge *bridge)
{
	kref_put(&bridge->ref, free_bridge);
}

static void free_bridge(struct kref *kref)
{
	struct acpiphp_context *context;
	struct acpiphp_bridge *bridge;
	struct acpiphp_slot *slot, *next;
	struct acpiphp_func *func, *tmp;

	mutex_lock(&acpiphp_context_lock);

	bridge = container_of(kref, struct acpiphp_bridge, ref);

	list_for_each_entry_safe(slot, next, &bridge->slots, node) {
		list_for_each_entry_safe(func, tmp, &slot->funcs, sibling)
			acpiphp_put_context(func_to_context(func));

		kfree(slot);
	}

	context = bridge->context;
	/* Root bridges will not have hotplug context. */
	if (context) {
		/* Release the reference taken by acpiphp_enumerate_slots(). */
		put_bridge(context->func.parent);
		context->bridge = NULL;
		acpiphp_put_context(context);
	}

	put_device(&bridge->pci_bus->dev);
	pci_dev_put(bridge->pci_dev);
	kfree(bridge);

	mutex_unlock(&acpiphp_context_lock);
}

/*
 * the _DCK method can do funny things... and sometimes not
 * hah-hah funny.
 *
 * TBD - figure out a way to only call fixups for
 * systems that require them.
 */
static void post_dock_fixups(acpi_handle not_used, u32 event, void *data)
{
	struct acpiphp_context *context = data;
	struct pci_bus *bus = context->func.slot->bus;
	u32 buses;

	if (!bus->self)
		return;

	/* fixup bad _DCK function that rewrites
	 * secondary bridge on slot
	 */
	pci_read_config_dword(bus->self,
			PCI_PRIMARY_BUS,
			&buses);

	if (((buses >> 8) & 0xff) != bus->busn_res.start) {
		buses = (buses & 0xff000000)
			| ((unsigned int)(bus->primary)     <<  0)
			| ((unsigned int)(bus->busn_res.start)   <<  8)
			| ((unsigned int)(bus->busn_res.end) << 16);
		pci_write_config_dword(bus->self, PCI_PRIMARY_BUS, buses);
	}
}


static const struct acpi_dock_ops acpiphp_dock_ops = {
	.fixup = post_dock_fixups,
	.handler = hotplug_event,
};

/* Check whether the PCI device is managed by native PCIe hotplug driver */
static bool device_is_managed_by_native_pciehp(struct pci_dev *pdev)
{
	u32 reg32;
	acpi_handle tmp;
	struct acpi_pci_root *root;

	/* Check whether the PCIe port supports native PCIe hotplug */
	if (pcie_capability_read_dword(pdev, PCI_EXP_SLTCAP, &reg32))
		return false;
	if (!(reg32 & PCI_EXP_SLTCAP_HPC))
		return false;

	/*
	 * Check whether native PCIe hotplug has been enabled for
	 * this PCIe hierarchy.
	 */
	tmp = acpi_find_root_bridge_handle(pdev);
	if (!tmp)
		return false;
	root = acpi_pci_find_root(tmp);
	if (!root)
		return false;
	if (!(root->osc_control_set & OSC_PCI_EXPRESS_NATIVE_HP_CONTROL))
		return false;

	return true;
}

static void acpiphp_dock_init(void *data)
{
	struct acpiphp_context *context = data;

	get_bridge(context->func.parent);
}

static void acpiphp_dock_release(void *data)
{
	struct acpiphp_context *context = data;

	put_bridge(context->func.parent);
}

/* callback routine to register each ACPI PCI slot object */
static acpi_status register_slot(acpi_handle handle, u32 lvl, void *data,
				 void **rv)
{
	struct acpiphp_bridge *bridge = data;
	struct acpiphp_context *context;
	struct acpiphp_slot *slot;
	struct acpiphp_func *newfunc;
	acpi_status status = AE_OK;
	unsigned long long adr;
	int device, function;
	struct pci_bus *pbus = bridge->pci_bus;
	struct pci_dev *pdev = bridge->pci_dev;
	u32 val;

	if (pdev && device_is_managed_by_native_pciehp(pdev))
		return AE_OK;

	status = acpi_evaluate_integer(handle, "_ADR", NULL, &adr);
	if (ACPI_FAILURE(status)) {
		acpi_handle_warn(handle, "can't evaluate _ADR (%#x)\n", status);
		return AE_OK;
	}

	device = (adr >> 16) & 0xffff;
	function = adr & 0xffff;

	mutex_lock(&acpiphp_context_lock);
	context = acpiphp_init_context(handle);
	if (!context) {
		mutex_unlock(&acpiphp_context_lock);
		acpi_handle_err(handle, "No hotplug context\n");
		return AE_NOT_EXIST;
	}
	newfunc = &context->func;
	newfunc->function = function;
	newfunc->parent = bridge;
	mutex_unlock(&acpiphp_context_lock);

	if (acpi_has_method(handle, "_EJ0"))
		newfunc->flags = FUNC_HAS_EJ0;

	if (acpi_has_method(handle, "_STA"))
		newfunc->flags |= FUNC_HAS_STA;

	if (acpi_has_method(handle, "_DCK"))
		newfunc->flags |= FUNC_HAS_DCK;

	/* search for objects that share the same slot */
	list_for_each_entry(slot, &bridge->slots, node)
		if (slot->device == device)
			goto slot_found;

	slot = kzalloc(sizeof(struct acpiphp_slot), GFP_KERNEL);
	if (!slot) {
		status = AE_NO_MEMORY;
		goto err;
	}

	slot->bus = bridge->pci_bus;
	slot->device = device;
	INIT_LIST_HEAD(&slot->funcs);
	mutex_init(&slot->crit_sect);

	list_add_tail(&slot->node, &bridge->slots);

	/* Register slots for ejectable funtions only. */
	if (acpi_pci_check_ejectable(pbus, handle)  || is_dock_device(handle)) {
		unsigned long long sun;
		int retval;

		bridge->nr_slots++;
		status = acpi_evaluate_integer(handle, "_SUN", NULL, &sun);
		if (ACPI_FAILURE(status))
			sun = bridge->nr_slots;

		dbg("found ACPI PCI Hotplug slot %llu at PCI %04x:%02x:%02x\n",
		    sun, pci_domain_nr(pbus), pbus->number, device);

		retval = acpiphp_register_hotplug_slot(slot, sun);
		if (retval) {
			slot->slot = NULL;
			bridge->nr_slots--;
			if (retval == -EBUSY)
				warn("Slot %llu already registered by another "
					"hotplug driver\n", sun);
			else
				warn("acpiphp_register_hotplug_slot failed "
					"(err code = 0x%x)\n", retval);
		}
		/* Even if the slot registration fails, we can still use it. */
	}

 slot_found:
	newfunc->slot = slot;
	list_add_tail(&newfunc->sibling, &slot->funcs);

	if (pci_bus_read_dev_vendor_id(pbus, PCI_DEVFN(device, function),
				       &val, 60*1000))
		slot->flags |= SLOT_ENABLED;

	if (is_dock_device(handle)) {
		/* we don't want to call this device's _EJ0
		 * because we want the dock notify handler
		 * to call it after it calls _DCK
		 */
		newfunc->flags &= ~FUNC_HAS_EJ0;
		if (register_hotplug_dock_device(handle,
			&acpiphp_dock_ops, context,
			acpiphp_dock_init, acpiphp_dock_release))
			dbg("failed to register dock device\n");
	}

	/* install notify handler */
	if (!(newfunc->flags & FUNC_HAS_DCK)) {
		status = acpi_install_notify_handler(handle, ACPI_SYSTEM_NOTIFY,
						     handle_hotplug_event,
						     context);
		if (ACPI_FAILURE(status))
			acpi_handle_err(handle,
					"failed to install notify handler\n");
	}

	return AE_OK;

 err:
	mutex_lock(&acpiphp_context_lock);
	acpiphp_put_context(context);
	mutex_unlock(&acpiphp_context_lock);
	return status;
}

static struct acpiphp_bridge *acpiphp_handle_to_bridge(acpi_handle handle)
{
	struct acpiphp_context *context;
	struct acpiphp_bridge *bridge = NULL;

	mutex_lock(&acpiphp_context_lock);
	context = acpiphp_get_context(handle);
	if (context) {
		bridge = context->bridge;
		if (bridge)
			get_bridge(bridge);

		acpiphp_put_context(context);
	}
	mutex_unlock(&acpiphp_context_lock);
	return bridge;
}

static void cleanup_bridge(struct acpiphp_bridge *bridge)
{
	struct acpiphp_slot *slot;
	struct acpiphp_func *func;
	acpi_status status;

	list_for_each_entry(slot, &bridge->slots, node) {
		list_for_each_entry(func, &slot->funcs, sibling) {
			acpi_handle handle = func_to_handle(func);

			if (is_dock_device(handle))
				unregister_hotplug_dock_device(handle);

			if (!(func->flags & FUNC_HAS_DCK)) {
				status = acpi_remove_notify_handler(handle,
							ACPI_SYSTEM_NOTIFY,
							handle_hotplug_event);
				if (ACPI_FAILURE(status))
					err("failed to remove notify handler\n");
			}
		}
		if (slot->slot)
			acpiphp_unregister_hotplug_slot(slot);
	}

	mutex_lock(&bridge_mutex);
	list_del(&bridge->list);
	mutex_unlock(&bridge_mutex);
}

/**
 * acpiphp_max_busnr - return the highest reserved bus number under the given bus.
 * @bus: bus to start search with
 */
static unsigned char acpiphp_max_busnr(struct pci_bus *bus)
{
	struct list_head *tmp;
	unsigned char max, n;

	/*
	 * pci_bus_max_busnr will return the highest
	 * reserved busnr for all these children.
	 * that is equivalent to the bus->subordinate
	 * value.  We don't want to use the parent's
	 * bus->subordinate value because it could have
	 * padding in it.
	 */
	max = bus->busn_res.start;

	list_for_each(tmp, &bus->children) {
		n = pci_bus_max_busnr(pci_bus_b(tmp));
		if (n > max)
			max = n;
	}
	return max;
}

/**
 * acpiphp_bus_trim - Trim device objects in an ACPI namespace subtree.
 * @handle: ACPI device object handle to start from.
 */
static void acpiphp_bus_trim(acpi_handle handle)
{
	struct acpi_device *adev = NULL;

	acpi_bus_get_device(handle, &adev);
	if (adev)
		acpi_bus_trim(adev);
}

/**
 * acpiphp_bus_add - Scan ACPI namespace subtree.
 * @handle: ACPI object handle to start the scan from.
 */
static void acpiphp_bus_add(acpi_handle handle)
{
	struct acpi_device *adev = NULL;

	acpi_bus_scan(handle);
	acpi_bus_get_device(handle, &adev);
	if (adev)
		acpi_device_set_power(adev, ACPI_STATE_D0);
}

static void acpiphp_set_acpi_region(struct acpiphp_slot *slot)
{
	struct acpiphp_func *func;
	union acpi_object params[2];
	struct acpi_object_list arg_list;

	list_for_each_entry(func, &slot->funcs, sibling) {
		arg_list.count = 2;
		arg_list.pointer = params;
		params[0].type = ACPI_TYPE_INTEGER;
		params[0].integer.value = ACPI_ADR_SPACE_PCI_CONFIG;
		params[1].type = ACPI_TYPE_INTEGER;
		params[1].integer.value = 1;
		/* _REG is optional, we don't care about if there is failure */
		acpi_evaluate_object(func_to_handle(func), "_REG", &arg_list,
				     NULL);
	}
}

static void check_hotplug_bridge(struct acpiphp_slot *slot, struct pci_dev *dev)
{
	struct acpiphp_func *func;

	/* quirk, or pcie could set it already */
	if (dev->is_hotplug_bridge)
		return;

	list_for_each_entry(func, &slot->funcs, sibling) {
		if (PCI_FUNC(dev->devfn) == func->function) {
			dev->is_hotplug_bridge = 1;
			break;
		}
	}
}

static int acpiphp_rescan_slot(struct acpiphp_slot *slot)
{
	struct acpiphp_func *func;

	list_for_each_entry(func, &slot->funcs, sibling)
		acpiphp_bus_add(func_to_handle(func));

	return pci_scan_slot(slot->bus, PCI_DEVFN(slot->device, 0));
}

/**
 * enable_slot - enable, configure a slot
 * @slot: slot to be enabled
 *
 * This function should be called per *physical slot*,
 * not per each slot object in ACPI namespace.
 */
static void __ref enable_slot(struct acpiphp_slot *slot)
{
	struct pci_dev *dev;
	struct pci_bus *bus = slot->bus;
	struct acpiphp_func *func;
	int max, pass;
	LIST_HEAD(add_list);
	int nr_found;

	nr_found = acpiphp_rescan_slot(slot);
	max = acpiphp_max_busnr(bus);
	for (pass = 0; pass < 2; pass++) {
		list_for_each_entry(dev, &bus->devices, bus_list) {
			if (PCI_SLOT(dev->devfn) != slot->device)
				continue;

			if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE ||
			    dev->hdr_type == PCI_HEADER_TYPE_CARDBUS) {
				max = pci_scan_bridge(bus, dev, max, pass);
				if (pass && dev->subordinate) {
					check_hotplug_bridge(slot, dev);
					pcibios_resource_survey_bus(dev->subordinate);
					__pci_bus_size_bridges(dev->subordinate,
							       &add_list);
				}
			}
		}
	}
	__pci_bus_assign_resources(bus, &add_list, NULL);
	/* Nothing more to do here if there are no new devices on this bus. */
	if (!nr_found && (slot->flags & SLOT_ENABLED))
		return;

	acpiphp_sanitize_bus(bus);
	acpiphp_set_hpp_values(bus);
	acpiphp_set_acpi_region(slot);

	list_for_each_entry(dev, &bus->devices, bus_list) {
		/* Assume that newly added devices are powered on already. */
		if (!dev->is_added)
			dev->current_state = PCI_D0;
	}

	pci_bus_add_devices(bus);

	slot->flags |= SLOT_ENABLED;
	list_for_each_entry(func, &slot->funcs, sibling) {
		dev = pci_get_slot(bus, PCI_DEVFN(slot->device,
						  func->function));
		if (!dev) {
			/* Do not set SLOT_ENABLED flag if some funcs
			   are not added. */
			slot->flags &= (~SLOT_ENABLED);
			continue;
		}
	}
}

/* return first device in slot, acquiring a reference on it */
static struct pci_dev *dev_in_slot(struct acpiphp_slot *slot)
{
	struct pci_bus *bus = slot->bus;
	struct pci_dev *dev;
	struct pci_dev *ret = NULL;

	down_read(&pci_bus_sem);
	list_for_each_entry(dev, &bus->devices, bus_list)
		if (PCI_SLOT(dev->devfn) == slot->device) {
			ret = pci_dev_get(dev);
			break;
		}
	up_read(&pci_bus_sem);

	return ret;
}

/**
 * disable_slot - disable a slot
 * @slot: ACPI PHP slot
 */
static void disable_slot(struct acpiphp_slot *slot)
{
	struct acpiphp_func *func;
	struct pci_dev *pdev;

	/*
	 * enable_slot() enumerates all functions in this device via
	 * pci_scan_slot(), whether they have associated ACPI hotplug
	 * methods (_EJ0, etc.) or not.  Therefore, we remove all functions
	 * here.
	 */
	while ((pdev = dev_in_slot(slot))) {
		pci_stop_and_remove_bus_device(pdev);
		pci_dev_put(pdev);
	}

	list_for_each_entry(func, &slot->funcs, sibling)
		acpiphp_bus_trim(func_to_handle(func));

	slot->flags &= (~SLOT_ENABLED);
}


/**
 * get_slot_status - get ACPI slot status
 * @slot: ACPI PHP slot
 *
 * If a slot has _STA for each function and if any one of them
 * returned non-zero status, return it.
 *
 * If a slot doesn't have _STA and if any one of its functions'
 * configuration space is configured, return 0x0f as a _STA.
 *
 * Otherwise return 0.
 */
static unsigned int get_slot_status(struct acpiphp_slot *slot)
{
	unsigned long long sta = 0;
	struct acpiphp_func *func;

	list_for_each_entry(func, &slot->funcs, sibling) {
		if (func->flags & FUNC_HAS_STA) {
			acpi_status status;

			status = acpi_evaluate_integer(func_to_handle(func),
						       "_STA", NULL, &sta);
			if (ACPI_SUCCESS(status) && sta)
				break;
		} else {
			u32 dvid;

			pci_bus_read_config_dword(slot->bus,
						  PCI_DEVFN(slot->device,
							    func->function),
						  PCI_VENDOR_ID, &dvid);
			if (dvid != 0xffffffff) {
				sta = ACPI_STA_ALL;
				break;
			}
		}
	}

	return (unsigned int)sta;
}

/**
 * trim_stale_devices - remove PCI devices that are not responding.
 * @dev: PCI device to start walking the hierarchy from.
 */
static void trim_stale_devices(struct pci_dev *dev)
{
	acpi_handle handle = ACPI_HANDLE(&dev->dev);
	struct pci_bus *bus = dev->subordinate;
	bool alive = false;

	if (handle) {
		acpi_status status;
		unsigned long long sta;

		status = acpi_evaluate_integer(handle, "_STA", NULL, &sta);
		alive = ACPI_SUCCESS(status) && sta == ACPI_STA_ALL;
	}
	if (!alive) {
		u32 v;

		/* Check if the device responds. */
		alive = pci_bus_read_dev_vendor_id(dev->bus, dev->devfn, &v, 0);
	}
	if (!alive) {
		pci_stop_and_remove_bus_device(dev);
		if (handle)
			acpiphp_bus_trim(handle);
	} else if (bus) {
		struct pci_dev *child, *tmp;

		/* The device is a bridge. so check the bus below it. */
		pm_runtime_get_sync(&dev->dev);
		list_for_each_entry_safe(child, tmp, &bus->devices, bus_list)
			trim_stale_devices(child);

		pm_runtime_put(&dev->dev);
	}
}

/**
 * acpiphp_check_bridge - re-enumerate devices
 * @bridge: where to begin re-enumeration
 *
 * Iterate over all slots under this bridge and make sure that if a
 * card is present they are enabled, and if not they are disabled.
 */
static void acpiphp_check_bridge(struct acpiphp_bridge *bridge)
{
	struct acpiphp_slot *slot;

	list_for_each_entry(slot, &bridge->slots, node) {
		struct pci_bus *bus = slot->bus;
		struct pci_dev *dev, *tmp;

		mutex_lock(&slot->crit_sect);
		/* wake up all functions */
		if (get_slot_status(slot) == ACPI_STA_ALL) {
			/* remove stale devices if any */
			list_for_each_entry_safe(dev, tmp, &bus->devices,
						 bus_list)
				if (PCI_SLOT(dev->devfn) == slot->device)
					trim_stale_devices(dev);

			/* configure all functions */
			enable_slot(slot);
		} else {
			disable_slot(slot);
		}
		mutex_unlock(&slot->crit_sect);
	}
}

static void acpiphp_set_hpp_values(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list)
		pci_configure_slot(dev);
}

/*
 * Remove devices for which we could not assign resources, call
 * arch specific code to fix-up the bus
 */
static void acpiphp_sanitize_bus(struct pci_bus *bus)
{
	struct pci_dev *dev, *tmp;
	int i;
	unsigned long type_mask = IORESOURCE_IO | IORESOURCE_MEM;

	list_for_each_entry_safe(dev, tmp, &bus->devices, bus_list) {
		for (i=0; i<PCI_BRIDGE_RESOURCES; i++) {
			struct resource *res = &dev->resource[i];
			if ((res->flags & type_mask) && !res->start &&
					res->end) {
				/* Could not assign a required resources
				 * for this device, remove it */
				pci_stop_and_remove_bus_device(dev);
				break;
			}
		}
	}
}

/*
 * ACPI event handlers
 */

void acpiphp_check_host_bridge(acpi_handle handle)
{
	struct acpiphp_bridge *bridge;

	bridge = acpiphp_handle_to_bridge(handle);
	if (bridge) {
		acpiphp_check_bridge(bridge);
		put_bridge(bridge);
	}
}

static void hotplug_event(acpi_handle handle, u32 type, void *data)
{
	struct acpiphp_context *context = data;
	struct acpiphp_func *func = &context->func;
	struct acpiphp_bridge *bridge;
	char objname[64];
	struct acpi_buffer buffer = { .length = sizeof(objname),
				      .pointer = objname };

	mutex_lock(&acpiphp_context_lock);
	bridge = context->bridge;
	if (bridge)
		get_bridge(bridge);

	mutex_unlock(&acpiphp_context_lock);

	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);

	switch (type) {
	case ACPI_NOTIFY_BUS_CHECK:
		/* bus re-enumerate */
		dbg("%s: Bus check notify on %s\n", __func__, objname);
		dbg("%s: re-enumerating slots under %s\n", __func__, objname);
		if (bridge) {
			acpiphp_check_bridge(bridge);
		} else {
			struct acpiphp_slot *slot = func->slot;

			mutex_lock(&slot->crit_sect);
			enable_slot(slot);
			mutex_unlock(&slot->crit_sect);
		}
		break;

	case ACPI_NOTIFY_DEVICE_CHECK:
		/* device check */
		dbg("%s: Device check notify on %s\n", __func__, objname);
		if (bridge) {
			acpiphp_check_bridge(bridge);
		} else {
			struct acpiphp_slot *slot = func->slot;
			int ret;

			/*
			 * Check if anything has changed in the slot and rescan
			 * from the parent if that's the case.
			 */
			mutex_lock(&slot->crit_sect);
			ret = acpiphp_rescan_slot(slot);
			mutex_unlock(&slot->crit_sect);
			if (ret)
				acpiphp_check_bridge(func->parent);
		}
		break;

	case ACPI_NOTIFY_EJECT_REQUEST:
		/* request device eject */
		dbg("%s: Device eject notify on %s\n", __func__, objname);
		acpiphp_disable_and_eject_slot(func->slot);
		break;
	}

	if (bridge)
		put_bridge(bridge);
}

static void hotplug_event_work(struct work_struct *work)
{
	struct acpiphp_context *context;
	struct acpi_hp_work *hp_work;

	hp_work = container_of(work, struct acpi_hp_work, work);
	context = hp_work->context;
	acpi_scan_lock_acquire();

	hotplug_event(hp_work->handle, hp_work->type, context);

	acpi_scan_lock_release();
	acpi_evaluate_hotplug_ost(hp_work->handle, hp_work->type,
				  ACPI_OST_SC_SUCCESS, NULL);
	kfree(hp_work); /* allocated in handle_hotplug_event() */
	put_bridge(context->func.parent);
}

/**
 * handle_hotplug_event - handle ACPI hotplug event
 * @handle: Notify()'ed acpi_handle
 * @type: Notify code
 * @data: pointer to acpiphp_context structure
 *
 * Handles ACPI event notification on slots.
 */
static void handle_hotplug_event(acpi_handle handle, u32 type, void *data)
{
	struct acpiphp_context *context;
	u32 ost_code = ACPI_OST_SC_SUCCESS;

	switch (type) {
	case ACPI_NOTIFY_BUS_CHECK:
	case ACPI_NOTIFY_DEVICE_CHECK:
		break;
	case ACPI_NOTIFY_EJECT_REQUEST:
		ost_code = ACPI_OST_SC_EJECT_IN_PROGRESS;
		acpi_evaluate_hotplug_ost(handle, type, ost_code, NULL);
		break;

	case ACPI_NOTIFY_DEVICE_WAKE:
		return;

	case ACPI_NOTIFY_FREQUENCY_MISMATCH:
		acpi_handle_err(handle, "Device cannot be configured due "
				"to a frequency mismatch\n");
		goto out;

	case ACPI_NOTIFY_BUS_MODE_MISMATCH:
		acpi_handle_err(handle, "Device cannot be configured due "
				"to a bus mode mismatch\n");
		goto out;

	case ACPI_NOTIFY_POWER_FAULT:
		acpi_handle_err(handle, "Device has suffered a power fault\n");
		goto out;

	default:
		acpi_handle_warn(handle, "Unsupported event type 0x%x\n", type);
		ost_code = ACPI_OST_SC_UNRECOGNIZED_NOTIFY;
		goto out;
	}

	mutex_lock(&acpiphp_context_lock);
	context = acpiphp_get_context(handle);
	if (context) {
		get_bridge(context->func.parent);
		acpiphp_put_context(context);
		alloc_acpi_hp_work(handle, type, context, hotplug_event_work);
		mutex_unlock(&acpiphp_context_lock);
		return;
	}
	mutex_unlock(&acpiphp_context_lock);
	ost_code = ACPI_OST_SC_NON_SPECIFIC_FAILURE;

 out:
	acpi_evaluate_hotplug_ost(handle, type, ost_code, NULL);
}

/*
 * Create hotplug slots for the PCI bus.
 * It should always return 0 to avoid skipping following notifiers.
 */
void acpiphp_enumerate_slots(struct pci_bus *bus)
{
	struct acpiphp_bridge *bridge;
	acpi_handle handle;
	acpi_status status;

	if (acpiphp_disabled)
		return;

	handle = ACPI_HANDLE(bus->bridge);
	if (!handle)
		return;

	bridge = kzalloc(sizeof(struct acpiphp_bridge), GFP_KERNEL);
	if (!bridge) {
		acpi_handle_err(handle, "No memory for bridge object\n");
		return;
	}

	INIT_LIST_HEAD(&bridge->slots);
	kref_init(&bridge->ref);
	bridge->pci_dev = pci_dev_get(bus->self);
	bridge->pci_bus = bus;

	/*
	 * Grab a ref to the subordinate PCI bus in case the bus is
	 * removed via PCI core logical hotplug. The ref pins the bus
	 * (which we access during module unload).
	 */
	get_device(&bus->dev);

	if (!pci_is_root_bus(bridge->pci_bus)) {
		struct acpiphp_context *context;

		/*
		 * This bridge should have been registered as a hotplug function
		 * under its parent, so the context should be there, unless the
		 * parent is going to be handled by pciehp, in which case this
		 * bridge is not interesting to us either.
		 */
		mutex_lock(&acpiphp_context_lock);
		context = acpiphp_get_context(handle);
		if (!context) {
			mutex_unlock(&acpiphp_context_lock);
			put_device(&bus->dev);
			pci_dev_put(bridge->pci_dev);
			kfree(bridge);
			return;
		}
		bridge->context = context;
		context->bridge = bridge;
		/* Get a reference to the parent bridge. */
		get_bridge(context->func.parent);
		mutex_unlock(&acpiphp_context_lock);
	}

	/* must be added to the list prior to calling register_slot */
	mutex_lock(&bridge_mutex);
	list_add(&bridge->list, &bridge_list);
	mutex_unlock(&bridge_mutex);

	/* register all slot objects under this bridge */
	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, 1,
				     register_slot, NULL, bridge, NULL);
	if (ACPI_FAILURE(status)) {
		acpi_handle_err(handle, "failed to register slots\n");
		cleanup_bridge(bridge);
		put_bridge(bridge);
	}
}

/* Destroy hotplug slots associated with the PCI bus */
void acpiphp_remove_slots(struct pci_bus *bus)
{
	struct acpiphp_bridge *bridge;

	if (acpiphp_disabled)
		return;

	mutex_lock(&bridge_mutex);
	list_for_each_entry(bridge, &bridge_list, list)
		if (bridge->pci_bus == bus) {
			mutex_unlock(&bridge_mutex);
			cleanup_bridge(bridge);
			put_bridge(bridge);
			return;
		}

	mutex_unlock(&bridge_mutex);
}

/**
 * acpiphp_enable_slot - power on slot
 * @slot: ACPI PHP slot
 */
int acpiphp_enable_slot(struct acpiphp_slot *slot)
{
	mutex_lock(&slot->crit_sect);
	/* configure all functions */
	if (!(slot->flags & SLOT_ENABLED))
		enable_slot(slot);

	mutex_unlock(&slot->crit_sect);
	return 0;
}

/**
 * acpiphp_disable_and_eject_slot - power off and eject slot
 * @slot: ACPI PHP slot
 */
int acpiphp_disable_and_eject_slot(struct acpiphp_slot *slot)
{
	struct acpiphp_func *func;
	int retval = 0;

	mutex_lock(&slot->crit_sect);

	/* unconfigure all functions */
	disable_slot(slot);

	list_for_each_entry(func, &slot->funcs, sibling)
		if (func->flags & FUNC_HAS_EJ0) {
			acpi_handle handle = func_to_handle(func);

			if (ACPI_FAILURE(acpi_evaluate_ej0(handle)))
				acpi_handle_err(handle, "_EJ0 failed\n");

			break;
		}

	mutex_unlock(&slot->crit_sect);
	return retval;
}


/*
 * slot enabled:  1
 * slot disabled: 0
 */
u8 acpiphp_get_power_status(struct acpiphp_slot *slot)
{
	return (slot->flags & SLOT_ENABLED);
}


/*
 * latch   open:  1
 * latch closed:  0
 */
u8 acpiphp_get_latch_status(struct acpiphp_slot *slot)
{
	return !(get_slot_status(slot) & ACPI_STA_DEVICE_UI);
}


/*
 * adapter presence : 1
 *          absence : 0
 */
u8 acpiphp_get_adapter_status(struct acpiphp_slot *slot)
{
	return !!get_slot_status(slot);
}

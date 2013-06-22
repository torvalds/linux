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
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/acpi.h>

#include "../pci.h"
#include "acpiphp.h"

static LIST_HEAD(bridge_list);
static DEFINE_MUTEX(bridge_mutex);

#define MY_NAME "acpiphp_glue"

static void handle_hotplug_event_bridge (acpi_handle, u32, void *);
static void acpiphp_sanitize_bus(struct pci_bus *bus);
static void acpiphp_set_hpp_values(struct pci_bus *bus);
static void handle_hotplug_event_func(acpi_handle handle, u32 type, void *context);
static void free_bridge(struct kref *kref);

/* callback routine to check for the existence of a pci dock device */
static acpi_status
is_pci_dock_device(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	int *count = (int *)context;

	if (is_dock_device(handle)) {
		(*count)++;
		return AE_CTRL_TERMINATE;
	} else {
		return AE_OK;
	}
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
	struct acpiphp_bridge *bridge;
	struct acpiphp_slot *slot, *next;
	struct acpiphp_func *func, *tmp;

	bridge = container_of(kref, struct acpiphp_bridge, ref);

	list_for_each_entry_safe(slot, next, &bridge->slots, node) {
		list_for_each_entry_safe(func, tmp, &slot->funcs, sibling) {
			kfree(func);
		}
		kfree(slot);
	}

	/* Release reference acquired by acpiphp_bridge_handle_to_function() */
	if ((bridge->flags & BRIDGE_HAS_EJ0) && bridge->func)
		put_bridge(bridge->func->slot->bridge);
	put_device(&bridge->pci_bus->dev);
	pci_dev_put(bridge->pci_dev);
	kfree(bridge);
}

/*
 * the _DCK method can do funny things... and sometimes not
 * hah-hah funny.
 *
 * TBD - figure out a way to only call fixups for
 * systems that require them.
 */
static int post_dock_fixups(struct notifier_block *nb, unsigned long val,
	void *v)
{
	struct acpiphp_func *func = container_of(nb, struct acpiphp_func, nb);
	struct pci_bus *bus = func->slot->bridge->pci_bus;
	u32 buses;

	if (!bus->self)
		return  NOTIFY_OK;

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
	return NOTIFY_OK;
}


static const struct acpi_dock_ops acpiphp_dock_ops = {
	.handler = handle_hotplug_event_func,
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

/* callback routine to register each ACPI PCI slot object */
static acpi_status
register_slot(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	struct acpiphp_bridge *bridge = (struct acpiphp_bridge *)context;
	struct acpiphp_slot *slot;
	struct acpiphp_func *newfunc;
	acpi_handle tmp;
	acpi_status status = AE_OK;
	unsigned long long adr, sun;
	int device, function, retval, found = 0;
	struct pci_bus *pbus = bridge->pci_bus;
	struct pci_dev *pdev;
	u32 val;

	if (!acpi_pci_check_ejectable(pbus, handle) && !is_dock_device(handle))
		return AE_OK;

	status = acpi_evaluate_integer(handle, "_ADR", NULL, &adr);
	if (ACPI_FAILURE(status)) {
		warn("can't evaluate _ADR (%#x)\n", status);
		return AE_OK;
	}

	device = (adr >> 16) & 0xffff;
	function = adr & 0xffff;

	pdev = bridge->pci_dev;
	if (pdev && device_is_managed_by_native_pciehp(pdev))
		return AE_OK;

	newfunc = kzalloc(sizeof(struct acpiphp_func), GFP_KERNEL);
	if (!newfunc)
		return AE_NO_MEMORY;

	newfunc->handle = handle;
	newfunc->function = function;

	if (ACPI_SUCCESS(acpi_get_handle(handle, "_EJ0", &tmp)))
		newfunc->flags = FUNC_HAS_EJ0;

	if (ACPI_SUCCESS(acpi_get_handle(handle, "_STA", &tmp)))
		newfunc->flags |= FUNC_HAS_STA;

	if (ACPI_SUCCESS(acpi_get_handle(handle, "_PS0", &tmp)))
		newfunc->flags |= FUNC_HAS_PS0;

	if (ACPI_SUCCESS(acpi_get_handle(handle, "_PS3", &tmp)))
		newfunc->flags |= FUNC_HAS_PS3;

	if (ACPI_SUCCESS(acpi_get_handle(handle, "_DCK", &tmp)))
		newfunc->flags |= FUNC_HAS_DCK;

	status = acpi_evaluate_integer(handle, "_SUN", NULL, &sun);
	if (ACPI_FAILURE(status)) {
		/*
		 * use the count of the number of slots we've found
		 * for the number of the slot
		 */
		sun = bridge->nr_slots+1;
	}

	/* search for objects that share the same slot */
	list_for_each_entry(slot, &bridge->slots, node)
		if (slot->device == device) {
			if (slot->sun != sun)
				warn("sibling found, but _SUN doesn't match!\n");
			found = 1;
			break;
		}

	if (!found) {
		slot = kzalloc(sizeof(struct acpiphp_slot), GFP_KERNEL);
		if (!slot) {
			kfree(newfunc);
			return AE_NO_MEMORY;
		}

		slot->bridge = bridge;
		slot->device = device;
		slot->sun = sun;
		INIT_LIST_HEAD(&slot->funcs);
		mutex_init(&slot->crit_sect);

		mutex_lock(&bridge_mutex);
		list_add_tail(&slot->node, &bridge->slots);
		mutex_unlock(&bridge_mutex);
		bridge->nr_slots++;

		dbg("found ACPI PCI Hotplug slot %llu at PCI %04x:%02x:%02x\n",
		    slot->sun, pci_domain_nr(pbus), pbus->number, device);
		retval = acpiphp_register_hotplug_slot(slot);
		if (retval) {
			if (retval == -EBUSY)
				warn("Slot %llu already registered by another "
					"hotplug driver\n", slot->sun);
			else
				warn("acpiphp_register_hotplug_slot failed "
					"(err code = 0x%x)\n", retval);
			goto err_exit;
		}
	}

	newfunc->slot = slot;
	mutex_lock(&bridge_mutex);
	list_add_tail(&newfunc->sibling, &slot->funcs);
	mutex_unlock(&bridge_mutex);

	if (pci_bus_read_dev_vendor_id(pbus, PCI_DEVFN(device, function),
				       &val, 60*1000))
		slot->flags |= (SLOT_ENABLED | SLOT_POWEREDON);

	if (is_dock_device(handle)) {
		/* we don't want to call this device's _EJ0
		 * because we want the dock notify handler
		 * to call it after it calls _DCK
		 */
		newfunc->flags &= ~FUNC_HAS_EJ0;
		if (register_hotplug_dock_device(handle,
			&acpiphp_dock_ops, newfunc))
			dbg("failed to register dock device\n");

		/* we need to be notified when dock events happen
		 * outside of the hotplug operation, since we may
		 * need to do fixups before we can hotplug.
		 */
		newfunc->nb.notifier_call = post_dock_fixups;
		if (register_dock_notifier(&newfunc->nb))
			dbg("failed to register a dock notifier");
	}

	/* install notify handler */
	if (!(newfunc->flags & FUNC_HAS_DCK)) {
		status = acpi_install_notify_handler(handle,
					     ACPI_SYSTEM_NOTIFY,
					     handle_hotplug_event_func,
					     newfunc);

		if (ACPI_FAILURE(status))
			err("failed to register interrupt notify handler\n");
	} else
		status = AE_OK;

	return status;

 err_exit:
	bridge->nr_slots--;
	mutex_lock(&bridge_mutex);
	list_del(&slot->node);
	mutex_unlock(&bridge_mutex);
	kfree(slot);
	kfree(newfunc);

	return AE_OK;
}


/* see if it's worth looking at this bridge */
static int detect_ejectable_slots(acpi_handle handle)
{
	int found = acpi_pci_detect_ejectable(handle);
	if (!found) {
		acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, (u32)1,
				    is_pci_dock_device, NULL, (void *)&found, NULL);
	}
	return found;
}

/* initialize miscellaneous stuff for both root and PCI-to-PCI bridge */
static void init_bridge_misc(struct acpiphp_bridge *bridge)
{
	acpi_status status;

	/* must be added to the list prior to calling register_slot */
	mutex_lock(&bridge_mutex);
	list_add(&bridge->list, &bridge_list);
	mutex_unlock(&bridge_mutex);

	/* register all slot objects under this bridge */
	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, bridge->handle, (u32)1,
				     register_slot, NULL, bridge, NULL);
	if (ACPI_FAILURE(status)) {
		mutex_lock(&bridge_mutex);
		list_del(&bridge->list);
		mutex_unlock(&bridge_mutex);
		return;
	}

	/* install notify handler for P2P bridges */
	if (!pci_is_root_bus(bridge->pci_bus)) {
		if ((bridge->flags & BRIDGE_HAS_EJ0) && bridge->func) {
			status = acpi_remove_notify_handler(bridge->func->handle,
						ACPI_SYSTEM_NOTIFY,
						handle_hotplug_event_func);
			if (ACPI_FAILURE(status))
				err("failed to remove notify handler\n");
		}
		status = acpi_install_notify_handler(bridge->handle,
					     ACPI_SYSTEM_NOTIFY,
					     handle_hotplug_event_bridge,
					     bridge);

		if (ACPI_FAILURE(status)) {
			err("failed to register interrupt notify handler\n");
		}
	}
}


/* find acpiphp_func from acpiphp_bridge */
static struct acpiphp_func *acpiphp_bridge_handle_to_function(acpi_handle handle)
{
	struct acpiphp_bridge *bridge;
	struct acpiphp_slot *slot;
	struct acpiphp_func *func = NULL;

	mutex_lock(&bridge_mutex);
	list_for_each_entry(bridge, &bridge_list, list) {
		list_for_each_entry(slot, &bridge->slots, node) {
			list_for_each_entry(func, &slot->funcs, sibling) {
				if (func->handle == handle) {
					get_bridge(func->slot->bridge);
					mutex_unlock(&bridge_mutex);
					return func;
				}
			}
		}
	}
	mutex_unlock(&bridge_mutex);

	return NULL;
}


static struct acpiphp_bridge *acpiphp_handle_to_bridge(acpi_handle handle)
{
	struct acpiphp_bridge *bridge;

	mutex_lock(&bridge_mutex);
	list_for_each_entry(bridge, &bridge_list, list)
		if (bridge->handle == handle) {
			get_bridge(bridge);
			mutex_unlock(&bridge_mutex);
			return bridge;
		}
	mutex_unlock(&bridge_mutex);

	return NULL;
}

static void cleanup_bridge(struct acpiphp_bridge *bridge)
{
	struct acpiphp_slot *slot;
	struct acpiphp_func *func;
	acpi_status status;
	acpi_handle handle = bridge->handle;

	if (!pci_is_root_bus(bridge->pci_bus)) {
		status = acpi_remove_notify_handler(handle,
					    ACPI_SYSTEM_NOTIFY,
					    handle_hotplug_event_bridge);
		if (ACPI_FAILURE(status))
			err("failed to remove notify handler\n");
	}

	if ((bridge->flags & BRIDGE_HAS_EJ0) && bridge->func) {
		status = acpi_install_notify_handler(bridge->func->handle,
						ACPI_SYSTEM_NOTIFY,
						handle_hotplug_event_func,
						bridge->func);
		if (ACPI_FAILURE(status))
			err("failed to install interrupt notify handler\n");
	}

	list_for_each_entry(slot, &bridge->slots, node) {
		list_for_each_entry(func, &slot->funcs, sibling) {
			if (is_dock_device(func->handle)) {
				unregister_hotplug_dock_device(func->handle);
				unregister_dock_notifier(&func->nb);
			}
			if (!(func->flags & FUNC_HAS_DCK)) {
				status = acpi_remove_notify_handler(func->handle,
						ACPI_SYSTEM_NOTIFY,
						handle_hotplug_event_func);
				if (ACPI_FAILURE(status))
					err("failed to remove notify handler\n");
			}
		}
		acpiphp_unregister_hotplug_slot(slot);
	}

	mutex_lock(&bridge_mutex);
	list_del(&bridge->list);
	mutex_unlock(&bridge_mutex);
}

static int power_on_slot(struct acpiphp_slot *slot)
{
	acpi_status status;
	struct acpiphp_func *func;
	int retval = 0;

	/* if already enabled, just skip */
	if (slot->flags & SLOT_POWEREDON)
		goto err_exit;

	list_for_each_entry(func, &slot->funcs, sibling) {
		if (func->flags & FUNC_HAS_PS0) {
			dbg("%s: executing _PS0\n", __func__);
			status = acpi_evaluate_object(func->handle, "_PS0", NULL, NULL);
			if (ACPI_FAILURE(status)) {
				warn("%s: _PS0 failed\n", __func__);
				retval = -1;
				goto err_exit;
			} else
				break;
		}
	}

	/* TBD: evaluate _STA to check if the slot is enabled */

	slot->flags |= SLOT_POWEREDON;

 err_exit:
	return retval;
}


static int power_off_slot(struct acpiphp_slot *slot)
{
	acpi_status status;
	struct acpiphp_func *func;

	int retval = 0;

	/* if already disabled, just skip */
	if ((slot->flags & SLOT_POWEREDON) == 0)
		goto err_exit;

	list_for_each_entry(func, &slot->funcs, sibling) {
		if (func->flags & FUNC_HAS_PS3) {
			status = acpi_evaluate_object(func->handle, "_PS3", NULL, NULL);
			if (ACPI_FAILURE(status)) {
				warn("%s: _PS3 failed\n", __func__);
				retval = -1;
				goto err_exit;
			} else
				break;
		}
	}

	/* TBD: evaluate _STA to check if the slot is disabled */

	slot->flags &= (~SLOT_POWEREDON);

 err_exit:
	return retval;
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
 * acpiphp_bus_add - add a new bus to acpi subsystem
 * @func: acpiphp_func of the bridge
 */
static int acpiphp_bus_add(struct acpiphp_func *func)
{
	struct acpi_device *device;
	int ret_val;

	if (!acpi_bus_get_device(func->handle, &device)) {
		dbg("bus exists... trim\n");
		/* this shouldn't be in here, so remove
		 * the bus then re-add it...
		 */
		acpi_bus_trim(device);
	}

	ret_val = acpi_bus_scan(func->handle);
	if (!ret_val)
		ret_val = acpi_bus_get_device(func->handle, &device);

	if (ret_val)
		dbg("error adding bus, %x\n", -ret_val);

	return ret_val;
}


/**
 * acpiphp_bus_trim - trim a bus from acpi subsystem
 * @handle: handle to acpi namespace
 */
static int acpiphp_bus_trim(acpi_handle handle)
{
	struct acpi_device *device;
	int retval;

	retval = acpi_bus_get_device(handle, &device);
	if (retval) {
		dbg("acpi_device not found\n");
		return retval;
	}

	acpi_bus_trim(device);
	return 0;
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
		acpi_evaluate_object(func->handle, "_REG", &arg_list, NULL);
	}
}

static void check_hotplug_bridge(struct acpiphp_slot *slot, struct pci_dev *dev)
{
	struct acpiphp_func *func;

	if (!dev->subordinate)
		return;

	/* quirk, or pcie could set it already */
	if (dev->is_hotplug_bridge)
		return;

	if (PCI_SLOT(dev->devfn) != slot->device)
		return;

	list_for_each_entry(func, &slot->funcs, sibling) {
		if (PCI_FUNC(dev->devfn) == func->function) {
			/* check if this bridge has ejectable slots */
			if ((detect_ejectable_slots(func->handle) > 0))
				dev->is_hotplug_bridge = 1;
			break;
		}
	}
}

/**
 * enable_device - enable, configure a slot
 * @slot: slot to be enabled
 *
 * This function should be called per *physical slot*,
 * not per each slot object in ACPI namespace.
 */
static int __ref enable_device(struct acpiphp_slot *slot)
{
	struct pci_dev *dev;
	struct pci_bus *bus = slot->bridge->pci_bus;
	struct acpiphp_func *func;
	int num, max, pass;
	LIST_HEAD(add_list);

	if (slot->flags & SLOT_ENABLED)
		goto err_exit;

	list_for_each_entry(func, &slot->funcs, sibling)
		acpiphp_bus_add(func);

	num = pci_scan_slot(bus, PCI_DEVFN(slot->device, 0));
	if (num == 0) {
		/* Maybe only part of funcs are added. */
		dbg("No new device found\n");
		goto err_exit;
	}

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
	acpiphp_sanitize_bus(bus);
	acpiphp_set_hpp_values(bus);
	acpiphp_set_acpi_region(slot);
	pci_enable_bridges(bus);

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


 err_exit:
	return 0;
}

/* return first device in slot, acquiring a reference on it */
static struct pci_dev *dev_in_slot(struct acpiphp_slot *slot)
{
	struct pci_bus *bus = slot->bridge->pci_bus;
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
 * disable_device - disable a slot
 * @slot: ACPI PHP slot
 */
static int disable_device(struct acpiphp_slot *slot)
{
	struct acpiphp_func *func;
	struct pci_dev *pdev;

	/*
	 * enable_device() enumerates all functions in this device via
	 * pci_scan_slot(), whether they have associated ACPI hotplug
	 * methods (_EJ0, etc.) or not.  Therefore, we remove all functions
	 * here.
	 */
	while ((pdev = dev_in_slot(slot))) {
		pci_stop_and_remove_bus_device(pdev);
		pci_dev_put(pdev);
	}

	list_for_each_entry(func, &slot->funcs, sibling) {
		acpiphp_bus_trim(func->handle);
	}

	slot->flags &= (~SLOT_ENABLED);

	return 0;
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
	acpi_status status;
	unsigned long long sta = 0;
	u32 dvid;
	struct acpiphp_func *func;

	list_for_each_entry(func, &slot->funcs, sibling) {
		if (func->flags & FUNC_HAS_STA) {
			status = acpi_evaluate_integer(func->handle, "_STA", NULL, &sta);
			if (ACPI_SUCCESS(status) && sta)
				break;
		} else {
			pci_bus_read_config_dword(slot->bridge->pci_bus,
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
 * acpiphp_eject_slot - physically eject the slot
 * @slot: ACPI PHP slot
 */
int acpiphp_eject_slot(struct acpiphp_slot *slot)
{
	acpi_status status;
	struct acpiphp_func *func;
	struct acpi_object_list arg_list;
	union acpi_object arg;

	list_for_each_entry(func, &slot->funcs, sibling) {
		/* We don't want to call _EJ0 on non-existing functions. */
		if ((func->flags & FUNC_HAS_EJ0)) {
			/* _EJ0 method take one argument */
			arg_list.count = 1;
			arg_list.pointer = &arg;
			arg.type = ACPI_TYPE_INTEGER;
			arg.integer.value = 1;

			status = acpi_evaluate_object(func->handle, "_EJ0", &arg_list, NULL);
			if (ACPI_FAILURE(status)) {
				warn("%s: _EJ0 failed\n", __func__);
				return -1;
			} else
				break;
		}
	}
	return 0;
}

/**
 * acpiphp_check_bridge - re-enumerate devices
 * @bridge: where to begin re-enumeration
 *
 * Iterate over all slots under this bridge and make sure that if a
 * card is present they are enabled, and if not they are disabled.
 */
static int acpiphp_check_bridge(struct acpiphp_bridge *bridge)
{
	struct acpiphp_slot *slot;
	int retval = 0;
	int enabled, disabled;

	enabled = disabled = 0;

	list_for_each_entry(slot, &bridge->slots, node) {
		unsigned int status = get_slot_status(slot);
		if (slot->flags & SLOT_ENABLED) {
			if (status == ACPI_STA_ALL)
				continue;
			retval = acpiphp_disable_slot(slot);
			if (retval) {
				err("Error occurred in disabling\n");
				goto err_exit;
			} else {
				acpiphp_eject_slot(slot);
			}
			disabled++;
		} else {
			if (status != ACPI_STA_ALL)
				continue;
			retval = acpiphp_enable_slot(slot);
			if (retval) {
				err("Error occurred in enabling\n");
				goto err_exit;
			}
			enabled++;
		}
	}

	dbg("%s: %d enabled, %d disabled\n", __func__, enabled, disabled);

 err_exit:
	return retval;
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

static acpi_status
check_sub_bridges(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	struct acpiphp_bridge *bridge;
	char objname[64];
	struct acpi_buffer buffer = { .length = sizeof(objname),
				      .pointer = objname };

	bridge = acpiphp_handle_to_bridge(handle);
	if (bridge) {
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
		dbg("%s: re-enumerating slots under %s\n",
			__func__, objname);
		acpiphp_check_bridge(bridge);
		put_bridge(bridge);
	}
	return AE_OK ;
}

void acpiphp_check_host_bridge(acpi_handle handle)
{
	struct acpiphp_bridge *bridge;

	bridge = acpiphp_handle_to_bridge(handle);
	if (bridge) {
		acpiphp_check_bridge(bridge);
		put_bridge(bridge);
	}

	acpi_walk_namespace(ACPI_TYPE_DEVICE, handle,
		ACPI_UINT32_MAX, check_sub_bridges, NULL, NULL, NULL);
}

static void _handle_hotplug_event_bridge(struct work_struct *work)
{
	struct acpiphp_bridge *bridge;
	char objname[64];
	struct acpi_buffer buffer = { .length = sizeof(objname),
				      .pointer = objname };
	struct acpi_hp_work *hp_work;
	acpi_handle handle;
	u32 type;

	hp_work = container_of(work, struct acpi_hp_work, work);
	handle = hp_work->handle;
	type = hp_work->type;
	bridge = (struct acpiphp_bridge *)hp_work->context;

	acpi_scan_lock_acquire();

	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);

	switch (type) {
	case ACPI_NOTIFY_BUS_CHECK:
		/* bus re-enumerate */
		dbg("%s: Bus check notify on %s\n", __func__, objname);
		dbg("%s: re-enumerating slots under %s\n", __func__, objname);
		acpiphp_check_bridge(bridge);
		acpi_walk_namespace(ACPI_TYPE_DEVICE, handle,
			ACPI_UINT32_MAX, check_sub_bridges, NULL, NULL, NULL);
		break;

	case ACPI_NOTIFY_DEVICE_CHECK:
		/* device check */
		dbg("%s: Device check notify on %s\n", __func__, objname);
		acpiphp_check_bridge(bridge);
		break;

	case ACPI_NOTIFY_DEVICE_WAKE:
		/* wake event */
		dbg("%s: Device wake notify on %s\n", __func__, objname);
		break;

	case ACPI_NOTIFY_EJECT_REQUEST:
		/* request device eject */
		dbg("%s: Device eject notify on %s\n", __func__, objname);
		if ((bridge->flags & BRIDGE_HAS_EJ0) && bridge->func) {
			struct acpiphp_slot *slot;
			slot = bridge->func->slot;
			if (!acpiphp_disable_slot(slot))
				acpiphp_eject_slot(slot);
		}
		break;

	case ACPI_NOTIFY_FREQUENCY_MISMATCH:
		printk(KERN_ERR "Device %s cannot be configured due"
				" to a frequency mismatch\n", objname);
		break;

	case ACPI_NOTIFY_BUS_MODE_MISMATCH:
		printk(KERN_ERR "Device %s cannot be configured due"
				" to a bus mode mismatch\n", objname);
		break;

	case ACPI_NOTIFY_POWER_FAULT:
		printk(KERN_ERR "Device %s has suffered a power fault\n",
				objname);
		break;

	default:
		warn("notify_handler: unknown event type 0x%x for %s\n", type, objname);
		break;
	}

	acpi_scan_lock_release();
	kfree(hp_work); /* allocated in handle_hotplug_event_bridge */
	put_bridge(bridge);
}

/**
 * handle_hotplug_event_bridge - handle ACPI event on bridges
 * @handle: Notify()'ed acpi_handle
 * @type: Notify code
 * @context: pointer to acpiphp_bridge structure
 *
 * Handles ACPI event notification on {host,p2p} bridges.
 */
static void handle_hotplug_event_bridge(acpi_handle handle, u32 type,
					void *context)
{
	struct acpiphp_bridge *bridge = context;

	/*
	 * Currently the code adds all hotplug events to the kacpid_wq
	 * queue when it should add hotplug events to the kacpi_hotplug_wq.
	 * The proper way to fix this is to reorganize the code so that
	 * drivers (dock, etc.) do not call acpi_os_execute(), etc.
	 * For now just re-add this work to the kacpi_hotplug_wq so we
	 * don't deadlock on hotplug actions.
	 */
	get_bridge(bridge);
	alloc_acpi_hp_work(handle, type, context, _handle_hotplug_event_bridge);
}

static void _handle_hotplug_event_func(struct work_struct *work)
{
	struct acpiphp_func *func;
	char objname[64];
	struct acpi_buffer buffer = { .length = sizeof(objname),
				      .pointer = objname };
	struct acpi_hp_work *hp_work;
	acpi_handle handle;
	u32 type;

	hp_work = container_of(work, struct acpi_hp_work, work);
	handle = hp_work->handle;
	type = hp_work->type;
	func = (struct acpiphp_func *)hp_work->context;

	acpi_scan_lock_acquire();

	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);

	switch (type) {
	case ACPI_NOTIFY_BUS_CHECK:
		/* bus re-enumerate */
		dbg("%s: Bus check notify on %s\n", __func__, objname);
		acpiphp_enable_slot(func->slot);
		break;

	case ACPI_NOTIFY_DEVICE_CHECK:
		/* device check : re-enumerate from parent bus */
		dbg("%s: Device check notify on %s\n", __func__, objname);
		acpiphp_check_bridge(func->slot->bridge);
		break;

	case ACPI_NOTIFY_DEVICE_WAKE:
		/* wake event */
		dbg("%s: Device wake notify on %s\n", __func__, objname);
		break;

	case ACPI_NOTIFY_EJECT_REQUEST:
		/* request device eject */
		dbg("%s: Device eject notify on %s\n", __func__, objname);
		if (!(acpiphp_disable_slot(func->slot)))
			acpiphp_eject_slot(func->slot);
		break;

	default:
		warn("notify_handler: unknown event type 0x%x for %s\n", type, objname);
		break;
	}

	acpi_scan_lock_release();
	kfree(hp_work); /* allocated in handle_hotplug_event_func */
	put_bridge(func->slot->bridge);
}

/**
 * handle_hotplug_event_func - handle ACPI event on functions (i.e. slots)
 * @handle: Notify()'ed acpi_handle
 * @type: Notify code
 * @context: pointer to acpiphp_func structure
 *
 * Handles ACPI event notification on slots.
 */
static void handle_hotplug_event_func(acpi_handle handle, u32 type,
				      void *context)
{
	struct acpiphp_func *func = context;

	/*
	 * Currently the code adds all hotplug events to the kacpid_wq
	 * queue when it should add hotplug events to the kacpi_hotplug_wq.
	 * The proper way to fix this is to reorganize the code so that
	 * drivers (dock, etc.) do not call acpi_os_execute(), etc.
	 * For now just re-add this work to the kacpi_hotplug_wq so we
	 * don't deadlock on hotplug actions.
	 */
	get_bridge(func->slot->bridge);
	alloc_acpi_hp_work(handle, type, context, _handle_hotplug_event_func);
}

/*
 * Create hotplug slots for the PCI bus.
 * It should always return 0 to avoid skipping following notifiers.
 */
void acpiphp_enumerate_slots(struct pci_bus *bus, acpi_handle handle)
{
	acpi_handle dummy_handle;
	struct acpiphp_bridge *bridge;

	if (acpiphp_disabled)
		return;

	if (detect_ejectable_slots(handle) <= 0)
		return;

	bridge = kzalloc(sizeof(struct acpiphp_bridge), GFP_KERNEL);
	if (bridge == NULL) {
		err("out of memory\n");
		return;
	}

	INIT_LIST_HEAD(&bridge->slots);
	kref_init(&bridge->ref);
	bridge->handle = handle;
	bridge->pci_dev = pci_dev_get(bus->self);
	bridge->pci_bus = bus;

	/*
	 * Grab a ref to the subordinate PCI bus in case the bus is
	 * removed via PCI core logical hotplug. The ref pins the bus
	 * (which we access during module unload).
	 */
	get_device(&bus->dev);

	if (!pci_is_root_bus(bridge->pci_bus) &&
	    ACPI_SUCCESS(acpi_get_handle(bridge->handle,
					"_EJ0", &dummy_handle))) {
		dbg("found ejectable p2p bridge\n");
		bridge->flags |= BRIDGE_HAS_EJ0;
		bridge->func = acpiphp_bridge_handle_to_function(handle);
	}

	init_bridge_misc(bridge);
}

/* Destroy hotplug slots associated with the PCI bus */
void acpiphp_remove_slots(struct pci_bus *bus)
{
	struct acpiphp_bridge *bridge, *tmp;

	if (acpiphp_disabled)
		return;

	list_for_each_entry_safe(bridge, tmp, &bridge_list, list)
		if (bridge->pci_bus == bus) {
			cleanup_bridge(bridge);
			put_bridge(bridge);
			break;
		}
}

/**
 * acpiphp_enable_slot - power on slot
 * @slot: ACPI PHP slot
 */
int acpiphp_enable_slot(struct acpiphp_slot *slot)
{
	int retval;

	mutex_lock(&slot->crit_sect);

	/* wake up all functions */
	retval = power_on_slot(slot);
	if (retval)
		goto err_exit;

	if (get_slot_status(slot) == ACPI_STA_ALL) {
		/* configure all functions */
		retval = enable_device(slot);
		if (retval)
			power_off_slot(slot);
	} else {
		dbg("%s: Slot status is not ACPI_STA_ALL\n", __func__);
		power_off_slot(slot);
	}

 err_exit:
	mutex_unlock(&slot->crit_sect);
	return retval;
}

/**
 * acpiphp_disable_slot - power off slot
 * @slot: ACPI PHP slot
 */
int acpiphp_disable_slot(struct acpiphp_slot *slot)
{
	int retval = 0;

	mutex_lock(&slot->crit_sect);

	/* unconfigure all functions */
	retval = disable_device(slot);
	if (retval)
		goto err_exit;

	/* power off all functions */
	retval = power_off_slot(slot);
	if (retval)
		goto err_exit;

 err_exit:
	mutex_unlock(&slot->crit_sect);
	return retval;
}


/*
 * slot enabled:  1
 * slot disabled: 0
 */
u8 acpiphp_get_power_status(struct acpiphp_slot *slot)
{
	return (slot->flags & SLOT_POWEREDON);
}


/*
 * latch   open:  1
 * latch closed:  0
 */
u8 acpiphp_get_latch_status(struct acpiphp_slot *slot)
{
	unsigned int sta;

	sta = get_slot_status(slot);

	return (sta & ACPI_STA_DEVICE_UI) ? 0 : 1;
}


/*
 * adapter presence : 1
 *          absence : 0
 */
u8 acpiphp_get_adapter_status(struct acpiphp_slot *slot)
{
	unsigned int sta;

	sta = get_slot_status(slot);

	return (sta == 0) ? 0 : 1;
}

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
 * Send feedback to <t-kochi@bq.jp.nec.com>
 *
 */

/*
 * Lifetime rules for pci_dev:
 *  - The one in acpiphp_func has its refcount elevated by pci_get_slot()
 *    when the driver is loaded or when an insertion event occurs.  It loses
 *    a refcount when its ejected or the driver unloads.
 *  - The one in acpiphp_bridge has its refcount elevated by pci_get_slot()
 *    when the bridge is scanned and it loses a refcount when the bridge
 *    is removed.
 */

#include <linux/init.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/smp_lock.h>
#include <linux/mutex.h>

#include "../pci.h"
#include "pci_hotplug.h"
#include "acpiphp.h"

static LIST_HEAD(bridge_list);

#define MY_NAME "acpiphp_glue"

static void handle_hotplug_event_bridge (acpi_handle, u32, void *);
static void acpiphp_sanitize_bus(struct pci_bus *bus);
static void acpiphp_set_hpp_values(acpi_handle handle, struct pci_bus *bus);


/*
 * initialization & terminatation routines
 */

/**
 * is_ejectable - determine if a slot is ejectable
 * @handle: handle to acpi namespace
 *
 * Ejectable slot should satisfy at least these conditions:
 *
 *  1. has _ADR method
 *  2. has _EJ0 method
 *
 * optionally
 *
 *  1. has _STA method
 *  2. has _PS0 method
 *  3. has _PS3 method
 *  4. ..
 *
 */
static int is_ejectable(acpi_handle handle)
{
	acpi_status status;
	acpi_handle tmp;

	status = acpi_get_handle(handle, "_ADR", &tmp);
	if (ACPI_FAILURE(status)) {
		return 0;
	}

	status = acpi_get_handle(handle, "_EJ0", &tmp);
	if (ACPI_FAILURE(status)) {
		return 0;
	}

	return 1;
}


/* callback routine to check the existence of ejectable slots */
static acpi_status
is_ejectable_slot(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	int *count = (int *)context;

	if (is_ejectable(handle)) {
		(*count)++;
		/* only one ejectable slot is enough */
		return AE_CTRL_TERMINATE;
	} else {
		return AE_OK;
	}
}


/* callback routine to register each ACPI PCI slot object */
static acpi_status
register_slot(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	struct acpiphp_bridge *bridge = (struct acpiphp_bridge *)context;
	struct acpiphp_slot *slot;
	struct acpiphp_func *newfunc;
	struct dependent_device *dd;
	acpi_handle tmp;
	acpi_status status = AE_OK;
	unsigned long adr, sun;
	int device, function, retval;

	status = acpi_evaluate_integer(handle, "_ADR", NULL, &adr);

	if (ACPI_FAILURE(status))
		return AE_OK;

	status = acpi_get_handle(handle, "_EJ0", &tmp);

	if (ACPI_FAILURE(status) && !(is_dependent_device(handle)))
		return AE_OK;

	device = (adr >> 16) & 0xffff;
	function = adr & 0xffff;

	newfunc = kzalloc(sizeof(struct acpiphp_func), GFP_KERNEL);
	if (!newfunc)
		return AE_NO_MEMORY;

	INIT_LIST_HEAD(&newfunc->sibling);
	newfunc->handle = handle;
	newfunc->function = function;
	if (ACPI_SUCCESS(status))
		newfunc->flags = FUNC_HAS_EJ0;

	if (ACPI_SUCCESS(acpi_get_handle(handle, "_STA", &tmp)))
		newfunc->flags |= FUNC_HAS_STA;

	if (ACPI_SUCCESS(acpi_get_handle(handle, "_PS0", &tmp)))
		newfunc->flags |= FUNC_HAS_PS0;

	if (ACPI_SUCCESS(acpi_get_handle(handle, "_PS3", &tmp)))
		newfunc->flags |= FUNC_HAS_PS3;

	if (ACPI_SUCCESS(acpi_get_handle(handle, "_DCK", &tmp))) {
		newfunc->flags |= FUNC_HAS_DCK;
		/* add to devices dependent on dock station,
		 * because this may actually be the dock bridge
		 */
		dd = alloc_dependent_device(handle);
                if (!dd)
                        err("Can't allocate memory for "
				"new dependent device!\n");
		else
			add_dependent_device(dd);
	}

	status = acpi_evaluate_integer(handle, "_SUN", NULL, &sun);
	if (ACPI_FAILURE(status))
		sun = -1;

	/* search for objects that share the same slot */
	for (slot = bridge->slots; slot; slot = slot->next)
		if (slot->device == device) {
			if (slot->sun != sun)
				warn("sibling found, but _SUN doesn't match!\n");
			break;
		}

	if (!slot) {
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

		slot->next = bridge->slots;
		bridge->slots = slot;

		bridge->nr_slots++;

		dbg("found ACPI PCI Hotplug slot %d at PCI %04x:%02x:%02x\n",
				slot->sun, pci_domain_nr(bridge->pci_bus),
				bridge->pci_bus->number, slot->device);
		retval = acpiphp_register_hotplug_slot(slot);
		if (retval) {
			warn("acpiphp_register_hotplug_slot failed(err code = 0x%x)\n", retval);
			goto err_exit;
		}
	}

	newfunc->slot = slot;
	list_add_tail(&newfunc->sibling, &slot->funcs);

	/* associate corresponding pci_dev */
	newfunc->pci_dev = pci_get_slot(bridge->pci_bus,
					 PCI_DEVFN(device, function));
	if (newfunc->pci_dev) {
		slot->flags |= (SLOT_ENABLED | SLOT_POWEREDON);
	}

	/* if this is a device dependent on a dock station,
	 * associate the acpiphp_func to the dependent_device
 	 * struct.
	 */
	if ((dd = get_dependent_device(handle))) {
		newfunc->flags |= FUNC_IS_DD;
		/*
		 * we don't want any devices which is dependent
		 * on the dock to have it's _EJ0 method executed.
		 * because we need to run _DCK first.
		 */
		newfunc->flags &= ~FUNC_HAS_EJ0;
		dd->func = newfunc;
		add_pci_dependent_device(dd);
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
	bridge->slots = slot->next;
	kfree(slot);
	kfree(newfunc);

	return AE_OK;
}


/* see if it's worth looking at this bridge */
static int detect_ejectable_slots(acpi_handle *bridge_handle)
{
	acpi_status status;
	int count;

	count = 0;

	/* only check slots defined directly below bridge object */
	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, bridge_handle, (u32)1,
				     is_ejectable_slot, (void *)&count, NULL);

	return count;
}


/* decode ACPI 2.0 _HPP hot plug parameters */
static void decode_hpp(struct acpiphp_bridge *bridge)
{
	acpi_status status;

	status = acpi_get_hp_params_from_firmware(bridge->pci_dev, &bridge->hpp);
	if (ACPI_FAILURE(status)) {
		/* use default numbers */
		bridge->hpp.cache_line_size = 0x10;
		bridge->hpp.latency_timer = 0x40;
		bridge->hpp.enable_serr = 0;
		bridge->hpp.enable_perr = 0;
	}
}



/* initialize miscellaneous stuff for both root and PCI-to-PCI bridge */
static void init_bridge_misc(struct acpiphp_bridge *bridge)
{
	acpi_status status;

	/* decode ACPI 2.0 _HPP (hot plug parameters) */
	decode_hpp(bridge);

	/* must be added to the list prior to calling register_slot */
	list_add(&bridge->list, &bridge_list);

	/* register all slot objects under this bridge */
	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, bridge->handle, (u32)1,
				     register_slot, bridge, NULL);
	if (ACPI_FAILURE(status)) {
		list_del(&bridge->list);
		return;
	}

	/* install notify handler */
	if (bridge->type != BRIDGE_TYPE_HOST) {
		status = acpi_install_notify_handler(bridge->handle,
					     ACPI_SYSTEM_NOTIFY,
					     handle_hotplug_event_bridge,
					     bridge);

		if (ACPI_FAILURE(status)) {
			err("failed to register interrupt notify handler\n");
		}
	}
}


/* allocate and initialize host bridge data structure */
static void add_host_bridge(acpi_handle *handle, struct pci_bus *pci_bus)
{
	struct acpiphp_bridge *bridge;

	bridge = kzalloc(sizeof(struct acpiphp_bridge), GFP_KERNEL);
	if (bridge == NULL)
		return;

	bridge->type = BRIDGE_TYPE_HOST;
	bridge->handle = handle;

	bridge->pci_bus = pci_bus;

	spin_lock_init(&bridge->res_lock);

	init_bridge_misc(bridge);
}


/* allocate and initialize PCI-to-PCI bridge data structure */
static void add_p2p_bridge(acpi_handle *handle, struct pci_dev *pci_dev)
{
	struct acpiphp_bridge *bridge;

	bridge = kzalloc(sizeof(struct acpiphp_bridge), GFP_KERNEL);
	if (bridge == NULL) {
		err("out of memory\n");
		return;
	}

	bridge->type = BRIDGE_TYPE_P2P;
	bridge->handle = handle;

	bridge->pci_dev = pci_dev_get(pci_dev);
	bridge->pci_bus = pci_dev->subordinate;
	if (!bridge->pci_bus) {
		err("This is not a PCI-to-PCI bridge!\n");
		goto err;
	}

	spin_lock_init(&bridge->res_lock);

	init_bridge_misc(bridge);
	return;
 err:
	pci_dev_put(pci_dev);
	kfree(bridge);
	return;
}


/* callback routine to find P2P bridges */
static acpi_status
find_p2p_bridge(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	acpi_status status;
	acpi_handle dummy_handle;
	unsigned long tmp;
	int device, function;
	struct pci_dev *dev;
	struct pci_bus *pci_bus = context;

	status = acpi_get_handle(handle, "_ADR", &dummy_handle);
	if (ACPI_FAILURE(status))
		return AE_OK;		/* continue */

	status = acpi_evaluate_integer(handle, "_ADR", NULL, &tmp);
	if (ACPI_FAILURE(status)) {
		dbg("%s: _ADR evaluation failure\n", __FUNCTION__);
		return AE_OK;
	}

	device = (tmp >> 16) & 0xffff;
	function = tmp & 0xffff;

	dev = pci_get_slot(pci_bus, PCI_DEVFN(device, function));

	if (!dev || !dev->subordinate)
		goto out;

	/* check if this bridge has ejectable slots */
	if ((detect_ejectable_slots(handle) > 0) ||
		(detect_dependent_devices(handle) > 0)) {
		dbg("found PCI-to-PCI bridge at PCI %s\n", pci_name(dev));
		add_p2p_bridge(handle, dev);
	}

	/* search P2P bridges under this p2p bridge */
	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, (u32)1,
				     find_p2p_bridge, dev->subordinate, NULL);
	if (ACPI_FAILURE(status))
		warn("find_p2p_bridge faied (error code = 0x%x)\n", status);

 out:
	pci_dev_put(dev);
	return AE_OK;
}


/* find hot-pluggable slots, and then find P2P bridge */
static int add_bridge(acpi_handle handle)
{
	acpi_status status;
	unsigned long tmp;
	int seg, bus;
	acpi_handle dummy_handle;
	struct pci_bus *pci_bus;

	/* if the bridge doesn't have _STA, we assume it is always there */
	status = acpi_get_handle(handle, "_STA", &dummy_handle);
	if (ACPI_SUCCESS(status)) {
		status = acpi_evaluate_integer(handle, "_STA", NULL, &tmp);
		if (ACPI_FAILURE(status)) {
			dbg("%s: _STA evaluation failure\n", __FUNCTION__);
			return 0;
		}
		if ((tmp & ACPI_STA_FUNCTIONING) == 0)
			/* don't register this object */
			return 0;
	}

	/* get PCI segment number */
	status = acpi_evaluate_integer(handle, "_SEG", NULL, &tmp);

	seg = ACPI_SUCCESS(status) ? tmp : 0;

	/* get PCI bus number */
	status = acpi_evaluate_integer(handle, "_BBN", NULL, &tmp);

	if (ACPI_SUCCESS(status)) {
		bus = tmp;
	} else {
		warn("can't get bus number, assuming 0\n");
		bus = 0;
	}

	pci_bus = pci_find_bus(seg, bus);
	if (!pci_bus) {
		err("Can't find bus %04x:%02x\n", seg, bus);
		return 0;
	}

	/* check if this bridge has ejectable slots */
	if (detect_ejectable_slots(handle) > 0) {
		dbg("found PCI host-bus bridge with hot-pluggable slots\n");
		add_host_bridge(handle, pci_bus);
		return 0;
	}

	/* search P2P bridges under this host bridge */
	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, (u32)1,
				     find_p2p_bridge, pci_bus, NULL);

	if (ACPI_FAILURE(status))
		warn("find_p2p_bridge faied (error code = 0x%x)\n",status);

	return 0;
}

static struct acpiphp_bridge *acpiphp_handle_to_bridge(acpi_handle handle)
{
	struct list_head *head;
	list_for_each(head, &bridge_list) {
		struct acpiphp_bridge *bridge = list_entry(head,
						struct acpiphp_bridge, list);
		if (bridge->handle == handle)
			return bridge;
	}

	return NULL;
}

static void cleanup_bridge(struct acpiphp_bridge *bridge)
{
	struct list_head *list, *tmp;
	struct acpiphp_slot *slot;
	acpi_status status;
	acpi_handle handle = bridge->handle;

	status = acpi_remove_notify_handler(handle, ACPI_SYSTEM_NOTIFY,
					    handle_hotplug_event_bridge);
	if (ACPI_FAILURE(status))
		err("failed to remove notify handler\n");

	slot = bridge->slots;
	while (slot) {
		struct acpiphp_slot *next = slot->next;
		list_for_each_safe (list, tmp, &slot->funcs) {
			struct acpiphp_func *func;
			func = list_entry(list, struct acpiphp_func, sibling);
			if (!(func->flags & FUNC_HAS_DCK)) {
				status = acpi_remove_notify_handler(func->handle,
						ACPI_SYSTEM_NOTIFY,
						handle_hotplug_event_func);
				if (ACPI_FAILURE(status))
					err("failed to remove notify handler\n");
			}
			pci_dev_put(func->pci_dev);
			list_del(list);
			kfree(func);
		}
		acpiphp_unregister_hotplug_slot(slot);
		list_del(&slot->funcs);
		kfree(slot);
		slot = next;
	}

	pci_dev_put(bridge->pci_dev);
	list_del(&bridge->list);
	kfree(bridge);
}

static acpi_status
cleanup_p2p_bridge(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	struct acpiphp_bridge *bridge;

	if (!(bridge = acpiphp_handle_to_bridge(handle)))
		return AE_OK;
	cleanup_bridge(bridge);
	return AE_OK;
}

static void remove_bridge(acpi_handle handle)
{
	struct acpiphp_bridge *bridge;

	bridge = acpiphp_handle_to_bridge(handle);
	if (bridge) {
		cleanup_bridge(bridge);
	} else {
		/* clean-up p2p bridges under this host bridge */
		acpi_walk_namespace(ACPI_TYPE_DEVICE, handle,
				    ACPI_UINT32_MAX, cleanup_p2p_bridge,
				    NULL, NULL);
	}
}

static struct pci_dev * get_apic_pci_info(acpi_handle handle)
{
	struct acpi_pci_id id;
	struct pci_bus *bus;
	struct pci_dev *dev;

	if (ACPI_FAILURE(acpi_get_pci_id(handle, &id)))
		return NULL;

	bus = pci_find_bus(id.segment, id.bus);
	if (!bus)
		return NULL;

	dev = pci_get_slot(bus, PCI_DEVFN(id.device, id.function));
	if (!dev)
		return NULL;

	if ((dev->class != PCI_CLASS_SYSTEM_PIC_IOAPIC) &&
	    (dev->class != PCI_CLASS_SYSTEM_PIC_IOXAPIC))
	{
		pci_dev_put(dev);
		return NULL;
	}

	return dev;
}

static int get_gsi_base(acpi_handle handle, u32 *gsi_base)
{
	acpi_status status;
	int result = -1;
	unsigned long gsb;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *obj;
	void *table;

	status = acpi_evaluate_integer(handle, "_GSB", NULL, &gsb);
	if (ACPI_SUCCESS(status)) {
		*gsi_base = (u32)gsb;
		return 0;
	}

	status = acpi_evaluate_object(handle, "_MAT", NULL, &buffer);
	if (ACPI_FAILURE(status) || !buffer.length || !buffer.pointer)
		return -1;

	obj = buffer.pointer;
	if (obj->type != ACPI_TYPE_BUFFER)
		goto out;

	table = obj->buffer.pointer;
	switch (((acpi_table_entry_header *)table)->type) {
	case ACPI_MADT_IOSAPIC:
		*gsi_base = ((struct acpi_table_iosapic *)table)->global_irq_base;
		result = 0;
		break;
	case ACPI_MADT_IOAPIC:
		*gsi_base = ((struct acpi_table_ioapic *)table)->global_irq_base;
		result = 0;
		break;
	default:
		break;
	}
 out:
	acpi_os_free(buffer.pointer);
	return result;
}

static acpi_status
ioapic_add(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	acpi_status status;
	unsigned long sta;
	acpi_handle tmp;
	struct pci_dev *pdev;
	u32 gsi_base;
	u64 phys_addr;

	/* Evaluate _STA if present */
	status = acpi_evaluate_integer(handle, "_STA", NULL, &sta);
	if (ACPI_SUCCESS(status) && sta != ACPI_STA_ALL)
		return AE_CTRL_DEPTH;

	/* Scan only PCI bus scope */
	status = acpi_get_handle(handle, "_HID", &tmp);
	if (ACPI_SUCCESS(status))
		return AE_CTRL_DEPTH;

	if (get_gsi_base(handle, &gsi_base))
		return AE_OK;

	pdev = get_apic_pci_info(handle);
	if (!pdev)
		return AE_OK;

	if (pci_enable_device(pdev)) {
		pci_dev_put(pdev);
		return AE_OK;
	}

	pci_set_master(pdev);

	if (pci_request_region(pdev, 0, "I/O APIC(acpiphp)")) {
		pci_disable_device(pdev);
		pci_dev_put(pdev);
		return AE_OK;
	}

	phys_addr = pci_resource_start(pdev, 0);
	if (acpi_register_ioapic(handle, phys_addr, gsi_base)) {
		pci_release_region(pdev, 0);
		pci_disable_device(pdev);
		pci_dev_put(pdev);
		return AE_OK;
	}

	return AE_OK;
}

static int acpiphp_configure_ioapics(acpi_handle handle)
{
	acpi_walk_namespace(ACPI_TYPE_DEVICE, handle,
			    ACPI_UINT32_MAX, ioapic_add, NULL, NULL);
	return 0;
}

static int power_on_slot(struct acpiphp_slot *slot)
{
	acpi_status status;
	struct acpiphp_func *func;
	struct list_head *l;
	int retval = 0;

	/* if already enabled, just skip */
	if (slot->flags & SLOT_POWEREDON)
		goto err_exit;

	list_for_each (l, &slot->funcs) {
		func = list_entry(l, struct acpiphp_func, sibling);

		if (func->flags & FUNC_HAS_PS0) {
			dbg("%s: executing _PS0\n", __FUNCTION__);
			status = acpi_evaluate_object(func->handle, "_PS0", NULL, NULL);
			if (ACPI_FAILURE(status)) {
				warn("%s: _PS0 failed\n", __FUNCTION__);
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
	struct list_head *l;

	int retval = 0;

	/* if already disabled, just skip */
	if ((slot->flags & SLOT_POWEREDON) == 0)
		goto err_exit;

	list_for_each (l, &slot->funcs) {
		func = list_entry(l, struct acpiphp_func, sibling);

		if (func->flags & FUNC_HAS_PS3) {
			status = acpi_evaluate_object(func->handle, "_PS3", NULL, NULL);
			if (ACPI_FAILURE(status)) {
				warn("%s: _PS3 failed\n", __FUNCTION__);
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
 * acpiphp_max_busnr - return the highest reserved bus number under
 * the given bus.
 * @bus: bus to start search with
 *
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
	max = bus->secondary;

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
 *
 */
static int acpiphp_bus_add(struct acpiphp_func *func)
{
	acpi_handle phandle;
	struct acpi_device *device, *pdevice;
	int ret_val;

	acpi_get_parent(func->handle, &phandle);
	if (acpi_bus_get_device(phandle, &pdevice)) {
		dbg("no parent device, assuming NULL\n");
		pdevice = NULL;
	}
	if (!acpi_bus_get_device(func->handle, &device)) {
		dbg("bus exists... trim\n");
		/* this shouldn't be in here, so remove
		 * the bus then re-add it...
		 */
		ret_val = acpi_bus_trim(device, 1);
		dbg("acpi_bus_trim return %x\n", ret_val);
	}

	ret_val = acpi_bus_add(&device, pdevice, func->handle,
		ACPI_BUS_TYPE_DEVICE);
	if (ret_val) {
		dbg("error adding bus, %x\n",
			-ret_val);
		goto acpiphp_bus_add_out;
	}
	/*
	 * try to start anyway.  We could have failed to add
	 * simply because this bus had previously been added
	 * on another add.  Don't bother with the return value
	 * we just keep going.
	 */
	ret_val = acpi_bus_start(device);

acpiphp_bus_add_out:
	return ret_val;
}


/**
 * acpiphp_bus_trim - trim a bus from acpi subsystem
 * @handle: handle to acpi namespace
 *
 */
int acpiphp_bus_trim(acpi_handle handle)
{
	struct acpi_device *device;
	int retval;

	retval = acpi_bus_get_device(handle, &device);
	if (retval) {
		dbg("acpi_device not found\n");
		return retval;
	}

	retval = acpi_bus_trim(device, 1);
	if (retval)
		err("cannot remove from acpi list\n");

	return retval;
}

/**
 * enable_device - enable, configure a slot
 * @slot: slot to be enabled
 *
 * This function should be called per *physical slot*,
 * not per each slot object in ACPI namespace.
 *
 */
static int enable_device(struct acpiphp_slot *slot)
{
	struct pci_dev *dev;
	struct pci_bus *bus = slot->bridge->pci_bus;
	struct list_head *l;
	struct acpiphp_func *func;
	int retval = 0;
	int num, max, pass;

	if (slot->flags & SLOT_ENABLED)
		goto err_exit;

	/* sanity check: dev should be NULL when hot-plugged in */
	dev = pci_get_slot(bus, PCI_DEVFN(slot->device, 0));
	if (dev) {
		/* This case shouldn't happen */
		err("pci_dev structure already exists.\n");
		pci_dev_put(dev);
		retval = -1;
		goto err_exit;
	}

	num = pci_scan_slot(bus, PCI_DEVFN(slot->device, 0));
	if (num == 0) {
		err("No new device found\n");
		retval = -1;
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
				if (pass && dev->subordinate)
					pci_bus_size_bridges(dev->subordinate);
			}
		}
	}

	list_for_each (l, &slot->funcs) {
		func = list_entry(l, struct acpiphp_func, sibling);
		acpiphp_bus_add(func);
	}

	pci_bus_assign_resources(bus);
	acpiphp_sanitize_bus(bus);
	pci_enable_bridges(bus);
	pci_bus_add_devices(bus);
	acpiphp_set_hpp_values(slot->bridge->handle, bus);
	acpiphp_configure_ioapics(slot->bridge->handle);

	/* associate pci_dev to our representation */
	list_for_each (l, &slot->funcs) {
		func = list_entry(l, struct acpiphp_func, sibling);
		func->pci_dev = pci_get_slot(bus, PCI_DEVFN(slot->device,
							func->function));
	}

	slot->flags |= SLOT_ENABLED;

 err_exit:
	return retval;
}


/**
 * disable_device - disable a slot
 */
static int disable_device(struct acpiphp_slot *slot)
{
	int retval = 0;
	struct acpiphp_func *func;
	struct list_head *l;

	/* is this slot already disabled? */
	if (!(slot->flags & SLOT_ENABLED))
		goto err_exit;

	list_for_each (l, &slot->funcs) {
		func = list_entry(l, struct acpiphp_func, sibling);

		acpiphp_bus_trim(func->handle);
		/* try to remove anyway.
		 * acpiphp_bus_add might have been failed */

		if (!func->pci_dev)
			continue;

		pci_remove_bus_device(func->pci_dev);
		pci_dev_put(func->pci_dev);
		func->pci_dev = NULL;
	}

	slot->flags &= (~SLOT_ENABLED);

 err_exit:
	return retval;
}


/**
 * get_slot_status - get ACPI slot status
 *
 * if a slot has _STA for each function and if any one of them
 * returned non-zero status, return it
 *
 * if a slot doesn't have _STA and if any one of its functions'
 * configuration space is configured, return 0x0f as a _STA
 *
 * otherwise return 0
 */
static unsigned int get_slot_status(struct acpiphp_slot *slot)
{
	acpi_status status;
	unsigned long sta = 0;
	u32 dvid;
	struct list_head *l;
	struct acpiphp_func *func;

	list_for_each (l, &slot->funcs) {
		func = list_entry(l, struct acpiphp_func, sibling);

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
 */
static int acpiphp_eject_slot(struct acpiphp_slot *slot)
{
	acpi_status status;
	struct acpiphp_func *func;
	struct list_head *l;
	struct acpi_object_list arg_list;
	union acpi_object arg;

	list_for_each (l, &slot->funcs) {
		func = list_entry(l, struct acpiphp_func, sibling);

		/* We don't want to call _EJ0 on non-existing functions. */
		if ((func->flags & FUNC_HAS_EJ0)) {
			/* _EJ0 method take one argument */
			arg_list.count = 1;
			arg_list.pointer = &arg;
			arg.type = ACPI_TYPE_INTEGER;
			arg.integer.value = 1;

			status = acpi_evaluate_object(func->handle, "_EJ0", &arg_list, NULL);
			if (ACPI_FAILURE(status)) {
				warn("%s: _EJ0 failed\n", __FUNCTION__);
				return -1;
			} else
				break;
		}
	}
	return 0;
}

/**
 * acpiphp_check_bridge - re-enumerate devices
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

	for (slot = bridge->slots; slot; slot = slot->next) {
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

	dbg("%s: %d enabled, %d disabled\n", __FUNCTION__, enabled, disabled);

 err_exit:
	return retval;
}

static void program_hpp(struct pci_dev *dev, struct acpiphp_bridge *bridge)
{
	u16 pci_cmd, pci_bctl;
	struct pci_dev *cdev;

	/* Program hpp values for this device */
	if (!(dev->hdr_type == PCI_HEADER_TYPE_NORMAL ||
			(dev->hdr_type == PCI_HEADER_TYPE_BRIDGE &&
			(dev->class >> 8) == PCI_CLASS_BRIDGE_PCI)))
		return;
	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE,
			bridge->hpp.cache_line_size);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER,
			bridge->hpp.latency_timer);
	pci_read_config_word(dev, PCI_COMMAND, &pci_cmd);
	if (bridge->hpp.enable_serr)
		pci_cmd |= PCI_COMMAND_SERR;
	else
		pci_cmd &= ~PCI_COMMAND_SERR;
	if (bridge->hpp.enable_perr)
		pci_cmd |= PCI_COMMAND_PARITY;
	else
		pci_cmd &= ~PCI_COMMAND_PARITY;
	pci_write_config_word(dev, PCI_COMMAND, pci_cmd);

	/* Program bridge control value and child devices */
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		pci_write_config_byte(dev, PCI_SEC_LATENCY_TIMER,
				bridge->hpp.latency_timer);
		pci_read_config_word(dev, PCI_BRIDGE_CONTROL, &pci_bctl);
		if (bridge->hpp.enable_serr)
			pci_bctl |= PCI_BRIDGE_CTL_SERR;
		else
			pci_bctl &= ~PCI_BRIDGE_CTL_SERR;
		if (bridge->hpp.enable_perr)
			pci_bctl |= PCI_BRIDGE_CTL_PARITY;
		else
			pci_bctl &= ~PCI_BRIDGE_CTL_PARITY;
		pci_write_config_word(dev, PCI_BRIDGE_CONTROL, pci_bctl);
		if (dev->subordinate) {
			list_for_each_entry(cdev, &dev->subordinate->devices,
					bus_list)
				program_hpp(cdev, bridge);
		}
	}
}

static void acpiphp_set_hpp_values(acpi_handle handle, struct pci_bus *bus)
{
	struct acpiphp_bridge bridge;
	struct pci_dev *dev;

	memset(&bridge, 0, sizeof(bridge));
	bridge.handle = handle;
	bridge.pci_dev = bus->self;
	decode_hpp(&bridge);
	list_for_each_entry(dev, &bus->devices, bus_list)
		program_hpp(dev, &bridge);

}

/*
 * Remove devices for which we could not assign resources, call
 * arch specific code to fix-up the bus
 */
static void acpiphp_sanitize_bus(struct pci_bus *bus)
{
	struct pci_dev *dev;
	int i;
	unsigned long type_mask = IORESOURCE_IO | IORESOURCE_MEM;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		for (i=0; i<PCI_BRIDGE_RESOURCES; i++) {
			struct resource *res = &dev->resource[i];
			if ((res->flags & type_mask) && !res->start &&
					res->end) {
				/* Could not assign a required resources
				 * for this device, remove it */
				pci_remove_bus_device(dev);
				break;
			}
		}
	}
}

/* Program resources in newly inserted bridge */
static int acpiphp_configure_bridge (acpi_handle handle)
{
	struct acpi_pci_id pci_id;
	struct pci_bus *bus;

	if (ACPI_FAILURE(acpi_get_pci_id(handle, &pci_id))) {
		err("cannot get PCI domain and bus number for bridge\n");
		return -EINVAL;
	}
	bus = pci_find_bus(pci_id.segment, pci_id.bus);
	if (!bus) {
		err("cannot find bus %d:%d\n",
				pci_id.segment, pci_id.bus);
		return -EINVAL;
	}

	pci_bus_size_bridges(bus);
	pci_bus_assign_resources(bus);
	acpiphp_sanitize_bus(bus);
	acpiphp_set_hpp_values(handle, bus);
	pci_enable_bridges(bus);
	acpiphp_configure_ioapics(handle);
	return 0;
}

static void handle_bridge_insertion(acpi_handle handle, u32 type)
{
	struct acpi_device *device, *pdevice;
	acpi_handle phandle;

	if ((type != ACPI_NOTIFY_BUS_CHECK) &&
			(type != ACPI_NOTIFY_DEVICE_CHECK)) {
		err("unexpected notification type %d\n", type);
		return;
	}

	acpi_get_parent(handle, &phandle);
	if (acpi_bus_get_device(phandle, &pdevice)) {
		dbg("no parent device, assuming NULL\n");
		pdevice = NULL;
	}
	if (acpi_bus_add(&device, pdevice, handle, ACPI_BUS_TYPE_DEVICE)) {
		err("cannot add bridge to acpi list\n");
		return;
	}
	if (!acpiphp_configure_bridge(handle) &&
		!acpi_bus_start(device))
		add_bridge(handle);
	else
		err("cannot configure and start bridge\n");

}

/*
 * ACPI event handlers
 */

/**
 * handle_hotplug_event_bridge - handle ACPI event on bridges
 *
 * @handle: Notify()'ed acpi_handle
 * @type: Notify code
 * @context: pointer to acpiphp_bridge structure
 *
 * handles ACPI event notification on {host,p2p} bridges
 *
 */
static void handle_hotplug_event_bridge(acpi_handle handle, u32 type, void *context)
{
	struct acpiphp_bridge *bridge;
	char objname[64];
	struct acpi_buffer buffer = { .length = sizeof(objname),
				      .pointer = objname };
	struct acpi_device *device;

	if (acpi_bus_get_device(handle, &device)) {
		/* This bridge must have just been physically inserted */
		handle_bridge_insertion(handle, type);
		return;
	}

	bridge = acpiphp_handle_to_bridge(handle);
	if (!bridge) {
		err("cannot get bridge info\n");
		return;
	}

	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);

	switch (type) {
	case ACPI_NOTIFY_BUS_CHECK:
		/* bus re-enumerate */
		dbg("%s: Bus check notify on %s\n", __FUNCTION__, objname);
		acpiphp_check_bridge(bridge);
		break;

	case ACPI_NOTIFY_DEVICE_CHECK:
		/* device check */
		dbg("%s: Device check notify on %s\n", __FUNCTION__, objname);
		acpiphp_check_bridge(bridge);
		break;

	case ACPI_NOTIFY_DEVICE_WAKE:
		/* wake event */
		dbg("%s: Device wake notify on %s\n", __FUNCTION__, objname);
		break;

	case ACPI_NOTIFY_EJECT_REQUEST:
		/* request device eject */
		dbg("%s: Device eject notify on %s\n", __FUNCTION__, objname);
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
}

/**
 * handle_hotplug_event_func - handle ACPI event on functions (i.e. slots)
 *
 * @handle: Notify()'ed acpi_handle
 * @type: Notify code
 * @context: pointer to acpiphp_func structure
 *
 * handles ACPI event notification on slots
 *
 */
void handle_hotplug_event_func(acpi_handle handle, u32 type, void *context)
{
	struct acpiphp_func *func;
	char objname[64];
	struct acpi_buffer buffer = { .length = sizeof(objname),
				      .pointer = objname };

	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);

	func = (struct acpiphp_func *)context;

	switch (type) {
	case ACPI_NOTIFY_BUS_CHECK:
		/* bus re-enumerate */
		dbg("%s: Bus check notify on %s\n", __FUNCTION__, objname);
		acpiphp_enable_slot(func->slot);
		break;

	case ACPI_NOTIFY_DEVICE_CHECK:
		/* device check : re-enumerate from parent bus */
		dbg("%s: Device check notify on %s\n", __FUNCTION__, objname);
		acpiphp_check_bridge(func->slot->bridge);
		break;

	case ACPI_NOTIFY_DEVICE_WAKE:
		/* wake event */
		dbg("%s: Device wake notify on %s\n", __FUNCTION__, objname);
		break;

	case ACPI_NOTIFY_EJECT_REQUEST:
		/* request device eject */
		dbg("%s: Device eject notify on %s\n", __FUNCTION__, objname);
		if (!(acpiphp_disable_slot(func->slot)))
			acpiphp_eject_slot(func->slot);
		break;

	default:
		warn("notify_handler: unknown event type 0x%x for %s\n", type, objname);
		break;
	}
}


static acpi_status
find_root_bridges(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	int *count = (int *)context;

	if (acpi_root_bridge(handle)) {
		acpi_install_notify_handler(handle, ACPI_SYSTEM_NOTIFY,
				handle_hotplug_event_bridge, NULL);
			(*count)++;
	}
	return AE_OK ;
}

static struct acpi_pci_driver acpi_pci_hp_driver = {
	.add =		add_bridge,
	.remove =	remove_bridge,
};

/**
 * acpiphp_glue_init - initializes all PCI hotplug - ACPI glue data structures
 *
 */
int __init acpiphp_glue_init(void)
{
	int num = 0;

	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
			ACPI_UINT32_MAX, find_root_bridges, &num, NULL);

	if (num <= 0)
		return -1;
	else
		acpi_pci_register_driver(&acpi_pci_hp_driver);

	return 0;
}


/**
 * acpiphp_glue_exit - terminates all PCI hotplug - ACPI glue data structures
 *
 * This function frees all data allocated in acpiphp_glue_init()
 */
void __exit acpiphp_glue_exit(void)
{
	acpi_pci_unregister_driver(&acpi_pci_hp_driver);
}


/**
 * acpiphp_get_num_slots - count number of slots in a system
 */
int __init acpiphp_get_num_slots(void)
{
	struct list_head *node;
	struct acpiphp_bridge *bridge;
	int num_slots;

	num_slots = 0;

	list_for_each (node, &bridge_list) {
		bridge = (struct acpiphp_bridge *)node;
		dbg("Bus %04x:%02x has %d slot%s\n",
				pci_domain_nr(bridge->pci_bus),
				bridge->pci_bus->number, bridge->nr_slots,
				bridge->nr_slots == 1 ? "" : "s");
		num_slots += bridge->nr_slots;
	}

	dbg("Total %d slots\n", num_slots);
	return num_slots;
}


#if 0
/**
 * acpiphp_for_each_slot - call function for each slot
 * @fn: callback function
 * @data: context to be passed to callback function
 *
 */
static int acpiphp_for_each_slot(acpiphp_callback fn, void *data)
{
	struct list_head *node;
	struct acpiphp_bridge *bridge;
	struct acpiphp_slot *slot;
	int retval = 0;

	list_for_each (node, &bridge_list) {
		bridge = (struct acpiphp_bridge *)node;
		for (slot = bridge->slots; slot; slot = slot->next) {
			retval = fn(slot, data);
			if (!retval)
				goto err_exit;
		}
	}

 err_exit:
	return retval;
}
#endif


/**
 * acpiphp_enable_slot - power on slot
 */
int acpiphp_enable_slot(struct acpiphp_slot *slot)
{
	int retval;

	mutex_lock(&slot->crit_sect);

	/* wake up all functions */
	retval = power_on_slot(slot);
	if (retval)
		goto err_exit;

	if (get_slot_status(slot) == ACPI_STA_ALL)
		/* configure all functions */
		retval = enable_device(slot);

 err_exit:
	mutex_unlock(&slot->crit_sect);
	return retval;
}

/**
 * acpiphp_disable_slot - power off slot
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
 * latch closed:  1
 * latch   open:  0
 */
u8 acpiphp_get_latch_status(struct acpiphp_slot *slot)
{
	unsigned int sta;

	sta = get_slot_status(slot);

	return (sta & ACPI_STA_SHOW_IN_UI) ? 1 : 0;
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


/*
 * pci address (seg/bus/dev)
 */
u32 acpiphp_get_address(struct acpiphp_slot *slot)
{
	u32 address;
	struct pci_bus *pci_bus = slot->bridge->pci_bus;

	address = (pci_domain_nr(pci_bus) << 16) |
		  (pci_bus->number << 8) |
		  slot->device;

	return address;
}

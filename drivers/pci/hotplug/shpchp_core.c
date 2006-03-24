/*
 * Standard Hot Plug Controller Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 * Copyright (C) 2003-2004 Intel Corporation
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
 * Send feedback to <greg@kroah.com>, <kristen.c.accardi@intel.com>
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/workqueue.h>
#include "shpchp.h"

/* Global variables */
int shpchp_debug;
int shpchp_poll_mode;
int shpchp_poll_time;
struct workqueue_struct *shpchp_wq;

#define DRIVER_VERSION	"0.4"
#define DRIVER_AUTHOR	"Dan Zink <dan.zink@compaq.com>, Greg Kroah-Hartman <greg@kroah.com>, Dely Sy <dely.l.sy@intel.com>"
#define DRIVER_DESC	"Standard Hot Plug PCI Controller Driver"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(shpchp_debug, bool, 0644);
module_param(shpchp_poll_mode, bool, 0644);
module_param(shpchp_poll_time, int, 0644);
MODULE_PARM_DESC(shpchp_debug, "Debugging mode enabled or not");
MODULE_PARM_DESC(shpchp_poll_mode, "Using polling mechanism for hot-plug events or not");
MODULE_PARM_DESC(shpchp_poll_time, "Polling mechanism frequency, in seconds");

#define SHPC_MODULE_NAME "shpchp"

static int set_attention_status (struct hotplug_slot *slot, u8 value);
static int enable_slot		(struct hotplug_slot *slot);
static int disable_slot		(struct hotplug_slot *slot);
static int get_power_status	(struct hotplug_slot *slot, u8 *value);
static int get_attention_status	(struct hotplug_slot *slot, u8 *value);
static int get_latch_status	(struct hotplug_slot *slot, u8 *value);
static int get_adapter_status	(struct hotplug_slot *slot, u8 *value);
static int get_address		(struct hotplug_slot *slot, u32 *value);
static int get_max_bus_speed	(struct hotplug_slot *slot, enum pci_bus_speed *value);
static int get_cur_bus_speed	(struct hotplug_slot *slot, enum pci_bus_speed *value);

static struct hotplug_slot_ops shpchp_hotplug_slot_ops = {
	.owner =		THIS_MODULE,
	.set_attention_status =	set_attention_status,
	.enable_slot =		enable_slot,
	.disable_slot =		disable_slot,
	.get_power_status =	get_power_status,
	.get_attention_status =	get_attention_status,
	.get_latch_status =	get_latch_status,
	.get_adapter_status =	get_adapter_status,
	.get_address =		get_address,
	.get_max_bus_speed =	get_max_bus_speed,
	.get_cur_bus_speed =	get_cur_bus_speed,
};

/**
 * release_slot - free up the memory used by a slot
 * @hotplug_slot: slot to free
 */
static void release_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	kfree(slot->hotplug_slot->info);
	kfree(slot->hotplug_slot);
	kfree(slot);
}

static void make_slot_name(struct slot *slot)
{
	snprintf(slot->hotplug_slot->name, SLOT_NAME_SIZE, "%04d_%04d",
		 slot->bus, slot->number);
}




static int
shpchprm_get_physical_slot_number(struct controller *ctrl, u32 *sun,
				u8 busnum, u8 devnum)
{
	int offset = devnum - ctrl->slot_device_offset;

	dbg("%s: ctrl->slot_num_inc %d, offset %d\n", __FUNCTION__,
			ctrl->slot_num_inc, offset);
	*sun = (u8) (ctrl->first_slot + ctrl->slot_num_inc *offset);
	return 0;
}



static int init_slots(struct controller *ctrl)
{
	struct slot *slot;
	struct hotplug_slot *hotplug_slot;
	struct hotplug_slot_info *info;
	int retval = -ENOMEM;
	int i;
	u32 sun;

	for (i = 0; i < ctrl->num_slots; i++) {
		slot = kzalloc(sizeof(*slot), GFP_KERNEL);
		if (!slot)
			goto error;

		hotplug_slot = kzalloc(sizeof(*hotplug_slot), GFP_KERNEL);
		if (!hotplug_slot)
			goto error_slot;
		slot->hotplug_slot = hotplug_slot;

		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (!info)
			goto error_hpslot;
		hotplug_slot->info = info;

		hotplug_slot->name = slot->name;

		slot->hp_slot = i;
		slot->ctrl = ctrl;
		slot->bus = ctrl->slot_bus;
		slot->device = ctrl->slot_device_offset + i;
		slot->hpc_ops = ctrl->hpc_ops;
		mutex_init(&slot->lock);

		if (shpchprm_get_physical_slot_number(ctrl, &sun,
						      slot->bus, slot->device))
			goto error_info;

		slot->number = sun;
		INIT_WORK(&slot->work, queue_pushbutton_work, slot);

		/* register this slot with the hotplug pci core */
		hotplug_slot->private = slot;
		hotplug_slot->release = &release_slot;
		make_slot_name(slot);
		hotplug_slot->ops = &shpchp_hotplug_slot_ops;

		get_power_status(hotplug_slot, &info->power_status);
		get_attention_status(hotplug_slot, &info->attention_status);
		get_latch_status(hotplug_slot, &info->latch_status);
		get_adapter_status(hotplug_slot, &info->adapter_status);

		dbg("Registering bus=%x dev=%x hp_slot=%x sun=%x "
		    "slot_device_offset=%x\n", slot->bus, slot->device,
		    slot->hp_slot, slot->number, ctrl->slot_device_offset);
		retval = pci_hp_register(slot->hotplug_slot);
		if (retval) {
			err("pci_hp_register failed with error %d\n", retval);
			goto error_info;
		}

		list_add(&slot->slot_list, &ctrl->slot_list);
	}

	return 0;
error_info:
	kfree(info);
error_hpslot:
	kfree(hotplug_slot);
error_slot:
	kfree(slot);
error:
	return retval;
}

void cleanup_slots(struct controller *ctrl)
{
	struct list_head *tmp;
	struct list_head *next;
	struct slot *slot;

	list_for_each_safe(tmp, next, &ctrl->slot_list) {
		slot = list_entry(tmp, struct slot, slot_list);
		list_del(&slot->slot_list);
		cancel_delayed_work(&slot->work);
		flush_scheduled_work();
		flush_workqueue(shpchp_wq);
		pci_hp_deregister(slot->hotplug_slot);
	}
}

static int get_ctlr_slot_config(struct controller *ctrl)
{
	int num_ctlr_slots;
	int first_device_num;
	int physical_slot_num;
	int updown;
	int rc;
	int flags;

	rc = shpc_get_ctlr_slot_config(ctrl, &num_ctlr_slots,
				       &first_device_num, &physical_slot_num,
				       &updown, &flags);
	if (rc) {
		err("%s: get_ctlr_slot_config fail for b:d (%x:%x)\n",
		    __FUNCTION__, ctrl->bus, ctrl->device);
		return -1;
	}

	ctrl->num_slots = num_ctlr_slots;
	ctrl->slot_device_offset = first_device_num;
	ctrl->first_slot = physical_slot_num;
	ctrl->slot_num_inc = updown;		/* either -1 or 1 */

	dbg("%s: num_slot(0x%x) 1st_dev(0x%x) psn(0x%x) updown(%d) for b:d "
	    "(%x:%x)\n", __FUNCTION__, num_ctlr_slots, first_device_num,
	    physical_slot_num, updown, ctrl->bus, ctrl->device);

	return 0;
}

/*
 * set_attention_status - Turns the Amber LED for a slot on, off or blink
 */
static int set_attention_status (struct hotplug_slot *hotplug_slot, u8 status)
{
	struct slot *slot = get_slot(hotplug_slot, __FUNCTION__);

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	hotplug_slot->info->attention_status = status;
	slot->hpc_ops->set_attention_status(slot, status);

	return 0;
}

static int enable_slot (struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = get_slot(hotplug_slot, __FUNCTION__);

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	return shpchp_sysfs_enable_slot(slot);
}

static int disable_slot (struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = get_slot(hotplug_slot, __FUNCTION__);

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	return shpchp_sysfs_disable_slot(slot);
}

static int get_power_status (struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = get_slot(hotplug_slot, __FUNCTION__);
	int retval;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	retval = slot->hpc_ops->get_power_status(slot, value);
	if (retval < 0)
		*value = hotplug_slot->info->power_status;

	return 0;
}

static int get_attention_status (struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = get_slot(hotplug_slot, __FUNCTION__);
	int retval;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	retval = slot->hpc_ops->get_attention_status(slot, value);
	if (retval < 0)
		*value = hotplug_slot->info->attention_status;

	return 0;
}

static int get_latch_status (struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = get_slot(hotplug_slot, __FUNCTION__);
	int retval;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	retval = slot->hpc_ops->get_latch_status(slot, value);
	if (retval < 0)
		*value = hotplug_slot->info->latch_status;

	return 0;
}

static int get_adapter_status (struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = get_slot(hotplug_slot, __FUNCTION__);
	int retval;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	retval = slot->hpc_ops->get_adapter_status(slot, value);
	if (retval < 0)
		*value = hotplug_slot->info->adapter_status;

	return 0;
}

static int get_address (struct hotplug_slot *hotplug_slot, u32 *value)
{
	struct slot *slot = get_slot(hotplug_slot, __FUNCTION__);
	struct pci_bus *bus = slot->ctrl->pci_dev->subordinate;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	*value = (pci_domain_nr(bus) << 16) | (slot->bus << 8) | slot->device;

	return 0;
}

static int get_max_bus_speed (struct hotplug_slot *hotplug_slot, enum pci_bus_speed *value)
{
	struct slot *slot = get_slot(hotplug_slot, __FUNCTION__);
	int retval;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	retval = slot->hpc_ops->get_max_bus_speed(slot, value);
	if (retval < 0)
		*value = PCI_SPEED_UNKNOWN;

	return 0;
}

static int get_cur_bus_speed (struct hotplug_slot *hotplug_slot, enum pci_bus_speed *value)
{
	struct slot *slot = get_slot(hotplug_slot, __FUNCTION__);
	int retval;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	retval = slot->hpc_ops->get_cur_bus_speed(slot, value);
	if (retval < 0)
		*value = PCI_SPEED_UNKNOWN;

	return 0;
}

static int is_shpc_capable(struct pci_dev *dev)
{
       if ((dev->vendor == PCI_VENDOR_ID_AMD) || (dev->device ==
                               PCI_DEVICE_ID_AMD_GOLAM_7450))
               return 1;
       if (pci_find_capability(dev, PCI_CAP_ID_SHPC))
               return 1;

       return 0;
}

static int shpc_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int rc;
	struct controller *ctrl;
	struct slot *t_slot;
	int first_device_num;	/* first PCI device number */
	int num_ctlr_slots;	/* number of slots implemented */

	if (!is_shpc_capable(pdev))
		return -ENODEV;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl) {
		err("%s : out of memory\n", __FUNCTION__);
		goto err_out_none;
	}
	INIT_LIST_HEAD(&ctrl->slot_list);

	rc = shpc_init(ctrl, pdev);
	if (rc) {
		dbg("%s: controller initialization failed\n",
		    SHPC_MODULE_NAME);
		goto err_out_free_ctrl;
	}

	pci_set_drvdata(pdev, ctrl);

	ctrl->bus = pdev->bus->number;
	ctrl->slot_bus = pdev->subordinate->number;
	ctrl->device = PCI_SLOT(pdev->devfn);
	ctrl->function = PCI_FUNC(pdev->devfn);

	dbg("ctrl bus=0x%x, device=%x, function=%x, irq=%x\n",
	    ctrl->bus, ctrl->device, ctrl->function, pdev->irq);

	/*
	 * Save configuration headers for this and subordinate PCI buses
	 */
	rc = get_ctlr_slot_config(ctrl);
	if (rc) {
		err(msg_initialization_err, rc);
		goto err_out_release_ctlr;
	}
	first_device_num = ctrl->slot_device_offset;
	num_ctlr_slots = ctrl->num_slots;

	ctrl->add_support = 1;

	/* Setup the slot information structures */
	rc = init_slots(ctrl);
	if (rc) {
		err(msg_initialization_err, 6);
		goto err_out_release_ctlr;
	}

	/* Now hpc_functions (slot->hpc_ops->functions) are ready  */
	t_slot = shpchp_find_slot(ctrl, first_device_num);

	/* Check for operation bus speed */
	rc = t_slot->hpc_ops->get_cur_bus_speed(t_slot, &ctrl->speed);
	dbg("%s: t_slot->hp_slot %x\n", __FUNCTION__,t_slot->hp_slot);

	if (rc || ctrl->speed == PCI_SPEED_UNKNOWN) {
		err(SHPC_MODULE_NAME ": Can't get current bus speed. "
		    "Set to 33MHz PCI.\n");
		ctrl->speed = PCI_SPEED_33MHz;
	}

	shpchp_create_ctrl_files(ctrl);

	return 0;

err_out_release_ctlr:
	ctrl->hpc_ops->release_ctlr(ctrl);
err_out_free_ctrl:
	kfree(ctrl);
err_out_none:
	return -ENODEV;
}

static void shpc_remove(struct pci_dev *dev)
{
	struct controller *ctrl = pci_get_drvdata(dev);

	shpchp_remove_ctrl_files(ctrl);
	ctrl->hpc_ops->release_ctlr(ctrl);
	kfree(ctrl);
}

static struct pci_device_id shpcd_pci_tbl[] = {
	{PCI_DEVICE_CLASS(((PCI_CLASS_BRIDGE_PCI << 8) | 0x00), ~0)},
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, shpcd_pci_tbl);

static struct pci_driver shpc_driver = {
	.name =		SHPC_MODULE_NAME,
	.id_table =	shpcd_pci_tbl,
	.probe =	shpc_probe,
	.remove =	shpc_remove,
};

static int __init shpcd_init(void)
{
	int retval = 0;

#ifdef CONFIG_HOTPLUG_PCI_SHPC_POLL_EVENT_MODE
	shpchp_poll_mode = 1;
#endif

	shpchp_wq = create_singlethread_workqueue("shpchpd");
	if (!shpchp_wq)
		return -ENOMEM;

	retval = pci_register_driver(&shpc_driver);
	dbg("%s: pci_register_driver = %d\n", __FUNCTION__, retval);
	info(DRIVER_DESC " version: " DRIVER_VERSION "\n");
	if (retval) {
		destroy_workqueue(shpchp_wq);
	}
	return retval;
}

static void __exit shpcd_cleanup(void)
{
	dbg("unload_shpchpd()\n");
	pci_unregister_driver(&shpc_driver);
	destroy_workqueue(shpchp_wq);
	info(DRIVER_DESC " version: " DRIVER_VERSION " unloaded\n");
}

module_init(shpcd_init);
module_exit(shpcd_cleanup);

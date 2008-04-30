/*
 * PCI Express Hot Plug Controller Driver
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
#include "pciehp.h"
#include <linux/interrupt.h>
#include <linux/time.h>

/* Global variables */
int pciehp_debug;
int pciehp_poll_mode;
int pciehp_poll_time;
int pciehp_force;
struct workqueue_struct *pciehp_wq;

#define DRIVER_VERSION	"0.4"
#define DRIVER_AUTHOR	"Dan Zink <dan.zink@compaq.com>, Greg Kroah-Hartman <greg@kroah.com>, Dely Sy <dely.l.sy@intel.com>"
#define DRIVER_DESC	"PCI Express Hot Plug Controller Driver"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(pciehp_debug, bool, 0644);
module_param(pciehp_poll_mode, bool, 0644);
module_param(pciehp_poll_time, int, 0644);
module_param(pciehp_force, bool, 0644);
MODULE_PARM_DESC(pciehp_debug, "Debugging mode enabled or not");
MODULE_PARM_DESC(pciehp_poll_mode, "Using polling mechanism for hot-plug events or not");
MODULE_PARM_DESC(pciehp_poll_time, "Polling mechanism frequency, in seconds");
MODULE_PARM_DESC(pciehp_force, "Force pciehp, even if _OSC and OSHP are missing");

#define PCIE_MODULE_NAME "pciehp"

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

static struct hotplug_slot_ops pciehp_hotplug_slot_ops = {
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

/*
 * Check the status of the Electro Mechanical Interlock (EMI)
 */
static int get_lock_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;
	return (slot->hpc_ops->get_emi_status(slot, value));
}

/*
 * sysfs interface for the Electro Mechanical Interlock (EMI)
 * 1 == locked, 0 == unlocked
 */
static ssize_t lock_read_file(struct hotplug_slot *slot, char *buf)
{
	int retval;
	u8 value;

	retval = get_lock_status(slot, &value);
	if (retval)
		goto lock_read_exit;
	retval = sprintf (buf, "%d\n", value);

lock_read_exit:
	return retval;
}

/*
 * Change the status of the Electro Mechanical Interlock (EMI)
 * This is a toggle - in addition there must be at least 1 second
 * in between toggles.
 */
static int set_lock_status(struct hotplug_slot *hotplug_slot, u8 status)
{
	struct slot *slot = hotplug_slot->private;
	int retval;
	u8 value;

	mutex_lock(&slot->ctrl->crit_sect);

	/* has it been >1 sec since our last toggle? */
	if ((get_seconds() - slot->last_emi_toggle) < 1)
		return -EINVAL;

	/* see what our current state is */
	retval = get_lock_status(hotplug_slot, &value);
	if (retval || (value == status))
		goto set_lock_exit;

	slot->hpc_ops->toggle_emi(slot);
set_lock_exit:
	mutex_unlock(&slot->ctrl->crit_sect);
	return 0;
}

/*
 * sysfs interface which allows the user to toggle the Electro Mechanical
 * Interlock.  Valid values are either 0 or 1.  0 == unlock, 1 == lock
 */
static ssize_t lock_write_file(struct hotplug_slot *slot, const char *buf,
		size_t count)
{
	unsigned long llock;
	u8 lock;
	int retval = 0;

	llock = simple_strtoul(buf, NULL, 10);
	lock = (u8)(llock & 0xff);

	switch (lock) {
		case 0:
		case 1:
			retval = set_lock_status(slot, lock);
			break;
		default:
			err ("%d is an invalid lock value\n", lock);
			retval = -EINVAL;
	}
	if (retval)
		return retval;
	return count;
}

static struct hotplug_slot_attribute hotplug_slot_attr_lock = {
	.attr = {.name = "lock", .mode = S_IFREG | S_IRUGO | S_IWUSR},
	.show = lock_read_file,
	.store = lock_write_file
};

/**
 * release_slot - free up the memory used by a slot
 * @hotplug_slot: slot to free
 */
static void release_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __func__, hotplug_slot->name);

	kfree(slot->hotplug_slot->info);
	kfree(slot->hotplug_slot);
	kfree(slot);
}

static void make_slot_name(struct slot *slot)
{
	snprintf(slot->hotplug_slot->name, SLOT_NAME_SIZE, "%04d_%04d",
		 slot->bus, slot->number);
}

static int init_slots(struct controller *ctrl)
{
	struct slot *slot;
	struct hotplug_slot *hotplug_slot;
	struct hotplug_slot_info *info;
	int retval = -ENOMEM;
	int i;

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
		slot->bus = ctrl->pci_dev->subordinate->number;
		slot->device = ctrl->slot_device_offset + i;
		slot->hpc_ops = ctrl->hpc_ops;
		slot->number = ctrl->first_slot;
		mutex_init(&slot->lock);
		INIT_DELAYED_WORK(&slot->work, pciehp_queue_pushbutton_work);

		/* register this slot with the hotplug pci core */
		hotplug_slot->private = slot;
		hotplug_slot->release = &release_slot;
		make_slot_name(slot);
		hotplug_slot->ops = &pciehp_hotplug_slot_ops;

		get_power_status(hotplug_slot, &info->power_status);
		get_attention_status(hotplug_slot, &info->attention_status);
		get_latch_status(hotplug_slot, &info->latch_status);
		get_adapter_status(hotplug_slot, &info->adapter_status);

		dbg("Registering bus=%x dev=%x hp_slot=%x sun=%x "
		    "slot_device_offset=%x\n", slot->bus, slot->device,
		    slot->hp_slot, slot->number, ctrl->slot_device_offset);
		retval = pci_hp_register(hotplug_slot);
		if (retval) {
			err ("pci_hp_register failed with error %d\n", retval);
			goto error_info;
		}
		/* create additional sysfs entries */
		if (EMI(ctrl->ctrlcap)) {
			retval = sysfs_create_file(&hotplug_slot->kobj,
				&hotplug_slot_attr_lock.attr);
			if (retval) {
				pci_hp_deregister(hotplug_slot);
				err("cannot create additional sysfs entries\n");
				goto error_info;
			}
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

static void cleanup_slots(struct controller *ctrl)
{
	struct list_head *tmp;
	struct list_head *next;
	struct slot *slot;

	list_for_each_safe(tmp, next, &ctrl->slot_list) {
		slot = list_entry(tmp, struct slot, slot_list);
		list_del(&slot->slot_list);
		if (EMI(ctrl->ctrlcap))
			sysfs_remove_file(&slot->hotplug_slot->kobj,
				&hotplug_slot_attr_lock.attr);
		cancel_delayed_work(&slot->work);
		flush_scheduled_work();
		flush_workqueue(pciehp_wq);
		pci_hp_deregister(slot->hotplug_slot);
	}
}

/*
 * set_attention_status - Turns the Amber LED for a slot on, off or blink
 */
static int set_attention_status(struct hotplug_slot *hotplug_slot, u8 status)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __func__, hotplug_slot->name);

	hotplug_slot->info->attention_status = status;

	if (ATTN_LED(slot->ctrl->ctrlcap))
		slot->hpc_ops->set_attention_status(slot, status);

	return 0;
}


static int enable_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __func__, hotplug_slot->name);

	return pciehp_sysfs_enable_slot(slot);
}


static int disable_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __func__, hotplug_slot->name);

	return pciehp_sysfs_disable_slot(slot);
}

static int get_power_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;
	int retval;

	dbg("%s - physical_slot = %s\n", __func__, hotplug_slot->name);

	retval = slot->hpc_ops->get_power_status(slot, value);
	if (retval < 0)
		*value = hotplug_slot->info->power_status;

	return 0;
}

static int get_attention_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;
	int retval;

	dbg("%s - physical_slot = %s\n", __func__, hotplug_slot->name);

	retval = slot->hpc_ops->get_attention_status(slot, value);
	if (retval < 0)
		*value = hotplug_slot->info->attention_status;

	return 0;
}

static int get_latch_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;
	int retval;

	dbg("%s - physical_slot = %s\n", __func__, hotplug_slot->name);

	retval = slot->hpc_ops->get_latch_status(slot, value);
	if (retval < 0)
		*value = hotplug_slot->info->latch_status;

	return 0;
}

static int get_adapter_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;
	int retval;

	dbg("%s - physical_slot = %s\n", __func__, hotplug_slot->name);

	retval = slot->hpc_ops->get_adapter_status(slot, value);
	if (retval < 0)
		*value = hotplug_slot->info->adapter_status;

	return 0;
}

static int get_address(struct hotplug_slot *hotplug_slot, u32 *value)
{
	struct slot *slot = hotplug_slot->private;
	struct pci_bus *bus = slot->ctrl->pci_dev->subordinate;

	dbg("%s - physical_slot = %s\n", __func__, hotplug_slot->name);

	*value = (pci_domain_nr(bus) << 16) | (slot->bus << 8) | slot->device;

	return 0;
}

static int get_max_bus_speed(struct hotplug_slot *hotplug_slot, enum pci_bus_speed *value)
{
	struct slot *slot = hotplug_slot->private;
	int retval;

	dbg("%s - physical_slot = %s\n", __func__, hotplug_slot->name);

	retval = slot->hpc_ops->get_max_bus_speed(slot, value);
	if (retval < 0)
		*value = PCI_SPEED_UNKNOWN;

	return 0;
}

static int get_cur_bus_speed(struct hotplug_slot *hotplug_slot, enum pci_bus_speed *value)
{
	struct slot *slot = hotplug_slot->private;
	int retval;

	dbg("%s - physical_slot = %s\n", __func__, hotplug_slot->name);

	retval = slot->hpc_ops->get_cur_bus_speed(slot, value);
	if (retval < 0)
		*value = PCI_SPEED_UNKNOWN;

	return 0;
}

static int pciehp_probe(struct pcie_device *dev, const struct pcie_port_service_id *id)
{
	int rc;
	struct controller *ctrl;
	struct slot *t_slot;
	u8 value;
	struct pci_dev *pdev;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl) {
		err("%s : out of memory\n", __func__);
		goto err_out_none;
	}
	INIT_LIST_HEAD(&ctrl->slot_list);

	pdev = dev->port;
	ctrl->pci_dev = pdev;

	rc = pcie_init(ctrl, dev);
	if (rc) {
		dbg("%s: controller initialization failed\n", PCIE_MODULE_NAME);
		goto err_out_free_ctrl;
	}

	pci_set_drvdata(pdev, ctrl);

	dbg("%s: ctrl bus=0x%x, device=%x, function=%x, irq=%x\n",
	    __func__, pdev->bus->number, PCI_SLOT(pdev->devfn),
	    PCI_FUNC(pdev->devfn), pdev->irq);

	/* Setup the slot information structures */
	rc = init_slots(ctrl);
	if (rc) {
		err("%s: slot initialization failed\n", PCIE_MODULE_NAME);
		goto err_out_release_ctlr;
	}

	t_slot = pciehp_find_slot(ctrl, ctrl->slot_device_offset);

	t_slot->hpc_ops->get_adapter_status(t_slot, &value); /* Check if slot is occupied */
	if (value && pciehp_force) {
		rc = pciehp_enable_slot(t_slot);
		if (rc)	/* -ENODEV: shouldn't happen, but deal with it */
			value = 0;
	}
	if ((POWER_CTRL(ctrl->ctrlcap)) && !value) {
		rc = t_slot->hpc_ops->power_off_slot(t_slot); /* Power off slot if not occupied*/
		if (rc)
			goto err_out_free_ctrl_slot;
	}

	return 0;

err_out_free_ctrl_slot:
	cleanup_slots(ctrl);
err_out_release_ctlr:
	ctrl->hpc_ops->release_ctlr(ctrl);
err_out_free_ctrl:
	kfree(ctrl);
err_out_none:
	return -ENODEV;
}

static void pciehp_remove (struct pcie_device *dev)
{
	struct pci_dev *pdev = dev->port;
	struct controller *ctrl = pci_get_drvdata(pdev);

	cleanup_slots(ctrl);
	ctrl->hpc_ops->release_ctlr(ctrl);
	kfree(ctrl);
}

#ifdef CONFIG_PM
static int pciehp_suspend (struct pcie_device *dev, pm_message_t state)
{
	printk("%s ENTRY\n", __func__);
	return 0;
}

static int pciehp_resume (struct pcie_device *dev)
{
	printk("%s ENTRY\n", __func__);
	if (pciehp_force) {
		struct pci_dev *pdev = dev->port;
		struct controller *ctrl = pci_get_drvdata(pdev);
		struct slot *t_slot;
		u8 status;

		/* reinitialize the chipset's event detection logic */
		pcie_init_hardware_part2(ctrl, dev);

		t_slot = pciehp_find_slot(ctrl, ctrl->slot_device_offset);

		/* Check if slot is occupied */
		t_slot->hpc_ops->get_adapter_status(t_slot, &status);
		if (status)
			pciehp_enable_slot(t_slot);
		else
			pciehp_disable_slot(t_slot);
	}
	return 0;
}
#endif

static struct pcie_port_service_id port_pci_ids[] = { {
	.vendor = PCI_ANY_ID,
	.device = PCI_ANY_ID,
	.port_type = PCIE_ANY_PORT,
	.service_type = PCIE_PORT_SERVICE_HP,
	.driver_data =	0,
	}, { /* end: all zeroes */ }
};
static const char device_name[] = "hpdriver";

static struct pcie_port_service_driver hpdriver_portdrv = {
	.name		= (char *)device_name,
	.id_table	= &port_pci_ids[0],

	.probe		= pciehp_probe,
	.remove		= pciehp_remove,

#ifdef	CONFIG_PM
	.suspend	= pciehp_suspend,
	.resume		= pciehp_resume,
#endif	/* PM */
};

static int __init pcied_init(void)
{
	int retval = 0;

	retval = pcie_port_service_register(&hpdriver_portdrv);
 	dbg("pcie_port_service_register = %d\n", retval);
  	info(DRIVER_DESC " version: " DRIVER_VERSION "\n");
 	if (retval)
		dbg("%s: Failure to register service\n", __func__);
	return retval;
}

static void __exit pcied_cleanup(void)
{
	dbg("unload_pciehpd()\n");
	pcie_port_service_unregister(&hpdriver_portdrv);
	info(DRIVER_DESC " version: " DRIVER_VERSION " unloaded\n");
}

module_init(pcied_init);
module_exit(pcied_cleanup);

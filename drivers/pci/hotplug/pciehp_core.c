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

/* Global variables */
int pciehp_debug;
int pciehp_poll_mode;
int pciehp_poll_time;
int pciehp_force;
struct controller *pciehp_ctrl_list;

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

static int pcie_start_thread (void);
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

/**
 * release_slot - free up the memory used by a slot
 * @hotplug_slot: slot to free
 */
static void release_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	kfree(slot->hotplug_slot->info);
	kfree(slot->hotplug_slot->name);
	kfree(slot->hotplug_slot);
	kfree(slot);
}

static int init_slots(struct controller *ctrl)
{
	struct slot *slot;
	struct hpc_ops *hpc_ops;
	struct hotplug_slot *hotplug_slot;
	struct hotplug_slot_info *hotplug_slot_info;
	u8 number_of_slots;
	u8 slot_device;
	u32 slot_number;
	int result = -ENOMEM;

	number_of_slots = ctrl->num_slots;
	slot_device = ctrl->slot_device_offset;
	slot_number = ctrl->first_slot;

	while (number_of_slots) {
		slot = kzalloc(sizeof(*slot), GFP_KERNEL);
		if (!slot)
			goto error;

		slot->hotplug_slot =
				kzalloc(sizeof(*(slot->hotplug_slot)),
						GFP_KERNEL);
		if (!slot->hotplug_slot)
			goto error_slot;
		hotplug_slot = slot->hotplug_slot;

		hotplug_slot->info =
			kzalloc(sizeof(*(hotplug_slot->info)),
						GFP_KERNEL);
		if (!hotplug_slot->info)
			goto error_hpslot;
		hotplug_slot_info = hotplug_slot->info;
		hotplug_slot->name = kmalloc(SLOT_NAME_SIZE, GFP_KERNEL);
		if (!hotplug_slot->name)
			goto error_info;

		slot->ctrl = ctrl;
		slot->bus = ctrl->slot_bus;
		slot->device = slot_device;
		slot->hpc_ops = hpc_ops = ctrl->hpc_ops;

		slot->number = ctrl->first_slot;
		slot->hp_slot = slot_device - ctrl->slot_device_offset;

		/* register this slot with the hotplug pci core */
		hotplug_slot->private = slot;
		hotplug_slot->release = &release_slot;
		make_slot_name(hotplug_slot->name, SLOT_NAME_SIZE, slot);
		hotplug_slot->ops = &pciehp_hotplug_slot_ops;

		hpc_ops->get_power_status(slot,
			&(hotplug_slot_info->power_status));
		hpc_ops->get_attention_status(slot,
			&(hotplug_slot_info->attention_status));
		hpc_ops->get_latch_status(slot,
			&(hotplug_slot_info->latch_status));
		hpc_ops->get_adapter_status(slot,
			&(hotplug_slot_info->adapter_status));

		dbg("Registering bus=%x dev=%x hp_slot=%x sun=%x "
			"slot_device_offset=%x\n",
			slot->bus, slot->device, slot->hp_slot, slot->number,
			ctrl->slot_device_offset);
		result = pci_hp_register(hotplug_slot);
		if (result) {
			err ("pci_hp_register failed with error %d\n", result);
			goto error_name;
		}

		slot->next = ctrl->slot;
		ctrl->slot = slot;

		number_of_slots--;
		slot_device++;
		slot_number += ctrl->slot_num_inc;
	}

	return 0;

error_name:
	kfree(hotplug_slot->name);
error_info:
	kfree(hotplug_slot_info);
error_hpslot:
	kfree(hotplug_slot);
error_slot:
	kfree(slot);
error:
	return result;
}


static int cleanup_slots (struct controller * ctrl)
{
	struct slot *old_slot, *next_slot;

	old_slot = ctrl->slot;
	ctrl->slot = NULL;

	while (old_slot) {
		next_slot = old_slot->next;
		pci_hp_deregister (old_slot->hotplug_slot);
		old_slot = next_slot;
	}


	return(0);
}

static int get_ctlr_slot_config(struct controller *ctrl)
{
	int num_ctlr_slots;		/* Not needed; PCI Express has 1 slot per port*/
	int first_device_num;		/* Not needed */
	int physical_slot_num;
	u8 ctrlcap;			
	int rc;

	rc = pcie_get_ctlr_slot_config(ctrl, &num_ctlr_slots, &first_device_num, &physical_slot_num, &ctrlcap);
	if (rc) {
		err("%s: get_ctlr_slot_config fail for b:d (%x:%x)\n", __FUNCTION__, ctrl->bus, ctrl->device);
		return (-1);
	}

	ctrl->num_slots = num_ctlr_slots;	/* PCI Express has 1 slot per port */
	ctrl->slot_device_offset = first_device_num;
	ctrl->first_slot = physical_slot_num;
	ctrl->ctrlcap = ctrlcap; 	

	dbg("%s: bus(0x%x) num_slot(0x%x) 1st_dev(0x%x) psn(0x%x) ctrlcap(%x) for b:d (%x:%x)\n",
		__FUNCTION__, ctrl->slot_bus, num_ctlr_slots, first_device_num, physical_slot_num, ctrlcap, 
		ctrl->bus, ctrl->device);

	return (0);
}


/*
 * set_attention_status - Turns the Amber LED for a slot on, off or blink
 */
static int set_attention_status(struct hotplug_slot *hotplug_slot, u8 status)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	hotplug_slot->info->attention_status = status;
	
	if (ATTN_LED(slot->ctrl->ctrlcap)) 
		slot->hpc_ops->set_attention_status(slot, status);

	return 0;
}


static int enable_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	return pciehp_enable_slot(slot);
}


static int disable_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	return pciehp_disable_slot(slot);
}

static int get_power_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;
	int retval;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	retval = slot->hpc_ops->get_power_status(slot, value);
	if (retval < 0)
		*value = hotplug_slot->info->power_status;

	return 0;
}

static int get_attention_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;
	int retval;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	retval = slot->hpc_ops->get_attention_status(slot, value);
	if (retval < 0)
		*value = hotplug_slot->info->attention_status;

	return 0;
}

static int get_latch_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;
	int retval;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	retval = slot->hpc_ops->get_latch_status(slot, value);
	if (retval < 0)
		*value = hotplug_slot->info->latch_status;

	return 0;
}

static int get_adapter_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;
	int retval;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	retval = slot->hpc_ops->get_adapter_status(slot, value);
	if (retval < 0)
		*value = hotplug_slot->info->adapter_status;

	return 0;
}

static int get_address(struct hotplug_slot *hotplug_slot, u32 *value)
{
	struct slot *slot = hotplug_slot->private;
	struct pci_bus *bus = slot->ctrl->pci_dev->subordinate;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	*value = (pci_domain_nr(bus) << 16) | (slot->bus << 8) | slot->device;

	return 0;
}

static int get_max_bus_speed(struct hotplug_slot *hotplug_slot, enum pci_bus_speed *value)
{
	struct slot *slot = hotplug_slot->private;
	int retval;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);
	
	retval = slot->hpc_ops->get_max_bus_speed(slot, value);
	if (retval < 0)
		*value = PCI_SPEED_UNKNOWN;

	return 0;
}

static int get_cur_bus_speed(struct hotplug_slot *hotplug_slot, enum pci_bus_speed *value)
{
	struct slot *slot = hotplug_slot->private;
	int retval;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);
	
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
	int first_device_num = 0 ;	/* first PCI device number supported by this PCIE */  
	int num_ctlr_slots;		/* number of slots supported by this HPC */
	u8 value;
	struct pci_dev *pdev;
	
	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl) {
		err("%s : out of memory\n", __FUNCTION__);
		goto err_out_none;
	}

	pdev = dev->port;
	ctrl->pci_dev = pdev;

	rc = pcie_init(ctrl, dev);
	if (rc) {
		dbg("%s: controller initialization failed\n", PCIE_MODULE_NAME);
		goto err_out_free_ctrl;
	}

	pci_set_drvdata(pdev, ctrl);

	ctrl->pci_bus = kmalloc(sizeof(*ctrl->pci_bus), GFP_KERNEL);
	if (!ctrl->pci_bus) {
		err("%s: out of memory\n", __FUNCTION__);
		rc = -ENOMEM;
		goto err_out_unmap_mmio_region;
	}
	memcpy (ctrl->pci_bus, pdev->bus, sizeof (*ctrl->pci_bus));
	ctrl->bus = pdev->bus->number;  /* ctrl bus */
	ctrl->slot_bus = pdev->subordinate->number;  /* bus controlled by this HPC */

	ctrl->device = PCI_SLOT(pdev->devfn);
	ctrl->function = PCI_FUNC(pdev->devfn);
	dbg("%s: ctrl bus=0x%x, device=%x, function=%x, irq=%x\n", __FUNCTION__,
		ctrl->bus, ctrl->device, ctrl->function, pdev->irq);

	/*
	 *	Save configuration headers for this and subordinate PCI buses
	 */

	rc = get_ctlr_slot_config(ctrl);
	if (rc) {
		err(msg_initialization_err, rc);
		goto err_out_free_ctrl_bus;
	}
	first_device_num = ctrl->slot_device_offset;
	num_ctlr_slots = ctrl->num_slots; 

	/* Setup the slot information structures */
	rc = init_slots(ctrl);
	if (rc) {
		err(msg_initialization_err, 6);
		goto err_out_free_ctrl_slot;
	}

	t_slot = pciehp_find_slot(ctrl, first_device_num);

	/*	Finish setting up the hot plug ctrl device */
	ctrl->next_event = 0;

	if (!pciehp_ctrl_list) {
		pciehp_ctrl_list = ctrl;
		ctrl->next = NULL;
	} else {
		ctrl->next = pciehp_ctrl_list;
		pciehp_ctrl_list = ctrl;
	}

	/* Wait for exclusive access to hardware */
	mutex_lock(&ctrl->ctrl_lock);

	t_slot->hpc_ops->get_adapter_status(t_slot, &value); /* Check if slot is occupied */
	
	if ((POWER_CTRL(ctrl->ctrlcap)) && !value) {
		rc = t_slot->hpc_ops->power_off_slot(t_slot); /* Power off slot if not occupied*/
		if (rc) {
			/* Done with exclusive hardware access */
			mutex_unlock(&ctrl->ctrl_lock);
			goto err_out_free_ctrl_slot;
		} else
			/* Wait for the command to complete */
			wait_for_ctrl_irq (ctrl);
	}

	/* Done with exclusive hardware access */
	mutex_unlock(&ctrl->ctrl_lock);

	return 0;

err_out_free_ctrl_slot:
	cleanup_slots(ctrl);
err_out_free_ctrl_bus:
	kfree(ctrl->pci_bus);
err_out_unmap_mmio_region:
	ctrl->hpc_ops->release_ctlr(ctrl);
err_out_free_ctrl:
	kfree(ctrl);
err_out_none:
	return -ENODEV;
}


static int pcie_start_thread(void)
{
	int retval = 0;
	
	dbg("Initialize + Start the notification/polling mechanism \n");

	retval = pciehp_event_start_thread();
	if (retval) {
		dbg("pciehp_event_start_thread() failed\n");
		return retval;
	}

	return retval;
}

static void __exit unload_pciehpd(void)
{
	struct controller *ctrl;
	struct controller *tctrl;

	ctrl = pciehp_ctrl_list;

	while (ctrl) {
		cleanup_slots(ctrl);

		kfree (ctrl->pci_bus);

		ctrl->hpc_ops->release_ctlr(ctrl);

		tctrl = ctrl;
		ctrl = ctrl->next;

		kfree(tctrl);
	}

	/* Stop the notification mechanism */
	pciehp_event_stop_thread();

}

static int hpdriver_context = 0;

static void pciehp_remove (struct pcie_device *device)
{
	printk("%s ENTRY\n", __FUNCTION__);	
	printk("%s -> Call free_irq for irq = %d\n",  
		__FUNCTION__, device->irq);
	free_irq(device->irq, &hpdriver_context);
}

#ifdef CONFIG_PM
static int pciehp_suspend (struct pcie_device *dev, pm_message_t state)
{
	printk("%s ENTRY\n", __FUNCTION__);	
	return 0;
}

static int pciehp_resume (struct pcie_device *dev)
{
	printk("%s ENTRY\n", __FUNCTION__);	
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

#ifdef CONFIG_HOTPLUG_PCI_PCIE_POLL_EVENT_MODE
	pciehp_poll_mode = 1;
#endif

	retval = pcie_start_thread();
	if (retval)
		goto error_hpc_init;

	retval = pcie_port_service_register(&hpdriver_portdrv);
 	dbg("pcie_port_service_register = %d\n", retval);
  	info(DRIVER_DESC " version: " DRIVER_VERSION "\n");
 	if (retval)
		dbg("%s: Failure to register service\n", __FUNCTION__);

error_hpc_init:
	if (retval) {
		pciehp_event_stop_thread();
	};

	return retval;
}

static void __exit pcied_cleanup(void)
{
	dbg("unload_pciehpd()\n");
	unload_pciehpd();

	pcie_port_service_unregister(&hpdriver_portdrv);

	info(DRIVER_DESC " version: " DRIVER_VERSION " unloaded\n");
}

module_init(pcied_init);
module_exit(pcied_cleanup);

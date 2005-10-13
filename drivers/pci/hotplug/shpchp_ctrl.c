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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/smp_lock.h>
#include <linux/pci.h>
#include "../pci.h"
#include "shpchp.h"

static void interrupt_event_handler(struct controller *ctrl);

static struct semaphore event_semaphore;	/* mutex for process loop (up if something to process) */
static struct semaphore event_exit;		/* guard ensure thread has exited before calling it quits */
static int event_finished;
static unsigned long pushbutton_pending;	/* = 0 */

u8 shpchp_handle_attention_button(u8 hp_slot, void *inst_id)
{
	struct controller *ctrl = (struct controller *) inst_id;
	struct slot *p_slot;
	u8 rc = 0;
	u8 getstatus;
	struct pci_func *func;
	struct event_info *taskInfo;

	/* Attention Button Change */
	dbg("shpchp:  Attention button interrupt received.\n");
	
	func = shpchp_slot_find(ctrl->slot_bus, (hp_slot + ctrl->slot_device_offset), 0);

	/* This is the structure that tells the worker thread what to do */
	taskInfo = &(ctrl->event_queue[ctrl->next_event]);
	p_slot = shpchp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	p_slot->hpc_ops->get_adapter_status(p_slot, &(func->presence_save));
	p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
	
	ctrl->next_event = (ctrl->next_event + 1) % 10;
	taskInfo->hp_slot = hp_slot;

	rc++;

	/*
	 *  Button pressed - See if need to TAKE ACTION!!!
	 */
	info("Button pressed on Slot(%d)\n", ctrl->first_slot + hp_slot);
	taskInfo->event_type = INT_BUTTON_PRESS;

	if ((p_slot->state == BLINKINGON_STATE)
	    || (p_slot->state == BLINKINGOFF_STATE)) {
		/* Cancel if we are still blinking; this means that we press the
		 * attention again before the 5 sec. limit expires to cancel hot-add
		 * or hot-remove
		 */
		taskInfo->event_type = INT_BUTTON_CANCEL;
		info("Button cancel on Slot(%d)\n", ctrl->first_slot + hp_slot);
	} else if ((p_slot->state == POWERON_STATE)
		   || (p_slot->state == POWEROFF_STATE)) {
		/* Ignore if the slot is on power-on or power-off state; this 
		 * means that the previous attention button action to hot-add or
		 * hot-remove is undergoing
		 */
		taskInfo->event_type = INT_BUTTON_IGNORE;
		info("Button ignore on Slot(%d)\n", ctrl->first_slot + hp_slot);
	}

	if (rc)
		up(&event_semaphore);	/* signal event thread that new event is posted */

	return 0;

}

u8 shpchp_handle_switch_change(u8 hp_slot, void *inst_id)
{
	struct controller *ctrl = (struct controller *) inst_id;
	struct slot *p_slot;
	u8 rc = 0;
	u8 getstatus;
	struct pci_func *func;
	struct event_info *taskInfo;

	/* Switch Change */
	dbg("shpchp:  Switch interrupt received.\n");

	func = shpchp_slot_find(ctrl->slot_bus, (hp_slot + ctrl->slot_device_offset), 0);

	/* This is the structure that tells the worker thread
	 * what to do
	 */
	taskInfo = &(ctrl->event_queue[ctrl->next_event]);
	ctrl->next_event = (ctrl->next_event + 1) % 10;
	taskInfo->hp_slot = hp_slot;

	rc++;
	p_slot = shpchp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);
	p_slot->hpc_ops->get_adapter_status(p_slot, &(func->presence_save));
	p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
	dbg("%s: Card present %x Power status %x\n", __FUNCTION__,
		func->presence_save, func->pwr_save);

	if (getstatus) {
		/*
		 * Switch opened
		 */
		info("Latch open on Slot(%d)\n", ctrl->first_slot + hp_slot);
		func->switch_save = 0;
		taskInfo->event_type = INT_SWITCH_OPEN;
		if (func->pwr_save && func->presence_save) {
			taskInfo->event_type = INT_POWER_FAULT;
			err("Surprise Removal of card\n");
		}
	} else {
		/*
		 *  Switch closed
		 */
		info("Latch close on Slot(%d)\n", ctrl->first_slot + hp_slot);
		func->switch_save = 0x10;
		taskInfo->event_type = INT_SWITCH_CLOSE;
	}

	if (rc)
		up(&event_semaphore);	/* signal event thread that new event is posted */

	return rc;
}

u8 shpchp_handle_presence_change(u8 hp_slot, void *inst_id)
{
	struct controller *ctrl = (struct controller *) inst_id;
	struct slot *p_slot;
	u8 rc = 0;
	/*u8 temp_byte;*/
	struct pci_func *func;
	struct event_info *taskInfo;

	/* Presence Change */
	dbg("shpchp:  Presence/Notify input change.\n");

	func = shpchp_slot_find(ctrl->slot_bus, (hp_slot + ctrl->slot_device_offset), 0);

	/* This is the structure that tells the worker thread
	 * what to do
	 */
	taskInfo = &(ctrl->event_queue[ctrl->next_event]);
	ctrl->next_event = (ctrl->next_event + 1) % 10;
	taskInfo->hp_slot = hp_slot;

	rc++;
	p_slot = shpchp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	/* 
	 * Save the presence state
	 */
	p_slot->hpc_ops->get_adapter_status(p_slot, &(func->presence_save));
	if (func->presence_save) {
		/*
		 * Card Present
		 */
		info("Card present on Slot(%d)\n", ctrl->first_slot + hp_slot);
		taskInfo->event_type = INT_PRESENCE_ON;
	} else {
		/*
		 * Not Present
		 */
		info("Card not present on Slot(%d)\n", ctrl->first_slot + hp_slot);
		taskInfo->event_type = INT_PRESENCE_OFF;
	}

	if (rc)
		up(&event_semaphore);	/* signal event thread that new event is posted */

	return rc;
}

u8 shpchp_handle_power_fault(u8 hp_slot, void *inst_id)
{
	struct controller *ctrl = (struct controller *) inst_id;
	struct slot *p_slot;
	u8 rc = 0;
	struct pci_func *func;
	struct event_info *taskInfo;

	/* Power fault */
	dbg("shpchp:  Power fault interrupt received.\n");

	func = shpchp_slot_find(ctrl->slot_bus, (hp_slot + ctrl->slot_device_offset), 0);

	/* This is the structure that tells the worker thread
	 * what to do
	 */
	taskInfo = &(ctrl->event_queue[ctrl->next_event]);
	ctrl->next_event = (ctrl->next_event + 1) % 10;
	taskInfo->hp_slot = hp_slot;

	rc++;
	p_slot = shpchp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	if ( !(p_slot->hpc_ops->query_power_fault(p_slot))) {
		/*
		 * Power fault Cleared
		 */
		info("Power fault cleared on Slot(%d)\n", ctrl->first_slot + hp_slot);
		func->status = 0x00;
		taskInfo->event_type = INT_POWER_FAULT_CLEAR;
	} else {
		/*
		 *   Power fault
		 */
		info("Power fault on Slot(%d)\n", ctrl->first_slot + hp_slot);
		taskInfo->event_type = INT_POWER_FAULT;
		/* set power fault status for this board */
		func->status = 0xFF;
		info("power fault bit %x set\n", hp_slot);
	}
	if (rc)
		up(&event_semaphore);	/* signal event thread that new event is posted */

	return rc;
}

/**
 * shpchp_slot_create - Creates a node and adds it to the proper bus.
 * @busnumber - bus where new node is to be located
 *
 * Returns pointer to the new node or NULL if unsuccessful
 */
struct pci_func *shpchp_slot_create(u8 busnumber)
{
	struct pci_func *new_slot;
	struct pci_func *next;

	new_slot = kmalloc(sizeof(*new_slot), GFP_KERNEL);

	if (new_slot == NULL) {
		return(new_slot);
	}

	memset(new_slot, 0, sizeof(struct pci_func));

	new_slot->next = NULL;
	new_slot->configured = 1;

	if (shpchp_slot_list[busnumber] == NULL) {
		shpchp_slot_list[busnumber] = new_slot;
	} else {
		next = shpchp_slot_list[busnumber];
		while (next->next != NULL)
			next = next->next;
		next->next = new_slot;
	}
	return(new_slot);
}


/*
 * slot_remove - Removes a node from the linked list of slots.
 * @old_slot: slot to remove
 *
 * Returns 0 if successful, !0 otherwise.
 */
static int slot_remove(struct pci_func * old_slot)
{
	struct pci_func *next;

	if (old_slot == NULL)
		return(1);

	next = shpchp_slot_list[old_slot->bus];

	if (next == NULL) {
		return(1);
	}

	if (next == old_slot) {
		shpchp_slot_list[old_slot->bus] = old_slot->next;
		kfree(old_slot);
		return(0);
	}

	while ((next->next != old_slot) && (next->next != NULL)) {
		next = next->next;
	}

	if (next->next == old_slot) {
		next->next = old_slot->next;
		kfree(old_slot);
		return(0);
	} else
		return(2);
}


/**
 * bridge_slot_remove - Removes a node from the linked list of slots.
 * @bridge: bridge to remove
 *
 * Returns 0 if successful, !0 otherwise.
 */
static int bridge_slot_remove(struct pci_func *bridge)
{
	u8 subordinateBus, secondaryBus;
	u8 tempBus;
	struct pci_func *next;

	if (bridge == NULL)
		return(1);

	secondaryBus = (bridge->config_space[0x06] >> 8) & 0xFF;
	subordinateBus = (bridge->config_space[0x06] >> 16) & 0xFF;

	for (tempBus = secondaryBus; tempBus <= subordinateBus; tempBus++) {
		next = shpchp_slot_list[tempBus];

		while (!slot_remove(next)) {
			next = shpchp_slot_list[tempBus];
		}
	}

	next = shpchp_slot_list[bridge->bus];

	if (next == NULL) {
		return(1);
	}

	if (next == bridge) {
		shpchp_slot_list[bridge->bus] = bridge->next;
		kfree(bridge);
		return(0);
	}

	while ((next->next != bridge) && (next->next != NULL)) {
		next = next->next;
	}

	if (next->next == bridge) {
		next->next = bridge->next;
		kfree(bridge);
		return(0);
	} else
		return(2);
}


/**
 * shpchp_slot_find - Looks for a node by bus, and device, multiple functions accessed
 * @bus: bus to find
 * @device: device to find
 * @index: is 0 for first function found, 1 for the second...
 *
 * Returns pointer to the node if successful, %NULL otherwise.
 */
struct pci_func *shpchp_slot_find(u8 bus, u8 device, u8 index)
{
	int found = -1;
	struct pci_func *func;

	func = shpchp_slot_list[bus];

	if ((func == NULL) || ((func->device == device) && (index == 0)))
		return(func);

	if (func->device == device)
		found++;

	while (func->next != NULL) {
		func = func->next;

		if (func->device == device)
			found++;

		if (found == index)
			return(func);
	}

	return(NULL);
}

static int is_bridge(struct pci_func * func)
{
	/* Check the header type */
	if (((func->config_space[0x03] >> 16) & 0xFF) == 0x01)
		return 1;
	else
		return 0;
}


/* The following routines constitute the bulk of the 
   hotplug controller logic
 */
static u32 change_bus_speed(struct controller *ctrl, struct slot *p_slot, enum pci_bus_speed speed)
{ 
	u32 rc = 0;

	dbg("%s: change to speed %d\n", __FUNCTION__, speed);
	down(&ctrl->crit_sect);
	if ((rc = p_slot->hpc_ops->set_bus_speed_mode(p_slot, speed))) {
		err("%s: Issue of set bus speed mode command failed\n", __FUNCTION__);
		up(&ctrl->crit_sect);
		return WRONG_BUS_FREQUENCY;
	}
	wait_for_ctrl_irq (ctrl);
		
	if ((rc = p_slot->hpc_ops->check_cmd_status(ctrl))) {
		err("%s: Can't set bus speed/mode in the case of adapter & bus mismatch\n",
			  __FUNCTION__);
		err("%s: Error code (%d)\n", __FUNCTION__, rc);
		up(&ctrl->crit_sect);
		return WRONG_BUS_FREQUENCY;
	}
	up(&ctrl->crit_sect);
	return rc;
}

static u32 fix_bus_speed(struct controller *ctrl, struct slot *pslot, u8 flag, 
enum pci_bus_speed asp, enum pci_bus_speed bsp, enum pci_bus_speed msp)
{ 
	u32 rc = 0;
	
	if (flag != 0) { /* Other slots on the same bus are occupied */
		if ( asp < bsp ) {
			err("%s: speed of bus %x and adapter %x mismatch\n", __FUNCTION__, bsp, asp);
			return WRONG_BUS_FREQUENCY;
		}
	} else {
		/* Other slots on the same bus are empty */
		if (msp == bsp) {
		/* if adapter_speed >= bus_speed, do nothing */
			if (asp < bsp) {
				/* 
				* Try to lower bus speed to accommodate the adapter if other slots 
				* on the same controller are empty
				*/
				if ((rc = change_bus_speed(ctrl, pslot, asp)))
					return rc;
			} 
		} else {
			if (asp < msp) {
				if ((rc = change_bus_speed(ctrl, pslot, asp)))
					return rc;
			} else {
				if ((rc = change_bus_speed(ctrl, pslot, msp)))
					return rc;
			}
		}
	}
	return rc;
}

/**
 * board_added - Called after a board has been added to the system.
 *
 * Turns power on for the board
 * Configures board
 *
 */
static u32 board_added(struct pci_func * func, struct controller * ctrl)
{
	u8 hp_slot;
	u8 slots_not_empty = 0;
	u32 rc = 0;
	struct slot *p_slot;
	enum pci_bus_speed adapter_speed, bus_speed, max_bus_speed;
	u8 pi, mode;

	p_slot = shpchp_find_slot(ctrl, func->device);
	hp_slot = func->device - ctrl->slot_device_offset;

	dbg("%s: func->device, slot_offset, hp_slot = %d, %d ,%d\n", __FUNCTION__, func->device, ctrl->slot_device_offset, hp_slot);

	/* Wait for exclusive access to hardware */
	down(&ctrl->crit_sect);

	/* Power on slot without connecting to bus */
	rc = p_slot->hpc_ops->power_on_slot(p_slot);
	if (rc) {
		err("%s: Failed to power on slot\n", __FUNCTION__);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return -1;
	}
			
	/* Wait for the command to complete */
	wait_for_ctrl_irq (ctrl);
	
	rc = p_slot->hpc_ops->check_cmd_status(ctrl);
	if (rc) {
		err("%s: Failed to power on slot, error code(%d)\n", __FUNCTION__, rc);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return -1;
	}

	
	if ((ctrl->pci_dev->vendor == 0x8086) && (ctrl->pci_dev->device == 0x0332)) {
		if (slots_not_empty)
			return WRONG_BUS_FREQUENCY;
		
		if ((rc = p_slot->hpc_ops->set_bus_speed_mode(p_slot, PCI_SPEED_33MHz))) {
			err("%s: Issue of set bus speed mode command failed\n", __FUNCTION__);
			up(&ctrl->crit_sect);
			return WRONG_BUS_FREQUENCY;
		}
		wait_for_ctrl_irq (ctrl);
		
		if ((rc = p_slot->hpc_ops->check_cmd_status(ctrl))) {
			err("%s: Can't set bus speed/mode in the case of adapter & bus mismatch\n",
				  __FUNCTION__);
			err("%s: Error code (%d)\n", __FUNCTION__, rc);
			up(&ctrl->crit_sect);
			return WRONG_BUS_FREQUENCY;
		}
		/* turn on board, blink green LED, turn off Amber LED */
		if ((rc = p_slot->hpc_ops->slot_enable(p_slot))) {
			err("%s: Issue of Slot Enable command failed\n", __FUNCTION__);
			up(&ctrl->crit_sect);
			return rc;
		}
		wait_for_ctrl_irq (ctrl);

		if ((rc = p_slot->hpc_ops->check_cmd_status(ctrl))) {
			err("%s: Failed to enable slot, error code(%d)\n", __FUNCTION__, rc);
			up(&ctrl->crit_sect);
			return rc;  
		}
	}
 
	rc = p_slot->hpc_ops->get_adapter_speed(p_slot, &adapter_speed);
	/* 0 = PCI 33Mhz, 1 = PCI 66 Mhz, 2 = PCI-X 66 PA, 4 = PCI-X 66 ECC, */
	/* 5 = PCI-X 133 PA, 7 = PCI-X 133 ECC,  0xa = PCI-X 133 Mhz 266, */
	/* 0xd = PCI-X 133 Mhz 533 */
	/* This encoding is different from the one used in cur_bus_speed & */
	/* max_bus_speed */

	if (rc  || adapter_speed == PCI_SPEED_UNKNOWN) {
		err("%s: Can't get adapter speed or bus mode mismatch\n", __FUNCTION__);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return WRONG_BUS_FREQUENCY;
	}

	rc = p_slot->hpc_ops->get_cur_bus_speed(p_slot, &bus_speed);
	if (rc || bus_speed == PCI_SPEED_UNKNOWN) {
		err("%s: Can't get bus operation speed\n", __FUNCTION__);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return WRONG_BUS_FREQUENCY;
	}

	rc = p_slot->hpc_ops->get_max_bus_speed(p_slot, &max_bus_speed);
	if (rc || max_bus_speed == PCI_SPEED_UNKNOWN) {
		err("%s: Can't get max bus operation speed\n", __FUNCTION__);
		max_bus_speed = bus_speed;
	}

	/* Done with exclusive hardware access */
	up(&ctrl->crit_sect);

	if ((rc  = p_slot->hpc_ops->get_prog_int(p_slot, &pi))) {
		err("%s: Can't get controller programming interface, set it to 1\n", __FUNCTION__);
		pi = 1;
	}

	/* Check if there are other slots or devices on the same bus */
	if (!list_empty(&ctrl->pci_dev->subordinate->devices))
		slots_not_empty = 1;

	dbg("%s: slots_not_empty %d, pi %d\n", __FUNCTION__, 
		slots_not_empty, pi);
	dbg("adapter_speed %d, bus_speed %d, max_bus_speed %d\n", 
		adapter_speed, bus_speed, max_bus_speed);

	if (pi == 2) {
		dbg("%s: In PI = %d\n", __FUNCTION__, pi);
		if ((rc = p_slot->hpc_ops->get_mode1_ECC_cap(p_slot, &mode))) {
			err("%s: Can't get Mode1_ECC, set mode to 0\n", __FUNCTION__);
			mode = 0;
		}

		switch (adapter_speed) {
		case PCI_SPEED_133MHz_PCIX_533:
		case PCI_SPEED_133MHz_PCIX_266:
			if ((bus_speed != adapter_speed) &&
			   ((rc = fix_bus_speed(ctrl, p_slot, slots_not_empty, adapter_speed, bus_speed, max_bus_speed)))) 
				return rc;
			break;	
		case PCI_SPEED_133MHz_PCIX_ECC:
		case PCI_SPEED_133MHz_PCIX:
			if (mode) { /* Bus - Mode 1 ECC */
				if ((bus_speed != 0x7) &&
				   ((rc = fix_bus_speed(ctrl, p_slot, slots_not_empty, adapter_speed, bus_speed, max_bus_speed)))) 
					return rc;
			} else {
				if ((bus_speed != 0x4) &&
				   ((rc = fix_bus_speed(ctrl, p_slot, slots_not_empty, adapter_speed, bus_speed, max_bus_speed)))) 
					return rc;
			}
			break;
		case PCI_SPEED_66MHz_PCIX_ECC:
		case PCI_SPEED_66MHz_PCIX:
			if (mode) { /* Bus - Mode 1 ECC */
				if ((bus_speed != 0x5) &&
				   ((rc = fix_bus_speed(ctrl, p_slot, slots_not_empty, adapter_speed, bus_speed, max_bus_speed)))) 
					return rc;
			} else {
				if ((bus_speed != 0x2) &&
				   ((rc = fix_bus_speed(ctrl, p_slot, slots_not_empty, adapter_speed, bus_speed, max_bus_speed)))) 
					return rc;
			}
			break;
		case PCI_SPEED_66MHz:
			if ((bus_speed != 0x1) &&
			   ((rc = fix_bus_speed(ctrl, p_slot, slots_not_empty, adapter_speed, bus_speed, max_bus_speed)))) 
				return rc;
			break;	
		case PCI_SPEED_33MHz:
			if (bus_speed > 0x0) {
				if (slots_not_empty == 0) {
					if ((rc = change_bus_speed(ctrl, p_slot, adapter_speed)))
						return rc;
				} else {
					err("%s: speed of bus %x and adapter %x mismatch\n", __FUNCTION__, bus_speed, adapter_speed);
					return WRONG_BUS_FREQUENCY;
				}
			}
			break;
		default:
			err("%s: speed of bus %x and adapter %x mismatch\n", __FUNCTION__, bus_speed, adapter_speed);
			return WRONG_BUS_FREQUENCY;
		}
	} else {
		/* If adpater_speed == bus_speed, nothing to do here */
		dbg("%s: In PI = %d\n", __FUNCTION__, pi);
		if ((adapter_speed != bus_speed) &&
		   ((rc = fix_bus_speed(ctrl, p_slot, slots_not_empty, adapter_speed, bus_speed, max_bus_speed))))
				return rc;
	}

	down(&ctrl->crit_sect);
	/* turn on board, blink green LED, turn off Amber LED */
	if ((rc = p_slot->hpc_ops->slot_enable(p_slot))) {
		err("%s: Issue of Slot Enable command failed\n", __FUNCTION__);
		up(&ctrl->crit_sect);
		return rc;
	}
	wait_for_ctrl_irq (ctrl);

	if ((rc = p_slot->hpc_ops->check_cmd_status(ctrl))) {
		err("%s: Failed to enable slot, error code(%d)\n", __FUNCTION__, rc);
		up(&ctrl->crit_sect);
		return rc;  
	}

	up(&ctrl->crit_sect);

	/* Wait for ~1 second */
	dbg("%s: before long_delay\n", __FUNCTION__);
	wait_for_ctrl_irq (ctrl);
	dbg("%s: after long_delay\n", __FUNCTION__);

	dbg("%s: func status = %x\n", __FUNCTION__, func->status);
	/* Check for a power fault */
	if (func->status == 0xFF) {
		/* power fault occurred, but it was benign */
		dbg("%s: power fault\n", __FUNCTION__);
		rc = POWER_FAILURE;
		func->status = 0;
		goto err_exit;
	}

	if (shpchp_configure_device(p_slot)) {
		err("Cannot add device at 0x%x:0x%x\n", p_slot->bus,
				p_slot->device);
		goto err_exit;
	}

	shpchp_save_slot_config(ctrl, func);

	func->status = 0;
	func->switch_save = 0x10;
	func->is_a_board = 0x01;
	func->pwr_save = 1;

	/* Wait for exclusive access to hardware */
	down(&ctrl->crit_sect);

	p_slot->hpc_ops->green_led_on(p_slot);

	/* Wait for the command to complete */
	wait_for_ctrl_irq (ctrl);

	/* Done with exclusive hardware access */
	up(&ctrl->crit_sect);

	return 0;

err_exit:
	/* Wait for exclusive access to hardware */
	down(&ctrl->crit_sect);

	/* turn off slot, turn on Amber LED, turn off Green LED */
	rc = p_slot->hpc_ops->slot_disable(p_slot);
	if (rc) {
		err("%s: Issue of Slot Disable command failed\n", __FUNCTION__);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return rc;
	}
	/* Wait for the command to complete */
	wait_for_ctrl_irq (ctrl);

	rc = p_slot->hpc_ops->check_cmd_status(ctrl);
	if (rc) {
		err("%s: Failed to disable slot, error code(%d)\n", __FUNCTION__, rc);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return rc;
	}

	/* Done with exclusive hardware access */
	up(&ctrl->crit_sect);

	return(rc);
}


/**
 * remove_board - Turns off slot and LED's
 *
 */
static u32 remove_board(struct pci_func *func, struct controller *ctrl)
{
	u8 device;
	u8 hp_slot;
	u32 rc;
	struct slot *p_slot;

	if (func == NULL)
		return(1);

	if (shpchp_unconfigure_device(func))
		return(1);

	device = func->device;

	hp_slot = func->device - ctrl->slot_device_offset;
	p_slot = shpchp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	dbg("In %s, hp_slot = %d\n", __FUNCTION__, hp_slot);

	/* Change status to shutdown */
	if (func->is_a_board)
		func->status = 0x01;
	func->configured = 0;

	/* Wait for exclusive access to hardware */
	down(&ctrl->crit_sect);

	/* turn off slot, turn on Amber LED, turn off Green LED */
	rc = p_slot->hpc_ops->slot_disable(p_slot);
	if (rc) {
		err("%s: Issue of Slot Disable command failed\n", __FUNCTION__);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return rc;
	}
	/* Wait for the command to complete */
	wait_for_ctrl_irq (ctrl);

	rc = p_slot->hpc_ops->check_cmd_status(ctrl);
	if (rc) {
		err("%s: Failed to disable slot, error code(%d)\n", __FUNCTION__, rc);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return rc;  
	}
	
	rc = p_slot->hpc_ops->set_attention_status(p_slot, 0);
	if (rc) {
		err("%s: Issue of Set Attention command failed\n", __FUNCTION__);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return rc;
	}
	/* Wait for the command to complete */
	wait_for_ctrl_irq (ctrl);

	/* Done with exclusive hardware access */
	up(&ctrl->crit_sect);

	if (ctrl->add_support) {
		while (func) {
			if (is_bridge(func)) {
				dbg("PCI Bridge Hot-Remove s:b:d:f(%02x:%02x:%02x:%02x)\n", ctrl->seg, func->bus, 
					func->device, func->function);
				bridge_slot_remove(func);
			} else
				dbg("PCI Function Hot-Remove s:b:d:f(%02x:%02x:%02x:%02x)\n", ctrl->seg, func->bus, 
					func->device, func->function);
				slot_remove(func);

			func = shpchp_slot_find(ctrl->slot_bus, device, 0);
		}

		/* Setup slot structure with entry for empty slot */
		func = shpchp_slot_create(ctrl->slot_bus);

		if (func == NULL) {
			return(1);
		}

		func->bus = ctrl->slot_bus;
		func->device = device;
		func->function = 0;
		func->configured = 0;
		func->switch_save = 0x10;
		func->pwr_save = 0;
		func->is_a_board = 0;
	}

	return 0;
}


static void pushbutton_helper_thread (unsigned long data)
{
	pushbutton_pending = data;

	up(&event_semaphore);
}


/**
 * shpchp_pushbutton_thread
 *
 * Scheduled procedure to handle blocking stuff for the pushbuttons
 * Handles all pending events and exits.
 *
 */
static void shpchp_pushbutton_thread (unsigned long slot)
{
	struct slot *p_slot = (struct slot *) slot;
	u8 getstatus;
	
	pushbutton_pending = 0;

	if (!p_slot) {
		dbg("%s: Error! slot NULL\n", __FUNCTION__);
		return;
	}

	p_slot->hpc_ops->get_power_status(p_slot, &getstatus);
	if (getstatus) {
		p_slot->state = POWEROFF_STATE;
		dbg("In power_down_board, b:d(%x:%x)\n", p_slot->bus, p_slot->device);

		shpchp_disable_slot(p_slot);
		p_slot->state = STATIC_STATE;
	} else {
		p_slot->state = POWERON_STATE;
		dbg("In add_board, b:d(%x:%x)\n", p_slot->bus, p_slot->device);

		if (shpchp_enable_slot(p_slot)) {
			/* Wait for exclusive access to hardware */
			down(&p_slot->ctrl->crit_sect);

			p_slot->hpc_ops->green_led_off(p_slot);

			/* Wait for the command to complete */
			wait_for_ctrl_irq (p_slot->ctrl);

			/* Done with exclusive hardware access */
			up(&p_slot->ctrl->crit_sect);
		}
		p_slot->state = STATIC_STATE;
	}

	return;
}


/* this is the main worker thread */
static int event_thread(void* data)
{
	struct controller *ctrl;
	lock_kernel();
	daemonize("shpchpd_event");
	unlock_kernel();

	while (1) {
		dbg("!!!!event_thread sleeping\n");
		down_interruptible (&event_semaphore);
		dbg("event_thread woken finished = %d\n", event_finished);
		if (event_finished || signal_pending(current))
			break;
		/* Do stuff here */
		if (pushbutton_pending)
			shpchp_pushbutton_thread(pushbutton_pending);
		else
			for (ctrl = shpchp_ctrl_list; ctrl; ctrl=ctrl->next)
				interrupt_event_handler(ctrl);
	}
	dbg("event_thread signals exit\n");
	up(&event_exit);
	return 0;
}

int shpchp_event_start_thread (void)
{
	int pid;

	/* initialize our semaphores */
	init_MUTEX_LOCKED(&event_exit);
	event_finished=0;

	init_MUTEX_LOCKED(&event_semaphore);
	pid = kernel_thread(event_thread, NULL, 0);

	if (pid < 0) {
		err ("Can't start up our event thread\n");
		return -1;
	}
	dbg("Our event thread pid = %d\n", pid);
	return 0;
}


void shpchp_event_stop_thread (void)
{
	event_finished = 1;
	dbg("event_thread finish command given\n");
	up(&event_semaphore);
	dbg("wait for event_thread to exit\n");
	down(&event_exit);
}


static int update_slot_info (struct slot *slot)
{
	struct hotplug_slot_info *info;
	int result;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	slot->hpc_ops->get_power_status(slot, &(info->power_status));
	slot->hpc_ops->get_attention_status(slot, &(info->attention_status));
	slot->hpc_ops->get_latch_status(slot, &(info->latch_status));
	slot->hpc_ops->get_adapter_status(slot, &(info->adapter_status));

	result = pci_hp_change_slot_info(slot->hotplug_slot, info);
	kfree (info);
	return result;
}

static void interrupt_event_handler(struct controller *ctrl)
{
	int loop = 0;
	int change = 1;
	struct pci_func *func;
	u8 hp_slot;
	u8 getstatus;
	struct slot *p_slot;

	dbg("%s:\n", __FUNCTION__);
	while (change) {
		change = 0;

		for (loop = 0; loop < 10; loop++) {
			if (ctrl->event_queue[loop].event_type != 0) {
				dbg("%s:loop %x event_type %x\n", __FUNCTION__, loop, 
					ctrl->event_queue[loop].event_type);
				hp_slot = ctrl->event_queue[loop].hp_slot;

				func = shpchp_slot_find(ctrl->slot_bus, (hp_slot + ctrl->slot_device_offset), 0);

				p_slot = shpchp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

				dbg("%s: hp_slot %d, func %p, p_slot %p\n", __FUNCTION__, hp_slot, func, p_slot);

				if (ctrl->event_queue[loop].event_type == INT_BUTTON_CANCEL) {
					dbg("%s: button cancel\n", __FUNCTION__);
					del_timer(&p_slot->task_event);

					switch (p_slot->state) {
					case BLINKINGOFF_STATE:
						/* Wait for exclusive access to hardware */
						down(&ctrl->crit_sect);

						p_slot->hpc_ops->green_led_on(p_slot);
						/* Wait for the command to complete */
						wait_for_ctrl_irq (ctrl);

						p_slot->hpc_ops->set_attention_status(p_slot, 0);

						/* Wait for the command to complete */
						wait_for_ctrl_irq (ctrl);

						/* Done with exclusive hardware access */
						up(&ctrl->crit_sect);
						break;
					case BLINKINGON_STATE:
						/* Wait for exclusive access to hardware */
						down(&ctrl->crit_sect);

						p_slot->hpc_ops->green_led_off(p_slot);
						/* Wait for the command to complete */
						wait_for_ctrl_irq (ctrl);

						p_slot->hpc_ops->set_attention_status(p_slot, 0);
						/* Wait for the command to complete */
						wait_for_ctrl_irq (ctrl);

						/* Done with exclusive hardware access */
						up(&ctrl->crit_sect);

						break;
					default:
						warn("Not a valid state\n");
						return;
					}
					info(msg_button_cancel, p_slot->number);
					p_slot->state = STATIC_STATE;
				} else if (ctrl->event_queue[loop].event_type == INT_BUTTON_PRESS) {
					/* Button Pressed (No action on 1st press...) */
					dbg("%s: Button pressed\n", __FUNCTION__);

					p_slot->hpc_ops->get_power_status(p_slot, &getstatus);
					if (getstatus) {
						/* slot is on */
						dbg("%s: slot is on\n", __FUNCTION__);
						p_slot->state = BLINKINGOFF_STATE;
						info(msg_button_off, p_slot->number);
					} else {
						/* slot is off */
						dbg("%s: slot is off\n", __FUNCTION__);
						p_slot->state = BLINKINGON_STATE;
						info(msg_button_on, p_slot->number);
					}

					/* Wait for exclusive access to hardware */
					down(&ctrl->crit_sect);

					/* blink green LED and turn off amber */
					p_slot->hpc_ops->green_led_blink(p_slot);
					/* Wait for the command to complete */
					wait_for_ctrl_irq (ctrl);
					
					p_slot->hpc_ops->set_attention_status(p_slot, 0);

					/* Wait for the command to complete */
					wait_for_ctrl_irq (ctrl);

					/* Done with exclusive hardware access */
					up(&ctrl->crit_sect);

					init_timer(&p_slot->task_event);
					p_slot->task_event.expires = jiffies + 5 * HZ;   /* 5 second delay */
					p_slot->task_event.function = (void (*)(unsigned long)) pushbutton_helper_thread;
					p_slot->task_event.data = (unsigned long) p_slot;

					dbg("%s: add_timer p_slot = %p\n", __FUNCTION__,(void *) p_slot);
					add_timer(&p_slot->task_event);
				} else if (ctrl->event_queue[loop].event_type == INT_POWER_FAULT) {
					/***********POWER FAULT********************/
					dbg("%s: power fault\n", __FUNCTION__);
					/* Wait for exclusive access to hardware */
					down(&ctrl->crit_sect);

					p_slot->hpc_ops->set_attention_status(p_slot, 1);
					/* Wait for the command to complete */
					wait_for_ctrl_irq (ctrl);
					
					p_slot->hpc_ops->green_led_off(p_slot);
					/* Wait for the command to complete */
					wait_for_ctrl_irq (ctrl);

					/* Done with exclusive hardware access */
					up(&ctrl->crit_sect);
				} else {
					/* refresh notification */
					if (p_slot)
						update_slot_info(p_slot);
				}

				ctrl->event_queue[loop].event_type = 0;

				change = 1;
			}
		}		/* End of FOR loop */
	}

	return;
}


int shpchp_enable_slot (struct slot *p_slot)
{
	u8 getstatus = 0;
	int rc;
	struct pci_func *func;

	func = shpchp_slot_find(p_slot->bus, p_slot->device, 0);
	if (!func) {
		dbg("%s: Error! slot NULL\n", __FUNCTION__);
		return -ENODEV;
	}

	/* Check to see if (latch closed, card present, power off) */
	down(&p_slot->ctrl->crit_sect);
	rc = p_slot->hpc_ops->get_adapter_status(p_slot, &getstatus);
	if (rc || !getstatus) {
		info("%s: no adapter on slot(%x)\n", __FUNCTION__, p_slot->number);
		up(&p_slot->ctrl->crit_sect);
		return -ENODEV;
	}
	rc = p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
	if (rc || getstatus) {
		info("%s: latch open on slot(%x)\n", __FUNCTION__, p_slot->number);
		up(&p_slot->ctrl->crit_sect);
		return -ENODEV;
	}
	rc = p_slot->hpc_ops->get_power_status(p_slot, &getstatus);
	if (rc || getstatus) {
		info("%s: already enabled on slot(%x)\n", __FUNCTION__, p_slot->number);
		up(&p_slot->ctrl->crit_sect);
		return -ENODEV;
	}
	up(&p_slot->ctrl->crit_sect);

	slot_remove(func);

	func = shpchp_slot_create(p_slot->bus);
	if (func == NULL)
		return -ENOMEM;

	func->bus = p_slot->bus;
	func->device = p_slot->device;
	func->function = 0;
	func->configured = 0;
	func->is_a_board = 1;

	/* We have to save the presence info for these slots */
	p_slot->hpc_ops->get_adapter_status(p_slot, &(func->presence_save));
	p_slot->hpc_ops->get_power_status(p_slot, &(func->pwr_save));
	dbg("%s: func->pwr_save %x\n", __FUNCTION__, func->pwr_save);
	p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
	func->switch_save = !getstatus? 0x10:0;

	rc = board_added(func, p_slot->ctrl);
	if (rc) {
		if (is_bridge(func))
			bridge_slot_remove(func);
		else
			slot_remove(func);

		/* Setup slot structure with entry for empty slot */
		func = shpchp_slot_create(p_slot->bus);
		if (func == NULL)
			return -ENOMEM;	/* Out of memory */

		func->bus = p_slot->bus;
		func->device = p_slot->device;
		func->function = 0;
		func->configured = 0;
		func->is_a_board = 1;

		/* We have to save the presence info for these slots */
		p_slot->hpc_ops->get_adapter_status(p_slot, &(func->presence_save));
		p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
		func->switch_save = !getstatus? 0x10:0;
	}

	if (p_slot)
		update_slot_info(p_slot);

	return rc;
}


int shpchp_disable_slot (struct slot *p_slot)
{
	u8 class_code, header_type, BCR;
	u8 index = 0;
	u8 getstatus = 0;
	u32 rc = 0;
	int ret = 0;
	unsigned int devfn;
	struct pci_bus *pci_bus;
	struct pci_func *func;

	if (!p_slot->ctrl)
		return -ENODEV;

	pci_bus = p_slot->ctrl->pci_dev->subordinate;

	/* Check to see if (latch closed, card present, power on) */
	down(&p_slot->ctrl->crit_sect);

	ret = p_slot->hpc_ops->get_adapter_status(p_slot, &getstatus);
	if (ret || !getstatus) {
		info("%s: no adapter on slot(%x)\n", __FUNCTION__, p_slot->number);
		up(&p_slot->ctrl->crit_sect);
		return -ENODEV;
	}
	ret = p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
	if (ret || getstatus) {
		info("%s: latch open on slot(%x)\n", __FUNCTION__, p_slot->number);
		up(&p_slot->ctrl->crit_sect);
		return -ENODEV;
	}
	ret = p_slot->hpc_ops->get_power_status(p_slot, &getstatus);
	if (ret || !getstatus) {
		info("%s: already disabled slot(%x)\n", __FUNCTION__, p_slot->number);
		up(&p_slot->ctrl->crit_sect);
		return -ENODEV;
	}
	up(&p_slot->ctrl->crit_sect);

	func = shpchp_slot_find(p_slot->bus, p_slot->device, index++);

	/* Make sure there are no video controllers here
	 * for all func of p_slot
	 */
	while (func && !rc) {
		pci_bus->number = func->bus;
		devfn = PCI_DEVFN(func->device, func->function);

		/* Check the Class Code */
		rc = pci_bus_read_config_byte (pci_bus, devfn, 0x0B, &class_code);
		if (rc)
			return -ENODEV;

		if (class_code == PCI_BASE_CLASS_DISPLAY) {
			/* Display/Video adapter (not supported) */
			rc = REMOVE_NOT_SUPPORTED;
		} else {
			/* See if it's a bridge */
			rc = pci_bus_read_config_byte (pci_bus, devfn, PCI_HEADER_TYPE, &header_type);
			if (rc)
				return -ENODEV;

			/* If it's a bridge, check the VGA Enable bit */
			if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {
				rc = pci_bus_read_config_byte (pci_bus, devfn, PCI_BRIDGE_CONTROL, &BCR);
				if (rc)
					return -ENODEV;

				/* If the VGA Enable bit is set, remove isn't supported */
				if (BCR & PCI_BRIDGE_CTL_VGA) {
					rc = REMOVE_NOT_SUPPORTED;
				}
			}
		}

		func = shpchp_slot_find(p_slot->bus, p_slot->device, index++);
	}

	func = shpchp_slot_find(p_slot->bus, p_slot->device, 0);
	if ((func != NULL) && !rc) {
		rc = remove_board(func, p_slot->ctrl);
	} else if (!rc)
		rc = -ENODEV;

	if (p_slot)
		update_slot_info(p_slot);

	return rc;
}


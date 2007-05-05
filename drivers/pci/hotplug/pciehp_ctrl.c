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
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/smp_lock.h>
#include <linux/pci.h>
#include <linux/workqueue.h>
#include "../pci.h"
#include "pciehp.h"

static void interrupt_event_handler(struct work_struct *work);
static int pciehp_enable_slot(struct slot *p_slot);
static int pciehp_disable_slot(struct slot *p_slot);

static int queue_interrupt_event(struct slot *p_slot, u32 event_type)
{
	struct event_info *info;

	info = kmalloc(sizeof(*info), GFP_ATOMIC);
	if (!info)
		return -ENOMEM;

	info->event_type = event_type;
	info->p_slot = p_slot;
	INIT_WORK(&info->work, interrupt_event_handler);

	schedule_work(&info->work);

	return 0;
}

u8 pciehp_handle_attention_button(u8 hp_slot, struct controller *ctrl)
{
	struct slot *p_slot;
	u32 event_type;

	/* Attention Button Change */
	dbg("pciehp:  Attention button interrupt received.\n");

	p_slot = pciehp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	/*
	 *  Button pressed - See if need to TAKE ACTION!!!
	 */
	info("Button pressed on Slot(%s)\n", p_slot->name);
	event_type = INT_BUTTON_PRESS;

	queue_interrupt_event(p_slot, event_type);

	return 0;
}

u8 pciehp_handle_switch_change(u8 hp_slot, struct controller *ctrl)
{
	struct slot *p_slot;
	u8 getstatus;
	u32 event_type;

	/* Switch Change */
	dbg("pciehp:  Switch interrupt received.\n");

	p_slot = pciehp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);
	p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);

	if (getstatus) {
		/*
		 * Switch opened
		 */
		info("Latch open on Slot(%s)\n", p_slot->name);
		event_type = INT_SWITCH_OPEN;
	} else {
		/*
		 *  Switch closed
		 */
		info("Latch close on Slot(%s)\n", p_slot->name);
		event_type = INT_SWITCH_CLOSE;
	}

	queue_interrupt_event(p_slot, event_type);

	return 1;
}

u8 pciehp_handle_presence_change(u8 hp_slot, struct controller *ctrl)
{
	struct slot *p_slot;
	u32 event_type;
	u8 presence_save;

	/* Presence Change */
	dbg("pciehp:  Presence/Notify input change.\n");

	p_slot = pciehp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	/* Switch is open, assume a presence change
	 * Save the presence state
	 */
	p_slot->hpc_ops->get_adapter_status(p_slot, &presence_save);
	if (presence_save) {
		/*
		 * Card Present
		 */
		info("Card present on Slot(%s)\n", p_slot->name);
		event_type = INT_PRESENCE_ON;
	} else {
		/*
		 * Not Present
		 */
		info("Card not present on Slot(%s)\n", p_slot->name);
		event_type = INT_PRESENCE_OFF;
	}

	queue_interrupt_event(p_slot, event_type);

	return 1;
}

u8 pciehp_handle_power_fault(u8 hp_slot, struct controller *ctrl)
{
	struct slot *p_slot;
	u32 event_type;

	/* power fault */
	dbg("pciehp:  Power fault interrupt received.\n");

	p_slot = pciehp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	if ( !(p_slot->hpc_ops->query_power_fault(p_slot))) {
		/*
		 * power fault Cleared
		 */
		info("Power fault cleared on Slot(%s)\n", p_slot->name);
		event_type = INT_POWER_FAULT_CLEAR;
	} else {
		/*
		 *   power fault
		 */
		info("Power fault on Slot(%s)\n", p_slot->name);
		event_type = INT_POWER_FAULT;
		info("power fault bit %x set\n", hp_slot);
	}

	queue_interrupt_event(p_slot, event_type);

	return 1;
}

/* The following routines constitute the bulk of the 
   hotplug controller logic
 */

static void set_slot_off(struct controller *ctrl, struct slot * pslot)
{
	/* turn off slot, turn on Amber LED, turn off Green LED if supported*/
	if (POWER_CTRL(ctrl->ctrlcap)) {
		if (pslot->hpc_ops->power_off_slot(pslot)) {   
			err("%s: Issue of Slot Power Off command failed\n",
			    __FUNCTION__);
			return;
		}
	}

	if (PWR_LED(ctrl->ctrlcap))
		pslot->hpc_ops->green_led_off(pslot);   

	if (ATTN_LED(ctrl->ctrlcap)) {
		if (pslot->hpc_ops->set_attention_status(pslot, 1)) {
			err("%s: Issue of Set Attention Led command failed\n",
			    __FUNCTION__);
			return;
		}
	}
}

/**
 * board_added - Called after a board has been added to the system.
 *
 * Turns power on for the board
 * Configures board
 *
 */
static int board_added(struct slot *p_slot)
{
	u8 hp_slot;
	int retval = 0;
	struct controller *ctrl = p_slot->ctrl;

	hp_slot = p_slot->device - ctrl->slot_device_offset;

	dbg("%s: slot device, slot offset, hp slot = %d, %d ,%d\n",
			__FUNCTION__, p_slot->device,
			ctrl->slot_device_offset, hp_slot);

	if (POWER_CTRL(ctrl->ctrlcap)) {
		/* Power on slot */
		retval = p_slot->hpc_ops->power_on_slot(p_slot);
		if (retval)
			return retval;
	}
	
	if (PWR_LED(ctrl->ctrlcap))
		p_slot->hpc_ops->green_led_blink(p_slot);

	/* Wait for ~1 second */
	msleep(1000);

	/* Check link training status */
	retval = p_slot->hpc_ops->check_lnk_status(ctrl);
	if (retval) {
		err("%s: Failed to check link status\n", __FUNCTION__);
		set_slot_off(ctrl, p_slot);
		return retval;
	}

	/* Check for a power fault */
	if (p_slot->hpc_ops->query_power_fault(p_slot)) {
		dbg("%s: power fault detected\n", __FUNCTION__);
		retval = POWER_FAILURE;
		goto err_exit;
	}

	retval = pciehp_configure_device(p_slot);
	if (retval) {
		err("Cannot add device 0x%x:%x\n", p_slot->bus,
		    p_slot->device);
		goto err_exit;
	}

	/*
	 * Some PCI Express root ports require fixup after hot-plug operation.
	 */
	if (pcie_mch_quirk)
		pci_fixup_device(pci_fixup_final, ctrl->pci_dev);
	if (PWR_LED(ctrl->ctrlcap))
  		p_slot->hpc_ops->green_led_on(p_slot);

	return 0;

err_exit:
	set_slot_off(ctrl, p_slot);
	return retval;
}

/**
 * remove_board - Turns off slot and LED's
 *
 */
static int remove_board(struct slot *p_slot)
{
	u8 device;
	u8 hp_slot;
	int retval = 0;
	struct controller *ctrl = p_slot->ctrl;

	retval = pciehp_unconfigure_device(p_slot);
	if (retval)
		return retval;

	device = p_slot->device;
	hp_slot = p_slot->device - ctrl->slot_device_offset;
	p_slot = pciehp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	dbg("In %s, hp_slot = %d\n", __FUNCTION__, hp_slot);

	if (POWER_CTRL(ctrl->ctrlcap)) {
		/* power off slot */
		retval = p_slot->hpc_ops->power_off_slot(p_slot);
		if (retval) {
			err("%s: Issue of Slot Disable command failed\n",
			    __FUNCTION__);
			return retval;
		}
	}

	if (PWR_LED(ctrl->ctrlcap))
		/* turn off Green LED */
		p_slot->hpc_ops->green_led_off(p_slot);

	return 0;
}

struct power_work_info {
	struct slot *p_slot;
	struct work_struct work;
};

/**
 * pciehp_pushbutton_thread
 *
 * Scheduled procedure to handle blocking stuff for the pushbuttons
 * Handles all pending events and exits.
 *
 */
static void pciehp_power_thread(struct work_struct *work)
{
	struct power_work_info *info =
		container_of(work, struct power_work_info, work);
	struct slot *p_slot = info->p_slot;

	mutex_lock(&p_slot->lock);
	switch (p_slot->state) {
	case POWEROFF_STATE:
		mutex_unlock(&p_slot->lock);
		dbg("%s: disabling bus:device(%x:%x)\n",
		    __FUNCTION__, p_slot->bus, p_slot->device);
		pciehp_disable_slot(p_slot);
		mutex_lock(&p_slot->lock);
		p_slot->state = STATIC_STATE;
		break;
	case POWERON_STATE:
		mutex_unlock(&p_slot->lock);
		if (pciehp_enable_slot(p_slot) &&
		    PWR_LED(p_slot->ctrl->ctrlcap))
			p_slot->hpc_ops->green_led_off(p_slot);
		mutex_lock(&p_slot->lock);
		p_slot->state = STATIC_STATE;
		break;
	default:
		break;
	}
	mutex_unlock(&p_slot->lock);

	kfree(info);
}

void pciehp_queue_pushbutton_work(struct work_struct *work)
{
	struct slot *p_slot = container_of(work, struct slot, work.work);
	struct power_work_info *info;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		err("%s: Cannot allocate memory\n", __FUNCTION__);
		return;
	}
	info->p_slot = p_slot;
	INIT_WORK(&info->work, pciehp_power_thread);

	mutex_lock(&p_slot->lock);
	switch (p_slot->state) {
	case BLINKINGOFF_STATE:
		p_slot->state = POWEROFF_STATE;
		break;
	case BLINKINGON_STATE:
		p_slot->state = POWERON_STATE;
		break;
	default:
		goto out;
	}
	queue_work(pciehp_wq, &info->work);
 out:
	mutex_unlock(&p_slot->lock);
}

static int update_slot_info(struct slot *slot)
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

/*
 * Note: This function must be called with slot->lock held
 */
static void handle_button_press_event(struct slot *p_slot)
{
	struct controller *ctrl = p_slot->ctrl;
	u8 getstatus;

	switch (p_slot->state) {
	case STATIC_STATE:
		p_slot->hpc_ops->get_power_status(p_slot, &getstatus);
		if (getstatus) {
			p_slot->state = BLINKINGOFF_STATE;
			info("PCI slot #%s - powering off due to button "
			     "press.\n", p_slot->name);
		} else {
			p_slot->state = BLINKINGON_STATE;
			info("PCI slot #%s - powering on due to button "
			     "press.\n", p_slot->name);
		}
		/* blink green LED and turn off amber */
		if (PWR_LED(ctrl->ctrlcap))
			p_slot->hpc_ops->green_led_blink(p_slot);
		if (ATTN_LED(ctrl->ctrlcap))
			p_slot->hpc_ops->set_attention_status(p_slot, 0);

		schedule_delayed_work(&p_slot->work, 5*HZ);
		break;
	case BLINKINGOFF_STATE:
	case BLINKINGON_STATE:
		/*
		 * Cancel if we are still blinking; this means that we
		 * press the attention again before the 5 sec. limit
		 * expires to cancel hot-add or hot-remove
		 */
		info("Button cancel on Slot(%s)\n", p_slot->name);
		dbg("%s: button cancel\n", __FUNCTION__);
		cancel_delayed_work(&p_slot->work);
		if (p_slot->state == BLINKINGOFF_STATE) {
			if (PWR_LED(ctrl->ctrlcap))
				p_slot->hpc_ops->green_led_on(p_slot);
		} else {
			if (PWR_LED(ctrl->ctrlcap))
				p_slot->hpc_ops->green_led_off(p_slot);
		}
		if (ATTN_LED(ctrl->ctrlcap))
			p_slot->hpc_ops->set_attention_status(p_slot, 0);
		info("PCI slot #%s - action canceled due to button press\n",
		     p_slot->name);
		p_slot->state = STATIC_STATE;
		break;
	case POWEROFF_STATE:
	case POWERON_STATE:
		/*
		 * Ignore if the slot is on power-on or power-off state;
		 * this means that the previous attention button action
		 * to hot-add or hot-remove is undergoing
		 */
		info("Button ignore on Slot(%s)\n", p_slot->name);
		update_slot_info(p_slot);
		break;
	default:
		warn("Not a valid state\n");
		break;
	}
}

/*
 * Note: This function must be called with slot->lock held
 */
static void handle_surprise_event(struct slot *p_slot)
{
	u8 getstatus;
	struct power_work_info *info;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		err("%s: Cannot allocate memory\n", __FUNCTION__);
		return;
	}
	info->p_slot = p_slot;
	INIT_WORK(&info->work, pciehp_power_thread);

	p_slot->hpc_ops->get_adapter_status(p_slot, &getstatus);
	if (!getstatus)
		p_slot->state = POWEROFF_STATE;
	else
		p_slot->state = POWERON_STATE;

	queue_work(pciehp_wq, &info->work);
}

static void interrupt_event_handler(struct work_struct *work)
{
	struct event_info *info = container_of(work, struct event_info, work);
	struct slot *p_slot = info->p_slot;
	struct controller *ctrl = p_slot->ctrl;

	mutex_lock(&p_slot->lock);
	switch (info->event_type) {
	case INT_BUTTON_PRESS:
		handle_button_press_event(p_slot);
		break;
	case INT_POWER_FAULT:
		if (!POWER_CTRL(ctrl->ctrlcap))
			break;
		if (ATTN_LED(ctrl->ctrlcap))
			p_slot->hpc_ops->set_attention_status(p_slot, 1);
		if (PWR_LED(ctrl->ctrlcap))
			p_slot->hpc_ops->green_led_off(p_slot);
		break;
	case INT_PRESENCE_ON:
	case INT_PRESENCE_OFF:
		if (!HP_SUPR_RM(ctrl->ctrlcap))
			break;
		dbg("Surprise Removal\n");
		update_slot_info(p_slot);
		handle_surprise_event(p_slot);
		break;
	default:
		update_slot_info(p_slot);
		break;
	}
	mutex_unlock(&p_slot->lock);

	kfree(info);
}

int pciehp_enable_slot(struct slot *p_slot)
{
	u8 getstatus = 0;
	int rc;

	/* Check to see if (latch closed, card present, power off) */
	mutex_lock(&p_slot->ctrl->crit_sect);

	rc = p_slot->hpc_ops->get_adapter_status(p_slot, &getstatus);
	if (rc || !getstatus) {
		info("%s: no adapter on slot(%s)\n", __FUNCTION__,
		     p_slot->name);
		mutex_unlock(&p_slot->ctrl->crit_sect);
		return -ENODEV;
	}
	if (MRL_SENS(p_slot->ctrl->ctrlcap)) {	
		rc = p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
		if (rc || getstatus) {
			info("%s: latch open on slot(%s)\n", __FUNCTION__,
			     p_slot->name);
			mutex_unlock(&p_slot->ctrl->crit_sect);
			return -ENODEV;
		}
	}
	
	if (POWER_CTRL(p_slot->ctrl->ctrlcap)) {	
		rc = p_slot->hpc_ops->get_power_status(p_slot, &getstatus);
		if (rc || getstatus) {
			info("%s: already enabled on slot(%s)\n", __FUNCTION__,
			     p_slot->name);
			mutex_unlock(&p_slot->ctrl->crit_sect);
			return -EINVAL;
		}
	}

	p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);

	rc = board_added(p_slot);
	if (rc) {
		p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
	}

	update_slot_info(p_slot);

	mutex_unlock(&p_slot->ctrl->crit_sect);
	return rc;
}


int pciehp_disable_slot(struct slot *p_slot)
{
	u8 getstatus = 0;
	int ret = 0;

	if (!p_slot->ctrl)
		return 1;

	/* Check to see if (latch closed, card present, power on) */
	mutex_lock(&p_slot->ctrl->crit_sect);

	if (!HP_SUPR_RM(p_slot->ctrl->ctrlcap)) {	
		ret = p_slot->hpc_ops->get_adapter_status(p_slot, &getstatus);
		if (ret || !getstatus) {
			info("%s: no adapter on slot(%s)\n", __FUNCTION__,
			     p_slot->name);
			mutex_unlock(&p_slot->ctrl->crit_sect);
			return -ENODEV;
		}
	}

	if (MRL_SENS(p_slot->ctrl->ctrlcap)) {	
		ret = p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
		if (ret || getstatus) {
			info("%s: latch open on slot(%s)\n", __FUNCTION__,
			     p_slot->name);
			mutex_unlock(&p_slot->ctrl->crit_sect);
			return -ENODEV;
		}
	}

	if (POWER_CTRL(p_slot->ctrl->ctrlcap)) {	
		ret = p_slot->hpc_ops->get_power_status(p_slot, &getstatus);
		if (ret || !getstatus) {
			info("%s: already disabled slot(%s)\n", __FUNCTION__,
			     p_slot->name);
			mutex_unlock(&p_slot->ctrl->crit_sect);
			return -EINVAL;
		}
	}

	ret = remove_board(p_slot);
	update_slot_info(p_slot);

	mutex_unlock(&p_slot->ctrl->crit_sect);
	return ret;
}

int pciehp_sysfs_enable_slot(struct slot *p_slot)
{
	int retval = -ENODEV;

	mutex_lock(&p_slot->lock);
	switch (p_slot->state) {
	case BLINKINGON_STATE:
		cancel_delayed_work(&p_slot->work);
	case STATIC_STATE:
		p_slot->state = POWERON_STATE;
		mutex_unlock(&p_slot->lock);
		retval = pciehp_enable_slot(p_slot);
		mutex_lock(&p_slot->lock);
		p_slot->state = STATIC_STATE;
		break;
	case POWERON_STATE:
		info("Slot %s is already in powering on state\n",
		     p_slot->name);
		break;
	case BLINKINGOFF_STATE:
	case POWEROFF_STATE:
		info("Already enabled on slot %s\n", p_slot->name);
		break;
	default:
		err("Not a valid state on slot %s\n", p_slot->name);
		break;
	}
	mutex_unlock(&p_slot->lock);

	return retval;
}

int pciehp_sysfs_disable_slot(struct slot *p_slot)
{
	int retval = -ENODEV;

	mutex_lock(&p_slot->lock);
	switch (p_slot->state) {
	case BLINKINGOFF_STATE:
		cancel_delayed_work(&p_slot->work);
	case STATIC_STATE:
		p_slot->state = POWEROFF_STATE;
		mutex_unlock(&p_slot->lock);
		retval = pciehp_disable_slot(p_slot);
		mutex_lock(&p_slot->lock);
		p_slot->state = STATIC_STATE;
		break;
	case POWEROFF_STATE:
		info("Slot %s is already in powering off state\n",
		     p_slot->name);
		break;
	case BLINKINGON_STATE:
	case POWERON_STATE:
		info("Already disabled on slot %s\n", p_slot->name);
		break;
	default:
		err("Not a valid state on slot %s\n", p_slot->name);
		break;
	}
	mutex_unlock(&p_slot->lock);

	return retval;
}

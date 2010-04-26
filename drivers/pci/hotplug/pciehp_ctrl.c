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
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/workqueue.h>
#include "../pci.h"
#include "pciehp.h"

static void interrupt_event_handler(struct work_struct *work);

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

u8 pciehp_handle_attention_button(struct slot *p_slot)
{
	u32 event_type;
	struct controller *ctrl = p_slot->ctrl;

	/* Attention Button Change */
	ctrl_dbg(ctrl, "Attention button interrupt received\n");

	/*
	 *  Button pressed - See if need to TAKE ACTION!!!
	 */
	ctrl_info(ctrl, "Button pressed on Slot(%s)\n", slot_name(p_slot));
	event_type = INT_BUTTON_PRESS;

	queue_interrupt_event(p_slot, event_type);

	return 0;
}

u8 pciehp_handle_switch_change(struct slot *p_slot)
{
	u8 getstatus;
	u32 event_type;
	struct controller *ctrl = p_slot->ctrl;

	/* Switch Change */
	ctrl_dbg(ctrl, "Switch interrupt received\n");

	pciehp_get_latch_status(p_slot, &getstatus);
	if (getstatus) {
		/*
		 * Switch opened
		 */
		ctrl_info(ctrl, "Latch open on Slot(%s)\n", slot_name(p_slot));
		event_type = INT_SWITCH_OPEN;
	} else {
		/*
		 *  Switch closed
		 */
		ctrl_info(ctrl, "Latch close on Slot(%s)\n", slot_name(p_slot));
		event_type = INT_SWITCH_CLOSE;
	}

	queue_interrupt_event(p_slot, event_type);

	return 1;
}

u8 pciehp_handle_presence_change(struct slot *p_slot)
{
	u32 event_type;
	u8 presence_save;
	struct controller *ctrl = p_slot->ctrl;

	/* Presence Change */
	ctrl_dbg(ctrl, "Presence/Notify input change\n");

	/* Switch is open, assume a presence change
	 * Save the presence state
	 */
	pciehp_get_adapter_status(p_slot, &presence_save);
	if (presence_save) {
		/*
		 * Card Present
		 */
		ctrl_info(ctrl, "Card present on Slot(%s)\n", slot_name(p_slot));
		event_type = INT_PRESENCE_ON;
	} else {
		/*
		 * Not Present
		 */
		ctrl_info(ctrl, "Card not present on Slot(%s)\n",
			  slot_name(p_slot));
		event_type = INT_PRESENCE_OFF;
	}

	queue_interrupt_event(p_slot, event_type);

	return 1;
}

u8 pciehp_handle_power_fault(struct slot *p_slot)
{
	u32 event_type;
	struct controller *ctrl = p_slot->ctrl;

	/* power fault */
	ctrl_dbg(ctrl, "Power fault interrupt received\n");
	ctrl_err(ctrl, "Power fault on slot %s\n", slot_name(p_slot));
	event_type = INT_POWER_FAULT;
	ctrl_info(ctrl, "Power fault bit %x set\n", 0);
	queue_interrupt_event(p_slot, event_type);

	return 1;
}

/* The following routines constitute the bulk of the
   hotplug controller logic
 */

static void set_slot_off(struct controller *ctrl, struct slot * pslot)
{
	/* turn off slot, turn on Amber LED, turn off Green LED if supported*/
	if (POWER_CTRL(ctrl)) {
		if (pciehp_power_off_slot(pslot)) {
			ctrl_err(ctrl,
				 "Issue of Slot Power Off command failed\n");
			return;
		}
		/*
		 * After turning power off, we must wait for at least 1 second
		 * before taking any action that relies on power having been
		 * removed from the slot/adapter.
		 */
		msleep(1000);
	}

	if (PWR_LED(ctrl))
		pciehp_green_led_off(pslot);

	if (ATTN_LED(ctrl)) {
		if (pciehp_set_attention_status(pslot, 1)) {
			ctrl_err(ctrl,
				 "Issue of Set Attention Led command failed\n");
			return;
		}
	}
}

/**
 * board_added - Called after a board has been added to the system.
 * @p_slot: &slot where board is added
 *
 * Turns power on for the board.
 * Configures board.
 */
static int board_added(struct slot *p_slot)
{
	int retval = 0;
	struct controller *ctrl = p_slot->ctrl;
	struct pci_bus *parent = ctrl->pcie->port->subordinate;

	if (POWER_CTRL(ctrl)) {
		/* Power on slot */
		retval = pciehp_power_on_slot(p_slot);
		if (retval)
			return retval;
	}

	if (PWR_LED(ctrl))
		pciehp_green_led_blink(p_slot);

	/* Check link training status */
	retval = pciehp_check_link_status(ctrl);
	if (retval) {
		ctrl_err(ctrl, "Failed to check link status\n");
		goto err_exit;
	}

	/* Check for a power fault */
	if (ctrl->power_fault_detected || pciehp_query_power_fault(p_slot)) {
		ctrl_err(ctrl, "Power fault on slot %s\n", slot_name(p_slot));
		retval = -EIO;
		goto err_exit;
	}

	retval = pciehp_configure_device(p_slot);
	if (retval) {
		ctrl_err(ctrl, "Cannot add device at %04x:%02x:00\n",
			 pci_domain_nr(parent), parent->number);
		goto err_exit;
	}

	if (PWR_LED(ctrl))
		pciehp_green_led_on(p_slot);

	return 0;

err_exit:
	set_slot_off(ctrl, p_slot);
	return retval;
}

/**
 * remove_board - Turns off slot and LEDs
 * @p_slot: slot where board is being removed
 */
static int remove_board(struct slot *p_slot)
{
	int retval = 0;
	struct controller *ctrl = p_slot->ctrl;

	retval = pciehp_unconfigure_device(p_slot);
	if (retval)
		return retval;

	if (POWER_CTRL(ctrl)) {
		/* power off slot */
		retval = pciehp_power_off_slot(p_slot);
		if (retval) {
			ctrl_err(ctrl,
				 "Issue of Slot Disable command failed\n");
			return retval;
		}
		/*
		 * After turning power off, we must wait for at least 1 second
		 * before taking any action that relies on power having been
		 * removed from the slot/adapter.
		 */
		msleep(1000);
	}

	/* turn off Green LED */
	if (PWR_LED(ctrl))
		pciehp_green_led_off(p_slot);

	return 0;
}

struct power_work_info {
	struct slot *p_slot;
	struct work_struct work;
};

/**
 * pciehp_power_thread - handle pushbutton events
 * @work: &struct work_struct describing work to be done
 *
 * Scheduled procedure to handle blocking stuff for the pushbuttons.
 * Handles all pending events and exits.
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
		ctrl_dbg(p_slot->ctrl,
			 "Disabling domain:bus:device=%04x:%02x:00\n",
			 pci_domain_nr(p_slot->ctrl->pcie->port->subordinate),
			 p_slot->ctrl->pcie->port->subordinate->number);
		pciehp_disable_slot(p_slot);
		mutex_lock(&p_slot->lock);
		p_slot->state = STATIC_STATE;
		break;
	case POWERON_STATE:
		mutex_unlock(&p_slot->lock);
		if (pciehp_enable_slot(p_slot) && PWR_LED(p_slot->ctrl))
			pciehp_green_led_off(p_slot);
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
		ctrl_err(p_slot->ctrl, "%s: Cannot allocate memory\n",
			 __func__);
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
		kfree(info);
		goto out;
	}
	queue_work(pciehp_wq, &info->work);
 out:
	mutex_unlock(&p_slot->lock);
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
		pciehp_get_power_status(p_slot, &getstatus);
		if (getstatus) {
			p_slot->state = BLINKINGOFF_STATE;
			ctrl_info(ctrl,
				  "PCI slot #%s - powering off due to button "
				  "press.\n", slot_name(p_slot));
		} else {
			p_slot->state = BLINKINGON_STATE;
			ctrl_info(ctrl,
				  "PCI slot #%s - powering on due to button "
				  "press.\n", slot_name(p_slot));
		}
		/* blink green LED and turn off amber */
		if (PWR_LED(ctrl))
			pciehp_green_led_blink(p_slot);
		if (ATTN_LED(ctrl))
			pciehp_set_attention_status(p_slot, 0);

		schedule_delayed_work(&p_slot->work, 5*HZ);
		break;
	case BLINKINGOFF_STATE:
	case BLINKINGON_STATE:
		/*
		 * Cancel if we are still blinking; this means that we
		 * press the attention again before the 5 sec. limit
		 * expires to cancel hot-add or hot-remove
		 */
		ctrl_info(ctrl, "Button cancel on Slot(%s)\n", slot_name(p_slot));
		cancel_delayed_work(&p_slot->work);
		if (p_slot->state == BLINKINGOFF_STATE) {
			if (PWR_LED(ctrl))
				pciehp_green_led_on(p_slot);
		} else {
			if (PWR_LED(ctrl))
				pciehp_green_led_off(p_slot);
		}
		if (ATTN_LED(ctrl))
			pciehp_set_attention_status(p_slot, 0);
		ctrl_info(ctrl, "PCI slot #%s - action canceled "
			  "due to button press\n", slot_name(p_slot));
		p_slot->state = STATIC_STATE;
		break;
	case POWEROFF_STATE:
	case POWERON_STATE:
		/*
		 * Ignore if the slot is on power-on or power-off state;
		 * this means that the previous attention button action
		 * to hot-add or hot-remove is undergoing
		 */
		ctrl_info(ctrl, "Button ignore on Slot(%s)\n", slot_name(p_slot));
		break;
	default:
		ctrl_warn(ctrl, "Not a valid state\n");
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
		ctrl_err(p_slot->ctrl, "%s: Cannot allocate memory\n",
			 __func__);
		return;
	}
	info->p_slot = p_slot;
	INIT_WORK(&info->work, pciehp_power_thread);

	pciehp_get_adapter_status(p_slot, &getstatus);
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
		if (!POWER_CTRL(ctrl))
			break;
		if (ATTN_LED(ctrl))
			pciehp_set_attention_status(p_slot, 1);
		if (PWR_LED(ctrl))
			pciehp_green_led_off(p_slot);
		break;
	case INT_PRESENCE_ON:
	case INT_PRESENCE_OFF:
		if (!HP_SUPR_RM(ctrl))
			break;
		ctrl_dbg(ctrl, "Surprise Removal\n");
		handle_surprise_event(p_slot);
		break;
	default:
		break;
	}
	mutex_unlock(&p_slot->lock);

	kfree(info);
}

int pciehp_enable_slot(struct slot *p_slot)
{
	u8 getstatus = 0;
	int rc;
	struct controller *ctrl = p_slot->ctrl;

	rc = pciehp_get_adapter_status(p_slot, &getstatus);
	if (rc || !getstatus) {
		ctrl_info(ctrl, "No adapter on slot(%s)\n", slot_name(p_slot));
		return -ENODEV;
	}
	if (MRL_SENS(p_slot->ctrl)) {
		rc = pciehp_get_latch_status(p_slot, &getstatus);
		if (rc || getstatus) {
			ctrl_info(ctrl, "Latch open on slot(%s)\n",
				  slot_name(p_slot));
			return -ENODEV;
		}
	}

	if (POWER_CTRL(p_slot->ctrl)) {
		rc = pciehp_get_power_status(p_slot, &getstatus);
		if (rc || getstatus) {
			ctrl_info(ctrl, "Already enabled on slot(%s)\n",
				  slot_name(p_slot));
			return -EINVAL;
		}
	}

	pciehp_get_latch_status(p_slot, &getstatus);

	rc = board_added(p_slot);
	if (rc) {
		pciehp_get_latch_status(p_slot, &getstatus);
	}
	return rc;
}


int pciehp_disable_slot(struct slot *p_slot)
{
	u8 getstatus = 0;
	int ret = 0;
	struct controller *ctrl = p_slot->ctrl;

	if (!p_slot->ctrl)
		return 1;

	if (!HP_SUPR_RM(p_slot->ctrl)) {
		ret = pciehp_get_adapter_status(p_slot, &getstatus);
		if (ret || !getstatus) {
			ctrl_info(ctrl, "No adapter on slot(%s)\n",
				  slot_name(p_slot));
			return -ENODEV;
		}
	}

	if (MRL_SENS(p_slot->ctrl)) {
		ret = pciehp_get_latch_status(p_slot, &getstatus);
		if (ret || getstatus) {
			ctrl_info(ctrl, "Latch open on slot(%s)\n",
				  slot_name(p_slot));
			return -ENODEV;
		}
	}

	if (POWER_CTRL(p_slot->ctrl)) {
		ret = pciehp_get_power_status(p_slot, &getstatus);
		if (ret || !getstatus) {
			ctrl_info(ctrl, "Already disabled on slot(%s)\n",
				  slot_name(p_slot));
			return -EINVAL;
		}
	}

	return remove_board(p_slot);
}

int pciehp_sysfs_enable_slot(struct slot *p_slot)
{
	int retval = -ENODEV;
	struct controller *ctrl = p_slot->ctrl;

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
		ctrl_info(ctrl, "Slot %s is already in powering on state\n",
			  slot_name(p_slot));
		break;
	case BLINKINGOFF_STATE:
	case POWEROFF_STATE:
		ctrl_info(ctrl, "Already enabled on slot %s\n",
			  slot_name(p_slot));
		break;
	default:
		ctrl_err(ctrl, "Not a valid state on slot %s\n",
			 slot_name(p_slot));
		break;
	}
	mutex_unlock(&p_slot->lock);

	return retval;
}

int pciehp_sysfs_disable_slot(struct slot *p_slot)
{
	int retval = -ENODEV;
	struct controller *ctrl = p_slot->ctrl;

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
		ctrl_info(ctrl, "Slot %s is already in powering off state\n",
			  slot_name(p_slot));
		break;
	case BLINKINGON_STATE:
	case POWERON_STATE:
		ctrl_info(ctrl, "Already disabled on slot %s\n",
			  slot_name(p_slot));
		break;
	default:
		ctrl_err(ctrl, "Not a valid state on slot %s\n",
			 slot_name(p_slot));
		break;
	}
	mutex_unlock(&p_slot->lock);

	return retval;
}

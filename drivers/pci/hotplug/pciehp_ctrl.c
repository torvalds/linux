// SPDX-License-Identifier: GPL-2.0+
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
 * Send feedback to <greg@kroah.com>, <kristen.c.accardi@intel.com>
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pm_runtime.h>
#include <linux/pci.h>
#include "pciehp.h"

/* The following routines constitute the bulk of the
   hotplug controller logic
 */

#define SAFE_REMOVAL	 true
#define SURPRISE_REMOVAL false

static void set_slot_off(struct controller *ctrl)
{
	/* turn off slot, turn on Amber LED, turn off Green LED if supported*/
	if (POWER_CTRL(ctrl)) {
		pciehp_power_off_slot(ctrl);

		/*
		 * After turning power off, we must wait for at least 1 second
		 * before taking any action that relies on power having been
		 * removed from the slot/adapter.
		 */
		msleep(1000);
	}

	pciehp_green_led_off(ctrl);
	pciehp_set_attention_status(ctrl, 1);
}

/**
 * board_added - Called after a board has been added to the system.
 * @ctrl: PCIe hotplug controller where board is added
 *
 * Turns power on for the board.
 * Configures board.
 */
static int board_added(struct controller *ctrl)
{
	int retval = 0;
	struct pci_bus *parent = ctrl->pcie->port->subordinate;

	if (POWER_CTRL(ctrl)) {
		/* Power on slot */
		retval = pciehp_power_on_slot(ctrl);
		if (retval)
			return retval;
	}

	pciehp_green_led_blink(ctrl);

	/* Check link training status */
	retval = pciehp_check_link_status(ctrl);
	if (retval) {
		ctrl_err(ctrl, "Failed to check link status\n");
		goto err_exit;
	}

	/* Check for a power fault */
	if (ctrl->power_fault_detected || pciehp_query_power_fault(ctrl)) {
		ctrl_err(ctrl, "Slot(%s): Power fault\n", slot_name(ctrl));
		retval = -EIO;
		goto err_exit;
	}

	retval = pciehp_configure_device(ctrl);
	if (retval) {
		if (retval != -EEXIST) {
			ctrl_err(ctrl, "Cannot add device at %04x:%02x:00\n",
				 pci_domain_nr(parent), parent->number);
			goto err_exit;
		}
	}

	pciehp_green_led_on(ctrl);
	pciehp_set_attention_status(ctrl, 0);
	return 0;

err_exit:
	set_slot_off(ctrl);
	return retval;
}

/**
 * remove_board - Turns off slot and LEDs
 * @ctrl: PCIe hotplug controller where board is being removed
 * @safe_removal: whether the board is safely removed (versus surprise removed)
 */
static void remove_board(struct controller *ctrl, bool safe_removal)
{
	pciehp_unconfigure_device(ctrl, safe_removal);

	if (POWER_CTRL(ctrl)) {
		pciehp_power_off_slot(ctrl);

		/*
		 * After turning power off, we must wait for at least 1 second
		 * before taking any action that relies on power having been
		 * removed from the slot/adapter.
		 */
		msleep(1000);
	}

	/* turn off Green LED */
	pciehp_green_led_off(ctrl);
}

static int pciehp_enable_slot(struct controller *ctrl);
static int pciehp_disable_slot(struct controller *ctrl, bool safe_removal);

void pciehp_request(struct controller *ctrl, int action)
{
	atomic_or(action, &ctrl->pending_events);
	if (!pciehp_poll_mode)
		irq_wake_thread(ctrl->pcie->irq, ctrl);
}

void pciehp_queue_pushbutton_work(struct work_struct *work)
{
	struct controller *ctrl = container_of(work, struct controller,
					       button_work.work);

	mutex_lock(&ctrl->state_lock);
	switch (ctrl->state) {
	case BLINKINGOFF_STATE:
		pciehp_request(ctrl, DISABLE_SLOT);
		break;
	case BLINKINGON_STATE:
		pciehp_request(ctrl, PCI_EXP_SLTSTA_PDC);
		break;
	default:
		break;
	}
	mutex_unlock(&ctrl->state_lock);
}

void pciehp_handle_button_press(struct controller *ctrl)
{
	mutex_lock(&ctrl->state_lock);
	switch (ctrl->state) {
	case OFF_STATE:
	case ON_STATE:
		if (ctrl->state == ON_STATE) {
			ctrl->state = BLINKINGOFF_STATE;
			ctrl_info(ctrl, "Slot(%s): Powering off due to button press\n",
				  slot_name(ctrl));
		} else {
			ctrl->state = BLINKINGON_STATE;
			ctrl_info(ctrl, "Slot(%s) Powering on due to button press\n",
				  slot_name(ctrl));
		}
		/* blink green LED and turn off amber */
		pciehp_green_led_blink(ctrl);
		pciehp_set_attention_status(ctrl, 0);
		schedule_delayed_work(&ctrl->button_work, 5 * HZ);
		break;
	case BLINKINGOFF_STATE:
	case BLINKINGON_STATE:
		/*
		 * Cancel if we are still blinking; this means that we
		 * press the attention again before the 5 sec. limit
		 * expires to cancel hot-add or hot-remove
		 */
		ctrl_info(ctrl, "Slot(%s): Button cancel\n", slot_name(ctrl));
		cancel_delayed_work(&ctrl->button_work);
		if (ctrl->state == BLINKINGOFF_STATE) {
			ctrl->state = ON_STATE;
			pciehp_green_led_on(ctrl);
		} else {
			ctrl->state = OFF_STATE;
			pciehp_green_led_off(ctrl);
		}
		pciehp_set_attention_status(ctrl, 0);
		ctrl_info(ctrl, "Slot(%s): Action canceled due to button press\n",
			  slot_name(ctrl));
		break;
	default:
		ctrl_err(ctrl, "Slot(%s): Ignoring invalid state %#x\n",
			 slot_name(ctrl), ctrl->state);
		break;
	}
	mutex_unlock(&ctrl->state_lock);
}

void pciehp_handle_disable_request(struct controller *ctrl)
{
	mutex_lock(&ctrl->state_lock);
	switch (ctrl->state) {
	case BLINKINGON_STATE:
	case BLINKINGOFF_STATE:
		cancel_delayed_work(&ctrl->button_work);
		break;
	}
	ctrl->state = POWEROFF_STATE;
	mutex_unlock(&ctrl->state_lock);

	ctrl->request_result = pciehp_disable_slot(ctrl, SAFE_REMOVAL);
}

void pciehp_handle_presence_or_link_change(struct controller *ctrl, u32 events)
{
	bool present, link_active;

	/*
	 * If the slot is on and presence or link has changed, turn it off.
	 * Even if it's occupied again, we cannot assume the card is the same.
	 */
	mutex_lock(&ctrl->state_lock);
	switch (ctrl->state) {
	case BLINKINGOFF_STATE:
		cancel_delayed_work(&ctrl->button_work);
		/* fall through */
	case ON_STATE:
		ctrl->state = POWEROFF_STATE;
		mutex_unlock(&ctrl->state_lock);
		if (events & PCI_EXP_SLTSTA_DLLSC)
			ctrl_info(ctrl, "Slot(%s): Link Down\n",
				  slot_name(ctrl));
		if (events & PCI_EXP_SLTSTA_PDC)
			ctrl_info(ctrl, "Slot(%s): Card not present\n",
				  slot_name(ctrl));
		pciehp_disable_slot(ctrl, SURPRISE_REMOVAL);
		break;
	default:
		mutex_unlock(&ctrl->state_lock);
		break;
	}

	/* Turn the slot on if it's occupied or link is up */
	mutex_lock(&ctrl->state_lock);
	present = pciehp_card_present(ctrl);
	link_active = pciehp_check_link_active(ctrl);
	if (!present && !link_active) {
		mutex_unlock(&ctrl->state_lock);
		return;
	}

	switch (ctrl->state) {
	case BLINKINGON_STATE:
		cancel_delayed_work(&ctrl->button_work);
		/* fall through */
	case OFF_STATE:
		ctrl->state = POWERON_STATE;
		mutex_unlock(&ctrl->state_lock);
		if (present)
			ctrl_info(ctrl, "Slot(%s): Card present\n",
				  slot_name(ctrl));
		if (link_active)
			ctrl_info(ctrl, "Slot(%s): Link Up\n",
				  slot_name(ctrl));
		ctrl->request_result = pciehp_enable_slot(ctrl);
		break;
	default:
		mutex_unlock(&ctrl->state_lock);
		break;
	}
}

static int __pciehp_enable_slot(struct controller *ctrl)
{
	u8 getstatus = 0;

	if (MRL_SENS(ctrl)) {
		pciehp_get_latch_status(ctrl, &getstatus);
		if (getstatus) {
			ctrl_info(ctrl, "Slot(%s): Latch open\n",
				  slot_name(ctrl));
			return -ENODEV;
		}
	}

	if (POWER_CTRL(ctrl)) {
		pciehp_get_power_status(ctrl, &getstatus);
		if (getstatus) {
			ctrl_info(ctrl, "Slot(%s): Already enabled\n",
				  slot_name(ctrl));
			return 0;
		}
	}

	return board_added(ctrl);
}

static int pciehp_enable_slot(struct controller *ctrl)
{
	int ret;

	pm_runtime_get_sync(&ctrl->pcie->port->dev);
	ret = __pciehp_enable_slot(ctrl);
	if (ret && ATTN_BUTTN(ctrl))
		pciehp_green_led_off(ctrl); /* may be blinking */
	pm_runtime_put(&ctrl->pcie->port->dev);

	mutex_lock(&ctrl->state_lock);
	ctrl->state = ret ? OFF_STATE : ON_STATE;
	mutex_unlock(&ctrl->state_lock);

	return ret;
}

static int __pciehp_disable_slot(struct controller *ctrl, bool safe_removal)
{
	u8 getstatus = 0;

	if (POWER_CTRL(ctrl)) {
		pciehp_get_power_status(ctrl, &getstatus);
		if (!getstatus) {
			ctrl_info(ctrl, "Slot(%s): Already disabled\n",
				  slot_name(ctrl));
			return -EINVAL;
		}
	}

	remove_board(ctrl, safe_removal);
	return 0;
}

static int pciehp_disable_slot(struct controller *ctrl, bool safe_removal)
{
	int ret;

	pm_runtime_get_sync(&ctrl->pcie->port->dev);
	ret = __pciehp_disable_slot(ctrl, safe_removal);
	pm_runtime_put(&ctrl->pcie->port->dev);

	mutex_lock(&ctrl->state_lock);
	ctrl->state = OFF_STATE;
	mutex_unlock(&ctrl->state_lock);

	return ret;
}

int pciehp_sysfs_enable_slot(struct hotplug_slot *hotplug_slot)
{
	struct controller *ctrl = to_ctrl(hotplug_slot);

	mutex_lock(&ctrl->state_lock);
	switch (ctrl->state) {
	case BLINKINGON_STATE:
	case OFF_STATE:
		mutex_unlock(&ctrl->state_lock);
		/*
		 * The IRQ thread becomes a no-op if the user pulls out the
		 * card before the thread wakes up, so initialize to -ENODEV.
		 */
		ctrl->request_result = -ENODEV;
		pciehp_request(ctrl, PCI_EXP_SLTSTA_PDC);
		wait_event(ctrl->requester,
			   !atomic_read(&ctrl->pending_events));
		return ctrl->request_result;
	case POWERON_STATE:
		ctrl_info(ctrl, "Slot(%s): Already in powering on state\n",
			  slot_name(ctrl));
		break;
	case BLINKINGOFF_STATE:
	case ON_STATE:
	case POWEROFF_STATE:
		ctrl_info(ctrl, "Slot(%s): Already enabled\n",
			  slot_name(ctrl));
		break;
	default:
		ctrl_err(ctrl, "Slot(%s): Invalid state %#x\n",
			 slot_name(ctrl), ctrl->state);
		break;
	}
	mutex_unlock(&ctrl->state_lock);

	return -ENODEV;
}

int pciehp_sysfs_disable_slot(struct hotplug_slot *hotplug_slot)
{
	struct controller *ctrl = to_ctrl(hotplug_slot);

	mutex_lock(&ctrl->state_lock);
	switch (ctrl->state) {
	case BLINKINGOFF_STATE:
	case ON_STATE:
		mutex_unlock(&ctrl->state_lock);
		pciehp_request(ctrl, DISABLE_SLOT);
		wait_event(ctrl->requester,
			   !atomic_read(&ctrl->pending_events));
		return ctrl->request_result;
	case POWEROFF_STATE:
		ctrl_info(ctrl, "Slot(%s): Already in powering off state\n",
			  slot_name(ctrl));
		break;
	case BLINKINGON_STATE:
	case OFF_STATE:
	case POWERON_STATE:
		ctrl_info(ctrl, "Slot(%s): Already disabled\n",
			  slot_name(ctrl));
		break;
	default:
		ctrl_err(ctrl, "Slot(%s): Invalid state %#x\n",
			 slot_name(ctrl), ctrl->state);
		break;
	}
	mutex_unlock(&ctrl->state_lock);

	return -ENODEV;
}

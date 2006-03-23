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
#include "../pci.h"
#include "pciehp.h"

static void interrupt_event_handler(struct controller *ctrl);

static struct semaphore event_semaphore;	/* mutex for process loop (up if something to process) */
static struct semaphore event_exit;		/* guard ensure thread has exited before calling it quits */
static int event_finished;
static unsigned long pushbutton_pending;	/* = 0 */
static unsigned long surprise_rm_pending;	/* = 0 */

u8 pciehp_handle_attention_button(u8 hp_slot, void *inst_id)
{
	struct controller *ctrl = (struct controller *) inst_id;
	struct slot *p_slot;
	u8 rc = 0;
	u8 getstatus;
	struct event_info *taskInfo;

	/* Attention Button Change */
	dbg("pciehp:  Attention button interrupt received.\n");
	
	/* This is the structure that tells the worker thread what to do */
	taskInfo = &(ctrl->event_queue[ctrl->next_event]);
	p_slot = pciehp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
	
	ctrl->next_event = (ctrl->next_event + 1) % MAX_EVENTS;
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

u8 pciehp_handle_switch_change(u8 hp_slot, void *inst_id)
{
	struct controller *ctrl = (struct controller *) inst_id;
	struct slot *p_slot;
	u8 rc = 0;
	u8 getstatus;
	struct event_info *taskInfo;

	/* Switch Change */
	dbg("pciehp:  Switch interrupt received.\n");

	/* This is the structure that tells the worker thread
	 * what to do
	 */
	taskInfo = &(ctrl->event_queue[ctrl->next_event]);
	ctrl->next_event = (ctrl->next_event + 1) % MAX_EVENTS;
	taskInfo->hp_slot = hp_slot;

	rc++;
	p_slot = pciehp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);
	p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);

	if (getstatus) {
		/*
		 * Switch opened
		 */
		info("Latch open on Slot(%d)\n", ctrl->first_slot + hp_slot);
		taskInfo->event_type = INT_SWITCH_OPEN;
	} else {
		/*
		 *  Switch closed
		 */
		info("Latch close on Slot(%d)\n", ctrl->first_slot + hp_slot);
		taskInfo->event_type = INT_SWITCH_CLOSE;
	}

	if (rc)
		up(&event_semaphore);	/* signal event thread that new event is posted */

	return rc;
}

u8 pciehp_handle_presence_change(u8 hp_slot, void *inst_id)
{
	struct controller *ctrl = (struct controller *) inst_id;
	struct slot *p_slot;
	u8 presence_save, rc = 0;
	struct event_info *taskInfo;

	/* Presence Change */
	dbg("pciehp:  Presence/Notify input change.\n");

	/* This is the structure that tells the worker thread
	 * what to do
	 */
	taskInfo = &(ctrl->event_queue[ctrl->next_event]);
	ctrl->next_event = (ctrl->next_event + 1) % MAX_EVENTS;
	taskInfo->hp_slot = hp_slot;

	rc++;
	p_slot = pciehp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	/* Switch is open, assume a presence change
	 * Save the presence state
	 */
	p_slot->hpc_ops->get_adapter_status(p_slot, &presence_save);
	if (presence_save) {
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

u8 pciehp_handle_power_fault(u8 hp_slot, void *inst_id)
{
	struct controller *ctrl = (struct controller *) inst_id;
	struct slot *p_slot;
	u8 rc = 0;
	struct event_info *taskInfo;

	/* power fault */
	dbg("pciehp:  Power fault interrupt received.\n");

	/* this is the structure that tells the worker thread
	 * what to do
	 */
	taskInfo = &(ctrl->event_queue[ctrl->next_event]);
	ctrl->next_event = (ctrl->next_event + 1) % MAX_EVENTS;
	taskInfo->hp_slot = hp_slot;

	rc++;
	p_slot = pciehp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	if ( !(p_slot->hpc_ops->query_power_fault(p_slot))) {
		/*
		 * power fault Cleared
		 */
		info("Power fault cleared on Slot(%d)\n", ctrl->first_slot + hp_slot);
		taskInfo->event_type = INT_POWER_FAULT_CLEAR;
	} else {
		/*
		 *   power fault
		 */
		info("Power fault on Slot(%d)\n", ctrl->first_slot + hp_slot);
		taskInfo->event_type = INT_POWER_FAULT;
		info("power fault bit %x set\n", hp_slot);
	}
	if (rc)
		up(&event_semaphore);	/* signal event thread that new event is posted */

	return rc;
}

/* The following routines constitute the bulk of the 
   hotplug controller logic
 */

static void set_slot_off(struct controller *ctrl, struct slot * pslot)
{
	/* Wait for exclusive access to hardware */
	mutex_lock(&ctrl->crit_sect);

	/* turn off slot, turn on Amber LED, turn off Green LED if supported*/
	if (POWER_CTRL(ctrl->ctrlcap)) {
		if (pslot->hpc_ops->power_off_slot(pslot)) {   
			err("%s: Issue of Slot Power Off command failed\n", __FUNCTION__);
			mutex_unlock(&ctrl->crit_sect);
			return;
		}
		wait_for_ctrl_irq (ctrl);
	}

	if (PWR_LED(ctrl->ctrlcap)) {
		pslot->hpc_ops->green_led_off(pslot);   
		wait_for_ctrl_irq (ctrl);
	}

	if (ATTN_LED(ctrl->ctrlcap)) { 
		if (pslot->hpc_ops->set_attention_status(pslot, 1)) {   
			err("%s: Issue of Set Attention Led command failed\n", __FUNCTION__);
			mutex_unlock(&ctrl->crit_sect);
			return;
		}
		wait_for_ctrl_irq (ctrl);
	}

	/* Done with exclusive hardware access */
	mutex_unlock(&ctrl->crit_sect);
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
	int rc = 0;
	struct controller *ctrl = p_slot->ctrl;

	hp_slot = p_slot->device - ctrl->slot_device_offset;

	dbg("%s: slot device, slot offset, hp slot = %d, %d ,%d\n",
			__FUNCTION__, p_slot->device,
			ctrl->slot_device_offset, hp_slot);

	/* Wait for exclusive access to hardware */
	mutex_lock(&ctrl->crit_sect);

	if (POWER_CTRL(ctrl->ctrlcap)) {
		/* Power on slot */
		rc = p_slot->hpc_ops->power_on_slot(p_slot);
		if (rc) {
			mutex_unlock(&ctrl->crit_sect);
			return -1;
		}

		/* Wait for the command to complete */
		wait_for_ctrl_irq (ctrl);
	}
	
	if (PWR_LED(ctrl->ctrlcap)) {
		p_slot->hpc_ops->green_led_blink(p_slot);
			
		/* Wait for the command to complete */
		wait_for_ctrl_irq (ctrl);
	}

	/* Done with exclusive hardware access */
	mutex_unlock(&ctrl->crit_sect);

	/* Wait for ~1 second */
	wait_for_ctrl_irq (ctrl);

	/*  Check link training status */
	rc = p_slot->hpc_ops->check_lnk_status(ctrl);  
	if (rc) {
		err("%s: Failed to check link status\n", __FUNCTION__);
		set_slot_off(ctrl, p_slot);
		return rc;
	}

	/* Check for a power fault */
	if (p_slot->hpc_ops->query_power_fault(p_slot)) {
		dbg("%s: power fault detected\n", __FUNCTION__);
		rc = POWER_FAILURE;
		goto err_exit;
	}

	rc = pciehp_configure_device(p_slot);
	if (rc) {
		err("Cannot add device 0x%x:%x\n", p_slot->bus,
				p_slot->device);
		goto err_exit;
	}

	/*
	 * Some PCI Express root ports require fixup after hot-plug operation.
	 */
	if (pcie_mch_quirk)
		pci_fixup_device(pci_fixup_final, ctrl->pci_dev);
	if (PWR_LED(ctrl->ctrlcap)) {
		/* Wait for exclusive access to hardware */
  		mutex_lock(&ctrl->crit_sect);

  		p_slot->hpc_ops->green_led_on(p_slot);
  
  		/* Wait for the command to complete */
  		wait_for_ctrl_irq (ctrl);
  	
  		/* Done with exclusive hardware access */
  		mutex_unlock(&ctrl->crit_sect);
  	}
	return 0;

err_exit:
	set_slot_off(ctrl, p_slot);
	return -1;
}


/**
 * remove_board - Turns off slot and LED's
 *
 */
static int remove_board(struct slot *p_slot)
{
	u8 device;
	u8 hp_slot;
	int rc;
	struct controller *ctrl = p_slot->ctrl;

	if (pciehp_unconfigure_device(p_slot))
		return 1;

	device = p_slot->device;

	hp_slot = p_slot->device - ctrl->slot_device_offset;
	p_slot = pciehp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	dbg("In %s, hp_slot = %d\n", __FUNCTION__, hp_slot);

	/* Wait for exclusive access to hardware */
	mutex_lock(&ctrl->crit_sect);

	if (POWER_CTRL(ctrl->ctrlcap)) {
		/* power off slot */
		rc = p_slot->hpc_ops->power_off_slot(p_slot);
		if (rc) {
			err("%s: Issue of Slot Disable command failed\n", __FUNCTION__);
			mutex_unlock(&ctrl->crit_sect);
			return rc;
		}
		/* Wait for the command to complete */
		wait_for_ctrl_irq (ctrl);
	}

	if (PWR_LED(ctrl->ctrlcap)) {
		/* turn off Green LED */
		p_slot->hpc_ops->green_led_off(p_slot);
	
		/* Wait for the command to complete */
		wait_for_ctrl_irq (ctrl);
	}

	/* Done with exclusive hardware access */
	mutex_unlock(&ctrl->crit_sect);

	return 0;
}


static void pushbutton_helper_thread(unsigned long data)
{
	pushbutton_pending = data;

	up(&event_semaphore);
}

/**
 * pciehp_pushbutton_thread
 *
 * Scheduled procedure to handle blocking stuff for the pushbuttons
 * Handles all pending events and exits.
 *
 */
static void pciehp_pushbutton_thread(unsigned long slot)
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
		dbg("%s: disabling bus:device(%x:%x)\n", __FUNCTION__,
				p_slot->bus, p_slot->device);

		pciehp_disable_slot(p_slot);
		p_slot->state = STATIC_STATE;
	} else {
		p_slot->state = POWERON_STATE;
		dbg("%s: adding bus:device(%x:%x)\n", __FUNCTION__,
				p_slot->bus, p_slot->device);

		if (pciehp_enable_slot(p_slot) && PWR_LED(p_slot->ctrl->ctrlcap)) {
			/* Wait for exclusive access to hardware */
			mutex_lock(&p_slot->ctrl->crit_sect);

			p_slot->hpc_ops->green_led_off(p_slot);

			/* Wait for the command to complete */
			wait_for_ctrl_irq (p_slot->ctrl);

			/* Done with exclusive hardware access */
			mutex_unlock(&p_slot->ctrl->crit_sect);
		}
		p_slot->state = STATIC_STATE;
	}

	return;
}

/**
 * pciehp_surprise_rm_thread
 *
 * Scheduled procedure to handle blocking stuff for the surprise removal
 * Handles all pending events and exits.
 *
 */
static void pciehp_surprise_rm_thread(unsigned long slot)
{
	struct slot *p_slot = (struct slot *) slot;
	u8 getstatus;
	
	surprise_rm_pending = 0;

	if (!p_slot) {
		dbg("%s: Error! slot NULL\n", __FUNCTION__);
		return;
	}

	p_slot->hpc_ops->get_adapter_status(p_slot, &getstatus);
	if (!getstatus) {
		p_slot->state = POWEROFF_STATE;
		dbg("%s: removing bus:device(%x:%x)\n",
				__FUNCTION__, p_slot->bus, p_slot->device);

		pciehp_disable_slot(p_slot);
		p_slot->state = STATIC_STATE;
	} else {
		p_slot->state = POWERON_STATE;
		dbg("%s: adding bus:device(%x:%x)\n",
				__FUNCTION__, p_slot->bus, p_slot->device);

		if (pciehp_enable_slot(p_slot) && PWR_LED(p_slot->ctrl->ctrlcap)) {
			/* Wait for exclusive access to hardware */
			mutex_lock(&p_slot->ctrl->crit_sect);

			p_slot->hpc_ops->green_led_off(p_slot);

			/* Wait for the command to complete */
			wait_for_ctrl_irq (p_slot->ctrl);

			/* Done with exclusive hardware access */
			mutex_unlock(&p_slot->ctrl->crit_sect);
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
	daemonize("pciehpd_event");

	unlock_kernel();

	while (1) {
		dbg("!!!!event_thread sleeping\n");
		down_interruptible (&event_semaphore);
		dbg("event_thread woken finished = %d\n", event_finished);
		if (event_finished || signal_pending(current))
			break;
		/* Do stuff here */
		if (pushbutton_pending)
			pciehp_pushbutton_thread(pushbutton_pending);
		else if (surprise_rm_pending)
			pciehp_surprise_rm_thread(surprise_rm_pending);
		else
			for (ctrl = pciehp_ctrl_list; ctrl; ctrl=ctrl->next)
				interrupt_event_handler(ctrl);
	}
	dbg("event_thread signals exit\n");
	up(&event_exit);
	return 0;
}

int pciehp_event_start_thread(void)
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
	return 0;
}


void pciehp_event_stop_thread(void)
{
	event_finished = 1;
	up(&event_semaphore);
	down(&event_exit);
}


static int update_slot_info(struct slot *slot)
{
	struct hotplug_slot_info *info;
	/* char buffer[SLOT_NAME_SIZE]; */
	int result;

	info = kmalloc(sizeof(struct hotplug_slot_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	/* make_slot_name (&buffer[0], SLOT_NAME_SIZE, slot); */

	slot->hpc_ops->get_power_status(slot, &(info->power_status));
	slot->hpc_ops->get_attention_status(slot, &(info->attention_status));
	slot->hpc_ops->get_latch_status(slot, &(info->latch_status));
	slot->hpc_ops->get_adapter_status(slot, &(info->adapter_status));

	/* result = pci_hp_change_slot_info(buffer, info); */
	result = pci_hp_change_slot_info(slot->hotplug_slot, info);
	kfree (info);
	return result;
}

static void interrupt_event_handler(struct controller *ctrl)
{
	int loop = 0;
	int change = 1;
	u8 hp_slot;
	u8 getstatus;
	struct slot *p_slot;

	while (change) {
		change = 0;

		for (loop = 0; loop < MAX_EVENTS; loop++) {
			if (ctrl->event_queue[loop].event_type != 0) {
				hp_slot = ctrl->event_queue[loop].hp_slot;

				p_slot = pciehp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

				if (ctrl->event_queue[loop].event_type == INT_BUTTON_CANCEL) {
					dbg("button cancel\n");
					del_timer(&p_slot->task_event);

					switch (p_slot->state) {
					case BLINKINGOFF_STATE:
						/* Wait for exclusive access to hardware */
						mutex_lock(&ctrl->crit_sect);
						
						if (PWR_LED(ctrl->ctrlcap)) {
							p_slot->hpc_ops->green_led_on(p_slot);
							/* Wait for the command to complete */
							wait_for_ctrl_irq (ctrl);
						}
						if (ATTN_LED(ctrl->ctrlcap)) {
							p_slot->hpc_ops->set_attention_status(p_slot, 0);

							/* Wait for the command to complete */
							wait_for_ctrl_irq (ctrl);
						}
						/* Done with exclusive hardware access */
						mutex_unlock(&ctrl->crit_sect);
						break;
					case BLINKINGON_STATE:
						/* Wait for exclusive access to hardware */
						mutex_lock(&ctrl->crit_sect);

						if (PWR_LED(ctrl->ctrlcap)) {
							p_slot->hpc_ops->green_led_off(p_slot);
							/* Wait for the command to complete */
							wait_for_ctrl_irq (ctrl);
						}
						if (ATTN_LED(ctrl->ctrlcap)){
							p_slot->hpc_ops->set_attention_status(p_slot, 0);
							/* Wait for the command to complete */
							wait_for_ctrl_irq (ctrl);
						}
						/* Done with exclusive hardware access */
						mutex_unlock(&ctrl->crit_sect);

						break;
					default:
						warn("Not a valid state\n");
						return;
					}
					info(msg_button_cancel, p_slot->number);
					p_slot->state = STATIC_STATE;
				}
				/* ***********Button Pressed (No action on 1st press...) */
				else if (ctrl->event_queue[loop].event_type == INT_BUTTON_PRESS) {
					
					if (ATTN_BUTTN(ctrl->ctrlcap)) {
						dbg("Button pressed\n");
						p_slot->hpc_ops->get_power_status(p_slot, &getstatus);
						if (getstatus) {
							/* slot is on */
							dbg("slot is on\n");
							p_slot->state = BLINKINGOFF_STATE;
							info(msg_button_off, p_slot->number);
						} else {
							/* slot is off */
							dbg("slot is off\n");
							p_slot->state = BLINKINGON_STATE;
							info(msg_button_on, p_slot->number);
						}

						/* Wait for exclusive access to hardware */
						mutex_lock(&ctrl->crit_sect);

						/* blink green LED and turn off amber */
						if (PWR_LED(ctrl->ctrlcap)) {
							p_slot->hpc_ops->green_led_blink(p_slot);
							/* Wait for the command to complete */
							wait_for_ctrl_irq (ctrl);
						}

						if (ATTN_LED(ctrl->ctrlcap)) {
							p_slot->hpc_ops->set_attention_status(p_slot, 0);

							/* Wait for the command to complete */
							wait_for_ctrl_irq (ctrl);
						}

						/* Done with exclusive hardware access */
						mutex_unlock(&ctrl->crit_sect);

						init_timer(&p_slot->task_event);
						p_slot->task_event.expires = jiffies + 5 * HZ;   /* 5 second delay */
						p_slot->task_event.function = (void (*)(unsigned long)) pushbutton_helper_thread;
						p_slot->task_event.data = (unsigned long) p_slot;

						add_timer(&p_slot->task_event);
					}
				}
				/***********POWER FAULT********************/
				else if (ctrl->event_queue[loop].event_type == INT_POWER_FAULT) {
					if (POWER_CTRL(ctrl->ctrlcap)) {
						dbg("power fault\n");
						/* Wait for exclusive access to hardware */
						mutex_lock(&ctrl->crit_sect);

						if (ATTN_LED(ctrl->ctrlcap)) {
							p_slot->hpc_ops->set_attention_status(p_slot, 1);
							wait_for_ctrl_irq (ctrl);
						}

						if (PWR_LED(ctrl->ctrlcap)) {
							p_slot->hpc_ops->green_led_off(p_slot);
							wait_for_ctrl_irq (ctrl);
						}

						/* Done with exclusive hardware access */
						mutex_unlock(&ctrl->crit_sect);
					}
				}
				/***********SURPRISE REMOVAL********************/
				else if ((ctrl->event_queue[loop].event_type == INT_PRESENCE_ON) || 
					(ctrl->event_queue[loop].event_type == INT_PRESENCE_OFF)) {
					if (HP_SUPR_RM(ctrl->ctrlcap)) {
						dbg("Surprise Removal\n");
						if (p_slot) {
							surprise_rm_pending = (unsigned long) p_slot;
							up(&event_semaphore);
							update_slot_info(p_slot);
						}
					}
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
}


int pciehp_enable_slot(struct slot *p_slot)
{
	u8 getstatus = 0;
	int rc;

	/* Check to see if (latch closed, card present, power off) */
	mutex_lock(&p_slot->ctrl->crit_sect);

	rc = p_slot->hpc_ops->get_adapter_status(p_slot, &getstatus);
	if (rc || !getstatus) {
		info("%s: no adapter on slot(%x)\n", __FUNCTION__, p_slot->number);
		mutex_unlock(&p_slot->ctrl->crit_sect);
		return 1;
	}
	if (MRL_SENS(p_slot->ctrl->ctrlcap)) {	
		rc = p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
		if (rc || getstatus) {
			info("%s: latch open on slot(%x)\n", __FUNCTION__, p_slot->number);
			mutex_unlock(&p_slot->ctrl->crit_sect);
			return 1;
		}
	}
	
	if (POWER_CTRL(p_slot->ctrl->ctrlcap)) {	
		rc = p_slot->hpc_ops->get_power_status(p_slot, &getstatus);
		if (rc || getstatus) {
			info("%s: already enabled on slot(%x)\n", __FUNCTION__, p_slot->number);
			mutex_unlock(&p_slot->ctrl->crit_sect);
			return 1;
		}
	}
	mutex_unlock(&p_slot->ctrl->crit_sect);

	p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);

	rc = board_added(p_slot);
	if (rc) {
		p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
	}

	if (p_slot)
		update_slot_info(p_slot);

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
			info("%s: no adapter on slot(%x)\n", __FUNCTION__, p_slot->number);
			mutex_unlock(&p_slot->ctrl->crit_sect);
			return 1;
		}
	}

	if (MRL_SENS(p_slot->ctrl->ctrlcap)) {	
		ret = p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
		if (ret || getstatus) {
			info("%s: latch open on slot(%x)\n", __FUNCTION__, p_slot->number);
			mutex_unlock(&p_slot->ctrl->crit_sect);
			return 1;
		}
	}

	if (POWER_CTRL(p_slot->ctrl->ctrlcap)) {	
		ret = p_slot->hpc_ops->get_power_status(p_slot, &getstatus);
		if (ret || !getstatus) {
			info("%s: already disabled slot(%x)\n", __FUNCTION__, p_slot->number);
			mutex_unlock(&p_slot->ctrl->crit_sect);
			return 1;
		}
	}

	mutex_unlock(&p_slot->ctrl->crit_sect);

	ret = remove_board(p_slot);
	update_slot_info(p_slot);
	return ret;
}


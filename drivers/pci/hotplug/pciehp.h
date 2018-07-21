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
#ifndef _PCIEHP_H
#define _PCIEHP_H

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/delay.h>
#include <linux/sched/signal.h>		/* signal_pending() */
#include <linux/pcieport_if.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#define MY_NAME	"pciehp"

extern bool pciehp_poll_mode;
extern int pciehp_poll_time;
extern bool pciehp_debug;

#define dbg(format, arg...)						\
do {									\
	if (pciehp_debug)						\
		printk(KERN_DEBUG "%s: " format, MY_NAME, ## arg);	\
} while (0)
#define err(format, arg...)						\
	printk(KERN_ERR "%s: " format, MY_NAME, ## arg)
#define info(format, arg...)						\
	printk(KERN_INFO "%s: " format, MY_NAME, ## arg)
#define warn(format, arg...)						\
	printk(KERN_WARNING "%s: " format, MY_NAME, ## arg)

#define ctrl_dbg(ctrl, format, arg...)					\
	do {								\
		if (pciehp_debug)					\
			dev_printk(KERN_DEBUG, &ctrl->pcie->device,	\
					format, ## arg);		\
	} while (0)
#define ctrl_err(ctrl, format, arg...)					\
	dev_err(&ctrl->pcie->device, format, ## arg)
#define ctrl_info(ctrl, format, arg...)					\
	dev_info(&ctrl->pcie->device, format, ## arg)
#define ctrl_warn(ctrl, format, arg...)					\
	dev_warn(&ctrl->pcie->device, format, ## arg)

#define SLOT_NAME_SIZE 10
struct slot {
	u8 state;
	struct controller *ctrl;
	struct hotplug_slot *hotplug_slot;
	struct delayed_work work;	/* work for button event */
	struct mutex lock;
	struct mutex hotplug_lock;
	struct workqueue_struct *wq;
};

struct event_info {
	u32 event_type;
	struct slot *p_slot;
	struct work_struct work;
};

struct controller {
	struct mutex ctrl_lock;		/* controller lock */
	struct pcie_device *pcie;	/* PCI Express port service */
	struct slot *slot;
	wait_queue_head_t queue;	/* sleep & wake process */
	u32 slot_cap;
	u16 slot_ctrl;
	struct timer_list poll_timer;
	unsigned long cmd_started;	/* jiffies */
	unsigned int cmd_busy:1;
	unsigned int link_active_reporting:1;
	unsigned int notification_enabled:1;
	unsigned int power_fault_detected;
};

#define INT_PRESENCE_ON			1
#define INT_PRESENCE_OFF		2
#define INT_POWER_FAULT			3
#define INT_BUTTON_PRESS		4
#define INT_LINK_UP			5
#define INT_LINK_DOWN			6

#define STATIC_STATE			0
#define BLINKINGON_STATE		1
#define BLINKINGOFF_STATE		2
#define POWERON_STATE			3
#define POWEROFF_STATE			4

#define ATTN_BUTTN(ctrl)	((ctrl)->slot_cap & PCI_EXP_SLTCAP_ABP)
#define POWER_CTRL(ctrl)	((ctrl)->slot_cap & PCI_EXP_SLTCAP_PCP)
#define MRL_SENS(ctrl)		((ctrl)->slot_cap & PCI_EXP_SLTCAP_MRLSP)
#define ATTN_LED(ctrl)		((ctrl)->slot_cap & PCI_EXP_SLTCAP_AIP)
#define PWR_LED(ctrl)		((ctrl)->slot_cap & PCI_EXP_SLTCAP_PIP)
#define HP_SUPR_RM(ctrl)	((ctrl)->slot_cap & PCI_EXP_SLTCAP_HPS)
#define EMI(ctrl)		((ctrl)->slot_cap & PCI_EXP_SLTCAP_EIP)
#define NO_CMD_CMPL(ctrl)	((ctrl)->slot_cap & PCI_EXP_SLTCAP_NCCS)
#define PSN(ctrl)		(((ctrl)->slot_cap & PCI_EXP_SLTCAP_PSN) >> 19)

int pciehp_sysfs_enable_slot(struct slot *slot);
int pciehp_sysfs_disable_slot(struct slot *slot);
void pciehp_queue_interrupt_event(struct slot *slot, u32 event_type);
int pciehp_configure_device(struct slot *p_slot);
int pciehp_unconfigure_device(struct slot *p_slot);
void pciehp_queue_pushbutton_work(struct work_struct *work);
struct controller *pcie_init(struct pcie_device *dev);
int pcie_init_notification(struct controller *ctrl);
int pciehp_enable_slot(struct slot *p_slot);
int pciehp_disable_slot(struct slot *p_slot);
void pcie_reenable_notification(struct controller *ctrl);
int pciehp_power_on_slot(struct slot *slot);
void pciehp_power_off_slot(struct slot *slot);
void pciehp_get_power_status(struct slot *slot, u8 *status);
void pciehp_get_attention_status(struct slot *slot, u8 *status);

void pciehp_set_attention_status(struct slot *slot, u8 status);
void pciehp_get_latch_status(struct slot *slot, u8 *status);
void pciehp_get_adapter_status(struct slot *slot, u8 *status);
int pciehp_query_power_fault(struct slot *slot);
void pciehp_green_led_on(struct slot *slot);
void pciehp_green_led_off(struct slot *slot);
void pciehp_green_led_blink(struct slot *slot);
int pciehp_check_link_status(struct controller *ctrl);
bool pciehp_check_link_active(struct controller *ctrl);
void pciehp_release_ctrl(struct controller *ctrl);
int pciehp_reset_slot(struct slot *slot, int probe);

int pciehp_set_raw_indicator_status(struct hotplug_slot *h_slot, u8 status);
int pciehp_get_raw_indicator_status(struct hotplug_slot *h_slot, u8 *status);

static inline const char *slot_name(struct slot *slot)
{
	return hotplug_slot_name(slot->hotplug_slot);
}

#endif				/* _PCIEHP_H */

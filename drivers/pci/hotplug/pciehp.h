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
#include <linux/sched.h>		/* signal_pending() */
#include <linux/pcieport_if.h>
#include <linux/mutex.h>

#define MY_NAME	"pciehp"

extern int pciehp_poll_mode;
extern int pciehp_poll_time;
extern int pciehp_debug;
extern int pciehp_force;
extern struct workqueue_struct *pciehp_wq;

#define dbg(format, arg...)						\
	do {								\
		if (pciehp_debug)					\
			printk("%s: " format, MY_NAME , ## arg);	\
	} while (0)
#define err(format, arg...)						\
	printk(KERN_ERR "%s: " format, MY_NAME , ## arg)
#define info(format, arg...)						\
	printk(KERN_INFO "%s: " format, MY_NAME , ## arg)
#define warn(format, arg...)						\
	printk(KERN_WARNING "%s: " format, MY_NAME , ## arg)

#define ctrl_dbg(ctrl, format, arg...)					\
	do {								\
		if (pciehp_debug)					\
			dev_printk(, &ctrl->pcie->device,		\
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
	u8 bus;
	u8 device;
	u8 state;
	u8 hp_slot;
	u32 number;
	struct controller *ctrl;
	struct hpc_ops *hpc_ops;
	struct hotplug_slot *hotplug_slot;
	struct list_head	slot_list;
	unsigned long last_emi_toggle;
	struct delayed_work work;	/* work for button event */
	struct mutex lock;
};

struct event_info {
	u32 event_type;
	struct slot *p_slot;
	struct work_struct work;
};

struct controller {
	struct mutex crit_sect;		/* critical section mutex */
	struct mutex ctrl_lock;		/* controller lock */
	int num_slots;			/* Number of slots on ctlr */
	int slot_num_inc;		/* 1 or -1 */
	struct pci_dev *pci_dev;
	struct pcie_device *pcie;	/* PCI Express port service */
	struct list_head slot_list;
	struct hpc_ops *hpc_ops;
	wait_queue_head_t queue;	/* sleep & wake process */
	u8 slot_device_offset;
	u32 first_slot;		/* First physical slot number */  /* PCIE only has 1 slot */
	u8 slot_bus;		/* Bus where the slots handled by this controller sit */
	u32 slot_cap;
	u8 cap_base;
	struct timer_list poll_timer;
	int cmd_busy;
	unsigned int no_cmd_complete:1;
	unsigned int link_active_reporting:1;
};

#define INT_BUTTON_IGNORE		0
#define INT_PRESENCE_ON			1
#define INT_PRESENCE_OFF		2
#define INT_SWITCH_CLOSE		3
#define INT_SWITCH_OPEN			4
#define INT_POWER_FAULT			5
#define INT_POWER_FAULT_CLEAR		6
#define INT_BUTTON_PRESS		7
#define INT_BUTTON_RELEASE		8
#define INT_BUTTON_CANCEL		9

#define STATIC_STATE			0
#define BLINKINGON_STATE		1
#define BLINKINGOFF_STATE		2
#define POWERON_STATE			3
#define POWEROFF_STATE			4

/* Error messages */
#define INTERLOCK_OPEN			0x00000002
#define ADD_NOT_SUPPORTED		0x00000003
#define CARD_FUNCTIONING		0x00000005
#define ADAPTER_NOT_SAME		0x00000006
#define NO_ADAPTER_PRESENT		0x00000009
#define NOT_ENOUGH_RESOURCES		0x0000000B
#define DEVICE_TYPE_NOT_SUPPORTED	0x0000000C
#define WRONG_BUS_FREQUENCY		0x0000000D
#define POWER_FAILURE			0x0000000E

/* Field definitions in Slot Capabilities Register */
#define ATTN_BUTTN_PRSN	0x00000001
#define	PWR_CTRL_PRSN	0x00000002
#define MRL_SENS_PRSN	0x00000004
#define ATTN_LED_PRSN	0x00000008
#define PWR_LED_PRSN	0x00000010
#define HP_SUPR_RM_SUP	0x00000020
#define EMI_PRSN	0x00020000
#define NO_CMD_CMPL_SUP	0x00040000

#define ATTN_BUTTN(ctrl)	((ctrl)->slot_cap & ATTN_BUTTN_PRSN)
#define POWER_CTRL(ctrl)	((ctrl)->slot_cap & PWR_CTRL_PRSN)
#define MRL_SENS(ctrl)		((ctrl)->slot_cap & MRL_SENS_PRSN)
#define ATTN_LED(ctrl)		((ctrl)->slot_cap & ATTN_LED_PRSN)
#define PWR_LED(ctrl)		((ctrl)->slot_cap & PWR_LED_PRSN)
#define HP_SUPR_RM(ctrl)	((ctrl)->slot_cap & HP_SUPR_RM_SUP)
#define EMI(ctrl)		((ctrl)->slot_cap & EMI_PRSN)
#define NO_CMD_CMPL(ctrl)	((ctrl)->slot_cap & NO_CMD_CMPL_SUP)

extern int pciehp_sysfs_enable_slot(struct slot *slot);
extern int pciehp_sysfs_disable_slot(struct slot *slot);
extern u8 pciehp_handle_attention_button(struct slot *p_slot);
  extern u8 pciehp_handle_switch_change(struct slot *p_slot);
extern u8 pciehp_handle_presence_change(struct slot *p_slot);
extern u8 pciehp_handle_power_fault(struct slot *p_slot);
extern int pciehp_configure_device(struct slot *p_slot);
extern int pciehp_unconfigure_device(struct slot *p_slot);
extern void pciehp_queue_pushbutton_work(struct work_struct *work);
struct controller *pcie_init(struct pcie_device *dev);
int pciehp_enable_slot(struct slot *p_slot);
int pciehp_disable_slot(struct slot *p_slot);
int pcie_enable_notification(struct controller *ctrl);

static inline const char *slot_name(struct slot *slot)
{
	return hotplug_slot_name(slot->hotplug_slot);
}

static inline struct slot *pciehp_find_slot(struct controller *ctrl, u8 device)
{
	struct slot *slot;

	list_for_each_entry(slot, &ctrl->slot_list, slot_list) {
		if (slot->device == device)
			return slot;
	}

	ctrl_err(ctrl, "Slot (device=0x%02x) not found\n", device);
	return NULL;
}

struct hpc_ops {
	int (*power_on_slot)(struct slot *slot);
	int (*power_off_slot)(struct slot *slot);
	int (*get_power_status)(struct slot *slot, u8 *status);
	int (*get_attention_status)(struct slot *slot, u8 *status);
	int (*set_attention_status)(struct slot *slot, u8 status);
	int (*get_latch_status)(struct slot *slot, u8 *status);
	int (*get_adapter_status)(struct slot *slot, u8 *status);
	int (*get_emi_status)(struct slot *slot, u8 *status);
	int (*toggle_emi)(struct slot *slot);
	int (*get_max_bus_speed)(struct slot *slot, enum pci_bus_speed *speed);
	int (*get_cur_bus_speed)(struct slot *slot, enum pci_bus_speed *speed);
	int (*get_max_lnk_width)(struct slot *slot, enum pcie_link_width *val);
	int (*get_cur_lnk_width)(struct slot *slot, enum pcie_link_width *val);
	int (*query_power_fault)(struct slot *slot);
	void (*green_led_on)(struct slot *slot);
	void (*green_led_off)(struct slot *slot);
	void (*green_led_blink)(struct slot *slot);
	void (*release_ctlr)(struct controller *ctrl);
	int (*check_lnk_status)(struct controller *ctrl);
};

#ifdef CONFIG_ACPI
#include <acpi/acpi.h>
#include <acpi/acpi_bus.h>
#include <linux/pci-acpi.h>

extern void __init pciehp_acpi_slot_detection_init(void);
extern int pciehp_acpi_slot_detection_check(struct pci_dev *dev);

static inline void pciehp_firmware_init(void)
{
	pciehp_acpi_slot_detection_init();
}

static inline int pciehp_get_hp_hw_control_from_firmware(struct pci_dev *dev)
{
	int retval;
	u32 flags = (OSC_PCI_EXPRESS_NATIVE_HP_CONTROL |
		     OSC_PCI_EXPRESS_CAP_STRUCTURE_CONTROL);
	retval = acpi_get_hp_hw_control_from_firmware(dev, flags);
	if (retval)
		return retval;
	return pciehp_acpi_slot_detection_check(dev);
}

static inline int pciehp_get_hp_params_from_firmware(struct pci_dev *dev,
			struct hotplug_params *hpp)
{
	if (ACPI_FAILURE(acpi_get_hp_params_from_firmware(dev->bus, hpp)))
		return -ENODEV;
	return 0;
}
#else
#define pciehp_firmware_init()				do {} while (0)
#define pciehp_get_hp_hw_control_from_firmware(dev) 	0
#define pciehp_get_hp_params_from_firmware(dev, hpp)    (-ENODEV)
#endif 				/* CONFIG_ACPI */
#endif				/* _PCIEHP_H */

/*
 * Standard Hot Plug Controller Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM
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
 * Send feedback to <greg@kroah.com>,<kristen.c.accardi@intel.com>
 *
 */
#ifndef _SHPCHP_H
#define _SHPCHP_H

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/sched.h>	/* signal_pending(), struct timer_list */
#include <linux/mutex.h>

#include "pci_hotplug.h"

#if !defined(MODULE)
	#define MY_NAME	"shpchp"
#else
	#define MY_NAME	THIS_MODULE->name
#endif

extern int shpchp_poll_mode;
extern int shpchp_poll_time;
extern int shpchp_debug;
extern struct workqueue_struct *shpchp_wq;

/*#define dbg(format, arg...) do { if (shpchp_debug) printk(KERN_DEBUG "%s: " format, MY_NAME , ## arg); } while (0)*/
#define dbg(format, arg...) do { if (shpchp_debug) printk("%s: " format, MY_NAME , ## arg); } while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format, MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format, MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format, MY_NAME , ## arg)

#define SLOT_NAME_SIZE 10
struct slot {
	u8 bus;
	u8 device;
	u16 status;
	u32 number;
	u8 is_a_board;
	u8 state;
	u8 presence_save;
	u8 pwr_save;
	struct timer_list task_event;
	u8 hp_slot;
	struct controller *ctrl;
	struct hpc_ops *hpc_ops;
	struct hotplug_slot *hotplug_slot;
	struct list_head	slot_list;
	char name[SLOT_NAME_SIZE];
	struct work_struct work;	/* work for button event */
	struct mutex lock;
};

struct event_info {
	u32 event_type;
	struct slot *p_slot;
	struct work_struct work;
};

struct controller {
	struct mutex crit_sect;		/* critical section mutex */
	struct mutex cmd_lock;		/* command lock */
	struct php_ctlr_state_s *hpc_ctlr_handle; /* HPC controller handle */
	int num_slots;			/* Number of slots on ctlr */
	int slot_num_inc;		/* 1 or -1 */
	struct pci_dev *pci_dev;
	struct list_head slot_list;
	struct hpc_ops *hpc_ops;
	wait_queue_head_t queue;	/* sleep & wake process */
	u8 bus;
	u8 device;
	u8 function;
	u8 slot_device_offset;
	u8 add_support;
	u32 pcix_misc2_reg;	/* for amd pogo errata */
	enum pci_bus_speed speed;
	u32 first_slot;		/* First physical slot number */
	u8 slot_bus;		/* Bus where the slots handled by this controller sit */
	u32 cap_offset;
	unsigned long mmio_base;
	unsigned long mmio_size;
	volatile int cmd_busy;
};


/* Define AMD SHPC ID  */
#define PCI_DEVICE_ID_AMD_GOLAM_7450	0x7450 
#define PCI_DEVICE_ID_AMD_POGO_7458	0x7458

/* AMD PCIX bridge registers */

#define PCIX_MEM_BASE_LIMIT_OFFSET	0x1C
#define PCIX_MISCII_OFFSET		0x48
#define PCIX_MISC_BRIDGE_ERRORS_OFFSET	0x80

/* AMD PCIX_MISCII masks and offsets */
#define PERRNONFATALENABLE_MASK		0x00040000
#define PERRFATALENABLE_MASK		0x00080000
#define PERRFLOODENABLE_MASK		0x00100000
#define SERRNONFATALENABLE_MASK		0x00200000
#define SERRFATALENABLE_MASK		0x00400000

/* AMD PCIX_MISC_BRIDGE_ERRORS masks and offsets */
#define PERR_OBSERVED_MASK		0x00000001

/* AMD PCIX_MEM_BASE_LIMIT masks */
#define RSE_MASK			0x40000000

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

#define PCI_TO_PCI_BRIDGE_CLASS		0x00060400

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

#define REMOVE_NOT_SUPPORTED		0x00000003

#define DISABLE_CARD			1

/*
 * error Messages
 */
#define msg_initialization_err	"Initialization failure, error=%d\n"
#define msg_button_on		"PCI slot #%d - powering on due to button press.\n"
#define msg_button_off		"PCI slot #%d - powering off due to button press.\n"
#define msg_button_cancel	"PCI slot #%d - action canceled due to button press.\n"

/* sysfs functions for the hotplug controller info */
extern void shpchp_create_ctrl_files	(struct controller *ctrl);

extern int	shpchp_sysfs_enable_slot(struct slot *slot);
extern int	shpchp_sysfs_disable_slot(struct slot *slot);

extern u8	shpchp_handle_attention_button(u8 hp_slot, void *inst_id);
extern u8	shpchp_handle_switch_change(u8 hp_slot, void *inst_id);
extern u8	shpchp_handle_presence_change(u8 hp_slot, void *inst_id);
extern u8	shpchp_handle_power_fault(u8 hp_slot, void *inst_id);

/* pci functions */
extern int	shpchp_save_config(struct controller *ctrl, int busnumber, int num_ctlr_slots, int first_device_num);
extern int	shpchp_configure_device(struct slot *p_slot);
extern int	shpchp_unconfigure_device(struct slot *p_slot);
extern void	shpchp_remove_ctrl_files(struct controller *ctrl);
extern void	cleanup_slots(struct controller *ctrl);
extern void	queue_pushbutton_work(void *data);


#ifdef CONFIG_ACPI
static inline int get_hp_params_from_firmware(struct pci_dev *dev,
			struct hotplug_params *hpp)
{
	if (ACPI_FAILURE(acpi_get_hp_params_from_firmware(dev, hpp)))
			return -ENODEV;
	return 0;
}
#define get_hp_hw_control_from_firmware(pdev) \
	do { \
		if (DEVICE_ACPI_HANDLE(&(pdev->dev))) \
			acpi_run_oshp(DEVICE_ACPI_HANDLE(&(pdev->dev))); \
	} while (0)
#else
#define get_hp_params_from_firmware(dev, hpp) (-ENODEV)
#define get_hp_hw_control_from_firmware(dev) do { } while (0)
#endif

struct ctrl_reg {
	volatile u32 base_offset;
	volatile u32 slot_avail1;
	volatile u32 slot_avail2;
	volatile u32 slot_config;
	volatile u16 sec_bus_config;
	volatile u8  msi_ctrl;
	volatile u8  prog_interface;
	volatile u16 cmd;
	volatile u16 cmd_status;
	volatile u32 intr_loc;
	volatile u32 serr_loc;
	volatile u32 serr_intr_enable;
	volatile u32 slot1;
	volatile u32 slot2;
	volatile u32 slot3;
	volatile u32 slot4;
	volatile u32 slot5;
	volatile u32 slot6;
	volatile u32 slot7;
	volatile u32 slot8;
	volatile u32 slot9;
	volatile u32 slot10;
	volatile u32 slot11;
	volatile u32 slot12;
} __attribute__ ((packed));

/* offsets to the controller registers based on the above structure layout */
enum ctrl_offsets {
	BASE_OFFSET =	offsetof(struct ctrl_reg, base_offset),
	SLOT_AVAIL1 =	offsetof(struct ctrl_reg, slot_avail1),
	SLOT_AVAIL2	=	offsetof(struct ctrl_reg, slot_avail2),
	SLOT_CONFIG =	offsetof(struct ctrl_reg, slot_config),
	SEC_BUS_CONFIG =	offsetof(struct ctrl_reg, sec_bus_config),
	MSI_CTRL	=	offsetof(struct ctrl_reg, msi_ctrl),
	PROG_INTERFACE =	offsetof(struct ctrl_reg, prog_interface),
	CMD		=	offsetof(struct ctrl_reg, cmd),
	CMD_STATUS	=	offsetof(struct ctrl_reg, cmd_status),
	INTR_LOC	= 	offsetof(struct ctrl_reg, intr_loc),
	SERR_LOC	= 	offsetof(struct ctrl_reg, serr_loc),
	SERR_INTR_ENABLE =	offsetof(struct ctrl_reg, serr_intr_enable),
	SLOT1 =		offsetof(struct ctrl_reg, slot1),
	SLOT2 =		offsetof(struct ctrl_reg, slot2),
	SLOT3 =		offsetof(struct ctrl_reg, slot3),
	SLOT4 =		offsetof(struct ctrl_reg, slot4),
	SLOT5 =		offsetof(struct ctrl_reg, slot5),
	SLOT6 =		offsetof(struct ctrl_reg, slot6),		
	SLOT7 =		offsetof(struct ctrl_reg, slot7),
	SLOT8 =		offsetof(struct ctrl_reg, slot8),
	SLOT9 =		offsetof(struct ctrl_reg, slot9),
	SLOT10 =	offsetof(struct ctrl_reg, slot10),
	SLOT11 =	offsetof(struct ctrl_reg, slot11),
	SLOT12 =	offsetof(struct ctrl_reg, slot12),
};
typedef u8(*php_intr_callback_t) (u8 hp_slot, void *instance_id);
struct php_ctlr_state_s {
	struct php_ctlr_state_s *pnext;
	struct pci_dev *pci_dev;
	unsigned int irq;
	unsigned long flags;	/* spinlock's */
	u32 slot_device_offset;
	u32 num_slots;
    	struct timer_list	int_poll_timer;	/* Added for poll event */
	php_intr_callback_t attention_button_callback;
	php_intr_callback_t switch_change_callback;
	php_intr_callback_t presence_change_callback;
	php_intr_callback_t power_fault_callback;
	void *callback_instance_id;
	void __iomem *creg;			/* Ptr to controller register space */
};
/* Inline functions */


/* Inline functions to check the sanity of a pointer that is passed to us */
static inline int slot_paranoia_check (struct slot *slot, const char *function)
{
	if (!slot) {
		dbg("%s - slot == NULL", function);
		return -1;
	}
	if (!slot->hotplug_slot) {
		dbg("%s - slot->hotplug_slot == NULL!", function);
		return -1;
	}
	return 0;
}

static inline struct slot *get_slot (struct hotplug_slot *hotplug_slot, const char *function)
{ 
	struct slot *slot;

	if (!hotplug_slot) {
		dbg("%s - hotplug_slot == NULL\n", function);
		return NULL;
	}

	slot = (struct slot *)hotplug_slot->private;
	if (slot_paranoia_check (slot, function))
                return NULL;
	return slot;
}

static inline struct slot *shpchp_find_slot (struct controller *ctrl, u8 device)
{
	struct slot *slot;

	if (!ctrl)
		return NULL;

	list_for_each_entry(slot, &ctrl->slot_list, slot_list) {
		if (slot->device == device)
			return slot;
	}

	err("%s: slot (device=0x%x) not found\n", __FUNCTION__, device);

	return NULL;
}

static inline void amd_pogo_errata_save_misc_reg(struct slot *p_slot)
{
	u32 pcix_misc2_temp;

	/* save MiscII register */
	pci_read_config_dword(p_slot->ctrl->pci_dev, PCIX_MISCII_OFFSET, &pcix_misc2_temp);

	p_slot->ctrl->pcix_misc2_reg = pcix_misc2_temp;

	/* clear SERR/PERR enable bits */
	pcix_misc2_temp &= ~SERRFATALENABLE_MASK;
	pcix_misc2_temp &= ~SERRNONFATALENABLE_MASK;
	pcix_misc2_temp &= ~PERRFLOODENABLE_MASK;
	pcix_misc2_temp &= ~PERRFATALENABLE_MASK;
	pcix_misc2_temp &= ~PERRNONFATALENABLE_MASK;
	pci_write_config_dword(p_slot->ctrl->pci_dev, PCIX_MISCII_OFFSET, pcix_misc2_temp);
}

static inline void amd_pogo_errata_restore_misc_reg(struct slot *p_slot)
{
	u32 pcix_misc2_temp;
	u32 pcix_bridge_errors_reg;
	u32 pcix_mem_base_reg;
	u8  perr_set;
	u8  rse_set;

	/* write-one-to-clear Bridge_Errors[ PERR_OBSERVED ] */
	pci_read_config_dword(p_slot->ctrl->pci_dev, PCIX_MISC_BRIDGE_ERRORS_OFFSET, &pcix_bridge_errors_reg);
	perr_set = pcix_bridge_errors_reg & PERR_OBSERVED_MASK;
	if (perr_set) {
		dbg ("%s  W1C: Bridge_Errors[ PERR_OBSERVED = %08X]\n",__FUNCTION__ , perr_set);

		pci_write_config_dword(p_slot->ctrl->pci_dev, PCIX_MISC_BRIDGE_ERRORS_OFFSET, perr_set);
	}

	/* write-one-to-clear Memory_Base_Limit[ RSE ] */
	pci_read_config_dword(p_slot->ctrl->pci_dev, PCIX_MEM_BASE_LIMIT_OFFSET, &pcix_mem_base_reg);
	rse_set = pcix_mem_base_reg & RSE_MASK;
	if (rse_set) {
		dbg ("%s  W1C: Memory_Base_Limit[ RSE ]\n",__FUNCTION__ );

		pci_write_config_dword(p_slot->ctrl->pci_dev, PCIX_MEM_BASE_LIMIT_OFFSET, rse_set);
	}
	/* restore MiscII register */
	pci_read_config_dword( p_slot->ctrl->pci_dev, PCIX_MISCII_OFFSET, &pcix_misc2_temp );

	if (p_slot->ctrl->pcix_misc2_reg & SERRFATALENABLE_MASK)
		pcix_misc2_temp |= SERRFATALENABLE_MASK;
	else
		pcix_misc2_temp &= ~SERRFATALENABLE_MASK;

	if (p_slot->ctrl->pcix_misc2_reg & SERRNONFATALENABLE_MASK)
		pcix_misc2_temp |= SERRNONFATALENABLE_MASK;
	else
		pcix_misc2_temp &= ~SERRNONFATALENABLE_MASK;

	if (p_slot->ctrl->pcix_misc2_reg & PERRFLOODENABLE_MASK)
		pcix_misc2_temp |= PERRFLOODENABLE_MASK;
	else
		pcix_misc2_temp &= ~PERRFLOODENABLE_MASK;

	if (p_slot->ctrl->pcix_misc2_reg & PERRFATALENABLE_MASK)
		pcix_misc2_temp |= PERRFATALENABLE_MASK;
	else
		pcix_misc2_temp &= ~PERRFATALENABLE_MASK;

	if (p_slot->ctrl->pcix_misc2_reg & PERRNONFATALENABLE_MASK)
		pcix_misc2_temp |= PERRNONFATALENABLE_MASK;
	else
		pcix_misc2_temp &= ~PERRNONFATALENABLE_MASK;
	pci_write_config_dword(p_slot->ctrl->pci_dev, PCIX_MISCII_OFFSET, pcix_misc2_temp);
}

enum php_ctlr_type {
	PCI,
	ISA,
	ACPI
};

int shpc_init( struct controller *ctrl, struct pci_dev *pdev);

int shpc_get_ctlr_slot_config( struct controller *ctrl,
		int *num_ctlr_slots,
		int *first_device_num,
		int *physical_slot_num,
		int *updown,
		int *flags);

struct hpc_ops {
	int	(*power_on_slot )		(struct slot *slot);
	int	(*slot_enable )			(struct slot *slot);
	int	(*slot_disable )		(struct slot *slot);
	int	(*set_bus_speed_mode)	(struct slot *slot, enum pci_bus_speed speed);
	int	(*get_power_status)		(struct slot *slot, u8 *status);
	int	(*get_attention_status)	(struct slot *slot, u8 *status);
	int	(*set_attention_status)	(struct slot *slot, u8 status);
	int	(*get_latch_status)		(struct slot *slot, u8 *status);
	int	(*get_adapter_status)	(struct slot *slot, u8 *status);

	int	(*get_max_bus_speed)	(struct slot *slot, enum pci_bus_speed *speed);
	int	(*get_cur_bus_speed)	(struct slot *slot, enum pci_bus_speed *speed);
	int	(*get_adapter_speed)	(struct slot *slot, enum pci_bus_speed *speed);
	int	(*get_mode1_ECC_cap)	(struct slot *slot, u8 *mode);
	int	(*get_prog_int)			(struct slot *slot, u8 *prog_int);

	int	(*query_power_fault)	(struct slot *slot);
	void	(*green_led_on)		(struct slot *slot);
	void	(*green_led_off)	(struct slot *slot);
	void	(*green_led_blink)	(struct slot *slot);
	void	(*release_ctlr)		(struct controller *ctrl);
	int (*check_cmd_status)		(struct controller *ctrl);
};

#endif				/* _SHPCHP_H */

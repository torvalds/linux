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
#include <asm/semaphore.h>
#include <asm/io.h>		
#include "pci_hotplug.h"

#if !defined(MODULE)
	#define MY_NAME	"shpchp"
#else
	#define MY_NAME	THIS_MODULE->name
#endif

extern int shpchp_poll_mode;
extern int shpchp_poll_time;
extern int shpchp_debug;

/*#define dbg(format, arg...) do { if (shpchp_debug) printk(KERN_DEBUG "%s: " format, MY_NAME , ## arg); } while (0)*/
#define dbg(format, arg...) do { if (shpchp_debug) printk("%s: " format, MY_NAME , ## arg); } while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format, MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format, MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format, MY_NAME , ## arg)

struct pci_func {
	struct pci_func *next;
	u8 bus;
	u8 device;
	u8 function;
	u8 is_a_board;
	u16 status;
	u8 configured;
	u8 switch_save;
	u8 presence_save;
	u8 pwr_save;
	u32 base_length[0x06];
	u8 base_type[0x06];
	u16 reserved2;
	u32 config_space[0x20];
	struct pci_resource *mem_head;
	struct pci_resource *p_mem_head;
	struct pci_resource *io_head;
	struct pci_resource *bus_head;
	struct pci_dev* pci_dev;
};

#define SLOT_MAGIC	0x67267321
struct slot {
	u32 magic;
	struct slot *next;
	u8 bus;
	u8 device;
	u32 number;
	u8 is_a_board;
	u8 configured;
	u8 state;
	u8 switch_save;
	u8 presence_save;
	u32 capabilities;
	u16 reserved2;
	struct timer_list task_event;
	u8 hp_slot;
	struct controller *ctrl;
	struct hpc_ops *hpc_ops;
	struct hotplug_slot *hotplug_slot;
	struct list_head	slot_list;
};

struct pci_resource {
	struct pci_resource * next;
	u32 base;
	u32 length;
};

struct event_info {
	u32 event_type;
	u8 hp_slot;
};

struct controller {
	struct controller *next;
	struct semaphore crit_sect;	/* critical section semaphore */
	void * hpc_ctlr_handle;		/* HPC controller handle */
	int num_slots;			/* Number of slots on ctlr */
	int slot_num_inc;		/* 1 or -1 */
	struct pci_resource *mem_head;
	struct pci_resource *p_mem_head;
	struct pci_resource *io_head;
	struct pci_resource *bus_head;
	struct pci_dev *pci_dev;
	struct pci_bus *pci_bus;
	struct event_info event_queue[10];
	struct slot *slot;
	struct hpc_ops *hpc_ops;
	wait_queue_head_t queue;	/* sleep & wake process */
	u8 next_event;
	u8 seg;
	u8 bus;
	u8 device;
	u8 function;
	u8 rev;
	u8 slot_device_offset;
	u8 add_support;
	enum pci_bus_speed speed;
	u32 first_slot;		/* First physical slot number */
	u8 slot_bus;		/* Bus where the slots handled by this controller sit */
	u8 push_flag;
	u16 ctlrcap;
	u16 vendor_id;
};

struct irq_mapping {
	u8 barber_pole;
	u8 valid_INT;
	u8 interrupt[4];
};

struct resource_lists {
	struct pci_resource *mem_head;
	struct pci_resource *p_mem_head;
	struct pci_resource *io_head;
	struct pci_resource *bus_head;
	struct irq_mapping *irqs;
};

/* Define AMD SHPC ID  */
#define PCI_DEVICE_ID_AMD_GOLAM_7450	0x7450 

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
#define msg_HPC_rev_error	"Unsupported revision of the PCI hot plug controller found.\n"
#define msg_HPC_non_shpc	"The PCI hot plug controller is not supported by this driver.\n"
#define msg_HPC_not_supported	"This system is not supported by this version of shpcphd mdoule. Upgrade to a newer version of shpchpd\n"
#define msg_unable_to_save	"Unable to store PCI hot plug add resource information. This system must be rebooted before adding any PCI devices.\n"
#define msg_button_on		"PCI slot #%d - powering on due to button press.\n"
#define msg_button_off		"PCI slot #%d - powering off due to button press.\n"
#define msg_button_cancel	"PCI slot #%d - action canceled due to button press.\n"
#define msg_button_ignore	"PCI slot #%d - button press ignored.  (action in progress...)\n"

/* sysfs functions for the hotplug controller info */
extern void shpchp_create_ctrl_files	(struct controller *ctrl);

/* controller functions */
extern int	shpchprm_find_available_resources(struct controller *ctrl);
extern int	shpchp_event_start_thread(void);
extern void	shpchp_event_stop_thread(void);
extern struct 	pci_func *shpchp_slot_create(unsigned char busnumber);
extern struct 	pci_func *shpchp_slot_find(unsigned char bus, unsigned char device, unsigned char index);
extern int	shpchp_enable_slot(struct slot *slot);
extern int	shpchp_disable_slot(struct slot *slot);

extern u8	shpchp_handle_attention_button(u8 hp_slot, void *inst_id);
extern u8	shpchp_handle_switch_change(u8 hp_slot, void *inst_id);
extern u8	shpchp_handle_presence_change(u8 hp_slot, void *inst_id);
extern u8	shpchp_handle_power_fault(u8 hp_slot, void *inst_id);

/* resource functions */
extern int	shpchp_resource_sort_and_combine(struct pci_resource **head);

/* pci functions */
extern int	shpchp_set_irq(u8 bus_num, u8 dev_num, u8 int_pin, u8 irq_num);
/*extern int	shpchp_get_bus_dev(struct controller *ctrl, u8 *bus_num, u8 *dev_num, struct slot *slot);*/
extern int	shpchp_save_config(struct controller *ctrl, int busnumber, int num_ctlr_slots, int first_device_num);
extern int	shpchp_save_used_resources(struct controller *ctrl, struct pci_func * func, int flag);
extern int	shpchp_save_slot_config(struct controller *ctrl, struct pci_func * new_slot);
extern void	shpchp_destroy_board_resources(struct pci_func * func);
extern int	shpchp_return_board_resources(struct pci_func * func, struct resource_lists * resources);
extern void	shpchp_destroy_resource_list(struct resource_lists * resources);
extern int	shpchp_configure_device(struct controller* ctrl, struct pci_func* func);
extern int	shpchp_unconfigure_device(struct pci_func* func);


/* Global variables */
extern struct controller *shpchp_ctrl_list;
extern struct pci_func *shpchp_slot_list[256];

/* These are added to support AMD shpc */
extern u8 shpchp_nic_irq;
extern u8 shpchp_disk_irq;

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
typedef u8(*php_intr_callback_t) (unsigned int change_id, void *instance_id);
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
	if (slot->magic != SLOT_MAGIC) {
		dbg("%s - bad magic number for slot", function);
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
	struct slot *p_slot, *tmp_slot = NULL;

	if (!ctrl)
		return NULL;

	p_slot = ctrl->slot;

	dbg("p_slot = %p\n", p_slot);

	while (p_slot && (p_slot->device != device)) {
		tmp_slot = p_slot;
		p_slot = p_slot->next;
		dbg("In while loop, p_slot = %p\n", p_slot);
	}
	if (p_slot == NULL) {
		err("ERROR: shpchp_find_slot device=0x%x\n", device);
		p_slot = tmp_slot;
	}

	return (p_slot);
}

static inline int wait_for_ctrl_irq (struct controller *ctrl)
{
    DECLARE_WAITQUEUE(wait, current);
	int retval = 0;

	dbg("%s : start\n",__FUNCTION__);

	add_wait_queue(&ctrl->queue, &wait);

	if (!shpchp_poll_mode) {
		/* Sleep for up to 1 second */
		msleep_interruptible(1000);
	} else {
		/* Sleep for up to 2 seconds */
		msleep_interruptible(2000);
	}
	remove_wait_queue(&ctrl->queue, &wait);
	if (signal_pending(current))
		retval =  -EINTR;

	dbg("%s : end\n", __FUNCTION__);
	return retval;
}

/* Puts node back in the resource list pointed to by head */
static inline void return_resource(struct pci_resource **head, struct pci_resource *node)
{
	if (!node || !head)
		return;
	node->next = *head;
	*head = node;
}

#define SLOT_NAME_SIZE 10

static inline void make_slot_name(char *buffer, int buffer_size, struct slot *slot)
{
	snprintf(buffer, buffer_size, "%04d_%04d", slot->bus, slot->number);
}

enum php_ctlr_type {
	PCI,
	ISA,
	ACPI
};

int shpc_init( struct controller *ctrl, struct pci_dev *pdev,
		php_intr_callback_t attention_button_callback,
		php_intr_callback_t switch_change_callback,
		php_intr_callback_t presence_change_callback,
		php_intr_callback_t power_fault_callback);

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
	int	(*enable_all_slots)		(struct slot *slot);
	int	(*pwr_on_all_slots)		(struct slot *slot);
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

/* SPDX-License-Identifier: GPL-2.0+ */
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
 * Send feedback to <greg@kroah.com>,<kristen.c.accardi@intel.com>
 *
 */
#ifndef _SHPCHP_H
#define _SHPCHP_H

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/delay.h>
#include <linux/sched/signal.h>	/* signal_pending(), struct timer_list */
#include <linux/mutex.h>
#include <linux/workqueue.h>

#if !defined(MODULE)
	#define MY_NAME	"shpchp"
#else
	#define MY_NAME	THIS_MODULE->name
#endif

extern bool shpchp_poll_mode;
extern int shpchp_poll_time;
extern bool shpchp_debug;

#define dbg(format, arg...)						\
do {									\
	if (shpchp_debug)						\
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
		if (shpchp_debug)					\
			pci_printk(KERN_DEBUG, ctrl->pci_dev,		\
					format, ## arg);		\
	} while (0)
#define ctrl_err(ctrl, format, arg...)					\
	pci_err(ctrl->pci_dev, format, ## arg)
#define ctrl_info(ctrl, format, arg...)					\
	pci_info(ctrl->pci_dev, format, ## arg)
#define ctrl_warn(ctrl, format, arg...)					\
	pci_warn(ctrl->pci_dev, format, ## arg)


#define SLOT_NAME_SIZE 10
struct slot {
	u8 bus;
	u8 device;
	u16 status;
	u32 number;
	u8 is_a_board;
	u8 state;
	u8 attention_save;
	u8 presence_save;
	u8 latch_save;
	u8 pwr_save;
	struct controller *ctrl;
	const struct hpc_ops *hpc_ops;
	struct hotplug_slot hotplug_slot;
	struct list_head	slot_list;
	struct delayed_work work;	/* work for button event */
	struct mutex lock;
	struct workqueue_struct *wq;
	u8 hp_slot;
};

struct event_info {
	u32 event_type;
	struct slot *p_slot;
	struct work_struct work;
};

struct controller {
	struct mutex crit_sect;		/* critical section mutex */
	struct mutex cmd_lock;		/* command lock */
	int num_slots;			/* Number of slots on ctlr */
	int slot_num_inc;		/* 1 or -1 */
	struct pci_dev *pci_dev;
	struct list_head slot_list;
	const struct hpc_ops *hpc_ops;
	wait_queue_head_t queue;	/* sleep & wake process */
	u8 slot_device_offset;
	u32 pcix_misc2_reg;	/* for amd pogo errata */
	u32 first_slot;		/* First physical slot number */
	u32 cap_offset;
	unsigned long mmio_base;
	unsigned long mmio_size;
	void __iomem *creg;
	struct timer_list poll_timer;
};

/* Define AMD SHPC ID  */
#define PCI_DEVICE_ID_AMD_POGO_7458	0x7458

/* AMD PCI-X bridge registers */
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

int __must_check shpchp_create_ctrl_files(struct controller *ctrl);
void shpchp_remove_ctrl_files(struct controller *ctrl);
int shpchp_sysfs_enable_slot(struct slot *slot);
int shpchp_sysfs_disable_slot(struct slot *slot);
u8 shpchp_handle_attention_button(u8 hp_slot, struct controller *ctrl);
u8 shpchp_handle_switch_change(u8 hp_slot, struct controller *ctrl);
u8 shpchp_handle_presence_change(u8 hp_slot, struct controller *ctrl);
u8 shpchp_handle_power_fault(u8 hp_slot, struct controller *ctrl);
int shpchp_configure_device(struct slot *p_slot);
void shpchp_unconfigure_device(struct slot *p_slot);
void cleanup_slots(struct controller *ctrl);
void shpchp_queue_pushbutton_work(struct work_struct *work);
int shpc_init(struct controller *ctrl, struct pci_dev *pdev);

static inline const char *slot_name(struct slot *slot)
{
	return hotplug_slot_name(&slot->hotplug_slot);
}

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
} __attribute__ ((packed));

/* offsets to the controller registers based on the above structure layout */
enum ctrl_offsets {
	BASE_OFFSET	 = offsetof(struct ctrl_reg, base_offset),
	SLOT_AVAIL1	 = offsetof(struct ctrl_reg, slot_avail1),
	SLOT_AVAIL2	 = offsetof(struct ctrl_reg, slot_avail2),
	SLOT_CONFIG	 = offsetof(struct ctrl_reg, slot_config),
	SEC_BUS_CONFIG	 = offsetof(struct ctrl_reg, sec_bus_config),
	MSI_CTRL	 = offsetof(struct ctrl_reg, msi_ctrl),
	PROG_INTERFACE	 = offsetof(struct ctrl_reg, prog_interface),
	CMD		 = offsetof(struct ctrl_reg, cmd),
	CMD_STATUS	 = offsetof(struct ctrl_reg, cmd_status),
	INTR_LOC	 = offsetof(struct ctrl_reg, intr_loc),
	SERR_LOC	 = offsetof(struct ctrl_reg, serr_loc),
	SERR_INTR_ENABLE = offsetof(struct ctrl_reg, serr_intr_enable),
	SLOT1		 = offsetof(struct ctrl_reg, slot1),
};

static inline struct slot *get_slot(struct hotplug_slot *hotplug_slot)
{
	return container_of(hotplug_slot, struct slot, hotplug_slot);
}

static inline struct slot *shpchp_find_slot(struct controller *ctrl, u8 device)
{
	struct slot *slot;

	list_for_each_entry(slot, &ctrl->slot_list, slot_list) {
		if (slot->device == device)
			return slot;
	}

	ctrl_err(ctrl, "Slot (device=0x%02x) not found\n", device);
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
		ctrl_dbg(p_slot->ctrl,
			 "Bridge_Errors[ PERR_OBSERVED = %08X] (W1C)\n",
			 perr_set);

		pci_write_config_dword(p_slot->ctrl->pci_dev, PCIX_MISC_BRIDGE_ERRORS_OFFSET, perr_set);
	}

	/* write-one-to-clear Memory_Base_Limit[ RSE ] */
	pci_read_config_dword(p_slot->ctrl->pci_dev, PCIX_MEM_BASE_LIMIT_OFFSET, &pcix_mem_base_reg);
	rse_set = pcix_mem_base_reg & RSE_MASK;
	if (rse_set) {
		ctrl_dbg(p_slot->ctrl, "Memory_Base_Limit[ RSE ] (W1C)\n");

		pci_write_config_dword(p_slot->ctrl->pci_dev, PCIX_MEM_BASE_LIMIT_OFFSET, rse_set);
	}
	/* restore MiscII register */
	pci_read_config_dword(p_slot->ctrl->pci_dev, PCIX_MISCII_OFFSET, &pcix_misc2_temp);

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

struct hpc_ops {
	int (*power_on_slot)(struct slot *slot);
	int (*slot_enable)(struct slot *slot);
	int (*slot_disable)(struct slot *slot);
	int (*set_bus_speed_mode)(struct slot *slot, enum pci_bus_speed speed);
	int (*get_power_status)(struct slot *slot, u8 *status);
	int (*get_attention_status)(struct slot *slot, u8 *status);
	int (*set_attention_status)(struct slot *slot, u8 status);
	int (*get_latch_status)(struct slot *slot, u8 *status);
	int (*get_adapter_status)(struct slot *slot, u8 *status);
	int (*get_adapter_speed)(struct slot *slot, enum pci_bus_speed *speed);
	int (*get_prog_int)(struct slot *slot, u8 *prog_int);
	int (*query_power_fault)(struct slot *slot);
	void (*green_led_on)(struct slot *slot);
	void (*green_led_off)(struct slot *slot);
	void (*green_led_blink)(struct slot *slot);
	void (*release_ctlr)(struct controller *ctrl);
	int (*check_cmd_status)(struct controller *ctrl);
};

#endif				/* _SHPCHP_H */

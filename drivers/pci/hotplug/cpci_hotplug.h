/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * CompactPCI Hot Plug Core Functions
 *
 * Copyright (C) 2002 SOMA Networks, Inc.
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 *
 * All rights reserved.
 *
 * Send feedback to <scottm@somanetworks.com>
 */

#ifndef _CPCI_HOTPLUG_H
#define _CPCI_HOTPLUG_H

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>

/* PICMG 2.1 R2.0 HS CSR bits: */
#define HS_CSR_INS	0x0080
#define HS_CSR_EXT	0x0040
#define HS_CSR_PI	0x0030
#define HS_CSR_LOO	0x0008
#define HS_CSR_PIE	0x0004
#define HS_CSR_EIM	0x0002
#define HS_CSR_DHA	0x0001

struct slot {
	u8 number;
	unsigned int devfn;
	struct pci_bus *bus;
	struct pci_dev *dev;
	unsigned int extracting;
	struct hotplug_slot *hotplug_slot;
	struct list_head slot_list;
};

struct cpci_hp_controller_ops {
	int (*query_enum)(void);
	int (*enable_irq)(void);
	int (*disable_irq)(void);
	int (*check_irq)(void *dev_id);
	int (*hardware_test)(struct slot *slot, u32 value);
	u8  (*get_power)(struct slot *slot);
	int (*set_power)(struct slot *slot, int value);
};

struct cpci_hp_controller {
	unsigned int irq;
	unsigned long irq_flags;
	char *devname;
	void *dev_id;
	char *name;
	struct cpci_hp_controller_ops *ops;
};

static inline const char *slot_name(struct slot *slot)
{
	return hotplug_slot_name(slot->hotplug_slot);
}

int cpci_hp_register_controller(struct cpci_hp_controller *controller);
int cpci_hp_unregister_controller(struct cpci_hp_controller *controller);
int cpci_hp_register_bus(struct pci_bus *bus, u8 first, u8 last);
int cpci_hp_unregister_bus(struct pci_bus *bus);
int cpci_hp_start(void);
int cpci_hp_stop(void);

/*
 * Internal function prototypes, these functions should not be used by
 * board/chassis drivers.
 */
u8 cpci_get_attention_status(struct slot *slot);
u8 cpci_get_latch_status(struct slot *slot);
u8 cpci_get_adapter_status(struct slot *slot);
u16 cpci_get_hs_csr(struct slot *slot);
int cpci_set_attention_status(struct slot *slot, int status);
int cpci_check_and_clear_ins(struct slot *slot);
int cpci_check_ext(struct slot *slot);
int cpci_clear_ext(struct slot *slot);
int cpci_led_on(struct slot *slot);
int cpci_led_off(struct slot *slot);
int cpci_configure_slot(struct slot *slot);
int cpci_unconfigure_slot(struct slot *slot);

#ifdef CONFIG_HOTPLUG_PCI_CPCI
int cpci_hotplug_init(int debug);
#else
static inline int cpci_hotplug_init(int debug) { return 0; }
#endif

#endif	/* _CPCI_HOTPLUG_H */

/*
 * ACPI PCI Hot Plug Controller Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 * Copyright (C) 2002 Hiroshi Aono (h-aono@ap.jp.nec.com)
 * Copyright (C) 2002,2003 Takayoshi Kochi (t-kochi@bq.jp.nec.com)
 * Copyright (C) 2002,2003 NEC Corporation
 * Copyright (C) 2003-2005 Matthew Wilcox (matthew.wilcox@hp.com)
 * Copyright (C) 2003-2005 Hewlett Packard
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
 * Send feedback to <gregkh@us.ibm.com>,
 *		    <t-kochi@bq.jp.nec.com>
 *
 */

#ifndef _ACPIPHP_H
#define _ACPIPHP_H

#include <linux/acpi.h>
#include <linux/kobject.h>	/* for KOBJ_NAME_LEN */
#include <linux/mutex.h>
#include "pci_hotplug.h"

#define dbg(format, arg...)					\
	do {							\
		if (acpiphp_debug)				\
			printk(KERN_DEBUG "%s: " format,	\
				MY_NAME , ## arg); 		\
	} while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format, MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format, MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format, MY_NAME , ## arg)

/* name size which is used for entries in pcihpfs */
#define SLOT_NAME_SIZE	KOBJ_NAME_LEN		/* {_SUN} */

struct acpiphp_bridge;
struct acpiphp_slot;

/*
 * struct slot - slot information for each *physical* slot
 */
struct slot {
	struct hotplug_slot	*hotplug_slot;
	struct acpiphp_slot	*acpi_slot;
};



/**
 * struct acpiphp_bridge - PCI bridge information
 *
 * for each bridge device in ACPI namespace
 */
struct acpiphp_bridge {
	struct list_head list;
	acpi_handle handle;
	struct acpiphp_slot *slots;

	/* Ejectable PCI-to-PCI bridge (PCI bridge and PCI function) */
	struct acpiphp_func *func;

	int type;
	int nr_slots;

	u32 flags;

	/* This bus (host bridge) or Secondary bus (PCI-to-PCI bridge) */
	struct pci_bus *pci_bus;

	/* PCI-to-PCI bridge device */
	struct pci_dev *pci_dev;

	/* ACPI 2.0 _HPP parameters */
	struct hotplug_params hpp;

	spinlock_t res_lock;
};


/**
 * struct acpiphp_slot - PCI slot information
 *
 * PCI slot information for each *physical* PCI slot
 */
struct acpiphp_slot {
	struct acpiphp_slot *next;
	struct acpiphp_bridge *bridge;	/* parent */
	struct list_head funcs;		/* one slot may have different
					   objects (i.e. for each function) */
	struct slot *slot;
	struct mutex crit_sect;

	u8		device;		/* pci device# */

	u32		sun;		/* ACPI _SUN (slot unique number) */
	u32		slotno;		/* slot number relative to bridge */
	u32		flags;		/* see below */
};


/**
 * struct acpiphp_func - PCI function information
 *
 * PCI function information for each object in ACPI namespace
 * typically 8 objects per slot (i.e. for each PCI function)
 */
struct acpiphp_func {
	struct acpiphp_slot *slot;	/* parent */
	struct acpiphp_bridge *bridge;	/* Ejectable PCI-to-PCI bridge */

	struct list_head sibling;
	struct pci_dev *pci_dev;

	acpi_handle	handle;

	u8		function;	/* pci function# */
	u32		flags;		/* see below */
};

/**
 * struct acpiphp_attention_info - device specific attention registration
 *
 * ACPI has no generic method of setting/getting attention status
 * this allows for device specific driver registration
 */
struct acpiphp_attention_info
{
	int (*set_attn)(struct hotplug_slot *slot, u8 status);
	int (*get_attn)(struct hotplug_slot *slot, u8 *status);
	struct module *owner;
};


struct dependent_device {
	struct list_head device_list;
	struct list_head pci_list;
	acpi_handle handle;
	struct acpiphp_func *func;
};


struct acpiphp_dock_station {
	acpi_handle handle;
	u32 last_dock_time;
	u32 flags;
	struct acpiphp_func *dock_bridge;
	struct list_head dependent_devices;
	struct list_head pci_dependent_devices;
};


/* PCI bus bridge HID */
#define ACPI_PCI_HOST_HID		"PNP0A03"

/* PCI BRIDGE type */
#define BRIDGE_TYPE_HOST		0
#define BRIDGE_TYPE_P2P			1

/* ACPI _STA method value (ignore bit 4; battery present) */
#define ACPI_STA_PRESENT		(0x00000001)
#define ACPI_STA_ENABLED		(0x00000002)
#define ACPI_STA_SHOW_IN_UI		(0x00000004)
#define ACPI_STA_FUNCTIONING		(0x00000008)
#define ACPI_STA_ALL			(0x0000000f)

/* bridge flags */
#define BRIDGE_HAS_STA		(0x00000001)
#define BRIDGE_HAS_EJ0		(0x00000002)
#define BRIDGE_HAS_HPP		(0x00000004)
#define BRIDGE_HAS_PS0		(0x00000010)
#define BRIDGE_HAS_PS1		(0x00000020)
#define BRIDGE_HAS_PS2		(0x00000040)
#define BRIDGE_HAS_PS3		(0x00000080)

/* slot flags */

#define SLOT_POWEREDON		(0x00000001)
#define SLOT_ENABLED		(0x00000002)
#define SLOT_MULTIFUNCTION	(0x00000004)

/* function flags */

#define FUNC_HAS_STA		(0x00000001)
#define FUNC_HAS_EJ0		(0x00000002)
#define FUNC_HAS_PS0		(0x00000010)
#define FUNC_HAS_PS1		(0x00000020)
#define FUNC_HAS_PS2		(0x00000040)
#define FUNC_HAS_PS3		(0x00000080)
#define FUNC_HAS_DCK            (0x00000100)
#define FUNC_IS_DD              (0x00000200)

/* dock station flags */
#define DOCK_DOCKING            (0x00000001)
#define DOCK_HAS_BRIDGE         (0x00000002)

/* function prototypes */

/* acpiphp_core.c */
extern int acpiphp_register_attention(struct acpiphp_attention_info*info);
extern int acpiphp_unregister_attention(struct acpiphp_attention_info *info);
extern int acpiphp_register_hotplug_slot(struct acpiphp_slot *slot);
extern void acpiphp_unregister_hotplug_slot(struct acpiphp_slot *slot);

/* acpiphp_glue.c */
extern int acpiphp_glue_init (void);
extern void acpiphp_glue_exit (void);
extern int acpiphp_get_num_slots (void);
typedef int (*acpiphp_callback)(struct acpiphp_slot *slot, void *data);
void handle_hotplug_event_func(acpi_handle, u32, void*);

extern int acpiphp_enable_slot (struct acpiphp_slot *slot);
extern int acpiphp_disable_slot (struct acpiphp_slot *slot);
extern u8 acpiphp_get_power_status (struct acpiphp_slot *slot);
extern u8 acpiphp_get_attention_status (struct acpiphp_slot *slot);
extern u8 acpiphp_get_latch_status (struct acpiphp_slot *slot);
extern u8 acpiphp_get_adapter_status (struct acpiphp_slot *slot);
extern u32 acpiphp_get_address (struct acpiphp_slot *slot);

/* acpiphp_dock.c */
extern int find_dock_station(void);
extern void remove_dock_station(void);
extern void add_dependent_device(struct dependent_device *new_dd);
extern void add_pci_dependent_device(struct dependent_device *new_dd);
extern struct dependent_device *get_dependent_device(acpi_handle handle);
extern int is_dependent_device(acpi_handle handle);
extern int detect_dependent_devices(acpi_handle *bridge_handle);
extern struct dependent_device *alloc_dependent_device(acpi_handle handle);

/* variables */
extern int acpiphp_debug;

#endif /* _ACPIPHP_H */

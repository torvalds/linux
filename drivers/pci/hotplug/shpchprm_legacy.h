/*
 * SHPCHPRM Legacy: PHP Resource Manager for Non-ACPI/Legacy platform using HRT
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

#ifndef _SHPCHPRM_LEGACY_H_
#define _SHPCHPRM_LEGACY_H_

#define ROM_PHY_ADDR	0x0F0000
#define ROM_PHY_LEN	0x00FFFF

struct slot_rt {
	u8 dev_func;
	u8 primary_bus;
	u8 secondary_bus;
	u8 max_bus;
	u16 io_base;
	u16 io_length;
	u16 mem_base;
	u16 mem_length;
	u16 pre_mem_base;
	u16 pre_mem_length;
} __attribute__ ((packed));

/* offsets to the hotplug slot resource table registers based on the above structure layout */
enum slot_rt_offsets {
	DEV_FUNC = offsetof(struct slot_rt, dev_func),
	PRIMARY_BUS = offsetof(struct slot_rt, primary_bus),
	SECONDARY_BUS = offsetof(struct slot_rt, secondary_bus),
	MAX_BUS = offsetof(struct slot_rt, max_bus),
	IO_BASE = offsetof(struct slot_rt, io_base),
	IO_LENGTH = offsetof(struct slot_rt, io_length),
	MEM_BASE = offsetof(struct slot_rt, mem_base),
	MEM_LENGTH = offsetof(struct slot_rt, mem_length),
	PRE_MEM_BASE = offsetof(struct slot_rt, pre_mem_base),
	PRE_MEM_LENGTH = offsetof(struct slot_rt, pre_mem_length),
};

struct hrt {
	char sig0;
	char sig1;
	char sig2;
	char sig3;
	u16 unused_IRQ;
	u16 PCIIRQ;
	u8 number_of_entries;
	u8 revision;
	u16 reserved1;
	u32 reserved2;
} __attribute__ ((packed));

/* offsets to the hotplug resource table registers based on the above structure layout */
enum hrt_offsets {
	SIG0 = offsetof(struct hrt, sig0),
	SIG1 = offsetof(struct hrt, sig1),
	SIG2 = offsetof(struct hrt, sig2),
	SIG3 = offsetof(struct hrt, sig3),
	UNUSED_IRQ = offsetof(struct hrt, unused_IRQ),
	PCIIRQ = offsetof(struct hrt, PCIIRQ),
	NUMBER_OF_ENTRIES = offsetof(struct hrt, number_of_entries),
	REVISION = offsetof(struct hrt, revision),
	HRT_RESERVED1 = offsetof(struct hrt, reserved1),
	HRT_RESERVED2 = offsetof(struct hrt, reserved2),
};

struct irq_info {
	u8 bus, devfn;		/* bus, device and function */
	struct {
		u8 link;	/* IRQ line ID, chipset dependent, 0=not routed */
		u16 bitmap;	/* Available IRQs */
	} __attribute__ ((packed)) irq[4];
	u8 slot;		/* slot number, 0=onboard */
	u8 rfu;
} __attribute__ ((packed));

struct irq_routing_table {
	u32 signature;		/* PIRQ_SIGNATURE should be here */
	u16 version;		/* PIRQ_VERSION */
	u16 size;			/* Table size in bytes */
	u8 rtr_bus, rtr_devfn;	/* Where the interrupt router lies */
	u16 exclusive_irqs;	/* IRQs devoted exclusively to PCI usage */
	u16 rtr_vendor, rtr_device;	/* Vendor and device ID of interrupt router */
	u32 miniport_data;	/* Crap */
	u8 rfu[11];
	u8 checksum;		/* Modulo 256 checksum must give zero */
	struct irq_info slots[0];
} __attribute__ ((packed));

#endif				/* _SHPCHPRM_LEGACY_H_ */

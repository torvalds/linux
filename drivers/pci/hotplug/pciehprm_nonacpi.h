/*
 * PCIEHPRM NONACPI: PHP Resource Manager for Non-ACPI/Legacy platform
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
 * Send feedback to <greg@kroah.com>, <dely.l.sy@intel.com>
 *
 */

#ifndef _PCIEHPRM_NONACPI_H_
#define _PCIEHPRM_NONACPI_H_

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

#endif				/* _PCIEHPRM_NONACPI_H_ */

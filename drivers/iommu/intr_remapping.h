/*
 * Copyright (C) 2012 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * This header file contains stuff that is shared between different interrupt
 * remapping drivers but with no need to be visible outside of the IOMMU layer.
 */

#ifndef __INTR_REMAPPING_H
#define __INTR_REMAPPING_H

#ifdef CONFIG_IRQ_REMAP

extern int disable_intremap;
extern int disable_sourceid_checking;
extern int no_x2apic_optout;

struct irq_remap_ops {
	/* Check whether Interrupt Remapping is supported */
	int (*supported)(void);

	/* Initializes hardware and makes it ready for remapping interrupts */
	int  (*hardware_init)(void);

	/* Enables the remapping hardware */
	int  (*hardware_enable)(void);
};

extern struct irq_remap_ops intel_irq_remap_ops;

#endif /* CONFIG_IRQ_REMAP */

#endif /* __INTR_REMAPPING_H */

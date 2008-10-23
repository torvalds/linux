/******************************************************************************
 * arch/ia64/include/asm/xen/irq.h
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _ASM_IA64_XEN_IRQ_H
#define _ASM_IA64_XEN_IRQ_H

/*
 * The flat IRQ space is divided into two regions:
 *  1. A one-to-one mapping of real physical IRQs. This space is only used
 *     if we have physical device-access privilege. This region is at the
 *     start of the IRQ space so that existing device drivers do not need
 *     to be modified to translate physical IRQ numbers into our IRQ space.
 *  3. A dynamic mapping of inter-domain and Xen-sourced virtual IRQs. These
 *     are bound using the provided bind/unbind functions.
 */

#define XEN_PIRQ_BASE		0
#define XEN_NR_PIRQS		256

#define XEN_DYNIRQ_BASE		(XEN_PIRQ_BASE + XEN_NR_PIRQS)
#define XEN_NR_DYNIRQS		(NR_CPUS * 8)

#define XEN_NR_IRQS		(XEN_NR_PIRQS + XEN_NR_DYNIRQS)

#endif /* _ASM_IA64_XEN_IRQ_H */

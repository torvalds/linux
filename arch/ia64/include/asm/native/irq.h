/******************************************************************************
 * arch/ia64/include/asm/native/irq.h
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
 */

#ifndef _ASM_IA64_NATIVE_IRQ_H
#define _ASM_IA64_NATIVE_IRQ_H

#define NR_VECTORS	256

#if (NR_VECTORS + 32 * NR_CPUS) < 1024
#define IA64_NATIVE_NR_IRQS (NR_VECTORS + 32 * NR_CPUS)
#else
#define IA64_NATIVE_NR_IRQS 1024
#endif

#endif /* _ASM_IA64_NATIVE_IRQ_H */

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
 * This header file contains the interface of the interrupt remapping code to
 * the x86 interrupt management code.
 */

#ifndef __X86_INTR_REMAPPING_H
#define __X86_INTR_REMAPPING_H

#ifdef CONFIG_IRQ_REMAP

extern int intr_remapping_enabled;

extern void setup_intr_remapping(void);
extern int intr_remapping_supported(void);
extern int intr_hardware_init(void);
extern int intr_hardware_enable(void);

#else  /* CONFIG_IRQ_REMAP */

#define intr_remapping_enabled	0

static inline void setup_intr_remapping(void) { }
static inline int intr_remapping_supported(void) { return 0; }
static inline int intr_hardware_init(void) { return -ENODEV; }
static inline int intr_hardware_enable(void) { return -ENODEV; }

#endif /* CONFIG_IRQ_REMAP */

#endif /* __X86_INTR_REMAPPING_H */

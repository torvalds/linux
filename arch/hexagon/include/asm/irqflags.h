/*
 * IRQ support for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#include <asm/hexagon_vm.h>
#include <linux/types.h>

static inline unsigned long arch_local_save_flags(void)
{
	return __vmgetie();
}

static inline unsigned long arch_local_irq_save(void)
{
	return __vmsetie(VM_INT_DISABLE);
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return !flags;
}

static inline bool arch_irqs_disabled(void)
{
	return !__vmgetie();
}

static inline void arch_local_irq_enable(void)
{
	__vmsetie(VM_INT_ENABLE);
}

static inline void arch_local_irq_disable(void)
{
	__vmsetie(VM_INT_DISABLE);
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	__vmsetie(flags);
}

#endif

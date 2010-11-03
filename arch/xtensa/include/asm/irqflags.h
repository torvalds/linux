/*
 * Xtensa IRQ flags handling functions
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_IRQFLAGS_H
#define _XTENSA_IRQFLAGS_H

#include <linux/types.h>

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;
	asm volatile("rsr %0,"__stringify(PS) : "=a" (flags));
	return flags;
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;
	asm volatile("rsil %0, "__stringify(LOCKLEVEL)
		     : "=a" (flags) :: "memory");
	return flags;
}

static inline void arch_local_irq_disable(void)
{
	arch_local_irq_save();
}

static inline void arch_local_irq_enable(void)
{
	unsigned long flags;
	asm volatile("rsil %0, 0" : "=a" (flags) :: "memory");
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	asm volatile("wsr %0, "__stringify(PS)" ; rsync"
		     :: "a" (flags) : "memory");
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & 0xf) != 0;
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#endif /* _XTENSA_IRQFLAGS_H */

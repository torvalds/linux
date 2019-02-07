/*
 * Xtensa IRQ flags handling functions
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 * Copyright (C) 2015 Cadence Design Systems Inc.
 */

#ifndef _XTENSA_IRQFLAGS_H
#define _XTENSA_IRQFLAGS_H

#include <linux/stringify.h>
#include <linux/types.h>
#include <asm/processor.h>

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;
	asm volatile("rsr %0, ps" : "=a" (flags));
	return flags;
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;
#if XTENSA_FAKE_NMI
#if defined(CONFIG_DEBUG_KERNEL) && (LOCKLEVEL | TOPLEVEL) >= XCHAL_DEBUGLEVEL
	unsigned long tmp;

	asm volatile("rsr	%0, ps\t\n"
		     "extui	%1, %0, 0, 4\t\n"
		     "bgei	%1, "__stringify(LOCKLEVEL)", 1f\t\n"
		     "rsil	%0, "__stringify(LOCKLEVEL)"\n"
		     "1:"
		     : "=a" (flags), "=a" (tmp) :: "memory");
#else
	asm volatile("rsr	%0, ps\t\n"
		     "or	%0, %0, %1\t\n"
		     "xsr	%0, ps\t\n"
		     "rsync"
		     : "=&a" (flags) : "a" (LOCKLEVEL) : "memory");
#endif
#else
	asm volatile("rsil	%0, "__stringify(LOCKLEVEL)
		     : "=a" (flags) :: "memory");
#endif
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
	asm volatile("wsr %0, ps; rsync"
		     :: "a" (flags) : "memory");
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
#if XCHAL_EXCM_LEVEL < LOCKLEVEL || (1 << PS_EXCM_BIT) < LOCKLEVEL
#error "XCHAL_EXCM_LEVEL and 1<<PS_EXCM_BIT must be no less than LOCKLEVEL"
#endif
	return (flags & (PS_INTLEVEL_MASK | (1 << PS_EXCM_BIT))) >= LOCKLEVEL;
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#endif /* _XTENSA_IRQFLAGS_H */

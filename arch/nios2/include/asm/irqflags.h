/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2010 Thomas Chou <thomas@wytron.com.tw>
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#include <asm/registers.h>

static inline unsigned long arch_local_save_flags(void)
{
	return RDCTL(CTL_FSTATUS);
}

/*
 * This will restore ALL status register flags, not only the interrupt
 * mask flag.
 */
static inline void arch_local_irq_restore(unsigned long flags)
{
	WRCTL(CTL_FSTATUS, flags);
}

static inline void arch_local_irq_disable(void)
{
	unsigned long flags;

	flags = arch_local_save_flags();
	arch_local_irq_restore(flags & ~STATUS_PIE);
}

static inline void arch_local_irq_enable(void)
{
	unsigned long flags;

	flags = arch_local_save_flags();
	arch_local_irq_restore(flags | STATUS_PIE);
}

static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & STATUS_PIE) == 0;
}

static inline int arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;

	flags = arch_local_save_flags();
	arch_local_irq_restore(flags & ~STATUS_PIE);
	return flags;
}

#endif /* _ASM_IRQFLAGS_H */

/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_IRQFLAGS_H
#define __ASM_AVR32_IRQFLAGS_H

#include <linux/types.h>
#include <asm/sysreg.h>

static inline unsigned long arch_local_save_flags(void)
{
	return sysreg_read(SR);
}

/*
 * This will restore ALL status register flags, not only the interrupt
 * mask flag.
 *
 * The empty asm statement informs the compiler of this fact while
 * also serving as a barrier.
 */
static inline void arch_local_irq_restore(unsigned long flags)
{
	sysreg_write(SR, flags);
	asm volatile("" : : : "memory", "cc");
}

static inline void arch_local_irq_disable(void)
{
	asm volatile("ssrf %0" : : "n"(SYSREG_GM_OFFSET) : "memory");
}

static inline void arch_local_irq_enable(void)
{
	asm volatile("csrf %0" : : "n"(SYSREG_GM_OFFSET) : "memory");
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & SYSREG_BIT(GM)) != 0;
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();

	arch_local_irq_disable();

	return flags;
}

#endif /* __ASM_AVR32_IRQFLAGS_H */

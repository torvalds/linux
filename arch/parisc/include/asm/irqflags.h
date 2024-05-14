/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PARISC_IRQFLAGS_H
#define __PARISC_IRQFLAGS_H

#include <linux/types.h>
#include <asm/psw.h>

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;
	asm volatile("ssm 0, %0" : "=r" (flags) : : "memory");
	return flags;
}

static inline void arch_local_irq_disable(void)
{
	asm volatile("rsm %0,%%r0\n" : : "i" (PSW_I) : "memory");
}

static inline void arch_local_irq_enable(void)
{
	asm volatile("ssm %0,%%r0\n" : : "i" (PSW_I) : "memory");
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;
	asm volatile("rsm %1,%0" : "=r" (flags) : "i" (PSW_I) : "memory");
	return flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	asm volatile("mtsm %0" : : "r" (flags) : "memory");
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & PSW_I) == 0;
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#endif /* __PARISC_IRQFLAGS_H */

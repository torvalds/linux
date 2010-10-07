#ifndef __ASM_CRIS_ARCH_IRQFLAGS_H
#define __ASM_CRIS_ARCH_IRQFLAGS_H

#include <linux/types.h>

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;
	asm volatile("move $ccr,%0" : "=rm" (flags) : : "memory");
	return flags;
}

static inline void arch_local_irq_disable(void)
{
	asm volatile("di" : : : "memory");
}

static inline void arch_local_irq_enable(void)
{
	asm volatile("ei" : : : "memory");
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();
	arch_local_irq_disable();
	return flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	asm volatile("move %0,$ccr" : : "rm" (flags) : "memory");
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & (1 << 5));
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#endif /* __ASM_CRIS_ARCH_IRQFLAGS_H */

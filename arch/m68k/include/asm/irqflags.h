#ifndef _M68K_IRQFLAGS_H
#define _M68K_IRQFLAGS_H

#include <linux/types.h>
#include <linux/hardirq.h>
#include <linux/preempt.h>
#include <asm/thread_info.h>
#include <asm/entry.h>

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;
	asm volatile ("movew %%sr,%0" : "=d" (flags) : : "memory");
	return flags;
}

static inline void arch_local_irq_disable(void)
{
#ifdef CONFIG_COLDFIRE
	asm volatile (
		"move	%/sr,%%d0	\n\t"
		"ori.l	#0x0700,%%d0	\n\t"
		"move	%%d0,%/sr	\n"
		: /* no outputs */
		:
		: "cc", "%d0", "memory");
#else
	asm volatile ("oriw  #0x0700,%%sr" : : : "memory");
#endif
}

static inline void arch_local_irq_enable(void)
{
#if defined(CONFIG_COLDFIRE)
	asm volatile (
		"move	%/sr,%%d0	\n\t"
		"andi.l	#0xf8ff,%%d0	\n\t"
		"move	%%d0,%/sr	\n"
		: /* no outputs */
		:
		: "cc", "%d0", "memory");
#else
# if defined(CONFIG_MMU)
	if (MACH_IS_Q40 || !hardirq_count())
# endif
		asm volatile (
			"andiw %0,%%sr"
			:
			: "i" (ALLOWINT)
			: "memory");
#endif
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();
	arch_local_irq_disable();
	return flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	asm volatile ("movew %0,%%sr" : : "d" (flags) : "memory");
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & ~ALLOWINT) != 0;
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#endif /* _M68K_IRQFLAGS_H */

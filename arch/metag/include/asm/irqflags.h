/*
 * IRQ flags handling
 *
 * This file gets included from lowlevel asm headers too, to provide
 * wrapped versions of the local_irq_*() APIs, based on the
 * raw_local_irq_*() functions from the lowlevel headers.
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#ifndef __ASSEMBLY__

#include <asm/core_reg.h>
#include <asm/metag_regs.h>

#define INTS_OFF_MASK TXSTATI_BGNDHALT_BIT

#ifdef CONFIG_SMP
extern unsigned int get_trigger_mask(void);
#else

extern unsigned int global_trigger_mask;

static inline unsigned int get_trigger_mask(void)
{
	return global_trigger_mask;
}
#endif

static inline unsigned long arch_local_save_flags(void)
{
	return __core_reg_get(TXMASKI);
}

static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & ~INTS_OFF_MASK) == 0;
}

static inline int arch_irqs_disabled(void)
{
	unsigned long flags = arch_local_save_flags();

	return arch_irqs_disabled_flags(flags);
}

static inline unsigned long __irqs_disabled(void)
{
	/*
	 * We shouldn't enable exceptions if they are not already
	 * enabled. This is required for chancalls to work correctly.
	 */
	return arch_local_save_flags() & INTS_OFF_MASK;
}

/*
 * For spinlocks, etc:
 */
static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags = __irqs_disabled();

	asm volatile("SWAP %0,TXMASKI\n" : "=r" (flags) : "0" (flags)
		     : "memory");

	return flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	asm volatile("MOV TXMASKI,%0\n" : : "r" (flags) : "memory");
}

static inline void arch_local_irq_disable(void)
{
	unsigned long flags = __irqs_disabled();

	asm volatile("MOV TXMASKI,%0\n" : : "r" (flags) : "memory");
}

static inline void arch_local_irq_enable(void)
{
#ifdef CONFIG_SMP
	preempt_disable();
	arch_local_irq_restore(get_trigger_mask());
	preempt_enable_no_resched();
#else
	arch_local_irq_restore(get_trigger_mask());
#endif
}

#endif /* (__ASSEMBLY__) */

#endif /* !(_ASM_IRQFLAGS_H) */

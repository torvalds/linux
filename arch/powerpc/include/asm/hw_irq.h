/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifndef _ASM_POWERPC_HW_IRQ_H
#define _ASM_POWERPC_HW_IRQ_H

#ifdef __KERNEL__

#include <linux/errno.h>
#include <linux/compiler.h>
#include <asm/ptrace.h>
#include <asm/processor.h>

#ifdef CONFIG_PPC64

/*
 * PACA flags in paca->irq_happened.
 *
 * This bits are set when interrupts occur while soft-disabled
 * and allow a proper replay. Additionally, PACA_IRQ_HARD_DIS
 * is set whenever we manually hard disable.
 */
#define PACA_IRQ_HARD_DIS	0x01
#define PACA_IRQ_DBELL		0x02
#define PACA_IRQ_EE		0x04
#define PACA_IRQ_DEC		0x08 /* Or FIT */
#define PACA_IRQ_EE_EDGE	0x10 /* BookE only */
#define PACA_IRQ_HMI		0x20
#define PACA_IRQ_PMI		0x40

/*
 * Some soft-masked interrupts must be hard masked until they are replayed
 * (e.g., because the soft-masked handler does not clear the exception).
 */
#ifdef CONFIG_PPC_BOOK3S
#define PACA_IRQ_MUST_HARD_MASK	(PACA_IRQ_EE|PACA_IRQ_PMI)
#else
#define PACA_IRQ_MUST_HARD_MASK	(PACA_IRQ_EE)
#endif

/*
 * flags for paca->irq_soft_mask
 */
#define IRQS_ENABLED		0
#define IRQS_DISABLED		1 /* local_irq_disable() interrupts */
#define IRQS_PMI_DISABLED	2
#define IRQS_ALL_DISABLED	(IRQS_DISABLED | IRQS_PMI_DISABLED)

#endif /* CONFIG_PPC64 */

#ifndef __ASSEMBLY__

extern void replay_system_reset(void);
extern void replay_soft_interrupts(void);

extern void timer_interrupt(struct pt_regs *);
extern void timer_broadcast_interrupt(void);
extern void performance_monitor_exception(struct pt_regs *regs);
extern void WatchdogException(struct pt_regs *regs);
extern void unknown_exception(struct pt_regs *regs);

#ifdef CONFIG_PPC64
#include <asm/paca.h>

static inline notrace unsigned long irq_soft_mask_return(void)
{
	unsigned long flags;

	asm volatile(
		"lbz %0,%1(13)"
		: "=r" (flags)
		: "i" (offsetof(struct paca_struct, irq_soft_mask)));

	return flags;
}

/*
 * The "memory" clobber acts as both a compiler barrier
 * for the critical section and as a clobber because
 * we changed paca->irq_soft_mask
 */
static inline notrace void irq_soft_mask_set(unsigned long mask)
{
#ifdef CONFIG_PPC_IRQ_SOFT_MASK_DEBUG
	/*
	 * The irq mask must always include the STD bit if any are set.
	 *
	 * and interrupts don't get replayed until the standard
	 * interrupt (local_irq_disable()) is unmasked.
	 *
	 * Other masks must only provide additional masking beyond
	 * the standard, and they are also not replayed until the
	 * standard interrupt becomes unmasked.
	 *
	 * This could be changed, but it will require partial
	 * unmasks to be replayed, among other things. For now, take
	 * the simple approach.
	 */
	WARN_ON(mask && !(mask & IRQS_DISABLED));
#endif

	asm volatile(
		"stb %0,%1(13)"
		:
		: "r" (mask),
		  "i" (offsetof(struct paca_struct, irq_soft_mask))
		: "memory");
}

static inline notrace unsigned long irq_soft_mask_set_return(unsigned long mask)
{
	unsigned long flags;

#ifdef CONFIG_PPC_IRQ_SOFT_MASK_DEBUG
	WARN_ON(mask && !(mask & IRQS_DISABLED));
#endif

	asm volatile(
		"lbz %0,%1(13); stb %2,%1(13)"
		: "=&r" (flags)
		: "i" (offsetof(struct paca_struct, irq_soft_mask)),
		  "r" (mask)
		: "memory");

	return flags;
}

static inline notrace unsigned long irq_soft_mask_or_return(unsigned long mask)
{
	unsigned long flags, tmp;

	asm volatile(
		"lbz %0,%2(13); or %1,%0,%3; stb %1,%2(13)"
		: "=&r" (flags), "=r" (tmp)
		: "i" (offsetof(struct paca_struct, irq_soft_mask)),
		  "r" (mask)
		: "memory");

#ifdef CONFIG_PPC_IRQ_SOFT_MASK_DEBUG
	WARN_ON((mask | flags) && !((mask | flags) & IRQS_DISABLED));
#endif

	return flags;
}

static inline unsigned long arch_local_save_flags(void)
{
	return irq_soft_mask_return();
}

static inline void arch_local_irq_disable(void)
{
	irq_soft_mask_set(IRQS_DISABLED);
}

extern void arch_local_irq_restore(unsigned long);

static inline void arch_local_irq_enable(void)
{
	arch_local_irq_restore(IRQS_ENABLED);
}

static inline unsigned long arch_local_irq_save(void)
{
	return irq_soft_mask_set_return(IRQS_DISABLED);
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return flags & IRQS_DISABLED;
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#ifdef CONFIG_PPC_BOOK3S
/*
 * To support disabling and enabling of irq with PMI, set of
 * new powerpc_local_irq_pmu_save() and powerpc_local_irq_restore()
 * functions are added. These macros are implemented using generic
 * linux local_irq_* code from include/linux/irqflags.h.
 */
#define raw_local_irq_pmu_save(flags)					\
	do {								\
		typecheck(unsigned long, flags);			\
		flags = irq_soft_mask_or_return(IRQS_DISABLED |	\
				IRQS_PMI_DISABLED);			\
	} while(0)

#define raw_local_irq_pmu_restore(flags)				\
	do {								\
		typecheck(unsigned long, flags);			\
		arch_local_irq_restore(flags);				\
	} while(0)

#ifdef CONFIG_TRACE_IRQFLAGS
#define powerpc_local_irq_pmu_save(flags)			\
	 do {							\
		raw_local_irq_pmu_save(flags);			\
		trace_hardirqs_off();				\
	} while(0)
#define powerpc_local_irq_pmu_restore(flags)			\
	do {							\
		if (raw_irqs_disabled_flags(flags)) {		\
			raw_local_irq_pmu_restore(flags);	\
			trace_hardirqs_off();			\
		} else {					\
			trace_hardirqs_on();			\
			raw_local_irq_pmu_restore(flags);	\
		}						\
	} while(0)
#else
#define powerpc_local_irq_pmu_save(flags)			\
	do {							\
		raw_local_irq_pmu_save(flags);			\
	} while(0)
#define powerpc_local_irq_pmu_restore(flags)			\
	do {							\
		raw_local_irq_pmu_restore(flags);		\
	} while (0)
#endif  /* CONFIG_TRACE_IRQFLAGS */

#endif /* CONFIG_PPC_BOOK3S */

#ifdef CONFIG_PPC_BOOK3E
#define __hard_irq_enable()	wrtee(MSR_EE)
#define __hard_irq_disable()	wrtee(0)
#define __hard_EE_RI_disable()	wrtee(0)
#define __hard_RI_enable()	do { } while (0)
#else
#define __hard_irq_enable()	__mtmsrd(MSR_EE|MSR_RI, 1)
#define __hard_irq_disable()	__mtmsrd(MSR_RI, 1)
#define __hard_EE_RI_disable()	__mtmsrd(0, 1)
#define __hard_RI_enable()	__mtmsrd(MSR_RI, 1)
#endif

#define hard_irq_disable()	do {					\
	unsigned long flags;						\
	__hard_irq_disable();						\
	flags = irq_soft_mask_set_return(IRQS_ALL_DISABLED);		\
	local_paca->irq_happened |= PACA_IRQ_HARD_DIS;			\
	if (!arch_irqs_disabled_flags(flags)) {				\
		asm ("stdx %%r1, 0, %1 ;"				\
		     : "=m" (local_paca->saved_r1)			\
		     : "b" (&local_paca->saved_r1));			\
		trace_hardirqs_off();					\
	}								\
} while(0)

static inline bool __lazy_irq_pending(u8 irq_happened)
{
	return !!(irq_happened & ~PACA_IRQ_HARD_DIS);
}

/*
 * Check if a lazy IRQ is pending. Should be called with IRQs hard disabled.
 */
static inline bool lazy_irq_pending(void)
{
	return __lazy_irq_pending(get_paca()->irq_happened);
}

/*
 * Check if a lazy IRQ is pending, with no debugging checks.
 * Should be called with IRQs hard disabled.
 * For use in RI disabled code or other constrained situations.
 */
static inline bool lazy_irq_pending_nocheck(void)
{
	return __lazy_irq_pending(local_paca->irq_happened);
}

/*
 * This is called by asynchronous interrupts to conditionally
 * re-enable hard interrupts after having cleared the source
 * of the interrupt. They are kept disabled if there is a different
 * soft-masked interrupt pending that requires hard masking.
 */
static inline void may_hard_irq_enable(void)
{
	if (!(get_paca()->irq_happened & PACA_IRQ_MUST_HARD_MASK)) {
		get_paca()->irq_happened &= ~PACA_IRQ_HARD_DIS;
		__hard_irq_enable();
	}
}

static inline bool arch_irq_disabled_regs(struct pt_regs *regs)
{
	return (regs->softe & IRQS_DISABLED);
}

extern bool prep_irq_for_idle(void);
extern bool prep_irq_for_idle_irqsoff(void);
extern void irq_set_pending_from_srr1(unsigned long srr1);

#define fini_irq_for_idle_irqsoff() trace_hardirqs_off();

extern void force_external_irq_replay(void);

#else /* CONFIG_PPC64 */

static inline unsigned long arch_local_save_flags(void)
{
	return mfmsr();
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	if (IS_ENABLED(CONFIG_BOOKE))
		wrtee(flags);
	else
		mtmsr(flags);
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();

	if (IS_ENABLED(CONFIG_BOOKE))
		wrtee(0);
	else if (IS_ENABLED(CONFIG_PPC_8xx))
		wrtspr(SPRN_EID);
	else
		mtmsr(flags & ~MSR_EE);

	return flags;
}

static inline void arch_local_irq_disable(void)
{
	if (IS_ENABLED(CONFIG_BOOKE))
		wrtee(0);
	else if (IS_ENABLED(CONFIG_PPC_8xx))
		wrtspr(SPRN_EID);
	else
		mtmsr(mfmsr() & ~MSR_EE);
}

static inline void arch_local_irq_enable(void)
{
	if (IS_ENABLED(CONFIG_BOOKE))
		wrtee(MSR_EE);
	else if (IS_ENABLED(CONFIG_PPC_8xx))
		wrtspr(SPRN_EIE);
	else
		mtmsr(mfmsr() | MSR_EE);
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & MSR_EE) == 0;
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#define hard_irq_disable()		arch_local_irq_disable()

static inline bool arch_irq_disabled_regs(struct pt_regs *regs)
{
	return !(regs->msr & MSR_EE);
}

static inline void may_hard_irq_enable(void) { }

#endif /* CONFIG_PPC64 */

#define ARCH_IRQ_INIT_FLAGS	IRQ_NOREQUEST

/*
 * interrupt-retrigger: should we handle this via lost interrupts and IPIs
 * or should we not care like we do now ? --BenH.
 */
struct irq_chip;

#endif  /* __ASSEMBLY__ */
#endif	/* __KERNEL__ */
#endif	/* _ASM_POWERPC_HW_IRQ_H */

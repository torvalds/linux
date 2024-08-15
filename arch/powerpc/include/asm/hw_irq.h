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
 * and allow a proper replay.
 *
 * The PACA_IRQ_HARD_DIS is set whenever we hard disable. It is almost
 * always in synch with the MSR[EE] state, except:
 * - A window in interrupt entry, where hardware disables MSR[EE] and that
 *   must be "reconciled" with the soft mask state.
 * - NMI interrupts that hit in awkward places, until they fix the state.
 * - When local irqs are being enabled and state is being fixed up.
 * - When returning from an interrupt there are some windows where this
 *   can become out of synch, but gets fixed before the RFI or before
 *   executing the next user instruction (see arch/powerpc/kernel/interrupt.c).
 */
#define PACA_IRQ_HARD_DIS	0x01
#define PACA_IRQ_DBELL		0x02
#define PACA_IRQ_EE		0x04
#define PACA_IRQ_DEC		0x08 /* Or FIT */
#define PACA_IRQ_HMI		0x10
#define PACA_IRQ_PMI		0x20
#define PACA_IRQ_REPLAYING	0x40

/*
 * Some soft-masked interrupts must be hard masked until they are replayed
 * (e.g., because the soft-masked handler does not clear the exception).
 * Interrupt replay itself must remain hard masked too.
 */
#ifdef CONFIG_PPC_BOOK3S
#define PACA_IRQ_MUST_HARD_MASK	(PACA_IRQ_EE|PACA_IRQ_PMI|PACA_IRQ_REPLAYING)
#else
#define PACA_IRQ_MUST_HARD_MASK	(PACA_IRQ_EE|PACA_IRQ_REPLAYING)
#endif

#endif /* CONFIG_PPC64 */

/*
 * flags for paca->irq_soft_mask
 */
#define IRQS_ENABLED		0
#define IRQS_DISABLED		1 /* local_irq_disable() interrupts */
#define IRQS_PMI_DISABLED	2
#define IRQS_ALL_DISABLED	(IRQS_DISABLED | IRQS_PMI_DISABLED)

#ifndef __ASSEMBLY__

static inline void __hard_irq_enable(void)
{
	if (IS_ENABLED(CONFIG_BOOKE_OR_40x))
		wrtee(MSR_EE);
	else if (IS_ENABLED(CONFIG_PPC_8xx))
		wrtspr(SPRN_EIE);
	else if (IS_ENABLED(CONFIG_PPC_BOOK3S_64))
		__mtmsrd(MSR_EE | MSR_RI, 1);
	else
		mtmsr(mfmsr() | MSR_EE);
}

static inline void __hard_irq_disable(void)
{
	if (IS_ENABLED(CONFIG_BOOKE_OR_40x))
		wrtee(0);
	else if (IS_ENABLED(CONFIG_PPC_8xx))
		wrtspr(SPRN_EID);
	else if (IS_ENABLED(CONFIG_PPC_BOOK3S_64))
		__mtmsrd(MSR_RI, 1);
	else
		mtmsr(mfmsr() & ~MSR_EE);
}

static inline void __hard_EE_RI_disable(void)
{
	if (IS_ENABLED(CONFIG_BOOKE_OR_40x))
		wrtee(0);
	else if (IS_ENABLED(CONFIG_PPC_8xx))
		wrtspr(SPRN_NRI);
	else if (IS_ENABLED(CONFIG_PPC_BOOK3S_64))
		__mtmsrd(0, 1);
	else
		mtmsr(mfmsr() & ~(MSR_EE | MSR_RI));
}

static inline void __hard_RI_enable(void)
{
	if (IS_ENABLED(CONFIG_BOOKE_OR_40x))
		return;

	if (IS_ENABLED(CONFIG_PPC_8xx))
		wrtspr(SPRN_EID);
	else if (IS_ENABLED(CONFIG_PPC_BOOK3S_64))
		__mtmsrd(MSR_RI, 1);
	else
		mtmsr(mfmsr() | MSR_RI);
}

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
	if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG))
		WARN_ON(mask && !(mask & IRQS_DISABLED));

	asm volatile(
		"stb %0,%1(13)"
		:
		: "r" (mask),
		  "i" (offsetof(struct paca_struct, irq_soft_mask))
		: "memory");
}

static inline notrace unsigned long irq_soft_mask_set_return(unsigned long mask)
{
	unsigned long flags = irq_soft_mask_return();

	irq_soft_mask_set(mask);

	return flags;
}

static inline notrace unsigned long irq_soft_mask_or_return(unsigned long mask)
{
	unsigned long flags = irq_soft_mask_return();

	irq_soft_mask_set(flags | mask);

	return flags;
}

static inline notrace unsigned long irq_soft_mask_andc_return(unsigned long mask)
{
	unsigned long flags = irq_soft_mask_return();

	irq_soft_mask_set(flags & ~mask);

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
	return irq_soft_mask_or_return(IRQS_DISABLED);
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return flags & IRQS_DISABLED;
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

static inline void set_pmi_irq_pending(void)
{
	/*
	 * Invoked from PMU callback functions to set PMI bit in the paca.
	 * This has to be called with irq's disabled (via hard_irq_disable()).
	 */
	if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG))
		WARN_ON_ONCE(mfmsr() & MSR_EE);

	get_paca()->irq_happened |= PACA_IRQ_PMI;
}

static inline void clear_pmi_irq_pending(void)
{
	/*
	 * Invoked from PMU callback functions to clear the pending PMI bit
	 * in the paca.
	 */
	if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG))
		WARN_ON_ONCE(mfmsr() & MSR_EE);

	get_paca()->irq_happened &= ~PACA_IRQ_PMI;
}

static inline bool pmi_irq_pending(void)
{
	/*
	 * Invoked from PMU callback functions to check if there is a pending
	 * PMI bit in the paca.
	 */
	if (get_paca()->irq_happened & PACA_IRQ_PMI)
		return true;

	return false;
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
		if (!raw_irqs_disabled_flags(flags))		\
			trace_hardirqs_off();			\
	} while(0)
#define powerpc_local_irq_pmu_restore(flags)			\
	do {							\
		if (!raw_irqs_disabled_flags(flags))		\
			trace_hardirqs_on();			\
		raw_local_irq_pmu_restore(flags);		\
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

#define hard_irq_disable()	do {					\
	unsigned long flags;						\
	__hard_irq_disable();						\
	flags = irq_soft_mask_set_return(IRQS_ALL_DISABLED);		\
	local_paca->irq_happened |= PACA_IRQ_HARD_DIS;			\
	if (!arch_irqs_disabled_flags(flags)) {				\
		asm volatile("std%X0 %1,%0" : "=m" (local_paca->saved_r1) \
					    : "r" (current_stack_pointer)); \
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

bool power_pmu_wants_prompt_pmi(void);

/*
 * This is called by asynchronous interrupts to check whether to
 * conditionally re-enable hard interrupts after having cleared
 * the source of the interrupt. They are kept disabled if there
 * is a different soft-masked interrupt pending that requires hard
 * masking.
 */
static inline bool should_hard_irq_enable(struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG)) {
		WARN_ON(irq_soft_mask_return() != IRQS_ALL_DISABLED);
		WARN_ON(!(get_paca()->irq_happened & PACA_IRQ_HARD_DIS));
		WARN_ON(mfmsr() & MSR_EE);
	}

	if (!IS_ENABLED(CONFIG_PERF_EVENTS))
		return false;
	/*
	 * If the PMU is not running, there is not much reason to enable
	 * MSR[EE] in irq handlers because any interrupts would just be
	 * soft-masked.
	 *
	 * TODO: Add test for 64e
	 */
	if (IS_ENABLED(CONFIG_PPC_BOOK3S_64)) {
		if (!power_pmu_wants_prompt_pmi())
			return false;
		/*
		 * If PMIs are disabled then IRQs should be disabled as well,
		 * so we shouldn't see this condition, check for it just in
		 * case because we are about to enable PMIs.
		 */
		if (WARN_ON_ONCE(regs->softe & IRQS_PMI_DISABLED))
			return false;
	}

	if (get_paca()->irq_happened & PACA_IRQ_MUST_HARD_MASK)
		return false;

	return true;
}

/*
 * Do the hard enabling, only call this if should_hard_irq_enable is true.
 * This allows PMI interrupts to profile irq handlers.
 */
static inline void do_hard_irq_enable(void)
{
	/*
	 * Asynch interrupts come in with IRQS_ALL_DISABLED,
	 * PACA_IRQ_HARD_DIS, and MSR[EE]=0.
	 */
	if (IS_ENABLED(CONFIG_PPC_BOOK3S_64))
		irq_soft_mask_andc_return(IRQS_PMI_DISABLED);
	get_paca()->irq_happened &= ~PACA_IRQ_HARD_DIS;
	__hard_irq_enable();
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

static inline void irq_soft_mask_regs_set_state(struct pt_regs *regs, unsigned long val)
{
	regs->softe = val;
}
#else /* CONFIG_PPC64 */

static inline notrace unsigned long irq_soft_mask_return(void)
{
	return 0;
}

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
	__hard_irq_disable();
}

static inline void arch_local_irq_enable(void)
{
	__hard_irq_enable();
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

static __always_inline bool should_hard_irq_enable(struct pt_regs *regs)
{
	return false;
}

static inline void do_hard_irq_enable(void)
{
	BUILD_BUG();
}

static inline void clear_pmi_irq_pending(void) { }
static inline void set_pmi_irq_pending(void) { }
static inline bool pmi_irq_pending(void) { return false; }

static inline void irq_soft_mask_regs_set_state(struct pt_regs *regs, unsigned long val)
{
}
#endif /* CONFIG_PPC64 */

static inline unsigned long mtmsr_isync_irqsafe(unsigned long msr)
{
#ifdef CONFIG_PPC64
	if (arch_irqs_disabled()) {
		/*
		 * With soft-masking, MSR[EE] can change from 1 to 0
		 * asynchronously when irqs are disabled, and we don't want to
		 * set MSR[EE] back to 1 here if that has happened. A race-free
		 * way to do this is ensure EE is already 0. Another way it
		 * could be done is with a RESTART_TABLE handler, but that's
		 * probably overkill here.
		 */
		msr &= ~MSR_EE;
		mtmsr_isync(msr);
		irq_soft_mask_set(IRQS_ALL_DISABLED);
		local_paca->irq_happened |= PACA_IRQ_HARD_DIS;
	} else
#endif
		mtmsr_isync(msr);

	return msr;
}


#define ARCH_IRQ_INIT_FLAGS	IRQ_NOREQUEST

#endif  /* __ASSEMBLY__ */
#endif	/* __KERNEL__ */
#endif	/* _ASM_POWERPC_HW_IRQ_H */

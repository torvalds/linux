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

#endif /* CONFIG_PPC64 */

#ifndef __ASSEMBLY__

extern void __replay_interrupt(unsigned int vector);

extern void timer_interrupt(struct pt_regs *);

#ifdef CONFIG_PPC64
#include <asm/paca.h>

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;

	asm volatile(
		"lbz %0,%1(13)"
		: "=r" (flags)
		: "i" (offsetof(struct paca_struct, soft_enabled)));

	return flags;
}

static inline unsigned long arch_local_irq_disable(void)
{
	unsigned long flags, zero;

	asm volatile(
		"li %1,0; lbz %0,%2(13); stb %1,%2(13)"
		: "=r" (flags), "=&r" (zero)
		: "i" (offsetof(struct paca_struct, soft_enabled))
		: "memory");

	return flags;
}

extern void arch_local_irq_restore(unsigned long);

static inline void arch_local_irq_enable(void)
{
	arch_local_irq_restore(1);
}

static inline unsigned long arch_local_irq_save(void)
{
	return arch_local_irq_disable();
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return flags == 0;
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#ifdef CONFIG_PPC_BOOK3E
#define __hard_irq_enable()	asm volatile("wrteei 1" : : : "memory");
#define __hard_irq_disable()	asm volatile("wrteei 0" : : : "memory");
#else
#define __hard_irq_enable()	__mtmsrd(local_paca->kernel_msr | MSR_EE, 1)
#define __hard_irq_disable()	__mtmsrd(local_paca->kernel_msr, 1)
#endif

static inline void hard_irq_disable(void)
{
	__hard_irq_disable();
	get_paca()->soft_enabled = 0;
	get_paca()->irq_happened |= PACA_IRQ_HARD_DIS;
}

/*
 * This is called by asynchronous interrupts to conditionally
 * re-enable hard interrupts when soft-disabled after having
 * cleared the source of the interrupt
 */
static inline void may_hard_irq_enable(void)
{
	get_paca()->irq_happened &= ~PACA_IRQ_HARD_DIS;
	if (!(get_paca()->irq_happened & PACA_IRQ_EE))
		__hard_irq_enable();
}

static inline bool arch_irq_disabled_regs(struct pt_regs *regs)
{
	return !regs->softe;
}

#else /* CONFIG_PPC64 */

#define SET_MSR_EE(x)	mtmsr(x)

static inline unsigned long arch_local_save_flags(void)
{
	return mfmsr();
}

static inline void arch_local_irq_restore(unsigned long flags)
{
#if defined(CONFIG_BOOKE)
	asm volatile("wrtee %0" : : "r" (flags) : "memory");
#else
	mtmsr(flags);
#endif
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();
#ifdef CONFIG_BOOKE
	asm volatile("wrteei 0" : : : "memory");
#else
	SET_MSR_EE(flags & ~MSR_EE);
#endif
	return flags;
}

static inline void arch_local_irq_disable(void)
{
#ifdef CONFIG_BOOKE
	asm volatile("wrteei 0" : : : "memory");
#else
	arch_local_irq_save();
#endif
}

static inline void arch_local_irq_enable(void)
{
#ifdef CONFIG_BOOKE
	asm volatile("wrteei 1" : : : "memory");
#else
	unsigned long msr = mfmsr();
	SET_MSR_EE(msr | MSR_EE);
#endif
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

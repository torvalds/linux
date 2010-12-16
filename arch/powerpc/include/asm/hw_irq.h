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

extern void timer_interrupt(struct pt_regs *);

#ifdef CONFIG_PPC64
#include <asm/paca.h>

static inline unsigned long local_get_flags(void)
{
	unsigned long flags;

	__asm__ __volatile__("lbz %0,%1(13)"
	: "=r" (flags)
	: "i" (offsetof(struct paca_struct, soft_enabled)));

	return flags;
}

static inline unsigned long raw_local_irq_disable(void)
{
	unsigned long flags, zero;

	__asm__ __volatile__("li %1,0; lbz %0,%2(13); stb %1,%2(13)"
	: "=r" (flags), "=&r" (zero)
	: "i" (offsetof(struct paca_struct, soft_enabled))
	: "memory");

	return flags;
}

extern void raw_local_irq_restore(unsigned long);
extern void iseries_handle_interrupts(void);

#define raw_local_irq_enable()		raw_local_irq_restore(1)
#define raw_local_save_flags(flags)	((flags) = local_get_flags())
#define raw_local_irq_save(flags)	((flags) = raw_local_irq_disable())

#define raw_irqs_disabled()		(local_get_flags() == 0)
#define raw_irqs_disabled_flags(flags)	((flags) == 0)

#ifdef CONFIG_PPC_BOOK3E
#define __hard_irq_enable()	__asm__ __volatile__("wrteei 1": : :"memory");
#define __hard_irq_disable()	__asm__ __volatile__("wrteei 0": : :"memory");
#else
#define __hard_irq_enable()	__mtmsrd(mfmsr() | MSR_EE, 1)
#define __hard_irq_disable()	__mtmsrd(mfmsr() & ~MSR_EE, 1)
#endif

#define  hard_irq_disable()			\
	do {					\
		__hard_irq_disable();		\
		get_paca()->soft_enabled = 0;	\
		get_paca()->hard_enabled = 0;	\
	} while(0)

static inline int irqs_disabled_flags(unsigned long flags)
{
	return flags == 0;
}

#else

#if defined(CONFIG_BOOKE)
#define SET_MSR_EE(x)	mtmsr(x)
#define raw_local_irq_restore(flags)	__asm__ __volatile__("wrtee %0" : : "r" (flags) : "memory")
#else
#define SET_MSR_EE(x)	mtmsr(x)
#define raw_local_irq_restore(flags)	mtmsr(flags)
#endif

static inline void raw_local_irq_disable(void)
{
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 0": : :"memory");
#else
	unsigned long msr;

	msr = mfmsr();
	SET_MSR_EE(msr & ~MSR_EE);
#endif
}

static inline void raw_local_irq_enable(void)
{
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 1": : :"memory");
#else
	unsigned long msr;

	msr = mfmsr();
	SET_MSR_EE(msr | MSR_EE);
#endif
}

static inline void raw_local_irq_save_ptr(unsigned long *flags)
{
	unsigned long msr;
	msr = mfmsr();
	*flags = msr;
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 0": : :"memory");
#else
	SET_MSR_EE(msr & ~MSR_EE);
#endif
}

#define raw_local_save_flags(flags)	((flags) = mfmsr())
#define raw_local_irq_save(flags)	raw_local_irq_save_ptr(&flags)
#define raw_irqs_disabled()		((mfmsr() & MSR_EE) == 0)
#define raw_irqs_disabled_flags(flags)	(((flags) & MSR_EE) == 0)

#define hard_irq_disable()		raw_local_irq_disable()

static inline int irqs_disabled_flags(unsigned long flags)
{
	return (flags & MSR_EE) == 0;
}

#endif /* CONFIG_PPC64 */

/*
 * interrupt-retrigger: should we handle this via lost interrupts and IPIs
 * or should we not care like we do now ? --BenH.
 */
struct irq_chip;

#endif	/* __KERNEL__ */
#endif	/* _ASM_POWERPC_HW_IRQ_H */

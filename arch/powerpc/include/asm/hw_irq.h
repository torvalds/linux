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
extern void iseries_handle_interrupts(void);

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
#define __hard_irq_enable()	__mtmsrd(mfmsr() | MSR_EE, 1)
#define __hard_irq_disable()	__mtmsrd(mfmsr() & ~MSR_EE, 1)
#endif

#define  hard_irq_disable()			\
	do {					\
		__hard_irq_disable();		\
		get_paca()->soft_enabled = 0;	\
		get_paca()->hard_enabled = 0;	\
	} while(0)

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

#endif /* CONFIG_PPC64 */

#define ARCH_IRQ_INIT_FLAGS	IRQ_NOREQUEST

/*
 * interrupt-retrigger: should we handle this via lost interrupts and IPIs
 * or should we not care like we do now ? --BenH.
 */
struct irq_chip;

#endif	/* __KERNEL__ */
#endif	/* _ASM_POWERPC_HW_IRQ_H */

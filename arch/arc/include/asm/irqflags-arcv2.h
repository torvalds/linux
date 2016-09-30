/*
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_IRQFLAGS_ARCV2_H
#define __ASM_IRQFLAGS_ARCV2_H

#include <asm/arcregs.h>

/* status32 Bits */
#define STATUS_AD_BIT	19   /* Disable Align chk: core supports non-aligned */
#define STATUS_IE_BIT	31

#define STATUS_AD_MASK		(1<<STATUS_AD_BIT)
#define STATUS_IE_MASK		(1<<STATUS_IE_BIT)

/* status32 Bits as encoded/expected by CLRI/SETI */
#define CLRI_STATUS_IE_BIT	4

#define CLRI_STATUS_E_MASK	0xF
#define CLRI_STATUS_IE_MASK	(1 << CLRI_STATUS_IE_BIT)

#define AUX_USER_SP		0x00D
#define AUX_IRQ_CTRL		0x00E
#define AUX_IRQ_ACT		0x043	/* Active Intr across all levels */
#define AUX_IRQ_LVL_PEND	0x200	/* Pending Intr across all levels */
#define AUX_IRQ_HINT		0x201	/* For generating Soft Interrupts */
#define AUX_IRQ_PRIORITY	0x206
#define ICAUSE			0x40a
#define AUX_IRQ_SELECT		0x40b
#define AUX_IRQ_ENABLE		0x40c

/* Was Intr taken in User Mode */
#define AUX_IRQ_ACT_BIT_U	31

/*
 * User space should be interruptable even by lowest prio interrupt
 * Safe even if actual interrupt priorities is fewer or even one
 */
#define ARCV2_IRQ_DEF_PRIO	15

/* seed value for status register */
#define ISA_INIT_STATUS_BITS	(STATUS_IE_MASK | STATUS_AD_MASK | \
					(ARCV2_IRQ_DEF_PRIO << 1))

/* SLEEP needs default irq priority (<=) which can interrupt the doze */
#define ISA_SLEEP_ARG		(0x10 | ARCV2_IRQ_DEF_PRIO)

#ifndef __ASSEMBLY__

/*
 * Save IRQ state and disable IRQs
 */
static inline long arch_local_irq_save(void)
{
	unsigned long flags;

	__asm__ __volatile__("	clri %0	\n" : "=r" (flags) : : "memory");

	return flags;
}

/*
 * restore saved IRQ state
 */
static inline void arch_local_irq_restore(unsigned long flags)
{
	__asm__ __volatile__("	seti %0	\n" : : "r" (flags) : "memory");
}

/*
 * Unconditionally Enable IRQs
 */
static inline void arch_local_irq_enable(void)
{
	unsigned int irqact = read_aux_reg(AUX_IRQ_ACT);

	if (irqact & 0xffff)
		write_aux_reg(AUX_IRQ_ACT, irqact & ~0xffff);

	__asm__ __volatile__("	seti	\n" : : : "memory");
}

/*
 * Unconditionally Disable IRQs
 */
static inline void arch_local_irq_disable(void)
{
	__asm__ __volatile__("	clri	\n" : : : "memory");
}

/*
 * save IRQ state
 */
static inline long arch_local_save_flags(void)
{
	unsigned long temp;

	__asm__ __volatile__(
	"	lr  %0, [status32]	\n"
	: "=&r"(temp)
	:
	: "memory");

	/* To be compatible with irq_save()/irq_restore()
	 * encode the irq bits as expected by CLRI/SETI
	 * (this was needed to make CONFIG_TRACE_IRQFLAGS work)
	 */
	temp = (1 << 5) |
		((!!(temp & STATUS_IE_MASK)) << CLRI_STATUS_IE_BIT) |
		((temp >> 1) & CLRI_STATUS_E_MASK);
	return temp;
}

/*
 * Query IRQ state
 */
static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & CLRI_STATUS_IE_MASK);
}

static inline int arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

static inline void arc_softirq_trigger(int irq)
{
	write_aux_reg(AUX_IRQ_HINT, irq);
}

static inline void arc_softirq_clear(int irq)
{
	write_aux_reg(AUX_IRQ_HINT, 0);
}

#else

#ifdef CONFIG_TRACE_IRQFLAGS

.macro TRACE_ASM_IRQ_DISABLE
	bl	trace_hardirqs_off
.endm

.macro TRACE_ASM_IRQ_ENABLE
	bl	trace_hardirqs_on
.endm

#else

.macro TRACE_ASM_IRQ_DISABLE
.endm

.macro TRACE_ASM_IRQ_ENABLE
.endm

#endif
.macro IRQ_DISABLE  scratch
	clri
	TRACE_ASM_IRQ_DISABLE
.endm

.macro IRQ_ENABLE  scratch
	TRACE_ASM_IRQ_ENABLE
	seti
.endm

#endif	/* __ASSEMBLY__ */

#endif

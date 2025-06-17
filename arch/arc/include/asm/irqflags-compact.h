/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __ASM_IRQFLAGS_ARCOMPACT_H
#define __ASM_IRQFLAGS_ARCOMPACT_H

/* vineetg: March 2010 : local_irq_save( ) optimisation
 *  -Remove explicit mov of current status32 into reg, that is not needed
 *  -Use BIC  insn instead of INVERTED + AND
 *  -Conditionally disable interrupts (if they are not enabled, don't disable)
*/

#include <asm/arcregs.h>

/* status32 Reg bits related to Interrupt Handling */
#define STATUS_E1_BIT		1	/* Int 1 enable */
#define STATUS_E2_BIT		2	/* Int 2 enable */
#define STATUS_A1_BIT		3	/* Int 1 active */
#define STATUS_A2_BIT		4	/* Int 2 active */
#define STATUS_AE_BIT		5	/* Exception active */

#define STATUS_E1_MASK		(1<<STATUS_E1_BIT)
#define STATUS_E2_MASK		(1<<STATUS_E2_BIT)
#define STATUS_A1_MASK		(1<<STATUS_A1_BIT)
#define STATUS_A2_MASK		(1<<STATUS_A2_BIT)
#define STATUS_AE_MASK		(1<<STATUS_AE_BIT)
#define STATUS_IE_MASK		(STATUS_E1_MASK | STATUS_E2_MASK)

/* Other Interrupt Handling related Aux regs */
#define AUX_IRQ_LEV		0x200	/* IRQ Priority: L1 or L2 */
#define AUX_IRQ_HINT		0x201	/* For generating Soft Interrupts */
#define AUX_IRQ_LV12		0x43	/* interrupt level register */

#define AUX_IENABLE		0x40c
#define AUX_ITRIGGER		0x40d
#define AUX_IPULSE		0x415

#define ISA_INIT_STATUS_BITS	STATUS_IE_MASK

#ifndef __ASSEMBLER__

/******************************************************************
 * IRQ Control Macros
 *
 * All of them have "memory" clobber (compiler barrier) which is needed to
 * ensure that LD/ST requiring irq safety (R-M-W when LLSC is not available)
 * are redone after IRQs are re-enabled (and gcc doesn't reuse stale register)
 *
 * Noted at the time of Abilis Timer List corruption
 *
 * Orig Bug + Rejected solution:
 * https://lore.kernel.org/lkml/1364553218-31255-1-git-send-email-vgupta@synopsys.com
 *
 * Reasoning:
 * https://lore.kernel.org/lkml/CA+55aFyFWjpSVQM6M266tKrG_ZXJzZ-nYejpmXYQXbrr42mGPQ@mail.gmail.com
 *
 ******************************************************************/

/*
 * Save IRQ state and disable IRQs
 */
static inline long arch_local_irq_save(void)
{
	unsigned long temp, flags;

	__asm__ __volatile__(
	"	lr  %1, [status32]	\n"
	"	bic %0, %1, %2		\n"
	"	and.f 0, %1, %2	\n"
	"	flag.nz %0		\n"
	: "=r"(temp), "=r"(flags)
	: "n"((STATUS_E1_MASK | STATUS_E2_MASK))
	: "memory", "cc");

	return flags;
}

/*
 * restore saved IRQ state
 */
static inline void arch_local_irq_restore(unsigned long flags)
{

	__asm__ __volatile__(
	"	flag %0			\n"
	:
	: "r"(flags)
	: "memory");
}

/*
 * Unconditionally Enable IRQs
 */
#ifdef CONFIG_ARC_COMPACT_IRQ_LEVELS
extern void arch_local_irq_enable(void);
#else
static inline void arch_local_irq_enable(void)
{
	unsigned long temp;

	__asm__ __volatile__(
	"	lr   %0, [status32]	\n"
	"	or   %0, %0, %1		\n"
	"	flag %0			\n"
	: "=&r"(temp)
	: "n"((STATUS_E1_MASK | STATUS_E2_MASK))
	: "cc", "memory");
}
#endif

/*
 * Unconditionally Disable IRQs
 */
static inline void arch_local_irq_disable(void)
{
	unsigned long temp;

	__asm__ __volatile__(
	"	lr  %0, [status32]	\n"
	"	and %0, %0, %1		\n"
	"	flag %0			\n"
	: "=&r"(temp)
	: "n"(~(STATUS_E1_MASK | STATUS_E2_MASK))
	: "memory");
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

	return temp;
}

/*
 * Query IRQ state
 */
static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & (STATUS_E1_MASK
#ifdef CONFIG_ARC_COMPACT_IRQ_LEVELS
			| STATUS_E2_MASK
#endif
		));
}

static inline int arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
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
	lr	\scratch, [status32]
	bic	\scratch, \scratch, (STATUS_E1_MASK | STATUS_E2_MASK)
	flag	\scratch
	TRACE_ASM_IRQ_DISABLE
.endm

.macro IRQ_ENABLE  scratch
	TRACE_ASM_IRQ_ENABLE
	lr	\scratch, [status32]
	or	\scratch, \scratch, (STATUS_E1_MASK | STATUS_E2_MASK)
	flag	\scratch
.endm

#endif	/* __ASSEMBLER__ */

#endif

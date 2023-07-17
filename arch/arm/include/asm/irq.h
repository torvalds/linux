/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARM_IRQ_H
#define __ASM_ARM_IRQ_H

#define NR_IRQS_LEGACY	16

#ifndef CONFIG_SPARSE_IRQ
#include <mach/irqs.h>
#else
#define NR_IRQS NR_IRQS_LEGACY
#endif

#ifndef irq_canonicalize
#define irq_canonicalize(i)	(i)
#endif

/*
 * Use this value to indicate lack of interrupt
 * capability
 */
#ifndef NO_IRQ
#define NO_IRQ	((unsigned int)(-1))
#endif

#ifndef __ASSEMBLY__
struct irqaction;
struct pt_regs;

void handle_IRQ(unsigned int, struct pt_regs *);

#ifdef CONFIG_SMP
#include <linux/cpumask.h>

extern void arch_trigger_cpumask_backtrace(const cpumask_t *mask,
					   bool exclude_self);
#define arch_trigger_cpumask_backtrace arch_trigger_cpumask_backtrace
#endif

static inline int nr_legacy_irqs(void)
{
	return NR_IRQS_LEGACY;
}

#endif

#endif


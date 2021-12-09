/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef __KERNEL__
#ifndef _ASM_POWERPC_IRQ_H
#define _ASM_POWERPC_IRQ_H

/*
 */

#include <linux/threads.h>
#include <linux/list.h>
#include <linux/radix-tree.h>

#include <asm/types.h>
#include <linux/atomic.h>


extern atomic_t ppc_n_lost_interrupts;

/* This number is used when no interrupt has been assigned */
#define NO_IRQ			(0)

/* Total number of virq in the platform */
#define NR_IRQS		CONFIG_NR_IRQS

/* Number of irqs reserved for a legacy isa controller */
#define NR_IRQS_LEGACY		16

extern irq_hw_number_t virq_to_hw(unsigned int virq);

static __inline__ int irq_canonicalize(int irq)
{
	return irq;
}

extern int distribute_irqs;

struct pt_regs;

#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
/*
 * Per-cpu stacks for handling critical, debug and machine check
 * level interrupts.
 */
extern void *critirq_ctx[NR_CPUS];
extern void *dbgirq_ctx[NR_CPUS];
extern void *mcheckirq_ctx[NR_CPUS];
#endif

/*
 * Per-cpu stacks for handling hard and soft interrupts.
 */
extern void *hardirq_ctx[NR_CPUS];
extern void *softirq_ctx[NR_CPUS];

void __do_IRQ(struct pt_regs *regs);
extern void __init init_IRQ(void);
extern void __do_irq(struct pt_regs *regs);

int irq_choose_cpu(const struct cpumask *mask);

#endif /* _ASM_IRQ_H */
#endif /* __KERNEL__ */

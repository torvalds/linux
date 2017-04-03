/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Waldorf GMBH, written by Ralf Baechle
 * Copyright (C) 1995, 96, 97, 98, 99, 2000, 01, 02, 03 by Ralf Baechle
 */
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <linux/linkage.h>
#include <linux/smp.h>
#include <linux/irqdomain.h>

#include <asm/mipsmtregs.h>

#include <irq.h>

#define IRQ_STACK_SIZE			THREAD_SIZE

extern void *irq_stack[NR_CPUS];

static inline bool on_irq_stack(int cpu, unsigned long sp)
{
	unsigned long low = (unsigned long)irq_stack[cpu];
	unsigned long high = low + IRQ_STACK_SIZE;

	return (low <= sp && sp <= high);
}

#ifdef CONFIG_I8259
static inline int irq_canonicalize(int irq)
{
	return ((irq == I8259A_IRQ_BASE + 2) ? I8259A_IRQ_BASE + 9 : irq);
}
#else
#define irq_canonicalize(irq) (irq)	/* Sane hardware, sane code ... */
#endif

asmlinkage void plat_irq_dispatch(void);

extern void do_IRQ(unsigned int irq);

extern void arch_init_irq(void);
extern void spurious_interrupt(void);

extern int allocate_irqno(void);
extern void alloc_legacy_irqno(void);
extern void free_irqno(unsigned int irq);

/*
 * Before R2 the timer and performance counter interrupts were both fixed to
 * IE7.	 Since R2 their number has to be read from the c0_intctl register.
 */
#define CP0_LEGACY_COMPARE_IRQ 7
#define CP0_LEGACY_PERFCNT_IRQ 7

extern int cp0_compare_irq;
extern int cp0_compare_irq_shift;
extern int cp0_perfcount_irq;
extern int cp0_fdc_irq;

extern int get_c0_fdc_int(void);

void arch_trigger_cpumask_backtrace(const struct cpumask *mask,
				    bool exclude_self);
#define arch_trigger_cpumask_backtrace arch_trigger_cpumask_backtrace

#endif /* _ASM_IRQ_H */

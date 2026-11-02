/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_HARDIRQ_H
#define _ASM_POWERPC_HARDIRQ_H

#include <linux/threads.h>
#include <linux/irq.h>

enum irq_stat_counts {
	IRQ_COUNT_LOC_TIMER,
	IRQ_COUNT_BCT_TIMER,
	IRQ_COUNT_OTHER_TIMER,
	IRQ_COUNT_SPURIOUS,
	IRQ_COUNT_PMI,
	IRQ_COUNT_MCE,
	IRQ_COUNT_NMI_SRESET,
#ifdef CONFIG_PPC_WATCHDOG
	IRQ_COUNT_WATCHDOG,
#endif
#ifdef CONFIG_PPC_DOORBELL
	IRQ_COUNT_DOORBELL,
#endif
	IRQ_COUNT_MAX,
};

typedef struct {
	unsigned int counts[IRQ_COUNT_MAX];
} ____cacheline_aligned irq_cpustat_t;

DECLARE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);
DECLARE_PER_CPU(unsigned int, __softirq_pending);
#define local_softirq_pending_ref       __softirq_pending

#define inc_irq_stat(index)	__this_cpu_inc(irq_stat.counts[IRQ_COUNT_##index])
void inc_irq_stat_and_enable(enum irq_stat_counts which);

#define __ARCH_IRQ_STAT
#define __ARCH_IRQ_EXIT_IRQS_DISABLED

static inline void ack_bad_irq(unsigned int irq)
{
	printk(KERN_CRIT "unexpected IRQ trap at vector %02x\n", irq);
}

extern u64 arch_irq_stat_cpu(unsigned int cpu);
#define arch_irq_stat_cpu	arch_irq_stat_cpu

#endif /* _ASM_POWERPC_HARDIRQ_H */

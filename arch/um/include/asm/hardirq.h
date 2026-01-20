/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_UM_HARDIRQ_H
#define __ASM_UM_HARDIRQ_H

#include <linux/cache.h>
#include <linux/threads.h>

#define __ARCH_IRQ_EXIT_IRQS_DISABLED 1

typedef struct {
	unsigned int __softirq_pending;
#if IS_ENABLED(CONFIG_SMP)
	unsigned int irq_resched_count;
	unsigned int irq_call_count;
#endif
} ____cacheline_aligned irq_cpustat_t;

DECLARE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

#define __ARCH_IRQ_STAT

#define inc_irq_stat(member)	this_cpu_inc(irq_stat.member)

#include <linux/irq.h>

static inline void ack_bad_irq(unsigned int irq)
{
	pr_crit("unexpected IRQ trap at vector %02x\n", irq);
}

#endif /* __ASM_UM_HARDIRQ_H */

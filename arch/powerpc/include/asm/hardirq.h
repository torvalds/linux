#ifndef _ASM_POWERPC_HARDIRQ_H
#define _ASM_POWERPC_HARDIRQ_H

#include <linux/threads.h>
#include <linux/irq.h>

typedef struct {
	unsigned int __softirq_pending;
} ____cacheline_aligned irq_cpustat_t;

DECLARE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

#define __ARCH_IRQ_STAT

#define local_softirq_pending()	__get_cpu_var(irq_stat).__softirq_pending

static inline void ack_bad_irq(unsigned int irq)
{
	printk(KERN_CRIT "unexpected IRQ trap at vector %02x\n", irq);
}

#endif /* _ASM_POWERPC_HARDIRQ_H */

#ifdef __KERNEL__
#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/threads.h>
#include <linux/irq.h>

typedef struct {
	unsigned int __softirq_pending;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#if NR_IRQS > 256
#define HARDIRQ_BITS	9
#else
#define HARDIRQ_BITS	8
#endif

/*
 * The hardirq mask has to be large enough to have
 * space for potentially all IRQ sources in the system
 * nesting on a single CPU:
 */
#if (1 << HARDIRQ_BITS) < NR_IRQS
# error HARDIRQ_BITS is too low!
#endif

static inline void ack_bad_irq(int irq)
{
	printk(KERN_CRIT "unexpected IRQ trap at vector %02x\n", irq);
	BUG();
}

#endif /* __ASM_HARDIRQ_H */
#endif /* __KERNEL__ */

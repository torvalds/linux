/* hardirq.h: PA-RISC hard IRQ support.
 *
 * Copyright (C) 2001 Matthew Wilcox <matthew@wil.cx>
 * Copyright (C) 2013 Helge Deller <deller@gmx.de>
 */

#ifndef _PARISC_HARDIRQ_H
#define _PARISC_HARDIRQ_H

#include <linux/cache.h>
#include <linux/threads.h>
#include <linux/irq.h>

typedef struct {
	unsigned int __softirq_pending;
#ifdef CONFIG_DEBUG_STACKOVERFLOW
	unsigned int kernel_stack_usage;
#endif
#ifdef CONFIG_SMP
	unsigned int irq_resched_count;
	unsigned int irq_call_count;
#endif
	unsigned int irq_tlb_count;
} ____cacheline_aligned irq_cpustat_t;

DECLARE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

#define __ARCH_IRQ_STAT
#define __IRQ_STAT(cpu, member) (irq_stat[cpu].member)
#define inc_irq_stat(member)	this_cpu_inc(irq_stat.member)
#define local_softirq_pending()	this_cpu_read(irq_stat.__softirq_pending)

#define __ARCH_SET_SOFTIRQ_PENDING

#define set_softirq_pending(x)	\
		this_cpu_write(irq_stat.__softirq_pending, (x))
#define or_softirq_pending(x)	this_cpu_or(irq_stat.__softirq_pending, (x))

#define ack_bad_irq(irq) WARN(1, "unexpected IRQ trap at vector %02x\n", irq)

#endif /* _PARISC_HARDIRQ_H */

#ifndef _ASM_X86_HARDIRQ_64_H
#define _ASM_X86_HARDIRQ_64_H

#include <linux/threads.h>
#include <linux/irq.h>

typedef struct {
	unsigned int __softirq_pending;
	unsigned int __nmi_count;	/* arch dependent */
	unsigned int apic_timer_irqs;	/* arch dependent */
	unsigned int irq0_irqs;
	unsigned int irq_resched_count;
	unsigned int irq_call_count;
	unsigned int irq_tlb_count;
	unsigned int irq_thermal_count;
	unsigned int irq_spurious_count;
	unsigned int irq_threshold_count;
} ____cacheline_aligned irq_cpustat_t;

DECLARE_PER_CPU(irq_cpustat_t, irq_stat);

/* We can have at most NR_VECTORS irqs routed to a cpu at a time */
#define MAX_HARDIRQS_PER_CPU NR_VECTORS

#define __ARCH_IRQ_STAT

#define inc_irq_stat(member)	percpu_add(irq_stat.member, 1)

#define local_softirq_pending()	percpu_read(irq_stat.__softirq_pending)

#define __ARCH_SET_SOFTIRQ_PENDING

#define set_softirq_pending(x)	percpu_write(irq_stat.__softirq_pending, (x))
#define or_softirq_pending(x)	percpu_or(irq_stat.__softirq_pending, (x))

extern void ack_bad_irq(unsigned int irq);

#endif /* _ASM_X86_HARDIRQ_64_H */

#ifndef _ASM_X86_HARDIRQ_32_H
#define _ASM_X86_HARDIRQ_32_H

#include <linux/threads.h>
#include <linux/irq.h>

typedef struct {
	unsigned int __softirq_pending;
	unsigned long idle_timestamp;
	unsigned int __nmi_count;	/* arch dependent */
	unsigned int apic_timer_irqs;	/* arch dependent */
	unsigned int irq0_irqs;
	unsigned int irq_resched_count;
	unsigned int irq_call_count;
	unsigned int irq_tlb_count;
	unsigned int irq_thermal_count;
	unsigned int irq_spurious_count;
} ____cacheline_aligned irq_cpustat_t;

DECLARE_PER_CPU(irq_cpustat_t, irq_stat);

#define __ARCH_IRQ_STAT
#define __IRQ_STAT(cpu, member) (per_cpu(irq_stat, cpu).member)

#define inc_irq_stat(member)	(__get_cpu_var(irq_stat).member++)

void ack_bad_irq(unsigned int irq);
#include <linux/irq_cpustat.h>

#endif /* _ASM_X86_HARDIRQ_32_H */

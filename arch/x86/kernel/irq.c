/*
 * Common interrupt code for 32 and 64 bit
 */
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>
#include <linux/smp.h>

#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/irq.h>

atomic_t irq_err_count;

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	printk(KERN_ERR "unexpected IRQ trap at vector %02x\n", irq);

#ifdef CONFIG_X86_LOCAL_APIC
	/*
	 * Currently unexpected vectors happen only on SMP and APIC.
	 * We _must_ ack these because every local APIC has only N
	 * irq slots per priority level, and a 'hanging, unacked' IRQ
	 * holds up an irq slot - in excessive cases (when multiple
	 * unexpected vectors occur) that might lock up the APIC
	 * completely.
	 * But only ack when the APIC is enabled -AK
	 */
	if (cpu_has_apic)
		ack_APIC_irq();
#endif
}

#ifdef CONFIG_X86_32
# define irq_stats(x)		(&per_cpu(irq_stat, x))
#else
# define irq_stats(x)		cpu_pda(x)
#endif
/*
 * /proc/interrupts printing:
 */
static int show_other_interrupts(struct seq_file *p)
{
	int j;

	seq_printf(p, "NMI: ");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->__nmi_count);
	seq_printf(p, "  Non-maskable interrupts\n");
#ifdef CONFIG_X86_LOCAL_APIC
	seq_printf(p, "LOC: ");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->apic_timer_irqs);
	seq_printf(p, "  Local timer interrupts\n");
#endif
#ifdef CONFIG_SMP
	seq_printf(p, "RES: ");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_resched_count);
	seq_printf(p, "  Rescheduling interrupts\n");
	seq_printf(p, "CAL: ");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_call_count);
	seq_printf(p, "  Function call interrupts\n");
	seq_printf(p, "TLB: ");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_tlb_count);
	seq_printf(p, "  TLB shootdowns\n");
#endif
#ifdef CONFIG_X86_MCE
	seq_printf(p, "TRM: ");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_thermal_count);
	seq_printf(p, "  Thermal event interrupts\n");
# ifdef CONFIG_X86_64
	seq_printf(p, "THR: ");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_threshold_count);
	seq_printf(p, "  Threshold APIC interrupts\n");
# endif
#endif
#ifdef CONFIG_X86_LOCAL_APIC
	seq_printf(p, "SPU: ");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_spurious_count);
	seq_printf(p, "  Spurious interrupts\n");
#endif
	seq_printf(p, "ERR: %10u\n", atomic_read(&irq_err_count));
#if defined(CONFIG_X86_IO_APIC)
	seq_printf(p, "MIS: %10u\n", atomic_read(&irq_mis_count));
#endif
	return 0;
}

int show_interrupts(struct seq_file *p, void *v)
{
	unsigned long flags, any_count = 0;
	int i = *(loff_t *) v, j;
	struct irqaction *action;
	struct irq_desc *desc;

	if (i > nr_irqs)
		return 0;

	if (i == nr_irqs)
		return show_other_interrupts(p);

	/* print header */
	if (i == 0) {
		seq_printf(p, "           ");
		for_each_online_cpu(j)
			seq_printf(p, "CPU%-8d", j);
		seq_putc(p, '\n');
	}

	desc = irq_to_desc(i);
	if (!desc)
		return 0;

	spin_lock_irqsave(&desc->lock, flags);
#ifndef CONFIG_SMP
	any_count = kstat_irqs(i);
#else
	for_each_online_cpu(j)
		any_count |= kstat_irqs_cpu(i, j);
#endif
	action = desc->action;
	if (!action && !any_count)
		goto out;

	seq_printf(p, "%3d: ", i);
#ifndef CONFIG_SMP
	seq_printf(p, "%10u ", kstat_irqs(i));
#else
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", kstat_irqs_cpu(i, j));
#endif
	seq_printf(p, " %8s", desc->chip->name);
	seq_printf(p, "-%-8s", desc->name);

	if (action) {
		seq_printf(p, "  %s", action->name);
		while ((action = action->next) != NULL)
			seq_printf(p, ", %s", action->name);
	}

	seq_putc(p, '\n');
out:
	spin_unlock_irqrestore(&desc->lock, flags);
	return 0;
}

/*
 * /proc/stat helpers
 */
u64 arch_irq_stat_cpu(unsigned int cpu)
{
	u64 sum = irq_stats(cpu)->__nmi_count;

#ifdef CONFIG_X86_LOCAL_APIC
	sum += irq_stats(cpu)->apic_timer_irqs;
#endif
#ifdef CONFIG_SMP
	sum += irq_stats(cpu)->irq_resched_count;
	sum += irq_stats(cpu)->irq_call_count;
	sum += irq_stats(cpu)->irq_tlb_count;
#endif
#ifdef CONFIG_X86_MCE
	sum += irq_stats(cpu)->irq_thermal_count;
# ifdef CONFIG_X86_64
	sum += irq_stats(cpu)->irq_threshold_count;
#endif
#endif
#ifdef CONFIG_X86_LOCAL_APIC
	sum += irq_stats(cpu)->irq_spurious_count;
#endif
	return sum;
}

u64 arch_irq_stat(void)
{
	u64 sum = atomic_read(&irq_err_count);

#ifdef CONFIG_X86_IO_APIC
	sum += atomic_read(&irq_mis_count);
#endif
	return sum;
}

EXPORT_SYMBOL_GPL(vector_used_by_percpu_irq);

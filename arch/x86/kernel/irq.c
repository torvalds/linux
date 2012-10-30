/*
 * Common interrupt code for 32 and 64 bit
 */
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/of.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/ftrace.h>
#include <linux/delay.h>
#include <linux/export.h>

#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/irq.h>
#include <asm/idle.h>
#include <asm/mce.h>
#include <asm/hw_irq.h>

atomic_t irq_err_count;

/* Function pointer for generic interrupt vector handling */
void (*x86_platform_ipi_callback)(void) = NULL;

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	if (printk_ratelimit())
		pr_err("unexpected IRQ trap at vector %02x\n", irq);

	/*
	 * Currently unexpected vectors happen only on SMP and APIC.
	 * We _must_ ack these because every local APIC has only N
	 * irq slots per priority level, and a 'hanging, unacked' IRQ
	 * holds up an irq slot - in excessive cases (when multiple
	 * unexpected vectors occur) that might lock up the APIC
	 * completely.
	 * But only ack when the APIC is enabled -AK
	 */
	ack_APIC_irq();
}

#define irq_stats(x)		(&per_cpu(irq_stat, x))
/*
 * /proc/interrupts printing for arch specific interrupts
 */
int arch_show_interrupts(struct seq_file *p, int prec)
{
	int j;

	seq_printf(p, "%*s: ", prec, "NMI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->__nmi_count);
	seq_printf(p, "  Non-maskable interrupts\n");
#ifdef CONFIG_X86_LOCAL_APIC
	seq_printf(p, "%*s: ", prec, "LOC");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->apic_timer_irqs);
	seq_printf(p, "  Local timer interrupts\n");

	seq_printf(p, "%*s: ", prec, "SPU");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_spurious_count);
	seq_printf(p, "  Spurious interrupts\n");
	seq_printf(p, "%*s: ", prec, "PMI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->apic_perf_irqs);
	seq_printf(p, "  Performance monitoring interrupts\n");
	seq_printf(p, "%*s: ", prec, "IWI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->apic_irq_work_irqs);
	seq_printf(p, "  IRQ work interrupts\n");
	seq_printf(p, "%*s: ", prec, "RTR");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->icr_read_retry_count);
	seq_printf(p, "  APIC ICR read retries\n");
#endif
	if (x86_platform_ipi_callback) {
		seq_printf(p, "%*s: ", prec, "PLT");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", irq_stats(j)->x86_platform_ipis);
		seq_printf(p, "  Platform interrupts\n");
	}
#ifdef CONFIG_SMP
	seq_printf(p, "%*s: ", prec, "RES");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_resched_count);
	seq_printf(p, "  Rescheduling interrupts\n");
	seq_printf(p, "%*s: ", prec, "CAL");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_call_count -
					irq_stats(j)->irq_tlb_count);
	seq_printf(p, "  Function call interrupts\n");
	seq_printf(p, "%*s: ", prec, "TLB");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_tlb_count);
	seq_printf(p, "  TLB shootdowns\n");
#endif
#ifdef CONFIG_X86_THERMAL_VECTOR
	seq_printf(p, "%*s: ", prec, "TRM");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_thermal_count);
	seq_printf(p, "  Thermal event interrupts\n");
#endif
#ifdef CONFIG_X86_MCE_THRESHOLD
	seq_printf(p, "%*s: ", prec, "THR");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_threshold_count);
	seq_printf(p, "  Threshold APIC interrupts\n");
#endif
#ifdef CONFIG_X86_MCE
	seq_printf(p, "%*s: ", prec, "MCE");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(mce_exception_count, j));
	seq_printf(p, "  Machine check exceptions\n");
	seq_printf(p, "%*s: ", prec, "MCP");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(mce_poll_count, j));
	seq_printf(p, "  Machine check polls\n");
#endif
	seq_printf(p, "%*s: %10u\n", prec, "ERR", atomic_read(&irq_err_count));
#if defined(CONFIG_X86_IO_APIC)
	seq_printf(p, "%*s: %10u\n", prec, "MIS", atomic_read(&irq_mis_count));
#endif
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
	sum += irq_stats(cpu)->irq_spurious_count;
	sum += irq_stats(cpu)->apic_perf_irqs;
	sum += irq_stats(cpu)->apic_irq_work_irqs;
	sum += irq_stats(cpu)->icr_read_retry_count;
#endif
	if (x86_platform_ipi_callback)
		sum += irq_stats(cpu)->x86_platform_ipis;
#ifdef CONFIG_SMP
	sum += irq_stats(cpu)->irq_resched_count;
	sum += irq_stats(cpu)->irq_call_count;
#endif
#ifdef CONFIG_X86_THERMAL_VECTOR
	sum += irq_stats(cpu)->irq_thermal_count;
#endif
#ifdef CONFIG_X86_MCE_THRESHOLD
	sum += irq_stats(cpu)->irq_threshold_count;
#endif
#ifdef CONFIG_X86_MCE
	sum += per_cpu(mce_exception_count, cpu);
	sum += per_cpu(mce_poll_count, cpu);
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


/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
unsigned int __irq_entry do_IRQ(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	/* high bit used in ret_from_ code  */
	unsigned vector = ~regs->orig_ax;
	unsigned irq;

	irq_enter();
	exit_idle();

	irq = __this_cpu_read(vector_irq[vector]);

	if (!handle_irq(irq, regs)) {
		ack_APIC_irq();

		if (printk_ratelimit())
			pr_emerg("%s: %d.%d No irq handler for vector (irq %d)\n",
				__func__, smp_processor_id(), vector, irq);
	}

	irq_exit();

	set_irq_regs(old_regs);
	return 1;
}

/*
 * Handler for X86_PLATFORM_IPI_VECTOR.
 */
void smp_x86_platform_ipi(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	ack_APIC_irq();

	irq_enter();

	exit_idle();

	inc_irq_stat(x86_platform_ipis);

	if (x86_platform_ipi_callback)
		x86_platform_ipi_callback();

	irq_exit();

	set_irq_regs(old_regs);
}

EXPORT_SYMBOL_GPL(vector_used_by_percpu_irq);

#ifdef CONFIG_HOTPLUG_CPU
/* A cpu has been removed from cpu_online_mask.  Reset irq affinities. */
void fixup_irqs(void)
{
	unsigned int irq, vector;
	static int warned;
	struct irq_desc *desc;
	struct irq_data *data;
	struct irq_chip *chip;

	for_each_irq_desc(irq, desc) {
		int break_affinity = 0;
		int set_affinity = 1;
		const struct cpumask *affinity;

		if (!desc)
			continue;
		if (irq == 2)
			continue;

		/* interrupt's are disabled at this point */
		raw_spin_lock(&desc->lock);

		data = irq_desc_get_irq_data(desc);
		affinity = data->affinity;
		if (!irq_has_action(irq) || irqd_is_per_cpu(data) ||
		    cpumask_subset(affinity, cpu_online_mask)) {
			raw_spin_unlock(&desc->lock);
			continue;
		}

		/*
		 * Complete the irq move. This cpu is going down and for
		 * non intr-remapping case, we can't wait till this interrupt
		 * arrives at this cpu before completing the irq move.
		 */
		irq_force_complete_move(irq);

		if (cpumask_any_and(affinity, cpu_online_mask) >= nr_cpu_ids) {
			break_affinity = 1;
			affinity = cpu_online_mask;
		}

		chip = irq_data_get_irq_chip(data);
		if (!irqd_can_move_in_process_context(data) && chip->irq_mask)
			chip->irq_mask(data);

		if (chip->irq_set_affinity)
			chip->irq_set_affinity(data, affinity, true);
		else if (!(warned++))
			set_affinity = 0;

		/*
		 * We unmask if the irq was not marked masked by the
		 * core code. That respects the lazy irq disable
		 * behaviour.
		 */
		if (!irqd_can_move_in_process_context(data) &&
		    !irqd_irq_masked(data) && chip->irq_unmask)
			chip->irq_unmask(data);

		raw_spin_unlock(&desc->lock);

		if (break_affinity && set_affinity)
			pr_notice("Broke affinity for irq %i\n", irq);
		else if (!set_affinity)
			pr_notice("Cannot set affinity for irq %i\n", irq);
	}

	/*
	 * We can remove mdelay() and then send spuriuous interrupts to
	 * new cpu targets for all the irqs that were handled previously by
	 * this cpu. While it works, I have seen spurious interrupt messages
	 * (nothing wrong but still...).
	 *
	 * So for now, retain mdelay(1) and check the IRR and then send those
	 * interrupts to new targets as this cpu is already offlined...
	 */
	mdelay(1);

	for (vector = FIRST_EXTERNAL_VECTOR; vector < NR_VECTORS; vector++) {
		unsigned int irr;

		if (__this_cpu_read(vector_irq[vector]) < 0)
			continue;

		irr = apic_read(APIC_IRR + (vector / 32 * 0x10));
		if (irr  & (1 << (vector % 32))) {
			irq = __this_cpu_read(vector_irq[vector]);

			desc = irq_to_desc(irq);
			data = irq_desc_get_irq_data(desc);
			chip = irq_data_get_irq_chip(data);
			raw_spin_lock(&desc->lock);
			if (chip->irq_retrigger)
				chip->irq_retrigger(data);
			raw_spin_unlock(&desc->lock);
		}
		__this_cpu_write(vector_irq[vector], -1);
	}
}
#endif

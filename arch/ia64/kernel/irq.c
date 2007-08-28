/*
 *	linux/arch/ia64/kernel/irq.c
 *
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQs should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 *
 * Copyright (C) Ashok Raj<ashok.raj@intel.com>, Intel Corporation 2004
 *
 * 4/14/2004: Added code to handle cpu migration and do safe irq
 *			migration without losing interrupts for iosapic
 *			architecture.
 */

#include <asm/delay.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	printk(KERN_ERR "Unexpected irq vector 0x%x on CPU %u!\n", irq, smp_processor_id());
}

#ifdef CONFIG_IA64_GENERIC
ia64_vector __ia64_irq_to_vector(int irq)
{
	return irq_cfg[irq].vector;
}

unsigned int __ia64_local_vector_to_irq (ia64_vector vec)
{
	return __get_cpu_var(vector_irq)[vec];
}
#endif

/*
 * Interrupt statistics:
 */

atomic_t irq_err_count;

/*
 * /proc/interrupts printing:
 */

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction * action;
	unsigned long flags;

	if (i == 0) {
		seq_printf(p, "           ");
		for_each_online_cpu(j) {
			seq_printf(p, "CPU%d       ",j);
		}
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action)
			goto skip;
		seq_printf(p, "%3d: ",i);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for_each_online_cpu(j) {
			seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
		}
#endif
		seq_printf(p, " %14s", irq_desc[i].chip->name);
		seq_printf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);

		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	} else if (i == NR_IRQS)
		seq_printf(p, "ERR: %10u\n", atomic_read(&irq_err_count));
	return 0;
}

#ifdef CONFIG_SMP
static char irq_redir [NR_IRQS]; // = { [0 ... NR_IRQS-1] = 1 };

void set_irq_affinity_info (unsigned int irq, int hwid, int redir)
{
	cpumask_t mask = CPU_MASK_NONE;

	cpu_set(cpu_logical_id(hwid), mask);

	if (irq < NR_IRQS) {
		irq_desc[irq].affinity = mask;
		irq_redir[irq] = (char) (redir & 0xff);
	}
}

bool is_affinity_mask_valid(cpumask_t cpumask)
{
	if (ia64_platform_is("sn2")) {
		/* Only allow one CPU to be specified in the smp_affinity mask */
		if (cpus_weight(cpumask) != 1)
			return false;
	}
	return true;
}

#endif /* CONFIG_SMP */

#ifdef CONFIG_HOTPLUG_CPU
unsigned int vectors_in_migration[NR_IRQS];

/*
 * Since cpu_online_map is already updated, we just need to check for
 * affinity that has zeros
 */
static void migrate_irqs(void)
{
	cpumask_t	mask;
	irq_desc_t *desc;
	int 		irq, new_cpu;

	for (irq=0; irq < NR_IRQS; irq++) {
		desc = irq_desc + irq;

		if (desc->status == IRQ_DISABLED)
			continue;

		/*
		 * No handling for now.
		 * TBD: Implement a disable function so we can now
		 * tell CPU not to respond to these local intr sources.
		 * such as ITV,CPEI,MCA etc.
		 */
		if (desc->status == IRQ_PER_CPU)
			continue;

		cpus_and(mask, irq_desc[irq].affinity, cpu_online_map);
		if (any_online_cpu(mask) == NR_CPUS) {
			/*
			 * Save it for phase 2 processing
			 */
			vectors_in_migration[irq] = irq;

			new_cpu = any_online_cpu(cpu_online_map);
			mask = cpumask_of_cpu(new_cpu);

			/*
			 * Al three are essential, currently WARN_ON.. maybe panic?
			 */
			if (desc->chip && desc->chip->disable &&
				desc->chip->enable && desc->chip->set_affinity) {
				desc->chip->disable(irq);
				desc->chip->set_affinity(irq, mask);
				desc->chip->enable(irq);
			} else {
				WARN_ON((!(desc->chip) || !(desc->chip->disable) ||
						!(desc->chip->enable) ||
						!(desc->chip->set_affinity)));
			}
		}
	}
}

void fixup_irqs(void)
{
	unsigned int irq;
	extern void ia64_process_pending_intr(void);
	extern void ia64_disable_timer(void);
	extern volatile int time_keeper_id;

	ia64_disable_timer();

	/*
	 * Find a new timesync master
	 */
	if (smp_processor_id() == time_keeper_id) {
		time_keeper_id = first_cpu(cpu_online_map);
		printk ("CPU %d is now promoted to time-keeper master\n", time_keeper_id);
	}

	/*
	 * Phase 1: Locate IRQs bound to this cpu and
	 * relocate them for cpu removal.
	 */
	migrate_irqs();

	/*
	 * Phase 2: Perform interrupt processing for all entries reported in
	 * local APIC.
	 */
	ia64_process_pending_intr();

	/*
	 * Phase 3: Now handle any interrupts not captured in local APIC.
	 * This is to account for cases that device interrupted during the time the
	 * rte was being disabled and re-programmed.
	 */
	for (irq=0; irq < NR_IRQS; irq++) {
		if (vectors_in_migration[irq]) {
			struct pt_regs *old_regs = set_irq_regs(NULL);

			vectors_in_migration[irq]=0;
			generic_handle_irq(irq);
			set_irq_regs(old_regs);
		}
	}

	/*
	 * Now let processor die. We do irq disable and max_xtp() to
	 * ensure there is no more interrupts routed to this processor.
	 * But the local timer interrupt can have 1 pending which we
	 * take care in timer_interrupt().
	 */
	max_xtp();
	local_irq_disable();
}
#endif

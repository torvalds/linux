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

#include <asm/mca.h>

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
	return __this_cpu_read(vector_irq[vec]);
}
#endif

/*
 * Interrupt statistics:
 */

atomic_t irq_err_count;

/*
 * /proc/interrupts printing:
 */
int arch_show_interrupts(struct seq_file *p, int prec)
{
	seq_printf(p, "ERR: %10u\n", atomic_read(&irq_err_count));
	return 0;
}

#ifdef CONFIG_SMP
static char irq_redir [NR_IRQS]; // = { [0 ... NR_IRQS-1] = 1 };

void set_irq_affinity_info (unsigned int irq, int hwid, int redir)
{
	if (irq < NR_IRQS) {
		cpumask_copy(irq_get_affinity_mask(irq),
			     cpumask_of(cpu_logical_id(hwid)));
		irq_redir[irq] = (char) (redir & 0xff);
	}
}

bool is_affinity_mask_valid(const struct cpumask *cpumask)
{
	if (ia64_platform_is("sn2")) {
		/* Only allow one CPU to be specified in the smp_affinity mask */
		if (cpumask_weight(cpumask) != 1)
			return false;
	}
	return true;
}

#endif /* CONFIG_SMP */

int __init arch_early_irq_init(void)
{
	ia64_mca_irq_init();
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
unsigned int vectors_in_migration[NR_IRQS];

/*
 * Since cpu_online_mask is already updated, we just need to check for
 * affinity that has zeros
 */
static void migrate_irqs(void)
{
	int 		irq, new_cpu;

	for (irq=0; irq < NR_IRQS; irq++) {
		struct irq_desc *desc = irq_to_desc(irq);
		struct irq_data *data = irq_desc_get_irq_data(desc);
		struct irq_chip *chip = irq_data_get_irq_chip(data);

		if (irqd_irq_disabled(data))
			continue;

		/*
		 * No handling for now.
		 * TBD: Implement a disable function so we can now
		 * tell CPU not to respond to these local intr sources.
		 * such as ITV,CPEI,MCA etc.
		 */
		if (irqd_is_per_cpu(data))
			continue;

		if (cpumask_any_and(irq_data_get_affinity_mask(data),
				    cpu_online_mask) >= nr_cpu_ids) {
			/*
			 * Save it for phase 2 processing
			 */
			vectors_in_migration[irq] = irq;

			new_cpu = cpumask_any(cpu_online_mask);

			/*
			 * Al three are essential, currently WARN_ON.. maybe panic?
			 */
			if (chip && chip->irq_disable &&
				chip->irq_enable && chip->irq_set_affinity) {
				chip->irq_disable(data);
				chip->irq_set_affinity(data,
						       cpumask_of(new_cpu), false);
				chip->irq_enable(data);
			} else {
				WARN_ON((!chip || !chip->irq_disable ||
					 !chip->irq_enable ||
					 !chip->irq_set_affinity));
			}
		}
	}
}

void fixup_irqs(void)
{
	unsigned int irq;
	extern void ia64_process_pending_intr(void);
	extern volatile int time_keeper_id;

	/* Mask ITV to disable timer */
	ia64_set_itv(1 << 16);

	/*
	 * Find a new timesync master
	 */
	if (smp_processor_id() == time_keeper_id) {
		time_keeper_id = cpumask_first(cpu_online_mask);
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

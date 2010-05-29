/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/uaccess.h>
#include <hv/drv_pcie_rc_intf.h>

/*
 * The set of interrupts we enable for raw_local_irq_enable().
 * This is initialized to have just a single interrupt that the kernel
 * doesn't actually use as a sentinel.  During kernel init,
 * interrupts are added as the kernel gets prepared to support them.
 * NOTE: we could probably initialize them all statically up front.
 */
DEFINE_PER_CPU(unsigned long long, interrupts_enabled_mask) =
  INITIAL_INTERRUPTS_ENABLED;
EXPORT_PER_CPU_SYMBOL(interrupts_enabled_mask);

/* Define per-tile device interrupt state */
DEFINE_PER_CPU(HV_IntrState, dev_intr_state);

DEFINE_PER_CPU(irq_cpustat_t, irq_stat) ____cacheline_internodealigned_in_smp;
EXPORT_PER_CPU_SYMBOL(irq_stat);



/*
 * Interrupt dispatcher, invoked upon a hypervisor device interrupt downcall
 */
void tile_dev_intr(struct pt_regs *regs, int intnum)
{
	int irq;

	/*
	 * Get the device interrupt pending mask from where the hypervisor
	 * has tucked it away for us.
	 */
	unsigned long pending_dev_intr_mask = __insn_mfspr(SPR_SYSTEM_SAVE_1_3);


	/* Track time spent here in an interrupt context. */
	struct pt_regs *old_regs = set_irq_regs(regs);
	irq_enter();

#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* Debugging check for stack overflow: less than 1/8th stack free? */
	{
		long sp = stack_pointer - (long) current_thread_info();
		if (unlikely(sp < (sizeof(struct thread_info) + STACK_WARN))) {
			printk(KERN_EMERG "tile_dev_intr: "
			       "stack overflow: %ld\n",
			       sp - sizeof(struct thread_info));
			dump_stack();
		}
	}
#endif

	for (irq = 0; pending_dev_intr_mask; ++irq) {
		if (pending_dev_intr_mask & 0x1) {
			generic_handle_irq(irq);

			/* Count device irqs; IPIs are counted elsewhere. */
			if (irq > HV_MAX_IPI_INTERRUPT)
				__get_cpu_var(irq_stat).irq_dev_intr_count++;
		}
		pending_dev_intr_mask >>= 1;
	}

	/*
	 * Track time spent against the current process again and
	 * process any softirqs if they are waiting.
	 */
	irq_exit();
	set_irq_regs(old_regs);
}


/* Mask an interrupt. */
static void hv_dev_irq_mask(unsigned int irq)
{
	HV_IntrState *p_intr_state = &__get_cpu_var(dev_intr_state);
	hv_disable_intr(p_intr_state, 1 << irq);
}

/* Unmask an interrupt. */
static void hv_dev_irq_unmask(unsigned int irq)
{
	/* Re-enable the hypervisor to generate interrupts. */
	HV_IntrState *p_intr_state = &__get_cpu_var(dev_intr_state);
	hv_enable_intr(p_intr_state, 1 << irq);
}

/*
 * The HV doesn't latch incoming interrupts while an interrupt is
 * disabled, so we need to reenable interrupts before running the
 * handler.
 *
 * ISSUE: Enabling the interrupt this early avoids any race conditions
 * but introduces the possibility of nested interrupt stack overflow.
 * An imminent change to the HV IRQ model will fix this.
 */
static void hv_dev_irq_ack(unsigned int irq)
{
	hv_dev_irq_unmask(irq);
}

/*
 * Since ack() reenables interrupts, there's nothing to do at eoi().
 */
static void hv_dev_irq_eoi(unsigned int irq)
{
}

static struct irq_chip hv_dev_irq_chip = {
	.typename = "hv_dev_irq_chip",
	.ack = hv_dev_irq_ack,
	.mask = hv_dev_irq_mask,
	.unmask = hv_dev_irq_unmask,
	.eoi = hv_dev_irq_eoi,
};

static struct irqaction resched_action = {
	.handler = handle_reschedule_ipi,
	.name = "resched",
	.dev_id = handle_reschedule_ipi /* unique token */,
};

void __init init_IRQ(void)
{
	/* Bind IPI irqs. Does this belong somewhere else in init? */
	tile_irq_activate(IRQ_RESCHEDULE);
	BUG_ON(setup_irq(IRQ_RESCHEDULE, &resched_action));
}

void __cpuinit init_per_tile_IRQs(void)
{
	int rc;

	/* Set the pointer to the per-tile device interrupt state. */
	HV_IntrState *sv_ptr = &__get_cpu_var(dev_intr_state);
	rc = hv_dev_register_intr_state(sv_ptr);
	if (rc != HV_OK)
		panic("hv_dev_register_intr_state: error %d", rc);

}

void tile_irq_activate(unsigned int irq)
{
	/*
	 * Paravirtualized drivers can call up to the HV to find out
	 * which irq they're associated with.  The HV interface
	 * doesn't provide a generic call for discovering all valid
	 * IRQs, so drivers must call this method to initialize newly
	 * discovered IRQs.
	 *
	 * We could also just initialize all 32 IRQs at startup, but
	 * doing so would lead to a kernel fault if an unexpected
	 * interrupt fires and jumps to a NULL action.  By defering
	 * the set_irq_chip_and_handler() call, unexpected IRQs are
	 * handled properly by handle_bad_irq().
	 */
	hv_dev_irq_mask(irq);
	set_irq_chip_and_handler(irq, &hv_dev_irq_chip, handle_percpu_irq);
}

void ack_bad_irq(unsigned int irq)
{
	printk(KERN_ERR "unexpected IRQ trap at vector %02x\n", irq);
}

/*
 * Generic, controller-independent functions:
 */

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction *action;
	unsigned long flags;

	if (i == 0) {
		seq_printf(p, "           ");
		for (j = 0; j < NR_CPUS; j++)
			if (cpu_online(j))
				seq_printf(p, "CPU%-8d", j);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		raw_spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action)
			goto skip;
		seq_printf(p, "%3d: ", i);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", kstat_irqs_cpu(i, j));
#endif
		seq_printf(p, " %14s", irq_desc[i].chip->typename);
		seq_printf(p, "  %s", action->name);

		for (action = action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);

		seq_putc(p, '\n');
skip:
		raw_spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	}
	return 0;
}

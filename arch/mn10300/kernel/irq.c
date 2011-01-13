/* MN10300 Arch-specific interrupt handling
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>
#include <linux/cpumask.h>
#include <asm/setup.h>
#include <asm/serial-regs.h>

unsigned long __mn10300_irq_enabled_epsw[NR_CPUS] __cacheline_aligned_in_smp = {
	[0 ... NR_CPUS - 1] = EPSW_IE | EPSW_IM_7
};
EXPORT_SYMBOL(__mn10300_irq_enabled_epsw);

#ifdef CONFIG_SMP
static char irq_affinity_online[NR_IRQS] = {
	[0 ... NR_IRQS - 1] = 0
};

#define NR_IRQ_WORDS	((NR_IRQS + 31) / 32)
static unsigned long irq_affinity_request[NR_IRQ_WORDS] = {
	[0 ... NR_IRQ_WORDS - 1] = 0
};
#endif  /* CONFIG_SMP */

atomic_t irq_err_count;

/*
 * MN10300 interrupt controller operations
 */
static void mn10300_cpupic_ack(unsigned int irq)
{
	unsigned long flags;
	u16 tmp;

	flags = arch_local_cli_save();
	GxICR_u8(irq) = GxICR_DETECT;
	tmp = GxICR(irq);
	arch_local_irq_restore(flags);
}

static void __mask_and_set_icr(unsigned int irq,
			       unsigned int mask, unsigned int set)
{
	unsigned long flags;
	u16 tmp;

	flags = arch_local_cli_save();
	tmp = GxICR(irq);
	GxICR(irq) = (tmp & mask) | set;
	tmp = GxICR(irq);
	arch_local_irq_restore(flags);
}

static void mn10300_cpupic_mask(unsigned int irq)
{
	__mask_and_set_icr(irq, GxICR_LEVEL, 0);
}

static void mn10300_cpupic_mask_ack(unsigned int irq)
{
#ifdef CONFIG_SMP
	unsigned long flags;
	u16 tmp;

	flags = arch_local_cli_save();

	if (!test_and_clear_bit(irq, irq_affinity_request)) {
		tmp = GxICR(irq);
		GxICR(irq) = (tmp & GxICR_LEVEL) | GxICR_DETECT;
		tmp = GxICR(irq);
	} else {
		u16 tmp2;
		tmp = GxICR(irq);
		GxICR(irq) = (tmp & GxICR_LEVEL);
		tmp2 = GxICR(irq);

		irq_affinity_online[irq] =
			any_online_cpu(*irq_desc[irq].affinity);
		CROSS_GxICR(irq, irq_affinity_online[irq]) =
			(tmp & (GxICR_LEVEL | GxICR_ENABLE)) | GxICR_DETECT;
		tmp = CROSS_GxICR(irq, irq_affinity_online[irq]);
	}

	arch_local_irq_restore(flags);
#else  /* CONFIG_SMP */
	__mask_and_set_icr(irq, GxICR_LEVEL, GxICR_DETECT);
#endif /* CONFIG_SMP */
}

static void mn10300_cpupic_unmask(unsigned int irq)
{
	__mask_and_set_icr(irq, GxICR_LEVEL, GxICR_ENABLE);
}

static void mn10300_cpupic_unmask_clear(unsigned int irq)
{
	/* the MN10300 PIC latches its interrupt request bit, even after the
	 * device has ceased to assert its interrupt line and the interrupt
	 * channel has been disabled in the PIC, so for level-triggered
	 * interrupts we need to clear the request bit when we re-enable */
#ifdef CONFIG_SMP
	unsigned long flags;
	u16 tmp;

	flags = arch_local_cli_save();

	if (!test_and_clear_bit(irq, irq_affinity_request)) {
		tmp = GxICR(irq);
		GxICR(irq) = (tmp & GxICR_LEVEL) | GxICR_ENABLE | GxICR_DETECT;
		tmp = GxICR(irq);
	} else {
		tmp = GxICR(irq);

		irq_affinity_online[irq] = any_online_cpu(*irq_desc[irq].affinity);
		CROSS_GxICR(irq, irq_affinity_online[irq]) = (tmp & GxICR_LEVEL) | GxICR_ENABLE | GxICR_DETECT;
		tmp = CROSS_GxICR(irq, irq_affinity_online[irq]);
	}

	arch_local_irq_restore(flags);
#else  /* CONFIG_SMP */
	__mask_and_set_icr(irq, GxICR_LEVEL, GxICR_ENABLE | GxICR_DETECT);
#endif /* CONFIG_SMP */
}

#ifdef CONFIG_SMP
static int
mn10300_cpupic_setaffinity(unsigned int irq, const struct cpumask *mask)
{
	unsigned long flags;
	int err;

	flags = arch_local_cli_save();

	/* check irq no */
	switch (irq) {
	case TMJCIRQ:
	case RESCHEDULE_IPI:
	case CALL_FUNC_SINGLE_IPI:
	case LOCAL_TIMER_IPI:
	case FLUSH_CACHE_IPI:
	case CALL_FUNCTION_NMI_IPI:
	case GDB_NMI_IPI:
#ifdef CONFIG_MN10300_TTYSM0
	case SC0RXIRQ:
	case SC0TXIRQ:
#ifdef CONFIG_MN10300_TTYSM0_TIMER8
	case TM8IRQ:
#elif CONFIG_MN10300_TTYSM0_TIMER2
	case TM2IRQ:
#endif /* CONFIG_MN10300_TTYSM0_TIMER8 */
#endif /* CONFIG_MN10300_TTYSM0 */

#ifdef CONFIG_MN10300_TTYSM1
	case SC1RXIRQ:
	case SC1TXIRQ:
#ifdef CONFIG_MN10300_TTYSM1_TIMER12
	case TM12IRQ:
#elif CONFIG_MN10300_TTYSM1_TIMER9
	case TM9IRQ:
#elif CONFIG_MN10300_TTYSM1_TIMER3
	case TM3IRQ:
#endif /* CONFIG_MN10300_TTYSM1_TIMER12 */
#endif /* CONFIG_MN10300_TTYSM1 */

#ifdef CONFIG_MN10300_TTYSM2
	case SC2RXIRQ:
	case SC2TXIRQ:
	case TM10IRQ:
#endif /* CONFIG_MN10300_TTYSM2 */
		err = -1;
		break;

	default:
		set_bit(irq, irq_affinity_request);
		err = 0;
		break;
	}

	arch_local_irq_restore(flags);
	return err;
}
#endif /* CONFIG_SMP */

/*
 * MN10300 PIC level-triggered IRQ handling.
 *
 * The PIC has no 'ACK' function per se.  It is possible to clear individual
 * channel latches, but each latch relatches whether or not the channel is
 * masked, so we need to clear the latch when we unmask the channel.
 *
 * Also for this reason, we don't supply an ack() op (it's unused anyway if
 * mask_ack() is provided), and mask_ack() just masks.
 */
static struct irq_chip mn10300_cpu_pic_level = {
	.name		= "cpu_l",
	.disable	= mn10300_cpupic_mask,
	.enable		= mn10300_cpupic_unmask_clear,
	.ack		= NULL,
	.mask		= mn10300_cpupic_mask,
	.mask_ack	= mn10300_cpupic_mask,
	.unmask		= mn10300_cpupic_unmask_clear,
#ifdef CONFIG_SMP
	.set_affinity	= mn10300_cpupic_setaffinity,
#endif
};

/*
 * MN10300 PIC edge-triggered IRQ handling.
 *
 * We use the latch clearing function of the PIC as the 'ACK' function.
 */
static struct irq_chip mn10300_cpu_pic_edge = {
	.name		= "cpu_e",
	.disable	= mn10300_cpupic_mask,
	.enable		= mn10300_cpupic_unmask,
	.ack		= mn10300_cpupic_ack,
	.mask		= mn10300_cpupic_mask,
	.mask_ack	= mn10300_cpupic_mask_ack,
	.unmask		= mn10300_cpupic_unmask,
#ifdef CONFIG_SMP
	.set_affinity	= mn10300_cpupic_setaffinity,
#endif
};

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(int irq)
{
	printk(KERN_WARNING "unexpected IRQ trap at vector %02x\n", irq);
}

/*
 * change the level at which an IRQ executes
 * - must not be called whilst interrupts are being processed!
 */
void set_intr_level(int irq, u16 level)
{
	BUG_ON(in_interrupt());

	__mask_and_set_icr(irq, GxICR_ENABLE, level);
}

void mn10300_intc_set_level(unsigned int irq, unsigned int level)
{
	set_intr_level(irq, NUM2GxICR_LEVEL(level) & GxICR_LEVEL);
}

void mn10300_intc_clear(unsigned int irq)
{
	__mask_and_set_icr(irq, GxICR_LEVEL | GxICR_ENABLE, GxICR_DETECT);
}

void mn10300_intc_set(unsigned int irq)
{
	__mask_and_set_icr(irq, 0, GxICR_REQUEST | GxICR_DETECT);
}

void mn10300_intc_enable(unsigned int irq)
{
	mn10300_cpupic_unmask(irq);
}

void mn10300_intc_disable(unsigned int irq)
{
	mn10300_cpupic_mask(irq);
}

/*
 * mark an interrupt to be ACK'd after interrupt handlers have been run rather
 * than before
 * - see Documentation/mn10300/features.txt
 */
void mn10300_set_lateack_irq_type(int irq)
{
	set_irq_chip_and_handler(irq, &mn10300_cpu_pic_level,
				 handle_level_irq);
}

/*
 * initialise the interrupt system
 */
void __init init_IRQ(void)
{
	int irq;

	for (irq = 0; irq < NR_IRQS; irq++)
		if (irq_desc[irq].chip == &no_irq_chip)
			/* due to the PIC latching interrupt requests, even
			 * when the IRQ is disabled, IRQ_PENDING is superfluous
			 * and we can use handle_level_irq() for edge-triggered
			 * interrupts */
			set_irq_chip_and_handler(irq, &mn10300_cpu_pic_edge,
						 handle_level_irq);

	unit_init_IRQ();
}

/*
 * handle normal device IRQs
 */
asmlinkage void do_IRQ(void)
{
	unsigned long sp, epsw, irq_disabled_epsw, old_irq_enabled_epsw;
	unsigned int cpu_id = smp_processor_id();
	int irq;

	sp = current_stack_pointer();
	BUG_ON(sp - (sp & ~(THREAD_SIZE - 1)) < STACK_WARN);

	/* make sure local_irq_enable() doesn't muck up the interrupt priority
	 * setting in EPSW */
	old_irq_enabled_epsw = __mn10300_irq_enabled_epsw[cpu_id];
	local_save_flags(epsw);
	__mn10300_irq_enabled_epsw[cpu_id] = EPSW_IE | (EPSW_IM & epsw);
	irq_disabled_epsw = EPSW_IE | MN10300_CLI_LEVEL;

#ifdef CONFIG_MN10300_WD_TIMER
	__IRQ_STAT(cpu_id, __irq_count)++;
#endif

	irq_enter();

	for (;;) {
		/* ask the interrupt controller for the next IRQ to process
		 * - the result we get depends on EPSW.IM
		 */
		irq = IAGR & IAGR_GN;
		if (!irq)
			break;

		local_irq_restore(irq_disabled_epsw);

		generic_handle_irq(irq >> 2);

		/* restore IRQ controls for IAGR access */
		local_irq_restore(epsw);
	}

	__mn10300_irq_enabled_epsw[cpu_id] = old_irq_enabled_epsw;

	irq_exit();
}

/*
 * Display interrupt management information through /proc/interrupts
 */
int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j, cpu;
	struct irqaction *action;
	unsigned long flags;

	switch (i) {
		/* display column title bar naming CPUs */
	case 0:
		seq_printf(p, "           ");
		for (j = 0; j < NR_CPUS; j++)
			if (cpu_online(j))
				seq_printf(p, "CPU%d       ", j);
		seq_putc(p, '\n');
		break;

		/* display information rows, one per active CPU */
	case 1 ... NR_IRQS - 1:
		raw_spin_lock_irqsave(&irq_desc[i].lock, flags);

		action = irq_desc[i].action;
		if (action) {
			seq_printf(p, "%3d: ", i);
			for_each_present_cpu(cpu)
				seq_printf(p, "%10u ", kstat_irqs_cpu(i, cpu));

			if (i < NR_CPU_IRQS)
				seq_printf(p, " %14s.%u",
					   irq_desc[i].chip->name,
					   (GxICR(i) & GxICR_LEVEL) >>
					   GxICR_LEVEL_SHIFT);
			else
				seq_printf(p, " %14s",
					   irq_desc[i].chip->name);

			seq_printf(p, "  %s", action->name);

			for (action = action->next;
			     action;
			     action = action->next)
				seq_printf(p, ", %s", action->name);

			seq_putc(p, '\n');
		}

		raw_spin_unlock_irqrestore(&irq_desc[i].lock, flags);
		break;

		/* polish off with NMI and error counters */
	case NR_IRQS:
#ifdef CONFIG_MN10300_WD_TIMER
		seq_printf(p, "NMI: ");
		for (j = 0; j < NR_CPUS; j++)
			if (cpu_online(j))
				seq_printf(p, "%10u ", nmi_count(j));
		seq_putc(p, '\n');
#endif

		seq_printf(p, "ERR: %10u\n", atomic_read(&irq_err_count));
		break;
	}

	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
void migrate_irqs(void)
{
	irq_desc_t *desc;
	int irq;
	unsigned int self, new;
	unsigned long flags;

	self = smp_processor_id();
	for (irq = 0; irq < NR_IRQS; irq++) {
		desc = irq_desc + irq;

		if (desc->status == IRQ_PER_CPU)
			continue;

		if (cpu_isset(self, irq_desc[irq].affinity) &&
		    !cpus_intersects(irq_affinity[irq], cpu_online_map)) {
			int cpu_id;
			cpu_id = first_cpu(cpu_online_map);
			cpu_set(cpu_id, irq_desc[irq].affinity);
		}
		/* We need to operate irq_affinity_online atomically. */
		arch_local_cli_save(flags);
		if (irq_affinity_online[irq] == self) {
			u16 x, tmp;

			x = GxICR(irq);
			GxICR(irq) = x & GxICR_LEVEL;
			tmp = GxICR(irq);

			new = any_online_cpu(irq_desc[irq].affinity);
			irq_affinity_online[irq] = new;

			CROSS_GxICR(irq, new) =
				(x & GxICR_LEVEL) | GxICR_DETECT;
			tmp = CROSS_GxICR(irq, new);

			x &= GxICR_LEVEL | GxICR_ENABLE;
			if (GxICR(irq) & GxICR_REQUEST)
				x |= GxICR_REQUEST | GxICR_DETECT;
			CROSS_GxICR(irq, new) = x;
			tmp = CROSS_GxICR(irq, new);
		}
		arch_local_irq_restore(flags);
	}
}
#endif /* CONFIG_HOTPLUG_CPU */

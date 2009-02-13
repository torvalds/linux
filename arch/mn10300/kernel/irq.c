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
#include <asm/setup.h>

unsigned long __mn10300_irq_enabled_epsw = EPSW_IE | EPSW_IM_7;
EXPORT_SYMBOL(__mn10300_irq_enabled_epsw);

atomic_t irq_err_count;

/*
 * MN10300 interrupt controller operations
 */
static void mn10300_cpupic_ack(unsigned int irq)
{
	u16 tmp;
	*(volatile u8 *) &GxICR(irq) = GxICR_DETECT;
	tmp = GxICR(irq);
}

static void mn10300_cpupic_mask(unsigned int irq)
{
	u16 tmp = GxICR(irq);
	GxICR(irq) = (tmp & GxICR_LEVEL);
	tmp = GxICR(irq);
}

static void mn10300_cpupic_mask_ack(unsigned int irq)
{
	u16 tmp = GxICR(irq);
	GxICR(irq) = (tmp & GxICR_LEVEL) | GxICR_DETECT;
	tmp = GxICR(irq);
}

static void mn10300_cpupic_unmask(unsigned int irq)
{
	u16 tmp = GxICR(irq);
	GxICR(irq) = (tmp & GxICR_LEVEL) | GxICR_ENABLE;
	tmp = GxICR(irq);
}

static void mn10300_cpupic_unmask_clear(unsigned int irq)
{
	/* the MN10300 PIC latches its interrupt request bit, even after the
	 * device has ceased to assert its interrupt line and the interrupt
	 * channel has been disabled in the PIC, so for level-triggered
	 * interrupts we need to clear the request bit when we re-enable */
	u16 tmp = GxICR(irq);
	GxICR(irq) = (tmp & GxICR_LEVEL) | GxICR_ENABLE | GxICR_DETECT;
	tmp = GxICR(irq);
}

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
	u16 tmp;

	if (in_interrupt())
		BUG();

	tmp = GxICR(irq);
	GxICR(irq) = (tmp & GxICR_ENABLE) | level;
	tmp = GxICR(irq);
}

/*
 * mark an interrupt to be ACK'd after interrupt handlers have been run rather
 * than before
 * - see Documentation/mn10300/features.txt
 */
void set_intr_postackable(int irq)
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
		if (irq_desc[irq].chip == &no_irq_type)
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
	int irq;

	sp = current_stack_pointer();
	if (sp - (sp & ~(THREAD_SIZE - 1)) < STACK_WARN)
		BUG();

	/* make sure local_irq_enable() doesn't muck up the interrupt priority
	 * setting in EPSW */
	old_irq_enabled_epsw = __mn10300_irq_enabled_epsw;
	local_save_flags(epsw);
	__mn10300_irq_enabled_epsw = EPSW_IE | (EPSW_IM & epsw);
	irq_disabled_epsw = EPSW_IE | MN10300_CLI_LEVEL;

	__IRQ_STAT(smp_processor_id(), __irq_count)++;

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

	__mn10300_irq_enabled_epsw = old_irq_enabled_epsw;

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
		spin_lock_irqsave(&irq_desc[i].lock, flags);

		action = irq_desc[i].action;
		if (action) {
			seq_printf(p, "%3d: ", i);
			for_each_present_cpu(cpu)
				seq_printf(p, "%10u ", kstat_irqs_cpu(i, cpu));
			seq_printf(p, " %14s.%u", irq_desc[i].chip->name,
				   (GxICR(i) & GxICR_LEVEL) >>
				   GxICR_LEVEL_SHIFT);
			seq_printf(p, "  %s", action->name);

			for (action = action->next;
			     action;
			     action = action->next)
				seq_printf(p, ", %s", action->name);

			seq_putc(p, '\n');
		}

		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
		break;

		/* polish off with NMI and error counters */
	case NR_IRQS:
		seq_printf(p, "NMI: ");
		for (j = 0; j < NR_CPUS; j++)
			if (cpu_online(j))
				seq_printf(p, "%10u ", nmi_count(j));
		seq_putc(p, '\n');

		seq_printf(p, "ERR: %10u\n", atomic_read(&irq_err_count));
		break;
	}

	return 0;
}

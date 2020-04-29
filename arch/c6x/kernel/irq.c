// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2011-2012 Texas Instruments Incorporated
 *
 *  This borrows heavily from powerpc version, which is:
 *
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan <cort@fsmlabs.com>
 *    Copyright (C) 1996-2001 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 */
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/radix-tree.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>

#include <asm/megamod-pic.h>
#include <asm/special_insns.h>

unsigned long irq_err_count;

static DEFINE_RAW_SPINLOCK(core_irq_lock);

static void mask_core_irq(struct irq_data *data)
{
	unsigned int prio = data->hwirq;

	raw_spin_lock(&core_irq_lock);
	and_creg(IER, ~(1 << prio));
	raw_spin_unlock(&core_irq_lock);
}

static void unmask_core_irq(struct irq_data *data)
{
	unsigned int prio = data->hwirq;

	raw_spin_lock(&core_irq_lock);
	or_creg(IER, 1 << prio);
	raw_spin_unlock(&core_irq_lock);
}

static struct irq_chip core_chip = {
	.name		= "core",
	.irq_mask	= mask_core_irq,
	.irq_unmask	= unmask_core_irq,
};

static int prio_to_virq[NR_PRIORITY_IRQS];

asmlinkage void c6x_do_IRQ(unsigned int prio, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();

	generic_handle_irq(prio_to_virq[prio]);

	irq_exit();

	set_irq_regs(old_regs);
}

static struct irq_domain *core_domain;

static int core_domain_map(struct irq_domain *h, unsigned int virq,
			   irq_hw_number_t hw)
{
	if (hw < 4 || hw >= NR_PRIORITY_IRQS)
		return -EINVAL;

	prio_to_virq[hw] = virq;

	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_chip_and_handler(virq, &core_chip, handle_level_irq);
	return 0;
}

static const struct irq_domain_ops core_domain_ops = {
	.map = core_domain_map,
	.xlate = irq_domain_xlate_onecell,
};

void __init init_IRQ(void)
{
	struct device_node *np;

	/* Mask all priority IRQs */
	and_creg(IER, ~0xfff0);

	np = of_find_compatible_node(NULL, NULL, "ti,c64x+core-pic");
	if (np != NULL) {
		/* create the core host */
		core_domain = irq_domain_add_linear(np, NR_PRIORITY_IRQS,
						    &core_domain_ops, NULL);
		if (core_domain)
			irq_set_default_host(core_domain);
		of_node_put(np);
	}

	printk(KERN_INFO "Core interrupt controller initialized\n");

	/* now we're ready for other SoC controllers */
	megamod_pic_init();

	/* Clear all general IRQ flags */
	set_creg(ICR, 0xfff0);
}

void ack_bad_irq(int irq)
{
	printk(KERN_ERR "IRQ: spurious interrupt %d\n", irq);
	irq_err_count++;
}

int arch_show_interrupts(struct seq_file *p, int prec)
{
	seq_printf(p, "%*s: %10lu\n", prec, "Err", irq_err_count);
	return 0;
}

/*
 * Marvell Armada 370 and Armada XP SoC IRQ handling
 *
 * Copyright (C) 2012 Marvell
 *
 * Lior Amsalem <alior@marvell.com>
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 * Ben Dooks <ben.dooks@codethink.co.uk>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irqdomain.h>
#include <asm/mach/arch.h>
#include <asm/exception.h>

/* Interrupt Controller Registers Map */
#define ARMADA_370_XP_INT_SET_MASK_OFFS		(0x48)
#define ARMADA_370_XP_INT_CLEAR_MASK_OFFS	(0x4C)

#define ARMADA_370_XP_INT_SET_ENABLE_OFFS	(0x30)
#define ARMADA_370_XP_INT_CLEAR_ENABLE_OFFS	(0x34)

#define ARMADA_370_XP_CPU_INTACK_OFFS		(0x44)

#define ARMADA_370_XP_NR_IRQS			(115)

static void __iomem *per_cpu_int_base;
static void __iomem *main_int_base;
static struct irq_domain *armada_370_xp_mpic_domain;

static void armada_370_xp_irq_mask(struct irq_data *d)
{
	writel(irqd_to_hwirq(d),
	       per_cpu_int_base + ARMADA_370_XP_INT_SET_MASK_OFFS);
}

static void armada_370_xp_irq_unmask(struct irq_data *d)
{
	writel(irqd_to_hwirq(d),
	       per_cpu_int_base + ARMADA_370_XP_INT_CLEAR_MASK_OFFS);
}

static struct irq_chip armada_370_xp_irq_chip = {
	.name		= "armada_370_xp_irq",
	.irq_mask       = armada_370_xp_irq_mask,
	.irq_mask_ack   = armada_370_xp_irq_mask,
	.irq_unmask     = armada_370_xp_irq_unmask,
};

static int armada_370_xp_mpic_irq_map(struct irq_domain *h,
				      unsigned int virq, irq_hw_number_t hw)
{
	armada_370_xp_irq_mask(irq_get_irq_data(virq));
	writel(hw, main_int_base + ARMADA_370_XP_INT_SET_ENABLE_OFFS);

	irq_set_chip_and_handler(virq, &armada_370_xp_irq_chip,
				 handle_level_irq);
	irq_set_status_flags(virq, IRQ_LEVEL);
	set_irq_flags(virq, IRQF_VALID | IRQF_PROBE);

	return 0;
}

static struct irq_domain_ops armada_370_xp_mpic_irq_ops = {
	.map = armada_370_xp_mpic_irq_map,
	.xlate = irq_domain_xlate_onecell,
};

static int __init armada_370_xp_mpic_of_init(struct device_node *node,
					     struct device_node *parent)
{
	main_int_base = of_iomap(node, 0);
	per_cpu_int_base = of_iomap(node, 1);

	BUG_ON(!main_int_base);
	BUG_ON(!per_cpu_int_base);

	armada_370_xp_mpic_domain =
	    irq_domain_add_linear(node, ARMADA_370_XP_NR_IRQS,
				  &armada_370_xp_mpic_irq_ops, NULL);

	if (!armada_370_xp_mpic_domain)
		panic("Unable to add Armada_370_Xp MPIC irq domain (DT)\n");

	irq_set_default_host(armada_370_xp_mpic_domain);
	return 0;
}

asmlinkage void __exception_irq_entry armada_370_xp_handle_irq(struct pt_regs
							       *regs)
{
	u32 irqstat, irqnr;

	do {
		irqstat = readl_relaxed(per_cpu_int_base +
					ARMADA_370_XP_CPU_INTACK_OFFS);
		irqnr = irqstat & 0x3FF;

		if (irqnr < 1023) {
			irqnr =
			    irq_find_mapping(armada_370_xp_mpic_domain, irqnr);
			handle_IRQ(irqnr, regs);
			continue;
		}

		break;
	} while (1);
}

static const struct of_device_id mpic_of_match[] __initconst = {
	{.compatible = "marvell,mpic", .data = armada_370_xp_mpic_of_init},
	{},
};

void __init armada_370_xp_init_irq(void)
{
	of_irq_init(mpic_of_match);
}

/*
 * Xtensa built-in interrupt controller
 *
 * Copyright (C) 2002 - 2013 Tensilica, Inc.
 * Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Chris Zankel <chris@zankel.net>
 * Kevin Chea
 */

#include <linux/bits.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/xtensa-pic.h>
#include <linux/of.h>

/*
 * Device Tree IRQ specifier translation function which works with one or
 * two cell bindings. First cell value maps directly to the hwirq number.
 * Second cell if present specifies whether hwirq number is external (1) or
 * internal (0).
 */
static int xtensa_pic_irq_domain_xlate(struct irq_domain *d,
		struct device_node *ctrlr,
		const u32 *intspec, unsigned int intsize,
		unsigned long *out_hwirq, unsigned int *out_type)
{
	return xtensa_irq_domain_xlate(intspec, intsize,
			intspec[0], intspec[0],
			out_hwirq, out_type);
}

static const struct irq_domain_ops xtensa_irq_domain_ops = {
	.xlate = xtensa_pic_irq_domain_xlate,
	.map = xtensa_irq_map,
};

static void xtensa_irq_mask(struct irq_data *d)
{
	u32 irq_mask;

	irq_mask = xtensa_get_sr(intenable);
	irq_mask &= ~BIT(d->hwirq);
	xtensa_set_sr(irq_mask, intenable);
}

static void xtensa_irq_unmask(struct irq_data *d)
{
	u32 irq_mask;

	irq_mask = xtensa_get_sr(intenable);
	irq_mask |= BIT(d->hwirq);
	xtensa_set_sr(irq_mask, intenable);
}

static void xtensa_irq_ack(struct irq_data *d)
{
	xtensa_set_sr(BIT(d->hwirq), intclear);
}

static int xtensa_irq_retrigger(struct irq_data *d)
{
	unsigned int mask = BIT(d->hwirq);

	if (WARN_ON(mask & ~XCHAL_INTTYPE_MASK_SOFTWARE))
		return 0;
	xtensa_set_sr(mask, intset);
	return 1;
}

static struct irq_chip xtensa_irq_chip = {
	.name		= "xtensa",
	.irq_mask	= xtensa_irq_mask,
	.irq_unmask	= xtensa_irq_unmask,
	.irq_ack	= xtensa_irq_ack,
	.irq_retrigger	= xtensa_irq_retrigger,
};

int __init xtensa_pic_init_legacy(struct device_node *interrupt_parent)
{
	struct irq_domain *root_domain =
		irq_domain_add_legacy(NULL, NR_IRQS - 1, 1, 0,
				&xtensa_irq_domain_ops, &xtensa_irq_chip);
	irq_set_default_host(root_domain);
	return 0;
}

static int __init xtensa_pic_init(struct device_node *np,
		struct device_node *interrupt_parent)
{
	struct irq_domain *root_domain =
		irq_domain_add_linear(np, NR_IRQS, &xtensa_irq_domain_ops,
				&xtensa_irq_chip);
	irq_set_default_host(root_domain);
	return 0;
}
IRQCHIP_DECLARE(xtensa_irq_chip, "cdns,xtensa-pic", xtensa_pic_init);

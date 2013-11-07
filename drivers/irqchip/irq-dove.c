/*
 * Marvell Dove SoCs PMU IRQ chip driver.
 *
 * Andrew Lunn <andrew@lunn.ch>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/exception.h>
#include <asm/mach/irq.h>

#include "irqchip.h"

#define DOVE_PMU_IRQ_CAUSE	0x00
#define DOVE_PMU_IRQ_MASK	0x04

static void dove_pmu_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct irq_domain *d = irq_get_handler_data(irq);
	struct irq_chip_generic *gc = irq_get_domain_generic_chip(d, 0);
	u32 stat = readl_relaxed(gc->reg_base + DOVE_PMU_IRQ_CAUSE) &
		   gc->mask_cache;

	while (stat) {
		u32 hwirq = ffs(stat) - 1;

		generic_handle_irq(irq_find_mapping(d, gc->irq_base + hwirq));
		stat &= ~(1 << hwirq);
	}
}

static void pmu_irq_ack(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = ~d->mask;

	/*
	 * The PMU mask register is not RW0C: it is RW.	 This means that
	 * the bits take whatever value is written to them; if you write
	 * a '1', you will set the interrupt.
	 *
	 * Unfortunately this means there is NO race free way to clear
	 * these interrupts.
	 *
	 * So, let's structure the code so that the window is as small as
	 * possible.
	 */
	irq_gc_lock(gc);
	mask &= irq_reg_readl(gc->reg_base +  ct->regs.ack);
	irq_reg_writel(mask, gc->reg_base +  ct->regs.ack);
	irq_gc_unlock(gc);
}

static int __init dove_pmu_irq_init(struct device_node *np,
				    struct device_node *parent)
{
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	struct resource r;
	struct irq_domain *domain;
	struct irq_chip_generic *gc;
	int ret, irq, nrirqs = 7;

	domain = irq_domain_add_linear(np, nrirqs,
				       &irq_generic_chip_ops, NULL);
	if (!domain) {
		pr_err("%s: unable to add irq domain\n", np->name);
		return -ENOMEM;
	}

	ret = irq_alloc_domain_generic_chips(domain, nrirqs, 1, np->name,
			     handle_level_irq, clr, 0, IRQ_GC_INIT_MASK_CACHE);
	if (ret) {
		pr_err("%s: unable to alloc irq domain gc\n", np->name);
		return ret;
	}

	ret = of_address_to_resource(np, 0, &r);
	if (ret) {
		pr_err("%s: unable to get resource\n", np->name);
		return ret;
	}

	if (!request_mem_region(r.start, resource_size(&r), np->name)) {
		pr_err("%s: unable to request mem region\n", np->name);
		return -ENOMEM;
	}

	/* Map the parent interrupt for the chained handler */
	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0) {
		pr_err("%s: unable to parse irq\n", np->name);
		return -EINVAL;
	}

	gc = irq_get_domain_generic_chip(domain, 0);
	gc->reg_base = ioremap(r.start, resource_size(&r));
	if (!gc->reg_base) {
		pr_err("%s: unable to map resource\n", np->name);
		return -ENOMEM;
	}

	gc->chip_types[0].regs.ack = DOVE_PMU_IRQ_CAUSE;
	gc->chip_types[0].regs.mask = DOVE_PMU_IRQ_MASK;
	gc->chip_types[0].chip.irq_ack = pmu_irq_ack;
	gc->chip_types[0].chip.irq_mask = irq_gc_mask_clr_bit;
	gc->chip_types[0].chip.irq_unmask = irq_gc_mask_set_bit;

	/* mask and clear all interrupts */
	writel(0, gc->reg_base + DOVE_PMU_IRQ_MASK);
	writel(0, gc->reg_base + DOVE_PMU_IRQ_CAUSE);

	irq_set_handler_data(irq, domain);
	irq_set_chained_handler(irq, dove_pmu_irq_handler);

	return 0;
}
IRQCHIP_DECLARE(dove_pmu_intc,
		"marvell,dove-pmu-intc", dove_pmu_irq_init);

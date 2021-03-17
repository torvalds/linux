/*
 * Synopsys DW APB ICTL irqchip driver.
 *
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * based on GPL'ed 2.6 kernel sources
 *  (c) Marvell International Ltd.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>

#define APB_INT_ENABLE_L	0x00
#define APB_INT_ENABLE_H	0x04
#define APB_INT_MASK_L		0x08
#define APB_INT_MASK_H		0x0c
#define APB_INT_FINALSTATUS_L	0x30
#define APB_INT_FINALSTATUS_H	0x34
#define APB_INT_BASE_OFFSET	0x04

/* irq domain of the primary interrupt controller. */
static struct irq_domain *dw_apb_ictl_irq_domain;

static void __irq_entry dw_apb_ictl_handle_irq(struct pt_regs *regs)
{
	struct irq_domain *d = dw_apb_ictl_irq_domain;
	int n;

	for (n = 0; n < d->revmap_size; n += 32) {
		struct irq_chip_generic *gc = irq_get_domain_generic_chip(d, n);
		u32 stat = readl_relaxed(gc->reg_base + APB_INT_FINALSTATUS_L);

		while (stat) {
			u32 hwirq = ffs(stat) - 1;

			handle_domain_irq(d, hwirq, regs);
			stat &= ~BIT(hwirq);
		}
	}
}

static void dw_apb_ictl_handle_irq_cascaded(struct irq_desc *desc)
{
	struct irq_domain *d = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int n;

	chained_irq_enter(chip, desc);

	for (n = 0; n < d->revmap_size; n += 32) {
		struct irq_chip_generic *gc = irq_get_domain_generic_chip(d, n);
		u32 stat = readl_relaxed(gc->reg_base + APB_INT_FINALSTATUS_L);

		while (stat) {
			u32 hwirq = ffs(stat) - 1;
			u32 virq = irq_find_mapping(d, gc->irq_base + hwirq);

			generic_handle_irq(virq);
			stat &= ~BIT(hwirq);
		}
	}

	chained_irq_exit(chip, desc);
}

static int dw_apb_ictl_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs, void *arg)
{
	int i, ret;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	struct irq_fwspec *fwspec = arg;

	ret = irq_domain_translate_onecell(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++)
		irq_map_generic_chip(domain, virq + i, hwirq + i);

	return 0;
}

static const struct irq_domain_ops dw_apb_ictl_irq_domain_ops = {
	.translate = irq_domain_translate_onecell,
	.alloc = dw_apb_ictl_irq_domain_alloc,
	.free = irq_domain_free_irqs_top,
};

#ifdef CONFIG_PM
static void dw_apb_ictl_resume(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);

	irq_gc_lock(gc);
	writel_relaxed(~0, gc->reg_base + ct->regs.enable);
	writel_relaxed(*ct->mask_cache, gc->reg_base + ct->regs.mask);
	irq_gc_unlock(gc);
}
#else
#define dw_apb_ictl_resume	NULL
#endif /* CONFIG_PM */

static int __init dw_apb_ictl_init(struct device_node *np,
				   struct device_node *parent)
{
	const struct irq_domain_ops *domain_ops;
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	struct resource r;
	struct irq_domain *domain;
	struct irq_chip_generic *gc;
	void __iomem *iobase;
	int ret, nrirqs, parent_irq, i;
	u32 reg;

	if (!parent) {
		/* Used as the primary interrupt controller */
		parent_irq = 0;
		domain_ops = &dw_apb_ictl_irq_domain_ops;
	} else {
		/* Map the parent interrupt for the chained handler */
		parent_irq = irq_of_parse_and_map(np, 0);
		if (parent_irq <= 0) {
			pr_err("%pOF: unable to parse irq\n", np);
			return -EINVAL;
		}
		domain_ops = &irq_generic_chip_ops;
	}

	ret = of_address_to_resource(np, 0, &r);
	if (ret) {
		pr_err("%pOF: unable to get resource\n", np);
		return ret;
	}

	if (!request_mem_region(r.start, resource_size(&r), np->full_name)) {
		pr_err("%pOF: unable to request mem region\n", np);
		return -ENOMEM;
	}

	iobase = ioremap(r.start, resource_size(&r));
	if (!iobase) {
		pr_err("%pOF: unable to map resource\n", np);
		ret = -ENOMEM;
		goto err_release;
	}

	/*
	 * DW IP can be configured to allow 2-64 irqs. We can determine
	 * the number of irqs supported by writing into enable register
	 * and look for bits not set, as corresponding flip-flops will
	 * have been removed by synthesis tool.
	 */

	/* mask and enable all interrupts */
	writel_relaxed(~0, iobase + APB_INT_MASK_L);
	writel_relaxed(~0, iobase + APB_INT_MASK_H);
	writel_relaxed(~0, iobase + APB_INT_ENABLE_L);
	writel_relaxed(~0, iobase + APB_INT_ENABLE_H);

	reg = readl_relaxed(iobase + APB_INT_ENABLE_H);
	if (reg)
		nrirqs = 32 + fls(reg);
	else
		nrirqs = fls(readl_relaxed(iobase + APB_INT_ENABLE_L));

	domain = irq_domain_add_linear(np, nrirqs, domain_ops, NULL);
	if (!domain) {
		pr_err("%pOF: unable to add irq domain\n", np);
		ret = -ENOMEM;
		goto err_unmap;
	}

	ret = irq_alloc_domain_generic_chips(domain, 32, 1, np->name,
					     handle_level_irq, clr, 0,
					     IRQ_GC_INIT_MASK_CACHE);
	if (ret) {
		pr_err("%pOF: unable to alloc irq domain gc\n", np);
		goto err_unmap;
	}

	for (i = 0; i < DIV_ROUND_UP(nrirqs, 32); i++) {
		gc = irq_get_domain_generic_chip(domain, i * 32);
		gc->reg_base = iobase + i * APB_INT_BASE_OFFSET;
		gc->chip_types[0].regs.mask = APB_INT_MASK_L;
		gc->chip_types[0].regs.enable = APB_INT_ENABLE_L;
		gc->chip_types[0].chip.irq_mask = irq_gc_mask_set_bit;
		gc->chip_types[0].chip.irq_unmask = irq_gc_mask_clr_bit;
		gc->chip_types[0].chip.irq_resume = dw_apb_ictl_resume;
	}

	if (parent_irq) {
		irq_set_chained_handler_and_data(parent_irq,
				dw_apb_ictl_handle_irq_cascaded, domain);
	} else {
		dw_apb_ictl_irq_domain = domain;
		set_handle_irq(dw_apb_ictl_handle_irq);
	}

	return 0;

err_unmap:
	iounmap(iobase);
err_release:
	release_mem_region(r.start, resource_size(&r));
	return ret;
}
IRQCHIP_DECLARE(dw_apb_ictl,
		"snps,dw-apb-ictl", dw_apb_ictl_init);

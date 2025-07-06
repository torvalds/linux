// SPDX-License-Identifier: GPL-2.0-only
/*
 * Microchip External Interrupt Controller driver
 *
 * Copyright (C) 2021 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Claudiu Beznea <claudiu.beznea@microchip.com>
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/syscore_ops.h>

#include <dt-bindings/interrupt-controller/arm-gic.h>

#define MCHP_EIC_GFCS			(0x0)
#define MCHP_EIC_SCFG(x)		(0x4 + (x) * 0x4)
#define MCHP_EIC_SCFG_EN		BIT(16)
#define MCHP_EIC_SCFG_LVL		BIT(9)
#define MCHP_EIC_SCFG_POL		BIT(8)

#define MCHP_EIC_NIRQ			(2)

/*
 * struct mchp_eic - EIC private data structure
 * @base: base address
 * @clk: peripheral clock
 * @domain: irq domain
 * @irqs: irqs b/w eic and gic
 * @scfg: backup for scfg registers (necessary for backup and self-refresh mode)
 * @wakeup_source: wakeup source mask
 */
struct mchp_eic {
	void __iomem *base;
	struct clk *clk;
	struct irq_domain *domain;
	u32 irqs[MCHP_EIC_NIRQ];
	u32 scfg[MCHP_EIC_NIRQ];
	u32 wakeup_source;
};

static struct mchp_eic *eic;

static void mchp_eic_irq_mask(struct irq_data *d)
{
	unsigned int tmp;

	tmp = readl_relaxed(eic->base + MCHP_EIC_SCFG(d->hwirq));
	tmp &= ~MCHP_EIC_SCFG_EN;
	writel_relaxed(tmp, eic->base + MCHP_EIC_SCFG(d->hwirq));

	irq_chip_mask_parent(d);
}

static void mchp_eic_irq_unmask(struct irq_data *d)
{
	unsigned int tmp;

	tmp = readl_relaxed(eic->base + MCHP_EIC_SCFG(d->hwirq));
	tmp |= MCHP_EIC_SCFG_EN;
	writel_relaxed(tmp, eic->base + MCHP_EIC_SCFG(d->hwirq));

	irq_chip_unmask_parent(d);
}

static int mchp_eic_irq_set_type(struct irq_data *d, unsigned int type)
{
	unsigned int parent_irq_type;
	unsigned int tmp;

	tmp = readl_relaxed(eic->base + MCHP_EIC_SCFG(d->hwirq));
	tmp &= ~(MCHP_EIC_SCFG_POL | MCHP_EIC_SCFG_LVL);
	switch (type) {
	case IRQ_TYPE_LEVEL_HIGH:
		tmp |= MCHP_EIC_SCFG_POL | MCHP_EIC_SCFG_LVL;
		parent_irq_type = IRQ_TYPE_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		tmp |= MCHP_EIC_SCFG_LVL;
		parent_irq_type = IRQ_TYPE_LEVEL_HIGH;
		break;
	case IRQ_TYPE_EDGE_RISING:
		parent_irq_type = IRQ_TYPE_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		tmp |= MCHP_EIC_SCFG_POL;
		parent_irq_type = IRQ_TYPE_EDGE_RISING;
		break;
	default:
		return -EINVAL;
	}

	writel_relaxed(tmp, eic->base + MCHP_EIC_SCFG(d->hwirq));

	return irq_chip_set_type_parent(d, parent_irq_type);
}

static int mchp_eic_irq_set_wake(struct irq_data *d, unsigned int on)
{
	irq_set_irq_wake(eic->irqs[d->hwirq], on);
	if (on)
		eic->wakeup_source |= BIT(d->hwirq);
	else
		eic->wakeup_source &= ~BIT(d->hwirq);

	return 0;
}

static int mchp_eic_irq_suspend(void)
{
	unsigned int hwirq;

	for (hwirq = 0; hwirq < MCHP_EIC_NIRQ; hwirq++)
		eic->scfg[hwirq] = readl_relaxed(eic->base +
						 MCHP_EIC_SCFG(hwirq));

	if (!eic->wakeup_source)
		clk_disable_unprepare(eic->clk);

	return 0;
}

static void mchp_eic_irq_resume(void)
{
	unsigned int hwirq;

	if (!eic->wakeup_source)
		clk_prepare_enable(eic->clk);

	for (hwirq = 0; hwirq < MCHP_EIC_NIRQ; hwirq++)
		writel_relaxed(eic->scfg[hwirq], eic->base +
			       MCHP_EIC_SCFG(hwirq));
}

static struct syscore_ops mchp_eic_syscore_ops = {
	.suspend = mchp_eic_irq_suspend,
	.resume = mchp_eic_irq_resume,
};

static struct irq_chip mchp_eic_chip = {
	.name		= "eic",
	.flags		= IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_SET_TYPE_MASKED,
	.irq_mask	= mchp_eic_irq_mask,
	.irq_unmask	= mchp_eic_irq_unmask,
	.irq_set_type	= mchp_eic_irq_set_type,
	.irq_ack	= irq_chip_ack_parent,
	.irq_eoi	= irq_chip_eoi_parent,
	.irq_retrigger	= irq_chip_retrigger_hierarchy,
	.irq_set_wake	= mchp_eic_irq_set_wake,
};

static int mchp_eic_domain_alloc(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *data)
{
	struct irq_fwspec *fwspec = data;
	struct irq_fwspec parent_fwspec;
	irq_hw_number_t hwirq;
	unsigned int type;
	int ret;

	if (WARN_ON(nr_irqs != 1))
		return -EINVAL;

	ret = irq_domain_translate_twocell(domain, fwspec, &hwirq, &type);
	if (ret || hwirq >= MCHP_EIC_NIRQ)
		return ret;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_LEVEL_HIGH:
		break;
	case IRQ_TYPE_EDGE_FALLING:
		type = IRQ_TYPE_EDGE_RISING;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		type = IRQ_TYPE_LEVEL_HIGH;
		break;
	default:
		return -EINVAL;
	}

	irq_domain_set_hwirq_and_chip(domain, virq, hwirq, &mchp_eic_chip, eic);

	parent_fwspec.fwnode = domain->parent->fwnode;
	parent_fwspec.param_count = 3;
	parent_fwspec.param[0] = GIC_SPI;
	parent_fwspec.param[1] = eic->irqs[hwirq];
	parent_fwspec.param[2] = type;

	return irq_domain_alloc_irqs_parent(domain, virq, 1, &parent_fwspec);
}

static const struct irq_domain_ops mchp_eic_domain_ops = {
	.translate	= irq_domain_translate_twocell,
	.alloc		= mchp_eic_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

static int mchp_eic_init(struct device_node *node, struct device_node *parent)
{
	struct irq_domain *parent_domain = NULL;
	int ret, i;

	eic = kzalloc(sizeof(*eic), GFP_KERNEL);
	if (!eic)
		return -ENOMEM;

	eic->base = of_iomap(node, 0);
	if (!eic->base) {
		ret = -ENOMEM;
		goto free;
	}

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		ret = -ENODEV;
		goto unmap;
	}

	eic->clk = of_clk_get_by_name(node, "pclk");
	if (IS_ERR(eic->clk)) {
		ret = PTR_ERR(eic->clk);
		goto unmap;
	}

	ret = clk_prepare_enable(eic->clk);
	if (ret)
		goto unmap;

	for (i = 0; i < MCHP_EIC_NIRQ; i++) {
		struct of_phandle_args irq;

		/* Disable it, if any. */
		writel_relaxed(0UL, eic->base + MCHP_EIC_SCFG(i));

		ret = of_irq_parse_one(node, i, &irq);
		if (ret)
			goto clk_unprepare;

		if (WARN_ON(irq.args_count != 3)) {
			ret = -EINVAL;
			goto clk_unprepare;
		}

		eic->irqs[i] = irq.args[1];
	}

	eic->domain = irq_domain_create_hierarchy(parent_domain, 0, MCHP_EIC_NIRQ,
						  of_fwnode_handle(node), &mchp_eic_domain_ops,
						  eic);
	if (!eic->domain) {
		pr_err("%pOF: Failed to add domain\n", node);
		ret = -ENODEV;
		goto clk_unprepare;
	}

	register_syscore_ops(&mchp_eic_syscore_ops);

	pr_info("%pOF: EIC registered, nr_irqs %u\n", node, MCHP_EIC_NIRQ);

	return 0;

clk_unprepare:
	clk_disable_unprepare(eic->clk);
unmap:
	iounmap(eic->base);
free:
	kfree(eic);
	return ret;
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(mchp_eic)
IRQCHIP_MATCH("microchip,sama7g5-eic", mchp_eic_init)
IRQCHIP_PLATFORM_DRIVER_END(mchp_eic)

MODULE_DESCRIPTION("Microchip External Interrupt Controller");
MODULE_AUTHOR("Claudiu Beznea <claudiu.beznea@microchip.com>");

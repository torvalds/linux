// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G2L IRQC Driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation.
 *
 * Author: Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#define IRQC_IRQ_START			1
#define IRQC_IRQ_COUNT			8
#define IRQC_TINT_START			(IRQC_IRQ_START + IRQC_IRQ_COUNT)
#define IRQC_TINT_COUNT			32
#define IRQC_NUM_IRQ			(IRQC_TINT_START + IRQC_TINT_COUNT)

#define ISCR				0x10
#define IITSR				0x14
#define TSCR				0x20
#define TITSR0				0x24
#define TITSR1				0x28
#define TITSR0_MAX_INT			16
#define TITSEL_WIDTH			0x2
#define TSSR(n)				(0x30 + ((n) * 4))
#define TIEN				BIT(7)
#define TSSEL_SHIFT(n)			(8 * (n))
#define TSSEL_MASK			GENMASK(7, 0)
#define IRQ_MASK			0x3

#define TSSR_OFFSET(n)			((n) % 4)
#define TSSR_INDEX(n)			((n) / 4)

#define TITSR_TITSEL_EDGE_RISING	0
#define TITSR_TITSEL_EDGE_FALLING	1
#define TITSR_TITSEL_LEVEL_HIGH		2
#define TITSR_TITSEL_LEVEL_LOW		3

#define IITSR_IITSEL(n, sense)		((sense) << ((n) * 2))
#define IITSR_IITSEL_LEVEL_LOW		0
#define IITSR_IITSEL_EDGE_FALLING	1
#define IITSR_IITSEL_EDGE_RISING	2
#define IITSR_IITSEL_EDGE_BOTH		3
#define IITSR_IITSEL_MASK(n)		IITSR_IITSEL((n), 3)

#define TINT_EXTRACT_HWIRQ(x)           FIELD_GET(GENMASK(15, 0), (x))
#define TINT_EXTRACT_GPIOINT(x)         FIELD_GET(GENMASK(31, 16), (x))

struct rzg2l_irqc_priv {
	void __iomem *base;
	struct irq_fwspec fwspec[IRQC_NUM_IRQ];
	raw_spinlock_t lock;
};

static struct rzg2l_irqc_priv *irq_data_to_priv(struct irq_data *data)
{
	return data->domain->host_data;
}

static void rzg2l_irq_eoi(struct irq_data *d)
{
	unsigned int hw_irq = irqd_to_hwirq(d) - IRQC_IRQ_START;
	struct rzg2l_irqc_priv *priv = irq_data_to_priv(d);
	u32 bit = BIT(hw_irq);
	u32 reg;

	reg = readl_relaxed(priv->base + ISCR);
	if (reg & bit)
		writel_relaxed(reg & ~bit, priv->base + ISCR);
}

static void rzg2l_tint_eoi(struct irq_data *d)
{
	unsigned int hw_irq = irqd_to_hwirq(d) - IRQC_TINT_START;
	struct rzg2l_irqc_priv *priv = irq_data_to_priv(d);
	u32 bit = BIT(hw_irq);
	u32 reg;

	reg = readl_relaxed(priv->base + TSCR);
	if (reg & bit)
		writel_relaxed(reg & ~bit, priv->base + TSCR);
}

static void rzg2l_irqc_eoi(struct irq_data *d)
{
	struct rzg2l_irqc_priv *priv = irq_data_to_priv(d);
	unsigned int hw_irq = irqd_to_hwirq(d);

	raw_spin_lock(&priv->lock);
	if (hw_irq >= IRQC_IRQ_START && hw_irq <= IRQC_IRQ_COUNT)
		rzg2l_irq_eoi(d);
	else if (hw_irq >= IRQC_TINT_START && hw_irq < IRQC_NUM_IRQ)
		rzg2l_tint_eoi(d);
	raw_spin_unlock(&priv->lock);
	irq_chip_eoi_parent(d);
}

static void rzg2l_irqc_irq_disable(struct irq_data *d)
{
	unsigned int hw_irq = irqd_to_hwirq(d);

	if (hw_irq >= IRQC_TINT_START && hw_irq < IRQC_NUM_IRQ) {
		struct rzg2l_irqc_priv *priv = irq_data_to_priv(d);
		u32 offset = hw_irq - IRQC_TINT_START;
		u32 tssr_offset = TSSR_OFFSET(offset);
		u8 tssr_index = TSSR_INDEX(offset);
		u32 reg;

		raw_spin_lock(&priv->lock);
		reg = readl_relaxed(priv->base + TSSR(tssr_index));
		reg &= ~(TSSEL_MASK << TSSEL_SHIFT(tssr_offset));
		writel_relaxed(reg, priv->base + TSSR(tssr_index));
		raw_spin_unlock(&priv->lock);
	}
	irq_chip_disable_parent(d);
}

static void rzg2l_irqc_irq_enable(struct irq_data *d)
{
	unsigned int hw_irq = irqd_to_hwirq(d);

	if (hw_irq >= IRQC_TINT_START && hw_irq < IRQC_NUM_IRQ) {
		struct rzg2l_irqc_priv *priv = irq_data_to_priv(d);
		unsigned long tint = (uintptr_t)d->chip_data;
		u32 offset = hw_irq - IRQC_TINT_START;
		u32 tssr_offset = TSSR_OFFSET(offset);
		u8 tssr_index = TSSR_INDEX(offset);
		u32 reg;

		raw_spin_lock(&priv->lock);
		reg = readl_relaxed(priv->base + TSSR(tssr_index));
		reg |= (TIEN | tint) << TSSEL_SHIFT(tssr_offset);
		writel_relaxed(reg, priv->base + TSSR(tssr_index));
		raw_spin_unlock(&priv->lock);
	}
	irq_chip_enable_parent(d);
}

static int rzg2l_irq_set_type(struct irq_data *d, unsigned int type)
{
	unsigned int hw_irq = irqd_to_hwirq(d) - IRQC_IRQ_START;
	struct rzg2l_irqc_priv *priv = irq_data_to_priv(d);
	u16 sense, tmp;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_LEVEL_LOW:
		sense = IITSR_IITSEL_LEVEL_LOW;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		sense = IITSR_IITSEL_EDGE_FALLING;
		break;

	case IRQ_TYPE_EDGE_RISING:
		sense = IITSR_IITSEL_EDGE_RISING;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		sense = IITSR_IITSEL_EDGE_BOTH;
		break;

	default:
		return -EINVAL;
	}

	raw_spin_lock(&priv->lock);
	tmp = readl_relaxed(priv->base + IITSR);
	tmp &= ~IITSR_IITSEL_MASK(hw_irq);
	tmp |= IITSR_IITSEL(hw_irq, sense);
	writel_relaxed(tmp, priv->base + IITSR);
	raw_spin_unlock(&priv->lock);

	return 0;
}

static int rzg2l_tint_set_edge(struct irq_data *d, unsigned int type)
{
	struct rzg2l_irqc_priv *priv = irq_data_to_priv(d);
	unsigned int hwirq = irqd_to_hwirq(d);
	u32 titseln = hwirq - IRQC_TINT_START;
	u32 offset;
	u8 sense;
	u32 reg;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		sense = TITSR_TITSEL_EDGE_RISING;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		sense = TITSR_TITSEL_EDGE_FALLING;
		break;

	default:
		return -EINVAL;
	}

	offset = TITSR0;
	if (titseln >= TITSR0_MAX_INT) {
		titseln -= TITSR0_MAX_INT;
		offset = TITSR1;
	}

	raw_spin_lock(&priv->lock);
	reg = readl_relaxed(priv->base + offset);
	reg &= ~(IRQ_MASK << (titseln * TITSEL_WIDTH));
	reg |= sense << (titseln * TITSEL_WIDTH);
	writel_relaxed(reg, priv->base + offset);
	raw_spin_unlock(&priv->lock);

	return 0;
}

static int rzg2l_irqc_set_type(struct irq_data *d, unsigned int type)
{
	unsigned int hw_irq = irqd_to_hwirq(d);
	int ret = -EINVAL;

	if (hw_irq >= IRQC_IRQ_START && hw_irq <= IRQC_IRQ_COUNT)
		ret = rzg2l_irq_set_type(d, type);
	else if (hw_irq >= IRQC_TINT_START && hw_irq < IRQC_NUM_IRQ)
		ret = rzg2l_tint_set_edge(d, type);
	if (ret)
		return ret;

	return irq_chip_set_type_parent(d, IRQ_TYPE_LEVEL_HIGH);
}

static const struct irq_chip irqc_chip = {
	.name			= "rzg2l-irqc",
	.irq_eoi		= rzg2l_irqc_eoi,
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_disable		= rzg2l_irqc_irq_disable,
	.irq_enable		= rzg2l_irqc_irq_enable,
	.irq_get_irqchip_state	= irq_chip_get_parent_state,
	.irq_set_irqchip_state	= irq_chip_set_parent_state,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_type		= rzg2l_irqc_set_type,
	.flags			= IRQCHIP_MASK_ON_SUSPEND |
				  IRQCHIP_SET_TYPE_MASKED |
				  IRQCHIP_SKIP_SET_WAKE,
};

static int rzg2l_irqc_alloc(struct irq_domain *domain, unsigned int virq,
			    unsigned int nr_irqs, void *arg)
{
	struct rzg2l_irqc_priv *priv = domain->host_data;
	unsigned long tint = 0;
	irq_hw_number_t hwirq;
	unsigned int type;
	int ret;

	ret = irq_domain_translate_twocell(domain, arg, &hwirq, &type);
	if (ret)
		return ret;

	/*
	 * For TINT interrupts ie where pinctrl driver is child of irqc domain
	 * the hwirq and TINT are encoded in fwspec->param[0].
	 * hwirq for TINT range from 9-40, hwirq is embedded 0-15 bits and TINT
	 * from 16-31 bits. TINT from the pinctrl driver needs to be programmed
	 * in IRQC registers to enable a given gpio pin as interrupt.
	 */
	if (hwirq > IRQC_IRQ_COUNT) {
		tint = TINT_EXTRACT_GPIOINT(hwirq);
		hwirq = TINT_EXTRACT_HWIRQ(hwirq);

		if (hwirq < IRQC_TINT_START)
			return -EINVAL;
	}

	if (hwirq > (IRQC_NUM_IRQ - 1))
		return -EINVAL;

	ret = irq_domain_set_hwirq_and_chip(domain, virq, hwirq, &irqc_chip,
					    (void *)(uintptr_t)tint);
	if (ret)
		return ret;

	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, &priv->fwspec[hwirq]);
}

static const struct irq_domain_ops rzg2l_irqc_domain_ops = {
	.alloc = rzg2l_irqc_alloc,
	.free = irq_domain_free_irqs_common,
	.translate = irq_domain_translate_twocell,
};

static int rzg2l_irqc_parse_interrupts(struct rzg2l_irqc_priv *priv,
				       struct device_node *np)
{
	struct of_phandle_args map;
	unsigned int i;
	int ret;

	for (i = 0; i < IRQC_NUM_IRQ; i++) {
		ret = of_irq_parse_one(np, i, &map);
		if (ret)
			return ret;
		of_phandle_args_to_fwspec(np, map.args, map.args_count,
					  &priv->fwspec[i]);
	}

	return 0;
}

static int rzg2l_irqc_init(struct device_node *node, struct device_node *parent)
{
	struct irq_domain *irq_domain, *parent_domain;
	struct platform_device *pdev;
	struct reset_control *resetn;
	struct rzg2l_irqc_priv *priv;
	int ret;

	pdev = of_find_device_by_node(node);
	if (!pdev)
		return -ENODEV;

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		dev_err(&pdev->dev, "cannot find parent domain\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_of_iomap(&pdev->dev, pdev->dev.of_node, 0, NULL);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	ret = rzg2l_irqc_parse_interrupts(priv, node);
	if (ret) {
		dev_err(&pdev->dev, "cannot parse interrupts: %d\n", ret);
		return ret;
	}

	resetn = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(resetn))
		return PTR_ERR(resetn);

	ret = reset_control_deassert(resetn);
	if (ret) {
		dev_err(&pdev->dev, "failed to deassert resetn pin, %d\n", ret);
		return ret;
	}

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "pm_runtime_resume_and_get failed: %d\n", ret);
		goto pm_disable;
	}

	raw_spin_lock_init(&priv->lock);

	irq_domain = irq_domain_add_hierarchy(parent_domain, 0, IRQC_NUM_IRQ,
					      node, &rzg2l_irqc_domain_ops,
					      priv);
	if (!irq_domain) {
		dev_err(&pdev->dev, "failed to add irq domain\n");
		ret = -ENOMEM;
		goto pm_put;
	}

	return 0;

pm_put:
	pm_runtime_put(&pdev->dev);
pm_disable:
	pm_runtime_disable(&pdev->dev);
	reset_control_assert(resetn);
	return ret;
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(rzg2l_irqc)
IRQCHIP_MATCH("renesas,rzg2l-irqc", rzg2l_irqc_init)
IRQCHIP_PLATFORM_DRIVER_END(rzg2l_irqc)
MODULE_AUTHOR("Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G2L IRQC Driver");
MODULE_LICENSE("GPL");

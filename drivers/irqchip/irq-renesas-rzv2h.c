// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/V2H(P) ICU Driver
 *
 * Based on irq-renesas-rzg2l.c
 *
 * Copyright (C) 2024 Renesas Electronics Corporation.
 *
 * Author: Fabrizio Castro <fabrizio.castro.jz@renesas.com>
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
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
#include <linux/syscore_ops.h>

/* DT "interrupts" indexes */
#define ICU_IRQ_START				1
#define ICU_IRQ_COUNT				16
#define ICU_TINT_START				(ICU_IRQ_START + ICU_IRQ_COUNT)
#define ICU_TINT_COUNT				32
#define ICU_NUM_IRQ				(ICU_TINT_START + ICU_TINT_COUNT)

/* Registers */
#define ICU_NSCNT				0x00
#define ICU_NSCLR				0x04
#define ICU_NITSR				0x08
#define ICU_ISCTR				0x10
#define ICU_ISCLR				0x14
#define ICU_IITSR				0x18
#define ICU_TSCTR				0x20
#define ICU_TSCLR				0x24
#define ICU_TITSR(k)				(0x28 + (k) * 4)
#define ICU_TSSR(k)				(0x30 + (k) * 4)

/* NMI */
#define ICU_NMI_EDGE_FALLING			0
#define ICU_NMI_EDGE_RISING			1

#define ICU_NSCLR_NCLR				BIT(0)

/* IRQ */
#define ICU_IRQ_LEVEL_LOW			0
#define ICU_IRQ_EDGE_FALLING			1
#define ICU_IRQ_EDGE_RISING			2
#define ICU_IRQ_EDGE_BOTH			3

#define ICU_IITSR_IITSEL_PREP(iitsel, n)	((iitsel) << ((n) * 2))
#define ICU_IITSR_IITSEL_GET(iitsr, n)		(((iitsr) >> ((n) * 2)) & 0x03)
#define ICU_IITSR_IITSEL_MASK(n)		ICU_IITSR_IITSEL_PREP(0x03, n)

/* TINT */
#define ICU_TINT_EDGE_RISING			0
#define ICU_TINT_EDGE_FALLING			1
#define ICU_TINT_LEVEL_HIGH			2
#define ICU_TINT_LEVEL_LOW			3

#define ICU_TSSR_K(tint_nr)			((tint_nr) / 4)
#define ICU_TSSR_TSSEL_N(tint_nr)		((tint_nr) % 4)
#define ICU_TSSR_TSSEL_PREP(tssel, n)		((tssel) << ((n) * 8))
#define ICU_TSSR_TSSEL_MASK(n)			ICU_TSSR_TSSEL_PREP(0x7F, n)
#define ICU_TSSR_TIEN(n)			(BIT(7) << ((n) * 8))

#define ICU_TITSR_K(tint_nr)			((tint_nr) / 16)
#define ICU_TITSR_TITSEL_N(tint_nr)		((tint_nr) % 16)
#define ICU_TITSR_TITSEL_PREP(titsel, n)	ICU_IITSR_IITSEL_PREP(titsel, n)
#define ICU_TITSR_TITSEL_MASK(n)		ICU_IITSR_IITSEL_MASK(n)
#define ICU_TITSR_TITSEL_GET(titsr, n)		ICU_IITSR_IITSEL_GET(titsr, n)

#define ICU_TINT_EXTRACT_HWIRQ(x)		FIELD_GET(GENMASK(15, 0), (x))
#define ICU_TINT_EXTRACT_GPIOINT(x)		FIELD_GET(GENMASK(31, 16), (x))
#define ICU_PB5_TINT				0x55

/**
 * struct rzv2h_icu_priv - Interrupt Control Unit controller private data structure.
 * @base:	Controller's base address
 * @irqchip:	Pointer to struct irq_chip
 * @fwspec:	IRQ firmware specific data
 * @lock:	Lock to serialize access to hardware registers
 */
struct rzv2h_icu_priv {
	void __iomem			*base;
	const struct irq_chip		*irqchip;
	struct irq_fwspec		fwspec[ICU_NUM_IRQ];
	raw_spinlock_t			lock;
};

static inline struct rzv2h_icu_priv *irq_data_to_priv(struct irq_data *data)
{
	return data->domain->host_data;
}

static void rzv2h_icu_eoi(struct irq_data *d)
{
	struct rzv2h_icu_priv *priv = irq_data_to_priv(d);
	unsigned int hw_irq = irqd_to_hwirq(d);
	unsigned int tintirq_nr;
	u32 bit;

	scoped_guard(raw_spinlock, &priv->lock) {
		if (hw_irq >= ICU_TINT_START) {
			tintirq_nr = hw_irq - ICU_TINT_START;
			bit = BIT(tintirq_nr);
			if (!irqd_is_level_type(d))
				writel_relaxed(bit, priv->base + ICU_TSCLR);
		} else if (hw_irq >= ICU_IRQ_START) {
			tintirq_nr = hw_irq - ICU_IRQ_START;
			bit = BIT(tintirq_nr);
			if (!irqd_is_level_type(d))
				writel_relaxed(bit, priv->base + ICU_ISCLR);
		} else {
			writel_relaxed(ICU_NSCLR_NCLR, priv->base + ICU_NSCLR);
		}
	}

	irq_chip_eoi_parent(d);
}

static void rzv2h_tint_irq_endisable(struct irq_data *d, bool enable)
{
	struct rzv2h_icu_priv *priv = irq_data_to_priv(d);
	unsigned int hw_irq = irqd_to_hwirq(d);
	u32 tint_nr, tssel_n, k, tssr;

	if (hw_irq < ICU_TINT_START)
		return;

	tint_nr = hw_irq - ICU_TINT_START;
	k = ICU_TSSR_K(tint_nr);
	tssel_n = ICU_TSSR_TSSEL_N(tint_nr);

	guard(raw_spinlock)(&priv->lock);
	tssr = readl_relaxed(priv->base + ICU_TSSR(k));
	if (enable)
		tssr |= ICU_TSSR_TIEN(tssel_n);
	else
		tssr &= ~ICU_TSSR_TIEN(tssel_n);
	writel_relaxed(tssr, priv->base + ICU_TSSR(k));
}

static void rzv2h_icu_irq_disable(struct irq_data *d)
{
	irq_chip_disable_parent(d);
	rzv2h_tint_irq_endisable(d, false);
}

static void rzv2h_icu_irq_enable(struct irq_data *d)
{
	rzv2h_tint_irq_endisable(d, true);
	irq_chip_enable_parent(d);
}

static int rzv2h_nmi_set_type(struct irq_data *d, unsigned int type)
{
	struct rzv2h_icu_priv *priv = irq_data_to_priv(d);
	u32 sense;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_FALLING:
		sense = ICU_NMI_EDGE_FALLING;
		break;

	case IRQ_TYPE_EDGE_RISING:
		sense = ICU_NMI_EDGE_RISING;
		break;

	default:
		return -EINVAL;
	}

	writel_relaxed(sense, priv->base + ICU_NITSR);

	return 0;
}

static void rzv2h_clear_irq_int(struct rzv2h_icu_priv *priv, unsigned int hwirq)
{
	unsigned int irq_nr = hwirq - ICU_IRQ_START;
	u32 isctr, iitsr, iitsel;
	u32 bit = BIT(irq_nr);

	isctr = readl_relaxed(priv->base + ICU_ISCTR);
	iitsr = readl_relaxed(priv->base + ICU_IITSR);
	iitsel = ICU_IITSR_IITSEL_GET(iitsr, irq_nr);

	/*
	 * When level sensing is used, the interrupt flag gets automatically cleared when the
	 * interrupt signal is de-asserted by the source of the interrupt request, therefore clear
	 * the interrupt only for edge triggered interrupts.
	 */
	if ((isctr & bit) && (iitsel != ICU_IRQ_LEVEL_LOW))
		writel_relaxed(bit, priv->base + ICU_ISCLR);
}

static int rzv2h_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct rzv2h_icu_priv *priv = irq_data_to_priv(d);
	unsigned int hwirq = irqd_to_hwirq(d);
	u32 irq_nr = hwirq - ICU_IRQ_START;
	u32 iitsr, sense;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_LEVEL_LOW:
		sense = ICU_IRQ_LEVEL_LOW;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		sense = ICU_IRQ_EDGE_FALLING;
		break;

	case IRQ_TYPE_EDGE_RISING:
		sense = ICU_IRQ_EDGE_RISING;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		sense = ICU_IRQ_EDGE_BOTH;
		break;

	default:
		return -EINVAL;
	}

	guard(raw_spinlock)(&priv->lock);
	iitsr = readl_relaxed(priv->base + ICU_IITSR);
	iitsr &= ~ICU_IITSR_IITSEL_MASK(irq_nr);
	iitsr |= ICU_IITSR_IITSEL_PREP(sense, irq_nr);
	rzv2h_clear_irq_int(priv, hwirq);
	writel_relaxed(iitsr, priv->base + ICU_IITSR);

	return 0;
}

static void rzv2h_clear_tint_int(struct rzv2h_icu_priv *priv, unsigned int hwirq)
{
	unsigned int tint_nr = hwirq - ICU_TINT_START;
	int titsel_n = ICU_TITSR_TITSEL_N(tint_nr);
	u32 tsctr, titsr, titsel;
	u32 bit = BIT(tint_nr);
	int k = tint_nr / 16;

	tsctr = readl_relaxed(priv->base + ICU_TSCTR);
	titsr = readl_relaxed(priv->base + ICU_TITSR(k));
	titsel = ICU_TITSR_TITSEL_GET(titsr, titsel_n);

	/*
	 * Writing 1 to the corresponding flag from register ICU_TSCTR only has effect if
	 * TSTATn = 1b and if it's a rising edge or a falling edge interrupt.
	 */
	if ((tsctr & bit) && ((titsel == ICU_TINT_EDGE_RISING) ||
			      (titsel == ICU_TINT_EDGE_FALLING)))
		writel_relaxed(bit, priv->base + ICU_TSCLR);
}

static int rzv2h_tint_set_type(struct irq_data *d, unsigned int type)
{
	u32 titsr, titsr_k, titsel_n, tien;
	struct rzv2h_icu_priv *priv;
	u32 tssr, tssr_k, tssel_n;
	unsigned int hwirq;
	u32 tint, sense;
	int tint_nr;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_LEVEL_LOW:
		sense = ICU_TINT_LEVEL_LOW;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		sense = ICU_TINT_LEVEL_HIGH;
		break;

	case IRQ_TYPE_EDGE_RISING:
		sense = ICU_TINT_EDGE_RISING;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		sense = ICU_TINT_EDGE_FALLING;
		break;

	default:
		return -EINVAL;
	}

	tint = (u32)(uintptr_t)irq_data_get_irq_chip_data(d);
	if (tint > ICU_PB5_TINT)
		return -EINVAL;

	priv = irq_data_to_priv(d);
	hwirq = irqd_to_hwirq(d);

	tint_nr = hwirq - ICU_TINT_START;

	tssr_k = ICU_TSSR_K(tint_nr);
	tssel_n = ICU_TSSR_TSSEL_N(tint_nr);

	titsr_k = ICU_TITSR_K(tint_nr);
	titsel_n = ICU_TITSR_TITSEL_N(tint_nr);
	tien = ICU_TSSR_TIEN(titsel_n);

	guard(raw_spinlock)(&priv->lock);

	tssr = readl_relaxed(priv->base + ICU_TSSR(tssr_k));
	tssr &= ~(ICU_TSSR_TSSEL_MASK(tssel_n) | tien);
	tssr |= ICU_TSSR_TSSEL_PREP(tint, tssel_n);

	writel_relaxed(tssr, priv->base + ICU_TSSR(tssr_k));

	titsr = readl_relaxed(priv->base + ICU_TITSR(titsr_k));
	titsr &= ~ICU_TITSR_TITSEL_MASK(titsel_n);
	titsr |= ICU_TITSR_TITSEL_PREP(sense, titsel_n);

	writel_relaxed(titsr, priv->base + ICU_TITSR(titsr_k));

	rzv2h_clear_tint_int(priv, hwirq);

	writel_relaxed(tssr | tien, priv->base + ICU_TSSR(tssr_k));

	return 0;
}

static int rzv2h_icu_set_type(struct irq_data *d, unsigned int type)
{
	unsigned int hw_irq = irqd_to_hwirq(d);
	int ret;

	if (hw_irq >= ICU_TINT_START)
		ret = rzv2h_tint_set_type(d, type);
	else if (hw_irq >= ICU_IRQ_START)
		ret = rzv2h_irq_set_type(d, type);
	else
		ret = rzv2h_nmi_set_type(d, type);

	if (ret)
		return ret;

	return irq_chip_set_type_parent(d, IRQ_TYPE_LEVEL_HIGH);
}

static const struct irq_chip rzv2h_icu_chip = {
	.name			= "rzv2h-icu",
	.irq_eoi		= rzv2h_icu_eoi,
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_disable		= rzv2h_icu_irq_disable,
	.irq_enable		= rzv2h_icu_irq_enable,
	.irq_get_irqchip_state	= irq_chip_get_parent_state,
	.irq_set_irqchip_state	= irq_chip_set_parent_state,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_type		= rzv2h_icu_set_type,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.flags			= IRQCHIP_SET_TYPE_MASKED,
};

static int rzv2h_icu_alloc(struct irq_domain *domain, unsigned int virq, unsigned int nr_irqs,
			   void *arg)
{
	struct rzv2h_icu_priv *priv = domain->host_data;
	unsigned long tint = 0;
	irq_hw_number_t hwirq;
	unsigned int type;
	int ret;

	ret = irq_domain_translate_twocell(domain, arg, &hwirq, &type);
	if (ret)
		return ret;

	/*
	 * For TINT interrupts the hwirq and TINT are encoded in
	 * fwspec->param[0].
	 * hwirq is embedded in bits 0-15.
	 * TINT is embedded in bits 16-31.
	 */
	if (hwirq >= ICU_TINT_START) {
		tint = ICU_TINT_EXTRACT_GPIOINT(hwirq);
		hwirq = ICU_TINT_EXTRACT_HWIRQ(hwirq);

		if (hwirq < ICU_TINT_START)
			return -EINVAL;
	}

	if (hwirq > (ICU_NUM_IRQ - 1))
		return -EINVAL;

	ret = irq_domain_set_hwirq_and_chip(domain, virq, hwirq, priv->irqchip,
					    (void *)(uintptr_t)tint);
	if (ret)
		return ret;

	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, &priv->fwspec[hwirq]);
}

static const struct irq_domain_ops rzv2h_icu_domain_ops = {
	.alloc		= rzv2h_icu_alloc,
	.free		= irq_domain_free_irqs_common,
	.translate	= irq_domain_translate_twocell,
};

static int rzv2h_icu_parse_interrupts(struct rzv2h_icu_priv *priv, struct device_node *np)
{
	struct of_phandle_args map;
	unsigned int i;
	int ret;

	for (i = 0; i < ICU_NUM_IRQ; i++) {
		ret = of_irq_parse_one(np, i, &map);
		if (ret)
			return ret;

		of_phandle_args_to_fwspec(np, map.args, map.args_count, &priv->fwspec[i]);
	}

	return 0;
}

static int rzv2h_icu_init(struct device_node *node, struct device_node *parent)
{
	struct irq_domain *irq_domain, *parent_domain;
	struct rzv2h_icu_priv *rzv2h_icu_data;
	struct platform_device *pdev;
	struct reset_control *resetn;
	int ret;

	pdev = of_find_device_by_node(node);
	if (!pdev)
		return -ENODEV;

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		dev_err(&pdev->dev, "cannot find parent domain\n");
		ret = -ENODEV;
		goto put_dev;
	}

	rzv2h_icu_data = devm_kzalloc(&pdev->dev, sizeof(*rzv2h_icu_data), GFP_KERNEL);
	if (!rzv2h_icu_data) {
		ret = -ENOMEM;
		goto put_dev;
	}

	rzv2h_icu_data->irqchip = &rzv2h_icu_chip;

	rzv2h_icu_data->base = devm_of_iomap(&pdev->dev, pdev->dev.of_node, 0, NULL);
	if (IS_ERR(rzv2h_icu_data->base)) {
		ret = PTR_ERR(rzv2h_icu_data->base);
		goto put_dev;
	}

	ret = rzv2h_icu_parse_interrupts(rzv2h_icu_data, node);
	if (ret) {
		dev_err(&pdev->dev, "cannot parse interrupts: %d\n", ret);
		goto put_dev;
	}

	resetn = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(resetn)) {
		ret = PTR_ERR(resetn);
		goto put_dev;
	}

	ret = reset_control_deassert(resetn);
	if (ret) {
		dev_err(&pdev->dev, "failed to deassert resetn pin, %d\n", ret);
		goto put_dev;
	}

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "pm_runtime_resume_and_get failed: %d\n", ret);
		goto pm_disable;
	}

	raw_spin_lock_init(&rzv2h_icu_data->lock);

	irq_domain = irq_domain_add_hierarchy(parent_domain, 0, ICU_NUM_IRQ, node,
					      &rzv2h_icu_domain_ops, rzv2h_icu_data);
	if (!irq_domain) {
		dev_err(&pdev->dev, "failed to add irq domain\n");
		ret = -ENOMEM;
		goto pm_put;
	}

	/*
	 * coccicheck complains about a missing put_device call before returning, but it's a false
	 * positive. We still need &pdev->dev after successfully returning from this function.
	 */
	return 0;

pm_put:
	pm_runtime_put(&pdev->dev);
pm_disable:
	pm_runtime_disable(&pdev->dev);
	reset_control_assert(resetn);
put_dev:
	put_device(&pdev->dev);

	return ret;
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(rzv2h_icu)
IRQCHIP_MATCH("renesas,r9a09g057-icu", rzv2h_icu_init)
IRQCHIP_PLATFORM_DRIVER_END(rzv2h_icu)
MODULE_AUTHOR("Fabrizio Castro <fabrizio.castro.jz@renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/V2H(P) ICU Driver");

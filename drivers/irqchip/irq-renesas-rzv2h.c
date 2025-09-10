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
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqchip/irq-renesas-rzv2h.h>
#include <linux/irqdomain.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

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
#define ICU_DMkSELy(k, y)			(0x420 + (k) * 0x20 + (y) * 4)
#define ICU_DMACKSELk(k)			(0x500 + (k) * 4)

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

#define ICU_TSSR_TSSEL_PREP(tssel, n, field_width)	((tssel) << ((n) * (field_width)))
#define ICU_TSSR_TSSEL_MASK(n, field_width)	\
({\
		typeof(field_width) (_field_width) = (field_width); \
		ICU_TSSR_TSSEL_PREP((GENMASK(((_field_width) - 2), 0)), (n), _field_width); \
})

#define ICU_TSSR_TIEN(n, field_width)	\
({\
		typeof(field_width) (_field_width) = (field_width); \
		BIT((_field_width) - 1) << ((n) * (_field_width)); \
})

#define ICU_TITSR_K(tint_nr)			((tint_nr) / 16)
#define ICU_TITSR_TITSEL_N(tint_nr)		((tint_nr) % 16)
#define ICU_TITSR_TITSEL_PREP(titsel, n)	ICU_IITSR_IITSEL_PREP(titsel, n)
#define ICU_TITSR_TITSEL_MASK(n)		ICU_IITSR_IITSEL_MASK(n)
#define ICU_TITSR_TITSEL_GET(titsr, n)		ICU_IITSR_IITSEL_GET(titsr, n)

#define ICU_TINT_EXTRACT_HWIRQ(x)		FIELD_GET(GENMASK(15, 0), (x))
#define ICU_TINT_EXTRACT_GPIOINT(x)		FIELD_GET(GENMASK(31, 16), (x))
#define ICU_RZG3E_TINT_OFFSET			0x800
#define ICU_RZG3E_TSSEL_MAX_VAL			0x8c
#define ICU_RZV2H_TSSEL_MAX_VAL			0x55

/**
 * struct rzv2h_hw_info - Interrupt Control Unit controller hardware info structure.
 * @tssel_lut:		TINT lookup table
 * @t_offs:		TINT offset
 * @max_tssel:		TSSEL max value
 * @field_width:	TSSR field width
 */
struct rzv2h_hw_info {
	const u8	*tssel_lut;
	u16		t_offs;
	u8		max_tssel;
	u8		field_width;
};

/* DMAC */
#define ICU_DMAC_DkRQ_SEL_MASK			GENMASK(9, 0)

#define ICU_DMAC_DMAREQ_SHIFT(up)		((up) * 16)
#define ICU_DMAC_DMAREQ_MASK(up)		(ICU_DMAC_DkRQ_SEL_MASK \
						 << ICU_DMAC_DMAREQ_SHIFT(up))
#define ICU_DMAC_PREP_DMAREQ(sel, up)		(FIELD_PREP(ICU_DMAC_DkRQ_SEL_MASK, (sel)) \
						 << ICU_DMAC_DMAREQ_SHIFT(up))

/**
 * struct rzv2h_icu_priv - Interrupt Control Unit controller private data structure.
 * @base:	Controller's base address
 * @fwspec:	IRQ firmware specific data
 * @lock:	Lock to serialize access to hardware registers
 * @info:	Pointer to struct rzv2h_hw_info
 */
struct rzv2h_icu_priv {
	void __iomem			*base;
	struct irq_fwspec		fwspec[ICU_NUM_IRQ];
	raw_spinlock_t			lock;
	const struct rzv2h_hw_info	*info;
};

void rzv2h_icu_register_dma_req(struct platform_device *icu_dev, u8 dmac_index, u8 dmac_channel,
				u16 req_no)
{
	struct rzv2h_icu_priv *priv = platform_get_drvdata(icu_dev);
	u32 icu_dmksely, dmareq, dmareq_mask;
	u8 y, upper;

	y = dmac_channel / 2;
	upper = dmac_channel % 2;

	dmareq = ICU_DMAC_PREP_DMAREQ(req_no, upper);
	dmareq_mask = ICU_DMAC_DMAREQ_MASK(upper);

	guard(raw_spinlock_irqsave)(&priv->lock);

	icu_dmksely = readl(priv->base + ICU_DMkSELy(dmac_index, y));
	icu_dmksely = (icu_dmksely & ~dmareq_mask) | dmareq;
	writel(icu_dmksely, priv->base + ICU_DMkSELy(dmac_index, y));
}
EXPORT_SYMBOL_GPL(rzv2h_icu_register_dma_req);

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
				writel_relaxed(bit, priv->base + priv->info->t_offs + ICU_TSCLR);
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
	u8 nr_tint;

	if (hw_irq < ICU_TINT_START)
		return;

	tint_nr = hw_irq - ICU_TINT_START;
	nr_tint = 32 / priv->info->field_width;
	k = tint_nr / nr_tint;
	tssel_n = tint_nr % nr_tint;

	guard(raw_spinlock)(&priv->lock);
	tssr = readl_relaxed(priv->base + priv->info->t_offs + ICU_TSSR(k));
	if (enable)
		tssr |= ICU_TSSR_TIEN(tssel_n, priv->info->field_width);
	else
		tssr &= ~ICU_TSSR_TIEN(tssel_n, priv->info->field_width);
	writel_relaxed(tssr, priv->base + priv->info->t_offs + ICU_TSSR(k));

	/*
	 * A glitch in the edge detection circuit can cause a spurious
	 * interrupt. Clear the status flag after setting the ICU_TSSRk
	 * registers, which is recommended by the hardware manual as a
	 * countermeasure.
	 */
	writel_relaxed(BIT(tint_nr), priv->base + priv->info->t_offs + ICU_TSCLR);
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

	tsctr = readl_relaxed(priv->base + priv->info->t_offs + ICU_TSCTR);
	titsr = readl_relaxed(priv->base + priv->info->t_offs + ICU_TITSR(k));
	titsel = ICU_TITSR_TITSEL_GET(titsr, titsel_n);

	/*
	 * Writing 1 to the corresponding flag from register ICU_TSCTR only has effect if
	 * TSTATn = 1b and if it's a rising edge or a falling edge interrupt.
	 */
	if ((tsctr & bit) && ((titsel == ICU_TINT_EDGE_RISING) ||
			      (titsel == ICU_TINT_EDGE_FALLING)))
		writel_relaxed(bit, priv->base + priv->info->t_offs + ICU_TSCLR);
}

static int rzv2h_tint_set_type(struct irq_data *d, unsigned int type)
{
	u32 titsr, titsr_k, titsel_n, tien;
	struct rzv2h_icu_priv *priv;
	u32 tssr, tssr_k, tssel_n;
	unsigned int hwirq;
	u32 tint, sense;
	int tint_nr;
	u8 nr_tint;

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

	priv = irq_data_to_priv(d);
	tint = (u32)(uintptr_t)irq_data_get_irq_chip_data(d);
	if (tint > priv->info->max_tssel)
		return -EINVAL;

	if (priv->info->tssel_lut)
		tint = priv->info->tssel_lut[tint];

	hwirq = irqd_to_hwirq(d);
	tint_nr = hwirq - ICU_TINT_START;

	nr_tint = 32 / priv->info->field_width;
	tssr_k = tint_nr / nr_tint;
	tssel_n = tint_nr % nr_tint;
	tien = ICU_TSSR_TIEN(tssel_n, priv->info->field_width);

	titsr_k = ICU_TITSR_K(tint_nr);
	titsel_n = ICU_TITSR_TITSEL_N(tint_nr);

	guard(raw_spinlock)(&priv->lock);

	tssr = readl_relaxed(priv->base + priv->info->t_offs + ICU_TSSR(tssr_k));
	tssr &= ~(ICU_TSSR_TSSEL_MASK(tssel_n, priv->info->field_width) | tien);
	tssr |= ICU_TSSR_TSSEL_PREP(tint, tssel_n, priv->info->field_width);

	writel_relaxed(tssr, priv->base + priv->info->t_offs + ICU_TSSR(tssr_k));

	titsr = readl_relaxed(priv->base + priv->info->t_offs + ICU_TITSR(titsr_k));
	titsr &= ~ICU_TITSR_TITSEL_MASK(titsel_n);
	titsr |= ICU_TITSR_TITSEL_PREP(sense, titsel_n);

	writel_relaxed(titsr, priv->base + priv->info->t_offs + ICU_TITSR(titsr_k));

	rzv2h_clear_tint_int(priv, hwirq);

	writel_relaxed(tssr | tien, priv->base + priv->info->t_offs + ICU_TSSR(tssr_k));

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
	.flags			= IRQCHIP_MASK_ON_SUSPEND |
				  IRQCHIP_SET_TYPE_MASKED |
				  IRQCHIP_SKIP_SET_WAKE,
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

	ret = irq_domain_set_hwirq_and_chip(domain, virq, hwirq, &rzv2h_icu_chip,
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

static void rzv2h_icu_put_device(void *data)
{
	put_device(data);
}

static int rzv2h_icu_init_common(struct device_node *node, struct device_node *parent,
				 const struct rzv2h_hw_info *hw_info)
{
	struct irq_domain *irq_domain, *parent_domain;
	struct rzv2h_icu_priv *rzv2h_icu_data;
	struct platform_device *pdev;
	struct reset_control *resetn;
	int ret;

	pdev = of_find_device_by_node(node);
	if (!pdev)
		return -ENODEV;

	ret = devm_add_action_or_reset(&pdev->dev, rzv2h_icu_put_device,
				       &pdev->dev);
	if (ret < 0)
		return ret;

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		dev_err(&pdev->dev, "cannot find parent domain\n");
		return -ENODEV;
	}

	rzv2h_icu_data = devm_kzalloc(&pdev->dev, sizeof(*rzv2h_icu_data), GFP_KERNEL);
	if (!rzv2h_icu_data)
		return -ENOMEM;

	platform_set_drvdata(pdev, rzv2h_icu_data);

	rzv2h_icu_data->base = devm_of_iomap(&pdev->dev, pdev->dev.of_node, 0, NULL);
	if (IS_ERR(rzv2h_icu_data->base))
		return PTR_ERR(rzv2h_icu_data->base);

	ret = rzv2h_icu_parse_interrupts(rzv2h_icu_data, node);
	if (ret) {
		dev_err(&pdev->dev, "cannot parse interrupts: %d\n", ret);
		return ret;
	}

	resetn = devm_reset_control_get_exclusive_deasserted(&pdev->dev, NULL);
	if (IS_ERR(resetn)) {
		ret = PTR_ERR(resetn);
		dev_err(&pdev->dev, "failed to acquire deasserted reset: %d\n", ret);
		return ret;
	}

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "devm_pm_runtime_enable failed, %d\n", ret);
		return ret;
	}

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "pm_runtime_resume_and_get failed: %d\n", ret);
		return ret;
	}

	raw_spin_lock_init(&rzv2h_icu_data->lock);

	irq_domain = irq_domain_create_hierarchy(parent_domain, 0, ICU_NUM_IRQ,
						 dev_fwnode(&pdev->dev), &rzv2h_icu_domain_ops,
						 rzv2h_icu_data);
	if (!irq_domain) {
		dev_err(&pdev->dev, "failed to add irq domain\n");
		ret = -ENOMEM;
		goto pm_put;
	}

	rzv2h_icu_data->info = hw_info;

	/*
	 * coccicheck complains about a missing put_device call before returning, but it's a false
	 * positive. We still need &pdev->dev after successfully returning from this function.
	 */
	return 0;

pm_put:
	pm_runtime_put(&pdev->dev);

	return ret;
}

/* Mapping based on port index on Table 4.2-6 and TSSEL bits on Table 4.6-4 */
static const u8 rzg3e_tssel_lut[] = {
	81, 82, 83, 84, 85, 86, 87, 88,		/* P00-P07 */
	89, 90, 91, 92, 93, 94, 95, 96,		/* P10-P17 */
	111, 112,				/* P20-P21 */
	97, 98, 99, 100, 101, 102, 103, 104,	/* P30-P37 */
	105, 106, 107, 108, 109, 110,		/* P40-P45 */
	113, 114, 115, 116, 117, 118, 119,	/* P50-P56 */
	120, 121, 122, 123, 124, 125, 126,	/* P60-P66 */
	127, 128, 129, 130, 131, 132, 133, 134,	/* P70-P77 */
	135, 136, 137, 138, 139, 140,		/* P80-P85 */
	43, 44, 45, 46, 47, 48, 49, 50,		/* PA0-PA7 */
	51, 52, 53, 54, 55, 56, 57, 58,		/* PB0-PB7 */
	59, 60,	61,				/* PC0-PC2 */
	62, 63, 64, 65, 66, 67, 68, 69,		/* PD0-PD7 */
	70, 71, 72, 73, 74, 75, 76, 77,		/* PE0-PE7 */
	78, 79, 80,				/* PF0-PF2 */
	25, 26, 27, 28, 29, 30, 31, 32,		/* PG0-PG7 */
	33, 34, 35, 36, 37, 38,			/* PH0-PH5 */
	4, 5, 6, 7, 8,				/* PJ0-PJ4 */
	39, 40, 41, 42,				/* PK0-PK3 */
	9, 10, 11, 12, 21, 22, 23, 24,		/* PL0-PL7 */
	13, 14, 15, 16, 17, 18, 19, 20,		/* PM0-PM7 */
	0, 1, 2, 3				/* PS0-PS3 */
};

static const struct rzv2h_hw_info rzg3e_hw_params = {
	.tssel_lut	= rzg3e_tssel_lut,
	.t_offs		= ICU_RZG3E_TINT_OFFSET,
	.max_tssel	= ICU_RZG3E_TSSEL_MAX_VAL,
	.field_width	= 16,
};

static const struct rzv2h_hw_info rzv2h_hw_params = {
	.t_offs		= 0,
	.max_tssel	= ICU_RZV2H_TSSEL_MAX_VAL,
	.field_width	= 8,
};

static int rzg3e_icu_init(struct device_node *node, struct device_node *parent)
{
	return rzv2h_icu_init_common(node, parent, &rzg3e_hw_params);
}

static int rzv2h_icu_init(struct device_node *node, struct device_node *parent)
{
	return rzv2h_icu_init_common(node, parent, &rzv2h_hw_params);
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(rzv2h_icu)
IRQCHIP_MATCH("renesas,r9a09g047-icu", rzg3e_icu_init)
IRQCHIP_MATCH("renesas,r9a09g057-icu", rzv2h_icu_init)
IRQCHIP_PLATFORM_DRIVER_END(rzv2h_icu)
MODULE_AUTHOR("Fabrizio Castro <fabrizio.castro.jz@renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/V2H(P) ICU Driver");

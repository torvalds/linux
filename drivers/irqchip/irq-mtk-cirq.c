// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Youlin.Pei <youlin.pei@mediatek.com>
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>

#define CIRQ_ACK	0x40
#define CIRQ_MASK_SET	0xc0
#define CIRQ_MASK_CLR	0x100
#define CIRQ_SENS_SET	0x180
#define CIRQ_SENS_CLR	0x1c0
#define CIRQ_POL_SET	0x240
#define CIRQ_POL_CLR	0x280
#define CIRQ_CONTROL	0x300

#define CIRQ_EN	0x1
#define CIRQ_EDGE	0x2
#define CIRQ_FLUSH	0x4

struct mtk_cirq_chip_data {
	void __iomem *base;
	unsigned int ext_irq_start;
	unsigned int ext_irq_end;
	struct irq_domain *domain;
};

static struct mtk_cirq_chip_data *cirq_data;

static void mtk_cirq_write_mask(struct irq_data *data, unsigned int offset)
{
	struct mtk_cirq_chip_data *chip_data = data->chip_data;
	unsigned int cirq_num = data->hwirq;
	u32 mask = 1 << (cirq_num % 32);

	writel_relaxed(mask, chip_data->base + offset + (cirq_num / 32) * 4);
}

static void mtk_cirq_mask(struct irq_data *data)
{
	mtk_cirq_write_mask(data, CIRQ_MASK_SET);
	irq_chip_mask_parent(data);
}

static void mtk_cirq_unmask(struct irq_data *data)
{
	mtk_cirq_write_mask(data, CIRQ_MASK_CLR);
	irq_chip_unmask_parent(data);
}

static int mtk_cirq_set_type(struct irq_data *data, unsigned int type)
{
	int ret;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_FALLING:
		mtk_cirq_write_mask(data, CIRQ_POL_CLR);
		mtk_cirq_write_mask(data, CIRQ_SENS_CLR);
		break;
	case IRQ_TYPE_EDGE_RISING:
		mtk_cirq_write_mask(data, CIRQ_POL_SET);
		mtk_cirq_write_mask(data, CIRQ_SENS_CLR);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		mtk_cirq_write_mask(data, CIRQ_POL_CLR);
		mtk_cirq_write_mask(data, CIRQ_SENS_SET);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		mtk_cirq_write_mask(data, CIRQ_POL_SET);
		mtk_cirq_write_mask(data, CIRQ_SENS_SET);
		break;
	default:
		break;
	}

	data = data->parent_data;
	ret = data->chip->irq_set_type(data, type);
	return ret;
}

static struct irq_chip mtk_cirq_chip = {
	.name			= "MT_CIRQ",
	.irq_mask		= mtk_cirq_mask,
	.irq_unmask		= mtk_cirq_unmask,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_type		= mtk_cirq_set_type,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
#ifdef CONFIG_SMP
	.irq_set_affinity	= irq_chip_set_affinity_parent,
#endif
};

static int mtk_cirq_domain_translate(struct irq_domain *d,
				     struct irq_fwspec *fwspec,
				     unsigned long *hwirq,
				     unsigned int *type)
{
	if (is_of_node(fwspec->fwnode)) {
		if (fwspec->param_count != 3)
			return -EINVAL;

		/* No PPI should point to this domain */
		if (fwspec->param[0] != 0)
			return -EINVAL;

		/* cirq support irq number check */
		if (fwspec->param[1] < cirq_data->ext_irq_start ||
		    fwspec->param[1] > cirq_data->ext_irq_end)
			return -EINVAL;

		*hwirq = fwspec->param[1] - cirq_data->ext_irq_start;
		*type = fwspec->param[2] & IRQ_TYPE_SENSE_MASK;
		return 0;
	}

	return -EINVAL;
}

static int mtk_cirq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *arg)
{
	int ret;
	irq_hw_number_t hwirq;
	unsigned int type;
	struct irq_fwspec *fwspec = arg;
	struct irq_fwspec parent_fwspec = *fwspec;

	ret = mtk_cirq_domain_translate(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	if (WARN_ON(nr_irqs != 1))
		return -EINVAL;

	irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
				      &mtk_cirq_chip,
				      domain->host_data);

	parent_fwspec.fwnode = domain->parent->fwnode;
	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs,
					    &parent_fwspec);
}

static const struct irq_domain_ops cirq_domain_ops = {
	.translate	= mtk_cirq_domain_translate,
	.alloc		= mtk_cirq_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

#ifdef CONFIG_PM_SLEEP
static int mtk_cirq_suspend(void)
{
	u32 value, mask;
	unsigned int irq, hwirq_num;
	bool pending, masked;
	int i, pendret, maskret;

	/*
	 * When external interrupts happened, CIRQ will record the status
	 * even CIRQ is not enabled. When execute flush command, CIRQ will
	 * resend the signals according to the status. So if don't clear the
	 * status, CIRQ will resend the wrong signals.
	 *
	 * arch_suspend_disable_irqs() will be called before CIRQ suspend
	 * callback. If clear all the status simply, the external interrupts
	 * which happened between arch_suspend_disable_irqs and CIRQ suspend
	 * callback will be lost. Using following steps to avoid this issue;
	 *
	 * - Iterate over all the CIRQ supported interrupts;
	 * - For each interrupt, inspect its pending and masked status at GIC
	 *   level;
	 * - If pending and unmasked, it happened between
	 *   arch_suspend_disable_irqs and CIRQ suspend callback, don't ACK
	 *   it. Otherwise, ACK it.
	 */
	hwirq_num = cirq_data->ext_irq_end - cirq_data->ext_irq_start + 1;
	for (i = 0; i < hwirq_num; i++) {
		irq = irq_find_mapping(cirq_data->domain, i);
		if (irq) {
			pendret = irq_get_irqchip_state(irq,
							IRQCHIP_STATE_PENDING,
							&pending);

			maskret = irq_get_irqchip_state(irq,
							IRQCHIP_STATE_MASKED,
							&masked);

			if (pendret == 0 && maskret == 0 &&
			    (pending && !masked))
				continue;
		}

		mask = 1 << (i % 32);
		writel_relaxed(mask, cirq_data->base + CIRQ_ACK + (i / 32) * 4);
	}

	/* set edge_only mode, record edge-triggerd interrupts */
	/* enable cirq */
	value = readl_relaxed(cirq_data->base + CIRQ_CONTROL);
	value |= (CIRQ_EDGE | CIRQ_EN);
	writel_relaxed(value, cirq_data->base + CIRQ_CONTROL);

	return 0;
}

static void mtk_cirq_resume(void)
{
	u32 value;

	/* flush recorded interrupts, will send signals to parent controller */
	value = readl_relaxed(cirq_data->base + CIRQ_CONTROL);
	writel_relaxed(value | CIRQ_FLUSH, cirq_data->base + CIRQ_CONTROL);

	/* disable cirq */
	value = readl_relaxed(cirq_data->base + CIRQ_CONTROL);
	value &= ~(CIRQ_EDGE | CIRQ_EN);
	writel_relaxed(value, cirq_data->base + CIRQ_CONTROL);
}

static struct syscore_ops mtk_cirq_syscore_ops = {
	.suspend	= mtk_cirq_suspend,
	.resume		= mtk_cirq_resume,
};

static void mtk_cirq_syscore_init(void)
{
	register_syscore_ops(&mtk_cirq_syscore_ops);
}
#else
static inline void mtk_cirq_syscore_init(void) {}
#endif

static int __init mtk_cirq_of_init(struct device_node *node,
				   struct device_node *parent)
{
	struct irq_domain *domain, *domain_parent;
	unsigned int irq_num;
	int ret;

	domain_parent = irq_find_host(parent);
	if (!domain_parent) {
		pr_err("mtk_cirq: interrupt-parent not found\n");
		return -EINVAL;
	}

	cirq_data = kzalloc(sizeof(*cirq_data), GFP_KERNEL);
	if (!cirq_data)
		return -ENOMEM;

	cirq_data->base = of_iomap(node, 0);
	if (!cirq_data->base) {
		pr_err("mtk_cirq: unable to map cirq register\n");
		ret = -ENXIO;
		goto out_free;
	}

	ret = of_property_read_u32_index(node, "mediatek,ext-irq-range", 0,
					 &cirq_data->ext_irq_start);
	if (ret)
		goto out_unmap;

	ret = of_property_read_u32_index(node, "mediatek,ext-irq-range", 1,
					 &cirq_data->ext_irq_end);
	if (ret)
		goto out_unmap;

	irq_num = cirq_data->ext_irq_end - cirq_data->ext_irq_start + 1;
	domain = irq_domain_add_hierarchy(domain_parent, 0,
					  irq_num, node,
					  &cirq_domain_ops, cirq_data);
	if (!domain) {
		ret = -ENOMEM;
		goto out_unmap;
	}
	cirq_data->domain = domain;

	mtk_cirq_syscore_init();

	return 0;

out_unmap:
	iounmap(cirq_data->base);
out_free:
	kfree(cirq_data);
	return ret;
}

IRQCHIP_DECLARE(mtk_cirq, "mediatek,mtk-cirq", mtk_cirq_of_init);

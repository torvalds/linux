// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH8100 External Interrupt Controller driver
 *
 * Copyright (C) 2023 StarFive Technology Co., Ltd.
 *
 * Author: Changhuang Liang <changhuang.liang@starfivetech.com>
 */

#define pr_fmt(fmt) "irq-starfive-jh8100: " fmt

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#define STARFIVE_INTC_SRC0_CLEAR	0x10
#define STARFIVE_INTC_SRC0_MASK		0x14
#define STARFIVE_INTC_SRC0_INT		0x1c

#define STARFIVE_INTC_SRC_IRQ_NUM	32

struct starfive_irq_chip {
	void __iomem		*base;
	struct irq_domain	*domain;
	raw_spinlock_t		lock;
};

static void starfive_intc_bit_set(struct starfive_irq_chip *irqc,
				  u32 reg, u32 bit_mask)
{
	u32 value;

	value = ioread32(irqc->base + reg);
	value |= bit_mask;
	iowrite32(value, irqc->base + reg);
}

static void starfive_intc_bit_clear(struct starfive_irq_chip *irqc,
				    u32 reg, u32 bit_mask)
{
	u32 value;

	value = ioread32(irqc->base + reg);
	value &= ~bit_mask;
	iowrite32(value, irqc->base + reg);
}

static void starfive_intc_unmask(struct irq_data *d)
{
	struct starfive_irq_chip *irqc = irq_data_get_irq_chip_data(d);

	raw_spin_lock(&irqc->lock);
	starfive_intc_bit_clear(irqc, STARFIVE_INTC_SRC0_MASK, BIT(d->hwirq));
	raw_spin_unlock(&irqc->lock);
}

static void starfive_intc_mask(struct irq_data *d)
{
	struct starfive_irq_chip *irqc = irq_data_get_irq_chip_data(d);

	raw_spin_lock(&irqc->lock);
	starfive_intc_bit_set(irqc, STARFIVE_INTC_SRC0_MASK, BIT(d->hwirq));
	raw_spin_unlock(&irqc->lock);
}

static struct irq_chip intc_dev = {
	.name		= "StarFive JH8100 INTC",
	.irq_unmask	= starfive_intc_unmask,
	.irq_mask	= starfive_intc_mask,
};

static int starfive_intc_map(struct irq_domain *d, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	irq_domain_set_info(d, irq, hwirq, &intc_dev, d->host_data,
			    handle_level_irq, NULL, NULL);

	return 0;
}

static const struct irq_domain_ops starfive_intc_domain_ops = {
	.xlate	= irq_domain_xlate_onecell,
	.map	= starfive_intc_map,
};

static void starfive_intc_irq_handler(struct irq_desc *desc)
{
	struct starfive_irq_chip *irqc = irq_data_get_irq_handler_data(&desc->irq_data);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long value;
	int hwirq;

	chained_irq_enter(chip, desc);

	value = ioread32(irqc->base + STARFIVE_INTC_SRC0_INT);
	while (value) {
		hwirq = ffs(value) - 1;

		generic_handle_domain_irq(irqc->domain, hwirq);

		starfive_intc_bit_set(irqc, STARFIVE_INTC_SRC0_CLEAR, BIT(hwirq));
		starfive_intc_bit_clear(irqc, STARFIVE_INTC_SRC0_CLEAR, BIT(hwirq));

		__clear_bit(hwirq, &value);
	}

	chained_irq_exit(chip, desc);
}

static int __init starfive_intc_init(struct device_node *intc,
				     struct device_node *parent)
{
	struct starfive_irq_chip *irqc;
	struct reset_control *rst;
	struct clk *clk;
	int parent_irq;
	int ret;

	irqc = kzalloc(sizeof(*irqc), GFP_KERNEL);
	if (!irqc)
		return -ENOMEM;

	irqc->base = of_iomap(intc, 0);
	if (!irqc->base) {
		pr_err("Unable to map registers\n");
		ret = -ENXIO;
		goto err_free;
	}

	rst = of_reset_control_get_exclusive(intc, NULL);
	if (IS_ERR(rst)) {
		pr_err("Unable to get reset control %pe\n", rst);
		ret = PTR_ERR(rst);
		goto err_unmap;
	}

	clk = of_clk_get(intc, 0);
	if (IS_ERR(clk)) {
		pr_err("Unable to get clock %pe\n", clk);
		ret = PTR_ERR(clk);
		goto err_reset_put;
	}

	ret = reset_control_deassert(rst);
	if (ret)
		goto err_clk_put;

	ret = clk_prepare_enable(clk);
	if (ret)
		goto err_reset_assert;

	raw_spin_lock_init(&irqc->lock);

	irqc->domain = irq_domain_create_linear(of_fwnode_handle(intc), STARFIVE_INTC_SRC_IRQ_NUM,
						&starfive_intc_domain_ops, irqc);
	if (!irqc->domain) {
		pr_err("Unable to create IRQ domain\n");
		ret = -EINVAL;
		goto err_clk_disable;
	}

	parent_irq = of_irq_get(intc, 0);
	if (parent_irq < 0) {
		pr_err("Failed to get main IRQ: %d\n", parent_irq);
		ret = parent_irq;
		goto err_remove_domain;
	}

	irq_set_chained_handler_and_data(parent_irq, starfive_intc_irq_handler,
					 irqc);

	pr_info("Interrupt controller register, nr_irqs %d\n",
		STARFIVE_INTC_SRC_IRQ_NUM);

	return 0;

err_remove_domain:
	irq_domain_remove(irqc->domain);
err_clk_disable:
	clk_disable_unprepare(clk);
err_reset_assert:
	reset_control_assert(rst);
err_clk_put:
	clk_put(clk);
err_reset_put:
	reset_control_put(rst);
err_unmap:
	iounmap(irqc->base);
err_free:
	kfree(irqc);
	return ret;
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(starfive_intc)
IRQCHIP_MATCH("starfive,jh8100-intc", starfive_intc_init)
IRQCHIP_PLATFORM_DRIVER_END(starfive_intc)

MODULE_DESCRIPTION("StarFive JH8100 External Interrupt Controller");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Changhuang Liang <changhuang.liang@starfivetech.com>");

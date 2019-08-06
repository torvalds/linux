// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2017 NXP
 * Copyright (C) 2018 Pengutronix, Lucas Stach <kernel@pengutronix.de>
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/spinlock.h>

#define CTRL_STRIDE_OFF(_t, _r)	(_t * 8 * _r)
#define CHANCTRL		0x0
#define CHANMASK(n, t)		(CTRL_STRIDE_OFF(t, 0) + 0x4 * (n) + 0x4)
#define CHANSET(n, t)		(CTRL_STRIDE_OFF(t, 1) + 0x4 * (n) + 0x4)
#define CHANSTATUS(n, t)	(CTRL_STRIDE_OFF(t, 2) + 0x4 * (n) + 0x4)
#define CHAN_MINTDIS(t)		(CTRL_STRIDE_OFF(t, 3) + 0x4)
#define CHAN_MASTRSTAT(t)	(CTRL_STRIDE_OFF(t, 3) + 0x8)

struct irqsteer_data {
	void __iomem		*regs;
	struct clk		*ipg_clk;
	int			irq;
	raw_spinlock_t		lock;
	int			irq_groups;
	int			channel;
	struct irq_domain	*domain;
	u32			*saved_reg;
};

static int imx_irqsteer_get_reg_index(struct irqsteer_data *data,
				      unsigned long irqnum)
{
	return (data->irq_groups * 2 - irqnum / 32 - 1);
}

static void imx_irqsteer_irq_unmask(struct irq_data *d)
{
	struct irqsteer_data *data = d->chip_data;
	int idx = imx_irqsteer_get_reg_index(data, d->hwirq);
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&data->lock, flags);
	val = readl_relaxed(data->regs + CHANMASK(idx, data->irq_groups));
	val |= BIT(d->hwirq % 32);
	writel_relaxed(val, data->regs + CHANMASK(idx, data->irq_groups));
	raw_spin_unlock_irqrestore(&data->lock, flags);
}

static void imx_irqsteer_irq_mask(struct irq_data *d)
{
	struct irqsteer_data *data = d->chip_data;
	int idx = imx_irqsteer_get_reg_index(data, d->hwirq);
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&data->lock, flags);
	val = readl_relaxed(data->regs + CHANMASK(idx, data->irq_groups));
	val &= ~BIT(d->hwirq % 32);
	writel_relaxed(val, data->regs + CHANMASK(idx, data->irq_groups));
	raw_spin_unlock_irqrestore(&data->lock, flags);
}

static struct irq_chip imx_irqsteer_irq_chip = {
	.name		= "irqsteer",
	.irq_mask	= imx_irqsteer_irq_mask,
	.irq_unmask	= imx_irqsteer_irq_unmask,
};

static int imx_irqsteer_irq_map(struct irq_domain *h, unsigned int irq,
				irq_hw_number_t hwirq)
{
	irq_set_status_flags(irq, IRQ_LEVEL);
	irq_set_chip_data(irq, h->host_data);
	irq_set_chip_and_handler(irq, &imx_irqsteer_irq_chip, handle_level_irq);

	return 0;
}

static const struct irq_domain_ops imx_irqsteer_domain_ops = {
	.map		= imx_irqsteer_irq_map,
	.xlate		= irq_domain_xlate_onecell,
};

static void imx_irqsteer_irq_handler(struct irq_desc *desc)
{
	struct irqsteer_data *data = irq_desc_get_handler_data(desc);
	int i;

	chained_irq_enter(irq_desc_get_chip(desc), desc);

	for (i = 0; i < data->irq_groups * 64; i += 32) {
		int idx = imx_irqsteer_get_reg_index(data, i);
		unsigned long irqmap;
		int pos, virq;

		irqmap = readl_relaxed(data->regs +
				       CHANSTATUS(idx, data->irq_groups));

		for_each_set_bit(pos, &irqmap, 32) {
			virq = irq_find_mapping(data->domain, pos + i);
			if (virq)
				generic_handle_irq(virq);
		}
	}

	chained_irq_exit(irq_desc_get_chip(desc), desc);
}

static int imx_irqsteer_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct irqsteer_data *data;
	struct resource *res;
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->regs)) {
		dev_err(&pdev->dev, "failed to initialize reg\n");
		return PTR_ERR(data->regs);
	}

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq <= 0) {
		dev_err(&pdev->dev, "failed to get irq\n");
		return -ENODEV;
	}

	data->ipg_clk = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(data->ipg_clk)) {
		ret = PTR_ERR(data->ipg_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get ipg clk: %d\n", ret);
		return ret;
	}

	raw_spin_lock_init(&data->lock);

	of_property_read_u32(np, "fsl,irq-groups", &data->irq_groups);
	of_property_read_u32(np, "fsl,channel", &data->channel);

	if (IS_ENABLED(CONFIG_PM_SLEEP)) {
		data->saved_reg = devm_kzalloc(&pdev->dev,
					sizeof(u32) * data->irq_groups * 2,
					GFP_KERNEL);
		if (!data->saved_reg)
			return -ENOMEM;
	}

	ret = clk_prepare_enable(data->ipg_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable ipg clk: %d\n", ret);
		return ret;
	}

	/* steer all IRQs into configured channel */
	writel_relaxed(BIT(data->channel), data->regs + CHANCTRL);

	data->domain = irq_domain_add_linear(np, data->irq_groups * 64,
					     &imx_irqsteer_domain_ops, data);
	if (!data->domain) {
		dev_err(&pdev->dev, "failed to create IRQ domain\n");
		clk_disable_unprepare(data->ipg_clk);
		return -ENOMEM;
	}

	irq_set_chained_handler_and_data(data->irq, imx_irqsteer_irq_handler,
					 data);

	platform_set_drvdata(pdev, data);

	return 0;
}

static int imx_irqsteer_remove(struct platform_device *pdev)
{
	struct irqsteer_data *irqsteer_data = platform_get_drvdata(pdev);

	irq_set_chained_handler_and_data(irqsteer_data->irq, NULL, NULL);
	irq_domain_remove(irqsteer_data->domain);

	clk_disable_unprepare(irqsteer_data->ipg_clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static void imx_irqsteer_save_regs(struct irqsteer_data *data)
{
	int i;

	for (i = 0; i < data->irq_groups * 2; i++)
		data->saved_reg[i] = readl_relaxed(data->regs +
						CHANMASK(i, data->irq_groups));
}

static void imx_irqsteer_restore_regs(struct irqsteer_data *data)
{
	int i;

	writel_relaxed(BIT(data->channel), data->regs + CHANCTRL);
	for (i = 0; i < data->irq_groups * 2; i++)
		writel_relaxed(data->saved_reg[i],
			       data->regs + CHANMASK(i, data->irq_groups));
}

static int imx_irqsteer_suspend(struct device *dev)
{
	struct irqsteer_data *irqsteer_data = dev_get_drvdata(dev);

	imx_irqsteer_save_regs(irqsteer_data);
	clk_disable_unprepare(irqsteer_data->ipg_clk);

	return 0;
}

static int imx_irqsteer_resume(struct device *dev)
{
	struct irqsteer_data *irqsteer_data = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(irqsteer_data->ipg_clk);
	if (ret) {
		dev_err(dev, "failed to enable ipg clk: %d\n", ret);
		return ret;
	}
	imx_irqsteer_restore_regs(irqsteer_data);

	return 0;
}
#endif

static const struct dev_pm_ops imx_irqsteer_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(imx_irqsteer_suspend, imx_irqsteer_resume)
};

static const struct of_device_id imx_irqsteer_dt_ids[] = {
	{ .compatible = "fsl,imx-irqsteer", },
	{},
};

static struct platform_driver imx_irqsteer_driver = {
	.driver = {
		.name = "imx-irqsteer",
		.of_match_table = imx_irqsteer_dt_ids,
		.pm = &imx_irqsteer_pm_ops,
	},
	.probe = imx_irqsteer_probe,
	.remove = imx_irqsteer_remove,
};
builtin_platform_driver(imx_irqsteer_driver);

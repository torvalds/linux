// SPDX-License-Identifier: GPL-2.0
// Copyright 2017 NXP

/*                     INTMUX Block Diagram
 *
 *                               ________________
 * interrupt source #  0  +---->|                |
 *                        |     |                |
 * interrupt source #  1  +++-->|                |
 *            ...         | |   |   channel # 0  |--------->interrupt out # 0
 *            ...         | |   |                |
 *            ...         | |   |                |
 * interrupt source # X-1 +++-->|________________|
 *                        | | |
 *                        | | |
 *                        | | |  ________________
 *                        +---->|                |
 *                        | | | |                |
 *                        | +-->|                |
 *                        | | | |   channel # 1  |--------->interrupt out # 1
 *                        | | +>|                |
 *                        | | | |                |
 *                        | | | |________________|
 *                        | | |
 *                        | | |
 *                        | | |       ...
 *                        | | |       ...
 *                        | | |
 *                        | | |  ________________
 *                        +---->|                |
 *                          | | |                |
 *                          +-->|                |
 *                            | |   channel # N  |--------->interrupt out # N
 *                            +>|                |
 *                              |                |
 *                              |________________|
 *
 *
 * N: Interrupt Channel Instance Number (N=7)
 * X: Interrupt Source Number for each channel (X=32)
 *
 * The INTMUX interrupt multiplexer has 8 channels, each channel receives 32
 * interrupt sources and generates 1 interrupt output.
 *
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/spinlock.h>
#include <linux/pm_runtime.h>

#define CHANIER(n)	(0x10 + (0x40 * n))
#define CHANIPR(n)	(0x20 + (0x40 * n))

#define CHAN_MAX_NUM		0x8

struct intmux_irqchip_data {
	u32			saved_reg;
	int			chanidx;
	int			irq;
	struct irq_domain	*domain;
};

struct intmux_data {
	raw_spinlock_t			lock;
	void __iomem			*regs;
	struct clk			*ipg_clk;
	int				channum;
	struct intmux_irqchip_data	irqchip_data[];
};

static void imx_intmux_irq_mask(struct irq_data *d)
{
	struct intmux_irqchip_data *irqchip_data = d->chip_data;
	int idx = irqchip_data->chanidx;
	struct intmux_data *data = container_of(irqchip_data, struct intmux_data,
						irqchip_data[idx]);
	unsigned long flags;
	void __iomem *reg;
	u32 val;

	raw_spin_lock_irqsave(&data->lock, flags);
	reg = data->regs + CHANIER(idx);
	val = readl_relaxed(reg);
	/* disable the interrupt source of this channel */
	val &= ~BIT(d->hwirq);
	writel_relaxed(val, reg);
	raw_spin_unlock_irqrestore(&data->lock, flags);
}

static void imx_intmux_irq_unmask(struct irq_data *d)
{
	struct intmux_irqchip_data *irqchip_data = d->chip_data;
	int idx = irqchip_data->chanidx;
	struct intmux_data *data = container_of(irqchip_data, struct intmux_data,
						irqchip_data[idx]);
	unsigned long flags;
	void __iomem *reg;
	u32 val;

	raw_spin_lock_irqsave(&data->lock, flags);
	reg = data->regs + CHANIER(idx);
	val = readl_relaxed(reg);
	/* enable the interrupt source of this channel */
	val |= BIT(d->hwirq);
	writel_relaxed(val, reg);
	raw_spin_unlock_irqrestore(&data->lock, flags);
}

static struct irq_chip imx_intmux_irq_chip __ro_after_init = {
	.name		= "intmux",
	.irq_mask	= imx_intmux_irq_mask,
	.irq_unmask	= imx_intmux_irq_unmask,
};

static int imx_intmux_irq_map(struct irq_domain *h, unsigned int irq,
			      irq_hw_number_t hwirq)
{
	struct intmux_irqchip_data *data = h->host_data;

	irq_set_chip_data(irq, data);
	irq_set_chip_and_handler(irq, &imx_intmux_irq_chip, handle_level_irq);

	return 0;
}

static int imx_intmux_irq_xlate(struct irq_domain *d, struct device_node *node,
				const u32 *intspec, unsigned int intsize,
				unsigned long *out_hwirq, unsigned int *out_type)
{
	struct intmux_irqchip_data *irqchip_data = d->host_data;
	int idx = irqchip_data->chanidx;
	struct intmux_data *data = container_of(irqchip_data, struct intmux_data,
						irqchip_data[idx]);

	/*
	 * two cells needed in interrupt specifier:
	 * the 1st cell: hw interrupt number
	 * the 2nd cell: channel index
	 */
	if (WARN_ON(intsize != 2))
		return -EINVAL;

	if (WARN_ON(intspec[1] >= data->channum))
		return -EINVAL;

	*out_hwirq = intspec[0];
	*out_type = IRQ_TYPE_LEVEL_HIGH;

	return 0;
}

static int imx_intmux_irq_select(struct irq_domain *d, struct irq_fwspec *fwspec,
				 enum irq_domain_bus_token bus_token)
{
	struct intmux_irqchip_data *irqchip_data = d->host_data;

	/* Not for us */
	if (fwspec->fwnode != d->fwnode)
		return false;

	return irqchip_data->chanidx == fwspec->param[1];
}

static const struct irq_domain_ops imx_intmux_domain_ops = {
	.map		= imx_intmux_irq_map,
	.xlate		= imx_intmux_irq_xlate,
	.select		= imx_intmux_irq_select,
};

static void imx_intmux_irq_handler(struct irq_desc *desc)
{
	struct intmux_irqchip_data *irqchip_data = irq_desc_get_handler_data(desc);
	int idx = irqchip_data->chanidx;
	struct intmux_data *data = container_of(irqchip_data, struct intmux_data,
						irqchip_data[idx]);
	unsigned long irqstat;
	int pos;

	chained_irq_enter(irq_desc_get_chip(desc), desc);

	/* read the interrupt source pending status of this channel */
	irqstat = readl_relaxed(data->regs + CHANIPR(idx));

	for_each_set_bit(pos, &irqstat, 32)
		generic_handle_domain_irq(irqchip_data->domain, pos);

	chained_irq_exit(irq_desc_get_chip(desc), desc);
}

static int imx_intmux_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct irq_domain *domain;
	struct intmux_data *data;
	int channum;
	int i, ret;

	channum = platform_irq_count(pdev);
	if (channum == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (channum > CHAN_MAX_NUM) {
		dev_err(&pdev->dev, "supports up to %d multiplex channels\n",
			CHAN_MAX_NUM);
		return -EINVAL;
	}

	data = devm_kzalloc(&pdev->dev, struct_size(data, irqchip_data, channum), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->regs)) {
		dev_err(&pdev->dev, "failed to initialize reg\n");
		return PTR_ERR(data->regs);
	}

	data->ipg_clk = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(data->ipg_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(data->ipg_clk),
				     "failed to get ipg clk\n");

	data->channum = channum;
	raw_spin_lock_init(&data->lock);

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = clk_prepare_enable(data->ipg_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable ipg clk: %d\n", ret);
		return ret;
	}

	for (i = 0; i < channum; i++) {
		data->irqchip_data[i].chanidx = i;

		data->irqchip_data[i].irq = irq_of_parse_and_map(np, i);
		if (data->irqchip_data[i].irq <= 0) {
			ret = -EINVAL;
			dev_err(&pdev->dev, "failed to get irq\n");
			goto out;
		}

		domain = irq_domain_add_linear(np, 32, &imx_intmux_domain_ops,
					       &data->irqchip_data[i]);
		if (!domain) {
			ret = -ENOMEM;
			dev_err(&pdev->dev, "failed to create IRQ domain\n");
			goto out;
		}
		data->irqchip_data[i].domain = domain;
		irq_domain_set_pm_device(domain, &pdev->dev);

		/* disable all interrupt sources of this channel firstly */
		writel_relaxed(0, data->regs + CHANIER(i));

		irq_set_chained_handler_and_data(data->irqchip_data[i].irq,
						 imx_intmux_irq_handler,
						 &data->irqchip_data[i]);
	}

	platform_set_drvdata(pdev, data);

	/*
	 * Let pm_runtime_put() disable clock.
	 * If CONFIG_PM is not enabled, the clock will stay powered.
	 */
	pm_runtime_put(&pdev->dev);

	return 0;
out:
	clk_disable_unprepare(data->ipg_clk);
	return ret;
}

static int imx_intmux_remove(struct platform_device *pdev)
{
	struct intmux_data *data = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < data->channum; i++) {
		/* disable all interrupt sources of this channel */
		writel_relaxed(0, data->regs + CHANIER(i));

		irq_set_chained_handler_and_data(data->irqchip_data[i].irq,
						 NULL, NULL);

		irq_domain_remove(data->irqchip_data[i].domain);
	}

	pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
static int imx_intmux_runtime_suspend(struct device *dev)
{
	struct intmux_data *data = dev_get_drvdata(dev);
	struct intmux_irqchip_data *irqchip_data;
	int i;

	for (i = 0; i < data->channum; i++) {
		irqchip_data = &data->irqchip_data[i];
		irqchip_data->saved_reg = readl_relaxed(data->regs + CHANIER(i));
	}

	clk_disable_unprepare(data->ipg_clk);

	return 0;
}

static int imx_intmux_runtime_resume(struct device *dev)
{
	struct intmux_data *data = dev_get_drvdata(dev);
	struct intmux_irqchip_data *irqchip_data;
	int ret, i;

	ret = clk_prepare_enable(data->ipg_clk);
	if (ret) {
		dev_err(dev, "failed to enable ipg clk: %d\n", ret);
		return ret;
	}

	for (i = 0; i < data->channum; i++) {
		irqchip_data = &data->irqchip_data[i];
		writel_relaxed(irqchip_data->saved_reg, data->regs + CHANIER(i));
	}

	return 0;
}
#endif

static const struct dev_pm_ops imx_intmux_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				      pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(imx_intmux_runtime_suspend,
			   imx_intmux_runtime_resume, NULL)
};

static const struct of_device_id imx_intmux_id[] = {
	{ .compatible = "fsl,imx-intmux", },
	{ /* sentinel */ },
};

static struct platform_driver imx_intmux_driver = {
	.driver = {
		.name = "imx-intmux",
		.of_match_table = imx_intmux_id,
		.pm = &imx_intmux_pm_ops,
	},
	.probe = imx_intmux_probe,
	.remove = imx_intmux_remove,
};
builtin_platform_driver(imx_intmux_driver);

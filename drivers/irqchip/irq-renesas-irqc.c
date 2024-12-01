// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas IRQC Driver
 *
 *  Copyright (C) 2013 Magnus Damm
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#define IRQC_IRQ_MAX	32	/* maximum 32 interrupts per driver instance */

#define IRQC_REQ_STS	0x00	/* Interrupt Request Status Register */
#define IRQC_EN_STS	0x04	/* Interrupt Enable Status Register */
#define IRQC_EN_SET	0x08	/* Interrupt Enable Set Register */
#define IRQC_INT_CPU_BASE(n) (0x000 + ((n) * 0x10))
				/* SYS-CPU vs. RT-CPU */
#define DETECT_STATUS	0x100	/* IRQn Detect Status Register */
#define MONITOR		0x104	/* IRQn Signal Level Monitor Register */
#define HLVL_STS	0x108	/* IRQn High Level Detect Status Register */
#define LLVL_STS	0x10c	/* IRQn Low Level Detect Status Register */
#define S_R_EDGE_STS	0x110	/* IRQn Sync Rising Edge Detect Status Reg. */
#define S_F_EDGE_STS	0x114	/* IRQn Sync Falling Edge Detect Status Reg. */
#define A_R_EDGE_STS	0x118	/* IRQn Async Rising Edge Detect Status Reg. */
#define A_F_EDGE_STS	0x11c	/* IRQn Async Falling Edge Detect Status Reg. */
#define CHTEN_STS	0x120	/* Chattering Reduction Status Register */
#define IRQC_CONFIG(n) (0x180 + ((n) * 0x04))
				/* IRQn Configuration Register */

struct irqc_irq {
	int hw_irq;
	int requested_irq;
	struct irqc_priv *p;
};

struct irqc_priv {
	void __iomem *iomem;
	void __iomem *cpu_int_base;
	struct irqc_irq irq[IRQC_IRQ_MAX];
	unsigned int number_of_irqs;
	struct device *dev;
	struct irq_chip_generic *gc;
	struct irq_domain *irq_domain;
	atomic_t wakeup_path;
};

static struct irqc_priv *irq_data_to_priv(struct irq_data *data)
{
	return data->domain->host_data;
}

static void irqc_dbg(struct irqc_irq *i, char *str)
{
	dev_dbg(i->p->dev, "%s (%d:%d)\n", str, i->requested_irq, i->hw_irq);
}

static unsigned char irqc_sense[IRQ_TYPE_SENSE_MASK + 1] = {
	[IRQ_TYPE_LEVEL_LOW]	= 0x01,
	[IRQ_TYPE_LEVEL_HIGH]	= 0x02,
	[IRQ_TYPE_EDGE_FALLING]	= 0x04,	/* Synchronous */
	[IRQ_TYPE_EDGE_RISING]	= 0x08,	/* Synchronous */
	[IRQ_TYPE_EDGE_BOTH]	= 0x0c,	/* Synchronous */
};

static int irqc_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct irqc_priv *p = irq_data_to_priv(d);
	int hw_irq = irqd_to_hwirq(d);
	unsigned char value = irqc_sense[type & IRQ_TYPE_SENSE_MASK];
	u32 tmp;

	irqc_dbg(&p->irq[hw_irq], "sense");

	if (!value)
		return -EINVAL;

	tmp = ioread32(p->iomem + IRQC_CONFIG(hw_irq));
	tmp &= ~0x3f;
	tmp |= value;
	iowrite32(tmp, p->iomem + IRQC_CONFIG(hw_irq));
	return 0;
}

static int irqc_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct irqc_priv *p = irq_data_to_priv(d);
	int hw_irq = irqd_to_hwirq(d);

	irq_set_irq_wake(p->irq[hw_irq].requested_irq, on);
	if (on)
		atomic_inc(&p->wakeup_path);
	else
		atomic_dec(&p->wakeup_path);

	return 0;
}

static irqreturn_t irqc_irq_handler(int irq, void *dev_id)
{
	struct irqc_irq *i = dev_id;
	struct irqc_priv *p = i->p;
	u32 bit = BIT(i->hw_irq);

	irqc_dbg(i, "demux1");

	if (ioread32(p->iomem + DETECT_STATUS) & bit) {
		iowrite32(bit, p->iomem + DETECT_STATUS);
		irqc_dbg(i, "demux2");
		generic_handle_domain_irq(p->irq_domain, i->hw_irq);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int irqc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const char *name = dev_name(dev);
	struct irqc_priv *p;
	int ret;
	int k;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->dev = dev;
	platform_set_drvdata(pdev, p);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	/* allow any number of IRQs between 1 and IRQC_IRQ_MAX */
	for (k = 0; k < IRQC_IRQ_MAX; k++) {
		ret = platform_get_irq_optional(pdev, k);
		if (ret == -ENXIO)
			break;
		if (ret < 0)
			goto err_runtime_pm_disable;

		p->irq[k].p = p;
		p->irq[k].hw_irq = k;
		p->irq[k].requested_irq = ret;
	}

	p->number_of_irqs = k;
	if (p->number_of_irqs < 1) {
		dev_err(dev, "not enough IRQ resources\n");
		ret = -EINVAL;
		goto err_runtime_pm_disable;
	}

	/* ioremap IOMEM and setup read/write callbacks */
	p->iomem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(p->iomem)) {
		ret = PTR_ERR(p->iomem);
		goto err_runtime_pm_disable;
	}

	p->cpu_int_base = p->iomem + IRQC_INT_CPU_BASE(0); /* SYS-SPI */

	p->irq_domain = irq_domain_add_linear(dev->of_node, p->number_of_irqs,
					      &irq_generic_chip_ops, p);
	if (!p->irq_domain) {
		ret = -ENXIO;
		dev_err(dev, "cannot initialize irq domain\n");
		goto err_runtime_pm_disable;
	}

	ret = irq_alloc_domain_generic_chips(p->irq_domain, p->number_of_irqs,
					     1, "irqc", handle_level_irq,
					     0, 0, IRQ_GC_INIT_NESTED_LOCK);
	if (ret) {
		dev_err(dev, "cannot allocate generic chip\n");
		goto err_remove_domain;
	}

	p->gc = irq_get_domain_generic_chip(p->irq_domain, 0);
	p->gc->reg_base = p->cpu_int_base;
	p->gc->chip_types[0].regs.enable = IRQC_EN_SET;
	p->gc->chip_types[0].regs.disable = IRQC_EN_STS;
	p->gc->chip_types[0].chip.irq_mask = irq_gc_mask_disable_reg;
	p->gc->chip_types[0].chip.irq_unmask = irq_gc_unmask_enable_reg;
	p->gc->chip_types[0].chip.irq_set_type	= irqc_irq_set_type;
	p->gc->chip_types[0].chip.irq_set_wake	= irqc_irq_set_wake;
	p->gc->chip_types[0].chip.flags	= IRQCHIP_MASK_ON_SUSPEND;

	irq_domain_set_pm_device(p->irq_domain, dev);

	/* request interrupts one by one */
	for (k = 0; k < p->number_of_irqs; k++) {
		if (devm_request_irq(dev, p->irq[k].requested_irq,
				     irqc_irq_handler, 0, name, &p->irq[k])) {
			dev_err(dev, "failed to request IRQ\n");
			ret = -ENOENT;
			goto err_remove_domain;
		}
	}

	dev_info(dev, "driving %d irqs\n", p->number_of_irqs);

	return 0;

err_remove_domain:
	irq_domain_remove(p->irq_domain);
err_runtime_pm_disable:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);
	return ret;
}

static void irqc_remove(struct platform_device *pdev)
{
	struct irqc_priv *p = platform_get_drvdata(pdev);

	irq_domain_remove(p->irq_domain);
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

static int __maybe_unused irqc_suspend(struct device *dev)
{
	struct irqc_priv *p = dev_get_drvdata(dev);

	if (atomic_read(&p->wakeup_path))
		device_set_wakeup_path(dev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(irqc_pm_ops, irqc_suspend, NULL);

static const struct of_device_id irqc_dt_ids[] = {
	{ .compatible = "renesas,irqc", },
	{},
};
MODULE_DEVICE_TABLE(of, irqc_dt_ids);

static struct platform_driver irqc_device_driver = {
	.probe		= irqc_probe,
	.remove		= irqc_remove,
	.driver		= {
		.name		= "renesas_irqc",
		.of_match_table	= irqc_dt_ids,
		.pm		= &irqc_pm_ops,
	}
};

static int __init irqc_init(void)
{
	return platform_driver_register(&irqc_device_driver);
}
postcore_initcall(irqc_init);

static void __exit irqc_exit(void)
{
	platform_driver_unregister(&irqc_device_driver);
}
module_exit(irqc_exit);

MODULE_AUTHOR("Magnus Damm");
MODULE_DESCRIPTION("Renesas IRQC Driver");

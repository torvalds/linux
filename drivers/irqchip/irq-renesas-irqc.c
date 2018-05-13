/*
 * Renesas IRQC Driver
 *
 *  Copyright (C) 2013 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
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
	struct platform_device *pdev;
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
	dev_dbg(&i->p->pdev->dev, "%s (%d:%d)\n",
		str, i->requested_irq, i->hw_irq);
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
		generic_handle_irq(irq_find_mapping(p->irq_domain, i->hw_irq));
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int irqc_probe(struct platform_device *pdev)
{
	struct irqc_priv *p;
	struct resource *io;
	struct resource *irq;
	const char *name = dev_name(&pdev->dev);
	int ret;
	int k;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		ret = -ENOMEM;
		goto err0;
	}

	p->pdev = pdev;
	platform_set_drvdata(pdev, p);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	/* get hold of manadatory IOMEM */
	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!io) {
		dev_err(&pdev->dev, "not enough IOMEM resources\n");
		ret = -EINVAL;
		goto err1;
	}

	/* allow any number of IRQs between 1 and IRQC_IRQ_MAX */
	for (k = 0; k < IRQC_IRQ_MAX; k++) {
		irq = platform_get_resource(pdev, IORESOURCE_IRQ, k);
		if (!irq)
			break;

		p->irq[k].p = p;
		p->irq[k].hw_irq = k;
		p->irq[k].requested_irq = irq->start;
	}

	p->number_of_irqs = k;
	if (p->number_of_irqs < 1) {
		dev_err(&pdev->dev, "not enough IRQ resources\n");
		ret = -EINVAL;
		goto err1;
	}

	/* ioremap IOMEM and setup read/write callbacks */
	p->iomem = ioremap_nocache(io->start, resource_size(io));
	if (!p->iomem) {
		dev_err(&pdev->dev, "failed to remap IOMEM\n");
		ret = -ENXIO;
		goto err2;
	}

	p->cpu_int_base = p->iomem + IRQC_INT_CPU_BASE(0); /* SYS-SPI */

	p->irq_domain = irq_domain_add_linear(pdev->dev.of_node,
					      p->number_of_irqs,
					      &irq_generic_chip_ops, p);
	if (!p->irq_domain) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "cannot initialize irq domain\n");
		goto err2;
	}

	ret = irq_alloc_domain_generic_chips(p->irq_domain, p->number_of_irqs,
					     1, name, handle_level_irq,
					     0, 0, IRQ_GC_INIT_NESTED_LOCK);
	if (ret) {
		dev_err(&pdev->dev, "cannot allocate generic chip\n");
		goto err3;
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

	/* request interrupts one by one */
	for (k = 0; k < p->number_of_irqs; k++) {
		if (request_irq(p->irq[k].requested_irq, irqc_irq_handler,
				0, name, &p->irq[k])) {
			dev_err(&pdev->dev, "failed to request IRQ\n");
			ret = -ENOENT;
			goto err4;
		}
	}

	dev_info(&pdev->dev, "driving %d irqs\n", p->number_of_irqs);

	return 0;
err4:
	while (--k >= 0)
		free_irq(p->irq[k].requested_irq, &p->irq[k]);

err3:
	irq_domain_remove(p->irq_domain);
err2:
	iounmap(p->iomem);
err1:
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	kfree(p);
err0:
	return ret;
}

static int irqc_remove(struct platform_device *pdev)
{
	struct irqc_priv *p = platform_get_drvdata(pdev);
	int k;

	for (k = 0; k < p->number_of_irqs; k++)
		free_irq(p->irq[k].requested_irq, &p->irq[k]);

	irq_domain_remove(p->irq_domain);
	iounmap(p->iomem);
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	kfree(p);
	return 0;
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
		.name	= "renesas_irqc",
		.of_match_table	= irqc_dt_ids,
		.pm	= &irqc_pm_ops,
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
MODULE_LICENSE("GPL v2");

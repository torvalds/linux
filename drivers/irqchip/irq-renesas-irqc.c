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
#include <linux/platform_data/irq-renesas-irqc.h>

#define IRQC_IRQ_MAX 32 /* maximum 32 interrupts per driver instance */

#define IRQC_REQ_STS 0x00
#define IRQC_EN_STS 0x04
#define IRQC_EN_SET 0x08
#define IRQC_INT_CPU_BASE(n) (0x000 + ((n) * 0x10))
#define DETECT_STATUS 0x100
#define IRQC_CONFIG(n) (0x180 + ((n) * 0x04))

struct irqc_irq {
	int hw_irq;
	int requested_irq;
	int domain_irq;
	struct irqc_priv *p;
};

struct irqc_priv {
	void __iomem *iomem;
	void __iomem *cpu_int_base;
	struct irqc_irq irq[IRQC_IRQ_MAX];
	struct renesas_irqc_config config;
	unsigned int number_of_irqs;
	struct platform_device *pdev;
	struct irq_chip irq_chip;
	struct irq_domain *irq_domain;
};

static void irqc_dbg(struct irqc_irq *i, char *str)
{
	dev_dbg(&i->p->pdev->dev, "%s (%d:%d:%d)\n",
		str, i->requested_irq, i->hw_irq, i->domain_irq);
}

static void irqc_irq_enable(struct irq_data *d)
{
	struct irqc_priv *p = irq_data_get_irq_chip_data(d);
	int hw_irq = irqd_to_hwirq(d);

	irqc_dbg(&p->irq[hw_irq], "enable");
	iowrite32(BIT(hw_irq), p->cpu_int_base + IRQC_EN_SET);
}

static void irqc_irq_disable(struct irq_data *d)
{
	struct irqc_priv *p = irq_data_get_irq_chip_data(d);
	int hw_irq = irqd_to_hwirq(d);

	irqc_dbg(&p->irq[hw_irq], "disable");
	iowrite32(BIT(hw_irq), p->cpu_int_base + IRQC_EN_STS);
}

#define INTC_IRQ_SENSE_VALID 0x10
#define INTC_IRQ_SENSE(x) (x + INTC_IRQ_SENSE_VALID)

static unsigned char irqc_sense[IRQ_TYPE_SENSE_MASK + 1] = {
	[IRQ_TYPE_LEVEL_LOW] = INTC_IRQ_SENSE(0x01),
	[IRQ_TYPE_LEVEL_HIGH] = INTC_IRQ_SENSE(0x02),
	[IRQ_TYPE_EDGE_FALLING] = INTC_IRQ_SENSE(0x04), /* Synchronous */
	[IRQ_TYPE_EDGE_RISING] = INTC_IRQ_SENSE(0x08), /* Synchronous */
	[IRQ_TYPE_EDGE_BOTH] = INTC_IRQ_SENSE(0x0c),  /* Synchronous */
};

static int irqc_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct irqc_priv *p = irq_data_get_irq_chip_data(d);
	int hw_irq = irqd_to_hwirq(d);
	unsigned char value = irqc_sense[type & IRQ_TYPE_SENSE_MASK];
	unsigned long tmp;

	irqc_dbg(&p->irq[hw_irq], "sense");

	if (!(value & INTC_IRQ_SENSE_VALID))
		return -EINVAL;

	tmp = ioread32(p->iomem + IRQC_CONFIG(hw_irq));
	tmp &= ~0x3f;
	tmp |= value ^ INTC_IRQ_SENSE_VALID;
	iowrite32(tmp, p->iomem + IRQC_CONFIG(hw_irq));
	return 0;
}

static irqreturn_t irqc_irq_handler(int irq, void *dev_id)
{
	struct irqc_irq *i = dev_id;
	struct irqc_priv *p = i->p;
	unsigned long bit = BIT(i->hw_irq);

	irqc_dbg(i, "demux1");

	if (ioread32(p->iomem + DETECT_STATUS) & bit) {
		iowrite32(bit, p->iomem + DETECT_STATUS);
		irqc_dbg(i, "demux2");
		generic_handle_irq(i->domain_irq);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int irqc_irq_domain_map(struct irq_domain *h, unsigned int virq,
			       irq_hw_number_t hw)
{
	struct irqc_priv *p = h->host_data;

	p->irq[hw].domain_irq = virq;
	p->irq[hw].hw_irq = hw;

	irqc_dbg(&p->irq[hw], "map");
	irq_set_chip_data(virq, h->host_data);
	irq_set_chip_and_handler(virq, &p->irq_chip, handle_level_irq);
	set_irq_flags(virq, IRQF_VALID); /* kill me now */
	return 0;
}

static struct irq_domain_ops irqc_irq_domain_ops = {
	.map	= irqc_irq_domain_map,
	.xlate  = irq_domain_xlate_twocell,
};

static int irqc_probe(struct platform_device *pdev)
{
	struct renesas_irqc_config *pdata = pdev->dev.platform_data;
	struct irqc_priv *p;
	struct resource *io;
	struct resource *irq;
	struct irq_chip *irq_chip;
	const char *name = dev_name(&pdev->dev);
	int ret;
	int k;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		ret = -ENOMEM;
		goto err0;
	}

	/* deal with driver instance configuration */
	if (pdata)
		memcpy(&p->config, pdata, sizeof(*pdata));

	p->pdev = pdev;
	platform_set_drvdata(pdev, p);

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

	irq_chip = &p->irq_chip;
	irq_chip->name = name;
	irq_chip->irq_mask = irqc_irq_disable;
	irq_chip->irq_unmask = irqc_irq_enable;
	irq_chip->irq_set_type = irqc_irq_set_type;
	irq_chip->flags	= IRQCHIP_SKIP_SET_WAKE;

	p->irq_domain = irq_domain_add_simple(pdev->dev.of_node,
					      p->number_of_irqs,
					      p->config.irq_base,
					      &irqc_irq_domain_ops, p);
	if (!p->irq_domain) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "cannot initialize irq domain\n");
		goto err2;
	}

	/* request interrupts one by one */
	for (k = 0; k < p->number_of_irqs; k++) {
		if (request_irq(p->irq[k].requested_irq, irqc_irq_handler,
				0, name, &p->irq[k])) {
			dev_err(&pdev->dev, "failed to request IRQ\n");
			ret = -ENOENT;
			goto err3;
		}
	}

	dev_info(&pdev->dev, "driving %d irqs\n", p->number_of_irqs);

	/* warn in case of mismatch if irq base is specified */
	if (p->config.irq_base) {
		if (p->config.irq_base != p->irq[0].domain_irq)
			dev_warn(&pdev->dev, "irq base mismatch (%d/%d)\n",
				 p->config.irq_base, p->irq[0].domain_irq);
	}

	return 0;
err3:
	while (--k >= 0)
		free_irq(p->irq[k].requested_irq, &p->irq[k]);

	irq_domain_remove(p->irq_domain);
err2:
	iounmap(p->iomem);
err1:
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
	kfree(p);
	return 0;
}

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
		.owner	= THIS_MODULE,
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

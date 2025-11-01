/*
 * Copyright (C) 2016 Marvell
 *
 * Yehuda Yitschak <yehuday@marvell.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>

#define PIC_CAUSE	       0x0
#define PIC_MASK	       0x4

#define PIC_MAX_IRQS		32
#define PIC_MAX_IRQ_MASK	((1UL << PIC_MAX_IRQS) - 1)

struct mvebu_pic {
	void __iomem *base;
	u32 parent_irq;
	struct irq_domain *domain;
	struct platform_device *pdev;
};

static void mvebu_pic_reset(struct mvebu_pic *pic)
{
	/* ACK and mask all interrupts */
	writel(0, pic->base + PIC_MASK);
	writel(PIC_MAX_IRQ_MASK, pic->base + PIC_CAUSE);
}

static void mvebu_pic_eoi_irq(struct irq_data *d)
{
	struct mvebu_pic *pic = irq_data_get_irq_chip_data(d);

	writel(1 << d->hwirq, pic->base + PIC_CAUSE);
}

static void mvebu_pic_mask_irq(struct irq_data *d)
{
	struct mvebu_pic *pic = irq_data_get_irq_chip_data(d);
	u32 reg;

	reg =  readl(pic->base + PIC_MASK);
	reg |= (1 << d->hwirq);
	writel(reg, pic->base + PIC_MASK);
}

static void mvebu_pic_unmask_irq(struct irq_data *d)
{
	struct mvebu_pic *pic = irq_data_get_irq_chip_data(d);
	u32 reg;

	reg = readl(pic->base + PIC_MASK);
	reg &= ~(1 << d->hwirq);
	writel(reg, pic->base + PIC_MASK);
}

static void mvebu_pic_print_chip(struct irq_data *d, struct seq_file *p)
{
	struct mvebu_pic *pic = irq_data_get_irq_chip_data(d);

	seq_puts(p, dev_name(&pic->pdev->dev));
}

static const struct irq_chip mvebu_pic_chip = {
	.irq_mask	= mvebu_pic_mask_irq,
	.irq_unmask	= mvebu_pic_unmask_irq,
	.irq_eoi	= mvebu_pic_eoi_irq,
	.irq_print_chip	= mvebu_pic_print_chip,
};

static int mvebu_pic_irq_map(struct irq_domain *domain, unsigned int virq,
			     irq_hw_number_t hwirq)
{
	struct mvebu_pic *pic = domain->host_data;

	irq_set_percpu_devid(virq);
	irq_set_chip_data(virq, pic);
	irq_set_chip_and_handler(virq, &mvebu_pic_chip, handle_percpu_devid_irq);
	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_probe(virq);

	return 0;
}

static const struct irq_domain_ops mvebu_pic_domain_ops = {
	.map = mvebu_pic_irq_map,
	.xlate = irq_domain_xlate_onecell,
};

static void mvebu_pic_handle_cascade_irq(struct irq_desc *desc)
{
	struct mvebu_pic *pic = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long irqmap, irqn;

	irqmap = readl_relaxed(pic->base + PIC_CAUSE);
	chained_irq_enter(chip, desc);

	for_each_set_bit(irqn, &irqmap, BITS_PER_LONG)
		generic_handle_domain_irq(pic->domain, irqn);

	chained_irq_exit(chip, desc);
}

static void mvebu_pic_enable_percpu_irq(void *data)
{
	struct mvebu_pic *pic = data;

	mvebu_pic_reset(pic);
	enable_percpu_irq(pic->parent_irq, IRQ_TYPE_NONE);
}

static void mvebu_pic_disable_percpu_irq(void *data)
{
	struct mvebu_pic *pic = data;

	disable_percpu_irq(pic->parent_irq);
}

static int mvebu_pic_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct mvebu_pic *pic;

	pic = devm_kzalloc(&pdev->dev, sizeof(struct mvebu_pic), GFP_KERNEL);
	if (!pic)
		return -ENOMEM;

	pic->pdev = pdev;
	pic->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pic->base))
		return PTR_ERR(pic->base);

	pic->parent_irq = irq_of_parse_and_map(node, 0);
	if (pic->parent_irq <= 0) {
		dev_err(&pdev->dev, "Failed to parse parent interrupt\n");
		return -EINVAL;
	}

	pic->domain = irq_domain_create_linear(dev_fwnode(&pdev->dev), PIC_MAX_IRQS,
					       &mvebu_pic_domain_ops, pic);
	if (!pic->domain) {
		dev_err(&pdev->dev, "Failed to allocate irq domain\n");
		return -ENOMEM;
	}

	irq_set_chained_handler(pic->parent_irq, mvebu_pic_handle_cascade_irq);
	irq_set_handler_data(pic->parent_irq, pic);

	on_each_cpu(mvebu_pic_enable_percpu_irq, pic, 1);

	platform_set_drvdata(pdev, pic);

	return 0;
}

static void mvebu_pic_remove(struct platform_device *pdev)
{
	struct mvebu_pic *pic = platform_get_drvdata(pdev);

	on_each_cpu(mvebu_pic_disable_percpu_irq, pic, 1);
	irq_domain_remove(pic->domain);
}

static const struct of_device_id mvebu_pic_of_match[] = {
	{ .compatible = "marvell,armada-8k-pic", },
	{},
};
MODULE_DEVICE_TABLE(of, mvebu_pic_of_match);

static struct platform_driver mvebu_pic_driver = {
	.probe		= mvebu_pic_probe,
	.remove		= mvebu_pic_remove,
	.driver = {
		.name		= "mvebu-pic",
		.of_match_table	= mvebu_pic_of_match,
	},
};
module_platform_driver(mvebu_pic_driver);

MODULE_AUTHOR("Yehuda Yitschak <yehuday@marvell.com>");
MODULE_AUTHOR("Thomas Petazzoni <thomas.petazzoni@free-electrons.com>");
MODULE_DESCRIPTION("Marvell Armada 7K/8K PIC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mvebu_pic");


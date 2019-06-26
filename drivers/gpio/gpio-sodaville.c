// SPDX-License-Identifier: GPL-2.0
/*
 *  GPIO interface for Intel Sodaville SoCs.
 *
 *  Copyright (c) 2010, 2011 Intel Corporation
 *
 *  Author: Hans J. Koch <hjk@linutronix.de>
 */

#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/of_irq.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#define DRV_NAME		"sdv_gpio"
#define SDV_NUM_PUB_GPIOS	12
#define PCI_DEVICE_ID_SDV_GPIO	0x2e67
#define GPIO_BAR		0

#define GPOUTR		0x00
#define GPOER		0x04
#define GPINR		0x08

#define GPSTR		0x0c
#define GPIT1R0		0x10
#define GPIO_INT	0x14
#define GPIT1R1		0x18

#define GPMUXCTL	0x1c

struct sdv_gpio_chip_data {
	int irq_base;
	void __iomem *gpio_pub_base;
	struct irq_domain *id;
	struct irq_chip_generic *gc;
	struct gpio_chip chip;
};

static int sdv_gpio_pub_set_type(struct irq_data *d, unsigned int type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct sdv_gpio_chip_data *sd = gc->private;
	void __iomem *type_reg;
	u32 reg;

	if (d->hwirq < 8)
		type_reg = sd->gpio_pub_base + GPIT1R0;
	else
		type_reg = sd->gpio_pub_base + GPIT1R1;

	reg = readl(type_reg);

	switch (type) {
	case IRQ_TYPE_LEVEL_HIGH:
		reg &= ~BIT(4 * (d->hwirq % 8));
		break;

	case IRQ_TYPE_LEVEL_LOW:
		reg |= BIT(4 * (d->hwirq % 8));
		break;

	default:
		return -EINVAL;
	}

	writel(reg, type_reg);
	return 0;
}

static irqreturn_t sdv_gpio_pub_irq_handler(int irq, void *data)
{
	struct sdv_gpio_chip_data *sd = data;
	unsigned long irq_stat = readl(sd->gpio_pub_base + GPSTR);
	int irq_bit;

	irq_stat &= readl(sd->gpio_pub_base + GPIO_INT);
	if (!irq_stat)
		return IRQ_NONE;

	for_each_set_bit(irq_bit, &irq_stat, 32)
		generic_handle_irq(irq_find_mapping(sd->id, irq_bit));

	return IRQ_HANDLED;
}

static int sdv_xlate(struct irq_domain *h, struct device_node *node,
		const u32 *intspec, u32 intsize, irq_hw_number_t *out_hwirq,
		u32 *out_type)
{
	u32 line, type;

	if (node != irq_domain_get_of_node(h))
		return -EINVAL;

	if (intsize < 2)
		return -EINVAL;

	line = *intspec;
	*out_hwirq = line;

	intspec++;
	type = *intspec;

	switch (type) {
	case IRQ_TYPE_LEVEL_LOW:
	case IRQ_TYPE_LEVEL_HIGH:
		*out_type = type;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct irq_domain_ops irq_domain_sdv_ops = {
	.xlate = sdv_xlate,
};

static int sdv_register_irqsupport(struct sdv_gpio_chip_data *sd,
		struct pci_dev *pdev)
{
	struct irq_chip_type *ct;
	int ret;

	sd->irq_base = devm_irq_alloc_descs(&pdev->dev, -1, 0,
					    SDV_NUM_PUB_GPIOS, -1);
	if (sd->irq_base < 0)
		return sd->irq_base;

	/* mask + ACK all interrupt sources */
	writel(0, sd->gpio_pub_base + GPIO_INT);
	writel((1 << 11) - 1, sd->gpio_pub_base + GPSTR);

	ret = devm_request_irq(&pdev->dev, pdev->irq,
			       sdv_gpio_pub_irq_handler, IRQF_SHARED,
			       "sdv_gpio", sd);
	if (ret)
		return ret;

	/*
	 * This gpio irq controller latches level irqs. Testing shows that if
	 * we unmask & ACK the IRQ before the source of the interrupt is gone
	 * then the interrupt is active again.
	 */
	sd->gc = devm_irq_alloc_generic_chip(&pdev->dev, "sdv-gpio", 1,
					     sd->irq_base,
					     sd->gpio_pub_base,
					     handle_fasteoi_irq);
	if (!sd->gc)
		return -ENOMEM;

	sd->gc->private = sd;
	ct = sd->gc->chip_types;
	ct->type = IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW;
	ct->regs.eoi = GPSTR;
	ct->regs.mask = GPIO_INT;
	ct->chip.irq_mask = irq_gc_mask_clr_bit;
	ct->chip.irq_unmask = irq_gc_mask_set_bit;
	ct->chip.irq_eoi = irq_gc_eoi;
	ct->chip.irq_set_type = sdv_gpio_pub_set_type;

	irq_setup_generic_chip(sd->gc, IRQ_MSK(SDV_NUM_PUB_GPIOS),
			IRQ_GC_INIT_MASK_CACHE, IRQ_NOREQUEST,
			IRQ_LEVEL | IRQ_NOPROBE);

	sd->id = irq_domain_add_legacy(pdev->dev.of_node, SDV_NUM_PUB_GPIOS,
				sd->irq_base, 0, &irq_domain_sdv_ops, sd);
	if (!sd->id)
		return -ENODEV;

	return 0;
}

static int sdv_gpio_probe(struct pci_dev *pdev,
					const struct pci_device_id *pci_id)
{
	struct sdv_gpio_chip_data *sd;
	int ret;
	u32 mux_val;

	sd = devm_kzalloc(&pdev->dev, sizeof(*sd), GFP_KERNEL);
	if (!sd)
		return -ENOMEM;

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "can't enable device.\n");
		return ret;
	}

	ret = pcim_iomap_regions(pdev, 1 << GPIO_BAR, DRV_NAME);
	if (ret) {
		dev_err(&pdev->dev, "can't alloc PCI BAR #%d\n", GPIO_BAR);
		return ret;
	}

	sd->gpio_pub_base = pcim_iomap_table(pdev)[GPIO_BAR];

	ret = of_property_read_u32(pdev->dev.of_node, "intel,muxctl", &mux_val);
	if (!ret)
		writel(mux_val, sd->gpio_pub_base + GPMUXCTL);

	ret = bgpio_init(&sd->chip, &pdev->dev, 4,
			sd->gpio_pub_base + GPINR, sd->gpio_pub_base + GPOUTR,
			NULL, sd->gpio_pub_base + GPOER, NULL, 0);
	if (ret)
		return ret;

	sd->chip.ngpio = SDV_NUM_PUB_GPIOS;

	ret = devm_gpiochip_add_data(&pdev->dev, &sd->chip, sd);
	if (ret < 0) {
		dev_err(&pdev->dev, "gpiochip_add() failed.\n");
		return ret;
	}

	ret = sdv_register_irqsupport(sd, pdev);
	if (ret)
		return ret;

	pci_set_drvdata(pdev, sd);
	dev_info(&pdev->dev, "Sodaville GPIO driver registered.\n");
	return 0;
}

static const struct pci_device_id sdv_gpio_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_SDV_GPIO) },
	{ 0, },
};

static struct pci_driver sdv_gpio_driver = {
	.driver = {
		.suppress_bind_attrs = true,
	},
	.name = DRV_NAME,
	.id_table = sdv_gpio_pci_ids,
	.probe = sdv_gpio_probe,
};
builtin_pci_driver(sdv_gpio_driver);

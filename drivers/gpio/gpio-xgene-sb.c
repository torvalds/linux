/*
 * AppliedMicro X-Gene SoC GPIO-Standby Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Author:	Tin Huynh <tnhuynh@apm.com>.
 *		Y Vo <yvo@apm.com>.
 *		Quan Nguyen <qnguyen@apm.com>.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio/driver.h>
#include <linux/acpi.h>

#include "gpiolib.h"

/* Common property names */
#define XGENE_NIRQ_PROPERTY		"apm,nr-irqs"
#define XGENE_NGPIO_PROPERTY		"apm,nr-gpios"
#define XGENE_IRQ_START_PROPERTY	"apm,irq-start"

#define XGENE_DFLT_MAX_NGPIO		22
#define XGENE_DFLT_MAX_NIRQ		6
#define XGENE_DFLT_IRQ_START_PIN	8
#define GPIO_MASK(x)			(1U << ((x) % 32))

#define MPA_GPIO_INT_LVL		0x0290
#define MPA_GPIO_OE_ADDR		0x029c
#define MPA_GPIO_OUT_ADDR		0x02a0
#define MPA_GPIO_IN_ADDR 		0x02a4
#define MPA_GPIO_SEL_LO 		0x0294

#define GPIO_INT_LEVEL_H	0x000001
#define GPIO_INT_LEVEL_L	0x000000

/**
 * struct xgene_gpio_sb - GPIO-Standby private data structure.
 * @gc:				memory-mapped GPIO controllers.
 * @regs:			GPIO register base offset
 * @irq_domain:			GPIO interrupt domain
 * @irq_start:			GPIO pin that start support interrupt
 * @nirq:			Number of GPIO pins that supports interrupt
 * @parent_irq_base:		Start parent HWIRQ
 */
struct xgene_gpio_sb {
	struct gpio_chip	gc;
	void __iomem		*regs;
	struct irq_domain	*irq_domain;
	u16			irq_start;
	u16			nirq;
	u16			parent_irq_base;
};

#define HWIRQ_TO_GPIO(priv, hwirq) ((hwirq) + (priv)->irq_start)
#define GPIO_TO_HWIRQ(priv, gpio) ((gpio) - (priv)->irq_start)

static void xgene_gpio_set_bit(struct gpio_chip *gc,
				void __iomem *reg, u32 gpio, int val)
{
	u32 data;

	data = gc->read_reg(reg);
	if (val)
		data |= GPIO_MASK(gpio);
	else
		data &= ~GPIO_MASK(gpio);
	gc->write_reg(reg, data);
}

static int xgene_gpio_sb_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct xgene_gpio_sb *priv = irq_data_get_irq_chip_data(d);
	int gpio = HWIRQ_TO_GPIO(priv, d->hwirq);
	int lvl_type = GPIO_INT_LEVEL_H;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_LEVEL_HIGH:
		lvl_type = GPIO_INT_LEVEL_H;
		break;
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_LEVEL_LOW:
		lvl_type = GPIO_INT_LEVEL_L;
		break;
	default:
		break;
	}

	xgene_gpio_set_bit(&priv->gc, priv->regs + MPA_GPIO_SEL_LO,
			gpio * 2, 1);
	xgene_gpio_set_bit(&priv->gc, priv->regs + MPA_GPIO_INT_LVL,
			d->hwirq, lvl_type);

	/* Propagate IRQ type setting to parent */
	if (type & IRQ_TYPE_EDGE_BOTH)
		return irq_chip_set_type_parent(d, IRQ_TYPE_EDGE_RISING);
	else
		return irq_chip_set_type_parent(d, IRQ_TYPE_LEVEL_HIGH);
}

static struct irq_chip xgene_gpio_sb_irq_chip = {
	.name           = "sbgpio",
	.irq_eoi	= irq_chip_eoi_parent,
	.irq_mask       = irq_chip_mask_parent,
	.irq_unmask     = irq_chip_unmask_parent,
	.irq_set_type   = xgene_gpio_sb_irq_set_type,
};

static int xgene_gpio_sb_to_irq(struct gpio_chip *gc, u32 gpio)
{
	struct xgene_gpio_sb *priv = gpiochip_get_data(gc);
	struct irq_fwspec fwspec;

	if ((gpio < priv->irq_start) ||
			(gpio > HWIRQ_TO_GPIO(priv, priv->nirq)))
		return -ENXIO;

	fwspec.fwnode = gc->parent->fwnode;
	fwspec.param_count = 2;
	fwspec.param[0] = GPIO_TO_HWIRQ(priv, gpio);
	fwspec.param[1] = IRQ_TYPE_NONE;
	return irq_create_fwspec_mapping(&fwspec);
}

static int xgene_gpio_sb_domain_activate(struct irq_domain *d,
					 struct irq_data *irq_data,
					 bool early)
{
	struct xgene_gpio_sb *priv = d->host_data;
	u32 gpio = HWIRQ_TO_GPIO(priv, irq_data->hwirq);

	if (gpiochip_lock_as_irq(&priv->gc, gpio)) {
		dev_err(priv->gc.parent,
		"Unable to configure XGene GPIO standby pin %d as IRQ\n",
				gpio);
		return -ENOSPC;
	}

	xgene_gpio_set_bit(&priv->gc, priv->regs + MPA_GPIO_SEL_LO,
			gpio * 2, 1);
	return 0;
}

static void xgene_gpio_sb_domain_deactivate(struct irq_domain *d,
		struct irq_data *irq_data)
{
	struct xgene_gpio_sb *priv = d->host_data;
	u32 gpio = HWIRQ_TO_GPIO(priv, irq_data->hwirq);

	gpiochip_unlock_as_irq(&priv->gc, gpio);
	xgene_gpio_set_bit(&priv->gc, priv->regs + MPA_GPIO_SEL_LO,
			gpio * 2, 0);
}

static int xgene_gpio_sb_domain_translate(struct irq_domain *d,
		struct irq_fwspec *fwspec,
		unsigned long *hwirq,
		unsigned int *type)
{
	struct xgene_gpio_sb *priv = d->host_data;

	if ((fwspec->param_count != 2) ||
		(fwspec->param[0] >= priv->nirq))
		return -EINVAL;
	*hwirq = fwspec->param[0];
	*type = fwspec->param[1];
	return 0;
}

static int xgene_gpio_sb_domain_alloc(struct irq_domain *domain,
					unsigned int virq,
					unsigned int nr_irqs, void *data)
{
	struct irq_fwspec *fwspec = data;
	struct irq_fwspec parent_fwspec;
	struct xgene_gpio_sb *priv = domain->host_data;
	irq_hw_number_t hwirq;
	unsigned int i;

	hwirq = fwspec->param[0];
	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
				&xgene_gpio_sb_irq_chip, priv);

	parent_fwspec.fwnode = domain->parent->fwnode;
	if (is_of_node(parent_fwspec.fwnode)) {
		parent_fwspec.param_count = 3;
		parent_fwspec.param[0] = 0;/* SPI */
		/* Skip SGIs and PPIs*/
		parent_fwspec.param[1] = hwirq + priv->parent_irq_base - 32;
		parent_fwspec.param[2] = fwspec->param[1];
	} else if (is_fwnode_irqchip(parent_fwspec.fwnode)) {
		parent_fwspec.param_count = 2;
		parent_fwspec.param[0] = hwirq + priv->parent_irq_base;
		parent_fwspec.param[1] = fwspec->param[1];
	} else
		return -EINVAL;

	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs,
			&parent_fwspec);
}

static const struct irq_domain_ops xgene_gpio_sb_domain_ops = {
	.translate      = xgene_gpio_sb_domain_translate,
	.alloc          = xgene_gpio_sb_domain_alloc,
	.free           = irq_domain_free_irqs_common,
	.activate	= xgene_gpio_sb_domain_activate,
	.deactivate	= xgene_gpio_sb_domain_deactivate,
};

static int xgene_gpio_sb_probe(struct platform_device *pdev)
{
	struct xgene_gpio_sb *priv;
	int ret;
	struct resource *res;
	void __iomem *regs;
	struct irq_domain *parent_domain = NULL;
	u32 val32;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	priv->regs = regs;

	ret = platform_get_irq(pdev, 0);
	if (ret > 0) {
		priv->parent_irq_base = irq_get_irq_data(ret)->hwirq;
		parent_domain = irq_get_irq_data(ret)->domain;
	}
	if (!parent_domain) {
		dev_err(&pdev->dev, "unable to obtain parent domain\n");
		return -ENODEV;
	}

	ret = bgpio_init(&priv->gc, &pdev->dev, 4,
			regs + MPA_GPIO_IN_ADDR,
			regs + MPA_GPIO_OUT_ADDR, NULL,
			regs + MPA_GPIO_OE_ADDR, NULL, 0);
        if (ret)
                return ret;

	priv->gc.to_irq = xgene_gpio_sb_to_irq;

	/* Retrieve start irq pin, use default if property not found */
	priv->irq_start = XGENE_DFLT_IRQ_START_PIN;
	if (!device_property_read_u32(&pdev->dev,
					XGENE_IRQ_START_PROPERTY, &val32))
		priv->irq_start = val32;

	/* Retrieve number irqs, use default if property not found */
	priv->nirq = XGENE_DFLT_MAX_NIRQ;
	if (!device_property_read_u32(&pdev->dev, XGENE_NIRQ_PROPERTY, &val32))
		priv->nirq = val32;

	/* Retrieve number gpio, use default if property not found */
	priv->gc.ngpio = XGENE_DFLT_MAX_NGPIO;
	if (!device_property_read_u32(&pdev->dev, XGENE_NGPIO_PROPERTY, &val32))
		priv->gc.ngpio = val32;

	dev_info(&pdev->dev, "Support %d gpios, %d irqs start from pin %d\n",
			priv->gc.ngpio, priv->nirq, priv->irq_start);

	platform_set_drvdata(pdev, priv);

	priv->irq_domain = irq_domain_create_hierarchy(parent_domain,
					0, priv->nirq, pdev->dev.fwnode,
					&xgene_gpio_sb_domain_ops, priv);
	if (!priv->irq_domain)
		return -ENODEV;

	priv->gc.irq.domain = priv->irq_domain;

	ret = devm_gpiochip_add_data(&pdev->dev, &priv->gc, priv);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to register X-Gene GPIO Standby driver\n");
		irq_domain_remove(priv->irq_domain);
		return ret;
	}

	dev_info(&pdev->dev, "X-Gene GPIO Standby driver registered\n");

	if (priv->nirq > 0) {
		/* Register interrupt handlers for gpio signaled acpi events */
		acpi_gpiochip_request_interrupts(&priv->gc);
	}

	return ret;
}

static int xgene_gpio_sb_remove(struct platform_device *pdev)
{
	struct xgene_gpio_sb *priv = platform_get_drvdata(pdev);

	if (priv->nirq > 0) {
		acpi_gpiochip_free_interrupts(&priv->gc);
	}

	irq_domain_remove(priv->irq_domain);

	return 0;
}

static const struct of_device_id xgene_gpio_sb_of_match[] = {
	{.compatible = "apm,xgene-gpio-sb", },
	{},
};
MODULE_DEVICE_TABLE(of, xgene_gpio_sb_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id xgene_gpio_sb_acpi_match[] = {
	{"APMC0D15", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, xgene_gpio_sb_acpi_match);
#endif

static struct platform_driver xgene_gpio_sb_driver = {
	.driver = {
		   .name = "xgene-gpio-sb",
		   .of_match_table = xgene_gpio_sb_of_match,
		   .acpi_match_table = ACPI_PTR(xgene_gpio_sb_acpi_match),
		   },
	.probe = xgene_gpio_sb_probe,
	.remove = xgene_gpio_sb_remove,
};
module_platform_driver(xgene_gpio_sb_driver);

MODULE_AUTHOR("AppliedMicro");
MODULE_DESCRIPTION("APM X-Gene GPIO Standby driver");
MODULE_LICENSE("GPL");

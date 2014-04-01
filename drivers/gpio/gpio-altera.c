/*
 * Copyright (C) 2013 Altera Corporation
 * Based on gpio-mpc8xxx.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/irqchip/chained_irq.h>

#define ALTERA_GPIO_DATA		0x0
#define ALTERA_GPIO_DIR			0x4
#define ALTERA_GPIO_IRQ_MASK		0x8
#define ALTERA_GPIO_EDGE_CAP		0xc
#define ALTERA_GPIO_OUTSET		0x10
#define ALTERA_GPIO_OUTCLEAR		0x14

/**
* struct altera_gpio_chip
* @mmchip		: memory mapped chip structure.
* @irq			: irq domain that this driver is registered to.
* @gpio_lock		: synchronization lock so that new irq/set/get requests
			  will be blocked until the current one completes.
* @interrupt_trigger	: specifies the hardware configured IRQ trigger type
			  (rising, falling, both, high)
* @mapped_irq		: kernel mapped irq number.
*/
struct altera_gpio_chip {
	struct of_mm_gpio_chip mmchip;
	struct irq_domain *domain;
	spinlock_t gpio_lock;
	int interrupt_trigger;
	int edge_type;
	int mapped_irq;
};

static void altera_gpio_irq_unmask(struct irq_data *d)
{
	struct altera_gpio_chip *altera_gc = irq_data_get_irq_chip_data(d);
	struct of_mm_gpio_chip *mm_gc = &altera_gc->mmchip;
	unsigned long flags;
	unsigned int intmask;

	spin_lock_irqsave(&altera_gc->gpio_lock, flags);
	intmask = readl(mm_gc->regs + ALTERA_GPIO_IRQ_MASK);
	/* Set ALTERA_GPIO_IRQ_MASK bit to unmask */
	intmask |= BIT(irqd_to_hwirq(d));
	writel(intmask, mm_gc->regs + ALTERA_GPIO_IRQ_MASK);
	spin_unlock_irqrestore(&altera_gc->gpio_lock, flags);
}

static void altera_gpio_irq_mask(struct irq_data *d)
{
	struct altera_gpio_chip *altera_gc = irq_data_get_irq_chip_data(d);
	struct of_mm_gpio_chip *mm_gc = &altera_gc->mmchip;
	unsigned long flags;
	unsigned int intmask;

	spin_lock_irqsave(&altera_gc->gpio_lock, flags);
	intmask = readl(mm_gc->regs + ALTERA_GPIO_IRQ_MASK);
	/* Clear ALTERA_GPIO_IRQ_MASK bit to mask */
	intmask &= ~BIT(irqd_to_hwirq(d));
	writel(intmask, mm_gc->regs + ALTERA_GPIO_IRQ_MASK);
	spin_unlock_irqrestore(&altera_gc->gpio_lock, flags);
}

static int altera_gpio_irq_set_type(struct irq_data *d,
				unsigned int type)
{
	struct altera_gpio_chip *altera_gc = irq_data_get_irq_chip_data(d);

	if (type == IRQ_TYPE_NONE)
		return 0;

	if (type == IRQ_TYPE_LEVEL_HIGH &&
	altera_gc->interrupt_trigger == IRQ_TYPE_LEVEL_HIGH) {
		return 0;
	} else {
		if (type == IRQ_TYPE_EDGE_RISING &&
		altera_gc->interrupt_trigger == IRQ_TYPE_EDGE_RISING)
			return 0;
		else if (type == IRQ_TYPE_EDGE_FALLING &&
		altera_gc->interrupt_trigger == IRQ_TYPE_EDGE_FALLING)
			return 0;
		else if (type == IRQ_TYPE_EDGE_BOTH &&
		altera_gc->interrupt_trigger == IRQ_TYPE_EDGE_BOTH)
			return 0;
	}

	return -EINVAL;
}

static unsigned int altera_gpio_irq_startup(struct irq_data *d)
{
	altera_gpio_irq_unmask(d);

	return 0;
}

static void altera_gpio_irq_shutdown(struct irq_data *d)
{
	altera_gpio_irq_unmask(d);
}

static struct irq_chip altera_irq_chip = {
	.name		= "altera-gpio",
	.irq_mask	= altera_gpio_irq_mask,
	.irq_unmask	= altera_gpio_irq_unmask,
	.irq_set_type	= altera_gpio_irq_set_type,
	.irq_startup	= altera_gpio_irq_startup,
	.irq_shutdown	= altera_gpio_irq_shutdown,
};

static int altera_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);

	return !!(readl(mm_gc->regs + ALTERA_GPIO_DATA) >> offset);
}

static void altera_gpio_set(struct gpio_chip *gc, unsigned offset, int value)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct altera_gpio_chip *chip = container_of(mm_gc,
				struct altera_gpio_chip, mmchip);
	unsigned long flags;
	unsigned int data_reg;

	spin_lock_irqsave(&chip->gpio_lock, flags);
	data_reg = readl(mm_gc->regs + ALTERA_GPIO_DATA);
	if (value)
		data_reg |= BIT(offset);
	else
		data_reg &= ~BIT(offset);
	writel(data_reg, mm_gc->regs + ALTERA_GPIO_DATA);
	spin_unlock_irqrestore(&chip->gpio_lock, flags);
}

static int altera_gpio_direction_input(struct gpio_chip *gc, unsigned offset)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct altera_gpio_chip *chip = container_of(mm_gc,
				struct altera_gpio_chip, mmchip);
	unsigned long flags;
	unsigned int gpio_ddr;

	spin_lock_irqsave(&chip->gpio_lock, flags);
	/* Set pin as input, assumes software controlled IP */
	gpio_ddr = readl(mm_gc->regs + ALTERA_GPIO_DIR);
	gpio_ddr &= ~BIT(offset);
	writel(gpio_ddr, mm_gc->regs + ALTERA_GPIO_DIR);
	spin_unlock_irqrestore(&chip->gpio_lock, flags);

	return 0;
}

static int altera_gpio_direction_output(struct gpio_chip *gc,
		unsigned offset, int value)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct altera_gpio_chip *chip = container_of(mm_gc,
				struct altera_gpio_chip, mmchip);
	unsigned long flags;
	unsigned int data_reg, gpio_ddr;

	spin_lock_irqsave(&chip->gpio_lock, flags);
	/* Sets the GPIO value */
	data_reg = readl(mm_gc->regs + ALTERA_GPIO_DATA);
	if (value)
		data_reg |= BIT(offset);
	else
		data_reg &= ~BIT(offset);
	writel(data_reg, mm_gc->regs + ALTERA_GPIO_DATA);

	/* Set pin as output, assumes software controlled IP */
	gpio_ddr = readl(mm_gc->regs + ALTERA_GPIO_DIR);
	gpio_ddr |= BIT(offset);
	writel(gpio_ddr, mm_gc->regs + ALTERA_GPIO_DIR);
	spin_unlock_irqrestore(&chip->gpio_lock, flags);

	return 0;
}

static int altera_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct altera_gpio_chip *altera_gc = container_of(mm_gc,
				struct altera_gpio_chip, mmchip);

	if (!altera_gc->domain)
		return -ENXIO;
	if (offset < altera_gc->mmchip.gc.ngpio)
		return irq_find_mapping(altera_gc->domain, offset);
	else
		return -ENXIO;
}

static void altera_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct altera_gpio_chip *altera_gc = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct of_mm_gpio_chip *mm_gc = &altera_gc->mmchip;
	unsigned long status;

	int i;

	chained_irq_enter(chip, desc);
	/* Handling for level trigger and edge trigger is different */
	if (altera_gc->interrupt_trigger == IRQ_TYPE_LEVEL_HIGH) {
		status = readl(mm_gc->regs + ALTERA_GPIO_DATA);
		status &= readl(mm_gc->regs + ALTERA_GPIO_IRQ_MASK);

		for (i = 0; i < mm_gc->gc.ngpio; i++) {
			if (status & BIT(i)) {
				generic_handle_irq(irq_find_mapping(
					altera_gc->domain, i));
			}
		}
	} else {
		while ((status =
			(readl(mm_gc->regs + ALTERA_GPIO_EDGE_CAP) &
			readl(mm_gc->regs + ALTERA_GPIO_IRQ_MASK)))) {
			writel(status, mm_gc->regs + ALTERA_GPIO_EDGE_CAP);
			for (i = 0; i < mm_gc->gc.ngpio; i++) {
				if (status & BIT(i)) {
					generic_handle_irq(irq_find_mapping(
						altera_gc->domain, i));
				}
			}
		}
	}

	chained_irq_exit(chip, desc);
}

static int altera_gpio_irq_map(struct irq_domain *h, unsigned int irq,
				irq_hw_number_t hw_irq_num)
{
	irq_set_chip_data(irq, h->host_data);
	irq_set_chip_and_handler(irq, &altera_irq_chip, handle_level_irq);
	irq_set_irq_type(irq, IRQ_TYPE_NONE);

	return 0;
}

static struct irq_domain_ops altera_gpio_irq_ops = {
	.map	= altera_gpio_irq_map,
	.xlate = irq_domain_xlate_onecell,
};

int altera_gpio_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	int i, id, reg, ret;
	struct altera_gpio_chip *altera_gc = devm_kzalloc(&pdev->dev,
				sizeof(*altera_gc), GFP_KERNEL);
	if (altera_gc == NULL) {
		pr_err("%s: out of memory\n", node->full_name);
		return -ENOMEM;
	}
	altera_gc->domain = 0;

	spin_lock_init(&altera_gc->gpio_lock);

	id = pdev->id;

	if (of_property_read_u32(node, "altr,gpio-bank-width", &reg))
		/*By default assume full GPIO controller*/
		altera_gc->mmchip.gc.ngpio = 32;
	else
		altera_gc->mmchip.gc.ngpio = reg;

	if (altera_gc->mmchip.gc.ngpio > 32) {
		dev_warn(&pdev->dev,
			"ngpio is greater than 32, defaulting to 32\n");
		altera_gc->mmchip.gc.ngpio = 32;
	}

	altera_gc->mmchip.gc.direction_input	= altera_gpio_direction_input;
	altera_gc->mmchip.gc.direction_output	= altera_gpio_direction_output;
	altera_gc->mmchip.gc.get		= altera_gpio_get;
	altera_gc->mmchip.gc.set		= altera_gpio_set;
	altera_gc->mmchip.gc.to_irq		= altera_gpio_to_irq;
	altera_gc->mmchip.gc.owner		= THIS_MODULE;

	ret = of_mm_gpiochip_add(node, &altera_gc->mmchip);
	if (ret) {
		dev_err(&pdev->dev, "Failed adding memory mapped gpiochip\n");
		return ret;
	}

	platform_set_drvdata(pdev, altera_gc);

	altera_gc->mapped_irq = irq_of_parse_and_map(node, 0);

	if (!altera_gc->mapped_irq)
		goto skip_irq;

	altera_gc->domain = irq_domain_add_linear(node,
		altera_gc->mmchip.gc.ngpio, &altera_gpio_irq_ops, altera_gc);

	if (!altera_gc->domain) {
		ret = -ENODEV;
		goto dispose_irq;
	}

	for (i = 0; i < altera_gc->mmchip.gc.ngpio; i++)
		irq_create_mapping(altera_gc->domain, i);

	if (of_property_read_u32(node, "altr,interrupt_type", &reg)) {
		ret = -EINVAL;
		dev_err(&pdev->dev,
			"altr,interrupt_type value not set in device tree\n");
		goto teardown;
	}
	altera_gc->interrupt_trigger = reg;

	irq_set_handler_data(altera_gc->mapped_irq, altera_gc);
	irq_set_chained_handler(altera_gc->mapped_irq, altera_gpio_irq_handler);

	return 0;

teardown:
	irq_domain_remove(altera_gc->domain);
dispose_irq:
	irq_dispose_mapping(altera_gc->mapped_irq);
	WARN_ON(gpiochip_remove(&altera_gc->mmchip.gc) < 0);

	pr_err("%s: registration failed with status %d\n",
		node->full_name, ret);

	return ret;
skip_irq:
	return 0;
}

static int altera_gpio_remove(struct platform_device *pdev)
{
	unsigned int irq, i;
	int status;
	struct altera_gpio_chip *altera_gc = platform_get_drvdata(pdev);

	status = gpiochip_remove(&altera_gc->mmchip.gc);

	if (status < 0)
		return status;

	if (altera_gc->domain) {
		irq_dispose_mapping(altera_gc->mapped_irq);

		for (i = 0; i < altera_gc->mmchip.gc.ngpio; i++) {
			irq = irq_find_mapping(altera_gc->domain, i);
			if (irq > 0)
				irq_dispose_mapping(irq);
		}

		irq_domain_remove(altera_gc->domain);
	}

	irq_set_handler_data(altera_gc->mapped_irq, NULL);
	irq_set_chained_handler(altera_gc->mapped_irq, NULL);
	return -EIO;
}

static struct of_device_id altera_gpio_of_match[] = {
	{ .compatible = "altr,pio-1.0", },
	{},
};
MODULE_DEVICE_TABLE(of, altera_gpio_of_match);

static struct platform_driver altera_gpio_driver = {
	.driver = {
		.name	= "altera_gpio",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(altera_gpio_of_match),
	},
	.probe		= altera_gpio_probe,
	.remove		= altera_gpio_remove,
};

static int __init altera_gpio_init(void)
{
	return platform_driver_register(&altera_gpio_driver);
}
subsys_initcall(altera_gpio_init);

static void __exit altera_gpio_exit(void)
{
	platform_driver_unregister(&altera_gpio_driver);
}
module_exit(altera_gpio_exit);

MODULE_AUTHOR("Tien Hock Loh <thloh@altera.com>");
MODULE_DESCRIPTION("Altera GPIO driver");
MODULE_LICENSE("GPL");

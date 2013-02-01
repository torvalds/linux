/*
 * Copyright (C) 2013 Altera Corporation
 * Based on gpio-mpc8xxx.c
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>

#define ALTERA_GPIO_DATA		0x0
#define ALTERA_GPIO_DIR			0x4
#define ALTERA_GPIO_IRQ_MASK		0x8
#define ALTERA_GPIO_EDGE_CAP		0xc
#define ALTERA_GPIO_OUTSET		0x10
#define ALTERA_GPIO_OUTCLEAR		0x14

struct altera_gpio_chip {
	struct of_mm_gpio_chip mmchip;
	struct irq_domain *irq;	/* GPIO controller IRQ number */
	spinlock_t gpio_lock;	/* Lock used for synchronization */
	int level_trigger;
	int hwirq;
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
	intmask |= (1 << irqd_to_hwirq(d));
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
	intmask &= ~(1 << irqd_to_hwirq(d));
	writel(intmask, mm_gc->regs + ALTERA_GPIO_IRQ_MASK);
	spin_unlock_irqrestore(&altera_gc->gpio_lock, flags);
}

static int altera_gpio_irq_set_type(struct irq_data *d,
				unsigned int type)
{
	if (type == IRQ_TYPE_NONE)
		return 0;
	return -EINVAL;
}

static struct irq_chip altera_irq_chip = {
	.name		= "altera-gpio",
	.irq_mask	= altera_gpio_irq_mask,
	.irq_unmask	= altera_gpio_irq_unmask,
	.irq_set_type	= altera_gpio_irq_set_type,
};

static int altera_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);

	return (readl(mm_gc->regs + ALTERA_GPIO_DATA) >> offset) & 1;
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
	data_reg = (data_reg & ~(1 << offset)) | (value << offset);
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
	gpio_ddr &= ~(1 << offset);
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
	data_reg = (data_reg & ~(1 << offset)) | (value << offset);
	 writel(data_reg, mm_gc->regs + ALTERA_GPIO_DATA);

	/* Set pin as output, assumes software controlled IP */
	gpio_ddr = readl(mm_gc->regs + ALTERA_GPIO_DIR);
	gpio_ddr |= (1 << offset);
	writel(gpio_ddr, mm_gc->regs + ALTERA_GPIO_DIR);
	spin_unlock_irqrestore(&chip->gpio_lock, flags);

	return 0;
}

static int altera_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct altera_gpio_chip *altera_gc = container_of(mm_gc,
				struct altera_gpio_chip, mmchip);

	if (altera_gc->irq == 0)
		return -ENXIO;
	if ((altera_gc->irq && offset) < altera_gc->mmchip.gc.ngpio)
		return irq_create_mapping(altera_gc->irq, offset);
	else
		return -ENXIO;
}

static void altera_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct altera_gpio_chip *altera_gc = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct of_mm_gpio_chip *mm_gc = &altera_gc->mmchip;
	unsigned long status;

	int base;
	chip->irq_mask(&desc->irq_data);

	if (altera_gc->level_trigger)
		status = readl(mm_gc->regs + ALTERA_GPIO_DATA);
	else {
		status = readl(mm_gc->regs + ALTERA_GPIO_EDGE_CAP);
		writel(status, mm_gc->regs + ALTERA_GPIO_EDGE_CAP);
	}

	status &= readl(mm_gc->regs + ALTERA_GPIO_IRQ_MASK);

	for (base = 0; base < mm_gc->gc.ngpio; base++) {
		if ((1 << base) & status) {
			generic_handle_irq(
				irq_linear_revmap(altera_gc->irq, base));
		}
	}
	chip->irq_eoi(irq_desc_get_irq_data(desc));
	chip->irq_unmask(&desc->irq_data);
}

static int altera_gpio_irq_map(struct irq_domain *h, unsigned int virq,
				irq_hw_number_t hw_irq_num)
{
	irq_set_chip_data(virq, h->host_data);
	irq_set_chip_and_handler(virq, &altera_irq_chip, handle_level_irq);
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static struct irq_domain_ops altera_gpio_irq_ops = {
	.map	= altera_gpio_irq_map,
	.xlate = irq_domain_xlate_twocell,
};

int __devinit altera_gpio_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	int id, reg, ret;
	struct altera_gpio_chip *altera_gc = devm_kzalloc(&pdev->dev,
				sizeof(*altera_gc), GFP_KERNEL);
	if (altera_gc == NULL) {
		ret = -ENOMEM;
		pr_err("%s: registration failed with status %d\n",
			node->full_name, ret);
		return ret;
	}
	altera_gc->irq = 0;

	spin_lock_init(&altera_gc->gpio_lock);

	id = pdev->id;

	if (of_property_read_u32(node, "width", &reg))
		/*By default assume full GPIO controller*/
		altera_gc->mmchip.gc.ngpio = 32;
	else
		altera_gc->mmchip.gc.ngpio = reg;

	if (altera_gc->mmchip.gc.ngpio > 32) {
		pr_warn("%s: ngpio is greater than 32, defaulting to 32\n",
			node->full_name);
		altera_gc->mmchip.gc.ngpio = 32;
	}

	altera_gc->mmchip.gc.direction_input	= altera_gpio_direction_input;
	altera_gc->mmchip.gc.direction_output	= altera_gpio_direction_output;
	altera_gc->mmchip.gc.get		= altera_gpio_get;
	altera_gc->mmchip.gc.set		= altera_gpio_set;
	altera_gc->mmchip.gc.to_irq		= altera_gpio_to_irq;
	altera_gc->mmchip.gc.owner		= THIS_MODULE;

	ret = of_mm_gpiochip_add(node, &altera_gc->mmchip);
	if (ret)
		goto err;

	platform_set_drvdata(pdev, altera_gc);

	if (of_get_property(node, "interrupts", &reg) == NULL)
		goto skip_irq;
	altera_gc->hwirq = irq_of_parse_and_map(node, 0);

	if (altera_gc->hwirq == NO_IRQ)
		goto skip_irq;

	altera_gc->irq = irq_domain_add_linear(node, altera_gc->mmchip.gc.ngpio,
				&altera_gpio_irq_ops, altera_gc);

	if (!altera_gc->irq) {
		ret = -ENODEV;
		goto dispose_irq;
	}

	if (of_property_read_u32(node, "level_trigger", &reg)) {
		ret = -EINVAL;
		pr_err("%s: level_trigger value not set in device tree\n",
			node->full_name);
		goto teardown;
	}
	altera_gc->level_trigger = reg;

	irq_set_handler_data(altera_gc->hwirq, altera_gc);
	irq_set_chained_handler(altera_gc->hwirq, altera_gpio_irq_handler);

	return 0;

teardown:
	irq_domain_remove(altera_gc->irq);
dispose_irq:
	irq_dispose_mapping(altera_gc->hwirq);
	WARN_ON(gpiochip_remove(&altera_gc->mmchip.gc) < 0);

err:
	pr_err("%s: registration failed with status %d\n",
		node->full_name, ret);
	devm_kfree(&pdev->dev, altera_gc);

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

	if (altera_gc->irq) {
		irq_dispose_mapping(altera_gc->hwirq);
	
		for (i = 0; i < altera_gc->mmchip.gc.ngpio; i++) {
			irq = irq_find_mapping(altera_gc->irq, i);
			if (irq > 0)
				irq_dispose_mapping(irq);
		}

		irq_domain_remove(altera_gc->irq);
	}

	irq_set_handler_data(altera_gc->hwirq, NULL);
	irq_set_chained_handler(altera_gc->hwirq, NULL);
	devm_kfree(&pdev->dev, altera_gc);
	return -EIO;
}

#ifdef CONFIG_OF
static struct of_device_id altera_gpio_of_match[] __devinitdata = {
	{ .compatible = "altr,pio-1.0", },
	{},
};
MODULE_DEVICE_TABLE(of, altera_gpio_of_match);
#else
#define altera_gpio_of_match NULL
#endif

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

MODULE_DESCRIPTION("Altera GPIO driver");
MODULE_LICENSE("GPL");

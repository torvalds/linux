// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013 Altera Corporation
 * Based on gpio-mpc8xxx.c
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/legacy-of-mm-gpiochip.h>
#include <linux/platform_device.h>

#define ALTERA_GPIO_MAX_NGPIO		32
#define ALTERA_GPIO_DATA		0x0
#define ALTERA_GPIO_DIR			0x4
#define ALTERA_GPIO_IRQ_MASK		0x8
#define ALTERA_GPIO_EDGE_CAP		0xc

/**
* struct altera_gpio_chip
* @mmchip		: memory mapped chip structure.
* @gpio_lock		: synchronization lock so that new irq/set/get requests
*			  will be blocked until the current one completes.
* @interrupt_trigger	: specifies the hardware configured IRQ trigger type
*			  (rising, falling, both, high)
* @mapped_irq		: kernel mapped irq number.
*/
struct altera_gpio_chip {
	struct of_mm_gpio_chip mmchip;
	raw_spinlock_t gpio_lock;
	int interrupt_trigger;
	int mapped_irq;
};

static void altera_gpio_irq_unmask(struct irq_data *d)
{
	struct altera_gpio_chip *altera_gc;
	struct of_mm_gpio_chip *mm_gc;
	unsigned long flags;
	u32 intmask;

	altera_gc = gpiochip_get_data(irq_data_get_irq_chip_data(d));
	mm_gc = &altera_gc->mmchip;
	gpiochip_enable_irq(&mm_gc->gc, irqd_to_hwirq(d));

	raw_spin_lock_irqsave(&altera_gc->gpio_lock, flags);
	intmask = readl(mm_gc->regs + ALTERA_GPIO_IRQ_MASK);
	/* Set ALTERA_GPIO_IRQ_MASK bit to unmask */
	intmask |= BIT(irqd_to_hwirq(d));
	writel(intmask, mm_gc->regs + ALTERA_GPIO_IRQ_MASK);
	raw_spin_unlock_irqrestore(&altera_gc->gpio_lock, flags);
}

static void altera_gpio_irq_mask(struct irq_data *d)
{
	struct altera_gpio_chip *altera_gc;
	struct of_mm_gpio_chip *mm_gc;
	unsigned long flags;
	u32 intmask;

	altera_gc = gpiochip_get_data(irq_data_get_irq_chip_data(d));
	mm_gc = &altera_gc->mmchip;

	raw_spin_lock_irqsave(&altera_gc->gpio_lock, flags);
	intmask = readl(mm_gc->regs + ALTERA_GPIO_IRQ_MASK);
	/* Clear ALTERA_GPIO_IRQ_MASK bit to mask */
	intmask &= ~BIT(irqd_to_hwirq(d));
	writel(intmask, mm_gc->regs + ALTERA_GPIO_IRQ_MASK);
	raw_spin_unlock_irqrestore(&altera_gc->gpio_lock, flags);
	gpiochip_disable_irq(&mm_gc->gc, irqd_to_hwirq(d));
}

/*
 * This controller's IRQ type is synthesized in hardware, so this function
 * just checks if the requested set_type matches the synthesized IRQ type
 */
static int altera_gpio_irq_set_type(struct irq_data *d,
				   unsigned int type)
{
	struct altera_gpio_chip *altera_gc;

	altera_gc = gpiochip_get_data(irq_data_get_irq_chip_data(d));

	if (type == IRQ_TYPE_NONE) {
		irq_set_handler_locked(d, handle_bad_irq);
		return 0;
	}
	if (type == altera_gc->interrupt_trigger) {
		if (type == IRQ_TYPE_LEVEL_HIGH)
			irq_set_handler_locked(d, handle_level_irq);
		else
			irq_set_handler_locked(d, handle_simple_irq);
		return 0;
	}
	irq_set_handler_locked(d, handle_bad_irq);
	return -EINVAL;
}

static unsigned int altera_gpio_irq_startup(struct irq_data *d)
{
	altera_gpio_irq_unmask(d);

	return 0;
}

static int altera_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct of_mm_gpio_chip *mm_gc;

	mm_gc = to_of_mm_gpio_chip(gc);

	return !!(readl(mm_gc->regs + ALTERA_GPIO_DATA) & BIT(offset));
}

static void altera_gpio_set(struct gpio_chip *gc, unsigned offset, int value)
{
	struct of_mm_gpio_chip *mm_gc;
	struct altera_gpio_chip *chip;
	unsigned long flags;
	unsigned int data_reg;

	mm_gc = to_of_mm_gpio_chip(gc);
	chip = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&chip->gpio_lock, flags);
	data_reg = readl(mm_gc->regs + ALTERA_GPIO_DATA);
	if (value)
		data_reg |= BIT(offset);
	else
		data_reg &= ~BIT(offset);
	writel(data_reg, mm_gc->regs + ALTERA_GPIO_DATA);
	raw_spin_unlock_irqrestore(&chip->gpio_lock, flags);
}

static int altera_gpio_direction_input(struct gpio_chip *gc, unsigned offset)
{
	struct of_mm_gpio_chip *mm_gc;
	struct altera_gpio_chip *chip;
	unsigned long flags;
	unsigned int gpio_ddr;

	mm_gc = to_of_mm_gpio_chip(gc);
	chip = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&chip->gpio_lock, flags);
	/* Set pin as input, assumes software controlled IP */
	gpio_ddr = readl(mm_gc->regs + ALTERA_GPIO_DIR);
	gpio_ddr &= ~BIT(offset);
	writel(gpio_ddr, mm_gc->regs + ALTERA_GPIO_DIR);
	raw_spin_unlock_irqrestore(&chip->gpio_lock, flags);

	return 0;
}

static int altera_gpio_direction_output(struct gpio_chip *gc,
		unsigned offset, int value)
{
	struct of_mm_gpio_chip *mm_gc;
	struct altera_gpio_chip *chip;
	unsigned long flags;
	unsigned int data_reg, gpio_ddr;

	mm_gc = to_of_mm_gpio_chip(gc);
	chip = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&chip->gpio_lock, flags);
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
	raw_spin_unlock_irqrestore(&chip->gpio_lock, flags);

	return 0;
}

static void altera_gpio_irq_edge_handler(struct irq_desc *desc)
{
	struct altera_gpio_chip *altera_gc;
	struct irq_chip *chip;
	struct of_mm_gpio_chip *mm_gc;
	struct irq_domain *irqdomain;
	unsigned long status;
	int i;

	altera_gc = gpiochip_get_data(irq_desc_get_handler_data(desc));
	chip = irq_desc_get_chip(desc);
	mm_gc = &altera_gc->mmchip;
	irqdomain = altera_gc->mmchip.gc.irq.domain;

	chained_irq_enter(chip, desc);

	while ((status =
	      (readl(mm_gc->regs + ALTERA_GPIO_EDGE_CAP) &
	      readl(mm_gc->regs + ALTERA_GPIO_IRQ_MASK)))) {
		writel(status, mm_gc->regs + ALTERA_GPIO_EDGE_CAP);
		for_each_set_bit(i, &status, mm_gc->gc.ngpio)
			generic_handle_domain_irq(irqdomain, i);
	}

	chained_irq_exit(chip, desc);
}

static void altera_gpio_irq_leveL_high_handler(struct irq_desc *desc)
{
	struct altera_gpio_chip *altera_gc;
	struct irq_chip *chip;
	struct of_mm_gpio_chip *mm_gc;
	struct irq_domain *irqdomain;
	unsigned long status;
	int i;

	altera_gc = gpiochip_get_data(irq_desc_get_handler_data(desc));
	chip = irq_desc_get_chip(desc);
	mm_gc = &altera_gc->mmchip;
	irqdomain = altera_gc->mmchip.gc.irq.domain;

	chained_irq_enter(chip, desc);

	status = readl(mm_gc->regs + ALTERA_GPIO_DATA);
	status &= readl(mm_gc->regs + ALTERA_GPIO_IRQ_MASK);

	for_each_set_bit(i, &status, mm_gc->gc.ngpio)
		generic_handle_domain_irq(irqdomain, i);

	chained_irq_exit(chip, desc);
}

static const struct irq_chip altera_gpio_irq_chip = {
	.name = "altera-gpio",
	.irq_mask = altera_gpio_irq_mask,
	.irq_unmask = altera_gpio_irq_unmask,
	.irq_set_type = altera_gpio_irq_set_type,
	.irq_startup  = altera_gpio_irq_startup,
	.irq_shutdown = altera_gpio_irq_mask,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int altera_gpio_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	int reg, ret;
	struct altera_gpio_chip *altera_gc;
	struct gpio_irq_chip *girq;

	altera_gc = devm_kzalloc(&pdev->dev, sizeof(*altera_gc), GFP_KERNEL);
	if (!altera_gc)
		return -ENOMEM;

	raw_spin_lock_init(&altera_gc->gpio_lock);

	if (of_property_read_u32(node, "altr,ngpio", &reg))
		/* By default assume maximum ngpio */
		altera_gc->mmchip.gc.ngpio = ALTERA_GPIO_MAX_NGPIO;
	else
		altera_gc->mmchip.gc.ngpio = reg;

	if (altera_gc->mmchip.gc.ngpio > ALTERA_GPIO_MAX_NGPIO) {
		dev_warn(&pdev->dev,
			"ngpio is greater than %d, defaulting to %d\n",
			ALTERA_GPIO_MAX_NGPIO, ALTERA_GPIO_MAX_NGPIO);
		altera_gc->mmchip.gc.ngpio = ALTERA_GPIO_MAX_NGPIO;
	}

	altera_gc->mmchip.gc.direction_input	= altera_gpio_direction_input;
	altera_gc->mmchip.gc.direction_output	= altera_gpio_direction_output;
	altera_gc->mmchip.gc.get		= altera_gpio_get;
	altera_gc->mmchip.gc.set		= altera_gpio_set;
	altera_gc->mmchip.gc.owner		= THIS_MODULE;
	altera_gc->mmchip.gc.parent		= &pdev->dev;

	altera_gc->mapped_irq = platform_get_irq_optional(pdev, 0);

	if (altera_gc->mapped_irq < 0)
		goto skip_irq;

	if (of_property_read_u32(node, "altr,interrupt-type", &reg)) {
		dev_err(&pdev->dev,
			"altr,interrupt-type value not set in device tree\n");
		return -EINVAL;
	}
	altera_gc->interrupt_trigger = reg;

	girq = &altera_gc->mmchip.gc.irq;
	gpio_irq_chip_set_chip(girq, &altera_gpio_irq_chip);

	if (altera_gc->interrupt_trigger == IRQ_TYPE_LEVEL_HIGH)
		girq->parent_handler = altera_gpio_irq_leveL_high_handler;
	else
		girq->parent_handler = altera_gpio_irq_edge_handler;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(&pdev->dev, 1, sizeof(*girq->parents),
				     GFP_KERNEL);
	if (!girq->parents)
		return -ENOMEM;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_bad_irq;
	girq->parents[0] = altera_gc->mapped_irq;

skip_irq:
	ret = of_mm_gpiochip_add_data(node, &altera_gc->mmchip, altera_gc);
	if (ret) {
		dev_err(&pdev->dev, "Failed adding memory mapped gpiochip\n");
		return ret;
	}

	platform_set_drvdata(pdev, altera_gc);

	return 0;
}

static int altera_gpio_remove(struct platform_device *pdev)
{
	struct altera_gpio_chip *altera_gc = platform_get_drvdata(pdev);

	of_mm_gpiochip_remove(&altera_gc->mmchip);

	return 0;
}

static const struct of_device_id altera_gpio_of_match[] = {
	{ .compatible = "altr,pio-1.0", },
	{},
};
MODULE_DEVICE_TABLE(of, altera_gpio_of_match);

static struct platform_driver altera_gpio_driver = {
	.driver = {
		.name	= "altera_gpio",
		.of_match_table = altera_gpio_of_match,
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

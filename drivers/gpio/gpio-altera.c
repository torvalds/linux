// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013 Altera Corporation
 * Based on gpio-mpc8xxx.c
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>

#define ALTERA_GPIO_MAX_NGPIO		32
#define ALTERA_GPIO_DATA		0x0
#define ALTERA_GPIO_DIR			0x4
#define ALTERA_GPIO_IRQ_MASK		0x8
#define ALTERA_GPIO_EDGE_CAP		0xc

/**
* struct altera_gpio_chip
* @chip			: Generic GPIO chip structure.
* @regs			: memory mapped IO address for the controller registers.
* @gpio_lock		: synchronization lock so that new irq/set/get requests
*			  will be blocked until the current one completes.
* @interrupt_trigger	: specifies the hardware configured IRQ trigger type
*			  (rising, falling, both, high)
*/
struct altera_gpio_chip {
	struct gpio_generic_chip chip;
	void __iomem *regs;
	raw_spinlock_t gpio_lock;
	int interrupt_trigger;
};

static void altera_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct altera_gpio_chip *altera_gc = gpiochip_get_data(gc);
	unsigned long flags;
	u32 intmask;

	gpiochip_enable_irq(gc, irqd_to_hwirq(d));

	raw_spin_lock_irqsave(&altera_gc->gpio_lock, flags);
	intmask = readl(altera_gc->regs + ALTERA_GPIO_IRQ_MASK);
	/* Set ALTERA_GPIO_IRQ_MASK bit to unmask */
	intmask |= BIT(irqd_to_hwirq(d));
	writel(intmask, altera_gc->regs + ALTERA_GPIO_IRQ_MASK);
	raw_spin_unlock_irqrestore(&altera_gc->gpio_lock, flags);
}

static void altera_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct altera_gpio_chip *altera_gc = gpiochip_get_data(gc);
	unsigned long flags;
	u32 intmask;

	raw_spin_lock_irqsave(&altera_gc->gpio_lock, flags);
	intmask = readl(altera_gc->regs + ALTERA_GPIO_IRQ_MASK);
	/* Clear ALTERA_GPIO_IRQ_MASK bit to mask */
	intmask &= ~BIT(irqd_to_hwirq(d));
	writel(intmask, altera_gc->regs + ALTERA_GPIO_IRQ_MASK);
	raw_spin_unlock_irqrestore(&altera_gc->gpio_lock, flags);

	gpiochip_disable_irq(gc, irqd_to_hwirq(d));
}

/*
 * This controller's IRQ type is synthesized in hardware, so this function
 * just checks if the requested set_type matches the synthesized IRQ type
 */
static int altera_gpio_irq_set_type(struct irq_data *d,
				   unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct altera_gpio_chip *altera_gc = gpiochip_get_data(gc);

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

static void altera_gpio_irq_edge_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct altera_gpio_chip *altera_gc = gpiochip_get_data(gc);
	struct irq_domain *irqdomain = gc->irq.domain;
	struct irq_chip *chip;
	unsigned long status;
	int i;

	chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	while ((status =
	        (readl(altera_gc->regs + ALTERA_GPIO_EDGE_CAP) &
	         readl(altera_gc->regs + ALTERA_GPIO_IRQ_MASK)))) {
		writel(status, altera_gc->regs + ALTERA_GPIO_EDGE_CAP);
		for_each_set_bit(i, &status, gc->ngpio)
			generic_handle_domain_irq(irqdomain, i);
	}

	chained_irq_exit(chip, desc);
}

static void altera_gpio_irq_leveL_high_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct altera_gpio_chip *altera_gc = gpiochip_get_data(gc);
	struct irq_domain *irqdomain = gc->irq.domain;
	struct irq_chip *chip;
	unsigned long status;
	int i;

	chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	status = readl(altera_gc->regs + ALTERA_GPIO_DATA);
	status &= readl(altera_gc->regs + ALTERA_GPIO_IRQ_MASK);

	for_each_set_bit(i, &status, gc->ngpio)
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
	struct gpio_generic_chip_config config;
	struct device *dev = &pdev->dev;
	int reg, ret;
	struct altera_gpio_chip *altera_gc;
	struct gpio_generic_chip *chip;
	struct gpio_chip *gc;
	struct gpio_irq_chip *girq;
	int mapped_irq;

	altera_gc = devm_kzalloc(&pdev->dev, sizeof(*altera_gc), GFP_KERNEL);
	if (!altera_gc)
		return -ENOMEM;

	raw_spin_lock_init(&altera_gc->gpio_lock);

	altera_gc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(altera_gc->regs))
		return dev_err_probe(dev, PTR_ERR(altera_gc->regs),
				     "failed to ioremap memory resource\n");

	chip = &altera_gc->chip;

	config = (struct gpio_generic_chip_config) {
		.dev = dev,
		.sz = 4,
		.dat = altera_gc->regs + ALTERA_GPIO_DATA,
		.set = altera_gc->regs + ALTERA_GPIO_DATA,
		.dirout = altera_gc->regs + ALTERA_GPIO_DIR,
	};

	ret = gpio_generic_chip_init(chip, &config);
	if (ret)
		return dev_err_probe(dev, ret, "unable to init generic GPIO\n");

	gc = &chip->gc;

	if (device_property_read_u32(dev, "altr,ngpio", &reg))
		/* By default assume maximum ngpio */
		gc->ngpio = ALTERA_GPIO_MAX_NGPIO;
	else
		gc->ngpio = reg;

	if (gc->ngpio > ALTERA_GPIO_MAX_NGPIO) {
		dev_warn(&pdev->dev,
			"ngpio is greater than %d, defaulting to %d\n",
			ALTERA_GPIO_MAX_NGPIO, ALTERA_GPIO_MAX_NGPIO);
		gc->ngpio = ALTERA_GPIO_MAX_NGPIO;
	}

	gc->base = -1;
	gc->label = devm_kasprintf(dev, GFP_KERNEL, "%pfw", dev_fwnode(dev));
	if (!gc->label)
		return -ENOMEM;

	mapped_irq = platform_get_irq_optional(pdev, 0);
	if (mapped_irq < 0)
		goto skip_irq;

	if (device_property_read_u32(dev, "altr,interrupt-type", &reg)) {
		dev_err(&pdev->dev,
			"altr,interrupt-type value not set in device tree\n");
		return -EINVAL;
	}
	altera_gc->interrupt_trigger = reg;

	girq = &gc->irq;
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
	girq->parents[0] = mapped_irq;

skip_irq:
	ret = devm_gpiochip_add_data(dev, gc, altera_gc);
	if (ret) {
		dev_err(&pdev->dev, "Failed adding memory mapped gpiochip\n");
		return ret;
	}

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

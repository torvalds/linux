// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/gpio/gpio-mb86s7x.c
 *
 *  Copyright (C) 2015 Fujitsu Semiconductor Limited
 *  Copyright (C) 2015 Linaro Ltd.
 */

#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/of_device.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#include "gpiolib.h"

/*
 * Only first 8bits of a register correspond to each pin,
 * so there are 4 registers for 32 pins.
 */
#define PDR(x)	(0x0 + x / 8 * 4)
#define DDR(x)	(0x10 + x / 8 * 4)
#define PFR(x)	(0x20 + x / 8 * 4)

#define OFFSET(x)	BIT((x) % 8)

struct mb86s70_gpio_chip {
	struct gpio_chip gc;
	void __iomem *base;
	struct clk *clk;
	spinlock_t lock;
};

static int mb86s70_gpio_request(struct gpio_chip *gc, unsigned gpio)
{
	struct mb86s70_gpio_chip *gchip = gpiochip_get_data(gc);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&gchip->lock, flags);

	val = readl(gchip->base + PFR(gpio));
	val &= ~OFFSET(gpio);
	writel(val, gchip->base + PFR(gpio));

	spin_unlock_irqrestore(&gchip->lock, flags);

	return 0;
}

static void mb86s70_gpio_free(struct gpio_chip *gc, unsigned gpio)
{
	struct mb86s70_gpio_chip *gchip = gpiochip_get_data(gc);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&gchip->lock, flags);

	val = readl(gchip->base + PFR(gpio));
	val |= OFFSET(gpio);
	writel(val, gchip->base + PFR(gpio));

	spin_unlock_irqrestore(&gchip->lock, flags);
}

static int mb86s70_gpio_direction_input(struct gpio_chip *gc, unsigned gpio)
{
	struct mb86s70_gpio_chip *gchip = gpiochip_get_data(gc);
	unsigned long flags;
	unsigned char val;

	spin_lock_irqsave(&gchip->lock, flags);

	val = readl(gchip->base + DDR(gpio));
	val &= ~OFFSET(gpio);
	writel(val, gchip->base + DDR(gpio));

	spin_unlock_irqrestore(&gchip->lock, flags);

	return 0;
}

static int mb86s70_gpio_direction_output(struct gpio_chip *gc,
					 unsigned gpio, int value)
{
	struct mb86s70_gpio_chip *gchip = gpiochip_get_data(gc);
	unsigned long flags;
	unsigned char val;

	spin_lock_irqsave(&gchip->lock, flags);

	val = readl(gchip->base + PDR(gpio));
	if (value)
		val |= OFFSET(gpio);
	else
		val &= ~OFFSET(gpio);
	writel(val, gchip->base + PDR(gpio));

	val = readl(gchip->base + DDR(gpio));
	val |= OFFSET(gpio);
	writel(val, gchip->base + DDR(gpio));

	spin_unlock_irqrestore(&gchip->lock, flags);

	return 0;
}

static int mb86s70_gpio_get(struct gpio_chip *gc, unsigned gpio)
{
	struct mb86s70_gpio_chip *gchip = gpiochip_get_data(gc);

	return !!(readl(gchip->base + PDR(gpio)) & OFFSET(gpio));
}

static void mb86s70_gpio_set(struct gpio_chip *gc, unsigned gpio, int value)
{
	struct mb86s70_gpio_chip *gchip = gpiochip_get_data(gc);
	unsigned long flags;
	unsigned char val;

	spin_lock_irqsave(&gchip->lock, flags);

	val = readl(gchip->base + PDR(gpio));
	if (value)
		val |= OFFSET(gpio);
	else
		val &= ~OFFSET(gpio);
	writel(val, gchip->base + PDR(gpio));

	spin_unlock_irqrestore(&gchip->lock, flags);
}

static int mb86s70_gpio_to_irq(struct gpio_chip *gc, unsigned int offset)
{
	int irq, index;

	for (index = 0;; index++) {
		irq = platform_get_irq(to_platform_device(gc->parent), index);
		if (irq <= 0)
			break;
		if (irq_get_irq_data(irq)->hwirq == offset)
			return irq;
	}
	return -EINVAL;
}

static int mb86s70_gpio_probe(struct platform_device *pdev)
{
	struct mb86s70_gpio_chip *gchip;
	int ret;

	gchip = devm_kzalloc(&pdev->dev, sizeof(*gchip), GFP_KERNEL);
	if (gchip == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, gchip);

	gchip->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gchip->base))
		return PTR_ERR(gchip->base);

	if (!has_acpi_companion(&pdev->dev)) {
		gchip->clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(gchip->clk))
			return PTR_ERR(gchip->clk);

		ret = clk_prepare_enable(gchip->clk);
		if (ret)
			return ret;
	}

	spin_lock_init(&gchip->lock);

	gchip->gc.direction_output = mb86s70_gpio_direction_output;
	gchip->gc.direction_input = mb86s70_gpio_direction_input;
	gchip->gc.request = mb86s70_gpio_request;
	gchip->gc.free = mb86s70_gpio_free;
	gchip->gc.get = mb86s70_gpio_get;
	gchip->gc.set = mb86s70_gpio_set;
	gchip->gc.label = dev_name(&pdev->dev);
	gchip->gc.ngpio = 32;
	gchip->gc.owner = THIS_MODULE;
	gchip->gc.parent = &pdev->dev;
	gchip->gc.base = -1;

	if (has_acpi_companion(&pdev->dev))
		gchip->gc.to_irq = mb86s70_gpio_to_irq;

	ret = gpiochip_add_data(&gchip->gc, gchip);
	if (ret) {
		dev_err(&pdev->dev, "couldn't register gpio driver\n");
		clk_disable_unprepare(gchip->clk);
		return ret;
	}

	if (has_acpi_companion(&pdev->dev))
		acpi_gpiochip_request_interrupts(&gchip->gc);

	return 0;
}

static int mb86s70_gpio_remove(struct platform_device *pdev)
{
	struct mb86s70_gpio_chip *gchip = platform_get_drvdata(pdev);

	if (has_acpi_companion(&pdev->dev))
		acpi_gpiochip_free_interrupts(&gchip->gc);
	gpiochip_remove(&gchip->gc);
	clk_disable_unprepare(gchip->clk);

	return 0;
}

static const struct of_device_id mb86s70_gpio_dt_ids[] = {
	{ .compatible = "fujitsu,mb86s70-gpio" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mb86s70_gpio_dt_ids);

#ifdef CONFIG_ACPI
static const struct acpi_device_id mb86s70_gpio_acpi_ids[] = {
	{ "SCX0007" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(acpi, mb86s70_gpio_acpi_ids);
#endif

static struct platform_driver mb86s70_gpio_driver = {
	.driver = {
		.name = "mb86s70-gpio",
		.of_match_table = mb86s70_gpio_dt_ids,
		.acpi_match_table = ACPI_PTR(mb86s70_gpio_acpi_ids),
	},
	.probe = mb86s70_gpio_probe,
	.remove = mb86s70_gpio_remove,
};
module_platform_driver(mb86s70_gpio_driver);

MODULE_DESCRIPTION("MB86S7x GPIO Driver");
MODULE_ALIAS("platform:mb86s70-gpio");
MODULE_LICENSE("GPL");

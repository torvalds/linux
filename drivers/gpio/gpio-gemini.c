/*
 * Gemini gpiochip and interrupt routines
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 *
 * Based on arch/arm/mach-gemini/gpio.c:
 * Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * Based on plat-mxc/gpio.c:
 * MXC GPIO support. (c) 2008 Daniel Mack <daniel@caiaq.de>
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 */
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>

/* GPIO registers definition */
#define GPIO_DATA_OUT		0x00
#define GPIO_DATA_IN		0x04
#define GPIO_DIR		0x08
#define GPIO_DATA_SET		0x10
#define GPIO_DATA_CLR		0x14
#define GPIO_PULL_EN		0x18
#define GPIO_PULL_TYPE		0x1C
#define GPIO_INT_EN		0x20
#define GPIO_INT_STAT		0x24
#define GPIO_INT_MASK		0x2C
#define GPIO_INT_CLR		0x30
#define GPIO_INT_TYPE		0x34
#define GPIO_INT_BOTH_EDGE	0x38
#define GPIO_INT_LEVEL		0x3C
#define GPIO_DEBOUNCE_EN	0x40
#define GPIO_DEBOUNCE_PRESCALE	0x44

/**
 * struct gemini_gpio - Gemini GPIO state container
 * @dev: containing device for this instance
 * @gc: gpiochip for this instance
 */
struct gemini_gpio {
	struct device *dev;
	struct gpio_chip gc;
	void __iomem *base;
};

static void gemini_gpio_ack_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct gemini_gpio *g = gpiochip_get_data(gc);

	writel(BIT(irqd_to_hwirq(d)), g->base + GPIO_INT_CLR);
}

static void gemini_gpio_mask_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct gemini_gpio *g = gpiochip_get_data(gc);
	u32 val;

	val = readl(g->base + GPIO_INT_EN);
	val &= ~BIT(irqd_to_hwirq(d));
	writel(val, g->base + GPIO_INT_EN);
}

static void gemini_gpio_unmask_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct gemini_gpio *g = gpiochip_get_data(gc);
	u32 val;

	val = readl(g->base + GPIO_INT_EN);
	val |= BIT(irqd_to_hwirq(d));
	writel(val, g->base + GPIO_INT_EN);
}

static int gemini_gpio_set_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct gemini_gpio *g = gpiochip_get_data(gc);
	u32 mask = BIT(irqd_to_hwirq(d));
	u32 reg_both, reg_level, reg_type;

	reg_type = readl(g->base + GPIO_INT_TYPE);
	reg_level = readl(g->base + GPIO_INT_LEVEL);
	reg_both = readl(g->base + GPIO_INT_BOTH_EDGE);

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		irq_set_handler_locked(d, handle_edge_irq);
		reg_type &= ~mask;
		reg_both |= mask;
		break;
	case IRQ_TYPE_EDGE_RISING:
		irq_set_handler_locked(d, handle_edge_irq);
		reg_type &= ~mask;
		reg_both &= ~mask;
		reg_level &= ~mask;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		irq_set_handler_locked(d, handle_edge_irq);
		reg_type &= ~mask;
		reg_both &= ~mask;
		reg_level |= mask;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		irq_set_handler_locked(d, handle_level_irq);
		reg_type |= mask;
		reg_level &= ~mask;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		irq_set_handler_locked(d, handle_level_irq);
		reg_type |= mask;
		reg_level |= mask;
		break;
	default:
		irq_set_handler_locked(d, handle_bad_irq);
		return -EINVAL;
	}

	writel(reg_type, g->base + GPIO_INT_TYPE);
	writel(reg_level, g->base + GPIO_INT_LEVEL);
	writel(reg_both, g->base + GPIO_INT_BOTH_EDGE);

	gemini_gpio_ack_irq(d);

	return 0;
}

static struct irq_chip gemini_gpio_irqchip = {
	.name = "GPIO",
	.irq_ack = gemini_gpio_ack_irq,
	.irq_mask = gemini_gpio_mask_irq,
	.irq_unmask = gemini_gpio_unmask_irq,
	.irq_set_type = gemini_gpio_set_irq_type,
};

static void gemini_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct gemini_gpio *g = gpiochip_get_data(gc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	int offset;
	unsigned long stat;

	chained_irq_enter(irqchip, desc);

	stat = readl(g->base + GPIO_INT_STAT);
	if (stat)
		for_each_set_bit(offset, &stat, gc->ngpio)
			generic_handle_irq(irq_find_mapping(gc->irqdomain,
							    offset));

	chained_irq_exit(irqchip, desc);
}

static int gemini_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct gemini_gpio *g;
	int irq;
	int ret;

	g = devm_kzalloc(dev, sizeof(*g), GFP_KERNEL);
	if (!g)
		return -ENOMEM;

	g->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	g->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(g->base))
		return PTR_ERR(g->base);

	irq = platform_get_irq(pdev, 0);
	if (!irq)
		return -EINVAL;

	ret = bgpio_init(&g->gc, dev, 4,
			 g->base + GPIO_DATA_IN,
			 g->base + GPIO_DATA_SET,
			 g->base + GPIO_DATA_CLR,
			 g->base + GPIO_DIR,
			 NULL,
			 0);
	if (ret) {
		dev_err(dev, "unable to init generic GPIO\n");
		return ret;
	}
	g->gc.label = "Gemini";
	g->gc.base = -1;
	g->gc.parent = dev;
	g->gc.owner = THIS_MODULE;
	/* ngpio is set by bgpio_init() */

	ret = devm_gpiochip_add_data(dev, &g->gc, g);
	if (ret)
		return ret;

	/* Disable, unmask and clear all interrupts */
	writel(0x0, g->base + GPIO_INT_EN);
	writel(0x0, g->base + GPIO_INT_MASK);
	writel(~0x0, g->base + GPIO_INT_CLR);

	ret = gpiochip_irqchip_add(&g->gc, &gemini_gpio_irqchip,
				   0, handle_bad_irq,
				   IRQ_TYPE_NONE);
	if (ret) {
		dev_info(dev, "could not add irqchip\n");
		return ret;
	}
	gpiochip_set_chained_irqchip(&g->gc, &gemini_gpio_irqchip,
				     irq, gemini_gpio_irq_handler);

	dev_info(dev, "Gemini GPIO @%p registered\n", g->base);

	return 0;
}

static const struct of_device_id gemini_gpio_of_match[] = {
	{
		.compatible = "cortina,gemini-gpio",
	},
	{},
};

static struct platform_driver gemini_gpio_driver = {
	.driver = {
		.name		= "gemini-gpio",
		.of_match_table = of_match_ptr(gemini_gpio_of_match),
	},
	.probe	= gemini_gpio_probe,
};
builtin_platform_driver(gemini_gpio_driver);

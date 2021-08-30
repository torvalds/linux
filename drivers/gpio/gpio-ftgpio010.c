// SPDX-License-Identifier: GPL-2.0
/*
 * Faraday Technolog FTGPIO010 gpiochip and interrupt routines
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
#include <linux/bitops.h>
#include <linux/clk.h>

/* GPIO registers definition */
#define GPIO_DATA_OUT		0x00
#define GPIO_DATA_IN		0x04
#define GPIO_DIR		0x08
#define GPIO_BYPASS_IN		0x0C
#define GPIO_DATA_SET		0x10
#define GPIO_DATA_CLR		0x14
#define GPIO_PULL_EN		0x18
#define GPIO_PULL_TYPE		0x1C
#define GPIO_INT_EN		0x20
#define GPIO_INT_STAT_RAW	0x24
#define GPIO_INT_STAT_MASKED	0x28
#define GPIO_INT_MASK		0x2C
#define GPIO_INT_CLR		0x30
#define GPIO_INT_TYPE		0x34
#define GPIO_INT_BOTH_EDGE	0x38
#define GPIO_INT_LEVEL		0x3C
#define GPIO_DEBOUNCE_EN	0x40
#define GPIO_DEBOUNCE_PRESCALE	0x44

/**
 * struct ftgpio_gpio - Gemini GPIO state container
 * @dev: containing device for this instance
 * @gc: gpiochip for this instance
 * @irq: irqchip for this instance
 * @base: remapped I/O-memory base
 * @clk: silicon clock
 */
struct ftgpio_gpio {
	struct device *dev;
	struct gpio_chip gc;
	struct irq_chip irq;
	void __iomem *base;
	struct clk *clk;
};

static void ftgpio_gpio_ack_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct ftgpio_gpio *g = gpiochip_get_data(gc);

	writel(BIT(irqd_to_hwirq(d)), g->base + GPIO_INT_CLR);
}

static void ftgpio_gpio_mask_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct ftgpio_gpio *g = gpiochip_get_data(gc);
	u32 val;

	val = readl(g->base + GPIO_INT_EN);
	val &= ~BIT(irqd_to_hwirq(d));
	writel(val, g->base + GPIO_INT_EN);
}

static void ftgpio_gpio_unmask_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct ftgpio_gpio *g = gpiochip_get_data(gc);
	u32 val;

	val = readl(g->base + GPIO_INT_EN);
	val |= BIT(irqd_to_hwirq(d));
	writel(val, g->base + GPIO_INT_EN);
}

static int ftgpio_gpio_set_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct ftgpio_gpio *g = gpiochip_get_data(gc);
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

	ftgpio_gpio_ack_irq(d);

	return 0;
}

static void ftgpio_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct ftgpio_gpio *g = gpiochip_get_data(gc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	int offset;
	unsigned long stat;

	chained_irq_enter(irqchip, desc);

	stat = readl(g->base + GPIO_INT_STAT_RAW);
	if (stat)
		for_each_set_bit(offset, &stat, gc->ngpio)
			generic_handle_domain_irq(gc->irq.domain, offset);

	chained_irq_exit(irqchip, desc);
}

static int ftgpio_gpio_set_config(struct gpio_chip *gc, unsigned int offset,
				  unsigned long config)
{
	enum pin_config_param param = pinconf_to_config_param(config);
	u32 arg = pinconf_to_config_argument(config);
	struct ftgpio_gpio *g = gpiochip_get_data(gc);
	unsigned long pclk_freq;
	u32 deb_div;
	u32 val;

	if (param != PIN_CONFIG_INPUT_DEBOUNCE)
		return -ENOTSUPP;

	/*
	 * Debounce only works if interrupts are enabled. The manual
	 * states that if PCLK is 66 MHz, and this is set to 0x7D0, then
	 * PCLK is divided down to 33 kHz for the debounce timer. 0x7D0 is
	 * 2000 decimal, so what they mean is simply that the PCLK is
	 * divided by this value.
	 *
	 * As we get a debounce setting in microseconds, we calculate the
	 * desired period time and see if we can get a suitable debounce
	 * time.
	 */
	pclk_freq = clk_get_rate(g->clk);
	deb_div = DIV_ROUND_CLOSEST(pclk_freq, arg);

	/* This register is only 24 bits wide */
	if (deb_div > (1 << 24))
		return -ENOTSUPP;

	dev_dbg(g->dev, "prescale divisor: %08x, resulting frequency %lu Hz\n",
		deb_div, (pclk_freq/deb_div));

	val = readl(g->base + GPIO_DEBOUNCE_PRESCALE);
	if (val == deb_div) {
		/*
		 * The debounce timer happens to already be set to the
		 * desirable value, what a coincidence! We can just enable
		 * debounce on this GPIO line and return. This happens more
		 * often than you think, for example when all GPIO keys
		 * on a system are requesting the same debounce interval.
		 */
		val = readl(g->base + GPIO_DEBOUNCE_EN);
		val |= BIT(offset);
		writel(val, g->base + GPIO_DEBOUNCE_EN);
		return 0;
	}

	val = readl(g->base + GPIO_DEBOUNCE_EN);
	if (val) {
		/*
		 * Oh no! Someone is already using the debounce with
		 * another setting than what we need. Bummer.
		 */
		return -ENOTSUPP;
	}

	/* First come, first serve */
	writel(deb_div, g->base + GPIO_DEBOUNCE_PRESCALE);
	/* Enable debounce */
	val |= BIT(offset);
	writel(val, g->base + GPIO_DEBOUNCE_EN);

	return 0;
}

static int ftgpio_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ftgpio_gpio *g;
	struct gpio_irq_chip *girq;
	int irq;
	int ret;

	g = devm_kzalloc(dev, sizeof(*g), GFP_KERNEL);
	if (!g)
		return -ENOMEM;

	g->dev = dev;

	g->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(g->base))
		return PTR_ERR(g->base);

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return irq ? irq : -EINVAL;

	g->clk = devm_clk_get(dev, NULL);
	if (!IS_ERR(g->clk)) {
		ret = clk_prepare_enable(g->clk);
		if (ret)
			return ret;
	} else if (PTR_ERR(g->clk) == -EPROBE_DEFER) {
		/*
		 * Percolate deferrals, for anything else,
		 * just live without the clocking.
		 */
		return PTR_ERR(g->clk);
	}

	ret = bgpio_init(&g->gc, dev, 4,
			 g->base + GPIO_DATA_IN,
			 g->base + GPIO_DATA_SET,
			 g->base + GPIO_DATA_CLR,
			 g->base + GPIO_DIR,
			 NULL,
			 0);
	if (ret) {
		dev_err(dev, "unable to init generic GPIO\n");
		goto dis_clk;
	}
	g->gc.label = "FTGPIO010";
	g->gc.base = -1;
	g->gc.parent = dev;
	g->gc.owner = THIS_MODULE;
	/* ngpio is set by bgpio_init() */

	/* We need a silicon clock to do debounce */
	if (!IS_ERR(g->clk))
		g->gc.set_config = ftgpio_gpio_set_config;

	g->irq.name = "FTGPIO010";
	g->irq.irq_ack = ftgpio_gpio_ack_irq;
	g->irq.irq_mask = ftgpio_gpio_mask_irq;
	g->irq.irq_unmask = ftgpio_gpio_unmask_irq;
	g->irq.irq_set_type = ftgpio_gpio_set_irq_type;

	girq = &g->gc.irq;
	girq->chip = &g->irq;
	girq->parent_handler = ftgpio_gpio_irq_handler;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(dev, 1, sizeof(*girq->parents),
				     GFP_KERNEL);
	if (!girq->parents) {
		ret = -ENOMEM;
		goto dis_clk;
	}
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_bad_irq;
	girq->parents[0] = irq;

	/* Disable, unmask and clear all interrupts */
	writel(0x0, g->base + GPIO_INT_EN);
	writel(0x0, g->base + GPIO_INT_MASK);
	writel(~0x0, g->base + GPIO_INT_CLR);

	/* Clear any use of debounce */
	writel(0x0, g->base + GPIO_DEBOUNCE_EN);

	ret = devm_gpiochip_add_data(dev, &g->gc, g);
	if (ret)
		goto dis_clk;

	platform_set_drvdata(pdev, g);
	dev_info(dev, "FTGPIO010 @%p registered\n", g->base);

	return 0;

dis_clk:
	if (!IS_ERR(g->clk))
		clk_disable_unprepare(g->clk);
	return ret;
}

static int ftgpio_gpio_remove(struct platform_device *pdev)
{
	struct ftgpio_gpio *g = platform_get_drvdata(pdev);

	if (!IS_ERR(g->clk))
		clk_disable_unprepare(g->clk);
	return 0;
}

static const struct of_device_id ftgpio_gpio_of_match[] = {
	{
		.compatible = "cortina,gemini-gpio",
	},
	{
		.compatible = "moxa,moxart-gpio",
	},
	{
		.compatible = "faraday,ftgpio010",
	},
	{},
};

static struct platform_driver ftgpio_gpio_driver = {
	.driver = {
		.name		= "ftgpio010-gpio",
		.of_match_table = of_match_ptr(ftgpio_gpio_of_match),
	},
	.probe = ftgpio_gpio_probe,
	.remove = ftgpio_gpio_remove,
};
builtin_platform_driver(ftgpio_gpio_driver);

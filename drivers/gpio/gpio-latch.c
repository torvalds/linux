// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO latch driver
 *
 *  Copyright (C) 2022 Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This driver implements a GPIO (or better GPO as there is no input)
 * multiplexer based on latches like this:
 *
 * CLK0 ----------------------.        ,--------.
 * CLK1 -------------------.  `--------|>    #0 |
 *                         |           |        |
 * OUT0 ----------------+--|-----------|D0    Q0|-----|<
 * OUT1 --------------+-|--|-----------|D1    Q1|-----|<
 * OUT2 ------------+-|-|--|-----------|D2    Q2|-----|<
 * OUT3 ----------+-|-|-|--|-----------|D3    Q3|-----|<
 * OUT4 --------+-|-|-|-|--|-----------|D4    Q4|-----|<
 * OUT5 ------+-|-|-|-|-|--|-----------|D5    Q5|-----|<
 * OUT6 ----+-|-|-|-|-|-|--|-----------|D6    Q6|-----|<
 * OUT7 --+-|-|-|-|-|-|-|--|-----------|D7    Q7|-----|<
 *        | | | | | | | |  |           `--------'
 *        | | | | | | | |  |
 *        | | | | | | | |  |           ,--------.
 *        | | | | | | | |  `-----------|>    #1 |
 *        | | | | | | | |              |        |
 *        | | | | | | | `--------------|D0    Q0|-----|<
 *        | | | | | | `----------------|D1    Q1|-----|<
 *        | | | | | `------------------|D2    Q2|-----|<
 *        | | | | `--------------------|D3    Q3|-----|<
 *        | | | `----------------------|D4    Q4|-----|<
 *        | | `------------------------|D5    Q5|-----|<
 *        | `--------------------------|D6    Q6|-----|<
 *        `----------------------------|D7    Q7|-----|<
 *                                     `--------'
 *
 * The above is just an example. The actual number of number of latches and
 * the number of inputs per latch is derived from the number of GPIOs given
 * in the corresponding device tree properties.
 */

#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#include "gpiolib.h"

struct gpio_latch_priv {
	struct gpio_chip gc;
	struct gpio_descs *clk_gpios;
	struct gpio_descs *latched_gpios;
	int n_latched_gpios;
	unsigned int setup_duration_ns;
	unsigned int clock_duration_ns;
	unsigned long *shadow;
	/*
	 * Depending on whether any of the underlying GPIOs may sleep we either
	 * use a mutex or a spinlock to protect our shadow map.
	 */
	union {
		struct mutex mutex; /* protects @shadow */
		spinlock_t spinlock; /* protects @shadow */
	};
};

static int gpio_latch_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	return GPIO_LINE_DIRECTION_OUT;
}

static void gpio_latch_set_unlocked(struct gpio_latch_priv *priv,
				    void (*set)(struct gpio_desc *desc, int value),
				    unsigned int offset, bool val)
{
	int latch = offset / priv->n_latched_gpios;
	int i;

	assign_bit(offset, priv->shadow, val);

	for (i = 0; i < priv->n_latched_gpios; i++)
		set(priv->latched_gpios->desc[i],
		    test_bit(latch * priv->n_latched_gpios + i, priv->shadow));

	ndelay(priv->setup_duration_ns);
	set(priv->clk_gpios->desc[latch], 1);
	ndelay(priv->clock_duration_ns);
	set(priv->clk_gpios->desc[latch], 0);
}

static void gpio_latch_set(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct gpio_latch_priv *priv = gpiochip_get_data(gc);
	unsigned long flags;

	spin_lock_irqsave(&priv->spinlock, flags);

	gpio_latch_set_unlocked(priv, gpiod_set_value, offset, val);

	spin_unlock_irqrestore(&priv->spinlock, flags);
}

static void gpio_latch_set_can_sleep(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct gpio_latch_priv *priv = gpiochip_get_data(gc);

	mutex_lock(&priv->mutex);

	gpio_latch_set_unlocked(priv, gpiod_set_value_cansleep, offset, val);

	mutex_unlock(&priv->mutex);
}

static bool gpio_latch_can_sleep(struct gpio_latch_priv *priv, unsigned int n_latches)
{
	int i;

	for (i = 0; i < n_latches; i++)
		if (gpiod_cansleep(priv->clk_gpios->desc[i]))
			return true;

	for (i = 0; i < priv->n_latched_gpios; i++)
		if (gpiod_cansleep(priv->latched_gpios->desc[i]))
			return true;

	return false;
}

/*
 * Some value which is still acceptable to delay in atomic context.
 * If we need to go higher we might have to switch to usleep_range(),
 * but that cannot ne used in atomic context and the driver would have
 * to be adjusted to support that.
 */
#define DURATION_NS_MAX 5000

static int gpio_latch_probe(struct platform_device *pdev)
{
	struct gpio_latch_priv *priv;
	unsigned int n_latches;
	struct device_node *np = pdev->dev.of_node;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk_gpios = devm_gpiod_get_array(&pdev->dev, "clk", GPIOD_OUT_LOW);
	if (IS_ERR(priv->clk_gpios))
		return PTR_ERR(priv->clk_gpios);

	priv->latched_gpios = devm_gpiod_get_array(&pdev->dev, "latched", GPIOD_OUT_LOW);
	if (IS_ERR(priv->latched_gpios))
		return PTR_ERR(priv->latched_gpios);

	n_latches = priv->clk_gpios->ndescs;
	priv->n_latched_gpios = priv->latched_gpios->ndescs;

	priv->shadow = devm_bitmap_zalloc(&pdev->dev, n_latches * priv->n_latched_gpios,
					  GFP_KERNEL);
	if (!priv->shadow)
		return -ENOMEM;

	if (gpio_latch_can_sleep(priv, n_latches)) {
		priv->gc.can_sleep = true;
		priv->gc.set = gpio_latch_set_can_sleep;
		mutex_init(&priv->mutex);
	} else {
		priv->gc.can_sleep = false;
		priv->gc.set = gpio_latch_set;
		spin_lock_init(&priv->spinlock);
	}

	of_property_read_u32(np, "setup-duration-ns", &priv->setup_duration_ns);
	if (priv->setup_duration_ns > DURATION_NS_MAX) {
		dev_warn(&pdev->dev, "setup-duration-ns too high, limit to %d\n",
			 DURATION_NS_MAX);
		priv->setup_duration_ns = DURATION_NS_MAX;
	}

	of_property_read_u32(np, "clock-duration-ns", &priv->clock_duration_ns);
	if (priv->clock_duration_ns > DURATION_NS_MAX) {
		dev_warn(&pdev->dev, "clock-duration-ns too high, limit to %d\n",
			 DURATION_NS_MAX);
		priv->clock_duration_ns = DURATION_NS_MAX;
	}

	priv->gc.get_direction = gpio_latch_get_direction;
	priv->gc.ngpio = n_latches * priv->n_latched_gpios;
	priv->gc.owner = THIS_MODULE;
	priv->gc.base = -1;
	priv->gc.parent = &pdev->dev;

	platform_set_drvdata(pdev, priv);

	return devm_gpiochip_add_data(&pdev->dev, &priv->gc, priv);
}

static const struct of_device_id gpio_latch_ids[] = {
	{
		.compatible = "gpio-latch",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, gpio_latch_ids);

static struct platform_driver gpio_latch_driver = {
	.driver	= {
		.name		= "gpio-latch",
		.of_match_table	= gpio_latch_ids,
	},
	.probe	= gpio_latch_probe,
};
module_platform_driver(gpio_latch_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION("GPIO latch driver");

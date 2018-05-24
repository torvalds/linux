/*
 * Driver for watchdog device controlled through GPIO-line
 *
 * Author: 2013, Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define SOFT_TIMEOUT_MIN	1
#define SOFT_TIMEOUT_DEF	60

enum {
	HW_ALGO_TOGGLE,
	HW_ALGO_LEVEL,
};

struct gpio_wdt_priv {
	struct gpio_desc	*gpiod;
	bool			state;
	bool			always_running;
	unsigned int		hw_algo;
	struct watchdog_device	wdd;
};

static void gpio_wdt_disable(struct gpio_wdt_priv *priv)
{
	/* Eternal ping */
	gpiod_set_value_cansleep(priv->gpiod, 1);

	/* Put GPIO back to tristate */
	if (priv->hw_algo == HW_ALGO_TOGGLE)
		gpiod_direction_input(priv->gpiod);
}

static int gpio_wdt_ping(struct watchdog_device *wdd)
{
	struct gpio_wdt_priv *priv = watchdog_get_drvdata(wdd);

	switch (priv->hw_algo) {
	case HW_ALGO_TOGGLE:
		/* Toggle output pin */
		priv->state = !priv->state;
		gpiod_set_value_cansleep(priv->gpiod, priv->state);
		break;
	case HW_ALGO_LEVEL:
		/* Pulse */
		gpiod_set_value_cansleep(priv->gpiod, 1);
		udelay(1);
		gpiod_set_value_cansleep(priv->gpiod, 0);
		break;
	}
	return 0;
}

static int gpio_wdt_start(struct watchdog_device *wdd)
{
	struct gpio_wdt_priv *priv = watchdog_get_drvdata(wdd);

	priv->state = 0;
	gpiod_direction_output(priv->gpiod, priv->state);

	set_bit(WDOG_HW_RUNNING, &wdd->status);

	return gpio_wdt_ping(wdd);
}

static int gpio_wdt_stop(struct watchdog_device *wdd)
{
	struct gpio_wdt_priv *priv = watchdog_get_drvdata(wdd);

	if (!priv->always_running) {
		gpio_wdt_disable(priv);
	} else {
		set_bit(WDOG_HW_RUNNING, &wdd->status);
	}

	return 0;
}

static const struct watchdog_info gpio_wdt_ident = {
	.options	= WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING |
			  WDIOF_SETTIMEOUT,
	.identity	= "GPIO Watchdog",
};

static const struct watchdog_ops gpio_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= gpio_wdt_start,
	.stop		= gpio_wdt_stop,
	.ping		= gpio_wdt_ping,
};

static int gpio_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct gpio_wdt_priv *priv;
	enum gpiod_flags gflags;
	unsigned int hw_margin;
	const char *algo;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	ret = of_property_read_string(np, "hw_algo", &algo);
	if (ret)
		return ret;
	if (!strcmp(algo, "toggle")) {
		priv->hw_algo = HW_ALGO_TOGGLE;
		gflags = GPIOD_IN;
	} else if (!strcmp(algo, "level")) {
		priv->hw_algo = HW_ALGO_LEVEL;
		gflags = GPIOD_OUT_LOW;
	} else {
		return -EINVAL;
	}

	priv->gpiod = devm_gpiod_get(dev, NULL, gflags);
	if (IS_ERR(priv->gpiod))
		return PTR_ERR(priv->gpiod);

	ret = of_property_read_u32(np,
				   "hw_margin_ms", &hw_margin);
	if (ret)
		return ret;
	/* Disallow values lower than 2 and higher than 65535 ms */
	if (hw_margin < 2 || hw_margin > 65535)
		return -EINVAL;

	priv->always_running = of_property_read_bool(np,
						     "always-running");

	watchdog_set_drvdata(&priv->wdd, priv);

	priv->wdd.info		= &gpio_wdt_ident;
	priv->wdd.ops		= &gpio_wdt_ops;
	priv->wdd.min_timeout	= SOFT_TIMEOUT_MIN;
	priv->wdd.max_hw_heartbeat_ms = hw_margin;
	priv->wdd.parent	= dev;
	priv->wdd.timeout	= SOFT_TIMEOUT_DEF;

	watchdog_init_timeout(&priv->wdd, 0, &pdev->dev);

	watchdog_stop_on_reboot(&priv->wdd);

	if (priv->always_running)
		gpio_wdt_start(&priv->wdd);

	ret = watchdog_register_device(&priv->wdd);

	return ret;
}

static int gpio_wdt_remove(struct platform_device *pdev)
{
	struct gpio_wdt_priv *priv = platform_get_drvdata(pdev);

	watchdog_unregister_device(&priv->wdd);

	return 0;
}

static const struct of_device_id gpio_wdt_dt_ids[] = {
	{ .compatible = "linux,wdt-gpio", },
	{ }
};
MODULE_DEVICE_TABLE(of, gpio_wdt_dt_ids);

static struct platform_driver gpio_wdt_driver = {
	.driver	= {
		.name		= "gpio-wdt",
		.of_match_table	= gpio_wdt_dt_ids,
	},
	.probe	= gpio_wdt_probe,
	.remove	= gpio_wdt_remove,
};

#ifdef CONFIG_GPIO_WATCHDOG_ARCH_INITCALL
static int __init gpio_wdt_init(void)
{
	return platform_driver_register(&gpio_wdt_driver);
}
arch_initcall(gpio_wdt_init);
#else
module_platform_driver(gpio_wdt_driver);
#endif

MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("GPIO Watchdog");
MODULE_LICENSE("GPL");

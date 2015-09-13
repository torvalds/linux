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
#include <linux/notifier.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/watchdog.h>

#define SOFT_TIMEOUT_MIN	1
#define SOFT_TIMEOUT_DEF	60
#define SOFT_TIMEOUT_MAX	0xffff

enum {
	HW_ALGO_TOGGLE,
	HW_ALGO_LEVEL,
};

struct gpio_wdt_priv {
	int			gpio;
	bool			active_low;
	bool			state;
	bool			always_running;
	bool			armed;
	unsigned int		hw_algo;
	unsigned int		hw_margin;
	unsigned long		last_jiffies;
	struct notifier_block	notifier;
	struct timer_list	timer;
	struct watchdog_device	wdd;
};

static void gpio_wdt_disable(struct gpio_wdt_priv *priv)
{
	gpio_set_value_cansleep(priv->gpio, !priv->active_low);

	/* Put GPIO back to tristate */
	if (priv->hw_algo == HW_ALGO_TOGGLE)
		gpio_direction_input(priv->gpio);
}

static void gpio_wdt_hwping(unsigned long data)
{
	struct watchdog_device *wdd = (struct watchdog_device *)data;
	struct gpio_wdt_priv *priv = watchdog_get_drvdata(wdd);

	if (priv->armed && time_after(jiffies, priv->last_jiffies +
				      msecs_to_jiffies(wdd->timeout * 1000))) {
		dev_crit(wdd->dev, "Timer expired. System will reboot soon!\n");
		return;
	}

	/* Restart timer */
	mod_timer(&priv->timer, jiffies + priv->hw_margin);

	switch (priv->hw_algo) {
	case HW_ALGO_TOGGLE:
		/* Toggle output pin */
		priv->state = !priv->state;
		gpio_set_value_cansleep(priv->gpio, priv->state);
		break;
	case HW_ALGO_LEVEL:
		/* Pulse */
		gpio_set_value_cansleep(priv->gpio, !priv->active_low);
		udelay(1);
		gpio_set_value_cansleep(priv->gpio, priv->active_low);
		break;
	}
}

static void gpio_wdt_start_impl(struct gpio_wdt_priv *priv)
{
	priv->state = priv->active_low;
	gpio_direction_output(priv->gpio, priv->state);
	priv->last_jiffies = jiffies;
	gpio_wdt_hwping((unsigned long)&priv->wdd);
}

static int gpio_wdt_start(struct watchdog_device *wdd)
{
	struct gpio_wdt_priv *priv = watchdog_get_drvdata(wdd);

	gpio_wdt_start_impl(priv);
	priv->armed = true;

	return 0;
}

static int gpio_wdt_stop(struct watchdog_device *wdd)
{
	struct gpio_wdt_priv *priv = watchdog_get_drvdata(wdd);

	priv->armed = false;
	if (!priv->always_running) {
		mod_timer(&priv->timer, 0);
		gpio_wdt_disable(priv);
	}

	return 0;
}

static int gpio_wdt_ping(struct watchdog_device *wdd)
{
	struct gpio_wdt_priv *priv = watchdog_get_drvdata(wdd);

	priv->last_jiffies = jiffies;

	return 0;
}

static int gpio_wdt_set_timeout(struct watchdog_device *wdd, unsigned int t)
{
	wdd->timeout = t;

	return gpio_wdt_ping(wdd);
}

static int gpio_wdt_notify_sys(struct notifier_block *nb, unsigned long code,
			       void *unused)
{
	struct gpio_wdt_priv *priv = container_of(nb, struct gpio_wdt_priv,
						  notifier);

	mod_timer(&priv->timer, 0);

	switch (code) {
	case SYS_HALT:
	case SYS_POWER_OFF:
		gpio_wdt_disable(priv);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
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
	.set_timeout	= gpio_wdt_set_timeout,
};

static int gpio_wdt_probe(struct platform_device *pdev)
{
	struct gpio_wdt_priv *priv;
	enum of_gpio_flags flags;
	unsigned int hw_margin;
	unsigned long f = 0;
	const char *algo;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->gpio = of_get_gpio_flags(pdev->dev.of_node, 0, &flags);
	if (!gpio_is_valid(priv->gpio))
		return priv->gpio;

	priv->active_low = flags & OF_GPIO_ACTIVE_LOW;

	ret = of_property_read_string(pdev->dev.of_node, "hw_algo", &algo);
	if (ret)
		return ret;
	if (!strcmp(algo, "toggle")) {
		priv->hw_algo = HW_ALGO_TOGGLE;
		f = GPIOF_IN;
	} else if (!strcmp(algo, "level")) {
		priv->hw_algo = HW_ALGO_LEVEL;
		f = priv->active_low ? GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW;
	} else {
		return -EINVAL;
	}

	ret = devm_gpio_request_one(&pdev->dev, priv->gpio, f,
				    dev_name(&pdev->dev));
	if (ret)
		return ret;

	ret = of_property_read_u32(pdev->dev.of_node,
				   "hw_margin_ms", &hw_margin);
	if (ret)
		return ret;
	/* Disallow values lower than 2 and higher than 65535 ms */
	if (hw_margin < 2 || hw_margin > 65535)
		return -EINVAL;

	/* Use safe value (1/2 of real timeout) */
	priv->hw_margin = msecs_to_jiffies(hw_margin / 2);

	priv->always_running = of_property_read_bool(pdev->dev.of_node,
						     "always-running");

	watchdog_set_drvdata(&priv->wdd, priv);

	priv->wdd.info		= &gpio_wdt_ident;
	priv->wdd.ops		= &gpio_wdt_ops;
	priv->wdd.min_timeout	= SOFT_TIMEOUT_MIN;
	priv->wdd.max_timeout	= SOFT_TIMEOUT_MAX;
	priv->wdd.parent	= &pdev->dev;

	if (watchdog_init_timeout(&priv->wdd, 0, &pdev->dev) < 0)
		priv->wdd.timeout = SOFT_TIMEOUT_DEF;

	setup_timer(&priv->timer, gpio_wdt_hwping, (unsigned long)&priv->wdd);

	ret = watchdog_register_device(&priv->wdd);
	if (ret)
		return ret;

	priv->notifier.notifier_call = gpio_wdt_notify_sys;
	ret = register_reboot_notifier(&priv->notifier);
	if (ret)
		goto error_unregister;

	if (priv->always_running)
		gpio_wdt_start_impl(priv);

	return 0;

error_unregister:
	watchdog_unregister_device(&priv->wdd);
	return ret;
}

static int gpio_wdt_remove(struct platform_device *pdev)
{
	struct gpio_wdt_priv *priv = platform_get_drvdata(pdev);

	del_timer_sync(&priv->timer);
	unregister_reboot_notifier(&priv->notifier);
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

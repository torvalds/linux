// SPDX-License-Identifier: GPL-2.0+
/*
 * Watchdog driver for the A21 VME CPU Boards
 *
 * Copyright (C) 2013 MEN Mikro Elektronik Nuernberg GmbH
 *
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/bitops.h>

#define NUM_GPIOS 6

enum a21_wdt_gpios {
	GPIO_WD_ENAB,
	GPIO_WD_FAST,
	GPIO_WD_TRIG,
	GPIO_WD_RST0,
	GPIO_WD_RST1,
	GPIO_WD_RST2,
};

struct a21_wdt_drv {
	struct watchdog_device wdt;
	unsigned gpios[NUM_GPIOS];
};

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			    __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static unsigned int a21_wdt_get_bootstatus(struct a21_wdt_drv *drv)
{
	int reset = 0;

	reset |= gpio_get_value(drv->gpios[GPIO_WD_RST0]) ? (1 << 0) : 0;
	reset |= gpio_get_value(drv->gpios[GPIO_WD_RST1]) ? (1 << 1) : 0;
	reset |= gpio_get_value(drv->gpios[GPIO_WD_RST2]) ? (1 << 2) : 0;

	return reset;
}

static int a21_wdt_start(struct watchdog_device *wdt)
{
	struct a21_wdt_drv *drv = watchdog_get_drvdata(wdt);

	gpio_set_value(drv->gpios[GPIO_WD_ENAB], 1);

	return 0;
}

static int a21_wdt_stop(struct watchdog_device *wdt)
{
	struct a21_wdt_drv *drv = watchdog_get_drvdata(wdt);

	gpio_set_value(drv->gpios[GPIO_WD_ENAB], 0);

	return 0;
}

static int a21_wdt_ping(struct watchdog_device *wdt)
{
	struct a21_wdt_drv *drv = watchdog_get_drvdata(wdt);

	gpio_set_value(drv->gpios[GPIO_WD_TRIG], 0);
	ndelay(10);
	gpio_set_value(drv->gpios[GPIO_WD_TRIG], 1);

	return 0;
}

static int a21_wdt_set_timeout(struct watchdog_device *wdt,
			       unsigned int timeout)
{
	struct a21_wdt_drv *drv = watchdog_get_drvdata(wdt);

	if (timeout != 1 && timeout != 30) {
		dev_err(wdt->parent, "Only 1 and 30 allowed as timeout\n");
		return -EINVAL;
	}

	if (timeout == 30 && wdt->timeout == 1) {
		dev_err(wdt->parent,
			"Transition from fast to slow mode not allowed\n");
		return -EINVAL;
	}

	if (timeout == 1)
		gpio_set_value(drv->gpios[GPIO_WD_FAST], 1);
	else
		gpio_set_value(drv->gpios[GPIO_WD_FAST], 0);

	wdt->timeout = timeout;

	return 0;
}

static const struct watchdog_info a21_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "MEN A21 Watchdog",
};

static const struct watchdog_ops a21_wdt_ops = {
	.owner = THIS_MODULE,
	.start = a21_wdt_start,
	.stop = a21_wdt_stop,
	.ping = a21_wdt_ping,
	.set_timeout = a21_wdt_set_timeout,
};

static struct watchdog_device a21_wdt = {
	.info = &a21_wdt_info,
	.ops = &a21_wdt_ops,
	.min_timeout = 1,
	.max_timeout = 30,
};

static int a21_wdt_probe(struct platform_device *pdev)
{
	struct device_node *node;
	struct a21_wdt_drv *drv;
	unsigned int reset = 0;
	int num_gpios;
	int ret;
	int i;

	drv = devm_kzalloc(&pdev->dev, sizeof(struct a21_wdt_drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	/* Fill GPIO pin array */
	node = pdev->dev.of_node;

	num_gpios = of_gpio_count(node);
	if (num_gpios != NUM_GPIOS) {
		dev_err(&pdev->dev, "gpios DT property wrong, got %d want %d",
			num_gpios, NUM_GPIOS);
		return -ENODEV;
	}

	for (i = 0; i < num_gpios; i++) {
		int val;

		val = of_get_gpio(node, i);
		if (val < 0)
			return val;

		drv->gpios[i] = val;
	}

	/* Request the used GPIOs */
	for (i = 0; i < num_gpios; i++) {
		ret = devm_gpio_request(&pdev->dev, drv->gpios[i],
					"MEN A21 Watchdog");
		if (ret)
			return ret;

		if (i < GPIO_WD_RST0)
			ret = gpio_direction_output(drv->gpios[i],
						gpio_get_value(drv->gpios[i]));
		else		/* GPIO_WD_RST[0..2] are inputs */
			ret = gpio_direction_input(drv->gpios[i]);
		if (ret)
			return ret;
	}

	watchdog_init_timeout(&a21_wdt, 30, &pdev->dev);
	watchdog_set_nowayout(&a21_wdt, nowayout);
	watchdog_set_drvdata(&a21_wdt, drv);
	a21_wdt.parent = &pdev->dev;

	reset = a21_wdt_get_bootstatus(drv);
	if (reset == 2)
		a21_wdt.bootstatus |= WDIOF_EXTERN1;
	else if (reset == 4)
		a21_wdt.bootstatus |= WDIOF_CARDRESET;
	else if (reset == 5)
		a21_wdt.bootstatus |= WDIOF_POWERUNDER;
	else if (reset == 7)
		a21_wdt.bootstatus |= WDIOF_EXTERN2;

	drv->wdt = a21_wdt;
	dev_set_drvdata(&pdev->dev, drv);

	ret = devm_watchdog_register_device(&pdev->dev, &a21_wdt);
	if (ret) {
		dev_err(&pdev->dev, "Cannot register watchdog device\n");
		return ret;
	}

	dev_info(&pdev->dev, "MEN A21 watchdog timer driver enabled\n");

	return 0;
}

static void a21_wdt_shutdown(struct platform_device *pdev)
{
	struct a21_wdt_drv *drv = dev_get_drvdata(&pdev->dev);

	gpio_set_value(drv->gpios[GPIO_WD_ENAB], 0);
}

static const struct of_device_id a21_wdt_ids[] = {
	{ .compatible = "men,a021-wdt" },
	{ },
};
MODULE_DEVICE_TABLE(of, a21_wdt_ids);

static struct platform_driver a21_wdt_driver = {
	.probe = a21_wdt_probe,
	.shutdown = a21_wdt_shutdown,
	.driver = {
		.name = "a21-watchdog",
		.of_match_table = a21_wdt_ids,
	},
};

module_platform_driver(a21_wdt_driver);

MODULE_AUTHOR("MEN Mikro Elektronik");
MODULE_DESCRIPTION("MEN A21 Watchdog");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:a21-watchdog");

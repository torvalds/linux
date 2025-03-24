// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Servergy CTS-1000 Setup
 *
 * Maintained by Ben Collins <ben.c@servergy.com>
 *
 * Copyright 2012 by Servergy, Inc.
 */

#define pr_fmt(fmt) "gpio-halt: " fmt

#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/workqueue.h>
#include <linux/reboot.h>
#include <linux/interrupt.h>

#include <asm/machdep.h>

static struct gpio_desc *halt_gpio;
static int halt_irq;

static const struct of_device_id child_match[] = {
	{
		.compatible = "sgy,gpio-halt",
	},
	{},
};

static void gpio_halt_wfn(struct work_struct *work)
{
	/* Likely wont return */
	orderly_poweroff(true);
}
static DECLARE_WORK(gpio_halt_wq, gpio_halt_wfn);

static void __noreturn gpio_halt_cb(void)
{
	pr_info("triggering GPIO.\n");

	/* Probably wont return */
	gpiod_set_value(halt_gpio, 1);

	panic("Halt failed\n");
}

/* This IRQ means someone pressed the power button and it is waiting for us
 * to handle the shutdown/poweroff. */
static irqreturn_t gpio_halt_irq(int irq, void *__data)
{
	struct platform_device *pdev = __data;

	dev_info(&pdev->dev, "scheduling shutdown due to power button IRQ\n");
	schedule_work(&gpio_halt_wq);

        return IRQ_HANDLED;
};

static int __gpio_halt_probe(struct platform_device *pdev,
			     struct device_node *halt_node)
{
	int err;

	halt_gpio = fwnode_gpiod_get_index(of_fwnode_handle(halt_node),
					   NULL, 0, GPIOD_OUT_LOW, "gpio-halt");
	err = PTR_ERR_OR_ZERO(halt_gpio);
	if (err) {
		dev_err(&pdev->dev, "failed to request halt GPIO: %d\n", err);
		return err;
	}

	/* Now get the IRQ which tells us when the power button is hit */
	halt_irq = irq_of_parse_and_map(halt_node, 0);
	err = request_irq(halt_irq, gpio_halt_irq,
			  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			  "gpio-halt", pdev);
	if (err) {
		dev_err(&pdev->dev, "failed to request IRQ %d: %d\n",
			halt_irq, err);
		gpiod_put(halt_gpio);
		halt_gpio = NULL;
		return err;
	}

	/* Register our halt function */
	ppc_md.halt = gpio_halt_cb;
	pm_power_off = gpio_halt_cb;

	dev_info(&pdev->dev, "registered halt GPIO, irq: %d\n", halt_irq);

	return 0;
}

static int gpio_halt_probe(struct platform_device *pdev)
{
	struct device_node *halt_node;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	/* If there's no matching child, this isn't really an error */
	halt_node = of_find_matching_node(pdev->dev.of_node, child_match);
	if (!halt_node)
		return -ENODEV;

	ret = __gpio_halt_probe(pdev, halt_node);
	of_node_put(halt_node);

	return ret;
}

static void gpio_halt_remove(struct platform_device *pdev)
{
	free_irq(halt_irq, pdev);
	cancel_work_sync(&gpio_halt_wq);

	ppc_md.halt = NULL;
	pm_power_off = NULL;

	gpiod_put(halt_gpio);
	halt_gpio = NULL;
}

static const struct of_device_id gpio_halt_match[] = {
	/* We match on the gpio bus itself and scan the children since they
	 * wont be matched against us. We know the bus wont match until it
	 * has been registered too. */
	{
		.compatible = "fsl,qoriq-gpio",
	},
	{},
};
MODULE_DEVICE_TABLE(of, gpio_halt_match);

static struct platform_driver gpio_halt_driver = {
	.driver = {
		.name		= "gpio-halt",
		.of_match_table = gpio_halt_match,
	},
	.probe		= gpio_halt_probe,
	.remove		= gpio_halt_remove,
};

module_platform_driver(gpio_halt_driver);

MODULE_DESCRIPTION("Driver to support GPIO triggered system halt for Servergy CTS-1000 Systems.");
MODULE_VERSION("1.0");
MODULE_AUTHOR("Ben Collins <ben.c@servergy.com>");
MODULE_LICENSE("GPL");

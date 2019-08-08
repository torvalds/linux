// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Servergy CTS-1000 Setup
 *
 * Maintained by Ben Collins <ben.c@servergy.com>
 *
 * Copyright 2012 by Servergy, Inc.
 */

#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/workqueue.h>
#include <linux/reboot.h>
#include <linux/interrupt.h>

#include <asm/machdep.h>

static struct device_node *halt_node;

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
	enum of_gpio_flags flags;
	int trigger, gpio;

	if (!halt_node)
		panic("No reset GPIO information was provided in DT\n");

	gpio = of_get_gpio_flags(halt_node, 0, &flags);

	if (!gpio_is_valid(gpio))
		panic("Provided GPIO is invalid\n");

	trigger = (flags == OF_GPIO_ACTIVE_LOW);

	printk(KERN_INFO "gpio-halt: triggering GPIO.\n");

	/* Probably wont return */
	gpio_set_value(gpio, trigger);

	panic("Halt failed\n");
}

/* This IRQ means someone pressed the power button and it is waiting for us
 * to handle the shutdown/poweroff. */
static irqreturn_t gpio_halt_irq(int irq, void *__data)
{
	printk(KERN_INFO "gpio-halt: shutdown due to power button IRQ.\n");
	schedule_work(&gpio_halt_wq);

        return IRQ_HANDLED;
};

static int gpio_halt_probe(struct platform_device *pdev)
{
	enum of_gpio_flags flags;
	struct device_node *node = pdev->dev.of_node;
	int gpio, err, irq;
	int trigger;

	if (!node)
		return -ENODEV;

	/* If there's no matching child, this isn't really an error */
	halt_node = of_find_matching_node(node, child_match);
	if (!halt_node)
		return 0;

	/* Technically we could just read the first one, but punish
	 * DT writers for invalid form. */
	if (of_gpio_count(halt_node) != 1)
		return -EINVAL;

	/* Get the gpio number relative to the dynamic base. */
	gpio = of_get_gpio_flags(halt_node, 0, &flags);
	if (!gpio_is_valid(gpio))
		return -EINVAL;

	err = gpio_request(gpio, "gpio-halt");
	if (err) {
		printk(KERN_ERR "gpio-halt: error requesting GPIO %d.\n",
		       gpio);
		halt_node = NULL;
		return err;
	}

	trigger = (flags == OF_GPIO_ACTIVE_LOW);

	gpio_direction_output(gpio, !trigger);

	/* Now get the IRQ which tells us when the power button is hit */
	irq = irq_of_parse_and_map(halt_node, 0);
	err = request_irq(irq, gpio_halt_irq, IRQF_TRIGGER_RISING |
			  IRQF_TRIGGER_FALLING, "gpio-halt", halt_node);
	if (err) {
		printk(KERN_ERR "gpio-halt: error requesting IRQ %d for "
		       "GPIO %d.\n", irq, gpio);
		gpio_free(gpio);
		halt_node = NULL;
		return err;
	}

	/* Register our halt function */
	ppc_md.halt = gpio_halt_cb;
	pm_power_off = gpio_halt_cb;

	printk(KERN_INFO "gpio-halt: registered GPIO %d (%d trigger, %d"
	       " irq).\n", gpio, trigger, irq);

	return 0;
}

static int gpio_halt_remove(struct platform_device *pdev)
{
	if (halt_node) {
		int gpio = of_get_gpio(halt_node, 0);
		int irq = irq_of_parse_and_map(halt_node, 0);

		free_irq(irq, halt_node);

		ppc_md.halt = NULL;
		pm_power_off = NULL;

		gpio_free(gpio);

		halt_node = NULL;
	}

	return 0;
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

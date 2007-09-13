/*
 * Driver for keys on GPIO lines capable of generating interrupts.
 *
 * Copyright 2005 Phil Blundell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/version.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>

#include <asm/gpio.h>

static irqreturn_t gpio_keys_isr(int irq, void *dev_id)
{
	int i;
	struct platform_device *pdev = dev_id;
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	struct input_dev *input = platform_get_drvdata(pdev);

	for (i = 0; i < pdata->nbuttons; i++) {
		struct gpio_keys_button *button = &pdata->buttons[i];
		int gpio = button->gpio;

		if (irq == gpio_to_irq(gpio)) {
			unsigned int type = button->type ?: EV_KEY;
			int state = (gpio_get_value(gpio) ? 1 : 0) ^ button->active_low;

			input_event(input, type, button->code, !!state);
			input_sync(input);
		}
	}

	return IRQ_HANDLED;
}

static int __devinit gpio_keys_probe(struct platform_device *pdev)
{
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	struct input_dev *input;
	int i, error;

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	platform_set_drvdata(pdev, input);

	input->evbit[0] = BIT(EV_KEY);

	input->name = pdev->name;
	input->phys = "gpio-keys/input0";
	input->dev.parent = &pdev->dev;

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	for (i = 0; i < pdata->nbuttons; i++) {
		struct gpio_keys_button *button = &pdata->buttons[i];
		int irq = gpio_to_irq(button->gpio);
		unsigned int type = button->type ?: EV_KEY;

		set_irq_type(irq, IRQ_TYPE_EDGE_BOTH);
		error = request_irq(irq, gpio_keys_isr, IRQF_SAMPLE_RANDOM,
				     button->desc ? button->desc : "gpio_keys",
				     pdev);
		if (error) {
			printk(KERN_ERR "gpio-keys: unable to claim irq %d; error %d\n",
				irq, error);
			goto fail;
		}

		input_set_capability(input, type, button->code);
	}

	error = input_register_device(input);
	if (error) {
		printk(KERN_ERR "Unable to register gpio-keys input device\n");
		goto fail;
	}

	return 0;

 fail:
	for (i = i - 1; i >= 0; i--)
		free_irq(gpio_to_irq(pdata->buttons[i].gpio), pdev);

	input_free_device(input);

	return error;
}

static int __devexit gpio_keys_remove(struct platform_device *pdev)
{
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	struct input_dev *input = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < pdata->nbuttons; i++) {
		int irq = gpio_to_irq(pdata->buttons[i].gpio);
		free_irq(irq, pdev);
	}

	input_unregister_device(input);

	return 0;
}

struct platform_driver gpio_keys_device_driver = {
	.probe		= gpio_keys_probe,
	.remove		= __devexit_p(gpio_keys_remove),
	.driver		= {
		.name	= "gpio-keys",
	}
};

static int __init gpio_keys_init(void)
{
	return platform_driver_register(&gpio_keys_device_driver);
}

static void __exit gpio_keys_exit(void)
{
	platform_driver_unregister(&gpio_keys_device_driver);
}

module_init(gpio_keys_init);
module_exit(gpio_keys_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Phil Blundell <pb@handhelds.org>");
MODULE_DESCRIPTION("Keyboard driver for CPU GPIOs");

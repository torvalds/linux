/*
 *  drivers/extcon/extcon_gpio.c
 *
 *  Single-state GPIO extcon driver based on extcon class
 *
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 *
 * Modified by MyungJoo Ham <myungjoo.ham@samsung.com> to support extcon
 * (originally switch class is supported)
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/extcon.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/extcon.h>
#include <linux/extcon/extcon_gpio.h>

struct gpio_extcon_data {
	struct extcon_dev edev;
	unsigned gpio;
	const char *state_on;
	const char *state_off;
	int irq;
	struct delayed_work work;
	unsigned long debounce_jiffies;
};

static void gpio_extcon_work(struct work_struct *work)
{
	int state;
	struct gpio_extcon_data	*data =
		container_of(to_delayed_work(work), struct gpio_extcon_data,
			     work);

	state = gpio_get_value(data->gpio);
	extcon_set_state(&data->edev, state);
}

static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
	struct gpio_extcon_data *extcon_data = dev_id;

	schedule_delayed_work(&extcon_data->work,
			      extcon_data->debounce_jiffies);
	return IRQ_HANDLED;
}

static ssize_t extcon_gpio_print_state(struct extcon_dev *edev, char *buf)
{
	struct gpio_extcon_data	*extcon_data =
		container_of(edev, struct gpio_extcon_data, edev);
	const char *state;
	if (extcon_get_state(edev))
		state = extcon_data->state_on;
	else
		state = extcon_data->state_off;

	if (state)
		return sprintf(buf, "%s\n", state);
	return -EINVAL;
}

static int __devinit gpio_extcon_probe(struct platform_device *pdev)
{
	struct gpio_extcon_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_extcon_data *extcon_data;
	int ret = 0;

	if (!pdata)
		return -EBUSY;
	if (!pdata->irq_flags) {
		dev_err(&pdev->dev, "IRQ flag is not specified.\n");
		return -EINVAL;
	}

	extcon_data = devm_kzalloc(&pdev->dev, sizeof(struct gpio_extcon_data),
				   GFP_KERNEL);
	if (!extcon_data)
		return -ENOMEM;

	extcon_data->edev.name = pdata->name;
	extcon_data->gpio = pdata->gpio;
	extcon_data->state_on = pdata->state_on;
	extcon_data->state_off = pdata->state_off;
	if (pdata->state_on && pdata->state_off)
		extcon_data->edev.print_state = extcon_gpio_print_state;
	extcon_data->debounce_jiffies = msecs_to_jiffies(pdata->debounce);

	ret = extcon_dev_register(&extcon_data->edev, &pdev->dev);
	if (ret < 0)
		return ret;

	ret = devm_gpio_request_one(&pdev->dev, extcon_data->gpio, GPIOF_DIR_IN,
				    pdev->name);
	if (ret < 0)
		goto err;

	INIT_DELAYED_WORK(&extcon_data->work, gpio_extcon_work);

	extcon_data->irq = gpio_to_irq(extcon_data->gpio);
	if (extcon_data->irq < 0) {
		ret = extcon_data->irq;
		goto err;
	}

	ret = request_any_context_irq(extcon_data->irq, gpio_irq_handler,
				      pdata->irq_flags, pdev->name,
				      extcon_data);
	if (ret < 0)
		goto err;

	platform_set_drvdata(pdev, extcon_data);
	/* Perform initial detection */
	gpio_extcon_work(&extcon_data->work.work);

	return 0;

err:
	extcon_dev_unregister(&extcon_data->edev);

	return ret;
}

static int __devexit gpio_extcon_remove(struct platform_device *pdev)
{
	struct gpio_extcon_data *extcon_data = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&extcon_data->work);
	free_irq(extcon_data->irq, extcon_data);
	extcon_dev_unregister(&extcon_data->edev);

	return 0;
}

static struct platform_driver gpio_extcon_driver = {
	.probe		= gpio_extcon_probe,
	.remove		= __devexit_p(gpio_extcon_remove),
	.driver		= {
		.name	= "extcon-gpio",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(gpio_extcon_driver);

MODULE_AUTHOR("Mike Lockwood <lockwood@android.com>");
MODULE_DESCRIPTION("GPIO extcon driver");
MODULE_LICENSE("GPL");

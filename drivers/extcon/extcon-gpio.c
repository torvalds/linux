/*
 * extcon_gpio.c - Single-state GPIO extcon driver based on extcon class
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
 */

#include <linux/extcon-provider.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

/**
 * struct gpio_extcon_data - A simple GPIO-controlled extcon device state container.
 * @edev:		Extcon device.
 * @irq:		Interrupt line for the external connector.
 * @work:		Work fired by the interrupt.
 * @debounce_jiffies:	Number of jiffies to wait for the GPIO to stabilize, from the debounce
 *			value.
 * @id_gpiod:		GPIO descriptor for this external connector.
 * @extcon_id:		The unique id of specific external connector.
 * @gpio:		Corresponding GPIO.
 * @gpio_active_low:	Boolean describing whether gpio active state is 1 or 0
 *			If true, low state of gpio means active.
 *			If false, high state of gpio means active.
 * @debounce:		Debounce time for GPIO IRQ in ms.
 * @irq_flags:		IRQ Flags (e.g., IRQF_TRIGGER_LOW).
 * @check_on_resume:	Boolean describing whether to check the state of gpio
 *			while resuming from sleep.
 */
struct gpio_extcon_data {
	struct extcon_dev *edev;
	int irq;
	struct delayed_work work;
	unsigned long debounce_jiffies;
	struct gpio_desc *id_gpiod;
	unsigned int extcon_id;
	unsigned gpio;
	bool gpio_active_low;
	unsigned long debounce;
	unsigned long irq_flags;
	bool check_on_resume;
};

static void gpio_extcon_work(struct work_struct *work)
{
	int state;
	struct gpio_extcon_data	*data =
		container_of(to_delayed_work(work), struct gpio_extcon_data,
			     work);

	state = gpiod_get_value_cansleep(data->id_gpiod);
	if (data->gpio_active_low)
		state = !state;

	extcon_set_state_sync(data->edev, data->extcon_id, state);
}

static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
	struct gpio_extcon_data *data = dev_id;

	queue_delayed_work(system_power_efficient_wq, &data->work,
			      data->debounce_jiffies);
	return IRQ_HANDLED;
}

static int gpio_extcon_init(struct device *dev, struct gpio_extcon_data *data)
{
	int ret;

	ret = devm_gpio_request_one(dev, data->gpio, GPIOF_DIR_IN,
				dev_name(dev));
	if (ret < 0)
		return ret;

	data->id_gpiod = gpio_to_desc(data->gpio);
	if (!data->id_gpiod)
		return -EINVAL;

	if (data->debounce) {
		ret = gpiod_set_debounce(data->id_gpiod,
					 data->debounce * 1000);
		if (ret < 0)
			data->debounce_jiffies =
				msecs_to_jiffies(data->debounce);
	}

	data->irq = gpiod_to_irq(data->id_gpiod);
	if (data->irq < 0)
		return data->irq;

	return 0;
}

static int gpio_extcon_probe(struct platform_device *pdev)
{
	struct gpio_extcon_data *data;
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(struct gpio_extcon_data),
				   GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/*
	 * FIXME: extcon_id represents the unique identifier of external
	 * connectors such as EXTCON_USB, EXTCON_DISP_HDMI and so on. extcon_id
	 * is necessary to register the extcon device. But, it's not yet
	 * developed to get the extcon id from device-tree or others.
	 * On later, it have to be solved.
	 */
	if (!data->irq_flags || data->extcon_id > EXTCON_NONE)
		return -EINVAL;

	/* Initialize the gpio */
	ret = gpio_extcon_init(&pdev->dev, data);
	if (ret < 0)
		return ret;

	/* Allocate the memory of extcon devie and register extcon device */
	data->edev = devm_extcon_dev_allocate(&pdev->dev, &data->extcon_id);
	if (IS_ERR(data->edev)) {
		dev_err(&pdev->dev, "failed to allocate extcon device\n");
		return -ENOMEM;
	}

	ret = devm_extcon_dev_register(&pdev->dev, data->edev);
	if (ret < 0)
		return ret;

	INIT_DELAYED_WORK(&data->work, gpio_extcon_work);

	/*
	 * Request the interrupt of gpio to detect whether external connector
	 * is attached or detached.
	 */
	ret = devm_request_any_context_irq(&pdev->dev, data->irq,
					gpio_irq_handler, data->irq_flags,
					pdev->name, data);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, data);
	/* Perform initial detection */
	gpio_extcon_work(&data->work.work);

	return 0;
}

static int gpio_extcon_remove(struct platform_device *pdev)
{
	struct gpio_extcon_data *data = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&data->work);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int gpio_extcon_resume(struct device *dev)
{
	struct gpio_extcon_data *data;

	data = dev_get_drvdata(dev);
	if (data->check_on_resume)
		queue_delayed_work(system_power_efficient_wq,
			&data->work, data->debounce_jiffies);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(gpio_extcon_pm_ops, NULL, gpio_extcon_resume);

static struct platform_driver gpio_extcon_driver = {
	.probe		= gpio_extcon_probe,
	.remove		= gpio_extcon_remove,
	.driver		= {
		.name	= "extcon-gpio",
		.pm	= &gpio_extcon_pm_ops,
	},
};

module_platform_driver(gpio_extcon_driver);

MODULE_AUTHOR("Mike Lockwood <lockwood@android.com>");
MODULE_DESCRIPTION("GPIO extcon driver");
MODULE_LICENSE("GPL");

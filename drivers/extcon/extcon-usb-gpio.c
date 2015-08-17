/**
 * drivers/extcon/extcon-usb-gpio.c - USB GPIO extcon driver
 *
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com
 * Author: Roger Quadros <rogerq@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/extcon.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#define USB_GPIO_DEBOUNCE_MS	20	/* ms */

struct usb_extcon_info {
	struct device *dev;
	struct extcon_dev *edev;

	struct gpio_desc *id_gpiod;
	int id_irq;

	unsigned long debounce_jiffies;
	struct delayed_work wq_detcable;
};

static const unsigned int usb_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static void usb_extcon_detect_cable(struct work_struct *work)
{
	int id;
	struct usb_extcon_info *info = container_of(to_delayed_work(work),
						    struct usb_extcon_info,
						    wq_detcable);

	/* check ID and update cable state */
	id = gpiod_get_value_cansleep(info->id_gpiod);
	if (id) {
		/*
		 * ID = 1 means USB HOST cable detached.
		 * As we don't have event for USB peripheral cable attached,
		 * we simulate USB peripheral attach here.
		 */
		extcon_set_cable_state_(info->edev, EXTCON_USB_HOST, false);
		extcon_set_cable_state_(info->edev, EXTCON_USB, true);
	} else {
		/*
		 * ID = 0 means USB HOST cable attached.
		 * As we don't have event for USB peripheral cable detached,
		 * we simulate USB peripheral detach here.
		 */
		extcon_set_cable_state_(info->edev, EXTCON_USB, false);
		extcon_set_cable_state_(info->edev, EXTCON_USB_HOST, true);
	}
}

static irqreturn_t usb_irq_handler(int irq, void *dev_id)
{
	struct usb_extcon_info *info = dev_id;

	queue_delayed_work(system_power_efficient_wq, &info->wq_detcable,
			   info->debounce_jiffies);

	return IRQ_HANDLED;
}

static int usb_extcon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct usb_extcon_info *info;
	int ret;

	if (!np)
		return -EINVAL;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;
	info->id_gpiod = devm_gpiod_get(&pdev->dev, "id", GPIOD_IN);
	if (IS_ERR(info->id_gpiod)) {
		dev_err(dev, "failed to get ID GPIO\n");
		return PTR_ERR(info->id_gpiod);
	}

	info->edev = devm_extcon_dev_allocate(dev, usb_extcon_cable);
	if (IS_ERR(info->edev)) {
		dev_err(dev, "failed to allocate extcon device\n");
		return -ENOMEM;
	}

	ret = devm_extcon_dev_register(dev, info->edev);
	if (ret < 0) {
		dev_err(dev, "failed to register extcon device\n");
		return ret;
	}

	ret = gpiod_set_debounce(info->id_gpiod,
				 USB_GPIO_DEBOUNCE_MS * 1000);
	if (ret < 0)
		info->debounce_jiffies = msecs_to_jiffies(USB_GPIO_DEBOUNCE_MS);

	INIT_DELAYED_WORK(&info->wq_detcable, usb_extcon_detect_cable);

	info->id_irq = gpiod_to_irq(info->id_gpiod);
	if (info->id_irq < 0) {
		dev_err(dev, "failed to get ID IRQ\n");
		return info->id_irq;
	}

	ret = devm_request_threaded_irq(dev, info->id_irq, NULL,
					usb_irq_handler,
					IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					pdev->name, info);
	if (ret < 0) {
		dev_err(dev, "failed to request handler for ID IRQ\n");
		return ret;
	}

	platform_set_drvdata(pdev, info);
	device_init_wakeup(dev, 1);

	/* Perform initial detection */
	usb_extcon_detect_cable(&info->wq_detcable.work);

	return 0;
}

static int usb_extcon_remove(struct platform_device *pdev)
{
	struct usb_extcon_info *info = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&info->wq_detcable);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int usb_extcon_suspend(struct device *dev)
{
	struct usb_extcon_info *info = dev_get_drvdata(dev);
	int ret = 0;

	if (device_may_wakeup(dev)) {
		ret = enable_irq_wake(info->id_irq);
		if (ret)
			return ret;
	}

	/*
	 * We don't want to process any IRQs after this point
	 * as GPIOs used behind I2C subsystem might not be
	 * accessible until resume completes. So disable IRQ.
	 */
	disable_irq(info->id_irq);

	return ret;
}

static int usb_extcon_resume(struct device *dev)
{
	struct usb_extcon_info *info = dev_get_drvdata(dev);
	int ret = 0;

	if (device_may_wakeup(dev)) {
		ret = disable_irq_wake(info->id_irq);
		if (ret)
			return ret;
	}

	enable_irq(info->id_irq);

	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(usb_extcon_pm_ops,
			 usb_extcon_suspend, usb_extcon_resume);

static const struct of_device_id usb_extcon_dt_match[] = {
	{ .compatible = "linux,extcon-usb-gpio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, usb_extcon_dt_match);

static struct platform_driver usb_extcon_driver = {
	.probe		= usb_extcon_probe,
	.remove		= usb_extcon_remove,
	.driver		= {
		.name	= "extcon-usb-gpio",
		.pm	= &usb_extcon_pm_ops,
		.of_match_table = usb_extcon_dt_match,
	},
};

module_platform_driver(usb_extcon_driver);

MODULE_AUTHOR("Roger Quadros <rogerq@ti.com>");
MODULE_DESCRIPTION("USB GPIO extcon driver");
MODULE_LICENSE("GPL v2");

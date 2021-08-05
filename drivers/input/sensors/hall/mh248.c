// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Bin Yang <yangbin@rock-chips.com>
 */


#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/of_gpio.h>
#include <linux/sensor-dev.h>
#include <linux/fb.h>
#include <linux/notifier.h>
#include <linux/rk_keys.h>
#include <linux/input.h>

struct mh248_para {
	struct device *dev;
	struct notifier_block fb_notif;
	struct mutex ops_lock;
	struct input_dev *hall_input;
	int is_suspend;
	int gpio_pin;
	int irq;
	int active_value;
};

static int hall_fb_notifier_callback(struct notifier_block *self,
				     unsigned long action, void *data)
{
	struct mh248_para *mh248;
	struct fb_event *event = data;

	mh248 = container_of(self, struct mh248_para, fb_notif);

	if (action != FB_EVENT_BLANK)
		return NOTIFY_DONE;

	mutex_lock(&mh248->ops_lock);
	switch (*((int *)event->data)) {
	case FB_BLANK_UNBLANK:
		mh248->is_suspend = 0;
		break;
	default:
		mh248->is_suspend = 1;
		break;
	}
	mutex_unlock(&mh248->ops_lock);

	return NOTIFY_OK;
}

static irqreturn_t hall_mh248_interrupt(int irq, void *dev_id)
{
	struct mh248_para *mh248 = (struct mh248_para *)dev_id;
	int gpio_value = 0;

	gpio_value = gpio_get_value(mh248->gpio_pin);
	if ((gpio_value != mh248->active_value) &&
	    (mh248->is_suspend == 0)) {
		input_report_key(mh248->hall_input, KEY_POWER, 1);
		input_sync(mh248->hall_input);
		input_report_key(mh248->hall_input, KEY_POWER, 0);
		input_sync(mh248->hall_input);
	} else if ((gpio_value == mh248->active_value) &&
		   (mh248->is_suspend == 1)) {
		input_report_key(mh248->hall_input, KEY_WAKEUP, 1);
		input_sync(mh248->hall_input);
		input_report_key(mh248->hall_input, KEY_WAKEUP, 0);
		input_sync(mh248->hall_input);
	}

	return IRQ_HANDLED;
}

static int hall_mh248_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mh248_para *mh248;
	enum of_gpio_flags irq_flags;
	int hallactive = 0;
	int ret = 0;

	mh248 = devm_kzalloc(&pdev->dev, sizeof(*mh248), GFP_KERNEL);
	if (!mh248)
		return -ENOMEM;

	mh248->dev = &pdev->dev;

	mh248->gpio_pin = of_get_named_gpio_flags(np, "irq-gpio",
						  0, &irq_flags);
	if (!gpio_is_valid(mh248->gpio_pin)) {
		dev_err(mh248->dev, "Can not read property irq-gpio\n");
		return mh248->gpio_pin;
	}
	mh248->irq = gpio_to_irq(mh248->gpio_pin);

	of_property_read_u32(np, "hall-active", &hallactive);
	mh248->active_value = hallactive;
	mh248->is_suspend = 0;
	mutex_init(&mh248->ops_lock);

	ret = devm_gpio_request_one(mh248->dev, mh248->gpio_pin,
				    GPIOF_DIR_IN, "hall_mh248");
	if (ret < 0) {
		dev_err(mh248->dev, "fail to request gpio:%d\n", mh248->gpio_pin);
		return ret;
	}

	ret = devm_request_threaded_irq(mh248->dev, mh248->irq,
					NULL, hall_mh248_interrupt,
					irq_flags | IRQF_NO_SUSPEND | IRQF_ONESHOT,
					"hall_mh248", mh248);
	if (ret < 0) {
		dev_err(mh248->dev, "request irq(%d) failed, ret=%d\n",
			mh248->irq, ret);
		return ret;
	}

	mh248->hall_input = devm_input_allocate_device(&pdev->dev);
	if (!mh248->hall_input) {
		dev_err(&pdev->dev, "Can't allocate hall input dev\n");
		return -ENOMEM;
	}
	mh248->hall_input->name = "hall wake key";
	input_set_capability(mh248->hall_input, EV_KEY, KEY_POWER);
	input_set_capability(mh248->hall_input, EV_KEY, KEY_WAKEUP);

	ret = input_register_device(mh248->hall_input);
	if (ret) {
		dev_err(&pdev->dev, "Unable to register input device, error: %d\n", ret);
		return ret;
	}

	enable_irq_wake(mh248->irq);
	mh248->fb_notif.notifier_call = hall_fb_notifier_callback;
	fb_register_client(&mh248->fb_notif);
	dev_info(mh248->dev, "hall_mh248_probe success.\n");

	return 0;
}

static const struct of_device_id hall_mh248_match[] = {
	{ .compatible = "hall-mh248" },
	{ /* Sentinel */ }
};

static struct platform_driver hall_mh248_driver = {
	.probe = hall_mh248_probe,
	.driver = {
		.name = "mh248",
		.owner = THIS_MODULE,
		.of_match_table	= hall_mh248_match,
	},
};

module_platform_driver(hall_mh248_driver);

MODULE_ALIAS("platform:mh248");
MODULE_AUTHOR("Bin Yang <yangbin@rock-chips.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Hall Sensor MH248 driver");

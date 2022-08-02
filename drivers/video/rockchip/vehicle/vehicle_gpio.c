// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/video/rockchip/video/vehicle_gpio.c
 *
 * Copyright (C) 2020 Rockchip Electronics Co.Ltd
 * Authors:
 *	Jianwei Fan <jianwei.fan@rock-chips.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/completion.h>
#include <linux/wakelock.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>

#include "vehicle_gpio.h"
#include "vehicle_main.h"

static void gpio_det_work_func(struct work_struct *work)
{
	struct gpio_detect *gpiod = container_of(work, struct gpio_detect,
			work.work);
	int val = gpio_get_value(gpiod->gpio);

	VEHICLE_DG("%s: gpiod->old val(%d), new val(%d)\n",
			__func__, gpiod->val, val);

	if (gpiod->val != val) {
		gpiod->val = val;
		vehicle_gpio_stat_change_notify();
	}
}

static irqreturn_t gpio_det_interrupt(int irq, void *dev_id)
{
	struct gpio_detect *gpiod = dev_id;
	int val = gpio_get_value(gpiod->gpio);
	unsigned int irqflags = IRQF_ONESHOT;

	if (val)
		irqflags |= IRQ_TYPE_EDGE_FALLING;
	else
		irqflags |= IRQ_TYPE_EDGE_RISING;
	irq_set_irq_type(gpiod->irq, irqflags);

	mod_delayed_work(system_wq, &gpiod->work,
			 msecs_to_jiffies(gpiod->debounce_ms));

	return IRQ_HANDLED;
}

static int vehicle_gpio_init_check(struct gpio_detect *gpiod)
{
	gpiod->val = gpio_get_value(gpiod->gpio);

	dev_info(gpiod->dev, "%s: gpiod->atv_val(%d), gpiod->val(%d)\n",
			__func__, gpiod->atv_val, gpiod->val);

	if (gpiod->atv_val == gpiod->val) {
		vehicle_gpio_stat_change_notify();
		return 1;
	} else {
		return 0;
	}
}

bool vehicle_gpio_reverse_check(struct gpio_detect *gpiod)
{
	int val = gpiod->val ^ gpiod->atv_val;

	if (gpiod->num == 0)
		return true;
	else
		return (val == 0) ? true : false;
}

static int gpio_parse_dt(struct gpio_detect *gpiod, const char *ad_name)
{
	struct device *dev = gpiod->dev;
	struct device_node *gpiod_node;
	struct device_node *node;
	const char *name;
	// int num;
	int ret = 0;

	gpiod_node = of_parse_phandle(dev->of_node, "rockchip,gpio-det", 0);
	if (!gpiod_node) {
		VEHICLE_DGERR("phase gpio-det from dts failed, maybe no use!\n");
		return 0;
	}

	gpiod->num = of_get_child_count(gpiod_node);
	if (gpiod->num == 0) {
		VEHICLE_DGERR("gpio-det child count is 0, maybe no use!\n");
		return 0;
	}

	of_property_read_u32(gpiod_node, "rockchip,camcap-mirror",
			     &gpiod->mirror);
	for_each_child_of_node(gpiod_node, node) {
		enum of_gpio_flags flags;

		name = of_get_property(node, "label", NULL);
		if (!strcmp(name, "car-reverse")) {
			gpiod->gpio = of_get_named_gpio_flags(node, "car-reverse-gpios", 0, &flags);
			if (!gpio_is_valid(gpiod->gpio)) {
				dev_err(dev, "failed to get car reverse gpio\n");
				ret = -ENOMEM;
			}
			gpiod->atv_val = !(flags & OF_GPIO_ACTIVE_LOW);
			of_property_read_u32(node, "linux,debounce-ms",
						  &gpiod->debounce_ms);
			break;
		}
	}

	VEHICLE_DG("%s:gpio %d, act_val %d, mirror %d, debounce_ms %d\n",
		__func__, gpiod->gpio, gpiod->atv_val, gpiod->mirror, gpiod->debounce_ms);
	return ret;
}

int vehicle_gpio_init(struct gpio_detect *gpiod, const char *ad_name)
{
	int gpio;
	int ret;
	unsigned long irqflags = IRQF_ONESHOT;

	if (gpio_parse_dt(gpiod, ad_name) < 0) {
		VEHICLE_DGERR("%s,parse dt failed\n", __func__);
		return -EINVAL;
	}

	gpio = gpiod->gpio;

	ret = gpio_request(gpio, "vehicle");
	if (ret < 0)
		VEHICLE_DGERR("%s:failed to request gpio %d, maybe no use\n", __func__, ret);

	dev_info(gpiod->dev, "%s: request irq gpio(%d)\n", __func__, gpio);
	gpio_direction_input(gpio);

	gpiod->irq = gpio_to_irq(gpio);
	if (gpiod->irq < 0)
		VEHICLE_DGERR("failed to get irq, GPIO %d, maybe no use\n", gpio);

	gpiod->val = gpio_get_value(gpio);
	if (gpiod->val)
		irqflags |= IRQ_TYPE_EDGE_FALLING;
	else
		irqflags |= IRQ_TYPE_EDGE_RISING;
	ret = devm_request_threaded_irq(gpiod->dev, gpiod->irq,
					NULL, gpio_det_interrupt,
					irqflags, "vehicle gpio", gpiod);
	if (ret < 0)
		VEHICLE_DGERR("request irq(%s) failed:%d\n",
			"vehicle", ret);

	//if not add in create_workqueue only execute once;
	INIT_DELAYED_WORK(&gpiod->work, gpio_det_work_func);

	vehicle_gpio_init_check(gpiod);

	return 0;
}

int vehicle_gpio_deinit(struct gpio_detect *gpiod)
{
	free_irq(gpiod->irq, gpiod);
	gpio_free(gpiod->gpio);
	return 0;
}

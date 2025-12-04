// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Linaro Ltd.
 */

#include <linux/auxiliary_bus.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/string_choices.h>
#include <linux/types.h>

#include "gpiolib-shared.h"

struct gpio_shared_proxy_data {
	struct gpio_chip gc;
	struct gpio_shared_desc *shared_desc;
	struct device *dev;
	bool voted_high;
};

static int
gpio_shared_proxy_set_unlocked(struct gpio_shared_proxy_data *proxy,
			       int (*set_func)(struct gpio_desc *desc, int value),
			       int value)
{
	struct gpio_shared_desc *shared_desc = proxy->shared_desc;
	struct gpio_desc *desc = shared_desc->desc;
	int ret = 0;

	gpio_shared_lockdep_assert(shared_desc);

	if (value) {
	       /* User wants to set value to high. */
		if (proxy->voted_high)
			/* Already voted for high, nothing to do. */
			goto out;

		/* Haven't voted for high yet. */
		if (!shared_desc->highcnt) {
			/*
			 * Current value is low, need to actually set value
			 * to high.
			 */
			ret = set_func(desc, 1);
			if (ret)
				goto out;
		}

		shared_desc->highcnt++;
		proxy->voted_high = true;

		goto out;
	}

	/* Desired value is low. */
	if (!proxy->voted_high)
		/* We didn't vote for high, nothing to do. */
		goto out;

	/* We previously voted for high. */
	if (shared_desc->highcnt == 1) {
		/* This is the last remaining vote for high, set value  to low. */
		ret = set_func(desc, 0);
		if (ret)
			goto out;
	}

	shared_desc->highcnt--;
	proxy->voted_high = false;

out:
	if (shared_desc->highcnt)
		dev_dbg(proxy->dev,
			"Voted for value '%s', effective value is 'high', number of votes for 'high': %u\n",
			str_high_low(value), shared_desc->highcnt);
	else
		dev_dbg(proxy->dev, "Voted for value 'low', effective value is 'low'\n");

	return ret;
}

static int gpio_shared_proxy_request(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_shared_proxy_data *proxy = gpiochip_get_data(gc);
	struct gpio_shared_desc *shared_desc = proxy->shared_desc;

	guard(gpio_shared_desc_lock)(shared_desc);

	proxy->shared_desc->usecnt++;

	dev_dbg(proxy->dev, "Shared GPIO requested, number of users: %u\n",
		proxy->shared_desc->usecnt);

	return 0;
}

static void gpio_shared_proxy_free(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_shared_proxy_data *proxy = gpiochip_get_data(gc);
	struct gpio_shared_desc *shared_desc = proxy->shared_desc;

	guard(gpio_shared_desc_lock)(shared_desc);

	proxy->shared_desc->usecnt--;

	dev_dbg(proxy->dev, "Shared GPIO freed, number of users: %u\n",
		proxy->shared_desc->usecnt);
}

static int gpio_shared_proxy_set_config(struct gpio_chip *gc,
					unsigned int offset, unsigned long cfg)
{
	struct gpio_shared_proxy_data *proxy = gpiochip_get_data(gc);
	struct gpio_shared_desc *shared_desc = proxy->shared_desc;
	struct gpio_desc *desc = shared_desc->desc;
	int ret;

	guard(gpio_shared_desc_lock)(shared_desc);

	if (shared_desc->usecnt > 1) {
		if (shared_desc->cfg != cfg) {
			dev_dbg(proxy->dev,
				"Shared GPIO's configuration already set, accepting changes but users may conflict!!\n");
		} else {
			dev_dbg(proxy->dev, "Equal config requested, nothing to do\n");
			return 0;
		}
	}

	ret = gpiod_set_config(desc, cfg);
	if (ret && ret != -ENOTSUPP)
		return ret;

	shared_desc->cfg = cfg;
	return 0;
}

static int gpio_shared_proxy_direction_input(struct gpio_chip *gc,
					     unsigned int offset)
{
	struct gpio_shared_proxy_data *proxy = gpiochip_get_data(gc);
	struct gpio_shared_desc *shared_desc = proxy->shared_desc;
	struct gpio_desc *desc = shared_desc->desc;
	int dir;

	guard(gpio_shared_desc_lock)(shared_desc);

	if (shared_desc->usecnt == 1) {
		dev_dbg(proxy->dev,
			"Only one user of this shared GPIO, allowing to set direction to input\n");

		return gpiod_direction_input(desc);
	}

	dir = gpiod_get_direction(desc);
	if (dir < 0)
		return dir;

	if (dir == GPIO_LINE_DIRECTION_OUT) {
		dev_dbg(proxy->dev,
			"Shared GPIO's direction already set to output, refusing to change\n");
		return -EPERM;
	}

	return 0;
}

static int gpio_shared_proxy_direction_output(struct gpio_chip *gc,
					      unsigned int offset, int value)
{
	struct gpio_shared_proxy_data *proxy = gpiochip_get_data(gc);
	struct gpio_shared_desc *shared_desc = proxy->shared_desc;
	struct gpio_desc *desc = shared_desc->desc;
	int ret, dir;

	guard(gpio_shared_desc_lock)(shared_desc);

	if (shared_desc->usecnt == 1) {
		dev_dbg(proxy->dev,
			"Only one user of this shared GPIO, allowing to set direction to output with value '%s'\n",
			str_high_low(value));

		ret = gpiod_direction_output(desc, value);
		if (ret)
			return ret;

		if (value) {
			proxy->voted_high = true;
			shared_desc->highcnt = 1;
		} else {
			proxy->voted_high = false;
			shared_desc->highcnt = 0;
		}

		return 0;
	}

	dir = gpiod_get_direction(desc);
	if (dir < 0)
		return dir;

	if (dir == GPIO_LINE_DIRECTION_IN) {
		dev_dbg(proxy->dev,
			"Shared GPIO's direction already set to input, refusing to change\n");
		return -EPERM;
	}

	return gpio_shared_proxy_set_unlocked(proxy, gpiod_direction_output, value);
}

static int gpio_shared_proxy_get(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_shared_proxy_data *proxy = gpiochip_get_data(gc);

	return gpiod_get_value(proxy->shared_desc->desc);
}

static int gpio_shared_proxy_get_cansleep(struct gpio_chip *gc,
					  unsigned int offset)
{
	struct gpio_shared_proxy_data *proxy = gpiochip_get_data(gc);

	return gpiod_get_value_cansleep(proxy->shared_desc->desc);
}

static int gpio_shared_proxy_do_set(struct gpio_shared_proxy_data *proxy,
				    int (*set_func)(struct gpio_desc *desc, int value),
				    int value)
{
	guard(gpio_shared_desc_lock)(proxy->shared_desc);

	return gpio_shared_proxy_set_unlocked(proxy, set_func, value);
}

static int gpio_shared_proxy_set(struct gpio_chip *gc, unsigned int offset,
				 int value)
{
	struct gpio_shared_proxy_data *proxy = gpiochip_get_data(gc);

	return gpio_shared_proxy_do_set(proxy, gpiod_set_value, value);
}

static int gpio_shared_proxy_set_cansleep(struct gpio_chip *gc,
					  unsigned int offset, int value)
{
	struct gpio_shared_proxy_data *proxy = gpiochip_get_data(gc);

	return gpio_shared_proxy_do_set(proxy, gpiod_set_value_cansleep, value);
}

static int gpio_shared_proxy_get_direction(struct gpio_chip *gc,
					   unsigned int offset)
{
	struct gpio_shared_proxy_data *proxy = gpiochip_get_data(gc);

	return gpiod_get_direction(proxy->shared_desc->desc);
}

static int gpio_shared_proxy_to_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_shared_proxy_data *proxy = gpiochip_get_data(gc);

	return gpiod_to_irq(proxy->shared_desc->desc);
}

static int gpio_shared_proxy_probe(struct auxiliary_device *adev,
				   const struct auxiliary_device_id *id)
{
	struct gpio_shared_proxy_data *proxy;
	struct gpio_shared_desc *shared_desc;
	struct device *dev = &adev->dev;
	struct gpio_chip *gc;

	shared_desc = devm_gpiod_shared_get(dev);
	if (IS_ERR(shared_desc))
		return PTR_ERR(shared_desc);

	proxy = devm_kzalloc(dev, sizeof(*proxy), GFP_KERNEL);
	if (!proxy)
		return -ENOMEM;

	proxy->shared_desc = shared_desc;
	proxy->dev = dev;

	gc = &proxy->gc;
	gc->base = -1;
	gc->ngpio = 1;
	gc->label = dev_name(dev);
	gc->parent = dev;
	gc->owner = THIS_MODULE;
	gc->can_sleep = shared_desc->can_sleep;

	gc->request = gpio_shared_proxy_request;
	gc->free = gpio_shared_proxy_free;
	gc->set_config = gpio_shared_proxy_set_config;
	gc->direction_input = gpio_shared_proxy_direction_input;
	gc->direction_output = gpio_shared_proxy_direction_output;
	if (gc->can_sleep) {
		gc->set = gpio_shared_proxy_set_cansleep;
		gc->get = gpio_shared_proxy_get_cansleep;
	} else {
		gc->set = gpio_shared_proxy_set;
		gc->get = gpio_shared_proxy_get;
	}
	gc->get_direction = gpio_shared_proxy_get_direction;
	gc->to_irq = gpio_shared_proxy_to_irq;

	return devm_gpiochip_add_data(dev, &proxy->gc, proxy);
}

static const struct auxiliary_device_id gpio_shared_proxy_id_table[] = {
	{ .name = "gpiolib_shared.proxy" },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, gpio_shared_proxy_id_table);

static struct auxiliary_driver gpio_shared_proxy_driver = {
	.driver = {
		.name = "gpio-shared-proxy",
		.suppress_bind_attrs = true,
	},
	.probe = gpio_shared_proxy_probe,
	.id_table = gpio_shared_proxy_id_table,
};
module_auxiliary_driver(gpio_shared_proxy_driver);

MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@linaro.org>");
MODULE_DESCRIPTION("Shared GPIO mux driver.");
MODULE_LICENSE("GPL");

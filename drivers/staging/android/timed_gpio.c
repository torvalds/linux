/* drivers/misc/timed_gpio.c
 *
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
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
#include <linux/platform_device.h>
#include <linux/hrtimer.h>
#include <linux/err.h>
#include <linux/gpio.h>

#include "timed_gpio.h"


static struct class *timed_gpio_class;

struct timed_gpio_data {
	struct device *dev;
	struct hrtimer timer;
	spinlock_t lock;
	unsigned 	gpio;
	int 		max_timeout;
	u8 		active_low;
};

static enum hrtimer_restart gpio_timer_func(struct hrtimer *timer)
{
	struct timed_gpio_data *gpio_data = container_of(timer, struct timed_gpio_data, timer);

	gpio_direction_output(gpio_data->gpio, gpio_data->active_low ? 1 : 0);
	return HRTIMER_NORESTART;
}

static ssize_t gpio_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct timed_gpio_data *gpio_data = dev_get_drvdata(dev);
	int remaining;

	if (hrtimer_active(&gpio_data->timer)) {
		ktime_t r = hrtimer_get_remaining(&gpio_data->timer);
		struct timeval t = ktime_to_timeval(r);
		remaining = t.tv_sec * 1000 + t.tv_usec / 1000;
	} else
		remaining = 0;

	return sprintf(buf, "%d\n", remaining);
}

static ssize_t gpio_enable_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct timed_gpio_data *gpio_data = dev_get_drvdata(dev);
	int value;
	unsigned long	flags;

	sscanf(buf, "%d", &value);

	spin_lock_irqsave(&gpio_data->lock, flags);

	/* cancel previous timer and set GPIO according to value */
	hrtimer_cancel(&gpio_data->timer);
	gpio_direction_output(gpio_data->gpio, gpio_data->active_low ? !value : !!value);

	if (value > 0) {
		if (value > gpio_data->max_timeout)
			value = gpio_data->max_timeout;

		hrtimer_start(&gpio_data->timer,
						ktime_set(value / 1000, (value % 1000) * 1000000),
						HRTIMER_MODE_REL);
	}

	spin_unlock_irqrestore(&gpio_data->lock, flags);

	return size;
}

static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR, gpio_enable_show, gpio_enable_store);

static int timed_gpio_probe(struct platform_device *pdev)
{
	struct timed_gpio_platform_data *pdata = pdev->dev.platform_data;
	struct timed_gpio *cur_gpio;
	struct timed_gpio_data *gpio_data, *gpio_dat;
	int i, ret = 0;

	if (!pdata)
		return -EBUSY;

	gpio_data = kzalloc(sizeof(struct timed_gpio_data) * pdata->num_gpios, GFP_KERNEL);
	if (!gpio_data)
		return -ENOMEM;

	for (i = 0; i < pdata->num_gpios; i++) {
		cur_gpio = &pdata->gpios[i];
		gpio_dat = &gpio_data[i];

		hrtimer_init(&gpio_dat->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		gpio_dat->timer.function = gpio_timer_func;
		spin_lock_init(&gpio_dat->lock);

		gpio_dat->gpio = cur_gpio->gpio;
		gpio_dat->max_timeout = cur_gpio->max_timeout;
		gpio_dat->active_low = cur_gpio->active_low;
		gpio_direction_output(gpio_dat->gpio, gpio_dat->active_low);

		gpio_dat->dev = device_create(timed_gpio_class, &pdev->dev, 0, "%s", cur_gpio->name);
		if (unlikely(IS_ERR(gpio_dat->dev)))
			return PTR_ERR(gpio_dat->dev);

		dev_set_drvdata(gpio_dat->dev, gpio_dat);
		ret = device_create_file(gpio_dat->dev, &dev_attr_enable);
		if (ret)
			return ret;
	}

	platform_set_drvdata(pdev, gpio_data);

	return 0;
}

static int timed_gpio_remove(struct platform_device *pdev)
{
	struct timed_gpio_platform_data *pdata = pdev->dev.platform_data;
	struct timed_gpio_data *gpio_data = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < pdata->num_gpios; i++) {
		device_remove_file(gpio_data[i].dev, &dev_attr_enable);
		device_unregister(gpio_data[i].dev);
	}

	kfree(gpio_data);

	return 0;
}

static struct platform_driver timed_gpio_driver = {
	.probe		= timed_gpio_probe,
	.remove		= timed_gpio_remove,
	.driver		= {
		.name		= "timed-gpio",
		.owner		= THIS_MODULE,
	},
};

static int __init timed_gpio_init(void)
{
	timed_gpio_class = class_create(THIS_MODULE, "timed_output");
	if (IS_ERR(timed_gpio_class))
		return PTR_ERR(timed_gpio_class);
	return platform_driver_register(&timed_gpio_driver);
}

static void __exit timed_gpio_exit(void)
{
	class_destroy(timed_gpio_class);
	platform_driver_unregister(&timed_gpio_driver);
}

module_init(timed_gpio_init);
module_exit(timed_gpio_exit);

MODULE_AUTHOR("Mike Lockwood <lockwood@android.com>");
MODULE_DESCRIPTION("timed gpio driver");
MODULE_LICENSE("GPL");

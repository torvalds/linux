/* drivers/staging/android/rk_timed_gpio.c
 *
 * Copyright (C) 2012-2016 ROCKCHIP.
 * Author: jerry <jerry.zhang@rock-chips.com>
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
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include "timed_output.h"

#define MAX_TIMEOUT        10000	/* 10s */

static struct vibrator {
	int gpio;
	struct wake_lock wklock;
	struct hrtimer timer;
	struct mutex lock;	/* mutex lock */
	struct work_struct work;
} vibdata;

static void rk_vibrator_off(void)
{
	gpio_direction_output(vibdata.gpio, 0);
	gpio_set_value(vibdata.gpio, 0);

	wake_unlock(&vibdata.wklock);
}

static void rk_vibrator_enable(struct timed_output_dev *sdev, int value)
{
	mutex_lock(&vibdata.lock);

	/* cancel previous timer and set GPIO according to value */
	hrtimer_cancel(&vibdata.timer);
	cancel_work_sync(&vibdata.work);
	if (value) {
		wake_lock(&vibdata.wklock);
		gpio_direction_output(vibdata.gpio, 1);
		gpio_set_value(vibdata.gpio, 1);

		if (value > 0) {
			if (value > MAX_TIMEOUT)
				value = MAX_TIMEOUT;
		value += 45;
		hrtimer_start(&vibdata.timer,
			ns_to_ktime((u64)value * NSEC_PER_MSEC),
			HRTIMER_MODE_REL);
		}
	} else {
		rk_vibrator_off();
	}

	mutex_unlock(&vibdata.lock);
}

static int rk_vibrator_get_time(struct timed_output_dev *sdev)
{
	if (hrtimer_active(&vibdata.timer)) {
		ktime_t r = hrtimer_get_remaining(&vibdata.timer);

		return ktime_to_ms(r);
	}

	return 0;
}

static enum hrtimer_restart rk_vibrator_timer_func(struct hrtimer *timer)
{
	schedule_work(&vibdata.work);
	return HRTIMER_NORESTART;
}

static void rk_vibrator_work(struct work_struct *work)
{
	rk_vibrator_off();
}

struct timed_output_dev rk_vibrator_driver = {
	.name = "vibrator",
	.enable = rk_vibrator_enable,
	.get_time = rk_vibrator_get_time,
};

static int vibrator_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device_node *np = pdev->dev.of_node;

	vibdata.gpio = of_get_named_gpio_flags(np, "vibrator-gpio", 0, NULL);
	if (!gpio_is_valid(vibdata.gpio))
		return -1;

	hrtimer_init(&vibdata.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vibdata.timer.function = rk_vibrator_timer_func;
	INIT_WORK(&vibdata.work, rk_vibrator_work);

	ret = devm_gpio_request(&pdev->dev, vibdata.gpio, "vibrator");
	if (ret < 0)
		return ret;

	wake_lock_init(&vibdata.wklock, WAKE_LOCK_SUSPEND, "vibrator");
	mutex_init(&vibdata.lock);
	ret = timed_output_dev_register(&rk_vibrator_driver);
	if (ret < 0)
		goto err_to_dev_reg;

	return 0;

err_to_dev_reg:
	mutex_destroy(&vibdata.lock);
	wake_lock_destroy(&vibdata.wklock);

	return ret;
}

static int vibrator_remove(struct platform_device *pdev)
{
	mutex_destroy(&vibdata.lock);
	wake_lock_destroy(&vibdata.wklock);
	timed_output_dev_unregister(&rk_vibrator_driver);

	return 0;
}

static const struct of_device_id vibrator_of_match[] = {
	{ .compatible = "rk-vibrator-gpio" },
	{ }
};

static struct platform_driver vibrator_driver = {
	.probe = vibrator_probe,
	.remove = vibrator_remove,
	.driver = {
		.name           = "rk-vibrator",
		.of_match_table = of_match_ptr(vibrator_of_match),
	},
};

module_platform_driver(vibrator_driver);

MODULE_AUTHOR("jerry.zhang@rock-chips.com");
MODULE_DESCRIPTION("RK Vibrator driver");
MODULE_LICENSE("GPL");

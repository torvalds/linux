/* drivers/input/misc/gpio_event.c
 *
 * Copyright (C) 2007 Google, Inc.
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
#include <linux/input.h>
#include <linux/gpio_event.h>
#include <linux/hrtimer.h>
#include <linux/platform_device.h>

struct gpio_event {
	struct input_dev *input_dev;
	const struct gpio_event_platform_data *info;
	void *state[0];
};

static int gpio_input_event(
	struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	int i;
	int ret = 0;
	int tmp_ret;
	struct gpio_event_info **ii;
	struct gpio_event *ip = input_get_drvdata(dev);

	for (i = 0, ii = ip->info->info; i < ip->info->info_count; i++, ii++) {
		if ((*ii)->event) {
			tmp_ret = (*ii)->event(ip->input_dev, *ii,
					&ip->state[i], type, code, value);
			if (tmp_ret)
				ret = tmp_ret;
		}
	}
	return ret;
}

static int gpio_event_call_all_func(struct gpio_event *ip, int func)
{
	int i;
	int ret;
	struct gpio_event_info **ii;

	if (func == GPIO_EVENT_FUNC_INIT || func == GPIO_EVENT_FUNC_RESUME) {
		ii = ip->info->info;
		for (i = 0; i < ip->info->info_count; i++, ii++) {
			if ((*ii)->func == NULL) {
				ret = -ENODEV;
				pr_err("gpio_event_probe: Incomplete pdata, "
					"no function\n");
				goto err_no_func;
			}
			ret = (*ii)->func(ip->input_dev, *ii, &ip->state[i],
					  func);
			if (ret) {
				pr_err("gpio_event_probe: function failed\n");
				goto err_func_failed;
			}
		}
		return 0;
	}

	ret = 0;
	i = ip->info->info_count;
	ii = ip->info->info + i;
	while (i > 0) {
		i--;
		ii--;
		(*ii)->func(ip->input_dev, *ii, &ip->state[i], func & ~1);
err_func_failed:
err_no_func:
		;
	}
	return ret;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
void gpio_event_suspend(struct early_suspend *h)
{
	struct gpio_event *ip;
	ip = container_of(h, struct gpio_event, early_suspend);
	gpio_event_call_all_func(ip, GPIO_EVENT_FUNC_SUSPEND);
	ip->info->power(ip->info, 0);
}

void gpio_event_resume(struct early_suspend *h)
{
	struct gpio_event *ip;
	ip = container_of(h, struct gpio_event, early_suspend);
	ip->info->power(ip->info, 1);
	gpio_event_call_all_func(ip, GPIO_EVENT_FUNC_RESUME);
}
#endif

static int __init gpio_event_probe(struct platform_device *pdev)
{
	int err;
	struct gpio_event *ip;
	struct input_dev *input_dev;
	struct gpio_event_platform_data *event_info;

	event_info = pdev->dev.platform_data;
	if (event_info == NULL) {
		pr_err("gpio_event_probe: No pdata\n");
		return -ENODEV;
	}
	if (event_info->name == NULL ||
	   event_info->info == NULL ||
	   event_info->info_count == 0) {
		pr_err("gpio_event_probe: Incomplete pdata\n");
		return -ENODEV;
	}
	ip = kzalloc(sizeof(*ip) +
		     sizeof(ip->state[0]) * event_info->info_count, GFP_KERNEL);
	if (ip == NULL) {
		err = -ENOMEM;
		pr_err("gpio_event_probe: Failed to allocate private data\n");
		goto err_kp_alloc_failed;
	}
	platform_set_drvdata(pdev, ip);

	input_dev = input_allocate_device();
	if (input_dev == NULL) {
		err = -ENOMEM;
		pr_err("gpio_event_probe: Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}
	input_set_drvdata(input_dev, ip);
	ip->input_dev = input_dev;
	ip->info = event_info;
	if (event_info->power) {
#ifdef CONFIG_HAS_EARLYSUSPEND
		ip->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
		ip->early_suspend.suspend = gpio_event_suspend;
		ip->early_suspend.resume = gpio_event_resume;
		register_early_suspend(&ip->early_suspend);
#endif
		ip->info->power(ip->info, 1);
	}

	input_dev->name = ip->info->name;
	input_dev->event = gpio_input_event;

	err = gpio_event_call_all_func(ip, GPIO_EVENT_FUNC_INIT);
	if (err)
		goto err_call_all_func_failed;

	err = input_register_device(input_dev);
	if (err) {
		pr_err("gpio_event_probe: Unable to register %s input device\n",
			input_dev->name);
		goto err_input_register_device_failed;
	}

	return 0;

err_input_register_device_failed:
	gpio_event_call_all_func(ip, GPIO_EVENT_FUNC_UNINIT);
err_call_all_func_failed:
	if (event_info->power) {
#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&ip->early_suspend);
#endif
		ip->info->power(ip->info, 0);
	}
	input_free_device(input_dev);
err_input_dev_alloc_failed:
	kfree(ip);
err_kp_alloc_failed:
	return err;
}

static int gpio_event_remove(struct platform_device *pdev)
{
	struct gpio_event *ip = platform_get_drvdata(pdev);

	gpio_event_call_all_func(ip, GPIO_EVENT_FUNC_UNINIT);
	if (ip->info->power) {
#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&ip->early_suspend);
#endif
		ip->info->power(ip->info, 0);
	}
	input_unregister_device(ip->input_dev);
	kfree(ip);
	return 0;
}

static struct platform_driver gpio_event_driver = {
	.probe		= gpio_event_probe,
	.remove		= gpio_event_remove,
	.driver		= {
		.name	= GPIO_EVENT_DEV_NAME,
	},
};

static int __devinit gpio_event_init(void)
{
	return platform_driver_register(&gpio_event_driver);
}

static void __exit gpio_event_exit(void)
{
	platform_driver_unregister(&gpio_event_driver);
}

module_init(gpio_event_init);
module_exit(gpio_event_exit);

MODULE_DESCRIPTION("GPIO Event Driver");
MODULE_LICENSE("GPL");


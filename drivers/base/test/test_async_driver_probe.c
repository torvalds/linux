/*
 * Copyright (C) 2014 Google, Inc.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/time.h>

#define TEST_PROBE_DELAY	(5 * 1000)	/* 5 sec */
#define TEST_PROBE_THRESHOLD	(TEST_PROBE_DELAY / 2)

static int test_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "sleeping for %d msecs in probe\n",
		 TEST_PROBE_DELAY);
	msleep(TEST_PROBE_DELAY);
	dev_info(&pdev->dev, "done sleeping\n");

	return 0;
}

static struct platform_driver async_driver = {
	.driver = {
		.name = "test_async_driver",
		.owner = THIS_MODULE,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = test_probe,
};

static struct platform_driver sync_driver = {
	.driver = {
		.name = "test_sync_driver",
		.owner = THIS_MODULE,
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
	},
	.probe = test_probe,
};

static struct platform_device *async_dev_1, *async_dev_2;
static struct platform_device *sync_dev_1;

static int __init test_async_probe_init(void)
{
	ktime_t calltime, delta;
	unsigned long long duration;
	int error;

	pr_info("registering first asynchronous device...\n");

	async_dev_1 = platform_device_register_simple("test_async_driver", 1,
						      NULL, 0);
	if (IS_ERR(async_dev_1)) {
		error = PTR_ERR(async_dev_1);
		pr_err("failed to create async_dev_1: %d", error);
		return error;
	}

	pr_info("registering asynchronous driver...\n");
	calltime = ktime_get();
	error = platform_driver_register(&async_driver);
	if (error) {
		pr_err("Failed to register async_driver: %d\n", error);
		goto err_unregister_async_dev_1;
	}

	delta = ktime_sub(ktime_get(), calltime);
	duration = (unsigned long long) ktime_to_ms(delta);
	pr_info("registration took %lld msecs\n", duration);
	if (duration > TEST_PROBE_THRESHOLD) {
		pr_err("test failed: probe took too long\n");
		error = -ETIMEDOUT;
		goto err_unregister_async_driver;
	}

	pr_info("registering second asynchronous device...\n");
	calltime = ktime_get();
	async_dev_2 = platform_device_register_simple("test_async_driver", 2,
						      NULL, 0);
	if (IS_ERR(async_dev_2)) {
		error = PTR_ERR(async_dev_2);
		pr_err("failed to create async_dev_2: %d", error);
		goto err_unregister_async_driver;
	}

	delta = ktime_sub(ktime_get(), calltime);
	duration = (unsigned long long) ktime_to_ms(delta);
	pr_info("registration took %lld msecs\n", duration);
	if (duration > TEST_PROBE_THRESHOLD) {
		pr_err("test failed: probe took too long\n");
		error = -ETIMEDOUT;
		goto err_unregister_async_dev_2;
	}

	pr_info("registering synchronous driver...\n");

	error = platform_driver_register(&sync_driver);
	if (error) {
		pr_err("Failed to register async_driver: %d\n", error);
		goto err_unregister_async_dev_2;
	}

	pr_info("registering synchronous device...\n");
	calltime = ktime_get();
	sync_dev_1 = platform_device_register_simple("test_sync_driver", 1,
						     NULL, 0);
	if (IS_ERR(async_dev_1)) {
		error = PTR_ERR(sync_dev_1);
		pr_err("failed to create sync_dev_1: %d", error);
		goto err_unregister_sync_driver;
	}

	delta = ktime_sub(ktime_get(), calltime);
	duration = (unsigned long long) ktime_to_ms(delta);
	pr_info("registration took %lld msecs\n", duration);
	if (duration < TEST_PROBE_THRESHOLD) {
		pr_err("test failed: probe was too quick\n");
		error = -ETIMEDOUT;
		goto err_unregister_sync_dev_1;
	}

	pr_info("completed successfully");

	return 0;

err_unregister_sync_dev_1:
	platform_device_unregister(sync_dev_1);

err_unregister_sync_driver:
	platform_driver_unregister(&sync_driver);

err_unregister_async_dev_2:
	platform_device_unregister(async_dev_2);

err_unregister_async_driver:
	platform_driver_unregister(&async_driver);

err_unregister_async_dev_1:
	platform_device_unregister(async_dev_1);

	return error;
}
module_init(test_async_probe_init);

static void __exit test_async_probe_exit(void)
{
	platform_driver_unregister(&async_driver);
	platform_driver_unregister(&sync_driver);
	platform_device_unregister(async_dev_1);
	platform_device_unregister(async_dev_2);
	platform_device_unregister(sync_dev_1);
}
module_exit(test_async_probe_exit);

MODULE_DESCRIPTION("Test module for asynchronous driver probing");
MODULE_AUTHOR("Dmitry Torokhov <dtor@chromium.org>");
MODULE_LICENSE("GPL");

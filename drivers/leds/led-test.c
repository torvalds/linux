// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC
 *
 * Author: Lee Jones <lee@kernel.org>
 */

#include <kunit/device.h>
#include <kunit/test.h>
#include <linux/device.h>
#include <linux/leds.h>

struct led_test_ddata {
	struct led_classdev cdev;
	struct device *dev;
};

static void led_test_class_register(struct kunit *test)
{
	struct led_test_ddata *ddata = test->priv;
	struct led_classdev *cdev = &ddata->cdev;
	struct device *dev = ddata->dev;
	int ret;

	cdev->name = "led-test";

	ret = devm_led_classdev_register(dev, cdev);
	KUNIT_ASSERT_EQ(test, ret, 0);
	if (ret)
		return;
}

static struct kunit_case led_test_cases[] = {
	KUNIT_CASE(led_test_class_register),
	{ }
};

static int led_test_init(struct kunit *test)
{
	struct led_test_ddata *ddata;
	struct device *dev;

	ddata = kunit_kzalloc(test, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	test->priv = ddata;

	dev = kunit_device_register(test, "led_test");
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	ddata->dev = get_device(dev);

	return 0;
}

static void led_test_exit(struct kunit *test)
{
	struct led_test_ddata *ddata = test->priv;

	if (ddata && ddata->dev)
		put_device(ddata->dev);
}

static struct kunit_suite led_test_suite = {
	.name = "led",
	.init = led_test_init,
	.exit = led_test_exit,
	.test_cases = led_test_cases,
};
kunit_test_suite(led_test_suite);

MODULE_AUTHOR("Lee Jones <lee@kernel.org>");
MODULE_DESCRIPTION("KUnit tests for the LED framework");
MODULE_LICENSE("GPL");

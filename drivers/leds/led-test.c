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

#define LED_TEST_POST_REG_BRIGHTNESS 10

struct led_test_ddata {
	struct led_classdev cdev;
	struct device *dev;
};

static enum led_brightness led_test_brightness_get(struct led_classdev *cdev)
{
	return LED_TEST_POST_REG_BRIGHTNESS;
}

static void led_test_class_register(struct kunit *test)
{
	struct led_test_ddata *ddata = test->priv;
	struct led_classdev *cdev_clash, *cdev = &ddata->cdev;
	struct device *dev = ddata->dev;
	int ret;

	/* Register a LED class device */
	cdev->name = "led-test";
	cdev->brightness_get = led_test_brightness_get;
	cdev->brightness = 0;

	ret = devm_led_classdev_register(dev, cdev);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, cdev->max_brightness, LED_FULL);
	KUNIT_EXPECT_EQ(test, cdev->brightness, LED_TEST_POST_REG_BRIGHTNESS);
	KUNIT_EXPECT_STREQ(test, cdev->dev->kobj.name, "led-test");

	/* Register again with the same name - expect it to pass with the LED renamed */
	cdev_clash = devm_kmemdup(dev, cdev, sizeof(*cdev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cdev_clash);

	ret = devm_led_classdev_register(dev, cdev_clash);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_STREQ(test, cdev_clash->dev->kobj.name, "led-test_1");
	KUNIT_EXPECT_STREQ(test, cdev_clash->name, "led-test");

	/* Enable name conflict rejection and register with the same name again - expect failure */
	cdev_clash->flags |= LED_REJECT_NAME_CONFLICT;
	ret = devm_led_classdev_register(dev, cdev_clash);
	KUNIT_EXPECT_EQ(test, ret, -EEXIST);
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

// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for the input core.
 *
 * Copyright (c) 2023 Red Hat Inc
 */

#include <linux/delay.h>
#include <linux/input.h>

#include <kunit/test.h>

#define POLL_INTERVAL 100

static int input_test_init(struct kunit *test)
{
	struct input_dev *input_dev;
	int ret;

	input_dev = input_allocate_device();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, input_dev);

	input_dev->name = "Test input device";
	input_dev->id.bustype = BUS_VIRTUAL;
	input_dev->id.vendor = 1;
	input_dev->id.product = 1;
	input_dev->id.version = 1;
	input_set_capability(input_dev, EV_KEY, BTN_LEFT);
	input_set_capability(input_dev, EV_KEY, BTN_RIGHT);

	ret = input_register_device(input_dev);
	if (ret) {
		input_free_device(input_dev);
		KUNIT_ASSERT_FAILURE(test, "Register device failed: %d", ret);
	}

	test->priv = input_dev;

	return 0;
}

static void input_test_exit(struct kunit *test)
{
	struct input_dev *input_dev = test->priv;

	input_unregister_device(input_dev);
	input_free_device(input_dev);
}

static void input_test_poll(struct input_dev *input) { }

static void input_test_polling(struct kunit *test)
{
	struct input_dev *input_dev = test->priv;

	/* Must fail because a poll handler has not been set-up yet */
	KUNIT_ASSERT_EQ(test, input_get_poll_interval(input_dev), -EINVAL);

	KUNIT_ASSERT_EQ(test, input_setup_polling(input_dev, input_test_poll), 0);

	input_set_poll_interval(input_dev, POLL_INTERVAL);

	/* Must succeed because poll handler was set-up and poll interval set */
	KUNIT_ASSERT_EQ(test, input_get_poll_interval(input_dev), POLL_INTERVAL);
}

static void input_test_timestamp(struct kunit *test)
{
	const ktime_t invalid_timestamp = ktime_set(0, 0);
	struct input_dev *input_dev = test->priv;
	ktime_t *timestamp, time;

	timestamp = input_get_timestamp(input_dev);
	time = timestamp[INPUT_CLK_MONO];

	/* The returned timestamp must always be valid */
	KUNIT_ASSERT_EQ(test, ktime_compare(time, invalid_timestamp), 1);

	time = ktime_get();
	input_set_timestamp(input_dev, time);

	timestamp = input_get_timestamp(input_dev);
	/* The timestamp must be the same than set before */
	KUNIT_ASSERT_EQ(test, ktime_compare(timestamp[INPUT_CLK_MONO], time), 0);
}

static void input_test_match_device_id(struct kunit *test)
{
	struct input_dev *input_dev = test->priv;
	struct input_device_id id;

	/*
	 * Must match when the input device bus, vendor, product, version
	 * and events capable of handling are the same and fail to match
	 * otherwise.
	 */
	id.flags = INPUT_DEVICE_ID_MATCH_BUS;
	id.bustype = BUS_VIRTUAL;
	KUNIT_ASSERT_TRUE(test, input_match_device_id(input_dev, &id));

	id.bustype = BUS_I2C;
	KUNIT_ASSERT_FALSE(test, input_match_device_id(input_dev, &id));

	id.flags = INPUT_DEVICE_ID_MATCH_VENDOR;
	id.vendor = 1;
	KUNIT_ASSERT_TRUE(test, input_match_device_id(input_dev, &id));

	id.vendor = 2;
	KUNIT_ASSERT_FALSE(test, input_match_device_id(input_dev, &id));

	id.flags = INPUT_DEVICE_ID_MATCH_PRODUCT;
	id.product = 1;
	KUNIT_ASSERT_TRUE(test, input_match_device_id(input_dev, &id));

	id.product = 2;
	KUNIT_ASSERT_FALSE(test, input_match_device_id(input_dev, &id));

	id.flags = INPUT_DEVICE_ID_MATCH_VERSION;
	id.version = 1;
	KUNIT_ASSERT_TRUE(test, input_match_device_id(input_dev, &id));

	id.version = 2;
	KUNIT_ASSERT_FALSE(test, input_match_device_id(input_dev, &id));

	id.flags = INPUT_DEVICE_ID_MATCH_EVBIT;
	__set_bit(EV_KEY, id.evbit);
	KUNIT_ASSERT_TRUE(test, input_match_device_id(input_dev, &id));

	__set_bit(EV_ABS, id.evbit);
	KUNIT_ASSERT_FALSE(test, input_match_device_id(input_dev, &id));
}

static struct kunit_case input_tests[] = {
	KUNIT_CASE(input_test_polling),
	KUNIT_CASE(input_test_timestamp),
	KUNIT_CASE(input_test_match_device_id),
	{ /* sentinel */ }
};

static struct kunit_suite input_test_suite = {
	.name = "input_core",
	.init = input_test_init,
	.exit = input_test_exit,
	.test_cases = input_tests,
};

kunit_test_suite(input_test_suite);

MODULE_AUTHOR("Javier Martinez Canillas <javierm@redhat.com>");
MODULE_LICENSE("GPL");

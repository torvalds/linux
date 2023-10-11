// SPDX-License-Identifier: GPL-2.0
// Copyright 2023 Maxime Ripard <mripard@kernel.org>

#include <kunit/resource.h>

#include <linux/device.h>

#define DEVICE_NAME "test"

struct test_priv {
	bool probe_done;
	bool release_done;
	wait_queue_head_t release_wq;
	struct device *dev;
};

static int root_device_devm_init(struct kunit *test)
{
	struct test_priv *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);
	init_waitqueue_head(&priv->release_wq);

	test->priv = priv;

	return 0;
}

static void devm_device_action(void *ptr)
{
	struct test_priv *priv = ptr;

	priv->release_done = true;
	wake_up_interruptible(&priv->release_wq);
}

#define RELEASE_TIMEOUT_MS	100

/*
 * Tests that a bus-less, non-probed device will run its device-managed
 * actions when unregistered.
 */
static void root_device_devm_register_unregister_test(struct kunit *test)
{
	struct test_priv *priv = test->priv;
	int ret;

	priv->dev = root_device_register(DEVICE_NAME);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->dev);

	ret = devm_add_action_or_reset(priv->dev, devm_device_action, priv);
	KUNIT_ASSERT_EQ(test, ret, 0);

	root_device_unregister(priv->dev);

	ret = wait_event_interruptible_timeout(priv->release_wq, priv->release_done,
					       msecs_to_jiffies(RELEASE_TIMEOUT_MS));
	KUNIT_EXPECT_GT(test, ret, 0);
}

static void devm_put_device_action(void *ptr)
{
	struct test_priv *priv = ptr;

	put_device(priv->dev);
	priv->release_done = true;
	wake_up_interruptible(&priv->release_wq);
}

/*
 * Tests that a bus-less, non-probed device will run its device-managed
 * actions when unregistered, even if someone still holds a reference to
 * it.
 */
static void root_device_devm_register_get_unregister_with_devm_test(struct kunit *test)
{
	struct test_priv *priv = test->priv;
	int ret;

	priv->dev = root_device_register(DEVICE_NAME);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->dev);

	get_device(priv->dev);

	ret = devm_add_action_or_reset(priv->dev, devm_put_device_action, priv);
	KUNIT_ASSERT_EQ(test, ret, 0);

	root_device_unregister(priv->dev);

	ret = wait_event_interruptible_timeout(priv->release_wq, priv->release_done,
					       msecs_to_jiffies(RELEASE_TIMEOUT_MS));
	KUNIT_EXPECT_GT(test, ret, 0);
}

static struct kunit_case root_device_devm_tests[] = {
	KUNIT_CASE(root_device_devm_register_unregister_test),
	KUNIT_CASE(root_device_devm_register_get_unregister_with_devm_test),
	{}
};

static struct kunit_suite root_device_devm_test_suite = {
	.name = "root-device-devm",
	.init = root_device_devm_init,
	.test_cases = root_device_devm_tests,
};

kunit_test_suite(root_device_devm_test_suite);

MODULE_DESCRIPTION("Test module for root devices");
MODULE_AUTHOR("Maxime Ripard <mripard@kernel.org>");
MODULE_LICENSE("GPL");

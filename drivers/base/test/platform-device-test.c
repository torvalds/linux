// SPDX-License-Identifier: GPL-2.0

#include <kunit/resource.h>

#include <linux/device.h>
#include <linux/platform_device.h>

#define DEVICE_NAME "test"

struct test_priv {
	bool probe_done;
	bool release_done;
	wait_queue_head_t probe_wq;
	wait_queue_head_t release_wq;
	struct device *dev;
};

static int platform_device_devm_init(struct kunit *test)
{
	struct test_priv *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);
	init_waitqueue_head(&priv->probe_wq);
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

static void devm_put_device_action(void *ptr)
{
	struct test_priv *priv = ptr;

	put_device(priv->dev);
	priv->release_done = true;
	wake_up_interruptible(&priv->release_wq);
}

#define RELEASE_TIMEOUT_MS	100

/*
 * Tests that a platform bus, non-probed device will run its
 * device-managed actions when unregistered.
 */
static void platform_device_devm_register_unregister_test(struct kunit *test)
{
	struct platform_device *pdev;
	struct test_priv *priv = test->priv;
	int ret;

	pdev = platform_device_alloc(DEVICE_NAME, PLATFORM_DEVID_NONE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	ret = platform_device_add(pdev);
	KUNIT_ASSERT_EQ(test, ret, 0);

	priv->dev = &pdev->dev;

	ret = devm_add_action_or_reset(priv->dev, devm_device_action, priv);
	KUNIT_ASSERT_EQ(test, ret, 0);

	platform_device_unregister(pdev);

	ret = wait_event_interruptible_timeout(priv->release_wq, priv->release_done,
					       msecs_to_jiffies(RELEASE_TIMEOUT_MS));
	KUNIT_EXPECT_GT(test, ret, 0);
}

/*
 * Tests that a platform bus, non-probed device will run its
 * device-managed actions when unregistered, even if someone still holds
 * a reference to it.
 */
static void platform_device_devm_register_get_unregister_with_devm_test(struct kunit *test)
{
	struct platform_device *pdev;
	struct test_priv *priv = test->priv;
	int ret;

	pdev = platform_device_alloc(DEVICE_NAME, PLATFORM_DEVID_NONE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	ret = platform_device_add(pdev);
	KUNIT_ASSERT_EQ(test, ret, 0);

	priv->dev = &pdev->dev;

	get_device(priv->dev);

	ret = devm_add_action_or_reset(priv->dev, devm_put_device_action, priv);
	KUNIT_ASSERT_EQ(test, ret, 0);

	platform_device_unregister(pdev);

	ret = wait_event_interruptible_timeout(priv->release_wq, priv->release_done,
					       msecs_to_jiffies(RELEASE_TIMEOUT_MS));
	KUNIT_EXPECT_GT(test, ret, 0);
}

static int fake_probe(struct platform_device *pdev)
{
	struct test_priv *priv = platform_get_drvdata(pdev);

	priv->probe_done = true;
	wake_up_interruptible(&priv->probe_wq);

	return 0;
}

static struct platform_driver fake_driver = {
	.probe	= fake_probe,
	.driver = {
		.name = DEVICE_NAME,
	},
};

/*
 * Tests that a platform bus, probed device will run its device-managed
 * actions when unregistered.
 */
static void probed_platform_device_devm_register_unregister_test(struct kunit *test)
{
	struct platform_device *pdev;
	struct test_priv *priv = test->priv;
	int ret;

	ret = platform_driver_register(&fake_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	pdev = platform_device_alloc(DEVICE_NAME, PLATFORM_DEVID_NONE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	priv->dev = &pdev->dev;
	platform_set_drvdata(pdev, priv);

	ret = platform_device_add(pdev);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = wait_event_interruptible_timeout(priv->probe_wq, priv->probe_done,
					       msecs_to_jiffies(RELEASE_TIMEOUT_MS));
	KUNIT_ASSERT_GT(test, ret, 0);

	ret = devm_add_action_or_reset(priv->dev, devm_device_action, priv);
	KUNIT_ASSERT_EQ(test, ret, 0);

	platform_device_unregister(pdev);

	ret = wait_event_interruptible_timeout(priv->release_wq, priv->release_done,
					       msecs_to_jiffies(RELEASE_TIMEOUT_MS));
	KUNIT_EXPECT_GT(test, ret, 0);

	platform_driver_unregister(&fake_driver);
}

/*
 * Tests that a platform bus, probed device will run its device-managed
 * actions when unregistered, even if someone still holds a reference to
 * it.
 */
static void probed_platform_device_devm_register_get_unregister_with_devm_test(struct kunit *test)
{
	struct platform_device *pdev;
	struct test_priv *priv = test->priv;
	int ret;

	ret = platform_driver_register(&fake_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	pdev = platform_device_alloc(DEVICE_NAME, PLATFORM_DEVID_NONE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	priv->dev = &pdev->dev;
	platform_set_drvdata(pdev, priv);

	ret = platform_device_add(pdev);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = wait_event_interruptible_timeout(priv->probe_wq, priv->probe_done,
					       msecs_to_jiffies(RELEASE_TIMEOUT_MS));
	KUNIT_ASSERT_GT(test, ret, 0);

	get_device(priv->dev);

	ret = devm_add_action_or_reset(priv->dev, devm_put_device_action, priv);
	KUNIT_ASSERT_EQ(test, ret, 0);

	platform_device_unregister(pdev);

	ret = wait_event_interruptible_timeout(priv->release_wq, priv->release_done,
					       msecs_to_jiffies(RELEASE_TIMEOUT_MS));
	KUNIT_EXPECT_GT(test, ret, 0);

	platform_driver_unregister(&fake_driver);
}

static struct kunit_case platform_device_devm_tests[] = {
	KUNIT_CASE(platform_device_devm_register_unregister_test),
	KUNIT_CASE(platform_device_devm_register_get_unregister_with_devm_test),
	KUNIT_CASE(probed_platform_device_devm_register_unregister_test),
	KUNIT_CASE(probed_platform_device_devm_register_get_unregister_with_devm_test),
	{}
};

static struct kunit_suite platform_device_devm_test_suite = {
	.name = "platform-device-devm",
	.init = platform_device_devm_init,
	.test_cases = platform_device_devm_tests,
};

kunit_test_suite(platform_device_devm_test_suite);

MODULE_DESCRIPTION("Test module for platform devices");
MODULE_AUTHOR("Maxime Ripard <mripard@kernel.org>");
MODULE_LICENSE("GPL");

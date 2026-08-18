// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Qualcomm Technologies, Inc. and/or its subsidiaries
 */

#include <linux/fwnode.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/gpio/property.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#include <kunit/platform_device.h>
#include <kunit/test.h>

#define GPIO_TEST_PROVIDER		"gpio-test-provider"
#define GPIO_SWNODE_TEST_CONSUMER	"gpio-swnode-test-consumer"
#define GPIO_UNBIND_TEST_CONSUMER	"gpio-unbind-test-consumer"

static int gpio_test_provider_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	return GPIO_LINE_DIRECTION_OUT;
}

static int gpio_test_provider_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	return 0;
}

static int gpio_test_provider_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_chip *gc;

	gc = devm_kzalloc(dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	gc->base = -1;
	gc->ngpio = 4;
	gc->label = "gpio-swnode-consumer-test-device";
	gc->parent = dev;
	gc->owner = THIS_MODULE;

	gc->get_direction = gpio_test_provider_get_direction;
	gc->set = gpio_test_provider_set;

	return devm_gpiochip_add_data(dev, gc, NULL);
}

static struct platform_driver gpio_test_provider_driver = {
	.probe = gpio_test_provider_probe,
	.driver = {
		.name = GPIO_TEST_PROVIDER,
	},
};

static const struct software_node gpio_test_provider_swnode = {
	.name = "gpio-test-provider-primary",
};

struct gpio_swnode_consumer_pdata {
	bool gpio_ok;
};

static const struct gpio_swnode_consumer_pdata gpio_swnode_pdata_template = {
	.gpio_ok = false,
};

static int gpio_swnode_consumer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_swnode_consumer_pdata *pdata = dev_get_platdata(dev);
	struct gpio_desc *desc;

	desc = devm_gpiod_get(dev, "foo", GPIOD_OUT_HIGH);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	pdata->gpio_ok = true;

	return 0;
}

static struct platform_driver gpio_swnode_consumer_driver = {
	.probe = gpio_swnode_consumer_probe,
	.driver = {
		.name = GPIO_SWNODE_TEST_CONSUMER,
	},
};

static void gpio_swnode_lookup_by_primary(struct kunit *test)
{
	struct gpio_swnode_consumer_pdata *pdata;
	struct platform_device_info pdevinfo;
	struct property_entry properties[2];
	struct platform_device *pdev;
	bool bound = false;
	int ret;

	ret = kunit_platform_driver_register(test, &gpio_test_provider_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_platform_driver_register(test, &gpio_swnode_consumer_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	pdevinfo = (struct platform_device_info){
		.name = GPIO_TEST_PROVIDER,
		.id = PLATFORM_DEVID_NONE,
		.swnode = &gpio_test_provider_swnode,
	};

	pdev = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	properties[0] = PROPERTY_ENTRY_GPIO("foo-gpios",
					    &gpio_test_provider_swnode,
					    0, GPIO_ACTIVE_HIGH);
	properties[1] = (struct property_entry){ };

	pdevinfo = (struct platform_device_info){
		.name = GPIO_SWNODE_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
		.data = &gpio_swnode_pdata_template,
		.size_data = sizeof(gpio_swnode_pdata_template),
		.properties = properties,
	};

	pdev = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	wait_for_device_probe();
	scoped_guard(device, &pdev->dev)
		bound = device_is_bound(&pdev->dev);

	KUNIT_ASSERT_TRUE(test, bound);

	pdata = dev_get_platdata(&pdev->dev);
	KUNIT_ASSERT_TRUE(test, pdata->gpio_ok);
}

static void gpio_swnode_lookup_by_secondary(struct kunit *test)
{
	struct gpio_swnode_consumer_pdata *pdata;
	struct platform_device_info pdevinfo;
	struct property_entry properties[2];
	struct fwnode_handle *primary;
	struct platform_device *pdev;
	bool bound = false;
	int ret;

	/*
	 * Can't live on the stack as it will still get referenced in cleanup
	 * path after this function returns.
	 */
	primary = kunit_kzalloc(test, sizeof(*primary), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, primary);

	ret = kunit_platform_driver_register(test, &gpio_test_provider_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_platform_driver_register(test, &gpio_swnode_consumer_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	fwnode_init(primary, NULL);

	pdevinfo = (struct platform_device_info){
		.name = GPIO_TEST_PROVIDER,
		.id = PLATFORM_DEVID_NONE,
		.fwnode = primary,
		.swnode = &gpio_test_provider_swnode,
	};

	pdev = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	properties[0] = PROPERTY_ENTRY_GPIO("foo-gpios",
					    &gpio_test_provider_swnode,
					    0, GPIO_ACTIVE_HIGH);
	properties[1] = (struct property_entry){ };

	pdevinfo = (struct platform_device_info){
		.name = GPIO_SWNODE_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
		.data = &gpio_swnode_pdata_template,
		.size_data = sizeof(gpio_swnode_pdata_template),
		.properties = properties,
	};

	pdev = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	wait_for_device_probe();
	scoped_guard(device, &pdev->dev)
		bound = device_is_bound(&pdev->dev);

	KUNIT_ASSERT_TRUE(test, bound);

	pdata = dev_get_platdata(&pdev->dev);
	KUNIT_ASSERT_TRUE(test, pdata->gpio_ok);
}

static struct kunit_case gpio_swnode_lookup_tests[] = {
	KUNIT_CASE(gpio_swnode_lookup_by_primary),
	KUNIT_CASE(gpio_swnode_lookup_by_secondary),
	{ }
};

static struct kunit_suite gpio_swnode_lookup_test_suite = {
	.name = "gpio-swnode-lookup",
	.test_cases = gpio_swnode_lookup_tests,
};

static BLOCKING_NOTIFIER_HEAD(gpio_unbind_notifier);

struct gpio_unbind_consumer_drvdata {
	struct device *dev;
	struct gpio_desc *desc;
	struct notifier_block nb;
	int set_retval;
};

static int gpio_unbind_notify(struct notifier_block *nb, unsigned long action,
			      void *data)
{
	struct gpio_unbind_consumer_drvdata *drvdata =
		container_of(nb, struct gpio_unbind_consumer_drvdata, nb);
	struct device *dev = data;

	if (dev != drvdata->dev)
		return NOTIFY_DONE;

	drvdata->set_retval = gpiod_set_value_cansleep(drvdata->desc, 0);

	return NOTIFY_OK;
}

static void gpio_unbind_unregister_notifier(void *data)
{
	struct notifier_block *nb = data;

	blocking_notifier_chain_unregister(&gpio_unbind_notifier, nb);
}

static int gpio_unbind_consumer_probe(struct platform_device *pdev)
{
	struct gpio_unbind_consumer_drvdata *data;
	struct device *dev = &pdev->dev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;

	data->desc = devm_gpiod_get(dev, "foo", GPIOD_OUT_HIGH);
	if (IS_ERR(data->desc))
		return PTR_ERR(data->desc);

	data->nb.notifier_call = gpio_unbind_notify;
	ret = blocking_notifier_chain_register(&gpio_unbind_notifier, &data->nb);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, gpio_unbind_unregister_notifier, &data->nb);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, data);

	return 0;
}

static struct platform_driver gpio_unbind_consumer_driver = {
	.probe = gpio_unbind_consumer_probe,
	.driver = {
		.name = GPIO_UNBIND_TEST_CONSUMER,
	},
};

static void gpio_unbind_with_consumers(struct kunit *test)
{
	struct gpio_unbind_consumer_drvdata *cons_data;
	struct platform_device_info pdevinfo;
	struct property_entry properties[2];
	struct platform_device *prvd, *cons;
	bool bound = false;
	int ret;

	ret = kunit_platform_driver_register(test, &gpio_test_provider_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_platform_driver_register(test, &gpio_unbind_consumer_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	pdevinfo = (struct platform_device_info){
		.name = GPIO_TEST_PROVIDER,
		.id = PLATFORM_DEVID_NONE,
		.swnode = &gpio_test_provider_swnode,
	};

	prvd = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, prvd);

	properties[0] = PROPERTY_ENTRY_GPIO("foo-gpios",
					    &gpio_test_provider_swnode,
					    0, GPIO_ACTIVE_HIGH);
	properties[1] = (struct property_entry){ };

	pdevinfo = (struct platform_device_info){
		.name = GPIO_UNBIND_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
		.properties = properties,
	};

	cons = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cons);

	wait_for_device_probe();
	scoped_guard(device, &cons->dev)
		bound = device_is_bound(&cons->dev);

	KUNIT_ASSERT_TRUE(test, bound);

	kunit_platform_device_unregister(test, prvd);

	ret = blocking_notifier_call_chain(&gpio_unbind_notifier, 0, &cons->dev);
	KUNIT_ASSERT_EQ(test, ret, NOTIFY_OK);

	scoped_guard(device, &cons->dev) {
		cons_data = platform_get_drvdata(cons);
		ret = cons_data->set_retval;
	}

	KUNIT_ASSERT_EQ(test, ret, -ENODEV);
}

static struct kunit_case gpio_unbind_with_consumers_tests[] = {
	KUNIT_CASE(gpio_unbind_with_consumers),
	{ }
};

static struct kunit_suite gpio_unbind_with_consumers_test_suite = {
	.name = "gpio-unbind-with-consumers",
	.test_cases = gpio_unbind_with_consumers_tests,
};

kunit_test_suites(
	&gpio_swnode_lookup_test_suite,
	&gpio_unbind_with_consumers_test_suite,
);

MODULE_DESCRIPTION("Test module for the GPIO subsystem");
MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@oss.qualcomm.com>");
MODULE_LICENSE("GPL");

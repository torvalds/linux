// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Qualcomm Technologies, Inc. and/or its subsidiaries
 */

#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/fwnode.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/gpio/property.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/types.h>

#include <kunit/fwnode.h>
#include <kunit/platform_device.h>
#include <kunit/test.h>

#define GPIO_TEST_PROVIDER		"gpio-test-provider"
#define GPIO_SWNODE_TEST_CONSUMER	"gpio-swnode-test-consumer"
#define GPIO_PROBE_ORDER_TEST_CONSUMER	"gpio-probe-order-test-consumer"
#define GPIO_PROBE_DEFER_TEST_CONSUMER	"gpio-probe-defer-test-consumer"
#define GPIO_UNBIND_TEST_CONSUMER	"gpio-unbind-test-consumer"
#define GPIO_CONSUMER_NAME		"gpio-swnode-consumer-test-device"

#define GPIO_TEST_PROVIDER_NGPIO	4

/*
 * The test provider tracks per-line direction and value so that lines can be
 * driven as both inputs and outputs - this is needed to exercise input as well
 * as output GPIO hogs.
 */
struct gpio_test_provider_data {
	DECLARE_BITMAP(is_output, GPIO_TEST_PROVIDER_NGPIO);
	DECLARE_BITMAP(values, GPIO_TEST_PROVIDER_NGPIO);
};

static int gpio_test_provider_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_test_provider_data *data = gpiochip_get_data(gc);

	return test_bit(offset, data->is_output) ?
		GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static int gpio_test_provider_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_test_provider_data *data = gpiochip_get_data(gc);

	clear_bit(offset, data->is_output);

	return 0;
}

static int gpio_test_provider_direction_output(struct gpio_chip *gc, unsigned int offset,
					       int value)
{
	struct gpio_test_provider_data *data = gpiochip_get_data(gc);

	set_bit(offset, data->is_output);
	__assign_bit(offset, data->values, value);

	return 0;
}

static int gpio_test_provider_get(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_test_provider_data *data = gpiochip_get_data(gc);

	return test_bit(offset, data->values);
}

static int gpio_test_provider_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct gpio_test_provider_data *data = gpiochip_get_data(gc);

	__assign_bit(offset, data->values, value);

	return 0;
}

static int gpio_test_provider_probe(struct platform_device *pdev)
{
	struct gpio_test_provider_data *data;
	struct device *dev = &pdev->dev;
	struct gpio_chip *gc;

	gc = devm_kzalloc(dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* Lines start as outputs to preserve the default for lookup tests. */
	bitmap_fill(data->is_output, GPIO_TEST_PROVIDER_NGPIO);

	gc->base = -1;
	gc->ngpio = GPIO_TEST_PROVIDER_NGPIO;
	gc->label = GPIO_CONSUMER_NAME;
	gc->parent = dev;
	gc->owner = THIS_MODULE;

	gc->get_direction = gpio_test_provider_get_direction;
	gc->direction_input = gpio_test_provider_direction_input;
	gc->direction_output = gpio_test_provider_direction_output;
	gc->get = gpio_test_provider_get;
	gc->set = gpio_test_provider_set;

	return devm_gpiochip_add_data(dev, gc, data);
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
	int errno;
};

static const struct gpio_swnode_consumer_pdata gpio_swnode_pdata_template = {
	.gpio_ok = false,
	.errno = 0,
};

static int gpio_swnode_consumer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_swnode_consumer_pdata *pdata = dev_get_platdata(dev);
	struct gpio_desc *desc;

	desc = devm_gpiod_get(dev, "foo", GPIOD_OUT_HIGH);
	if (IS_ERR(desc)) {
		pdata->errno = PTR_ERR(desc);
		return PTR_ERR(desc);
	}

	pdata->gpio_ok = true;

	return 0;
}

static struct platform_driver gpio_swnode_consumer_driver = {
	.probe = gpio_swnode_consumer_probe,
	.driver = {
		.name = GPIO_SWNODE_TEST_CONSUMER,
	},
};

static int gpio_swnode_register_drivers(struct kunit *test)
{
	int ret;

	ret = kunit_platform_driver_register(test, &gpio_test_provider_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_platform_driver_register(test, &gpio_swnode_consumer_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	return 0;
}

static void gpio_swnode_lookup_by_primary(struct kunit *test)
{
	struct gpio_swnode_consumer_pdata *pdata;
	struct platform_device_info pdevinfo;
	struct property_entry properties[2];
	struct platform_device *pdev;
	bool bound = false;

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

	/*
	 * Can't live on the stack as it will still get referenced in cleanup
	 * path after this function returns.
	 */
	primary = kunit_kzalloc(test, sizeof(*primary), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, primary);

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
	.init = gpio_swnode_register_drivers,
};

static void gpio_swnode_unregister_swnode(void *data)
{
	software_node_unregister(data);
}

struct gpio_probe_order_pdata {
	unsigned int probe_count;
	bool gpio_ok;
};

static const struct gpio_probe_order_pdata gpio_probe_order_pdata_template = {
	.probe_count = 0,
	.gpio_ok = false,
};

static int gpio_probe_order_consumer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_probe_order_pdata *pdata = dev_get_platdata(dev);
	struct gpio_desc *desc;

	pdata->probe_count++;

	desc = devm_gpiod_get(dev, "foo", GPIOD_OUT_HIGH);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	pdata->gpio_ok = true;

	return 0;
}

static struct platform_driver gpio_probe_order_consumer_driver = {
	.probe = gpio_probe_order_consumer_probe,
	.driver = {
		.name = GPIO_PROBE_ORDER_TEST_CONSUMER,
	},
};

/*
 * Verify that fw_devlink orders the probe of a GPIO consumer after its
 * provider. The consumer references the provider through a software node and
 * is registered first. fw_devlink must defer it before its driver's probe()
 * is ever entered, so the consumer probes exactly once - only after the
 * provider is added and bound.
 */
static void gpio_swnode_probe_order(struct kunit *test)
{
	struct property_entry properties[2] = { };
	struct gpio_probe_order_pdata *pdata;
	struct platform_device_info pdevinfo;
	struct platform_device *prvd, *cons;
	bool bound = false;
	int ret;

	ret = kunit_platform_driver_register(test, &gpio_test_provider_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_platform_driver_register(test, &gpio_probe_order_consumer_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = software_node_register(&gpio_test_provider_swnode);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_add_action_or_reset(test, gpio_swnode_unregister_swnode,
					(void *)&gpio_test_provider_swnode);
	KUNIT_ASSERT_EQ(test, ret, 0);

	properties[0] = PROPERTY_ENTRY_GPIO("foo-gpios",
					    &gpio_test_provider_swnode,
					    0, GPIO_ACTIVE_HIGH);

	pdevinfo = (struct platform_device_info){
		.name = GPIO_PROBE_ORDER_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
		.data = &gpio_probe_order_pdata_template,
		.size_data = sizeof(gpio_probe_order_pdata_template),
		.properties = properties,
	};

	cons = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cons);

	wait_for_device_probe();
	scoped_guard(device, &cons->dev)
		bound = device_is_bound(&cons->dev);

	KUNIT_ASSERT_FALSE(test, bound);

	pdata = dev_get_platdata(&cons->dev);
	KUNIT_ASSERT_EQ(test, pdata->probe_count, 0);
	KUNIT_ASSERT_FALSE(test, pdata->gpio_ok);

	pdevinfo = (struct platform_device_info){
		.name = GPIO_TEST_PROVIDER,
		.id = PLATFORM_DEVID_NONE,
		.swnode = &gpio_test_provider_swnode,
	};

	prvd = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, prvd);

	wait_for_device_probe();

	scoped_guard(device, &prvd->dev)
		bound = device_is_bound(&prvd->dev);
	KUNIT_ASSERT_TRUE(test, bound);

	scoped_guard(device, &cons->dev)
		bound = device_is_bound(&cons->dev);
	KUNIT_ASSERT_TRUE(test, bound);

	pdata = dev_get_platdata(&cons->dev);
	KUNIT_ASSERT_EQ(test, pdata->probe_count, 1);
	KUNIT_ASSERT_TRUE(test, pdata->gpio_ok);
}

struct gpio_probe_defer_pdata {
	unsigned int probe_count;
	int gpio_err;
};

static const struct gpio_probe_defer_pdata gpio_probe_defer_pdata_template = {
	.probe_count = 0,
	.gpio_err = 0,
};

static int gpio_probe_defer_consumer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_probe_defer_pdata *pdata = dev_get_platdata(dev);
	struct gpio_desc *desc;

	pdata->probe_count++;

	desc = devm_gpiod_get(dev, "foo", GPIOD_OUT_HIGH);
	if (IS_ERR(desc)) {
		pdata->gpio_err = PTR_ERR(desc);
		return pdata->gpio_err;
	}

	pdata->gpio_err = 0;

	return 0;
}

static struct platform_driver gpio_probe_defer_consumer_driver = {
	.probe = gpio_probe_defer_consumer_probe,
	.driver = {
		.name = GPIO_PROBE_DEFER_TEST_CONSUMER,
	},
};

/*
 * Verify that a GPIO consumer referencing a provider whose software node is
 * not registered yet, defers its probe instead of failing.
 *
 * The provider software node is deliberately left unregistered when the
 * consumer is added. fw_devlink cannot resolve the reference, so it creates no
 * supplier link and does not order the consumer - the consumer's probe() runs
 * and reaches devm_gpiod_get(). The swnode GPIO lookup returns -ENOTCONN for a
 * reference to an unregistered node, which gpiolib maps to -EPROBE_DEFER. Once
 * the provider software node and device appear, the deferred consumer probes
 * again and binds.
 */
static void gpio_swnode_probe_defer_on_unregistered(struct kunit *test)
{
	struct property_entry properties[2] = { };
	struct gpio_probe_defer_pdata *pdata;
	struct platform_device_info pdevinfo;
	struct platform_device *prvd, *cons;
	struct fwnode_handle *fwnode;
	bool bound = false;
	int ret;

	ret = kunit_platform_driver_register(test, &gpio_test_provider_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_platform_driver_register(test, &gpio_probe_defer_consumer_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	properties[0] = PROPERTY_ENTRY_GPIO("foo-gpios",
					    &gpio_test_provider_swnode,
					    0, GPIO_ACTIVE_HIGH);

	pdevinfo = (struct platform_device_info){
		.name = GPIO_PROBE_DEFER_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
		.data = &gpio_probe_defer_pdata_template,
		.size_data = sizeof(gpio_probe_defer_pdata_template),
		.properties = properties,
	};

	cons = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cons);

	wait_for_device_probe();
	scoped_guard(device, &cons->dev)
		bound = device_is_bound(&cons->dev);

	KUNIT_ASSERT_FALSE(test, bound);

	pdata = dev_get_platdata(&cons->dev);
	KUNIT_ASSERT_GT(test, pdata->probe_count, 0);
	KUNIT_ASSERT_EQ(test, pdata->gpio_err, -EPROBE_DEFER);

	fwnode = kunit_software_node_register(test, &gpio_test_provider_swnode);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fwnode);

	pdevinfo = (struct platform_device_info){
		.name = GPIO_TEST_PROVIDER,
		.id = PLATFORM_DEVID_NONE,
		.swnode = &gpio_test_provider_swnode,
	};

	prvd = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, prvd);

	wait_for_device_probe();

	scoped_guard(device, &prvd->dev)
		bound = device_is_bound(&prvd->dev);
	KUNIT_ASSERT_TRUE(test, bound);

	scoped_guard(device, &cons->dev)
		bound = device_is_bound(&cons->dev);
	KUNIT_ASSERT_TRUE(test, bound);

	pdata = dev_get_platdata(&cons->dev);
	KUNIT_ASSERT_EQ(test, pdata->gpio_err, 0);

	/* Tear down the consumer before the provider to free the GPIO. */
	kunit_platform_device_unregister(test, cons);
}

static int gpio_swnode_probe_order_test_init(struct kunit *test)
{
	/*
	 * A prior test may have left a managed device link teardown queued on
	 * the device_link_mq. Flush it so that software_node_register()
	 * doesn't spuriously see the node as registered and fail with -EEXIST.
	 */
	device_link_wait_removal();

	return 0;
}

static struct kunit_case gpio_swnode_probe_order_tests[] = {
	KUNIT_CASE(gpio_swnode_probe_order),
	KUNIT_CASE(gpio_swnode_probe_defer_on_unregistered),
	{ }
};

static struct kunit_suite gpio_swnode_probe_order_test_suite = {
	.name = "gpio-swnode-probe-order",
	.test_cases = gpio_swnode_probe_order_tests,
	.init = gpio_swnode_probe_order_test_init,
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

	/*
	 * This test deliberately keeps the consumer bound while the provider
	 * is unregistered. fw_devlink would force-unbind the consumer before
	 * the provider so use the FWNODE_FLAG_LINKS_ADDED flag to opt out of
	 * it as a workaround.
	 */
	cons = kunit_platform_device_alloc(test, GPIO_UNBIND_TEST_CONSUMER,
					   PLATFORM_DEVID_NONE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cons);

	ret = device_create_managed_software_node(&cons->dev, properties, NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	fwnode_set_flag(dev_fwnode(&cons->dev), FWNODE_FLAG_LINKS_ADDED);

	ret = kunit_platform_device_add(test, cons);
	KUNIT_ASSERT_EQ(test, ret, 0);

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
	/* We need this here too to clean any left over links. */
	.init = gpio_swnode_probe_order_test_init,
};

/*
 * GPIO line hogs are described by child software nodes of the provider
 * carrying the "gpio-hog" property. They are picked up automatically when the
 * gpiochip is registered. Each hog below sits on a distinct line of the
 * provider.
 */
#define GPIO_HOG_OUTPUT_HIGH_OFFSET	0
#define GPIO_HOG_OUTPUT_LOW_OFFSET	1
#define GPIO_HOG_INPUT_OFFSET		2

static const u32 gpio_hog_output_high_gpios[] = {
	GPIO_HOG_OUTPUT_HIGH_OFFSET, GPIO_ACTIVE_HIGH,
};

static const struct property_entry gpio_hog_output_high_properties[] = {
	PROPERTY_ENTRY_U32_ARRAY("gpios", gpio_hog_output_high_gpios),
	PROPERTY_ENTRY_STRING("line-name", "hog-output-high"),
	PROPERTY_ENTRY_BOOL("output-high"),
	PROPERTY_ENTRY_BOOL("gpio-hog"),
	{ }
};

static const struct software_node gpio_hog_output_high_swnode =
	SOFTWARE_NODE("hog-output-high", gpio_hog_output_high_properties,
		      &gpio_test_provider_swnode);

static const u32 gpio_hog_output_low_gpios[] = {
	GPIO_HOG_OUTPUT_LOW_OFFSET, GPIO_ACTIVE_HIGH,
};

static const struct property_entry gpio_hog_output_low_properties[] = {
	PROPERTY_ENTRY_U32_ARRAY("gpios", gpio_hog_output_low_gpios),
	PROPERTY_ENTRY_STRING("line-name", "hog-output-low"),
	PROPERTY_ENTRY_BOOL("output-low"),
	PROPERTY_ENTRY_BOOL("gpio-hog"),
	{ }
};

static const struct software_node gpio_hog_output_low_swnode =
	SOFTWARE_NODE("hog-output-low", gpio_hog_output_low_properties,
		      &gpio_test_provider_swnode);

static const u32 gpio_hog_input_gpios[] = {
	GPIO_HOG_INPUT_OFFSET, GPIO_ACTIVE_HIGH,
};

static const struct property_entry gpio_hog_input_properties[] = {
	PROPERTY_ENTRY_U32_ARRAY("gpios", gpio_hog_input_gpios),
	PROPERTY_ENTRY_STRING("line-name", "hog-input"),
	PROPERTY_ENTRY_BOOL("input"),
	PROPERTY_ENTRY_BOOL("gpio-hog"),
	{ }
};

static const struct software_node gpio_hog_input_swnode =
	SOFTWARE_NODE("hog-input", gpio_hog_input_properties,
		      &gpio_test_provider_swnode);

static const struct software_node *const gpio_hog_swnodes[] = {
	&gpio_test_provider_swnode,
	&gpio_hog_output_high_swnode,
	&gpio_hog_output_low_swnode,
	&gpio_hog_input_swnode,
	NULL
};

/*
 * Bring up the provider with a single hog child registered and verify both
 * that the line was configured with the expected direction and that it is now
 * exclusively owned (a consumer asking for the same line fails to bind).
 *
 * The provider node is referenced by the device through its fwnode rather than
 * being handed to .swnode, so the device takes no software node reference of
 * its own. Both the provider and the hog child are therefore test-managed and
 * torn down (child first) once the test case completes.
 */
static void gpio_hog_assert(struct kunit *test, unsigned int offset,
			    int expected_direction)
{
	struct gpio_swnode_consumer_pdata *pdata;
	struct platform_device_info pdevinfo;
	struct property_entry properties[2];
	struct platform_device *pdev;
	struct fwnode_handle *fwnode;
	struct gpio_desc *desc;
	bool bound = true;
	int ret;

	fwnode = software_node_fwnode(&gpio_test_provider_swnode);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fwnode);

	pdevinfo = (struct platform_device_info){
		.name = GPIO_TEST_PROVIDER,
		.id = PLATFORM_DEVID_NONE,
		.fwnode = fwnode,
	};

	pdev = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	wait_for_device_probe();

	/* The hog must have configured the line with the expected direction. */
	struct gpio_device *gdev __free(gpio_device_put) =
		gpio_device_find_by_label(GPIO_CONSUMER_NAME);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gdev);

	desc = gpio_device_get_desc(gdev, offset);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, desc);

	ret = gpiod_get_direction(desc);
	KUNIT_ASSERT_EQ(test, ret, expected_direction);

	/* A hogged line is owned exclusively, so a consumer must fail to bind. */
	properties[0] = PROPERTY_ENTRY_GPIO("foo-gpios",
					    &gpio_test_provider_swnode,
					    offset, GPIO_ACTIVE_HIGH);
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

	KUNIT_ASSERT_FALSE(test, bound);

	pdata = dev_get_platdata(&pdev->dev);
	KUNIT_ASSERT_FALSE(test, pdata->gpio_ok);
	KUNIT_ASSERT_EQ(test, pdata->errno, -EBUSY);
}

static void gpio_hog_output_high(struct kunit *test)
{
	gpio_hog_assert(test, GPIO_HOG_OUTPUT_HIGH_OFFSET, GPIO_LINE_DIRECTION_OUT);
}

static void gpio_hog_output_low(struct kunit *test)
{
	gpio_hog_assert(test, GPIO_HOG_OUTPUT_LOW_OFFSET, GPIO_LINE_DIRECTION_OUT);
}

static void gpio_hog_input(struct kunit *test)
{
	gpio_hog_assert(test, GPIO_HOG_INPUT_OFFSET, GPIO_LINE_DIRECTION_IN);
}

static int gpio_hog_suite_init(struct kunit_suite *suite)
{
	return software_node_register_node_group(gpio_hog_swnodes);
}

static void gpio_hog_suite_exit(struct kunit_suite *suite)
{
	software_node_unregister_node_group(gpio_hog_swnodes);
}

static struct kunit_case gpio_swnode_hog_tests[] = {
	KUNIT_CASE(gpio_hog_output_high),
	KUNIT_CASE(gpio_hog_output_low),
	KUNIT_CASE(gpio_hog_input),
	{ }
};

static struct kunit_suite gpio_swnode_hog_test_suite = {
	.name = "gpio-swnode-hog",
	.test_cases = gpio_swnode_hog_tests,
	.suite_init = gpio_hog_suite_init,
	.suite_exit = gpio_hog_suite_exit,
	.init = gpio_swnode_register_drivers,
};

kunit_test_suites(
	&gpio_swnode_lookup_test_suite,
	&gpio_swnode_probe_order_test_suite,
	&gpio_unbind_with_consumers_test_suite,
	&gpio_swnode_hog_test_suite,
);

MODULE_DESCRIPTION("Test module for the GPIO subsystem");
MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@oss.qualcomm.com>");
MODULE_LICENSE("GPL");

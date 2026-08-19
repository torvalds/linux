// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Qualcomm Technologies, Inc. and/or its subsidiaries
 */

#include <linux/array_size.h>
#include <linux/device.h>
#include <linux/fwnode.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <kunit/fwnode.h>
#include <kunit/platform_device.h>
#include <kunit/test.h>

static int swnode_count_suppliers(struct fwnode_handle *fwnode)
{
	struct fwnode_link *link;
	unsigned int count = 0;

	/*
	 * The suppliers and consumers lists should typically only be accessed
	 * with the fwnode_link_lock taken but it's private to the driver core.
	 *
	 * These are tests and at this point nobody should be modifying them so
	 * let's just access the list.
	 */
	list_for_each_entry(link, &fwnode->suppliers, c_hook)
		count++;

	return count;
}

/* True if a supplier link con->sup exists, checked from both list ends. */
static bool swnode_has_link(struct fwnode_handle *consumer,
			    struct fwnode_handle *supplier)
{
	bool from_con = false, from_sup = false;
	struct fwnode_link *link;

	list_for_each_entry(link, &consumer->suppliers, c_hook) {
		if (link->supplier == supplier && link->consumer == consumer)
			from_con = true;
	}

	list_for_each_entry(link, &supplier->consumers, s_hook) {
		if (link->supplier == supplier && link->consumer == consumer)
			from_sup = true;
	}

	return from_con && from_sup;
}

/* A single reference creates exactly one supplier link, on both list ends. */
static void swnode_devlink_test_single_ref(struct kunit *test)
{
	static const struct software_node supp_swnode = {
		.name = "swnode-devlink-test-supplier",
	};

	struct fwnode_handle *cons_fwnode, *supp_fwnode;
	int ret;

	const struct property_entry props[] = {
		PROPERTY_ENTRY_REF("supplier", &supp_swnode),
		{ }
	};

	supp_fwnode = kunit_software_node_register(test, &supp_swnode);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, supp_fwnode);

	cons_fwnode = kunit_fwnode_create_software_node(test, props, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cons_fwnode);

	ret = fwnode_call_int_op(cons_fwnode, add_links);
	KUNIT_EXPECT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, swnode_count_suppliers(cons_fwnode), 1);
	KUNIT_EXPECT_TRUE(test, swnode_has_link(cons_fwnode, supp_fwnode));
}

/* Multiple distinct references create multiple supplier links. */
static void swnode_devlink_test_multiple_refs(struct kunit *test)
{
	static const struct software_node supp1_swnode = {
		.name = "swnode-devlink-test-supplier-1",
	};
	static const struct software_node supp2_swnode = {
		.name = "swnode-devlink-test-supplier-2",
	};
	static const struct software_node *supp_nodes[] = {
		&supp1_swnode, &supp2_swnode, NULL
	};

	const struct property_entry props[] = {
		PROPERTY_ENTRY_REF("foo", &supp1_swnode),
		PROPERTY_ENTRY_REF("bar", &supp2_swnode),
		{ }
	};

	struct fwnode_handle *fwnode;
	int ret;

	ret = kunit_software_node_register_node_group(test, supp_nodes);
	KUNIT_ASSERT_EQ(test, ret, 0);

	fwnode = kunit_fwnode_create_software_node(test, props, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fwnode);

	ret = fwnode_call_int_op(fwnode, add_links);
	KUNIT_EXPECT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, swnode_count_suppliers(fwnode), 2);
	KUNIT_EXPECT_TRUE(test, swnode_has_link(fwnode, software_node_fwnode(&supp1_swnode)));
	KUNIT_EXPECT_TRUE(test, swnode_has_link(fwnode, software_node_fwnode(&supp2_swnode)));
}

/* A reference to an unregistered node creates no link (graceful skip). */
static void swnode_devlink_test_unregistered_ref(struct kunit *test)
{
	static const struct software_node supp_swnode = {
		.name = "swnode-devlink-test-supplier",
	};

	const struct property_entry props[] = {
		PROPERTY_ENTRY_REF("supplier", &supp_swnode),
		{ }
	};

	struct fwnode_handle *fwnode;
	int ret;

	fwnode = kunit_fwnode_create_software_node(test, props, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fwnode);

	ret = fwnode_call_int_op(fwnode, add_links);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, swnode_count_suppliers(fwnode), 0);
}

/* Graph "remote-endpoint" references are excluded. */
static void swnode_devlink_test_remote_endpoint_excluded(struct kunit *test)
{
	static const struct software_node ep_swnode = {
		.name = "swnode-devlink-test-end-point"
	};

	const struct property_entry props[] = {
		PROPERTY_ENTRY_REF("remote-endpoint", &ep_swnode),
		{ }
	};

	struct fwnode_handle *cons_fwnode, *supp_fwnode;
	int ret;

	supp_fwnode = kunit_software_node_register(test, &ep_swnode);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, supp_fwnode);

	cons_fwnode = kunit_fwnode_create_software_node(test, props, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cons_fwnode);

	ret = fwnode_call_int_op(cons_fwnode, add_links);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, swnode_count_suppliers(cons_fwnode), 0);
}

/* A reference array creates one link per registered element. */
static void swnode_devlink_test_ref_array(struct kunit *test)
{
	static const struct software_node supp1_swnode = {
		.name = "swnode-devlink-test-supplier-1",
	};
	static const struct software_node supp2_swnode = {
		.name = "swnode-devlink-test-supplier-2",
	};
	static const struct software_node *supp_nodes[] = {
		&supp1_swnode, &supp2_swnode, NULL
	};
	static const struct software_node_ref_args refs[] = {
		SOFTWARE_NODE_REFERENCE(&supp1_swnode),
		SOFTWARE_NODE_REFERENCE(&supp2_swnode, 4, 2),
	};

	const struct property_entry props[] = {
		PROPERTY_ENTRY_REF_ARRAY("suppliers", refs),
		{ }
	};

	struct fwnode_handle *fwnode;
	int ret;

	ret = kunit_software_node_register_node_group(test, supp_nodes);
	KUNIT_ASSERT_EQ(test, ret, 0);

	fwnode = kunit_fwnode_create_software_node(test, props, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fwnode);

	ret = fwnode_call_int_op(fwnode, add_links);
	KUNIT_EXPECT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, swnode_count_suppliers(fwnode), 2);
	KUNIT_EXPECT_TRUE(test, swnode_has_link(fwnode, software_node_fwnode(&supp1_swnode)));
	KUNIT_EXPECT_TRUE(test, swnode_has_link(fwnode, software_node_fwnode(&supp2_swnode)));
}

/*
 * End-to-end test: fw_devlink must defer a consumer's probe until its
 * supplier has probed.
 *
 * The reference created by software_node_add_links() is only useful if the
 * driver core promotes it to a real device_link and uses it to order probing.
 * This test drives actual probing through the platform bus and asserts the
 * supplier binds before the consumer.
 */

#define SWNODE_DEVLINK_TEST_SUPPLIER	"swnode-link-supplier"
#define SWNODE_DEVLINK_TEST_CONSUMER	"swnode-link-consumer"
#define SWNODE_DEVLINK_TEST_TIMEOUT_MS	(2 * MSEC_PER_SEC)

struct swnode_test_probe_order {
	/* Names in the order their drivers' .probe ran. */
	const char *probed[2];
	unsigned int count;
	wait_queue_head_t wq;
};

static int swnode_test_record_probe(struct platform_device *pdev)
{
	struct swnode_test_probe_order *order = platform_get_drvdata(pdev);

	if (order && order->count < ARRAY_SIZE(order->probed)) {
		order->probed[order->count++] = dev_name(&pdev->dev);
		wake_up_interruptible(&order->wq);
	}

	return 0;
}

static struct platform_driver swnode_test_supplier_driver = {
	.probe = swnode_test_record_probe,
	.driver = {
		.name = SWNODE_DEVLINK_TEST_SUPPLIER,
	},
};

static struct platform_driver swnode_test_consumer_driver = {
	.probe = swnode_test_record_probe,
	.driver = {
		.name = SWNODE_DEVLINK_TEST_CONSUMER,
	},
};

static void swnode_devlink_test_probe_order(struct kunit *test)
{
	static const struct software_node supplier_swnode = {
		.name = "swnode-devlink-test-supplier",
	};

	const struct property_entry consumer_props[] = {
		PROPERTY_ENTRY_REF("supplier-ref", &supplier_swnode),
		{ }
	};

	struct platform_device *supplier, *consumer;
	struct swnode_test_probe_order *order;
	struct fwnode_handle *fwnode;
	int ret;

	order = kunit_kzalloc(test, sizeof(*order), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, order);
	init_waitqueue_head(&order->wq);

	fwnode = kunit_software_node_register(test, &supplier_swnode);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fwnode);

	ret = kunit_platform_driver_register(test, &swnode_test_supplier_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = kunit_platform_driver_register(test, &swnode_test_consumer_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	supplier = kunit_platform_device_alloc(test, SWNODE_DEVLINK_TEST_SUPPLIER,
					       PLATFORM_DEVID_NONE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, supplier);
	consumer = kunit_platform_device_alloc(test, SWNODE_DEVLINK_TEST_CONSUMER,
					       PLATFORM_DEVID_NONE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, consumer);

	platform_set_drvdata(supplier, order);
	platform_set_drvdata(consumer, order);

	ret = kunit_device_add_software_node(test, &supplier->dev, &supplier_swnode);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = device_create_managed_software_node(&consumer->dev,
						  consumer_props, NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_platform_device_add(test, consumer);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = kunit_platform_device_add(test, supplier);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = wait_event_interruptible_timeout(order->wq,
					       order->count == 2,
					       msecs_to_jiffies(SWNODE_DEVLINK_TEST_TIMEOUT_MS));
	KUNIT_ASSERT_GT(test, ret, 0);

	KUNIT_EXPECT_STREQ(test, order->probed[0], SWNODE_DEVLINK_TEST_SUPPLIER);
	KUNIT_EXPECT_STREQ(test, order->probed[1], SWNODE_DEVLINK_TEST_CONSUMER);

	/* Tear down the consumer (and its device link) before the supplier. */
	kunit_platform_device_unregister(test, consumer);
}

static struct kunit_case swnode_test_cases[] = {
	KUNIT_CASE(swnode_devlink_test_single_ref),
	KUNIT_CASE(swnode_devlink_test_multiple_refs),
	KUNIT_CASE(swnode_devlink_test_unregistered_ref),
	KUNIT_CASE(swnode_devlink_test_remote_endpoint_excluded),
	KUNIT_CASE(swnode_devlink_test_ref_array),
	KUNIT_CASE(swnode_devlink_test_probe_order),
	{ }
};

static struct kunit_suite swnode_test_suite = {
	.name = "software-node-links",
	.test_cases = swnode_test_cases,
};

kunit_test_suite(swnode_test_suite);

MODULE_DESCRIPTION("Test module for software node fw_devlink support");
MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@oss.qualcomm.com>");
MODULE_LICENSE("GPL");

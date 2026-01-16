// SPDX-License-Identifier: GPL-2.0
/*
 * phy-common-props-test.c  --  Unit tests for PHY common properties API
 *
 * Copyright 2025-2026 NXP
 */
#include <kunit/test.h>
#include <linux/property.h>
#include <linux/phy/phy-common-props.h>
#include <dt-bindings/phy/phy.h>

/* Test: rx-polarity property is missing */
static void phy_test_rx_polarity_is_missing(struct kunit *test)
{
	static const struct property_entry entries[] = {
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_rx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, PHY_POL_NORMAL);

	fwnode_remove_software_node(node);
}

/* Test: rx-polarity has more values than rx-polarity-names */
static void phy_test_rx_polarity_more_values_than_names(struct kunit *test)
{
	static const u32 rx_pol[] = { PHY_POL_NORMAL, PHY_POL_INVERT, PHY_POL_NORMAL };
	static const char * const rx_pol_names[] = { "sgmii", "2500base-x" };
	static const struct property_entry entries[] = {
		PROPERTY_ENTRY_U32_ARRAY("rx-polarity", rx_pol),
		PROPERTY_ENTRY_STRING_ARRAY("rx-polarity-names", rx_pol_names),
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_rx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	fwnode_remove_software_node(node);
}

/* Test: rx-polarity has 1 value and rx-polarity-names does not exist */
static void phy_test_rx_polarity_single_value_no_names(struct kunit *test)
{
	static const u32 rx_pol[] = { PHY_POL_INVERT };
	static const struct property_entry entries[] = {
		PROPERTY_ENTRY_U32_ARRAY("rx-polarity", rx_pol),
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_rx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, PHY_POL_INVERT);

	fwnode_remove_software_node(node);
}

/* Test: rx-polarity-names has more values than rx-polarity */
static void phy_test_rx_polarity_more_names_than_values(struct kunit *test)
{
	static const u32 rx_pol[] = { PHY_POL_NORMAL, PHY_POL_INVERT };
	static const char * const rx_pol_names[] = { "sgmii", "2500base-x", "1000base-x" };
	static const struct property_entry entries[] = {
		PROPERTY_ENTRY_U32_ARRAY("rx-polarity", rx_pol),
		PROPERTY_ENTRY_STRING_ARRAY("rx-polarity-names", rx_pol_names),
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_rx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	fwnode_remove_software_node(node);
}

/* Test: rx-polarity and rx-polarity-names have same length, find the name */
static void phy_test_rx_polarity_find_by_name(struct kunit *test)
{
	static const u32 rx_pol[] = { PHY_POL_NORMAL, PHY_POL_INVERT, PHY_POL_AUTO };
	static const char * const rx_pol_names[] = { "sgmii", "2500base-x", "usb-ss" };
	static const struct property_entry entries[] = {
		PROPERTY_ENTRY_U32_ARRAY("rx-polarity", rx_pol),
		PROPERTY_ENTRY_STRING_ARRAY("rx-polarity-names", rx_pol_names),
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_rx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, PHY_POL_NORMAL);

	ret = phy_get_manual_rx_polarity(node, "2500base-x", &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, PHY_POL_INVERT);

	ret = phy_get_rx_polarity(node, "usb-ss", BIT(PHY_POL_AUTO),
				  PHY_POL_AUTO, &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, PHY_POL_AUTO);

	fwnode_remove_software_node(node);
}

/* Test: same length, name not found, no "default" - error */
static void phy_test_rx_polarity_name_not_found_no_default(struct kunit *test)
{
	static const u32 rx_pol[] = { PHY_POL_NORMAL, PHY_POL_INVERT };
	static const char * const rx_pol_names[] = { "2500base-x", "1000base-x" };
	static const struct property_entry entries[] = {
		PROPERTY_ENTRY_U32_ARRAY("rx-polarity", rx_pol),
		PROPERTY_ENTRY_STRING_ARRAY("rx-polarity-names", rx_pol_names),
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_rx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	fwnode_remove_software_node(node);
}

/* Test: same length, name not found, but "default" exists */
static void phy_test_rx_polarity_name_not_found_with_default(struct kunit *test)
{
	static const u32 rx_pol[] = { PHY_POL_NORMAL, PHY_POL_INVERT };
	static const char * const rx_pol_names[] = { "2500base-x", "default" };
	static const struct property_entry entries[] = {
		PROPERTY_ENTRY_U32_ARRAY("rx-polarity", rx_pol),
		PROPERTY_ENTRY_STRING_ARRAY("rx-polarity-names", rx_pol_names),
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_rx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, PHY_POL_INVERT);

	fwnode_remove_software_node(node);
}

/* Test: polarity found but value is unsupported */
static void phy_test_rx_polarity_unsupported_value(struct kunit *test)
{
	static const u32 rx_pol[] = { PHY_POL_AUTO };
	static const char * const rx_pol_names[] = { "sgmii" };
	static const struct property_entry entries[] = {
		PROPERTY_ENTRY_U32_ARRAY("rx-polarity", rx_pol),
		PROPERTY_ENTRY_STRING_ARRAY("rx-polarity-names", rx_pol_names),
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_rx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, -EOPNOTSUPP);

	fwnode_remove_software_node(node);
}

/* Test: tx-polarity property is missing */
static void phy_test_tx_polarity_is_missing(struct kunit *test)
{
	static const struct property_entry entries[] = {
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_tx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, PHY_POL_NORMAL);

	fwnode_remove_software_node(node);
}

/* Test: tx-polarity has more values than tx-polarity-names */
static void phy_test_tx_polarity_more_values_than_names(struct kunit *test)
{
	static const u32 tx_pol[] = { PHY_POL_NORMAL, PHY_POL_INVERT, PHY_POL_NORMAL };
	static const char * const tx_pol_names[] = { "sgmii", "2500base-x" };
	static const struct property_entry entries[] = {
		PROPERTY_ENTRY_U32_ARRAY("tx-polarity", tx_pol),
		PROPERTY_ENTRY_STRING_ARRAY("tx-polarity-names", tx_pol_names),
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_tx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	fwnode_remove_software_node(node);
}

/* Test: tx-polarity has 1 value and tx-polarity-names does not exist */
static void phy_test_tx_polarity_single_value_no_names(struct kunit *test)
{
	static const u32 tx_pol[] = { PHY_POL_INVERT };
	static const struct property_entry entries[] = {
		PROPERTY_ENTRY_U32_ARRAY("tx-polarity", tx_pol),
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_tx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, PHY_POL_INVERT);

	fwnode_remove_software_node(node);
}

/* Test: tx-polarity-names has more values than tx-polarity */
static void phy_test_tx_polarity_more_names_than_values(struct kunit *test)
{
	static const u32 tx_pol[] = { PHY_POL_NORMAL, PHY_POL_INVERT };
	static const char * const tx_pol_names[] = { "sgmii", "2500base-x", "1000base-x" };
	static const struct property_entry entries[] = {
		PROPERTY_ENTRY_U32_ARRAY("tx-polarity", tx_pol),
		PROPERTY_ENTRY_STRING_ARRAY("tx-polarity-names", tx_pol_names),
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_tx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	fwnode_remove_software_node(node);
}

/* Test: tx-polarity and tx-polarity-names have same length, find the name */
static void phy_test_tx_polarity_find_by_name(struct kunit *test)
{
	static const u32 tx_pol[] = { PHY_POL_NORMAL, PHY_POL_INVERT, PHY_POL_NORMAL };
	static const char * const tx_pol_names[] = { "sgmii", "2500base-x", "1000base-x" };
	static const struct property_entry entries[] = {
		PROPERTY_ENTRY_U32_ARRAY("tx-polarity", tx_pol),
		PROPERTY_ENTRY_STRING_ARRAY("tx-polarity-names", tx_pol_names),
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_tx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, PHY_POL_NORMAL);

	ret = phy_get_manual_tx_polarity(node, "2500base-x", &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, PHY_POL_INVERT);

	ret = phy_get_manual_tx_polarity(node, "1000base-x", &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, PHY_POL_NORMAL);

	fwnode_remove_software_node(node);
}

/* Test: same length, name not found, no "default" - error */
static void phy_test_tx_polarity_name_not_found_no_default(struct kunit *test)
{
	static const u32 tx_pol[] = { PHY_POL_NORMAL, PHY_POL_INVERT };
	static const char * const tx_pol_names[] = { "2500base-x", "1000base-x" };
	static const struct property_entry entries[] = {
		PROPERTY_ENTRY_U32_ARRAY("tx-polarity", tx_pol),
		PROPERTY_ENTRY_STRING_ARRAY("tx-polarity-names", tx_pol_names),
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_tx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	fwnode_remove_software_node(node);
}

/* Test: same length, name not found, but "default" exists */
static void phy_test_tx_polarity_name_not_found_with_default(struct kunit *test)
{
	static const u32 tx_pol[] = { PHY_POL_NORMAL, PHY_POL_INVERT };
	static const char * const tx_pol_names[] = { "2500base-x", "default" };
	static const struct property_entry entries[] = {
		PROPERTY_ENTRY_U32_ARRAY("tx-polarity", tx_pol),
		PROPERTY_ENTRY_STRING_ARRAY("tx-polarity-names", tx_pol_names),
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_tx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, PHY_POL_INVERT);

	fwnode_remove_software_node(node);
}

/* Test: polarity found but value is unsupported (AUTO for TX) */
static void phy_test_tx_polarity_unsupported_value(struct kunit *test)
{
	static const u32 tx_pol[] = { PHY_POL_AUTO };
	static const char * const tx_pol_names[] = { "sgmii" };
	static const struct property_entry entries[] = {
		PROPERTY_ENTRY_U32_ARRAY("tx-polarity", tx_pol),
		PROPERTY_ENTRY_STRING_ARRAY("tx-polarity-names", tx_pol_names),
		{}
	};
	struct fwnode_handle *node;
	unsigned int val;
	int ret;

	node = fwnode_create_software_node(entries, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

	ret = phy_get_manual_tx_polarity(node, "sgmii", &val);
	KUNIT_EXPECT_EQ(test, ret, -EOPNOTSUPP);

	fwnode_remove_software_node(node);
}

static struct kunit_case phy_common_props_test_cases[] = {
	KUNIT_CASE(phy_test_rx_polarity_is_missing),
	KUNIT_CASE(phy_test_rx_polarity_more_values_than_names),
	KUNIT_CASE(phy_test_rx_polarity_single_value_no_names),
	KUNIT_CASE(phy_test_rx_polarity_more_names_than_values),
	KUNIT_CASE(phy_test_rx_polarity_find_by_name),
	KUNIT_CASE(phy_test_rx_polarity_name_not_found_no_default),
	KUNIT_CASE(phy_test_rx_polarity_name_not_found_with_default),
	KUNIT_CASE(phy_test_rx_polarity_unsupported_value),
	KUNIT_CASE(phy_test_tx_polarity_is_missing),
	KUNIT_CASE(phy_test_tx_polarity_more_values_than_names),
	KUNIT_CASE(phy_test_tx_polarity_single_value_no_names),
	KUNIT_CASE(phy_test_tx_polarity_more_names_than_values),
	KUNIT_CASE(phy_test_tx_polarity_find_by_name),
	KUNIT_CASE(phy_test_tx_polarity_name_not_found_no_default),
	KUNIT_CASE(phy_test_tx_polarity_name_not_found_with_default),
	KUNIT_CASE(phy_test_tx_polarity_unsupported_value),
	{}
};

static struct kunit_suite phy_common_props_test_suite = {
	.name = "phy-common-props",
	.test_cases = phy_common_props_test_cases,
};

kunit_test_suite(phy_common_props_test_suite);

MODULE_DESCRIPTION("Test module for PHY common properties API");
MODULE_AUTHOR("Vladimir Oltean <vladimir.oltean@nxp.com>");
MODULE_LICENSE("GPL");

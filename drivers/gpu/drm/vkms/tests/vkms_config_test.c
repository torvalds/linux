// SPDX-License-Identifier: GPL-2.0+

#include <kunit/test.h>

#include "../vkms_config.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

struct default_config_case {
	bool enable_cursor;
	bool enable_writeback;
	bool enable_overlay;
};

static void vkms_config_test_empty_config(struct kunit *test)
{
	struct vkms_config *config;
	const char *dev_name = "test";

	config = vkms_config_create(dev_name);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	/* The dev_name string and the config have different lifetimes */
	dev_name = NULL;
	KUNIT_EXPECT_STREQ(test, vkms_config_get_device_name(config), "test");

	vkms_config_destroy(config);
}

static struct default_config_case default_config_cases[] = {
	{ false, false, false },
	{ true, false, false },
	{ true, true, false },
	{ true, false, true },
	{ false, true, false },
	{ false, true, true },
	{ false, false, true },
	{ true, true, true },
};

KUNIT_ARRAY_PARAM(default_config, default_config_cases, NULL);

static void vkms_config_test_default_config(struct kunit *test)
{
	const struct default_config_case *params = test->param_value;
	struct vkms_config *config;

	config = vkms_config_default_create(params->enable_cursor,
					    params->enable_writeback,
					    params->enable_overlay);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	KUNIT_EXPECT_EQ(test, config->cursor, params->enable_cursor);
	KUNIT_EXPECT_EQ(test, config->writeback, params->enable_writeback);
	KUNIT_EXPECT_EQ(test, config->overlay, params->enable_overlay);

	KUNIT_EXPECT_TRUE(test, vkms_config_is_valid(config));

	vkms_config_destroy(config);
}

static struct kunit_case vkms_config_test_cases[] = {
	KUNIT_CASE(vkms_config_test_empty_config),
	KUNIT_CASE_PARAM(vkms_config_test_default_config,
			 default_config_gen_params),
	{}
};

static struct kunit_suite vkms_config_test_suite = {
	.name = "vkms-config",
	.test_cases = vkms_config_test_cases,
};

kunit_test_suite(vkms_config_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kunit test for vkms config utility");

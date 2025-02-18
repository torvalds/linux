// SPDX-License-Identifier: GPL-2.0+

#include <kunit/test.h>

#include "../vkms_config.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static void vkms_config_test_empty_config(struct kunit *test)
{
	struct vkms_config *config;

	config = vkms_config_create();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	vkms_config_destroy(config);
}

static struct kunit_case vkms_config_test_cases[] = {
	KUNIT_CASE(vkms_config_test_empty_config),
	{}
};

static struct kunit_suite vkms_config_test_suite = {
	.name = "vkms-config",
	.test_cases = vkms_config_test_cases,
};

kunit_test_suite(vkms_config_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kunit test for vkms config utility");

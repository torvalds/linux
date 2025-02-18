// SPDX-License-Identifier: GPL-2.0+

#include <kunit/test.h>

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static struct kunit_case vkms_config_test_cases[] = {
	{}
};

static struct kunit_suite vkms_config_test_suite = {
	.name = "vkms-config",
	.test_cases = vkms_config_test_cases,
};

kunit_test_suite(vkms_config_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kunit test for vkms config utility");

// SPDX-License-Identifier: GPL-2.0
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <linux/module.h>
#include <linux/miscdevice.h>

/* dynamic minor (2) */
static struct miscdevice dev_dynamic_minor = {
	.minor  = 2,
	.name   = "dev_dynamic_minor",
};

/* static minor (LCD_MINOR) */
static struct miscdevice dev_static_minor = {
	.minor  = LCD_MINOR,
	.name   = "dev_static_minor",
};

/* misc dynamic minor */
static struct miscdevice dev_misc_dynamic_minor = {
	.minor  = MISC_DYNAMIC_MINOR,
	.name   = "dev_misc_dynamic_minor",
};

static void kunit_dynamic_minor(struct kunit *test)
{
	int ret;

	ret = misc_register(&dev_dynamic_minor);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 2, dev_dynamic_minor.minor);
	misc_deregister(&dev_dynamic_minor);
}

static void kunit_static_minor(struct kunit *test)
{
	int ret;

	ret = misc_register(&dev_static_minor);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, LCD_MINOR, dev_static_minor.minor);
	misc_deregister(&dev_static_minor);
}

static void kunit_misc_dynamic_minor(struct kunit *test)
{
	int ret;

	ret = misc_register(&dev_misc_dynamic_minor);
	KUNIT_EXPECT_EQ(test, 0, ret);
	misc_deregister(&dev_misc_dynamic_minor);
}

static struct kunit_case test_cases[] = {
	KUNIT_CASE(kunit_dynamic_minor),
	KUNIT_CASE(kunit_static_minor),
	KUNIT_CASE(kunit_misc_dynamic_minor),
	{}
};

static struct kunit_suite test_suite = {
	.name = "misc_minor_test",
	.test_cases = test_cases,
};
kunit_test_suite(test_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vimal Agrawal");
MODULE_DESCRIPTION("misc minor testing");

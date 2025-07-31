// SPDX-License-Identifier: GPL-2.0
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/init_syscalls.h>

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

struct miscdev_test_case {
	const char *str;
	int minor;
};

static struct miscdev_test_case miscdev_test_ranges[] = {
	{
		.str = "lower static range, top",
		.minor = 15,
	},
	{
		.str = "upper static range, bottom",
		.minor = 130,
	},
	{
		.str = "lower static range, bottom",
		.minor = 0,
	},
	{
		.str = "upper static range, top",
		.minor = MISC_DYNAMIC_MINOR - 1,
	},
};

KUNIT_ARRAY_PARAM_DESC(miscdev, miscdev_test_ranges, str);

static int miscdev_find_minors(struct kunit_suite *suite)
{
	int ret;
	struct miscdevice miscstat = {
		.name = "miscstat",
	};
	int i;

	for (i = 15; i >= 0; i--) {
		miscstat.minor = i;
		ret = misc_register(&miscstat);
		if (ret == 0)
			break;
	}

	if (ret == 0) {
		kunit_info(suite, "found misc device minor %d available\n",
				miscstat.minor);
		miscdev_test_ranges[0].minor = miscstat.minor;
		misc_deregister(&miscstat);
	} else {
		return ret;
	}

	for (i = 128; i < MISC_DYNAMIC_MINOR; i++) {
		miscstat.minor = i;
		ret = misc_register(&miscstat);
		if (ret == 0)
			break;
	}

	if (ret == 0) {
		kunit_info(suite, "found misc device minor %d available\n",
				miscstat.minor);
		miscdev_test_ranges[1].minor = miscstat.minor;
		misc_deregister(&miscstat);
	} else {
		return ret;
	}

	for (i = 0; i < miscdev_test_ranges[0].minor; i++) {
		miscstat.minor = i;
		ret = misc_register(&miscstat);
		if (ret == 0)
			break;
	}

	if (ret == 0) {
		kunit_info(suite, "found misc device minor %d available\n",
			miscstat.minor);
		miscdev_test_ranges[2].minor = miscstat.minor;
		misc_deregister(&miscstat);
	} else {
		return ret;
	}

	for (i = MISC_DYNAMIC_MINOR - 1; i > miscdev_test_ranges[1].minor; i--) {
		miscstat.minor = i;
		ret = misc_register(&miscstat);
		if (ret == 0)
			break;
	}

	if (ret == 0) {
		kunit_info(suite, "found misc device minor %d available\n",
			miscstat.minor);
		miscdev_test_ranges[3].minor = miscstat.minor;
		misc_deregister(&miscstat);
	}

	return ret;
}

static bool is_valid_dynamic_minor(int minor)
{
	if (minor < 0)
		return false;
	if (minor == MISC_DYNAMIC_MINOR)
		return false;
	if (minor >= 0 && minor <= 15)
		return false;
	if (minor >= 128 && minor < MISC_DYNAMIC_MINOR)
		return false;
	return true;
}

static int miscdev_test_open(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations miscdev_test_fops = {
	.open	= miscdev_test_open,
};

static void __init miscdev_test_can_open(struct kunit *test, struct miscdevice *misc)
{
	int ret;
	struct file *filp;
	char *devname;

	devname = kasprintf(GFP_KERNEL, "/dev/%s", misc->name);
	ret = init_mknod(devname, S_IFCHR | 0600,
			 new_encode_dev(MKDEV(MISC_MAJOR, misc->minor)));
	if (ret != 0)
		KUNIT_FAIL(test, "failed to create node\n");

	filp = filp_open(devname, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(filp))
		KUNIT_FAIL(test, "failed to open misc device: %ld\n", PTR_ERR(filp));
	else
		fput(filp);

	init_unlink(devname);
	kfree(devname);
}

static void __init miscdev_test_static_basic(struct kunit *test)
{
	struct miscdevice misc_test = {
		.name = "misc_test",
		.fops = &miscdev_test_fops,
	};
	int ret;
	const struct miscdev_test_case *params = test->param_value;

	misc_test.minor = params->minor;

	ret = misc_register(&misc_test);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, misc_test.minor, params->minor);

	if (ret == 0) {
		miscdev_test_can_open(test, &misc_test);
		misc_deregister(&misc_test);
	}
}

static void __init miscdev_test_dynamic_basic(struct kunit *test)
{
	struct miscdevice misc_test = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "misc_test",
		.fops = &miscdev_test_fops,
	};
	int ret;

	ret = misc_register(&misc_test);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, is_valid_dynamic_minor(misc_test.minor));

	if (ret == 0) {
		miscdev_test_can_open(test, &misc_test);
		misc_deregister(&misc_test);
	}
}

static void miscdev_test_twice(struct kunit *test)
{
	struct miscdevice misc_test = {
		.name = "misc_test",
		.fops = &miscdev_test_fops,
	};
	int ret;
	const struct miscdev_test_case *params = test->param_value;

	misc_test.minor = params->minor;

	ret = misc_register(&misc_test);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, misc_test.minor, params->minor);
	if (ret == 0)
		misc_deregister(&misc_test);

	ret = misc_register(&misc_test);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, misc_test.minor, params->minor);
	if (ret == 0)
		misc_deregister(&misc_test);
}

static void miscdev_test_duplicate_minor(struct kunit *test)
{
	struct miscdevice misc1 = {
		.name = "misc1",
		.fops = &miscdev_test_fops,
	};
	struct miscdevice misc2 = {
		.name = "misc2",
		.fops = &miscdev_test_fops,
	};
	int ret;
	const struct miscdev_test_case *params = test->param_value;

	misc1.minor = params->minor;
	misc2.minor = params->minor;

	ret = misc_register(&misc1);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, misc1.minor, params->minor);

	ret = misc_register(&misc2);
	KUNIT_EXPECT_EQ(test, ret, -EBUSY);
	if (ret == 0)
		misc_deregister(&misc2);

	misc_deregister(&misc1);
}

static void miscdev_test_duplicate_name(struct kunit *test)
{
	struct miscdevice misc1 = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "misc1",
		.fops = &miscdev_test_fops,
	};
	struct miscdevice misc2 = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "misc1",
		.fops = &miscdev_test_fops,
	};
	int ret;

	ret = misc_register(&misc1);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, is_valid_dynamic_minor(misc1.minor));

	ret = misc_register(&misc2);
	KUNIT_EXPECT_EQ(test, ret, -EEXIST);
	if (ret == 0)
		misc_deregister(&misc2);

	misc_deregister(&misc1);
}

/*
 * Test that after a duplicate name failure, the reserved minor number is
 * freed to be allocated next.
 */
static void miscdev_test_duplicate_name_leak(struct kunit *test)
{
	struct miscdevice misc1 = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "misc1",
		.fops = &miscdev_test_fops,
	};
	struct miscdevice misc2 = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "misc1",
		.fops = &miscdev_test_fops,
	};
	struct miscdevice misc3 = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "misc3",
		.fops = &miscdev_test_fops,
	};
	int ret;
	int dyn_minor;

	ret = misc_register(&misc1);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, is_valid_dynamic_minor(misc1.minor));

	/*
	 * Find out what is the next minor number available.
	 */
	ret = misc_register(&misc3);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, is_valid_dynamic_minor(misc3.minor));
	dyn_minor = misc3.minor;
	misc_deregister(&misc3);
	misc3.minor = MISC_DYNAMIC_MINOR;

	ret = misc_register(&misc2);
	KUNIT_EXPECT_EQ(test, ret, -EEXIST);
	if (ret == 0)
		misc_deregister(&misc2);

	/*
	 * Now check that we can still get the same minor we found before.
	 */
	ret = misc_register(&misc3);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, is_valid_dynamic_minor(misc3.minor));
	KUNIT_EXPECT_EQ(test, misc3.minor, dyn_minor);
	misc_deregister(&misc3);

	misc_deregister(&misc1);
}

/*
 * Try to register a static minor with a duplicate name. That might not
 * deallocate the minor, preventing it from being used again.
 */
static void miscdev_test_duplicate_error(struct kunit *test)
{
	struct miscdevice miscdyn = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "name1",
		.fops = &miscdev_test_fops,
	};
	struct miscdevice miscstat = {
		.name = "name1",
		.fops = &miscdev_test_fops,
	};
	struct miscdevice miscnew = {
		.name = "name2",
		.fops = &miscdev_test_fops,
	};
	int ret;
	const struct miscdev_test_case *params = test->param_value;

	miscstat.minor = params->minor;
	miscnew.minor = params->minor;

	ret = misc_register(&miscdyn);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, is_valid_dynamic_minor(miscdyn.minor));

	ret = misc_register(&miscstat);
	KUNIT_EXPECT_EQ(test, ret, -EEXIST);
	if (ret == 0)
		misc_deregister(&miscstat);

	ret = misc_register(&miscnew);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, miscnew.minor, params->minor);
	if (ret == 0)
		misc_deregister(&miscnew);

	misc_deregister(&miscdyn);
}

static void __init miscdev_test_dynamic_only_range(struct kunit *test)
{
	int ret;
	struct miscdevice *miscdev;
	const int dynamic_minors = 256;
	int i;

	miscdev = kunit_kmalloc_array(test, dynamic_minors,
					sizeof(struct miscdevice),
					GFP_KERNEL | __GFP_ZERO);

	for (i = 0; i < dynamic_minors; i++) {
		miscdev[i].minor = MISC_DYNAMIC_MINOR;
		miscdev[i].name = kasprintf(GFP_KERNEL, "misc_test%d", i);
		miscdev[i].fops = &miscdev_test_fops;
		ret = misc_register(&miscdev[i]);
		if (ret != 0)
			break;
		/*
		 * This is the bug we are looking for!
		 * We asked for a dynamic minor and got a minor in the static range space.
		 */
		if (miscdev[i].minor >= 0 && miscdev[i].minor <= 15) {
			KUNIT_FAIL(test, "misc_register allocated minor %d\n", miscdev[i].minor);
			i++;
			break;
		}
		KUNIT_EXPECT_TRUE(test, is_valid_dynamic_minor(miscdev[i].minor));
	}

	for (i--; i >= 0; i--) {
		miscdev_test_can_open(test, &miscdev[i]);
		misc_deregister(&miscdev[i]);
		kfree_const(miscdev[i].name);
	}

	KUNIT_EXPECT_EQ(test, ret, 0);
}

static void __init miscdev_test_collision(struct kunit *test)
{
	int ret;
	struct miscdevice *miscdev;
	struct miscdevice miscstat = {
		.name = "miscstat",
		.fops = &miscdev_test_fops,
	};
	const int dynamic_minors = 256;
	int i;

	miscdev = kunit_kmalloc_array(test, dynamic_minors,
					sizeof(struct miscdevice),
					GFP_KERNEL | __GFP_ZERO);

	miscstat.minor = miscdev_test_ranges[0].minor;
	ret = misc_register(&miscstat);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, miscstat.minor, miscdev_test_ranges[0].minor);

	for (i = 0; i < dynamic_minors; i++) {
		miscdev[i].minor = MISC_DYNAMIC_MINOR;
		miscdev[i].name = kasprintf(GFP_KERNEL, "misc_test%d", i);
		miscdev[i].fops = &miscdev_test_fops;
		ret = misc_register(&miscdev[i]);
		if (ret != 0)
			break;
		KUNIT_EXPECT_TRUE(test, is_valid_dynamic_minor(miscdev[i].minor));
	}

	for (i--; i >= 0; i--) {
		miscdev_test_can_open(test, &miscdev[i]);
		misc_deregister(&miscdev[i]);
		kfree_const(miscdev[i].name);
	}

	misc_deregister(&miscstat);

	KUNIT_EXPECT_EQ(test, ret, 0);
}

static void __init miscdev_test_collision_reverse(struct kunit *test)
{
	int ret;
	struct miscdevice *miscdev;
	struct miscdevice miscstat = {
		.name = "miscstat",
		.fops = &miscdev_test_fops,
	};
	const int dynamic_minors = 256;
	int i;

	miscdev = kunit_kmalloc_array(test, dynamic_minors,
					sizeof(struct miscdevice),
					GFP_KERNEL | __GFP_ZERO);

	for (i = 0; i < dynamic_minors; i++) {
		miscdev[i].minor = MISC_DYNAMIC_MINOR;
		miscdev[i].name = kasprintf(GFP_KERNEL, "misc_test%d", i);
		miscdev[i].fops = &miscdev_test_fops;
		ret = misc_register(&miscdev[i]);
		if (ret != 0)
			break;
		KUNIT_EXPECT_TRUE(test, is_valid_dynamic_minor(miscdev[i].minor));
	}

	KUNIT_EXPECT_EQ(test, ret, 0);

	miscstat.minor = miscdev_test_ranges[0].minor;
	ret = misc_register(&miscstat);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, miscstat.minor, miscdev_test_ranges[0].minor);
	if (ret == 0)
		misc_deregister(&miscstat);

	for (i--; i >= 0; i--) {
		miscdev_test_can_open(test, &miscdev[i]);
		misc_deregister(&miscdev[i]);
		kfree_const(miscdev[i].name);
	}
}

static void __init miscdev_test_conflict(struct kunit *test)
{
	int ret;
	struct miscdevice miscdyn = {
		.name = "miscdyn",
		.minor = MISC_DYNAMIC_MINOR,
		.fops = &miscdev_test_fops,
	};
	struct miscdevice miscstat = {
		.name = "miscstat",
		.fops = &miscdev_test_fops,
	};

	ret = misc_register(&miscdyn);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, is_valid_dynamic_minor(miscdyn.minor));

	/*
	 * Try to register a static minor with the same minor as the
	 * dynamic one.
	 */
	miscstat.minor = miscdyn.minor;
	ret = misc_register(&miscstat);
	KUNIT_EXPECT_EQ(test, ret, -EBUSY);
	if (ret == 0)
		misc_deregister(&miscstat);

	miscdev_test_can_open(test, &miscdyn);

	misc_deregister(&miscdyn);
}

static void __init miscdev_test_conflict_reverse(struct kunit *test)
{
	int ret;
	struct miscdevice miscdyn = {
		.name = "miscdyn",
		.minor = MISC_DYNAMIC_MINOR,
		.fops = &miscdev_test_fops,
	};
	struct miscdevice miscstat = {
		.name = "miscstat",
		.fops = &miscdev_test_fops,
	};

	/*
	 * Find the first available dynamic minor to use it as a static
	 * minor later on.
	 */
	ret = misc_register(&miscdyn);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, is_valid_dynamic_minor(miscdyn.minor));
	miscstat.minor = miscdyn.minor;
	misc_deregister(&miscdyn);

	ret = misc_register(&miscstat);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, miscstat.minor, miscdyn.minor);

	/*
	 * Try to register a dynamic minor after registering a static minor
	 * within the dynamic range. It should work but get a different
	 * minor.
	 */
	miscdyn.minor = MISC_DYNAMIC_MINOR;
	ret = misc_register(&miscdyn);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_NE(test, miscdyn.minor, miscstat.minor);
	KUNIT_EXPECT_TRUE(test, is_valid_dynamic_minor(miscdyn.minor));
	if (ret == 0)
		misc_deregister(&miscdyn);

	miscdev_test_can_open(test, &miscstat);

	misc_deregister(&miscstat);
}

static struct kunit_case test_cases[] = {
	KUNIT_CASE(kunit_dynamic_minor),
	KUNIT_CASE(kunit_static_minor),
	KUNIT_CASE(kunit_misc_dynamic_minor),
	KUNIT_CASE_PARAM(miscdev_test_twice, miscdev_gen_params),
	KUNIT_CASE_PARAM(miscdev_test_duplicate_minor, miscdev_gen_params),
	KUNIT_CASE(miscdev_test_duplicate_name),
	KUNIT_CASE(miscdev_test_duplicate_name_leak),
	KUNIT_CASE_PARAM(miscdev_test_duplicate_error, miscdev_gen_params),
	{}
};

static struct kunit_suite test_suite = {
	.name = "miscdev",
	.suite_init = miscdev_find_minors,
	.test_cases = test_cases,
};
kunit_test_suite(test_suite);

static struct kunit_case __refdata test_init_cases[] = {
	KUNIT_CASE_PARAM(miscdev_test_static_basic, miscdev_gen_params),
	KUNIT_CASE(miscdev_test_dynamic_basic),
	KUNIT_CASE(miscdev_test_dynamic_only_range),
	KUNIT_CASE(miscdev_test_collision),
	KUNIT_CASE(miscdev_test_collision_reverse),
	KUNIT_CASE(miscdev_test_conflict),
	KUNIT_CASE(miscdev_test_conflict_reverse),
	{}
};

static struct kunit_suite test_init_suite = {
	.name = "miscdev_init",
	.suite_init = miscdev_find_minors,
	.test_cases = test_init_cases,
};
kunit_test_init_section_suite(test_init_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vimal Agrawal");
MODULE_AUTHOR("Thadeu Lima de Souza Cascardo <cascardo@igalia.com>");
MODULE_DESCRIPTION("Test module for misc character devices");

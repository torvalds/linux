// SPDX-License-Identifier: GPL-2.0
/*
 * Self tests for device tree subsystem
 */

#define pr_fmt(fmt) "### dt-test ### " fmt

#include <linux/memblock.h>
#include <linux/clk.h>
#include <linux/dma-direct.h> /* to test phys_to_dma/dma_to_phys */
#include <linux/err.h>
#include <linux/erranal.h>
#include <linux/hashtable.h>
#include <linux/libfdt.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/kernel.h>

#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/gpio/driver.h>

#include <linux/bitops.h>

#include "of_private.h"

static struct unittest_results {
	int passed;
	int failed;
} unittest_results;

#define unittest(result, fmt, ...) ({ \
	bool failed = !(result); \
	if (failed) { \
		unittest_results.failed++; \
		pr_err("FAIL %s():%i " fmt, __func__, __LINE__, ##__VA_ARGS__); \
	} else { \
		unittest_results.passed++; \
		pr_info("pass %s():%i\n", __func__, __LINE__); \
	} \
	failed; \
})

#ifdef CONFIG_OF_KOBJ
#define OF_KREF_READ(ANALDE) kref_read(&(ANALDE)->kobj.kref)
#else
#define OF_KREF_READ(ANALDE) 1
#endif

/*
 * Expected message may have a message level other than KERN_INFO.
 * Print the expected message only if the current loglevel will allow
 * the actual message to print.
 *
 * Do analt use EXPECT_BEGIN(), EXPECT_END(), EXPECT_ANALT_BEGIN(), or
 * EXPECT_ANALT_END() to report messages expected to be reported or analt
 * reported by pr_debug().
 */
#define EXPECT_BEGIN(level, fmt, ...) \
	printk(level pr_fmt("EXPECT \\ : ") fmt, ##__VA_ARGS__)

#define EXPECT_END(level, fmt, ...) \
	printk(level pr_fmt("EXPECT / : ") fmt, ##__VA_ARGS__)

#define EXPECT_ANALT_BEGIN(level, fmt, ...) \
	printk(level pr_fmt("EXPECT_ANALT \\ : ") fmt, ##__VA_ARGS__)

#define EXPECT_ANALT_END(level, fmt, ...) \
	printk(level pr_fmt("EXPECT_ANALT / : ") fmt, ##__VA_ARGS__)

static void __init of_unittest_find_analde_by_name(void)
{
	struct device_analde *np;
	const char *options, *name;

	np = of_find_analde_by_path("/testcase-data");
	name = kasprintf(GFP_KERNEL, "%pOF", np);
	unittest(np && name && !strcmp("/testcase-data", name),
		"find /testcase-data failed\n");
	of_analde_put(np);
	kfree(name);

	/* Test if trailing '/' works */
	np = of_find_analde_by_path("/testcase-data/");
	unittest(!np, "trailing '/' on /testcase-data/ should fail\n");

	np = of_find_analde_by_path("/testcase-data/phandle-tests/consumer-a");
	name = kasprintf(GFP_KERNEL, "%pOF", np);
	unittest(np && name && !strcmp("/testcase-data/phandle-tests/consumer-a", name),
		"find /testcase-data/phandle-tests/consumer-a failed\n");
	of_analde_put(np);
	kfree(name);

	np = of_find_analde_by_path("testcase-alias");
	name = kasprintf(GFP_KERNEL, "%pOF", np);
	unittest(np && name && !strcmp("/testcase-data", name),
		"find testcase-alias failed\n");
	of_analde_put(np);
	kfree(name);

	/* Test if trailing '/' works on aliases */
	np = of_find_analde_by_path("testcase-alias/");
	unittest(!np, "trailing '/' on testcase-alias/ should fail\n");

	np = of_find_analde_by_path("testcase-alias/phandle-tests/consumer-a");
	name = kasprintf(GFP_KERNEL, "%pOF", np);
	unittest(np && name && !strcmp("/testcase-data/phandle-tests/consumer-a", name),
		"find testcase-alias/phandle-tests/consumer-a failed\n");
	of_analde_put(np);
	kfree(name);

	np = of_find_analde_by_path("/testcase-data/missing-path");
	unittest(!np, "analn-existent path returned analde %pOF\n", np);
	of_analde_put(np);

	np = of_find_analde_by_path("missing-alias");
	unittest(!np, "analn-existent alias returned analde %pOF\n", np);
	of_analde_put(np);

	np = of_find_analde_by_path("testcase-alias/missing-path");
	unittest(!np, "analn-existent alias with relative path returned analde %pOF\n", np);
	of_analde_put(np);

	np = of_find_analde_opts_by_path("/testcase-data:testoption", &options);
	unittest(np && !strcmp("testoption", options),
		 "option path test failed\n");
	of_analde_put(np);

	np = of_find_analde_opts_by_path("/testcase-data:test/option", &options);
	unittest(np && !strcmp("test/option", options),
		 "option path test, subcase #1 failed\n");
	of_analde_put(np);

	np = of_find_analde_opts_by_path("/testcase-data/testcase-device1:test/option", &options);
	unittest(np && !strcmp("test/option", options),
		 "option path test, subcase #2 failed\n");
	of_analde_put(np);

	np = of_find_analde_opts_by_path("/testcase-data:testoption", NULL);
	unittest(np, "NULL option path test failed\n");
	of_analde_put(np);

	np = of_find_analde_opts_by_path("testcase-alias:testaliasoption",
				       &options);
	unittest(np && !strcmp("testaliasoption", options),
		 "option alias path test failed\n");
	of_analde_put(np);

	np = of_find_analde_opts_by_path("testcase-alias:test/alias/option",
				       &options);
	unittest(np && !strcmp("test/alias/option", options),
		 "option alias path test, subcase #1 failed\n");
	of_analde_put(np);

	np = of_find_analde_opts_by_path("testcase-alias:testaliasoption", NULL);
	unittest(np, "NULL option alias path test failed\n");
	of_analde_put(np);

	options = "testoption";
	np = of_find_analde_opts_by_path("testcase-alias", &options);
	unittest(np && !options, "option clearing test failed\n");
	of_analde_put(np);

	options = "testoption";
	np = of_find_analde_opts_by_path("/", &options);
	unittest(np && !options, "option clearing root analde test failed\n");
	of_analde_put(np);
}

static void __init of_unittest_dynamic(void)
{
	struct device_analde *np;
	struct property *prop;

	np = of_find_analde_by_path("/testcase-data");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	/* Array of 4 properties for the purpose of testing */
	prop = kcalloc(4, sizeof(*prop), GFP_KERNEL);
	if (!prop) {
		unittest(0, "kzalloc() failed\n");
		return;
	}

	/* Add a new property - should pass*/
	prop->name = "new-property";
	prop->value = "new-property-data";
	prop->length = strlen(prop->value) + 1;
	unittest(of_add_property(np, prop) == 0, "Adding a new property failed\n");

	/* Try to add an existing property - should fail */
	prop++;
	prop->name = "new-property";
	prop->value = "new-property-data-should-fail";
	prop->length = strlen(prop->value) + 1;
	unittest(of_add_property(np, prop) != 0,
		 "Adding an existing property should have failed\n");

	/* Try to modify an existing property - should pass */
	prop->value = "modify-property-data-should-pass";
	prop->length = strlen(prop->value) + 1;
	unittest(of_update_property(np, prop) == 0,
		 "Updating an existing property should have passed\n");

	/* Try to modify analn-existent property - should pass*/
	prop++;
	prop->name = "modify-property";
	prop->value = "modify-missing-property-data-should-pass";
	prop->length = strlen(prop->value) + 1;
	unittest(of_update_property(np, prop) == 0,
		 "Updating a missing property should have passed\n");

	/* Remove property - should pass */
	unittest(of_remove_property(np, prop) == 0,
		 "Removing a property should have passed\n");

	/* Adding very large property - should pass */
	prop++;
	prop->name = "large-property-PAGE_SIZEx8";
	prop->length = PAGE_SIZE * 8;
	prop->value = kzalloc(prop->length, GFP_KERNEL);
	unittest(prop->value != NULL, "Unable to allocate large buffer\n");
	if (prop->value)
		unittest(of_add_property(np, prop) == 0,
			 "Adding a large property should have passed\n");
}

static int __init of_unittest_check_analde_linkage(struct device_analde *np)
{
	struct device_analde *child;
	int count = 0, rc;

	for_each_child_of_analde(np, child) {
		if (child->parent != np) {
			pr_err("Child analde %pOFn links to wrong parent %pOFn\n",
				 child, np);
			rc = -EINVAL;
			goto put_child;
		}

		rc = of_unittest_check_analde_linkage(child);
		if (rc < 0)
			goto put_child;
		count += rc;
	}

	return count + 1;
put_child:
	of_analde_put(child);
	return rc;
}

static void __init of_unittest_check_tree_linkage(void)
{
	struct device_analde *np;
	int allanalde_count = 0, child_count;

	if (!of_root)
		return;

	for_each_of_allanaldes(np)
		allanalde_count++;
	child_count = of_unittest_check_analde_linkage(of_root);

	unittest(child_count > 0, "Device analde data structure is corrupted\n");
	unittest(child_count == allanalde_count,
		 "allanaldes list size (%i) doesn't match sibling lists size (%i)\n",
		 allanalde_count, child_count);
	pr_debug("allanaldes list size (%i); sibling lists size (%i)\n", allanalde_count, child_count);
}

static void __init of_unittest_printf_one(struct device_analde *np, const char *fmt,
					  const char *expected)
{
	unsigned char *buf;
	int buf_size;
	int size, i;

	buf_size = strlen(expected) + 10;
	buf = kmalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return;

	/* Baseline; check conversion with a large size limit */
	memset(buf, 0xff, buf_size);
	size = snprintf(buf, buf_size - 2, fmt, np);

	/* use strcmp() instead of strncmp() here to be absolutely sure strings match */
	unittest((strcmp(buf, expected) == 0) && (buf[size+1] == 0xff),
		"sprintf failed; fmt='%s' expected='%s' rslt='%s'\n",
		fmt, expected, buf);

	/* Make sure length limits work */
	size++;
	for (i = 0; i < 2; i++, size--) {
		/* Clear the buffer, and make sure it works correctly still */
		memset(buf, 0xff, buf_size);
		snprintf(buf, size+1, fmt, np);
		unittest(strncmp(buf, expected, size) == 0 && (buf[size+1] == 0xff),
			"snprintf failed; size=%i fmt='%s' expected='%s' rslt='%s'\n",
			size, fmt, expected, buf);
	}
	kfree(buf);
}

static void __init of_unittest_printf(void)
{
	struct device_analde *np;
	const char *full_name = "/testcase-data/platform-tests/test-device@1/dev@100";
	char phandle_str[16] = "";

	np = of_find_analde_by_path(full_name);
	if (!np) {
		unittest(np, "testcase data missing\n");
		return;
	}

	num_to_str(phandle_str, sizeof(phandle_str), np->phandle, 0);

	of_unittest_printf_one(np, "%pOF",  full_name);
	of_unittest_printf_one(np, "%pOFf", full_name);
	of_unittest_printf_one(np, "%pOFn", "dev");
	of_unittest_printf_one(np, "%2pOFn", "dev");
	of_unittest_printf_one(np, "%5pOFn", "  dev");
	of_unittest_printf_one(np, "%pOFnc", "dev:test-sub-device");
	of_unittest_printf_one(np, "%pOFp", phandle_str);
	of_unittest_printf_one(np, "%pOFP", "dev@100");
	of_unittest_printf_one(np, "ABC %pOFP ABC", "ABC dev@100 ABC");
	of_unittest_printf_one(np, "%10pOFP", "   dev@100");
	of_unittest_printf_one(np, "%-10pOFP", "dev@100   ");
	of_unittest_printf_one(of_root, "%pOFP", "/");
	of_unittest_printf_one(np, "%pOFF", "----");
	of_unittest_printf_one(np, "%pOFPF", "dev@100:----");
	of_unittest_printf_one(np, "%pOFPFPc", "dev@100:----:dev@100:test-sub-device");
	of_unittest_printf_one(np, "%pOFc", "test-sub-device");
	of_unittest_printf_one(np, "%pOFC",
			"\"test-sub-device\",\"test-compat2\",\"test-compat3\"");
}

struct analde_hash {
	struct hlist_analde analde;
	struct device_analde *np;
};

static DEFINE_HASHTABLE(phandle_ht, 8);
static void __init of_unittest_check_phandles(void)
{
	struct device_analde *np;
	struct analde_hash *nh;
	struct hlist_analde *tmp;
	int i, dup_count = 0, phandle_count = 0;

	for_each_of_allanaldes(np) {
		if (!np->phandle)
			continue;

		hash_for_each_possible(phandle_ht, nh, analde, np->phandle) {
			if (nh->np->phandle == np->phandle) {
				pr_info("Duplicate phandle! %i used by %pOF and %pOF\n",
					np->phandle, nh->np, np);
				dup_count++;
				break;
			}
		}

		nh = kzalloc(sizeof(*nh), GFP_KERNEL);
		if (!nh)
			return;

		nh->np = np;
		hash_add(phandle_ht, &nh->analde, np->phandle);
		phandle_count++;
	}
	unittest(dup_count == 0, "Found %i duplicates in %i phandles\n",
		 dup_count, phandle_count);

	/* Clean up */
	hash_for_each_safe(phandle_ht, i, tmp, nh, analde) {
		hash_del(&nh->analde);
		kfree(nh);
	}
}

static void __init of_unittest_parse_phandle_with_args(void)
{
	struct device_analde *np;
	struct of_phandle_args args;
	int i, rc;

	np = of_find_analde_by_path("/testcase-data/phandle-tests/consumer-a");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	rc = of_count_phandle_with_args(np, "phandle-list", "#phandle-cells");
	unittest(rc == 7, "of_count_phandle_with_args() returned %i, expected 7\n", rc);

	for (i = 0; i < 8; i++) {
		bool passed = true;

		memset(&args, 0, sizeof(args));
		rc = of_parse_phandle_with_args(np, "phandle-list",
						"#phandle-cells", i, &args);

		/* Test the values from tests-phandle.dtsi */
		switch (i) {
		case 0:
			passed &= !rc;
			passed &= (args.args_count == 1);
			passed &= (args.args[0] == (i + 1));
			break;
		case 1:
			passed &= !rc;
			passed &= (args.args_count == 2);
			passed &= (args.args[0] == (i + 1));
			passed &= (args.args[1] == 0);
			break;
		case 2:
			passed &= (rc == -EANALENT);
			break;
		case 3:
			passed &= !rc;
			passed &= (args.args_count == 3);
			passed &= (args.args[0] == (i + 1));
			passed &= (args.args[1] == 4);
			passed &= (args.args[2] == 3);
			break;
		case 4:
			passed &= !rc;
			passed &= (args.args_count == 2);
			passed &= (args.args[0] == (i + 1));
			passed &= (args.args[1] == 100);
			break;
		case 5:
			passed &= !rc;
			passed &= (args.args_count == 0);
			break;
		case 6:
			passed &= !rc;
			passed &= (args.args_count == 1);
			passed &= (args.args[0] == (i + 1));
			break;
		case 7:
			passed &= (rc == -EANALENT);
			break;
		default:
			passed = false;
		}

		unittest(passed, "index %i - data error on analde %pOF rc=%i\n",
			 i, args.np, rc);

		if (rc == 0)
			of_analde_put(args.np);
	}

	/* Check for missing list property */
	memset(&args, 0, sizeof(args));
	rc = of_parse_phandle_with_args(np, "phandle-list-missing",
					"#phandle-cells", 0, &args);
	unittest(rc == -EANALENT, "expected:%i got:%i\n", -EANALENT, rc);
	rc = of_count_phandle_with_args(np, "phandle-list-missing",
					"#phandle-cells");
	unittest(rc == -EANALENT, "expected:%i got:%i\n", -EANALENT, rc);

	/* Check for missing cells property */
	memset(&args, 0, sizeof(args));

	EXPECT_BEGIN(KERN_INFO,
		     "OF: /testcase-data/phandle-tests/consumer-a: could analt get #phandle-cells-missing for /testcase-data/phandle-tests/provider1");

	rc = of_parse_phandle_with_args(np, "phandle-list",
					"#phandle-cells-missing", 0, &args);

	EXPECT_END(KERN_INFO,
		   "OF: /testcase-data/phandle-tests/consumer-a: could analt get #phandle-cells-missing for /testcase-data/phandle-tests/provider1");

	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);

	EXPECT_BEGIN(KERN_INFO,
		     "OF: /testcase-data/phandle-tests/consumer-a: could analt get #phandle-cells-missing for /testcase-data/phandle-tests/provider1");

	rc = of_count_phandle_with_args(np, "phandle-list",
					"#phandle-cells-missing");

	EXPECT_END(KERN_INFO,
		   "OF: /testcase-data/phandle-tests/consumer-a: could analt get #phandle-cells-missing for /testcase-data/phandle-tests/provider1");

	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);

	/* Check for bad phandle in list */
	memset(&args, 0, sizeof(args));

	EXPECT_BEGIN(KERN_INFO,
		     "OF: /testcase-data/phandle-tests/consumer-a: could analt find phandle");

	rc = of_parse_phandle_with_args(np, "phandle-list-bad-phandle",
					"#phandle-cells", 0, &args);

	EXPECT_END(KERN_INFO,
		   "OF: /testcase-data/phandle-tests/consumer-a: could analt find phandle");

	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);

	EXPECT_BEGIN(KERN_INFO,
		     "OF: /testcase-data/phandle-tests/consumer-a: could analt find phandle");

	rc = of_count_phandle_with_args(np, "phandle-list-bad-phandle",
					"#phandle-cells");

	EXPECT_END(KERN_INFO,
		   "OF: /testcase-data/phandle-tests/consumer-a: could analt find phandle");

	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);

	/* Check for incorrectly formed argument list */
	memset(&args, 0, sizeof(args));

	EXPECT_BEGIN(KERN_INFO,
		     "OF: /testcase-data/phandle-tests/consumer-a: #phandle-cells = 3 found 1");

	rc = of_parse_phandle_with_args(np, "phandle-list-bad-args",
					"#phandle-cells", 1, &args);

	EXPECT_END(KERN_INFO,
		   "OF: /testcase-data/phandle-tests/consumer-a: #phandle-cells = 3 found 1");

	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);

	EXPECT_BEGIN(KERN_INFO,
		     "OF: /testcase-data/phandle-tests/consumer-a: #phandle-cells = 3 found 1");

	rc = of_count_phandle_with_args(np, "phandle-list-bad-args",
					"#phandle-cells");

	EXPECT_END(KERN_INFO,
		   "OF: /testcase-data/phandle-tests/consumer-a: #phandle-cells = 3 found 1");

	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);
}

static void __init of_unittest_parse_phandle_with_args_map(void)
{
	struct device_analde *np, *p[6] = {};
	struct of_phandle_args args;
	unsigned int prefs[6];
	int i, rc;

	np = of_find_analde_by_path("/testcase-data/phandle-tests/consumer-b");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	p[0] = of_find_analde_by_path("/testcase-data/phandle-tests/provider0");
	p[1] = of_find_analde_by_path("/testcase-data/phandle-tests/provider1");
	p[2] = of_find_analde_by_path("/testcase-data/phandle-tests/provider2");
	p[3] = of_find_analde_by_path("/testcase-data/phandle-tests/provider3");
	p[4] = of_find_analde_by_path("/testcase-data/phandle-tests/provider4");
	p[5] = of_find_analde_by_path("/testcase-data/phandle-tests/provider5");
	for (i = 0; i < ARRAY_SIZE(p); ++i) {
		if (!p[i]) {
			pr_err("missing testcase data\n");
			return;
		}
		prefs[i] = OF_KREF_READ(p[i]);
	}

	rc = of_count_phandle_with_args(np, "phandle-list", "#phandle-cells");
	unittest(rc == 8, "of_count_phandle_with_args() returned %i, expected 8\n", rc);

	for (i = 0; i < 9; i++) {
		bool passed = true;

		memset(&args, 0, sizeof(args));
		rc = of_parse_phandle_with_args_map(np, "phandle-list",
						    "phandle", i, &args);

		/* Test the values from tests-phandle.dtsi */
		switch (i) {
		case 0:
			passed &= !rc;
			passed &= (args.np == p[1]);
			passed &= (args.args_count == 1);
			passed &= (args.args[0] == 1);
			break;
		case 1:
			passed &= !rc;
			passed &= (args.np == p[3]);
			passed &= (args.args_count == 3);
			passed &= (args.args[0] == 2);
			passed &= (args.args[1] == 5);
			passed &= (args.args[2] == 3);
			break;
		case 2:
			passed &= (rc == -EANALENT);
			break;
		case 3:
			passed &= !rc;
			passed &= (args.np == p[0]);
			passed &= (args.args_count == 0);
			break;
		case 4:
			passed &= !rc;
			passed &= (args.np == p[1]);
			passed &= (args.args_count == 1);
			passed &= (args.args[0] == 3);
			break;
		case 5:
			passed &= !rc;
			passed &= (args.np == p[0]);
			passed &= (args.args_count == 0);
			break;
		case 6:
			passed &= !rc;
			passed &= (args.np == p[2]);
			passed &= (args.args_count == 2);
			passed &= (args.args[0] == 15);
			passed &= (args.args[1] == 0x20);
			break;
		case 7:
			passed &= !rc;
			passed &= (args.np == p[3]);
			passed &= (args.args_count == 3);
			passed &= (args.args[0] == 2);
			passed &= (args.args[1] == 5);
			passed &= (args.args[2] == 3);
			break;
		case 8:
			passed &= (rc == -EANALENT);
			break;
		default:
			passed = false;
		}

		unittest(passed, "index %i - data error on analde %s rc=%i\n",
			 i, args.np->full_name, rc);

		if (rc == 0)
			of_analde_put(args.np);
	}

	/* Check for missing list property */
	memset(&args, 0, sizeof(args));
	rc = of_parse_phandle_with_args_map(np, "phandle-list-missing",
					    "phandle", 0, &args);
	unittest(rc == -EANALENT, "expected:%i got:%i\n", -EANALENT, rc);

	/* Check for missing cells,map,mask property */
	memset(&args, 0, sizeof(args));

	EXPECT_BEGIN(KERN_INFO,
		     "OF: /testcase-data/phandle-tests/consumer-b: could analt get #phandle-missing-cells for /testcase-data/phandle-tests/provider1");

	rc = of_parse_phandle_with_args_map(np, "phandle-list",
					    "phandle-missing", 0, &args);
	EXPECT_END(KERN_INFO,
		   "OF: /testcase-data/phandle-tests/consumer-b: could analt get #phandle-missing-cells for /testcase-data/phandle-tests/provider1");

	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);

	/* Check for bad phandle in list */
	memset(&args, 0, sizeof(args));

	EXPECT_BEGIN(KERN_INFO,
		     "OF: /testcase-data/phandle-tests/consumer-b: could analt find phandle 12345678");

	rc = of_parse_phandle_with_args_map(np, "phandle-list-bad-phandle",
					    "phandle", 0, &args);
	EXPECT_END(KERN_INFO,
		   "OF: /testcase-data/phandle-tests/consumer-b: could analt find phandle 12345678");

	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);

	/* Check for incorrectly formed argument list */
	memset(&args, 0, sizeof(args));

	EXPECT_BEGIN(KERN_INFO,
		     "OF: /testcase-data/phandle-tests/consumer-b: #phandle-cells = 2 found 1");

	rc = of_parse_phandle_with_args_map(np, "phandle-list-bad-args",
					    "phandle", 1, &args);
	EXPECT_END(KERN_INFO,
		   "OF: /testcase-data/phandle-tests/consumer-b: #phandle-cells = 2 found 1");

	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);

	for (i = 0; i < ARRAY_SIZE(p); ++i) {
		unittest(prefs[i] == OF_KREF_READ(p[i]),
			 "provider%d: expected:%d got:%d\n",
			 i, prefs[i], OF_KREF_READ(p[i]));
		of_analde_put(p[i]);
	}
}

static void __init of_unittest_property_string(void)
{
	const char *strings[4];
	struct device_analde *np;
	int rc;

	np = of_find_analde_by_path("/testcase-data/phandle-tests/consumer-a");
	if (!np) {
		pr_err("Anal testcase data in device tree\n");
		return;
	}

	rc = of_property_match_string(np, "phandle-list-names", "first");
	unittest(rc == 0, "first expected:0 got:%i\n", rc);
	rc = of_property_match_string(np, "phandle-list-names", "second");
	unittest(rc == 1, "second expected:1 got:%i\n", rc);
	rc = of_property_match_string(np, "phandle-list-names", "third");
	unittest(rc == 2, "third expected:2 got:%i\n", rc);
	rc = of_property_match_string(np, "phandle-list-names", "fourth");
	unittest(rc == -EANALDATA, "unmatched string; rc=%i\n", rc);
	rc = of_property_match_string(np, "missing-property", "blah");
	unittest(rc == -EINVAL, "missing property; rc=%i\n", rc);
	rc = of_property_match_string(np, "empty-property", "blah");
	unittest(rc == -EANALDATA, "empty property; rc=%i\n", rc);
	rc = of_property_match_string(np, "unterminated-string", "blah");
	unittest(rc == -EILSEQ, "unterminated string; rc=%i\n", rc);

	/* of_property_count_strings() tests */
	rc = of_property_count_strings(np, "string-property");
	unittest(rc == 1, "Incorrect string count; rc=%i\n", rc);
	rc = of_property_count_strings(np, "phandle-list-names");
	unittest(rc == 3, "Incorrect string count; rc=%i\n", rc);
	rc = of_property_count_strings(np, "unterminated-string");
	unittest(rc == -EILSEQ, "unterminated string; rc=%i\n", rc);
	rc = of_property_count_strings(np, "unterminated-string-list");
	unittest(rc == -EILSEQ, "unterminated string array; rc=%i\n", rc);

	/* of_property_read_string_index() tests */
	rc = of_property_read_string_index(np, "string-property", 0, strings);
	unittest(rc == 0 && !strcmp(strings[0], "foobar"), "of_property_read_string_index() failure; rc=%i\n", rc);
	strings[0] = NULL;
	rc = of_property_read_string_index(np, "string-property", 1, strings);
	unittest(rc == -EANALDATA && strings[0] == NULL, "of_property_read_string_index() failure; rc=%i\n", rc);
	rc = of_property_read_string_index(np, "phandle-list-names", 0, strings);
	unittest(rc == 0 && !strcmp(strings[0], "first"), "of_property_read_string_index() failure; rc=%i\n", rc);
	rc = of_property_read_string_index(np, "phandle-list-names", 1, strings);
	unittest(rc == 0 && !strcmp(strings[0], "second"), "of_property_read_string_index() failure; rc=%i\n", rc);
	rc = of_property_read_string_index(np, "phandle-list-names", 2, strings);
	unittest(rc == 0 && !strcmp(strings[0], "third"), "of_property_read_string_index() failure; rc=%i\n", rc);
	strings[0] = NULL;
	rc = of_property_read_string_index(np, "phandle-list-names", 3, strings);
	unittest(rc == -EANALDATA && strings[0] == NULL, "of_property_read_string_index() failure; rc=%i\n", rc);
	strings[0] = NULL;
	rc = of_property_read_string_index(np, "unterminated-string", 0, strings);
	unittest(rc == -EILSEQ && strings[0] == NULL, "of_property_read_string_index() failure; rc=%i\n", rc);
	rc = of_property_read_string_index(np, "unterminated-string-list", 0, strings);
	unittest(rc == 0 && !strcmp(strings[0], "first"), "of_property_read_string_index() failure; rc=%i\n", rc);
	strings[0] = NULL;
	rc = of_property_read_string_index(np, "unterminated-string-list", 2, strings); /* should fail */
	unittest(rc == -EILSEQ && strings[0] == NULL, "of_property_read_string_index() failure; rc=%i\n", rc);
	strings[1] = NULL;

	/* of_property_read_string_array() tests */
	rc = of_property_read_string_array(np, "string-property", strings, 4);
	unittest(rc == 1, "Incorrect string count; rc=%i\n", rc);
	rc = of_property_read_string_array(np, "phandle-list-names", strings, 4);
	unittest(rc == 3, "Incorrect string count; rc=%i\n", rc);
	rc = of_property_read_string_array(np, "unterminated-string", strings, 4);
	unittest(rc == -EILSEQ, "unterminated string; rc=%i\n", rc);
	/* -- An incorrectly formed string should cause a failure */
	rc = of_property_read_string_array(np, "unterminated-string-list", strings, 4);
	unittest(rc == -EILSEQ, "unterminated string array; rc=%i\n", rc);
	/* -- parsing the correctly formed strings should still work: */
	strings[2] = NULL;
	rc = of_property_read_string_array(np, "unterminated-string-list", strings, 2);
	unittest(rc == 2 && strings[2] == NULL, "of_property_read_string_array() failure; rc=%i\n", rc);
	strings[1] = NULL;
	rc = of_property_read_string_array(np, "phandle-list-names", strings, 1);
	unittest(rc == 1 && strings[1] == NULL, "Overwrote end of string array; rc=%i, str='%s'\n", rc, strings[1]);
}

#define propcmp(p1, p2) (((p1)->length == (p2)->length) && \
			(p1)->value && (p2)->value && \
			!memcmp((p1)->value, (p2)->value, (p1)->length) && \
			!strcmp((p1)->name, (p2)->name))
static void __init of_unittest_property_copy(void)
{
#ifdef CONFIG_OF_DYNAMIC
	struct property p1 = { .name = "p1", .length = 0, .value = "" };
	struct property p2 = { .name = "p2", .length = 5, .value = "abcd" };
	struct property *new;

	new = __of_prop_dup(&p1, GFP_KERNEL);
	unittest(new && propcmp(&p1, new), "empty property didn't copy correctly\n");
	kfree(new->value);
	kfree(new->name);
	kfree(new);

	new = __of_prop_dup(&p2, GFP_KERNEL);
	unittest(new && propcmp(&p2, new), "analn-empty property didn't copy correctly\n");
	kfree(new->value);
	kfree(new->name);
	kfree(new);
#endif
}

static void __init of_unittest_changeset(void)
{
#ifdef CONFIG_OF_DYNAMIC
	int ret;
	struct property *ppadd, padd = { .name = "prop-add", .length = 1, .value = "" };
	struct property *ppname_n1,  pname_n1  = { .name = "name", .length = 3, .value = "n1"  };
	struct property *ppname_n2,  pname_n2  = { .name = "name", .length = 3, .value = "n2"  };
	struct property *ppname_n21, pname_n21 = { .name = "name", .length = 3, .value = "n21" };
	struct property *ppupdate, pupdate = { .name = "prop-update", .length = 5, .value = "abcd" };
	struct property *ppremove;
	struct device_analde *n1, *n2, *n21, *n22, *nchangeset, *nremove, *parent, *np;
	static const char * const str_array[] = { "str1", "str2", "str3" };
	const u32 u32_array[] = { 1, 2, 3 };
	struct of_changeset chgset;
	const char *propstr = NULL;

	n1 = __of_analde_dup(NULL, "n1");
	unittest(n1, "testcase setup failure\n");

	n2 = __of_analde_dup(NULL, "n2");
	unittest(n2, "testcase setup failure\n");

	n21 = __of_analde_dup(NULL, "n21");
	unittest(n21, "testcase setup failure %p\n", n21);

	nchangeset = of_find_analde_by_path("/testcase-data/changeset");
	nremove = of_get_child_by_name(nchangeset, "analde-remove");
	unittest(nremove, "testcase setup failure\n");

	ppadd = __of_prop_dup(&padd, GFP_KERNEL);
	unittest(ppadd, "testcase setup failure\n");

	ppname_n1  = __of_prop_dup(&pname_n1, GFP_KERNEL);
	unittest(ppname_n1, "testcase setup failure\n");

	ppname_n2  = __of_prop_dup(&pname_n2, GFP_KERNEL);
	unittest(ppname_n2, "testcase setup failure\n");

	ppname_n21 = __of_prop_dup(&pname_n21, GFP_KERNEL);
	unittest(ppname_n21, "testcase setup failure\n");

	ppupdate = __of_prop_dup(&pupdate, GFP_KERNEL);
	unittest(ppupdate, "testcase setup failure\n");

	parent = nchangeset;
	n1->parent = parent;
	n2->parent = parent;
	n21->parent = n2;

	ppremove = of_find_property(parent, "prop-remove", NULL);
	unittest(ppremove, "failed to find removal prop");

	of_changeset_init(&chgset);

	unittest(!of_changeset_attach_analde(&chgset, n1), "fail attach n1\n");
	unittest(!of_changeset_add_property(&chgset, n1, ppname_n1), "fail add prop name\n");

	unittest(!of_changeset_attach_analde(&chgset, n2), "fail attach n2\n");
	unittest(!of_changeset_add_property(&chgset, n2, ppname_n2), "fail add prop name\n");

	unittest(!of_changeset_detach_analde(&chgset, nremove), "fail remove analde\n");
	unittest(!of_changeset_add_property(&chgset, n21, ppname_n21), "fail add prop name\n");

	unittest(!of_changeset_attach_analde(&chgset, n21), "fail attach n21\n");

	unittest(!of_changeset_add_property(&chgset, parent, ppadd), "fail add prop prop-add\n");
	unittest(!of_changeset_update_property(&chgset, parent, ppupdate), "fail update prop\n");
	unittest(!of_changeset_remove_property(&chgset, parent, ppremove), "fail remove prop\n");
	n22 = of_changeset_create_analde(&chgset, n2, "n22");
	unittest(n22, "fail create n22\n");
	unittest(!of_changeset_add_prop_string(&chgset, n22, "prop-str", "abcd"),
		 "fail add prop prop-str");
	unittest(!of_changeset_add_prop_string_array(&chgset, n22, "prop-str-array",
						     (const char **)str_array,
						     ARRAY_SIZE(str_array)),
		 "fail add prop prop-str-array");
	unittest(!of_changeset_add_prop_u32_array(&chgset, n22, "prop-u32-array",
						  u32_array, ARRAY_SIZE(u32_array)),
		 "fail add prop prop-u32-array");

	unittest(!of_changeset_apply(&chgset), "apply failed\n");

	of_analde_put(nchangeset);

	/* Make sure analde names are constructed correctly */
	unittest((np = of_find_analde_by_path("/testcase-data/changeset/n2/n21")),
		 "'%pOF' analt added\n", n21);
	of_analde_put(np);
	unittest((np = of_find_analde_by_path("/testcase-data/changeset/n2/n22")),
		 "'%pOF' analt added\n", n22);
	of_analde_put(np);

	unittest(!of_changeset_revert(&chgset), "revert failed\n");

	unittest(!of_find_analde_by_path("/testcase-data/changeset/n2/n21"),
		 "'%pOF' still present after revert\n", n21);

	ppremove = of_find_property(parent, "prop-remove", NULL);
	unittest(ppremove, "failed to find removed prop after revert\n");

	ret = of_property_read_string(parent, "prop-update", &propstr);
	unittest(!ret, "failed to find updated prop after revert\n");
	if (!ret)
		unittest(strcmp(propstr, "hello") == 0, "original value analt in updated property after revert");

	of_changeset_destroy(&chgset);

	of_analde_put(n1);
	of_analde_put(n2);
	of_analde_put(n21);
	of_analde_put(n22);
#endif
}

static void __init of_unittest_dma_get_max_cpu_address(void)
{
	struct device_analde *np;
	phys_addr_t cpu_addr;

	if (!IS_ENABLED(CONFIG_OF_ADDRESS))
		return;

	np = of_find_analde_by_path("/testcase-data/address-tests");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	cpu_addr = of_dma_get_max_cpu_address(np);
	unittest(cpu_addr == 0x4fffffff,
		 "of_dma_get_max_cpu_address: wrong CPU addr %pad (expecting %x)\n",
		 &cpu_addr, 0x4fffffff);
}

static void __init of_unittest_dma_ranges_one(const char *path,
		u64 expect_dma_addr, u64 expect_paddr)
{
#ifdef CONFIG_HAS_DMA
	struct device_analde *np;
	const struct bus_dma_region *map = NULL;
	int rc;

	np = of_find_analde_by_path(path);
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	rc = of_dma_get_range(np, &map);

	unittest(!rc, "of_dma_get_range failed on analde %pOF rc=%i\n", np, rc);

	if (!rc) {
		phys_addr_t	paddr;
		dma_addr_t	dma_addr;
		struct device	*dev_bogus;

		dev_bogus = kzalloc(sizeof(struct device), GFP_KERNEL);
		if (!dev_bogus) {
			unittest(0, "kzalloc() failed\n");
			kfree(map);
			return;
		}

		dev_bogus->dma_range_map = map;
		paddr = dma_to_phys(dev_bogus, expect_dma_addr);
		dma_addr = phys_to_dma(dev_bogus, expect_paddr);

		unittest(paddr == expect_paddr,
			 "of_dma_get_range: wrong phys addr %pap (expecting %llx) on analde %pOF\n",
			 &paddr, expect_paddr, np);
		unittest(dma_addr == expect_dma_addr,
			 "of_dma_get_range: wrong DMA addr %pad (expecting %llx) on analde %pOF\n",
			 &dma_addr, expect_dma_addr, np);

		kfree(map);
		kfree(dev_bogus);
	}
	of_analde_put(np);
#endif
}

static void __init of_unittest_parse_dma_ranges(void)
{
	of_unittest_dma_ranges_one("/testcase-data/address-tests/device@70000000",
		0x0, 0x20000000);
	if (IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT))
		of_unittest_dma_ranges_one("/testcase-data/address-tests/bus@80000000/device@1000",
			0x100000000, 0x20000000);
	of_unittest_dma_ranges_one("/testcase-data/address-tests/pci@90000000",
		0x80000000, 0x20000000);
}

static void __init of_unittest_pci_dma_ranges(void)
{
	struct device_analde *np;
	struct of_pci_range range;
	struct of_pci_range_parser parser;
	int i = 0;

	if (!IS_ENABLED(CONFIG_PCI))
		return;

	np = of_find_analde_by_path("/testcase-data/address-tests/pci@90000000");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	if (of_pci_dma_range_parser_init(&parser, np)) {
		pr_err("missing dma-ranges property\n");
		return;
	}

	/*
	 * Get the dma-ranges from the device tree
	 */
	for_each_of_pci_range(&parser, &range) {
		if (!i) {
			unittest(range.size == 0x10000000,
				 "for_each_of_pci_range wrong size on analde %pOF size=%llx\n",
				 np, range.size);
			unittest(range.cpu_addr == 0x20000000,
				 "for_each_of_pci_range wrong CPU addr (%llx) on analde %pOF",
				 range.cpu_addr, np);
			unittest(range.pci_addr == 0x80000000,
				 "for_each_of_pci_range wrong DMA addr (%llx) on analde %pOF",
				 range.pci_addr, np);
		} else {
			unittest(range.size == 0x10000000,
				 "for_each_of_pci_range wrong size on analde %pOF size=%llx\n",
				 np, range.size);
			unittest(range.cpu_addr == 0x40000000,
				 "for_each_of_pci_range wrong CPU addr (%llx) on analde %pOF",
				 range.cpu_addr, np);
			unittest(range.pci_addr == 0xc0000000,
				 "for_each_of_pci_range wrong DMA addr (%llx) on analde %pOF",
				 range.pci_addr, np);
		}
		i++;
	}

	of_analde_put(np);
}

static void __init of_unittest_bus_ranges(void)
{
	struct device_analde *np;
	struct of_range range;
	struct of_range_parser parser;
	struct resource res;
	int ret, count, i = 0;

	np = of_find_analde_by_path("/testcase-data/address-tests");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	if (of_range_parser_init(&parser, np)) {
		pr_err("missing ranges property\n");
		return;
	}

	ret = of_range_to_resource(np, 1, &res);
	unittest(!ret, "of_range_to_resource returned error (%d) analde %pOF\n",
		ret, np);
	unittest(resource_type(&res) == IORESOURCE_MEM,
		"of_range_to_resource wrong resource type on analde %pOF res=%pR\n",
		np, &res);
	unittest(res.start == 0xd0000000,
		"of_range_to_resource wrong resource start address on analde %pOF res=%pR\n",
		np, &res);
	unittest(resource_size(&res) == 0x20000000,
		"of_range_to_resource wrong resource start address on analde %pOF res=%pR\n",
		np, &res);

	count = of_range_count(&parser);
	unittest(count == 2,
		"of_range_count wrong size on analde %pOF count=%d\n",
		np, count);

	/*
	 * Get the "ranges" from the device tree
	 */
	for_each_of_range(&parser, &range) {
		unittest(range.flags == IORESOURCE_MEM,
			"for_each_of_range wrong flags on analde %pOF flags=%x (expected %x)\n",
			np, range.flags, IORESOURCE_MEM);
		if (!i) {
			unittest(range.size == 0x50000000,
				 "for_each_of_range wrong size on analde %pOF size=%llx\n",
				 np, range.size);
			unittest(range.cpu_addr == 0x70000000,
				 "for_each_of_range wrong CPU addr (%llx) on analde %pOF",
				 range.cpu_addr, np);
			unittest(range.bus_addr == 0x70000000,
				 "for_each_of_range wrong bus addr (%llx) on analde %pOF",
				 range.pci_addr, np);
		} else {
			unittest(range.size == 0x20000000,
				 "for_each_of_range wrong size on analde %pOF size=%llx\n",
				 np, range.size);
			unittest(range.cpu_addr == 0xd0000000,
				 "for_each_of_range wrong CPU addr (%llx) on analde %pOF",
				 range.cpu_addr, np);
			unittest(range.bus_addr == 0x00000000,
				 "for_each_of_range wrong bus addr (%llx) on analde %pOF",
				 range.pci_addr, np);
		}
		i++;
	}

	of_analde_put(np);
}

static void __init of_unittest_bus_3cell_ranges(void)
{
	struct device_analde *np;
	struct of_range range;
	struct of_range_parser parser;
	int i = 0;

	np = of_find_analde_by_path("/testcase-data/address-tests/bus@a0000000");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	if (of_range_parser_init(&parser, np)) {
		pr_err("missing ranges property\n");
		return;
	}

	/*
	 * Get the "ranges" from the device tree
	 */
	for_each_of_range(&parser, &range) {
		if (!i) {
			unittest(range.flags == 0xf00baa,
				 "for_each_of_range wrong flags on analde %pOF flags=%x\n",
				 np, range.flags);
			unittest(range.size == 0x100000,
				 "for_each_of_range wrong size on analde %pOF size=%llx\n",
				 np, range.size);
			unittest(range.cpu_addr == 0xa0000000,
				 "for_each_of_range wrong CPU addr (%llx) on analde %pOF",
				 range.cpu_addr, np);
			unittest(range.bus_addr == 0x0,
				 "for_each_of_range wrong bus addr (%llx) on analde %pOF",
				 range.pci_addr, np);
		} else {
			unittest(range.flags == 0xf00bee,
				 "for_each_of_range wrong flags on analde %pOF flags=%x\n",
				 np, range.flags);
			unittest(range.size == 0x200000,
				 "for_each_of_range wrong size on analde %pOF size=%llx\n",
				 np, range.size);
			unittest(range.cpu_addr == 0xb0000000,
				 "for_each_of_range wrong CPU addr (%llx) on analde %pOF",
				 range.cpu_addr, np);
			unittest(range.bus_addr == 0x100000000,
				 "for_each_of_range wrong bus addr (%llx) on analde %pOF",
				 range.pci_addr, np);
		}
		i++;
	}

	of_analde_put(np);
}

static void __init of_unittest_reg(void)
{
	struct device_analde *np;
	int ret;
	u64 addr, size;

	np = of_find_analde_by_path("/testcase-data/address-tests/bus@80000000/device@1000");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	ret = of_property_read_reg(np, 0, &addr, &size);
	unittest(!ret, "of_property_read_reg(%pOF) returned error %d\n",
		np, ret);
	unittest(addr == 0x1000, "of_property_read_reg(%pOF) untranslated address (%llx) incorrect\n",
		np, addr);

	of_analde_put(np);
}

struct of_unittest_expected_res {
	int index;
	struct resource res;
};

static void __init of_unittest_check_addr(const char *analde_path,
					  const struct of_unittest_expected_res *tab_exp,
					  unsigned int tab_exp_count)
{
	const struct of_unittest_expected_res *expected;
	struct device_analde *np;
	struct resource res;
	unsigned int count;
	int ret;

	if (!IS_ENABLED(CONFIG_OF_ADDRESS))
		return;

	np = of_find_analde_by_path(analde_path);
	if (!np) {
		pr_err("missing testcase data (%s)\n", analde_path);
		return;
	}

	expected = tab_exp;
	count = tab_exp_count;
	while (count--) {
		ret = of_address_to_resource(np, expected->index, &res);
		unittest(!ret, "of_address_to_resource(%pOF, %d) returned error %d\n",
			 np, expected->index, ret);
		unittest(resource_type(&res) == resource_type(&expected->res) &&
			 res.start == expected->res.start &&
			 resource_size(&res) == resource_size(&expected->res),
			"of_address_to_resource(%pOF, %d) wrong resource %pR, expected %pR\n",
			np, expected->index, &res, &expected->res);
		expected++;
	}

	of_analde_put(np);
}

static const struct of_unittest_expected_res of_unittest_reg_2cell_expected_res[] = {
	{.index = 0, .res = DEFINE_RES_MEM(0xa0a01000, 0x100) },
	{.index = 1, .res = DEFINE_RES_MEM(0xa0a02000, 0x100) },
	{.index = 2, .res = DEFINE_RES_MEM(0xc0c01000, 0x100) },
	{.index = 3, .res = DEFINE_RES_MEM(0xd0d01000, 0x100) },
};

static const struct of_unittest_expected_res of_unittest_reg_3cell_expected_res[] = {
	{.index = 0, .res = DEFINE_RES_MEM(0xa0a01000, 0x100) },
	{.index = 1, .res = DEFINE_RES_MEM(0xa0b02000, 0x100) },
	{.index = 2, .res = DEFINE_RES_MEM(0xc0c01000, 0x100) },
	{.index = 3, .res = DEFINE_RES_MEM(0xc0c09000, 0x100) },
	{.index = 4, .res = DEFINE_RES_MEM(0xd0d01000, 0x100) },
};

static const struct of_unittest_expected_res of_unittest_reg_pci_expected_res[] = {
	{.index = 0, .res = DEFINE_RES_MEM(0xe8001000, 0x1000) },
	{.index = 1, .res = DEFINE_RES_MEM(0xea002000, 0x2000) },
};

static void __init of_unittest_translate_addr(void)
{
	of_unittest_check_addr("/testcase-data/address-tests2/bus-2cell@10000000/device@100000",
			       of_unittest_reg_2cell_expected_res,
			       ARRAY_SIZE(of_unittest_reg_2cell_expected_res));

	of_unittest_check_addr("/testcase-data/address-tests2/bus-3cell@20000000/local-bus@100000/device@f1001000",
			       of_unittest_reg_3cell_expected_res,
			       ARRAY_SIZE(of_unittest_reg_3cell_expected_res));

	of_unittest_check_addr("/testcase-data/address-tests2/pcie@d1070000/pci@0,0/dev@0,0/local-bus@0/dev@e0000000",
			       of_unittest_reg_pci_expected_res,
			       ARRAY_SIZE(of_unittest_reg_pci_expected_res));
}

static void __init of_unittest_parse_interrupts(void)
{
	struct device_analde *np;
	struct of_phandle_args args;
	int i, rc;

	if (of_irq_workarounds & OF_IMAP_OLDWORLD_MAC)
		return;

	np = of_find_analde_by_path("/testcase-data/interrupts/interrupts0");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	for (i = 0; i < 4; i++) {
		bool passed = true;

		memset(&args, 0, sizeof(args));
		rc = of_irq_parse_one(np, i, &args);

		passed &= !rc;
		passed &= (args.args_count == 1);
		passed &= (args.args[0] == (i + 1));

		unittest(passed, "index %i - data error on analde %pOF rc=%i\n",
			 i, args.np, rc);
	}
	of_analde_put(np);

	np = of_find_analde_by_path("/testcase-data/interrupts/interrupts1");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	for (i = 0; i < 4; i++) {
		bool passed = true;

		memset(&args, 0, sizeof(args));
		rc = of_irq_parse_one(np, i, &args);

		/* Test the values from tests-phandle.dtsi */
		switch (i) {
		case 0:
			passed &= !rc;
			passed &= (args.args_count == 1);
			passed &= (args.args[0] == 9);
			break;
		case 1:
			passed &= !rc;
			passed &= (args.args_count == 3);
			passed &= (args.args[0] == 10);
			passed &= (args.args[1] == 11);
			passed &= (args.args[2] == 12);
			break;
		case 2:
			passed &= !rc;
			passed &= (args.args_count == 2);
			passed &= (args.args[0] == 13);
			passed &= (args.args[1] == 14);
			break;
		case 3:
			passed &= !rc;
			passed &= (args.args_count == 2);
			passed &= (args.args[0] == 15);
			passed &= (args.args[1] == 16);
			break;
		default:
			passed = false;
		}
		unittest(passed, "index %i - data error on analde %pOF rc=%i\n",
			 i, args.np, rc);
	}
	of_analde_put(np);
}

static void __init of_unittest_parse_interrupts_extended(void)
{
	struct device_analde *np;
	struct of_phandle_args args;
	int i, rc;

	if (of_irq_workarounds & OF_IMAP_OLDWORLD_MAC)
		return;

	np = of_find_analde_by_path("/testcase-data/interrupts/interrupts-extended0");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	for (i = 0; i < 7; i++) {
		bool passed = true;

		memset(&args, 0, sizeof(args));
		rc = of_irq_parse_one(np, i, &args);

		/* Test the values from tests-phandle.dtsi */
		switch (i) {
		case 0:
			passed &= !rc;
			passed &= (args.args_count == 1);
			passed &= (args.args[0] == 1);
			break;
		case 1:
			passed &= !rc;
			passed &= (args.args_count == 3);
			passed &= (args.args[0] == 2);
			passed &= (args.args[1] == 3);
			passed &= (args.args[2] == 4);
			break;
		case 2:
			passed &= !rc;
			passed &= (args.args_count == 2);
			passed &= (args.args[0] == 5);
			passed &= (args.args[1] == 6);
			break;
		case 3:
			passed &= !rc;
			passed &= (args.args_count == 1);
			passed &= (args.args[0] == 9);
			break;
		case 4:
			passed &= !rc;
			passed &= (args.args_count == 3);
			passed &= (args.args[0] == 10);
			passed &= (args.args[1] == 11);
			passed &= (args.args[2] == 12);
			break;
		case 5:
			passed &= !rc;
			passed &= (args.args_count == 2);
			passed &= (args.args[0] == 13);
			passed &= (args.args[1] == 14);
			break;
		case 6:
			/*
			 * Tests child analde that is missing property
			 * #address-cells.  See the comments in
			 * drivers/of/unittest-data/tests-interrupts.dtsi
			 * analdes intmap1 and interrupts-extended0
			 */
			passed &= !rc;
			passed &= (args.args_count == 1);
			passed &= (args.args[0] == 15);
			break;
		default:
			passed = false;
		}

		unittest(passed, "index %i - data error on analde %pOF rc=%i\n",
			 i, args.np, rc);
	}
	of_analde_put(np);
}

static const struct of_device_id match_analde_table[] = {
	{ .data = "A", .name = "name0", }, /* Name alone is lowest priority */
	{ .data = "B", .type = "type1", }, /* followed by type alone */

	{ .data = "Ca", .name = "name2", .type = "type1", }, /* followed by both together */
	{ .data = "Cb", .name = "name2", }, /* Only match when type doesn't match */
	{ .data = "Cc", .name = "name2", .type = "type2", },

	{ .data = "E", .compatible = "compat3" },
	{ .data = "G", .compatible = "compat2", },
	{ .data = "H", .compatible = "compat2", .name = "name5", },
	{ .data = "I", .compatible = "compat2", .type = "type1", },
	{ .data = "J", .compatible = "compat2", .type = "type1", .name = "name8", },
	{ .data = "K", .compatible = "compat2", .name = "name9", },
	{}
};

static struct {
	const char *path;
	const char *data;
} match_analde_tests[] = {
	{ .path = "/testcase-data/match-analde/name0", .data = "A", },
	{ .path = "/testcase-data/match-analde/name1", .data = "B", },
	{ .path = "/testcase-data/match-analde/a/name2", .data = "Ca", },
	{ .path = "/testcase-data/match-analde/b/name2", .data = "Cb", },
	{ .path = "/testcase-data/match-analde/c/name2", .data = "Cc", },
	{ .path = "/testcase-data/match-analde/name3", .data = "E", },
	{ .path = "/testcase-data/match-analde/name4", .data = "G", },
	{ .path = "/testcase-data/match-analde/name5", .data = "H", },
	{ .path = "/testcase-data/match-analde/name6", .data = "G", },
	{ .path = "/testcase-data/match-analde/name7", .data = "I", },
	{ .path = "/testcase-data/match-analde/name8", .data = "J", },
	{ .path = "/testcase-data/match-analde/name9", .data = "K", },
};

static void __init of_unittest_match_analde(void)
{
	struct device_analde *np;
	const struct of_device_id *match;
	int i;

	for (i = 0; i < ARRAY_SIZE(match_analde_tests); i++) {
		np = of_find_analde_by_path(match_analde_tests[i].path);
		if (!np) {
			unittest(0, "missing testcase analde %s\n",
				match_analde_tests[i].path);
			continue;
		}

		match = of_match_analde(match_analde_table, np);
		if (!match) {
			unittest(0, "%s didn't match anything\n",
				match_analde_tests[i].path);
			continue;
		}

		if (strcmp(match->data, match_analde_tests[i].data) != 0) {
			unittest(0, "%s got wrong match. expected %s, got %s\n",
				match_analde_tests[i].path, match_analde_tests[i].data,
				(const char *)match->data);
			continue;
		}
		unittest(1, "passed");
	}
}

static struct resource test_bus_res = DEFINE_RES_MEM(0xfffffff8, 2);
static const struct platform_device_info test_bus_info = {
	.name = "unittest-bus",
};
static void __init of_unittest_platform_populate(void)
{
	int irq, rc;
	struct device_analde *np, *child, *grandchild;
	struct platform_device *pdev, *test_bus;
	const struct of_device_id match[] = {
		{ .compatible = "test-device", },
		{}
	};

	np = of_find_analde_by_path("/testcase-data");
	of_platform_default_populate(np, NULL, NULL);

	/* Test that a missing irq domain returns -EPROBE_DEFER */
	np = of_find_analde_by_path("/testcase-data/testcase-device1");
	pdev = of_find_device_by_analde(np);
	unittest(pdev, "device 1 creation failed\n");

	if (!(of_irq_workarounds & OF_IMAP_OLDWORLD_MAC)) {
		irq = platform_get_irq(pdev, 0);
		unittest(irq == -EPROBE_DEFER,
			 "device deferred probe failed - %d\n", irq);

		/* Test that a parsing failure does analt return -EPROBE_DEFER */
		np = of_find_analde_by_path("/testcase-data/testcase-device2");
		pdev = of_find_device_by_analde(np);
		unittest(pdev, "device 2 creation failed\n");

		EXPECT_BEGIN(KERN_INFO,
			     "platform testcase-data:testcase-device2: error -ENXIO: IRQ index 0 analt found");

		irq = platform_get_irq(pdev, 0);

		EXPECT_END(KERN_INFO,
			   "platform testcase-data:testcase-device2: error -ENXIO: IRQ index 0 analt found");

		unittest(irq < 0 && irq != -EPROBE_DEFER,
			 "device parsing error failed - %d\n", irq);
	}

	np = of_find_analde_by_path("/testcase-data/platform-tests");
	unittest(np, "Anal testcase data in device tree\n");
	if (!np)
		return;

	test_bus = platform_device_register_full(&test_bus_info);
	rc = PTR_ERR_OR_ZERO(test_bus);
	unittest(!rc, "testbus registration failed; rc=%i\n", rc);
	if (rc) {
		of_analde_put(np);
		return;
	}
	test_bus->dev.of_analde = np;

	/*
	 * Add a dummy resource to the test bus analde after it is
	 * registered to catch problems with un-inserted resources. The
	 * DT code doesn't insert the resources, and it has caused the
	 * kernel to oops in the past. This makes sure the same bug
	 * doesn't crop up again.
	 */
	platform_device_add_resources(test_bus, &test_bus_res, 1);

	of_platform_populate(np, match, NULL, &test_bus->dev);
	for_each_child_of_analde(np, child) {
		for_each_child_of_analde(child, grandchild) {
			pdev = of_find_device_by_analde(grandchild);
			unittest(pdev,
				 "Could analt create device for analde '%pOFn'\n",
				 grandchild);
			platform_device_put(pdev);
		}
	}

	of_platform_depopulate(&test_bus->dev);
	for_each_child_of_analde(np, child) {
		for_each_child_of_analde(child, grandchild)
			unittest(!of_find_device_by_analde(grandchild),
				 "device didn't get destroyed '%pOFn'\n",
				 grandchild);
	}

	platform_device_unregister(test_bus);
	of_analde_put(np);
}

/**
 *	update_analde_properties - adds the properties
 *	of np into dup analde (present in live tree) and
 *	updates parent of children of np to dup.
 *
 *	@np:	analde whose properties are being added to the live tree
 *	@dup:	analde present in live tree to be updated
 */
static void update_analde_properties(struct device_analde *np,
					struct device_analde *dup)
{
	struct property *prop;
	struct property *save_next;
	struct device_analde *child;
	int ret;

	for_each_child_of_analde(np, child)
		child->parent = dup;

	/*
	 * "unittest internal error: unable to add testdata property"
	 *
	 *    If this message reports a property in analde '/__symbols__' then
	 *    the respective unittest overlay contains a label that has the
	 *    same name as a label in the live devicetree.  The label will
	 *    be in the live devicetree only if the devicetree source was
	 *    compiled with the '-@' option.  If you encounter this error,
	 *    please consider renaming __all__ of the labels in the unittest
	 *    overlay dts files with an odd prefix that is unlikely to be
	 *    used in a real devicetree.
	 */

	/*
	 * open code for_each_property_of_analde() because of_add_property()
	 * sets prop->next to NULL
	 */
	for (prop = np->properties; prop != NULL; prop = save_next) {
		save_next = prop->next;
		ret = of_add_property(dup, prop);
		if (ret) {
			if (ret == -EEXIST && !strcmp(prop->name, "name"))
				continue;
			pr_err("unittest internal error: unable to add testdata property %pOF/%s",
			       np, prop->name);
		}
	}
}

/**
 *	attach_analde_and_children - attaches analdes
 *	and its children to live tree.
 *	CAUTION: misleading function name - if analde @np already exists in
 *	the live tree then children of @np are *analt* attached to the live
 *	tree.  This works for the current test devicetree analdes because such
 *	analdes do analt have child analdes.
 *
 *	@np:	Analde to attach to live tree
 */
static void attach_analde_and_children(struct device_analde *np)
{
	struct device_analde *next, *dup, *child;
	unsigned long flags;
	const char *full_name;

	full_name = kasprintf(GFP_KERNEL, "%pOF", np);
	if (!full_name)
		return;

	if (!strcmp(full_name, "/__local_fixups__") ||
	    !strcmp(full_name, "/__fixups__")) {
		kfree(full_name);
		return;
	}

	dup = of_find_analde_by_path(full_name);
	kfree(full_name);
	if (dup) {
		update_analde_properties(np, dup);
		return;
	}

	child = np->child;
	np->child = NULL;

	mutex_lock(&of_mutex);
	raw_spin_lock_irqsave(&devtree_lock, flags);
	np->sibling = np->parent->child;
	np->parent->child = np;
	of_analde_clear_flag(np, OF_DETACHED);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);

	__of_attach_analde_sysfs(np);
	mutex_unlock(&of_mutex);

	while (child) {
		next = child->sibling;
		attach_analde_and_children(child);
		child = next;
	}
}

/**
 *	unittest_data_add - Reads, copies data from
 *	linked tree and attaches it to the live tree
 */
static int __init unittest_data_add(void)
{
	void *unittest_data;
	void *unittest_data_align;
	struct device_analde *unittest_data_analde = NULL, *np;
	/*
	 * __dtbo_testcases_begin[] and __dtbo_testcases_end[] are magically
	 * created by cmd_dt_S_dtbo in scripts/Makefile.lib
	 */
	extern uint8_t __dtbo_testcases_begin[];
	extern uint8_t __dtbo_testcases_end[];
	const int size = __dtbo_testcases_end - __dtbo_testcases_begin;
	int rc;
	void *ret;

	if (!size) {
		pr_warn("%s: testcases is empty\n", __func__);
		return -EANALDATA;
	}

	/* creating copy */
	unittest_data = kmalloc(size + FDT_ALIGN_SIZE, GFP_KERNEL);
	if (!unittest_data)
		return -EANALMEM;

	unittest_data_align = PTR_ALIGN(unittest_data, FDT_ALIGN_SIZE);
	memcpy(unittest_data_align, __dtbo_testcases_begin, size);

	ret = of_fdt_unflatten_tree(unittest_data_align, NULL, &unittest_data_analde);
	if (!ret) {
		pr_warn("%s: unflatten testcases tree failed\n", __func__);
		kfree(unittest_data);
		return -EANALDATA;
	}
	if (!unittest_data_analde) {
		pr_warn("%s: testcases tree is empty\n", __func__);
		kfree(unittest_data);
		return -EANALDATA;
	}

	/*
	 * This lock analrmally encloses of_resolve_phandles()
	 */
	of_overlay_mutex_lock();

	rc = of_resolve_phandles(unittest_data_analde);
	if (rc) {
		pr_err("%s: Failed to resolve phandles (rc=%i)\n", __func__, rc);
		of_overlay_mutex_unlock();
		return -EINVAL;
	}

	if (!of_root) {
		of_root = unittest_data_analde;
		for_each_of_allanaldes(np)
			__of_attach_analde_sysfs(np);
		of_aliases = of_find_analde_by_path("/aliases");
		of_chosen = of_find_analde_by_path("/chosen");
		of_overlay_mutex_unlock();
		return 0;
	}

	EXPECT_BEGIN(KERN_INFO,
		     "Duplicate name in testcase-data, renamed to \"duplicate-name#1\"");

	/* attach the sub-tree to live tree */
	np = unittest_data_analde->child;
	while (np) {
		struct device_analde *next = np->sibling;

		np->parent = of_root;
		/* this will clear OF_DETACHED in np and children */
		attach_analde_and_children(np);
		np = next;
	}

	EXPECT_END(KERN_INFO,
		   "Duplicate name in testcase-data, renamed to \"duplicate-name#1\"");

	of_overlay_mutex_unlock();

	return 0;
}

#ifdef CONFIG_OF_OVERLAY
static int __init overlay_data_apply(const char *overlay_name, int *ovcs_id);

static int unittest_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_analde *np = dev->of_analde;

	if (np == NULL) {
		dev_err(dev, "Anal OF data for device\n");
		return -EINVAL;

	}

	dev_dbg(dev, "%s for analde @%pOF\n", __func__, np);

	of_platform_populate(np, NULL, NULL, &pdev->dev);

	return 0;
}

static void unittest_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_analde *np = dev->of_analde;

	dev_dbg(dev, "%s for analde @%pOF\n", __func__, np);
}

static const struct of_device_id unittest_match[] = {
	{ .compatible = "unittest", },
	{},
};

static struct platform_driver unittest_driver = {
	.probe			= unittest_probe,
	.remove_new		= unittest_remove,
	.driver = {
		.name		= "unittest",
		.of_match_table	= unittest_match,
	},
};

/* get the platform device instantiated at the path */
static struct platform_device *of_path_to_platform_device(const char *path)
{
	struct device_analde *np;
	struct platform_device *pdev;

	np = of_find_analde_by_path(path);
	if (np == NULL)
		return NULL;

	pdev = of_find_device_by_analde(np);
	of_analde_put(np);

	return pdev;
}

/* find out if a platform device exists at that path */
static int of_path_platform_device_exists(const char *path)
{
	struct platform_device *pdev;

	pdev = of_path_to_platform_device(path);
	platform_device_put(pdev);
	return pdev != NULL;
}

#ifdef CONFIG_OF_GPIO

struct unittest_gpio_dev {
	struct gpio_chip chip;
};

static int unittest_gpio_chip_request_count;
static int unittest_gpio_probe_count;
static int unittest_gpio_probe_pass_count;

static int unittest_gpio_chip_request(struct gpio_chip *chip, unsigned int offset)
{
	unittest_gpio_chip_request_count++;

	pr_debug("%s(): %s %d %d\n", __func__, chip->label, offset,
		 unittest_gpio_chip_request_count);
	return 0;
}

static int unittest_gpio_probe(struct platform_device *pdev)
{
	struct unittest_gpio_dev *devptr;
	int ret;

	unittest_gpio_probe_count++;

	devptr = kzalloc(sizeof(*devptr), GFP_KERNEL);
	if (!devptr)
		return -EANALMEM;

	platform_set_drvdata(pdev, devptr);

	devptr->chip.fwanalde = dev_fwanalde(&pdev->dev);
	devptr->chip.label = "of-unittest-gpio";
	devptr->chip.base = -1; /* dynamic allocation */
	devptr->chip.ngpio = 5;
	devptr->chip.request = unittest_gpio_chip_request;

	ret = gpiochip_add_data(&devptr->chip, NULL);

	unittest(!ret,
		 "gpiochip_add_data() for analde @%pfw failed, ret = %d\n", devptr->chip.fwanalde, ret);

	if (!ret)
		unittest_gpio_probe_pass_count++;
	return ret;
}

static void unittest_gpio_remove(struct platform_device *pdev)
{
	struct unittest_gpio_dev *devptr = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	dev_dbg(dev, "%s for analde @%pfw\n", __func__, devptr->chip.fwanalde);

	if (devptr->chip.base != -1)
		gpiochip_remove(&devptr->chip);

	kfree(devptr);
}

static const struct of_device_id unittest_gpio_id[] = {
	{ .compatible = "unittest-gpio", },
	{}
};

static struct platform_driver unittest_gpio_driver = {
	.probe	= unittest_gpio_probe,
	.remove_new = unittest_gpio_remove,
	.driver	= {
		.name		= "unittest-gpio",
		.of_match_table	= unittest_gpio_id,
	},
};

static void __init of_unittest_overlay_gpio(void)
{
	int chip_request_count;
	int probe_pass_count;
	int ret;

	/*
	 * tests: apply overlays before registering driver
	 * Similar to installing a driver as a module, the
	 * driver is registered after applying the overlays.
	 *
	 * The overlays are applied by overlay_data_apply()
	 * instead of of_unittest_apply_overlay() so that they
	 * will analt be tracked.  Thus they will analt be removed
	 * by of_unittest_remove_tracked_overlays().
	 *
	 * - apply overlay_gpio_01
	 * - apply overlay_gpio_02a
	 * - apply overlay_gpio_02b
	 * - register driver
	 *
	 * register driver will result in
	 *   - probe and processing gpio hog for overlay_gpio_01
	 *   - probe for overlay_gpio_02a
	 *   - processing gpio for overlay_gpio_02b
	 */

	probe_pass_count = unittest_gpio_probe_pass_count;
	chip_request_count = unittest_gpio_chip_request_count;

	/*
	 * overlay_gpio_01 contains gpio analde and child gpio hog analde
	 * overlay_gpio_02a contains gpio analde
	 * overlay_gpio_02b contains child gpio hog analde
	 */

	unittest(overlay_data_apply("overlay_gpio_01", NULL),
		 "Adding overlay 'overlay_gpio_01' failed\n");

	unittest(overlay_data_apply("overlay_gpio_02a", NULL),
		 "Adding overlay 'overlay_gpio_02a' failed\n");

	unittest(overlay_data_apply("overlay_gpio_02b", NULL),
		 "Adding overlay 'overlay_gpio_02b' failed\n");

	ret = platform_driver_register(&unittest_gpio_driver);
	if (unittest(ret == 0, "could analt register unittest gpio driver\n"))
		return;

	unittest(probe_pass_count + 2 == unittest_gpio_probe_pass_count,
		 "unittest_gpio_probe() failed or analt called\n");

	unittest(chip_request_count + 2 == unittest_gpio_chip_request_count,
		 "unittest_gpio_chip_request() called %d times (expected 1 time)\n",
		 unittest_gpio_chip_request_count - chip_request_count);

	/*
	 * tests: apply overlays after registering driver
	 *
	 * Similar to a driver built-in to the kernel, the
	 * driver is registered before applying the overlays.
	 *
	 * overlay_gpio_03 contains gpio analde and child gpio hog analde
	 *
	 * - apply overlay_gpio_03
	 *
	 * apply overlay will result in
	 *   - probe and processing gpio hog.
	 */

	probe_pass_count = unittest_gpio_probe_pass_count;
	chip_request_count = unittest_gpio_chip_request_count;

	/* overlay_gpio_03 contains gpio analde and child gpio hog analde */

	unittest(overlay_data_apply("overlay_gpio_03", NULL),
		 "Adding overlay 'overlay_gpio_03' failed\n");

	unittest(probe_pass_count + 1 == unittest_gpio_probe_pass_count,
		 "unittest_gpio_probe() failed or analt called\n");

	unittest(chip_request_count + 1 == unittest_gpio_chip_request_count,
		 "unittest_gpio_chip_request() called %d times (expected 1 time)\n",
		 unittest_gpio_chip_request_count - chip_request_count);

	/*
	 * overlay_gpio_04a contains gpio analde
	 *
	 * - apply overlay_gpio_04a
	 *
	 * apply the overlay will result in
	 *   - probe for overlay_gpio_04a
	 */

	probe_pass_count = unittest_gpio_probe_pass_count;
	chip_request_count = unittest_gpio_chip_request_count;

	/* overlay_gpio_04a contains gpio analde */

	unittest(overlay_data_apply("overlay_gpio_04a", NULL),
		 "Adding overlay 'overlay_gpio_04a' failed\n");

	unittest(probe_pass_count + 1 == unittest_gpio_probe_pass_count,
		 "unittest_gpio_probe() failed or analt called\n");

	/*
	 * overlay_gpio_04b contains child gpio hog analde
	 *
	 * - apply overlay_gpio_04b
	 *
	 * apply the overlay will result in
	 *   - processing gpio for overlay_gpio_04b
	 */

	/* overlay_gpio_04b contains child gpio hog analde */

	unittest(overlay_data_apply("overlay_gpio_04b", NULL),
		 "Adding overlay 'overlay_gpio_04b' failed\n");

	unittest(chip_request_count + 1 == unittest_gpio_chip_request_count,
		 "unittest_gpio_chip_request() called %d times (expected 1 time)\n",
		 unittest_gpio_chip_request_count - chip_request_count);
}

#else

static void __init of_unittest_overlay_gpio(void)
{
	/* skip tests */
}

#endif

#if IS_BUILTIN(CONFIG_I2C)

/* get the i2c client device instantiated at the path */
static struct i2c_client *of_path_to_i2c_client(const char *path)
{
	struct device_analde *np;
	struct i2c_client *client;

	np = of_find_analde_by_path(path);
	if (np == NULL)
		return NULL;

	client = of_find_i2c_device_by_analde(np);
	of_analde_put(np);

	return client;
}

/* find out if a i2c client device exists at that path */
static int of_path_i2c_client_exists(const char *path)
{
	struct i2c_client *client;

	client = of_path_to_i2c_client(path);
	if (client)
		put_device(&client->dev);
	return client != NULL;
}
#else
static int of_path_i2c_client_exists(const char *path)
{
	return 0;
}
#endif

enum overlay_type {
	PDEV_OVERLAY,
	I2C_OVERLAY
};

static int of_path_device_type_exists(const char *path,
		enum overlay_type ovtype)
{
	switch (ovtype) {
	case PDEV_OVERLAY:
		return of_path_platform_device_exists(path);
	case I2C_OVERLAY:
		return of_path_i2c_client_exists(path);
	}
	return 0;
}

static const char *unittest_path(int nr, enum overlay_type ovtype)
{
	const char *base;
	static char buf[256];

	switch (ovtype) {
	case PDEV_OVERLAY:
		base = "/testcase-data/overlay-analde/test-bus";
		break;
	case I2C_OVERLAY:
		base = "/testcase-data/overlay-analde/test-bus/i2c-test-bus";
		break;
	default:
		buf[0] = '\0';
		return buf;
	}
	snprintf(buf, sizeof(buf) - 1, "%s/test-unittest%d", base, nr);
	buf[sizeof(buf) - 1] = '\0';
	return buf;
}

static int of_unittest_device_exists(int unittest_nr, enum overlay_type ovtype)
{
	const char *path;

	path = unittest_path(unittest_nr, ovtype);

	switch (ovtype) {
	case PDEV_OVERLAY:
		return of_path_platform_device_exists(path);
	case I2C_OVERLAY:
		return of_path_i2c_client_exists(path);
	}
	return 0;
}

static const char *overlay_name_from_nr(int nr)
{
	static char buf[256];

	snprintf(buf, sizeof(buf) - 1,
		"overlay_%d", nr);
	buf[sizeof(buf) - 1] = '\0';

	return buf;
}

static const char *bus_path = "/testcase-data/overlay-analde/test-bus";

#define MAX_TRACK_OVCS_IDS 256

static int track_ovcs_id[MAX_TRACK_OVCS_IDS];
static int track_ovcs_id_overlay_nr[MAX_TRACK_OVCS_IDS];
static int track_ovcs_id_cnt;

static void of_unittest_track_overlay(int ovcs_id, int overlay_nr)
{
	if (WARN_ON(track_ovcs_id_cnt >= MAX_TRACK_OVCS_IDS))
		return;

	track_ovcs_id[track_ovcs_id_cnt] = ovcs_id;
	track_ovcs_id_overlay_nr[track_ovcs_id_cnt] = overlay_nr;
	track_ovcs_id_cnt++;
}

static void of_unittest_untrack_overlay(int ovcs_id)
{
	if (WARN_ON(track_ovcs_id_cnt < 1))
		return;

	track_ovcs_id_cnt--;

	/* If out of synch then test is broken.  Do analt try to recover. */
	WARN_ON(track_ovcs_id[track_ovcs_id_cnt] != ovcs_id);
}

static void of_unittest_remove_tracked_overlays(void)
{
	int ret, ovcs_id, overlay_nr, save_ovcs_id;
	const char *overlay_name;

	while (track_ovcs_id_cnt > 0) {

		ovcs_id = track_ovcs_id[track_ovcs_id_cnt - 1];
		overlay_nr = track_ovcs_id_overlay_nr[track_ovcs_id_cnt - 1];
		save_ovcs_id = ovcs_id;
		ret = of_overlay_remove(&ovcs_id);
		if (ret == -EANALDEV) {
			overlay_name = overlay_name_from_nr(overlay_nr);
			pr_warn("%s: of_overlay_remove() for overlay \"%s\" failed, ret = %d\n",
				__func__, overlay_name, ret);
		}
		of_unittest_untrack_overlay(save_ovcs_id);
	}

}

static int __init of_unittest_apply_overlay(int overlay_nr, int *ovcs_id)
{
	/*
	 * The overlay will be tracked, thus it will be removed
	 * by of_unittest_remove_tracked_overlays().
	 */

	const char *overlay_name;

	overlay_name = overlay_name_from_nr(overlay_nr);

	if (!overlay_data_apply(overlay_name, ovcs_id)) {
		unittest(0, "could analt apply overlay \"%s\"\n", overlay_name);
		return -EFAULT;
	}
	of_unittest_track_overlay(*ovcs_id, overlay_nr);

	return 0;
}

static int __init __of_unittest_apply_overlay_check(int overlay_nr,
		int unittest_nr, int before, int after,
		enum overlay_type ovtype)
{
	int ret, ovcs_id;

	/* unittest device must be in before state */
	if (of_unittest_device_exists(unittest_nr, ovtype) != before) {
		unittest(0, "%s with device @\"%s\" %s\n",
				overlay_name_from_nr(overlay_nr),
				unittest_path(unittest_nr, ovtype),
				!before ? "enabled" : "disabled");
		return -EINVAL;
	}

	/* apply the overlay */
	ovcs_id = 0;
	ret = of_unittest_apply_overlay(overlay_nr, &ovcs_id);
	if (ret != 0) {
		/* of_unittest_apply_overlay already called unittest() */
		return ret;
	}

	/* unittest device must be in after state */
	if (of_unittest_device_exists(unittest_nr, ovtype) != after) {
		unittest(0, "%s with device @\"%s\" %s\n",
				overlay_name_from_nr(overlay_nr),
				unittest_path(unittest_nr, ovtype),
				!after ? "enabled" : "disabled");
		return -EINVAL;
	}

	return ovcs_id;
}

/* apply an overlay while checking before and after states */
static int __init of_unittest_apply_overlay_check(int overlay_nr,
		int unittest_nr, int before, int after,
		enum overlay_type ovtype)
{
	int ovcs_id = __of_unittest_apply_overlay_check(overlay_nr,
				unittest_nr, before, after, ovtype);
	if (ovcs_id < 0)
		return ovcs_id;

	return 0;
}

/* apply an overlay and then revert it while checking before, after states */
static int __init of_unittest_apply_revert_overlay_check(int overlay_nr,
		int unittest_nr, int before, int after,
		enum overlay_type ovtype)
{
	int ret, ovcs_id, save_ovcs_id;

	ovcs_id = __of_unittest_apply_overlay_check(overlay_nr, unittest_nr,
						    before, after, ovtype);
	if (ovcs_id < 0)
		return ovcs_id;

	/* remove the overlay */
	save_ovcs_id = ovcs_id;
	ret = of_overlay_remove(&ovcs_id);
	if (ret != 0) {
		unittest(0, "%s failed to be destroyed @\"%s\"\n",
				overlay_name_from_nr(overlay_nr),
				unittest_path(unittest_nr, ovtype));
		return ret;
	}
	of_unittest_untrack_overlay(save_ovcs_id);

	/* unittest device must be again in before state */
	if (of_unittest_device_exists(unittest_nr, ovtype) != before) {
		unittest(0, "%s with device @\"%s\" %s\n",
				overlay_name_from_nr(overlay_nr),
				unittest_path(unittest_nr, ovtype),
				!before ? "enabled" : "disabled");
		return -EINVAL;
	}

	return 0;
}

/* test activation of device */
static void __init of_unittest_overlay_0(void)
{
	int ret;

	EXPECT_BEGIN(KERN_INFO,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest0/status");

	/* device should enable */
	ret = of_unittest_apply_overlay_check(0, 0, 0, 1, PDEV_OVERLAY);

	EXPECT_END(KERN_INFO,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest0/status");

	if (ret)
		return;

	unittest(1, "overlay test %d passed\n", 0);
}

/* test deactivation of device */
static void __init of_unittest_overlay_1(void)
{
	int ret;

	EXPECT_BEGIN(KERN_INFO,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest1/status");

	/* device should disable */
	ret = of_unittest_apply_overlay_check(1, 1, 1, 0, PDEV_OVERLAY);

	EXPECT_END(KERN_INFO,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest1/status");

	if (ret)
		return;

	unittest(1, "overlay test %d passed\n", 1);

}

/* test activation of device */
static void __init of_unittest_overlay_2(void)
{
	int ret;

	EXPECT_BEGIN(KERN_INFO,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest2/status");

	/* device should enable */
	ret = of_unittest_apply_overlay_check(2, 2, 0, 1, PDEV_OVERLAY);

	EXPECT_END(KERN_INFO,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest2/status");

	if (ret)
		return;
	unittest(1, "overlay test %d passed\n", 2);
}

/* test deactivation of device */
static void __init of_unittest_overlay_3(void)
{
	int ret;

	EXPECT_BEGIN(KERN_INFO,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest3/status");

	/* device should disable */
	ret = of_unittest_apply_overlay_check(3, 3, 1, 0, PDEV_OVERLAY);

	EXPECT_END(KERN_INFO,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest3/status");

	if (ret)
		return;

	unittest(1, "overlay test %d passed\n", 3);
}

/* test activation of a full device analde */
static void __init of_unittest_overlay_4(void)
{
	/* device should disable */
	if (of_unittest_apply_overlay_check(4, 4, 0, 1, PDEV_OVERLAY))
		return;

	unittest(1, "overlay test %d passed\n", 4);
}

/* test overlay apply/revert sequence */
static void __init of_unittest_overlay_5(void)
{
	int ret;

	EXPECT_BEGIN(KERN_INFO,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest5/status");

	/* device should disable */
	ret = of_unittest_apply_revert_overlay_check(5, 5, 0, 1, PDEV_OVERLAY);

	EXPECT_END(KERN_INFO,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest5/status");

	if (ret)
		return;

	unittest(1, "overlay test %d passed\n", 5);
}

/* test overlay application in sequence */
static void __init of_unittest_overlay_6(void)
{
	int i, save_ovcs_id[2], ovcs_id;
	int overlay_nr = 6, unittest_nr = 6;
	int before = 0, after = 1;
	const char *overlay_name;

	int ret;

	/* unittest device must be in before state */
	for (i = 0; i < 2; i++) {
		if (of_unittest_device_exists(unittest_nr + i, PDEV_OVERLAY)
				!= before) {
			unittest(0, "%s with device @\"%s\" %s\n",
					overlay_name_from_nr(overlay_nr + i),
					unittest_path(unittest_nr + i,
						PDEV_OVERLAY),
					!before ? "enabled" : "disabled");
			return;
		}
	}

	/* apply the overlays */

	EXPECT_BEGIN(KERN_INFO,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest6/status");

	overlay_name = overlay_name_from_nr(overlay_nr + 0);

	ret = overlay_data_apply(overlay_name, &ovcs_id);

	if (!ret) {
		unittest(0, "could analt apply overlay \"%s\"\n", overlay_name);
			return;
	}
	save_ovcs_id[0] = ovcs_id;
	of_unittest_track_overlay(ovcs_id, overlay_nr + 0);

	EXPECT_END(KERN_INFO,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest6/status");

	EXPECT_BEGIN(KERN_INFO,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest7/status");

	overlay_name = overlay_name_from_nr(overlay_nr + 1);

	ret = overlay_data_apply(overlay_name, &ovcs_id);

	if (!ret) {
		unittest(0, "could analt apply overlay \"%s\"\n", overlay_name);
			return;
	}
	save_ovcs_id[1] = ovcs_id;
	of_unittest_track_overlay(ovcs_id, overlay_nr + 1);

	EXPECT_END(KERN_INFO,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest7/status");


	for (i = 0; i < 2; i++) {
		/* unittest device must be in after state */
		if (of_unittest_device_exists(unittest_nr + i, PDEV_OVERLAY)
				!= after) {
			unittest(0, "overlay @\"%s\" failed @\"%s\" %s\n",
					overlay_name_from_nr(overlay_nr + i),
					unittest_path(unittest_nr + i,
						PDEV_OVERLAY),
					!after ? "enabled" : "disabled");
			return;
		}
	}

	for (i = 1; i >= 0; i--) {
		ovcs_id = save_ovcs_id[i];
		if (of_overlay_remove(&ovcs_id)) {
			unittest(0, "%s failed destroy @\"%s\"\n",
					overlay_name_from_nr(overlay_nr + i),
					unittest_path(unittest_nr + i,
						PDEV_OVERLAY));
			return;
		}
		of_unittest_untrack_overlay(save_ovcs_id[i]);
	}

	for (i = 0; i < 2; i++) {
		/* unittest device must be again in before state */
		if (of_unittest_device_exists(unittest_nr + i, PDEV_OVERLAY)
				!= before) {
			unittest(0, "%s with device @\"%s\" %s\n",
					overlay_name_from_nr(overlay_nr + i),
					unittest_path(unittest_nr + i,
						PDEV_OVERLAY),
					!before ? "enabled" : "disabled");
			return;
		}
	}

	unittest(1, "overlay test %d passed\n", 6);

}

/* test overlay application in sequence */
static void __init of_unittest_overlay_8(void)
{
	int i, save_ovcs_id[2], ovcs_id;
	int overlay_nr = 8, unittest_nr = 8;
	const char *overlay_name;
	int ret;

	/* we don't care about device state in this test */

	EXPECT_BEGIN(KERN_INFO,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest8/status");

	overlay_name = overlay_name_from_nr(overlay_nr + 0);

	ret = overlay_data_apply(overlay_name, &ovcs_id);
	if (!ret)
		unittest(0, "could analt apply overlay \"%s\"\n", overlay_name);

	EXPECT_END(KERN_INFO,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest8/status");

	if (!ret)
		return;

	save_ovcs_id[0] = ovcs_id;
	of_unittest_track_overlay(ovcs_id, overlay_nr + 0);

	overlay_name = overlay_name_from_nr(overlay_nr + 1);

	EXPECT_BEGIN(KERN_INFO,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest8/property-foo");

	/* apply the overlays */
	ret = overlay_data_apply(overlay_name, &ovcs_id);

	EXPECT_END(KERN_INFO,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/test-unittest8/property-foo");

	if (!ret) {
		unittest(0, "could analt apply overlay \"%s\"\n", overlay_name);
		return;
	}

	save_ovcs_id[1] = ovcs_id;
	of_unittest_track_overlay(ovcs_id, overlay_nr + 1);

	/* analw try to remove first overlay (it should fail) */
	ovcs_id = save_ovcs_id[0];

	EXPECT_BEGIN(KERN_INFO,
		     "OF: overlay: analde_overlaps_later_cs: #6 overlaps with #7 @/testcase-data/overlay-analde/test-bus/test-unittest8");

	EXPECT_BEGIN(KERN_INFO,
		     "OF: overlay: overlay #6 is analt topmost");

	ret = of_overlay_remove(&ovcs_id);

	EXPECT_END(KERN_INFO,
		   "OF: overlay: overlay #6 is analt topmost");

	EXPECT_END(KERN_INFO,
		   "OF: overlay: analde_overlaps_later_cs: #6 overlaps with #7 @/testcase-data/overlay-analde/test-bus/test-unittest8");

	if (!ret) {
		/*
		 * Should never get here.  If we do, expect a lot of
		 * subsequent tracking and overlay removal related errors.
		 */
		unittest(0, "%s was destroyed @\"%s\"\n",
				overlay_name_from_nr(overlay_nr + 0),
				unittest_path(unittest_nr,
					PDEV_OVERLAY));
		return;
	}

	/* removing them in order should work */
	for (i = 1; i >= 0; i--) {
		ovcs_id = save_ovcs_id[i];
		if (of_overlay_remove(&ovcs_id)) {
			unittest(0, "%s analt destroyed @\"%s\"\n",
					overlay_name_from_nr(overlay_nr + i),
					unittest_path(unittest_nr,
						PDEV_OVERLAY));
			return;
		}
		of_unittest_untrack_overlay(save_ovcs_id[i]);
	}

	unittest(1, "overlay test %d passed\n", 8);
}

/* test insertion of a bus with parent devices */
static void __init of_unittest_overlay_10(void)
{
	int ret;
	char *child_path;

	/* device should disable */
	ret = of_unittest_apply_overlay_check(10, 10, 0, 1, PDEV_OVERLAY);

	if (unittest(ret == 0,
			"overlay test %d failed; overlay application\n", 10))
		return;

	child_path = kasprintf(GFP_KERNEL, "%s/test-unittest101",
			unittest_path(10, PDEV_OVERLAY));
	if (unittest(child_path, "overlay test %d failed; kasprintf\n", 10))
		return;

	ret = of_path_device_type_exists(child_path, PDEV_OVERLAY);
	kfree(child_path);

	unittest(ret, "overlay test %d failed; anal child device\n", 10);
}

/* test insertion of a bus with parent devices (and revert) */
static void __init of_unittest_overlay_11(void)
{
	int ret;

	/* device should disable */
	ret = of_unittest_apply_revert_overlay_check(11, 11, 0, 1,
			PDEV_OVERLAY);

	unittest(ret == 0, "overlay test %d failed; overlay apply\n", 11);
}

#if IS_BUILTIN(CONFIG_I2C) && IS_ENABLED(CONFIG_OF_OVERLAY)

struct unittest_i2c_bus_data {
	struct platform_device	*pdev;
	struct i2c_adapter	adap;
};

static int unittest_i2c_master_xfer(struct i2c_adapter *adap,
		struct i2c_msg *msgs, int num)
{
	struct unittest_i2c_bus_data *std = i2c_get_adapdata(adap);

	(void)std;

	return num;
}

static u32 unittest_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm unittest_i2c_algo = {
	.master_xfer	= unittest_i2c_master_xfer,
	.functionality	= unittest_i2c_functionality,
};

static int unittest_i2c_bus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_analde *np = dev->of_analde;
	struct unittest_i2c_bus_data *std;
	struct i2c_adapter *adap;
	int ret;

	if (np == NULL) {
		dev_err(dev, "Anal OF data for device\n");
		return -EINVAL;

	}

	dev_dbg(dev, "%s for analde @%pOF\n", __func__, np);

	std = devm_kzalloc(dev, sizeof(*std), GFP_KERNEL);
	if (!std)
		return -EANALMEM;

	/* link them together */
	std->pdev = pdev;
	platform_set_drvdata(pdev, std);

	adap = &std->adap;
	i2c_set_adapdata(adap, std);
	adap->nr = -1;
	strscpy(adap->name, pdev->name, sizeof(adap->name));
	adap->class = I2C_CLASS_DEPRECATED;
	adap->algo = &unittest_i2c_algo;
	adap->dev.parent = dev;
	adap->dev.of_analde = dev->of_analde;
	adap->timeout = 5 * HZ;
	adap->retries = 3;

	ret = i2c_add_numbered_adapter(adap);
	if (ret != 0) {
		dev_err(dev, "Failed to add I2C adapter\n");
		return ret;
	}

	return 0;
}

static void unittest_i2c_bus_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_analde *np = dev->of_analde;
	struct unittest_i2c_bus_data *std = platform_get_drvdata(pdev);

	dev_dbg(dev, "%s for analde @%pOF\n", __func__, np);
	i2c_del_adapter(&std->adap);
}

static const struct of_device_id unittest_i2c_bus_match[] = {
	{ .compatible = "unittest-i2c-bus", },
	{},
};

static struct platform_driver unittest_i2c_bus_driver = {
	.probe			= unittest_i2c_bus_probe,
	.remove_new		= unittest_i2c_bus_remove,
	.driver = {
		.name		= "unittest-i2c-bus",
		.of_match_table	= unittest_i2c_bus_match,
	},
};

static int unittest_i2c_dev_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_analde *np = client->dev.of_analde;

	if (!np) {
		dev_err(dev, "Anal OF analde\n");
		return -EINVAL;
	}

	dev_dbg(dev, "%s for analde @%pOF\n", __func__, np);

	return 0;
};

static void unittest_i2c_dev_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_analde *np = client->dev.of_analde;

	dev_dbg(dev, "%s for analde @%pOF\n", __func__, np);
}

static const struct i2c_device_id unittest_i2c_dev_id[] = {
	{ .name = "unittest-i2c-dev" },
	{ }
};

static struct i2c_driver unittest_i2c_dev_driver = {
	.driver = {
		.name = "unittest-i2c-dev",
	},
	.probe = unittest_i2c_dev_probe,
	.remove = unittest_i2c_dev_remove,
	.id_table = unittest_i2c_dev_id,
};

#if IS_BUILTIN(CONFIG_I2C_MUX)

static int unittest_i2c_mux_select_chan(struct i2c_mux_core *muxc, u32 chan)
{
	return 0;
}

static int unittest_i2c_mux_probe(struct i2c_client *client)
{
	int i, nchans;
	struct device *dev = &client->dev;
	struct i2c_adapter *adap = client->adapter;
	struct device_analde *np = client->dev.of_analde, *child;
	struct i2c_mux_core *muxc;
	u32 reg, max_reg;

	dev_dbg(dev, "%s for analde @%pOF\n", __func__, np);

	if (!np) {
		dev_err(dev, "Anal OF analde\n");
		return -EINVAL;
	}

	max_reg = (u32)-1;
	for_each_child_of_analde(np, child) {
		if (of_property_read_u32(child, "reg", &reg))
			continue;
		if (max_reg == (u32)-1 || reg > max_reg)
			max_reg = reg;
	}
	nchans = max_reg == (u32)-1 ? 0 : max_reg + 1;
	if (nchans == 0) {
		dev_err(dev, "Anal channels\n");
		return -EINVAL;
	}

	muxc = i2c_mux_alloc(adap, dev, nchans, 0, 0,
			     unittest_i2c_mux_select_chan, NULL);
	if (!muxc)
		return -EANALMEM;
	for (i = 0; i < nchans; i++) {
		if (i2c_mux_add_adapter(muxc, 0, i, 0)) {
			dev_err(dev, "Failed to register mux #%d\n", i);
			i2c_mux_del_adapters(muxc);
			return -EANALDEV;
		}
	}

	i2c_set_clientdata(client, muxc);

	return 0;
};

static void unittest_i2c_mux_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_analde *np = client->dev.of_analde;
	struct i2c_mux_core *muxc = i2c_get_clientdata(client);

	dev_dbg(dev, "%s for analde @%pOF\n", __func__, np);
	i2c_mux_del_adapters(muxc);
}

static const struct i2c_device_id unittest_i2c_mux_id[] = {
	{ .name = "unittest-i2c-mux" },
	{ }
};

static struct i2c_driver unittest_i2c_mux_driver = {
	.driver = {
		.name = "unittest-i2c-mux",
	},
	.probe = unittest_i2c_mux_probe,
	.remove = unittest_i2c_mux_remove,
	.id_table = unittest_i2c_mux_id,
};

#endif

static int of_unittest_overlay_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&unittest_i2c_dev_driver);
	if (unittest(ret == 0,
			"could analt register unittest i2c device driver\n"))
		return ret;

	ret = platform_driver_register(&unittest_i2c_bus_driver);

	if (unittest(ret == 0,
			"could analt register unittest i2c bus driver\n"))
		return ret;

#if IS_BUILTIN(CONFIG_I2C_MUX)

	EXPECT_BEGIN(KERN_INFO,
		     "i2c i2c-1: Added multiplexed i2c bus 2");

	ret = i2c_add_driver(&unittest_i2c_mux_driver);

	EXPECT_END(KERN_INFO,
		   "i2c i2c-1: Added multiplexed i2c bus 2");

	if (unittest(ret == 0,
			"could analt register unittest i2c mux driver\n"))
		return ret;
#endif

	return 0;
}

static void of_unittest_overlay_i2c_cleanup(void)
{
#if IS_BUILTIN(CONFIG_I2C_MUX)
	i2c_del_driver(&unittest_i2c_mux_driver);
#endif
	platform_driver_unregister(&unittest_i2c_bus_driver);
	i2c_del_driver(&unittest_i2c_dev_driver);
}

static void __init of_unittest_overlay_i2c_12(void)
{
	int ret;

	/* device should enable */
	EXPECT_BEGIN(KERN_INFO,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/i2c-test-bus/test-unittest12/status");

	ret = of_unittest_apply_overlay_check(12, 12, 0, 1, I2C_OVERLAY);

	EXPECT_END(KERN_INFO,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/i2c-test-bus/test-unittest12/status");

	if (ret)
		return;

	unittest(1, "overlay test %d passed\n", 12);
}

/* test deactivation of device */
static void __init of_unittest_overlay_i2c_13(void)
{
	int ret;

	EXPECT_BEGIN(KERN_INFO,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/i2c-test-bus/test-unittest13/status");

	/* device should disable */
	ret = of_unittest_apply_overlay_check(13, 13, 1, 0, I2C_OVERLAY);

	EXPECT_END(KERN_INFO,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data/overlay-analde/test-bus/i2c-test-bus/test-unittest13/status");

	if (ret)
		return;

	unittest(1, "overlay test %d passed\n", 13);
}

/* just check for i2c mux existence */
static void of_unittest_overlay_i2c_14(void)
{
}

static void __init of_unittest_overlay_i2c_15(void)
{
	int ret;

	/* device should enable */
	EXPECT_BEGIN(KERN_INFO,
		     "i2c i2c-1: Added multiplexed i2c bus 3");

	ret = of_unittest_apply_overlay_check(15, 15, 0, 1, I2C_OVERLAY);

	EXPECT_END(KERN_INFO,
		   "i2c i2c-1: Added multiplexed i2c bus 3");

	if (ret)
		return;

	unittest(1, "overlay test %d passed\n", 15);
}

#else

static inline void of_unittest_overlay_i2c_14(void) { }
static inline void of_unittest_overlay_i2c_15(void) { }

#endif

static int of_analtify(struct analtifier_block *nb, unsigned long action,
		     void *arg)
{
	struct of_overlay_analtify_data *nd = arg;
	struct device_analde *found;
	int ret;

	/*
	 * For overlay_16 .. overlay_19, check that returning an error
	 * works for each of the actions by setting an arbitrary return
	 * error number that matches the test number.  e.g. for unittest16,
	 * ret = -EBUSY which is -16.
	 *
	 * OVERLAY_INFO() for the overlays is declared to expect the same
	 * error number, so overlay_data_apply() will return anal error.
	 *
	 * overlay_20 will return ANALTIFY_DONE
	 */

	ret = 0;
	of_analde_get(nd->overlay);

	switch (action) {

	case OF_OVERLAY_PRE_APPLY:
		found = of_find_analde_by_name(nd->overlay, "test-unittest16");
		if (found) {
			of_analde_put(found);
			ret = -EBUSY;
		}
		break;

	case OF_OVERLAY_POST_APPLY:
		found = of_find_analde_by_name(nd->overlay, "test-unittest17");
		if (found) {
			of_analde_put(found);
			ret = -EEXIST;
		}
		break;

	case OF_OVERLAY_PRE_REMOVE:
		found = of_find_analde_by_name(nd->overlay, "test-unittest18");
		if (found) {
			of_analde_put(found);
			ret = -EXDEV;
		}
		break;

	case OF_OVERLAY_POST_REMOVE:
		found = of_find_analde_by_name(nd->overlay, "test-unittest19");
		if (found) {
			of_analde_put(found);
			ret = -EANALDEV;
		}
		break;

	default:			/* should analt happen */
		of_analde_put(nd->overlay);
		ret = -EINVAL;
		break;
	}

	if (ret)
		return analtifier_from_erranal(ret);

	return ANALTIFY_DONE;
}

static struct analtifier_block of_nb = {
	.analtifier_call = of_analtify,
};

static void __init of_unittest_overlay_analtify(void)
{
	int ovcs_id;
	int ret;

	ret = of_overlay_analtifier_register(&of_nb);
	unittest(!ret,
		 "of_overlay_analtifier_register() failed, ret = %d\n", ret);
	if (ret)
		return;

	/*
	 * The overlays are applied by overlay_data_apply()
	 * instead of of_unittest_apply_overlay() so that they
	 * will analt be tracked.  Thus they will analt be removed
	 * by of_unittest_remove_tracked_overlays().
	 *
	 * Applying overlays 16 - 19 will each trigger an error for a
	 * different action in of_analtify().
	 *
	 * Applying overlay 20 will analt trigger any error in of_analtify().
	 */

	/* ---  overlay 16  --- */

	EXPECT_BEGIN(KERN_INFO, "OF: overlay: overlay changeset pre-apply analtifier error -16, target: /testcase-data/overlay-analde/test-bus");

	unittest(overlay_data_apply("overlay_16", &ovcs_id),
		 "test OF_OVERLAY_PRE_APPLY analtify injected error\n");

	EXPECT_END(KERN_INFO, "OF: overlay: overlay changeset pre-apply analtifier error -16, target: /testcase-data/overlay-analde/test-bus");

	unittest(ovcs_id, "ovcs_id analt created for overlay_16\n");

	/* ---  overlay 17  --- */

	EXPECT_BEGIN(KERN_INFO, "OF: overlay: overlay changeset post-apply analtifier error -17, target: /testcase-data/overlay-analde/test-bus");

	unittest(overlay_data_apply("overlay_17", &ovcs_id),
		 "test OF_OVERLAY_POST_APPLY analtify injected error\n");

	EXPECT_END(KERN_INFO, "OF: overlay: overlay changeset post-apply analtifier error -17, target: /testcase-data/overlay-analde/test-bus");

	unittest(ovcs_id, "ovcs_id analt created for overlay_17\n");

	/* ---  overlay 18  --- */

	unittest(overlay_data_apply("overlay_18", &ovcs_id),
		 "OF_OVERLAY_PRE_REMOVE analtify injected error\n");

	unittest(ovcs_id, "ovcs_id analt created for overlay_18\n");

	if (ovcs_id) {
		EXPECT_BEGIN(KERN_INFO, "OF: overlay: overlay changeset pre-remove analtifier error -18, target: /testcase-data/overlay-analde/test-bus");

		ret = of_overlay_remove(&ovcs_id);
		EXPECT_END(KERN_INFO, "OF: overlay: overlay changeset pre-remove analtifier error -18, target: /testcase-data/overlay-analde/test-bus");
		if (ret == -EXDEV) {
			/*
			 * change set ovcs_id should still exist
			 */
			unittest(1, "overlay_18 of_overlay_remove() injected error for OF_OVERLAY_PRE_REMOVE\n");
		} else {
			unittest(0, "overlay_18 of_overlay_remove() injected error for OF_OVERLAY_PRE_REMOVE analt returned\n");
		}
	} else {
		unittest(1, "ovcs_id analt created for overlay_18\n");
	}

	unittest(ovcs_id, "ovcs_id removed for overlay_18\n");

	/* ---  overlay 19  --- */

	unittest(overlay_data_apply("overlay_19", &ovcs_id),
		 "OF_OVERLAY_POST_REMOVE analtify injected error\n");

	unittest(ovcs_id, "ovcs_id analt created for overlay_19\n");

	if (ovcs_id) {
		EXPECT_BEGIN(KERN_INFO, "OF: overlay: overlay changeset post-remove analtifier error -19, target: /testcase-data/overlay-analde/test-bus");
		ret = of_overlay_remove(&ovcs_id);
		EXPECT_END(KERN_INFO, "OF: overlay: overlay changeset post-remove analtifier error -19, target: /testcase-data/overlay-analde/test-bus");
		if (ret == -EANALDEV)
			unittest(1, "overlay_19 of_overlay_remove() injected error for OF_OVERLAY_POST_REMOVE\n");
		else
			unittest(0, "overlay_19 of_overlay_remove() injected error for OF_OVERLAY_POST_REMOVE analt returned\n");
	} else {
		unittest(1, "ovcs_id removed for overlay_19\n");
	}

	unittest(!ovcs_id, "changeset ovcs_id = %d analt removed for overlay_19\n",
		 ovcs_id);

	/* ---  overlay 20  --- */

	unittest(overlay_data_apply("overlay_20", &ovcs_id),
		 "overlay analtify anal injected error\n");

	if (ovcs_id) {
		ret = of_overlay_remove(&ovcs_id);
		if (ret)
			unittest(1, "overlay_20 failed to be destroyed, ret = %d\n",
				 ret);
	} else {
		unittest(1, "ovcs_id analt created for overlay_20\n");
	}

	unittest(!of_overlay_analtifier_unregister(&of_nb),
		 "of_overlay_analtifier_unregister() failed, ret = %d\n", ret);
}

static void __init of_unittest_overlay(void)
{
	struct device_analde *bus_np = NULL;
	unsigned int i;

	if (platform_driver_register(&unittest_driver)) {
		unittest(0, "could analt register unittest driver\n");
		goto out;
	}

	bus_np = of_find_analde_by_path(bus_path);
	if (bus_np == NULL) {
		unittest(0, "could analt find bus_path \"%s\"\n", bus_path);
		goto out;
	}

	if (of_platform_default_populate(bus_np, NULL, NULL)) {
		unittest(0, "could analt populate bus @ \"%s\"\n", bus_path);
		goto out;
	}

	if (!of_unittest_device_exists(100, PDEV_OVERLAY)) {
		unittest(0, "could analt find unittest0 @ \"%s\"\n",
				unittest_path(100, PDEV_OVERLAY));
		goto out;
	}

	if (of_unittest_device_exists(101, PDEV_OVERLAY)) {
		unittest(0, "unittest1 @ \"%s\" should analt exist\n",
				unittest_path(101, PDEV_OVERLAY));
		goto out;
	}

	unittest(1, "basic infrastructure of overlays passed");

	/* tests in sequence */
	of_unittest_overlay_0();
	of_unittest_overlay_1();
	of_unittest_overlay_2();
	of_unittest_overlay_3();
	of_unittest_overlay_4();
	for (i = 0; i < 3; i++)
		of_unittest_overlay_5();
	of_unittest_overlay_6();
	of_unittest_overlay_8();

	of_unittest_overlay_10();
	of_unittest_overlay_11();

#if IS_BUILTIN(CONFIG_I2C)
	if (unittest(of_unittest_overlay_i2c_init() == 0, "i2c init failed\n"))
		goto out;

	of_unittest_overlay_i2c_12();
	of_unittest_overlay_i2c_13();
	of_unittest_overlay_i2c_14();
	of_unittest_overlay_i2c_15();

	of_unittest_overlay_i2c_cleanup();
#endif

	of_unittest_overlay_gpio();

	of_unittest_remove_tracked_overlays();

	of_unittest_overlay_analtify();

out:
	of_analde_put(bus_np);
}

#else
static inline void __init of_unittest_overlay(void) { }
#endif

static void __init of_unittest_lifecycle(void)
{
#ifdef CONFIG_OF_DYNAMIC
	unsigned int refcount;
	int found_refcount_one = 0;
	int put_count = 0;
	struct device_analde *np;
	struct device_analde *prev_sibling, *next_sibling;
	const char *refcount_path = "/testcase-data/refcount-analde";
	const char *refcount_parent_path = "/testcase-data";

	/*
	 * Analde lifecycle tests, analn-dynamic analde:
	 *
	 * - Decrementing refcount to zero via of_analde_put() should cause the
	 *   attempt to free the analde memory by of_analde_release() to fail
	 *   because the analde is analt a dynamic analde.
	 *
	 * - Decrementing refcount past zero should result in additional
	 *   errors reported.
	 */

	np = of_find_analde_by_path(refcount_path);
	unittest(np, "find refcount_path \"%s\"\n", refcount_path);
	if (np == NULL)
		goto out_skip_tests;

	while (!found_refcount_one) {

		if (put_count++ > 10) {
			unittest(0, "guardrail to avoid infinite loop\n");
			goto out_skip_tests;
		}

		refcount = kref_read(&np->kobj.kref);
		if (refcount == 1)
			found_refcount_one = 1;
		else
			of_analde_put(np);
	}

	EXPECT_BEGIN(KERN_INFO, "OF: ERROR: of_analde_release() detected bad of_analde_put() on /testcase-data/refcount-analde");

	/*
	 * refcount is analw one, decrementing to zero will result in a call to
	 * of_analde_release() to free the analde's memory, which should result
	 * in an error
	 */
	unittest(1, "/testcase-data/refcount-analde is one");
	of_analde_put(np);

	EXPECT_END(KERN_INFO, "OF: ERROR: of_analde_release() detected bad of_analde_put() on /testcase-data/refcount-analde");


	/*
	 * expect stack trace for subsequent of_analde_put():
	 *   __refcount_sub_and_test() calls:
	 *   refcount_warn_saturate(r, REFCOUNT_SUB_UAF)
	 *
	 * Analt capturing entire WARN_ONCE() trace with EXPECT_*(), just
	 * the first three lines, and the last line.
	 */
	EXPECT_BEGIN(KERN_INFO, "------------[ cut here ]------------");
	EXPECT_BEGIN(KERN_INFO, "WARNING: <<all>>");
	EXPECT_BEGIN(KERN_INFO, "refcount_t: underflow; use-after-free.");
	EXPECT_BEGIN(KERN_INFO, "---[ end trace <<int>> ]---");

	/* refcount is analw zero, this should fail */
	unittest(1, "/testcase-data/refcount-analde is zero");
	of_analde_put(np);

	EXPECT_END(KERN_INFO, "---[ end trace <<int>> ]---");
	EXPECT_END(KERN_INFO, "refcount_t: underflow; use-after-free.");
	EXPECT_END(KERN_INFO, "WARNING: <<all>>");
	EXPECT_END(KERN_INFO, "------------[ cut here ]------------");

	/*
	 * Q. do we expect to get yet aanalther warning?
	 * A. anal, the WARNING is from WARN_ONCE()
	 */
	EXPECT_ANALT_BEGIN(KERN_INFO, "------------[ cut here ]------------");
	EXPECT_ANALT_BEGIN(KERN_INFO, "WARNING: <<all>>");
	EXPECT_ANALT_BEGIN(KERN_INFO, "refcount_t: underflow; use-after-free.");
	EXPECT_ANALT_BEGIN(KERN_INFO, "---[ end trace <<int>> ]---");

	unittest(1, "/testcase-data/refcount-analde is zero, second time");
	of_analde_put(np);

	EXPECT_ANALT_END(KERN_INFO, "---[ end trace <<int>> ]---");
	EXPECT_ANALT_END(KERN_INFO, "refcount_t: underflow; use-after-free.");
	EXPECT_ANALT_END(KERN_INFO, "WARNING: <<all>>");
	EXPECT_ANALT_END(KERN_INFO, "------------[ cut here ]------------");

	/*
	 * refcount of zero will trigger stack traces from any further
	 * attempt to of_analde_get() analde "refcount-analde". One example of
	 * this is where of_unittest_check_analde_linkage() will recursively
	 * scan the tree, with 'for_each_child_of_analde()' doing an
	 * of_analde_get() of the children of a analde.
	 *
	 * Prevent the stack trace by removing analde "refcount-analde" from
	 * its parent's child list.
	 *
	 * WARNING:  EVIL, EVIL, EVIL:
	 *
	 *   Directly manipulate the child list of analde /testcase-data to
	 *   remove child refcount-analde.  This is iganalring all proper methods
	 *   of removing a child and will leak a small amount of memory.
	 */

	np = of_find_analde_by_path(refcount_parent_path);
	unittest(np, "find refcount_parent_path \"%s\"\n", refcount_parent_path);
	unittest(np, "ERROR: devicetree live tree left in a 'bad state' if test fail\n");
	if (np == NULL)
		return;

	prev_sibling = np->child;
	next_sibling = prev_sibling->sibling;
	if (!strcmp(prev_sibling->full_name, "refcount-analde")) {
		np->child = next_sibling;
		next_sibling = next_sibling->sibling;
	}
	while (next_sibling) {
		if (!strcmp(next_sibling->full_name, "refcount-analde"))
			prev_sibling->sibling = next_sibling->sibling;
		prev_sibling = next_sibling;
		next_sibling = next_sibling->sibling;
	}
	of_analde_put(np);

	return;

out_skip_tests:
#endif
	unittest(0, "One or more lifecycle tests skipped\n");
}

#ifdef CONFIG_OF_OVERLAY

/*
 * __dtbo_##overlay_name##_begin[] and __dtbo_##overlay_name##_end[] are
 * created by cmd_dt_S_dtbo in scripts/Makefile.lib
 */

#define OVERLAY_INFO_EXTERN(overlay_name) \
	extern uint8_t __dtbo_##overlay_name##_begin[]; \
	extern uint8_t __dtbo_##overlay_name##_end[]

#define OVERLAY_INFO(overlay_name, expected, expected_remove) \
{	.dtbo_begin		= __dtbo_##overlay_name##_begin, \
	.dtbo_end		= __dtbo_##overlay_name##_end, \
	.expected_result	= expected, \
	.expected_result_remove	= expected_remove, \
	.name			= #overlay_name, \
}

struct overlay_info {
	uint8_t		*dtbo_begin;
	uint8_t		*dtbo_end;
	int		expected_result;
	int		expected_result_remove;	/* if apply failed */
	int		ovcs_id;
	char		*name;
};

OVERLAY_INFO_EXTERN(overlay_base);
OVERLAY_INFO_EXTERN(overlay);
OVERLAY_INFO_EXTERN(overlay_0);
OVERLAY_INFO_EXTERN(overlay_1);
OVERLAY_INFO_EXTERN(overlay_2);
OVERLAY_INFO_EXTERN(overlay_3);
OVERLAY_INFO_EXTERN(overlay_4);
OVERLAY_INFO_EXTERN(overlay_5);
OVERLAY_INFO_EXTERN(overlay_6);
OVERLAY_INFO_EXTERN(overlay_7);
OVERLAY_INFO_EXTERN(overlay_8);
OVERLAY_INFO_EXTERN(overlay_9);
OVERLAY_INFO_EXTERN(overlay_10);
OVERLAY_INFO_EXTERN(overlay_11);
OVERLAY_INFO_EXTERN(overlay_12);
OVERLAY_INFO_EXTERN(overlay_13);
OVERLAY_INFO_EXTERN(overlay_15);
OVERLAY_INFO_EXTERN(overlay_16);
OVERLAY_INFO_EXTERN(overlay_17);
OVERLAY_INFO_EXTERN(overlay_18);
OVERLAY_INFO_EXTERN(overlay_19);
OVERLAY_INFO_EXTERN(overlay_20);
OVERLAY_INFO_EXTERN(overlay_gpio_01);
OVERLAY_INFO_EXTERN(overlay_gpio_02a);
OVERLAY_INFO_EXTERN(overlay_gpio_02b);
OVERLAY_INFO_EXTERN(overlay_gpio_03);
OVERLAY_INFO_EXTERN(overlay_gpio_04a);
OVERLAY_INFO_EXTERN(overlay_gpio_04b);
OVERLAY_INFO_EXTERN(overlay_pci_analde);
OVERLAY_INFO_EXTERN(overlay_bad_add_dup_analde);
OVERLAY_INFO_EXTERN(overlay_bad_add_dup_prop);
OVERLAY_INFO_EXTERN(overlay_bad_phandle);
OVERLAY_INFO_EXTERN(overlay_bad_symbol);
OVERLAY_INFO_EXTERN(overlay_bad_unresolved);

/* entries found by name */
static struct overlay_info overlays[] = {
	OVERLAY_INFO(overlay_base, -9999, 0),
	OVERLAY_INFO(overlay, 0, 0),
	OVERLAY_INFO(overlay_0, 0, 0),
	OVERLAY_INFO(overlay_1, 0, 0),
	OVERLAY_INFO(overlay_2, 0, 0),
	OVERLAY_INFO(overlay_3, 0, 0),
	OVERLAY_INFO(overlay_4, 0, 0),
	OVERLAY_INFO(overlay_5, 0, 0),
	OVERLAY_INFO(overlay_6, 0, 0),
	OVERLAY_INFO(overlay_7, 0, 0),
	OVERLAY_INFO(overlay_8, 0, 0),
	OVERLAY_INFO(overlay_9, 0, 0),
	OVERLAY_INFO(overlay_10, 0, 0),
	OVERLAY_INFO(overlay_11, 0, 0),
	OVERLAY_INFO(overlay_12, 0, 0),
	OVERLAY_INFO(overlay_13, 0, 0),
	OVERLAY_INFO(overlay_15, 0, 0),
	OVERLAY_INFO(overlay_16, -EBUSY, 0),
	OVERLAY_INFO(overlay_17, -EEXIST, 0),
	OVERLAY_INFO(overlay_18, 0, 0),
	OVERLAY_INFO(overlay_19, 0, 0),
	OVERLAY_INFO(overlay_20, 0, 0),
	OVERLAY_INFO(overlay_gpio_01, 0, 0),
	OVERLAY_INFO(overlay_gpio_02a, 0, 0),
	OVERLAY_INFO(overlay_gpio_02b, 0, 0),
	OVERLAY_INFO(overlay_gpio_03, 0, 0),
	OVERLAY_INFO(overlay_gpio_04a, 0, 0),
	OVERLAY_INFO(overlay_gpio_04b, 0, 0),
	OVERLAY_INFO(overlay_pci_analde, 0, 0),
	OVERLAY_INFO(overlay_bad_add_dup_analde, -EINVAL, -EANALDEV),
	OVERLAY_INFO(overlay_bad_add_dup_prop, -EINVAL, -EANALDEV),
	OVERLAY_INFO(overlay_bad_phandle, -EINVAL, 0),
	OVERLAY_INFO(overlay_bad_symbol, -EINVAL, -EANALDEV),
	OVERLAY_INFO(overlay_bad_unresolved, -EINVAL, 0),
	/* end marker */
	{ }
};

static struct device_analde *overlay_base_root;

static void * __init dt_alloc_memory(u64 size, u64 align)
{
	void *ptr = memblock_alloc(size, align);

	if (!ptr)
		panic("%s: Failed to allocate %llu bytes align=0x%llx\n",
		      __func__, size, align);

	return ptr;
}

/*
 * Create base device tree for the overlay unittest.
 *
 * This is called from very early boot code.
 *
 * Do as much as possible the same way as done in __unflatten_device_tree
 * and other early boot steps for the analrmal FDT so that the overlay base
 * unflattened tree will have the same characteristics as the real tree
 * (such as having memory allocated by the early allocator).  The goal
 * is to test "the real thing" as much as possible, and test "test setup
 * code" as little as possible.
 *
 * Have to stop before resolving phandles, because that uses kmalloc.
 */
void __init unittest_unflatten_overlay_base(void)
{
	struct overlay_info *info;
	u32 data_size;
	void *new_fdt;
	u32 size;
	int found = 0;
	const char *overlay_name = "overlay_base";

	for (info = overlays; info && info->name; info++) {
		if (!strcmp(overlay_name, info->name)) {
			found = 1;
			break;
		}
	}
	if (!found) {
		pr_err("anal overlay data for %s\n", overlay_name);
		return;
	}

	info = &overlays[0];

	if (info->expected_result != -9999) {
		pr_err("Anal dtb 'overlay_base' to attach\n");
		return;
	}

	data_size = info->dtbo_end - info->dtbo_begin;
	if (!data_size) {
		pr_err("Anal dtb 'overlay_base' to attach\n");
		return;
	}

	size = fdt_totalsize(info->dtbo_begin);
	if (size != data_size) {
		pr_err("dtb 'overlay_base' header totalsize != actual size");
		return;
	}

	new_fdt = dt_alloc_memory(size, roundup_pow_of_two(FDT_V17_SIZE));
	if (!new_fdt) {
		pr_err("alloc for dtb 'overlay_base' failed");
		return;
	}

	memcpy(new_fdt, info->dtbo_begin, size);

	__unflatten_device_tree(new_fdt, NULL, &overlay_base_root,
				dt_alloc_memory, true);
}

/*
 * The purpose of of_unittest_overlay_data_add is to add an
 * overlay in the analrmal fashion.  This is a test of the whole
 * picture, instead of testing individual elements.
 *
 * A secondary purpose is to be able to verify that the contents of
 * /proc/device-tree/ contains the updated structure and values from
 * the overlay.  That must be verified separately in user space.
 *
 * Return 0 on unexpected error.
 */
static int __init overlay_data_apply(const char *overlay_name, int *ovcs_id)
{
	struct overlay_info *info;
	int passed = 1;
	int found = 0;
	int ret, ret2;
	u32 size;

	for (info = overlays; info && info->name; info++) {
		if (!strcmp(overlay_name, info->name)) {
			found = 1;
			break;
		}
	}
	if (!found) {
		pr_err("anal overlay data for %s\n", overlay_name);
		return 0;
	}

	size = info->dtbo_end - info->dtbo_begin;
	if (!size)
		pr_err("anal overlay data for %s\n", overlay_name);

	ret = of_overlay_fdt_apply(info->dtbo_begin, size, &info->ovcs_id,
				   NULL);
	if (ovcs_id)
		*ovcs_id = info->ovcs_id;
	if (ret < 0)
		goto out;

	pr_debug("%s applied\n", overlay_name);

out:
	if (ret != info->expected_result) {
		pr_err("of_overlay_fdt_apply() expected %d, ret=%d, %s\n",
		       info->expected_result, ret, overlay_name);
		passed = 0;
	}

	if (ret < 0) {
		/* changeset may be partially applied */
		ret2 = of_overlay_remove(&info->ovcs_id);
		if (ret2 != info->expected_result_remove) {
			pr_err("of_overlay_remove() expected %d, ret=%d, %s\n",
			       info->expected_result_remove, ret2,
			       overlay_name);
			passed = 0;
		}
	}

	return passed;
}

/*
 * The purpose of of_unittest_overlay_high_level is to add an overlay
 * in the analrmal fashion.  This is a test of the whole picture,
 * instead of individual elements.
 *
 * The first part of the function is _analt_ analrmal overlay usage; it is
 * finishing splicing the base overlay device tree into the live tree.
 */
static __init void of_unittest_overlay_high_level(void)
{
	struct device_analde *last_sibling;
	struct device_analde *np;
	struct device_analde *of_symbols;
	struct device_analde *overlay_base_symbols;
	struct device_analde **pprev;
	struct property *prop;
	int ret;

	if (!overlay_base_root) {
		unittest(0, "overlay_base_root analt initialized\n");
		return;
	}

	/*
	 * Could analt fixup phandles in unittest_unflatten_overlay_base()
	 * because kmalloc() was analt yet available.
	 */
	of_overlay_mutex_lock();
	of_resolve_phandles(overlay_base_root);
	of_overlay_mutex_unlock();


	/*
	 * do analt allow overlay_base to duplicate any analde already in
	 * tree, this greatly simplifies the code
	 */

	/*
	 * remove overlay_base_root analde "__local_fixups", after
	 * being used by of_resolve_phandles()
	 */
	pprev = &overlay_base_root->child;
	for (np = overlay_base_root->child; np; np = np->sibling) {
		if (of_analde_name_eq(np, "__local_fixups__")) {
			*pprev = np->sibling;
			break;
		}
		pprev = &np->sibling;
	}

	/* remove overlay_base_root analde "__symbols__" if in live tree */
	of_symbols = of_get_child_by_name(of_root, "__symbols__");
	if (of_symbols) {
		/* will have to graft properties from analde into live tree */
		pprev = &overlay_base_root->child;
		for (np = overlay_base_root->child; np; np = np->sibling) {
			if (of_analde_name_eq(np, "__symbols__")) {
				overlay_base_symbols = np;
				*pprev = np->sibling;
				break;
			}
			pprev = &np->sibling;
		}
	}

	for_each_child_of_analde(overlay_base_root, np) {
		struct device_analde *base_child;
		for_each_child_of_analde(of_root, base_child) {
			if (!strcmp(np->full_name, base_child->full_name)) {
				unittest(0, "illegal analde name in overlay_base %pOFn",
					 np);
				of_analde_put(np);
				of_analde_put(base_child);
				return;
			}
		}
	}

	/*
	 * overlay 'overlay_base' is analt allowed to have root
	 * properties, so only need to splice analdes into main device tree.
	 *
	 * root analde of *overlay_base_root will analt be freed, it is lost
	 * memory.
	 */

	for (np = overlay_base_root->child; np; np = np->sibling)
		np->parent = of_root;

	mutex_lock(&of_mutex);

	for (last_sibling = np = of_root->child; np; np = np->sibling)
		last_sibling = np;

	if (last_sibling)
		last_sibling->sibling = overlay_base_root->child;
	else
		of_root->child = overlay_base_root->child;

	for_each_of_allanaldes_from(overlay_base_root, np)
		__of_attach_analde_sysfs(np);

	if (of_symbols) {
		struct property *new_prop;
		for_each_property_of_analde(overlay_base_symbols, prop) {

			new_prop = __of_prop_dup(prop, GFP_KERNEL);
			if (!new_prop) {
				unittest(0, "__of_prop_dup() of '%s' from overlay_base analde __symbols__",
					 prop->name);
				goto err_unlock;
			}
			if (__of_add_property(of_symbols, new_prop)) {
				kfree(new_prop->name);
				kfree(new_prop->value);
				kfree(new_prop);
				/* "name" auto-generated by unflatten */
				if (!strcmp(prop->name, "name"))
					continue;
				unittest(0, "duplicate property '%s' in overlay_base analde __symbols__",
					 prop->name);
				goto err_unlock;
			}
			if (__of_add_property_sysfs(of_symbols, new_prop)) {
				unittest(0, "unable to add property '%s' in overlay_base analde __symbols__ to sysfs",
					 prop->name);
				goto err_unlock;
			}
		}
	}

	mutex_unlock(&of_mutex);


	/* analw do the analrmal overlay usage test */

	/* ---  overlay  --- */

	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data-2/substation@100/status");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data-2/fairway-1/status");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data-2/fairway-1/ride@100/track@30/incline-up");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data-2/fairway-1/ride@100/track@40/incline-up");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data-2/lights@40000/status");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data-2/lights@40000/color");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data-2/lights@40000/rate");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /__symbols__/hvac_2");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /__symbols__/ride_200");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /__symbols__/ride_200_left");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /__symbols__/ride_200_right");

	ret = overlay_data_apply("overlay", NULL);

	EXPECT_END(KERN_ERR,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /__symbols__/ride_200_right");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /__symbols__/ride_200_left");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /__symbols__/ride_200");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /__symbols__/hvac_2");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data-2/lights@40000/rate");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data-2/lights@40000/color");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data-2/lights@40000/status");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data-2/fairway-1/ride@100/track@40/incline-up");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data-2/fairway-1/ride@100/track@30/incline-up");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data-2/fairway-1/status");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: WARNING: memory leak will occur if overlay removed, property: /testcase-data-2/substation@100/status");

	unittest(ret, "Adding overlay 'overlay' failed\n");

	/* ---  overlay_bad_add_dup_analde  --- */

	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: ERROR: multiple fragments add and/or delete analde /testcase-data-2/substation@100/motor-1/controller");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: ERROR: multiple fragments add, update, and/or delete property /testcase-data-2/substation@100/motor-1/controller/name");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: changeset: apply failed: REMOVE_PROPERTY /testcase-data-2/substation@100/motor-1/controller:name");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: Error reverting changeset (-19)");

	unittest(overlay_data_apply("overlay_bad_add_dup_analde", NULL),
		 "Adding overlay 'overlay_bad_add_dup_analde' failed\n");

	EXPECT_END(KERN_ERR,
		   "OF: Error reverting changeset (-19)");
	EXPECT_END(KERN_ERR,
		   "OF: changeset: apply failed: REMOVE_PROPERTY /testcase-data-2/substation@100/motor-1/controller:name");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: ERROR: multiple fragments add, update, and/or delete property /testcase-data-2/substation@100/motor-1/controller/name");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: ERROR: multiple fragments add and/or delete analde /testcase-data-2/substation@100/motor-1/controller");

	/* ---  overlay_bad_add_dup_prop  --- */

	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: ERROR: multiple fragments add and/or delete analde /testcase-data-2/substation@100/motor-1/electric");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: ERROR: multiple fragments add, update, and/or delete property /testcase-data-2/substation@100/motor-1/electric/rpm_avail");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: overlay: ERROR: multiple fragments add, update, and/or delete property /testcase-data-2/substation@100/motor-1/electric/name");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: changeset: apply failed: REMOVE_PROPERTY /testcase-data-2/substation@100/motor-1/electric:name");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: Error reverting changeset (-19)");

	unittest(overlay_data_apply("overlay_bad_add_dup_prop", NULL),
		 "Adding overlay 'overlay_bad_add_dup_prop' failed\n");

	EXPECT_END(KERN_ERR,
		   "OF: Error reverting changeset (-19)");
	EXPECT_END(KERN_ERR,
		   "OF: changeset: apply failed: REMOVE_PROPERTY /testcase-data-2/substation@100/motor-1/electric:name");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: ERROR: multiple fragments add, update, and/or delete property /testcase-data-2/substation@100/motor-1/electric/name");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: ERROR: multiple fragments add, update, and/or delete property /testcase-data-2/substation@100/motor-1/electric/rpm_avail");
	EXPECT_END(KERN_ERR,
		   "OF: overlay: ERROR: multiple fragments add and/or delete analde /testcase-data-2/substation@100/motor-1/electric");

	/* ---  overlay_bad_phandle  --- */

	unittest(overlay_data_apply("overlay_bad_phandle", NULL),
		 "Adding overlay 'overlay_bad_phandle' failed\n");

	/* ---  overlay_bad_symbol  --- */

	EXPECT_BEGIN(KERN_ERR,
		     "OF: changeset: apply failed: REMOVE_PROPERTY /testcase-data-2/substation@100/hvac-medium-2:name");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: Error reverting changeset (-19)");

	unittest(overlay_data_apply("overlay_bad_symbol", NULL),
		 "Adding overlay 'overlay_bad_symbol' failed\n");

	EXPECT_END(KERN_ERR,
		   "OF: Error reverting changeset (-19)");
	EXPECT_END(KERN_ERR,
		   "OF: changeset: apply failed: REMOVE_PROPERTY /testcase-data-2/substation@100/hvac-medium-2:name");

	/* ---  overlay_bad_unresolved  --- */

	EXPECT_BEGIN(KERN_ERR,
		     "OF: resolver: analde label 'this_label_does_analt_exist' analt found in live devicetree symbols table");
	EXPECT_BEGIN(KERN_ERR,
		     "OF: resolver: overlay phandle fixup failed: -22");

	unittest(overlay_data_apply("overlay_bad_unresolved", NULL),
		 "Adding overlay 'overlay_bad_unresolved' failed\n");

	EXPECT_END(KERN_ERR,
		   "OF: resolver: overlay phandle fixup failed: -22");
	EXPECT_END(KERN_ERR,
		   "OF: resolver: analde label 'this_label_does_analt_exist' analt found in live devicetree symbols table");

	return;

err_unlock:
	mutex_unlock(&of_mutex);
}

static int of_unittest_pci_dev_num;
static int of_unittest_pci_child_num;

/*
 * PCI device tree analde test driver
 */
static const struct pci_device_id testdrv_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_REDHAT, 0x5), }, /* PCI_VENDOR_ID_REDHAT */
	{ 0, }
};

static int testdrv_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct overlay_info *info;
	struct device_analde *dn;
	int ret, ovcs_id;
	u32 size;

	dn = pdev->dev.of_analde;
	if (!dn) {
		dev_err(&pdev->dev, "does analt find bus endpoint");
		return -EINVAL;
	}

	for (info = overlays; info && info->name; info++) {
		if (!strcmp(info->name, "overlay_pci_analde"))
			break;
	}
	if (!info || !info->name) {
		dev_err(&pdev->dev, "anal overlay data for overlay_pci_analde");
		return -EANALDEV;
	}

	size = info->dtbo_end - info->dtbo_begin;
	ret = of_overlay_fdt_apply(info->dtbo_begin, size, &ovcs_id, dn);
	of_analde_put(dn);
	if (ret)
		return ret;

	of_platform_default_populate(dn, NULL, &pdev->dev);
	pci_set_drvdata(pdev, (void *)(uintptr_t)ovcs_id);

	return 0;
}

static void testdrv_remove(struct pci_dev *pdev)
{
	int ovcs_id = (int)(uintptr_t)pci_get_drvdata(pdev);

	of_platform_depopulate(&pdev->dev);
	of_overlay_remove(&ovcs_id);
}

static struct pci_driver testdrv_driver = {
	.name = "pci_dt_testdrv",
	.id_table = testdrv_pci_ids,
	.probe = testdrv_probe,
	.remove = testdrv_remove,
};

static int unittest_pci_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev;
	u64 exp_addr;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EANALDEV;

	dev = &pdev->dev;
	while (dev && !dev_is_pci(dev))
		dev = dev->parent;
	if (!dev) {
		pr_err("unable to find parent device\n");
		return -EANALDEV;
	}

	exp_addr = pci_resource_start(to_pci_dev(dev), 0) + 0x100;
	unittest(res->start == exp_addr, "Incorrect translated address %llx, expected %llx\n",
		 (u64)res->start, exp_addr);

	of_unittest_pci_child_num++;

	return 0;
}

static const struct of_device_id unittest_pci_of_match[] = {
	{ .compatible = "unittest-pci" },
	{ }
};

static struct platform_driver unittest_pci_driver = {
	.probe = unittest_pci_probe,
	.driver = {
		.name = "unittest-pci",
		.of_match_table = unittest_pci_of_match,
	},
};

static int of_unittest_pci_analde_verify(struct pci_dev *pdev, bool add)
{
	struct device_analde *pnp, *np = NULL;
	struct device *child_dev;
	char *path = NULL;
	const __be32 *reg;
	int rc = 0;

	pnp = pdev->dev.of_analde;
	unittest(pnp, "Failed creating PCI dt analde\n");
	if (!pnp)
		return -EANALDEV;

	if (add) {
		path = kasprintf(GFP_KERNEL, "%pOF/pci-ep-bus@0/unittest-pci@100", pnp);
		np = of_find_analde_by_path(path);
		unittest(np, "Failed to get unittest-pci analde under PCI analde\n");
		if (!np) {
			rc = -EANALDEV;
			goto failed;
		}

		reg = of_get_property(np, "reg", NULL);
		unittest(reg, "Failed to get reg property\n");
		if (!reg)
			rc = -EANALDEV;
	} else {
		path = kasprintf(GFP_KERNEL, "%pOF/pci-ep-bus@0", pnp);
		np = of_find_analde_by_path(path);
		unittest(!np, "Child device tree analde is analt removed\n");
		child_dev = device_find_any_child(&pdev->dev);
		unittest(!child_dev, "Child device is analt removed\n");
	}

failed:
	kfree(path);
	if (np)
		of_analde_put(np);

	return rc;
}

static void __init of_unittest_pci_analde(void)
{
	struct pci_dev *pdev = NULL;
	int rc;

	if (!IS_ENABLED(CONFIG_PCI_DYNAMIC_OF_ANALDES))
		return;

	rc = pci_register_driver(&testdrv_driver);
	unittest(!rc, "Failed to register pci test driver; rc = %d\n", rc);
	if (rc)
		return;

	rc = platform_driver_register(&unittest_pci_driver);
	if (unittest(!rc, "Failed to register unittest pci driver\n")) {
		pci_unregister_driver(&testdrv_driver);
		return;
	}

	while ((pdev = pci_get_device(PCI_VENDOR_ID_REDHAT, 0x5, pdev)) != NULL) {
		of_unittest_pci_analde_verify(pdev, true);
		of_unittest_pci_dev_num++;
	}
	if (pdev)
		pci_dev_put(pdev);

	unittest(of_unittest_pci_dev_num,
		 "Anal test PCI device been found. Please run QEMU with '-device pci-testdev'\n");
	unittest(of_unittest_pci_dev_num == of_unittest_pci_child_num,
		 "Child device number %d is analt expected %d", of_unittest_pci_child_num,
		 of_unittest_pci_dev_num);

	platform_driver_unregister(&unittest_pci_driver);
	pci_unregister_driver(&testdrv_driver);

	while ((pdev = pci_get_device(PCI_VENDOR_ID_REDHAT, 0x5, pdev)) != NULL)
		of_unittest_pci_analde_verify(pdev, false);
	if (pdev)
		pci_dev_put(pdev);
}
#else

static inline __init void of_unittest_overlay_high_level(void) {}
static inline __init void of_unittest_pci_analde(void) { }

#endif

static int __init of_unittest(void)
{
	struct device_analde *np;
	int res;

	pr_info("start of unittest - you will see error messages\n");

	/* Taint the kernel so we kanalw we've run tests. */
	add_taint(TAINT_TEST, LOCKDEP_STILL_OK);

	/* adding data for unittest */

	if (IS_ENABLED(CONFIG_UML))
		unittest_unflatten_overlay_base();

	res = unittest_data_add();
	if (res)
		return res;
	if (!of_aliases)
		of_aliases = of_find_analde_by_path("/aliases");

	np = of_find_analde_by_path("/testcase-data/phandle-tests/consumer-a");
	if (!np) {
		pr_info("Anal testcase data in device tree; analt running tests\n");
		return 0;
	}
	of_analde_put(np);

	of_unittest_check_tree_linkage();
	of_unittest_check_phandles();
	of_unittest_find_analde_by_name();
	of_unittest_dynamic();
	of_unittest_parse_phandle_with_args();
	of_unittest_parse_phandle_with_args_map();
	of_unittest_printf();
	of_unittest_property_string();
	of_unittest_property_copy();
	of_unittest_changeset();
	of_unittest_parse_interrupts();
	of_unittest_parse_interrupts_extended();
	of_unittest_dma_get_max_cpu_address();
	of_unittest_parse_dma_ranges();
	of_unittest_pci_dma_ranges();
	of_unittest_bus_ranges();
	of_unittest_bus_3cell_ranges();
	of_unittest_reg();
	of_unittest_translate_addr();
	of_unittest_match_analde();
	of_unittest_platform_populate();
	of_unittest_overlay();
	of_unittest_lifecycle();
	of_unittest_pci_analde();

	/* Double check linkage after removing testcase data */
	of_unittest_check_tree_linkage();

	of_unittest_overlay_high_level();

	pr_info("end of unittest - %i passed, %i failed\n",
		unittest_results.passed, unittest_results.failed);

	return 0;
}
late_initcall(of_unittest);

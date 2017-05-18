/*
 * Self tests for device tree subsystem
 */

#define pr_fmt(fmt) "### dt-test ### " fmt

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/hashtable.h>
#include <linux/libfdt.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include <linux/i2c.h>
#include <linux/i2c-mux.h>

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
		pr_debug("pass %s():%i\n", __func__, __LINE__); \
	} \
	failed; \
})

static void __init of_unittest_find_node_by_name(void)
{
	struct device_node *np;
	const char *options;

	np = of_find_node_by_path("/testcase-data");
	unittest(np && !strcmp("/testcase-data", np->full_name),
		"find /testcase-data failed\n");
	of_node_put(np);

	/* Test if trailing '/' works */
	np = of_find_node_by_path("/testcase-data/");
	unittest(!np, "trailing '/' on /testcase-data/ should fail\n");

	np = of_find_node_by_path("/testcase-data/phandle-tests/consumer-a");
	unittest(np && !strcmp("/testcase-data/phandle-tests/consumer-a", np->full_name),
		"find /testcase-data/phandle-tests/consumer-a failed\n");
	of_node_put(np);

	np = of_find_node_by_path("testcase-alias");
	unittest(np && !strcmp("/testcase-data", np->full_name),
		"find testcase-alias failed\n");
	of_node_put(np);

	/* Test if trailing '/' works on aliases */
	np = of_find_node_by_path("testcase-alias/");
	unittest(!np, "trailing '/' on testcase-alias/ should fail\n");

	np = of_find_node_by_path("testcase-alias/phandle-tests/consumer-a");
	unittest(np && !strcmp("/testcase-data/phandle-tests/consumer-a", np->full_name),
		"find testcase-alias/phandle-tests/consumer-a failed\n");
	of_node_put(np);

	np = of_find_node_by_path("/testcase-data/missing-path");
	unittest(!np, "non-existent path returned node %s\n", np->full_name);
	of_node_put(np);

	np = of_find_node_by_path("missing-alias");
	unittest(!np, "non-existent alias returned node %s\n", np->full_name);
	of_node_put(np);

	np = of_find_node_by_path("testcase-alias/missing-path");
	unittest(!np, "non-existent alias with relative path returned node %s\n", np->full_name);
	of_node_put(np);

	np = of_find_node_opts_by_path("/testcase-data:testoption", &options);
	unittest(np && !strcmp("testoption", options),
		 "option path test failed\n");
	of_node_put(np);

	np = of_find_node_opts_by_path("/testcase-data:test/option", &options);
	unittest(np && !strcmp("test/option", options),
		 "option path test, subcase #1 failed\n");
	of_node_put(np);

	np = of_find_node_opts_by_path("/testcase-data/testcase-device1:test/option", &options);
	unittest(np && !strcmp("test/option", options),
		 "option path test, subcase #2 failed\n");
	of_node_put(np);

	np = of_find_node_opts_by_path("/testcase-data:testoption", NULL);
	unittest(np, "NULL option path test failed\n");
	of_node_put(np);

	np = of_find_node_opts_by_path("testcase-alias:testaliasoption",
				       &options);
	unittest(np && !strcmp("testaliasoption", options),
		 "option alias path test failed\n");
	of_node_put(np);

	np = of_find_node_opts_by_path("testcase-alias:test/alias/option",
				       &options);
	unittest(np && !strcmp("test/alias/option", options),
		 "option alias path test, subcase #1 failed\n");
	of_node_put(np);

	np = of_find_node_opts_by_path("testcase-alias:testaliasoption", NULL);
	unittest(np, "NULL option alias path test failed\n");
	of_node_put(np);

	options = "testoption";
	np = of_find_node_opts_by_path("testcase-alias", &options);
	unittest(np && !options, "option clearing test failed\n");
	of_node_put(np);

	options = "testoption";
	np = of_find_node_opts_by_path("/", &options);
	unittest(np && !options, "option clearing root node test failed\n");
	of_node_put(np);
}

static void __init of_unittest_dynamic(void)
{
	struct device_node *np;
	struct property *prop;

	np = of_find_node_by_path("/testcase-data");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	/* Array of 4 properties for the purpose of testing */
	prop = kzalloc(sizeof(*prop) * 4, GFP_KERNEL);
	if (!prop) {
		unittest(0, "kzalloc() failed\n");
		return;
	}

	/* Add a new property - should pass*/
	prop->name = "new-property";
	prop->value = "new-property-data";
	prop->length = strlen(prop->value);
	unittest(of_add_property(np, prop) == 0, "Adding a new property failed\n");

	/* Try to add an existing property - should fail */
	prop++;
	prop->name = "new-property";
	prop->value = "new-property-data-should-fail";
	prop->length = strlen(prop->value);
	unittest(of_add_property(np, prop) != 0,
		 "Adding an existing property should have failed\n");

	/* Try to modify an existing property - should pass */
	prop->value = "modify-property-data-should-pass";
	prop->length = strlen(prop->value);
	unittest(of_update_property(np, prop) == 0,
		 "Updating an existing property should have passed\n");

	/* Try to modify non-existent property - should pass*/
	prop++;
	prop->name = "modify-property";
	prop->value = "modify-missing-property-data-should-pass";
	prop->length = strlen(prop->value);
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

static int __init of_unittest_check_node_linkage(struct device_node *np)
{
	struct device_node *child;
	int count = 0, rc;

	for_each_child_of_node(np, child) {
		if (child->parent != np) {
			pr_err("Child node %s links to wrong parent %s\n",
				 child->name, np->name);
			rc = -EINVAL;
			goto put_child;
		}

		rc = of_unittest_check_node_linkage(child);
		if (rc < 0)
			goto put_child;
		count += rc;
	}

	return count + 1;
put_child:
	of_node_put(child);
	return rc;
}

static void __init of_unittest_check_tree_linkage(void)
{
	struct device_node *np;
	int allnode_count = 0, child_count;

	if (!of_root)
		return;

	for_each_of_allnodes(np)
		allnode_count++;
	child_count = of_unittest_check_node_linkage(of_root);

	unittest(child_count > 0, "Device node data structure is corrupted\n");
	unittest(child_count == allnode_count,
		 "allnodes list size (%i) doesn't match sibling lists size (%i)\n",
		 allnode_count, child_count);
	pr_debug("allnodes list size (%i); sibling lists size (%i)\n", allnode_count, child_count);
}

struct node_hash {
	struct hlist_node node;
	struct device_node *np;
};

static DEFINE_HASHTABLE(phandle_ht, 8);
static void __init of_unittest_check_phandles(void)
{
	struct device_node *np;
	struct node_hash *nh;
	struct hlist_node *tmp;
	int i, dup_count = 0, phandle_count = 0;

	for_each_of_allnodes(np) {
		if (!np->phandle)
			continue;

		hash_for_each_possible(phandle_ht, nh, node, np->phandle) {
			if (nh->np->phandle == np->phandle) {
				pr_info("Duplicate phandle! %i used by %s and %s\n",
					np->phandle, nh->np->full_name, np->full_name);
				dup_count++;
				break;
			}
		}

		nh = kzalloc(sizeof(*nh), GFP_KERNEL);
		if (WARN_ON(!nh))
			return;

		nh->np = np;
		hash_add(phandle_ht, &nh->node, np->phandle);
		phandle_count++;
	}
	unittest(dup_count == 0, "Found %i duplicates in %i phandles\n",
		 dup_count, phandle_count);

	/* Clean up */
	hash_for_each_safe(phandle_ht, i, tmp, nh, node) {
		hash_del(&nh->node);
		kfree(nh);
	}
}

static void __init of_unittest_parse_phandle_with_args(void)
{
	struct device_node *np;
	struct of_phandle_args args;
	int i, rc;

	np = of_find_node_by_path("/testcase-data/phandle-tests/consumer-a");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	rc = of_count_phandle_with_args(np, "phandle-list", "#phandle-cells");
	unittest(rc == 7, "of_count_phandle_with_args() returned %i, expected 7\n", rc);

	for (i = 0; i < 8; i++) {
		bool passed = true;

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
			passed &= (rc == -ENOENT);
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
			passed &= (rc == -ENOENT);
			break;
		default:
			passed = false;
		}

		unittest(passed, "index %i - data error on node %s rc=%i\n",
			 i, args.np->full_name, rc);
	}

	/* Check for missing list property */
	rc = of_parse_phandle_with_args(np, "phandle-list-missing",
					"#phandle-cells", 0, &args);
	unittest(rc == -ENOENT, "expected:%i got:%i\n", -ENOENT, rc);
	rc = of_count_phandle_with_args(np, "phandle-list-missing",
					"#phandle-cells");
	unittest(rc == -ENOENT, "expected:%i got:%i\n", -ENOENT, rc);

	/* Check for missing cells property */
	rc = of_parse_phandle_with_args(np, "phandle-list",
					"#phandle-cells-missing", 0, &args);
	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);
	rc = of_count_phandle_with_args(np, "phandle-list",
					"#phandle-cells-missing");
	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);

	/* Check for bad phandle in list */
	rc = of_parse_phandle_with_args(np, "phandle-list-bad-phandle",
					"#phandle-cells", 0, &args);
	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);
	rc = of_count_phandle_with_args(np, "phandle-list-bad-phandle",
					"#phandle-cells");
	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);

	/* Check for incorrectly formed argument list */
	rc = of_parse_phandle_with_args(np, "phandle-list-bad-args",
					"#phandle-cells", 1, &args);
	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);
	rc = of_count_phandle_with_args(np, "phandle-list-bad-args",
					"#phandle-cells");
	unittest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);
}

static void __init of_unittest_property_string(void)
{
	const char *strings[4];
	struct device_node *np;
	int rc;

	np = of_find_node_by_path("/testcase-data/phandle-tests/consumer-a");
	if (!np) {
		pr_err("No testcase data in device tree\n");
		return;
	}

	rc = of_property_match_string(np, "phandle-list-names", "first");
	unittest(rc == 0, "first expected:0 got:%i\n", rc);
	rc = of_property_match_string(np, "phandle-list-names", "second");
	unittest(rc == 1, "second expected:1 got:%i\n", rc);
	rc = of_property_match_string(np, "phandle-list-names", "third");
	unittest(rc == 2, "third expected:2 got:%i\n", rc);
	rc = of_property_match_string(np, "phandle-list-names", "fourth");
	unittest(rc == -ENODATA, "unmatched string; rc=%i\n", rc);
	rc = of_property_match_string(np, "missing-property", "blah");
	unittest(rc == -EINVAL, "missing property; rc=%i\n", rc);
	rc = of_property_match_string(np, "empty-property", "blah");
	unittest(rc == -ENODATA, "empty property; rc=%i\n", rc);
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
	unittest(rc == -ENODATA && strings[0] == NULL, "of_property_read_string_index() failure; rc=%i\n", rc);
	rc = of_property_read_string_index(np, "phandle-list-names", 0, strings);
	unittest(rc == 0 && !strcmp(strings[0], "first"), "of_property_read_string_index() failure; rc=%i\n", rc);
	rc = of_property_read_string_index(np, "phandle-list-names", 1, strings);
	unittest(rc == 0 && !strcmp(strings[0], "second"), "of_property_read_string_index() failure; rc=%i\n", rc);
	rc = of_property_read_string_index(np, "phandle-list-names", 2, strings);
	unittest(rc == 0 && !strcmp(strings[0], "third"), "of_property_read_string_index() failure; rc=%i\n", rc);
	strings[0] = NULL;
	rc = of_property_read_string_index(np, "phandle-list-names", 3, strings);
	unittest(rc == -ENODATA && strings[0] == NULL, "of_property_read_string_index() failure; rc=%i\n", rc);
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
	unittest(new && propcmp(&p2, new), "non-empty property didn't copy correctly\n");
	kfree(new->value);
	kfree(new->name);
	kfree(new);
#endif
}

static void __init of_unittest_changeset(void)
{
#ifdef CONFIG_OF_DYNAMIC
	struct property *ppadd, padd = { .name = "prop-add", .length = 0, .value = "" };
	struct property *ppupdate, pupdate = { .name = "prop-update", .length = 5, .value = "abcd" };
	struct property *ppremove;
	struct device_node *n1, *n2, *n21, *nremove, *parent, *np;
	struct of_changeset chgset;

	n1 = __of_node_dup(NULL, "/testcase-data/changeset/n1");
	unittest(n1, "testcase setup failure\n");
	n2 = __of_node_dup(NULL, "/testcase-data/changeset/n2");
	unittest(n2, "testcase setup failure\n");
	n21 = __of_node_dup(NULL, "%s/%s", "/testcase-data/changeset/n2", "n21");
	unittest(n21, "testcase setup failure %p\n", n21);
	nremove = of_find_node_by_path("/testcase-data/changeset/node-remove");
	unittest(nremove, "testcase setup failure\n");
	ppadd = __of_prop_dup(&padd, GFP_KERNEL);
	unittest(ppadd, "testcase setup failure\n");
	ppupdate = __of_prop_dup(&pupdate, GFP_KERNEL);
	unittest(ppupdate, "testcase setup failure\n");
	parent = nremove->parent;
	n1->parent = parent;
	n2->parent = parent;
	n21->parent = n2;
	n2->child = n21;
	ppremove = of_find_property(parent, "prop-remove", NULL);
	unittest(ppremove, "failed to find removal prop");

	of_changeset_init(&chgset);
	unittest(!of_changeset_attach_node(&chgset, n1), "fail attach n1\n");
	unittest(!of_changeset_attach_node(&chgset, n2), "fail attach n2\n");
	unittest(!of_changeset_detach_node(&chgset, nremove), "fail remove node\n");
	unittest(!of_changeset_attach_node(&chgset, n21), "fail attach n21\n");
	unittest(!of_changeset_add_property(&chgset, parent, ppadd), "fail add prop\n");
	unittest(!of_changeset_update_property(&chgset, parent, ppupdate), "fail update prop\n");
	unittest(!of_changeset_remove_property(&chgset, parent, ppremove), "fail remove prop\n");
	unittest(!of_changeset_apply(&chgset), "apply failed\n");

	/* Make sure node names are constructed correctly */
	unittest((np = of_find_node_by_path("/testcase-data/changeset/n2/n21")),
		 "'%s' not added\n", n21->full_name);
	of_node_put(np);

	unittest(!of_changeset_revert(&chgset), "revert failed\n");

	of_changeset_destroy(&chgset);
#endif
}

static void __init of_unittest_parse_interrupts(void)
{
	struct device_node *np;
	struct of_phandle_args args;
	int i, rc;

	np = of_find_node_by_path("/testcase-data/interrupts/interrupts0");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	for (i = 0; i < 4; i++) {
		bool passed = true;

		args.args_count = 0;
		rc = of_irq_parse_one(np, i, &args);

		passed &= !rc;
		passed &= (args.args_count == 1);
		passed &= (args.args[0] == (i + 1));

		unittest(passed, "index %i - data error on node %s rc=%i\n",
			 i, args.np->full_name, rc);
	}
	of_node_put(np);

	np = of_find_node_by_path("/testcase-data/interrupts/interrupts1");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	for (i = 0; i < 4; i++) {
		bool passed = true;

		args.args_count = 0;
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
		unittest(passed, "index %i - data error on node %s rc=%i\n",
			 i, args.np->full_name, rc);
	}
	of_node_put(np);
}

static void __init of_unittest_parse_interrupts_extended(void)
{
	struct device_node *np;
	struct of_phandle_args args;
	int i, rc;

	np = of_find_node_by_path("/testcase-data/interrupts/interrupts-extended0");
	if (!np) {
		pr_err("missing testcase data\n");
		return;
	}

	for (i = 0; i < 7; i++) {
		bool passed = true;

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
			passed &= !rc;
			passed &= (args.args_count == 1);
			passed &= (args.args[0] == 15);
			break;
		default:
			passed = false;
		}

		unittest(passed, "index %i - data error on node %s rc=%i\n",
			 i, args.np->full_name, rc);
	}
	of_node_put(np);
}

static const struct of_device_id match_node_table[] = {
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
} match_node_tests[] = {
	{ .path = "/testcase-data/match-node/name0", .data = "A", },
	{ .path = "/testcase-data/match-node/name1", .data = "B", },
	{ .path = "/testcase-data/match-node/a/name2", .data = "Ca", },
	{ .path = "/testcase-data/match-node/b/name2", .data = "Cb", },
	{ .path = "/testcase-data/match-node/c/name2", .data = "Cc", },
	{ .path = "/testcase-data/match-node/name3", .data = "E", },
	{ .path = "/testcase-data/match-node/name4", .data = "G", },
	{ .path = "/testcase-data/match-node/name5", .data = "H", },
	{ .path = "/testcase-data/match-node/name6", .data = "G", },
	{ .path = "/testcase-data/match-node/name7", .data = "I", },
	{ .path = "/testcase-data/match-node/name8", .data = "J", },
	{ .path = "/testcase-data/match-node/name9", .data = "K", },
};

static void __init of_unittest_match_node(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	int i;

	for (i = 0; i < ARRAY_SIZE(match_node_tests); i++) {
		np = of_find_node_by_path(match_node_tests[i].path);
		if (!np) {
			unittest(0, "missing testcase node %s\n",
				match_node_tests[i].path);
			continue;
		}

		match = of_match_node(match_node_table, np);
		if (!match) {
			unittest(0, "%s didn't match anything\n",
				match_node_tests[i].path);
			continue;
		}

		if (strcmp(match->data, match_node_tests[i].data) != 0) {
			unittest(0, "%s got wrong match. expected %s, got %s\n",
				match_node_tests[i].path, match_node_tests[i].data,
				(const char *)match->data);
			continue;
		}
		unittest(1, "passed");
	}
}

static struct resource test_bus_res = {
	.start = 0xfffffff8,
	.end = 0xfffffff9,
	.flags = IORESOURCE_MEM,
};
static const struct platform_device_info test_bus_info = {
	.name = "unittest-bus",
};
static void __init of_unittest_platform_populate(void)
{
	int irq, rc;
	struct device_node *np, *child, *grandchild;
	struct platform_device *pdev, *test_bus;
	const struct of_device_id match[] = {
		{ .compatible = "test-device", },
		{}
	};

	np = of_find_node_by_path("/testcase-data");
	of_platform_default_populate(np, NULL, NULL);

	/* Test that a missing irq domain returns -EPROBE_DEFER */
	np = of_find_node_by_path("/testcase-data/testcase-device1");
	pdev = of_find_device_by_node(np);
	unittest(pdev, "device 1 creation failed\n");

	irq = platform_get_irq(pdev, 0);
	unittest(irq == -EPROBE_DEFER, "device deferred probe failed - %d\n", irq);

	/* Test that a parsing failure does not return -EPROBE_DEFER */
	np = of_find_node_by_path("/testcase-data/testcase-device2");
	pdev = of_find_device_by_node(np);
	unittest(pdev, "device 2 creation failed\n");
	irq = platform_get_irq(pdev, 0);
	unittest(irq < 0 && irq != -EPROBE_DEFER, "device parsing error failed - %d\n", irq);

	np = of_find_node_by_path("/testcase-data/platform-tests");
	unittest(np, "No testcase data in device tree\n");
	if (!np)
		return;

	test_bus = platform_device_register_full(&test_bus_info);
	rc = PTR_ERR_OR_ZERO(test_bus);
	unittest(!rc, "testbus registration failed; rc=%i\n", rc);
	if (rc)
		return;
	test_bus->dev.of_node = np;

	/*
	 * Add a dummy resource to the test bus node after it is
	 * registered to catch problems with un-inserted resources. The
	 * DT code doesn't insert the resources, and it has caused the
	 * kernel to oops in the past. This makes sure the same bug
	 * doesn't crop up again.
	 */
	platform_device_add_resources(test_bus, &test_bus_res, 1);

	of_platform_populate(np, match, NULL, &test_bus->dev);
	for_each_child_of_node(np, child) {
		for_each_child_of_node(child, grandchild)
			unittest(of_find_device_by_node(grandchild),
				 "Could not create device for node '%s'\n",
				 grandchild->name);
	}

	of_platform_depopulate(&test_bus->dev);
	for_each_child_of_node(np, child) {
		for_each_child_of_node(child, grandchild)
			unittest(!of_find_device_by_node(grandchild),
				 "device didn't get destroyed '%s'\n",
				 grandchild->name);
	}

	platform_device_unregister(test_bus);
	of_node_put(np);
}

/**
 *	update_node_properties - adds the properties
 *	of np into dup node (present in live tree) and
 *	updates parent of children of np to dup.
 *
 *	@np:	node already present in live tree
 *	@dup:	node present in live tree to be updated
 */
static void update_node_properties(struct device_node *np,
					struct device_node *dup)
{
	struct property *prop;
	struct device_node *child;

	for_each_property_of_node(np, prop)
		of_add_property(dup, prop);

	for_each_child_of_node(np, child)
		child->parent = dup;
}

/**
 *	attach_node_and_children - attaches nodes
 *	and its children to live tree
 *
 *	@np:	Node to attach to live tree
 */
static int attach_node_and_children(struct device_node *np)
{
	struct device_node *next, *dup, *child;
	unsigned long flags;

	dup = of_find_node_by_path(np->full_name);
	if (dup) {
		update_node_properties(np, dup);
		return 0;
	}

	child = np->child;
	np->child = NULL;

	mutex_lock(&of_mutex);
	raw_spin_lock_irqsave(&devtree_lock, flags);
	np->sibling = np->parent->child;
	np->parent->child = np;
	of_node_clear_flag(np, OF_DETACHED);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);

	__of_attach_node_sysfs(np);
	mutex_unlock(&of_mutex);

	while (child) {
		next = child->sibling;
		attach_node_and_children(child);
		child = next;
	}

	return 0;
}

/**
 *	unittest_data_add - Reads, copies data from
 *	linked tree and attaches it to the live tree
 */
static int __init unittest_data_add(void)
{
	void *unittest_data;
	struct device_node *unittest_data_node, *np;
	/*
	 * __dtb_testcases_begin[] and __dtb_testcases_end[] are magically
	 * created by cmd_dt_S_dtb in scripts/Makefile.lib
	 */
	extern uint8_t __dtb_testcases_begin[];
	extern uint8_t __dtb_testcases_end[];
	const int size = __dtb_testcases_end - __dtb_testcases_begin;
	int rc;

	if (!size) {
		pr_warn("%s: No testcase data to attach; not running tests\n",
			__func__);
		return -ENODATA;
	}

	/* creating copy */
	unittest_data = kmemdup(__dtb_testcases_begin, size, GFP_KERNEL);

	if (!unittest_data) {
		pr_warn("%s: Failed to allocate memory for unittest_data; "
			"not running tests\n", __func__);
		return -ENOMEM;
	}
	of_fdt_unflatten_tree(unittest_data, NULL, &unittest_data_node);
	if (!unittest_data_node) {
		pr_warn("%s: No tree to attach; not running tests\n", __func__);
		return -ENODATA;
	}
	of_node_set_flag(unittest_data_node, OF_DETACHED);
	rc = of_resolve_phandles(unittest_data_node);
	if (rc) {
		pr_err("%s: Failed to resolve phandles (rc=%i)\n", __func__, rc);
		return -EINVAL;
	}

	if (!of_root) {
		of_root = unittest_data_node;
		for_each_of_allnodes(np)
			__of_attach_node_sysfs(np);
		of_aliases = of_find_node_by_path("/aliases");
		of_chosen = of_find_node_by_path("/chosen");
		return 0;
	}

	/* attach the sub-tree to live tree */
	np = unittest_data_node->child;
	while (np) {
		struct device_node *next = np->sibling;

		np->parent = of_root;
		attach_node_and_children(np);
		np = next;
	}
	return 0;
}

#ifdef CONFIG_OF_OVERLAY

static int unittest_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	if (np == NULL) {
		dev_err(dev, "No OF data for device\n");
		return -EINVAL;

	}

	dev_dbg(dev, "%s for node @%s\n", __func__, np->full_name);

	of_platform_populate(np, NULL, NULL, &pdev->dev);

	return 0;
}

static int unittest_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	dev_dbg(dev, "%s for node @%s\n", __func__, np->full_name);
	return 0;
}

static const struct of_device_id unittest_match[] = {
	{ .compatible = "unittest", },
	{},
};

static struct platform_driver unittest_driver = {
	.probe			= unittest_probe,
	.remove			= unittest_remove,
	.driver = {
		.name		= "unittest",
		.of_match_table	= of_match_ptr(unittest_match),
	},
};

/* get the platform device instantiated at the path */
static struct platform_device *of_path_to_platform_device(const char *path)
{
	struct device_node *np;
	struct platform_device *pdev;

	np = of_find_node_by_path(path);
	if (np == NULL)
		return NULL;

	pdev = of_find_device_by_node(np);
	of_node_put(np);

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

#if IS_BUILTIN(CONFIG_I2C)

/* get the i2c client device instantiated at the path */
static struct i2c_client *of_path_to_i2c_client(const char *path)
{
	struct device_node *np;
	struct i2c_client *client;

	np = of_find_node_by_path(path);
	if (np == NULL)
		return NULL;

	client = of_find_i2c_device_by_node(np);
	of_node_put(np);

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
		base = "/testcase-data/overlay-node/test-bus";
		break;
	case I2C_OVERLAY:
		base = "/testcase-data/overlay-node/test-bus/i2c-test-bus";
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

static const char *overlay_path(int nr)
{
	static char buf[256];

	snprintf(buf, sizeof(buf) - 1,
		"/testcase-data/overlay%d", nr);
	buf[sizeof(buf) - 1] = '\0';

	return buf;
}

static const char *bus_path = "/testcase-data/overlay-node/test-bus";

/* it is guaranteed that overlay ids are assigned in sequence */
#define MAX_UNITTEST_OVERLAYS	256
static unsigned long overlay_id_bits[BITS_TO_LONGS(MAX_UNITTEST_OVERLAYS)];
static int overlay_first_id = -1;

static void of_unittest_track_overlay(int id)
{
	if (overlay_first_id < 0)
		overlay_first_id = id;
	id -= overlay_first_id;

	/* we shouldn't need that many */
	BUG_ON(id >= MAX_UNITTEST_OVERLAYS);
	overlay_id_bits[BIT_WORD(id)] |= BIT_MASK(id);
}

static void of_unittest_untrack_overlay(int id)
{
	if (overlay_first_id < 0)
		return;
	id -= overlay_first_id;
	BUG_ON(id >= MAX_UNITTEST_OVERLAYS);
	overlay_id_bits[BIT_WORD(id)] &= ~BIT_MASK(id);
}

static void of_unittest_destroy_tracked_overlays(void)
{
	int id, ret, defers;

	if (overlay_first_id < 0)
		return;

	/* try until no defers */
	do {
		defers = 0;
		/* remove in reverse order */
		for (id = MAX_UNITTEST_OVERLAYS - 1; id >= 0; id--) {
			if (!(overlay_id_bits[BIT_WORD(id)] & BIT_MASK(id)))
				continue;

			ret = of_overlay_destroy(id + overlay_first_id);
			if (ret == -ENODEV) {
				pr_warn("%s: no overlay to destroy for #%d\n",
					__func__, id + overlay_first_id);
				continue;
			}
			if (ret != 0) {
				defers++;
				pr_warn("%s: overlay destroy failed for #%d\n",
					__func__, id + overlay_first_id);
				continue;
			}

			overlay_id_bits[BIT_WORD(id)] &= ~BIT_MASK(id);
		}
	} while (defers > 0);
}

static int of_unittest_apply_overlay(int overlay_nr, int unittest_nr,
		int *overlay_id)
{
	struct device_node *np = NULL;
	int ret, id = -1;

	np = of_find_node_by_path(overlay_path(overlay_nr));
	if (np == NULL) {
		unittest(0, "could not find overlay node @\"%s\"\n",
				overlay_path(overlay_nr));
		ret = -EINVAL;
		goto out;
	}

	ret = of_overlay_create(np);
	if (ret < 0) {
		unittest(0, "could not create overlay from \"%s\"\n",
				overlay_path(overlay_nr));
		goto out;
	}
	id = ret;
	of_unittest_track_overlay(id);

	ret = 0;

out:
	of_node_put(np);

	if (overlay_id)
		*overlay_id = id;

	return ret;
}

/* apply an overlay while checking before and after states */
static int of_unittest_apply_overlay_check(int overlay_nr, int unittest_nr,
		int before, int after, enum overlay_type ovtype)
{
	int ret;

	/* unittest device must not be in before state */
	if (of_unittest_device_exists(unittest_nr, ovtype) != before) {
		unittest(0, "overlay @\"%s\" with device @\"%s\" %s\n",
				overlay_path(overlay_nr),
				unittest_path(unittest_nr, ovtype),
				!before ? "enabled" : "disabled");
		return -EINVAL;
	}

	ret = of_unittest_apply_overlay(overlay_nr, unittest_nr, NULL);
	if (ret != 0) {
		/* of_unittest_apply_overlay already called unittest() */
		return ret;
	}

	/* unittest device must be to set to after state */
	if (of_unittest_device_exists(unittest_nr, ovtype) != after) {
		unittest(0, "overlay @\"%s\" failed to create @\"%s\" %s\n",
				overlay_path(overlay_nr),
				unittest_path(unittest_nr, ovtype),
				!after ? "enabled" : "disabled");
		return -EINVAL;
	}

	return 0;
}

/* apply an overlay and then revert it while checking before, after states */
static int of_unittest_apply_revert_overlay_check(int overlay_nr,
		int unittest_nr, int before, int after,
		enum overlay_type ovtype)
{
	int ret, ov_id;

	/* unittest device must be in before state */
	if (of_unittest_device_exists(unittest_nr, ovtype) != before) {
		unittest(0, "overlay @\"%s\" with device @\"%s\" %s\n",
				overlay_path(overlay_nr),
				unittest_path(unittest_nr, ovtype),
				!before ? "enabled" : "disabled");
		return -EINVAL;
	}

	/* apply the overlay */
	ret = of_unittest_apply_overlay(overlay_nr, unittest_nr, &ov_id);
	if (ret != 0) {
		/* of_unittest_apply_overlay already called unittest() */
		return ret;
	}

	/* unittest device must be in after state */
	if (of_unittest_device_exists(unittest_nr, ovtype) != after) {
		unittest(0, "overlay @\"%s\" failed to create @\"%s\" %s\n",
				overlay_path(overlay_nr),
				unittest_path(unittest_nr, ovtype),
				!after ? "enabled" : "disabled");
		return -EINVAL;
	}

	ret = of_overlay_destroy(ov_id);
	if (ret != 0) {
		unittest(0, "overlay @\"%s\" failed to be destroyed @\"%s\"\n",
				overlay_path(overlay_nr),
				unittest_path(unittest_nr, ovtype));
		return ret;
	}

	/* unittest device must be again in before state */
	if (of_unittest_device_exists(unittest_nr, PDEV_OVERLAY) != before) {
		unittest(0, "overlay @\"%s\" with device @\"%s\" %s\n",
				overlay_path(overlay_nr),
				unittest_path(unittest_nr, ovtype),
				!before ? "enabled" : "disabled");
		return -EINVAL;
	}

	return 0;
}

/* test activation of device */
static void of_unittest_overlay_0(void)
{
	int ret;

	/* device should enable */
	ret = of_unittest_apply_overlay_check(0, 0, 0, 1, PDEV_OVERLAY);
	if (ret != 0)
		return;

	unittest(1, "overlay test %d passed\n", 0);
}

/* test deactivation of device */
static void of_unittest_overlay_1(void)
{
	int ret;

	/* device should disable */
	ret = of_unittest_apply_overlay_check(1, 1, 1, 0, PDEV_OVERLAY);
	if (ret != 0)
		return;

	unittest(1, "overlay test %d passed\n", 1);
}

/* test activation of device */
static void of_unittest_overlay_2(void)
{
	int ret;

	/* device should enable */
	ret = of_unittest_apply_overlay_check(2, 2, 0, 1, PDEV_OVERLAY);
	if (ret != 0)
		return;

	unittest(1, "overlay test %d passed\n", 2);
}

/* test deactivation of device */
static void of_unittest_overlay_3(void)
{
	int ret;

	/* device should disable */
	ret = of_unittest_apply_overlay_check(3, 3, 1, 0, PDEV_OVERLAY);
	if (ret != 0)
		return;

	unittest(1, "overlay test %d passed\n", 3);
}

/* test activation of a full device node */
static void of_unittest_overlay_4(void)
{
	int ret;

	/* device should disable */
	ret = of_unittest_apply_overlay_check(4, 4, 0, 1, PDEV_OVERLAY);
	if (ret != 0)
		return;

	unittest(1, "overlay test %d passed\n", 4);
}

/* test overlay apply/revert sequence */
static void of_unittest_overlay_5(void)
{
	int ret;

	/* device should disable */
	ret = of_unittest_apply_revert_overlay_check(5, 5, 0, 1, PDEV_OVERLAY);
	if (ret != 0)
		return;

	unittest(1, "overlay test %d passed\n", 5);
}

/* test overlay application in sequence */
static void of_unittest_overlay_6(void)
{
	struct device_node *np;
	int ret, i, ov_id[2];
	int overlay_nr = 6, unittest_nr = 6;
	int before = 0, after = 1;

	/* unittest device must be in before state */
	for (i = 0; i < 2; i++) {
		if (of_unittest_device_exists(unittest_nr + i, PDEV_OVERLAY)
				!= before) {
			unittest(0, "overlay @\"%s\" with device @\"%s\" %s\n",
					overlay_path(overlay_nr + i),
					unittest_path(unittest_nr + i,
						PDEV_OVERLAY),
					!before ? "enabled" : "disabled");
			return;
		}
	}

	/* apply the overlays */
	for (i = 0; i < 2; i++) {

		np = of_find_node_by_path(overlay_path(overlay_nr + i));
		if (np == NULL) {
			unittest(0, "could not find overlay node @\"%s\"\n",
					overlay_path(overlay_nr + i));
			return;
		}

		ret = of_overlay_create(np);
		if (ret < 0)  {
			unittest(0, "could not create overlay from \"%s\"\n",
					overlay_path(overlay_nr + i));
			return;
		}
		ov_id[i] = ret;
		of_unittest_track_overlay(ov_id[i]);
	}

	for (i = 0; i < 2; i++) {
		/* unittest device must be in after state */
		if (of_unittest_device_exists(unittest_nr + i, PDEV_OVERLAY)
				!= after) {
			unittest(0, "overlay @\"%s\" failed @\"%s\" %s\n",
					overlay_path(overlay_nr + i),
					unittest_path(unittest_nr + i,
						PDEV_OVERLAY),
					!after ? "enabled" : "disabled");
			return;
		}
	}

	for (i = 1; i >= 0; i--) {
		ret = of_overlay_destroy(ov_id[i]);
		if (ret != 0) {
			unittest(0, "overlay @\"%s\" failed destroy @\"%s\"\n",
					overlay_path(overlay_nr + i),
					unittest_path(unittest_nr + i,
						PDEV_OVERLAY));
			return;
		}
		of_unittest_untrack_overlay(ov_id[i]);
	}

	for (i = 0; i < 2; i++) {
		/* unittest device must be again in before state */
		if (of_unittest_device_exists(unittest_nr + i, PDEV_OVERLAY)
				!= before) {
			unittest(0, "overlay @\"%s\" with device @\"%s\" %s\n",
					overlay_path(overlay_nr + i),
					unittest_path(unittest_nr + i,
						PDEV_OVERLAY),
					!before ? "enabled" : "disabled");
			return;
		}
	}

	unittest(1, "overlay test %d passed\n", 6);
}

/* test overlay application in sequence */
static void of_unittest_overlay_8(void)
{
	struct device_node *np;
	int ret, i, ov_id[2];
	int overlay_nr = 8, unittest_nr = 8;

	/* we don't care about device state in this test */

	/* apply the overlays */
	for (i = 0; i < 2; i++) {

		np = of_find_node_by_path(overlay_path(overlay_nr + i));
		if (np == NULL) {
			unittest(0, "could not find overlay node @\"%s\"\n",
					overlay_path(overlay_nr + i));
			return;
		}

		ret = of_overlay_create(np);
		if (ret < 0)  {
			unittest(0, "could not create overlay from \"%s\"\n",
					overlay_path(overlay_nr + i));
			return;
		}
		ov_id[i] = ret;
		of_unittest_track_overlay(ov_id[i]);
	}

	/* now try to remove first overlay (it should fail) */
	ret = of_overlay_destroy(ov_id[0]);
	if (ret == 0) {
		unittest(0, "overlay @\"%s\" was destroyed @\"%s\"\n",
				overlay_path(overlay_nr + 0),
				unittest_path(unittest_nr,
					PDEV_OVERLAY));
		return;
	}

	/* removing them in order should work */
	for (i = 1; i >= 0; i--) {
		ret = of_overlay_destroy(ov_id[i]);
		if (ret != 0) {
			unittest(0, "overlay @\"%s\" not destroyed @\"%s\"\n",
					overlay_path(overlay_nr + i),
					unittest_path(unittest_nr,
						PDEV_OVERLAY));
			return;
		}
		of_unittest_untrack_overlay(ov_id[i]);
	}

	unittest(1, "overlay test %d passed\n", 8);
}

/* test insertion of a bus with parent devices */
static void of_unittest_overlay_10(void)
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
	if (unittest(ret, "overlay test %d failed; no child device\n", 10))
		return;
}

/* test insertion of a bus with parent devices (and revert) */
static void of_unittest_overlay_11(void)
{
	int ret;

	/* device should disable */
	ret = of_unittest_apply_revert_overlay_check(11, 11, 0, 1,
			PDEV_OVERLAY);
	if (unittest(ret == 0,
			"overlay test %d failed; overlay application\n", 11))
		return;
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
	struct device_node *np = dev->of_node;
	struct unittest_i2c_bus_data *std;
	struct i2c_adapter *adap;
	int ret;

	if (np == NULL) {
		dev_err(dev, "No OF data for device\n");
		return -EINVAL;

	}

	dev_dbg(dev, "%s for node @%s\n", __func__, np->full_name);

	std = devm_kzalloc(dev, sizeof(*std), GFP_KERNEL);
	if (!std) {
		dev_err(dev, "Failed to allocate unittest i2c data\n");
		return -ENOMEM;
	}

	/* link them together */
	std->pdev = pdev;
	platform_set_drvdata(pdev, std);

	adap = &std->adap;
	i2c_set_adapdata(adap, std);
	adap->nr = -1;
	strlcpy(adap->name, pdev->name, sizeof(adap->name));
	adap->class = I2C_CLASS_DEPRECATED;
	adap->algo = &unittest_i2c_algo;
	adap->dev.parent = dev;
	adap->dev.of_node = dev->of_node;
	adap->timeout = 5 * HZ;
	adap->retries = 3;

	ret = i2c_add_numbered_adapter(adap);
	if (ret != 0) {
		dev_err(dev, "Failed to add I2C adapter\n");
		return ret;
	}

	return 0;
}

static int unittest_i2c_bus_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct unittest_i2c_bus_data *std = platform_get_drvdata(pdev);

	dev_dbg(dev, "%s for node @%s\n", __func__, np->full_name);
	i2c_del_adapter(&std->adap);

	return 0;
}

static const struct of_device_id unittest_i2c_bus_match[] = {
	{ .compatible = "unittest-i2c-bus", },
	{},
};

static struct platform_driver unittest_i2c_bus_driver = {
	.probe			= unittest_i2c_bus_probe,
	.remove			= unittest_i2c_bus_remove,
	.driver = {
		.name		= "unittest-i2c-bus",
		.of_match_table	= of_match_ptr(unittest_i2c_bus_match),
	},
};

static int unittest_i2c_dev_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *np = client->dev.of_node;

	if (!np) {
		dev_err(dev, "No OF node\n");
		return -EINVAL;
	}

	dev_dbg(dev, "%s for node @%s\n", __func__, np->full_name);

	return 0;
};

static int unittest_i2c_dev_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *np = client->dev.of_node;

	dev_dbg(dev, "%s for node @%s\n", __func__, np->full_name);
	return 0;
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

static int unittest_i2c_mux_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret, i, nchans;
	struct device *dev = &client->dev;
	struct i2c_adapter *adap = to_i2c_adapter(dev->parent);
	struct device_node *np = client->dev.of_node, *child;
	struct i2c_mux_core *muxc;
	u32 reg, max_reg;

	dev_dbg(dev, "%s for node @%s\n", __func__, np->full_name);

	if (!np) {
		dev_err(dev, "No OF node\n");
		return -EINVAL;
	}

	max_reg = (u32)-1;
	for_each_child_of_node(np, child) {
		ret = of_property_read_u32(child, "reg", &reg);
		if (ret)
			continue;
		if (max_reg == (u32)-1 || reg > max_reg)
			max_reg = reg;
	}
	nchans = max_reg == (u32)-1 ? 0 : max_reg + 1;
	if (nchans == 0) {
		dev_err(dev, "No channels\n");
		return -EINVAL;
	}

	muxc = i2c_mux_alloc(adap, dev, nchans, 0, 0,
			     unittest_i2c_mux_select_chan, NULL);
	if (!muxc)
		return -ENOMEM;
	for (i = 0; i < nchans; i++) {
		ret = i2c_mux_add_adapter(muxc, 0, i, 0);
		if (ret) {
			dev_err(dev, "Failed to register mux #%d\n", i);
			i2c_mux_del_adapters(muxc);
			return -ENODEV;
		}
	}

	i2c_set_clientdata(client, muxc);

	return 0;
};

static int unittest_i2c_mux_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *np = client->dev.of_node;
	struct i2c_mux_core *muxc = i2c_get_clientdata(client);

	dev_dbg(dev, "%s for node @%s\n", __func__, np->full_name);
	i2c_mux_del_adapters(muxc);
	return 0;
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
			"could not register unittest i2c device driver\n"))
		return ret;

	ret = platform_driver_register(&unittest_i2c_bus_driver);
	if (unittest(ret == 0,
			"could not register unittest i2c bus driver\n"))
		return ret;

#if IS_BUILTIN(CONFIG_I2C_MUX)
	ret = i2c_add_driver(&unittest_i2c_mux_driver);
	if (unittest(ret == 0,
			"could not register unittest i2c mux driver\n"))
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

static void of_unittest_overlay_i2c_12(void)
{
	int ret;

	/* device should enable */
	ret = of_unittest_apply_overlay_check(12, 12, 0, 1, I2C_OVERLAY);
	if (ret != 0)
		return;

	unittest(1, "overlay test %d passed\n", 12);
}

/* test deactivation of device */
static void of_unittest_overlay_i2c_13(void)
{
	int ret;

	/* device should disable */
	ret = of_unittest_apply_overlay_check(13, 13, 1, 0, I2C_OVERLAY);
	if (ret != 0)
		return;

	unittest(1, "overlay test %d passed\n", 13);
}

/* just check for i2c mux existence */
static void of_unittest_overlay_i2c_14(void)
{
}

static void of_unittest_overlay_i2c_15(void)
{
	int ret;

	/* device should enable */
	ret = of_unittest_apply_overlay_check(15, 15, 0, 1, I2C_OVERLAY);
	if (ret != 0)
		return;

	unittest(1, "overlay test %d passed\n", 15);
}

#else

static inline void of_unittest_overlay_i2c_14(void) { }
static inline void of_unittest_overlay_i2c_15(void) { }

#endif

static void __init of_unittest_overlay(void)
{
	struct device_node *bus_np = NULL;
	int ret;

	ret = platform_driver_register(&unittest_driver);
	if (ret != 0) {
		unittest(0, "could not register unittest driver\n");
		goto out;
	}

	bus_np = of_find_node_by_path(bus_path);
	if (bus_np == NULL) {
		unittest(0, "could not find bus_path \"%s\"\n", bus_path);
		goto out;
	}

	ret = of_platform_default_populate(bus_np, NULL, NULL);
	if (ret != 0) {
		unittest(0, "could not populate bus @ \"%s\"\n", bus_path);
		goto out;
	}

	if (!of_unittest_device_exists(100, PDEV_OVERLAY)) {
		unittest(0, "could not find unittest0 @ \"%s\"\n",
				unittest_path(100, PDEV_OVERLAY));
		goto out;
	}

	if (of_unittest_device_exists(101, PDEV_OVERLAY)) {
		unittest(0, "unittest1 @ \"%s\" should not exist\n",
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

	of_unittest_destroy_tracked_overlays();

out:
	of_node_put(bus_np);
}

#else
static inline void __init of_unittest_overlay(void) { }
#endif

/*
 * __dtb_ot_begin[] and __dtb_ot_end[] are created by cmd_dt_S_dtb
 * in scripts/Makefile.lib
 */

#define OVERLAY_INFO_EXTERN(name) \
	extern uint8_t __dtb_##name##_begin[]; \
	extern uint8_t __dtb_##name##_end[]

#define OVERLAY_INFO(name, expected) \
{	.dtb_begin	 = __dtb_##name##_begin, \
	.dtb_end	 = __dtb_##name##_end, \
	.expected_result = expected, \
}

struct overlay_info {
	uint8_t		   *dtb_begin;
	uint8_t		   *dtb_end;
	void		   *data;
	struct device_node *np_overlay;
	int		   expected_result;
	int		   overlay_id;
};

OVERLAY_INFO_EXTERN(overlay_base);
OVERLAY_INFO_EXTERN(overlay);
OVERLAY_INFO_EXTERN(overlay_bad_phandle);

#ifdef CONFIG_OF_OVERLAY

/* order of entries is hard-coded into users of overlays[] */
static struct overlay_info overlays[] = {
	OVERLAY_INFO(overlay_base, -9999),
	OVERLAY_INFO(overlay, 0),
	OVERLAY_INFO(overlay_bad_phandle, -EINVAL),
	{}
};

static struct device_node *overlay_base_root;

/*
 * Create base device tree for the overlay unittest.
 *
 * This is called from very early boot code.
 *
 * Do as much as possible the same way as done in __unflatten_device_tree
 * and other early boot steps for the normal FDT so that the overlay base
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
	u32 size;

	info = &overlays[0];

	if (info->expected_result != -9999) {
		pr_err("No dtb 'overlay_base' to attach\n");
		return;
	}

	data_size = info->dtb_end - info->dtb_begin;
	if (!data_size) {
		pr_err("No dtb 'overlay_base' to attach\n");
		return;
	}

	size = fdt_totalsize(info->dtb_begin);
	if (size != data_size) {
		pr_err("dtb 'overlay_base' header totalsize != actual size");
		return;
	}

	info->data = early_init_dt_alloc_memory_arch(size,
					     roundup_pow_of_two(FDT_V17_SIZE));
	if (!info->data) {
		pr_err("alloc for dtb 'overlay_base' failed");
		return;
	}

	memcpy(info->data, info->dtb_begin, size);

	__unflatten_device_tree(info->data, NULL, &info->np_overlay,
				early_init_dt_alloc_memory_arch, true);
	overlay_base_root = info->np_overlay;
}

/*
 * The purpose of of_unittest_overlay_data_add is to add an
 * overlay in the normal fashion.  This is a test of the whole
 * picture, instead of testing individual elements.
 *
 * A secondary purpose is to be able to verify that the contents of
 * /proc/device-tree/ contains the updated structure and values from
 * the overlay.  That must be verified separately in user space.
 *
 * Return 0 on unexpected error.
 */
static int __init overlay_data_add(int onum)
{
	struct overlay_info *info;
	int k;
	int ret;
	u32 size;
	u32 size_from_header;

	for (k = 0, info = overlays; info; info++, k++) {
		if (k == onum)
			break;
	}
	if (onum > k)
		return 0;

	size = info->dtb_end - info->dtb_begin;
	if (!size) {
		pr_err("no overlay to attach, %d\n", onum);
		ret = 0;
	}

	size_from_header = fdt_totalsize(info->dtb_begin);
	if (size_from_header != size) {
		pr_err("overlay header totalsize != actual size, %d", onum);
		return 0;
	}

	/*
	 * Must create permanent copy of FDT because of_fdt_unflatten_tree()
	 * will create pointers to the passed in FDT in the EDT.
	 */
	info->data = kmemdup(info->dtb_begin, size, GFP_KERNEL);
	if (!info->data) {
		pr_err("unable to allocate memory for data, %d\n", onum);
		return 0;
	}

	of_fdt_unflatten_tree(info->data, NULL, &info->np_overlay);
	if (!info->np_overlay) {
		pr_err("unable to unflatten overlay, %d\n", onum);
		ret = 0;
		goto out_free_data;
	}
	of_node_set_flag(info->np_overlay, OF_DETACHED);

	ret = of_resolve_phandles(info->np_overlay);
	if (ret) {
		pr_err("resolve ot phandles (ret=%d), %d\n", ret, onum);
		goto out_free_np_overlay;
	}

	ret = of_overlay_create(info->np_overlay);
	if (ret < 0) {
		pr_err("of_overlay_create() (ret=%d), %d\n", ret, onum);
		goto out_free_np_overlay;
	} else {
		info->overlay_id = ret;
		ret = 0;
	}

	pr_debug("__dtb_overlay_begin applied, overlay id %d\n", ret);

	goto out;

out_free_np_overlay:
	/*
	 * info->np_overlay is the unflattened device tree
	 * It has not been spliced into the live tree.
	 */

	/* todo: function to free unflattened device tree */

out_free_data:
	kfree(info->data);

out:
	return (ret == info->expected_result);
}

/*
 * The purpose of of_unittest_overlay_high_level is to add an overlay
 * in the normal fashion.  This is a test of the whole picture,
 * instead of individual elements.
 *
 * The first part of the function is _not_ normal overlay usage; it is
 * finishing splicing the base overlay device tree into the live tree.
 */
static __init void of_unittest_overlay_high_level(void)
{
	struct device_node *last_sibling;
	struct device_node *np;
	struct device_node *of_symbols;
	struct device_node *overlay_base_symbols;
	struct device_node **pprev;
	struct property *prop;
	int ret;

	if (!overlay_base_root) {
		unittest(0, "overlay_base_root not initialized\n");
		return;
	}

	/*
	 * Could not fixup phandles in unittest_unflatten_overlay_base()
	 * because kmalloc() was not yet available.
	 */
	of_resolve_phandles(overlay_base_root);

	/*
	 * do not allow overlay_base to duplicate any node already in
	 * tree, this greatly simplifies the code
	 */

	/*
	 * remove overlay_base_root node "__local_fixups", after
	 * being used by of_resolve_phandles()
	 */
	pprev = &overlay_base_root->child;
	for (np = overlay_base_root->child; np; np = np->sibling) {
		if (!of_node_cmp(np->name, "__local_fixups__")) {
			*pprev = np->sibling;
			break;
		}
		pprev = &np->sibling;
	}

	/* remove overlay_base_root node "__symbols__" if in live tree */
	of_symbols = of_get_child_by_name(of_root, "__symbols__");
	if (of_symbols) {
		/* will have to graft properties from node into live tree */
		pprev = &overlay_base_root->child;
		for (np = overlay_base_root->child; np; np = np->sibling) {
			if (!of_node_cmp(np->name, "__symbols__")) {
				overlay_base_symbols = np;
				*pprev = np->sibling;
				break;
			}
			pprev = &np->sibling;
		}
	}

	for (np = overlay_base_root->child; np; np = np->sibling) {
		if (of_get_child_by_name(of_root, np->name)) {
			unittest(0, "illegal node name in overlay_base %s",
				np->name);
			return;
		}
	}

	/*
	 * overlay 'overlay_base' is not allowed to have root
	 * properties, so only need to splice nodes into main device tree.
	 *
	 * root node of *overlay_base_root will not be freed, it is lost
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

	for_each_of_allnodes_from(overlay_base_root, np)
		__of_attach_node_sysfs(np);

	if (of_symbols) {
		for_each_property_of_node(overlay_base_symbols, prop) {
			ret = __of_add_property(of_symbols, prop);
			if (ret) {
				unittest(0,
					 "duplicate property '%s' in overlay_base node __symbols__",
					 prop->name);
				goto err_unlock;
			}
			ret = __of_add_property_sysfs(of_symbols, prop);
			if (ret) {
				unittest(0,
					 "unable to add property '%s' in overlay_base node __symbols__ to sysfs",
					 prop->name);
				goto err_unlock;
			}
		}
	}

	mutex_unlock(&of_mutex);


	/* now do the normal overlay usage test */

	unittest(overlay_data_add(1),
		 "Adding overlay 'overlay' failed\n");

	unittest(overlay_data_add(2),
		 "Adding overlay 'overlay_bad_phandle' failed\n");
	return;

err_unlock:
	mutex_unlock(&of_mutex);
}

#else

static inline __init void of_unittest_overlay_high_level(void) {}

#endif

static int __init of_unittest(void)
{
	struct device_node *np;
	int res;

	/* adding data for unittest */
	res = unittest_data_add();
	if (res)
		return res;
	if (!of_aliases)
		of_aliases = of_find_node_by_path("/aliases");

	np = of_find_node_by_path("/testcase-data/phandle-tests/consumer-a");
	if (!np) {
		pr_info("No testcase data in device tree; not running tests\n");
		return 0;
	}
	of_node_put(np);

	pr_info("start of unittest - you will see error messages\n");
	of_unittest_check_tree_linkage();
	of_unittest_check_phandles();
	of_unittest_find_node_by_name();
	of_unittest_dynamic();
	of_unittest_parse_phandle_with_args();
	of_unittest_property_string();
	of_unittest_property_copy();
	of_unittest_changeset();
	of_unittest_parse_interrupts();
	of_unittest_parse_interrupts_extended();
	of_unittest_match_node();
	of_unittest_platform_populate();
	of_unittest_overlay();

	/* Double check linkage after removing testcase data */
	of_unittest_check_tree_linkage();

	of_unittest_overlay_high_level();

	pr_info("end of unittest - %i passed, %i failed\n",
		unittest_results.passed, unittest_results.failed);

	return 0;
}
late_initcall(of_unittest);

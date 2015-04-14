/*
 * Self tests for device tree subsystem
 */

#define pr_fmt(fmt) "### dt-test ### " fmt

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/hashtable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>

#include <linux/i2c.h>
#include <linux/i2c-mux.h>

#include "of_private.h"

static struct selftest_results {
	int passed;
	int failed;
} selftest_results;

#define selftest(result, fmt, ...) ({ \
	bool failed = !(result); \
	if (failed) { \
		selftest_results.failed++; \
		pr_err("FAIL %s():%i " fmt, __func__, __LINE__, ##__VA_ARGS__); \
	} else { \
		selftest_results.passed++; \
		pr_debug("pass %s():%i\n", __func__, __LINE__); \
	} \
	failed; \
})

static void __init of_selftest_find_node_by_name(void)
{
	struct device_node *np;
	const char *options;

	np = of_find_node_by_path("/testcase-data");
	selftest(np && !strcmp("/testcase-data", np->full_name),
		"find /testcase-data failed\n");
	of_node_put(np);

	/* Test if trailing '/' works */
	np = of_find_node_by_path("/testcase-data/");
	selftest(!np, "trailing '/' on /testcase-data/ should fail\n");

	np = of_find_node_by_path("/testcase-data/phandle-tests/consumer-a");
	selftest(np && !strcmp("/testcase-data/phandle-tests/consumer-a", np->full_name),
		"find /testcase-data/phandle-tests/consumer-a failed\n");
	of_node_put(np);

	np = of_find_node_by_path("testcase-alias");
	selftest(np && !strcmp("/testcase-data", np->full_name),
		"find testcase-alias failed\n");
	of_node_put(np);

	/* Test if trailing '/' works on aliases */
	np = of_find_node_by_path("testcase-alias/");
	selftest(!np, "trailing '/' on testcase-alias/ should fail\n");

	np = of_find_node_by_path("testcase-alias/phandle-tests/consumer-a");
	selftest(np && !strcmp("/testcase-data/phandle-tests/consumer-a", np->full_name),
		"find testcase-alias/phandle-tests/consumer-a failed\n");
	of_node_put(np);

	np = of_find_node_by_path("/testcase-data/missing-path");
	selftest(!np, "non-existent path returned node %s\n", np->full_name);
	of_node_put(np);

	np = of_find_node_by_path("missing-alias");
	selftest(!np, "non-existent alias returned node %s\n", np->full_name);
	of_node_put(np);

	np = of_find_node_by_path("testcase-alias/missing-path");
	selftest(!np, "non-existent alias with relative path returned node %s\n", np->full_name);
	of_node_put(np);

	np = of_find_node_opts_by_path("/testcase-data:testoption", &options);
	selftest(np && !strcmp("testoption", options),
		 "option path test failed\n");
	of_node_put(np);

	np = of_find_node_opts_by_path("/testcase-data:test/option", &options);
	selftest(np && !strcmp("test/option", options),
		 "option path test, subcase #1 failed\n");
	of_node_put(np);

	np = of_find_node_opts_by_path("/testcase-data:testoption", NULL);
	selftest(np, "NULL option path test failed\n");
	of_node_put(np);

	np = of_find_node_opts_by_path("testcase-alias:testaliasoption",
				       &options);
	selftest(np && !strcmp("testaliasoption", options),
		 "option alias path test failed\n");
	of_node_put(np);

	np = of_find_node_opts_by_path("testcase-alias:test/alias/option",
				       &options);
	selftest(np && !strcmp("test/alias/option", options),
		 "option alias path test, subcase #1 failed\n");
	of_node_put(np);

	np = of_find_node_opts_by_path("testcase-alias:testaliasoption", NULL);
	selftest(np, "NULL option alias path test failed\n");
	of_node_put(np);

	options = "testoption";
	np = of_find_node_opts_by_path("testcase-alias", &options);
	selftest(np && !options, "option clearing test failed\n");
	of_node_put(np);

	options = "testoption";
	np = of_find_node_opts_by_path("/", &options);
	selftest(np && !options, "option clearing root node test failed\n");
	of_node_put(np);
}

static void __init of_selftest_dynamic(void)
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
		selftest(0, "kzalloc() failed\n");
		return;
	}

	/* Add a new property - should pass*/
	prop->name = "new-property";
	prop->value = "new-property-data";
	prop->length = strlen(prop->value);
	selftest(of_add_property(np, prop) == 0, "Adding a new property failed\n");

	/* Try to add an existing property - should fail */
	prop++;
	prop->name = "new-property";
	prop->value = "new-property-data-should-fail";
	prop->length = strlen(prop->value);
	selftest(of_add_property(np, prop) != 0,
		 "Adding an existing property should have failed\n");

	/* Try to modify an existing property - should pass */
	prop->value = "modify-property-data-should-pass";
	prop->length = strlen(prop->value);
	selftest(of_update_property(np, prop) == 0,
		 "Updating an existing property should have passed\n");

	/* Try to modify non-existent property - should pass*/
	prop++;
	prop->name = "modify-property";
	prop->value = "modify-missing-property-data-should-pass";
	prop->length = strlen(prop->value);
	selftest(of_update_property(np, prop) == 0,
		 "Updating a missing property should have passed\n");

	/* Remove property - should pass */
	selftest(of_remove_property(np, prop) == 0,
		 "Removing a property should have passed\n");

	/* Adding very large property - should pass */
	prop++;
	prop->name = "large-property-PAGE_SIZEx8";
	prop->length = PAGE_SIZE * 8;
	prop->value = kzalloc(prop->length, GFP_KERNEL);
	selftest(prop->value != NULL, "Unable to allocate large buffer\n");
	if (prop->value)
		selftest(of_add_property(np, prop) == 0,
			 "Adding a large property should have passed\n");
}

static int __init of_selftest_check_node_linkage(struct device_node *np)
{
	struct device_node *child;
	int count = 0, rc;

	for_each_child_of_node(np, child) {
		if (child->parent != np) {
			pr_err("Child node %s links to wrong parent %s\n",
				 child->name, np->name);
			return -EINVAL;
		}

		rc = of_selftest_check_node_linkage(child);
		if (rc < 0)
			return rc;
		count += rc;
	}

	return count + 1;
}

static void __init of_selftest_check_tree_linkage(void)
{
	struct device_node *np;
	int allnode_count = 0, child_count;

	if (!of_root)
		return;

	for_each_of_allnodes(np)
		allnode_count++;
	child_count = of_selftest_check_node_linkage(of_root);

	selftest(child_count > 0, "Device node data structure is corrupted\n");
	selftest(child_count == allnode_count, "allnodes list size (%i) doesn't match"
		 "sibling lists size (%i)\n", allnode_count, child_count);
	pr_debug("allnodes list size (%i); sibling lists size (%i)\n", allnode_count, child_count);
}

struct node_hash {
	struct hlist_node node;
	struct device_node *np;
};

static DEFINE_HASHTABLE(phandle_ht, 8);
static void __init of_selftest_check_phandles(void)
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
	selftest(dup_count == 0, "Found %i duplicates in %i phandles\n",
		 dup_count, phandle_count);

	/* Clean up */
	hash_for_each_safe(phandle_ht, i, tmp, nh, node) {
		hash_del(&nh->node);
		kfree(nh);
	}
}

static void __init of_selftest_parse_phandle_with_args(void)
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
	selftest(rc == 7, "of_count_phandle_with_args() returned %i, expected 7\n", rc);

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

		selftest(passed, "index %i - data error on node %s rc=%i\n",
			 i, args.np->full_name, rc);
	}

	/* Check for missing list property */
	rc = of_parse_phandle_with_args(np, "phandle-list-missing",
					"#phandle-cells", 0, &args);
	selftest(rc == -ENOENT, "expected:%i got:%i\n", -ENOENT, rc);
	rc = of_count_phandle_with_args(np, "phandle-list-missing",
					"#phandle-cells");
	selftest(rc == -ENOENT, "expected:%i got:%i\n", -ENOENT, rc);

	/* Check for missing cells property */
	rc = of_parse_phandle_with_args(np, "phandle-list",
					"#phandle-cells-missing", 0, &args);
	selftest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);
	rc = of_count_phandle_with_args(np, "phandle-list",
					"#phandle-cells-missing");
	selftest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);

	/* Check for bad phandle in list */
	rc = of_parse_phandle_with_args(np, "phandle-list-bad-phandle",
					"#phandle-cells", 0, &args);
	selftest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);
	rc = of_count_phandle_with_args(np, "phandle-list-bad-phandle",
					"#phandle-cells");
	selftest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);

	/* Check for incorrectly formed argument list */
	rc = of_parse_phandle_with_args(np, "phandle-list-bad-args",
					"#phandle-cells", 1, &args);
	selftest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);
	rc = of_count_phandle_with_args(np, "phandle-list-bad-args",
					"#phandle-cells");
	selftest(rc == -EINVAL, "expected:%i got:%i\n", -EINVAL, rc);
}

static void __init of_selftest_property_string(void)
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
	selftest(rc == 0, "first expected:0 got:%i\n", rc);
	rc = of_property_match_string(np, "phandle-list-names", "second");
	selftest(rc == 1, "second expected:1 got:%i\n", rc);
	rc = of_property_match_string(np, "phandle-list-names", "third");
	selftest(rc == 2, "third expected:2 got:%i\n", rc);
	rc = of_property_match_string(np, "phandle-list-names", "fourth");
	selftest(rc == -ENODATA, "unmatched string; rc=%i\n", rc);
	rc = of_property_match_string(np, "missing-property", "blah");
	selftest(rc == -EINVAL, "missing property; rc=%i\n", rc);
	rc = of_property_match_string(np, "empty-property", "blah");
	selftest(rc == -ENODATA, "empty property; rc=%i\n", rc);
	rc = of_property_match_string(np, "unterminated-string", "blah");
	selftest(rc == -EILSEQ, "unterminated string; rc=%i\n", rc);

	/* of_property_count_strings() tests */
	rc = of_property_count_strings(np, "string-property");
	selftest(rc == 1, "Incorrect string count; rc=%i\n", rc);
	rc = of_property_count_strings(np, "phandle-list-names");
	selftest(rc == 3, "Incorrect string count; rc=%i\n", rc);
	rc = of_property_count_strings(np, "unterminated-string");
	selftest(rc == -EILSEQ, "unterminated string; rc=%i\n", rc);
	rc = of_property_count_strings(np, "unterminated-string-list");
	selftest(rc == -EILSEQ, "unterminated string array; rc=%i\n", rc);

	/* of_property_read_string_index() tests */
	rc = of_property_read_string_index(np, "string-property", 0, strings);
	selftest(rc == 0 && !strcmp(strings[0], "foobar"), "of_property_read_string_index() failure; rc=%i\n", rc);
	strings[0] = NULL;
	rc = of_property_read_string_index(np, "string-property", 1, strings);
	selftest(rc == -ENODATA && strings[0] == NULL, "of_property_read_string_index() failure; rc=%i\n", rc);
	rc = of_property_read_string_index(np, "phandle-list-names", 0, strings);
	selftest(rc == 0 && !strcmp(strings[0], "first"), "of_property_read_string_index() failure; rc=%i\n", rc);
	rc = of_property_read_string_index(np, "phandle-list-names", 1, strings);
	selftest(rc == 0 && !strcmp(strings[0], "second"), "of_property_read_string_index() failure; rc=%i\n", rc);
	rc = of_property_read_string_index(np, "phandle-list-names", 2, strings);
	selftest(rc == 0 && !strcmp(strings[0], "third"), "of_property_read_string_index() failure; rc=%i\n", rc);
	strings[0] = NULL;
	rc = of_property_read_string_index(np, "phandle-list-names", 3, strings);
	selftest(rc == -ENODATA && strings[0] == NULL, "of_property_read_string_index() failure; rc=%i\n", rc);
	strings[0] = NULL;
	rc = of_property_read_string_index(np, "unterminated-string", 0, strings);
	selftest(rc == -EILSEQ && strings[0] == NULL, "of_property_read_string_index() failure; rc=%i\n", rc);
	rc = of_property_read_string_index(np, "unterminated-string-list", 0, strings);
	selftest(rc == 0 && !strcmp(strings[0], "first"), "of_property_read_string_index() failure; rc=%i\n", rc);
	strings[0] = NULL;
	rc = of_property_read_string_index(np, "unterminated-string-list", 2, strings); /* should fail */
	selftest(rc == -EILSEQ && strings[0] == NULL, "of_property_read_string_index() failure; rc=%i\n", rc);
	strings[1] = NULL;

	/* of_property_read_string_array() tests */
	rc = of_property_read_string_array(np, "string-property", strings, 4);
	selftest(rc == 1, "Incorrect string count; rc=%i\n", rc);
	rc = of_property_read_string_array(np, "phandle-list-names", strings, 4);
	selftest(rc == 3, "Incorrect string count; rc=%i\n", rc);
	rc = of_property_read_string_array(np, "unterminated-string", strings, 4);
	selftest(rc == -EILSEQ, "unterminated string; rc=%i\n", rc);
	/* -- An incorrectly formed string should cause a failure */
	rc = of_property_read_string_array(np, "unterminated-string-list", strings, 4);
	selftest(rc == -EILSEQ, "unterminated string array; rc=%i\n", rc);
	/* -- parsing the correctly formed strings should still work: */
	strings[2] = NULL;
	rc = of_property_read_string_array(np, "unterminated-string-list", strings, 2);
	selftest(rc == 2 && strings[2] == NULL, "of_property_read_string_array() failure; rc=%i\n", rc);
	strings[1] = NULL;
	rc = of_property_read_string_array(np, "phandle-list-names", strings, 1);
	selftest(rc == 1 && strings[1] == NULL, "Overwrote end of string array; rc=%i, str='%s'\n", rc, strings[1]);
}

#define propcmp(p1, p2) (((p1)->length == (p2)->length) && \
			(p1)->value && (p2)->value && \
			!memcmp((p1)->value, (p2)->value, (p1)->length) && \
			!strcmp((p1)->name, (p2)->name))
static void __init of_selftest_property_copy(void)
{
#ifdef CONFIG_OF_DYNAMIC
	struct property p1 = { .name = "p1", .length = 0, .value = "" };
	struct property p2 = { .name = "p2", .length = 5, .value = "abcd" };
	struct property *new;

	new = __of_prop_dup(&p1, GFP_KERNEL);
	selftest(new && propcmp(&p1, new), "empty property didn't copy correctly\n");
	kfree(new->value);
	kfree(new->name);
	kfree(new);

	new = __of_prop_dup(&p2, GFP_KERNEL);
	selftest(new && propcmp(&p2, new), "non-empty property didn't copy correctly\n");
	kfree(new->value);
	kfree(new->name);
	kfree(new);
#endif
}

static void __init of_selftest_changeset(void)
{
#ifdef CONFIG_OF_DYNAMIC
	struct property *ppadd, padd = { .name = "prop-add", .length = 0, .value = "" };
	struct property *ppupdate, pupdate = { .name = "prop-update", .length = 5, .value = "abcd" };
	struct property *ppremove;
	struct device_node *n1, *n2, *n21, *nremove, *parent, *np;
	struct of_changeset chgset;

	n1 = __of_node_dup(NULL, "/testcase-data/changeset/n1");
	selftest(n1, "testcase setup failure\n");
	n2 = __of_node_dup(NULL, "/testcase-data/changeset/n2");
	selftest(n2, "testcase setup failure\n");
	n21 = __of_node_dup(NULL, "%s/%s", "/testcase-data/changeset/n2", "n21");
	selftest(n21, "testcase setup failure %p\n", n21);
	nremove = of_find_node_by_path("/testcase-data/changeset/node-remove");
	selftest(nremove, "testcase setup failure\n");
	ppadd = __of_prop_dup(&padd, GFP_KERNEL);
	selftest(ppadd, "testcase setup failure\n");
	ppupdate = __of_prop_dup(&pupdate, GFP_KERNEL);
	selftest(ppupdate, "testcase setup failure\n");
	parent = nremove->parent;
	n1->parent = parent;
	n2->parent = parent;
	n21->parent = n2;
	n2->child = n21;
	ppremove = of_find_property(parent, "prop-remove", NULL);
	selftest(ppremove, "failed to find removal prop");

	of_changeset_init(&chgset);
	selftest(!of_changeset_attach_node(&chgset, n1), "fail attach n1\n");
	selftest(!of_changeset_attach_node(&chgset, n2), "fail attach n2\n");
	selftest(!of_changeset_detach_node(&chgset, nremove), "fail remove node\n");
	selftest(!of_changeset_attach_node(&chgset, n21), "fail attach n21\n");
	selftest(!of_changeset_add_property(&chgset, parent, ppadd), "fail add prop\n");
	selftest(!of_changeset_update_property(&chgset, parent, ppupdate), "fail update prop\n");
	selftest(!of_changeset_remove_property(&chgset, parent, ppremove), "fail remove prop\n");
	mutex_lock(&of_mutex);
	selftest(!of_changeset_apply(&chgset), "apply failed\n");
	mutex_unlock(&of_mutex);

	/* Make sure node names are constructed correctly */
	selftest((np = of_find_node_by_path("/testcase-data/changeset/n2/n21")),
		 "'%s' not added\n", n21->full_name);
	of_node_put(np);

	mutex_lock(&of_mutex);
	selftest(!of_changeset_revert(&chgset), "revert failed\n");
	mutex_unlock(&of_mutex);

	of_changeset_destroy(&chgset);
#endif
}

static void __init of_selftest_parse_interrupts(void)
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

		selftest(passed, "index %i - data error on node %s rc=%i\n",
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
		selftest(passed, "index %i - data error on node %s rc=%i\n",
			 i, args.np->full_name, rc);
	}
	of_node_put(np);
}

static void __init of_selftest_parse_interrupts_extended(void)
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

		selftest(passed, "index %i - data error on node %s rc=%i\n",
			 i, args.np->full_name, rc);
	}
	of_node_put(np);
}

static struct of_device_id match_node_table[] = {
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

static void __init of_selftest_match_node(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	int i;

	for (i = 0; i < ARRAY_SIZE(match_node_tests); i++) {
		np = of_find_node_by_path(match_node_tests[i].path);
		if (!np) {
			selftest(0, "missing testcase node %s\n",
				match_node_tests[i].path);
			continue;
		}

		match = of_match_node(match_node_table, np);
		if (!match) {
			selftest(0, "%s didn't match anything\n",
				match_node_tests[i].path);
			continue;
		}

		if (strcmp(match->data, match_node_tests[i].data) != 0) {
			selftest(0, "%s got wrong match. expected %s, got %s\n",
				match_node_tests[i].path, match_node_tests[i].data,
				(const char *)match->data);
			continue;
		}
		selftest(1, "passed");
	}
}

struct device test_bus = {
	.init_name = "unittest-bus",
};
static void __init of_selftest_platform_populate(void)
{
	int irq, rc;
	struct device_node *np, *child, *grandchild;
	struct platform_device *pdev;
	struct of_device_id match[] = {
		{ .compatible = "test-device", },
		{}
	};

	np = of_find_node_by_path("/testcase-data");
	of_platform_populate(np, of_default_bus_match_table, NULL, NULL);

	/* Test that a missing irq domain returns -EPROBE_DEFER */
	np = of_find_node_by_path("/testcase-data/testcase-device1");
	pdev = of_find_device_by_node(np);
	selftest(pdev, "device 1 creation failed\n");

	irq = platform_get_irq(pdev, 0);
	selftest(irq == -EPROBE_DEFER, "device deferred probe failed - %d\n", irq);

	/* Test that a parsing failure does not return -EPROBE_DEFER */
	np = of_find_node_by_path("/testcase-data/testcase-device2");
	pdev = of_find_device_by_node(np);
	selftest(pdev, "device 2 creation failed\n");
	irq = platform_get_irq(pdev, 0);
	selftest(irq < 0 && irq != -EPROBE_DEFER, "device parsing error failed - %d\n", irq);

	if (selftest(np = of_find_node_by_path("/testcase-data/platform-tests"),
		     "No testcase data in device tree\n"));
		return;

	if (selftest(!(rc = device_register(&test_bus)),
		     "testbus registration failed; rc=%i\n", rc));
		return;

	for_each_child_of_node(np, child) {
		of_platform_populate(child, match, NULL, &test_bus);
		for_each_child_of_node(child, grandchild)
			selftest(of_find_device_by_node(grandchild),
				 "Could not create device for node '%s'\n",
				 grandchild->name);
	}

	of_platform_depopulate(&test_bus);
	for_each_child_of_node(np, child) {
		for_each_child_of_node(child, grandchild)
			selftest(!of_find_device_by_node(grandchild),
				 "device didn't get destroyed '%s'\n",
				 grandchild->name);
	}

	device_unregister(&test_bus);
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
 *	selftest_data_add - Reads, copies data from
 *	linked tree and attaches it to the live tree
 */
static int __init selftest_data_add(void)
{
	void *selftest_data;
	struct device_node *selftest_data_node, *np;
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
	selftest_data = kmemdup(__dtb_testcases_begin, size, GFP_KERNEL);

	if (!selftest_data) {
		pr_warn("%s: Failed to allocate memory for selftest_data; "
			"not running tests\n", __func__);
		return -ENOMEM;
	}
	of_fdt_unflatten_tree(selftest_data, &selftest_data_node);
	if (!selftest_data_node) {
		pr_warn("%s: No tree to attach; not running tests\n", __func__);
		return -ENODATA;
	}
	of_node_set_flag(selftest_data_node, OF_DETACHED);
	rc = of_resolve_phandles(selftest_data_node);
	if (rc) {
		pr_err("%s: Failed to resolve phandles (rc=%i)\n", __func__, rc);
		return -EINVAL;
	}

	if (!of_root) {
		of_root = selftest_data_node;
		for_each_of_allnodes(np)
			__of_attach_node_sysfs(np);
		of_aliases = of_find_node_by_path("/aliases");
		of_chosen = of_find_node_by_path("/chosen");
		return 0;
	}

	/* attach the sub-tree to live tree */
	np = selftest_data_node->child;
	while (np) {
		struct device_node *next = np->sibling;
		np->parent = of_root;
		attach_node_and_children(np);
		np = next;
	}
	return 0;
}

#ifdef CONFIG_OF_OVERLAY

static int selftest_probe(struct platform_device *pdev)
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

static int selftest_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	dev_dbg(dev, "%s for node @%s\n", __func__, np->full_name);
	return 0;
}

static struct of_device_id selftest_match[] = {
	{ .compatible = "selftest", },
	{},
};

static struct platform_driver selftest_driver = {
	.probe			= selftest_probe,
	.remove			= selftest_remove,
	.driver = {
		.name		= "selftest",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(selftest_match),
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

static const char *selftest_path(int nr, enum overlay_type ovtype)
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
	snprintf(buf, sizeof(buf) - 1, "%s/test-selftest%d", base, nr);
	buf[sizeof(buf) - 1] = '\0';
	return buf;
}

static int of_selftest_device_exists(int selftest_nr, enum overlay_type ovtype)
{
	const char *path;

	path = selftest_path(selftest_nr, ovtype);

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

static int of_selftest_apply_overlay(int selftest_nr, int overlay_nr,
		int *overlay_id)
{
	struct device_node *np = NULL;
	int ret, id = -1;

	np = of_find_node_by_path(overlay_path(overlay_nr));
	if (np == NULL) {
		selftest(0, "could not find overlay node @\"%s\"\n",
				overlay_path(overlay_nr));
		ret = -EINVAL;
		goto out;
	}

	ret = of_overlay_create(np);
	if (ret < 0) {
		selftest(0, "could not create overlay from \"%s\"\n",
				overlay_path(overlay_nr));
		goto out;
	}
	id = ret;

	ret = 0;

out:
	of_node_put(np);

	if (overlay_id)
		*overlay_id = id;

	return ret;
}

/* apply an overlay while checking before and after states */
static int of_selftest_apply_overlay_check(int overlay_nr, int selftest_nr,
		int before, int after, enum overlay_type ovtype)
{
	int ret;

	/* selftest device must not be in before state */
	if (of_selftest_device_exists(selftest_nr, ovtype) != before) {
		selftest(0, "overlay @\"%s\" with device @\"%s\" %s\n",
				overlay_path(overlay_nr),
				selftest_path(selftest_nr, ovtype),
				!before ? "enabled" : "disabled");
		return -EINVAL;
	}

	ret = of_selftest_apply_overlay(overlay_nr, selftest_nr, NULL);
	if (ret != 0) {
		/* of_selftest_apply_overlay already called selftest() */
		return ret;
	}

	/* selftest device must be to set to after state */
	if (of_selftest_device_exists(selftest_nr, ovtype) != after) {
		selftest(0, "overlay @\"%s\" failed to create @\"%s\" %s\n",
				overlay_path(overlay_nr),
				selftest_path(selftest_nr, ovtype),
				!after ? "enabled" : "disabled");
		return -EINVAL;
	}

	return 0;
}

/* apply an overlay and then revert it while checking before, after states */
static int of_selftest_apply_revert_overlay_check(int overlay_nr,
		int selftest_nr, int before, int after,
		enum overlay_type ovtype)
{
	int ret, ov_id;

	/* selftest device must be in before state */
	if (of_selftest_device_exists(selftest_nr, ovtype) != before) {
		selftest(0, "overlay @\"%s\" with device @\"%s\" %s\n",
				overlay_path(overlay_nr),
				selftest_path(selftest_nr, ovtype),
				!before ? "enabled" : "disabled");
		return -EINVAL;
	}

	/* apply the overlay */
	ret = of_selftest_apply_overlay(overlay_nr, selftest_nr, &ov_id);
	if (ret != 0) {
		/* of_selftest_apply_overlay already called selftest() */
		return ret;
	}

	/* selftest device must be in after state */
	if (of_selftest_device_exists(selftest_nr, ovtype) != after) {
		selftest(0, "overlay @\"%s\" failed to create @\"%s\" %s\n",
				overlay_path(overlay_nr),
				selftest_path(selftest_nr, ovtype),
				!after ? "enabled" : "disabled");
		return -EINVAL;
	}

	ret = of_overlay_destroy(ov_id);
	if (ret != 0) {
		selftest(0, "overlay @\"%s\" failed to be destroyed @\"%s\"\n",
				overlay_path(overlay_nr),
				selftest_path(selftest_nr, ovtype));
		return ret;
	}

	/* selftest device must be again in before state */
	if (of_selftest_device_exists(selftest_nr, PDEV_OVERLAY) != before) {
		selftest(0, "overlay @\"%s\" with device @\"%s\" %s\n",
				overlay_path(overlay_nr),
				selftest_path(selftest_nr, ovtype),
				!before ? "enabled" : "disabled");
		return -EINVAL;
	}

	return 0;
}

/* test activation of device */
static void of_selftest_overlay_0(void)
{
	int ret;

	/* device should enable */
	ret = of_selftest_apply_overlay_check(0, 0, 0, 1, PDEV_OVERLAY);
	if (ret != 0)
		return;

	selftest(1, "overlay test %d passed\n", 0);
}

/* test deactivation of device */
static void of_selftest_overlay_1(void)
{
	int ret;

	/* device should disable */
	ret = of_selftest_apply_overlay_check(1, 1, 1, 0, PDEV_OVERLAY);
	if (ret != 0)
		return;

	selftest(1, "overlay test %d passed\n", 1);
}

/* test activation of device */
static void of_selftest_overlay_2(void)
{
	int ret;

	/* device should enable */
	ret = of_selftest_apply_overlay_check(2, 2, 0, 1, PDEV_OVERLAY);
	if (ret != 0)
		return;

	selftest(1, "overlay test %d passed\n", 2);
}

/* test deactivation of device */
static void of_selftest_overlay_3(void)
{
	int ret;

	/* device should disable */
	ret = of_selftest_apply_overlay_check(3, 3, 1, 0, PDEV_OVERLAY);
	if (ret != 0)
		return;

	selftest(1, "overlay test %d passed\n", 3);
}

/* test activation of a full device node */
static void of_selftest_overlay_4(void)
{
	int ret;

	/* device should disable */
	ret = of_selftest_apply_overlay_check(4, 4, 0, 1, PDEV_OVERLAY);
	if (ret != 0)
		return;

	selftest(1, "overlay test %d passed\n", 4);
}

/* test overlay apply/revert sequence */
static void of_selftest_overlay_5(void)
{
	int ret;

	/* device should disable */
	ret = of_selftest_apply_revert_overlay_check(5, 5, 0, 1, PDEV_OVERLAY);
	if (ret != 0)
		return;

	selftest(1, "overlay test %d passed\n", 5);
}

/* test overlay application in sequence */
static void of_selftest_overlay_6(void)
{
	struct device_node *np;
	int ret, i, ov_id[2];
	int overlay_nr = 6, selftest_nr = 6;
	int before = 0, after = 1;

	/* selftest device must be in before state */
	for (i = 0; i < 2; i++) {
		if (of_selftest_device_exists(selftest_nr + i, PDEV_OVERLAY)
				!= before) {
			selftest(0, "overlay @\"%s\" with device @\"%s\" %s\n",
					overlay_path(overlay_nr + i),
					selftest_path(selftest_nr + i,
						PDEV_OVERLAY),
					!before ? "enabled" : "disabled");
			return;
		}
	}

	/* apply the overlays */
	for (i = 0; i < 2; i++) {

		np = of_find_node_by_path(overlay_path(overlay_nr + i));
		if (np == NULL) {
			selftest(0, "could not find overlay node @\"%s\"\n",
					overlay_path(overlay_nr + i));
			return;
		}

		ret = of_overlay_create(np);
		if (ret < 0)  {
			selftest(0, "could not create overlay from \"%s\"\n",
					overlay_path(overlay_nr + i));
			return;
		}
		ov_id[i] = ret;
	}

	for (i = 0; i < 2; i++) {
		/* selftest device must be in after state */
		if (of_selftest_device_exists(selftest_nr + i, PDEV_OVERLAY)
				!= after) {
			selftest(0, "overlay @\"%s\" failed @\"%s\" %s\n",
					overlay_path(overlay_nr + i),
					selftest_path(selftest_nr + i,
						PDEV_OVERLAY),
					!after ? "enabled" : "disabled");
			return;
		}
	}

	for (i = 1; i >= 0; i--) {
		ret = of_overlay_destroy(ov_id[i]);
		if (ret != 0) {
			selftest(0, "overlay @\"%s\" failed destroy @\"%s\"\n",
					overlay_path(overlay_nr + i),
					selftest_path(selftest_nr + i,
						PDEV_OVERLAY));
			return;
		}
	}

	for (i = 0; i < 2; i++) {
		/* selftest device must be again in before state */
		if (of_selftest_device_exists(selftest_nr + i, PDEV_OVERLAY)
				!= before) {
			selftest(0, "overlay @\"%s\" with device @\"%s\" %s\n",
					overlay_path(overlay_nr + i),
					selftest_path(selftest_nr + i,
						PDEV_OVERLAY),
					!before ? "enabled" : "disabled");
			return;
		}
	}

	selftest(1, "overlay test %d passed\n", 6);
}

/* test overlay application in sequence */
static void of_selftest_overlay_8(void)
{
	struct device_node *np;
	int ret, i, ov_id[2];
	int overlay_nr = 8, selftest_nr = 8;

	/* we don't care about device state in this test */

	/* apply the overlays */
	for (i = 0; i < 2; i++) {

		np = of_find_node_by_path(overlay_path(overlay_nr + i));
		if (np == NULL) {
			selftest(0, "could not find overlay node @\"%s\"\n",
					overlay_path(overlay_nr + i));
			return;
		}

		ret = of_overlay_create(np);
		if (ret < 0)  {
			selftest(0, "could not create overlay from \"%s\"\n",
					overlay_path(overlay_nr + i));
			return;
		}
		ov_id[i] = ret;
	}

	/* now try to remove first overlay (it should fail) */
	ret = of_overlay_destroy(ov_id[0]);
	if (ret == 0) {
		selftest(0, "overlay @\"%s\" was destroyed @\"%s\"\n",
				overlay_path(overlay_nr + 0),
				selftest_path(selftest_nr,
					PDEV_OVERLAY));
		return;
	}

	/* removing them in order should work */
	for (i = 1; i >= 0; i--) {
		ret = of_overlay_destroy(ov_id[i]);
		if (ret != 0) {
			selftest(0, "overlay @\"%s\" not destroyed @\"%s\"\n",
					overlay_path(overlay_nr + i),
					selftest_path(selftest_nr,
						PDEV_OVERLAY));
			return;
		}
	}

	selftest(1, "overlay test %d passed\n", 8);
}

/* test insertion of a bus with parent devices */
static void of_selftest_overlay_10(void)
{
	int ret;
	char *child_path;

	/* device should disable */
	ret = of_selftest_apply_overlay_check(10, 10, 0, 1, PDEV_OVERLAY);
	if (selftest(ret == 0,
			"overlay test %d failed; overlay application\n", 10))
		return;

	child_path = kasprintf(GFP_KERNEL, "%s/test-selftest101",
			selftest_path(10, PDEV_OVERLAY));
	if (selftest(child_path, "overlay test %d failed; kasprintf\n", 10))
		return;

	ret = of_path_device_type_exists(child_path, PDEV_OVERLAY);
	kfree(child_path);
	if (selftest(ret, "overlay test %d failed; no child device\n", 10))
		return;
}

/* test insertion of a bus with parent devices (and revert) */
static void of_selftest_overlay_11(void)
{
	int ret;

	/* device should disable */
	ret = of_selftest_apply_revert_overlay_check(11, 11, 0, 1,
			PDEV_OVERLAY);
	if (selftest(ret == 0,
			"overlay test %d failed; overlay application\n", 11))
		return;
}

#if IS_BUILTIN(CONFIG_I2C) && IS_ENABLED(CONFIG_OF_OVERLAY)

struct selftest_i2c_bus_data {
	struct platform_device	*pdev;
	struct i2c_adapter	adap;
};

static int selftest_i2c_master_xfer(struct i2c_adapter *adap,
		struct i2c_msg *msgs, int num)
{
	struct selftest_i2c_bus_data *std = i2c_get_adapdata(adap);

	(void)std;

	return num;
}

static u32 selftest_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm selftest_i2c_algo = {
	.master_xfer	= selftest_i2c_master_xfer,
	.functionality	= selftest_i2c_functionality,
};

static int selftest_i2c_bus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct selftest_i2c_bus_data *std;
	struct i2c_adapter *adap;
	int ret;

	if (np == NULL) {
		dev_err(dev, "No OF data for device\n");
		return -EINVAL;

	}

	dev_dbg(dev, "%s for node @%s\n", __func__, np->full_name);

	std = devm_kzalloc(dev, sizeof(*std), GFP_KERNEL);
	if (!std) {
		dev_err(dev, "Failed to allocate selftest i2c data\n");
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
	adap->algo = &selftest_i2c_algo;
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

static int selftest_i2c_bus_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct selftest_i2c_bus_data *std = platform_get_drvdata(pdev);

	dev_dbg(dev, "%s for node @%s\n", __func__, np->full_name);
	i2c_del_adapter(&std->adap);

	return 0;
}

static struct of_device_id selftest_i2c_bus_match[] = {
	{ .compatible = "selftest-i2c-bus", },
	{},
};

static struct platform_driver selftest_i2c_bus_driver = {
	.probe			= selftest_i2c_bus_probe,
	.remove			= selftest_i2c_bus_remove,
	.driver = {
		.name		= "selftest-i2c-bus",
		.of_match_table	= of_match_ptr(selftest_i2c_bus_match),
	},
};

static int selftest_i2c_dev_probe(struct i2c_client *client,
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

static int selftest_i2c_dev_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *np = client->dev.of_node;

	dev_dbg(dev, "%s for node @%s\n", __func__, np->full_name);
	return 0;
}

static const struct i2c_device_id selftest_i2c_dev_id[] = {
	{ .name = "selftest-i2c-dev" },
	{ }
};

static struct i2c_driver selftest_i2c_dev_driver = {
	.driver = {
		.name = "selftest-i2c-dev",
		.owner = THIS_MODULE,
	},
	.probe = selftest_i2c_dev_probe,
	.remove = selftest_i2c_dev_remove,
	.id_table = selftest_i2c_dev_id,
};

#if IS_BUILTIN(CONFIG_I2C_MUX)

struct selftest_i2c_mux_data {
	int nchans;
	struct i2c_adapter *adap[];
};

static int selftest_i2c_mux_select_chan(struct i2c_adapter *adap,
			       void *client, u32 chan)
{
	return 0;
}

static int selftest_i2c_mux_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret, i, nchans, size;
	struct device *dev = &client->dev;
	struct i2c_adapter *adap = to_i2c_adapter(dev->parent);
	struct device_node *np = client->dev.of_node, *child;
	struct selftest_i2c_mux_data *stm;
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

	size = offsetof(struct selftest_i2c_mux_data, adap[nchans]);
	stm = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!stm) {
		dev_err(dev, "Out of memory\n");
		return -ENOMEM;
	}
	stm->nchans = nchans;
	for (i = 0; i < nchans; i++) {
		stm->adap[i] = i2c_add_mux_adapter(adap, dev, client,
				0, i, 0, selftest_i2c_mux_select_chan, NULL);
		if (!stm->adap[i]) {
			dev_err(dev, "Failed to register mux #%d\n", i);
			for (i--; i >= 0; i--)
				i2c_del_mux_adapter(stm->adap[i]);
			return -ENODEV;
		}
	}

	i2c_set_clientdata(client, stm);

	return 0;
};

static int selftest_i2c_mux_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *np = client->dev.of_node;
	struct selftest_i2c_mux_data *stm = i2c_get_clientdata(client);
	int i;

	dev_dbg(dev, "%s for node @%s\n", __func__, np->full_name);
	for (i = stm->nchans - 1; i >= 0; i--)
		i2c_del_mux_adapter(stm->adap[i]);
	return 0;
}

static const struct i2c_device_id selftest_i2c_mux_id[] = {
	{ .name = "selftest-i2c-mux" },
	{ }
};

static struct i2c_driver selftest_i2c_mux_driver = {
	.driver = {
		.name = "selftest-i2c-mux",
		.owner = THIS_MODULE,
	},
	.probe = selftest_i2c_mux_probe,
	.remove = selftest_i2c_mux_remove,
	.id_table = selftest_i2c_mux_id,
};

#endif

static int of_selftest_overlay_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&selftest_i2c_dev_driver);
	if (selftest(ret == 0,
			"could not register selftest i2c device driver\n"))
		return ret;

	ret = platform_driver_register(&selftest_i2c_bus_driver);
	if (selftest(ret == 0,
			"could not register selftest i2c bus driver\n"))
		return ret;

#if IS_BUILTIN(CONFIG_I2C_MUX)
	ret = i2c_add_driver(&selftest_i2c_mux_driver);
	if (selftest(ret == 0,
			"could not register selftest i2c mux driver\n"))
		return ret;
#endif

	return 0;
}

static void of_selftest_overlay_i2c_cleanup(void)
{
#if IS_BUILTIN(CONFIG_I2C_MUX)
	i2c_del_driver(&selftest_i2c_mux_driver);
#endif
	platform_driver_unregister(&selftest_i2c_bus_driver);
	i2c_del_driver(&selftest_i2c_dev_driver);
}

static void of_selftest_overlay_i2c_12(void)
{
	int ret;

	/* device should enable */
	ret = of_selftest_apply_overlay_check(12, 12, 0, 1, I2C_OVERLAY);
	if (ret != 0)
		return;

	selftest(1, "overlay test %d passed\n", 12);
}

/* test deactivation of device */
static void of_selftest_overlay_i2c_13(void)
{
	int ret;

	/* device should disable */
	ret = of_selftest_apply_overlay_check(13, 13, 1, 0, I2C_OVERLAY);
	if (ret != 0)
		return;

	selftest(1, "overlay test %d passed\n", 13);
}

/* just check for i2c mux existence */
static void of_selftest_overlay_i2c_14(void)
{
}

static void of_selftest_overlay_i2c_15(void)
{
	int ret;

	/* device should enable */
	ret = of_selftest_apply_overlay_check(16, 15, 0, 1, I2C_OVERLAY);
	if (ret != 0)
		return;

	selftest(1, "overlay test %d passed\n", 15);
}

#else

static inline void of_selftest_overlay_i2c_14(void) { }
static inline void of_selftest_overlay_i2c_15(void) { }

#endif

static void __init of_selftest_overlay(void)
{
	struct device_node *bus_np = NULL;
	int ret;

	ret = platform_driver_register(&selftest_driver);
	if (ret != 0) {
		selftest(0, "could not register selftest driver\n");
		goto out;
	}

	bus_np = of_find_node_by_path(bus_path);
	if (bus_np == NULL) {
		selftest(0, "could not find bus_path \"%s\"\n", bus_path);
		goto out;
	}

	ret = of_platform_populate(bus_np, of_default_bus_match_table,
			NULL, NULL);
	if (ret != 0) {
		selftest(0, "could not populate bus @ \"%s\"\n", bus_path);
		goto out;
	}

	if (!of_selftest_device_exists(100, PDEV_OVERLAY)) {
		selftest(0, "could not find selftest0 @ \"%s\"\n",
				selftest_path(100, PDEV_OVERLAY));
		goto out;
	}

	if (of_selftest_device_exists(101, PDEV_OVERLAY)) {
		selftest(0, "selftest1 @ \"%s\" should not exist\n",
				selftest_path(101, PDEV_OVERLAY));
		goto out;
	}

	selftest(1, "basic infrastructure of overlays passed");

	/* tests in sequence */
	of_selftest_overlay_0();
	of_selftest_overlay_1();
	of_selftest_overlay_2();
	of_selftest_overlay_3();
	of_selftest_overlay_4();
	of_selftest_overlay_5();
	of_selftest_overlay_6();
	of_selftest_overlay_8();

	of_selftest_overlay_10();
	of_selftest_overlay_11();

#if IS_BUILTIN(CONFIG_I2C)
	if (selftest(of_selftest_overlay_i2c_init() == 0, "i2c init failed\n"))
		goto out;

	of_selftest_overlay_i2c_12();
	of_selftest_overlay_i2c_13();
	of_selftest_overlay_i2c_14();
	of_selftest_overlay_i2c_15();

	of_selftest_overlay_i2c_cleanup();
#endif

out:
	of_node_put(bus_np);
}

#else
static inline void __init of_selftest_overlay(void) { }
#endif

static int __init of_selftest(void)
{
	struct device_node *np;
	int res;

	/* adding data for selftest */
	res = selftest_data_add();
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

	pr_info("start of selftest - you will see error messages\n");
	of_selftest_check_tree_linkage();
	of_selftest_check_phandles();
	of_selftest_find_node_by_name();
	of_selftest_dynamic();
	of_selftest_parse_phandle_with_args();
	of_selftest_property_string();
	of_selftest_property_copy();
	of_selftest_changeset();
	of_selftest_parse_interrupts();
	of_selftest_parse_interrupts_extended();
	of_selftest_match_node();
	of_selftest_platform_populate();
	of_selftest_overlay();

	/* Double check linkage after removing testcase data */
	of_selftest_check_tree_linkage();

	pr_info("end of selftest - %i passed, %i failed\n",
		selftest_results.passed, selftest_results.failed);

	return 0;
}
late_initcall(of_selftest);

/*
 * Self tests for device tree subsystem
 */

#define pr_fmt(fmt) "### dt-test ### " fmt

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/device.h>

#include "of_private.h"

static struct selftest_results {
	int passed;
	int failed;
} selftest_results;

#define selftest(result, fmt, ...) { \
	if (!(result)) { \
		pr_err("FAIL %s:%i " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
		selftest_results.passed = false; \
	} else { \
		pr_info("pass %s:%i\n", __FILE__, __LINE__); \
	} \
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

static void __init of_selftest_property_match_string(void)
{
	struct device_node *np;
	int rc;

	pr_info("start\n");
	np = of_find_node_by_path("/testcase-data/phandle-tests/consumer-a");
	if (!np) {
		pr_err("No testcase data in device tree\n");
		return;
	}

	rc = of_property_match_string(np, "phandle-list-names", "first");
	selftest(rc == 0, "first expected:0 got:%i\n", rc);
	rc = of_property_match_string(np, "phandle-list-names", "second");
	selftest(rc == 1, "second expected:0 got:%i\n", rc);
	rc = of_property_match_string(np, "phandle-list-names", "third");
	selftest(rc == 2, "third expected:0 got:%i\n", rc);
	rc = of_property_match_string(np, "phandle-list-names", "fourth");
	selftest(rc == -ENODATA, "unmatched string; rc=%i", rc);
	rc = of_property_match_string(np, "missing-property", "blah");
	selftest(rc == -EINVAL, "missing property; rc=%i", rc);
	rc = of_property_match_string(np, "empty-property", "blah");
	selftest(rc == -ENODATA, "empty property; rc=%i", rc);
	rc = of_property_match_string(np, "unterminated-string", "blah");
	selftest(rc == -EILSEQ, "unterminated string; rc=%i", rc);
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
	struct device_node *n1, *n2, *n21, *nremove, *parent;
	struct of_changeset chgset;

	of_changeset_init(&chgset);
	n1 = __of_node_alloc("/testcase-data/changeset/n1", GFP_KERNEL);
	selftest(n1, "testcase setup failure\n");
	n2 = __of_node_alloc("/testcase-data/changeset/n2", GFP_KERNEL);
	selftest(n2, "testcase setup failure\n");
	n21 = __of_node_alloc("/testcase-data/changeset/n2/n21", GFP_KERNEL);
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

	mutex_lock(&of_mutex);
	selftest(!of_changeset_revert(&chgset), "revert failed\n");
	mutex_unlock(&of_mutex);

	of_changeset_destroy(&chgset);
#endif
}

static int __init of_selftest(void)
{
	struct device_node *np;

	np = of_find_node_by_path("/testcase-data/phandle-tests/consumer-a");
	if (!np) {
		pr_info("No testcase data in device tree; not running tests\n");
		return 0;
	}
	of_node_put(np);

	pr_info("start of selftest - you will see error messages\n");
	of_selftest_parse_phandle_with_args();
	of_selftest_property_match_string();
	of_selftest_property_copy();
	of_selftest_changeset();
	pr_info("end of selftest - %i passed, %i failed\n",
		selftest_results.passed, selftest_results.failed);
	return 0;
}
late_initcall(of_selftest);

// SPDX-License-Identifier: GPL-2.0
/*
 * Test managed DeviceTree APIs
 */

#include <linux/of.h>
#include <linux/of_fdt.h>

#include <kunit/of.h>
#include <kunit/test.h>
#include <kunit/resource.h>

#if defined(CONFIG_OF_OVERLAY) && defined(CONFIG_OF_EARLY_FLATTREE)

static void of_overlay_fdt_apply_kunit_exit(void *ovcs_id)
{
	of_overlay_remove(ovcs_id);
}

/**
 * of_overlay_fdt_apply_kunit() - Test managed of_overlay_fdt_apply()
 * @test: test context
 * @overlay_fdt: device tree overlay to apply
 * @overlay_fdt_size: size in bytes of @overlay_fdt
 * @ovcs_id: identifier of overlay, used to remove the overlay
 *
 * Just like of_overlay_fdt_apply(), except the overlay is managed by the test
 * case and is automatically removed with of_overlay_remove() after the test
 * case concludes.
 *
 * Return: 0 on success, negative errno on failure
 */
int of_overlay_fdt_apply_kunit(struct kunit *test, void *overlay_fdt,
			       u32 overlay_fdt_size, int *ovcs_id)
{
	int ret;
	int *copy_id;

	copy_id = kunit_kmalloc(test, sizeof(*copy_id), GFP_KERNEL);
	if (!copy_id)
		return -ENOMEM;

	ret = of_overlay_fdt_apply(overlay_fdt, overlay_fdt_size,
				   ovcs_id, NULL);
	if (ret)
		return ret;

	*copy_id = *ovcs_id;

	return kunit_add_action_or_reset(test, of_overlay_fdt_apply_kunit_exit,
					 copy_id);
}
EXPORT_SYMBOL_GPL(of_overlay_fdt_apply_kunit);

#endif

KUNIT_DEFINE_ACTION_WRAPPER(of_node_put_wrapper, of_node_put, struct device_node *);

/**
 * of_node_put_kunit() - Test managed of_node_put()
 * @test: test context
 * @node: node to pass to `of_node_put()`
 *
 * Just like of_node_put(), except the node is managed by the test case and is
 * automatically put with of_node_put() after the test case concludes.
 */
void of_node_put_kunit(struct kunit *test, struct device_node *node)
{
	if (kunit_add_action(test, of_node_put_wrapper, node)) {
		KUNIT_FAIL(test,
			   "Can't allocate a kunit resource to put of_node\n");
	}
}
EXPORT_SYMBOL_GPL(of_node_put_kunit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Test managed DeviceTree APIs");

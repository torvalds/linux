// SPDX-License-Identifier: GPL-2.0
#include <linux/platform_device.h>
#include <linux/memregion.h>
#include <linux/module.h>
#include <linux/dax.h>
#include <linux/mm.h>

static bool nohmem;
module_param_named(disable, nohmem, bool, 0444);

static bool platform_initialized;
static DEFINE_MUTEX(hmem_resource_lock);
static struct resource hmem_active = {
	.name = "HMEM devices",
	.start = 0,
	.end = -1,
	.flags = IORESOURCE_MEM,
};

int walk_hmem_resources(struct device *host, walk_hmem_fn fn)
{
	struct resource *res;
	int rc = 0;

	mutex_lock(&hmem_resource_lock);
	for (res = hmem_active.child; res; res = res->sibling) {
		rc = fn(host, (int) res->desc, res);
		if (rc)
			break;
	}
	mutex_unlock(&hmem_resource_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(walk_hmem_resources);

static void __hmem_register_resource(int target_nid, struct resource *res)
{
	struct platform_device *pdev;
	struct resource *new;
	int rc;

	new = __request_region(&hmem_active, res->start, resource_size(res), "",
			       0);
	if (!new) {
		pr_debug("hmem range %pr already active\n", res);
		return;
	}

	new->desc = target_nid;

	if (platform_initialized)
		return;

	pdev = platform_device_alloc("hmem_platform", 0);
	if (!pdev) {
		pr_err_once("failed to register device-dax hmem_platform device\n");
		return;
	}

	rc = platform_device_add(pdev);
	if (rc)
		platform_device_put(pdev);
	else
		platform_initialized = true;
}

void hmem_register_resource(int target_nid, struct resource *res)
{
	if (nohmem)
		return;

	mutex_lock(&hmem_resource_lock);
	__hmem_register_resource(target_nid, res);
	mutex_unlock(&hmem_resource_lock);
}

static __init int hmem_register_one(struct resource *res, void *data)
{
	hmem_register_resource(phys_to_target_node(res->start), res);

	return 0;
}

static __init int hmem_init(void)
{
	walk_iomem_res_desc(IORES_DESC_SOFT_RESERVED,
			IORESOURCE_MEM, 0, -1, NULL, hmem_register_one);
	return 0;
}

/*
 * As this is a fallback for address ranges unclaimed by the ACPI HMAT
 * parsing it must be at an initcall level greater than hmat_init().
 */
device_initcall(hmem_init);

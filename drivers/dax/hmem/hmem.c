// SPDX-License-Identifier: GPL-2.0
#include <linux/platform_device.h>
#include <linux/memregion.h>
#include <linux/module.h>
#include <linux/pfn_t.h>
#include "../bus.h"

static bool region_idle;
module_param_named(region_idle, region_idle, bool, 0644);

static int dax_hmem_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dax_region *dax_region;
	struct memregion_info *mri;
	struct dev_dax_data data;
	struct dev_dax *dev_dax;
	struct resource *res;
	struct range range;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOMEM;

	mri = dev->platform_data;
	range.start = res->start;
	range.end = res->end;
	dax_region = alloc_dax_region(dev, pdev->id, &range, mri->target_node,
			PMD_SIZE, 0);
	if (!dax_region)
		return -ENOMEM;

	data = (struct dev_dax_data) {
		.dax_region = dax_region,
		.id = -1,
		.size = region_idle ? 0 : resource_size(res),
	};
	dev_dax = devm_create_dev_dax(&data);
	if (IS_ERR(dev_dax))
		return PTR_ERR(dev_dax);

	/* child dev_dax instances now own the lifetime of the dax_region */
	dax_region_put(dax_region);
	return 0;
}

static int dax_hmem_remove(struct platform_device *pdev)
{
	/* devm handles teardown */
	return 0;
}

static struct platform_driver dax_hmem_driver = {
	.probe = dax_hmem_probe,
	.remove = dax_hmem_remove,
	.driver = {
		.name = "hmem",
	},
};

module_platform_driver(dax_hmem_driver);

MODULE_ALIAS("platform:hmem*");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");

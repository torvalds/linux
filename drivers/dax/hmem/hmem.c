// SPDX-License-Identifier: GPL-2.0
#include <linux/platform_device.h>
#include <linux/memregion.h>
#include <linux/module.h>
#include <linux/pfn_t.h>
#include <linux/dax.h>
#include "../bus.h"

static bool region_idle;
module_param_named(region_idle, region_idle, bool, 0644);

static int dax_hmem_probe(struct platform_device *pdev)
{
	unsigned long flags = IORESOURCE_DAX_KMEM;
	struct device *dev = &pdev->dev;
	struct dax_region *dax_region;
	struct memregion_info *mri;
	struct dev_dax_data data;

	/*
	 * @region_idle == true indicates that an administrative agent
	 * wants to manipulate the range partitioning before the devices
	 * are created, so do not send them to the dax_kmem driver by
	 * default.
	 */
	if (region_idle)
		flags = 0;

	mri = dev->platform_data;
	dax_region = alloc_dax_region(dev, pdev->id, &mri->range,
				      mri->target_node, PMD_SIZE, flags);
	if (!dax_region)
		return -ENOMEM;

	data = (struct dev_dax_data) {
		.dax_region = dax_region,
		.id = -1,
		.size = region_idle ? 0 : range_len(&mri->range),
	};

	return PTR_ERR_OR_ZERO(devm_create_dev_dax(&data));
}

static struct platform_driver dax_hmem_driver = {
	.probe = dax_hmem_probe,
	.driver = {
		.name = "hmem",
	},
};

static void release_memregion(void *data)
{
	memregion_free((long) data);
}

static void release_hmem(void *pdev)
{
	platform_device_unregister(pdev);
}

static int hmem_register_device(struct device *host, int target_nid,
				const struct resource *res)
{
	struct platform_device *pdev;
	struct memregion_info info;
	long id;
	int rc;

	if (IS_ENABLED(CONFIG_CXL_REGION) &&
	    region_intersects(res->start, resource_size(res), IORESOURCE_MEM,
			      IORES_DESC_CXL) != REGION_DISJOINT) {
		dev_dbg(host, "deferring range to CXL: %pr\n", res);
		return 0;
	}

	rc = region_intersects(res->start, resource_size(res), IORESOURCE_MEM,
			       IORES_DESC_SOFT_RESERVED);
	if (rc != REGION_INTERSECTS)
		return 0;

	id = memregion_alloc(GFP_KERNEL);
	if (id < 0) {
		dev_err(host, "memregion allocation failure for %pr\n", res);
		return -ENOMEM;
	}
	rc = devm_add_action_or_reset(host, release_memregion, (void *) id);
	if (rc)
		return rc;

	pdev = platform_device_alloc("hmem", id);
	if (!pdev) {
		dev_err(host, "device allocation failure for %pr\n", res);
		return -ENOMEM;
	}

	pdev->dev.numa_node = numa_map_to_online_node(target_nid);
	info = (struct memregion_info) {
		.target_node = target_nid,
		.range = {
			.start = res->start,
			.end = res->end,
		},
	};
	rc = platform_device_add_data(pdev, &info, sizeof(info));
	if (rc < 0) {
		dev_err(host, "memregion_info allocation failure for %pr\n",
		       res);
		goto out_put;
	}

	rc = platform_device_add(pdev);
	if (rc < 0) {
		dev_err(host, "%s add failed for %pr\n", dev_name(&pdev->dev),
			res);
		goto out_put;
	}

	return devm_add_action_or_reset(host, release_hmem, pdev);

out_put:
	platform_device_put(pdev);
	return rc;
}

static int dax_hmem_platform_probe(struct platform_device *pdev)
{
	return walk_hmem_resources(&pdev->dev, hmem_register_device);
}

static struct platform_driver dax_hmem_platform_driver = {
	.probe = dax_hmem_platform_probe,
	.driver = {
		.name = "hmem_platform",
	},
};

static __init int dax_hmem_init(void)
{
	int rc;

	rc = platform_driver_register(&dax_hmem_platform_driver);
	if (rc)
		return rc;

	rc = platform_driver_register(&dax_hmem_driver);
	if (rc)
		platform_driver_unregister(&dax_hmem_platform_driver);

	return rc;
}

static __exit void dax_hmem_exit(void)
{
	platform_driver_unregister(&dax_hmem_driver);
	platform_driver_unregister(&dax_hmem_platform_driver);
}

module_init(dax_hmem_init);
module_exit(dax_hmem_exit);

/* Allow for CXL to define its own dax regions */
#if IS_ENABLED(CONFIG_CXL_REGION)
#if IS_MODULE(CONFIG_CXL_ACPI)
MODULE_SOFTDEP("pre: cxl_acpi");
#endif
#endif

MODULE_ALIAS("platform:hmem*");
MODULE_ALIAS("platform:hmem_platform*");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");

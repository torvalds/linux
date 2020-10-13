// SPDX-License-Identifier: GPL-2.0
#include <linux/platform_device.h>
#include <linux/memregion.h>
#include <linux/module.h>
#include <linux/dax.h>
#include <linux/mm.h>

void hmem_register_device(int target_nid, struct resource *r)
{
	/* define a clean / non-busy resource for the platform device */
	struct resource res = {
		.start = r->start,
		.end = r->end,
		.flags = IORESOURCE_MEM,
	};
	struct platform_device *pdev;
	struct memregion_info info;
	int rc, id;

	rc = region_intersects(res.start, resource_size(&res), IORESOURCE_MEM,
			IORES_DESC_SOFT_RESERVED);
	if (rc != REGION_INTERSECTS)
		return;

	id = memregion_alloc(GFP_KERNEL);
	if (id < 0) {
		pr_err("memregion allocation failure for %pr\n", &res);
		return;
	}

	pdev = platform_device_alloc("hmem", id);
	if (!pdev) {
		pr_err("hmem device allocation failure for %pr\n", &res);
		goto out_pdev;
	}

	pdev->dev.numa_node = numa_map_to_online_node(target_nid);
	info = (struct memregion_info) {
		.target_node = target_nid,
	};
	rc = platform_device_add_data(pdev, &info, sizeof(info));
	if (rc < 0) {
		pr_err("hmem memregion_info allocation failure for %pr\n", &res);
		goto out_pdev;
	}

	rc = platform_device_add_resources(pdev, &res, 1);
	if (rc < 0) {
		pr_err("hmem resource allocation failure for %pr\n", &res);
		goto out_resource;
	}

	rc = platform_device_add(pdev);
	if (rc < 0) {
		dev_err(&pdev->dev, "device add failed for %pr\n", &res);
		goto out_resource;
	}

	return;

out_resource:
	put_device(&pdev->dev);
out_pdev:
	memregion_free(id);
}

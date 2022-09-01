// SPDX-License-Identifier: GPL-2.0
/*
 * Stub IOMMU driver which does nothing.
 * The main purpose of it being present is to reuse generic IOMMU device tree
 * bindings by Xen grant DMA-mapping layer.
 *
 * Copyright (C) 2022 EPAM Systems Inc.
 */

#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/platform_device.h>

struct grant_dma_iommu_device {
	struct device *dev;
	struct iommu_device iommu;
};

/* Nothing is really needed here */
static const struct iommu_ops grant_dma_iommu_ops;

static const struct of_device_id grant_dma_iommu_of_match[] = {
	{ .compatible = "xen,grant-dma" },
	{ },
};

static int grant_dma_iommu_probe(struct platform_device *pdev)
{
	struct grant_dma_iommu_device *mmu;
	int ret;

	mmu = devm_kzalloc(&pdev->dev, sizeof(*mmu), GFP_KERNEL);
	if (!mmu)
		return -ENOMEM;

	mmu->dev = &pdev->dev;

	ret = iommu_device_register(&mmu->iommu, &grant_dma_iommu_ops, &pdev->dev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, mmu);

	return 0;
}

static int grant_dma_iommu_remove(struct platform_device *pdev)
{
	struct grant_dma_iommu_device *mmu = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	iommu_device_unregister(&mmu->iommu);

	return 0;
}

static struct platform_driver grant_dma_iommu_driver = {
	.driver = {
		.name = "grant-dma-iommu",
		.of_match_table = grant_dma_iommu_of_match,
	},
	.probe = grant_dma_iommu_probe,
	.remove = grant_dma_iommu_remove,
};

static int __init grant_dma_iommu_init(void)
{
	struct device_node *iommu_np;

	iommu_np = of_find_matching_node(NULL, grant_dma_iommu_of_match);
	if (!iommu_np)
		return 0;

	of_node_put(iommu_np);

	return platform_driver_register(&grant_dma_iommu_driver);
}
subsys_initcall(grant_dma_iommu_init);

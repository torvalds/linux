// SPDX-License-Identifier: GPL-2.0-only
/*
 * IOMMU driver for BCM2712 TLB cache
 *
 * Copyright (c) 2023 Raspberry Pi Ltd.
 * Copyright (c) 2026 Daniel Drake
 *
 * The BCM2712 IOMMUC is a centralized TLB which accelerates address translation
 * across the SoC's IOMMU devices. If an address mapping is not found in the
 * IOMMU's local TLB cache, then this IOMMUC is consulted. The IOMMUC must be
 * explicitly invalidated when modifying or unmapping IOMMU page tables.
 */

#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include "bcm2712-iommu-cache.h"

struct bcm2712_iommu_cache {
	struct device *dev;
	spinlock_t hw_lock;
	void __iomem *reg_base;
};

#define MMUC_CONTROL_ENABLE   BIT(0)
#define MMUC_CONTROL_FLUSH    BIT(1)
#define MMUC_CONTROL_FLUSHING BIT(2)

void bcm2712_iommu_cache_flush(struct bcm2712_iommu_cache *cache)
{
	u32 val;
	int ret;

	scoped_guard(spinlock_irqsave, &cache->hw_lock) {
		writel(MMUC_CONTROL_ENABLE | MMUC_CONTROL_FLUSH,
		       cache->reg_base);

		ret = readl_poll_timeout_atomic(cache->reg_base, val,
						!(val & MMUC_CONTROL_FLUSHING),
						0, 50);
	}

	if (ret)
		dev_err_ratelimited(cache->dev, "cache flush timed out\n");
}

static int bcm2712_iommu_cache_probe(struct platform_device *pdev)
{
	struct bcm2712_iommu_cache *cache;

	cache = devm_kzalloc(&pdev->dev, sizeof(*cache), GFP_KERNEL);
	if (!cache)
		return -ENOMEM;

	cache->dev = &pdev->dev;
	spin_lock_init(&cache->hw_lock);

	cache->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(cache->reg_base))
		return PTR_ERR(cache->reg_base);

	platform_set_drvdata(pdev, cache);
	return 0;
}

static const struct of_device_id bcm2712_iommu_cache_of_match[] = {
	{ .compatible = "brcm,bcm2712-iommuc" },
	{ /* sentinel */ },
};

static struct platform_driver bcm2712_iommu_cache_driver = {
	.probe = bcm2712_iommu_cache_probe,
	.driver = {
		.name = "bcm2712-iommu-cache",
		.of_match_table = bcm2712_iommu_cache_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(bcm2712_iommu_cache_driver);

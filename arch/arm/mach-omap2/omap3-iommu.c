/*
 * omap iommu: omap3 device registration
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>

#include <plat/iommu.h>

struct iommu_device {
	resource_size_t base;
	int irq;
	struct iommu_platform_data pdata;
	struct resource res[2];
};

static struct iommu_device devices[] = {
	{
		.base = 0x480bd400,
		.irq = 24,
		.pdata = {
			.name = "isp",
			.nr_tlb_entries = 8,
			.clk_name = "cam_ick",
		},
	},
#if defined(CONFIG_MPU_BRIDGE_IOMMU)
	{
		.base = 0x5d000000,
		.irq = 28,
		.pdata = {
			.name = "iva2",
			.nr_tlb_entries = 32,
			.clk_name = "iva2_ck",
		},
	},
#endif
};
#define NR_IOMMU_DEVICES ARRAY_SIZE(devices)

static struct platform_device *omap3_iommu_pdev[NR_IOMMU_DEVICES];

static int __init omap3_iommu_init(void)
{
	int i, err;
	struct resource res[] = {
		{ .flags = IORESOURCE_MEM },
		{ .flags = IORESOURCE_IRQ },
	};

	for (i = 0; i < NR_IOMMU_DEVICES; i++) {
		struct platform_device *pdev;
		const struct iommu_device *d = &devices[i];

		pdev = platform_device_alloc("omap-iommu", i);
		if (!pdev) {
			err = -ENOMEM;
			goto err_out;
		}

		res[0].start = d->base;
		res[0].end = d->base + MMU_REG_SIZE - 1;
		res[1].start = res[1].end = d->irq;

		err = platform_device_add_resources(pdev, res,
						    ARRAY_SIZE(res));
		if (err)
			goto err_out;
		err = platform_device_add_data(pdev, &d->pdata,
					       sizeof(d->pdata));
		if (err)
			goto err_out;
		err = platform_device_add(pdev);
		if (err)
			goto err_out;
		omap3_iommu_pdev[i] = pdev;
	}
	return 0;

err_out:
	while (i--)
		platform_device_put(omap3_iommu_pdev[i]);
	return err;
}
module_init(omap3_iommu_init);

static void __exit omap3_iommu_exit(void)
{
	int i;

	for (i = 0; i < NR_IOMMU_DEVICES; i++)
		platform_device_unregister(omap3_iommu_pdev[i]);
}
module_exit(omap3_iommu_exit);

MODULE_AUTHOR("Hiroshi DOYU");
MODULE_DESCRIPTION("omap iommu: omap3 device registration");
MODULE_LICENSE("GPL v2");

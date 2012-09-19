/*
 * omap iommu: omap device registration
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>

#include <plat/iommu.h>
#include <plat/irqs.h>

struct iommu_device {
	resource_size_t base;
	int irq;
	struct iommu_platform_data pdata;
	struct resource res[2];
};
static struct iommu_device *devices;
static int num_iommu_devices;

#ifdef CONFIG_ARCH_OMAP3
static struct iommu_device omap3_devices[] = {
	{
		.base = 0x480bd400,
		.irq = 24,
		.pdata = {
			.name = "isp",
			.nr_tlb_entries = 8,
			.clk_name = "cam_ick",
			.da_start = 0x0,
			.da_end = 0xFFFFF000,
		},
	},
#if defined(CONFIG_OMAP_IOMMU_IVA2)
	{
		.base = 0x5d000000,
		.irq = 28,
		.pdata = {
			.name = "iva2",
			.nr_tlb_entries = 32,
			.clk_name = "iva2_ck",
			.da_start = 0x11000000,
			.da_end = 0xFFFFF000,
		},
	},
#endif
};
#define NR_OMAP3_IOMMU_DEVICES ARRAY_SIZE(omap3_devices)
static struct platform_device *omap3_iommu_pdev[NR_OMAP3_IOMMU_DEVICES];
#else
#define omap3_devices		NULL
#define NR_OMAP3_IOMMU_DEVICES	0
#define omap3_iommu_pdev	NULL
#endif

#ifdef CONFIG_ARCH_OMAP4
static struct iommu_device omap4_devices[] = {
	{
		.base = OMAP4_MMU1_BASE,
		.irq = OMAP44XX_IRQ_DUCATI_MMU,
		.pdata = {
			.name = "ducati",
			.nr_tlb_entries = 32,
			.clk_name = "ipu_fck",
			.da_start = 0x0,
			.da_end = 0xFFFFF000,
		},
	},
	{
		.base = OMAP4_MMU2_BASE,
		.irq = OMAP44XX_IRQ_TESLA_MMU,
		.pdata = {
			.name = "tesla",
			.nr_tlb_entries = 32,
			.clk_name = "dsp_fck",
			.da_start = 0x0,
			.da_end = 0xFFFFF000,
		},
	},
};
#define NR_OMAP4_IOMMU_DEVICES ARRAY_SIZE(omap4_devices)
static struct platform_device *omap4_iommu_pdev[NR_OMAP4_IOMMU_DEVICES];
#else
#define omap4_devices		NULL
#define NR_OMAP4_IOMMU_DEVICES	0
#define omap4_iommu_pdev	NULL
#endif

static struct platform_device **omap_iommu_pdev;

static int __init omap_iommu_init(void)
{
	int i, err;
	struct resource res[] = {
		{ .flags = IORESOURCE_MEM },
		{ .flags = IORESOURCE_IRQ },
	};

	if (cpu_is_omap34xx()) {
		devices = omap3_devices;
		omap_iommu_pdev = omap3_iommu_pdev;
		num_iommu_devices = NR_OMAP3_IOMMU_DEVICES;
	} else if (cpu_is_omap44xx()) {
		devices = omap4_devices;
		omap_iommu_pdev = omap4_iommu_pdev;
		num_iommu_devices = NR_OMAP4_IOMMU_DEVICES;
	} else
		return -ENODEV;

	for (i = 0; i < num_iommu_devices; i++) {
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
		omap_iommu_pdev[i] = pdev;
	}
	return 0;

err_out:
	while (i--)
		platform_device_put(omap_iommu_pdev[i]);
	return err;
}
/* must be ready before omap3isp is probed */
subsys_initcall(omap_iommu_init);

static void __exit omap_iommu_exit(void)
{
	int i;

	for (i = 0; i < num_iommu_devices; i++)
		platform_device_unregister(omap_iommu_pdev[i]);
}
module_exit(omap_iommu_exit);

MODULE_AUTHOR("Hiroshi DOYU");
MODULE_DESCRIPTION("omap iommu: omap device registration");
MODULE_LICENSE("GPL v2");

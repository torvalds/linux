/*
 * IPMMU/IPMMUI
 * Copyright (C) 2012  Hideki EIRAKU
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/err.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/platform_data/sh_ipmmu.h>
#include "shmobile-ipmmu.h"

#define IMCTR1 0x000
#define IMCTR2 0x004
#define IMASID 0x010
#define IMTTBR 0x014
#define IMTTBCR 0x018

#define IMCTR1_TLBEN (1 << 0)
#define IMCTR1_FLUSH (1 << 1)

static void ipmmu_reg_write(struct shmobile_ipmmu *ipmmu, unsigned long reg_off,
			    unsigned long data)
{
	iowrite32(data, ipmmu->ipmmu_base + reg_off);
}

void ipmmu_tlb_flush(struct shmobile_ipmmu *ipmmu)
{
	if (!ipmmu)
		return;

	spin_lock(&ipmmu->flush_lock);
	if (ipmmu->tlb_enabled)
		ipmmu_reg_write(ipmmu, IMCTR1, IMCTR1_FLUSH | IMCTR1_TLBEN);
	else
		ipmmu_reg_write(ipmmu, IMCTR1, IMCTR1_FLUSH);
	spin_unlock(&ipmmu->flush_lock);
}

void ipmmu_tlb_set(struct shmobile_ipmmu *ipmmu, unsigned long phys, int size,
		   int asid)
{
	if (!ipmmu)
		return;

	spin_lock(&ipmmu->flush_lock);
	switch (size) {
	default:
		ipmmu->tlb_enabled = 0;
		break;
	case 0x2000:
		ipmmu_reg_write(ipmmu, IMTTBCR, 1);
		ipmmu->tlb_enabled = 1;
		break;
	case 0x1000:
		ipmmu_reg_write(ipmmu, IMTTBCR, 2);
		ipmmu->tlb_enabled = 1;
		break;
	case 0x800:
		ipmmu_reg_write(ipmmu, IMTTBCR, 3);
		ipmmu->tlb_enabled = 1;
		break;
	case 0x400:
		ipmmu_reg_write(ipmmu, IMTTBCR, 4);
		ipmmu->tlb_enabled = 1;
		break;
	case 0x200:
		ipmmu_reg_write(ipmmu, IMTTBCR, 5);
		ipmmu->tlb_enabled = 1;
		break;
	case 0x100:
		ipmmu_reg_write(ipmmu, IMTTBCR, 6);
		ipmmu->tlb_enabled = 1;
		break;
	case 0x80:
		ipmmu_reg_write(ipmmu, IMTTBCR, 7);
		ipmmu->tlb_enabled = 1;
		break;
	}
	ipmmu_reg_write(ipmmu, IMTTBR, phys);
	ipmmu_reg_write(ipmmu, IMASID, asid);
	spin_unlock(&ipmmu->flush_lock);
}

static int ipmmu_probe(struct platform_device *pdev)
{
	struct shmobile_ipmmu *ipmmu;
	struct resource *res;
	struct shmobile_ipmmu_platform_data *pdata = pdev->dev.platform_data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "cannot get platform resources\n");
		return -ENOENT;
	}
	ipmmu = devm_kzalloc(&pdev->dev, sizeof(*ipmmu), GFP_KERNEL);
	if (!ipmmu) {
		dev_err(&pdev->dev, "cannot allocate device data\n");
		return -ENOMEM;
	}
	spin_lock_init(&ipmmu->flush_lock);
	ipmmu->dev = &pdev->dev;
	ipmmu->ipmmu_base = devm_ioremap_nocache(&pdev->dev, res->start,
						resource_size(res));
	if (!ipmmu->ipmmu_base) {
		dev_err(&pdev->dev, "ioremap_nocache failed\n");
		return -ENOMEM;
	}
	ipmmu->dev_names = pdata->dev_names;
	ipmmu->num_dev_names = pdata->num_dev_names;
	platform_set_drvdata(pdev, ipmmu);
	ipmmu_reg_write(ipmmu, IMCTR1, 0x0); /* disable TLB */
	ipmmu_reg_write(ipmmu, IMCTR2, 0x0); /* disable PMB */
	ipmmu_iommu_init(ipmmu);
	return 0;
}

static struct platform_driver ipmmu_driver = {
	.probe = ipmmu_probe,
	.driver = {
		.owner = THIS_MODULE,
		.name = "ipmmu",
	},
};

static int __init ipmmu_init(void)
{
	return platform_driver_register(&ipmmu_driver);
}
subsys_initcall(ipmmu_init);

/*
 * Universal Flash Storage Host controller Platform bus based glue driver
 *
 * This code is based on drivers/scsi/ufs/ufshcd-pltfrm.c
 * Copyright (C) 2011-2013 Samsung India Software Operations
 *
 * Authors:
 *	Santosh Yaraganavi <santosh.sy@samsung.com>
 *	Vinayak Holikatti <h.vinayak@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * See the COPYING file in the top-level directory or visit
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This program is provided "AS IS" and "WITH ALL FAULTS" and
 * without warranty of any kind. You are solely responsible for
 * determining the appropriateness of using and distributing
 * the program and assume all risks associated with your exercise
 * of rights with respect to the program, including but not limited
 * to infringement of third party rights, the risks and costs of
 * program errors, damage to or loss of data, programs or equipment,
 * and unavailability or interruption of operations. Under no
 * circumstances will the contributor of this Program be liable for
 * any damages of any kind arising from your use or distribution of
 * this program.
 */

#include "ufshcd.h"
#include <linux/platform_device.h>

#ifdef CONFIG_PM
/**
 * ufshcd_pltfrm_suspend - suspend power management function
 * @dev: pointer to device handle
 *
 *
 * Returns 0
 */
static int ufshcd_pltfrm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	/*
	 * TODO:
	 * 1. Call ufshcd_suspend
	 * 2. Do bus specific power management
	 */

	disable_irq(hba->irq);

	return 0;
}

/**
 * ufshcd_pltfrm_resume - resume power management function
 * @dev: pointer to device handle
 *
 * Returns 0
 */
static int ufshcd_pltfrm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	/*
	 * TODO:
	 * 1. Call ufshcd_resume.
	 * 2. Do bus specific wake up
	 */

	enable_irq(hba->irq);

	return 0;
}
#else
#define ufshcd_pltfrm_suspend	NULL
#define ufshcd_pltfrm_resume	NULL
#endif

/**
 * ufshcd_pltfrm_probe - probe routine of the driver
 * @pdev: pointer to Platform device handle
 *
 * Returns 0 on success, non-zero value on failure
 */
static int ufshcd_pltfrm_probe(struct platform_device *pdev)
{
	struct ufs_hba *hba;
	void __iomem *mmio_base;
	struct resource *mem_res;
	struct resource *irq_res;
	resource_size_t mem_size;
	int err;
	struct device *dev = &pdev->dev;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev,
			"Memory resource not available\n");
		err = -ENODEV;
		goto out_error;
	}

	mem_size = resource_size(mem_res);
	if (!request_mem_region(mem_res->start, mem_size, "ufshcd")) {
		dev_err(&pdev->dev,
			"Cannot reserve the memory resource\n");
		err = -EBUSY;
		goto out_error;
	}

	mmio_base = ioremap_nocache(mem_res->start, mem_size);
	if (!mmio_base) {
		dev_err(&pdev->dev, "memory map failed\n");
		err = -ENOMEM;
		goto out_release_regions;
	}

	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq_res) {
		dev_err(&pdev->dev, "IRQ resource not available\n");
		err = -ENODEV;
		goto out_iounmap;
	}

	err = dma_set_coherent_mask(dev, dev->coherent_dma_mask);
	if (err) {
		dev_err(&pdev->dev, "set dma mask failed\n");
		goto out_iounmap;
	}

	err = ufshcd_init(&pdev->dev, &hba, mmio_base, irq_res->start);
	if (err) {
		dev_err(&pdev->dev, "Intialization failed\n");
		goto out_iounmap;
	}

	platform_set_drvdata(pdev, hba);

	return 0;

out_iounmap:
	iounmap(mmio_base);
out_release_regions:
	release_mem_region(mem_res->start, mem_size);
out_error:
	return err;
}

/**
 * ufshcd_pltfrm_remove - remove platform driver routine
 * @pdev: pointer to platform device handle
 *
 * Returns 0 on success, non-zero value on failure
 */
static int ufshcd_pltfrm_remove(struct platform_device *pdev)
{
	struct resource *mem_res;
	resource_size_t mem_size;
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	disable_irq(hba->irq);

	/* Some buggy controllers raise interrupt after
	 * the resources are removed. So first we unregister the
	 * irq handler and then the resources used by driver
	 */

	free_irq(hba->irq, hba);
	ufshcd_remove(hba);
	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res)
		dev_err(&pdev->dev, "ufshcd: Memory resource not available\n");
	else {
		mem_size = resource_size(mem_res);
		release_mem_region(mem_res->start, mem_size);
	}
	return 0;
}

static const struct of_device_id ufs_of_match[] = {
	{ .compatible = "jedec,ufs-1.1"},
};

static const struct dev_pm_ops ufshcd_dev_pm_ops = {
	.suspend	= ufshcd_pltfrm_suspend,
	.resume		= ufshcd_pltfrm_resume,
};

static struct platform_driver ufshcd_pltfrm_driver = {
	.probe	= ufshcd_pltfrm_probe,
	.remove	= ufshcd_pltfrm_remove,
	.driver	= {
		.name	= "ufshcd",
		.owner	= THIS_MODULE,
		.pm	= &ufshcd_dev_pm_ops,
		.of_match_table = ufs_of_match,
	},
};

module_platform_driver(ufshcd_pltfrm_driver);

MODULE_AUTHOR("Santosh Yaragnavi <santosh.sy@samsung.com>");
MODULE_AUTHOR("Vinayak Holikatti <h.vinayak@samsung.com>");
MODULE_DESCRIPTION("UFS host controller Pltform bus based glue driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(UFSHCD_DRIVER_VERSION);

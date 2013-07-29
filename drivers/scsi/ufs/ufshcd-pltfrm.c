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

#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "ufshcd.h"

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

#ifdef CONFIG_PM_RUNTIME
static int ufshcd_pltfrm_runtime_suspend(struct device *dev)
{
	struct ufs_hba *hba =  dev_get_drvdata(dev);

	if (!hba)
		return 0;

	return ufshcd_runtime_suspend(hba);
}
static int ufshcd_pltfrm_runtime_resume(struct device *dev)
{
	struct ufs_hba *hba =  dev_get_drvdata(dev);

	if (!hba)
		return 0;

	return ufshcd_runtime_resume(hba);
}
static int ufshcd_pltfrm_runtime_idle(struct device *dev)
{
	struct ufs_hba *hba =  dev_get_drvdata(dev);

	if (!hba)
		return 0;

	return ufshcd_runtime_idle(hba);
}
#else /* !CONFIG_PM_RUNTIME */
#define ufshcd_pltfrm_runtime_suspend	NULL
#define ufshcd_pltfrm_runtime_resume	NULL
#define ufshcd_pltfrm_runtime_idle	NULL
#endif /* CONFIG_PM_RUNTIME */

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
	int irq, err;
	struct device *dev = &pdev->dev;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(dev, "Memory resource not available\n");
		err = -ENODEV;
		goto out;
	}

	mmio_base = devm_ioremap_resource(dev, mem_res);
	if (IS_ERR(mmio_base)) {
		dev_err(dev, "memory map failed\n");
		err = PTR_ERR(mmio_base);
		goto out;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "IRQ resource not available\n");
		err = -ENODEV;
		goto out;
	}

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	err = ufshcd_init(dev, &hba, mmio_base, irq);
	if (err) {
		dev_err(dev, "Intialization failed\n");
		goto out_disable_rpm;
	}

	platform_set_drvdata(pdev, hba);

	return 0;

out_disable_rpm:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
out:
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
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	pm_runtime_get_sync(&(pdev)->dev);

	disable_irq(hba->irq);
	ufshcd_remove(hba);
	return 0;
}

static const struct of_device_id ufs_of_match[] = {
	{ .compatible = "jedec,ufs-1.1"},
	{},
};

static const struct dev_pm_ops ufshcd_dev_pm_ops = {
	.suspend	= ufshcd_pltfrm_suspend,
	.resume		= ufshcd_pltfrm_resume,
	.runtime_suspend = ufshcd_pltfrm_runtime_suspend,
	.runtime_resume  = ufshcd_pltfrm_runtime_resume,
	.runtime_idle    = ufshcd_pltfrm_runtime_idle,
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

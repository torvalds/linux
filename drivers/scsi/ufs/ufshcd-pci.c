/*
 * Universal Flash Storage Host controller PCI glue driver
 *
 * This code is based on drivers/scsi/ufs/ufshcd-pci.c
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
#include <linux/pci.h>
#include <linux/pm_runtime.h>

#ifdef CONFIG_PM
/**
 * ufshcd_pci_suspend - suspend power management function
 * @pdev: pointer to PCI device handle
 * @state: power state
 *
 * Returns -ENOSYS
 */
static int ufshcd_pci_suspend(struct device *dev)
{
	/*
	 * TODO:
	 * 1. Call ufshcd_suspend
	 * 2. Do bus specific power management
	 */

	return -ENOSYS;
}

/**
 * ufshcd_pci_resume - resume power management function
 * @pdev: pointer to PCI device handle
 *
 * Returns -ENOSYS
 */
static int ufshcd_pci_resume(struct device *dev)
{
	/*
	 * TODO:
	 * 1. Call ufshcd_resume.
	 * 2. Do bus specific wake up
	 */

	return -ENOSYS;
}
#else
#define ufshcd_pci_suspend	NULL
#define ufshcd_pci_resume	NULL
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_RUNTIME
static int ufshcd_pci_runtime_suspend(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);

	if (!hba)
		return 0;

	return ufshcd_runtime_suspend(hba);
}
static int ufshcd_pci_runtime_resume(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);

	if (!hba)
		return 0;

	return ufshcd_runtime_resume(hba);
}
static int ufshcd_pci_runtime_idle(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);

	if (!hba)
		return 0;

	return ufshcd_runtime_idle(hba);
}
#else /* !CONFIG_PM_RUNTIME */
#define ufshcd_pci_runtime_suspend	NULL
#define ufshcd_pci_runtime_resume	NULL
#define ufshcd_pci_runtime_idle	NULL
#endif /* CONFIG_PM_RUNTIME */

/**
 * ufshcd_pci_shutdown - main function to put the controller in reset state
 * @pdev: pointer to PCI device handle
 */
static void ufshcd_pci_shutdown(struct pci_dev *pdev)
{
	ufshcd_hba_stop((struct ufs_hba *)pci_get_drvdata(pdev));
}

/**
 * ufshcd_pci_remove - de-allocate PCI/SCSI host and host memory space
 *		data structure memory
 * @pdev - pointer to PCI handle
 */
static void ufshcd_pci_remove(struct pci_dev *pdev)
{
	struct ufs_hba *hba = pci_get_drvdata(pdev);

	pm_runtime_forbid(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);

	disable_irq(pdev->irq);
	ufshcd_remove(hba);
	pci_release_regions(pdev);
	pci_set_drvdata(pdev, NULL);
	pci_clear_master(pdev);
	pci_disable_device(pdev);
}

/**
 * ufshcd_set_dma_mask - Set dma mask based on the controller
 *			 addressing capability
 * @pdev: PCI device structure
 *
 * Returns 0 for success, non-zero for failure
 */
static int ufshcd_set_dma_mask(struct pci_dev *pdev)
{
	int err;

	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(64))
		&& !pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64)))
		return 0;
	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (!err)
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	return err;
}

/**
 * ufshcd_pci_probe - probe routine of the driver
 * @pdev: pointer to PCI device handle
 * @id: PCI device id
 *
 * Returns 0 on success, non-zero value on failure
 */
static int
ufshcd_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ufs_hba *hba;
	void __iomem *mmio_base;
	int err;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device failed\n");
		goto out_error;
	}

	pci_set_master(pdev);


	err = pci_request_regions(pdev, UFSHCD);
	if (err < 0) {
		dev_err(&pdev->dev, "request regions failed\n");
		goto out_disable;
	}

	mmio_base = pci_ioremap_bar(pdev, 0);
	if (!mmio_base) {
		dev_err(&pdev->dev, "memory map failed\n");
		err = -ENOMEM;
		goto out_release_regions;
	}

	err = ufshcd_set_dma_mask(pdev);
	if (err) {
		dev_err(&pdev->dev, "set dma mask failed\n");
		goto out_iounmap;
	}

	err = ufshcd_init(&pdev->dev, &hba, mmio_base, pdev->irq);
	if (err) {
		dev_err(&pdev->dev, "Initialization failed\n");
		goto out_iounmap;
	}

	pci_set_drvdata(pdev, hba);
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_allow(&pdev->dev);

	return 0;

out_iounmap:
	iounmap(mmio_base);
out_release_regions:
	pci_release_regions(pdev);
out_disable:
	pci_clear_master(pdev);
	pci_disable_device(pdev);
out_error:
	return err;
}

static const struct dev_pm_ops ufshcd_pci_pm_ops = {
	.suspend	= ufshcd_pci_suspend,
	.resume		= ufshcd_pci_resume,
	.runtime_suspend = ufshcd_pci_runtime_suspend,
	.runtime_resume  = ufshcd_pci_runtime_resume,
	.runtime_idle    = ufshcd_pci_runtime_idle,
};

static DEFINE_PCI_DEVICE_TABLE(ufshcd_pci_tbl) = {
	{ PCI_VENDOR_ID_SAMSUNG, 0xC00C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ }	/* terminate list */
};

MODULE_DEVICE_TABLE(pci, ufshcd_pci_tbl);

static struct pci_driver ufshcd_pci_driver = {
	.name = UFSHCD,
	.id_table = ufshcd_pci_tbl,
	.probe = ufshcd_pci_probe,
	.remove = ufshcd_pci_remove,
	.shutdown = ufshcd_pci_shutdown,
	.driver = {
		.pm = &ufshcd_pci_pm_ops
	},
};

module_pci_driver(ufshcd_pci_driver);

MODULE_AUTHOR("Santosh Yaragnavi <santosh.sy@samsung.com>");
MODULE_AUTHOR("Vinayak Holikatti <h.vinayak@samsung.com>");
MODULE_DESCRIPTION("UFS host controller PCI glue driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(UFSHCD_DRIVER_VERSION);

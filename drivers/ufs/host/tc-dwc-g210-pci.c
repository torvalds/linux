// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synopsys G210 Test Chip driver
 *
 * Copyright (C) 2015-2016 Synopsys, Inc. (www.synopsys.com)
 *
 * Authors: Joao Pinto <jpinto@synopsys.com>
 */

#include <ufs/ufshcd.h>
#include "ufshcd-dwc.h"
#include "tc-dwc-g210.h"

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>

/* Test Chip type expected values */
#define TC_G210_20BIT 20
#define TC_G210_40BIT 40
#define TC_G210_INV 0

static int tc_type = TC_G210_INV;
module_param(tc_type, int, 0);
MODULE_PARM_DESC(tc_type, "Test Chip Type (20 = 20-bit, 40 = 40-bit)");

/*
 * struct ufs_hba_dwc_vops - UFS DWC specific variant operations
 */
static struct ufs_hba_variant_ops tc_dwc_g210_pci_hba_vops = {
	.name                   = "tc-dwc-g210-pci",
	.link_startup_notify	= ufshcd_dwc_link_startup_notify,
};

/**
 * tc_dwc_g210_pci_remove - de-allocate PCI/SCSI host and host memory space
 *		data structure memory
 * @pdev: pointer to PCI handle
 */
static void tc_dwc_g210_pci_remove(struct pci_dev *pdev)
{
	struct ufs_hba *hba = pci_get_drvdata(pdev);

	pm_runtime_forbid(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);
	ufshcd_remove(hba);
}

/**
 * tc_dwc_g210_pci_probe - probe routine of the driver
 * @pdev: pointer to PCI device handle
 * @id: PCI device id
 *
 * Returns 0 on success, non-zero value on failure
 */
static int
tc_dwc_g210_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ufs_hba *hba;
	void __iomem *mmio_base;
	int err;

	/* Check Test Chip type and set the specific setup routine */
	if (tc_type == TC_G210_20BIT) {
		tc_dwc_g210_pci_hba_vops.phy_initialization =
						tc_dwc_g210_config_20_bit;
	} else if (tc_type == TC_G210_40BIT) {
		tc_dwc_g210_pci_hba_vops.phy_initialization =
						tc_dwc_g210_config_40_bit;
	} else {
		dev_err(&pdev->dev, "test chip version not specified\n");
		return -EPERM;
	}

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "pcim_enable_device failed\n");
		return err;
	}

	pci_set_master(pdev);

	err = pcim_iomap_regions(pdev, 1 << 0, UFSHCD);
	if (err < 0) {
		dev_err(&pdev->dev, "request and iomap failed\n");
		return err;
	}

	mmio_base = pcim_iomap_table(pdev)[0];

	err = ufshcd_alloc_host(&pdev->dev, &hba);
	if (err) {
		dev_err(&pdev->dev, "Allocation failed\n");
		return err;
	}

	hba->vops = &tc_dwc_g210_pci_hba_vops;

	err = ufshcd_init(hba, mmio_base, pdev->irq);
	if (err) {
		dev_err(&pdev->dev, "Initialization failed\n");
		return err;
	}

	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_allow(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops tc_dwc_g210_pci_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ufshcd_system_suspend, ufshcd_system_resume)
	SET_RUNTIME_PM_OPS(ufshcd_runtime_suspend, ufshcd_runtime_resume, NULL)
	.prepare	 = ufshcd_suspend_prepare,
	.complete	 = ufshcd_resume_complete,
};

static const struct pci_device_id tc_dwc_g210_pci_tbl[] = {
	{ PCI_VENDOR_ID_SYNOPSYS, 0xB101, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_SYNOPSYS, 0xB102, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ }	/* terminate list */
};

MODULE_DEVICE_TABLE(pci, tc_dwc_g210_pci_tbl);

static struct pci_driver tc_dwc_g210_pci_driver = {
	.name = "tc-dwc-g210-pci",
	.id_table = tc_dwc_g210_pci_tbl,
	.probe = tc_dwc_g210_pci_probe,
	.remove = tc_dwc_g210_pci_remove,
	.driver = {
		.pm = &tc_dwc_g210_pci_pm_ops
	},
};

module_pci_driver(tc_dwc_g210_pci_driver);

MODULE_AUTHOR("Joao Pinto <Joao.Pinto@synopsys.com>");
MODULE_DESCRIPTION("Synopsys Test Chip G210 PCI glue driver");
MODULE_LICENSE("Dual BSD/GPL");

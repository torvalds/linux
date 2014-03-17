/*
 * Copyright (c) 2012 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/pci.h>
#include <linux/moduleparam.h>

#include "wil6210.h"

static int use_msi = 1;
module_param(use_msi, int, S_IRUGO);
MODULE_PARM_DESC(use_msi,
		 " Use MSI interrupt: "
		 "0 - don't, 1 - (default) - single, or 3");

/* Bus ops */
static int wil_if_pcie_enable(struct wil6210_priv *wil)
{
	struct pci_dev *pdev = wil->pdev;
	int rc;

	pci_set_master(pdev);

	/*
	 * how many MSI interrupts to request?
	 */
	switch (use_msi) {
	case 3:
	case 1:
	case 0:
		break;
	default:
		wil_err(wil, "Invalid use_msi=%d, default to 1\n",
			use_msi);
		use_msi = 1;
	}
	wil->n_msi = use_msi;
	if (wil->n_msi) {
		wil_dbg_misc(wil, "Setup %d MSI interrupts\n", use_msi);
		rc = pci_enable_msi_block(pdev, wil->n_msi);
		if (rc && (wil->n_msi == 3)) {
			wil_err(wil, "3 MSI mode failed, try 1 MSI\n");
			wil->n_msi = 1;
			rc = pci_enable_msi_block(pdev, wil->n_msi);
		}
		if (rc) {
			wil_err(wil, "pci_enable_msi failed, use INTx\n");
			wil->n_msi = 0;
		}
	} else {
		wil_dbg_misc(wil, "MSI interrupts disabled, use INTx\n");
	}

	rc = wil6210_init_irq(wil, pdev->irq);
	if (rc)
		goto stop_master;

	/* need reset here to obtain MAC */
	rc = wil_reset(wil);
	if (rc)
		goto release_irq;

	wil_info(wil, "HW version: 0x%08x\n", wil->hw_version);

	return 0;

 release_irq:
	wil6210_fini_irq(wil, pdev->irq);
	/* safe to call if no MSI */
	pci_disable_msi(pdev);
 stop_master:
	pci_clear_master(pdev);
	return rc;
}

static int wil_if_pcie_disable(struct wil6210_priv *wil)
{
	struct pci_dev *pdev = wil->pdev;

	pci_clear_master(pdev);
	/* disable and release IRQ */
	wil6210_fini_irq(wil, pdev->irq);
	/* safe to call if no MSI */
	pci_disable_msi(pdev);
	/* TODO: disable HW */

	return 0;
}

static int wil_pcie_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct wil6210_priv *wil;
	struct device *dev = &pdev->dev;
	void __iomem *csr;
	int rc;

	/* check HW */
	dev_info(&pdev->dev, WIL_NAME " device found [%04x:%04x] (rev %x)\n",
		 (int)pdev->vendor, (int)pdev->device, (int)pdev->revision);

	if (pci_resource_len(pdev, 0) != WIL6210_MEM_SIZE) {
		dev_err(&pdev->dev, "Not " WIL_NAME "? "
			"BAR0 size is %lu while expecting %lu\n",
			(ulong)pci_resource_len(pdev, 0), WIL6210_MEM_SIZE);
		return -ENODEV;
	}

	rc = pci_enable_device(pdev);
	if (rc) {
		dev_err(&pdev->dev, "pci_enable_device failed\n");
		return -ENODEV;
	}
	/* rollback to err_disable_pdev */

	rc = pci_request_region(pdev, 0, WIL_NAME);
	if (rc) {
		dev_err(&pdev->dev, "pci_request_region failed\n");
		goto err_disable_pdev;
	}
	/* rollback to err_release_reg */

	csr = pci_ioremap_bar(pdev, 0);
	if (!csr) {
		dev_err(&pdev->dev, "pci_ioremap_bar failed\n");
		rc = -ENODEV;
		goto err_release_reg;
	}
	/* rollback to err_iounmap */
	dev_info(&pdev->dev, "CSR at %pR -> %p\n", &pdev->resource[0], csr);

	wil = wil_if_alloc(dev, csr);
	if (IS_ERR(wil)) {
		rc = (int)PTR_ERR(wil);
		dev_err(dev, "wil_if_alloc failed: %d\n", rc);
		goto err_iounmap;
	}
	/* rollback to if_free */

	pci_set_drvdata(pdev, wil);
	wil->pdev = pdev;

	wil6210_clear_irq(wil);
	/* FW should raise IRQ when ready */
	rc = wil_if_pcie_enable(wil);
	if (rc) {
		wil_err(wil, "Enable device failed\n");
		goto if_free;
	}
	/* rollback to bus_disable */

	rc = wil_if_add(wil);
	if (rc) {
		wil_err(wil, "wil_if_add failed: %d\n", rc);
		goto bus_disable;
	}

	wil6210_debugfs_init(wil);

	/* check FW is alive */
	wmi_echo(wil);

	return 0;

 bus_disable:
	wil_if_pcie_disable(wil);
 if_free:
	wil_if_free(wil);
 err_iounmap:
	pci_iounmap(pdev, csr);
 err_release_reg:
	pci_release_region(pdev, 0);
 err_disable_pdev:
	pci_disable_device(pdev);

	return rc;
}

static void wil_pcie_remove(struct pci_dev *pdev)
{
	struct wil6210_priv *wil = pci_get_drvdata(pdev);

	wil6210_debugfs_remove(wil);
	wil_if_pcie_disable(wil);
	wil_if_remove(wil);
	wil_if_free(wil);
	pci_iounmap(pdev, wil->csr);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
}

static DEFINE_PCI_DEVICE_TABLE(wil6210_pcie_ids) = {
	{ PCI_DEVICE(0x1ae9, 0x0301) },
	{ /* end: all zeroes */	},
};
MODULE_DEVICE_TABLE(pci, wil6210_pcie_ids);

static struct pci_driver wil6210_driver = {
	.probe		= wil_pcie_probe,
	.remove		= wil_pcie_remove,
	.id_table	= wil6210_pcie_ids,
	.name		= WIL_NAME,
};

module_pci_driver(wil6210_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Qualcomm Atheros <wil6210@qca.qualcomm.com>");
MODULE_DESCRIPTION("Driver for 60g WiFi WIL6210 card");

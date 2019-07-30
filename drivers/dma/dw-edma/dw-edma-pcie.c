// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare eDMA PCIe driver
 *
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/dma/edma.h>
#include <linux/pci-epf.h>
#include <linux/msi.h>

#include "dw-edma-core.h"

struct dw_edma_pcie_data {
	/* eDMA registers location */
	enum pci_barno			rg_bar;
	off_t				rg_off;
	size_t				rg_sz;
	/* eDMA memory linked list location */
	enum pci_barno			ll_bar;
	off_t				ll_off;
	size_t				ll_sz;
	/* eDMA memory data location */
	enum pci_barno			dt_bar;
	off_t				dt_off;
	size_t				dt_sz;
	/* Other */
	u32				version;
	enum dw_edma_mode		mode;
	u8				irqs;
};

static const struct dw_edma_pcie_data snps_edda_data = {
	/* eDMA registers location */
	.rg_bar				= BAR_0,
	.rg_off				= 0x00001000,	/*  4 Kbytes */
	.rg_sz				= 0x00002000,	/*  8 Kbytes */
	/* eDMA memory linked list location */
	.ll_bar				= BAR_2,
	.ll_off				= 0x00000000,	/*  0 Kbytes */
	.ll_sz				= 0x00800000,	/*  8 Mbytes */
	/* eDMA memory data location */
	.dt_bar				= BAR_2,
	.dt_off				= 0x00800000,	/*  8 Mbytes */
	.dt_sz				= 0x03800000,	/* 56 Mbytes */
	/* Other */
	.version			= 0,
	.mode				= EDMA_MODE_UNROLL,
	.irqs				= 1,
};

static int dw_edma_pcie_probe(struct pci_dev *pdev,
			      const struct pci_device_id *pid)
{
	const struct dw_edma_pcie_data *pdata = (void *)pid->driver_data;
	struct device *dev = &pdev->dev;
	struct dw_edma_chip *chip;
	int err, nr_irqs;
	struct dw_edma *dw;

	/* Enable PCI device */
	err = pcim_enable_device(pdev);
	if (err) {
		pci_err(pdev, "enabling device failed\n");
		return err;
	}

	/* Mapping PCI BAR regions */
	err = pcim_iomap_regions(pdev, BIT(pdata->rg_bar) |
				       BIT(pdata->ll_bar) |
				       BIT(pdata->dt_bar),
				 pci_name(pdev));
	if (err) {
		pci_err(pdev, "eDMA BAR I/O remapping failed\n");
		return err;
	}

	pci_set_master(pdev);

	/* DMA configuration */
	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (!err) {
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
		if (err) {
			pci_err(pdev, "consistent DMA mask 64 set failed\n");
			return err;
		}
	} else {
		pci_err(pdev, "DMA mask 64 set failed\n");

		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			pci_err(pdev, "DMA mask 32 set failed\n");
			return err;
		}

		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			pci_err(pdev, "consistent DMA mask 32 set failed\n");
			return err;
		}
	}

	/* Data structure allocation */
	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	dw = devm_kzalloc(dev, sizeof(*dw), GFP_KERNEL);
	if (!dw)
		return -ENOMEM;

	/* IRQs allocation */
	nr_irqs = pci_alloc_irq_vectors(pdev, 1, pdata->irqs,
					PCI_IRQ_MSI | PCI_IRQ_MSIX);
	if (nr_irqs < 1) {
		pci_err(pdev, "fail to alloc IRQ vector (number of IRQs=%u)\n",
			nr_irqs);
		return -EPERM;
	}

	/* Data structure initialization */
	chip->dw = dw;
	chip->dev = dev;
	chip->id = pdev->devfn;
	chip->irq = pdev->irq;

	dw->rg_region.vaddr = (dma_addr_t)pcim_iomap_table(pdev)[pdata->rg_bar];
	dw->rg_region.vaddr += pdata->rg_off;
	dw->rg_region.paddr = pdev->resource[pdata->rg_bar].start;
	dw->rg_region.paddr += pdata->rg_off;
	dw->rg_region.sz = pdata->rg_sz;

	dw->ll_region.vaddr = (dma_addr_t)pcim_iomap_table(pdev)[pdata->ll_bar];
	dw->ll_region.vaddr += pdata->ll_off;
	dw->ll_region.paddr = pdev->resource[pdata->ll_bar].start;
	dw->ll_region.paddr += pdata->ll_off;
	dw->ll_region.sz = pdata->ll_sz;

	dw->dt_region.vaddr = (dma_addr_t)pcim_iomap_table(pdev)[pdata->dt_bar];
	dw->dt_region.vaddr += pdata->dt_off;
	dw->dt_region.paddr = pdev->resource[pdata->dt_bar].start;
	dw->dt_region.paddr += pdata->dt_off;
	dw->dt_region.sz = pdata->dt_sz;

	dw->version = pdata->version;
	dw->mode = pdata->mode;
	dw->nr_irqs = nr_irqs;

	/* Debug info */
	pci_dbg(pdev, "Version:\t%u\n", dw->version);

	pci_dbg(pdev, "Mode:\t%s\n",
		dw->mode == EDMA_MODE_LEGACY ? "Legacy" : "Unroll");

	pci_dbg(pdev, "Registers:\tBAR=%u, off=0x%.8lx, sz=0x%zx bytes, addr(v=%pa, p=%pa)\n",
		pdata->rg_bar, pdata->rg_off, pdata->rg_sz,
		&dw->rg_region.vaddr, &dw->rg_region.paddr);

	pci_dbg(pdev, "L. List:\tBAR=%u, off=0x%.8lx, sz=0x%zx bytes, addr(v=%pa, p=%pa)\n",
		pdata->ll_bar, pdata->ll_off, pdata->ll_sz,
		&dw->ll_region.vaddr, &dw->ll_region.paddr);

	pci_dbg(pdev, "Data:\tBAR=%u, off=0x%.8lx, sz=0x%zx bytes, addr(v=%pa, p=%pa)\n",
		pdata->dt_bar, pdata->dt_off, pdata->dt_sz,
		&dw->dt_region.vaddr, &dw->dt_region.paddr);

	pci_dbg(pdev, "Nr. IRQs:\t%u\n", dw->nr_irqs);

	/* Validating if PCI interrupts were enabled */
	if (!pci_dev_msi_enabled(pdev)) {
		pci_err(pdev, "enable interrupt failed\n");
		return -EPERM;
	}

	dw->irq = devm_kcalloc(dev, nr_irqs, sizeof(*dw->irq), GFP_KERNEL);
	if (!dw->irq)
		return -ENOMEM;

	/* Starting eDMA driver */
	err = dw_edma_probe(chip);
	if (err) {
		pci_err(pdev, "eDMA probe failed\n");
		return err;
	}

	/* Saving data structure reference */
	pci_set_drvdata(pdev, chip);

	return 0;
}

static void dw_edma_pcie_remove(struct pci_dev *pdev)
{
	struct dw_edma_chip *chip = pci_get_drvdata(pdev);
	int err;

	/* Stopping eDMA driver */
	err = dw_edma_remove(chip);
	if (err)
		pci_warn(pdev, "can't remove device properly: %d\n", err);

	/* Freeing IRQs */
	pci_free_irq_vectors(pdev);
}

static const struct pci_device_id dw_edma_pcie_id_table[] = {
	{ PCI_DEVICE_DATA(SYNOPSYS, EDDA, &snps_edda_data) },
	{ }
};
MODULE_DEVICE_TABLE(pci, dw_edma_pcie_id_table);

static struct pci_driver dw_edma_pcie_driver = {
	.name		= "dw-edma-pcie",
	.id_table	= dw_edma_pcie_id_table,
	.probe		= dw_edma_pcie_probe,
	.remove		= dw_edma_pcie_remove,
};

module_pci_driver(dw_edma_pcie_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Synopsys DesignWare eDMA PCIe driver");
MODULE_AUTHOR("Gustavo Pimentel <gustavo.pimentel@synopsys.com>");

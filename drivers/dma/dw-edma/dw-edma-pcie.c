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
#include <linux/bitfield.h>

#include "dw-edma-core.h"

#define DW_PCIE_VSEC_DMA_ID			0x6
#define DW_PCIE_VSEC_DMA_BAR			GENMASK(10, 8)
#define DW_PCIE_VSEC_DMA_MAP			GENMASK(2, 0)
#define DW_PCIE_VSEC_DMA_WR_CH			GENMASK(9, 0)
#define DW_PCIE_VSEC_DMA_RD_CH			GENMASK(25, 16)

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
	enum dw_edma_map_format		mf;
	u8				irqs;
	u16				wr_ch_cnt;
	u16				rd_ch_cnt;
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
	.mf				= EDMA_MF_EDMA_UNROLL,
	.irqs				= 1,
	.wr_ch_cnt			= 0,
	.rd_ch_cnt			= 0,
};

static int dw_edma_pcie_irq_vector(struct device *dev, unsigned int nr)
{
	return pci_irq_vector(to_pci_dev(dev), nr);
}

static const struct dw_edma_core_ops dw_edma_pcie_core_ops = {
	.irq_vector = dw_edma_pcie_irq_vector,
};

static void dw_edma_pcie_get_vsec_dma_data(struct pci_dev *pdev,
					   struct dw_edma_pcie_data *pdata)
{
	u32 val, map;
	u16 vsec;
	u64 off;

	vsec = pci_find_vsec_capability(pdev, PCI_VENDOR_ID_SYNOPSYS,
					DW_PCIE_VSEC_DMA_ID);
	if (!vsec)
		return;

	pci_read_config_dword(pdev, vsec + PCI_VNDR_HEADER, &val);
	if (PCI_VNDR_HEADER_REV(val) != 0x00 ||
	    PCI_VNDR_HEADER_LEN(val) != 0x18)
		return;

	pci_dbg(pdev, "Detected PCIe Vendor-Specific Extended Capability DMA\n");
	pci_read_config_dword(pdev, vsec + 0x8, &val);
	map = FIELD_GET(DW_PCIE_VSEC_DMA_MAP, val);
	if (map != EDMA_MF_EDMA_LEGACY &&
	    map != EDMA_MF_EDMA_UNROLL &&
	    map != EDMA_MF_HDMA_COMPAT)
		return;

	pdata->mf = map;
	pdata->rg_bar = FIELD_GET(DW_PCIE_VSEC_DMA_BAR, val);

	pci_read_config_dword(pdev, vsec + 0xc, &val);
	pdata->wr_ch_cnt = FIELD_GET(DW_PCIE_VSEC_DMA_WR_CH, val);
	pdata->rd_ch_cnt = FIELD_GET(DW_PCIE_VSEC_DMA_RD_CH, val);

	pci_read_config_dword(pdev, vsec + 0x14, &val);
	off = val;
	pci_read_config_dword(pdev, vsec + 0x10, &val);
	off <<= 32;
	off |= val;
	pdata->rg_off = off;
}

static int dw_edma_pcie_probe(struct pci_dev *pdev,
			      const struct pci_device_id *pid)
{
	struct dw_edma_pcie_data *pdata = (void *)pid->driver_data;
	struct dw_edma_pcie_data vsec_data;
	struct device *dev = &pdev->dev;
	struct dw_edma_chip *chip;
	struct dw_edma *dw;
	int err, nr_irqs;

	/* Enable PCI device */
	err = pcim_enable_device(pdev);
	if (err) {
		pci_err(pdev, "enabling device failed\n");
		return err;
	}

	memcpy(&vsec_data, pdata, sizeof(struct dw_edma_pcie_data));

	/*
	 * Tries to find if exists a PCIe Vendor-Specific Extended Capability
	 * for the DMA, if one exists, then reconfigures it.
	 */
	dw_edma_pcie_get_vsec_dma_data(pdev, &vsec_data);

	/* Mapping PCI BAR regions */
	err = pcim_iomap_regions(pdev, BIT(vsec_data.rg_bar) |
				       BIT(vsec_data.ll_bar) |
				       BIT(vsec_data.dt_bar),
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
	nr_irqs = pci_alloc_irq_vectors(pdev, 1, vsec_data.irqs,
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

	dw->rg_region.vaddr = pcim_iomap_table(pdev)[vsec_data.rg_bar];
	dw->rg_region.vaddr += vsec_data.rg_off;
	dw->rg_region.paddr = pdev->resource[vsec_data.rg_bar].start;
	dw->rg_region.paddr += vsec_data.rg_off;
	dw->rg_region.sz = vsec_data.rg_sz;

	dw->ll_region.vaddr = pcim_iomap_table(pdev)[vsec_data.ll_bar];
	dw->ll_region.vaddr += vsec_data.ll_off;
	dw->ll_region.paddr = pdev->resource[vsec_data.ll_bar].start;
	dw->ll_region.paddr += vsec_data.ll_off;
	dw->ll_region.sz = vsec_data.ll_sz;

	dw->dt_region.vaddr = pcim_iomap_table(pdev)[vsec_data.dt_bar];
	dw->dt_region.vaddr += vsec_data.dt_off;
	dw->dt_region.paddr = pdev->resource[vsec_data.dt_bar].start;
	dw->dt_region.paddr += vsec_data.dt_off;
	dw->dt_region.sz = vsec_data.dt_sz;

	dw->mf = vsec_data.mf;
	dw->nr_irqs = nr_irqs;
	dw->ops = &dw_edma_pcie_core_ops;
	dw->wr_ch_cnt = vsec_data.wr_ch_cnt;
	dw->rd_ch_cnt = vsec_data.rd_ch_cnt;

	/* Debug info */
	if (dw->mf == EDMA_MF_EDMA_LEGACY)
		pci_dbg(pdev, "Version:\teDMA Port Logic (0x%x)\n", dw->mf);
	else if (dw->mf == EDMA_MF_EDMA_UNROLL)
		pci_dbg(pdev, "Version:\teDMA Unroll (0x%x)\n", dw->mf);
	else if (dw->mf == EDMA_MF_HDMA_COMPAT)
		pci_dbg(pdev, "Version:\tHDMA Compatible (0x%x)\n", dw->mf);
	else
		pci_dbg(pdev, "Version:\tUnknown (0x%x)\n", dw->mf);

	pci_dbg(pdev, "Registers:\tBAR=%u, off=0x%.8lx, sz=0x%zx bytes, addr(v=%p, p=%pa)\n",
		vsec_data.rg_bar, vsec_data.rg_off, vsec_data.rg_sz,
		dw->rg_region.vaddr, &dw->rg_region.paddr);

	pci_dbg(pdev, "L. List:\tBAR=%u, off=0x%.8lx, sz=0x%zx bytes, addr(v=%p, p=%pa)\n",
		vsec_data.ll_bar, vsec_data.ll_off, vsec_data.ll_sz,
		dw->ll_region.vaddr, &dw->ll_region.paddr);

	pci_dbg(pdev, "Data:\tBAR=%u, off=0x%.8lx, sz=0x%zx bytes, addr(v=%p, p=%pa)\n",
		vsec_data.dt_bar, vsec_data.dt_off, vsec_data.dt_sz,
		dw->dt_region.vaddr, &dw->dt_region.paddr);

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

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

#define DW_BLOCK(a, b, c) \
	{ \
		.bar = a, \
		.off = b, \
		.sz = c, \
	},

struct dw_edma_block {
	enum pci_barno			bar;
	off_t				off;
	size_t				sz;
};

struct dw_edma_pcie_data {
	/* eDMA registers location */
	struct dw_edma_block		rg;
	/* eDMA memory linked list location */
	struct dw_edma_block		ll_wr[EDMA_MAX_WR_CH];
	struct dw_edma_block		ll_rd[EDMA_MAX_RD_CH];
	/* eDMA memory data location */
	struct dw_edma_block		dt_wr[EDMA_MAX_WR_CH];
	struct dw_edma_block		dt_rd[EDMA_MAX_RD_CH];
	/* Other */
	enum dw_edma_map_format		mf;
	u8				irqs;
	u16				wr_ch_cnt;
	u16				rd_ch_cnt;
};

static const struct dw_edma_pcie_data snps_edda_data = {
	/* eDMA registers location */
	.rg.bar				= BAR_0,
	.rg.off				= 0x00001000,	/*  4 Kbytes */
	.rg.sz				= 0x00002000,	/*  8 Kbytes */
	/* eDMA memory linked list location */
	.ll_wr = {
		/* Channel 0 - BAR 2, offset 0 Mbytes, size 2 Kbytes */
		DW_BLOCK(BAR_2, 0x00000000, 0x00000800)
		/* Channel 1 - BAR 2, offset 2 Mbytes, size 2 Kbytes */
		DW_BLOCK(BAR_2, 0x00200000, 0x00000800)
	},
	.ll_rd = {
		/* Channel 0 - BAR 2, offset 4 Mbytes, size 2 Kbytes */
		DW_BLOCK(BAR_2, 0x00400000, 0x00000800)
		/* Channel 1 - BAR 2, offset 6 Mbytes, size 2 Kbytes */
		DW_BLOCK(BAR_2, 0x00600000, 0x00000800)
	},
	/* eDMA memory data location */
	.dt_wr = {
		/* Channel 0 - BAR 2, offset 8 Mbytes, size 2 Kbytes */
		DW_BLOCK(BAR_2, 0x00800000, 0x00000800)
		/* Channel 1 - BAR 2, offset 9 Mbytes, size 2 Kbytes */
		DW_BLOCK(BAR_2, 0x00900000, 0x00000800)
	},
	.dt_rd = {
		/* Channel 0 - BAR 2, offset 10 Mbytes, size 2 Kbytes */
		DW_BLOCK(BAR_2, 0x00a00000, 0x00000800)
		/* Channel 1 - BAR 2, offset 11 Mbytes, size 2 Kbytes */
		DW_BLOCK(BAR_2, 0x00b00000, 0x00000800)
	},
	/* Other */
	.mf				= EDMA_MF_EDMA_UNROLL,
	.irqs				= 1,
	.wr_ch_cnt			= 2,
	.rd_ch_cnt			= 2,
};

static int dw_edma_pcie_irq_vector(struct device *dev, unsigned int nr)
{
	return pci_irq_vector(to_pci_dev(dev), nr);
}

static u64 dw_edma_pcie_address(struct device *dev, phys_addr_t cpu_addr)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pci_bus_region region;
	struct resource res = {
		.flags = IORESOURCE_MEM,
		.start = cpu_addr,
		.end = cpu_addr,
	};

	pcibios_resource_to_bus(pdev->bus, &region, &res);
	return region.start;
}

static const struct dw_edma_plat_ops dw_edma_pcie_plat_ops = {
	.irq_vector = dw_edma_pcie_irq_vector,
	.pci_address = dw_edma_pcie_address,
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
	pdata->rg.bar = FIELD_GET(DW_PCIE_VSEC_DMA_BAR, val);

	pci_read_config_dword(pdev, vsec + 0xc, &val);
	pdata->wr_ch_cnt = min_t(u16, pdata->wr_ch_cnt,
				 FIELD_GET(DW_PCIE_VSEC_DMA_WR_CH, val));
	pdata->rd_ch_cnt = min_t(u16, pdata->rd_ch_cnt,
				 FIELD_GET(DW_PCIE_VSEC_DMA_RD_CH, val));

	pci_read_config_dword(pdev, vsec + 0x14, &val);
	off = val;
	pci_read_config_dword(pdev, vsec + 0x10, &val);
	off <<= 32;
	off |= val;
	pdata->rg.off = off;
}

static int dw_edma_pcie_probe(struct pci_dev *pdev,
			      const struct pci_device_id *pid)
{
	struct dw_edma_pcie_data *pdata = (void *)pid->driver_data;
	struct dw_edma_pcie_data vsec_data;
	struct device *dev = &pdev->dev;
	struct dw_edma_chip *chip;
	int err, nr_irqs;
	int i, mask;

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
	mask = BIT(vsec_data.rg.bar);
	for (i = 0; i < vsec_data.wr_ch_cnt; i++) {
		mask |= BIT(vsec_data.ll_wr[i].bar);
		mask |= BIT(vsec_data.dt_wr[i].bar);
	}
	for (i = 0; i < vsec_data.rd_ch_cnt; i++) {
		mask |= BIT(vsec_data.ll_rd[i].bar);
		mask |= BIT(vsec_data.dt_rd[i].bar);
	}
	err = pcim_iomap_regions(pdev, mask, pci_name(pdev));
	if (err) {
		pci_err(pdev, "eDMA BAR I/O remapping failed\n");
		return err;
	}

	pci_set_master(pdev);

	/* DMA configuration */
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		pci_err(pdev, "DMA mask 64 set failed\n");
		return err;
	}

	/* Data structure allocation */
	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
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
	chip->dev = dev;

	chip->mf = vsec_data.mf;
	chip->nr_irqs = nr_irqs;
	chip->ops = &dw_edma_pcie_plat_ops;

	chip->ll_wr_cnt = vsec_data.wr_ch_cnt;
	chip->ll_rd_cnt = vsec_data.rd_ch_cnt;

	chip->reg_base = pcim_iomap_table(pdev)[vsec_data.rg.bar];
	if (!chip->reg_base)
		return -ENOMEM;

	for (i = 0; i < chip->ll_wr_cnt; i++) {
		struct dw_edma_region *ll_region = &chip->ll_region_wr[i];
		struct dw_edma_region *dt_region = &chip->dt_region_wr[i];
		struct dw_edma_block *ll_block = &vsec_data.ll_wr[i];
		struct dw_edma_block *dt_block = &vsec_data.dt_wr[i];

		ll_region->vaddr.io = pcim_iomap_table(pdev)[ll_block->bar];
		if (!ll_region->vaddr.io)
			return -ENOMEM;

		ll_region->vaddr.io += ll_block->off;
		ll_region->paddr = pci_bus_address(pdev, ll_block->bar);
		ll_region->paddr += ll_block->off;
		ll_region->sz = ll_block->sz;

		dt_region->vaddr.io = pcim_iomap_table(pdev)[dt_block->bar];
		if (!dt_region->vaddr.io)
			return -ENOMEM;

		dt_region->vaddr.io += dt_block->off;
		dt_region->paddr = pci_bus_address(pdev, dt_block->bar);
		dt_region->paddr += dt_block->off;
		dt_region->sz = dt_block->sz;
	}

	for (i = 0; i < chip->ll_rd_cnt; i++) {
		struct dw_edma_region *ll_region = &chip->ll_region_rd[i];
		struct dw_edma_region *dt_region = &chip->dt_region_rd[i];
		struct dw_edma_block *ll_block = &vsec_data.ll_rd[i];
		struct dw_edma_block *dt_block = &vsec_data.dt_rd[i];

		ll_region->vaddr.io = pcim_iomap_table(pdev)[ll_block->bar];
		if (!ll_region->vaddr.io)
			return -ENOMEM;

		ll_region->vaddr.io += ll_block->off;
		ll_region->paddr = pci_bus_address(pdev, ll_block->bar);
		ll_region->paddr += ll_block->off;
		ll_region->sz = ll_block->sz;

		dt_region->vaddr.io = pcim_iomap_table(pdev)[dt_block->bar];
		if (!dt_region->vaddr.io)
			return -ENOMEM;

		dt_region->vaddr.io += dt_block->off;
		dt_region->paddr = pci_bus_address(pdev, dt_block->bar);
		dt_region->paddr += dt_block->off;
		dt_region->sz = dt_block->sz;
	}

	/* Debug info */
	if (chip->mf == EDMA_MF_EDMA_LEGACY)
		pci_dbg(pdev, "Version:\teDMA Port Logic (0x%x)\n", chip->mf);
	else if (chip->mf == EDMA_MF_EDMA_UNROLL)
		pci_dbg(pdev, "Version:\teDMA Unroll (0x%x)\n", chip->mf);
	else if (chip->mf == EDMA_MF_HDMA_COMPAT)
		pci_dbg(pdev, "Version:\tHDMA Compatible (0x%x)\n", chip->mf);
	else
		pci_dbg(pdev, "Version:\tUnknown (0x%x)\n", chip->mf);

	pci_dbg(pdev, "Registers:\tBAR=%u, off=0x%.8lx, sz=0x%zx bytes, addr(v=%p)\n",
		vsec_data.rg.bar, vsec_data.rg.off, vsec_data.rg.sz,
		chip->reg_base);


	for (i = 0; i < chip->ll_wr_cnt; i++) {
		pci_dbg(pdev, "L. List:\tWRITE CH%.2u, BAR=%u, off=0x%.8lx, sz=0x%zx bytes, addr(v=%p, p=%pa)\n",
			i, vsec_data.ll_wr[i].bar,
			vsec_data.ll_wr[i].off, chip->ll_region_wr[i].sz,
			chip->ll_region_wr[i].vaddr.io, &chip->ll_region_wr[i].paddr);

		pci_dbg(pdev, "Data:\tWRITE CH%.2u, BAR=%u, off=0x%.8lx, sz=0x%zx bytes, addr(v=%p, p=%pa)\n",
			i, vsec_data.dt_wr[i].bar,
			vsec_data.dt_wr[i].off, chip->dt_region_wr[i].sz,
			chip->dt_region_wr[i].vaddr.io, &chip->dt_region_wr[i].paddr);
	}

	for (i = 0; i < chip->ll_rd_cnt; i++) {
		pci_dbg(pdev, "L. List:\tREAD CH%.2u, BAR=%u, off=0x%.8lx, sz=0x%zx bytes, addr(v=%p, p=%pa)\n",
			i, vsec_data.ll_rd[i].bar,
			vsec_data.ll_rd[i].off, chip->ll_region_rd[i].sz,
			chip->ll_region_rd[i].vaddr.io, &chip->ll_region_rd[i].paddr);

		pci_dbg(pdev, "Data:\tREAD CH%.2u, BAR=%u, off=0x%.8lx, sz=0x%zx bytes, addr(v=%p, p=%pa)\n",
			i, vsec_data.dt_rd[i].bar,
			vsec_data.dt_rd[i].off, chip->dt_region_rd[i].sz,
			chip->dt_region_rd[i].vaddr.io, &chip->dt_region_rd[i].paddr);
	}

	pci_dbg(pdev, "Nr. IRQs:\t%u\n", chip->nr_irqs);

	/* Validating if PCI interrupts were enabled */
	if (!pci_dev_msi_enabled(pdev)) {
		pci_err(pdev, "enable interrupt failed\n");
		return -EPERM;
	}

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

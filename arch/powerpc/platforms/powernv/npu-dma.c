/*
 * This file implements the DMA operations for NVLink devices. The NPU
 * devices all point to the same iommu table as the parent PCI device.
 *
 * Copyright Alistair Popple, IBM Corporation 2015.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <linux/pci.h>
#include <linux/memblock.h>
#include <linux/iommu.h>

#include <asm/iommu.h>
#include <asm/pnv-pci.h>
#include <asm/msi_bitmap.h>
#include <asm/opal.h>

#include "powernv.h"
#include "pci.h"

/*
 * Other types of TCE cache invalidation are not functional in the
 * hardware.
 */
static struct pci_dev *get_pci_dev(struct device_node *dn)
{
	return PCI_DN(dn)->pcidev;
}

/* Given a NPU device get the associated PCI device. */
struct pci_dev *pnv_pci_get_gpu_dev(struct pci_dev *npdev)
{
	struct device_node *dn;
	struct pci_dev *gpdev;

	/* Get assoicated PCI device */
	dn = of_parse_phandle(npdev->dev.of_node, "ibm,gpu", 0);
	if (!dn)
		return NULL;

	gpdev = get_pci_dev(dn);
	of_node_put(dn);

	return gpdev;
}
EXPORT_SYMBOL(pnv_pci_get_gpu_dev);

/* Given the real PCI device get a linked NPU device. */
struct pci_dev *pnv_pci_get_npu_dev(struct pci_dev *gpdev, int index)
{
	struct device_node *dn;
	struct pci_dev *npdev;

	/* Get assoicated PCI device */
	dn = of_parse_phandle(gpdev->dev.of_node, "ibm,npu", index);
	if (!dn)
		return NULL;

	npdev = get_pci_dev(dn);
	of_node_put(dn);

	return npdev;
}
EXPORT_SYMBOL(pnv_pci_get_npu_dev);

#define NPU_DMA_OP_UNSUPPORTED()					\
	dev_err_once(dev, "%s operation unsupported for NVLink devices\n", \
		__func__)

static void *dma_npu_alloc(struct device *dev, size_t size,
			   dma_addr_t *dma_handle, gfp_t flag,
			   struct dma_attrs *attrs)
{
	NPU_DMA_OP_UNSUPPORTED();
	return NULL;
}

static void dma_npu_free(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t dma_handle,
			 struct dma_attrs *attrs)
{
	NPU_DMA_OP_UNSUPPORTED();
}

static dma_addr_t dma_npu_map_page(struct device *dev, struct page *page,
				   unsigned long offset, size_t size,
				   enum dma_data_direction direction,
				   struct dma_attrs *attrs)
{
	NPU_DMA_OP_UNSUPPORTED();
	return 0;
}

static int dma_npu_map_sg(struct device *dev, struct scatterlist *sglist,
			  int nelems, enum dma_data_direction direction,
			  struct dma_attrs *attrs)
{
	NPU_DMA_OP_UNSUPPORTED();
	return 0;
}

static int dma_npu_dma_supported(struct device *dev, u64 mask)
{
	NPU_DMA_OP_UNSUPPORTED();
	return 0;
}

static u64 dma_npu_get_required_mask(struct device *dev)
{
	NPU_DMA_OP_UNSUPPORTED();
	return 0;
}

struct dma_map_ops dma_npu_ops = {
	.map_page		= dma_npu_map_page,
	.map_sg			= dma_npu_map_sg,
	.alloc			= dma_npu_alloc,
	.free			= dma_npu_free,
	.dma_supported		= dma_npu_dma_supported,
	.get_required_mask	= dma_npu_get_required_mask,
};

/*
 * Returns the PE assoicated with the PCI device of the given
 * NPU. Returns the linked pci device if pci_dev != NULL.
 */
static struct pnv_ioda_pe *get_gpu_pci_dev_and_pe(struct pnv_ioda_pe *npe,
						  struct pci_dev **gpdev)
{
	struct pnv_phb *phb;
	struct pci_controller *hose;
	struct pci_dev *pdev;
	struct pnv_ioda_pe *pe;
	struct pci_dn *pdn;

	pdev = pnv_pci_get_gpu_dev(npe->pdev);
	if (!pdev)
		return NULL;

	pdn = pci_get_pdn(pdev);
	if (WARN_ON(!pdn || pdn->pe_number == IODA_INVALID_PE))
		return NULL;

	hose = pci_bus_to_host(pdev->bus);
	phb = hose->private_data;
	pe = &phb->ioda.pe_array[pdn->pe_number];

	if (gpdev)
		*gpdev = pdev;

	return pe;
}

long pnv_npu_set_window(struct pnv_ioda_pe *npe, int num,
		struct iommu_table *tbl)
{
	struct pnv_phb *phb = npe->phb;
	int64_t rc;
	const unsigned long size = tbl->it_indirect_levels ?
		tbl->it_level_size : tbl->it_size;
	const __u64 start_addr = tbl->it_offset << tbl->it_page_shift;
	const __u64 win_size = tbl->it_size << tbl->it_page_shift;

	pe_info(npe, "Setting up window %llx..%llx pg=%lx\n",
			start_addr, start_addr + win_size - 1,
			IOMMU_PAGE_SIZE(tbl));

	rc = opal_pci_map_pe_dma_window(phb->opal_id,
			npe->pe_number,
			npe->pe_number,
			tbl->it_indirect_levels + 1,
			__pa(tbl->it_base),
			size << 3,
			IOMMU_PAGE_SIZE(tbl));
	if (rc) {
		pe_err(npe, "Failed to configure TCE table, err %lld\n", rc);
		return rc;
	}
	pnv_pci_ioda2_tce_invalidate_entire(phb, false);

	/* Add the table to the list so its TCE cache will get invalidated */
	pnv_pci_link_table_and_group(phb->hose->node, num,
			tbl, &npe->table_group);

	return 0;
}

long pnv_npu_unset_window(struct pnv_ioda_pe *npe, int num)
{
	struct pnv_phb *phb = npe->phb;
	int64_t rc;

	pe_info(npe, "Removing DMA window\n");

	rc = opal_pci_map_pe_dma_window(phb->opal_id, npe->pe_number,
			npe->pe_number,
			0/* levels */, 0/* table address */,
			0/* table size */, 0/* page size */);
	if (rc) {
		pe_err(npe, "Unmapping failed, ret = %lld\n", rc);
		return rc;
	}
	pnv_pci_ioda2_tce_invalidate_entire(phb, false);

	pnv_pci_unlink_table_and_group(npe->table_group.tables[num],
			&npe->table_group);

	return 0;
}

/*
 * Enables 32 bit DMA on NPU.
 */
static void pnv_npu_dma_set_32(struct pnv_ioda_pe *npe)
{
	struct pci_dev *gpdev;
	struct pnv_ioda_pe *gpe;
	int64_t rc;

	/*
	 * Find the assoicated PCI devices and get the dma window
	 * information from there.
	 */
	if (!npe->pdev || !(npe->flags & PNV_IODA_PE_DEV))
		return;

	gpe = get_gpu_pci_dev_and_pe(npe, &gpdev);
	if (!gpe)
		return;

	rc = pnv_npu_set_window(npe, 0, gpe->table_group.tables[0]);

	/*
	 * We don't initialise npu_pe->tce32_table as we always use
	 * dma_npu_ops which are nops.
	 */
	set_dma_ops(&npe->pdev->dev, &dma_npu_ops);
}

/*
 * Enables bypass mode on the NPU. The NPU only supports one
 * window per link, so bypass needs to be explicitly enabled or
 * disabled. Unlike for a PHB3 bypass and non-bypass modes can't be
 * active at the same time.
 */
static int pnv_npu_dma_set_bypass(struct pnv_ioda_pe *npe)
{
	struct pnv_phb *phb = npe->phb;
	int64_t rc = 0;
	phys_addr_t top = memblock_end_of_DRAM();

	if (phb->type != PNV_PHB_NPU || !npe->pdev)
		return -EINVAL;

	rc = pnv_npu_unset_window(npe, 0);
	if (rc != OPAL_SUCCESS)
		return rc;

	/* Enable the bypass window */

	top = roundup_pow_of_two(top);
	dev_info(&npe->pdev->dev, "Enabling bypass for PE %d\n",
			npe->pe_number);
	rc = opal_pci_map_pe_dma_window_real(phb->opal_id,
			npe->pe_number, npe->pe_number,
			0 /* bypass base */, top);

	if (rc == OPAL_SUCCESS)
		pnv_pci_ioda2_tce_invalidate_entire(phb, false);

	return rc;
}

void pnv_npu_try_dma_set_bypass(struct pci_dev *gpdev, bool bypass)
{
	int i;
	struct pnv_phb *phb;
	struct pci_dn *pdn;
	struct pnv_ioda_pe *npe;
	struct pci_dev *npdev;

	for (i = 0; ; ++i) {
		npdev = pnv_pci_get_npu_dev(gpdev, i);

		if (!npdev)
			break;

		pdn = pci_get_pdn(npdev);
		if (WARN_ON(!pdn || pdn->pe_number == IODA_INVALID_PE))
			return;

		phb = pci_bus_to_host(npdev->bus)->private_data;

		/* We only do bypass if it's enabled on the linked device */
		npe = &phb->ioda.pe_array[pdn->pe_number];

		if (bypass) {
			dev_info(&npdev->dev,
					"Using 64-bit DMA iommu bypass\n");
			pnv_npu_dma_set_bypass(npe);
		} else {
			dev_info(&npdev->dev, "Using 32-bit DMA via iommu\n");
			pnv_npu_dma_set_32(npe);
		}
	}
}

/* Switch ownership from platform code to external user (e.g. VFIO) */
void pnv_npu_take_ownership(struct pnv_ioda_pe *npe)
{
	struct pnv_phb *phb = npe->phb;
	int64_t rc;

	/*
	 * Note: NPU has just a single TVE in the hardware which means that
	 * while used by the kernel, it can have either 32bit window or
	 * DMA bypass but never both. So we deconfigure 32bit window only
	 * if it was enabled at the moment of ownership change.
	 */
	if (npe->table_group.tables[0]) {
		pnv_npu_unset_window(npe, 0);
		return;
	}

	/* Disable bypass */
	rc = opal_pci_map_pe_dma_window_real(phb->opal_id,
			npe->pe_number, npe->pe_number,
			0 /* bypass base */, 0);
	if (rc) {
		pe_err(npe, "Failed to disable bypass, err %lld\n", rc);
		return;
	}
	pnv_pci_ioda2_tce_invalidate_entire(npe->phb, false);
}

struct pnv_ioda_pe *pnv_pci_npu_setup_iommu(struct pnv_ioda_pe *npe)
{
	struct pnv_phb *phb = npe->phb;
	struct pci_bus *pbus = phb->hose->bus;
	struct pci_dev *npdev, *gpdev = NULL, *gptmp;
	struct pnv_ioda_pe *gpe = get_gpu_pci_dev_and_pe(npe, &gpdev);

	if (!gpe || !gpdev)
		return NULL;

	list_for_each_entry(npdev, &pbus->devices, bus_list) {
		gptmp = pnv_pci_get_gpu_dev(npdev);

		if (gptmp != gpdev)
			continue;

		pe_info(gpe, "Attached NPU %s\n", dev_name(&npdev->dev));
		iommu_group_add_device(gpe->table_group.group, &npdev->dev);
	}

	return gpe;
}

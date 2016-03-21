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
#define TCE_KILL_INVAL_ALL PPC_BIT(0)

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

	if (npe->flags & PNV_IODA_PE_PEER) {
		pe = npe->peers[0];
		pdev = pe->pdev;
	} else {
		pdev = pnv_pci_get_gpu_dev(npe->pdev);
		if (!pdev)
			return NULL;

		pdn = pci_get_pdn(pdev);
		if (WARN_ON(!pdn || pdn->pe_number == IODA_INVALID_PE))
			return NULL;

		hose = pci_bus_to_host(pdev->bus);
		phb = hose->private_data;
		pe = &phb->ioda.pe_array[pdn->pe_number];
	}

	if (gpdev)
		*gpdev = pdev;

	return pe;
}

void pnv_npu_tce_invalidate_entire(struct pnv_ioda_pe *npe)
{
	struct pnv_phb *phb = npe->phb;

	if (WARN_ON(phb->type != PNV_PHB_NPU ||
		    !phb->ioda.tce_inval_reg ||
		    !(npe->flags & PNV_IODA_PE_DEV)))
		return;

	mb(); /* Ensure previous TCE table stores are visible */
	__raw_writeq(cpu_to_be64(TCE_KILL_INVAL_ALL),
		phb->ioda.tce_inval_reg);
}

void pnv_npu_tce_invalidate(struct pnv_ioda_pe *npe,
				struct iommu_table *tbl,
				unsigned long index,
				unsigned long npages,
				bool rm)
{
	struct pnv_phb *phb = npe->phb;

	/* We can only invalidate the whole cache on NPU */
	unsigned long val = TCE_KILL_INVAL_ALL;

	if (WARN_ON(phb->type != PNV_PHB_NPU ||
		    !phb->ioda.tce_inval_reg ||
		    !(npe->flags & PNV_IODA_PE_DEV)))
		return;

	mb(); /* Ensure previous TCE table stores are visible */
	if (rm)
		__raw_rm_writeq(cpu_to_be64(val),
		  (__be64 __iomem *) phb->ioda.tce_inval_reg_phys);
	else
		__raw_writeq(cpu_to_be64(val),
			phb->ioda.tce_inval_reg);
}

void pnv_npu_init_dma_pe(struct pnv_ioda_pe *npe)
{
	struct pnv_ioda_pe *gpe;
	struct pci_dev *gpdev;
	int i, avail = -1;

	if (!npe->pdev || !(npe->flags & PNV_IODA_PE_DEV))
		return;

	gpe = get_gpu_pci_dev_and_pe(npe, &gpdev);
	if (!gpe)
		return;

	for (i = 0; i < PNV_IODA_MAX_PEER_PES; i++) {
		/* Nothing to do if the PE is already connected. */
		if (gpe->peers[i] == npe)
			return;

		if (!gpe->peers[i])
			avail = i;
	}

	if (WARN_ON(avail < 0))
		return;

	gpe->peers[avail] = npe;
	gpe->flags |= PNV_IODA_PE_PEER;

	/*
	 * We assume that the NPU devices only have a single peer PE
	 * (the GPU PCIe device PE).
	 */
	npe->peers[0] = gpe;
	npe->flags |= PNV_IODA_PE_PEER;
}

/*
 * For the NPU we want to point the TCE table at the same table as the
 * real PCI device.
 */
static void pnv_npu_disable_bypass(struct pnv_ioda_pe *npe)
{
	struct pnv_phb *phb = npe->phb;
	struct pci_dev *gpdev;
	struct pnv_ioda_pe *gpe;
	void *addr;
	unsigned int size;
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

	addr = (void *)gpe->table_group.tables[0]->it_base;
	size = gpe->table_group.tables[0]->it_size << 3;
	rc = opal_pci_map_pe_dma_window(phb->opal_id, npe->pe_number,
					npe->pe_number, 1, __pa(addr),
					size, 0x1000);
	if (rc != OPAL_SUCCESS)
		pr_warn("%s: Error %lld setting DMA window on PHB#%d-PE#%d\n",
			__func__, rc, phb->hose->global_number, npe->pe_number);

	/*
	 * We don't initialise npu_pe->tce32_table as we always use
	 * dma_npu_ops which are nops.
	 */
	set_dma_ops(&npe->pdev->dev, &dma_npu_ops);
}

/*
 * Enable/disable bypass mode on the NPU. The NPU only supports one
 * window per link, so bypass needs to be explicitly enabled or
 * disabled. Unlike for a PHB3 bypass and non-bypass modes can't be
 * active at the same time.
 */
int pnv_npu_dma_set_bypass(struct pnv_ioda_pe *npe, bool enable)
{
	struct pnv_phb *phb = npe->phb;
	int64_t rc = 0;

	if (phb->type != PNV_PHB_NPU || !npe->pdev)
		return -EINVAL;

	if (enable) {
		/* Enable the bypass window */
		phys_addr_t top = memblock_end_of_DRAM();

		npe->tce_bypass_base = 0;
		top = roundup_pow_of_two(top);
		dev_info(&npe->pdev->dev, "Enabling bypass for PE %d\n",
			 npe->pe_number);
		rc = opal_pci_map_pe_dma_window_real(phb->opal_id,
					npe->pe_number, npe->pe_number,
					npe->tce_bypass_base, top);
	} else {
		/*
		 * Disable the bypass window by replacing it with the
		 * TCE32 window.
		 */
		pnv_npu_disable_bypass(npe);
	}

	return rc;
}

int pnv_npu_dma_set_mask(struct pci_dev *npdev, u64 dma_mask)
{
	struct pci_controller *hose = pci_bus_to_host(npdev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct pci_dn *pdn = pci_get_pdn(npdev);
	struct pnv_ioda_pe *npe, *gpe;
	struct pci_dev *gpdev;
	uint64_t top;
	bool bypass = false;

	if (WARN_ON(!pdn || pdn->pe_number == IODA_INVALID_PE))
		return -ENXIO;

	/* We only do bypass if it's enabled on the linked device */
	npe = &phb->ioda.pe_array[pdn->pe_number];
	gpe = get_gpu_pci_dev_and_pe(npe, &gpdev);
	if (!gpe)
		return -ENODEV;

	if (gpe->tce_bypass_enabled) {
		top = gpe->tce_bypass_base + memblock_end_of_DRAM() - 1;
		bypass = (dma_mask >= top);
	}

	if (bypass)
		dev_info(&npdev->dev, "Using 64-bit DMA iommu bypass\n");
	else
		dev_info(&npdev->dev, "Using 32-bit DMA via iommu\n");

	pnv_npu_dma_set_bypass(npe, bypass);
	*npdev->dev.dma_mask = dma_mask;

	return 0;
}

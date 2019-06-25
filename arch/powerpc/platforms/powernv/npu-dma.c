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

#include <linux/mmu_notifier.h>
#include <linux/mmu_context.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/memblock.h>
#include <linux/sizes.h>

#include <asm/debugfs.h>
#include <asm/powernv.h>
#include <asm/opal.h>

#include "pci.h"

static struct pci_dev *get_pci_dev(struct device_node *dn)
{
	struct pci_dn *pdn = PCI_DN(dn);
	struct pci_dev *pdev;

	pdev = pci_get_domain_bus_and_slot(pci_domain_nr(pdn->phb->bus),
					   pdn->busno, pdn->devfn);

	/*
	 * pci_get_domain_bus_and_slot() increased the reference count of
	 * the PCI device, but callers don't need that actually as the PE
	 * already holds a reference to the device. Since callers aren't
	 * aware of the reference count change, call pci_dev_put() now to
	 * avoid leaks.
	 */
	if (pdev)
		pci_dev_put(pdev);

	return pdev;
}

/* Given a NPU device get the associated PCI device. */
struct pci_dev *pnv_pci_get_gpu_dev(struct pci_dev *npdev)
{
	struct device_node *dn;
	struct pci_dev *gpdev;

	if (WARN_ON(!npdev))
		return NULL;

	if (WARN_ON(!npdev->dev.of_node))
		return NULL;

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

	if (WARN_ON(!gpdev))
		return NULL;

	/* Not all PCI devices have device-tree nodes */
	if (!gpdev->dev.of_node)
		return NULL;

	/* Get assoicated PCI device */
	dn = of_parse_phandle(gpdev->dev.of_node, "ibm,npu", index);
	if (!dn)
		return NULL;

	npdev = get_pci_dev(dn);
	of_node_put(dn);

	return npdev;
}
EXPORT_SYMBOL(pnv_pci_get_npu_dev);

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

static long pnv_npu_unset_window(struct iommu_table_group *table_group,
		int num);

static long pnv_npu_set_window(struct iommu_table_group *table_group, int num,
		struct iommu_table *tbl)
{
	struct pnv_ioda_pe *npe = container_of(table_group, struct pnv_ioda_pe,
			table_group);
	struct pnv_phb *phb = npe->phb;
	int64_t rc;
	const unsigned long size = tbl->it_indirect_levels ?
		tbl->it_level_size : tbl->it_size;
	const __u64 start_addr = tbl->it_offset << tbl->it_page_shift;
	const __u64 win_size = tbl->it_size << tbl->it_page_shift;
	int num2 = (num == 0) ? 1 : 0;

	/* NPU has just one TVE so if there is another table, remove it first */
	if (npe->table_group.tables[num2])
		pnv_npu_unset_window(&npe->table_group, num2);

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

static long pnv_npu_unset_window(struct iommu_table_group *table_group, int num)
{
	struct pnv_ioda_pe *npe = container_of(table_group, struct pnv_ioda_pe,
			table_group);
	struct pnv_phb *phb = npe->phb;
	int64_t rc;

	if (!npe->table_group.tables[num])
		return 0;

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

	rc = pnv_npu_set_window(&npe->table_group, 0,
			gpe->table_group.tables[0]);

	/*
	 * NVLink devices use the same TCE table configuration as
	 * their parent device so drivers shouldn't be doing DMA
	 * operations directly on these devices.
	 */
	set_dma_ops(&npe->pdev->dev, &dma_dummy_ops);
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

	if (phb->type != PNV_PHB_NPU_NVLINK || !npe->pdev)
		return -EINVAL;

	rc = pnv_npu_unset_window(&npe->table_group, 0);
	if (rc != OPAL_SUCCESS)
		return rc;

	/* Enable the bypass window */

	top = roundup_pow_of_two(top);
	dev_info(&npe->pdev->dev, "Enabling bypass for PE %x\n",
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

#ifdef CONFIG_IOMMU_API
/* Switch ownership from platform code to external user (e.g. VFIO) */
static void pnv_npu_take_ownership(struct iommu_table_group *table_group)
{
	struct pnv_ioda_pe *npe = container_of(table_group, struct pnv_ioda_pe,
			table_group);
	struct pnv_phb *phb = npe->phb;
	int64_t rc;
	struct pci_dev *gpdev = NULL;

	/*
	 * Note: NPU has just a single TVE in the hardware which means that
	 * while used by the kernel, it can have either 32bit window or
	 * DMA bypass but never both. So we deconfigure 32bit window only
	 * if it was enabled at the moment of ownership change.
	 */
	if (npe->table_group.tables[0]) {
		pnv_npu_unset_window(&npe->table_group, 0);
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

	get_gpu_pci_dev_and_pe(npe, &gpdev);
	if (gpdev)
		pnv_npu2_unmap_lpar_dev(gpdev);
}

static void pnv_npu_release_ownership(struct iommu_table_group *table_group)
{
	struct pnv_ioda_pe *npe = container_of(table_group, struct pnv_ioda_pe,
			table_group);
	struct pci_dev *gpdev = NULL;

	get_gpu_pci_dev_and_pe(npe, &gpdev);
	if (gpdev)
		pnv_npu2_map_lpar_dev(gpdev, 0, MSR_DR | MSR_PR | MSR_HV);
}

static struct iommu_table_group_ops pnv_pci_npu_ops = {
	.set_window = pnv_npu_set_window,
	.unset_window = pnv_npu_unset_window,
	.take_ownership = pnv_npu_take_ownership,
	.release_ownership = pnv_npu_release_ownership,
};
#endif /* !CONFIG_IOMMU_API */

/*
 * NPU2 ATS
 */
/* Maximum possible number of ATSD MMIO registers per NPU */
#define NV_NMMU_ATSD_REGS 8
#define NV_NPU_MAX_PE_NUM	16

/*
 * A compound NPU IOMMU group which might consist of 1 GPU + 2xNPUs (POWER8) or
 * up to 3 x (GPU + 2xNPUs) (POWER9).
 */
struct npu_comp {
	struct iommu_table_group table_group;
	int pe_num;
	struct pnv_ioda_pe *pe[NV_NPU_MAX_PE_NUM];
};

/* An NPU descriptor, valid for POWER9 only */
struct npu {
	int index;
	struct npu_comp npucomp;
};

#ifdef CONFIG_IOMMU_API
static long pnv_npu_peers_create_table_userspace(
		struct iommu_table_group *table_group,
		int num, __u32 page_shift, __u64 window_size, __u32 levels,
		struct iommu_table **ptbl)
{
	struct npu_comp *npucomp = container_of(table_group, struct npu_comp,
			table_group);

	if (!npucomp->pe_num || !npucomp->pe[0] ||
			!npucomp->pe[0]->table_group.ops ||
			!npucomp->pe[0]->table_group.ops->create_table)
		return -EFAULT;

	return npucomp->pe[0]->table_group.ops->create_table(
			&npucomp->pe[0]->table_group, num, page_shift,
			window_size, levels, ptbl);
}

static long pnv_npu_peers_set_window(struct iommu_table_group *table_group,
		int num, struct iommu_table *tbl)
{
	int i, j;
	long ret = 0;
	struct npu_comp *npucomp = container_of(table_group, struct npu_comp,
			table_group);

	for (i = 0; i < npucomp->pe_num; ++i) {
		struct pnv_ioda_pe *pe = npucomp->pe[i];

		if (!pe->table_group.ops->set_window)
			continue;

		ret = pe->table_group.ops->set_window(&pe->table_group,
				num, tbl);
		if (ret)
			break;
	}

	if (ret) {
		for (j = 0; j < i; ++j) {
			struct pnv_ioda_pe *pe = npucomp->pe[j];

			if (!pe->table_group.ops->unset_window)
				continue;

			ret = pe->table_group.ops->unset_window(
					&pe->table_group, num);
			if (ret)
				break;
		}
	} else {
		table_group->tables[num] = iommu_tce_table_get(tbl);
	}

	return ret;
}

static long pnv_npu_peers_unset_window(struct iommu_table_group *table_group,
		int num)
{
	int i, j;
	long ret = 0;
	struct npu_comp *npucomp = container_of(table_group, struct npu_comp,
			table_group);

	for (i = 0; i < npucomp->pe_num; ++i) {
		struct pnv_ioda_pe *pe = npucomp->pe[i];

		WARN_ON(npucomp->table_group.tables[num] !=
				table_group->tables[num]);
		if (!npucomp->table_group.tables[num])
			continue;

		if (!pe->table_group.ops->unset_window)
			continue;

		ret = pe->table_group.ops->unset_window(&pe->table_group, num);
		if (ret)
			break;
	}

	if (ret) {
		for (j = 0; j < i; ++j) {
			struct pnv_ioda_pe *pe = npucomp->pe[j];

			if (!npucomp->table_group.tables[num])
				continue;

			if (!pe->table_group.ops->set_window)
				continue;

			ret = pe->table_group.ops->set_window(&pe->table_group,
					num, table_group->tables[num]);
			if (ret)
				break;
		}
	} else if (table_group->tables[num]) {
		iommu_tce_table_put(table_group->tables[num]);
		table_group->tables[num] = NULL;
	}

	return ret;
}

static void pnv_npu_peers_take_ownership(struct iommu_table_group *table_group)
{
	int i;
	struct npu_comp *npucomp = container_of(table_group, struct npu_comp,
			table_group);

	for (i = 0; i < npucomp->pe_num; ++i) {
		struct pnv_ioda_pe *pe = npucomp->pe[i];

		if (!pe->table_group.ops->take_ownership)
			continue;
		pe->table_group.ops->take_ownership(&pe->table_group);
	}
}

static void pnv_npu_peers_release_ownership(
		struct iommu_table_group *table_group)
{
	int i;
	struct npu_comp *npucomp = container_of(table_group, struct npu_comp,
			table_group);

	for (i = 0; i < npucomp->pe_num; ++i) {
		struct pnv_ioda_pe *pe = npucomp->pe[i];

		if (!pe->table_group.ops->release_ownership)
			continue;
		pe->table_group.ops->release_ownership(&pe->table_group);
	}
}

static struct iommu_table_group_ops pnv_npu_peers_ops = {
	.get_table_size = pnv_pci_ioda2_get_table_size,
	.create_table = pnv_npu_peers_create_table_userspace,
	.set_window = pnv_npu_peers_set_window,
	.unset_window = pnv_npu_peers_unset_window,
	.take_ownership = pnv_npu_peers_take_ownership,
	.release_ownership = pnv_npu_peers_release_ownership,
};

static void pnv_comp_attach_table_group(struct npu_comp *npucomp,
		struct pnv_ioda_pe *pe)
{
	if (WARN_ON(npucomp->pe_num == NV_NPU_MAX_PE_NUM))
		return;

	npucomp->pe[npucomp->pe_num] = pe;
	++npucomp->pe_num;
}

struct iommu_table_group *pnv_try_setup_npu_table_group(struct pnv_ioda_pe *pe)
{
	struct iommu_table_group *table_group;
	struct npu_comp *npucomp;
	struct pci_dev *gpdev = NULL;
	struct pci_controller *hose;
	struct pci_dev *npdev = NULL;

	list_for_each_entry(gpdev, &pe->pbus->devices, bus_list) {
		npdev = pnv_pci_get_npu_dev(gpdev, 0);
		if (npdev)
			break;
	}

	if (!npdev)
		/* It is not an NPU attached device, skip */
		return NULL;

	hose = pci_bus_to_host(npdev->bus);

	if (hose->npu) {
		table_group = &hose->npu->npucomp.table_group;

		if (!table_group->group) {
			table_group->ops = &pnv_npu_peers_ops;
			iommu_register_group(table_group,
					hose->global_number,
					pe->pe_number);
		}
	} else {
		/* Create a group for 1 GPU and attached NPUs for POWER8 */
		pe->npucomp = kzalloc(sizeof(*pe->npucomp), GFP_KERNEL);
		table_group = &pe->npucomp->table_group;
		table_group->ops = &pnv_npu_peers_ops;
		iommu_register_group(table_group, hose->global_number,
				pe->pe_number);
	}

	/* Steal capabilities from a GPU PE */
	table_group->max_dynamic_windows_supported =
		pe->table_group.max_dynamic_windows_supported;
	table_group->tce32_start = pe->table_group.tce32_start;
	table_group->tce32_size = pe->table_group.tce32_size;
	table_group->max_levels = pe->table_group.max_levels;
	if (!table_group->pgsizes)
		table_group->pgsizes = pe->table_group.pgsizes;

	npucomp = container_of(table_group, struct npu_comp, table_group);
	pnv_comp_attach_table_group(npucomp, pe);

	return table_group;
}

struct iommu_table_group *pnv_npu_compound_attach(struct pnv_ioda_pe *pe)
{
	struct iommu_table_group *table_group;
	struct npu_comp *npucomp;
	struct pci_dev *gpdev = NULL;
	struct pci_dev *npdev;
	struct pnv_ioda_pe *gpe = get_gpu_pci_dev_and_pe(pe, &gpdev);

	WARN_ON(!(pe->flags & PNV_IODA_PE_DEV));
	if (!gpe)
		return NULL;

	/*
	 * IODA2 bridges get this set up from pci_controller_ops::setup_bridge
	 * but NPU bridges do not have this hook defined so we do it here.
	 * We do not setup other table group parameters as they won't be used
	 * anyway - NVLink bridges are subordinate PEs.
	 */
	pe->table_group.ops = &pnv_pci_npu_ops;

	table_group = iommu_group_get_iommudata(
			iommu_group_get(&gpdev->dev));

	/*
	 * On P9 NPU PHB and PCI PHB support different page sizes,
	 * keep only matching. We expect here that NVLink bridge PE pgsizes is
	 * initialized by the caller.
	 */
	table_group->pgsizes &= pe->table_group.pgsizes;
	npucomp = container_of(table_group, struct npu_comp, table_group);
	pnv_comp_attach_table_group(npucomp, pe);

	list_for_each_entry(npdev, &pe->phb->hose->bus->devices, bus_list) {
		struct pci_dev *gpdevtmp = pnv_pci_get_gpu_dev(npdev);

		if (gpdevtmp != gpdev)
			continue;

		iommu_add_device(table_group, &npdev->dev);
	}

	return table_group;
}
#endif /* CONFIG_IOMMU_API */

int pnv_npu2_init(struct pci_controller *hose)
{
	static int npu_index;
	struct npu *npu;
	int ret;

	npu = kzalloc(sizeof(*npu), GFP_KERNEL);
	if (!npu)
		return -ENOMEM;

	npu_index++;
	if (WARN_ON(npu_index >= NV_MAX_NPUS)) {
		ret = -ENOSPC;
		goto fail_exit;
	}
	npu->index = npu_index;
	hose->npu = npu;

	return 0;

fail_exit:
	kfree(npu);
	return ret;
}

int pnv_npu2_map_lpar_dev(struct pci_dev *gpdev, unsigned int lparid,
		unsigned long msr)
{
	int ret;
	struct pci_dev *npdev = pnv_pci_get_npu_dev(gpdev, 0);
	struct pci_controller *hose;
	struct pnv_phb *nphb;

	if (!npdev)
		return -ENODEV;

	hose = pci_bus_to_host(npdev->bus);
	nphb = hose->private_data;

	dev_dbg(&gpdev->dev, "Map LPAR opalid=%llu lparid=%u\n",
			nphb->opal_id, lparid);
	/*
	 * Currently we only support radix and non-zero LPCR only makes sense
	 * for hash tables so skiboot expects the LPCR parameter to be a zero.
	 */
	ret = opal_npu_map_lpar(nphb->opal_id, pci_dev_id(gpdev), lparid,
				0 /* LPCR bits */);
	if (ret) {
		dev_err(&gpdev->dev, "Error %d mapping device to LPAR\n", ret);
		return ret;
	}

	dev_dbg(&gpdev->dev, "init context opalid=%llu msr=%lx\n",
			nphb->opal_id, msr);
	ret = opal_npu_init_context(nphb->opal_id, 0/*__unused*/, msr,
				    pci_dev_id(gpdev));
	if (ret < 0)
		dev_err(&gpdev->dev, "Failed to init context: %d\n", ret);
	else
		ret = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(pnv_npu2_map_lpar_dev);

void pnv_npu2_map_lpar(struct pnv_ioda_pe *gpe, unsigned long msr)
{
	struct pci_dev *gpdev;

	list_for_each_entry(gpdev, &gpe->pbus->devices, bus_list)
		pnv_npu2_map_lpar_dev(gpdev, 0, msr);
}

int pnv_npu2_unmap_lpar_dev(struct pci_dev *gpdev)
{
	int ret;
	struct pci_dev *npdev = pnv_pci_get_npu_dev(gpdev, 0);
	struct pci_controller *hose;
	struct pnv_phb *nphb;

	if (!npdev)
		return -ENODEV;

	hose = pci_bus_to_host(npdev->bus);
	nphb = hose->private_data;

	dev_dbg(&gpdev->dev, "destroy context opalid=%llu\n",
			nphb->opal_id);
	ret = opal_npu_destroy_context(nphb->opal_id, 0/*__unused*/,
				       pci_dev_id(gpdev));
	if (ret < 0) {
		dev_err(&gpdev->dev, "Failed to destroy context: %d\n", ret);
		return ret;
	}

	/* Set LPID to 0 anyway, just to be safe */
	dev_dbg(&gpdev->dev, "Map LPAR opalid=%llu lparid=0\n", nphb->opal_id);
	ret = opal_npu_map_lpar(nphb->opal_id, pci_dev_id(gpdev), 0 /*LPID*/,
				0 /* LPCR bits */);
	if (ret)
		dev_err(&gpdev->dev, "Error %d mapping device to LPAR\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(pnv_npu2_unmap_lpar_dev);

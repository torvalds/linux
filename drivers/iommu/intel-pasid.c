// SPDX-License-Identifier: GPL-2.0
/**
 * intel-pasid.c - PASID idr, table and entry manipulation
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */

#define pr_fmt(fmt)	"DMAR: " fmt

#include <linux/dmar.h>
#include <linux/intel-iommu.h>
#include <linux/iommu.h>
#include <linux/memory.h>
#include <linux/pci.h>
#include <linux/pci-ats.h>
#include <linux/spinlock.h>

#include "intel-pasid.h"

/*
 * Intel IOMMU system wide PASID name space:
 */
static DEFINE_SPINLOCK(pasid_lock);
u32 intel_pasid_max_id = PASID_MAX;
static DEFINE_IDR(pasid_idr);

int intel_pasid_alloc_id(void *ptr, int start, int end, gfp_t gfp)
{
	int ret, min, max;

	min = max_t(int, start, PASID_MIN);
	max = min_t(int, end, intel_pasid_max_id);

	WARN_ON(in_interrupt());
	idr_preload(gfp);
	spin_lock(&pasid_lock);
	ret = idr_alloc(&pasid_idr, ptr, min, max, GFP_ATOMIC);
	spin_unlock(&pasid_lock);
	idr_preload_end();

	return ret;
}

void intel_pasid_free_id(int pasid)
{
	spin_lock(&pasid_lock);
	idr_remove(&pasid_idr, pasid);
	spin_unlock(&pasid_lock);
}

void *intel_pasid_lookup_id(int pasid)
{
	void *p;

	spin_lock(&pasid_lock);
	p = idr_find(&pasid_idr, pasid);
	spin_unlock(&pasid_lock);

	return p;
}

/*
 * Per device pasid table management:
 */
static inline void
device_attach_pasid_table(struct device_domain_info *info,
			  struct pasid_table *pasid_table)
{
	info->pasid_table = pasid_table;
	list_add(&info->table, &pasid_table->dev);
}

static inline void
device_detach_pasid_table(struct device_domain_info *info,
			  struct pasid_table *pasid_table)
{
	info->pasid_table = NULL;
	list_del(&info->table);
}

struct pasid_table_opaque {
	struct pasid_table	**pasid_table;
	int			segment;
	int			bus;
	int			devfn;
};

static int search_pasid_table(struct device_domain_info *info, void *opaque)
{
	struct pasid_table_opaque *data = opaque;

	if (info->iommu->segment == data->segment &&
	    info->bus == data->bus &&
	    info->devfn == data->devfn &&
	    info->pasid_table) {
		*data->pasid_table = info->pasid_table;
		return 1;
	}

	return 0;
}

static int get_alias_pasid_table(struct pci_dev *pdev, u16 alias, void *opaque)
{
	struct pasid_table_opaque *data = opaque;

	data->segment = pci_domain_nr(pdev->bus);
	data->bus = PCI_BUS_NUM(alias);
	data->devfn = alias & 0xff;

	return for_each_device_domain(&search_pasid_table, data);
}

/*
 * Allocate a pasid table for @dev. It should be called in a
 * single-thread context.
 */
int intel_pasid_alloc_table(struct device *dev)
{
	struct device_domain_info *info;
	struct pasid_table *pasid_table;
	struct pasid_table_opaque data;
	struct page *pages;
	int max_pasid = 0;
	int ret, order;
	int size;

	might_sleep();
	info = dev->archdata.iommu;
	if (WARN_ON(!info || !dev_is_pci(dev) || info->pasid_table))
		return -EINVAL;

	/* DMA alias device already has a pasid table, use it: */
	data.pasid_table = &pasid_table;
	ret = pci_for_each_dma_alias(to_pci_dev(dev),
				     &get_alias_pasid_table, &data);
	if (ret)
		goto attach_out;

	pasid_table = kzalloc(sizeof(*pasid_table), GFP_KERNEL);
	if (!pasid_table)
		return -ENOMEM;
	INIT_LIST_HEAD(&pasid_table->dev);

	if (info->pasid_supported)
		max_pasid = min_t(int, pci_max_pasids(to_pci_dev(dev)),
				  intel_pasid_max_id);

	size = max_pasid >> (PASID_PDE_SHIFT - 3);
	order = size ? get_order(size) : 0;
	pages = alloc_pages_node(info->iommu->node,
				 GFP_KERNEL | __GFP_ZERO, order);
	if (!pages)
		return -ENOMEM;

	pasid_table->table = page_address(pages);
	pasid_table->order = order;
	pasid_table->max_pasid = 1 << (order + PAGE_SHIFT + 3);

attach_out:
	device_attach_pasid_table(info, pasid_table);

	return 0;
}

/* Get PRESENT bit of a PASID directory entry. */
static inline bool
pasid_pde_is_present(struct pasid_dir_entry *pde)
{
	return READ_ONCE(pde->val) & PASID_PTE_PRESENT;
}

/* Get PASID table from a PASID directory entry. */
static inline struct pasid_entry *
get_pasid_table_from_pde(struct pasid_dir_entry *pde)
{
	if (!pasid_pde_is_present(pde))
		return NULL;

	return phys_to_virt(READ_ONCE(pde->val) & PDE_PFN_MASK);
}

void intel_pasid_free_table(struct device *dev)
{
	struct device_domain_info *info;
	struct pasid_table *pasid_table;
	struct pasid_dir_entry *dir;
	struct pasid_entry *table;
	int i, max_pde;

	info = dev->archdata.iommu;
	if (!info || !dev_is_pci(dev) || !info->pasid_table)
		return;

	pasid_table = info->pasid_table;
	device_detach_pasid_table(info, pasid_table);

	if (!list_empty(&pasid_table->dev))
		return;

	/* Free scalable mode PASID directory tables: */
	dir = pasid_table->table;
	max_pde = pasid_table->max_pasid >> PASID_PDE_SHIFT;
	for (i = 0; i < max_pde; i++) {
		table = get_pasid_table_from_pde(&dir[i]);
		free_pgtable_page(table);
	}

	free_pages((unsigned long)pasid_table->table, pasid_table->order);
	kfree(pasid_table);
}

struct pasid_table *intel_pasid_get_table(struct device *dev)
{
	struct device_domain_info *info;

	info = dev->archdata.iommu;
	if (!info)
		return NULL;

	return info->pasid_table;
}

int intel_pasid_get_dev_max_id(struct device *dev)
{
	struct device_domain_info *info;

	info = dev->archdata.iommu;
	if (!info || !info->pasid_table)
		return 0;

	return info->pasid_table->max_pasid;
}

struct pasid_entry *intel_pasid_get_entry(struct device *dev, int pasid)
{
	struct device_domain_info *info;
	struct pasid_table *pasid_table;
	struct pasid_dir_entry *dir;
	struct pasid_entry *entries;
	int dir_index, index;

	pasid_table = intel_pasid_get_table(dev);
	if (WARN_ON(!pasid_table || pasid < 0 ||
		    pasid >= intel_pasid_get_dev_max_id(dev)))
		return NULL;

	dir = pasid_table->table;
	info = dev->archdata.iommu;
	dir_index = pasid >> PASID_PDE_SHIFT;
	index = pasid & PASID_PTE_MASK;

	spin_lock(&pasid_lock);
	entries = get_pasid_table_from_pde(&dir[dir_index]);
	if (!entries) {
		entries = alloc_pgtable_page(info->iommu->node);
		if (!entries) {
			spin_unlock(&pasid_lock);
			return NULL;
		}

		WRITE_ONCE(dir[dir_index].val,
			   (u64)virt_to_phys(entries) | PASID_PTE_PRESENT);
	}
	spin_unlock(&pasid_lock);

	return &entries[index];
}

/*
 * Interfaces for PASID table entry manipulation:
 */
static inline void pasid_clear_entry(struct pasid_entry *pe)
{
	WRITE_ONCE(pe->val[0], 0);
	WRITE_ONCE(pe->val[1], 0);
	WRITE_ONCE(pe->val[2], 0);
	WRITE_ONCE(pe->val[3], 0);
	WRITE_ONCE(pe->val[4], 0);
	WRITE_ONCE(pe->val[5], 0);
	WRITE_ONCE(pe->val[6], 0);
	WRITE_ONCE(pe->val[7], 0);
}

void intel_pasid_clear_entry(struct device *dev, int pasid)
{
	struct pasid_entry *pe;

	pe = intel_pasid_get_entry(dev, pasid);
	if (WARN_ON(!pe))
		return;

	pasid_clear_entry(pe);
}

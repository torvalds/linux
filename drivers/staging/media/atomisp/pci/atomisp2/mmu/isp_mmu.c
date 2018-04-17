/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */
/*
 * ISP MMU management wrap code
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/mm.h>		/* for GFP_ATOMIC */
#include <linux/slab.h>		/* for kmalloc */
#include <linux/list.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/sizes.h>

#ifdef CONFIG_X86
#include <asm/set_memory.h>
#endif

#include "atomisp_internal.h"
#include "mmu/isp_mmu.h"

/*
 * 64-bit x86 processor physical address layout:
 * 0		- 0x7fffffff		DDR RAM	(2GB)
 * 0x80000000	- 0xffffffff		MMIO	(2GB)
 * 0x100000000	- 0x3fffffffffff	DDR RAM	(64TB)
 * So if the system has more than 2GB DDR memory, the lower 2GB occupies the
 * physical address 0 - 0x7fffffff and the rest will start from 0x100000000.
 * We have to make sure memory is allocated from the lower 2GB for devices
 * that are only 32-bit capable(e.g. the ISP MMU).
 *
 * For any confusion, contact bin.gao@intel.com.
 */
#define NR_PAGES_2GB	(SZ_2G / PAGE_SIZE)

static void free_mmu_map(struct isp_mmu *mmu, unsigned int start_isp_virt,
				unsigned int end_isp_virt);

static unsigned int atomisp_get_pte(phys_addr_t pt, unsigned int idx)
{
	unsigned int *pt_virt = phys_to_virt(pt);
	return *(pt_virt + idx);
}

static void atomisp_set_pte(phys_addr_t pt,
			    unsigned int idx, unsigned int pte)
{
	unsigned int *pt_virt = phys_to_virt(pt);
	*(pt_virt + idx) = pte;
}

static void *isp_pt_phys_to_virt(phys_addr_t phys)
{
	return phys_to_virt(phys);
}

static phys_addr_t isp_pte_to_pgaddr(struct isp_mmu *mmu,
				     unsigned int pte)
{
	return mmu->driver->pte_to_phys(mmu, pte);
}

static unsigned int isp_pgaddr_to_pte_valid(struct isp_mmu *mmu,
					    phys_addr_t phys)
{
	unsigned int pte = mmu->driver->phys_to_pte(mmu, phys);
	return (unsigned int) (pte | ISP_PTE_VALID_MASK(mmu));
}

/*
 * allocate a uncacheable page table.
 * return physical address.
 */
static phys_addr_t alloc_page_table(struct isp_mmu *mmu)
{
	int i;
	phys_addr_t page;
	void *virt;

	/*page table lock may needed here*/
	/*
	 * The slab allocator(kmem_cache and kmalloc family) doesn't handle
	 * GFP_DMA32 flag, so we have to use buddy allocator.
	 */
	if (totalram_pages > (unsigned long)NR_PAGES_2GB)
		virt = (void *)__get_free_page(GFP_KERNEL | GFP_DMA32);
	else
		virt = kmem_cache_zalloc(mmu->tbl_cache, GFP_KERNEL);
	if (!virt)
		return (phys_addr_t)NULL_PAGE;

	/*
	 * we need a uncacheable page table.
	 */
#ifdef	CONFIG_X86
	set_memory_uc((unsigned long)virt, 1);
#endif

	page = virt_to_phys(virt);

	for (i = 0; i < 1024; i++) {
		/* NEED CHECK */
		atomisp_set_pte(page, i, mmu->driver->null_pte);
	}

	return page;
}

static void free_page_table(struct isp_mmu *mmu, phys_addr_t page)
{
	void *virt;
	page &= ISP_PAGE_MASK;
	/*
	 * reset the page to write back before free
	 */
	virt = phys_to_virt(page);

#ifdef	CONFIG_X86
	set_memory_wb((unsigned long)virt, 1);
#endif

	kmem_cache_free(mmu->tbl_cache, virt);
}

static void mmu_remap_error(struct isp_mmu *mmu,
			    phys_addr_t l1_pt, unsigned int l1_idx,
			    phys_addr_t l2_pt, unsigned int l2_idx,
			    unsigned int isp_virt, phys_addr_t old_phys,
			    phys_addr_t new_phys)
{
	dev_err(atomisp_dev, "address remap:\n\n"
		     "\tL1 PT: virt = %p, phys = 0x%llx, "
		     "idx = %d\n"
		     "\tL2 PT: virt = %p, phys = 0x%llx, "
		     "idx = %d\n"
		     "\told: isp_virt = 0x%x, phys = 0x%llx\n"
		     "\tnew: isp_virt = 0x%x, phys = 0x%llx\n",
		     isp_pt_phys_to_virt(l1_pt),
		     (u64)l1_pt, l1_idx,
		     isp_pt_phys_to_virt(l2_pt),
		     (u64)l2_pt, l2_idx, isp_virt,
		     (u64)old_phys, isp_virt,
		     (u64)new_phys);
}

static void mmu_unmap_l2_pte_error(struct isp_mmu *mmu,
				   phys_addr_t l1_pt, unsigned int l1_idx,
				   phys_addr_t l2_pt, unsigned int l2_idx,
				   unsigned int isp_virt, unsigned int pte)
{
	dev_err(atomisp_dev, "unmap unvalid L2 pte:\n\n"
		     "\tL1 PT: virt = %p, phys = 0x%llx, "
		     "idx = %d\n"
		     "\tL2 PT: virt = %p, phys = 0x%llx, "
		     "idx = %d\n"
		     "\tisp_virt = 0x%x, pte(page phys) = 0x%x\n",
		     isp_pt_phys_to_virt(l1_pt),
		     (u64)l1_pt, l1_idx,
		     isp_pt_phys_to_virt(l2_pt),
		     (u64)l2_pt, l2_idx, isp_virt,
		     pte);
}

static void mmu_unmap_l1_pte_error(struct isp_mmu *mmu,
				   phys_addr_t l1_pt, unsigned int l1_idx,
				   unsigned int isp_virt, unsigned int pte)
{
	dev_err(atomisp_dev, "unmap unvalid L1 pte (L2 PT):\n\n"
		     "\tL1 PT: virt = %p, phys = 0x%llx, "
		     "idx = %d\n"
		     "\tisp_virt = 0x%x, l1_pte(L2 PT) = 0x%x\n",
		     isp_pt_phys_to_virt(l1_pt),
		     (u64)l1_pt, l1_idx, (unsigned int)isp_virt,
		     pte);
}

static void mmu_unmap_l1_pt_error(struct isp_mmu *mmu, unsigned int pte)
{
	dev_err(atomisp_dev, "unmap unvalid L1PT:\n\n"
		     "L1PT = 0x%x\n", (unsigned int)pte);
}

/*
 * Update L2 page table according to isp virtual address and page physical
 * address
 */
static int mmu_l2_map(struct isp_mmu *mmu, phys_addr_t l1_pt,
		      unsigned int l1_idx, phys_addr_t l2_pt,
		      unsigned int start, unsigned int end, phys_addr_t phys)
{
	unsigned int ptr;
	unsigned int idx;
	unsigned int pte;

	l2_pt &= ISP_PAGE_MASK;

	start = start & ISP_PAGE_MASK;
	end = ISP_PAGE_ALIGN(end);
	phys &= ISP_PAGE_MASK;

	ptr = start;
	do {
		idx = ISP_PTR_TO_L2_IDX(ptr);

		pte = atomisp_get_pte(l2_pt, idx);

		if (ISP_PTE_VALID(mmu, pte)) {
			mmu_remap_error(mmu, l1_pt, l1_idx,
					  l2_pt, idx, ptr, pte, phys);

			/* free all mapped pages */
			free_mmu_map(mmu, start, ptr);

			return -EINVAL;
		}

		pte = isp_pgaddr_to_pte_valid(mmu, phys);

		atomisp_set_pte(l2_pt, idx, pte);
		mmu->l2_pgt_refcount[l1_idx]++;
		ptr += (1U << ISP_L2PT_OFFSET);
		phys += (1U << ISP_L2PT_OFFSET);
	} while (ptr < end && idx < ISP_L2PT_PTES - 1);

	return 0;
}

/*
 * Update L1 page table according to isp virtual address and page physical
 * address
 */
static int mmu_l1_map(struct isp_mmu *mmu, phys_addr_t l1_pt,
		      unsigned int start, unsigned int end,
		      phys_addr_t phys)
{
	phys_addr_t l2_pt;
	unsigned int ptr, l1_aligned;
	unsigned int idx;
	unsigned int l2_pte;
	int ret;

	l1_pt &= ISP_PAGE_MASK;

	start = start & ISP_PAGE_MASK;
	end = ISP_PAGE_ALIGN(end);
	phys &= ISP_PAGE_MASK;

	ptr = start;
	do {
		idx = ISP_PTR_TO_L1_IDX(ptr);

		l2_pte = atomisp_get_pte(l1_pt, idx);

		if (!ISP_PTE_VALID(mmu, l2_pte)) {
			l2_pt = alloc_page_table(mmu);
			if (l2_pt == NULL_PAGE) {
				dev_err(atomisp_dev,
					     "alloc page table fail.\n");

				/* free all mapped pages */
				free_mmu_map(mmu, start, ptr);

				return -ENOMEM;
			}

			l2_pte = isp_pgaddr_to_pte_valid(mmu, l2_pt);

			atomisp_set_pte(l1_pt, idx, l2_pte);
			mmu->l2_pgt_refcount[idx] = 0;
		}

		l2_pt = isp_pte_to_pgaddr(mmu, l2_pte);

		l1_aligned = (ptr & ISP_PAGE_MASK) + (1U << ISP_L1PT_OFFSET);

		if (l1_aligned < end) {
			ret = mmu_l2_map(mmu, l1_pt, idx,
					   l2_pt, ptr, l1_aligned, phys);
			phys += (l1_aligned - ptr);
			ptr = l1_aligned;
		} else {
			ret = mmu_l2_map(mmu, l1_pt, idx,
					   l2_pt, ptr, end, phys);
			phys += (end - ptr);
			ptr = end;
		}

		if (ret) {
			dev_err(atomisp_dev, "setup mapping in L2PT fail.\n");

			/* free all mapped pages */
			free_mmu_map(mmu, start, ptr);

			return -EINVAL;
		}
	} while (ptr < end && idx < ISP_L1PT_PTES);

	return 0;
}

/*
 * Update page table according to isp virtual address and page physical
 * address
 */
static int mmu_map(struct isp_mmu *mmu, unsigned int isp_virt,
		   phys_addr_t phys, unsigned int pgnr)
{
	unsigned int start, end;
	phys_addr_t l1_pt;
	int ret;

	mutex_lock(&mmu->pt_mutex);
	if (!ISP_PTE_VALID(mmu, mmu->l1_pte)) {
		/*
		 * allocate 1 new page for L1 page table
		 */
		l1_pt = alloc_page_table(mmu);
		if (l1_pt == NULL_PAGE) {
			dev_err(atomisp_dev, "alloc page table fail.\n");
			mutex_unlock(&mmu->pt_mutex);
			return -ENOMEM;
		}

		/*
		 * setup L1 page table physical addr to MMU
		 */
		ret = mmu->driver->set_pd_base(mmu, l1_pt);
		if (ret) {
			dev_err(atomisp_dev,
				 "set page directory base address fail.\n");
			mutex_unlock(&mmu->pt_mutex);
			return ret;
		}
		mmu->base_address = l1_pt;
		mmu->l1_pte = isp_pgaddr_to_pte_valid(mmu, l1_pt);
		memset(mmu->l2_pgt_refcount, 0, sizeof(int) * ISP_L1PT_PTES);
	}

	l1_pt = isp_pte_to_pgaddr(mmu, mmu->l1_pte);

	start = (isp_virt) & ISP_PAGE_MASK;
	end = start + (pgnr << ISP_PAGE_OFFSET);
	phys &= ISP_PAGE_MASK;

	ret = mmu_l1_map(mmu, l1_pt, start, end, phys);

	if (ret)
		dev_err(atomisp_dev, "setup mapping in L1PT fail.\n");

	mutex_unlock(&mmu->pt_mutex);
	return ret;
}

/*
 * Free L2 page table according to isp virtual address and page physical
 * address
 */
static void mmu_l2_unmap(struct isp_mmu *mmu, phys_addr_t l1_pt,
			   unsigned int l1_idx, phys_addr_t l2_pt,
			   unsigned int start, unsigned int end)
{

	unsigned int ptr;
	unsigned int idx;
	unsigned int pte;

	l2_pt &= ISP_PAGE_MASK;

	start = start & ISP_PAGE_MASK;
	end = ISP_PAGE_ALIGN(end);

	ptr = start;
	do {
		idx = ISP_PTR_TO_L2_IDX(ptr);

		pte = atomisp_get_pte(l2_pt, idx);

		if (!ISP_PTE_VALID(mmu, pte))
			mmu_unmap_l2_pte_error(mmu, l1_pt, l1_idx,
						 l2_pt, idx, ptr, pte);

		atomisp_set_pte(l2_pt, idx, mmu->driver->null_pte);
		mmu->l2_pgt_refcount[l1_idx]--;
		ptr += (1U << ISP_L2PT_OFFSET);
	} while (ptr < end && idx < ISP_L2PT_PTES - 1);

	if (mmu->l2_pgt_refcount[l1_idx] == 0) {
		free_page_table(mmu, l2_pt);
		atomisp_set_pte(l1_pt, l1_idx, mmu->driver->null_pte);
	}
}

/*
 * Free L1 page table according to isp virtual address and page physical
 * address
 */
static void mmu_l1_unmap(struct isp_mmu *mmu, phys_addr_t l1_pt,
			   unsigned int start, unsigned int end)
{
	phys_addr_t l2_pt;
	unsigned int ptr, l1_aligned;
	unsigned int idx;
	unsigned int l2_pte;

	l1_pt &= ISP_PAGE_MASK;

	start = start & ISP_PAGE_MASK;
	end = ISP_PAGE_ALIGN(end);

	ptr = start;
	do {
		idx = ISP_PTR_TO_L1_IDX(ptr);

		l2_pte = atomisp_get_pte(l1_pt, idx);

		if (!ISP_PTE_VALID(mmu, l2_pte)) {
			mmu_unmap_l1_pte_error(mmu, l1_pt, idx, ptr, l2_pte);
			continue;
		}

		l2_pt = isp_pte_to_pgaddr(mmu, l2_pte);

		l1_aligned = (ptr & ISP_PAGE_MASK) + (1U << ISP_L1PT_OFFSET);

		if (l1_aligned < end) {
			mmu_l2_unmap(mmu, l1_pt, idx, l2_pt, ptr, l1_aligned);
			ptr = l1_aligned;
		} else {
			mmu_l2_unmap(mmu, l1_pt, idx, l2_pt, ptr, end);
			ptr = end;
		}
		/*
		 * use the same L2 page next time, so we dont
		 * need to invalidate and free this PT.
		 */
		/*      atomisp_set_pte(l1_pt, idx, NULL_PTE); */
	} while (ptr < end && idx < ISP_L1PT_PTES);
}

/*
 * Free page table according to isp virtual address and page physical
 * address
 */
static void mmu_unmap(struct isp_mmu *mmu, unsigned int isp_virt,
			unsigned int pgnr)
{
	unsigned int start, end;
	phys_addr_t l1_pt;

	mutex_lock(&mmu->pt_mutex);
	if (!ISP_PTE_VALID(mmu, mmu->l1_pte)) {
		mmu_unmap_l1_pt_error(mmu, mmu->l1_pte);
		mutex_unlock(&mmu->pt_mutex);
		return;
	}

	l1_pt = isp_pte_to_pgaddr(mmu, mmu->l1_pte);

	start = (isp_virt) & ISP_PAGE_MASK;
	end = start + (pgnr << ISP_PAGE_OFFSET);

	mmu_l1_unmap(mmu, l1_pt, start, end);
	mutex_unlock(&mmu->pt_mutex);
}

/*
 * Free page tables according to isp start virtual address and end virtual
 * address.
 */
static void free_mmu_map(struct isp_mmu *mmu, unsigned int start_isp_virt,
				unsigned int end_isp_virt)
{
	unsigned int pgnr;
	unsigned int start, end;

	start = (start_isp_virt) & ISP_PAGE_MASK;
	end = (end_isp_virt) & ISP_PAGE_MASK;
	pgnr = (end - start) >> ISP_PAGE_OFFSET;
	mmu_unmap(mmu, start, pgnr);
}

int isp_mmu_map(struct isp_mmu *mmu, unsigned int isp_virt,
		phys_addr_t phys, unsigned int pgnr)
{
	return mmu_map(mmu, isp_virt, phys, pgnr);
}

void isp_mmu_unmap(struct isp_mmu *mmu, unsigned int isp_virt,
		   unsigned int pgnr)
{
	mmu_unmap(mmu, isp_virt, pgnr);
}

static void isp_mmu_flush_tlb_range_default(struct isp_mmu *mmu,
					      unsigned int start,
					      unsigned int size)
{
	isp_mmu_flush_tlb(mmu);
}

/*MMU init for internal structure*/
int isp_mmu_init(struct isp_mmu *mmu, struct isp_mmu_client *driver)
{
	if (!mmu)		/* error */
		return -EINVAL;
	if (!driver)		/* error */
		return -EINVAL;

	if (!driver->name)
		dev_warn(atomisp_dev, "NULL name for MMU driver...\n");

	mmu->driver = driver;

	if (!driver->set_pd_base || !driver->tlb_flush_all) {
		dev_err(atomisp_dev,
			    "set_pd_base or tlb_flush_all operation "
			     "not provided.\n");
		return -EINVAL;
	}

	if (!driver->tlb_flush_range)
		driver->tlb_flush_range = isp_mmu_flush_tlb_range_default;

	if (!driver->pte_valid_mask) {
		dev_err(atomisp_dev, "PTE_MASK is missing from mmu driver\n");
		return -EINVAL;
	}

	mmu->l1_pte = driver->null_pte;

	mutex_init(&mmu->pt_mutex);

	mmu->tbl_cache = kmem_cache_create("iopte_cache", ISP_PAGE_SIZE,
					   ISP_PAGE_SIZE, SLAB_HWCACHE_ALIGN,
					   NULL);
	if (!mmu->tbl_cache)
		return -ENOMEM;

	return 0;
}

/*Free L1 and L2 page table*/
void isp_mmu_exit(struct isp_mmu *mmu)
{
	unsigned int idx;
	unsigned int pte;
	phys_addr_t l1_pt, l2_pt;

	if (!mmu)
		return;

	if (!ISP_PTE_VALID(mmu, mmu->l1_pte)) {
		dev_warn(atomisp_dev, "invalid L1PT: pte = 0x%x\n",
			    (unsigned int)mmu->l1_pte);
		return;
	}

	l1_pt = isp_pte_to_pgaddr(mmu, mmu->l1_pte);

	for (idx = 0; idx < ISP_L1PT_PTES; idx++) {
		pte = atomisp_get_pte(l1_pt, idx);

		if (ISP_PTE_VALID(mmu, pte)) {
			l2_pt = isp_pte_to_pgaddr(mmu, pte);

			free_page_table(mmu, l2_pt);
		}
	}

	free_page_table(mmu, l1_pt);

	kmem_cache_destroy(mmu->tbl_cache);
}

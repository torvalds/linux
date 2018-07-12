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
 * ISP MMU driver for classic two-level page tables
 */
#ifndef	__ISP_MMU_H__
#define	__ISP_MMU_H__

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/slab.h>

/*
 * do not change these values, the page size for ISP must be the
 * same as kernel's page size.
 */
#define	ISP_PAGE_OFFSET		12
#define	ISP_PAGE_SIZE		(1U << ISP_PAGE_OFFSET)
#define	ISP_PAGE_MASK		(~(phys_addr_t)(ISP_PAGE_SIZE - 1))

#define	ISP_L1PT_OFFSET		22
#define	ISP_L1PT_MASK		(~((1U << ISP_L1PT_OFFSET) - 1))

#define	ISP_L2PT_OFFSET		12
#define	ISP_L2PT_MASK		(~(ISP_L1PT_MASK|(~(ISP_PAGE_MASK))))

#define	ISP_L1PT_PTES		1024
#define	ISP_L2PT_PTES		1024

#define	ISP_PTR_TO_L1_IDX(x)	((((x) & ISP_L1PT_MASK)) \
					>> ISP_L1PT_OFFSET)

#define	ISP_PTR_TO_L2_IDX(x)	((((x) & ISP_L2PT_MASK)) \
					>> ISP_L2PT_OFFSET)

#define	ISP_PAGE_ALIGN(x)	(((x) + (ISP_PAGE_SIZE-1)) \
					& ISP_PAGE_MASK)

#define	ISP_PT_TO_VIRT(l1_idx, l2_idx, offset) do {\
		((l1_idx) << ISP_L1PT_OFFSET) | \
		((l2_idx) << ISP_L2PT_OFFSET) | \
		(offset)\
} while (0)

#define	pgnr_to_size(pgnr)	((pgnr) << ISP_PAGE_OFFSET)
#define	size_to_pgnr_ceil(size)	(((size) + (1 << ISP_PAGE_OFFSET) - 1)\
						>> ISP_PAGE_OFFSET)
#define	size_to_pgnr_bottom(size)	((size) >> ISP_PAGE_OFFSET)

struct isp_mmu;

struct isp_mmu_client {
	/*
	 * const value
	 *
	 * @name:
	 *      driver name
	 * @pte_valid_mask:
	 *      should be 1 bit valid data, meaning the value should
	 *      be power of 2.
	 */
	char *name;
	unsigned int pte_valid_mask;
	unsigned int null_pte;

	/*
	 * get page directory base address (physical address).
	 *
	 * must be provided.
	 */
	unsigned int (*get_pd_base) (struct isp_mmu *mmu, phys_addr_t pd_base);
	/*
	 * callback to flush tlb.
	 *
	 * tlb_flush_range will at least flush TLBs containing
	 * address mapping from addr to addr + size.
	 *
	 * tlb_flush_all will flush all TLBs.
	 *
	 * tlb_flush_all is must be provided. if tlb_flush_range is
	 * not valid, it will set to tlb_flush_all by default.
	 */
	void (*tlb_flush_range) (struct isp_mmu *mmu,
				 unsigned int addr, unsigned int size);
	void (*tlb_flush_all) (struct isp_mmu *mmu);
	unsigned int (*phys_to_pte) (struct isp_mmu *mmu,
				     phys_addr_t phys);
	phys_addr_t (*pte_to_phys) (struct isp_mmu *mmu,
				    unsigned int pte);

};

struct isp_mmu {
	struct isp_mmu_client *driver;
	unsigned int l1_pte;
	int l2_pgt_refcount[ISP_L1PT_PTES];
	phys_addr_t base_address;

	struct mutex pt_mutex;
	struct kmem_cache *tbl_cache;
};

/* flags for PDE and PTE */
#define	ISP_PTE_VALID_MASK(mmu)	\
	((mmu)->driver->pte_valid_mask)

#define	ISP_PTE_VALID(mmu, pte)	\
	((pte) & ISP_PTE_VALID_MASK(mmu))

#define	NULL_PAGE	((phys_addr_t)(-1) & ISP_PAGE_MASK)
#define	PAGE_VALID(page)	((page) != NULL_PAGE)

/*
 * init mmu with specific mmu driver.
 */
int isp_mmu_init(struct isp_mmu *mmu, struct isp_mmu_client *driver);
/*
 * cleanup all mmu related things.
 */
void isp_mmu_exit(struct isp_mmu *mmu);

/*
 * setup/remove address mapping for pgnr continous physical pages
 * and isp_virt.
 *
 * map/unmap is mutex lock protected, and caller does not have
 * to do lock/unlock operation.
 *
 * map/unmap will not flush tlb, and caller needs to deal with
 * this itself.
 */
int isp_mmu_map(struct isp_mmu *mmu, unsigned int isp_virt,
		phys_addr_t phys, unsigned int pgnr);

void isp_mmu_unmap(struct isp_mmu *mmu, unsigned int isp_virt,
		   unsigned int pgnr);

static inline void isp_mmu_flush_tlb_all(struct isp_mmu *mmu)
{
	if (mmu->driver && mmu->driver->tlb_flush_all)
		mmu->driver->tlb_flush_all(mmu);
}

#define isp_mmu_flush_tlb isp_mmu_flush_tlb_all

static inline void isp_mmu_flush_tlb_range(struct isp_mmu *mmu,
		unsigned int start, unsigned int size)
{
	if (mmu->driver && mmu->driver->tlb_flush_range)
		mmu->driver->tlb_flush_range(mmu, start, size);
}

#endif /* ISP_MMU_H_ */

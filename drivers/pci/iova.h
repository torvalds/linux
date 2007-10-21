/*
 * Copyright (c) 2006, Intel Corporation.
 *
 * This file is released under the GPLv2.
 *
 * Copyright (C) 2006 Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *
 */

#ifndef _IOVA_H_
#define _IOVA_H_

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/rbtree.h>
#include <linux/dma-mapping.h>

/*
 * We need a fixed PAGE_SIZE of 4K irrespective of
 * arch PAGE_SIZE for IOMMU page tables.
 */
#define PAGE_SHIFT_4K		(12)
#define PAGE_SIZE_4K		(1UL << PAGE_SHIFT_4K)
#define PAGE_MASK_4K		(((u64)-1) << PAGE_SHIFT_4K)
#define PAGE_ALIGN_4K(addr)	(((addr) + PAGE_SIZE_4K - 1) & PAGE_MASK_4K)

/* IO virtual address start page frame number */
#define IOVA_START_PFN		(1)

#define IOVA_PFN(addr)		((addr) >> PAGE_SHIFT_4K)
#define DMA_32BIT_PFN	IOVA_PFN(DMA_32BIT_MASK)
#define DMA_64BIT_PFN	IOVA_PFN(DMA_64BIT_MASK)

/* iova structure */
struct iova {
	struct rb_node	node;
	unsigned long	pfn_hi; /* IOMMU dish out addr hi */
	unsigned long	pfn_lo; /* IOMMU dish out addr lo */
};

/* holds all the iova translations for a domain */
struct iova_domain {
	spinlock_t	iova_alloc_lock;/* Lock to protect iova  allocation */
	spinlock_t	iova_rbtree_lock; /* Lock to protect update of rbtree */
	struct rb_root	rbroot;		/* iova domain rbtree root */
	struct rb_node	*cached32_node; /* Save last alloced node */
};

struct iova *alloc_iova_mem(void);
void free_iova_mem(struct iova *iova);
void free_iova(struct iova_domain *iovad, unsigned long pfn);
void __free_iova(struct iova_domain *iovad, struct iova *iova);
struct iova *alloc_iova(struct iova_domain *iovad, unsigned long size,
	unsigned long limit_pfn,
	bool size_aligned);
struct iova *reserve_iova(struct iova_domain *iovad, unsigned long pfn_lo,
	unsigned long pfn_hi);
void copy_reserved_iova(struct iova_domain *from, struct iova_domain *to);
void init_iova_domain(struct iova_domain *iovad);
struct iova *find_iova(struct iova_domain *iovad, unsigned long pfn);
void put_iova_domain(struct iova_domain *iovad);

#endif

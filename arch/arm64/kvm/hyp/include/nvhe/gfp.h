/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_HYP_GFP_H
#define __KVM_HYP_GFP_H

#include <linux/list.h>

#include <nvhe/memory.h>
#include <nvhe/spinlock.h>

#define HYP_NO_ORDER	USHRT_MAX

struct hyp_pool {
	/*
	 * Spinlock protecting concurrent changes to the memory pool as well as
	 * the struct hyp_page of the pool's pages until we have a proper atomic
	 * API at EL2.
	 */
	hyp_spinlock_t lock;
	struct list_head free_area[MAX_ORDER];
	phys_addr_t range_start;
	phys_addr_t range_end;
	unsigned short max_order;
};

/* Allocation */
void *hyp_alloc_pages(struct hyp_pool *pool, unsigned short order);
void hyp_split_page(struct hyp_page *page);
void hyp_get_page(struct hyp_pool *pool, void *addr);
void hyp_put_page(struct hyp_pool *pool, void *addr);

/* Used pages cannot be freed */
int hyp_pool_init(struct hyp_pool *pool, u64 pfn, unsigned int nr_pages,
		  unsigned int reserved_pages);
#endif /* __KVM_HYP_GFP_H */

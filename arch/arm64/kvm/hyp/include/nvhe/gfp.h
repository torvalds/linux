/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_HYP_GFP_H
#define __KVM_HYP_GFP_H

#include <linux/list.h>

#include <nvhe/memory.h>
#include <nvhe/spinlock.h>

#define HYP_NO_ORDER	UINT_MAX

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
	unsigned int max_order;
};

static inline void hyp_page_ref_inc(struct hyp_page *p)
{
	struct hyp_pool *pool = hyp_page_to_pool(p);

	hyp_spin_lock(&pool->lock);
	p->refcount++;
	hyp_spin_unlock(&pool->lock);
}

static inline int hyp_page_ref_dec_and_test(struct hyp_page *p)
{
	struct hyp_pool *pool = hyp_page_to_pool(p);
	int ret;

	hyp_spin_lock(&pool->lock);
	p->refcount--;
	ret = (p->refcount == 0);
	hyp_spin_unlock(&pool->lock);

	return ret;
}

static inline void hyp_set_page_refcounted(struct hyp_page *p)
{
	struct hyp_pool *pool = hyp_page_to_pool(p);

	hyp_spin_lock(&pool->lock);
	if (p->refcount) {
		hyp_spin_unlock(&pool->lock);
		hyp_panic();
	}
	p->refcount = 1;
	hyp_spin_unlock(&pool->lock);
}

/* Allocation */
void *hyp_alloc_pages(struct hyp_pool *pool, unsigned int order);
void hyp_get_page(void *addr);
void hyp_put_page(void *addr);

/* Used pages cannot be freed */
int hyp_pool_init(struct hyp_pool *pool, u64 pfn, unsigned int nr_pages,
		  unsigned int reserved_pages);
#endif /* __KVM_HYP_GFP_H */

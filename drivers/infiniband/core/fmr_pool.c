/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/jhash.h>
#include <linux/kthread.h>

#include <rdma/ib_fmr_pool.h>

#include "core_priv.h"

#define PFX "fmr_pool: "

enum {
	IB_FMR_MAX_REMAPS = 32,

	IB_FMR_HASH_BITS  = 8,
	IB_FMR_HASH_SIZE  = 1 << IB_FMR_HASH_BITS,
	IB_FMR_HASH_MASK  = IB_FMR_HASH_SIZE - 1
};

/*
 * If an FMR is not in use, then the list member will point to either
 * its pool's free_list (if the FMR can be mapped again; that is,
 * remap_count < pool->max_remaps) or its pool's dirty_list (if the
 * FMR needs to be unmapped before being remapped).  In either of
 * these cases it is a bug if the ref_count is not 0.  In other words,
 * if ref_count is > 0, then the list member must not be linked into
 * either free_list or dirty_list.
 *
 * The cache_node member is used to link the FMR into a cache bucket
 * (if caching is enabled).  This is independent of the reference
 * count of the FMR.  When a valid FMR is released, its ref_count is
 * decremented, and if ref_count reaches 0, the FMR is placed in
 * either free_list or dirty_list as appropriate.  However, it is not
 * removed from the cache and may be "revived" if a call to
 * ib_fmr_register_physical() occurs before the FMR is remapped.  In
 * this case we just increment the ref_count and remove the FMR from
 * free_list/dirty_list.
 *
 * Before we remap an FMR from free_list, we remove it from the cache
 * (to prevent another user from obtaining a stale FMR).  When an FMR
 * is released, we add it to the tail of the free list, so that our
 * cache eviction policy is "least recently used."
 *
 * All manipulation of ref_count, list and cache_node is protected by
 * pool_lock to maintain consistency.
 */

struct ib_fmr_pool {
	spinlock_t                pool_lock;

	int                       pool_size;
	int                       max_pages;
	int			  max_remaps;
	int                       dirty_watermark;
	int                       dirty_len;
	struct list_head          free_list;
	struct list_head          dirty_list;
	struct hlist_head        *cache_bucket;

	void                     (*flush_function)(struct ib_fmr_pool *pool,
						   void *              arg);
	void                     *flush_arg;

	struct kthread_worker	  *worker;
	struct kthread_work	  work;

	atomic_t                  req_ser;
	atomic_t                  flush_ser;

	wait_queue_head_t         force_wait;
};

static inline u32 ib_fmr_hash(u64 first_page)
{
	return jhash_2words((u32) first_page, (u32) (first_page >> 32), 0) &
		(IB_FMR_HASH_SIZE - 1);
}

/* Caller must hold pool_lock */
static inline struct ib_pool_fmr *ib_fmr_cache_lookup(struct ib_fmr_pool *pool,
						      u64 *page_list,
						      int  page_list_len,
						      u64  io_virtual_address)
{
	struct hlist_head *bucket;
	struct ib_pool_fmr *fmr;

	if (!pool->cache_bucket)
		return NULL;

	bucket = pool->cache_bucket + ib_fmr_hash(*page_list);

	hlist_for_each_entry(fmr, bucket, cache_node)
		if (io_virtual_address == fmr->io_virtual_address &&
		    page_list_len      == fmr->page_list_len      &&
		    !memcmp(page_list, fmr->page_list,
			    page_list_len * sizeof *page_list))
			return fmr;

	return NULL;
}

static void ib_fmr_batch_release(struct ib_fmr_pool *pool)
{
	int                 ret;
	struct ib_pool_fmr *fmr;
	LIST_HEAD(unmap_list);
	LIST_HEAD(fmr_list);

	spin_lock_irq(&pool->pool_lock);

	list_for_each_entry(fmr, &pool->dirty_list, list) {
		hlist_del_init(&fmr->cache_node);
		fmr->remap_count = 0;
		list_add_tail(&fmr->fmr->list, &fmr_list);

#ifdef DEBUG
		if (fmr->ref_count !=0) {
			pr_warn(PFX "Unmapping FMR 0x%08x with ref count %d\n",
				fmr, fmr->ref_count);
		}
#endif
	}

	list_splice_init(&pool->dirty_list, &unmap_list);
	pool->dirty_len = 0;

	spin_unlock_irq(&pool->pool_lock);

	if (list_empty(&unmap_list)) {
		return;
	}

	ret = ib_unmap_fmr(&fmr_list);
	if (ret)
		pr_warn(PFX "ib_unmap_fmr returned %d\n", ret);

	spin_lock_irq(&pool->pool_lock);
	list_splice(&unmap_list, &pool->free_list);
	spin_unlock_irq(&pool->pool_lock);
}

static void ib_fmr_cleanup_func(struct kthread_work *work)
{
	struct ib_fmr_pool *pool = container_of(work, struct ib_fmr_pool, work);

	ib_fmr_batch_release(pool);
	atomic_inc(&pool->flush_ser);
	wake_up_interruptible(&pool->force_wait);

	if (pool->flush_function)
		pool->flush_function(pool, pool->flush_arg);

	if (atomic_read(&pool->flush_ser) - atomic_read(&pool->req_ser) < 0)
		kthread_queue_work(pool->worker, &pool->work);
}

/**
 * ib_create_fmr_pool - Create an FMR pool
 * @pd:Protection domain for FMRs
 * @params:FMR pool parameters
 *
 * Create a pool of FMRs.  Return value is pointer to new pool or
 * error code if creation failed.
 */
struct ib_fmr_pool *ib_create_fmr_pool(struct ib_pd             *pd,
				       struct ib_fmr_pool_param *params)
{
	struct ib_device   *device;
	struct ib_fmr_pool *pool;
	int i;
	int ret;
	int max_remaps;

	if (!params)
		return ERR_PTR(-EINVAL);

	device = pd->device;
	if (!device->ops.alloc_fmr    || !device->ops.dealloc_fmr  ||
	    !device->ops.map_phys_fmr || !device->ops.unmap_fmr) {
		dev_info(&device->dev, "Device does not support FMRs\n");
		return ERR_PTR(-ENOSYS);
	}

	if (!device->attrs.max_map_per_fmr)
		max_remaps = IB_FMR_MAX_REMAPS;
	else
		max_remaps = device->attrs.max_map_per_fmr;

	pool = kmalloc(sizeof *pool, GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	pool->cache_bucket   = NULL;
	pool->flush_function = params->flush_function;
	pool->flush_arg      = params->flush_arg;

	INIT_LIST_HEAD(&pool->free_list);
	INIT_LIST_HEAD(&pool->dirty_list);

	if (params->cache) {
		pool->cache_bucket =
			kmalloc_array(IB_FMR_HASH_SIZE,
				      sizeof(*pool->cache_bucket),
				      GFP_KERNEL);
		if (!pool->cache_bucket) {
			ret = -ENOMEM;
			goto out_free_pool;
		}

		for (i = 0; i < IB_FMR_HASH_SIZE; ++i)
			INIT_HLIST_HEAD(pool->cache_bucket + i);
	}

	pool->pool_size       = 0;
	pool->max_pages       = params->max_pages_per_fmr;
	pool->max_remaps      = max_remaps;
	pool->dirty_watermark = params->dirty_watermark;
	pool->dirty_len       = 0;
	spin_lock_init(&pool->pool_lock);
	atomic_set(&pool->req_ser,   0);
	atomic_set(&pool->flush_ser, 0);
	init_waitqueue_head(&pool->force_wait);

	pool->worker =
		kthread_create_worker(0, "ib_fmr(%s)", dev_name(&device->dev));
	if (IS_ERR(pool->worker)) {
		pr_warn(PFX "couldn't start cleanup kthread worker\n");
		ret = PTR_ERR(pool->worker);
		goto out_free_pool;
	}
	kthread_init_work(&pool->work, ib_fmr_cleanup_func);

	{
		struct ib_pool_fmr *fmr;
		struct ib_fmr_attr fmr_attr = {
			.max_pages  = params->max_pages_per_fmr,
			.max_maps   = pool->max_remaps,
			.page_shift = params->page_shift
		};
		int bytes_per_fmr = sizeof *fmr;

		if (pool->cache_bucket)
			bytes_per_fmr += params->max_pages_per_fmr * sizeof (u64);

		for (i = 0; i < params->pool_size; ++i) {
			fmr = kmalloc(bytes_per_fmr, GFP_KERNEL);
			if (!fmr)
				goto out_fail;

			fmr->pool             = pool;
			fmr->remap_count      = 0;
			fmr->ref_count        = 0;
			INIT_HLIST_NODE(&fmr->cache_node);

			fmr->fmr = ib_alloc_fmr(pd, params->access, &fmr_attr);
			if (IS_ERR(fmr->fmr)) {
				pr_warn(PFX "fmr_create failed for FMR %d\n",
					i);
				kfree(fmr);
				goto out_fail;
			}

			list_add_tail(&fmr->list, &pool->free_list);
			++pool->pool_size;
		}
	}

	return pool;

 out_free_pool:
	kfree(pool->cache_bucket);
	kfree(pool);

	return ERR_PTR(ret);

 out_fail:
	ib_destroy_fmr_pool(pool);

	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL(ib_create_fmr_pool);

/**
 * ib_destroy_fmr_pool - Free FMR pool
 * @pool:FMR pool to free
 *
 * Destroy an FMR pool and free all associated resources.
 */
void ib_destroy_fmr_pool(struct ib_fmr_pool *pool)
{
	struct ib_pool_fmr *fmr;
	struct ib_pool_fmr *tmp;
	LIST_HEAD(fmr_list);
	int                 i;

	kthread_destroy_worker(pool->worker);
	ib_fmr_batch_release(pool);

	i = 0;
	list_for_each_entry_safe(fmr, tmp, &pool->free_list, list) {
		if (fmr->remap_count) {
			INIT_LIST_HEAD(&fmr_list);
			list_add_tail(&fmr->fmr->list, &fmr_list);
			ib_unmap_fmr(&fmr_list);
		}
		ib_dealloc_fmr(fmr->fmr);
		list_del(&fmr->list);
		kfree(fmr);
		++i;
	}

	if (i < pool->pool_size)
		pr_warn(PFX "pool still has %d regions registered\n",
			pool->pool_size - i);

	kfree(pool->cache_bucket);
	kfree(pool);
}
EXPORT_SYMBOL(ib_destroy_fmr_pool);

/**
 * ib_flush_fmr_pool - Invalidate all unmapped FMRs
 * @pool:FMR pool to flush
 *
 * Ensure that all unmapped FMRs are fully invalidated.
 */
int ib_flush_fmr_pool(struct ib_fmr_pool *pool)
{
	int serial;
	struct ib_pool_fmr *fmr, *next;

	/*
	 * The free_list holds FMRs that may have been used
	 * but have not been remapped enough times to be dirty.
	 * Put them on the dirty list now so that the cleanup
	 * thread will reap them too.
	 */
	spin_lock_irq(&pool->pool_lock);
	list_for_each_entry_safe(fmr, next, &pool->free_list, list) {
		if (fmr->remap_count > 0)
			list_move(&fmr->list, &pool->dirty_list);
	}
	spin_unlock_irq(&pool->pool_lock);

	serial = atomic_inc_return(&pool->req_ser);
	kthread_queue_work(pool->worker, &pool->work);

	if (wait_event_interruptible(pool->force_wait,
				     atomic_read(&pool->flush_ser) - serial >= 0))
		return -EINTR;

	return 0;
}
EXPORT_SYMBOL(ib_flush_fmr_pool);

/**
 * ib_fmr_pool_map_phys - Map an FMR from an FMR pool.
 * @pool_handle: FMR pool to allocate FMR from
 * @page_list: List of pages to map
 * @list_len: Number of pages in @page_list
 * @io_virtual_address: I/O virtual address for new FMR
 */
struct ib_pool_fmr *ib_fmr_pool_map_phys(struct ib_fmr_pool *pool_handle,
					 u64                *page_list,
					 int                 list_len,
					 u64                 io_virtual_address)
{
	struct ib_fmr_pool *pool = pool_handle;
	struct ib_pool_fmr *fmr;
	unsigned long       flags;
	int                 result;

	if (list_len < 1 || list_len > pool->max_pages)
		return ERR_PTR(-EINVAL);

	spin_lock_irqsave(&pool->pool_lock, flags);
	fmr = ib_fmr_cache_lookup(pool,
				  page_list,
				  list_len,
				  io_virtual_address);
	if (fmr) {
		/* found in cache */
		++fmr->ref_count;
		if (fmr->ref_count == 1) {
			list_del(&fmr->list);
		}

		spin_unlock_irqrestore(&pool->pool_lock, flags);

		return fmr;
	}

	if (list_empty(&pool->free_list)) {
		spin_unlock_irqrestore(&pool->pool_lock, flags);
		return ERR_PTR(-EAGAIN);
	}

	fmr = list_entry(pool->free_list.next, struct ib_pool_fmr, list);
	list_del(&fmr->list);
	hlist_del_init(&fmr->cache_node);
	spin_unlock_irqrestore(&pool->pool_lock, flags);

	result = ib_map_phys_fmr(fmr->fmr, page_list, list_len,
				 io_virtual_address);

	if (result) {
		spin_lock_irqsave(&pool->pool_lock, flags);
		list_add(&fmr->list, &pool->free_list);
		spin_unlock_irqrestore(&pool->pool_lock, flags);

		pr_warn(PFX "fmr_map returns %d\n", result);

		return ERR_PTR(result);
	}

	++fmr->remap_count;
	fmr->ref_count = 1;

	if (pool->cache_bucket) {
		fmr->io_virtual_address = io_virtual_address;
		fmr->page_list_len      = list_len;
		memcpy(fmr->page_list, page_list, list_len * sizeof(*page_list));

		spin_lock_irqsave(&pool->pool_lock, flags);
		hlist_add_head(&fmr->cache_node,
			       pool->cache_bucket + ib_fmr_hash(fmr->page_list[0]));
		spin_unlock_irqrestore(&pool->pool_lock, flags);
	}

	return fmr;
}
EXPORT_SYMBOL(ib_fmr_pool_map_phys);

/**
 * ib_fmr_pool_unmap - Unmap FMR
 * @fmr:FMR to unmap
 *
 * Unmap an FMR.  The FMR mapping may remain valid until the FMR is
 * reused (or until ib_flush_fmr_pool() is called).
 */
void ib_fmr_pool_unmap(struct ib_pool_fmr *fmr)
{
	struct ib_fmr_pool *pool;
	unsigned long flags;

	pool = fmr->pool;

	spin_lock_irqsave(&pool->pool_lock, flags);

	--fmr->ref_count;
	if (!fmr->ref_count) {
		if (fmr->remap_count < pool->max_remaps) {
			list_add_tail(&fmr->list, &pool->free_list);
		} else {
			list_add_tail(&fmr->list, &pool->dirty_list);
			if (++pool->dirty_len >= pool->dirty_watermark) {
				atomic_inc(&pool->req_ser);
				kthread_queue_work(pool->worker, &pool->work);
			}
		}
	}

#ifdef DEBUG
	if (fmr->ref_count < 0)
		pr_warn(PFX "FMR %p has ref count %d < 0\n",
			fmr, fmr->ref_count);
#endif

	spin_unlock_irqrestore(&pool->pool_lock, flags);
}
EXPORT_SYMBOL(ib_fmr_pool_unmap);

// SPDX-License-Identifier: GPL-2.0  OR Linux-OpenIB
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/sort.h>
#include <linux/spinlock.h>
#include <rdma/ib_verbs.h>
#include <linux/timer.h>

#include "frmr_pools.h"

#define FRMR_POOLS_DEFAULT_AGING_PERIOD_SECS 60

static int push_handle_to_queue_locked(struct frmr_queue *queue, u32 handle)
{
	u32 tmp = queue->ci % NUM_HANDLES_PER_PAGE;
	struct frmr_handles_page *page;

	if (queue->ci >= queue->num_pages * NUM_HANDLES_PER_PAGE) {
		page = kzalloc_obj(*page, GFP_ATOMIC);
		if (!page)
			return -ENOMEM;
		queue->num_pages++;
		list_add_tail(&page->list, &queue->pages_list);
	} else {
		page = list_last_entry(&queue->pages_list,
				       struct frmr_handles_page, list);
	}

	page->handles[tmp] = handle;
	queue->ci++;
	return 0;
}

static u32 pop_handle_from_queue_locked(struct frmr_queue *queue)
{
	u32 tmp = (queue->ci - 1) % NUM_HANDLES_PER_PAGE;
	struct frmr_handles_page *page;
	u32 handle;

	page = list_last_entry(&queue->pages_list, struct frmr_handles_page,
			       list);
	handle = page->handles[tmp];
	queue->ci--;

	if (!tmp) {
		list_del(&page->list);
		queue->num_pages--;
		kfree(page);
	}

	return handle;
}

static bool pop_frmr_handles_page(struct ib_frmr_pool *pool,
				  struct frmr_queue *queue,
				  struct frmr_handles_page **page, u32 *count)
{
	spin_lock(&pool->lock);
	if (list_empty(&queue->pages_list)) {
		spin_unlock(&pool->lock);
		return false;
	}

	*page = list_first_entry(&queue->pages_list, struct frmr_handles_page,
				 list);
	list_del(&(*page)->list);
	queue->num_pages--;

	/* If this is the last page, count may be less than
	 * NUM_HANDLES_PER_PAGE.
	 */
	if (queue->ci >= NUM_HANDLES_PER_PAGE)
		*count = NUM_HANDLES_PER_PAGE;
	else
		*count = queue->ci;

	queue->ci -= *count;
	spin_unlock(&pool->lock);
	return true;
}

static void destroy_all_handles_in_queue(struct ib_device *device,
					 struct ib_frmr_pool *pool,
					 struct frmr_queue *queue)
{
	struct ib_frmr_pools *pools = device->frmr_pools;
	struct frmr_handles_page *page;
	u32 count;

	while (pop_frmr_handles_page(pool, queue, &page, &count)) {
		pools->pool_ops->destroy_frmrs(device, page->handles, count);
		kfree(page);
	}
}

/*
 * Bulk-move all handles from @src into @dst without allocating new pages.
 * If @dst has a partial tail page, fill it handle-by-handle from @src first
 * to preserve the invariant that only the tail page is partial, then splice
 * the remaining @src pages onto @dst. On return @src is empty.
 *
 * Caller must hold the lock protecting both queues.
 */
static void splice_frmr_queue_locked(struct frmr_queue *dst,
				     struct frmr_queue *src)
{
	u32 free_in_tail = dst->ci % NUM_HANDLES_PER_PAGE;
	u32 handle;

	if (free_in_tail) {
		free_in_tail = NUM_HANDLES_PER_PAGE - free_in_tail;
		while (free_in_tail && src->ci) {
			handle = pop_handle_from_queue_locked(src);
			push_handle_to_queue_locked(dst, handle);
			free_in_tail--;
		}
	}

	if (src->ci > 0) {
		list_splice_tail_init(&src->pages_list, &dst->pages_list);
		dst->num_pages += src->num_pages;
		dst->ci += src->ci;
		src->num_pages = 0;
		src->ci = 0;
	}
}

static bool age_pinned_pool(struct ib_device *device, struct ib_frmr_pool *pool)
{
	struct ib_frmr_pools *pools = device->frmr_pools;
	u32 total, to_destroy, destroyed = 0;
	bool has_work = false;
	u32 *handles;

	spin_lock(&pool->lock);
	total = pool->queue.ci + pool->inactive_queue.ci + pool->in_use;
	if (total <= pool->pinned_handles) {
		spin_unlock(&pool->lock);
		return false;
	}

	to_destroy = min(total - pool->pinned_handles, pool->inactive_queue.ci);

	handles = kcalloc(to_destroy, sizeof(*handles), GFP_ATOMIC);
	if (!handles) {
		spin_unlock(&pool->lock);
		return true;
	}

	/* Destroy all excess handles in the inactive queue */
	for (; destroyed < to_destroy; destroyed++)
		handles[destroyed] = pop_handle_from_queue_locked(
			&pool->inactive_queue);

	/* Move all handles from regular queue to inactive queue */
	if (pool->queue.ci > 0) {
		splice_frmr_queue_locked(&pool->inactive_queue, &pool->queue);
		has_work = true;
	}

	spin_unlock(&pool->lock);

	if (destroyed)
		pools->pool_ops->destroy_frmrs(device, handles, destroyed);
	kfree(handles);
	return has_work;
}

static void pool_aging_work(struct work_struct *work)
{
	struct ib_frmr_pool *pool = container_of(
		to_delayed_work(work), struct ib_frmr_pool, aging_work);
	struct ib_frmr_pools *pools = pool->device->frmr_pools;
	bool has_work = false;

	if (pool->pinned_handles) {
		has_work = age_pinned_pool(pool->device, pool);
		goto out;
	}

	destroy_all_handles_in_queue(pool->device, pool, &pool->inactive_queue);

	/* Move all pages from regular queue to inactive queue */
	spin_lock(&pool->lock);
	if (pool->queue.ci > 0) {
		splice_frmr_queue_locked(&pool->inactive_queue, &pool->queue);
		has_work = true;
	}
	spin_unlock(&pool->lock);

out:
	/* Reschedule if there are handles to age in next aging period */
	if (has_work)
		queue_delayed_work(
			pools->aging_wq, &pool->aging_work,
			secs_to_jiffies(READ_ONCE(pools->aging_period_sec)));
}

static void destroy_frmr_pool(struct ib_device *device,
			      struct ib_frmr_pool *pool)
{
	cancel_delayed_work_sync(&pool->aging_work);
	destroy_all_handles_in_queue(device, pool, &pool->queue);
	destroy_all_handles_in_queue(device, pool, &pool->inactive_queue);

	kfree(pool);
}

/*
 * Initialize the FRMR pools for a device.
 *
 * @device: The device to initialize the FRMR pools for.
 * @pool_ops: The pool operations to use.
 *
 * Returns 0 on success, negative error code on failure.
 */
int ib_frmr_pools_init(struct ib_device *device,
		       const struct ib_frmr_pool_ops *pool_ops)
{
	struct ib_frmr_pools *pools;

	pools = kzalloc_obj(*pools);
	if (!pools)
		return -ENOMEM;

	pools->rb_root = RB_ROOT;
	rwlock_init(&pools->rb_lock);
	pools->pool_ops = pool_ops;
	pools->aging_wq = create_singlethread_workqueue("frmr_aging_wq");
	if (!pools->aging_wq) {
		kfree(pools);
		return -ENOMEM;
	}

	pools->aging_period_sec = FRMR_POOLS_DEFAULT_AGING_PERIOD_SECS;

	device->frmr_pools = pools;
	return 0;
}
EXPORT_SYMBOL(ib_frmr_pools_init);

/*
 * Clean up the FRMR pools for a device.
 *
 * @device: The device to clean up the FRMR pools for.
 *
 * Call cleanup only after all FRMR handles have been pushed back to the pool
 * and no other FRMR operations are allowed to run in parallel.
 * Ensuring this allows us to save synchronization overhead in pop and push
 * operations.
 */
void ib_frmr_pools_cleanup(struct ib_device *device)
{
	struct ib_frmr_pools *pools = device->frmr_pools;
	struct ib_frmr_pool *pool, *next;

	if (!pools)
		return;

	rbtree_postorder_for_each_entry_safe(pool, next, &pools->rb_root, node)
		destroy_frmr_pool(device, pool);

	destroy_workqueue(pools->aging_wq);
	kfree(pools);
	device->frmr_pools = NULL;
}
EXPORT_SYMBOL(ib_frmr_pools_cleanup);

int ib_frmr_pools_set_aging_period(struct ib_device *device, u32 period_sec)
{
	struct ib_frmr_pools *pools = device->frmr_pools;
	struct ib_frmr_pool *pool;
	struct rb_node *node;

	if (!pools)
		return -EINVAL;

	if (period_sec == 0)
		return -EINVAL;

	WRITE_ONCE(pools->aging_period_sec, period_sec);

	read_lock(&pools->rb_lock);
	for (node = rb_first(&pools->rb_root); node; node = rb_next(node)) {
		pool = rb_entry(node, struct ib_frmr_pool, node);
		mod_delayed_work(pools->aging_wq, &pool->aging_work,
				 secs_to_jiffies(period_sec));
	}
	read_unlock(&pools->rb_lock);

	return 0;
}

static inline int compare_keys(struct ib_frmr_key *key1,
			       struct ib_frmr_key *key2)
{
	int res;

	res = cmp_int(key1->ats, key2->ats);
	if (res)
		return res;

	res = cmp_int(key1->access_flags, key2->access_flags);
	if (res)
		return res;

	res = cmp_int(key1->vendor_key, key2->vendor_key);
	if (res)
		return res;

	res = cmp_int(key1->kernel_vendor_key, key2->kernel_vendor_key);
	if (res)
		return res;

	/*
	 * allow using handles that support more DMA blocks, up to twice the
	 * requested number
	 */
	res = cmp_int(key1->num_dma_blocks, key2->num_dma_blocks);
	if (res > 0) {
		if (key1->num_dma_blocks - key2->num_dma_blocks <
		    key2->num_dma_blocks)
			return 0;
	}

	return res;
}

static int frmr_pool_cmp_find(const void *key, const struct rb_node *node)
{
	struct ib_frmr_pool *pool = rb_entry(node, struct ib_frmr_pool, node);

	return compare_keys(&pool->key, (struct ib_frmr_key *)key);
}

static int frmr_pool_cmp_add(struct rb_node *new, const struct rb_node *node)
{
	struct ib_frmr_pool *new_pool =
		rb_entry(new, struct ib_frmr_pool, node);
	struct ib_frmr_pool *pool = rb_entry(node, struct ib_frmr_pool, node);

	return compare_keys(&pool->key, &new_pool->key);
}

static struct ib_frmr_pool *ib_frmr_pool_find(struct ib_frmr_pools *pools,
					      struct ib_frmr_key *key)
{
	struct ib_frmr_pool *pool;
	struct rb_node *node;

	/* find operation is done under read lock for performance reasons.
	 * The case of threads failing to find the same pool and creating it
	 * is handled by the create_frmr_pool function.
	 */
	read_lock(&pools->rb_lock);
	node = rb_find(key, &pools->rb_root, frmr_pool_cmp_find);
	pool = rb_entry_safe(node, struct ib_frmr_pool, node);
	read_unlock(&pools->rb_lock);

	return pool;
}

static struct ib_frmr_pool *create_frmr_pool(struct ib_device *device,
					     struct ib_frmr_key *key)
{
	struct ib_frmr_pools *pools = device->frmr_pools;
	struct ib_frmr_pool *pool;
	struct rb_node *existing;

	pool = kzalloc_obj(*pool);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	memcpy(&pool->key, key, sizeof(*key));
	INIT_LIST_HEAD(&pool->queue.pages_list);
	INIT_LIST_HEAD(&pool->inactive_queue.pages_list);
	spin_lock_init(&pool->lock);
	INIT_DELAYED_WORK(&pool->aging_work, pool_aging_work);
	pool->device = device;

	write_lock(&pools->rb_lock);
	existing = rb_find_add(&pool->node, &pools->rb_root, frmr_pool_cmp_add);
	write_unlock(&pools->rb_lock);

	/* If a different thread has already created the pool, return it.
	 * The insert operation is done under the write lock so we are sure
	 * that the pool is not inserted twice.
	 */
	if (existing) {
		kfree(pool);
		return rb_entry(existing, struct ib_frmr_pool, node);
	}

	return pool;
}

int ib_frmr_pools_set_pinned(struct ib_device *device, struct ib_frmr_key *key,
			     u32 pinned_handles)
{
	struct ib_frmr_pools *pools = device->frmr_pools;
	struct ib_frmr_key driver_key = {};
	struct ib_frmr_pool *pool;
	u32 needed_handles;
	u32 current_total;
	int i, ret = 0;
	u32 *handles;

	if (!pools)
		return -EINVAL;

	ret = ib_check_mr_access(device, key->access_flags);
	if (ret)
		return ret;

	if (pools->pool_ops->build_key) {
		ret = pools->pool_ops->build_key(device, key, &driver_key);
		if (ret)
			return ret;
	} else {
		memcpy(&driver_key, key, sizeof(*key));
	}

	pool = ib_frmr_pool_find(pools, &driver_key);
	if (!pool) {
		pool = create_frmr_pool(device, &driver_key);
		if (IS_ERR(pool))
			return PTR_ERR(pool);
	}

	spin_lock(&pool->lock);
	current_total = pool->in_use + pool->queue.ci + pool->inactive_queue.ci;

	if (current_total < pinned_handles)
		needed_handles = pinned_handles - current_total;
	else
		needed_handles = 0;

	pool->pinned_handles = pinned_handles;
	spin_unlock(&pool->lock);

	if (!needed_handles)
		goto schedule_aging;

	handles = kcalloc(needed_handles, sizeof(*handles), GFP_KERNEL);
	if (!handles)
		return -ENOMEM;

	ret = pools->pool_ops->create_frmrs(device, &driver_key, handles,
					    needed_handles);
	if (ret) {
		kfree(handles);
		return ret;
	}

	spin_lock(&pool->lock);
	for (i = 0; i < needed_handles; i++) {
		ret = push_handle_to_queue_locked(&pool->queue,
						  handles[i]);
		if (ret)
			break;
	}
	spin_unlock(&pool->lock);

	if (ret) {
		/* Destroy handles created but never pushed to the pool. */
		pools->pool_ops->destroy_frmrs(device, &handles[i],
				needed_handles - i);
	}

	kfree(handles);

schedule_aging:
	/* Ensure aging is scheduled to adjust to new pinned handles count */
	mod_delayed_work(pools->aging_wq, &pool->aging_work, 0);

	return ret;
}

static int get_frmr_from_pool(struct ib_device *device,
			      struct ib_frmr_pool *pool, struct ib_mr *mr)
{
	struct ib_frmr_pools *pools = device->frmr_pools;
	u32 handle;
	int err;

	spin_lock(&pool->lock);
	if (pool->queue.ci == 0) {
		if (pool->inactive_queue.ci > 0) {
			handle = pop_handle_from_queue_locked(
				&pool->inactive_queue);
		} else {
			spin_unlock(&pool->lock);
			err = pools->pool_ops->create_frmrs(device, &pool->key,
							    &handle, 1);
			if (err)
				return err;
			spin_lock(&pool->lock);
		}
	} else {
		handle = pop_handle_from_queue_locked(&pool->queue);
	}

	pool->in_use++;
	if (pool->in_use > pool->max_in_use)
		pool->max_in_use = pool->in_use;

	spin_unlock(&pool->lock);

	mr->frmr.pool = pool;
	mr->frmr.handle = handle;

	return 0;
}

/*
 * Pop an FRMR handle from the pool.
 *
 * @device: The device to pop the FRMR handle from.
 * @mr: The MR to pop the FRMR handle from.
 *
 * Returns 0 on success, negative error code on failure.
 */
int ib_frmr_pool_pop(struct ib_device *device, struct ib_mr *mr)
{
	struct ib_frmr_pools *pools = device->frmr_pools;
	struct ib_frmr_pool *pool;

	if (WARN_ON_ONCE(!pools))
		return -EINVAL;

	pool = ib_frmr_pool_find(pools, &mr->frmr.key);
	if (!pool) {
		pool = create_frmr_pool(device, &mr->frmr.key);
		if (IS_ERR(pool))
			return PTR_ERR(pool);
	}

	return get_frmr_from_pool(device, pool, mr);
}
EXPORT_SYMBOL(ib_frmr_pool_pop);

/*
 * Push an FRMR handle back to the pool.
 *
 * @device: The device to push the FRMR handle to.
 * @mr: The MR containing the FRMR handle to push back to the pool.
 *
 */
void ib_frmr_pool_push(struct ib_device *device, struct ib_mr *mr)
{
	struct ib_frmr_pool *pool = mr->frmr.pool;
	struct ib_frmr_pools *pools = device->frmr_pools;
	bool schedule_aging = false;
	int ret;

	spin_lock(&pool->lock);
	pool->in_use--;
	ret = push_handle_to_queue_locked(&pool->queue, mr->frmr.handle);

	/* Schedule aging every time an empty pool becomes non-empty */
	if (!ret && pool->queue.ci == 1)
		schedule_aging = true;

	spin_unlock(&pool->lock);

	if (ret) {
		pools->pool_ops->destroy_frmrs(device, &mr->frmr.handle, 1);
		return;
	}

	if (schedule_aging)
		queue_delayed_work(pools->aging_wq, &pool->aging_work,
			secs_to_jiffies(READ_ONCE(pools->aging_period_sec)));

}
EXPORT_SYMBOL(ib_frmr_pool_push);

/*
 * Drop a handle previously popped from the pool without returning it for
 * reuse. The caller is responsible for destroying the underlying hardware
 * resource.
 */
void ib_frmr_pool_drop(struct ib_mr *mr)
{
	struct ib_frmr_pool *pool = mr->frmr.pool;

	spin_lock(&pool->lock);
	pool->in_use--;
	spin_unlock(&pool->lock);
}
EXPORT_SYMBOL(ib_frmr_pool_drop);

/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König
 */

#include <linux/iosys-map.h>
#include <linux/io-mapping.h>
#include <linux/scatterlist.h>

#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_resource.h>

#include <drm/drm_util.h>

/**
 * ttm_lru_bulk_move_init - initialize a bulk move structure
 * @bulk: the structure to init
 *
 * For now just memset the structure to zero.
 */
void ttm_lru_bulk_move_init(struct ttm_lru_bulk_move *bulk)
{
	memset(bulk, 0, sizeof(*bulk));
}
EXPORT_SYMBOL(ttm_lru_bulk_move_init);

/**
 * ttm_lru_bulk_move_tail - bulk move range of resources to the LRU tail.
 *
 * @bulk: bulk move structure
 *
 * Bulk move BOs to the LRU tail, only valid to use when driver makes sure that
 * resource order never changes. Should be called with &ttm_device.lru_lock held.
 */
void ttm_lru_bulk_move_tail(struct ttm_lru_bulk_move *bulk)
{
	unsigned i, j;

	for (i = 0; i < TTM_NUM_MEM_TYPES; ++i) {
		for (j = 0; j < TTM_MAX_BO_PRIORITY; ++j) {
			struct ttm_lru_bulk_move_pos *pos = &bulk->pos[i][j];
			struct ttm_resource_manager *man;

			if (!pos->first)
				continue;

			lockdep_assert_held(&pos->first->bo->bdev->lru_lock);
			dma_resv_assert_held(pos->first->bo->base.resv);
			dma_resv_assert_held(pos->last->bo->base.resv);

			man = ttm_manager_type(pos->first->bo->bdev, i);
			list_bulk_move_tail(&man->lru[j], &pos->first->lru,
					    &pos->last->lru);
		}
	}
}
EXPORT_SYMBOL(ttm_lru_bulk_move_tail);

/* Return the bulk move pos object for this resource */
static struct ttm_lru_bulk_move_pos *
ttm_lru_bulk_move_pos(struct ttm_lru_bulk_move *bulk, struct ttm_resource *res)
{
	return &bulk->pos[res->mem_type][res->bo->priority];
}

/* Move the resource to the tail of the bulk move range */
static void ttm_lru_bulk_move_pos_tail(struct ttm_lru_bulk_move_pos *pos,
				       struct ttm_resource *res)
{
	if (pos->last != res) {
		if (pos->first == res)
			pos->first = list_next_entry(res, lru);
		list_move(&res->lru, &pos->last->lru);
		pos->last = res;
	}
}

/* Add the resource to a bulk_move cursor */
static void ttm_lru_bulk_move_add(struct ttm_lru_bulk_move *bulk,
				  struct ttm_resource *res)
{
	struct ttm_lru_bulk_move_pos *pos = ttm_lru_bulk_move_pos(bulk, res);

	if (!pos->first) {
		pos->first = res;
		pos->last = res;
	} else {
		ttm_lru_bulk_move_pos_tail(pos, res);
	}
}

/* Remove the resource from a bulk_move range */
static void ttm_lru_bulk_move_del(struct ttm_lru_bulk_move *bulk,
				  struct ttm_resource *res)
{
	struct ttm_lru_bulk_move_pos *pos = ttm_lru_bulk_move_pos(bulk, res);

	if (unlikely(WARN_ON(!pos->first || !pos->last) ||
		     (pos->first == res && pos->last == res))) {
		pos->first = NULL;
		pos->last = NULL;
	} else if (pos->first == res) {
		pos->first = list_next_entry(res, lru);
	} else if (pos->last == res) {
		pos->last = list_prev_entry(res, lru);
	} else {
		list_move(&res->lru, &pos->last->lru);
	}
}

/* Add the resource to a bulk move if the BO is configured for it */
void ttm_resource_add_bulk_move(struct ttm_resource *res,
				struct ttm_buffer_object *bo)
{
	if (bo->bulk_move && !bo->pin_count)
		ttm_lru_bulk_move_add(bo->bulk_move, res);
}

/* Remove the resource from a bulk move if the BO is configured for it */
void ttm_resource_del_bulk_move(struct ttm_resource *res,
				struct ttm_buffer_object *bo)
{
	if (bo->bulk_move && !bo->pin_count)
		ttm_lru_bulk_move_del(bo->bulk_move, res);
}

/* Move a resource to the LRU or bulk tail */
void ttm_resource_move_to_lru_tail(struct ttm_resource *res)
{
	struct ttm_buffer_object *bo = res->bo;
	struct ttm_device *bdev = bo->bdev;

	lockdep_assert_held(&bo->bdev->lru_lock);

	if (bo->pin_count) {
		list_move_tail(&res->lru, &bdev->pinned);

	} else	if (bo->bulk_move) {
		struct ttm_lru_bulk_move_pos *pos =
			ttm_lru_bulk_move_pos(bo->bulk_move, res);

		ttm_lru_bulk_move_pos_tail(pos, res);
	} else {
		struct ttm_resource_manager *man;

		man = ttm_manager_type(bdev, res->mem_type);
		list_move_tail(&res->lru, &man->lru[bo->priority]);
	}
}

/**
 * ttm_resource_init - resource object constructure
 * @bo: buffer object this resources is allocated for
 * @place: placement of the resource
 * @res: the resource object to inistilize
 *
 * Initialize a new resource object. Counterpart of ttm_resource_fini().
 */
void ttm_resource_init(struct ttm_buffer_object *bo,
                       const struct ttm_place *place,
                       struct ttm_resource *res)
{
	struct ttm_resource_manager *man;

	res->start = 0;
	res->size = bo->base.size;
	res->mem_type = place->mem_type;
	res->placement = place->flags;
	res->bus.addr = NULL;
	res->bus.offset = 0;
	res->bus.is_iomem = false;
	res->bus.caching = ttm_cached;
	res->bo = bo;

	man = ttm_manager_type(bo->bdev, place->mem_type);
	spin_lock(&bo->bdev->lru_lock);
	if (bo->pin_count)
		list_add_tail(&res->lru, &bo->bdev->pinned);
	else
		list_add_tail(&res->lru, &man->lru[bo->priority]);
	man->usage += res->size;
	spin_unlock(&bo->bdev->lru_lock);
}
EXPORT_SYMBOL(ttm_resource_init);

/**
 * ttm_resource_fini - resource destructor
 * @man: the resource manager this resource belongs to
 * @res: the resource to clean up
 *
 * Should be used by resource manager backends to clean up the TTM resource
 * objects before freeing the underlying structure. Makes sure the resource is
 * removed from the LRU before destruction.
 * Counterpart of ttm_resource_init().
 */
void ttm_resource_fini(struct ttm_resource_manager *man,
		       struct ttm_resource *res)
{
	struct ttm_device *bdev = man->bdev;

	spin_lock(&bdev->lru_lock);
	list_del_init(&res->lru);
	man->usage -= res->size;
	spin_unlock(&bdev->lru_lock);
}
EXPORT_SYMBOL(ttm_resource_fini);

int ttm_resource_alloc(struct ttm_buffer_object *bo,
		       const struct ttm_place *place,
		       struct ttm_resource **res_ptr)
{
	struct ttm_resource_manager *man =
		ttm_manager_type(bo->bdev, place->mem_type);
	int ret;

	ret = man->func->alloc(man, bo, place, res_ptr);
	if (ret)
		return ret;

	spin_lock(&bo->bdev->lru_lock);
	ttm_resource_add_bulk_move(*res_ptr, bo);
	spin_unlock(&bo->bdev->lru_lock);
	return 0;
}
EXPORT_SYMBOL_FOR_TESTS_ONLY(ttm_resource_alloc);

void ttm_resource_free(struct ttm_buffer_object *bo, struct ttm_resource **res)
{
	struct ttm_resource_manager *man;

	if (!*res)
		return;

	spin_lock(&bo->bdev->lru_lock);
	ttm_resource_del_bulk_move(*res, bo);
	spin_unlock(&bo->bdev->lru_lock);
	man = ttm_manager_type(bo->bdev, (*res)->mem_type);
	man->func->free(man, *res);
	*res = NULL;
}
EXPORT_SYMBOL(ttm_resource_free);

/**
 * ttm_resource_intersects - test for intersection
 *
 * @bdev: TTM device structure
 * @res: The resource to test
 * @place: The placement to test
 * @size: How many bytes the new allocation needs.
 *
 * Test if @res intersects with @place and @size. Used for testing if evictions
 * are valueable or not.
 *
 * Returns true if the res placement intersects with @place and @size.
 */
bool ttm_resource_intersects(struct ttm_device *bdev,
			     struct ttm_resource *res,
			     const struct ttm_place *place,
			     size_t size)
{
	struct ttm_resource_manager *man;

	if (!res)
		return false;

	man = ttm_manager_type(bdev, res->mem_type);
	if (!place || !man->func->intersects)
		return true;

	return man->func->intersects(man, res, place, size);
}

/**
 * ttm_resource_compatible - check if resource is compatible with placement
 *
 * @res: the resource to check
 * @placement: the placement to check against
 *
 * Returns true if the placement is compatible.
 */
bool ttm_resource_compatible(struct ttm_resource *res,
			     struct ttm_placement *placement)
{
	struct ttm_buffer_object *bo = res->bo;
	struct ttm_device *bdev = bo->bdev;
	unsigned i;

	if (res->placement & TTM_PL_FLAG_TEMPORARY)
		return false;

	for (i = 0; i < placement->num_placement; i++) {
		const struct ttm_place *place = &placement->placement[i];
		struct ttm_resource_manager *man;

		if (res->mem_type != place->mem_type)
			continue;

		man = ttm_manager_type(bdev, res->mem_type);
		if (man->func->compatible &&
		    !man->func->compatible(man, res, place, bo->base.size))
			continue;

		if ((!(place->flags & TTM_PL_FLAG_CONTIGUOUS) ||
		     (res->placement & TTM_PL_FLAG_CONTIGUOUS)))
			return true;
	}
	return false;
}

void ttm_resource_set_bo(struct ttm_resource *res,
			 struct ttm_buffer_object *bo)
{
	spin_lock(&bo->bdev->lru_lock);
	res->bo = bo;
	spin_unlock(&bo->bdev->lru_lock);
}

/**
 * ttm_resource_manager_init
 *
 * @man: memory manager object to init
 * @bdev: ttm device this manager belongs to
 * @size: size of managed resources in arbitrary units
 *
 * Initialise core parts of a manager object.
 */
void ttm_resource_manager_init(struct ttm_resource_manager *man,
			       struct ttm_device *bdev,
			       uint64_t size)
{
	unsigned i;

	spin_lock_init(&man->move_lock);
	man->bdev = bdev;
	man->size = size;
	man->usage = 0;

	for (i = 0; i < TTM_MAX_BO_PRIORITY; ++i)
		INIT_LIST_HEAD(&man->lru[i]);
	man->move = NULL;
}
EXPORT_SYMBOL(ttm_resource_manager_init);

/*
 * ttm_resource_manager_evict_all
 *
 * @bdev - device to use
 * @man - manager to use
 *
 * Evict all the objects out of a memory manager until it is empty.
 * Part of memory manager cleanup sequence.
 */
int ttm_resource_manager_evict_all(struct ttm_device *bdev,
				   struct ttm_resource_manager *man)
{
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = false,
		.force_alloc = true
	};
	struct dma_fence *fence;
	int ret;
	unsigned i;

	/*
	 * Can't use standard list traversal since we're unlocking.
	 */

	spin_lock(&bdev->lru_lock);
	for (i = 0; i < TTM_MAX_BO_PRIORITY; ++i) {
		while (!list_empty(&man->lru[i])) {
			spin_unlock(&bdev->lru_lock);
			ret = ttm_mem_evict_first(bdev, man, NULL, &ctx,
						  NULL);
			if (ret)
				return ret;
			spin_lock(&bdev->lru_lock);
		}
	}
	spin_unlock(&bdev->lru_lock);

	spin_lock(&man->move_lock);
	fence = dma_fence_get(man->move);
	spin_unlock(&man->move_lock);

	if (fence) {
		ret = dma_fence_wait(fence, false);
		dma_fence_put(fence);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ttm_resource_manager_evict_all);

/**
 * ttm_resource_manager_usage
 *
 * @man: A memory manager object.
 *
 * Return how many resources are currently used.
 */
uint64_t ttm_resource_manager_usage(struct ttm_resource_manager *man)
{
	uint64_t usage;

	spin_lock(&man->bdev->lru_lock);
	usage = man->usage;
	spin_unlock(&man->bdev->lru_lock);
	return usage;
}
EXPORT_SYMBOL(ttm_resource_manager_usage);

/**
 * ttm_resource_manager_debug
 *
 * @man: manager type to dump.
 * @p: printer to use for debug.
 */
void ttm_resource_manager_debug(struct ttm_resource_manager *man,
				struct drm_printer *p)
{
	drm_printf(p, "  use_type: %d\n", man->use_type);
	drm_printf(p, "  use_tt: %d\n", man->use_tt);
	drm_printf(p, "  size: %llu\n", man->size);
	drm_printf(p, "  usage: %llu\n", ttm_resource_manager_usage(man));
	if (man->func->debug)
		man->func->debug(man, p);
}
EXPORT_SYMBOL(ttm_resource_manager_debug);

/**
 * ttm_resource_manager_first
 *
 * @man: resource manager to iterate over
 * @cursor: cursor to record the position
 *
 * Returns the first resource from the resource manager.
 */
struct ttm_resource *
ttm_resource_manager_first(struct ttm_resource_manager *man,
			   struct ttm_resource_cursor *cursor)
{
	struct ttm_resource *res;

	lockdep_assert_held(&man->bdev->lru_lock);

	for (cursor->priority = 0; cursor->priority < TTM_MAX_BO_PRIORITY;
	     ++cursor->priority)
		list_for_each_entry(res, &man->lru[cursor->priority], lru)
			return res;

	return NULL;
}

/**
 * ttm_resource_manager_next
 *
 * @man: resource manager to iterate over
 * @cursor: cursor to record the position
 * @res: the current resource pointer
 *
 * Returns the next resource from the resource manager.
 */
struct ttm_resource *
ttm_resource_manager_next(struct ttm_resource_manager *man,
			  struct ttm_resource_cursor *cursor,
			  struct ttm_resource *res)
{
	lockdep_assert_held(&man->bdev->lru_lock);

	list_for_each_entry_continue(res, &man->lru[cursor->priority], lru)
		return res;

	for (++cursor->priority; cursor->priority < TTM_MAX_BO_PRIORITY;
	     ++cursor->priority)
		list_for_each_entry(res, &man->lru[cursor->priority], lru)
			return res;

	return NULL;
}

static void ttm_kmap_iter_iomap_map_local(struct ttm_kmap_iter *iter,
					  struct iosys_map *dmap,
					  pgoff_t i)
{
	struct ttm_kmap_iter_iomap *iter_io =
		container_of(iter, typeof(*iter_io), base);
	void __iomem *addr;

retry:
	while (i >= iter_io->cache.end) {
		iter_io->cache.sg = iter_io->cache.sg ?
			sg_next(iter_io->cache.sg) : iter_io->st->sgl;
		iter_io->cache.i = iter_io->cache.end;
		iter_io->cache.end += sg_dma_len(iter_io->cache.sg) >>
			PAGE_SHIFT;
		iter_io->cache.offs = sg_dma_address(iter_io->cache.sg) -
			iter_io->start;
	}

	if (i < iter_io->cache.i) {
		iter_io->cache.end = 0;
		iter_io->cache.sg = NULL;
		goto retry;
	}

	addr = io_mapping_map_local_wc(iter_io->iomap, iter_io->cache.offs +
				       (((resource_size_t)i - iter_io->cache.i)
					<< PAGE_SHIFT));
	iosys_map_set_vaddr_iomem(dmap, addr);
}

static void ttm_kmap_iter_iomap_unmap_local(struct ttm_kmap_iter *iter,
					    struct iosys_map *map)
{
	io_mapping_unmap_local(map->vaddr_iomem);
}

static const struct ttm_kmap_iter_ops ttm_kmap_iter_io_ops = {
	.map_local =  ttm_kmap_iter_iomap_map_local,
	.unmap_local = ttm_kmap_iter_iomap_unmap_local,
	.maps_tt = false,
};

/**
 * ttm_kmap_iter_iomap_init - Initialize a struct ttm_kmap_iter_iomap
 * @iter_io: The struct ttm_kmap_iter_iomap to initialize.
 * @iomap: The struct io_mapping representing the underlying linear io_memory.
 * @st: sg_table into @iomap, representing the memory of the struct
 * ttm_resource.
 * @start: Offset that needs to be subtracted from @st to make
 * sg_dma_address(st->sgl) - @start == 0 for @iomap start.
 *
 * Return: Pointer to the embedded struct ttm_kmap_iter.
 */
struct ttm_kmap_iter *
ttm_kmap_iter_iomap_init(struct ttm_kmap_iter_iomap *iter_io,
			 struct io_mapping *iomap,
			 struct sg_table *st,
			 resource_size_t start)
{
	iter_io->base.ops = &ttm_kmap_iter_io_ops;
	iter_io->iomap = iomap;
	iter_io->st = st;
	iter_io->start = start;
	memset(&iter_io->cache, 0, sizeof(iter_io->cache));

	return &iter_io->base;
}
EXPORT_SYMBOL(ttm_kmap_iter_iomap_init);

/**
 * DOC: Linear io iterator
 *
 * This code should die in the not too near future. Best would be if we could
 * make io-mapping use memremap for all io memory, and have memremap
 * implement a kmap_local functionality. We could then strip a huge amount of
 * code. These linear io iterators are implemented to mimic old functionality,
 * and they don't use kmap_local semantics at all internally. Rather ioremap or
 * friends, and at least on 32-bit they add global TLB flushes and points
 * of failure.
 */

static void ttm_kmap_iter_linear_io_map_local(struct ttm_kmap_iter *iter,
					      struct iosys_map *dmap,
					      pgoff_t i)
{
	struct ttm_kmap_iter_linear_io *iter_io =
		container_of(iter, typeof(*iter_io), base);

	*dmap = iter_io->dmap;
	iosys_map_incr(dmap, i * PAGE_SIZE);
}

static const struct ttm_kmap_iter_ops ttm_kmap_iter_linear_io_ops = {
	.map_local =  ttm_kmap_iter_linear_io_map_local,
	.maps_tt = false,
};

/**
 * ttm_kmap_iter_linear_io_init - Initialize an iterator for linear io memory
 * @iter_io: The iterator to initialize
 * @bdev: The TTM device
 * @mem: The ttm resource representing the iomap.
 *
 * This function is for internal TTM use only. It sets up a memcpy kmap iterator
 * pointing at a linear chunk of io memory.
 *
 * Return: A pointer to the embedded struct ttm_kmap_iter or error pointer on
 * failure.
 */
struct ttm_kmap_iter *
ttm_kmap_iter_linear_io_init(struct ttm_kmap_iter_linear_io *iter_io,
			     struct ttm_device *bdev,
			     struct ttm_resource *mem)
{
	int ret;

	ret = ttm_mem_io_reserve(bdev, mem);
	if (ret)
		goto out_err;
	if (!mem->bus.is_iomem) {
		ret = -EINVAL;
		goto out_io_free;
	}

	if (mem->bus.addr) {
		iosys_map_set_vaddr(&iter_io->dmap, mem->bus.addr);
		iter_io->needs_unmap = false;
	} else {
		iter_io->needs_unmap = true;
		memset(&iter_io->dmap, 0, sizeof(iter_io->dmap));
		if (mem->bus.caching == ttm_write_combined)
			iosys_map_set_vaddr_iomem(&iter_io->dmap,
						  ioremap_wc(mem->bus.offset,
							     mem->size));
		else if (mem->bus.caching == ttm_cached)
			iosys_map_set_vaddr(&iter_io->dmap,
					    memremap(mem->bus.offset, mem->size,
						     MEMREMAP_WB |
						     MEMREMAP_WT |
						     MEMREMAP_WC));

		/* If uncached requested or if mapping cached or wc failed */
		if (iosys_map_is_null(&iter_io->dmap))
			iosys_map_set_vaddr_iomem(&iter_io->dmap,
						  ioremap(mem->bus.offset,
							  mem->size));

		if (iosys_map_is_null(&iter_io->dmap)) {
			ret = -ENOMEM;
			goto out_io_free;
		}
	}

	iter_io->base.ops = &ttm_kmap_iter_linear_io_ops;
	return &iter_io->base;

out_io_free:
	ttm_mem_io_free(bdev, mem);
out_err:
	return ERR_PTR(ret);
}

/**
 * ttm_kmap_iter_linear_io_fini - Clean up an iterator for linear io memory
 * @iter_io: The iterator to initialize
 * @bdev: The TTM device
 * @mem: The ttm resource representing the iomap.
 *
 * This function is for internal TTM use only. It cleans up a memcpy kmap
 * iterator initialized by ttm_kmap_iter_linear_io_init.
 */
void
ttm_kmap_iter_linear_io_fini(struct ttm_kmap_iter_linear_io *iter_io,
			     struct ttm_device *bdev,
			     struct ttm_resource *mem)
{
	if (iter_io->needs_unmap && iosys_map_is_set(&iter_io->dmap)) {
		if (iter_io->dmap.is_iomem)
			iounmap(iter_io->dmap.vaddr_iomem);
		else
			memunmap(iter_io->dmap.vaddr);
	}

	ttm_mem_io_free(bdev, mem);
}

#if defined(CONFIG_DEBUG_FS)

static int ttm_resource_manager_show(struct seq_file *m, void *unused)
{
	struct ttm_resource_manager *man =
		(struct ttm_resource_manager *)m->private;
	struct drm_printer p = drm_seq_file_printer(m);
	ttm_resource_manager_debug(man, &p);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ttm_resource_manager);

#endif

/**
 * ttm_resource_manager_create_debugfs - Create debugfs entry for specified
 * resource manager.
 * @man: The TTM resource manager for which the debugfs stats file be creates
 * @parent: debugfs directory in which the file will reside
 * @name: The filename to create.
 *
 * This function setups up a debugfs file that can be used to look
 * at debug statistics of the specified ttm_resource_manager.
 */
void ttm_resource_manager_create_debugfs(struct ttm_resource_manager *man,
					 struct dentry * parent,
					 const char *name)
{
#if defined(CONFIG_DEBUG_FS)
	debugfs_create_file(name, 0444, parent, man, &ttm_resource_manager_fops);
#endif
}
EXPORT_SYMBOL(ttm_resource_manager_create_debugfs);

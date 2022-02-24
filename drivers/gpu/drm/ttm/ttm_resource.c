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

#include <linux/dma-buf-map.h>
#include <linux/io-mapping.h>
#include <linux/scatterlist.h>

#include <drm/ttm/ttm_resource.h>
#include <drm/ttm/ttm_bo_driver.h>

void ttm_resource_init(struct ttm_buffer_object *bo,
                       const struct ttm_place *place,
                       struct ttm_resource *res)
{
	res->start = 0;
	res->num_pages = PFN_UP(bo->base.size);
	res->mem_type = place->mem_type;
	res->placement = place->flags;
	res->bus.addr = NULL;
	res->bus.offset = 0;
	res->bus.is_iomem = false;
	res->bus.caching = ttm_cached;
}
EXPORT_SYMBOL(ttm_resource_init);

int ttm_resource_alloc(struct ttm_buffer_object *bo,
		       const struct ttm_place *place,
		       struct ttm_resource **res_ptr)
{
	struct ttm_resource_manager *man =
		ttm_manager_type(bo->bdev, place->mem_type);

	return man->func->alloc(man, bo, place, res_ptr);
}

void ttm_resource_free(struct ttm_buffer_object *bo, struct ttm_resource **res)
{
	struct ttm_resource_manager *man;

	if (!*res)
		return;

	man = ttm_manager_type(bo->bdev, (*res)->mem_type);
	man->func->free(man, *res);
	*res = NULL;
}
EXPORT_SYMBOL(ttm_resource_free);

static bool ttm_resource_places_compat(struct ttm_resource *res,
				       const struct ttm_place *places,
				       unsigned num_placement)
{
	unsigned i;

	if (res->placement & TTM_PL_FLAG_TEMPORARY)
		return false;

	for (i = 0; i < num_placement; i++) {
		const struct ttm_place *heap = &places[i];

		if (res->start < heap->fpfn || (heap->lpfn &&
		    (res->start + res->num_pages) > heap->lpfn))
			continue;

		if ((res->mem_type == heap->mem_type) &&
		    (!(heap->flags & TTM_PL_FLAG_CONTIGUOUS) ||
		     (res->placement & TTM_PL_FLAG_CONTIGUOUS)))
			return true;
	}
	return false;
}

/**
 * ttm_resource_compat - check if resource is compatible with placement
 *
 * @res: the resource to check
 * @placement: the placement to check against
 *
 * Returns true if the placement is compatible.
 */
bool ttm_resource_compat(struct ttm_resource *res,
			 struct ttm_placement *placement)
{
	if (ttm_resource_places_compat(res, placement->placement,
				       placement->num_placement))
		return true;

	if ((placement->busy_placement != placement->placement ||
	     placement->num_busy_placement > placement->num_placement) &&
	    ttm_resource_places_compat(res, placement->busy_placement,
				       placement->num_busy_placement))
		return true;

	return false;
}
EXPORT_SYMBOL(ttm_resource_compat);

/**
 * ttm_resource_manager_init
 *
 * @man: memory manager object to init
 * @p_size: size managed area in pages.
 *
 * Initialise core parts of a manager object.
 */
void ttm_resource_manager_init(struct ttm_resource_manager *man,
			       unsigned long p_size)
{
	unsigned i;

	spin_lock_init(&man->move_lock);
	man->size = p_size;

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
	if (man->func->debug)
		man->func->debug(man, p);
}
EXPORT_SYMBOL(ttm_resource_manager_debug);

static void ttm_kmap_iter_iomap_map_local(struct ttm_kmap_iter *iter,
					  struct dma_buf_map *dmap,
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
	dma_buf_map_set_vaddr_iomem(dmap, addr);
}

static void ttm_kmap_iter_iomap_unmap_local(struct ttm_kmap_iter *iter,
					    struct dma_buf_map *map)
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
					      struct dma_buf_map *dmap,
					      pgoff_t i)
{
	struct ttm_kmap_iter_linear_io *iter_io =
		container_of(iter, typeof(*iter_io), base);

	*dmap = iter_io->dmap;
	dma_buf_map_incr(dmap, i * PAGE_SIZE);
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
		dma_buf_map_set_vaddr(&iter_io->dmap, mem->bus.addr);
		iter_io->needs_unmap = false;
	} else {
		size_t bus_size = (size_t)mem->num_pages << PAGE_SHIFT;

		iter_io->needs_unmap = true;
		memset(&iter_io->dmap, 0, sizeof(iter_io->dmap));
		if (mem->bus.caching == ttm_write_combined)
			dma_buf_map_set_vaddr_iomem(&iter_io->dmap,
						    ioremap_wc(mem->bus.offset,
							       bus_size));
		else if (mem->bus.caching == ttm_cached)
			dma_buf_map_set_vaddr(&iter_io->dmap,
					      memremap(mem->bus.offset, bus_size,
						       MEMREMAP_WB |
						       MEMREMAP_WT |
						       MEMREMAP_WC));

		/* If uncached requested or if mapping cached or wc failed */
		if (dma_buf_map_is_null(&iter_io->dmap))
			dma_buf_map_set_vaddr_iomem(&iter_io->dmap,
						    ioremap(mem->bus.offset,
							    bus_size));

		if (dma_buf_map_is_null(&iter_io->dmap)) {
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
	if (iter_io->needs_unmap && dma_buf_map_is_set(&iter_io->dmap)) {
		if (iter_io->dmap.is_iomem)
			iounmap(iter_io->dmap.vaddr_iomem);
		else
			memunmap(iter_io->dmap.vaddr);
	}

	ttm_mem_io_free(bdev, mem);
}

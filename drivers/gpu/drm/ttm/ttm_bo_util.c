/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright (c) 2007-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */
#include <linux/swap.h>
#include <linux/vmalloc.h>

#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_tt.h>

#include <drm/drm_cache.h>

struct ttm_transfer_obj {
	struct ttm_buffer_object base;
	struct ttm_buffer_object *bo;
};

int ttm_mem_io_reserve(struct ttm_device *bdev,
		       struct ttm_resource *mem)
{
	if (mem->bus.offset || mem->bus.addr)
		return 0;

	mem->bus.is_iomem = false;
	if (!bdev->funcs->io_mem_reserve)
		return 0;

	return bdev->funcs->io_mem_reserve(bdev, mem);
}

void ttm_mem_io_free(struct ttm_device *bdev,
		     struct ttm_resource *mem)
{
	if (!mem)
		return;

	if (!mem->bus.offset && !mem->bus.addr)
		return;

	if (bdev->funcs->io_mem_free)
		bdev->funcs->io_mem_free(bdev, mem);

	mem->bus.offset = 0;
	mem->bus.addr = NULL;
}

/**
 * ttm_move_memcpy - Helper to perform a memcpy ttm move operation.
 * @clear: Whether to clear rather than copy.
 * @num_pages: Number of pages of the operation.
 * @dst_iter: A struct ttm_kmap_iter representing the destination resource.
 * @src_iter: A struct ttm_kmap_iter representing the source resource.
 *
 * This function is intended to be able to move out async under a
 * dma-fence if desired.
 */
void ttm_move_memcpy(bool clear,
		     u32 num_pages,
		     struct ttm_kmap_iter *dst_iter,
		     struct ttm_kmap_iter *src_iter)
{
	const struct ttm_kmap_iter_ops *dst_ops = dst_iter->ops;
	const struct ttm_kmap_iter_ops *src_ops = src_iter->ops;
	struct iosys_map src_map, dst_map;
	pgoff_t i;

	/* Single TTM move. NOP */
	if (dst_ops->maps_tt && src_ops->maps_tt)
		return;

	/* Don't move nonexistent data. Clear destination instead. */
	if (clear) {
		for (i = 0; i < num_pages; ++i) {
			dst_ops->map_local(dst_iter, &dst_map, i);
			if (dst_map.is_iomem)
				memset_io(dst_map.vaddr_iomem, 0, PAGE_SIZE);
			else
				memset(dst_map.vaddr, 0, PAGE_SIZE);
			if (dst_ops->unmap_local)
				dst_ops->unmap_local(dst_iter, &dst_map);
		}
		return;
	}

	for (i = 0; i < num_pages; ++i) {
		dst_ops->map_local(dst_iter, &dst_map, i);
		src_ops->map_local(src_iter, &src_map, i);

		drm_memcpy_from_wc(&dst_map, &src_map, PAGE_SIZE);

		if (src_ops->unmap_local)
			src_ops->unmap_local(src_iter, &src_map);
		if (dst_ops->unmap_local)
			dst_ops->unmap_local(dst_iter, &dst_map);
	}
}
EXPORT_SYMBOL(ttm_move_memcpy);

/**
 * ttm_bo_move_memcpy
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @ctx: operation context
 * @dst_mem: struct ttm_resource indicating where to move.
 *
 * Fallback move function for a mappable buffer object in mappable memory.
 * The function will, if successful,
 * free any old aperture space, and set (@new_mem)->mm_node to NULL,
 * and update the (@bo)->mem placement flags. If unsuccessful, the old
 * data remains untouched, and it's up to the caller to free the
 * memory space indicated by @new_mem.
 * Returns:
 * !0: Failure.
 */
int ttm_bo_move_memcpy(struct ttm_buffer_object *bo,
		       struct ttm_operation_ctx *ctx,
		       struct ttm_resource *dst_mem)
{
	struct ttm_device *bdev = bo->bdev;
	struct ttm_resource_manager *dst_man =
		ttm_manager_type(bo->bdev, dst_mem->mem_type);
	struct ttm_tt *ttm = bo->ttm;
	struct ttm_resource *src_mem = bo->resource;
	struct ttm_resource_manager *src_man;
	union {
		struct ttm_kmap_iter_tt tt;
		struct ttm_kmap_iter_linear_io io;
	} _dst_iter, _src_iter;
	struct ttm_kmap_iter *dst_iter, *src_iter;
	bool clear;
	int ret = 0;

	if (WARN_ON(!src_mem))
		return -EINVAL;

	src_man = ttm_manager_type(bdev, src_mem->mem_type);
	if (ttm && ((ttm->page_flags & TTM_TT_FLAG_SWAPPED) ||
		    dst_man->use_tt)) {
		ret = ttm_bo_populate(bo, ctx);
		if (ret)
			return ret;
	}

	dst_iter = ttm_kmap_iter_linear_io_init(&_dst_iter.io, bdev, dst_mem);
	if (PTR_ERR(dst_iter) == -EINVAL && dst_man->use_tt)
		dst_iter = ttm_kmap_iter_tt_init(&_dst_iter.tt, bo->ttm);
	if (IS_ERR(dst_iter))
		return PTR_ERR(dst_iter);

	src_iter = ttm_kmap_iter_linear_io_init(&_src_iter.io, bdev, src_mem);
	if (PTR_ERR(src_iter) == -EINVAL && src_man->use_tt)
		src_iter = ttm_kmap_iter_tt_init(&_src_iter.tt, bo->ttm);
	if (IS_ERR(src_iter)) {
		ret = PTR_ERR(src_iter);
		goto out_src_iter;
	}

	clear = src_iter->ops->maps_tt && (!ttm || !ttm_tt_is_populated(ttm));
	if (!(clear && ttm && !(ttm->page_flags & TTM_TT_FLAG_ZERO_ALLOC)))
		ttm_move_memcpy(clear, PFN_UP(dst_mem->size), dst_iter, src_iter);

	if (!src_iter->ops->maps_tt)
		ttm_kmap_iter_linear_io_fini(&_src_iter.io, bdev, src_mem);
	ttm_bo_move_sync_cleanup(bo, dst_mem);

out_src_iter:
	if (!dst_iter->ops->maps_tt)
		ttm_kmap_iter_linear_io_fini(&_dst_iter.io, bdev, dst_mem);

	return ret;
}
EXPORT_SYMBOL(ttm_bo_move_memcpy);

static void ttm_transfered_destroy(struct ttm_buffer_object *bo)
{
	struct ttm_transfer_obj *fbo;

	fbo = container_of(bo, struct ttm_transfer_obj, base);
	dma_resv_fini(&fbo->base.base._resv);
	ttm_bo_put(fbo->bo);
	kfree(fbo);
}

/**
 * ttm_buffer_object_transfer
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @new_obj: A pointer to a pointer to a newly created ttm_buffer_object,
 * holding the data of @bo with the old placement.
 *
 * This is a utility function that may be called after an accelerated move
 * has been scheduled. A new buffer object is created as a placeholder for
 * the old data while it's being copied. When that buffer object is idle,
 * it can be destroyed, releasing the space of the old placement.
 * Returns:
 * !0: Failure.
 */

static int ttm_buffer_object_transfer(struct ttm_buffer_object *bo,
				      struct ttm_buffer_object **new_obj)
{
	struct ttm_transfer_obj *fbo;
	int ret;

	fbo = kmalloc(sizeof(*fbo), GFP_KERNEL);
	if (!fbo)
		return -ENOMEM;

	fbo->base = *bo;

	/**
	 * Fix up members that we shouldn't copy directly:
	 * TODO: Explicit member copy would probably be better here.
	 */

	atomic_inc(&ttm_glob.bo_count);
	drm_vma_node_reset(&fbo->base.base.vma_node);

	kref_init(&fbo->base.kref);
	fbo->base.destroy = &ttm_transfered_destroy;
	fbo->base.pin_count = 0;
	if (bo->type != ttm_bo_type_sg)
		fbo->base.base.resv = &fbo->base.base._resv;

	dma_resv_init(&fbo->base.base._resv);
	fbo->base.base.dev = NULL;
	ret = dma_resv_trylock(&fbo->base.base._resv);
	WARN_ON(!ret);

	if (fbo->base.resource) {
		ttm_resource_set_bo(fbo->base.resource, &fbo->base);
		bo->resource = NULL;
		ttm_bo_set_bulk_move(&fbo->base, NULL);
	} else {
		fbo->base.bulk_move = NULL;
	}

	ret = dma_resv_reserve_fences(&fbo->base.base._resv, 1);
	if (ret) {
		kfree(fbo);
		return ret;
	}

	ttm_bo_get(bo);
	fbo->bo = bo;

	ttm_bo_move_to_lru_tail_unlocked(&fbo->base);

	*new_obj = &fbo->base;
	return 0;
}

/**
 * ttm_io_prot
 *
 * @bo: ttm buffer object
 * @res: ttm resource object
 * @tmp: Page protection flag for a normal, cached mapping.
 *
 * Utility function that returns the pgprot_t that should be used for
 * setting up a PTE with the caching model indicated by @c_state.
 */
pgprot_t ttm_io_prot(struct ttm_buffer_object *bo, struct ttm_resource *res,
		     pgprot_t tmp)
{
	struct ttm_resource_manager *man;
	enum ttm_caching caching;

	man = ttm_manager_type(bo->bdev, res->mem_type);
	if (man->use_tt) {
		caching = bo->ttm->caching;
		if (bo->ttm->page_flags & TTM_TT_FLAG_DECRYPTED)
			tmp = pgprot_decrypted(tmp);
	} else  {
		caching = res->bus.caching;
	}

	return ttm_prot_from_caching(caching, tmp);
}
EXPORT_SYMBOL(ttm_io_prot);

static int ttm_bo_ioremap(struct ttm_buffer_object *bo,
			  unsigned long offset,
			  unsigned long size,
			  struct ttm_bo_kmap_obj *map)
{
	struct ttm_resource *mem = bo->resource;

	if (bo->resource->bus.addr) {
		map->bo_kmap_type = ttm_bo_map_premapped;
		map->virtual = ((u8 *)bo->resource->bus.addr) + offset;
	} else {
		resource_size_t res = bo->resource->bus.offset + offset;

		map->bo_kmap_type = ttm_bo_map_iomap;
		if (mem->bus.caching == ttm_write_combined)
			map->virtual = ioremap_wc(res, size);
#ifdef CONFIG_X86
		else if (mem->bus.caching == ttm_cached)
			map->virtual = ioremap_cache(res, size);
#endif
		else
			map->virtual = ioremap(res, size);
	}
	return (!map->virtual) ? -ENOMEM : 0;
}

static int ttm_bo_kmap_ttm(struct ttm_buffer_object *bo,
			   unsigned long start_page,
			   unsigned long num_pages,
			   struct ttm_bo_kmap_obj *map)
{
	struct ttm_resource *mem = bo->resource;
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = false
	};
	struct ttm_tt *ttm = bo->ttm;
	struct ttm_resource_manager *man =
			ttm_manager_type(bo->bdev, bo->resource->mem_type);
	pgprot_t prot;
	int ret;

	BUG_ON(!ttm);

	ret = ttm_bo_populate(bo, &ctx);
	if (ret)
		return ret;

	if (num_pages == 1 && ttm->caching == ttm_cached &&
	    !(man->use_tt && (ttm->page_flags & TTM_TT_FLAG_DECRYPTED))) {
		/*
		 * We're mapping a single page, and the desired
		 * page protection is consistent with the bo.
		 */

		map->bo_kmap_type = ttm_bo_map_kmap;
		map->page = ttm->pages[start_page];
		map->virtual = kmap(map->page);
	} else {
		/*
		 * We need to use vmap to get the desired page protection
		 * or to make the buffer object look contiguous.
		 */
		prot = ttm_io_prot(bo, mem, PAGE_KERNEL);
		map->bo_kmap_type = ttm_bo_map_vmap;
		map->virtual = vmap(ttm->pages + start_page, num_pages,
				    0, prot);
	}
	return (!map->virtual) ? -ENOMEM : 0;
}

/**
 * ttm_bo_kmap
 *
 * @bo: The buffer object.
 * @start_page: The first page to map.
 * @num_pages: Number of pages to map.
 * @map: pointer to a struct ttm_bo_kmap_obj representing the map.
 *
 * Sets up a kernel virtual mapping, using ioremap, vmap or kmap to the
 * data in the buffer object. The ttm_kmap_obj_virtual function can then be
 * used to obtain a virtual address to the data.
 *
 * Returns
 * -ENOMEM: Out of memory.
 * -EINVAL: Invalid range.
 */
int ttm_bo_kmap(struct ttm_buffer_object *bo,
		unsigned long start_page, unsigned long num_pages,
		struct ttm_bo_kmap_obj *map)
{
	unsigned long offset, size;
	int ret;

	map->virtual = NULL;
	map->bo = bo;
	if (num_pages > PFN_UP(bo->resource->size))
		return -EINVAL;
	if ((start_page + num_pages) > PFN_UP(bo->resource->size))
		return -EINVAL;

	ret = ttm_mem_io_reserve(bo->bdev, bo->resource);
	if (ret)
		return ret;
	if (!bo->resource->bus.is_iomem) {
		return ttm_bo_kmap_ttm(bo, start_page, num_pages, map);
	} else {
		offset = start_page << PAGE_SHIFT;
		size = num_pages << PAGE_SHIFT;
		return ttm_bo_ioremap(bo, offset, size, map);
	}
}
EXPORT_SYMBOL(ttm_bo_kmap);

/**
 * ttm_bo_kunmap
 *
 * @map: Object describing the map to unmap.
 *
 * Unmaps a kernel map set up by ttm_bo_kmap.
 */
void ttm_bo_kunmap(struct ttm_bo_kmap_obj *map)
{
	if (!map->virtual)
		return;
	switch (map->bo_kmap_type) {
	case ttm_bo_map_iomap:
		iounmap(map->virtual);
		break;
	case ttm_bo_map_vmap:
		vunmap(map->virtual);
		break;
	case ttm_bo_map_kmap:
		kunmap(map->page);
		break;
	case ttm_bo_map_premapped:
		break;
	default:
		BUG();
	}
	ttm_mem_io_free(map->bo->bdev, map->bo->resource);
	map->virtual = NULL;
	map->page = NULL;
}
EXPORT_SYMBOL(ttm_bo_kunmap);

/**
 * ttm_bo_vmap
 *
 * @bo: The buffer object.
 * @map: pointer to a struct iosys_map representing the map.
 *
 * Sets up a kernel virtual mapping, using ioremap or vmap to the
 * data in the buffer object. The parameter @map returns the virtual
 * address as struct iosys_map. Unmap the buffer with ttm_bo_vunmap().
 *
 * Returns
 * -ENOMEM: Out of memory.
 * -EINVAL: Invalid range.
 */
int ttm_bo_vmap(struct ttm_buffer_object *bo, struct iosys_map *map)
{
	struct ttm_resource *mem = bo->resource;
	int ret;

	dma_resv_assert_held(bo->base.resv);

	ret = ttm_mem_io_reserve(bo->bdev, mem);
	if (ret)
		return ret;

	if (mem->bus.is_iomem) {
		void __iomem *vaddr_iomem;

		if (mem->bus.addr)
			vaddr_iomem = (void __iomem *)mem->bus.addr;
		else if (mem->bus.caching == ttm_write_combined)
			vaddr_iomem = ioremap_wc(mem->bus.offset,
						 bo->base.size);
#ifdef CONFIG_X86
		else if (mem->bus.caching == ttm_cached)
			vaddr_iomem = ioremap_cache(mem->bus.offset,
						  bo->base.size);
#endif
		else
			vaddr_iomem = ioremap(mem->bus.offset, bo->base.size);

		if (!vaddr_iomem)
			return -ENOMEM;

		iosys_map_set_vaddr_iomem(map, vaddr_iomem);

	} else {
		struct ttm_operation_ctx ctx = {
			.interruptible = false,
			.no_wait_gpu = false
		};
		struct ttm_tt *ttm = bo->ttm;
		pgprot_t prot;
		void *vaddr;

		ret = ttm_bo_populate(bo, &ctx);
		if (ret)
			return ret;

		/*
		 * We need to use vmap to get the desired page protection
		 * or to make the buffer object look contiguous.
		 */
		prot = ttm_io_prot(bo, mem, PAGE_KERNEL);
		vaddr = vmap(ttm->pages, ttm->num_pages, 0, prot);
		if (!vaddr)
			return -ENOMEM;

		iosys_map_set_vaddr(map, vaddr);
	}

	return 0;
}
EXPORT_SYMBOL(ttm_bo_vmap);

/**
 * ttm_bo_vunmap
 *
 * @bo: The buffer object.
 * @map: Object describing the map to unmap.
 *
 * Unmaps a kernel map set up by ttm_bo_vmap().
 */
void ttm_bo_vunmap(struct ttm_buffer_object *bo, struct iosys_map *map)
{
	struct ttm_resource *mem = bo->resource;

	dma_resv_assert_held(bo->base.resv);

	if (iosys_map_is_null(map))
		return;

	if (!map->is_iomem)
		vunmap(map->vaddr);
	else if (!mem->bus.addr)
		iounmap(map->vaddr_iomem);
	iosys_map_clear(map);

	ttm_mem_io_free(bo->bdev, bo->resource);
}
EXPORT_SYMBOL(ttm_bo_vunmap);

static int ttm_bo_wait_free_node(struct ttm_buffer_object *bo,
				 bool dst_use_tt)
{
	long ret;

	ret = dma_resv_wait_timeout(bo->base.resv, DMA_RESV_USAGE_BOOKKEEP,
				    false, 15 * HZ);
	if (ret == 0)
		return -EBUSY;
	if (ret < 0)
		return ret;

	if (!dst_use_tt)
		ttm_bo_tt_destroy(bo);
	ttm_resource_free(bo, &bo->resource);
	return 0;
}

static int ttm_bo_move_to_ghost(struct ttm_buffer_object *bo,
				struct dma_fence *fence,
				bool dst_use_tt)
{
	struct ttm_buffer_object *ghost_obj;
	int ret;

	/**
	 * This should help pipeline ordinary buffer moves.
	 *
	 * Hang old buffer memory on a new buffer object,
	 * and leave it to be released when the GPU
	 * operation has completed.
	 */

	ret = ttm_buffer_object_transfer(bo, &ghost_obj);
	if (ret)
		return ret;

	dma_resv_add_fence(&ghost_obj->base._resv, fence,
			   DMA_RESV_USAGE_KERNEL);

	/**
	 * If we're not moving to fixed memory, the TTM object
	 * needs to stay alive. Otherwhise hang it on the ghost
	 * bo to be unbound and destroyed.
	 */

	if (dst_use_tt)
		ghost_obj->ttm = NULL;
	else
		bo->ttm = NULL;

	dma_resv_unlock(&ghost_obj->base._resv);
	ttm_bo_put(ghost_obj);
	return 0;
}

static void ttm_bo_move_pipeline_evict(struct ttm_buffer_object *bo,
				       struct dma_fence *fence)
{
	struct ttm_device *bdev = bo->bdev;
	struct ttm_resource_manager *from;

	from = ttm_manager_type(bdev, bo->resource->mem_type);

	/**
	 * BO doesn't have a TTM we need to bind/unbind. Just remember
	 * this eviction and free up the allocation
	 */
	spin_lock(&from->move_lock);
	if (!from->move || dma_fence_is_later(fence, from->move)) {
		dma_fence_put(from->move);
		from->move = dma_fence_get(fence);
	}
	spin_unlock(&from->move_lock);

	ttm_resource_free(bo, &bo->resource);
}

/**
 * ttm_bo_move_accel_cleanup - cleanup helper for hw copies
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @fence: A fence object that signals when moving is complete.
 * @evict: This is an evict move. Don't return until the buffer is idle.
 * @pipeline: evictions are to be pipelined.
 * @new_mem: struct ttm_resource indicating where to move.
 *
 * Accelerated move function to be called when an accelerated move
 * has been scheduled. The function will create a new temporary buffer object
 * representing the old placement, and put the sync object on both buffer
 * objects. After that the newly created buffer object is unref'd to be
 * destroyed when the move is complete. This will help pipeline
 * buffer moves.
 */
int ttm_bo_move_accel_cleanup(struct ttm_buffer_object *bo,
			      struct dma_fence *fence,
			      bool evict,
			      bool pipeline,
			      struct ttm_resource *new_mem)
{
	struct ttm_device *bdev = bo->bdev;
	struct ttm_resource_manager *from = ttm_manager_type(bdev, bo->resource->mem_type);
	struct ttm_resource_manager *man = ttm_manager_type(bdev, new_mem->mem_type);
	int ret = 0;

	dma_resv_add_fence(bo->base.resv, fence, DMA_RESV_USAGE_KERNEL);
	if (!evict)
		ret = ttm_bo_move_to_ghost(bo, fence, man->use_tt);
	else if (!from->use_tt && pipeline)
		ttm_bo_move_pipeline_evict(bo, fence);
	else
		ret = ttm_bo_wait_free_node(bo, man->use_tt);

	if (ret)
		return ret;

	ttm_bo_assign_mem(bo, new_mem);

	return 0;
}
EXPORT_SYMBOL(ttm_bo_move_accel_cleanup);

/**
 * ttm_bo_move_sync_cleanup - cleanup by waiting for the move to finish
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @new_mem: struct ttm_resource indicating where to move.
 *
 * Special case of ttm_bo_move_accel_cleanup where the bo is guaranteed
 * by the caller to be idle. Typically used after memcpy buffer moves.
 */
void ttm_bo_move_sync_cleanup(struct ttm_buffer_object *bo,
			      struct ttm_resource *new_mem)
{
	struct ttm_device *bdev = bo->bdev;
	struct ttm_resource_manager *man = ttm_manager_type(bdev, new_mem->mem_type);
	int ret;

	ret = ttm_bo_wait_free_node(bo, man->use_tt);
	if (WARN_ON(ret))
		return;

	ttm_bo_assign_mem(bo, new_mem);
}
EXPORT_SYMBOL(ttm_bo_move_sync_cleanup);

/**
 * ttm_bo_pipeline_gutting - purge the contents of a bo
 * @bo: The buffer object
 *
 * Purge the contents of a bo, async if the bo is not idle.
 * After a successful call, the bo is left unpopulated in
 * system placement. The function may wait uninterruptible
 * for idle on OOM.
 *
 * Return: 0 if successful, negative error code on failure.
 */
int ttm_bo_pipeline_gutting(struct ttm_buffer_object *bo)
{
	struct ttm_buffer_object *ghost;
	struct ttm_tt *ttm;
	int ret;

	/* If already idle, no need for ghost object dance. */
	if (dma_resv_test_signaled(bo->base.resv, DMA_RESV_USAGE_BOOKKEEP)) {
		if (!bo->ttm) {
			/* See comment below about clearing. */
			ret = ttm_tt_create(bo, true);
			if (ret)
				return ret;
		} else {
			ttm_tt_unpopulate(bo->bdev, bo->ttm);
			if (bo->type == ttm_bo_type_device)
				ttm_tt_mark_for_clear(bo->ttm);
		}
		ttm_resource_free(bo, &bo->resource);
		return 0;
	}

	/*
	 * We need an unpopulated ttm_tt after giving our current one,
	 * if any, to the ghost object. And we can't afford to fail
	 * creating one *after* the operation. If the bo subsequently gets
	 * resurrected, make sure it's cleared (if ttm_bo_type_device)
	 * to avoid leaking sensitive information to user-space.
	 */

	ttm = bo->ttm;
	bo->ttm = NULL;
	ret = ttm_tt_create(bo, true);
	swap(bo->ttm, ttm);
	if (ret)
		return ret;

	ret = ttm_buffer_object_transfer(bo, &ghost);
	if (ret)
		goto error_destroy_tt;

	ret = dma_resv_copy_fences(&ghost->base._resv, bo->base.resv);
	/* Last resort, wait for the BO to be idle when we are OOM */
	if (ret) {
		dma_resv_wait_timeout(bo->base.resv, DMA_RESV_USAGE_BOOKKEEP,
				      false, MAX_SCHEDULE_TIMEOUT);
	}

	dma_resv_unlock(&ghost->base._resv);
	ttm_bo_put(ghost);
	bo->ttm = ttm;
	return 0;

error_destroy_tt:
	ttm_tt_destroy(bo->bdev, ttm);
	return ret;
}

static bool ttm_lru_walk_trylock(struct ttm_operation_ctx *ctx,
				 struct ttm_buffer_object *bo,
				 bool *needs_unlock)
{
	*needs_unlock = false;

	if (dma_resv_trylock(bo->base.resv)) {
		*needs_unlock = true;
		return true;
	}

	if (bo->base.resv == ctx->resv && ctx->allow_res_evict) {
		dma_resv_assert_held(bo->base.resv);
		return true;
	}

	return false;
}

static int ttm_lru_walk_ticketlock(struct ttm_lru_walk *walk,
				   struct ttm_buffer_object *bo,
				   bool *needs_unlock)
{
	struct dma_resv *resv = bo->base.resv;
	int ret;

	if (walk->ctx->interruptible)
		ret = dma_resv_lock_interruptible(resv, walk->ticket);
	else
		ret = dma_resv_lock(resv, walk->ticket);

	if (!ret) {
		*needs_unlock = true;
		/*
		 * Only a single ticketlock per loop. Ticketlocks are prone
		 * to return -EDEADLK causing the eviction to fail, so
		 * after waiting for the ticketlock, revert back to
		 * trylocking for this walk.
		 */
		walk->ticket = NULL;
	} else if (ret == -EDEADLK) {
		/* Caller needs to exit the ww transaction. */
		ret = -ENOSPC;
	}

	return ret;
}

static void ttm_lru_walk_unlock(struct ttm_buffer_object *bo, bool locked)
{
	if (locked)
		dma_resv_unlock(bo->base.resv);
}

/**
 * ttm_lru_walk_for_evict() - Perform a LRU list walk, with actions taken on
 * valid items.
 * @walk: describe the walks and actions taken
 * @bdev: The TTM device.
 * @man: The struct ttm_resource manager whose LRU lists we're walking.
 * @target: The end condition for the walk.
 *
 * The LRU lists of @man are walk, and for each struct ttm_resource encountered,
 * the corresponding ttm_buffer_object is locked and taken a reference on, and
 * the LRU lock is dropped. the LRU lock may be dropped before locking and, in
 * that case, it's verified that the item actually remains on the LRU list after
 * the lock, and that the buffer object didn't switch resource in between.
 *
 * With a locked object, the actions indicated by @walk->process_bo are
 * performed, and after that, the bo is unlocked, the refcount dropped and the
 * next struct ttm_resource is processed. Here, the walker relies on
 * TTM's restartable LRU list implementation.
 *
 * Typically @walk->process_bo() would return the number of pages evicted,
 * swapped or shrunken, so that when the total exceeds @target, or when the
 * LRU list has been walked in full, iteration is terminated. It's also terminated
 * on error. Note that the definition of @target is done by the caller, it
 * could have a different meaning than the number of pages.
 *
 * Note that the way dma_resv individualization is done, locking needs to be done
 * either with the LRU lock held (trylocking only) or with a reference on the
 * object.
 *
 * Return: The progress made towards target or negative error code on error.
 */
s64 ttm_lru_walk_for_evict(struct ttm_lru_walk *walk, struct ttm_device *bdev,
			   struct ttm_resource_manager *man, s64 target)
{
	struct ttm_resource_cursor cursor;
	struct ttm_resource *res;
	s64 progress = 0;
	s64 lret;

	spin_lock(&bdev->lru_lock);
	ttm_resource_cursor_init(&cursor, man);
	ttm_resource_manager_for_each_res(&cursor, res) {
		struct ttm_buffer_object *bo = res->bo;
		bool bo_needs_unlock = false;
		bool bo_locked = false;
		int mem_type;

		/*
		 * Attempt a trylock before taking a reference on the bo,
		 * since if we do it the other way around, and the trylock fails,
		 * we need to drop the lru lock to put the bo.
		 */
		if (ttm_lru_walk_trylock(walk->ctx, bo, &bo_needs_unlock))
			bo_locked = true;
		else if (!walk->ticket || walk->ctx->no_wait_gpu ||
			 walk->trylock_only)
			continue;

		if (!ttm_bo_get_unless_zero(bo)) {
			ttm_lru_walk_unlock(bo, bo_needs_unlock);
			continue;
		}

		mem_type = res->mem_type;
		spin_unlock(&bdev->lru_lock);

		lret = 0;
		if (!bo_locked)
			lret = ttm_lru_walk_ticketlock(walk, bo, &bo_needs_unlock);

		/*
		 * Note that in between the release of the lru lock and the
		 * ticketlock, the bo may have switched resource,
		 * and also memory type, since the resource may have been
		 * freed and allocated again with a different memory type.
		 * In that case, just skip it.
		 */
		if (!lret && bo->resource && bo->resource->mem_type == mem_type)
			lret = walk->ops->process_bo(walk, bo);

		ttm_lru_walk_unlock(bo, bo_needs_unlock);
		ttm_bo_put(bo);
		if (lret == -EBUSY || lret == -EALREADY)
			lret = 0;
		progress = (lret < 0) ? lret : progress + lret;

		spin_lock(&bdev->lru_lock);
		if (progress < 0 || progress >= target)
			break;
	}
	ttm_resource_cursor_fini(&cursor);
	spin_unlock(&bdev->lru_lock);

	return progress;
}
EXPORT_SYMBOL(ttm_lru_walk_for_evict);

static void ttm_bo_lru_cursor_cleanup_bo(struct ttm_bo_lru_cursor *curs)
{
	struct ttm_buffer_object *bo = curs->bo;

	if (bo) {
		if (curs->needs_unlock)
			dma_resv_unlock(bo->base.resv);
		ttm_bo_put(bo);
		curs->bo = NULL;
	}
}

/**
 * ttm_bo_lru_cursor_fini() - Stop using a struct ttm_bo_lru_cursor
 * and clean up any iteration it was used for.
 * @curs: The cursor.
 */
void ttm_bo_lru_cursor_fini(struct ttm_bo_lru_cursor *curs)
{
	spinlock_t *lru_lock = &curs->res_curs.man->bdev->lru_lock;

	ttm_bo_lru_cursor_cleanup_bo(curs);
	spin_lock(lru_lock);
	ttm_resource_cursor_fini(&curs->res_curs);
	spin_unlock(lru_lock);
}
EXPORT_SYMBOL(ttm_bo_lru_cursor_fini);

/**
 * ttm_bo_lru_cursor_init() - Initialize a struct ttm_bo_lru_cursor
 * @curs: The ttm_bo_lru_cursor to initialize.
 * @man: The ttm resource_manager whose LRU lists to iterate over.
 * @ctx: The ttm_operation_ctx to govern the locking.
 *
 * Initialize a struct ttm_bo_lru_cursor. Currently only trylocking
 * or prelocked buffer objects are available as detailed by
 * @ctx::resv and @ctx::allow_res_evict. Ticketlocking is not
 * supported.
 *
 * Return: Pointer to @curs. The function does not fail.
 */
struct ttm_bo_lru_cursor *
ttm_bo_lru_cursor_init(struct ttm_bo_lru_cursor *curs,
		       struct ttm_resource_manager *man,
		       struct ttm_operation_ctx *ctx)
{
	memset(curs, 0, sizeof(*curs));
	ttm_resource_cursor_init(&curs->res_curs, man);
	curs->ctx = ctx;

	return curs;
}
EXPORT_SYMBOL(ttm_bo_lru_cursor_init);

static struct ttm_buffer_object *
ttm_bo_from_res_reserved(struct ttm_resource *res, struct ttm_bo_lru_cursor *curs)
{
	struct ttm_buffer_object *bo = res->bo;

	if (!ttm_lru_walk_trylock(curs->ctx, bo, &curs->needs_unlock))
		return NULL;

	if (!ttm_bo_get_unless_zero(bo)) {
		if (curs->needs_unlock)
			dma_resv_unlock(bo->base.resv);
		return NULL;
	}

	curs->bo = bo;
	return bo;
}

/**
 * ttm_bo_lru_cursor_next() - Continue iterating a manager's LRU lists
 * to find and lock buffer object.
 * @curs: The cursor initialized using ttm_bo_lru_cursor_init() and
 * ttm_bo_lru_cursor_first().
 *
 * Return: A pointer to a locked and reference-counted buffer object,
 * or NULL if none could be found and looping should be terminated.
 */
struct ttm_buffer_object *ttm_bo_lru_cursor_next(struct ttm_bo_lru_cursor *curs)
{
	spinlock_t *lru_lock = &curs->res_curs.man->bdev->lru_lock;
	struct ttm_resource *res = NULL;
	struct ttm_buffer_object *bo;

	ttm_bo_lru_cursor_cleanup_bo(curs);

	spin_lock(lru_lock);
	for (;;) {
		res = ttm_resource_manager_next(&curs->res_curs);
		if (!res)
			break;

		bo = ttm_bo_from_res_reserved(res, curs);
		if (bo)
			break;
	}

	spin_unlock(lru_lock);
	return res ? bo : NULL;
}
EXPORT_SYMBOL(ttm_bo_lru_cursor_next);

/**
 * ttm_bo_lru_cursor_first() - Start iterating a manager's LRU lists
 * to find and lock buffer object.
 * @curs: The cursor initialized using ttm_bo_lru_cursor_init().
 *
 * Return: A pointer to a locked and reference-counted buffer object,
 * or NULL if none could be found and looping should be terminated.
 */
struct ttm_buffer_object *ttm_bo_lru_cursor_first(struct ttm_bo_lru_cursor *curs)
{
	spinlock_t *lru_lock = &curs->res_curs.man->bdev->lru_lock;
	struct ttm_buffer_object *bo;
	struct ttm_resource *res;

	spin_lock(lru_lock);
	res = ttm_resource_manager_first(&curs->res_curs);
	if (!res) {
		spin_unlock(lru_lock);
		return NULL;
	}

	bo = ttm_bo_from_res_reserved(res, curs);
	spin_unlock(lru_lock);

	return bo ? bo : ttm_bo_lru_cursor_next(curs);
}
EXPORT_SYMBOL(ttm_bo_lru_cursor_first);

/**
 * ttm_bo_shrink() - Helper to shrink a ttm buffer object.
 * @ctx: The struct ttm_operation_ctx used for the shrinking operation.
 * @bo: The buffer object.
 * @flags: Flags governing the shrinking behaviour.
 *
 * The function uses the ttm_tt_back_up functionality to back up or
 * purge a struct ttm_tt. If the bo is not in system, it's first
 * moved there.
 *
 * Return: The number of pages shrunken or purged, or
 * negative error code on failure.
 */
long ttm_bo_shrink(struct ttm_operation_ctx *ctx, struct ttm_buffer_object *bo,
		   const struct ttm_bo_shrink_flags flags)
{
	static const struct ttm_place sys_placement_flags = {
		.fpfn = 0,
		.lpfn = 0,
		.mem_type = TTM_PL_SYSTEM,
		.flags = 0,
	};
	static struct ttm_placement sys_placement = {
		.num_placement = 1,
		.placement = &sys_placement_flags,
	};
	struct ttm_tt *tt = bo->ttm;
	long lret;

	dma_resv_assert_held(bo->base.resv);

	if (flags.allow_move && bo->resource->mem_type != TTM_PL_SYSTEM) {
		int ret = ttm_bo_validate(bo, &sys_placement, ctx);

		/* Consider -ENOMEM and -ENOSPC non-fatal. */
		if (ret) {
			if (ret == -ENOMEM || ret == -ENOSPC)
				ret = -EBUSY;
			return ret;
		}
	}

	ttm_bo_unmap_virtual(bo);
	lret = ttm_bo_wait_ctx(bo, ctx);
	if (lret < 0)
		return lret;

	if (bo->bulk_move) {
		spin_lock(&bo->bdev->lru_lock);
		ttm_resource_del_bulk_move(bo->resource, bo);
		spin_unlock(&bo->bdev->lru_lock);
	}

	lret = ttm_tt_backup(bo->bdev, tt, (struct ttm_backup_flags)
			     {.purge = flags.purge,
			      .writeback = flags.writeback});

	if (lret <= 0 && bo->bulk_move) {
		spin_lock(&bo->bdev->lru_lock);
		ttm_resource_add_bulk_move(bo->resource, bo);
		spin_unlock(&bo->bdev->lru_lock);
	}

	if (lret < 0 && lret != -EINTR)
		return -EBUSY;

	return lret;
}
EXPORT_SYMBOL(ttm_bo_shrink);

/**
 * ttm_bo_shrink_suitable() - Whether a bo is suitable for shinking
 * @ctx: The struct ttm_operation_ctx governing the shrinking.
 * @bo: The candidate for shrinking.
 *
 * Check whether the object, given the information available to TTM,
 * is suitable for shinking, This function can and should be used
 * before attempting to shrink an object.
 *
 * Return: true if suitable. false if not.
 */
bool ttm_bo_shrink_suitable(struct ttm_buffer_object *bo, struct ttm_operation_ctx *ctx)
{
	return bo->ttm && ttm_tt_is_populated(bo->ttm) && !bo->pin_count &&
		(!ctx->no_wait_gpu ||
		 dma_resv_test_signaled(bo->base.resv, DMA_RESV_USAGE_BOOKKEEP));
}
EXPORT_SYMBOL(ttm_bo_shrink_suitable);

/**
 * ttm_bo_shrink_avoid_wait() - Whether to avoid waiting for GPU
 * during shrinking
 *
 * In some situations, like direct reclaim, waiting (in particular gpu waiting)
 * should be avoided since it may stall a system that could otherwise make progress
 * shrinking something else less time consuming.
 *
 * Return: true if gpu waiting should be avoided, false if not.
 */
bool ttm_bo_shrink_avoid_wait(void)
{
	return !current_is_kswapd();
}
EXPORT_SYMBOL(ttm_bo_shrink_avoid_wait);

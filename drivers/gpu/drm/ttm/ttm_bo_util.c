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

#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/drm_vma_manager.h>
#include <linux/io.h>
#include <linux/highmem.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/dma-resv.h>

struct ttm_transfer_obj {
	struct ttm_buffer_object base;
	struct ttm_buffer_object *bo;
};

void ttm_bo_free_old_node(struct ttm_buffer_object *bo)
{
	ttm_resource_free(bo, &bo->mem);
}

int ttm_bo_move_ttm(struct ttm_buffer_object *bo,
		   struct ttm_operation_ctx *ctx,
		    struct ttm_resource *new_mem)
{
	struct ttm_tt *ttm = bo->ttm;
	struct ttm_resource *old_mem = &bo->mem;
	int ret;

	if (old_mem->mem_type != TTM_PL_SYSTEM) {
		ret = ttm_bo_wait(bo, ctx->interruptible, ctx->no_wait_gpu);

		if (unlikely(ret != 0)) {
			if (ret != -ERESTARTSYS)
				pr_err("Failed to expire sync object before unbinding TTM\n");
			return ret;
		}

		ttm_bo_tt_unbind(bo);
		ttm_bo_free_old_node(bo);
		old_mem->mem_type = TTM_PL_SYSTEM;
	}

	ret = ttm_tt_set_placement_caching(ttm, new_mem->placement);
	if (unlikely(ret != 0))
		return ret;

	if (new_mem->mem_type != TTM_PL_SYSTEM) {

		ret = ttm_tt_populate(bo->bdev, ttm, ctx);
		if (unlikely(ret != 0))
			return ret;

		ret = ttm_bo_tt_bind(bo, new_mem);
		if (unlikely(ret != 0))
			return ret;
	}

	ttm_bo_assign_mem(bo, new_mem);
	return 0;
}
EXPORT_SYMBOL(ttm_bo_move_ttm);

int ttm_mem_io_reserve(struct ttm_bo_device *bdev,
		       struct ttm_resource *mem)
{
	if (mem->bus.offset || mem->bus.addr)
		return 0;

	mem->bus.is_iomem = false;
	if (!bdev->driver->io_mem_reserve)
		return 0;

	return bdev->driver->io_mem_reserve(bdev, mem);
}

void ttm_mem_io_free(struct ttm_bo_device *bdev,
		     struct ttm_resource *mem)
{
	if (!mem->bus.offset && !mem->bus.addr)
		return;

	if (bdev->driver->io_mem_free)
		bdev->driver->io_mem_free(bdev, mem);

	mem->bus.offset = 0;
	mem->bus.addr = NULL;
}

static int ttm_resource_ioremap(struct ttm_bo_device *bdev,
			       struct ttm_resource *mem,
			       void **virtual)
{
	int ret;
	void *addr;

	*virtual = NULL;
	ret = ttm_mem_io_reserve(bdev, mem);
	if (ret || !mem->bus.is_iomem)
		return ret;

	if (mem->bus.addr) {
		addr = mem->bus.addr;
	} else {
		size_t bus_size = (size_t)mem->num_pages << PAGE_SHIFT;

		if (mem->placement & TTM_PL_FLAG_WC)
			addr = ioremap_wc(mem->bus.offset, bus_size);
		else
			addr = ioremap(mem->bus.offset, bus_size);
		if (!addr) {
			ttm_mem_io_free(bdev, mem);
			return -ENOMEM;
		}
	}
	*virtual = addr;
	return 0;
}

static void ttm_resource_iounmap(struct ttm_bo_device *bdev,
				struct ttm_resource *mem,
				void *virtual)
{
	if (virtual && mem->bus.addr == NULL)
		iounmap(virtual);
	ttm_mem_io_free(bdev, mem);
}

static int ttm_copy_io_page(void *dst, void *src, unsigned long page)
{
	uint32_t *dstP =
	    (uint32_t *) ((unsigned long)dst + (page << PAGE_SHIFT));
	uint32_t *srcP =
	    (uint32_t *) ((unsigned long)src + (page << PAGE_SHIFT));

	int i;
	for (i = 0; i < PAGE_SIZE / sizeof(uint32_t); ++i)
		iowrite32(ioread32(srcP++), dstP++);
	return 0;
}

static int ttm_copy_io_ttm_page(struct ttm_tt *ttm, void *src,
				unsigned long page,
				pgprot_t prot)
{
	struct page *d = ttm->pages[page];
	void *dst;

	if (!d)
		return -ENOMEM;

	src = (void *)((unsigned long)src + (page << PAGE_SHIFT));
	dst = kmap_atomic_prot(d, prot);
	if (!dst)
		return -ENOMEM;

	memcpy_fromio(dst, src, PAGE_SIZE);

	kunmap_atomic(dst);

	return 0;
}

static int ttm_copy_ttm_io_page(struct ttm_tt *ttm, void *dst,
				unsigned long page,
				pgprot_t prot)
{
	struct page *s = ttm->pages[page];
	void *src;

	if (!s)
		return -ENOMEM;

	dst = (void *)((unsigned long)dst + (page << PAGE_SHIFT));
	src = kmap_atomic_prot(s, prot);
	if (!src)
		return -ENOMEM;

	memcpy_toio(dst, src, PAGE_SIZE);

	kunmap_atomic(src);

	return 0;
}

int ttm_bo_move_memcpy(struct ttm_buffer_object *bo,
		       struct ttm_operation_ctx *ctx,
		       struct ttm_resource *new_mem)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_resource_manager *man = ttm_manager_type(bdev, new_mem->mem_type);
	struct ttm_tt *ttm = bo->ttm;
	struct ttm_resource *old_mem = &bo->mem;
	struct ttm_resource old_copy = *old_mem;
	void *old_iomap;
	void *new_iomap;
	int ret;
	unsigned long i;
	unsigned long page;
	unsigned long add = 0;
	int dir;

	ret = ttm_bo_wait(bo, ctx->interruptible, ctx->no_wait_gpu);
	if (ret)
		return ret;

	ret = ttm_resource_ioremap(bdev, old_mem, &old_iomap);
	if (ret)
		return ret;
	ret = ttm_resource_ioremap(bdev, new_mem, &new_iomap);
	if (ret)
		goto out;

	/*
	 * Single TTM move. NOP.
	 */
	if (old_iomap == NULL && new_iomap == NULL)
		goto out2;

	/*
	 * Don't move nonexistent data. Clear destination instead.
	 */
	if (old_iomap == NULL &&
	    (ttm == NULL || (!ttm_tt_is_populated(ttm) &&
			     !(ttm->page_flags & TTM_PAGE_FLAG_SWAPPED)))) {
		memset_io(new_iomap, 0, new_mem->num_pages*PAGE_SIZE);
		goto out2;
	}

	/*
	 * TTM might be null for moves within the same region.
	 */
	if (ttm) {
		ret = ttm_tt_populate(bdev, ttm, ctx);
		if (ret)
			goto out1;
	}

	add = 0;
	dir = 1;

	if ((old_mem->mem_type == new_mem->mem_type) &&
	    (new_mem->start < old_mem->start + old_mem->size)) {
		dir = -1;
		add = new_mem->num_pages - 1;
	}

	for (i = 0; i < new_mem->num_pages; ++i) {
		page = i * dir + add;
		if (old_iomap == NULL) {
			pgprot_t prot = ttm_io_prot(old_mem->placement,
						    PAGE_KERNEL);
			ret = ttm_copy_ttm_io_page(ttm, new_iomap, page,
						   prot);
		} else if (new_iomap == NULL) {
			pgprot_t prot = ttm_io_prot(new_mem->placement,
						    PAGE_KERNEL);
			ret = ttm_copy_io_ttm_page(ttm, old_iomap, page,
						   prot);
		} else {
			ret = ttm_copy_io_page(new_iomap, old_iomap, page);
		}
		if (ret)
			goto out1;
	}
	mb();
out2:
	old_copy = *old_mem;

	ttm_bo_assign_mem(bo, new_mem);

	if (!man->use_tt)
		ttm_bo_tt_destroy(bo);

out1:
	ttm_resource_iounmap(bdev, old_mem, new_iomap);
out:
	ttm_resource_iounmap(bdev, &old_copy, old_iomap);

	/*
	 * On error, keep the mm node!
	 */
	if (!ret)
		ttm_resource_free(bo, &old_copy);
	return ret;
}
EXPORT_SYMBOL(ttm_bo_move_memcpy);

static void ttm_transfered_destroy(struct ttm_buffer_object *bo)
{
	struct ttm_transfer_obj *fbo;

	fbo = container_of(bo, struct ttm_transfer_obj, base);
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

	ttm_bo_get(bo);
	fbo->bo = bo;

	/**
	 * Fix up members that we shouldn't copy directly:
	 * TODO: Explicit member copy would probably be better here.
	 */

	atomic_inc(&ttm_bo_glob.bo_count);
	INIT_LIST_HEAD(&fbo->base.ddestroy);
	INIT_LIST_HEAD(&fbo->base.lru);
	INIT_LIST_HEAD(&fbo->base.swap);
	fbo->base.moving = NULL;
	drm_vma_node_reset(&fbo->base.base.vma_node);

	kref_init(&fbo->base.kref);
	fbo->base.destroy = &ttm_transfered_destroy;
	fbo->base.acc_size = 0;
	fbo->base.pin_count = 1;
	if (bo->type != ttm_bo_type_sg)
		fbo->base.base.resv = &fbo->base.base._resv;

	dma_resv_init(&fbo->base.base._resv);
	fbo->base.base.dev = NULL;
	ret = dma_resv_trylock(&fbo->base.base._resv);
	WARN_ON(!ret);

	*new_obj = &fbo->base;
	return 0;
}

pgprot_t ttm_io_prot(uint32_t caching_flags, pgprot_t tmp)
{
	/* Cached mappings need no adjustment */
	if (caching_flags & TTM_PL_FLAG_CACHED)
		return tmp;

#if defined(__i386__) || defined(__x86_64__)
	if (caching_flags & TTM_PL_FLAG_WC)
		tmp = pgprot_writecombine(tmp);
	else if (boot_cpu_data.x86 > 3)
		tmp = pgprot_noncached(tmp);
#endif
#if defined(__ia64__) || defined(__arm__) || defined(__aarch64__) || \
    defined(__powerpc__) || defined(__mips__)
	if (caching_flags & TTM_PL_FLAG_WC)
		tmp = pgprot_writecombine(tmp);
	else
		tmp = pgprot_noncached(tmp);
#endif
#if defined(__sparc__)
	tmp = pgprot_noncached(tmp);
#endif
	return tmp;
}
EXPORT_SYMBOL(ttm_io_prot);

static int ttm_bo_ioremap(struct ttm_buffer_object *bo,
			  unsigned long offset,
			  unsigned long size,
			  struct ttm_bo_kmap_obj *map)
{
	struct ttm_resource *mem = &bo->mem;

	if (bo->mem.bus.addr) {
		map->bo_kmap_type = ttm_bo_map_premapped;
		map->virtual = (void *)(((u8 *)bo->mem.bus.addr) + offset);
	} else {
		map->bo_kmap_type = ttm_bo_map_iomap;
		if (mem->placement & TTM_PL_FLAG_WC)
			map->virtual = ioremap_wc(bo->mem.bus.offset + offset,
						  size);
		else
			map->virtual = ioremap(bo->mem.bus.offset + offset,
					       size);
	}
	return (!map->virtual) ? -ENOMEM : 0;
}

static int ttm_bo_kmap_ttm(struct ttm_buffer_object *bo,
			   unsigned long start_page,
			   unsigned long num_pages,
			   struct ttm_bo_kmap_obj *map)
{
	struct ttm_resource *mem = &bo->mem;
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = false
	};
	struct ttm_tt *ttm = bo->ttm;
	pgprot_t prot;
	int ret;

	BUG_ON(!ttm);

	ret = ttm_tt_populate(bo->bdev, ttm, &ctx);
	if (ret)
		return ret;

	if (num_pages == 1 && (mem->placement & TTM_PL_FLAG_CACHED)) {
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
		prot = ttm_io_prot(mem->placement, PAGE_KERNEL);
		map->bo_kmap_type = ttm_bo_map_vmap;
		map->virtual = vmap(ttm->pages + start_page, num_pages,
				    0, prot);
	}
	return (!map->virtual) ? -ENOMEM : 0;
}

int ttm_bo_kmap(struct ttm_buffer_object *bo,
		unsigned long start_page, unsigned long num_pages,
		struct ttm_bo_kmap_obj *map)
{
	unsigned long offset, size;
	int ret;

	map->virtual = NULL;
	map->bo = bo;
	if (num_pages > bo->num_pages)
		return -EINVAL;
	if (start_page > bo->num_pages)
		return -EINVAL;

	ret = ttm_mem_io_reserve(bo->bdev, &bo->mem);
	if (ret)
		return ret;
	if (!bo->mem.bus.is_iomem) {
		return ttm_bo_kmap_ttm(bo, start_page, num_pages, map);
	} else {
		offset = start_page << PAGE_SHIFT;
		size = num_pages << PAGE_SHIFT;
		return ttm_bo_ioremap(bo, offset, size, map);
	}
}
EXPORT_SYMBOL(ttm_bo_kmap);

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
	ttm_mem_io_free(map->bo->bdev, &map->bo->mem);
	map->virtual = NULL;
	map->page = NULL;
}
EXPORT_SYMBOL(ttm_bo_kunmap);

static int ttm_bo_wait_free_node(struct ttm_buffer_object *bo,
				 bool dst_use_tt)
{
	int ret;
	ret = ttm_bo_wait(bo, false, false);
	if (ret)
		return ret;

	if (!dst_use_tt)
		ttm_bo_tt_destroy(bo);
	ttm_bo_free_old_node(bo);
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

	dma_fence_put(bo->moving);
	bo->moving = dma_fence_get(fence);

	ret = ttm_buffer_object_transfer(bo, &ghost_obj);
	if (ret)
		return ret;

	dma_resv_add_excl_fence(&ghost_obj->base._resv, fence);

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
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_resource_manager *from = ttm_manager_type(bdev, bo->mem.mem_type);

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

	ttm_bo_free_old_node(bo);

	dma_fence_put(bo->moving);
	bo->moving = dma_fence_get(fence);
}

int ttm_bo_move_accel_cleanup(struct ttm_buffer_object *bo,
			      struct dma_fence *fence,
			      bool evict,
			      bool pipeline,
			      struct ttm_resource *new_mem)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_resource_manager *from = ttm_manager_type(bdev, bo->mem.mem_type);
	struct ttm_resource_manager *man = ttm_manager_type(bdev, new_mem->mem_type);
	int ret = 0;

	dma_resv_add_excl_fence(bo->base.resv, fence);
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

int ttm_bo_pipeline_gutting(struct ttm_buffer_object *bo)
{
	struct ttm_buffer_object *ghost;
	int ret;

	ret = ttm_buffer_object_transfer(bo, &ghost);
	if (ret)
		return ret;

	ret = dma_resv_copy_fences(&ghost->base._resv, bo->base.resv);
	/* Last resort, wait for the BO to be idle when we are OOM */
	if (ret)
		ttm_bo_wait(bo, false, false);

	memset(&bo->mem, 0, sizeof(bo->mem));
	bo->mem.mem_type = TTM_PL_SYSTEM;
	bo->ttm = NULL;

	dma_resv_unlock(&ghost->base._resv);
	ttm_bo_put(ghost);

	return 0;
}

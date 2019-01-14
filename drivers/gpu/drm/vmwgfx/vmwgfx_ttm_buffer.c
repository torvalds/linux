// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2009-2015 VMware, Inc., Palo Alto, CA., USA
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

#include "vmwgfx_drv.h"
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_page_alloc.h>

static const struct ttm_place vram_placement_flags = {
	.fpfn = 0,
	.lpfn = 0,
	.flags = TTM_PL_FLAG_VRAM | TTM_PL_FLAG_CACHED
};

static const struct ttm_place vram_ne_placement_flags = {
	.fpfn = 0,
	.lpfn = 0,
	.flags = TTM_PL_FLAG_VRAM | TTM_PL_FLAG_CACHED | TTM_PL_FLAG_NO_EVICT
};

static const struct ttm_place sys_placement_flags = {
	.fpfn = 0,
	.lpfn = 0,
	.flags = TTM_PL_FLAG_SYSTEM | TTM_PL_FLAG_CACHED
};

static const struct ttm_place sys_ne_placement_flags = {
	.fpfn = 0,
	.lpfn = 0,
	.flags = TTM_PL_FLAG_SYSTEM | TTM_PL_FLAG_CACHED | TTM_PL_FLAG_NO_EVICT
};

static const struct ttm_place gmr_placement_flags = {
	.fpfn = 0,
	.lpfn = 0,
	.flags = VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED
};

static const struct ttm_place gmr_ne_placement_flags = {
	.fpfn = 0,
	.lpfn = 0,
	.flags = VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED | TTM_PL_FLAG_NO_EVICT
};

static const struct ttm_place mob_placement_flags = {
	.fpfn = 0,
	.lpfn = 0,
	.flags = VMW_PL_FLAG_MOB | TTM_PL_FLAG_CACHED
};

static const struct ttm_place mob_ne_placement_flags = {
	.fpfn = 0,
	.lpfn = 0,
	.flags = VMW_PL_FLAG_MOB | TTM_PL_FLAG_CACHED | TTM_PL_FLAG_NO_EVICT
};

struct ttm_placement vmw_vram_placement = {
	.num_placement = 1,
	.placement = &vram_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &vram_placement_flags
};

static const struct ttm_place vram_gmr_placement_flags[] = {
	{
		.fpfn = 0,
		.lpfn = 0,
		.flags = TTM_PL_FLAG_VRAM | TTM_PL_FLAG_CACHED
	}, {
		.fpfn = 0,
		.lpfn = 0,
		.flags = VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED
	}
};

static const struct ttm_place gmr_vram_placement_flags[] = {
	{
		.fpfn = 0,
		.lpfn = 0,
		.flags = VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED
	}, {
		.fpfn = 0,
		.lpfn = 0,
		.flags = TTM_PL_FLAG_VRAM | TTM_PL_FLAG_CACHED
	}
};

struct ttm_placement vmw_vram_gmr_placement = {
	.num_placement = 2,
	.placement = vram_gmr_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &gmr_placement_flags
};

static const struct ttm_place vram_gmr_ne_placement_flags[] = {
	{
		.fpfn = 0,
		.lpfn = 0,
		.flags = TTM_PL_FLAG_VRAM | TTM_PL_FLAG_CACHED |
			 TTM_PL_FLAG_NO_EVICT
	}, {
		.fpfn = 0,
		.lpfn = 0,
		.flags = VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED |
			 TTM_PL_FLAG_NO_EVICT
	}
};

struct ttm_placement vmw_vram_gmr_ne_placement = {
	.num_placement = 2,
	.placement = vram_gmr_ne_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &gmr_ne_placement_flags
};

struct ttm_placement vmw_vram_sys_placement = {
	.num_placement = 1,
	.placement = &vram_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &sys_placement_flags
};

struct ttm_placement vmw_vram_ne_placement = {
	.num_placement = 1,
	.placement = &vram_ne_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &vram_ne_placement_flags
};

struct ttm_placement vmw_sys_placement = {
	.num_placement = 1,
	.placement = &sys_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &sys_placement_flags
};

struct ttm_placement vmw_sys_ne_placement = {
	.num_placement = 1,
	.placement = &sys_ne_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &sys_ne_placement_flags
};

static const struct ttm_place evictable_placement_flags[] = {
	{
		.fpfn = 0,
		.lpfn = 0,
		.flags = TTM_PL_FLAG_SYSTEM | TTM_PL_FLAG_CACHED
	}, {
		.fpfn = 0,
		.lpfn = 0,
		.flags = TTM_PL_FLAG_VRAM | TTM_PL_FLAG_CACHED
	}, {
		.fpfn = 0,
		.lpfn = 0,
		.flags = VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED
	}, {
		.fpfn = 0,
		.lpfn = 0,
		.flags = VMW_PL_FLAG_MOB | TTM_PL_FLAG_CACHED
	}
};

static const struct ttm_place nonfixed_placement_flags[] = {
	{
		.fpfn = 0,
		.lpfn = 0,
		.flags = TTM_PL_FLAG_SYSTEM | TTM_PL_FLAG_CACHED
	}, {
		.fpfn = 0,
		.lpfn = 0,
		.flags = VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED
	}, {
		.fpfn = 0,
		.lpfn = 0,
		.flags = VMW_PL_FLAG_MOB | TTM_PL_FLAG_CACHED
	}
};

struct ttm_placement vmw_evictable_placement = {
	.num_placement = 4,
	.placement = evictable_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &sys_placement_flags
};

struct ttm_placement vmw_srf_placement = {
	.num_placement = 1,
	.num_busy_placement = 2,
	.placement = &gmr_placement_flags,
	.busy_placement = gmr_vram_placement_flags
};

struct ttm_placement vmw_mob_placement = {
	.num_placement = 1,
	.num_busy_placement = 1,
	.placement = &mob_placement_flags,
	.busy_placement = &mob_placement_flags
};

struct ttm_placement vmw_mob_ne_placement = {
	.num_placement = 1,
	.num_busy_placement = 1,
	.placement = &mob_ne_placement_flags,
	.busy_placement = &mob_ne_placement_flags
};

struct ttm_placement vmw_nonfixed_placement = {
	.num_placement = 3,
	.placement = nonfixed_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &sys_placement_flags
};

struct vmw_ttm_tt {
	struct ttm_dma_tt dma_ttm;
	struct vmw_private *dev_priv;
	int gmr_id;
	struct vmw_mob *mob;
	int mem_type;
	struct sg_table sgt;
	struct vmw_sg_table vsgt;
	uint64_t sg_alloc_size;
	bool mapped;
};

const size_t vmw_tt_size = sizeof(struct vmw_ttm_tt);

/**
 * Helper functions to advance a struct vmw_piter iterator.
 *
 * @viter: Pointer to the iterator.
 *
 * These functions return false if past the end of the list,
 * true otherwise. Functions are selected depending on the current
 * DMA mapping mode.
 */
static bool __vmw_piter_non_sg_next(struct vmw_piter *viter)
{
	return ++(viter->i) < viter->num_pages;
}

static bool __vmw_piter_sg_next(struct vmw_piter *viter)
{
	return __sg_page_iter_next(&viter->iter);
}


/**
 * Helper functions to return a pointer to the current page.
 *
 * @viter: Pointer to the iterator
 *
 * These functions return a pointer to the page currently
 * pointed to by @viter. Functions are selected depending on the
 * current mapping mode.
 */
static struct page *__vmw_piter_non_sg_page(struct vmw_piter *viter)
{
	return viter->pages[viter->i];
}

static struct page *__vmw_piter_sg_page(struct vmw_piter *viter)
{
	return sg_page_iter_page(&viter->iter);
}


/**
 * Helper functions to return the DMA address of the current page.
 *
 * @viter: Pointer to the iterator
 *
 * These functions return the DMA address of the page currently
 * pointed to by @viter. Functions are selected depending on the
 * current mapping mode.
 */
static dma_addr_t __vmw_piter_phys_addr(struct vmw_piter *viter)
{
	return page_to_phys(viter->pages[viter->i]);
}

static dma_addr_t __vmw_piter_dma_addr(struct vmw_piter *viter)
{
	return viter->addrs[viter->i];
}

static dma_addr_t __vmw_piter_sg_addr(struct vmw_piter *viter)
{
	return sg_page_iter_dma_address(&viter->iter);
}


/**
 * vmw_piter_start - Initialize a struct vmw_piter.
 *
 * @viter: Pointer to the iterator to initialize
 * @vsgt: Pointer to a struct vmw_sg_table to initialize from
 *
 * Note that we're following the convention of __sg_page_iter_start, so that
 * the iterator doesn't point to a valid page after initialization; it has
 * to be advanced one step first.
 */
void vmw_piter_start(struct vmw_piter *viter, const struct vmw_sg_table *vsgt,
		     unsigned long p_offset)
{
	viter->i = p_offset - 1;
	viter->num_pages = vsgt->num_pages;
	switch (vsgt->mode) {
	case vmw_dma_phys:
		viter->next = &__vmw_piter_non_sg_next;
		viter->dma_address = &__vmw_piter_phys_addr;
		viter->page = &__vmw_piter_non_sg_page;
		viter->pages = vsgt->pages;
		break;
	case vmw_dma_alloc_coherent:
		viter->next = &__vmw_piter_non_sg_next;
		viter->dma_address = &__vmw_piter_dma_addr;
		viter->page = &__vmw_piter_non_sg_page;
		viter->addrs = vsgt->addrs;
		viter->pages = vsgt->pages;
		break;
	case vmw_dma_map_populate:
	case vmw_dma_map_bind:
		viter->next = &__vmw_piter_sg_next;
		viter->dma_address = &__vmw_piter_sg_addr;
		viter->page = &__vmw_piter_sg_page;
		__sg_page_iter_start(&viter->iter, vsgt->sgt->sgl,
				     vsgt->sgt->orig_nents, p_offset);
		break;
	default:
		BUG();
	}
}

/**
 * vmw_ttm_unmap_from_dma - unmap  device addresses previsouly mapped for
 * TTM pages
 *
 * @vmw_tt: Pointer to a struct vmw_ttm_backend
 *
 * Used to free dma mappings previously mapped by vmw_ttm_map_for_dma.
 */
static void vmw_ttm_unmap_from_dma(struct vmw_ttm_tt *vmw_tt)
{
	struct device *dev = vmw_tt->dev_priv->dev->dev;

	dma_unmap_sg(dev, vmw_tt->sgt.sgl, vmw_tt->sgt.nents,
		DMA_BIDIRECTIONAL);
	vmw_tt->sgt.nents = vmw_tt->sgt.orig_nents;
}

/**
 * vmw_ttm_map_for_dma - map TTM pages to get device addresses
 *
 * @vmw_tt: Pointer to a struct vmw_ttm_backend
 *
 * This function is used to get device addresses from the kernel DMA layer.
 * However, it's violating the DMA API in that when this operation has been
 * performed, it's illegal for the CPU to write to the pages without first
 * unmapping the DMA mappings, or calling dma_sync_sg_for_cpu(). It is
 * therefore only legal to call this function if we know that the function
 * dma_sync_sg_for_cpu() is a NOP, and dma_sync_sg_for_device() is at most
 * a CPU write buffer flush.
 */
static int vmw_ttm_map_for_dma(struct vmw_ttm_tt *vmw_tt)
{
	struct device *dev = vmw_tt->dev_priv->dev->dev;
	int ret;

	ret = dma_map_sg(dev, vmw_tt->sgt.sgl, vmw_tt->sgt.orig_nents,
			 DMA_BIDIRECTIONAL);
	if (unlikely(ret == 0))
		return -ENOMEM;

	vmw_tt->sgt.nents = ret;

	return 0;
}

/**
 * vmw_ttm_map_dma - Make sure TTM pages are visible to the device
 *
 * @vmw_tt: Pointer to a struct vmw_ttm_tt
 *
 * Select the correct function for and make sure the TTM pages are
 * visible to the device. Allocate storage for the device mappings.
 * If a mapping has already been performed, indicated by the storage
 * pointer being non NULL, the function returns success.
 */
static int vmw_ttm_map_dma(struct vmw_ttm_tt *vmw_tt)
{
	struct vmw_private *dev_priv = vmw_tt->dev_priv;
	struct ttm_mem_global *glob = vmw_mem_glob(dev_priv);
	struct vmw_sg_table *vsgt = &vmw_tt->vsgt;
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false
	};
	struct vmw_piter iter;
	dma_addr_t old;
	int ret = 0;
	static size_t sgl_size;
	static size_t sgt_size;

	if (vmw_tt->mapped)
		return 0;

	vsgt->mode = dev_priv->map_mode;
	vsgt->pages = vmw_tt->dma_ttm.ttm.pages;
	vsgt->num_pages = vmw_tt->dma_ttm.ttm.num_pages;
	vsgt->addrs = vmw_tt->dma_ttm.dma_address;
	vsgt->sgt = &vmw_tt->sgt;

	switch (dev_priv->map_mode) {
	case vmw_dma_map_bind:
	case vmw_dma_map_populate:
		if (unlikely(!sgl_size)) {
			sgl_size = ttm_round_pot(sizeof(struct scatterlist));
			sgt_size = ttm_round_pot(sizeof(struct sg_table));
		}
		vmw_tt->sg_alloc_size = sgt_size + sgl_size * vsgt->num_pages;
		ret = ttm_mem_global_alloc(glob, vmw_tt->sg_alloc_size, &ctx);
		if (unlikely(ret != 0))
			return ret;

		ret = sg_alloc_table_from_pages(&vmw_tt->sgt, vsgt->pages,
						vsgt->num_pages, 0,
						(unsigned long)
						vsgt->num_pages << PAGE_SHIFT,
						GFP_KERNEL);
		if (unlikely(ret != 0))
			goto out_sg_alloc_fail;

		if (vsgt->num_pages > vmw_tt->sgt.nents) {
			uint64_t over_alloc =
				sgl_size * (vsgt->num_pages -
					    vmw_tt->sgt.nents);

			ttm_mem_global_free(glob, over_alloc);
			vmw_tt->sg_alloc_size -= over_alloc;
		}

		ret = vmw_ttm_map_for_dma(vmw_tt);
		if (unlikely(ret != 0))
			goto out_map_fail;

		break;
	default:
		break;
	}

	old = ~((dma_addr_t) 0);
	vmw_tt->vsgt.num_regions = 0;
	for (vmw_piter_start(&iter, vsgt, 0); vmw_piter_next(&iter);) {
		dma_addr_t cur = vmw_piter_dma_addr(&iter);

		if (cur != old + PAGE_SIZE)
			vmw_tt->vsgt.num_regions++;
		old = cur;
	}

	vmw_tt->mapped = true;
	return 0;

out_map_fail:
	sg_free_table(vmw_tt->vsgt.sgt);
	vmw_tt->vsgt.sgt = NULL;
out_sg_alloc_fail:
	ttm_mem_global_free(glob, vmw_tt->sg_alloc_size);
	return ret;
}

/**
 * vmw_ttm_unmap_dma - Tear down any TTM page device mappings
 *
 * @vmw_tt: Pointer to a struct vmw_ttm_tt
 *
 * Tear down any previously set up device DMA mappings and free
 * any storage space allocated for them. If there are no mappings set up,
 * this function is a NOP.
 */
static void vmw_ttm_unmap_dma(struct vmw_ttm_tt *vmw_tt)
{
	struct vmw_private *dev_priv = vmw_tt->dev_priv;

	if (!vmw_tt->vsgt.sgt)
		return;

	switch (dev_priv->map_mode) {
	case vmw_dma_map_bind:
	case vmw_dma_map_populate:
		vmw_ttm_unmap_from_dma(vmw_tt);
		sg_free_table(vmw_tt->vsgt.sgt);
		vmw_tt->vsgt.sgt = NULL;
		ttm_mem_global_free(vmw_mem_glob(dev_priv),
				    vmw_tt->sg_alloc_size);
		break;
	default:
		break;
	}
	vmw_tt->mapped = false;
}


/**
 * vmw_bo_map_dma - Make sure buffer object pages are visible to the device
 *
 * @bo: Pointer to a struct ttm_buffer_object
 *
 * Wrapper around vmw_ttm_map_dma, that takes a TTM buffer object pointer
 * instead of a pointer to a struct vmw_ttm_backend as argument.
 * Note that the buffer object must be either pinned or reserved before
 * calling this function.
 */
int vmw_bo_map_dma(struct ttm_buffer_object *bo)
{
	struct vmw_ttm_tt *vmw_tt =
		container_of(bo->ttm, struct vmw_ttm_tt, dma_ttm.ttm);

	return vmw_ttm_map_dma(vmw_tt);
}


/**
 * vmw_bo_unmap_dma - Make sure buffer object pages are visible to the device
 *
 * @bo: Pointer to a struct ttm_buffer_object
 *
 * Wrapper around vmw_ttm_unmap_dma, that takes a TTM buffer object pointer
 * instead of a pointer to a struct vmw_ttm_backend as argument.
 */
void vmw_bo_unmap_dma(struct ttm_buffer_object *bo)
{
	struct vmw_ttm_tt *vmw_tt =
		container_of(bo->ttm, struct vmw_ttm_tt, dma_ttm.ttm);

	vmw_ttm_unmap_dma(vmw_tt);
}


/**
 * vmw_bo_sg_table - Return a struct vmw_sg_table object for a
 * TTM buffer object
 *
 * @bo: Pointer to a struct ttm_buffer_object
 *
 * Returns a pointer to a struct vmw_sg_table object. The object should
 * not be freed after use.
 * Note that for the device addresses to be valid, the buffer object must
 * either be reserved or pinned.
 */
const struct vmw_sg_table *vmw_bo_sg_table(struct ttm_buffer_object *bo)
{
	struct vmw_ttm_tt *vmw_tt =
		container_of(bo->ttm, struct vmw_ttm_tt, dma_ttm.ttm);

	return &vmw_tt->vsgt;
}


static int vmw_ttm_bind(struct ttm_tt *ttm, struct ttm_mem_reg *bo_mem)
{
	struct vmw_ttm_tt *vmw_be =
		container_of(ttm, struct vmw_ttm_tt, dma_ttm.ttm);
	int ret;

	ret = vmw_ttm_map_dma(vmw_be);
	if (unlikely(ret != 0))
		return ret;

	vmw_be->gmr_id = bo_mem->start;
	vmw_be->mem_type = bo_mem->mem_type;

	switch (bo_mem->mem_type) {
	case VMW_PL_GMR:
		return vmw_gmr_bind(vmw_be->dev_priv, &vmw_be->vsgt,
				    ttm->num_pages, vmw_be->gmr_id);
	case VMW_PL_MOB:
		if (unlikely(vmw_be->mob == NULL)) {
			vmw_be->mob =
				vmw_mob_create(ttm->num_pages);
			if (unlikely(vmw_be->mob == NULL))
				return -ENOMEM;
		}

		return vmw_mob_bind(vmw_be->dev_priv, vmw_be->mob,
				    &vmw_be->vsgt, ttm->num_pages,
				    vmw_be->gmr_id);
	default:
		BUG();
	}
	return 0;
}

static int vmw_ttm_unbind(struct ttm_tt *ttm)
{
	struct vmw_ttm_tt *vmw_be =
		container_of(ttm, struct vmw_ttm_tt, dma_ttm.ttm);

	switch (vmw_be->mem_type) {
	case VMW_PL_GMR:
		vmw_gmr_unbind(vmw_be->dev_priv, vmw_be->gmr_id);
		break;
	case VMW_PL_MOB:
		vmw_mob_unbind(vmw_be->dev_priv, vmw_be->mob);
		break;
	default:
		BUG();
	}

	if (vmw_be->dev_priv->map_mode == vmw_dma_map_bind)
		vmw_ttm_unmap_dma(vmw_be);

	return 0;
}


static void vmw_ttm_destroy(struct ttm_tt *ttm)
{
	struct vmw_ttm_tt *vmw_be =
		container_of(ttm, struct vmw_ttm_tt, dma_ttm.ttm);

	vmw_ttm_unmap_dma(vmw_be);
	if (vmw_be->dev_priv->map_mode == vmw_dma_alloc_coherent)
		ttm_dma_tt_fini(&vmw_be->dma_ttm);
	else
		ttm_tt_fini(ttm);

	if (vmw_be->mob)
		vmw_mob_destroy(vmw_be->mob);

	kfree(vmw_be);
}


static int vmw_ttm_populate(struct ttm_tt *ttm, struct ttm_operation_ctx *ctx)
{
	struct vmw_ttm_tt *vmw_tt =
		container_of(ttm, struct vmw_ttm_tt, dma_ttm.ttm);
	struct vmw_private *dev_priv = vmw_tt->dev_priv;
	struct ttm_mem_global *glob = vmw_mem_glob(dev_priv);
	int ret;

	if (ttm->state != tt_unpopulated)
		return 0;

	if (dev_priv->map_mode == vmw_dma_alloc_coherent) {
		size_t size =
			ttm_round_pot(ttm->num_pages * sizeof(dma_addr_t));
		ret = ttm_mem_global_alloc(glob, size, ctx);
		if (unlikely(ret != 0))
			return ret;

		ret = ttm_dma_populate(&vmw_tt->dma_ttm, dev_priv->dev->dev,
					ctx);
		if (unlikely(ret != 0))
			ttm_mem_global_free(glob, size);
	} else
		ret = ttm_pool_populate(ttm, ctx);

	return ret;
}

static void vmw_ttm_unpopulate(struct ttm_tt *ttm)
{
	struct vmw_ttm_tt *vmw_tt = container_of(ttm, struct vmw_ttm_tt,
						 dma_ttm.ttm);
	struct vmw_private *dev_priv = vmw_tt->dev_priv;
	struct ttm_mem_global *glob = vmw_mem_glob(dev_priv);


	if (vmw_tt->mob) {
		vmw_mob_destroy(vmw_tt->mob);
		vmw_tt->mob = NULL;
	}

	vmw_ttm_unmap_dma(vmw_tt);
	if (dev_priv->map_mode == vmw_dma_alloc_coherent) {
		size_t size =
			ttm_round_pot(ttm->num_pages * sizeof(dma_addr_t));

		ttm_dma_unpopulate(&vmw_tt->dma_ttm, dev_priv->dev->dev);
		ttm_mem_global_free(glob, size);
	} else
		ttm_pool_unpopulate(ttm);
}

static struct ttm_backend_func vmw_ttm_func = {
	.bind = vmw_ttm_bind,
	.unbind = vmw_ttm_unbind,
	.destroy = vmw_ttm_destroy,
};

static struct ttm_tt *vmw_ttm_tt_create(struct ttm_buffer_object *bo,
					uint32_t page_flags)
{
	struct vmw_ttm_tt *vmw_be;
	int ret;

	vmw_be = kzalloc(sizeof(*vmw_be), GFP_KERNEL);
	if (!vmw_be)
		return NULL;

	vmw_be->dma_ttm.ttm.func = &vmw_ttm_func;
	vmw_be->dev_priv = container_of(bo->bdev, struct vmw_private, bdev);
	vmw_be->mob = NULL;

	if (vmw_be->dev_priv->map_mode == vmw_dma_alloc_coherent)
		ret = ttm_dma_tt_init(&vmw_be->dma_ttm, bo, page_flags);
	else
		ret = ttm_tt_init(&vmw_be->dma_ttm.ttm, bo, page_flags);
	if (unlikely(ret != 0))
		goto out_no_init;

	return &vmw_be->dma_ttm.ttm;
out_no_init:
	kfree(vmw_be);
	return NULL;
}

static int vmw_invalidate_caches(struct ttm_bo_device *bdev, uint32_t flags)
{
	return 0;
}

static int vmw_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
		      struct ttm_mem_type_manager *man)
{
	switch (type) {
	case TTM_PL_SYSTEM:
		/* System memory */

		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_CACHED;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case TTM_PL_VRAM:
		/* "On-card" video ram */
		man->func = &ttm_bo_manager_func;
		man->gpu_offset = 0;
		man->flags = TTM_MEMTYPE_FLAG_FIXED | TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_CACHED;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case VMW_PL_GMR:
	case VMW_PL_MOB:
		/*
		 * "Guest Memory Regions" is an aperture like feature with
		 *  one slot per bo. There is an upper limit of the number of
		 *  slots as well as the bo size.
		 */
		man->func = &vmw_gmrid_manager_func;
		man->gpu_offset = 0;
		man->flags = TTM_MEMTYPE_FLAG_CMA | TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_CACHED;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	default:
		DRM_ERROR("Unsupported memory type %u\n", (unsigned)type);
		return -EINVAL;
	}
	return 0;
}

static void vmw_evict_flags(struct ttm_buffer_object *bo,
		     struct ttm_placement *placement)
{
	*placement = vmw_sys_placement;
}

static int vmw_verify_access(struct ttm_buffer_object *bo, struct file *filp)
{
	struct ttm_object_file *tfile =
		vmw_fpriv((struct drm_file *)filp->private_data)->tfile;

	return vmw_user_bo_verify_access(bo, tfile);
}

static int vmw_ttm_io_mem_reserve(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
	struct vmw_private *dev_priv = container_of(bdev, struct vmw_private, bdev);

	mem->bus.addr = NULL;
	mem->bus.is_iomem = false;
	mem->bus.offset = 0;
	mem->bus.size = mem->num_pages << PAGE_SHIFT;
	mem->bus.base = 0;
	if (!(man->flags & TTM_MEMTYPE_FLAG_MAPPABLE))
		return -EINVAL;
	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
	case VMW_PL_GMR:
	case VMW_PL_MOB:
		return 0;
	case TTM_PL_VRAM:
		mem->bus.offset = mem->start << PAGE_SHIFT;
		mem->bus.base = dev_priv->vram_start;
		mem->bus.is_iomem = true;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void vmw_ttm_io_mem_free(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
}

static int vmw_ttm_fault_reserve_notify(struct ttm_buffer_object *bo)
{
	return 0;
}

/**
 * vmw_move_notify - TTM move_notify_callback
 *
 * @bo: The TTM buffer object about to move.
 * @mem: The struct ttm_mem_reg indicating to what memory
 *       region the move is taking place.
 *
 * Calls move_notify for all subsystems needing it.
 * (currently only resources).
 */
static void vmw_move_notify(struct ttm_buffer_object *bo,
			    bool evict,
			    struct ttm_mem_reg *mem)
{
	vmw_bo_move_notify(bo, mem);
	vmw_query_move_notify(bo, mem);
}


/**
 * vmw_swap_notify - TTM move_notify_callback
 *
 * @bo: The TTM buffer object about to be swapped out.
 */
static void vmw_swap_notify(struct ttm_buffer_object *bo)
{
	vmw_bo_swap_notify(bo);
	(void) ttm_bo_wait(bo, false, false);
}


struct ttm_bo_driver vmw_bo_driver = {
	.ttm_tt_create = &vmw_ttm_tt_create,
	.ttm_tt_populate = &vmw_ttm_populate,
	.ttm_tt_unpopulate = &vmw_ttm_unpopulate,
	.invalidate_caches = vmw_invalidate_caches,
	.init_mem_type = vmw_init_mem_type,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = vmw_evict_flags,
	.move = NULL,
	.verify_access = vmw_verify_access,
	.move_notify = vmw_move_notify,
	.swap_notify = vmw_swap_notify,
	.fault_reserve_notify = &vmw_ttm_fault_reserve_notify,
	.io_mem_reserve = &vmw_ttm_io_mem_reserve,
	.io_mem_free = &vmw_ttm_io_mem_free,
};

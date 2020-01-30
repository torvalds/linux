// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2018-2019 Qiang Yu <yuq825@gmail.com> */

#include <drm/drm_prime.h>
#include <linux/pagemap.h>
#include <linux/dma-mapping.h>

#include "lima_object.h"

void lima_bo_destroy(struct lima_bo *bo)
{
	if (bo->sgt) {
		kfree(bo->pages);
		drm_prime_gem_destroy(&bo->gem, bo->sgt);
	} else {
		if (bo->pages_dma_addr) {
			int i, npages = bo->gem.size >> PAGE_SHIFT;

			for (i = 0; i < npages; i++) {
				if (bo->pages_dma_addr[i])
					dma_unmap_page(bo->gem.dev->dev,
						       bo->pages_dma_addr[i],
						       PAGE_SIZE, DMA_BIDIRECTIONAL);
			}
		}

		if (bo->pages)
			drm_gem_put_pages(&bo->gem, bo->pages, true, true);
	}

	kfree(bo->pages_dma_addr);
	drm_gem_object_release(&bo->gem);
	kfree(bo);
}

static struct lima_bo *lima_bo_create_struct(struct lima_device *dev, u32 size, u32 flags)
{
	struct lima_bo *bo;
	int err;

	size = PAGE_ALIGN(size);

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	mutex_init(&bo->lock);
	INIT_LIST_HEAD(&bo->va);

	err = drm_gem_object_init(dev->ddev, &bo->gem, size);
	if (err) {
		kfree(bo);
		return ERR_PTR(err);
	}

	return bo;
}

struct lima_bo *lima_bo_create(struct lima_device *dev, u32 size,
			       u32 flags, struct sg_table *sgt)
{
	int i, err;
	size_t npages;
	struct lima_bo *bo, *ret;

	bo = lima_bo_create_struct(dev, size, flags);
	if (IS_ERR(bo))
		return bo;

	npages = bo->gem.size >> PAGE_SHIFT;

	bo->pages_dma_addr = kcalloc(npages, sizeof(dma_addr_t), GFP_KERNEL);
	if (!bo->pages_dma_addr) {
		ret = ERR_PTR(-ENOMEM);
		goto err_out;
	}

	if (sgt) {
		bo->sgt = sgt;

		bo->pages = kcalloc(npages, sizeof(*bo->pages), GFP_KERNEL);
		if (!bo->pages) {
			ret = ERR_PTR(-ENOMEM);
			goto err_out;
		}

		err = drm_prime_sg_to_page_addr_arrays(
			sgt, bo->pages, bo->pages_dma_addr, npages);
		if (err) {
			ret = ERR_PTR(err);
			goto err_out;
		}
	} else {
		mapping_set_gfp_mask(bo->gem.filp->f_mapping, GFP_DMA32);
		bo->pages = drm_gem_get_pages(&bo->gem);
		if (IS_ERR(bo->pages)) {
			ret = ERR_CAST(bo->pages);
			bo->pages = NULL;
			goto err_out;
		}

		for (i = 0; i < npages; i++) {
			dma_addr_t addr = dma_map_page(dev->dev, bo->pages[i], 0,
						       PAGE_SIZE, DMA_BIDIRECTIONAL);
			if (dma_mapping_error(dev->dev, addr)) {
				ret = ERR_PTR(-EFAULT);
				goto err_out;
			}
			bo->pages_dma_addr[i] = addr;
		}

	}

	return bo;

err_out:
	lima_bo_destroy(bo);
	return ret;
}

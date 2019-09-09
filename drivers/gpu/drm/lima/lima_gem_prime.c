// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2018-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/dma-buf.h>
#include <drm/drm_prime.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>

#include "lima_device.h"
#include "lima_object.h"
#include "lima_gem.h"
#include "lima_gem_prime.h"

struct drm_gem_object *lima_gem_prime_import_sg_table(
	struct drm_device *dev, struct dma_buf_attachment *attach,
	struct sg_table *sgt)
{
	struct lima_device *ldev = to_lima_dev(dev);
	struct lima_bo *bo;

	bo = lima_bo_create(ldev, attach->dmabuf->size, 0, sgt,
			    attach->dmabuf->resv);
	if (IS_ERR(bo))
		return ERR_CAST(bo);

	return &bo->gem;
}

struct sg_table *lima_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct lima_bo *bo = to_lima_bo(obj);
	int npages = obj->size >> PAGE_SHIFT;

	return drm_prime_pages_to_sg(bo->pages, npages);
}

int lima_gem_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	if (ret)
		return ret;

	lima_set_vma_flags(vma);
	return 0;
}

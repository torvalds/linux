/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/dma-buf.h>
#include "etnaviv_drv.h"
#include "etnaviv_gem.h"


struct sg_table *etnaviv_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct etnaviv_gem_object *etnaviv_obj = to_etnaviv_bo(obj);
	int npages = obj->size >> PAGE_SHIFT;

	if (WARN_ON(!etnaviv_obj->pages))  /* should have already pinned! */
		return NULL;

	return drm_prime_pages_to_sg(etnaviv_obj->pages, npages);
}

void *etnaviv_gem_prime_vmap(struct drm_gem_object *obj)
{
	return etnaviv_gem_vmap(obj);
}

void etnaviv_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	/* TODO msm_gem_vunmap() */
}

int etnaviv_gem_prime_mmap(struct drm_gem_object *obj,
			   struct vm_area_struct *vma)
{
	struct etnaviv_gem_object *etnaviv_obj = to_etnaviv_bo(obj);
	int ret;

	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	if (ret < 0)
		return ret;

	return etnaviv_obj->ops->mmap(etnaviv_obj, vma);
}

int etnaviv_gem_prime_pin(struct drm_gem_object *obj)
{
	if (!obj->import_attach) {
		struct etnaviv_gem_object *etnaviv_obj = to_etnaviv_bo(obj);

		mutex_lock(&etnaviv_obj->lock);
		etnaviv_gem_get_pages(etnaviv_obj);
		mutex_unlock(&etnaviv_obj->lock);
	}
	return 0;
}

void etnaviv_gem_prime_unpin(struct drm_gem_object *obj)
{
	if (!obj->import_attach) {
		struct etnaviv_gem_object *etnaviv_obj = to_etnaviv_bo(obj);

		mutex_lock(&etnaviv_obj->lock);
		etnaviv_gem_put_pages(to_etnaviv_bo(obj));
		mutex_unlock(&etnaviv_obj->lock);
	}
}

static void etnaviv_gem_prime_release(struct etnaviv_gem_object *etnaviv_obj)
{
	if (etnaviv_obj->vaddr)
		dma_buf_vunmap(etnaviv_obj->base.import_attach->dmabuf,
			       etnaviv_obj->vaddr);

	/* Don't drop the pages for imported dmabuf, as they are not
	 * ours, just free the array we allocated:
	 */
	if (etnaviv_obj->pages)
		drm_free_large(etnaviv_obj->pages);

	drm_prime_gem_destroy(&etnaviv_obj->base, etnaviv_obj->sgt);
}

static void *etnaviv_gem_prime_vmap_impl(struct etnaviv_gem_object *etnaviv_obj)
{
	lockdep_assert_held(&etnaviv_obj->lock);

	return dma_buf_vmap(etnaviv_obj->base.import_attach->dmabuf);
}

static int etnaviv_gem_prime_mmap_obj(struct etnaviv_gem_object *etnaviv_obj,
		struct vm_area_struct *vma)
{
	return dma_buf_mmap(etnaviv_obj->base.dma_buf, vma, 0);
}

static const struct etnaviv_gem_ops etnaviv_gem_prime_ops = {
	/* .get_pages should never be called */
	.release = etnaviv_gem_prime_release,
	.vmap = etnaviv_gem_prime_vmap_impl,
	.mmap = etnaviv_gem_prime_mmap_obj,
};

struct drm_gem_object *etnaviv_gem_prime_import_sg_table(struct drm_device *dev,
	struct dma_buf_attachment *attach, struct sg_table *sgt)
{
	struct etnaviv_gem_object *etnaviv_obj;
	size_t size = PAGE_ALIGN(attach->dmabuf->size);
	int ret, npages;

	ret = etnaviv_gem_new_private(dev, size, ETNA_BO_WC,
				      attach->dmabuf->resv,
				      &etnaviv_gem_prime_ops, &etnaviv_obj);
	if (ret < 0)
		return ERR_PTR(ret);

	npages = size / PAGE_SIZE;

	etnaviv_obj->sgt = sgt;
	etnaviv_obj->pages = drm_malloc_ab(npages, sizeof(struct page *));
	if (!etnaviv_obj->pages) {
		ret = -ENOMEM;
		goto fail;
	}

	ret = drm_prime_sg_to_page_addr_arrays(sgt, etnaviv_obj->pages,
					       NULL, npages);
	if (ret)
		goto fail;

	ret = etnaviv_gem_obj_add(dev, &etnaviv_obj->base);
	if (ret)
		goto fail;

	return &etnaviv_obj->base;

fail:
	drm_gem_object_unreference_unlocked(&etnaviv_obj->base);

	return ERR_PTR(ret);
}

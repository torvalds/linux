// SPDX-License-Identifier: GPL-2.0 OR MIT

/*
 *  Xen para-virtual DRM device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#include <linux/dma-buf.h>
#include <linux/scatterlist.h>
#include <linux/shmem_fs.h>

#include <drm/drm_fb_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_prime.h>
#include <drm/drm_probe_helper.h>

#include <xen/balloon.h>
#include <xen/xen.h>

#include "xen_drm_front.h"
#include "xen_drm_front_gem.h"

struct xen_gem_object {
	struct drm_gem_object base;

	size_t num_pages;
	struct page **pages;

	/* set for buffers allocated by the backend */
	bool be_alloc;

	/* this is for imported PRIME buffer */
	struct sg_table *sgt_imported;
};

static inline struct xen_gem_object *
to_xen_gem_obj(struct drm_gem_object *gem_obj)
{
	return container_of(gem_obj, struct xen_gem_object, base);
}

static int gem_alloc_pages_array(struct xen_gem_object *xen_obj,
				 size_t buf_size)
{
	xen_obj->num_pages = DIV_ROUND_UP(buf_size, PAGE_SIZE);
	xen_obj->pages = kvmalloc_array(xen_obj->num_pages,
					sizeof(struct page *), GFP_KERNEL);
	return !xen_obj->pages ? -ENOMEM : 0;
}

static void gem_free_pages_array(struct xen_gem_object *xen_obj)
{
	kvfree(xen_obj->pages);
	xen_obj->pages = NULL;
}

static const struct vm_operations_struct xen_drm_drv_vm_ops = {
	.open           = drm_gem_vm_open,
	.close          = drm_gem_vm_close,
};

static const struct drm_gem_object_funcs xen_drm_front_gem_object_funcs = {
	.free = xen_drm_front_gem_object_free,
	.get_sg_table = xen_drm_front_gem_get_sg_table,
	.vmap = xen_drm_front_gem_prime_vmap,
	.vunmap = xen_drm_front_gem_prime_vunmap,
	.vm_ops = &xen_drm_drv_vm_ops,
};

static struct xen_gem_object *gem_create_obj(struct drm_device *dev,
					     size_t size)
{
	struct xen_gem_object *xen_obj;
	int ret;

	xen_obj = kzalloc(sizeof(*xen_obj), GFP_KERNEL);
	if (!xen_obj)
		return ERR_PTR(-ENOMEM);

	xen_obj->base.funcs = &xen_drm_front_gem_object_funcs;

	ret = drm_gem_object_init(dev, &xen_obj->base, size);
	if (ret < 0) {
		kfree(xen_obj);
		return ERR_PTR(ret);
	}

	return xen_obj;
}

static struct xen_gem_object *gem_create(struct drm_device *dev, size_t size)
{
	struct xen_drm_front_drm_info *drm_info = dev->dev_private;
	struct xen_gem_object *xen_obj;
	int ret;

	size = round_up(size, PAGE_SIZE);
	xen_obj = gem_create_obj(dev, size);
	if (IS_ERR(xen_obj))
		return xen_obj;

	if (drm_info->front_info->cfg.be_alloc) {
		/*
		 * backend will allocate space for this buffer, so
		 * only allocate array of pointers to pages
		 */
		ret = gem_alloc_pages_array(xen_obj, size);
		if (ret < 0)
			goto fail;

		/*
		 * allocate ballooned pages which will be used to map
		 * grant references provided by the backend
		 */
		ret = xen_alloc_unpopulated_pages(xen_obj->num_pages,
					          xen_obj->pages);
		if (ret < 0) {
			DRM_ERROR("Cannot allocate %zu ballooned pages: %d\n",
				  xen_obj->num_pages, ret);
			gem_free_pages_array(xen_obj);
			goto fail;
		}

		xen_obj->be_alloc = true;
		return xen_obj;
	}
	/*
	 * need to allocate backing pages now, so we can share those
	 * with the backend
	 */
	xen_obj->num_pages = DIV_ROUND_UP(size, PAGE_SIZE);
	xen_obj->pages = drm_gem_get_pages(&xen_obj->base);
	if (IS_ERR(xen_obj->pages)) {
		ret = PTR_ERR(xen_obj->pages);
		xen_obj->pages = NULL;
		goto fail;
	}

	return xen_obj;

fail:
	DRM_ERROR("Failed to allocate buffer with size %zu\n", size);
	return ERR_PTR(ret);
}

struct drm_gem_object *xen_drm_front_gem_create(struct drm_device *dev,
						size_t size)
{
	struct xen_gem_object *xen_obj;

	xen_obj = gem_create(dev, size);
	if (IS_ERR(xen_obj))
		return ERR_CAST(xen_obj);

	return &xen_obj->base;
}

void xen_drm_front_gem_free_object_unlocked(struct drm_gem_object *gem_obj)
{
	struct xen_gem_object *xen_obj = to_xen_gem_obj(gem_obj);

	if (xen_obj->base.import_attach) {
		drm_prime_gem_destroy(&xen_obj->base, xen_obj->sgt_imported);
		gem_free_pages_array(xen_obj);
	} else {
		if (xen_obj->pages) {
			if (xen_obj->be_alloc) {
				xen_free_unpopulated_pages(xen_obj->num_pages,
							   xen_obj->pages);
				gem_free_pages_array(xen_obj);
			} else {
				drm_gem_put_pages(&xen_obj->base,
						  xen_obj->pages, true, false);
			}
		}
	}
	drm_gem_object_release(gem_obj);
	kfree(xen_obj);
}

struct page **xen_drm_front_gem_get_pages(struct drm_gem_object *gem_obj)
{
	struct xen_gem_object *xen_obj = to_xen_gem_obj(gem_obj);

	return xen_obj->pages;
}

struct sg_table *xen_drm_front_gem_get_sg_table(struct drm_gem_object *gem_obj)
{
	struct xen_gem_object *xen_obj = to_xen_gem_obj(gem_obj);

	if (!xen_obj->pages)
		return ERR_PTR(-ENOMEM);

	return drm_prime_pages_to_sg(gem_obj->dev,
				     xen_obj->pages, xen_obj->num_pages);
}

struct drm_gem_object *
xen_drm_front_gem_import_sg_table(struct drm_device *dev,
				  struct dma_buf_attachment *attach,
				  struct sg_table *sgt)
{
	struct xen_drm_front_drm_info *drm_info = dev->dev_private;
	struct xen_gem_object *xen_obj;
	size_t size;
	int ret;

	size = attach->dmabuf->size;
	xen_obj = gem_create_obj(dev, size);
	if (IS_ERR(xen_obj))
		return ERR_CAST(xen_obj);

	ret = gem_alloc_pages_array(xen_obj, size);
	if (ret < 0)
		return ERR_PTR(ret);

	xen_obj->sgt_imported = sgt;

	ret = drm_prime_sg_to_page_addr_arrays(sgt, xen_obj->pages,
					       NULL, xen_obj->num_pages);
	if (ret < 0)
		return ERR_PTR(ret);

	ret = xen_drm_front_dbuf_create(drm_info->front_info,
					xen_drm_front_dbuf_to_cookie(&xen_obj->base),
					0, 0, 0, size, sgt->sgl->offset,
					xen_obj->pages);
	if (ret < 0)
		return ERR_PTR(ret);

	DRM_DEBUG("Imported buffer of size %zu with nents %u\n",
		  size, sgt->nents);

	return &xen_obj->base;
}

static int gem_mmap_obj(struct xen_gem_object *xen_obj,
			struct vm_area_struct *vma)
{
	int ret;

	/*
	 * clear the VM_PFNMAP flag that was set by drm_gem_mmap(), and set the
	 * vm_pgoff (used as a fake buffer offset by DRM) to 0 as we want to map
	 * the whole buffer.
	 */
	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;
	vma->vm_pgoff = 0;
	/*
	 * According to Xen on ARM ABI (xen/include/public/arch-arm.h):
	 * all memory which is shared with other entities in the system
	 * (including the hypervisor and other guests) must reside in memory
	 * which is mapped as Normal Inner Write-Back Outer Write-Back
	 * Inner-Shareable.
	 */
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);

	/*
	 * vm_operations_struct.fault handler will be called if CPU access
	 * to VM is here. For GPUs this isn't the case, because CPU
	 * doesn't touch the memory. Insert pages now, so both CPU and GPU are
	 * happy.
	 * FIXME: as we insert all the pages now then no .fault handler must
	 * be called, so don't provide one
	 */
	ret = vm_map_pages(vma, xen_obj->pages, xen_obj->num_pages);
	if (ret < 0)
		DRM_ERROR("Failed to map pages into vma: %d\n", ret);

	return ret;
}

int xen_drm_front_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct xen_gem_object *xen_obj;
	struct drm_gem_object *gem_obj;
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret < 0)
		return ret;

	gem_obj = vma->vm_private_data;
	xen_obj = to_xen_gem_obj(gem_obj);
	return gem_mmap_obj(xen_obj, vma);
}

void *xen_drm_front_gem_prime_vmap(struct drm_gem_object *gem_obj)
{
	struct xen_gem_object *xen_obj = to_xen_gem_obj(gem_obj);

	if (!xen_obj->pages)
		return NULL;

	/* Please see comment in gem_mmap_obj on mapping and attributes. */
	return vmap(xen_obj->pages, xen_obj->num_pages,
		    VM_MAP, PAGE_KERNEL);
}

void xen_drm_front_gem_prime_vunmap(struct drm_gem_object *gem_obj,
				    void *vaddr)
{
	vunmap(vaddr);
}

int xen_drm_front_gem_prime_mmap(struct drm_gem_object *gem_obj,
				 struct vm_area_struct *vma)
{
	struct xen_gem_object *xen_obj;
	int ret;

	ret = drm_gem_mmap_obj(gem_obj, gem_obj->size, vma);
	if (ret < 0)
		return ret;

	xen_obj = to_xen_gem_obj(gem_obj);
	return gem_mmap_obj(xen_obj, vma);
}

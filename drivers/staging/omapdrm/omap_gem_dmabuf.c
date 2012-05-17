/*
 * drivers/staging/omapdrm/omap_gem_dmabuf.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob.clark@linaro.org>
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

#include "omap_drv.h"

#include <linux/dma-buf.h>

static struct sg_table *omap_gem_map_dma_buf(
		struct dma_buf_attachment *attachment,
		enum dma_data_direction dir)
{
	struct drm_gem_object *obj = attachment->dmabuf->priv;
	struct sg_table *sg;
	dma_addr_t paddr;
	int ret;

	sg = kzalloc(sizeof(*sg), GFP_KERNEL);
	if (!sg)
		return ERR_PTR(-ENOMEM);

	/* camera, etc, need physically contiguous.. but we need a
	 * better way to know this..
	 */
	ret = omap_gem_get_paddr(obj, &paddr, true);
	if (ret)
		goto out;

	ret = sg_alloc_table(sg, 1, GFP_KERNEL);
	if (ret)
		goto out;

	sg_init_table(sg->sgl, 1);
	sg_dma_len(sg->sgl) = obj->size;
	sg_set_page(sg->sgl, pfn_to_page(PFN_DOWN(paddr)), obj->size, 0);
	sg_dma_address(sg->sgl) = paddr;

	/* this should be after _get_paddr() to ensure we have pages attached */
	omap_gem_dma_sync(obj, dir);

out:
	if (ret)
		return ERR_PTR(ret);
	return sg;
}

static void omap_gem_unmap_dma_buf(struct dma_buf_attachment *attachment,
		struct sg_table *sg, enum dma_data_direction dir)
{
	struct drm_gem_object *obj = attachment->dmabuf->priv;
	omap_gem_put_paddr(obj);
	sg_free_table(sg);
	kfree(sg);
}

static void omap_gem_dmabuf_release(struct dma_buf *buffer)
{
	struct drm_gem_object *obj = buffer->priv;
	/* release reference that was taken when dmabuf was exported
	 * in omap_gem_prime_set()..
	 */
	drm_gem_object_unreference_unlocked(obj);
}


static int omap_gem_dmabuf_begin_cpu_access(struct dma_buf *buffer,
		size_t start, size_t len, enum dma_data_direction dir)
{
	struct drm_gem_object *obj = buffer->priv;
	struct page **pages;
	if (omap_gem_flags(obj) & OMAP_BO_TILED) {
		/* TODO we would need to pin at least part of the buffer to
		 * get de-tiled view.  For now just reject it.
		 */
		return -ENOMEM;
	}
	/* make sure we have the pages: */
	return omap_gem_get_pages(obj, &pages, true);
}

static void omap_gem_dmabuf_end_cpu_access(struct dma_buf *buffer,
		size_t start, size_t len, enum dma_data_direction dir)
{
	struct drm_gem_object *obj = buffer->priv;
	omap_gem_put_pages(obj);
}


static void *omap_gem_dmabuf_kmap_atomic(struct dma_buf *buffer,
		unsigned long page_num)
{
	struct drm_gem_object *obj = buffer->priv;
	struct page **pages;
	omap_gem_get_pages(obj, &pages, false);
	omap_gem_cpu_sync(obj, page_num);
	return kmap_atomic(pages[page_num]);
}

static void omap_gem_dmabuf_kunmap_atomic(struct dma_buf *buffer,
		unsigned long page_num, void *addr)
{
	kunmap_atomic(addr);
}

static void *omap_gem_dmabuf_kmap(struct dma_buf *buffer,
		unsigned long page_num)
{
	struct drm_gem_object *obj = buffer->priv;
	struct page **pages;
	omap_gem_get_pages(obj, &pages, false);
	omap_gem_cpu_sync(obj, page_num);
	return kmap(pages[page_num]);
}

static void omap_gem_dmabuf_kunmap(struct dma_buf *buffer,
		unsigned long page_num, void *addr)
{
	struct drm_gem_object *obj = buffer->priv;
	struct page **pages;
	omap_gem_get_pages(obj, &pages, false);
	kunmap(pages[page_num]);
}

/*
 * TODO maybe we can split up drm_gem_mmap to avoid duplicating
 * some here.. or at least have a drm_dmabuf_mmap helper.
 */
static int omap_gem_dmabuf_mmap(struct dma_buf *buffer,
		struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = buffer->priv;
	int ret = 0;

	if (WARN_ON(!obj->filp))
		return -EINVAL;

	/* Check for valid size. */
	if (omap_gem_mmap_size(obj) < vma->vm_end - vma->vm_start) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (!obj->dev->driver->gem_vm_ops) {
		ret = -EINVAL;
		goto out_unlock;
	}

	vma->vm_flags |= VM_RESERVED | VM_IO | VM_PFNMAP | VM_DONTEXPAND;
	vma->vm_ops = obj->dev->driver->gem_vm_ops;
	vma->vm_private_data = obj;
	vma->vm_page_prot =  pgprot_writecombine(vm_get_page_prot(vma->vm_flags));

	/* Take a ref for this mapping of the object, so that the fault
	 * handler can dereference the mmap offset's pointer to the object.
	 * This reference is cleaned up by the corresponding vm_close
	 * (which should happen whether the vma was created by this call, or
	 * by a vm_open due to mremap or partial unmap or whatever).
	 */
	vma->vm_ops->open(vma);

out_unlock:

	return omap_gem_mmap_obj(obj, vma);
}

struct dma_buf_ops omap_dmabuf_ops = {
		.map_dma_buf = omap_gem_map_dma_buf,
		.unmap_dma_buf = omap_gem_unmap_dma_buf,
		.release = omap_gem_dmabuf_release,
		.begin_cpu_access = omap_gem_dmabuf_begin_cpu_access,
		.end_cpu_access = omap_gem_dmabuf_end_cpu_access,
		.kmap_atomic = omap_gem_dmabuf_kmap_atomic,
		.kunmap_atomic = omap_gem_dmabuf_kunmap_atomic,
		.kmap = omap_gem_dmabuf_kmap,
		.kunmap = omap_gem_dmabuf_kunmap,
		.mmap = omap_gem_dmabuf_mmap,
};

struct dma_buf * omap_gem_prime_export(struct drm_device *dev,
		struct drm_gem_object *obj, int flags)
{
	return dma_buf_export(obj, &omap_dmabuf_ops, obj->size, 0600);
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 * Copyright (c) 2025 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/dma-buf.h>
#include <linux/vmalloc.h>

#include <drm/drm.h>
#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_prime.h>
#include <drm/drm_print.h>

#include "mtk_drm_drv.h"
#include "mtk_gem.h"

static int mtk_gem_object_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma);

static void mtk_gem_free_object(struct drm_gem_object *obj)
{
	struct drm_gem_dma_object *dma_obj = to_drm_gem_dma_obj(obj);
	struct mtk_drm_private *priv = obj->dev->dev_private;

	if (dma_obj->sgt)
		drm_prime_gem_destroy(obj, dma_obj->sgt);
	else
		dma_free_wc(priv->dma_dev, dma_obj->base.size,
			    dma_obj->vaddr, dma_obj->dma_addr);

	/* release file pointer to gem object. */
	drm_gem_object_release(obj);

	kfree(dma_obj);
}

/*
 * Allocate a sg_table for this GEM object.
 * Note: Both the table's contents, and the sg_table itself must be freed by
 *       the caller.
 * Returns a pointer to the newly allocated sg_table, or an ERR_PTR() error.
 */
static struct sg_table *mtk_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct drm_gem_dma_object *dma_obj = to_drm_gem_dma_obj(obj);
	struct mtk_drm_private *priv = obj->dev->dev_private;
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	ret = dma_get_sgtable(priv->dma_dev, sgt, dma_obj->vaddr,
			      dma_obj->dma_addr, obj->size);
	if (ret) {
		DRM_ERROR("failed to allocate sgt, %d\n", ret);
		kfree(sgt);
		return ERR_PTR(ret);
	}

	return sgt;
}

static const struct drm_gem_object_funcs mtk_gem_object_funcs = {
	.free = mtk_gem_free_object,
	.print_info = drm_gem_dma_object_print_info,
	.get_sg_table = mtk_gem_prime_get_sg_table,
	.vmap = drm_gem_dma_object_vmap,
	.mmap = mtk_gem_object_mmap,
	.vm_ops = &drm_gem_dma_vm_ops,
};

static struct drm_gem_dma_object *mtk_gem_init(struct drm_device *dev,
					unsigned long size, bool private)
{
	struct drm_gem_dma_object *dma_obj;
	int ret;

	size = round_up(size, PAGE_SIZE);

	if (size == 0)
		return ERR_PTR(-EINVAL);

	dma_obj = kzalloc(sizeof(*dma_obj), GFP_KERNEL);
	if (!dma_obj)
		return ERR_PTR(-ENOMEM);

	dma_obj->base.funcs = &mtk_gem_object_funcs;

	if (private) {
		ret = 0;
		drm_gem_private_object_init(dev, &dma_obj->base, size);
	} else {
		ret = drm_gem_object_init(dev, &dma_obj->base, size);
	}
	if (ret) {
		DRM_ERROR("failed to initialize gem object\n");
		kfree(dma_obj);
		return ERR_PTR(ret);
	}

	return dma_obj;
}

static struct drm_gem_dma_object *mtk_gem_create(struct drm_device *dev, size_t size)
{
	struct mtk_drm_private *priv = dev->dev_private;
	struct drm_gem_dma_object *dma_obj;
	struct drm_gem_object *obj;
	int ret;

	dma_obj = mtk_gem_init(dev, size, false);
	if (IS_ERR(dma_obj))
		return ERR_CAST(dma_obj);

	obj = &dma_obj->base;

	dma_obj->vaddr = dma_alloc_wc(priv->dma_dev, obj->size,
				      &dma_obj->dma_addr,
				      GFP_KERNEL | __GFP_NOWARN);
	if (!dma_obj->vaddr) {
		DRM_ERROR("failed to allocate %zx byte dma buffer", obj->size);
		ret = -ENOMEM;
		goto err_gem_free;
	}

	DRM_DEBUG_DRIVER("vaddr = %p dma_addr = %pad size = %zu\n",
			 dma_obj->vaddr, &dma_obj->dma_addr,
			 size);

	return dma_obj;

err_gem_free:
	drm_gem_object_release(obj);
	kfree(dma_obj);
	return ERR_PTR(ret);
}

int mtk_gem_dumb_create(struct drm_file *file_priv, struct drm_device *dev,
			struct drm_mode_create_dumb *args)
{
	struct drm_gem_dma_object *dma_obj;
	int ret;

	args->pitch = DIV_ROUND_UP(args->width * args->bpp, 8);

	/*
	 * Multiply 2 variables of different types,
	 * for example: args->size = args->spacing * args->height;
	 * may cause coverity issue with unintentional overflow.
	 */
	args->size = args->pitch;
	args->size *= args->height;

	dma_obj = mtk_gem_create(dev, args->size);
	if (IS_ERR(dma_obj))
		return PTR_ERR(dma_obj);

	/*
	 * allocate a id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, &dma_obj->base, &args->handle);
	if (ret)
		goto err_handle_create;

	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_put(&dma_obj->base);

	return 0;

err_handle_create:
	mtk_gem_free_object(&dma_obj->base);
	return ret;
}

static int mtk_gem_object_mmap(struct drm_gem_object *obj,
			       struct vm_area_struct *vma)

{
	struct drm_gem_dma_object *dma_obj = to_drm_gem_dma_obj(obj);
	struct mtk_drm_private *priv = obj->dev->dev_private;
	int ret;

	/*
	 * Set vm_pgoff (used as a fake buffer offset by DRM) to 0 and map the
	 * whole buffer from the start.
	 */
	vma->vm_pgoff -= drm_vma_node_start(&obj->vma_node);

	/*
	 * dma_alloc_attrs() allocated a struct page table for mtk_gem, so clear
	 * VM_PFNMAP flag that was set by drm_gem_mmap_obj()/drm_gem_mmap().
	 */
	vm_flags_mod(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP, VM_PFNMAP);

	vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

	ret = dma_mmap_wc(priv->dma_dev, vma, dma_obj->vaddr,
			  dma_obj->dma_addr, obj->size);
	if (ret)
		drm_gem_vm_close(vma);

	return ret;
}

struct drm_gem_object *mtk_gem_prime_import_sg_table(struct drm_device *dev,
			struct dma_buf_attachment *attach, struct sg_table *sgt)
{
	struct drm_gem_dma_object *dma_obj;

	/* check if the entries in the sg_table are contiguous */
	if (drm_prime_get_contiguous_size(sgt) < attach->dmabuf->size) {
		DRM_ERROR("sg_table is not contiguous");
		return ERR_PTR(-EINVAL);
	}

	dma_obj = mtk_gem_init(dev, attach->dmabuf->size, true);
	if (IS_ERR(dma_obj))
		return ERR_CAST(dma_obj);

	dma_obj->dma_addr = sg_dma_address(sgt->sgl);
	dma_obj->sgt = sgt;

	return &dma_obj->base;
}

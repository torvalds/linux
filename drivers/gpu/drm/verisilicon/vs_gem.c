// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/dma-buf.h>
#include <linux/of_reserved_mem.h>
#include <drm/drm_gem_cma_helper.h>

#include "vs_drv.h"
#include "vs_gem.h"

static const struct drm_gem_object_funcs vs_gem_default_funcs;

static void nonseq_free(struct page **pages, unsigned int nr_page)
{
	u32 i;

	if (!pages)
		return;

	for (i = 0; i < nr_page; i++)
		__free_page(pages[i]);

}

#ifdef CONFIG_VERISILICON_MMU
static int get_pages(unsigned int nr_page, struct vs_gem_object *vs_obj)
{
	struct page *pages;
	u32 i, num_page, page_count = 0;
	int order = 0;
	gfp_t gfp = GFP_KERNEL;

	if (!vs_obj->pages)
		return -EINVAL;

	gfp &= ~__GFP_HIGHMEM;
	gfp |= __GFP_DMA32;

	num_page = nr_page;

	do {
		pages = NULL;
		order = get_order(num_page * PAGE_SIZE);
		num_page = 1 << order;

		if ((num_page + page_count > nr_page) || (order >= MAX_ORDER)) {
			num_page = num_page >> 1;
			continue;
		}

		pages = alloc_pages(gfp, order);
		if (!pages) {
			if (num_page == 1) {
				nonseq_free(vs_obj->pages, page_count);
				return -ENOMEM;
			}

			num_page = num_page >> 1;
		} else {
			for (i = 0; i < num_page; i++) {
				vs_obj->pages[page_count + i] = &pages[i];
				SetPageReserved(vs_obj->pages[page_count + i]);
			}

			page_count += num_page;
			num_page = nr_page - page_count;
		}

	} while (page_count < nr_page);

	vs_obj->get_pages = true;

	return 0;
}
#endif

static void put_pages(unsigned int nr_page, struct vs_gem_object *vs_obj)
{
	u32 i;

	for (i = 0; i < nr_page; i++)
		ClearPageReserved(vs_obj->pages[i]);

	nonseq_free(vs_obj->pages, nr_page);

}

static int vs_gem_alloc_buf(struct vs_gem_object *vs_obj)
{
	struct drm_device *dev = vs_obj->base.dev;
	unsigned int nr_pages;
	struct sg_table sgt;
	int ret = -ENOMEM;
#ifdef CONFIG_VERISILICON_MMU
	struct vs_drm_private *priv = dev->dev_private;
#endif

	if (vs_obj->dma_addr) {
		DRM_DEV_DEBUG_KMS(dev->dev, "already allocated.\n");
		return 0;
	}

	vs_obj->dma_attrs = DMA_ATTR_WRITE_COMBINE
			   | DMA_ATTR_NO_KERNEL_MAPPING;

	if (!is_iommu_enabled(dev))
		vs_obj->dma_attrs |= DMA_ATTR_FORCE_CONTIGUOUS;

	nr_pages = vs_obj->size >> PAGE_SHIFT;

	vs_obj->pages = kvmalloc_array(nr_pages, sizeof(struct page *),
					GFP_KERNEL | __GFP_ZERO);
	if (!vs_obj->pages) {
		DRM_DEV_ERROR(dev->dev, "failed to allocate pages.\n");
		return -ENOMEM;
	}

	vs_obj->cookie = dma_alloc_attrs(to_dma_dev(dev), vs_obj->size,
						&vs_obj->dma_addr, GFP_KERNEL,
						vs_obj->dma_attrs);

	DRM_DEV_DEBUG(dev->dev,"Allocated coherent memory, vaddr: 0x%0llX, paddr: 0x%0llX, size: %lu\n",
		(u64)vs_obj->cookie,vs_obj->dma_addr,vs_obj->size);
	if (!vs_obj->cookie) {
#ifdef CONFIG_VERISILICON_MMU
		ret = get_pages(nr_pages, vs_obj);
		if (ret) {
			DRM_DEV_ERROR(dev->dev, "fail to allocate buffer.\n");
			goto err_free;
		}
#else
		DRM_DEV_ERROR(dev->dev, "failed to allocate buffer.\n");
		goto err_free;
#endif
	}

#ifdef CONFIG_VERISILICON_MMU
	/* MMU map*/
	if (!priv->mmu) {
		DRM_DEV_ERROR(dev->dev, "invalid mmu.\n");
		ret = -EINVAL;
		goto err_mem_free;
	}

	/* mmu for ree driver */
	if (!vs_obj->get_pages)
		ret = dc_mmu_map_memory(priv->mmu, (u64)vs_obj->dma_addr,
					nr_pages, &vs_obj->iova, true, false);
	else
		ret = dc_mmu_map_memory(priv->mmu, (u64)vs_obj->pages,
					nr_pages, &vs_obj->iova, false, false);

	if (ret) {
		DRM_DEV_ERROR(dev->dev, "failed to do mmu map.\n");
		goto err_mem_free;
	}
#else
	vs_obj->iova = vs_obj->dma_addr;
#endif

	if (!vs_obj->get_pages) {
		ret = dma_get_sgtable_attrs(to_dma_dev(dev), &sgt,
						vs_obj->cookie, vs_obj->dma_addr,
						vs_obj->size, vs_obj->dma_attrs);
		if (ret < 0) {
			DRM_DEV_ERROR(dev->dev, "failed to get sgtable.\n");
			goto err_mem_free;
		}

		if (drm_prime_sg_to_page_array(&sgt, vs_obj->pages, nr_pages)) {
			DRM_DEV_ERROR(dev->dev, "invalid sgtable.\n");
			ret = -EINVAL;
			goto err_sgt_free;
		}

		sg_free_table(&sgt);
	}

	return 0;

err_sgt_free:
	sg_free_table(&sgt);
err_mem_free:
	if (!vs_obj->get_pages)
		dma_free_attrs(to_dma_dev(dev), vs_obj->size, vs_obj->cookie,
				   vs_obj->dma_addr, vs_obj->dma_attrs);
	else
		put_pages(nr_pages, vs_obj);
err_free:
	kvfree(vs_obj->pages);

	return ret;
}

static void vs_gem_free_buf(struct vs_gem_object *vs_obj)
{
	struct drm_device *dev = vs_obj->base.dev;
#ifdef CONFIG_VERISILICON_MMU
	struct vs_drm_private *priv = dev->dev_private;
	unsigned int nr_pages;
#endif

	if ((!vs_obj->get_pages) && (!vs_obj->dma_addr)) {
		DRM_DEV_DEBUG_KMS(dev->dev, "dma_addr is invalid.\n");
		return;
	}

#ifdef CONFIG_VERISILICON_MMU
	if (!priv->mmu) {
		DRM_DEV_ERROR(dev->dev, "invalid mmu.\n");
		return;
	}

	nr_pages = vs_obj->size >> PAGE_SHIFT;
	dc_mmu_unmap_memory(priv->mmu, vs_obj->iova, nr_pages);
#endif

	if (!vs_obj->get_pages)
		dma_free_attrs(to_dma_dev(dev), vs_obj->size, vs_obj->cookie,
				(dma_addr_t)vs_obj->dma_addr,
				vs_obj->dma_attrs);
	else
		put_pages(vs_obj->size >> PAGE_SHIFT, vs_obj);

	kvfree(vs_obj->pages);
}

static void vs_gem_free_object(struct drm_gem_object *obj)
{
	struct vs_gem_object *vs_obj = to_vs_gem_object(obj);

	if (obj->import_attach)
		drm_prime_gem_destroy(obj, vs_obj->sgt);
	else
		vs_gem_free_buf(vs_obj);

	drm_gem_object_release(obj);

	kfree(vs_obj);
}

static struct vs_gem_object *vs_gem_alloc_object(struct drm_device *dev,
						 size_t size)
{
	struct vs_gem_object *vs_obj;
	struct drm_gem_object *obj;
	int ret;

	vs_obj = kzalloc(sizeof(*vs_obj), GFP_KERNEL);
	if (!vs_obj)
		return ERR_PTR(-ENOMEM);

	vs_obj->size = size;
	obj = &vs_obj->base;

	ret = drm_gem_object_init(dev, obj, size);
	if (ret)
		goto err_free;

	vs_obj->base.funcs = &vs_gem_default_funcs;

	ret = drm_gem_create_mmap_offset(obj);
	if (ret) {
		drm_gem_object_release(obj);
		goto err_free;
	}

	return vs_obj;

err_free:
	kfree(vs_obj);
	return ERR_PTR(ret);
}

struct vs_gem_object *vs_gem_create_object(struct drm_device *dev,
					   size_t size)
{
	struct vs_gem_object *vs_obj;
	int ret;

	size = PAGE_ALIGN(size);

	vs_obj = vs_gem_alloc_object(dev, size);
	if (IS_ERR(vs_obj))
		return vs_obj;

	ret = vs_gem_alloc_buf(vs_obj);
	if (ret) {
		drm_gem_object_release(&vs_obj->base);
		kfree(vs_obj);
		return ERR_PTR(ret);
	}

	return vs_obj;
}

static struct vs_gem_object *vs_gem_create_with_handle(struct drm_device *dev,
							   struct drm_file *file,
							   size_t size,
							   unsigned int *handle)
{
	struct vs_gem_object *vs_obj;
	struct drm_gem_object *obj;
	int ret;

	vs_obj = vs_gem_create_object(dev, size);
	if (IS_ERR(vs_obj))
		return vs_obj;

	obj = &vs_obj->base;

	ret = drm_gem_handle_create(file, obj, handle);

#if KERNEL_VERSION(5, 9, 0) <= LINUX_VERSION_CODE
	drm_gem_object_put(obj);
#else
	drm_gem_object_put_unlocked(obj);
#endif

	if (ret)
		return ERR_PTR(ret);

	return vs_obj;
}

static int vs_gem_mmap_obj(struct drm_gem_object *obj,
			   struct vm_area_struct *vma)
{
	struct vs_gem_object *vs_obj = to_vs_gem_object(obj);
	struct drm_device *drm_dev = vs_obj->base.dev;
	unsigned long vm_size;
	int ret = 0;

	vm_size = vma->vm_end - vma->vm_start;
	if (vm_size > vs_obj->size)
		return -EINVAL;

	vma->vm_pgoff = 0;

	if (!vs_obj->get_pages) {
		vma->vm_flags &= ~VM_PFNMAP;

		ret = dma_mmap_attrs(to_dma_dev(drm_dev), vma, vs_obj->cookie,
					vs_obj->dma_addr, vs_obj->size,
					 vs_obj->dma_attrs);
	} else {
		u32 i, nr_pages, pfn = 0U;
		unsigned long start;

		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
		vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND |
						 VM_DONTDUMP;
		start = vma->vm_start;
		vm_size = PAGE_ALIGN(vm_size);
		nr_pages = vm_size >> PAGE_SHIFT;

		for (i = 0; i < nr_pages; i++) {
			pfn = page_to_pfn(vs_obj->pages[i]);

			ret = remap_pfn_range(vma, start, pfn, PAGE_SIZE,
						vma->vm_page_prot);
			if (ret < 0)
				break;

			start += PAGE_SIZE;
		}
	}

	if (ret)
		drm_gem_vm_close(vma);

	return ret;
}

struct sg_table *vs_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct vs_gem_object *vs_obj = to_vs_gem_object(obj);

#if KERNEL_VERSION(5, 9, 0) <= LINUX_VERSION_CODE
	return drm_prime_pages_to_sg(obj->dev, vs_obj->pages,
					 vs_obj->size >> PAGE_SHIFT);
#else
	return drm_prime_pages_to_sg(vs_obj->pages, vs_obj->size >> PAGE_SHIFT);
#endif
}

int vs_gem_prime_vmap(struct drm_gem_object *obj, struct dma_buf_map *map)
{
	struct vs_gem_object *vs_obj = to_vs_gem_object(obj);

	void * vaddr = vs_obj->dma_attrs & DMA_ATTR_NO_KERNEL_MAPPING ?
		       page_address(vs_obj->cookie) : vs_obj->cookie;

	dma_buf_map_set_vaddr(map, vaddr);

	return 0;
}

void vs_gem_prime_vunmap(struct drm_gem_object *obj, struct dma_buf_map *map)
{
	/* Nothing to do */
}

static const struct vm_operations_struct vs_vm_ops = {
	.open  = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct drm_gem_object_funcs vs_gem_default_funcs = {
	.free = vs_gem_free_object,
	.get_sg_table = vs_gem_prime_get_sg_table,
	.vmap = vs_gem_prime_vmap,
	.vunmap = vs_gem_prime_vunmap,
	.vm_ops = &vs_vm_ops,
};

int vs_gem_dumb_create(struct drm_file *file,
			   struct drm_device *dev,
			   struct drm_mode_create_dumb *args)
{
	struct vs_drm_private *priv = dev->dev_private;
	struct vs_gem_object *vs_obj;
	unsigned int pitch = DIV_ROUND_UP(args->width * args->bpp, 8);

	if (args->bpp % 10)
		args->pitch = ALIGN(pitch, priv->pitch_alignment);
	else
		/* for costum 10bit format with no bit gaps */
		args->pitch = pitch;
	args->size = PAGE_ALIGN(args->pitch * args->height);
	vs_obj = vs_gem_create_with_handle(dev, file, args->size,
						 &args->handle);
	return PTR_ERR_OR_ZERO(vs_obj);
}

struct drm_gem_object *vs_gem_prime_import(struct drm_device *dev,
					   struct dma_buf *dma_buf)
{
	return drm_gem_prime_import_dev(dev, dma_buf, to_dma_dev(dev));
}

struct drm_gem_object *
vs_gem_prime_import_sg_table(struct drm_device *dev,
				 struct dma_buf_attachment *attach,
				 struct sg_table *sgt)
{
	struct vs_gem_object *vs_obj;
	int npages;
	int ret;
	struct scatterlist *s;
	u32 i;
	dma_addr_t expected;
	size_t size = attach->dmabuf->size;
#ifdef CONFIG_VERISILICON_MMU
	u32 iova;
	struct vs_drm_private *priv = dev->dev_private;

	if (!priv->mmu) {
		DRM_ERROR("invalid mmu.\n");
		ret = -EINVAL;
		return ERR_PTR(ret);
	}
#endif

	size = PAGE_ALIGN(size);

	vs_obj = vs_gem_alloc_object(dev, size);
	if (IS_ERR(vs_obj))
		return ERR_CAST(vs_obj);

	expected = sg_dma_address(sgt->sgl);
	for_each_sg(sgt->sgl, s, sgt->nents, i) {
		if (sg_dma_address(s) != expected) {
#ifndef CONFIG_VERISILICON_MMU
			DRM_ERROR("sg_table is not contiguous");
			ret = -EINVAL;
			goto err;
#endif
		}

		if (sg_dma_len(s) & (PAGE_SIZE-1)) {
			ret = -EINVAL;
			goto err;
		}

#ifdef CONFIG_VERISILICON_MMU
		iova = 0;
		npages = sg_dma_len(s) >> PAGE_SHIFT;
		ret = dc_mmu_map_memory(priv->mmu, (u64)sg_dma_address(s),
					npages, &iova, true, false);
		if (ret) {
			DRM_ERROR("failed to do mmu map.\n");
			goto err;
		}

		if (i == 0)
			vs_obj->iova = iova;
#else
		if (i == 0)
			vs_obj->iova = sg_dma_address(s);
#endif

		expected = sg_dma_address(s) + sg_dma_len(s);
	}

	vs_obj->dma_addr = sg_dma_address(sgt->sgl);

	npages = vs_obj->size >> PAGE_SHIFT;
	vs_obj->pages = kvmalloc_array(npages, sizeof(struct page *),
					GFP_KERNEL);
	if (!vs_obj->pages) {
		ret = -ENOMEM;
		goto err;
	}

	//ret = drm_prime_sg_to_dma_addr_array(sgt, vs_obj->dma_addr,npages);
	ret = drm_prime_sg_to_page_array(sgt, vs_obj->pages, npages);
	if (ret)
		goto err_free_page;

	vs_obj->sgt = sgt;

	return &vs_obj->base;

err_free_page:
	kvfree(vs_obj->pages);
err:
	vs_gem_free_object(&vs_obj->base);

	return ERR_PTR(ret);
}

int vs_gem_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	int ret = 0;

	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	if (ret < 0)
		return ret;

	return vs_gem_mmap_obj(obj, vma);
}

int vs_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_gem_object *obj;
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	obj = vma->vm_private_data;

	if (obj->import_attach)
		return dma_buf_mmap(obj->dma_buf, vma, 0);

	return vs_gem_mmap_obj(obj, vma);
}

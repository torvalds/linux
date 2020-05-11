/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drm.h>
#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <drm/drm_vma_manager.h>

#include <linux/dma-buf.h>
#include <linux/genalloc.h>
#include <linux/iommu.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_gem.h"

struct page_info {
	struct page *page;
	struct list_head list;
};

#define PG_ROUND       8

static int rockchip_gem_iommu_map(struct rockchip_gem_object *rk_obj)
{
	struct drm_device *drm = rk_obj->base.dev;
	struct rockchip_drm_private *private = drm->dev_private;
	int prot = IOMMU_READ | IOMMU_WRITE | IOMMU_TLB_SHOT_ENTIRE;
	ssize_t ret;

	mutex_lock(&private->mm_lock);
	ret = drm_mm_insert_node_generic(&private->mm, &rk_obj->mm,
					 rk_obj->base.size, PAGE_SIZE,
					 0, 0);
	mutex_unlock(&private->mm_lock);

	if (ret < 0) {
		DRM_ERROR("out of I/O virtual memory: %zd\n", ret);
		return ret;
	}

	rk_obj->dma_addr = rk_obj->mm.start;

	ret = iommu_map_sg(private->domain, rk_obj->dma_addr, rk_obj->sgt->sgl,
			   rk_obj->sgt->nents, prot);
	if (ret < rk_obj->base.size) {
		DRM_ERROR("failed to map buffer: size=%zd request_size=%zd\n",
			  ret, rk_obj->base.size);
		ret = -ENOMEM;
		goto err_remove_node;
	}

	rk_obj->size = ret;

	return 0;

err_remove_node:
	mutex_lock(&private->mm_lock);
	drm_mm_remove_node(&rk_obj->mm);
	mutex_unlock(&private->mm_lock);

	return ret;
}

static int rockchip_gem_iommu_unmap(struct rockchip_gem_object *rk_obj)
{
	struct drm_device *drm = rk_obj->base.dev;
	struct rockchip_drm_private *private = drm->dev_private;

	iommu_unmap(private->domain, rk_obj->dma_addr, rk_obj->size);

	mutex_lock(&private->mm_lock);

	drm_mm_remove_node(&rk_obj->mm);

	mutex_unlock(&private->mm_lock);

	return 0;
}

static void rockchip_gem_free_list(struct list_head lists[])
{
	struct page_info *info, *tmp_info;
	int i;

	for (i = 0; i < PG_ROUND; i++) {
		list_for_each_entry_safe(info, tmp_info, &lists[i], list) {
			list_del(&info->list);
			kfree(info);
		}
	}
}

static int rockchip_gem_get_pages(struct rockchip_gem_object *rk_obj)
{
	struct drm_device *drm = rk_obj->base.dev;
	int ret, i;
	struct scatterlist *s;
	unsigned int cur_page;
	struct page **pages, **dst_pages;
	int j;
	int n_pages;
	unsigned long chunk_pages;
	unsigned long remain;
	struct list_head lists[PG_ROUND];
	dma_addr_t phys;
	int end = 0;
	unsigned int bit12_14;
	unsigned int block_index[PG_ROUND] = {0};
	struct page_info *info;
	unsigned int maximum;

	for (i = 0; i < PG_ROUND; i++)
		INIT_LIST_HEAD(&lists[i]);

	pages = drm_gem_get_pages(&rk_obj->base);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	rk_obj->pages = pages;

	rk_obj->num_pages = rk_obj->base.size >> PAGE_SHIFT;

	n_pages = rk_obj->num_pages;

	dst_pages = __vmalloc(sizeof(struct page *) * n_pages,
			 GFP_KERNEL | __GFP_HIGHMEM, PAGE_KERNEL);
	if (!dst_pages) {
		ret = -ENOMEM;
		goto err_put_pages;
	}

	cur_page = 0;
	remain = n_pages;
	/* look for the end of the current chunk */
	while (remain) {
		for (j = cur_page + 1; j < n_pages; ++j) {
			if (page_to_pfn(pages[j]) !=
				page_to_pfn(pages[j - 1]) + 1)
				break;
		}

		chunk_pages = j - cur_page;
		if (chunk_pages > 7) {
			for (i = 0; i < chunk_pages; i++)
				dst_pages[end + i] = pages[cur_page + i];
			end += chunk_pages;
		} else {
			for (i = 0; i < chunk_pages; i++) {
				info = kmalloc(sizeof(*info), GFP_KERNEL);
				if (!info) {
					ret = -ENOMEM;
					goto err_put_list;
				}

				INIT_LIST_HEAD(&info->list);
				info->page = pages[cur_page + i];
				phys = page_to_phys(info->page);
				bit12_14 = (phys >> 12) & 0x7;
				list_add_tail(&info->list, &lists[bit12_14]);
				block_index[bit12_14]++;
			}
		}

		cur_page = j;
		remain -= chunk_pages;
	}

	maximum = block_index[0];
	for (i = 1; i < PG_ROUND; i++)
		maximum = max(maximum, block_index[i]);

	for (i = 0; i < maximum; i++) {
		for (j = 0; j < PG_ROUND; j++) {
			if (!list_empty(&lists[j])) {
				struct page_info *info;

				info = list_first_entry(&lists[j],
							struct page_info, list);
				dst_pages[end++] = info->page;
				list_del(&info->list);
				kfree(info);
			}
		}
	}

	DRM_DEBUG_KMS("%s, %d, end = %d, n_pages = %d\n", __func__, __LINE__,
			end, n_pages);

	rk_obj->sgt = drm_prime_pages_to_sg(dst_pages, rk_obj->num_pages);

	if (IS_ERR(rk_obj->sgt)) {
		ret = PTR_ERR(rk_obj->sgt);
		goto err_put_list;
	}

	rk_obj->pages = dst_pages;

	/*
	 * Fake up the SG table so that dma_sync_sg_for_device() can be used
	 * to flush the pages associated with it.
	 *
	 * TODO: Replace this by drm_clflush_sg() once it can be implemented
	 * without relying on symbols that are not exported.
	 */
	for_each_sg(rk_obj->sgt->sgl, s, rk_obj->sgt->nents, i)
		sg_dma_address(s) = sg_phys(s);

	dma_sync_sg_for_device(drm->dev, rk_obj->sgt->sgl, rk_obj->sgt->nents,
			       DMA_TO_DEVICE);

	kvfree(pages);

	return 0;

err_put_list:
	rockchip_gem_free_list(lists);
	kvfree(dst_pages);
err_put_pages:
	drm_gem_put_pages(&rk_obj->base, rk_obj->pages, false, false);
	return ret;
}

static void rockchip_gem_put_pages(struct rockchip_gem_object *rk_obj)
{
	sg_free_table(rk_obj->sgt);
	kfree(rk_obj->sgt);
	drm_gem_put_pages(&rk_obj->base, rk_obj->pages, true, true);
}

static inline void *drm_calloc_large(size_t nmemb, size_t size);
static inline void drm_free_large(void *ptr);
static void rockchip_gem_free_dma(struct rockchip_gem_object *rk_obj);
static int rockchip_gem_alloc_dma(struct rockchip_gem_object *rk_obj,
				  bool alloc_kmap)
{
	struct drm_gem_object *obj = &rk_obj->base;
	struct drm_device *drm = obj->dev;
	struct sg_table *sgt;
	int ret, i;
	struct scatterlist *s;

	rk_obj->dma_attrs = DMA_ATTR_WRITE_COMBINE;

	if (!alloc_kmap)
		rk_obj->dma_attrs |= DMA_ATTR_NO_KERNEL_MAPPING;

	rk_obj->kvaddr = dma_alloc_attrs(drm->dev, obj->size,
					 &rk_obj->dma_handle, GFP_KERNEL,
					 rk_obj->dma_attrs);
	if (!rk_obj->kvaddr) {
		DRM_ERROR("failed to allocate %zu byte dma buffer", obj->size);
		return -ENOMEM;
	}

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		ret = -ENOMEM;
		goto err_dma_free;
	}

	ret = dma_get_sgtable_attrs(drm->dev, sgt, rk_obj->kvaddr,
				    rk_obj->dma_handle, obj->size,
				    rk_obj->dma_attrs);
	if (ret) {
		DRM_ERROR("failed to allocate sgt, %d\n", ret);
		goto err_sgt_free;
	}

	for_each_sg(sgt->sgl, s, sgt->nents, i)
		sg_dma_address(s) = sg_phys(s);

	rk_obj->num_pages = rk_obj->base.size >> PAGE_SHIFT;

	rk_obj->pages = drm_calloc_large(rk_obj->num_pages,
					 sizeof(*rk_obj->pages));
	if (!rk_obj->pages) {
		DRM_ERROR("failed to allocate pages.\n");
		goto err_sg_table_free;
	}

	if (drm_prime_sg_to_page_addr_arrays(sgt, rk_obj->pages, NULL,
					     rk_obj->num_pages)) {
		DRM_ERROR("invalid sgtable.\n");
		ret = -EINVAL;
		goto err_page_free;
	}

	rk_obj->sgt = sgt;

	return 0;

err_page_free:
	drm_free_large(rk_obj->pages);
err_sg_table_free:
	sg_free_table(sgt);
err_sgt_free:
	kfree(sgt);
err_dma_free:
	dma_free_attrs(drm->dev, obj->size, rk_obj->kvaddr,
		       rk_obj->dma_addr, rk_obj->dma_attrs);

	return ret;
}

static inline void *drm_calloc_large(size_t nmemb, size_t size)
{
	if (size != 0 && nmemb > SIZE_MAX / size)
		return NULL;

	if (size * nmemb <= PAGE_SIZE)
		return kcalloc(nmemb, size, GFP_KERNEL);

	return __vmalloc(size * nmemb,
			 GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO, PAGE_KERNEL);
}

static inline void drm_free_large(void *ptr)
{
	kvfree(ptr);
}

static int rockchip_gem_alloc_secure(struct rockchip_gem_object *rk_obj)
{
	struct drm_gem_object *obj = &rk_obj->base;
	struct drm_device *drm = obj->dev;
	struct rockchip_drm_private *private = drm->dev_private;
	unsigned long paddr;
	struct sg_table *sgt;
	int ret = 0, i;

	if (!private->secure_buffer_pool) {
		DRM_ERROR("No secure buffer pool found\n");
		return -ENOMEM;
	}

	paddr = gen_pool_alloc(private->secure_buffer_pool, rk_obj->base.size);
	if (!paddr) {
		DRM_ERROR("failed to allocate secure buffer\n");
		return -ENOMEM;
	}

	rk_obj->dma_handle = paddr;
	rk_obj->num_pages = rk_obj->base.size >> PAGE_SHIFT;

	rk_obj->pages = drm_calloc_large(rk_obj->num_pages,
					 sizeof(*rk_obj->pages));
	if (!rk_obj->pages) {
		DRM_ERROR("failed to allocate pages.\n");
		ret = -ENOMEM;
		goto err_buf_free;
	}

	i = 0;
	while (i < rk_obj->num_pages) {
		rk_obj->pages[i] = phys_to_page(paddr);
		paddr += PAGE_SIZE;
		i++;
	}
	sgt = drm_prime_pages_to_sg(rk_obj->pages, rk_obj->num_pages);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto err_free_pages;
	}

	rk_obj->sgt = sgt;

	return 0;

err_free_pages:
	drm_free_large(rk_obj->pages);
err_buf_free:
	gen_pool_free(private->secure_buffer_pool, paddr, rk_obj->base.size);

	return ret;
}

static void rockchip_gem_free_secure(struct rockchip_gem_object *rk_obj)
{
	struct drm_gem_object *obj = &rk_obj->base;
	struct drm_device *drm = obj->dev;
	struct rockchip_drm_private *private = drm->dev_private;

	drm_free_large(rk_obj->pages);
	sg_free_table(rk_obj->sgt);
	kfree(rk_obj->sgt);
	gen_pool_free(private->secure_buffer_pool, rk_obj->dma_handle,
		      rk_obj->base.size);
}

static int rockchip_gem_alloc_buf(struct rockchip_gem_object *rk_obj,
				  bool alloc_kmap)
{
	struct drm_gem_object *obj = &rk_obj->base;
	struct drm_device *drm = obj->dev;
	struct rockchip_drm_private *private = drm->dev_private;
	int ret = 0;

	if (!private->domain)
		rk_obj->flags |= ROCKCHIP_BO_CONTIG;

	if (rk_obj->flags & ROCKCHIP_BO_SECURE) {
		rk_obj->buf_type = ROCKCHIP_GEM_BUF_TYPE_SECURE;
		rk_obj->flags |= ROCKCHIP_BO_CONTIG;
		if (alloc_kmap) {
			DRM_ERROR("Not allow alloc secure buffer with kmap\n");
			return -EINVAL;
		}
		ret = rockchip_gem_alloc_secure(rk_obj);
		if (ret)
			return ret;
	} else if (rk_obj->flags & ROCKCHIP_BO_CONTIG) {
		rk_obj->buf_type = ROCKCHIP_GEM_BUF_TYPE_CMA;
		ret = rockchip_gem_alloc_dma(rk_obj, alloc_kmap);
		if (ret)
			return ret;
	} else {
		rk_obj->buf_type = ROCKCHIP_GEM_BUF_TYPE_SHMEM;
		ret = rockchip_gem_get_pages(rk_obj);
		if (ret < 0)
			return ret;

		if (alloc_kmap) {
			rk_obj->kvaddr = vmap(rk_obj->pages, rk_obj->num_pages,
					      VM_MAP,
					      pgprot_writecombine(PAGE_KERNEL));
			if (!rk_obj->kvaddr) {
				DRM_ERROR("failed to vmap() buffer\n");
				ret = -ENOMEM;
				goto err_iommu_free;
			}
		}
	}

	if (private->domain) {
		ret = rockchip_gem_iommu_map(rk_obj);
		if (ret < 0)
			goto err_free;
	} else {
		WARN_ON(!rk_obj->dma_handle);
		rk_obj->dma_addr = rk_obj->dma_handle;
	}

	return 0;

err_iommu_free:
	if (private->domain)
		rockchip_gem_iommu_unmap(rk_obj);
err_free:
	if (rk_obj->buf_type == ROCKCHIP_GEM_BUF_TYPE_SECURE)
		rockchip_gem_free_secure(rk_obj);
	else if (rk_obj->buf_type == ROCKCHIP_GEM_BUF_TYPE_CMA)
		rockchip_gem_free_dma(rk_obj);
	else
		rockchip_gem_put_pages(rk_obj);
	return ret;
}

static void rockchip_gem_free_dma(struct rockchip_gem_object *rk_obj)
{
	struct drm_gem_object *obj = &rk_obj->base;
	struct drm_device *drm = obj->dev;

	drm_free_large(rk_obj->pages);
	sg_free_table(rk_obj->sgt);
	kfree(rk_obj->sgt);
	dma_free_attrs(drm->dev, obj->size, rk_obj->kvaddr,
		       rk_obj->dma_handle, rk_obj->dma_attrs);
}

static void rockchip_gem_free_buf(struct rockchip_gem_object *rk_obj)
{
	struct drm_device *drm = rk_obj->base.dev;
	struct rockchip_drm_private *private = drm->dev_private;

	if (private->domain)
		rockchip_gem_iommu_unmap(rk_obj);

	if (rk_obj->buf_type == ROCKCHIP_GEM_BUF_TYPE_SHMEM) {
		vunmap(rk_obj->kvaddr);
		rockchip_gem_put_pages(rk_obj);
	} else if (rk_obj->buf_type == ROCKCHIP_GEM_BUF_TYPE_SECURE) {
		rockchip_gem_free_secure(rk_obj);
	} else {
		rockchip_gem_free_dma(rk_obj);
	}
}

/*
 * __vm_map_pages - maps range of kernel pages into user vma
 * @vma: user vma to map to
 * @pages: pointer to array of source kernel pages
 * @num: number of pages in page array
 * @offset: user's requested vm_pgoff
 *
 * This allows drivers to map range of kernel pages into a user vma.
 *
 * Return: 0 on success and error code otherwise.
 */
static int __vm_map_pages(struct vm_area_struct *vma, struct page **pages,
			  unsigned long num, unsigned long offset)
{
	unsigned long count = vma_pages(vma);
	unsigned long uaddr = vma->vm_start;
	int ret, i;

	/* Fail if the user requested offset is beyond the end of the object */
	if (offset > num)
		return -ENXIO;

	/* Fail if the user requested size exceeds available object size */
	if (count > num - offset)
		return -ENXIO;

	for (i = 0; i < count; i++) {
		ret = vm_insert_page(vma, uaddr, pages[offset + i]);
		if (ret < 0)
			return ret;
		uaddr += PAGE_SIZE;
	}

	return 0;
}

static int rockchip_drm_gem_object_mmap_iommu(struct drm_gem_object *obj,
					      struct vm_area_struct *vma)
{
	struct rockchip_gem_object *rk_obj = to_rockchip_obj(obj);
	unsigned int count = obj->size >> PAGE_SHIFT;
	unsigned long user_count = vma_pages(vma);

	if (user_count == 0)
		return -ENXIO;

	return __vm_map_pages(vma, rk_obj->pages, count, vma->vm_pgoff);
}

static int rockchip_drm_gem_object_mmap_dma(struct drm_gem_object *obj,
					    struct vm_area_struct *vma)
{
	struct rockchip_gem_object *rk_obj = to_rockchip_obj(obj);
	struct drm_device *drm = obj->dev;

	return dma_mmap_attrs(drm->dev, vma, rk_obj->kvaddr, rk_obj->dma_addr,
			      obj->size, rk_obj->dma_attrs);
}

static int rockchip_drm_gem_object_mmap(struct drm_gem_object *obj,
					struct vm_area_struct *vma)
{
	int ret;
	struct rockchip_gem_object *rk_obj = to_rockchip_obj(obj);

	/* default is wc. */
	if (rk_obj->flags & ROCKCHIP_BO_CACHABLE)
		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);

	/*
	 * We allocated a struct page table for rk_obj, so clear
	 * VM_PFNMAP flag that was set by drm_gem_mmap_obj()/drm_gem_mmap().
	 */
	vma->vm_flags &= ~VM_PFNMAP;

	if (rk_obj->buf_type == ROCKCHIP_GEM_BUF_TYPE_SECURE) {
		DRM_ERROR("Disallow mmap for secure buffer\n");
		ret = -EINVAL;
	} else if (rk_obj->pages) {
		ret = rockchip_drm_gem_object_mmap_iommu(obj, vma);
	} else {
		ret = rockchip_drm_gem_object_mmap_dma(obj, vma);
	}

	if (ret)
		drm_gem_vm_close(vma);

	return ret;
}

int rockchip_gem_mmap_buf(struct drm_gem_object *obj,
			  struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	if (ret)
		return ret;

	return rockchip_drm_gem_object_mmap(obj, vma);
}

/* drm driver mmap file operations */
int rockchip_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_gem_object *obj;
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	/*
	 * Set vm_pgoff (used as a fake buffer offset by DRM) to 0 and map the
	 * whole buffer from the start.
	 */
	vma->vm_pgoff = 0;

	obj = vma->vm_private_data;

	return rockchip_drm_gem_object_mmap(obj, vma);
}

static void rockchip_gem_release_object(struct rockchip_gem_object *rk_obj)
{
	drm_gem_object_release(&rk_obj->base);
	kfree(rk_obj);
}

static struct rockchip_gem_object *
	rockchip_gem_alloc_object(struct drm_device *drm, unsigned int size)
{
	struct address_space *mapping;
	struct rockchip_gem_object *rk_obj;
	struct drm_gem_object *obj;

#ifdef CONFIG_ARM_LPAE
	gfp_t gfp_mask = GFP_HIGHUSER | __GFP_RECLAIMABLE | __GFP_DMA32;
#else
	gfp_t gfp_mask = GFP_HIGHUSER | __GFP_RECLAIMABLE;
#endif
	size = round_up(size, PAGE_SIZE);

	rk_obj = kzalloc(sizeof(*rk_obj), GFP_KERNEL);
	if (!rk_obj)
		return ERR_PTR(-ENOMEM);

	obj = &rk_obj->base;

	drm_gem_object_init(drm, obj, size);

	mapping = file_inode(obj->filp)->i_mapping;
	mapping_set_gfp_mask(mapping, gfp_mask);

	return rk_obj;
}

struct rockchip_gem_object *
rockchip_gem_create_object(struct drm_device *drm, unsigned int size,
			   bool alloc_kmap, unsigned int flags)
{
	struct rockchip_gem_object *rk_obj;
	int ret;

	rk_obj = rockchip_gem_alloc_object(drm, size);
	if (IS_ERR(rk_obj))
		return rk_obj;
	rk_obj->flags = flags;

	ret = rockchip_gem_alloc_buf(rk_obj, alloc_kmap);
	if (ret)
		goto err_free_rk_obj;

	return rk_obj;

err_free_rk_obj:
	rockchip_gem_release_object(rk_obj);
	return ERR_PTR(ret);
}

/*
 * rockchip_gem_free_object - (struct drm_driver)->gem_free_object_unlocked
 * callback function
 */
void rockchip_gem_free_object(struct drm_gem_object *obj)
{
	struct drm_device *drm = obj->dev;
	struct rockchip_drm_private *private = drm->dev_private;
	struct rockchip_gem_object *rk_obj = to_rockchip_obj(obj);

	if (obj->import_attach) {
		if (private->domain) {
			rockchip_gem_iommu_unmap(rk_obj);
		} else {
			dma_unmap_sg(drm->dev, rk_obj->sgt->sgl,
				     rk_obj->sgt->nents, DMA_BIDIRECTIONAL);
		}
#ifndef CONFIG_ARCH_ROCKCHIP
		drm_prime_gem_destroy(obj, rk_obj->sgt);
#endif
	} else {
		rockchip_gem_free_buf(rk_obj);
	}

	rockchip_gem_release_object(rk_obj);
}

/*
 * rockchip_gem_create_with_handle - allocate an object with the given
 * size and create a gem handle on it
 *
 * returns a struct rockchip_gem_object* on success or ERR_PTR values
 * on failure.
 */
static struct rockchip_gem_object *
rockchip_gem_create_with_handle(struct drm_file *file_priv,
				struct drm_device *drm, unsigned int size,
				unsigned int *handle, unsigned int flags)
{
	struct rockchip_gem_object *rk_obj;
	struct drm_gem_object *obj;
	int ret;

	rk_obj = rockchip_gem_create_object(drm, size, false, flags);
	if (IS_ERR(rk_obj))
		return ERR_CAST(rk_obj);

	obj = &rk_obj->base;

	/*
	 * allocate a id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, obj, handle);
	if (ret)
		goto err_handle_create;

	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_put_unlocked(obj);

	return rk_obj;

err_handle_create:
	rockchip_gem_free_object(obj);

	return ERR_PTR(ret);
}

/*
 * rockchip_gem_dumb_create - (struct drm_driver)->dumb_create callback
 * function
 *
 * This aligns the pitch and size arguments to the minimum required. wrap
 * this into your own function if you need bigger alignment.
 */
int rockchip_gem_dumb_create(struct drm_file *file_priv,
			     struct drm_device *dev,
			     struct drm_mode_create_dumb *args)
{
	struct rockchip_gem_object *rk_obj;
	int min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);

	/*
	 * align to 64 bytes since Mali requires it.
	 */
	args->pitch = ALIGN(min_pitch, 64);
	args->size = args->pitch * args->height;

	rk_obj = rockchip_gem_create_with_handle(file_priv, dev, args->size,
						 &args->handle, args->flags);

	return PTR_ERR_OR_ZERO(rk_obj);
}

/*
 * Allocate a sg_table for this GEM object.
 * Note: Both the table's contents, and the sg_table itself must be freed by
 *       the caller.
 * Returns a pointer to the newly allocated sg_table, or an ERR_PTR() error.
 */
struct sg_table *rockchip_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct rockchip_gem_object *rk_obj = to_rockchip_obj(obj);
	struct drm_device *drm = obj->dev;
	struct sg_table *sgt;
	int ret;

	if (rk_obj->pages)
		return drm_prime_pages_to_sg(rk_obj->pages, rk_obj->num_pages);

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	ret = dma_get_sgtable_attrs(drm->dev, sgt, rk_obj->kvaddr,
				    rk_obj->dma_addr, obj->size,
				    rk_obj->dma_attrs);
	if (ret) {
		DRM_ERROR("failed to allocate sgt, %d\n", ret);
		kfree(sgt);
		return ERR_PTR(ret);
	}

	return sgt;
}

static unsigned long rockchip_sg_get_contiguous_size(struct sg_table *sgt,
						     int count)
{
	struct scatterlist *s;
	dma_addr_t expected = sg_dma_address(sgt->sgl);
	unsigned int i;
	unsigned long size = 0;

	for_each_sg(sgt->sgl, s, count, i) {
		if (sg_dma_address(s) != expected)
			break;
		expected = sg_dma_address(s) + sg_dma_len(s);
		size += sg_dma_len(s);
	}
	return size;
}

static int
rockchip_gem_iommu_map_sg(struct drm_device *drm,
			  struct dma_buf_attachment *attach,
			  struct sg_table *sg,
			  struct rockchip_gem_object *rk_obj)
{
	rk_obj->sgt = sg;
	return rockchip_gem_iommu_map(rk_obj);
}

static int
rockchip_gem_dma_map_sg(struct drm_device *drm,
			struct dma_buf_attachment *attach,
			struct sg_table *sg,
			struct rockchip_gem_object *rk_obj)
{
	int count = dma_map_sg(drm->dev, sg->sgl, sg->nents,
			       DMA_BIDIRECTIONAL);
	if (!count)
		return -EINVAL;

	if (rockchip_sg_get_contiguous_size(sg, count) < attach->dmabuf->size) {
		DRM_ERROR("failed to map sg_table to contiguous linear address.\n");
		dma_unmap_sg(drm->dev, sg->sgl, sg->nents,
			     DMA_BIDIRECTIONAL);
		return -EINVAL;
	}

	rk_obj->dma_addr = sg_dma_address(sg->sgl);
	rk_obj->sgt = sg;
	return 0;
}

struct drm_gem_object *
rockchip_gem_prime_import_sg_table(struct drm_device *drm,
				   struct dma_buf_attachment *attach,
				   struct sg_table *sg)
{
	struct rockchip_drm_private *private = drm->dev_private;
	struct rockchip_gem_object *rk_obj;
	int ret;

	rk_obj = rockchip_gem_alloc_object(drm, attach->dmabuf->size);
	if (IS_ERR(rk_obj))
		return ERR_CAST(rk_obj);

	if (private->domain)
		ret = rockchip_gem_iommu_map_sg(drm, attach, sg, rk_obj);
	else
		ret = rockchip_gem_dma_map_sg(drm, attach, sg, rk_obj);

	if (ret < 0) {
		DRM_ERROR("failed to import sg table: %d\n", ret);
		goto err_free_rk_obj;
	}

	return &rk_obj->base;

err_free_rk_obj:
	rockchip_gem_release_object(rk_obj);
	return ERR_PTR(ret);
}

void *rockchip_gem_prime_vmap(struct drm_gem_object *obj)
{
	struct rockchip_gem_object *rk_obj = to_rockchip_obj(obj);

	if (rk_obj->pages)
		return vmap(rk_obj->pages, rk_obj->num_pages, VM_MAP,
			    pgprot_writecombine(PAGE_KERNEL));

	if (rk_obj->dma_attrs & DMA_ATTR_NO_KERNEL_MAPPING)
		return NULL;

	return rk_obj->kvaddr;
}

void rockchip_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	struct rockchip_gem_object *rk_obj = to_rockchip_obj(obj);

	if (rk_obj->pages) {
		vunmap(vaddr);
		return;
	}

	/* Nothing to do if allocated by DMA mapping API. */
}

int rockchip_gem_dumb_map_offset(struct drm_file *file_priv,
				 struct drm_device *dev, uint32_t handle,
				 uint64_t *offset)
{
	struct drm_gem_object *obj;
	int ret;

	obj = drm_gem_object_lookup(file_priv, handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return -EINVAL;
	}

	ret = drm_gem_create_mmap_offset(obj);
	if (ret)
		goto out;

	*offset = drm_vma_node_offset_addr(&obj->vma_node);
	DRM_DEBUG_KMS("offset = 0x%llx\n", *offset);

out:
	drm_gem_object_unreference_unlocked(obj);

	return 0;
}

int rockchip_gem_create_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_rockchip_gem_create *args = data;
	struct rockchip_gem_object *rk_obj;

	rk_obj = rockchip_gem_create_with_handle(file_priv, dev, args->size,
						 &args->handle, args->flags);
	return PTR_ERR_OR_ZERO(rk_obj);
}

int rockchip_gem_map_offset_ioctl(struct drm_device *drm, void *data,
				  struct drm_file *file_priv)
{
	struct drm_rockchip_gem_map_off *args = data;

	return rockchip_gem_dumb_map_offset(file_priv, drm, args->handle,
					    &args->offset);
}

int rockchip_gem_get_phys_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_rockchip_gem_phys *args = data;
	struct rockchip_gem_object *rk_obj;
	struct drm_gem_object *obj;
	int ret = 0;

	obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return -EINVAL;
	}
	rk_obj = to_rockchip_obj(obj);

	if (!(rk_obj->flags & ROCKCHIP_BO_CONTIG)) {
		DRM_ERROR("Can't get phys address from non-continue buf.\n");
		ret = -EINVAL;
		goto out;
	}

	args->phy_addr = page_to_phys(rk_obj->pages[0]);

out:
	drm_gem_object_unreference_unlocked(obj);
	return ret;
}

int rockchip_gem_prime_begin_cpu_access(struct drm_gem_object *obj,
					enum dma_data_direction dir)
{
	struct rockchip_gem_object *rk_obj = to_rockchip_obj(obj);
	struct drm_device *drm = obj->dev;

	if (!rk_obj->sgt)
		return 0;

	dma_sync_sg_for_cpu(drm->dev, rk_obj->sgt->sgl,
			    rk_obj->sgt->nents, dir);
	return 0;
}

int rockchip_gem_prime_end_cpu_access(struct drm_gem_object *obj,
				   enum dma_data_direction dir)
{
	struct rockchip_gem_object *rk_obj = to_rockchip_obj(obj);
	struct drm_device *drm = obj->dev;

	if (!rk_obj->sgt)
		return 0;

	dma_sync_sg_for_device(drm->dev, rk_obj->sgt->sgl,
			       rk_obj->sgt->nents, dir);
	return 0;
}

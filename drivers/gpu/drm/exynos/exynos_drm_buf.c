/* exynos_drm_buf.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Author: Inki Dae <inki.dae@samsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "drm.h"
#include "exynos_drm.h"

#include "exynos_drm_drv.h"
#include "exynos_drm_gem.h"
#include "exynos_drm_buf.h"

static int lowlevel_buffer_allocate(struct drm_device *dev,
		unsigned int flags, struct exynos_drm_gem_buf *buf)
{
	dma_addr_t start_addr, end_addr;
	unsigned int npages, page_size, i = 0;
	struct scatterlist *sgl;
	int ret = 0;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (flags & EXYNOS_BO_NONCONTIG) {
		DRM_DEBUG_KMS("not support allocation type.\n");
		return -EINVAL;
	}

	if (buf->dma_addr) {
		DRM_DEBUG_KMS("already allocated.\n");
		return 0;
	}

	if (buf->size >= SZ_1M) {
		npages = (buf->size >> SECTION_SHIFT) + 1;
		page_size = SECTION_SIZE;
	} else if (buf->size >= SZ_64K) {
		npages = (buf->size >> 16) + 1;
		page_size = SZ_64K;
	} else {
		npages = (buf->size >> PAGE_SHIFT) + 1;
		page_size = PAGE_SIZE;
	}

	buf->sgt = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!buf->sgt) {
		DRM_ERROR("failed to allocate sg table.\n");
		return -ENOMEM;
	}

	ret = sg_alloc_table(buf->sgt, npages, GFP_KERNEL);
	if (ret < 0) {
		DRM_ERROR("failed to initialize sg table.\n");
		kfree(buf->sgt);
		buf->sgt = NULL;
		return -ENOMEM;
	}

		buf->kvaddr = dma_alloc_writecombine(dev->dev, buf->size,
				&buf->dma_addr, GFP_KERNEL);
		if (!buf->kvaddr) {
			DRM_ERROR("failed to allocate buffer.\n");
			ret = -ENOMEM;
			goto err1;
		}

		start_addr = buf->dma_addr;
		end_addr = buf->dma_addr + buf->size;

		buf->pages = kzalloc(sizeof(struct page) * npages, GFP_KERNEL);
		if (!buf->pages) {
			DRM_ERROR("failed to allocate pages.\n");
			ret = -ENOMEM;
			goto err2;
		}

	start_addr = buf->dma_addr;
	end_addr = buf->dma_addr + buf->size;

	buf->pages = kzalloc(sizeof(struct page) * npages, GFP_KERNEL);
	if (!buf->pages) {
		DRM_ERROR("failed to allocate pages.\n");
		ret = -ENOMEM;
		goto err2;
	}

	sgl = buf->sgt->sgl;

	while (i < npages) {
		buf->pages[i] = phys_to_page(start_addr);
		sg_set_page(sgl, buf->pages[i], page_size, 0);
		sg_dma_address(sgl) = start_addr;
		start_addr += page_size;
		if (end_addr - start_addr < page_size)
			break;
		sgl = sg_next(sgl);
		i++;
	}

	buf->pages[i] = phys_to_page(start_addr);

	sgl = sg_next(sgl);
	sg_set_page(sgl, buf->pages[i+1], end_addr - start_addr, 0);

	DRM_DEBUG_KMS("vaddr(0x%lx), dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)buf->kvaddr,
			(unsigned long)buf->dma_addr,
			buf->size);

	return ret;
err2:
	dma_free_writecombine(dev->dev, buf->size, buf->kvaddr,
			(dma_addr_t)buf->dma_addr);
	buf->dma_addr = (dma_addr_t)NULL;
err1:
	sg_free_table(buf->sgt);
	kfree(buf->sgt);
	buf->sgt = NULL;

	return ret;
}

static void lowlevel_buffer_deallocate(struct drm_device *dev,
		unsigned int flags, struct exynos_drm_gem_buf *buf)
{
	DRM_DEBUG_KMS("%s.\n", __FILE__);

	/*
	 * release only physically continuous memory and
	 * non-continuous memory would be released by exynos
	 * gem framework.
	 */
	if (flags & EXYNOS_BO_NONCONTIG) {
		DRM_DEBUG_KMS("not support allocation type.\n");
		return;
	}

	if (!buf->dma_addr) {
		DRM_DEBUG_KMS("dma_addr is invalid.\n");
		return;
	}

	DRM_DEBUG_KMS("vaddr(0x%lx), dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)buf->kvaddr,
			(unsigned long)buf->dma_addr,
			buf->size);

	sg_free_table(buf->sgt);

	kfree(buf->sgt);
	buf->sgt = NULL;

	kfree(buf->pages);
	buf->pages = NULL;

	dma_free_writecombine(dev->dev, buf->size, buf->kvaddr,
				(dma_addr_t)buf->dma_addr);
	buf->dma_addr = (dma_addr_t)NULL;
}

struct exynos_drm_gem_buf *exynos_drm_init_buf(struct drm_device *dev,
						unsigned int size)
{
	struct exynos_drm_gem_buf *buffer;

	DRM_DEBUG_KMS("%s.\n", __FILE__);
	DRM_DEBUG_KMS("desired size = 0x%x\n", size);

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer) {
		DRM_ERROR("failed to allocate exynos_drm_gem_buf.\n");
		return NULL;
	}

	buffer->size = size;
	return buffer;
}

void exynos_drm_fini_buf(struct drm_device *dev,
				struct exynos_drm_gem_buf *buffer)
{
	DRM_DEBUG_KMS("%s.\n", __FILE__);

	if (!buffer) {
		DRM_DEBUG_KMS("buffer is null.\n");
		return;
	}

	kfree(buffer);
	buffer = NULL;
}

int exynos_drm_alloc_buf(struct drm_device *dev,
		struct exynos_drm_gem_buf *buf, unsigned int flags)
{

	/*
	 * allocate memory region and set the memory information
	 * to vaddr and dma_addr of a buffer object.
	 */
	if (lowlevel_buffer_allocate(dev, flags, buf) < 0)
		return -ENOMEM;

	return 0;
}

void exynos_drm_free_buf(struct drm_device *dev,
		unsigned int flags, struct exynos_drm_gem_buf *buffer)
{

	lowlevel_buffer_deallocate(dev, flags, buffer);
}

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

#include "exynos_drm_drv.h"
#include "exynos_drm_gem.h"
#include "exynos_drm_buf.h"

static int lowlevel_buffer_allocate(struct drm_device *dev,
		struct exynos_drm_gem_buf *buffer)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	buffer->kvaddr = dma_alloc_writecombine(dev->dev, buffer->size,
			&buffer->dma_addr, GFP_KERNEL);
	if (!buffer->kvaddr) {
		DRM_ERROR("failed to allocate buffer.\n");
		return -ENOMEM;
	}

	DRM_DEBUG_KMS("vaddr(0x%lx), dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)buffer->kvaddr,
			(unsigned long)buffer->dma_addr,
			buffer->size);

	return 0;
}

static void lowlevel_buffer_deallocate(struct drm_device *dev,
		struct exynos_drm_gem_buf *buffer)
{
	DRM_DEBUG_KMS("%s.\n", __FILE__);

	if (buffer->dma_addr && buffer->size)
		dma_free_writecombine(dev->dev, buffer->size, buffer->kvaddr,
				(dma_addr_t)buffer->dma_addr);
	else
		DRM_DEBUG_KMS("buffer data are invalid.\n");
}

struct exynos_drm_gem_buf *exynos_drm_buf_create(struct drm_device *dev,
		unsigned int size)
{
	struct exynos_drm_gem_buf *buffer;

	DRM_DEBUG_KMS("%s.\n", __FILE__);
	DRM_DEBUG_KMS("desired size = 0x%x\n", size);

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer) {
		DRM_ERROR("failed to allocate exynos_drm_gem_buf.\n");
		return ERR_PTR(-ENOMEM);
	}

	buffer->size = size;

	/*
	 * allocate memory region with size and set the memory information
	 * to vaddr and dma_addr of a buffer object.
	 */
	if (lowlevel_buffer_allocate(dev, buffer) < 0) {
		kfree(buffer);
		buffer = NULL;
		return ERR_PTR(-ENOMEM);
	}

	return buffer;
}

void exynos_drm_buf_destroy(struct drm_device *dev,
		struct exynos_drm_gem_buf *buffer)
{
	DRM_DEBUG_KMS("%s.\n", __FILE__);

	if (!buffer) {
		DRM_DEBUG_KMS("buffer is null.\n");
		return;
	}

	lowlevel_buffer_deallocate(dev, buffer);

	kfree(buffer);
	buffer = NULL;
}

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM Buffer Management Module");
MODULE_LICENSE("GPL");

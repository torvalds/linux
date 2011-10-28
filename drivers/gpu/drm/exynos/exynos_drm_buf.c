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
#include "exynos_drm_buf.h"

static DEFINE_MUTEX(exynos_drm_buf_lock);

static int lowlevel_buffer_allocate(struct drm_device *dev,
		struct exynos_drm_buf_entry *entry)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	entry->vaddr = dma_alloc_writecombine(dev->dev, entry->size,
			(dma_addr_t *)&entry->paddr, GFP_KERNEL);
	if (!entry->paddr) {
		DRM_ERROR("failed to allocate buffer.\n");
		return -ENOMEM;
	}

	DRM_DEBUG_KMS("allocated : vaddr(0x%x), paddr(0x%x), size(0x%x)\n",
			(unsigned int)entry->vaddr, entry->paddr, entry->size);

	return 0;
}

static void lowlevel_buffer_deallocate(struct drm_device *dev,
		struct exynos_drm_buf_entry *entry)
{
	DRM_DEBUG_KMS("%s.\n", __FILE__);

	if (entry->paddr && entry->vaddr && entry->size)
		dma_free_writecombine(dev->dev, entry->size, entry->vaddr,
				entry->paddr);
	else
		DRM_DEBUG_KMS("entry data is null.\n");
}

struct exynos_drm_buf_entry *exynos_drm_buf_create(struct drm_device *dev,
		unsigned int size)
{
	struct exynos_drm_buf_entry *entry;

	DRM_DEBUG_KMS("%s.\n", __FILE__);

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		DRM_ERROR("failed to allocate exynos_drm_buf_entry.\n");
		return ERR_PTR(-ENOMEM);
	}

	entry->size = size;

	/*
	 * allocate memory region with size and set the memory information
	 * to vaddr and paddr of a entry object.
	 */
	if (lowlevel_buffer_allocate(dev, entry) < 0) {
		kfree(entry);
		entry = NULL;
		return ERR_PTR(-ENOMEM);
	}

	return entry;
}

void exynos_drm_buf_destroy(struct drm_device *dev,
		struct exynos_drm_buf_entry *entry)
{
	DRM_DEBUG_KMS("%s.\n", __FILE__);

	if (!entry) {
		DRM_DEBUG_KMS("entry is null.\n");
		return;
	}

	lowlevel_buffer_deallocate(dev, entry);

	kfree(entry);
	entry = NULL;
}

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM Buffer Management Module");
MODULE_LICENSE("GPL");

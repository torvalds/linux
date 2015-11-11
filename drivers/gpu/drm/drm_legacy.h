#ifndef __DRM_LEGACY_H__
#define __DRM_LEGACY_H__

/*
 * Copyright (c) 2014 David Herrmann <dh.herrmann@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * This file contains legacy interfaces that modern drm drivers
 * should no longer be using. They cannot be removed as legacy
 * drivers use them, and removing them are API breaks.
 */
#include <linux/list.h>
#include <drm/drm_legacy.h>

struct agp_memory;
struct drm_device;
struct drm_file;

/*
 * Generic DRM Contexts
 */

#define DRM_KERNEL_CONTEXT		0
#define DRM_RESERVED_CONTEXTS		1

void drm_legacy_ctxbitmap_init(struct drm_device *dev);
void drm_legacy_ctxbitmap_cleanup(struct drm_device *dev);
void drm_legacy_ctxbitmap_free(struct drm_device *dev, int ctx_handle);
void drm_legacy_ctxbitmap_flush(struct drm_device *dev, struct drm_file *file);

int drm_legacy_resctx(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_addctx(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_getctx(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_switchctx(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_newctx(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_rmctx(struct drm_device *d, void *v, struct drm_file *f);

int drm_legacy_setsareactx(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_getsareactx(struct drm_device *d, void *v, struct drm_file *f);

/*
 * Generic Buffer Management
 */

#define DRM_MAP_HASH_OFFSET 0x10000000

int drm_legacy_addmap_ioctl(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_rmmap_ioctl(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_addbufs(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_infobufs(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_markbufs(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_freebufs(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_mapbufs(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_dma_ioctl(struct drm_device *d, void *v, struct drm_file *f);

void drm_legacy_vma_flush(struct drm_device *d);

/*
 * AGP Support
 */

struct drm_agp_mem {
	unsigned long handle;
	struct agp_memory *memory;
	unsigned long bound;
	int pages;
	struct list_head head;
};

/*
 * Generic Userspace Locking-API
 */

int drm_legacy_i_have_hw_lock(struct drm_device *d, struct drm_file *f);
int drm_legacy_lock(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_unlock(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_lock_free(struct drm_lock_data *lock, unsigned int ctx);

/* DMA support */
int drm_legacy_dma_setup(struct drm_device *dev);
void drm_legacy_dma_takedown(struct drm_device *dev);
void drm_legacy_free_buffer(struct drm_device *dev,
			    struct drm_buf * buf);
void drm_legacy_reclaim_buffers(struct drm_device *dev,
				struct drm_file *filp);

/* Scatter Gather Support */
void drm_legacy_sg_cleanup(struct drm_device *dev);
int drm_legacy_sg_alloc(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int drm_legacy_sg_free(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);

#endif /* __DRM_LEGACY_H__ */

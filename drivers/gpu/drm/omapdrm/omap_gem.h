/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap_gem.h -- OMAP DRM GEM Object Management
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 */

#ifndef __OMAPDRM_GEM_H__
#define __OMAPDRM_GEM_H__

#include <linux/types.h>
#include <linux/mm_types.h>

enum dma_data_direction;

struct dma_buf;
struct drm_device;
struct drm_file;
struct drm_gem_object;
struct drm_mode_create_dumb;
struct file;
struct list_head;
struct page;
struct seq_file;
struct vm_area_struct;
struct vm_fault;

union omap_gem_size;

/* Initialization and Cleanup */
void omap_gem_init(struct drm_device *dev);
void omap_gem_deinit(struct drm_device *dev);

#ifdef CONFIG_PM
int omap_gem_resume(struct drm_device *dev);
#endif

#ifdef CONFIG_DEBUG_FS
void omap_gem_describe(struct drm_gem_object *obj, struct seq_file *m);
void omap_gem_describe_objects(struct list_head *list, struct seq_file *m);
#endif

/* GEM Object Creation and Deletion */
struct drm_gem_object *omap_gem_new(struct drm_device *dev,
		union omap_gem_size gsize, u32 flags);
struct drm_gem_object *omap_gem_new_dmabuf(struct drm_device *dev, size_t size,
		struct sg_table *sgt);
int omap_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		union omap_gem_size gsize, u32 flags, u32 *handle);
void *omap_gem_vaddr(struct drm_gem_object *obj);

/* Dumb Buffers Interface */
int omap_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
		u32 handle, u64 *offset);
int omap_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		struct drm_mode_create_dumb *args);

/* mmap() Interface */
int omap_gem_mmap(struct file *filp, struct vm_area_struct *vma);
int omap_gem_mmap_obj(struct drm_gem_object *obj,
		struct vm_area_struct *vma);
u64 omap_gem_mmap_offset(struct drm_gem_object *obj);
size_t omap_gem_mmap_size(struct drm_gem_object *obj);

/* PRIME Interface */
struct dma_buf *omap_gem_prime_export(struct drm_gem_object *obj, int flags);
struct drm_gem_object *omap_gem_prime_import(struct drm_device *dev,
		struct dma_buf *buffer);

int omap_gem_roll(struct drm_gem_object *obj, u32 roll);
void omap_gem_cpu_sync_page(struct drm_gem_object *obj, int pgoff);
void omap_gem_dma_sync_buffer(struct drm_gem_object *obj,
		enum dma_data_direction dir);
int omap_gem_pin(struct drm_gem_object *obj, dma_addr_t *dma_addr);
void omap_gem_unpin(struct drm_gem_object *obj);
int omap_gem_get_pages(struct drm_gem_object *obj, struct page ***pages,
		bool remap);
int omap_gem_put_pages(struct drm_gem_object *obj);

u32 omap_gem_flags(struct drm_gem_object *obj);
int omap_gem_rotated_dma_addr(struct drm_gem_object *obj, u32 orient,
		int x, int y, dma_addr_t *dma_addr);
int omap_gem_tiled_stride(struct drm_gem_object *obj, u32 orient);
struct sg_table *omap_gem_get_sg(struct drm_gem_object *obj,
		enum dma_data_direction dir);
void omap_gem_put_sg(struct drm_gem_object *obj, struct sg_table *sgt);

#endif /* __OMAPDRM_GEM_H__ */

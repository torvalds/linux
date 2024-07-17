/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#ifndef _MTK_GEM_H_
#define _MTK_GEM_H_

#include <drm/drm_gem.h>

/*
 * mtk drm buffer structure.
 *
 * @base: a gem object.
 *	- a new handle to this gem object would be created
 *	by drm_gem_handle_create().
 * @cookie: the return value of dma_alloc_attrs(), keep it for dma_free_attrs()
 * @kvaddr: kernel virtual address of gem buffer.
 * @dma_addr: dma address of gem buffer.
 * @dma_attrs: dma attributes of gem buffer.
 *
 * P.S. this object would be transferred to user as kms_bo.handle so
 *	user can access the buffer through kms_bo.handle.
 */
struct mtk_gem_obj {
	struct drm_gem_object	base;
	void			*cookie;
	void			*kvaddr;
	dma_addr_t		dma_addr;
	unsigned long		dma_attrs;
	struct sg_table		*sg;
	struct page		**pages;
};

#define to_mtk_gem_obj(x) container_of(x, struct mtk_gem_obj, base)

void mtk_gem_free_object(struct drm_gem_object *gem);
struct mtk_gem_obj *mtk_gem_create(struct drm_device *dev, size_t size,
				   bool alloc_kmap);
int mtk_gem_dumb_create(struct drm_file *file_priv, struct drm_device *dev,
			struct drm_mode_create_dumb *args);
struct sg_table *mtk_gem_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *mtk_gem_prime_import_sg_table(struct drm_device *dev,
			struct dma_buf_attachment *attach, struct sg_table *sg);
int mtk_gem_prime_vmap(struct drm_gem_object *obj, struct iosys_map *map);
void mtk_gem_prime_vunmap(struct drm_gem_object *obj, struct iosys_map *map);

#endif

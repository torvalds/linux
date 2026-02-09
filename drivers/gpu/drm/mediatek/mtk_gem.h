/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#ifndef _MTK_GEM_H_
#define _MTK_GEM_H_

#include <drm/drm_gem.h>
#include <drm/drm_gem_dma_helper.h>

int mtk_gem_dumb_create(struct drm_file *file_priv, struct drm_device *dev,
			struct drm_mode_create_dumb *args);
struct drm_gem_object *mtk_gem_prime_import_sg_table(struct drm_device *dev,
			struct dma_buf_attachment *attach, struct sg_table *sg);

#endif

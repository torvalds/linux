/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * RZ/G2L Display Unit Mode Setting
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 *
 * Based on rcar_du_kms.h
 */

#ifndef __RZG2L_DU_KMS_H__
#define __RZG2L_DU_KMS_H__

#include <linux/types.h>

struct dma_buf_attachment;
struct drm_file;
struct drm_device;
struct drm_gem_object;
struct drm_mode_create_dumb;
struct rzg2l_du_device;
struct sg_table;

struct rzg2l_du_format_info {
	u32 fourcc;
	u32 v4l2;
	unsigned int bpp;
	unsigned int planes;
	unsigned int hsub;
};

const struct rzg2l_du_format_info *rzg2l_du_format_info(u32 fourcc);

int rzg2l_du_modeset_init(struct rzg2l_du_device *rcdu);

int rzg2l_du_dumb_create(struct drm_file *file, struct drm_device *dev,
			 struct drm_mode_create_dumb *args);

struct drm_gem_object *
rzg2l_du_gem_prime_import_sg_table(struct drm_device *dev,
				   struct dma_buf_attachment *attach,
				   struct sg_table *sgt);

#endif /* __RZG2L_DU_KMS_H__ */

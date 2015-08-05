/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _EXYNOS_DRM_ENCODER_H_
#define _EXYNOS_DRM_ENCODER_H_

#include "exynos_drm_drv.h"

void exynos_drm_encoder_setup(struct drm_device *dev);
int exynos_drm_encoder_create(struct drm_device *dev, struct exynos_drm_encoder
			      *encoder, enum exynos_drm_output_type type);

#endif

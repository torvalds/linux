/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap_encoder.h -- OMAP DRM Encoder
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 */

#ifndef __OMAPDRM_ENCODER_H__
#define __OMAPDRM_ENCODER_H__

struct drm_device;
struct drm_encoder;
struct omap_dss_device;

struct drm_encoder *omap_encoder_init(struct drm_device *dev,
				      struct omap_dss_device *output);

#endif /* __OMAPDRM_ENCODER_H__ */

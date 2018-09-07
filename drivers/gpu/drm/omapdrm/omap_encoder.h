/*
 * omap_encoder.h -- OMAP DRM Encoder
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __OMAPDRM_ENCODER_H__
#define __OMAPDRM_ENCODER_H__

struct drm_device;
struct drm_encoder;
struct omap_dss_device;

struct drm_encoder *omap_encoder_init(struct drm_device *dev,
				      struct omap_dss_device *output,
				      struct omap_dss_device *display);

#endif /* __OMAPDRM_ENCODER_H__ */

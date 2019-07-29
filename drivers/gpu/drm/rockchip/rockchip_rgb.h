//SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      Sandy Huang <hjc@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifdef CONFIG_ROCKCHIP_RGB
struct rockchip_rgb *rockchip_rgb_init(struct device *dev,
				       struct drm_crtc *crtc,
				       struct drm_device *drm_dev);
void rockchip_rgb_fini(struct rockchip_rgb *rgb);
#else
static inline struct rockchip_rgb *rockchip_rgb_init(struct device *dev,
						     struct drm_crtc *crtc,
						     struct drm_device *drm_dev)
{
	return NULL;
}

static inline void rockchip_rgb_fini(struct rockchip_rgb *rgb)
{
}
#endif

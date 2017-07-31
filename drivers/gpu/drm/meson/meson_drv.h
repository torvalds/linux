/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MESON_DRV_H
#define __MESON_DRV_H

#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <drm/drmP.h>

struct meson_drm {
	struct device *dev;
	void __iomem *io_base;
	struct regmap *hhi;
	struct regmap *dmc;
	int vsync_irq;

	struct drm_device *drm;
	struct drm_crtc *crtc;
	struct drm_fbdev_cma *fbdev;
	struct drm_plane *primary_plane;

	/* Components Data */
	struct {
		bool osd1_enabled;
		bool osd1_interlace;
		bool osd1_commit;
		uint32_t osd1_ctrl_stat;
		uint32_t osd1_blk0_cfg[5];
	} viu;

	struct {
		unsigned int current_mode;
	} venc;
};

static inline int meson_vpu_is_compatible(struct meson_drm *priv,
					  const char *compat)
{
	return of_device_is_compatible(priv->dev->of_node, compat);
}

#endif /* __MESON_DRV_H */

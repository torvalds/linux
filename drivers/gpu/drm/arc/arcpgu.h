/*
 * ARC PGU DRM driver.
 *
 * Copyright (C) 2016 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _ARCPGU_H_
#define _ARCPGU_H_

struct arcpgu_drm_private {
	void __iomem		*regs;
	struct clk		*clk;
	struct drm_fbdev_cma	*fbdev;
	struct drm_framebuffer	*fb;
	struct list_head	event_list;
	struct drm_crtc		crtc;
	struct drm_plane	*plane;
};

#define crtc_to_arcpgu_priv(x) container_of(x, struct arcpgu_drm_private, crtc)

static inline void arc_pgu_write(struct arcpgu_drm_private *arcpgu,
				 unsigned int reg, u32 value)
{
	iowrite32(value, arcpgu->regs + reg);
}

static inline u32 arc_pgu_read(struct arcpgu_drm_private *arcpgu,
			       unsigned int reg)
{
	return ioread32(arcpgu->regs + reg);
}

int arc_pgu_setup_crtc(struct drm_device *dev);
int arcpgu_drm_hdmi_init(struct drm_device *drm, struct device_node *np);
struct drm_fbdev_cma *arcpgu_fbdev_cma_init(struct drm_device *dev,
	unsigned int preferred_bpp, unsigned int num_crtc,
	unsigned int max_conn_count);

#endif

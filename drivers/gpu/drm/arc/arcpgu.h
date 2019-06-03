/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ARC PGU DRM driver.
 *
 * Copyright (C) 2016 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef _ARCPGU_H_
#define _ARCPGU_H_

struct arcpgu_drm_private {
	void __iomem		*regs;
	struct clk		*clk;
	struct drm_framebuffer	*fb;
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
int arcpgu_drm_sim_init(struct drm_device *drm, struct device_node *np);

#endif

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * R-Car LVDS Encoder
 *
 * Copyright (C) 2013-2018 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#ifndef __RCAR_LVDS_H__
#define __RCAR_LVDS_H__

struct drm_bridge;

#if IS_ENABLED(CONFIG_DRM_RCAR_LVDS)
int rcar_lvds_clk_enable(struct drm_bridge *bridge, unsigned long freq);
void rcar_lvds_clk_disable(struct drm_bridge *bridge);
bool rcar_lvds_dual_link(struct drm_bridge *bridge);
bool rcar_lvds_is_connected(struct drm_bridge *bridge);
#else
static inline int rcar_lvds_clk_enable(struct drm_bridge *bridge,
				       unsigned long freq)
{
	return -ENOSYS;
}
static inline void rcar_lvds_clk_disable(struct drm_bridge *bridge) { }
static inline bool rcar_lvds_dual_link(struct drm_bridge *bridge)
{
	return false;
}
static inline bool rcar_lvds_is_connected(struct drm_bridge *bridge)
{
	return false;
}
#endif /* CONFIG_DRM_RCAR_LVDS */

#endif /* __RCAR_LVDS_H__ */

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * R-Car Display Unit Writeback Support
 *
 * Copyright (C) 2019 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef __RCAR_DU_WRITEBACK_H__
#define __RCAR_DU_WRITEBACK_H__

#include <drm/drm_plane.h>

struct rcar_du_crtc;
struct rcar_du_device;
struct vsp1_du_atomic_pipe_config;

#ifdef CONFIG_DRM_RCAR_WRITEBACK
int rcar_du_writeback_init(struct rcar_du_device *rcdu,
			   struct rcar_du_crtc *rcrtc);
void rcar_du_writeback_setup(struct rcar_du_crtc *rcrtc,
			     struct vsp1_du_writeback_config *cfg);
void rcar_du_writeback_complete(struct rcar_du_crtc *rcrtc);
#else
static inline int rcar_du_writeback_init(struct rcar_du_device *rcdu,
					 struct rcar_du_crtc *rcrtc)
{
	return -ENXIO;
}
static inline void
rcar_du_writeback_setup(struct rcar_du_crtc *rcrtc,
			struct vsp1_du_writeback_config *cfg)
{
}
static inline void rcar_du_writeback_complete(struct rcar_du_crtc *rcrtc)
{
}
#endif

#endif /* __RCAR_DU_WRITEBACK_H__ */

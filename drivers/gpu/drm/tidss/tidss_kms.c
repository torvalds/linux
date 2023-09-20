// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_vblank.h>

#include "tidss_crtc.h"
#include "tidss_dispc.h"
#include "tidss_drv.h"
#include "tidss_encoder.h"
#include "tidss_kms.h"
#include "tidss_plane.h"

static void tidss_atomic_commit_tail(struct drm_atomic_state *old_state)
{
	struct drm_device *ddev = old_state->dev;
	struct tidss_device *tidss = to_tidss(ddev);

	dev_dbg(ddev->dev, "%s\n", __func__);

	tidss_runtime_get(tidss);

	drm_atomic_helper_commit_modeset_disables(ddev, old_state);
	drm_atomic_helper_commit_planes(ddev, old_state, 0);
	drm_atomic_helper_commit_modeset_enables(ddev, old_state);

	drm_atomic_helper_commit_hw_done(old_state);
	drm_atomic_helper_wait_for_flip_done(ddev, old_state);

	drm_atomic_helper_cleanup_planes(ddev, old_state);

	tidss_runtime_put(tidss);
}

static const struct drm_mode_config_helper_funcs mode_config_helper_funcs = {
	.atomic_commit_tail = tidss_atomic_commit_tail,
};

static int tidss_atomic_check(struct drm_device *ddev,
			      struct drm_atomic_state *state)
{
	struct drm_plane_state *opstate;
	struct drm_plane_state *npstate;
	struct drm_plane *plane;
	struct drm_crtc_state *cstate;
	struct drm_crtc *crtc;
	int ret, i;

	ret = drm_atomic_helper_check(ddev, state);
	if (ret)
		return ret;

	/*
	 * Add all active planes on a CRTC to the atomic state, if
	 * x/y/z position or activity of any plane on that CRTC
	 * changes. This is needed for updating the plane positions in
	 * tidss_crtc_position_planes() which is called from
	 * crtc_atomic_enable() and crtc_atomic_flush(). We have an
	 * extra flag to mark x,y-position changes and together
	 * with zpos_changed the condition recognizes all the above
	 * cases.
	 */
	for_each_oldnew_plane_in_state(state, plane, opstate, npstate, i) {
		if (!npstate->crtc || !npstate->visible)
			continue;

		if (!opstate->crtc || opstate->crtc_x != npstate->crtc_x ||
		    opstate->crtc_y != npstate->crtc_y) {
			cstate = drm_atomic_get_crtc_state(state,
							   npstate->crtc);
			if (IS_ERR(cstate))
				return PTR_ERR(cstate);
			to_tidss_crtc_state(cstate)->plane_pos_changed = true;
		}
	}

	for_each_new_crtc_in_state(state, crtc, cstate, i) {
		if (to_tidss_crtc_state(cstate)->plane_pos_changed ||
		    cstate->zpos_changed) {
			ret = drm_atomic_add_affected_planes(state, crtc);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static const struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = tidss_atomic_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int tidss_dispc_modeset_init(struct tidss_device *tidss)
{
	struct device *dev = tidss->dev;
	unsigned int fourccs_len;
	const u32 *fourccs = dispc_plane_formats(tidss->dispc, &fourccs_len);
	unsigned int i;

	struct pipe {
		u32 hw_videoport;
		struct drm_bridge *bridge;
		u32 enc_type;
	};

	const struct dispc_features *feat = tidss->feat;
	u32 max_vps = feat->num_vps;
	u32 max_planes = feat->num_planes;

	struct pipe pipes[TIDSS_MAX_PORTS];
	u32 num_pipes = 0;
	u32 crtc_mask;

	/* first find all the connected panels & bridges */

	for (i = 0; i < max_vps; i++) {
		struct drm_panel *panel;
		struct drm_bridge *bridge;
		u32 enc_type = DRM_MODE_ENCODER_NONE;
		int ret;

		ret = drm_of_find_panel_or_bridge(dev->of_node, i, 0,
						  &panel, &bridge);
		if (ret == -ENODEV) {
			dev_dbg(dev, "no panel/bridge for port %d\n", i);
			continue;
		} else if (ret) {
			dev_dbg(dev, "port %d probe returned %d\n", i, ret);
			return ret;
		}

		if (panel) {
			u32 conn_type;

			dev_dbg(dev, "Setting up panel for port %d\n", i);

			switch (feat->vp_bus_type[i]) {
			case DISPC_VP_OLDI:
				enc_type = DRM_MODE_ENCODER_LVDS;
				conn_type = DRM_MODE_CONNECTOR_LVDS;
				break;
			case DISPC_VP_DPI:
				enc_type = DRM_MODE_ENCODER_DPI;
				conn_type = DRM_MODE_CONNECTOR_DPI;
				break;
			default:
				WARN_ON(1);
				return -EINVAL;
			}

			if (panel->connector_type != conn_type) {
				dev_err(dev,
					"%s: Panel %s has incompatible connector type for vp%d (%d != %d)\n",
					 __func__, dev_name(panel->dev), i,
					 panel->connector_type, conn_type);
				return -EINVAL;
			}

			bridge = devm_drm_panel_bridge_add(dev, panel);
			if (IS_ERR(bridge)) {
				dev_err(dev,
					"failed to set up panel bridge for port %d\n",
					i);
				return PTR_ERR(bridge);
			}
		}

		pipes[num_pipes].hw_videoport = i;
		pipes[num_pipes].bridge = bridge;
		pipes[num_pipes].enc_type = enc_type;
		num_pipes++;
	}

	/* all planes can be on any crtc */
	crtc_mask = (1 << num_pipes) - 1;

	/* then create a plane, a crtc and an encoder for each panel/bridge */

	for (i = 0; i < num_pipes; ++i) {
		struct tidss_plane *tplane;
		struct tidss_crtc *tcrtc;
		u32 hw_plane_id = feat->vid_order[tidss->num_planes];
		int ret;

		tplane = tidss_plane_create(tidss, hw_plane_id,
					    DRM_PLANE_TYPE_PRIMARY, crtc_mask,
					    fourccs, fourccs_len);
		if (IS_ERR(tplane)) {
			dev_err(tidss->dev, "plane create failed\n");
			return PTR_ERR(tplane);
		}

		tidss->planes[tidss->num_planes++] = &tplane->plane;

		tcrtc = tidss_crtc_create(tidss, pipes[i].hw_videoport,
					  &tplane->plane);
		if (IS_ERR(tcrtc)) {
			dev_err(tidss->dev, "crtc create failed\n");
			return PTR_ERR(tcrtc);
		}

		tidss->crtcs[tidss->num_crtcs++] = &tcrtc->crtc;

		ret = tidss_encoder_create(tidss, pipes[i].bridge,
					   pipes[i].enc_type,
					   1 << tcrtc->crtc.index);
		if (ret) {
			dev_err(tidss->dev, "encoder create failed\n");
			return ret;
		}
	}

	/* create overlay planes of the leftover planes */

	while (tidss->num_planes < max_planes) {
		struct tidss_plane *tplane;
		u32 hw_plane_id = feat->vid_order[tidss->num_planes];

		tplane = tidss_plane_create(tidss, hw_plane_id,
					    DRM_PLANE_TYPE_OVERLAY, crtc_mask,
					    fourccs, fourccs_len);

		if (IS_ERR(tplane)) {
			dev_err(tidss->dev, "plane create failed\n");
			return PTR_ERR(tplane);
		}

		tidss->planes[tidss->num_planes++] = &tplane->plane;
	}

	return 0;
}

int tidss_modeset_init(struct tidss_device *tidss)
{
	struct drm_device *ddev = &tidss->ddev;
	int ret;

	dev_dbg(tidss->dev, "%s\n", __func__);

	ret = drmm_mode_config_init(ddev);
	if (ret)
		return ret;

	ddev->mode_config.min_width = 8;
	ddev->mode_config.min_height = 8;
	ddev->mode_config.max_width = 8096;
	ddev->mode_config.max_height = 8096;
	ddev->mode_config.normalize_zpos = true;
	ddev->mode_config.funcs = &mode_config_funcs;
	ddev->mode_config.helper_private = &mode_config_helper_funcs;

	ret = tidss_dispc_modeset_init(tidss);
	if (ret)
		return ret;

	ret = drm_vblank_init(ddev, tidss->num_crtcs);
	if (ret)
		return ret;

	drm_mode_config_reset(ddev);

	dev_dbg(tidss->dev, "%s done\n", __func__);

	return 0;
}

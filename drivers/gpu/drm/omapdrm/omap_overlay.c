// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Texas Instruments Incorporated -  http://www.ti.com/
 * Author: Benoit Parrot <bparrot@ti.com>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>

#include "omap_dmm_tiler.h"
#include "omap_drv.h"

/*
 * overlay funcs
 */
static const char * const overlay_id_to_name[] = {
	[OMAP_DSS_GFX] = "gfx",
	[OMAP_DSS_VIDEO1] = "vid1",
	[OMAP_DSS_VIDEO2] = "vid2",
	[OMAP_DSS_VIDEO3] = "vid3",
};

static void omap_overlay_destroy(struct omap_hw_overlay *overlay)
{
	kfree(overlay);
}

static struct omap_hw_overlay *omap_overlay_init(enum omap_plane_id overlay_id,
						 enum omap_overlay_caps caps)
{
	struct omap_hw_overlay *overlay;

	overlay = kzalloc(sizeof(*overlay), GFP_KERNEL);
	if (!overlay)
		return ERR_PTR(-ENOMEM);

	overlay->name = overlay_id_to_name[overlay_id];
	overlay->id = overlay_id;
	overlay->caps = caps;

	return overlay;
}

int omap_hwoverlays_init(struct omap_drm_private *priv)
{
	static const enum omap_plane_id hw_plane_ids[] = {
			OMAP_DSS_GFX, OMAP_DSS_VIDEO1,
			OMAP_DSS_VIDEO2, OMAP_DSS_VIDEO3,
	};
	u32 num_overlays = dispc_get_num_ovls(priv->dispc);
	enum omap_overlay_caps caps;
	int i, ret;

	for (i = 0; i < num_overlays; i++) {
		struct omap_hw_overlay *overlay;

		caps = dispc_ovl_get_caps(priv->dispc, hw_plane_ids[i]);
		overlay = omap_overlay_init(hw_plane_ids[i], caps);
		if (IS_ERR(overlay)) {
			ret = PTR_ERR(overlay);
			dev_err(priv->dev, "failed to construct overlay for %s (%d)\n",
				overlay_id_to_name[i], ret);
			omap_hwoverlays_destroy(priv);
			return ret;
		}
		overlay->idx = priv->num_ovls;
		priv->overlays[priv->num_ovls++] = overlay;
	}

	return 0;
}

void omap_hwoverlays_destroy(struct omap_drm_private *priv)
{
	int i;

	for (i = 0; i < priv->num_ovls; i++) {
		omap_overlay_destroy(priv->overlays[i]);
		priv->overlays[i] = NULL;
	}

	priv->num_ovls = 0;
}

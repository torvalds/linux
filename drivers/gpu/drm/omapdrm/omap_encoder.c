/*
 * drivers/gpu/drm/omapdrm/omap_encoder.c
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

#include "omap_drv.h"

#include "drm_crtc.h"
#include "drm_crtc_helper.h"

#include <linux/list.h>


/*
 * encoder funcs
 */

#define to_omap_encoder(x) container_of(x, struct omap_encoder, base)

/* The encoder and connector both map to same dssdev.. the encoder
 * handles the 'active' parts, ie. anything the modifies the state
 * of the hw, and the connector handles the 'read-only' parts, like
 * detecting connection and reading edid.
 */
struct omap_encoder {
	struct drm_encoder base;
	struct omap_dss_device *dssdev;
};

static void omap_encoder_destroy(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	drm_encoder_cleanup(encoder);
	kfree(omap_encoder);
}

static const struct drm_encoder_funcs omap_encoder_funcs = {
	.destroy = omap_encoder_destroy,
};

/*
 * The CRTC drm_crtc_helper_set_mode() doesn't really give us the right
 * order.. the easiest way to work around this for now is to make all
 * the encoder-helper's no-op's and have the omap_crtc code take care
 * of the sequencing and call us in the right points.
 *
 * Eventually to handle connecting CRTCs to different encoders properly,
 * either the CRTC helpers need to change or we need to replace
 * drm_crtc_helper_set_mode(), but lets wait until atomic-modeset for
 * that.
 */

static void omap_encoder_dpms(struct drm_encoder *encoder, int mode)
{
}

static bool omap_encoder_mode_fixup(struct drm_encoder *encoder,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void omap_encoder_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
}

static void omap_encoder_prepare(struct drm_encoder *encoder)
{
}

static void omap_encoder_commit(struct drm_encoder *encoder)
{
}

static const struct drm_encoder_helper_funcs omap_encoder_helper_funcs = {
	.dpms = omap_encoder_dpms,
	.mode_fixup = omap_encoder_mode_fixup,
	.mode_set = omap_encoder_mode_set,
	.prepare = omap_encoder_prepare,
	.commit = omap_encoder_commit,
};

/*
 * Instead of relying on the helpers for modeset, the omap_crtc code
 * calls these functions in the proper sequence.
 */

int omap_encoder_set_enabled(struct drm_encoder *encoder, bool enabled)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct omap_dss_device *dssdev = omap_encoder->dssdev;
	struct omap_dss_driver *dssdrv = dssdev->driver;

	if (enabled) {
		return dssdrv->enable(dssdev);
	} else {
		dssdrv->disable(dssdev);
		return 0;
	}
}

int omap_encoder_update(struct drm_encoder *encoder,
		struct omap_overlay_manager *mgr,
		struct omap_video_timings *timings)
{
	struct drm_device *dev = encoder->dev;
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct omap_dss_device *dssdev = omap_encoder->dssdev;
	struct omap_dss_driver *dssdrv = dssdev->driver;
	int ret;

	dssdev->output->manager = mgr;

	ret = dssdrv->check_timings(dssdev, timings);
	if (ret) {
		dev_err(dev->dev, "could not set timings: %d\n", ret);
		return ret;
	}

	dssdrv->set_timings(dssdev, timings);

	return 0;
}

/* initialize encoder */
struct drm_encoder *omap_encoder_init(struct drm_device *dev,
		struct omap_dss_device *dssdev)
{
	struct drm_encoder *encoder = NULL;
	struct omap_encoder *omap_encoder;

	omap_encoder = kzalloc(sizeof(*omap_encoder), GFP_KERNEL);
	if (!omap_encoder)
		goto fail;

	omap_encoder->dssdev = dssdev;

	encoder = &omap_encoder->base;

	drm_encoder_init(dev, encoder, &omap_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(encoder, &omap_encoder_helper_funcs);

	return encoder;

fail:
	if (encoder)
		omap_encoder_destroy(encoder);

	return NULL;
}

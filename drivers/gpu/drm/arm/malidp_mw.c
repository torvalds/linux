// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2016 ARM Limited. All rights reserved.
 * Author: Brian Starkey <brian.starkey@arm.com>
 *
 * ARM Mali DP Writeback connector implementation
 */
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drmP.h>
#include <drm/drm_writeback.h>

#include "malidp_drv.h"
#include "malidp_hw.h"
#include "malidp_mw.h"

#define to_mw_state(_state) (struct malidp_mw_connector_state *)(_state)

struct malidp_mw_connector_state {
	struct drm_connector_state base;
	dma_addr_t addrs[2];
	s32 pitches[2];
	u8 format;
	u8 n_planes;
	bool rgb2yuv_initialized;
	const s16 *rgb2yuv_coeffs;
};

static int malidp_mw_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	return drm_add_modes_noedid(connector, dev->mode_config.max_width,
				    dev->mode_config.max_height);
}

static enum drm_mode_status
malidp_mw_connector_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	int w = mode->hdisplay, h = mode->vdisplay;

	if ((w < mode_config->min_width) || (w > mode_config->max_width))
		return MODE_BAD_HVALUE;

	if ((h < mode_config->min_height) || (h > mode_config->max_height))
		return MODE_BAD_VVALUE;

	return MODE_OK;
}

static const struct drm_connector_helper_funcs malidp_mw_connector_helper_funcs = {
	.get_modes = malidp_mw_connector_get_modes,
	.mode_valid = malidp_mw_connector_mode_valid,
};

static void malidp_mw_connector_reset(struct drm_connector *connector)
{
	struct malidp_mw_connector_state *mw_state =
		kzalloc(sizeof(*mw_state), GFP_KERNEL);

	if (connector->state)
		__drm_atomic_helper_connector_destroy_state(connector->state);

	kfree(connector->state);
	__drm_atomic_helper_connector_reset(connector, &mw_state->base);
}

static enum drm_connector_status
malidp_mw_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void malidp_mw_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static struct drm_connector_state *
malidp_mw_connector_duplicate_state(struct drm_connector *connector)
{
	struct malidp_mw_connector_state *mw_state, *mw_current_state;

	if (WARN_ON(!connector->state))
		return NULL;

	mw_state = kzalloc(sizeof(*mw_state), GFP_KERNEL);
	if (!mw_state)
		return NULL;

	mw_current_state = to_mw_state(connector->state);
	mw_state->rgb2yuv_coeffs = mw_current_state->rgb2yuv_coeffs;
	mw_state->rgb2yuv_initialized = mw_current_state->rgb2yuv_initialized;

	__drm_atomic_helper_connector_duplicate_state(connector, &mw_state->base);

	return &mw_state->base;
}

static const struct drm_connector_funcs malidp_mw_connector_funcs = {
	.reset = malidp_mw_connector_reset,
	.detect = malidp_mw_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = malidp_mw_connector_destroy,
	.atomic_duplicate_state = malidp_mw_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const s16 rgb2yuv_coeffs_bt709_limited[MALIDP_COLORADJ_NUM_COEFFS] = {
	47,  157,   16,
	-26,  -87,  112,
	112, -102,  -10,
	16,  128,  128
};

static int
malidp_mw_encoder_atomic_check(struct drm_encoder *encoder,
			       struct drm_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state)
{
	struct malidp_mw_connector_state *mw_state = to_mw_state(conn_state);
	struct malidp_drm *malidp = encoder->dev->dev_private;
	struct drm_framebuffer *fb;
	int i, n_planes;

	if (!conn_state->writeback_job || !conn_state->writeback_job->fb)
		return 0;

	fb = conn_state->writeback_job->fb;
	if ((fb->width != crtc_state->mode.hdisplay) ||
	    (fb->height != crtc_state->mode.vdisplay)) {
		DRM_DEBUG_KMS("Invalid framebuffer size %ux%u\n",
				fb->width, fb->height);
		return -EINVAL;
	}

	mw_state->format =
		malidp_hw_get_format_id(&malidp->dev->hw->map, SE_MEMWRITE,
					fb->format->format);
	if (mw_state->format == MALIDP_INVALID_FORMAT_ID) {
		struct drm_format_name_buf format_name;

		DRM_DEBUG_KMS("Invalid pixel format %s\n",
			      drm_get_format_name(fb->format->format,
						  &format_name));
		return -EINVAL;
	}

	n_planes = drm_format_num_planes(fb->format->format);
	for (i = 0; i < n_planes; i++) {
		struct drm_gem_cma_object *obj = drm_fb_cma_get_gem_obj(fb, i);
		/* memory write buffers are never rotated */
		u8 alignment = malidp_hw_get_pitch_align(malidp->dev, 0);

		if (fb->pitches[i] & (alignment - 1)) {
			DRM_DEBUG_KMS("Invalid pitch %u for plane %d\n",
				      fb->pitches[i], i);
			return -EINVAL;
		}
		mw_state->pitches[i] = fb->pitches[i];
		mw_state->addrs[i] = obj->paddr + fb->offsets[i];
	}
	mw_state->n_planes = n_planes;

	if (fb->format->is_yuv)
		mw_state->rgb2yuv_coeffs = rgb2yuv_coeffs_bt709_limited;

	return 0;
}

static const struct drm_encoder_helper_funcs malidp_mw_encoder_helper_funcs = {
	.atomic_check = malidp_mw_encoder_atomic_check,
};

static u32 *get_writeback_formats(struct malidp_drm *malidp, int *n_formats)
{
	const struct malidp_hw_regmap *map = &malidp->dev->hw->map;
	u32 *formats;
	int n, i;

	formats = kcalloc(map->n_pixel_formats, sizeof(*formats),
			  GFP_KERNEL);
	if (!formats)
		return NULL;

	for (n = 0, i = 0;  i < map->n_pixel_formats; i++) {
		if (map->pixel_formats[i].layer & SE_MEMWRITE)
			formats[n++] = map->pixel_formats[i].format;
	}

	*n_formats = n;

	return formats;
}

int malidp_mw_connector_init(struct drm_device *drm)
{
	struct malidp_drm *malidp = drm->dev_private;
	u32 *formats;
	int ret, n_formats;

	if (!malidp->dev->hw->enable_memwrite)
		return 0;

	malidp->mw_connector.encoder.possible_crtcs = 1 << drm_crtc_index(&malidp->crtc);
	drm_connector_helper_add(&malidp->mw_connector.base,
				 &malidp_mw_connector_helper_funcs);

	formats = get_writeback_formats(malidp, &n_formats);
	if (!formats)
		return -ENOMEM;

	ret = drm_writeback_connector_init(drm, &malidp->mw_connector,
					   &malidp_mw_connector_funcs,
					   &malidp_mw_encoder_helper_funcs,
					   formats, n_formats);
	kfree(formats);
	if (ret)
		return ret;

	return 0;
}

void malidp_mw_atomic_commit(struct drm_device *drm,
			     struct drm_atomic_state *old_state)
{
	struct malidp_drm *malidp = drm->dev_private;
	struct drm_writeback_connector *mw_conn = &malidp->mw_connector;
	struct drm_connector_state *conn_state = mw_conn->base.state;
	struct malidp_hw_device *hwdev = malidp->dev;
	struct malidp_mw_connector_state *mw_state;

	if (!conn_state)
		return;

	mw_state = to_mw_state(conn_state);

	if (conn_state->writeback_job && conn_state->writeback_job->fb) {
		struct drm_framebuffer *fb = conn_state->writeback_job->fb;

		DRM_DEV_DEBUG_DRIVER(drm->dev,
				     "Enable memwrite %ux%u:%d %pad fmt: %u\n",
				     fb->width, fb->height,
				     mw_state->pitches[0],
				     &mw_state->addrs[0],
				     mw_state->format);

		drm_writeback_queue_job(mw_conn, conn_state->writeback_job);
		conn_state->writeback_job = NULL;
		hwdev->hw->enable_memwrite(hwdev, mw_state->addrs,
					   mw_state->pitches, mw_state->n_planes,
					   fb->width, fb->height, mw_state->format,
					   !mw_state->rgb2yuv_initialized ?
					   mw_state->rgb2yuv_coeffs : NULL);
		mw_state->rgb2yuv_initialized = !!mw_state->rgb2yuv_coeffs;
	} else {
		DRM_DEV_DEBUG_DRIVER(drm->dev, "Disable memwrite\n");
		hwdev->hw->disable_memwrite(hwdev);
	}
}

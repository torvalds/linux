// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * Freescale DCU drm device driver
 */

#include <linux/regmap.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>

#include "fsl_dcu_drm_drv.h"
#include "fsl_dcu_drm_plane.h"

static int fsl_dcu_drm_plane_index(struct drm_plane *plane)
{
	struct fsl_dcu_drm_device *fsl_dev = plane->dev->dev_private;
	unsigned int total_layer = fsl_dev->soc->total_layer;
	unsigned int index;

	index = drm_plane_index(plane);
	if (index < total_layer)
		return total_layer - index - 1;

	dev_err(fsl_dev->dev, "No more layer left\n");
	return -EINVAL;
}

static int fsl_dcu_drm_plane_atomic_check(struct drm_plane *plane,
					  struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;

	if (!state->fb || !state->crtc)
		return 0;

	switch (fb->format->format) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB4444:
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_YUV422:
		return 0;
	default:
		return -EINVAL;
	}
}

static void fsl_dcu_drm_plane_atomic_disable(struct drm_plane *plane,
					     struct drm_plane_state *old_state)
{
	struct fsl_dcu_drm_device *fsl_dev = plane->dev->dev_private;
	unsigned int value;
	int index;

	index = fsl_dcu_drm_plane_index(plane);
	if (index < 0)
		return;

	regmap_read(fsl_dev->regmap, DCU_CTRLDESCLN(index, 4), &value);
	value &= ~DCU_LAYER_EN;
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN(index, 4), value);
}

static void fsl_dcu_drm_plane_atomic_update(struct drm_plane *plane,
					    struct drm_plane_state *old_state)

{
	struct fsl_dcu_drm_device *fsl_dev = plane->dev->dev_private;
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = plane->state->fb;
	struct drm_gem_cma_object *gem;
	unsigned int alpha = DCU_LAYER_AB_NONE, bpp;
	int index;

	if (!fb)
		return;

	index = fsl_dcu_drm_plane_index(plane);
	if (index < 0)
		return;

	gem = drm_fb_cma_get_gem_obj(fb, 0);

	switch (fb->format->format) {
	case DRM_FORMAT_RGB565:
		bpp = FSL_DCU_RGB565;
		break;
	case DRM_FORMAT_RGB888:
		bpp = FSL_DCU_RGB888;
		break;
	case DRM_FORMAT_ARGB8888:
		alpha = DCU_LAYER_AB_WHOLE_FRAME;
		/* fall-through */
	case DRM_FORMAT_XRGB8888:
		bpp = FSL_DCU_ARGB8888;
		break;
	case DRM_FORMAT_ARGB4444:
		alpha = DCU_LAYER_AB_WHOLE_FRAME;
		/* fall-through */
	case DRM_FORMAT_XRGB4444:
		bpp = FSL_DCU_ARGB4444;
		break;
	case DRM_FORMAT_ARGB1555:
		alpha = DCU_LAYER_AB_WHOLE_FRAME;
		/* fall-through */
	case DRM_FORMAT_XRGB1555:
		bpp = FSL_DCU_ARGB1555;
		break;
	case DRM_FORMAT_YUV422:
		bpp = FSL_DCU_YUV422;
		break;
	default:
		return;
	}

	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN(index, 1),
		     DCU_LAYER_HEIGHT(state->crtc_h) |
		     DCU_LAYER_WIDTH(state->crtc_w));
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN(index, 2),
		     DCU_LAYER_POSY(state->crtc_y) |
		     DCU_LAYER_POSX(state->crtc_x));
	regmap_write(fsl_dev->regmap,
		     DCU_CTRLDESCLN(index, 3), gem->paddr);
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN(index, 4),
		     DCU_LAYER_EN |
		     DCU_LAYER_TRANS(0xff) |
		     DCU_LAYER_BPP(bpp) |
		     alpha);
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN(index, 5),
		     DCU_LAYER_CKMAX_R(0xFF) |
		     DCU_LAYER_CKMAX_G(0xFF) |
		     DCU_LAYER_CKMAX_B(0xFF));
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN(index, 6),
		     DCU_LAYER_CKMIN_R(0) |
		     DCU_LAYER_CKMIN_G(0) |
		     DCU_LAYER_CKMIN_B(0));
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN(index, 7), 0);
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN(index, 8),
		     DCU_LAYER_FG_FCOLOR(0));
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN(index, 9),
		     DCU_LAYER_BG_BCOLOR(0));

	if (!strcmp(fsl_dev->soc->name, "ls1021a")) {
		regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN(index, 10),
			     DCU_LAYER_POST_SKIP(0) |
			     DCU_LAYER_PRE_SKIP(0));
	}

	return;
}

static const struct drm_plane_helper_funcs fsl_dcu_drm_plane_helper_funcs = {
	.atomic_check = fsl_dcu_drm_plane_atomic_check,
	.atomic_disable = fsl_dcu_drm_plane_atomic_disable,
	.atomic_update = fsl_dcu_drm_plane_atomic_update,
};

static void fsl_dcu_drm_plane_destroy(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);
	kfree(plane);
}

static const struct drm_plane_funcs fsl_dcu_drm_plane_funcs = {
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.destroy = fsl_dcu_drm_plane_destroy,
	.disable_plane = drm_atomic_helper_disable_plane,
	.reset = drm_atomic_helper_plane_reset,
	.update_plane = drm_atomic_helper_update_plane,
};

static const u32 fsl_dcu_drm_plane_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_YUV422,
};

void fsl_dcu_drm_init_planes(struct drm_device *dev)
{
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;
	int i, j;

	for (i = 0; i < fsl_dev->soc->total_layer; i++) {
		for (j = 1; j <= fsl_dev->soc->layer_regs; j++)
			regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN(i, j), 0);
	}
}

struct drm_plane *fsl_dcu_drm_primary_create_plane(struct drm_device *dev)
{
	struct drm_plane *primary;
	int ret;

	primary = kzalloc(sizeof(*primary), GFP_KERNEL);
	if (!primary) {
		DRM_DEBUG_KMS("Failed to allocate primary plane\n");
		return NULL;
	}

	/* possible_crtc's will be filled in later by crtc_init */
	ret = drm_universal_plane_init(dev, primary, 0,
				       &fsl_dcu_drm_plane_funcs,
				       fsl_dcu_drm_plane_formats,
				       ARRAY_SIZE(fsl_dcu_drm_plane_formats),
				       NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		kfree(primary);
		primary = NULL;
	}
	drm_plane_helper_add(primary, &fsl_dcu_drm_plane_helper_funcs);

	return primary;
}

// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>
#include "komeda_dev.h"
#include "komeda_kms.h"
#include "komeda_framebuffer.h"

static int
komeda_plane_init_data_flow(struct drm_plane_state *st,
			    struct komeda_crtc_state *kcrtc_st,
			    struct komeda_data_flow_cfg *dflow)
{
	struct komeda_plane *kplane = to_kplane(st->plane);
	struct drm_framebuffer *fb = st->fb;
	const struct komeda_format_caps *caps = to_kfb(fb)->format_caps;
	struct komeda_pipeline *pipe = kplane->layer->base.pipeline;

	memset(dflow, 0, sizeof(*dflow));

	dflow->blending_zorder = st->normalized_zpos;
	if (pipe == to_kcrtc(st->crtc)->master)
		dflow->blending_zorder -= kcrtc_st->max_slave_zorder;
	if (dflow->blending_zorder < 0) {
		DRM_DEBUG_ATOMIC("%s zorder:%d < max_slave_zorder: %d.\n",
				 st->plane->name, st->normalized_zpos,
				 kcrtc_st->max_slave_zorder);
		return -EINVAL;
	}

	dflow->pixel_blend_mode = st->pixel_blend_mode;
	dflow->layer_alpha = st->alpha >> 8;

	dflow->out_x = st->crtc_x;
	dflow->out_y = st->crtc_y;
	dflow->out_w = st->crtc_w;
	dflow->out_h = st->crtc_h;

	dflow->in_x = st->src_x >> 16;
	dflow->in_y = st->src_y >> 16;
	dflow->in_w = st->src_w >> 16;
	dflow->in_h = st->src_h >> 16;

	dflow->rot = drm_rotation_simplify(st->rotation, caps->supported_rots);
	if (!has_bits(dflow->rot, caps->supported_rots)) {
		DRM_DEBUG_ATOMIC("rotation(0x%x) isn't supported by %p4cc with modifier: 0x%llx.\n",
				 dflow->rot, &caps->fourcc, fb->modifier);
		return -EINVAL;
	}

	komeda_complete_data_flow_cfg(kplane->layer, dflow, fb);

	return 0;
}

/**
 * komeda_plane_atomic_check - build input data flow
 * @plane: DRM plane
 * @state: the plane state object
 *
 * RETURNS:
 * Zero for success or -errno
 */
static int
komeda_plane_atomic_check(struct drm_plane *plane,
			  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct komeda_plane *kplane = to_kplane(plane);
	struct komeda_plane_state *kplane_st = to_kplane_st(new_plane_state);
	struct komeda_layer *layer = kplane->layer;
	struct drm_crtc_state *crtc_st;
	struct komeda_crtc_state *kcrtc_st;
	struct komeda_data_flow_cfg dflow;
	int err;

	if (!new_plane_state->crtc || !new_plane_state->fb)
		return 0;

	crtc_st = drm_atomic_get_crtc_state(state,
					    new_plane_state->crtc);
	if (IS_ERR(crtc_st) || !crtc_st->enable) {
		DRM_DEBUG_ATOMIC("Cannot update plane on a disabled CRTC.\n");
		return -EINVAL;
	}

	/* crtc is inactive, skip the resource assignment */
	if (!crtc_st->active)
		return 0;

	kcrtc_st = to_kcrtc_st(crtc_st);

	err = komeda_plane_init_data_flow(new_plane_state, kcrtc_st, &dflow);
	if (err)
		return err;

	if (dflow.en_split)
		err = komeda_build_layer_split_data_flow(layer,
				kplane_st, kcrtc_st, &dflow);
	else
		err = komeda_build_layer_data_flow(layer,
				kplane_st, kcrtc_st, &dflow);

	return err;
}

/* plane doesn't represent a real HW, so there is no HW update for plane.
 * komeda handles all the HW update in crtc->atomic_flush
 */
static void
komeda_plane_atomic_update(struct drm_plane *plane,
			   struct drm_atomic_state *state)
{
}

static const struct drm_plane_helper_funcs komeda_plane_helper_funcs = {
	.atomic_check	= komeda_plane_atomic_check,
	.atomic_update	= komeda_plane_atomic_update,
};

static void komeda_plane_destroy(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);

	kfree(to_kplane(plane));
}

static void komeda_plane_reset(struct drm_plane *plane)
{
	struct komeda_plane_state *state;

	if (plane->state)
		__drm_atomic_helper_plane_destroy_state(plane->state);

	kfree(plane->state);
	plane->state = NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state)
		__drm_atomic_helper_plane_reset(plane, &state->base);
}

static struct drm_plane_state *
komeda_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct komeda_plane_state *new;

	if (WARN_ON(!plane->state))
		return NULL;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &new->base);

	return &new->base;
}

static void
komeda_plane_atomic_destroy_state(struct drm_plane *plane,
				  struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(to_kplane_st(state));
}

static bool
komeda_plane_format_mod_supported(struct drm_plane *plane,
				  u32 format, u64 modifier)
{
	struct komeda_dev *mdev = plane->dev->dev_private;
	struct komeda_plane *kplane = to_kplane(plane);
	u32 layer_type = kplane->layer->layer_type;

	return komeda_format_mod_supported(&mdev->fmt_tbl, layer_type,
					   format, modifier, 0);
}

static const struct drm_plane_funcs komeda_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= komeda_plane_destroy,
	.reset			= komeda_plane_reset,
	.atomic_duplicate_state	= komeda_plane_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_plane_atomic_destroy_state,
	.format_mod_supported	= komeda_plane_format_mod_supported,
};

/* for komeda, which is pipeline can be share between crtcs */
static u32 get_possible_crtcs(struct komeda_kms_dev *kms,
			      struct komeda_pipeline *pipe)
{
	struct komeda_crtc *crtc;
	u32 possible_crtcs = 0;
	int i;

	for (i = 0; i < kms->n_crtcs; i++) {
		crtc = &kms->crtcs[i];

		if ((pipe == crtc->master) || (pipe == crtc->slave))
			possible_crtcs |= BIT(i);
	}

	return possible_crtcs;
}

static void
komeda_set_crtc_plane_mask(struct komeda_kms_dev *kms,
			   struct komeda_pipeline *pipe,
			   struct drm_plane *plane)
{
	struct komeda_crtc *kcrtc;
	int i;

	for (i = 0; i < kms->n_crtcs; i++) {
		kcrtc = &kms->crtcs[i];

		if (pipe == kcrtc->slave)
			kcrtc->slave_planes |= BIT(drm_plane_index(plane));
	}
}

/* use Layer0 as primary */
static u32 get_plane_type(struct komeda_kms_dev *kms,
			  struct komeda_component *c)
{
	bool is_primary = (c->id == KOMEDA_COMPONENT_LAYER0);

	return is_primary ? DRM_PLANE_TYPE_PRIMARY : DRM_PLANE_TYPE_OVERLAY;
}

static int komeda_plane_add(struct komeda_kms_dev *kms,
			    struct komeda_layer *layer)
{
	struct komeda_dev *mdev = kms->base.dev_private;
	struct komeda_component *c = &layer->base;
	struct komeda_plane *kplane;
	struct drm_plane *plane;
	u32 *formats, n_formats = 0;
	int err;

	kplane = kzalloc(sizeof(*kplane), GFP_KERNEL);
	if (!kplane)
		return -ENOMEM;

	plane = &kplane->base;
	kplane->layer = layer;

	formats = komeda_get_layer_fourcc_list(&mdev->fmt_tbl,
					       layer->layer_type, &n_formats);
	if (!formats) {
		kfree(kplane);
		return -ENOMEM;
	}

	err = drm_universal_plane_init(&kms->base, plane,
			get_possible_crtcs(kms, c->pipeline),
			&komeda_plane_funcs,
			formats, n_formats, komeda_supported_modifiers,
			get_plane_type(kms, c),
			"%s", c->name);

	komeda_put_fourcc_list(formats);

	if (err) {
		kfree(kplane);
		return err;
	}

	drm_plane_helper_add(plane, &komeda_plane_helper_funcs);

	err = drm_plane_create_rotation_property(plane, DRM_MODE_ROTATE_0,
						 layer->supported_rots);
	if (err)
		goto cleanup;

	err = drm_plane_create_alpha_property(plane);
	if (err)
		goto cleanup;

	err = drm_plane_create_blend_mode_property(plane,
			BIT(DRM_MODE_BLEND_PIXEL_NONE) |
			BIT(DRM_MODE_BLEND_PREMULTI)   |
			BIT(DRM_MODE_BLEND_COVERAGE));
	if (err)
		goto cleanup;

	err = drm_plane_create_color_properties(plane,
			BIT(DRM_COLOR_YCBCR_BT601) |
			BIT(DRM_COLOR_YCBCR_BT709) |
			BIT(DRM_COLOR_YCBCR_BT2020),
			BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) |
			BIT(DRM_COLOR_YCBCR_FULL_RANGE),
			DRM_COLOR_YCBCR_BT601,
			DRM_COLOR_YCBCR_LIMITED_RANGE);
	if (err)
		goto cleanup;

	err = drm_plane_create_zpos_property(plane, layer->base.id, 0, 8);
	if (err)
		goto cleanup;

	komeda_set_crtc_plane_mask(kms, c->pipeline, plane);

	return 0;
cleanup:
	komeda_plane_destroy(plane);
	return err;
}

int komeda_kms_add_planes(struct komeda_kms_dev *kms, struct komeda_dev *mdev)
{
	struct komeda_pipeline *pipe;
	int i, j, err;

	for (i = 0; i < mdev->n_pipelines; i++) {
		pipe = mdev->pipelines[i];

		for (j = 0; j < pipe->n_layers; j++) {
			err = komeda_plane_add(kms, pipe->layers[j]);
			if (err)
				return err;
		}
	}

	return 0;
}

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "dcss-dev.h"
#include "dcss-kms.h"

static const u32 dcss_common_formats[] = {
	/* RGB */
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_RGBX1010102,
	DRM_FORMAT_BGRX1010102,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_RGBA1010102,
	DRM_FORMAT_BGRA1010102,
};

static const u64 dcss_video_format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static const u64 dcss_graphics_format_modifiers[] = {
	DRM_FORMAT_MOD_VIVANTE_TILED,
	DRM_FORMAT_MOD_VIVANTE_SUPER_TILED,
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static inline struct dcss_plane *to_dcss_plane(struct drm_plane *p)
{
	return container_of(p, struct dcss_plane, base);
}

static inline bool dcss_plane_fb_is_linear(const struct drm_framebuffer *fb)
{
	return ((fb->flags & DRM_MODE_FB_MODIFIERS) == 0) ||
	       ((fb->flags & DRM_MODE_FB_MODIFIERS) != 0 &&
		fb->modifier == DRM_FORMAT_MOD_LINEAR);
}

static void dcss_plane_destroy(struct drm_plane *plane)
{
	struct dcss_plane *dcss_plane = container_of(plane, struct dcss_plane,
						     base);

	drm_plane_cleanup(plane);
	kfree(dcss_plane);
}

static bool dcss_plane_format_mod_supported(struct drm_plane *plane,
					    u32 format,
					    u64 modifier)
{
	switch (plane->type) {
	case DRM_PLANE_TYPE_PRIMARY:
		switch (format) {
		case DRM_FORMAT_ARGB8888:
		case DRM_FORMAT_XRGB8888:
		case DRM_FORMAT_ARGB2101010:
			return modifier == DRM_FORMAT_MOD_LINEAR ||
			       modifier == DRM_FORMAT_MOD_VIVANTE_TILED ||
			       modifier == DRM_FORMAT_MOD_VIVANTE_SUPER_TILED;
		default:
			return modifier == DRM_FORMAT_MOD_LINEAR;
		}
		break;
	case DRM_PLANE_TYPE_OVERLAY:
		return modifier == DRM_FORMAT_MOD_LINEAR;
	default:
		return false;
	}
}

static const struct drm_plane_funcs dcss_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= dcss_plane_destroy,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
	.format_mod_supported	= dcss_plane_format_mod_supported,
};

static bool dcss_plane_can_rotate(const struct drm_format_info *format,
				  bool mod_present, u64 modifier,
				  unsigned int rotation)
{
	bool linear_format = !mod_present ||
			     (mod_present && modifier == DRM_FORMAT_MOD_LINEAR);
	u32 supported_rotation = DRM_MODE_ROTATE_0;

	if (!format->is_yuv && linear_format)
		supported_rotation = DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_180 |
				     DRM_MODE_REFLECT_MASK;
	else if (!format->is_yuv &&
		 (modifier == DRM_FORMAT_MOD_VIVANTE_TILED ||
		  modifier == DRM_FORMAT_MOD_VIVANTE_SUPER_TILED))
		supported_rotation = DRM_MODE_ROTATE_MASK |
				     DRM_MODE_REFLECT_MASK;
	else if (format->is_yuv && linear_format &&
		 (format->format == DRM_FORMAT_NV12 ||
		  format->format == DRM_FORMAT_NV21))
		supported_rotation = DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_180 |
				     DRM_MODE_REFLECT_MASK;

	return !!(rotation & supported_rotation);
}

static bool dcss_plane_is_source_size_allowed(u16 src_w, u16 src_h, u32 pix_fmt)
{
	if (src_w < 64 &&
	    (pix_fmt == DRM_FORMAT_NV12 || pix_fmt == DRM_FORMAT_NV21))
		return false;
	else if (src_w < 32 &&
		 (pix_fmt == DRM_FORMAT_UYVY || pix_fmt == DRM_FORMAT_VYUY ||
		  pix_fmt == DRM_FORMAT_YUYV || pix_fmt == DRM_FORMAT_YVYU))
		return false;

	return src_w >= 16 && src_h >= 8;
}

static int dcss_plane_atomic_check(struct drm_plane *plane,
				   struct drm_plane_state *state)
{
	struct dcss_plane *dcss_plane = to_dcss_plane(plane);
	struct dcss_dev *dcss = plane->dev->dev_private;
	struct drm_framebuffer *fb = state->fb;
	bool is_primary_plane = plane->type == DRM_PLANE_TYPE_PRIMARY;
	struct drm_gem_cma_object *cma_obj;
	struct drm_crtc_state *crtc_state;
	int hdisplay, vdisplay;
	int min, max;
	int ret;

	if (!fb || !state->crtc)
		return 0;

	cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	WARN_ON(!cma_obj);

	crtc_state = drm_atomic_get_existing_crtc_state(state->state,
							state->crtc);

	hdisplay = crtc_state->adjusted_mode.hdisplay;
	vdisplay = crtc_state->adjusted_mode.vdisplay;

	if (!dcss_plane_is_source_size_allowed(state->src_w >> 16,
					       state->src_h >> 16,
					       fb->format->format)) {
		DRM_DEBUG_KMS("Source plane size is not allowed!\n");
		return -EINVAL;
	}

	dcss_scaler_get_min_max_ratios(dcss->scaler, dcss_plane->ch_num,
				       &min, &max);

	ret = drm_atomic_helper_check_plane_state(state, crtc_state,
						  min, max, !is_primary_plane,
						  false);
	if (ret)
		return ret;

	if (!state->visible)
		return 0;

	if (!dcss_plane_can_rotate(fb->format,
				   !!(fb->flags & DRM_MODE_FB_MODIFIERS),
				   fb->modifier,
				   state->rotation)) {
		DRM_DEBUG_KMS("requested rotation is not allowed!\n");
		return -EINVAL;
	}

	if ((state->crtc_x < 0 || state->crtc_y < 0 ||
	     state->crtc_x + state->crtc_w > hdisplay ||
	     state->crtc_y + state->crtc_h > vdisplay) &&
	    !dcss_plane_fb_is_linear(fb)) {
		DRM_DEBUG_KMS("requested cropping operation is not allowed!\n");
		return -EINVAL;
	}

	if ((fb->flags & DRM_MODE_FB_MODIFIERS) &&
	    !plane->funcs->format_mod_supported(plane,
				fb->format->format,
				fb->modifier)) {
		DRM_DEBUG_KMS("Invalid modifier: %llx", fb->modifier);
		return -EINVAL;
	}

	return 0;
}

static void dcss_plane_atomic_set_base(struct dcss_plane *dcss_plane)
{
	struct drm_plane *plane = &dcss_plane->base;
	struct drm_plane_state *state = plane->state;
	struct dcss_dev *dcss = plane->dev->dev_private;
	struct drm_framebuffer *fb = state->fb;
	const struct drm_format_info *format = fb->format;
	struct drm_gem_cma_object *cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	unsigned long p1_ba = 0, p2_ba = 0;

	if (!format->is_yuv ||
	    format->format == DRM_FORMAT_NV12 ||
	    format->format == DRM_FORMAT_NV21)
		p1_ba = cma_obj->paddr + fb->offsets[0] +
			fb->pitches[0] * (state->src.y1 >> 16) +
			format->char_per_block[0] * (state->src.x1 >> 16);
	else if (format->format == DRM_FORMAT_UYVY ||
		 format->format == DRM_FORMAT_VYUY ||
		 format->format == DRM_FORMAT_YUYV ||
		 format->format == DRM_FORMAT_YVYU)
		p1_ba = cma_obj->paddr + fb->offsets[0] +
			fb->pitches[0] * (state->src.y1 >> 16) +
			2 * format->char_per_block[0] * (state->src.x1 >> 17);

	if (format->format == DRM_FORMAT_NV12 ||
	    format->format == DRM_FORMAT_NV21)
		p2_ba = cma_obj->paddr + fb->offsets[1] +
			(((fb->pitches[1] >> 1) * (state->src.y1 >> 17) +
			(state->src.x1 >> 17)) << 1);

	dcss_dpr_addr_set(dcss->dpr, dcss_plane->ch_num, p1_ba, p2_ba,
			  fb->pitches[0]);
}

static bool dcss_plane_needs_setup(struct drm_plane_state *state,
				   struct drm_plane_state *old_state)
{
	struct drm_framebuffer *fb = state->fb;
	struct drm_framebuffer *old_fb = old_state->fb;

	return state->crtc_x != old_state->crtc_x ||
	       state->crtc_y != old_state->crtc_y ||
	       state->crtc_w != old_state->crtc_w ||
	       state->crtc_h != old_state->crtc_h ||
	       state->src_x  != old_state->src_x  ||
	       state->src_y  != old_state->src_y  ||
	       state->src_w  != old_state->src_w  ||
	       state->src_h  != old_state->src_h  ||
	       fb->format->format != old_fb->format->format ||
	       fb->modifier  != old_fb->modifier ||
	       state->rotation != old_state->rotation;
}

static void dcss_plane_atomic_update(struct drm_plane *plane,
				     struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = plane->state;
	struct dcss_plane *dcss_plane = to_dcss_plane(plane);
	struct dcss_dev *dcss = plane->dev->dev_private;
	struct drm_framebuffer *fb = state->fb;
	u32 pixel_format;
	struct drm_crtc_state *crtc_state;
	bool modifiers_present;
	u32 src_w, src_h, dst_w, dst_h;
	struct drm_rect src, dst;
	bool enable = true;
	bool is_rotation_90_or_270;

	if (!fb || !state->crtc || !state->visible)
		return;

	pixel_format = state->fb->format->format;
	crtc_state = state->crtc->state;
	modifiers_present = !!(fb->flags & DRM_MODE_FB_MODIFIERS);

	if (old_state->fb && !drm_atomic_crtc_needs_modeset(crtc_state) &&
	    !dcss_plane_needs_setup(state, old_state)) {
		dcss_plane_atomic_set_base(dcss_plane);
		return;
	}

	src = plane->state->src;
	dst = plane->state->dst;

	/*
	 * The width and height after clipping.
	 */
	src_w = drm_rect_width(&src) >> 16;
	src_h = drm_rect_height(&src) >> 16;
	dst_w = drm_rect_width(&dst);
	dst_h = drm_rect_height(&dst);

	if (plane->type == DRM_PLANE_TYPE_OVERLAY &&
	    modifiers_present && fb->modifier == DRM_FORMAT_MOD_LINEAR)
		modifiers_present = false;

	dcss_dpr_format_set(dcss->dpr, dcss_plane->ch_num, state->fb->format,
			    modifiers_present ? fb->modifier :
						DRM_FORMAT_MOD_LINEAR);
	dcss_dpr_set_res(dcss->dpr, dcss_plane->ch_num, src_w, src_h);
	dcss_dpr_set_rotation(dcss->dpr, dcss_plane->ch_num,
			      state->rotation);

	dcss_plane_atomic_set_base(dcss_plane);

	is_rotation_90_or_270 = state->rotation & (DRM_MODE_ROTATE_90 |
						   DRM_MODE_ROTATE_270);

	dcss_scaler_setup(dcss->scaler, dcss_plane->ch_num,
			  state->fb->format,
			  is_rotation_90_or_270 ? src_h : src_w,
			  is_rotation_90_or_270 ? src_w : src_h,
			  dst_w, dst_h,
			  drm_mode_vrefresh(&crtc_state->mode));

	dcss_dtg_plane_pos_set(dcss->dtg, dcss_plane->ch_num,
			       dst.x1, dst.y1, dst_w, dst_h);
	dcss_dtg_plane_alpha_set(dcss->dtg, dcss_plane->ch_num,
				 fb->format, state->alpha >> 8);

	if (!dcss_plane->ch_num && (state->alpha >> 8) == 0)
		enable = false;

	dcss_dpr_enable(dcss->dpr, dcss_plane->ch_num, enable);
	dcss_scaler_ch_enable(dcss->scaler, dcss_plane->ch_num, enable);

	if (!enable)
		dcss_dtg_plane_pos_set(dcss->dtg, dcss_plane->ch_num,
				       0, 0, 0, 0);

	dcss_dtg_ch_enable(dcss->dtg, dcss_plane->ch_num, enable);
}

static void dcss_plane_atomic_disable(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
	struct dcss_plane *dcss_plane = to_dcss_plane(plane);
	struct dcss_dev *dcss = plane->dev->dev_private;

	dcss_dpr_enable(dcss->dpr, dcss_plane->ch_num, false);
	dcss_scaler_ch_enable(dcss->scaler, dcss_plane->ch_num, false);
	dcss_dtg_plane_pos_set(dcss->dtg, dcss_plane->ch_num, 0, 0, 0, 0);
	dcss_dtg_ch_enable(dcss->dtg, dcss_plane->ch_num, false);
}

static const struct drm_plane_helper_funcs dcss_plane_helper_funcs = {
	.prepare_fb = drm_gem_fb_prepare_fb,
	.atomic_check = dcss_plane_atomic_check,
	.atomic_update = dcss_plane_atomic_update,
	.atomic_disable = dcss_plane_atomic_disable,
};

struct dcss_plane *dcss_plane_init(struct drm_device *drm,
				   unsigned int possible_crtcs,
				   enum drm_plane_type type,
				   unsigned int zpos)
{
	struct dcss_plane *dcss_plane;
	const u64 *format_modifiers = dcss_video_format_modifiers;
	int ret;

	if (zpos > 2)
		return ERR_PTR(-EINVAL);

	dcss_plane = kzalloc(sizeof(*dcss_plane), GFP_KERNEL);
	if (!dcss_plane) {
		DRM_ERROR("failed to allocate plane\n");
		return ERR_PTR(-ENOMEM);
	}

	if (type == DRM_PLANE_TYPE_PRIMARY)
		format_modifiers = dcss_graphics_format_modifiers;

	ret = drm_universal_plane_init(drm, &dcss_plane->base, possible_crtcs,
				       &dcss_plane_funcs, dcss_common_formats,
				       ARRAY_SIZE(dcss_common_formats),
				       format_modifiers, type, NULL);
	if (ret) {
		DRM_ERROR("failed to initialize plane\n");
		kfree(dcss_plane);
		return ERR_PTR(ret);
	}

	drm_plane_helper_add(&dcss_plane->base, &dcss_plane_helper_funcs);

	ret = drm_plane_create_zpos_immutable_property(&dcss_plane->base, zpos);
	if (ret)
		return ERR_PTR(ret);

	drm_plane_create_rotation_property(&dcss_plane->base,
					   DRM_MODE_ROTATE_0,
					   DRM_MODE_ROTATE_0   |
					   DRM_MODE_ROTATE_90  |
					   DRM_MODE_ROTATE_180 |
					   DRM_MODE_ROTATE_270 |
					   DRM_MODE_REFLECT_X  |
					   DRM_MODE_REFLECT_Y);

	dcss_plane->ch_num = zpos;

	return dcss_plane;
}

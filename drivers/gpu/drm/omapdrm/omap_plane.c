// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Rob Clark <rob.clark@linaro.org>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_fourcc.h>

#include "omap_dmm_tiler.h"
#include "omap_drv.h"

/*
 * plane funcs
 */

#define to_omap_plane_state(x) container_of(x, struct omap_plane_state, base)

struct omap_plane_state {
	/* Must be first. */
	struct drm_plane_state base;

	struct omap_hw_overlay *overlay;
	struct omap_hw_overlay *r_overlay;  /* right overlay */
};

#define to_omap_plane(x) container_of(x, struct omap_plane, base)

struct omap_plane {
	struct drm_plane base;
	enum omap_plane_id id;
};

bool is_omap_plane_dual_overlay(struct drm_plane_state *state)
{
	struct omap_plane_state *omap_state = to_omap_plane_state(state);

	return !!omap_state->r_overlay;
}

static int omap_plane_prepare_fb(struct drm_plane *plane,
				 struct drm_plane_state *new_state)
{
	if (!new_state->fb)
		return 0;

	drm_gem_plane_helper_prepare_fb(plane, new_state);

	return omap_framebuffer_pin(new_state->fb);
}

static void omap_plane_cleanup_fb(struct drm_plane *plane,
				  struct drm_plane_state *old_state)
{
	if (old_state->fb)
		omap_framebuffer_unpin(old_state->fb);
}

static void omap_plane_atomic_update(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct omap_drm_private *priv = plane->dev->dev_private;
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state,
									   plane);
	struct omap_plane_state *new_omap_state;
	struct omap_plane_state *old_omap_state;
	struct omap_overlay_info info, r_info;
	enum omap_plane_id ovl_id, r_ovl_id;
	int ret;
	bool dual_ovl;

	new_omap_state = to_omap_plane_state(new_state);
	old_omap_state = to_omap_plane_state(old_state);

	dual_ovl = is_omap_plane_dual_overlay(new_state);

	/* Cleanup previously held overlay if needed */
	if (old_omap_state->overlay)
		omap_overlay_update_state(priv, old_omap_state->overlay);
	if (old_omap_state->r_overlay)
		omap_overlay_update_state(priv, old_omap_state->r_overlay);

	if (!new_omap_state->overlay) {
		DBG("[PLANE:%d:%s] no overlay attached", plane->base.id, plane->name);
		return;
	}

	ovl_id = new_omap_state->overlay->id;
	DBG("%s, crtc=%p fb=%p", plane->name, new_state->crtc,
	    new_state->fb);

	memset(&info, 0, sizeof(info));
	info.rotation_type = OMAP_DSS_ROT_NONE;
	info.rotation = DRM_MODE_ROTATE_0;
	info.global_alpha = new_state->alpha >> 8;
	info.zorder = new_state->normalized_zpos;
	if (new_state->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI)
		info.pre_mult_alpha = 1;
	else
		info.pre_mult_alpha = 0;
	info.color_encoding = new_state->color_encoding;
	info.color_range = new_state->color_range;

	r_info = info;

	/* update scanout: */
	omap_framebuffer_update_scanout(new_state->fb, new_state, &info,
					dual_ovl ? &r_info : NULL);

	DBG("%s: %dx%d -> %dx%d (%d)",
			new_omap_state->overlay->name, info.width, info.height,
			info.out_width, info.out_height, info.screen_width);
	DBG("%d,%d %pad %pad", info.pos_x, info.pos_y,
			&info.paddr, &info.p_uv_addr);

	if (dual_ovl) {
		r_ovl_id = new_omap_state->r_overlay->id;
		/*
		 * If the current plane uses 2 hw planes the very next
		 * zorder is used by the r_overlay so we just use the
		 * main overlay zorder + 1
		 */
		r_info.zorder = info.zorder + 1;

		DBG("%s: %dx%d -> %dx%d (%d)",
		    new_omap_state->r_overlay->name,
		    r_info.width, r_info.height,
		    r_info.out_width, r_info.out_height, r_info.screen_width);
		DBG("%d,%d %pad %pad", r_info.pos_x, r_info.pos_y,
		    &r_info.paddr, &r_info.p_uv_addr);
	}

	/* and finally, update omapdss: */
	ret = dispc_ovl_setup(priv->dispc, ovl_id, &info,
			      omap_crtc_timings(new_state->crtc), false,
			      omap_crtc_channel(new_state->crtc));
	if (ret) {
		dev_err(plane->dev->dev, "Failed to setup plane %s\n",
			plane->name);
		dispc_ovl_enable(priv->dispc, ovl_id, false);
		return;
	}

	dispc_ovl_enable(priv->dispc, ovl_id, true);

	if (dual_ovl) {
		ret = dispc_ovl_setup(priv->dispc, r_ovl_id, &r_info,
				      omap_crtc_timings(new_state->crtc), false,
				      omap_crtc_channel(new_state->crtc));
		if (ret) {
			dev_err(plane->dev->dev, "Failed to setup plane right-overlay %s\n",
				plane->name);
			dispc_ovl_enable(priv->dispc, r_ovl_id, false);
			dispc_ovl_enable(priv->dispc, ovl_id, false);
			return;
		}

		dispc_ovl_enable(priv->dispc, r_ovl_id, true);
	}
}

static void omap_plane_atomic_disable(struct drm_plane *plane,
				      struct drm_atomic_state *state)
{
	struct omap_drm_private *priv = plane->dev->dev_private;
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state,
									   plane);
	struct omap_plane_state *new_omap_state;
	struct omap_plane_state *old_omap_state;

	new_omap_state = to_omap_plane_state(new_state);
	old_omap_state = to_omap_plane_state(old_state);

	if (!old_omap_state->overlay)
		return;

	new_state->rotation = DRM_MODE_ROTATE_0;
	new_state->zpos = plane->type == DRM_PLANE_TYPE_PRIMARY ? 0 : omap_plane->id;

	omap_overlay_update_state(priv, old_omap_state->overlay);
	new_omap_state->overlay = NULL;

	if (is_omap_plane_dual_overlay(old_state)) {
		omap_overlay_update_state(priv, old_omap_state->r_overlay);
		new_omap_state->r_overlay = NULL;
	}
}

#define FRAC_16_16(mult, div)    (((mult) << 16) / (div))

static int omap_plane_atomic_check(struct drm_plane *plane,
				   struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state,
										 plane);
	struct omap_drm_private *priv = plane->dev->dev_private;
	struct omap_plane_state *omap_state = to_omap_plane_state(new_plane_state);
	struct omap_global_state *omap_overlay_global_state;
	struct drm_crtc_state *crtc_state;
	bool new_r_hw_overlay = false;
	bool new_hw_overlay = false;
	u32 max_width, max_height;
	struct drm_crtc *crtc;
	u16 width, height;
	u32 caps = 0;
	u32 fourcc;
	int ret;

	omap_overlay_global_state = omap_get_global_state(state);
	if (IS_ERR(omap_overlay_global_state))
		return PTR_ERR(omap_overlay_global_state);

	dispc_ovl_get_max_size(priv->dispc, &width, &height);
	max_width = width << 16;
	max_height = height << 16;

	crtc = new_plane_state->crtc ? new_plane_state->crtc : plane->state->crtc;
	if (!crtc)
		return 0;

	crtc_state = drm_atomic_get_existing_crtc_state(state, crtc);
	/* we should have a crtc state if the plane is attached to a crtc */
	if (WARN_ON(!crtc_state))
		return 0;

	/*
	 * Note: these are just sanity checks to filter out totally bad scaling
	 * factors. The real limits must be calculated case by case, and
	 * unfortunately we currently do those checks only at the commit
	 * phase in dispc.
	 */
	ret = drm_atomic_helper_check_plane_state(new_plane_state, crtc_state,
						  FRAC_16_16(1, 8), FRAC_16_16(8, 1),
						  true, true);
	if (ret)
		return ret;

	DBG("%s: visible %d -> %d", plane->name,
	    old_plane_state->visible, new_plane_state->visible);

	if (!new_plane_state->visible) {
		omap_overlay_release(state, omap_state->overlay);
		omap_overlay_release(state, omap_state->r_overlay);
		omap_state->overlay = NULL;
		omap_state->r_overlay = NULL;
		return 0;
	}

	if (new_plane_state->crtc_x < 0 || new_plane_state->crtc_y < 0)
		return -EINVAL;

	if (new_plane_state->crtc_x + new_plane_state->crtc_w > crtc_state->adjusted_mode.hdisplay)
		return -EINVAL;

	if (new_plane_state->crtc_y + new_plane_state->crtc_h > crtc_state->adjusted_mode.vdisplay)
		return -EINVAL;

	/* Make sure dimensions are within bounds. */
	if (new_plane_state->src_h > max_height || new_plane_state->crtc_h > height)
		return -EINVAL;


	if (new_plane_state->src_w > max_width || new_plane_state->crtc_w > width) {
		bool is_fourcc_yuv = new_plane_state->fb->format->is_yuv;

		if (is_fourcc_yuv && (((new_plane_state->src_w >> 16) / 2 & 1) ||
				      new_plane_state->crtc_w / 2 & 1)) {
			/*
			 * When calculating the split overlay width
			 * and it yield an odd value we will need to adjust
			 * the indivual width +/- 1. So make sure it fits
			 */
			if (new_plane_state->src_w <= ((2 * width - 1) << 16) &&
			    new_plane_state->crtc_w <= (2 * width - 1))
				new_r_hw_overlay = true;
			else
				return -EINVAL;
		} else {
			if (new_plane_state->src_w <= (2 * max_width) &&
			    new_plane_state->crtc_w <= (2 * width))
				new_r_hw_overlay = true;
			else
				return -EINVAL;
		}
	}

	if (new_plane_state->rotation != DRM_MODE_ROTATE_0 &&
	    !omap_framebuffer_supports_rotation(new_plane_state->fb))
		return -EINVAL;

	if ((new_plane_state->src_w >> 16) != new_plane_state->crtc_w ||
	    (new_plane_state->src_h >> 16) != new_plane_state->crtc_h)
		caps |= OMAP_DSS_OVL_CAP_SCALE;

	fourcc = new_plane_state->fb->format->format;

	/*
	 * (re)allocate hw overlay if we don't have one or
	 * there is a caps mismatch
	 */
	if (!omap_state->overlay || (caps & ~omap_state->overlay->caps)) {
		new_hw_overlay = true;
	} else {
		/* check supported format */
		if (!dispc_ovl_color_mode_supported(priv->dispc, omap_state->overlay->id,
						    fourcc))
			new_hw_overlay = true;
	}

	/*
	 * check if we need two overlays and only have 1 or
	 * if we had 2 overlays but will only need 1
	 */
	if ((new_r_hw_overlay && !omap_state->r_overlay) ||
	    (!new_r_hw_overlay && omap_state->r_overlay))
		new_hw_overlay = true;

	if (new_hw_overlay) {
		struct omap_hw_overlay *old_ovl = omap_state->overlay;
		struct omap_hw_overlay *old_r_ovl = omap_state->r_overlay;
		struct omap_hw_overlay *new_ovl = NULL;
		struct omap_hw_overlay *new_r_ovl = NULL;

		omap_overlay_release(state, old_ovl);
		omap_overlay_release(state, old_r_ovl);

		ret = omap_overlay_assign(state, plane, caps, fourcc, &new_ovl,
					  new_r_hw_overlay ? &new_r_ovl : NULL);
		if (ret) {
			DBG("%s: failed to assign hw_overlay", plane->name);
			omap_state->overlay = NULL;
			omap_state->r_overlay = NULL;
			return ret;
		}

		omap_state->overlay = new_ovl;
		if (new_r_hw_overlay)
			omap_state->r_overlay = new_r_ovl;
		else
			omap_state->r_overlay = NULL;
	}

	DBG("plane: %s overlay_id: %d", plane->name, omap_state->overlay->id);

	if (omap_state->r_overlay)
		DBG("plane: %s r_overlay_id: %d", plane->name, omap_state->r_overlay->id);

	return 0;
}

static const struct drm_plane_helper_funcs omap_plane_helper_funcs = {
	.prepare_fb = omap_plane_prepare_fb,
	.cleanup_fb = omap_plane_cleanup_fb,
	.atomic_check = omap_plane_atomic_check,
	.atomic_update = omap_plane_atomic_update,
	.atomic_disable = omap_plane_atomic_disable,
};

static void omap_plane_destroy(struct drm_plane *plane)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);

	DBG("%s", plane->name);

	drm_plane_cleanup(plane);

	kfree(omap_plane);
}

/* helper to install properties which are common to planes and crtcs */
void omap_plane_install_properties(struct drm_plane *plane,
		struct drm_mode_object *obj)
{
	struct drm_device *dev = plane->dev;
	struct omap_drm_private *priv = dev->dev_private;

	if (priv->has_dmm) {
		if (!plane->rotation_property)
			drm_plane_create_rotation_property(plane,
							   DRM_MODE_ROTATE_0,
							   DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_90 |
							   DRM_MODE_ROTATE_180 | DRM_MODE_ROTATE_270 |
							   DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y);

		/* Attach the rotation property also to the crtc object */
		if (plane->rotation_property && obj != &plane->base)
			drm_object_attach_property(obj, plane->rotation_property,
						   DRM_MODE_ROTATE_0);
	}

	drm_object_attach_property(obj, priv->zorder_prop, 0);
}

static void omap_plane_reset(struct drm_plane *plane)
{
	struct omap_plane_state *omap_state;

	if (plane->state)
		drm_atomic_helper_plane_destroy_state(plane, plane->state);

	omap_state = kzalloc(sizeof(*omap_state), GFP_KERNEL);
	if (!omap_state)
		return;

	__drm_atomic_helper_plane_reset(plane, &omap_state->base);
}

static struct drm_plane_state *
omap_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct omap_plane_state *state, *current_state;

	if (WARN_ON(!plane->state))
		return NULL;

	current_state = to_omap_plane_state(plane->state);

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &state->base);

	state->overlay = current_state->overlay;
	state->r_overlay = current_state->r_overlay;

	return &state->base;
}

static void omap_plane_atomic_print_state(struct drm_printer *p,
					  const struct drm_plane_state *state)
{
	struct omap_plane_state *omap_state = to_omap_plane_state(state);

	if (omap_state->overlay)
		drm_printf(p, "\toverlay=%s (caps=0x%x)\n",
			   omap_state->overlay->name,
			   omap_state->overlay->caps);
	else
		drm_printf(p, "\toverlay=None\n");
	if (omap_state->r_overlay)
		drm_printf(p, "\tr_overlay=%s (caps=0x%x)\n",
			   omap_state->r_overlay->name,
			   omap_state->r_overlay->caps);
	else
		drm_printf(p, "\tr_overlay=None\n");
}

static int omap_plane_atomic_set_property(struct drm_plane *plane,
					  struct drm_plane_state *state,
					  struct drm_property *property,
					  u64 val)
{
	struct omap_drm_private *priv = plane->dev->dev_private;

	if (property == priv->zorder_prop)
		state->zpos = val;
	else
		return -EINVAL;

	return 0;
}

static int omap_plane_atomic_get_property(struct drm_plane *plane,
					  const struct drm_plane_state *state,
					  struct drm_property *property,
					  u64 *val)
{
	struct omap_drm_private *priv = plane->dev->dev_private;

	if (property == priv->zorder_prop)
		*val = state->zpos;
	else
		return -EINVAL;

	return 0;
}

static const struct drm_plane_funcs omap_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.reset = omap_plane_reset,
	.destroy = omap_plane_destroy,
	.atomic_duplicate_state = omap_plane_atomic_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.atomic_set_property = omap_plane_atomic_set_property,
	.atomic_get_property = omap_plane_atomic_get_property,
	.atomic_print_state = omap_plane_atomic_print_state,
};

static bool omap_plane_supports_yuv(struct drm_plane *plane)
{
	struct omap_drm_private *priv = plane->dev->dev_private;
	struct omap_plane *omap_plane = to_omap_plane(plane);
	const u32 *formats = dispc_ovl_get_color_modes(priv->dispc, omap_plane->id);
	u32 i;

	for (i = 0; formats[i]; i++)
		if (formats[i] == DRM_FORMAT_YUYV ||
		    formats[i] == DRM_FORMAT_UYVY ||
		    formats[i] == DRM_FORMAT_NV12)
			return true;

	return false;
}

/* initialize plane */
struct drm_plane *omap_plane_init(struct drm_device *dev,
		int idx, enum drm_plane_type type,
		u32 possible_crtcs)
{
	struct omap_drm_private *priv = dev->dev_private;
	unsigned int num_planes = dispc_get_num_ovls(priv->dispc);
	struct drm_plane *plane;
	struct omap_plane *omap_plane;
	unsigned int zpos;
	int ret;
	u32 nformats;
	const u32 *formats;

	if (WARN_ON(idx >= num_planes))
		return ERR_PTR(-EINVAL);

	omap_plane = kzalloc(sizeof(*omap_plane), GFP_KERNEL);
	if (!omap_plane)
		return ERR_PTR(-ENOMEM);

	omap_plane->id = idx;

	DBG("%d: type=%d", omap_plane->id, type);
	DBG("	crtc_mask: 0x%04x", possible_crtcs);

	formats = dispc_ovl_get_color_modes(priv->dispc, omap_plane->id);
	for (nformats = 0; formats[nformats]; ++nformats)
		;

	plane = &omap_plane->base;

	ret = drm_universal_plane_init(dev, plane, possible_crtcs,
				       &omap_plane_funcs, formats,
				       nformats, NULL, type, NULL);
	if (ret < 0)
		goto error;

	drm_plane_helper_add(plane, &omap_plane_helper_funcs);

	omap_plane_install_properties(plane, &plane->base);

	/*
	 * Set the zpos default depending on whether we are a primary or overlay
	 * plane.
	 */
	if (plane->type == DRM_PLANE_TYPE_PRIMARY)
		zpos = 0;
	else
		zpos = omap_plane->id;
	drm_plane_create_zpos_property(plane, zpos, 0, num_planes - 1);
	drm_plane_create_alpha_property(plane);
	drm_plane_create_blend_mode_property(plane, BIT(DRM_MODE_BLEND_PREMULTI) |
					     BIT(DRM_MODE_BLEND_COVERAGE));

	if (omap_plane_supports_yuv(plane))
		drm_plane_create_color_properties(plane,
						  BIT(DRM_COLOR_YCBCR_BT601) |
						  BIT(DRM_COLOR_YCBCR_BT709),
						  BIT(DRM_COLOR_YCBCR_FULL_RANGE) |
						  BIT(DRM_COLOR_YCBCR_LIMITED_RANGE),
						  DRM_COLOR_YCBCR_BT601,
						  DRM_COLOR_YCBCR_FULL_RANGE);

	return plane;

error:
	dev_err(dev->dev, "%s(): could not create plane: %d\n",
		__func__, omap_plane->id);

	kfree(omap_plane);
	return NULL;
}

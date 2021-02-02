// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Russell King
 *  Rewritten from the dovefb driver, and Armada510 manuals.
 */

#include <drm/armada_drm.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_plane_helper.h>

#include "armada_crtc.h"
#include "armada_drm.h"
#include "armada_fb.h"
#include "armada_gem.h"
#include "armada_hw.h"
#include "armada_ioctlP.h"
#include "armada_plane.h"
#include "armada_trace.h"

#define DEFAULT_BRIGHTNESS	0
#define DEFAULT_CONTRAST	0x4000
#define DEFAULT_SATURATION	0x4000
#define DEFAULT_ENCODING	DRM_COLOR_YCBCR_BT601

struct armada_overlay_state {
	struct armada_plane_state base;
	u32 colorkey_yr;
	u32 colorkey_ug;
	u32 colorkey_vb;
	u32 colorkey_mode;
	u32 colorkey_enable;
	s16 brightness;
	u16 contrast;
	u16 saturation;
};
#define drm_to_overlay_state(s) \
	container_of(s, struct armada_overlay_state, base.base)

static inline u32 armada_spu_contrast(struct drm_plane_state *state)
{
	return drm_to_overlay_state(state)->brightness << 16 |
	       drm_to_overlay_state(state)->contrast;
}

static inline u32 armada_spu_saturation(struct drm_plane_state *state)
{
	/* Docs say 15:0, but it seems to actually be 31:16 on Armada 510 */
	return drm_to_overlay_state(state)->saturation << 16;
}

static inline u32 armada_csc(struct drm_plane_state *state)
{
	/*
	 * The CFG_CSC_RGB_* settings control the output of the colour space
	 * converter, setting the range of output values it produces.  Since
	 * we will be blending with the full-range graphics, we need to
	 * produce full-range RGB output from the conversion.
	 */
	return CFG_CSC_RGB_COMPUTER |
	       (state->color_encoding == DRM_COLOR_YCBCR_BT709 ?
			CFG_CSC_YUV_CCIR709 : CFG_CSC_YUV_CCIR601);
}

/* === Plane support === */
static void armada_drm_overlay_plane_atomic_update(struct drm_plane *plane,
	struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = plane->state;
	struct armada_crtc *dcrtc;
	struct armada_regs *regs;
	unsigned int idx;
	u32 cfg, cfg_mask, val;

	DRM_DEBUG_KMS("[PLANE:%d:%s]\n", plane->base.id, plane->name);

	if (!state->fb || WARN_ON(!state->crtc))
		return;

	DRM_DEBUG_KMS("[PLANE:%d:%s] is on [CRTC:%d:%s] with [FB:%d] visible %u->%u\n",
		plane->base.id, plane->name,
		state->crtc->base.id, state->crtc->name,
		state->fb->base.id,
		old_state->visible, state->visible);

	dcrtc = drm_to_armada_crtc(state->crtc);
	regs = dcrtc->regs + dcrtc->regs_idx;

	idx = 0;
	if (!old_state->visible && state->visible)
		armada_reg_queue_mod(regs, idx,
				     0, CFG_PDWN16x66 | CFG_PDWN32x66,
				     LCD_SPU_SRAM_PARA1);
	val = armada_src_hw(state);
	if (armada_src_hw(old_state) != val)
		armada_reg_queue_set(regs, idx, val, LCD_SPU_DMA_HPXL_VLN);
	val = armada_dst_yx(state);
	if (armada_dst_yx(old_state) != val)
		armada_reg_queue_set(regs, idx, val, LCD_SPU_DMA_OVSA_HPXL_VLN);
	val = armada_dst_hw(state);
	if (armada_dst_hw(old_state) != val)
		armada_reg_queue_set(regs, idx, val, LCD_SPU_DZM_HPXL_VLN);
	/* FIXME: overlay on an interlaced display */
	if (old_state->src.x1 != state->src.x1 ||
	    old_state->src.y1 != state->src.y1 ||
	    old_state->fb != state->fb ||
	    state->crtc->state->mode_changed) {
		const struct drm_format_info *format;
		u16 src_x;

		armada_reg_queue_set(regs, idx, armada_addr(state, 0, 0),
				     LCD_SPU_DMA_START_ADDR_Y0);
		armada_reg_queue_set(regs, idx, armada_addr(state, 0, 1),
				     LCD_SPU_DMA_START_ADDR_U0);
		armada_reg_queue_set(regs, idx, armada_addr(state, 0, 2),
				     LCD_SPU_DMA_START_ADDR_V0);
		armada_reg_queue_set(regs, idx, armada_addr(state, 1, 0),
				     LCD_SPU_DMA_START_ADDR_Y1);
		armada_reg_queue_set(regs, idx, armada_addr(state, 1, 1),
				     LCD_SPU_DMA_START_ADDR_U1);
		armada_reg_queue_set(regs, idx, armada_addr(state, 1, 2),
				     LCD_SPU_DMA_START_ADDR_V1);

		val = armada_pitch(state, 0) << 16 | armada_pitch(state, 0);
		armada_reg_queue_set(regs, idx, val, LCD_SPU_DMA_PITCH_YC);
		val = armada_pitch(state, 1) << 16 | armada_pitch(state, 2);
		armada_reg_queue_set(regs, idx, val, LCD_SPU_DMA_PITCH_UV);

		cfg = CFG_DMA_FMT(drm_fb_to_armada_fb(state->fb)->fmt) |
		      CFG_DMA_MOD(drm_fb_to_armada_fb(state->fb)->mod) |
		      CFG_CBSH_ENA;
		if (state->visible)
			cfg |= CFG_DMA_ENA;

		/*
		 * Shifting a YUV packed format image by one pixel causes the
		 * U/V planes to swap.  Compensate for it by also toggling
		 * the UV swap.
		 */
		format = state->fb->format;
		src_x = state->src.x1 >> 16;
		if (format->num_planes == 1 && src_x & (format->hsub - 1))
			cfg ^= CFG_DMA_MOD(CFG_SWAPUV);
		if (to_armada_plane_state(state)->interlace)
			cfg |= CFG_DMA_FTOGGLE;
		cfg_mask = CFG_CBSH_ENA | CFG_DMAFORMAT |
			   CFG_DMA_MOD(CFG_SWAPRB | CFG_SWAPUV |
				       CFG_SWAPYU | CFG_YUV2RGB) |
			   CFG_DMA_FTOGGLE | CFG_DMA_TSTMODE |
			   CFG_DMA_ENA;
	} else if (old_state->visible != state->visible) {
		cfg = state->visible ? CFG_DMA_ENA : 0;
		cfg_mask = CFG_DMA_ENA;
	} else {
		cfg = cfg_mask = 0;
	}
	if (drm_rect_width(&old_state->src) != drm_rect_width(&state->src) ||
	    drm_rect_width(&old_state->dst) != drm_rect_width(&state->dst)) {
		cfg_mask |= CFG_DMA_HSMOOTH;
		if (drm_rect_width(&state->src) >> 16 !=
		    drm_rect_width(&state->dst))
			cfg |= CFG_DMA_HSMOOTH;
	}

	if (cfg_mask)
		armada_reg_queue_mod(regs, idx, cfg, cfg_mask,
				     LCD_SPU_DMA_CTRL0);

	val = armada_spu_contrast(state);
	if ((!old_state->visible && state->visible) ||
	    armada_spu_contrast(old_state) != val)
		armada_reg_queue_set(regs, idx, val, LCD_SPU_CONTRAST);
	val = armada_spu_saturation(state);
	if ((!old_state->visible && state->visible) ||
	    armada_spu_saturation(old_state) != val)
		armada_reg_queue_set(regs, idx, val, LCD_SPU_SATURATION);
	if (!old_state->visible && state->visible)
		armada_reg_queue_set(regs, idx, 0x00002000, LCD_SPU_CBSH_HUE);
	val = armada_csc(state);
	if ((!old_state->visible && state->visible) ||
	    armada_csc(old_state) != val)
		armada_reg_queue_mod(regs, idx, val, CFG_CSC_MASK,
				     LCD_SPU_IOPAD_CONTROL);
	val = drm_to_overlay_state(state)->colorkey_yr;
	if ((!old_state->visible && state->visible) ||
	    drm_to_overlay_state(old_state)->colorkey_yr != val)
		armada_reg_queue_set(regs, idx, val, LCD_SPU_COLORKEY_Y);
	val = drm_to_overlay_state(state)->colorkey_ug;
	if ((!old_state->visible && state->visible) ||
	    drm_to_overlay_state(old_state)->colorkey_ug != val)
		armada_reg_queue_set(regs, idx, val, LCD_SPU_COLORKEY_U);
	val = drm_to_overlay_state(state)->colorkey_vb;
	if ((!old_state->visible && state->visible) ||
	    drm_to_overlay_state(old_state)->colorkey_vb != val)
		armada_reg_queue_set(regs, idx, val, LCD_SPU_COLORKEY_V);
	val = drm_to_overlay_state(state)->colorkey_mode;
	if ((!old_state->visible && state->visible) ||
	    drm_to_overlay_state(old_state)->colorkey_mode != val)
		armada_reg_queue_mod(regs, idx, val, CFG_CKMODE_MASK |
				     CFG_ALPHAM_MASK | CFG_ALPHA_MASK,
				     LCD_SPU_DMA_CTRL1);
	val = drm_to_overlay_state(state)->colorkey_enable;
	if (((!old_state->visible && state->visible) ||
	     drm_to_overlay_state(old_state)->colorkey_enable != val) &&
	    dcrtc->variant->has_spu_adv_reg)
		armada_reg_queue_mod(regs, idx, val, ADV_GRACOLORKEY |
				     ADV_VIDCOLORKEY, LCD_SPU_ADV_REG);

	dcrtc->regs_idx += idx;
}

static void armada_drm_overlay_plane_atomic_disable(struct drm_plane *plane,
	struct drm_plane_state *old_state)
{
	struct armada_crtc *dcrtc;
	struct armada_regs *regs;
	unsigned int idx = 0;

	DRM_DEBUG_KMS("[PLANE:%d:%s]\n", plane->base.id, plane->name);

	if (!old_state->crtc)
		return;

	DRM_DEBUG_KMS("[PLANE:%d:%s] was on [CRTC:%d:%s] with [FB:%d]\n",
		plane->base.id, plane->name,
		old_state->crtc->base.id, old_state->crtc->name,
		old_state->fb->base.id);

	dcrtc = drm_to_armada_crtc(old_state->crtc);
	regs = dcrtc->regs + dcrtc->regs_idx;

	/* Disable plane and power down the YUV FIFOs */
	armada_reg_queue_mod(regs, idx, 0, CFG_DMA_ENA, LCD_SPU_DMA_CTRL0);
	armada_reg_queue_mod(regs, idx, CFG_PDWN16x66 | CFG_PDWN32x66, 0,
			     LCD_SPU_SRAM_PARA1);

	dcrtc->regs_idx += idx;
}

static const struct drm_plane_helper_funcs armada_overlay_plane_helper_funcs = {
	.prepare_fb	= armada_drm_plane_prepare_fb,
	.cleanup_fb	= armada_drm_plane_cleanup_fb,
	.atomic_check	= armada_drm_plane_atomic_check,
	.atomic_update	= armada_drm_overlay_plane_atomic_update,
	.atomic_disable	= armada_drm_overlay_plane_atomic_disable,
};

static int
armada_overlay_plane_update(struct drm_plane *plane, struct drm_crtc *crtc,
	struct drm_framebuffer *fb,
	int crtc_x, int crtc_y, unsigned crtc_w, unsigned crtc_h,
	uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h,
	struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_plane_state *plane_state;
	int ret = 0;

	trace_armada_ovl_plane_update(plane, crtc, fb,
				 crtc_x, crtc_y, crtc_w, crtc_h,
				 src_x, src_y, src_w, src_h);

	state = drm_atomic_state_alloc(plane->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;
	plane_state = drm_atomic_get_plane_state(state, plane);
	if (IS_ERR(plane_state)) {
		ret = PTR_ERR(plane_state);
		goto fail;
	}

	ret = drm_atomic_set_crtc_for_plane(plane_state, crtc);
	if (ret != 0)
		goto fail;

	drm_atomic_set_fb_for_plane(plane_state, fb);
	plane_state->crtc_x = crtc_x;
	plane_state->crtc_y = crtc_y;
	plane_state->crtc_h = crtc_h;
	plane_state->crtc_w = crtc_w;
	plane_state->src_x = src_x;
	plane_state->src_y = src_y;
	plane_state->src_h = src_h;
	plane_state->src_w = src_w;

	ret = drm_atomic_nonblocking_commit(state);
fail:
	drm_atomic_state_put(state);
	return ret;
}

static void armada_ovl_plane_destroy(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);
	kfree(plane);
}

static void armada_overlay_reset(struct drm_plane *plane)
{
	struct armada_overlay_state *state;

	if (plane->state)
		__drm_atomic_helper_plane_destroy_state(plane->state);
	kfree(plane->state);
	plane->state = NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state) {
		state->colorkey_yr = 0xfefefe00;
		state->colorkey_ug = 0x01010100;
		state->colorkey_vb = 0x01010100;
		state->colorkey_mode = CFG_CKMODE(CKMODE_RGB) |
				       CFG_ALPHAM_GRA | CFG_ALPHA(0);
		state->colorkey_enable = ADV_GRACOLORKEY;
		state->brightness = DEFAULT_BRIGHTNESS;
		state->contrast = DEFAULT_CONTRAST;
		state->saturation = DEFAULT_SATURATION;
		__drm_atomic_helper_plane_reset(plane, &state->base.base);
		state->base.base.color_encoding = DEFAULT_ENCODING;
		state->base.base.color_range = DRM_COLOR_YCBCR_LIMITED_RANGE;
	}
}

static struct drm_plane_state *
armada_overlay_duplicate_state(struct drm_plane *plane)
{
	struct armada_overlay_state *state;

	if (WARN_ON(!plane->state))
		return NULL;

	state = kmemdup(plane->state, sizeof(*state), GFP_KERNEL);
	if (state)
		__drm_atomic_helper_plane_duplicate_state(plane,
							  &state->base.base);
	return &state->base.base;
}

static int armada_overlay_set_property(struct drm_plane *plane,
	struct drm_plane_state *state, struct drm_property *property,
	uint64_t val)
{
	struct armada_private *priv = drm_to_armada_dev(plane->dev);

#define K2R(val) (((val) >> 0) & 0xff)
#define K2G(val) (((val) >> 8) & 0xff)
#define K2B(val) (((val) >> 16) & 0xff)
	if (property == priv->colorkey_prop) {
#define CCC(v) ((v) << 24 | (v) << 16 | (v) << 8)
		drm_to_overlay_state(state)->colorkey_yr = CCC(K2R(val));
		drm_to_overlay_state(state)->colorkey_ug = CCC(K2G(val));
		drm_to_overlay_state(state)->colorkey_vb = CCC(K2B(val));
#undef CCC
	} else if (property == priv->colorkey_min_prop) {
		drm_to_overlay_state(state)->colorkey_yr &= ~0x00ff0000;
		drm_to_overlay_state(state)->colorkey_yr |= K2R(val) << 16;
		drm_to_overlay_state(state)->colorkey_ug &= ~0x00ff0000;
		drm_to_overlay_state(state)->colorkey_ug |= K2G(val) << 16;
		drm_to_overlay_state(state)->colorkey_vb &= ~0x00ff0000;
		drm_to_overlay_state(state)->colorkey_vb |= K2B(val) << 16;
	} else if (property == priv->colorkey_max_prop) {
		drm_to_overlay_state(state)->colorkey_yr &= ~0xff000000;
		drm_to_overlay_state(state)->colorkey_yr |= K2R(val) << 24;
		drm_to_overlay_state(state)->colorkey_ug &= ~0xff000000;
		drm_to_overlay_state(state)->colorkey_ug |= K2G(val) << 24;
		drm_to_overlay_state(state)->colorkey_vb &= ~0xff000000;
		drm_to_overlay_state(state)->colorkey_vb |= K2B(val) << 24;
	} else if (property == priv->colorkey_val_prop) {
		drm_to_overlay_state(state)->colorkey_yr &= ~0x0000ff00;
		drm_to_overlay_state(state)->colorkey_yr |= K2R(val) << 8;
		drm_to_overlay_state(state)->colorkey_ug &= ~0x0000ff00;
		drm_to_overlay_state(state)->colorkey_ug |= K2G(val) << 8;
		drm_to_overlay_state(state)->colorkey_vb &= ~0x0000ff00;
		drm_to_overlay_state(state)->colorkey_vb |= K2B(val) << 8;
	} else if (property == priv->colorkey_alpha_prop) {
		drm_to_overlay_state(state)->colorkey_yr &= ~0x000000ff;
		drm_to_overlay_state(state)->colorkey_yr |= K2R(val);
		drm_to_overlay_state(state)->colorkey_ug &= ~0x000000ff;
		drm_to_overlay_state(state)->colorkey_ug |= K2G(val);
		drm_to_overlay_state(state)->colorkey_vb &= ~0x000000ff;
		drm_to_overlay_state(state)->colorkey_vb |= K2B(val);
	} else if (property == priv->colorkey_mode_prop) {
		if (val == CKMODE_DISABLE) {
			drm_to_overlay_state(state)->colorkey_mode =
				CFG_CKMODE(CKMODE_DISABLE) |
				CFG_ALPHAM_CFG | CFG_ALPHA(255);
			drm_to_overlay_state(state)->colorkey_enable = 0;
		} else {
			drm_to_overlay_state(state)->colorkey_mode =
				CFG_CKMODE(val) |
				CFG_ALPHAM_GRA | CFG_ALPHA(0);
			drm_to_overlay_state(state)->colorkey_enable =
				ADV_GRACOLORKEY;
		}
	} else if (property == priv->brightness_prop) {
		drm_to_overlay_state(state)->brightness = val - 256;
	} else if (property == priv->contrast_prop) {
		drm_to_overlay_state(state)->contrast = val;
	} else if (property == priv->saturation_prop) {
		drm_to_overlay_state(state)->saturation = val;
	} else {
		return -EINVAL;
	}
	return 0;
}

static int armada_overlay_get_property(struct drm_plane *plane,
	const struct drm_plane_state *state, struct drm_property *property,
	uint64_t *val)
{
	struct armada_private *priv = drm_to_armada_dev(plane->dev);

#define C2K(c,s)	(((c) >> (s)) & 0xff)
#define R2BGR(r,g,b,s)	(C2K(r,s) << 0 | C2K(g,s) << 8 | C2K(b,s) << 16)
	if (property == priv->colorkey_prop) {
		/* Do best-efforts here for this property */
		*val = R2BGR(drm_to_overlay_state(state)->colorkey_yr,
			     drm_to_overlay_state(state)->colorkey_ug,
			     drm_to_overlay_state(state)->colorkey_vb, 16);
		/* If min != max, or min != val, error out */
		if (*val != R2BGR(drm_to_overlay_state(state)->colorkey_yr,
				  drm_to_overlay_state(state)->colorkey_ug,
				  drm_to_overlay_state(state)->colorkey_vb, 24) ||
		    *val != R2BGR(drm_to_overlay_state(state)->colorkey_yr,
				  drm_to_overlay_state(state)->colorkey_ug,
				  drm_to_overlay_state(state)->colorkey_vb, 8))
			return -EINVAL;
	} else if (property == priv->colorkey_min_prop) {
		*val = R2BGR(drm_to_overlay_state(state)->colorkey_yr,
			     drm_to_overlay_state(state)->colorkey_ug,
			     drm_to_overlay_state(state)->colorkey_vb, 16);
	} else if (property == priv->colorkey_max_prop) {
		*val = R2BGR(drm_to_overlay_state(state)->colorkey_yr,
			     drm_to_overlay_state(state)->colorkey_ug,
			     drm_to_overlay_state(state)->colorkey_vb, 24);
	} else if (property == priv->colorkey_val_prop) {
		*val = R2BGR(drm_to_overlay_state(state)->colorkey_yr,
			     drm_to_overlay_state(state)->colorkey_ug,
			     drm_to_overlay_state(state)->colorkey_vb, 8);
	} else if (property == priv->colorkey_alpha_prop) {
		*val = R2BGR(drm_to_overlay_state(state)->colorkey_yr,
			     drm_to_overlay_state(state)->colorkey_ug,
			     drm_to_overlay_state(state)->colorkey_vb, 0);
	} else if (property == priv->colorkey_mode_prop) {
		*val = (drm_to_overlay_state(state)->colorkey_mode &
			CFG_CKMODE_MASK) >> ffs(CFG_CKMODE_MASK);
	} else if (property == priv->brightness_prop) {
		*val = drm_to_overlay_state(state)->brightness + 256;
	} else if (property == priv->contrast_prop) {
		*val = drm_to_overlay_state(state)->contrast;
	} else if (property == priv->saturation_prop) {
		*val = drm_to_overlay_state(state)->saturation;
	} else {
		return -EINVAL;
	}
	return 0;
}

static const struct drm_plane_funcs armada_ovl_plane_funcs = {
	.update_plane	= armada_overlay_plane_update,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= armada_ovl_plane_destroy,
	.reset		= armada_overlay_reset,
	.atomic_duplicate_state = armada_overlay_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.atomic_set_property = armada_overlay_set_property,
	.atomic_get_property = armada_overlay_get_property,
};

static const uint32_t armada_ovl_formats[] = {
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YVU422,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
};

static const struct drm_prop_enum_list armada_drm_colorkey_enum_list[] = {
	{ CKMODE_DISABLE, "disabled" },
	{ CKMODE_Y,       "Y component" },
	{ CKMODE_U,       "U component" },
	{ CKMODE_V,       "V component" },
	{ CKMODE_RGB,     "RGB" },
	{ CKMODE_R,       "R component" },
	{ CKMODE_G,       "G component" },
	{ CKMODE_B,       "B component" },
};

static int armada_overlay_create_properties(struct drm_device *dev)
{
	struct armada_private *priv = drm_to_armada_dev(dev);

	if (priv->colorkey_prop)
		return 0;

	priv->colorkey_prop = drm_property_create_range(dev, 0,
				"colorkey", 0, 0xffffff);
	priv->colorkey_min_prop = drm_property_create_range(dev, 0,
				"colorkey_min", 0, 0xffffff);
	priv->colorkey_max_prop = drm_property_create_range(dev, 0,
				"colorkey_max", 0, 0xffffff);
	priv->colorkey_val_prop = drm_property_create_range(dev, 0,
				"colorkey_val", 0, 0xffffff);
	priv->colorkey_alpha_prop = drm_property_create_range(dev, 0,
				"colorkey_alpha", 0, 0xffffff);
	priv->colorkey_mode_prop = drm_property_create_enum(dev, 0,
				"colorkey_mode",
				armada_drm_colorkey_enum_list,
				ARRAY_SIZE(armada_drm_colorkey_enum_list));
	priv->brightness_prop = drm_property_create_range(dev, 0,
				"brightness", 0, 256 + 255);
	priv->contrast_prop = drm_property_create_range(dev, 0,
				"contrast", 0, 0x7fff);
	priv->saturation_prop = drm_property_create_range(dev, 0,
				"saturation", 0, 0x7fff);

	if (!priv->colorkey_prop)
		return -ENOMEM;

	return 0;
}

int armada_overlay_plane_create(struct drm_device *dev, unsigned long crtcs)
{
	struct armada_private *priv = drm_to_armada_dev(dev);
	struct drm_mode_object *mobj;
	struct drm_plane *overlay;
	int ret;

	ret = armada_overlay_create_properties(dev);
	if (ret)
		return ret;

	overlay = kzalloc(sizeof(*overlay), GFP_KERNEL);
	if (!overlay)
		return -ENOMEM;

	drm_plane_helper_add(overlay, &armada_overlay_plane_helper_funcs);

	ret = drm_universal_plane_init(dev, overlay, crtcs,
				       &armada_ovl_plane_funcs,
				       armada_ovl_formats,
				       ARRAY_SIZE(armada_ovl_formats),
				       NULL,
				       DRM_PLANE_TYPE_OVERLAY, NULL);
	if (ret) {
		kfree(overlay);
		return ret;
	}

	mobj = &overlay->base;
	drm_object_attach_property(mobj, priv->colorkey_prop,
				   0x0101fe);
	drm_object_attach_property(mobj, priv->colorkey_min_prop,
				   0x0101fe);
	drm_object_attach_property(mobj, priv->colorkey_max_prop,
				   0x0101fe);
	drm_object_attach_property(mobj, priv->colorkey_val_prop,
				   0x0101fe);
	drm_object_attach_property(mobj, priv->colorkey_alpha_prop,
				   0x000000);
	drm_object_attach_property(mobj, priv->colorkey_mode_prop,
				   CKMODE_RGB);
	drm_object_attach_property(mobj, priv->brightness_prop,
				   256 + DEFAULT_BRIGHTNESS);
	drm_object_attach_property(mobj, priv->contrast_prop,
				   DEFAULT_CONTRAST);
	drm_object_attach_property(mobj, priv->saturation_prop,
				   DEFAULT_SATURATION);

	ret = drm_plane_create_color_properties(overlay,
						BIT(DRM_COLOR_YCBCR_BT601) |
						BIT(DRM_COLOR_YCBCR_BT709),
						BIT(DRM_COLOR_YCBCR_LIMITED_RANGE),
						DEFAULT_ENCODING,
						DRM_COLOR_YCBCR_LIMITED_RANGE);

	return ret;
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <soc/tegra/pmc.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>

#include "dc.h"
#include "drm.h"
#include "gem.h"
#include "hub.h"
#include "plane.h"

static void tegra_crtc_atomic_destroy_state(struct drm_crtc *crtc,
					    struct drm_crtc_state *state);

static void tegra_dc_stats_reset(struct tegra_dc_stats *stats)
{
	stats->frames = 0;
	stats->vblank = 0;
	stats->underflow = 0;
	stats->overflow = 0;
}

/* Reads the active copy of a register. */
static u32 tegra_dc_readl_active(struct tegra_dc *dc, unsigned long offset)
{
	u32 value;

	tegra_dc_writel(dc, READ_MUX, DC_CMD_STATE_ACCESS);
	value = tegra_dc_readl(dc, offset);
	tegra_dc_writel(dc, 0, DC_CMD_STATE_ACCESS);

	return value;
}

static inline unsigned int tegra_plane_offset(struct tegra_plane *plane,
					      unsigned int offset)
{
	if (offset >= 0x500 && offset <= 0x638) {
		offset = 0x000 + (offset - 0x500);
		return plane->offset + offset;
	}

	if (offset >= 0x700 && offset <= 0x719) {
		offset = 0x180 + (offset - 0x700);
		return plane->offset + offset;
	}

	if (offset >= 0x800 && offset <= 0x839) {
		offset = 0x1c0 + (offset - 0x800);
		return plane->offset + offset;
	}

	dev_WARN(plane->dc->dev, "invalid offset: %x\n", offset);

	return plane->offset + offset;
}

static inline u32 tegra_plane_readl(struct tegra_plane *plane,
				    unsigned int offset)
{
	return tegra_dc_readl(plane->dc, tegra_plane_offset(plane, offset));
}

static inline void tegra_plane_writel(struct tegra_plane *plane, u32 value,
				      unsigned int offset)
{
	tegra_dc_writel(plane->dc, value, tegra_plane_offset(plane, offset));
}

bool tegra_dc_has_output(struct tegra_dc *dc, struct device *dev)
{
	struct device_node *np = dc->dev->of_node;
	struct of_phandle_iterator it;
	int err;

	of_for_each_phandle(&it, err, np, "nvidia,outputs", NULL, 0)
		if (it.node == dev->of_node)
			return true;

	return false;
}

/*
 * Double-buffered registers have two copies: ASSEMBLY and ACTIVE. When the
 * *_ACT_REQ bits are set the ASSEMBLY copy is latched into the ACTIVE copy.
 * Latching happens mmediately if the display controller is in STOP mode or
 * on the next frame boundary otherwise.
 *
 * Triple-buffered registers have three copies: ASSEMBLY, ARM and ACTIVE. The
 * ASSEMBLY copy is latched into the ARM copy immediately after *_UPDATE bits
 * are written. When the *_ACT_REQ bits are written, the ARM copy is latched
 * into the ACTIVE copy, either immediately if the display controller is in
 * STOP mode, or at the next frame boundary otherwise.
 */
void tegra_dc_commit(struct tegra_dc *dc)
{
	tegra_dc_writel(dc, GENERAL_ACT_REQ << 8, DC_CMD_STATE_CONTROL);
	tegra_dc_writel(dc, GENERAL_ACT_REQ, DC_CMD_STATE_CONTROL);
}

static inline u32 compute_dda_inc(unsigned int in, unsigned int out, bool v,
				  unsigned int bpp)
{
	fixed20_12 outf = dfixed_init(out);
	fixed20_12 inf = dfixed_init(in);
	u32 dda_inc;
	int max;

	if (v)
		max = 15;
	else {
		switch (bpp) {
		case 2:
			max = 8;
			break;

		default:
			WARN_ON_ONCE(1);
			/* fallthrough */
		case 4:
			max = 4;
			break;
		}
	}

	outf.full = max_t(u32, outf.full - dfixed_const(1), dfixed_const(1));
	inf.full -= dfixed_const(1);

	dda_inc = dfixed_div(inf, outf);
	dda_inc = min_t(u32, dda_inc, dfixed_const(max));

	return dda_inc;
}

static inline u32 compute_initial_dda(unsigned int in)
{
	fixed20_12 inf = dfixed_init(in);
	return dfixed_frac(inf);
}

static void tegra_plane_setup_blending_legacy(struct tegra_plane *plane)
{
	u32 background[3] = {
		BLEND_WEIGHT1(0) | BLEND_WEIGHT0(0) | BLEND_COLOR_KEY_NONE,
		BLEND_WEIGHT1(0) | BLEND_WEIGHT0(0) | BLEND_COLOR_KEY_NONE,
		BLEND_WEIGHT1(0) | BLEND_WEIGHT0(0) | BLEND_COLOR_KEY_NONE,
	};
	u32 foreground = BLEND_WEIGHT1(255) | BLEND_WEIGHT0(255) |
			 BLEND_COLOR_KEY_NONE;
	u32 blendnokey = BLEND_WEIGHT1(255) | BLEND_WEIGHT0(255);
	struct tegra_plane_state *state;
	u32 blending[2];
	unsigned int i;

	/* disable blending for non-overlapping case */
	tegra_plane_writel(plane, blendnokey, DC_WIN_BLEND_NOKEY);
	tegra_plane_writel(plane, foreground, DC_WIN_BLEND_1WIN);

	state = to_tegra_plane_state(plane->base.state);

	if (state->opaque) {
		/*
		 * Since custom fix-weight blending isn't utilized and weight
		 * of top window is set to max, we can enforce dependent
		 * blending which in this case results in transparent bottom
		 * window if top window is opaque and if top window enables
		 * alpha blending, then bottom window is getting alpha value
		 * of 1 minus the sum of alpha components of the overlapping
		 * plane.
		 */
		background[0] |= BLEND_CONTROL_DEPENDENT;
		background[1] |= BLEND_CONTROL_DEPENDENT;

		/*
		 * The region where three windows overlap is the intersection
		 * of the two regions where two windows overlap. It contributes
		 * to the area if all of the windows on top of it have an alpha
		 * component.
		 */
		switch (state->base.normalized_zpos) {
		case 0:
			if (state->blending[0].alpha &&
			    state->blending[1].alpha)
				background[2] |= BLEND_CONTROL_DEPENDENT;
			break;

		case 1:
			background[2] |= BLEND_CONTROL_DEPENDENT;
			break;
		}
	} else {
		/*
		 * Enable alpha blending if pixel format has an alpha
		 * component.
		 */
		foreground |= BLEND_CONTROL_ALPHA;

		/*
		 * If any of the windows on top of this window is opaque, it
		 * will completely conceal this window within that area. If
		 * top window has an alpha component, it is blended over the
		 * bottom window.
		 */
		for (i = 0; i < 2; i++) {
			if (state->blending[i].alpha &&
			    state->blending[i].top)
				background[i] |= BLEND_CONTROL_DEPENDENT;
		}

		switch (state->base.normalized_zpos) {
		case 0:
			if (state->blending[0].alpha &&
			    state->blending[1].alpha)
				background[2] |= BLEND_CONTROL_DEPENDENT;
			break;

		case 1:
			/*
			 * When both middle and topmost windows have an alpha,
			 * these windows a mixed together and then the result
			 * is blended over the bottom window.
			 */
			if (state->blending[0].alpha &&
			    state->blending[0].top)
				background[2] |= BLEND_CONTROL_ALPHA;

			if (state->blending[1].alpha &&
			    state->blending[1].top)
				background[2] |= BLEND_CONTROL_ALPHA;
			break;
		}
	}

	switch (state->base.normalized_zpos) {
	case 0:
		tegra_plane_writel(plane, background[0], DC_WIN_BLEND_2WIN_X);
		tegra_plane_writel(plane, background[1], DC_WIN_BLEND_2WIN_Y);
		tegra_plane_writel(plane, background[2], DC_WIN_BLEND_3WIN_XY);
		break;

	case 1:
		/*
		 * If window B / C is topmost, then X / Y registers are
		 * matching the order of blending[...] state indices,
		 * otherwise a swap is required.
		 */
		if (!state->blending[0].top && state->blending[1].top) {
			blending[0] = foreground;
			blending[1] = background[1];
		} else {
			blending[0] = background[0];
			blending[1] = foreground;
		}

		tegra_plane_writel(plane, blending[0], DC_WIN_BLEND_2WIN_X);
		tegra_plane_writel(plane, blending[1], DC_WIN_BLEND_2WIN_Y);
		tegra_plane_writel(plane, background[2], DC_WIN_BLEND_3WIN_XY);
		break;

	case 2:
		tegra_plane_writel(plane, foreground, DC_WIN_BLEND_2WIN_X);
		tegra_plane_writel(plane, foreground, DC_WIN_BLEND_2WIN_Y);
		tegra_plane_writel(plane, foreground, DC_WIN_BLEND_3WIN_XY);
		break;
	}
}

static void tegra_plane_setup_blending(struct tegra_plane *plane,
				       const struct tegra_dc_window *window)
{
	u32 value;

	value = BLEND_FACTOR_DST_ALPHA_ZERO | BLEND_FACTOR_SRC_ALPHA_K2 |
		BLEND_FACTOR_DST_COLOR_NEG_K1_TIMES_SRC |
		BLEND_FACTOR_SRC_COLOR_K1_TIMES_SRC;
	tegra_plane_writel(plane, value, DC_WIN_BLEND_MATCH_SELECT);

	value = BLEND_FACTOR_DST_ALPHA_ZERO | BLEND_FACTOR_SRC_ALPHA_K2 |
		BLEND_FACTOR_DST_COLOR_NEG_K1_TIMES_SRC |
		BLEND_FACTOR_SRC_COLOR_K1_TIMES_SRC;
	tegra_plane_writel(plane, value, DC_WIN_BLEND_NOMATCH_SELECT);

	value = K2(255) | K1(255) | WINDOW_LAYER_DEPTH(255 - window->zpos);
	tegra_plane_writel(plane, value, DC_WIN_BLEND_LAYER_CONTROL);
}

static bool
tegra_plane_use_horizontal_filtering(struct tegra_plane *plane,
				     const struct tegra_dc_window *window)
{
	struct tegra_dc *dc = plane->dc;

	if (window->src.w == window->dst.w)
		return false;

	if (plane->index == 0 && dc->soc->has_win_a_without_filters)
		return false;

	return true;
}

static bool
tegra_plane_use_vertical_filtering(struct tegra_plane *plane,
				   const struct tegra_dc_window *window)
{
	struct tegra_dc *dc = plane->dc;

	if (window->src.h == window->dst.h)
		return false;

	if (plane->index == 0 && dc->soc->has_win_a_without_filters)
		return false;

	if (plane->index == 2 && dc->soc->has_win_c_without_vert_filter)
		return false;

	return true;
}

static void tegra_dc_setup_window(struct tegra_plane *plane,
				  const struct tegra_dc_window *window)
{
	unsigned h_offset, v_offset, h_size, v_size, h_dda, v_dda, bpp;
	struct tegra_dc *dc = plane->dc;
	bool yuv, planar;
	u32 value;

	/*
	 * For YUV planar modes, the number of bytes per pixel takes into
	 * account only the luma component and therefore is 1.
	 */
	yuv = tegra_plane_format_is_yuv(window->format, &planar);
	if (!yuv)
		bpp = window->bits_per_pixel / 8;
	else
		bpp = planar ? 1 : 2;

	tegra_plane_writel(plane, window->format, DC_WIN_COLOR_DEPTH);
	tegra_plane_writel(plane, window->swap, DC_WIN_BYTE_SWAP);

	value = V_POSITION(window->dst.y) | H_POSITION(window->dst.x);
	tegra_plane_writel(plane, value, DC_WIN_POSITION);

	value = V_SIZE(window->dst.h) | H_SIZE(window->dst.w);
	tegra_plane_writel(plane, value, DC_WIN_SIZE);

	h_offset = window->src.x * bpp;
	v_offset = window->src.y;
	h_size = window->src.w * bpp;
	v_size = window->src.h;

	value = V_PRESCALED_SIZE(v_size) | H_PRESCALED_SIZE(h_size);
	tegra_plane_writel(plane, value, DC_WIN_PRESCALED_SIZE);

	/*
	 * For DDA computations the number of bytes per pixel for YUV planar
	 * modes needs to take into account all Y, U and V components.
	 */
	if (yuv && planar)
		bpp = 2;

	h_dda = compute_dda_inc(window->src.w, window->dst.w, false, bpp);
	v_dda = compute_dda_inc(window->src.h, window->dst.h, true, bpp);

	value = V_DDA_INC(v_dda) | H_DDA_INC(h_dda);
	tegra_plane_writel(plane, value, DC_WIN_DDA_INC);

	h_dda = compute_initial_dda(window->src.x);
	v_dda = compute_initial_dda(window->src.y);

	tegra_plane_writel(plane, h_dda, DC_WIN_H_INITIAL_DDA);
	tegra_plane_writel(plane, v_dda, DC_WIN_V_INITIAL_DDA);

	tegra_plane_writel(plane, 0, DC_WIN_UV_BUF_STRIDE);
	tegra_plane_writel(plane, 0, DC_WIN_BUF_STRIDE);

	tegra_plane_writel(plane, window->base[0], DC_WINBUF_START_ADDR);

	if (yuv && planar) {
		tegra_plane_writel(plane, window->base[1], DC_WINBUF_START_ADDR_U);
		tegra_plane_writel(plane, window->base[2], DC_WINBUF_START_ADDR_V);
		value = window->stride[1] << 16 | window->stride[0];
		tegra_plane_writel(plane, value, DC_WIN_LINE_STRIDE);
	} else {
		tegra_plane_writel(plane, window->stride[0], DC_WIN_LINE_STRIDE);
	}

	if (window->bottom_up)
		v_offset += window->src.h - 1;

	tegra_plane_writel(plane, h_offset, DC_WINBUF_ADDR_H_OFFSET);
	tegra_plane_writel(plane, v_offset, DC_WINBUF_ADDR_V_OFFSET);

	if (dc->soc->supports_block_linear) {
		unsigned long height = window->tiling.value;

		switch (window->tiling.mode) {
		case TEGRA_BO_TILING_MODE_PITCH:
			value = DC_WINBUF_SURFACE_KIND_PITCH;
			break;

		case TEGRA_BO_TILING_MODE_TILED:
			value = DC_WINBUF_SURFACE_KIND_TILED;
			break;

		case TEGRA_BO_TILING_MODE_BLOCK:
			value = DC_WINBUF_SURFACE_KIND_BLOCK_HEIGHT(height) |
				DC_WINBUF_SURFACE_KIND_BLOCK;
			break;
		}

		tegra_plane_writel(plane, value, DC_WINBUF_SURFACE_KIND);
	} else {
		switch (window->tiling.mode) {
		case TEGRA_BO_TILING_MODE_PITCH:
			value = DC_WIN_BUFFER_ADDR_MODE_LINEAR_UV |
				DC_WIN_BUFFER_ADDR_MODE_LINEAR;
			break;

		case TEGRA_BO_TILING_MODE_TILED:
			value = DC_WIN_BUFFER_ADDR_MODE_TILE_UV |
				DC_WIN_BUFFER_ADDR_MODE_TILE;
			break;

		case TEGRA_BO_TILING_MODE_BLOCK:
			/*
			 * No need to handle this here because ->atomic_check
			 * will already have filtered it out.
			 */
			break;
		}

		tegra_plane_writel(plane, value, DC_WIN_BUFFER_ADDR_MODE);
	}

	value = WIN_ENABLE;

	if (yuv) {
		/* setup default colorspace conversion coefficients */
		tegra_plane_writel(plane, 0x00f0, DC_WIN_CSC_YOF);
		tegra_plane_writel(plane, 0x012a, DC_WIN_CSC_KYRGB);
		tegra_plane_writel(plane, 0x0000, DC_WIN_CSC_KUR);
		tegra_plane_writel(plane, 0x0198, DC_WIN_CSC_KVR);
		tegra_plane_writel(plane, 0x039b, DC_WIN_CSC_KUG);
		tegra_plane_writel(plane, 0x032f, DC_WIN_CSC_KVG);
		tegra_plane_writel(plane, 0x0204, DC_WIN_CSC_KUB);
		tegra_plane_writel(plane, 0x0000, DC_WIN_CSC_KVB);

		value |= CSC_ENABLE;
	} else if (window->bits_per_pixel < 24) {
		value |= COLOR_EXPAND;
	}

	if (window->bottom_up)
		value |= V_DIRECTION;

	if (tegra_plane_use_horizontal_filtering(plane, window)) {
		/*
		 * Enable horizontal 6-tap filter and set filtering
		 * coefficients to the default values defined in TRM.
		 */
		tegra_plane_writel(plane, 0x00008000, DC_WIN_H_FILTER_P(0));
		tegra_plane_writel(plane, 0x3e087ce1, DC_WIN_H_FILTER_P(1));
		tegra_plane_writel(plane, 0x3b117ac1, DC_WIN_H_FILTER_P(2));
		tegra_plane_writel(plane, 0x591b73aa, DC_WIN_H_FILTER_P(3));
		tegra_plane_writel(plane, 0x57256d9a, DC_WIN_H_FILTER_P(4));
		tegra_plane_writel(plane, 0x552f668b, DC_WIN_H_FILTER_P(5));
		tegra_plane_writel(plane, 0x73385e8b, DC_WIN_H_FILTER_P(6));
		tegra_plane_writel(plane, 0x72435583, DC_WIN_H_FILTER_P(7));
		tegra_plane_writel(plane, 0x714c4c8b, DC_WIN_H_FILTER_P(8));
		tegra_plane_writel(plane, 0x70554393, DC_WIN_H_FILTER_P(9));
		tegra_plane_writel(plane, 0x715e389b, DC_WIN_H_FILTER_P(10));
		tegra_plane_writel(plane, 0x71662faa, DC_WIN_H_FILTER_P(11));
		tegra_plane_writel(plane, 0x536d25ba, DC_WIN_H_FILTER_P(12));
		tegra_plane_writel(plane, 0x55731bca, DC_WIN_H_FILTER_P(13));
		tegra_plane_writel(plane, 0x387a11d9, DC_WIN_H_FILTER_P(14));
		tegra_plane_writel(plane, 0x3c7c08f1, DC_WIN_H_FILTER_P(15));

		value |= H_FILTER;
	}

	if (tegra_plane_use_vertical_filtering(plane, window)) {
		unsigned int i, k;

		/*
		 * Enable vertical 2-tap filter and set filtering
		 * coefficients to the default values defined in TRM.
		 */
		for (i = 0, k = 128; i < 16; i++, k -= 8)
			tegra_plane_writel(plane, k, DC_WIN_V_FILTER_P(i));

		value |= V_FILTER;
	}

	tegra_plane_writel(plane, value, DC_WIN_WIN_OPTIONS);

	if (dc->soc->has_legacy_blending)
		tegra_plane_setup_blending_legacy(plane);
	else
		tegra_plane_setup_blending(plane, window);
}

static const u32 tegra20_primary_formats[] = {
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB8888,
	/* non-native formats */
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
};

static const u64 tegra20_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_NVIDIA_TEGRA_TILED,
	DRM_FORMAT_MOD_INVALID
};

static const u32 tegra114_primary_formats[] = {
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB8888,
	/* new on Tegra114 */
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
};

static const u32 tegra124_primary_formats[] = {
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB8888,
	/* new on Tegra114 */
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	/* new on Tegra124 */
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,
};

static const u64 tegra124_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(0),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(1),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(2),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(3),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(4),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(5),
	DRM_FORMAT_MOD_INVALID
};

static int tegra_plane_atomic_check(struct drm_plane *plane,
				    struct drm_plane_state *state)
{
	struct tegra_plane_state *plane_state = to_tegra_plane_state(state);
	unsigned int rotation = DRM_MODE_ROTATE_0 | DRM_MODE_REFLECT_Y;
	struct tegra_bo_tiling *tiling = &plane_state->tiling;
	struct tegra_plane *tegra = to_tegra_plane(plane);
	struct tegra_dc *dc = to_tegra_dc(state->crtc);
	int err;

	/* no need for further checks if the plane is being disabled */
	if (!state->crtc)
		return 0;

	err = tegra_plane_format(state->fb->format->format,
				 &plane_state->format,
				 &plane_state->swap);
	if (err < 0)
		return err;

	/*
	 * Tegra20 and Tegra30 are special cases here because they support
	 * only variants of specific formats with an alpha component, but not
	 * the corresponding opaque formats. However, the opaque formats can
	 * be emulated by disabling alpha blending for the plane.
	 */
	if (dc->soc->has_legacy_blending) {
		err = tegra_plane_setup_legacy_state(tegra, plane_state);
		if (err < 0)
			return err;
	}

	err = tegra_fb_get_tiling(state->fb, tiling);
	if (err < 0)
		return err;

	if (tiling->mode == TEGRA_BO_TILING_MODE_BLOCK &&
	    !dc->soc->supports_block_linear) {
		DRM_ERROR("hardware doesn't support block linear mode\n");
		return -EINVAL;
	}

	rotation = drm_rotation_simplify(state->rotation, rotation);

	if (rotation & DRM_MODE_REFLECT_Y)
		plane_state->bottom_up = true;
	else
		plane_state->bottom_up = false;

	/*
	 * Tegra doesn't support different strides for U and V planes so we
	 * error out if the user tries to display a framebuffer with such a
	 * configuration.
	 */
	if (state->fb->format->num_planes > 2) {
		if (state->fb->pitches[2] != state->fb->pitches[1]) {
			DRM_ERROR("unsupported UV-plane configuration\n");
			return -EINVAL;
		}
	}

	err = tegra_plane_state_add(tegra, state);
	if (err < 0)
		return err;

	return 0;
}

static void tegra_plane_atomic_disable(struct drm_plane *plane,
				       struct drm_plane_state *old_state)
{
	struct tegra_plane *p = to_tegra_plane(plane);
	u32 value;

	/* rien ne va plus */
	if (!old_state || !old_state->crtc)
		return;

	value = tegra_plane_readl(p, DC_WIN_WIN_OPTIONS);
	value &= ~WIN_ENABLE;
	tegra_plane_writel(p, value, DC_WIN_WIN_OPTIONS);
}

static void tegra_plane_atomic_update(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
	struct tegra_plane_state *state = to_tegra_plane_state(plane->state);
	struct drm_framebuffer *fb = plane->state->fb;
	struct tegra_plane *p = to_tegra_plane(plane);
	struct tegra_dc_window window;
	unsigned int i;

	/* rien ne va plus */
	if (!plane->state->crtc || !plane->state->fb)
		return;

	if (!plane->state->visible)
		return tegra_plane_atomic_disable(plane, old_state);

	memset(&window, 0, sizeof(window));
	window.src.x = plane->state->src.x1 >> 16;
	window.src.y = plane->state->src.y1 >> 16;
	window.src.w = drm_rect_width(&plane->state->src) >> 16;
	window.src.h = drm_rect_height(&plane->state->src) >> 16;
	window.dst.x = plane->state->dst.x1;
	window.dst.y = plane->state->dst.y1;
	window.dst.w = drm_rect_width(&plane->state->dst);
	window.dst.h = drm_rect_height(&plane->state->dst);
	window.bits_per_pixel = fb->format->cpp[0] * 8;
	window.bottom_up = tegra_fb_is_bottom_up(fb) || state->bottom_up;

	/* copy from state */
	window.zpos = plane->state->normalized_zpos;
	window.tiling = state->tiling;
	window.format = state->format;
	window.swap = state->swap;

	for (i = 0; i < fb->format->num_planes; i++) {
		window.base[i] = state->iova[i] + fb->offsets[i];

		/*
		 * Tegra uses a shared stride for UV planes. Framebuffers are
		 * already checked for this in the tegra_plane_atomic_check()
		 * function, so it's safe to ignore the V-plane pitch here.
		 */
		if (i < 2)
			window.stride[i] = fb->pitches[i];
	}

	tegra_dc_setup_window(p, &window);
}

static const struct drm_plane_helper_funcs tegra_plane_helper_funcs = {
	.prepare_fb = tegra_plane_prepare_fb,
	.cleanup_fb = tegra_plane_cleanup_fb,
	.atomic_check = tegra_plane_atomic_check,
	.atomic_disable = tegra_plane_atomic_disable,
	.atomic_update = tegra_plane_atomic_update,
};

static unsigned long tegra_plane_get_possible_crtcs(struct drm_device *drm)
{
	/*
	 * Ideally this would use drm_crtc_mask(), but that would require the
	 * CRTC to already be in the mode_config's list of CRTCs. However, it
	 * will only be added to that list in the drm_crtc_init_with_planes()
	 * (in tegra_dc_init()), which in turn requires registration of these
	 * planes. So we have ourselves a nice little chicken and egg problem
	 * here.
	 *
	 * We work around this by manually creating the mask from the number
	 * of CRTCs that have been registered, and should therefore always be
	 * the same as drm_crtc_index() after registration.
	 */
	return 1 << drm->mode_config.num_crtc;
}

static struct drm_plane *tegra_primary_plane_create(struct drm_device *drm,
						    struct tegra_dc *dc)
{
	unsigned long possible_crtcs = tegra_plane_get_possible_crtcs(drm);
	enum drm_plane_type type = DRM_PLANE_TYPE_PRIMARY;
	struct tegra_plane *plane;
	unsigned int num_formats;
	const u64 *modifiers;
	const u32 *formats;
	int err;

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	/* Always use window A as primary window */
	plane->offset = 0xa00;
	plane->index = 0;
	plane->dc = dc;

	num_formats = dc->soc->num_primary_formats;
	formats = dc->soc->primary_formats;
	modifiers = dc->soc->modifiers;

	err = drm_universal_plane_init(drm, &plane->base, possible_crtcs,
				       &tegra_plane_funcs, formats,
				       num_formats, modifiers, type, NULL);
	if (err < 0) {
		kfree(plane);
		return ERR_PTR(err);
	}

	drm_plane_helper_add(&plane->base, &tegra_plane_helper_funcs);
	drm_plane_create_zpos_property(&plane->base, plane->index, 0, 255);

	err = drm_plane_create_rotation_property(&plane->base,
						 DRM_MODE_ROTATE_0,
						 DRM_MODE_ROTATE_0 |
						 DRM_MODE_REFLECT_Y);
	if (err < 0)
		dev_err(dc->dev, "failed to create rotation property: %d\n",
			err);

	return &plane->base;
}

static const u32 tegra_cursor_plane_formats[] = {
	DRM_FORMAT_RGBA8888,
};

static int tegra_cursor_atomic_check(struct drm_plane *plane,
				     struct drm_plane_state *state)
{
	struct tegra_plane *tegra = to_tegra_plane(plane);
	int err;

	/* no need for further checks if the plane is being disabled */
	if (!state->crtc)
		return 0;

	/* scaling not supported for cursor */
	if ((state->src_w >> 16 != state->crtc_w) ||
	    (state->src_h >> 16 != state->crtc_h))
		return -EINVAL;

	/* only square cursors supported */
	if (state->src_w != state->src_h)
		return -EINVAL;

	if (state->crtc_w != 32 && state->crtc_w != 64 &&
	    state->crtc_w != 128 && state->crtc_w != 256)
		return -EINVAL;

	err = tegra_plane_state_add(tegra, state);
	if (err < 0)
		return err;

	return 0;
}

static void tegra_cursor_atomic_update(struct drm_plane *plane,
				       struct drm_plane_state *old_state)
{
	struct tegra_plane_state *state = to_tegra_plane_state(plane->state);
	struct tegra_dc *dc = to_tegra_dc(plane->state->crtc);
	u32 value = CURSOR_CLIP_DISPLAY;

	/* rien ne va plus */
	if (!plane->state->crtc || !plane->state->fb)
		return;

	switch (plane->state->crtc_w) {
	case 32:
		value |= CURSOR_SIZE_32x32;
		break;

	case 64:
		value |= CURSOR_SIZE_64x64;
		break;

	case 128:
		value |= CURSOR_SIZE_128x128;
		break;

	case 256:
		value |= CURSOR_SIZE_256x256;
		break;

	default:
		WARN(1, "cursor size %ux%u not supported\n",
		     plane->state->crtc_w, plane->state->crtc_h);
		return;
	}

	value |= (state->iova[0] >> 10) & 0x3fffff;
	tegra_dc_writel(dc, value, DC_DISP_CURSOR_START_ADDR);

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	value = (state->iova[0] >> 32) & 0x3;
	tegra_dc_writel(dc, value, DC_DISP_CURSOR_START_ADDR_HI);
#endif

	/* enable cursor and set blend mode */
	value = tegra_dc_readl(dc, DC_DISP_DISP_WIN_OPTIONS);
	value |= CURSOR_ENABLE;
	tegra_dc_writel(dc, value, DC_DISP_DISP_WIN_OPTIONS);

	value = tegra_dc_readl(dc, DC_DISP_BLEND_CURSOR_CONTROL);
	value &= ~CURSOR_DST_BLEND_MASK;
	value &= ~CURSOR_SRC_BLEND_MASK;
	value |= CURSOR_MODE_NORMAL;
	value |= CURSOR_DST_BLEND_NEG_K1_TIMES_SRC;
	value |= CURSOR_SRC_BLEND_K1_TIMES_SRC;
	value |= CURSOR_ALPHA;
	tegra_dc_writel(dc, value, DC_DISP_BLEND_CURSOR_CONTROL);

	/* position the cursor */
	value = (plane->state->crtc_y & 0x3fff) << 16 |
		(plane->state->crtc_x & 0x3fff);
	tegra_dc_writel(dc, value, DC_DISP_CURSOR_POSITION);
}

static void tegra_cursor_atomic_disable(struct drm_plane *plane,
					struct drm_plane_state *old_state)
{
	struct tegra_dc *dc;
	u32 value;

	/* rien ne va plus */
	if (!old_state || !old_state->crtc)
		return;

	dc = to_tegra_dc(old_state->crtc);

	value = tegra_dc_readl(dc, DC_DISP_DISP_WIN_OPTIONS);
	value &= ~CURSOR_ENABLE;
	tegra_dc_writel(dc, value, DC_DISP_DISP_WIN_OPTIONS);
}

static const struct drm_plane_helper_funcs tegra_cursor_plane_helper_funcs = {
	.prepare_fb = tegra_plane_prepare_fb,
	.cleanup_fb = tegra_plane_cleanup_fb,
	.atomic_check = tegra_cursor_atomic_check,
	.atomic_update = tegra_cursor_atomic_update,
	.atomic_disable = tegra_cursor_atomic_disable,
};

static struct drm_plane *tegra_dc_cursor_plane_create(struct drm_device *drm,
						      struct tegra_dc *dc)
{
	unsigned long possible_crtcs = tegra_plane_get_possible_crtcs(drm);
	struct tegra_plane *plane;
	unsigned int num_formats;
	const u32 *formats;
	int err;

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	/*
	 * This index is kind of fake. The cursor isn't a regular plane, but
	 * its update and activation request bits in DC_CMD_STATE_CONTROL do
	 * use the same programming. Setting this fake index here allows the
	 * code in tegra_add_plane_state() to do the right thing without the
	 * need to special-casing the cursor plane.
	 */
	plane->index = 6;
	plane->dc = dc;

	num_formats = ARRAY_SIZE(tegra_cursor_plane_formats);
	formats = tegra_cursor_plane_formats;

	err = drm_universal_plane_init(drm, &plane->base, possible_crtcs,
				       &tegra_plane_funcs, formats,
				       num_formats, NULL,
				       DRM_PLANE_TYPE_CURSOR, NULL);
	if (err < 0) {
		kfree(plane);
		return ERR_PTR(err);
	}

	drm_plane_helper_add(&plane->base, &tegra_cursor_plane_helper_funcs);
	drm_plane_create_zpos_immutable_property(&plane->base, 255);

	return &plane->base;
}

static const u32 tegra20_overlay_formats[] = {
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB8888,
	/* non-native formats */
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
	/* planar formats */
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YUV422,
};

static const u32 tegra114_overlay_formats[] = {
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB8888,
	/* new on Tegra114 */
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	/* planar formats */
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YUV422,
};

static const u32 tegra124_overlay_formats[] = {
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB8888,
	/* new on Tegra114 */
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	/* new on Tegra124 */
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,
	/* planar formats */
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YUV422,
};

static struct drm_plane *tegra_dc_overlay_plane_create(struct drm_device *drm,
						       struct tegra_dc *dc,
						       unsigned int index,
						       bool cursor)
{
	unsigned long possible_crtcs = tegra_plane_get_possible_crtcs(drm);
	struct tegra_plane *plane;
	unsigned int num_formats;
	enum drm_plane_type type;
	const u32 *formats;
	int err;

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	plane->offset = 0xa00 + 0x200 * index;
	plane->index = index;
	plane->dc = dc;

	num_formats = dc->soc->num_overlay_formats;
	formats = dc->soc->overlay_formats;

	if (!cursor)
		type = DRM_PLANE_TYPE_OVERLAY;
	else
		type = DRM_PLANE_TYPE_CURSOR;

	err = drm_universal_plane_init(drm, &plane->base, possible_crtcs,
				       &tegra_plane_funcs, formats,
				       num_formats, NULL, type, NULL);
	if (err < 0) {
		kfree(plane);
		return ERR_PTR(err);
	}

	drm_plane_helper_add(&plane->base, &tegra_plane_helper_funcs);
	drm_plane_create_zpos_property(&plane->base, plane->index, 0, 255);

	err = drm_plane_create_rotation_property(&plane->base,
						 DRM_MODE_ROTATE_0,
						 DRM_MODE_ROTATE_0 |
						 DRM_MODE_REFLECT_Y);
	if (err < 0)
		dev_err(dc->dev, "failed to create rotation property: %d\n",
			err);

	return &plane->base;
}

static struct drm_plane *tegra_dc_add_shared_planes(struct drm_device *drm,
						    struct tegra_dc *dc)
{
	struct drm_plane *plane, *primary = NULL;
	unsigned int i, j;

	for (i = 0; i < dc->soc->num_wgrps; i++) {
		const struct tegra_windowgroup_soc *wgrp = &dc->soc->wgrps[i];

		if (wgrp->dc == dc->pipe) {
			for (j = 0; j < wgrp->num_windows; j++) {
				unsigned int index = wgrp->windows[j];

				plane = tegra_shared_plane_create(drm, dc,
								  wgrp->index,
								  index);
				if (IS_ERR(plane))
					return plane;

				/*
				 * Choose the first shared plane owned by this
				 * head as the primary plane.
				 */
				if (!primary) {
					plane->type = DRM_PLANE_TYPE_PRIMARY;
					primary = plane;
				}
			}
		}
	}

	return primary;
}

static struct drm_plane *tegra_dc_add_planes(struct drm_device *drm,
					     struct tegra_dc *dc)
{
	struct drm_plane *planes[2], *primary;
	unsigned int planes_num;
	unsigned int i;
	int err;

	primary = tegra_primary_plane_create(drm, dc);
	if (IS_ERR(primary))
		return primary;

	if (dc->soc->supports_cursor)
		planes_num = 2;
	else
		planes_num = 1;

	for (i = 0; i < planes_num; i++) {
		planes[i] = tegra_dc_overlay_plane_create(drm, dc, 1 + i,
							  false);
		if (IS_ERR(planes[i])) {
			err = PTR_ERR(planes[i]);

			while (i--)
				tegra_plane_funcs.destroy(planes[i]);

			tegra_plane_funcs.destroy(primary);
			return ERR_PTR(err);
		}
	}

	return primary;
}

static void tegra_dc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
}

static void tegra_crtc_reset(struct drm_crtc *crtc)
{
	struct tegra_dc_state *state = kzalloc(sizeof(*state), GFP_KERNEL);

	if (crtc->state)
		tegra_crtc_atomic_destroy_state(crtc, crtc->state);

	__drm_atomic_helper_crtc_reset(crtc, &state->base);
}

static struct drm_crtc_state *
tegra_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct tegra_dc_state *state = to_dc_state(crtc->state);
	struct tegra_dc_state *copy;

	copy = kmalloc(sizeof(*copy), GFP_KERNEL);
	if (!copy)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &copy->base);
	copy->clk = state->clk;
	copy->pclk = state->pclk;
	copy->div = state->div;
	copy->planes = state->planes;

	return &copy->base;
}

static void tegra_crtc_atomic_destroy_state(struct drm_crtc *crtc,
					    struct drm_crtc_state *state)
{
	__drm_atomic_helper_crtc_destroy_state(state);
	kfree(state);
}

#define DEBUGFS_REG32(_name) { .name = #_name, .offset = _name }

static const struct debugfs_reg32 tegra_dc_regs[] = {
	DEBUGFS_REG32(DC_CMD_GENERAL_INCR_SYNCPT),
	DEBUGFS_REG32(DC_CMD_GENERAL_INCR_SYNCPT_CNTRL),
	DEBUGFS_REG32(DC_CMD_GENERAL_INCR_SYNCPT_ERROR),
	DEBUGFS_REG32(DC_CMD_WIN_A_INCR_SYNCPT),
	DEBUGFS_REG32(DC_CMD_WIN_A_INCR_SYNCPT_CNTRL),
	DEBUGFS_REG32(DC_CMD_WIN_A_INCR_SYNCPT_ERROR),
	DEBUGFS_REG32(DC_CMD_WIN_B_INCR_SYNCPT),
	DEBUGFS_REG32(DC_CMD_WIN_B_INCR_SYNCPT_CNTRL),
	DEBUGFS_REG32(DC_CMD_WIN_B_INCR_SYNCPT_ERROR),
	DEBUGFS_REG32(DC_CMD_WIN_C_INCR_SYNCPT),
	DEBUGFS_REG32(DC_CMD_WIN_C_INCR_SYNCPT_CNTRL),
	DEBUGFS_REG32(DC_CMD_WIN_C_INCR_SYNCPT_ERROR),
	DEBUGFS_REG32(DC_CMD_CONT_SYNCPT_VSYNC),
	DEBUGFS_REG32(DC_CMD_DISPLAY_COMMAND_OPTION0),
	DEBUGFS_REG32(DC_CMD_DISPLAY_COMMAND),
	DEBUGFS_REG32(DC_CMD_SIGNAL_RAISE),
	DEBUGFS_REG32(DC_CMD_DISPLAY_POWER_CONTROL),
	DEBUGFS_REG32(DC_CMD_INT_STATUS),
	DEBUGFS_REG32(DC_CMD_INT_MASK),
	DEBUGFS_REG32(DC_CMD_INT_ENABLE),
	DEBUGFS_REG32(DC_CMD_INT_TYPE),
	DEBUGFS_REG32(DC_CMD_INT_POLARITY),
	DEBUGFS_REG32(DC_CMD_SIGNAL_RAISE1),
	DEBUGFS_REG32(DC_CMD_SIGNAL_RAISE2),
	DEBUGFS_REG32(DC_CMD_SIGNAL_RAISE3),
	DEBUGFS_REG32(DC_CMD_STATE_ACCESS),
	DEBUGFS_REG32(DC_CMD_STATE_CONTROL),
	DEBUGFS_REG32(DC_CMD_DISPLAY_WINDOW_HEADER),
	DEBUGFS_REG32(DC_CMD_REG_ACT_CONTROL),
	DEBUGFS_REG32(DC_COM_CRC_CONTROL),
	DEBUGFS_REG32(DC_COM_CRC_CHECKSUM),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_ENABLE(0)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_ENABLE(1)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_ENABLE(2)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_ENABLE(3)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_POLARITY(0)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_POLARITY(1)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_POLARITY(2)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_POLARITY(3)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_DATA(0)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_DATA(1)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_DATA(2)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_DATA(3)),
	DEBUGFS_REG32(DC_COM_PIN_INPUT_ENABLE(0)),
	DEBUGFS_REG32(DC_COM_PIN_INPUT_ENABLE(1)),
	DEBUGFS_REG32(DC_COM_PIN_INPUT_ENABLE(2)),
	DEBUGFS_REG32(DC_COM_PIN_INPUT_ENABLE(3)),
	DEBUGFS_REG32(DC_COM_PIN_INPUT_DATA(0)),
	DEBUGFS_REG32(DC_COM_PIN_INPUT_DATA(1)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_SELECT(0)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_SELECT(1)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_SELECT(2)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_SELECT(3)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_SELECT(4)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_SELECT(5)),
	DEBUGFS_REG32(DC_COM_PIN_OUTPUT_SELECT(6)),
	DEBUGFS_REG32(DC_COM_PIN_MISC_CONTROL),
	DEBUGFS_REG32(DC_COM_PIN_PM0_CONTROL),
	DEBUGFS_REG32(DC_COM_PIN_PM0_DUTY_CYCLE),
	DEBUGFS_REG32(DC_COM_PIN_PM1_CONTROL),
	DEBUGFS_REG32(DC_COM_PIN_PM1_DUTY_CYCLE),
	DEBUGFS_REG32(DC_COM_SPI_CONTROL),
	DEBUGFS_REG32(DC_COM_SPI_START_BYTE),
	DEBUGFS_REG32(DC_COM_HSPI_WRITE_DATA_AB),
	DEBUGFS_REG32(DC_COM_HSPI_WRITE_DATA_CD),
	DEBUGFS_REG32(DC_COM_HSPI_CS_DC),
	DEBUGFS_REG32(DC_COM_SCRATCH_REGISTER_A),
	DEBUGFS_REG32(DC_COM_SCRATCH_REGISTER_B),
	DEBUGFS_REG32(DC_COM_GPIO_CTRL),
	DEBUGFS_REG32(DC_COM_GPIO_DEBOUNCE_COUNTER),
	DEBUGFS_REG32(DC_COM_CRC_CHECKSUM_LATCHED),
	DEBUGFS_REG32(DC_DISP_DISP_SIGNAL_OPTIONS0),
	DEBUGFS_REG32(DC_DISP_DISP_SIGNAL_OPTIONS1),
	DEBUGFS_REG32(DC_DISP_DISP_WIN_OPTIONS),
	DEBUGFS_REG32(DC_DISP_DISP_MEM_HIGH_PRIORITY),
	DEBUGFS_REG32(DC_DISP_DISP_MEM_HIGH_PRIORITY_TIMER),
	DEBUGFS_REG32(DC_DISP_DISP_TIMING_OPTIONS),
	DEBUGFS_REG32(DC_DISP_REF_TO_SYNC),
	DEBUGFS_REG32(DC_DISP_SYNC_WIDTH),
	DEBUGFS_REG32(DC_DISP_BACK_PORCH),
	DEBUGFS_REG32(DC_DISP_ACTIVE),
	DEBUGFS_REG32(DC_DISP_FRONT_PORCH),
	DEBUGFS_REG32(DC_DISP_H_PULSE0_CONTROL),
	DEBUGFS_REG32(DC_DISP_H_PULSE0_POSITION_A),
	DEBUGFS_REG32(DC_DISP_H_PULSE0_POSITION_B),
	DEBUGFS_REG32(DC_DISP_H_PULSE0_POSITION_C),
	DEBUGFS_REG32(DC_DISP_H_PULSE0_POSITION_D),
	DEBUGFS_REG32(DC_DISP_H_PULSE1_CONTROL),
	DEBUGFS_REG32(DC_DISP_H_PULSE1_POSITION_A),
	DEBUGFS_REG32(DC_DISP_H_PULSE1_POSITION_B),
	DEBUGFS_REG32(DC_DISP_H_PULSE1_POSITION_C),
	DEBUGFS_REG32(DC_DISP_H_PULSE1_POSITION_D),
	DEBUGFS_REG32(DC_DISP_H_PULSE2_CONTROL),
	DEBUGFS_REG32(DC_DISP_H_PULSE2_POSITION_A),
	DEBUGFS_REG32(DC_DISP_H_PULSE2_POSITION_B),
	DEBUGFS_REG32(DC_DISP_H_PULSE2_POSITION_C),
	DEBUGFS_REG32(DC_DISP_H_PULSE2_POSITION_D),
	DEBUGFS_REG32(DC_DISP_V_PULSE0_CONTROL),
	DEBUGFS_REG32(DC_DISP_V_PULSE0_POSITION_A),
	DEBUGFS_REG32(DC_DISP_V_PULSE0_POSITION_B),
	DEBUGFS_REG32(DC_DISP_V_PULSE0_POSITION_C),
	DEBUGFS_REG32(DC_DISP_V_PULSE1_CONTROL),
	DEBUGFS_REG32(DC_DISP_V_PULSE1_POSITION_A),
	DEBUGFS_REG32(DC_DISP_V_PULSE1_POSITION_B),
	DEBUGFS_REG32(DC_DISP_V_PULSE1_POSITION_C),
	DEBUGFS_REG32(DC_DISP_V_PULSE2_CONTROL),
	DEBUGFS_REG32(DC_DISP_V_PULSE2_POSITION_A),
	DEBUGFS_REG32(DC_DISP_V_PULSE3_CONTROL),
	DEBUGFS_REG32(DC_DISP_V_PULSE3_POSITION_A),
	DEBUGFS_REG32(DC_DISP_M0_CONTROL),
	DEBUGFS_REG32(DC_DISP_M1_CONTROL),
	DEBUGFS_REG32(DC_DISP_DI_CONTROL),
	DEBUGFS_REG32(DC_DISP_PP_CONTROL),
	DEBUGFS_REG32(DC_DISP_PP_SELECT_A),
	DEBUGFS_REG32(DC_DISP_PP_SELECT_B),
	DEBUGFS_REG32(DC_DISP_PP_SELECT_C),
	DEBUGFS_REG32(DC_DISP_PP_SELECT_D),
	DEBUGFS_REG32(DC_DISP_DISP_CLOCK_CONTROL),
	DEBUGFS_REG32(DC_DISP_DISP_INTERFACE_CONTROL),
	DEBUGFS_REG32(DC_DISP_DISP_COLOR_CONTROL),
	DEBUGFS_REG32(DC_DISP_SHIFT_CLOCK_OPTIONS),
	DEBUGFS_REG32(DC_DISP_DATA_ENABLE_OPTIONS),
	DEBUGFS_REG32(DC_DISP_SERIAL_INTERFACE_OPTIONS),
	DEBUGFS_REG32(DC_DISP_LCD_SPI_OPTIONS),
	DEBUGFS_REG32(DC_DISP_BORDER_COLOR),
	DEBUGFS_REG32(DC_DISP_COLOR_KEY0_LOWER),
	DEBUGFS_REG32(DC_DISP_COLOR_KEY0_UPPER),
	DEBUGFS_REG32(DC_DISP_COLOR_KEY1_LOWER),
	DEBUGFS_REG32(DC_DISP_COLOR_KEY1_UPPER),
	DEBUGFS_REG32(DC_DISP_CURSOR_FOREGROUND),
	DEBUGFS_REG32(DC_DISP_CURSOR_BACKGROUND),
	DEBUGFS_REG32(DC_DISP_CURSOR_START_ADDR),
	DEBUGFS_REG32(DC_DISP_CURSOR_START_ADDR_NS),
	DEBUGFS_REG32(DC_DISP_CURSOR_POSITION),
	DEBUGFS_REG32(DC_DISP_CURSOR_POSITION_NS),
	DEBUGFS_REG32(DC_DISP_INIT_SEQ_CONTROL),
	DEBUGFS_REG32(DC_DISP_SPI_INIT_SEQ_DATA_A),
	DEBUGFS_REG32(DC_DISP_SPI_INIT_SEQ_DATA_B),
	DEBUGFS_REG32(DC_DISP_SPI_INIT_SEQ_DATA_C),
	DEBUGFS_REG32(DC_DISP_SPI_INIT_SEQ_DATA_D),
	DEBUGFS_REG32(DC_DISP_DC_MCCIF_FIFOCTRL),
	DEBUGFS_REG32(DC_DISP_MCCIF_DISPLAY0A_HYST),
	DEBUGFS_REG32(DC_DISP_MCCIF_DISPLAY0B_HYST),
	DEBUGFS_REG32(DC_DISP_MCCIF_DISPLAY1A_HYST),
	DEBUGFS_REG32(DC_DISP_MCCIF_DISPLAY1B_HYST),
	DEBUGFS_REG32(DC_DISP_DAC_CRT_CTRL),
	DEBUGFS_REG32(DC_DISP_DISP_MISC_CONTROL),
	DEBUGFS_REG32(DC_DISP_SD_CONTROL),
	DEBUGFS_REG32(DC_DISP_SD_CSC_COEFF),
	DEBUGFS_REG32(DC_DISP_SD_LUT(0)),
	DEBUGFS_REG32(DC_DISP_SD_LUT(1)),
	DEBUGFS_REG32(DC_DISP_SD_LUT(2)),
	DEBUGFS_REG32(DC_DISP_SD_LUT(3)),
	DEBUGFS_REG32(DC_DISP_SD_LUT(4)),
	DEBUGFS_REG32(DC_DISP_SD_LUT(5)),
	DEBUGFS_REG32(DC_DISP_SD_LUT(6)),
	DEBUGFS_REG32(DC_DISP_SD_LUT(7)),
	DEBUGFS_REG32(DC_DISP_SD_LUT(8)),
	DEBUGFS_REG32(DC_DISP_SD_FLICKER_CONTROL),
	DEBUGFS_REG32(DC_DISP_DC_PIXEL_COUNT),
	DEBUGFS_REG32(DC_DISP_SD_HISTOGRAM(0)),
	DEBUGFS_REG32(DC_DISP_SD_HISTOGRAM(1)),
	DEBUGFS_REG32(DC_DISP_SD_HISTOGRAM(2)),
	DEBUGFS_REG32(DC_DISP_SD_HISTOGRAM(3)),
	DEBUGFS_REG32(DC_DISP_SD_HISTOGRAM(4)),
	DEBUGFS_REG32(DC_DISP_SD_HISTOGRAM(5)),
	DEBUGFS_REG32(DC_DISP_SD_HISTOGRAM(6)),
	DEBUGFS_REG32(DC_DISP_SD_HISTOGRAM(7)),
	DEBUGFS_REG32(DC_DISP_SD_BL_TF(0)),
	DEBUGFS_REG32(DC_DISP_SD_BL_TF(1)),
	DEBUGFS_REG32(DC_DISP_SD_BL_TF(2)),
	DEBUGFS_REG32(DC_DISP_SD_BL_TF(3)),
	DEBUGFS_REG32(DC_DISP_SD_BL_CONTROL),
	DEBUGFS_REG32(DC_DISP_SD_HW_K_VALUES),
	DEBUGFS_REG32(DC_DISP_SD_MAN_K_VALUES),
	DEBUGFS_REG32(DC_DISP_CURSOR_START_ADDR_HI),
	DEBUGFS_REG32(DC_DISP_BLEND_CURSOR_CONTROL),
	DEBUGFS_REG32(DC_WIN_WIN_OPTIONS),
	DEBUGFS_REG32(DC_WIN_BYTE_SWAP),
	DEBUGFS_REG32(DC_WIN_BUFFER_CONTROL),
	DEBUGFS_REG32(DC_WIN_COLOR_DEPTH),
	DEBUGFS_REG32(DC_WIN_POSITION),
	DEBUGFS_REG32(DC_WIN_SIZE),
	DEBUGFS_REG32(DC_WIN_PRESCALED_SIZE),
	DEBUGFS_REG32(DC_WIN_H_INITIAL_DDA),
	DEBUGFS_REG32(DC_WIN_V_INITIAL_DDA),
	DEBUGFS_REG32(DC_WIN_DDA_INC),
	DEBUGFS_REG32(DC_WIN_LINE_STRIDE),
	DEBUGFS_REG32(DC_WIN_BUF_STRIDE),
	DEBUGFS_REG32(DC_WIN_UV_BUF_STRIDE),
	DEBUGFS_REG32(DC_WIN_BUFFER_ADDR_MODE),
	DEBUGFS_REG32(DC_WIN_DV_CONTROL),
	DEBUGFS_REG32(DC_WIN_BLEND_NOKEY),
	DEBUGFS_REG32(DC_WIN_BLEND_1WIN),
	DEBUGFS_REG32(DC_WIN_BLEND_2WIN_X),
	DEBUGFS_REG32(DC_WIN_BLEND_2WIN_Y),
	DEBUGFS_REG32(DC_WIN_BLEND_3WIN_XY),
	DEBUGFS_REG32(DC_WIN_HP_FETCH_CONTROL),
	DEBUGFS_REG32(DC_WINBUF_START_ADDR),
	DEBUGFS_REG32(DC_WINBUF_START_ADDR_NS),
	DEBUGFS_REG32(DC_WINBUF_START_ADDR_U),
	DEBUGFS_REG32(DC_WINBUF_START_ADDR_U_NS),
	DEBUGFS_REG32(DC_WINBUF_START_ADDR_V),
	DEBUGFS_REG32(DC_WINBUF_START_ADDR_V_NS),
	DEBUGFS_REG32(DC_WINBUF_ADDR_H_OFFSET),
	DEBUGFS_REG32(DC_WINBUF_ADDR_H_OFFSET_NS),
	DEBUGFS_REG32(DC_WINBUF_ADDR_V_OFFSET),
	DEBUGFS_REG32(DC_WINBUF_ADDR_V_OFFSET_NS),
	DEBUGFS_REG32(DC_WINBUF_UFLOW_STATUS),
	DEBUGFS_REG32(DC_WINBUF_AD_UFLOW_STATUS),
	DEBUGFS_REG32(DC_WINBUF_BD_UFLOW_STATUS),
	DEBUGFS_REG32(DC_WINBUF_CD_UFLOW_STATUS),
};

static int tegra_dc_show_regs(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct tegra_dc *dc = node->info_ent->data;
	unsigned int i;
	int err = 0;

	drm_modeset_lock(&dc->base.mutex, NULL);

	if (!dc->base.state->active) {
		err = -EBUSY;
		goto unlock;
	}

	for (i = 0; i < ARRAY_SIZE(tegra_dc_regs); i++) {
		unsigned int offset = tegra_dc_regs[i].offset;

		seq_printf(s, "%-40s %#05x %08x\n", tegra_dc_regs[i].name,
			   offset, tegra_dc_readl(dc, offset));
	}

unlock:
	drm_modeset_unlock(&dc->base.mutex);
	return err;
}

static int tegra_dc_show_crc(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct tegra_dc *dc = node->info_ent->data;
	int err = 0;
	u32 value;

	drm_modeset_lock(&dc->base.mutex, NULL);

	if (!dc->base.state->active) {
		err = -EBUSY;
		goto unlock;
	}

	value = DC_COM_CRC_CONTROL_ACTIVE_DATA | DC_COM_CRC_CONTROL_ENABLE;
	tegra_dc_writel(dc, value, DC_COM_CRC_CONTROL);
	tegra_dc_commit(dc);

	drm_crtc_wait_one_vblank(&dc->base);
	drm_crtc_wait_one_vblank(&dc->base);

	value = tegra_dc_readl(dc, DC_COM_CRC_CHECKSUM);
	seq_printf(s, "%08x\n", value);

	tegra_dc_writel(dc, 0, DC_COM_CRC_CONTROL);

unlock:
	drm_modeset_unlock(&dc->base.mutex);
	return err;
}

static int tegra_dc_show_stats(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct tegra_dc *dc = node->info_ent->data;

	seq_printf(s, "frames: %lu\n", dc->stats.frames);
	seq_printf(s, "vblank: %lu\n", dc->stats.vblank);
	seq_printf(s, "underflow: %lu\n", dc->stats.underflow);
	seq_printf(s, "overflow: %lu\n", dc->stats.overflow);

	return 0;
}

static struct drm_info_list debugfs_files[] = {
	{ "regs", tegra_dc_show_regs, 0, NULL },
	{ "crc", tegra_dc_show_crc, 0, NULL },
	{ "stats", tegra_dc_show_stats, 0, NULL },
};

static int tegra_dc_late_register(struct drm_crtc *crtc)
{
	unsigned int i, count = ARRAY_SIZE(debugfs_files);
	struct drm_minor *minor = crtc->dev->primary;
	struct dentry *root;
	struct tegra_dc *dc = to_tegra_dc(crtc);

#ifdef CONFIG_DEBUG_FS
	root = crtc->debugfs_entry;
#else
	root = NULL;
#endif

	dc->debugfs_files = kmemdup(debugfs_files, sizeof(debugfs_files),
				    GFP_KERNEL);
	if (!dc->debugfs_files)
		return -ENOMEM;

	for (i = 0; i < count; i++)
		dc->debugfs_files[i].data = dc;

	drm_debugfs_create_files(dc->debugfs_files, count, root, minor);

	return 0;
}

static void tegra_dc_early_unregister(struct drm_crtc *crtc)
{
	unsigned int count = ARRAY_SIZE(debugfs_files);
	struct drm_minor *minor = crtc->dev->primary;
	struct tegra_dc *dc = to_tegra_dc(crtc);

	drm_debugfs_remove_files(dc->debugfs_files, count, minor);
	kfree(dc->debugfs_files);
	dc->debugfs_files = NULL;
}

static u32 tegra_dc_get_vblank_counter(struct drm_crtc *crtc)
{
	struct tegra_dc *dc = to_tegra_dc(crtc);

	/* XXX vblank syncpoints don't work with nvdisplay yet */
	if (dc->syncpt && !dc->soc->has_nvdisplay)
		return host1x_syncpt_read(dc->syncpt);

	/* fallback to software emulated VBLANK counter */
	return (u32)drm_crtc_vblank_count(&dc->base);
}

static int tegra_dc_enable_vblank(struct drm_crtc *crtc)
{
	struct tegra_dc *dc = to_tegra_dc(crtc);
	u32 value;

	value = tegra_dc_readl(dc, DC_CMD_INT_MASK);
	value |= VBLANK_INT;
	tegra_dc_writel(dc, value, DC_CMD_INT_MASK);

	return 0;
}

static void tegra_dc_disable_vblank(struct drm_crtc *crtc)
{
	struct tegra_dc *dc = to_tegra_dc(crtc);
	u32 value;

	value = tegra_dc_readl(dc, DC_CMD_INT_MASK);
	value &= ~VBLANK_INT;
	tegra_dc_writel(dc, value, DC_CMD_INT_MASK);
}

static const struct drm_crtc_funcs tegra_crtc_funcs = {
	.page_flip = drm_atomic_helper_page_flip,
	.set_config = drm_atomic_helper_set_config,
	.destroy = tegra_dc_destroy,
	.reset = tegra_crtc_reset,
	.atomic_duplicate_state = tegra_crtc_atomic_duplicate_state,
	.atomic_destroy_state = tegra_crtc_atomic_destroy_state,
	.late_register = tegra_dc_late_register,
	.early_unregister = tegra_dc_early_unregister,
	.get_vblank_counter = tegra_dc_get_vblank_counter,
	.enable_vblank = tegra_dc_enable_vblank,
	.disable_vblank = tegra_dc_disable_vblank,
};

static int tegra_dc_set_timings(struct tegra_dc *dc,
				struct drm_display_mode *mode)
{
	unsigned int h_ref_to_sync = 1;
	unsigned int v_ref_to_sync = 1;
	unsigned long value;

	if (!dc->soc->has_nvdisplay) {
		tegra_dc_writel(dc, 0x0, DC_DISP_DISP_TIMING_OPTIONS);

		value = (v_ref_to_sync << 16) | h_ref_to_sync;
		tegra_dc_writel(dc, value, DC_DISP_REF_TO_SYNC);
	}

	value = ((mode->vsync_end - mode->vsync_start) << 16) |
		((mode->hsync_end - mode->hsync_start) <<  0);
	tegra_dc_writel(dc, value, DC_DISP_SYNC_WIDTH);

	value = ((mode->vtotal - mode->vsync_end) << 16) |
		((mode->htotal - mode->hsync_end) <<  0);
	tegra_dc_writel(dc, value, DC_DISP_BACK_PORCH);

	value = ((mode->vsync_start - mode->vdisplay) << 16) |
		((mode->hsync_start - mode->hdisplay) <<  0);
	tegra_dc_writel(dc, value, DC_DISP_FRONT_PORCH);

	value = (mode->vdisplay << 16) | mode->hdisplay;
	tegra_dc_writel(dc, value, DC_DISP_ACTIVE);

	return 0;
}

/**
 * tegra_dc_state_setup_clock - check clock settings and store them in atomic
 *     state
 * @dc: display controller
 * @crtc_state: CRTC atomic state
 * @clk: parent clock for display controller
 * @pclk: pixel clock
 * @div: shift clock divider
 *
 * Returns:
 * 0 on success or a negative error-code on failure.
 */
int tegra_dc_state_setup_clock(struct tegra_dc *dc,
			       struct drm_crtc_state *crtc_state,
			       struct clk *clk, unsigned long pclk,
			       unsigned int div)
{
	struct tegra_dc_state *state = to_dc_state(crtc_state);

	if (!clk_has_parent(dc->clk, clk))
		return -EINVAL;

	state->clk = clk;
	state->pclk = pclk;
	state->div = div;

	return 0;
}

static void tegra_dc_commit_state(struct tegra_dc *dc,
				  struct tegra_dc_state *state)
{
	u32 value;
	int err;

	err = clk_set_parent(dc->clk, state->clk);
	if (err < 0)
		dev_err(dc->dev, "failed to set parent clock: %d\n", err);

	/*
	 * Outputs may not want to change the parent clock rate. This is only
	 * relevant to Tegra20 where only a single display PLL is available.
	 * Since that PLL would typically be used for HDMI, an internal LVDS
	 * panel would need to be driven by some other clock such as PLL_P
	 * which is shared with other peripherals. Changing the clock rate
	 * should therefore be avoided.
	 */
	if (state->pclk > 0) {
		err = clk_set_rate(state->clk, state->pclk);
		if (err < 0)
			dev_err(dc->dev,
				"failed to set clock rate to %lu Hz\n",
				state->pclk);
	}

	DRM_DEBUG_KMS("rate: %lu, div: %u\n", clk_get_rate(dc->clk),
		      state->div);
	DRM_DEBUG_KMS("pclk: %lu\n", state->pclk);

	if (!dc->soc->has_nvdisplay) {
		value = SHIFT_CLK_DIVIDER(state->div) | PIXEL_CLK_DIVIDER_PCD1;
		tegra_dc_writel(dc, value, DC_DISP_DISP_CLOCK_CONTROL);
	}

	err = clk_set_rate(dc->clk, state->pclk);
	if (err < 0)
		dev_err(dc->dev, "failed to set clock %pC to %lu Hz: %d\n",
			dc->clk, state->pclk, err);
}

static void tegra_dc_stop(struct tegra_dc *dc)
{
	u32 value;

	/* stop the display controller */
	value = tegra_dc_readl(dc, DC_CMD_DISPLAY_COMMAND);
	value &= ~DISP_CTRL_MODE_MASK;
	tegra_dc_writel(dc, value, DC_CMD_DISPLAY_COMMAND);

	tegra_dc_commit(dc);
}

static bool tegra_dc_idle(struct tegra_dc *dc)
{
	u32 value;

	value = tegra_dc_readl_active(dc, DC_CMD_DISPLAY_COMMAND);

	return (value & DISP_CTRL_MODE_MASK) == 0;
}

static int tegra_dc_wait_idle(struct tegra_dc *dc, unsigned long timeout)
{
	timeout = jiffies + msecs_to_jiffies(timeout);

	while (time_before(jiffies, timeout)) {
		if (tegra_dc_idle(dc))
			return 0;

		usleep_range(1000, 2000);
	}

	dev_dbg(dc->dev, "timeout waiting for DC to become idle\n");
	return -ETIMEDOUT;
}

static void tegra_crtc_atomic_disable(struct drm_crtc *crtc,
				      struct drm_crtc_state *old_state)
{
	struct tegra_dc *dc = to_tegra_dc(crtc);
	u32 value;
	int err;

	if (!tegra_dc_idle(dc)) {
		tegra_dc_stop(dc);

		/*
		 * Ignore the return value, there isn't anything useful to do
		 * in case this fails.
		 */
		tegra_dc_wait_idle(dc, 100);
	}

	/*
	 * This should really be part of the RGB encoder driver, but clearing
	 * these bits has the side-effect of stopping the display controller.
	 * When that happens no VBLANK interrupts will be raised. At the same
	 * time the encoder is disabled before the display controller, so the
	 * above code is always going to timeout waiting for the controller
	 * to go idle.
	 *
	 * Given the close coupling between the RGB encoder and the display
	 * controller doing it here is still kind of okay. None of the other
	 * encoder drivers require these bits to be cleared.
	 *
	 * XXX: Perhaps given that the display controller is switched off at
	 * this point anyway maybe clearing these bits isn't even useful for
	 * the RGB encoder?
	 */
	if (dc->rgb) {
		value = tegra_dc_readl(dc, DC_CMD_DISPLAY_POWER_CONTROL);
		value &= ~(PW0_ENABLE | PW1_ENABLE | PW2_ENABLE | PW3_ENABLE |
			   PW4_ENABLE | PM0_ENABLE | PM1_ENABLE);
		tegra_dc_writel(dc, value, DC_CMD_DISPLAY_POWER_CONTROL);
	}

	tegra_dc_stats_reset(&dc->stats);
	drm_crtc_vblank_off(crtc);

	spin_lock_irq(&crtc->dev->event_lock);

	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}

	spin_unlock_irq(&crtc->dev->event_lock);

	err = host1x_client_suspend(&dc->client);
	if (err < 0)
		dev_err(dc->dev, "failed to suspend: %d\n", err);
}

static void tegra_crtc_atomic_enable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_state)
{
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct tegra_dc_state *state = to_dc_state(crtc->state);
	struct tegra_dc *dc = to_tegra_dc(crtc);
	u32 value;
	int err;

	err = host1x_client_resume(&dc->client);
	if (err < 0) {
		dev_err(dc->dev, "failed to resume: %d\n", err);
		return;
	}

	/* initialize display controller */
	if (dc->syncpt) {
		u32 syncpt = host1x_syncpt_id(dc->syncpt), enable;

		if (dc->soc->has_nvdisplay)
			enable = 1 << 31;
		else
			enable = 1 << 8;

		value = SYNCPT_CNTRL_NO_STALL;
		tegra_dc_writel(dc, value, DC_CMD_GENERAL_INCR_SYNCPT_CNTRL);

		value = enable | syncpt;
		tegra_dc_writel(dc, value, DC_CMD_CONT_SYNCPT_VSYNC);
	}

	if (dc->soc->has_nvdisplay) {
		value = DSC_TO_UF_INT | DSC_BBUF_UF_INT | DSC_RBUF_UF_INT |
			DSC_OBUF_UF_INT;
		tegra_dc_writel(dc, value, DC_CMD_INT_TYPE);

		value = DSC_TO_UF_INT | DSC_BBUF_UF_INT | DSC_RBUF_UF_INT |
			DSC_OBUF_UF_INT | SD3_BUCKET_WALK_DONE_INT |
			HEAD_UF_INT | MSF_INT | REG_TMOUT_INT |
			REGION_CRC_INT | V_PULSE2_INT | V_PULSE3_INT |
			VBLANK_INT | FRAME_END_INT;
		tegra_dc_writel(dc, value, DC_CMD_INT_POLARITY);

		value = SD3_BUCKET_WALK_DONE_INT | HEAD_UF_INT | VBLANK_INT |
			FRAME_END_INT;
		tegra_dc_writel(dc, value, DC_CMD_INT_ENABLE);

		value = HEAD_UF_INT | REG_TMOUT_INT | FRAME_END_INT;
		tegra_dc_writel(dc, value, DC_CMD_INT_MASK);

		tegra_dc_writel(dc, READ_MUX, DC_CMD_STATE_ACCESS);
	} else {
		value = WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT |
			WIN_A_OF_INT | WIN_B_OF_INT | WIN_C_OF_INT;
		tegra_dc_writel(dc, value, DC_CMD_INT_TYPE);

		value = WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT |
			WIN_A_OF_INT | WIN_B_OF_INT | WIN_C_OF_INT;
		tegra_dc_writel(dc, value, DC_CMD_INT_POLARITY);

		/* initialize timer */
		value = CURSOR_THRESHOLD(0) | WINDOW_A_THRESHOLD(0x20) |
			WINDOW_B_THRESHOLD(0x20) | WINDOW_C_THRESHOLD(0x20);
		tegra_dc_writel(dc, value, DC_DISP_DISP_MEM_HIGH_PRIORITY);

		value = CURSOR_THRESHOLD(0) | WINDOW_A_THRESHOLD(1) |
			WINDOW_B_THRESHOLD(1) | WINDOW_C_THRESHOLD(1);
		tegra_dc_writel(dc, value, DC_DISP_DISP_MEM_HIGH_PRIORITY_TIMER);

		value = VBLANK_INT | WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT |
			WIN_A_OF_INT | WIN_B_OF_INT | WIN_C_OF_INT;
		tegra_dc_writel(dc, value, DC_CMD_INT_ENABLE);

		value = WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT |
			WIN_A_OF_INT | WIN_B_OF_INT | WIN_C_OF_INT;
		tegra_dc_writel(dc, value, DC_CMD_INT_MASK);
	}

	if (dc->soc->supports_background_color)
		tegra_dc_writel(dc, 0, DC_DISP_BLEND_BACKGROUND_COLOR);
	else
		tegra_dc_writel(dc, 0, DC_DISP_BORDER_COLOR);

	/* apply PLL and pixel clock changes */
	tegra_dc_commit_state(dc, state);

	/* program display mode */
	tegra_dc_set_timings(dc, mode);

	/* interlacing isn't supported yet, so disable it */
	if (dc->soc->supports_interlacing) {
		value = tegra_dc_readl(dc, DC_DISP_INTERLACE_CONTROL);
		value &= ~INTERLACE_ENABLE;
		tegra_dc_writel(dc, value, DC_DISP_INTERLACE_CONTROL);
	}

	value = tegra_dc_readl(dc, DC_CMD_DISPLAY_COMMAND);
	value &= ~DISP_CTRL_MODE_MASK;
	value |= DISP_CTRL_MODE_C_DISPLAY;
	tegra_dc_writel(dc, value, DC_CMD_DISPLAY_COMMAND);

	if (!dc->soc->has_nvdisplay) {
		value = tegra_dc_readl(dc, DC_CMD_DISPLAY_POWER_CONTROL);
		value |= PW0_ENABLE | PW1_ENABLE | PW2_ENABLE | PW3_ENABLE |
			 PW4_ENABLE | PM0_ENABLE | PM1_ENABLE;
		tegra_dc_writel(dc, value, DC_CMD_DISPLAY_POWER_CONTROL);
	}

	/* enable underflow reporting and display red for missing pixels */
	if (dc->soc->has_nvdisplay) {
		value = UNDERFLOW_MODE_RED | UNDERFLOW_REPORT_ENABLE;
		tegra_dc_writel(dc, value, DC_COM_RG_UNDERFLOW);
	}

	tegra_dc_commit(dc);

	drm_crtc_vblank_on(crtc);
}

static void tegra_crtc_atomic_begin(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_crtc_state)
{
	unsigned long flags;

	if (crtc->state->event) {
		spin_lock_irqsave(&crtc->dev->event_lock, flags);

		if (drm_crtc_vblank_get(crtc) != 0)
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
		else
			drm_crtc_arm_vblank_event(crtc, crtc->state->event);

		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

		crtc->state->event = NULL;
	}
}

static void tegra_crtc_atomic_flush(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_crtc_state)
{
	struct tegra_dc_state *state = to_dc_state(crtc->state);
	struct tegra_dc *dc = to_tegra_dc(crtc);
	u32 value;

	value = state->planes << 8 | GENERAL_UPDATE;
	tegra_dc_writel(dc, value, DC_CMD_STATE_CONTROL);
	value = tegra_dc_readl(dc, DC_CMD_STATE_CONTROL);

	value = state->planes | GENERAL_ACT_REQ;
	tegra_dc_writel(dc, value, DC_CMD_STATE_CONTROL);
	value = tegra_dc_readl(dc, DC_CMD_STATE_CONTROL);
}

static const struct drm_crtc_helper_funcs tegra_crtc_helper_funcs = {
	.atomic_begin = tegra_crtc_atomic_begin,
	.atomic_flush = tegra_crtc_atomic_flush,
	.atomic_enable = tegra_crtc_atomic_enable,
	.atomic_disable = tegra_crtc_atomic_disable,
};

static irqreturn_t tegra_dc_irq(int irq, void *data)
{
	struct tegra_dc *dc = data;
	unsigned long status;

	status = tegra_dc_readl(dc, DC_CMD_INT_STATUS);
	tegra_dc_writel(dc, status, DC_CMD_INT_STATUS);

	if (status & FRAME_END_INT) {
		/*
		dev_dbg(dc->dev, "%s(): frame end\n", __func__);
		*/
		dc->stats.frames++;
	}

	if (status & VBLANK_INT) {
		/*
		dev_dbg(dc->dev, "%s(): vertical blank\n", __func__);
		*/
		drm_crtc_handle_vblank(&dc->base);
		dc->stats.vblank++;
	}

	if (status & (WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT)) {
		/*
		dev_dbg(dc->dev, "%s(): underflow\n", __func__);
		*/
		dc->stats.underflow++;
	}

	if (status & (WIN_A_OF_INT | WIN_B_OF_INT | WIN_C_OF_INT)) {
		/*
		dev_dbg(dc->dev, "%s(): overflow\n", __func__);
		*/
		dc->stats.overflow++;
	}

	if (status & HEAD_UF_INT) {
		dev_dbg_ratelimited(dc->dev, "%s(): head underflow\n", __func__);
		dc->stats.underflow++;
	}

	return IRQ_HANDLED;
}

static bool tegra_dc_has_window_groups(struct tegra_dc *dc)
{
	unsigned int i;

	if (!dc->soc->wgrps)
		return true;

	for (i = 0; i < dc->soc->num_wgrps; i++) {
		const struct tegra_windowgroup_soc *wgrp = &dc->soc->wgrps[i];

		if (wgrp->dc == dc->pipe && wgrp->num_windows > 0)
			return true;
	}

	return false;
}

static int tegra_dc_init(struct host1x_client *client)
{
	struct drm_device *drm = dev_get_drvdata(client->host);
	unsigned long flags = HOST1X_SYNCPT_CLIENT_MANAGED;
	struct tegra_dc *dc = host1x_client_to_dc(client);
	struct tegra_drm *tegra = drm->dev_private;
	struct drm_plane *primary = NULL;
	struct drm_plane *cursor = NULL;
	int err;

	/*
	 * XXX do not register DCs with no window groups because we cannot
	 * assign a primary plane to them, which in turn will cause KMS to
	 * crash.
	 */
	if (!tegra_dc_has_window_groups(dc))
		return 0;

	/*
	 * Set the display hub as the host1x client parent for the display
	 * controller. This is needed for the runtime reference counting that
	 * ensures the display hub is always powered when any of the display
	 * controllers are.
	 */
	if (dc->soc->has_nvdisplay)
		client->parent = &tegra->hub->client;

	dc->syncpt = host1x_syncpt_request(client, flags);
	if (!dc->syncpt)
		dev_warn(dc->dev, "failed to allocate syncpoint\n");

	err = host1x_client_iommu_attach(client);
	if (err < 0 && err != -ENODEV) {
		dev_err(client->dev, "failed to attach to domain: %d\n", err);
		return err;
	}

	if (dc->soc->wgrps)
		primary = tegra_dc_add_shared_planes(drm, dc);
	else
		primary = tegra_dc_add_planes(drm, dc);

	if (IS_ERR(primary)) {
		err = PTR_ERR(primary);
		goto cleanup;
	}

	if (dc->soc->supports_cursor) {
		cursor = tegra_dc_cursor_plane_create(drm, dc);
		if (IS_ERR(cursor)) {
			err = PTR_ERR(cursor);
			goto cleanup;
		}
	} else {
		/* dedicate one overlay to mouse cursor */
		cursor = tegra_dc_overlay_plane_create(drm, dc, 2, true);
		if (IS_ERR(cursor)) {
			err = PTR_ERR(cursor);
			goto cleanup;
		}
	}

	err = drm_crtc_init_with_planes(drm, &dc->base, primary, cursor,
					&tegra_crtc_funcs, NULL);
	if (err < 0)
		goto cleanup;

	drm_crtc_helper_add(&dc->base, &tegra_crtc_helper_funcs);

	/*
	 * Keep track of the minimum pitch alignment across all display
	 * controllers.
	 */
	if (dc->soc->pitch_align > tegra->pitch_align)
		tegra->pitch_align = dc->soc->pitch_align;

	err = tegra_dc_rgb_init(drm, dc);
	if (err < 0 && err != -ENODEV) {
		dev_err(dc->dev, "failed to initialize RGB output: %d\n", err);
		goto cleanup;
	}

	err = devm_request_irq(dc->dev, dc->irq, tegra_dc_irq, 0,
			       dev_name(dc->dev), dc);
	if (err < 0) {
		dev_err(dc->dev, "failed to request IRQ#%u: %d\n", dc->irq,
			err);
		goto cleanup;
	}

	/*
	 * Inherit the DMA parameters (such as maximum segment size) from the
	 * parent host1x device.
	 */
	client->dev->dma_parms = client->host->dma_parms;

	return 0;

cleanup:
	if (!IS_ERR_OR_NULL(cursor))
		drm_plane_cleanup(cursor);

	if (!IS_ERR(primary))
		drm_plane_cleanup(primary);

	host1x_client_iommu_detach(client);
	host1x_syncpt_free(dc->syncpt);

	return err;
}

static int tegra_dc_exit(struct host1x_client *client)
{
	struct tegra_dc *dc = host1x_client_to_dc(client);
	int err;

	if (!tegra_dc_has_window_groups(dc))
		return 0;

	/* avoid a dangling pointer just in case this disappears */
	client->dev->dma_parms = NULL;

	devm_free_irq(dc->dev, dc->irq, dc);

	err = tegra_dc_rgb_exit(dc);
	if (err) {
		dev_err(dc->dev, "failed to shutdown RGB output: %d\n", err);
		return err;
	}

	host1x_client_iommu_detach(client);
	host1x_syncpt_free(dc->syncpt);

	return 0;
}

static int tegra_dc_runtime_suspend(struct host1x_client *client)
{
	struct tegra_dc *dc = host1x_client_to_dc(client);
	struct device *dev = client->dev;
	int err;

	err = reset_control_assert(dc->rst);
	if (err < 0) {
		dev_err(dev, "failed to assert reset: %d\n", err);
		return err;
	}

	if (dc->soc->has_powergate)
		tegra_powergate_power_off(dc->powergate);

	clk_disable_unprepare(dc->clk);
	pm_runtime_put_sync(dev);

	return 0;
}

static int tegra_dc_runtime_resume(struct host1x_client *client)
{
	struct tegra_dc *dc = host1x_client_to_dc(client);
	struct device *dev = client->dev;
	int err;

	err = pm_runtime_get_sync(dev);
	if (err < 0) {
		dev_err(dev, "failed to get runtime PM: %d\n", err);
		return err;
	}

	if (dc->soc->has_powergate) {
		err = tegra_powergate_sequence_power_up(dc->powergate, dc->clk,
							dc->rst);
		if (err < 0) {
			dev_err(dev, "failed to power partition: %d\n", err);
			goto put_rpm;
		}
	} else {
		err = clk_prepare_enable(dc->clk);
		if (err < 0) {
			dev_err(dev, "failed to enable clock: %d\n", err);
			goto put_rpm;
		}

		err = reset_control_deassert(dc->rst);
		if (err < 0) {
			dev_err(dev, "failed to deassert reset: %d\n", err);
			goto disable_clk;
		}
	}

	return 0;

disable_clk:
	clk_disable_unprepare(dc->clk);
put_rpm:
	pm_runtime_put_sync(dev);
	return err;
}

static const struct host1x_client_ops dc_client_ops = {
	.init = tegra_dc_init,
	.exit = tegra_dc_exit,
	.suspend = tegra_dc_runtime_suspend,
	.resume = tegra_dc_runtime_resume,
};

static const struct tegra_dc_soc_info tegra20_dc_soc_info = {
	.supports_background_color = false,
	.supports_interlacing = false,
	.supports_cursor = false,
	.supports_block_linear = false,
	.has_legacy_blending = true,
	.pitch_align = 8,
	.has_powergate = false,
	.coupled_pm = true,
	.has_nvdisplay = false,
	.num_primary_formats = ARRAY_SIZE(tegra20_primary_formats),
	.primary_formats = tegra20_primary_formats,
	.num_overlay_formats = ARRAY_SIZE(tegra20_overlay_formats),
	.overlay_formats = tegra20_overlay_formats,
	.modifiers = tegra20_modifiers,
	.has_win_a_without_filters = true,
	.has_win_c_without_vert_filter = true,
};

static const struct tegra_dc_soc_info tegra30_dc_soc_info = {
	.supports_background_color = false,
	.supports_interlacing = false,
	.supports_cursor = false,
	.supports_block_linear = false,
	.has_legacy_blending = true,
	.pitch_align = 8,
	.has_powergate = false,
	.coupled_pm = false,
	.has_nvdisplay = false,
	.num_primary_formats = ARRAY_SIZE(tegra20_primary_formats),
	.primary_formats = tegra20_primary_formats,
	.num_overlay_formats = ARRAY_SIZE(tegra20_overlay_formats),
	.overlay_formats = tegra20_overlay_formats,
	.modifiers = tegra20_modifiers,
	.has_win_a_without_filters = false,
	.has_win_c_without_vert_filter = false,
};

static const struct tegra_dc_soc_info tegra114_dc_soc_info = {
	.supports_background_color = false,
	.supports_interlacing = false,
	.supports_cursor = false,
	.supports_block_linear = false,
	.has_legacy_blending = true,
	.pitch_align = 64,
	.has_powergate = true,
	.coupled_pm = false,
	.has_nvdisplay = false,
	.num_primary_formats = ARRAY_SIZE(tegra114_primary_formats),
	.primary_formats = tegra114_primary_formats,
	.num_overlay_formats = ARRAY_SIZE(tegra114_overlay_formats),
	.overlay_formats = tegra114_overlay_formats,
	.modifiers = tegra20_modifiers,
	.has_win_a_without_filters = false,
	.has_win_c_without_vert_filter = false,
};

static const struct tegra_dc_soc_info tegra124_dc_soc_info = {
	.supports_background_color = true,
	.supports_interlacing = true,
	.supports_cursor = true,
	.supports_block_linear = true,
	.has_legacy_blending = false,
	.pitch_align = 64,
	.has_powergate = true,
	.coupled_pm = false,
	.has_nvdisplay = false,
	.num_primary_formats = ARRAY_SIZE(tegra124_primary_formats),
	.primary_formats = tegra124_primary_formats,
	.num_overlay_formats = ARRAY_SIZE(tegra124_overlay_formats),
	.overlay_formats = tegra124_overlay_formats,
	.modifiers = tegra124_modifiers,
	.has_win_a_without_filters = false,
	.has_win_c_without_vert_filter = false,
};

static const struct tegra_dc_soc_info tegra210_dc_soc_info = {
	.supports_background_color = true,
	.supports_interlacing = true,
	.supports_cursor = true,
	.supports_block_linear = true,
	.has_legacy_blending = false,
	.pitch_align = 64,
	.has_powergate = true,
	.coupled_pm = false,
	.has_nvdisplay = false,
	.num_primary_formats = ARRAY_SIZE(tegra114_primary_formats),
	.primary_formats = tegra114_primary_formats,
	.num_overlay_formats = ARRAY_SIZE(tegra114_overlay_formats),
	.overlay_formats = tegra114_overlay_formats,
	.modifiers = tegra124_modifiers,
	.has_win_a_without_filters = false,
	.has_win_c_without_vert_filter = false,
};

static const struct tegra_windowgroup_soc tegra186_dc_wgrps[] = {
	{
		.index = 0,
		.dc = 0,
		.windows = (const unsigned int[]) { 0 },
		.num_windows = 1,
	}, {
		.index = 1,
		.dc = 1,
		.windows = (const unsigned int[]) { 1 },
		.num_windows = 1,
	}, {
		.index = 2,
		.dc = 1,
		.windows = (const unsigned int[]) { 2 },
		.num_windows = 1,
	}, {
		.index = 3,
		.dc = 2,
		.windows = (const unsigned int[]) { 3 },
		.num_windows = 1,
	}, {
		.index = 4,
		.dc = 2,
		.windows = (const unsigned int[]) { 4 },
		.num_windows = 1,
	}, {
		.index = 5,
		.dc = 2,
		.windows = (const unsigned int[]) { 5 },
		.num_windows = 1,
	},
};

static const struct tegra_dc_soc_info tegra186_dc_soc_info = {
	.supports_background_color = true,
	.supports_interlacing = true,
	.supports_cursor = true,
	.supports_block_linear = true,
	.has_legacy_blending = false,
	.pitch_align = 64,
	.has_powergate = false,
	.coupled_pm = false,
	.has_nvdisplay = true,
	.wgrps = tegra186_dc_wgrps,
	.num_wgrps = ARRAY_SIZE(tegra186_dc_wgrps),
};

static const struct tegra_windowgroup_soc tegra194_dc_wgrps[] = {
	{
		.index = 0,
		.dc = 0,
		.windows = (const unsigned int[]) { 0 },
		.num_windows = 1,
	}, {
		.index = 1,
		.dc = 1,
		.windows = (const unsigned int[]) { 1 },
		.num_windows = 1,
	}, {
		.index = 2,
		.dc = 1,
		.windows = (const unsigned int[]) { 2 },
		.num_windows = 1,
	}, {
		.index = 3,
		.dc = 2,
		.windows = (const unsigned int[]) { 3 },
		.num_windows = 1,
	}, {
		.index = 4,
		.dc = 2,
		.windows = (const unsigned int[]) { 4 },
		.num_windows = 1,
	}, {
		.index = 5,
		.dc = 2,
		.windows = (const unsigned int[]) { 5 },
		.num_windows = 1,
	},
};

static const struct tegra_dc_soc_info tegra194_dc_soc_info = {
	.supports_background_color = true,
	.supports_interlacing = true,
	.supports_cursor = true,
	.supports_block_linear = true,
	.has_legacy_blending = false,
	.pitch_align = 64,
	.has_powergate = false,
	.coupled_pm = false,
	.has_nvdisplay = true,
	.wgrps = tegra194_dc_wgrps,
	.num_wgrps = ARRAY_SIZE(tegra194_dc_wgrps),
};

static const struct of_device_id tegra_dc_of_match[] = {
	{
		.compatible = "nvidia,tegra194-dc",
		.data = &tegra194_dc_soc_info,
	}, {
		.compatible = "nvidia,tegra186-dc",
		.data = &tegra186_dc_soc_info,
	}, {
		.compatible = "nvidia,tegra210-dc",
		.data = &tegra210_dc_soc_info,
	}, {
		.compatible = "nvidia,tegra124-dc",
		.data = &tegra124_dc_soc_info,
	}, {
		.compatible = "nvidia,tegra114-dc",
		.data = &tegra114_dc_soc_info,
	}, {
		.compatible = "nvidia,tegra30-dc",
		.data = &tegra30_dc_soc_info,
	}, {
		.compatible = "nvidia,tegra20-dc",
		.data = &tegra20_dc_soc_info,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, tegra_dc_of_match);

static int tegra_dc_parse_dt(struct tegra_dc *dc)
{
	struct device_node *np;
	u32 value = 0;
	int err;

	err = of_property_read_u32(dc->dev->of_node, "nvidia,head", &value);
	if (err < 0) {
		dev_err(dc->dev, "missing \"nvidia,head\" property\n");

		/*
		 * If the nvidia,head property isn't present, try to find the
		 * correct head number by looking up the position of this
		 * display controller's node within the device tree. Assuming
		 * that the nodes are ordered properly in the DTS file and
		 * that the translation into a flattened device tree blob
		 * preserves that ordering this will actually yield the right
		 * head number.
		 *
		 * If those assumptions don't hold, this will still work for
		 * cases where only a single display controller is used.
		 */
		for_each_matching_node(np, tegra_dc_of_match) {
			if (np == dc->dev->of_node) {
				of_node_put(np);
				break;
			}

			value++;
		}
	}

	dc->pipe = value;

	return 0;
}

static int tegra_dc_match_by_pipe(struct device *dev, const void *data)
{
	struct tegra_dc *dc = dev_get_drvdata(dev);
	unsigned int pipe = (unsigned long)(void *)data;

	return dc->pipe == pipe;
}

static int tegra_dc_couple(struct tegra_dc *dc)
{
	/*
	 * On Tegra20, DC1 requires DC0 to be taken out of reset in order to
	 * be enabled, otherwise CPU hangs on writing to CMD_DISPLAY_COMMAND /
	 * POWER_CONTROL registers during CRTC enabling.
	 */
	if (dc->soc->coupled_pm && dc->pipe == 1) {
		u32 flags = DL_FLAG_PM_RUNTIME | DL_FLAG_AUTOREMOVE_CONSUMER;
		struct device_link *link;
		struct device *partner;

		partner = driver_find_device(dc->dev->driver, NULL, NULL,
					     tegra_dc_match_by_pipe);
		if (!partner)
			return -EPROBE_DEFER;

		link = device_link_add(dc->dev, partner, flags);
		if (!link) {
			dev_err(dc->dev, "failed to link controllers\n");
			return -EINVAL;
		}

		dev_dbg(dc->dev, "coupled to %s\n", dev_name(partner));
	}

	return 0;
}

static int tegra_dc_probe(struct platform_device *pdev)
{
	struct tegra_dc *dc;
	int err;

	dc = devm_kzalloc(&pdev->dev, sizeof(*dc), GFP_KERNEL);
	if (!dc)
		return -ENOMEM;

	dc->soc = of_device_get_match_data(&pdev->dev);

	INIT_LIST_HEAD(&dc->list);
	dc->dev = &pdev->dev;

	err = tegra_dc_parse_dt(dc);
	if (err < 0)
		return err;

	err = tegra_dc_couple(dc);
	if (err < 0)
		return err;

	dc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dc->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(dc->clk);
	}

	dc->rst = devm_reset_control_get(&pdev->dev, "dc");
	if (IS_ERR(dc->rst)) {
		dev_err(&pdev->dev, "failed to get reset\n");
		return PTR_ERR(dc->rst);
	}

	/* assert reset and disable clock */
	err = clk_prepare_enable(dc->clk);
	if (err < 0)
		return err;

	usleep_range(2000, 4000);

	err = reset_control_assert(dc->rst);
	if (err < 0)
		return err;

	usleep_range(2000, 4000);

	clk_disable_unprepare(dc->clk);

	if (dc->soc->has_powergate) {
		if (dc->pipe == 0)
			dc->powergate = TEGRA_POWERGATE_DIS;
		else
			dc->powergate = TEGRA_POWERGATE_DISB;

		tegra_powergate_power_off(dc->powergate);
	}

	dc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dc->regs))
		return PTR_ERR(dc->regs);

	dc->irq = platform_get_irq(pdev, 0);
	if (dc->irq < 0) {
		dev_err(&pdev->dev, "failed to get IRQ\n");
		return -ENXIO;
	}

	err = tegra_dc_rgb_probe(dc);
	if (err < 0 && err != -ENODEV) {
		const char *level = KERN_ERR;

		if (err == -EPROBE_DEFER)
			level = KERN_DEBUG;

		dev_printk(level, dc->dev, "failed to probe RGB output: %d\n",
			   err);
		return err;
	}

	platform_set_drvdata(pdev, dc);
	pm_runtime_enable(&pdev->dev);

	INIT_LIST_HEAD(&dc->client.list);
	dc->client.ops = &dc_client_ops;
	dc->client.dev = &pdev->dev;

	err = host1x_client_register(&dc->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register host1x client: %d\n",
			err);
		goto disable_pm;
	}

	return 0;

disable_pm:
	pm_runtime_disable(&pdev->dev);
	tegra_dc_rgb_remove(dc);

	return err;
}

static int tegra_dc_remove(struct platform_device *pdev)
{
	struct tegra_dc *dc = platform_get_drvdata(pdev);
	int err;

	err = host1x_client_unregister(&dc->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to unregister host1x client: %d\n",
			err);
		return err;
	}

	err = tegra_dc_rgb_remove(dc);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to remove RGB output: %d\n", err);
		return err;
	}

	pm_runtime_disable(&pdev->dev);

	return 0;
}

struct platform_driver tegra_dc_driver = {
	.driver = {
		.name = "tegra-dc",
		.of_match_table = tegra_dc_of_match,
	},
	.probe = tegra_dc_probe,
	.remove = tegra_dc_remove,
};

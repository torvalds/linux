// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/host1x.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_probe_helper.h>

#include "drm.h"
#include "dc.h"
#include "plane.h"

#define NFB 24

static const u32 tegra_shared_plane_formats[] = {
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	/* new on Tegra114 */
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	/* planar formats */
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YUV422,
};

static const u64 tegra_shared_plane_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(0),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(1),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(2),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(3),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(4),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(5),
	/*
	 * The GPU sector layout is only supported on Tegra194, but these will
	 * be filtered out later on by ->format_mod_supported() on SoCs where
	 * it isn't supported.
	 */
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(0) | DRM_FORMAT_MOD_NVIDIA_SECTOR_LAYOUT,
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(1) | DRM_FORMAT_MOD_NVIDIA_SECTOR_LAYOUT,
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(2) | DRM_FORMAT_MOD_NVIDIA_SECTOR_LAYOUT,
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(3) | DRM_FORMAT_MOD_NVIDIA_SECTOR_LAYOUT,
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(4) | DRM_FORMAT_MOD_NVIDIA_SECTOR_LAYOUT,
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(5) | DRM_FORMAT_MOD_NVIDIA_SECTOR_LAYOUT,
	/* sentinel */
	DRM_FORMAT_MOD_INVALID
};

static inline unsigned int tegra_plane_offset(struct tegra_plane *plane,
					      unsigned int offset)
{
	if (offset >= 0x500 && offset <= 0x581) {
		offset = 0x000 + (offset - 0x500);
		return plane->offset + offset;
	}

	if (offset >= 0x700 && offset <= 0x73c) {
		offset = 0x180 + (offset - 0x700);
		return plane->offset + offset;
	}

	if (offset >= 0x800 && offset <= 0x83e) {
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

static int tegra_windowgroup_enable(struct tegra_windowgroup *wgrp)
{
	int err = 0;

	mutex_lock(&wgrp->lock);

	if (wgrp->usecount == 0) {
		err = host1x_client_resume(wgrp->parent);
		if (err < 0) {
			dev_err(wgrp->parent->dev, "failed to resume: %d\n", err);
			goto unlock;
		}

		reset_control_deassert(wgrp->rst);
	}

	wgrp->usecount++;

unlock:
	mutex_unlock(&wgrp->lock);
	return err;
}

static void tegra_windowgroup_disable(struct tegra_windowgroup *wgrp)
{
	int err;

	mutex_lock(&wgrp->lock);

	if (wgrp->usecount == 1) {
		err = reset_control_assert(wgrp->rst);
		if (err < 0) {
			pr_err("failed to assert reset for window group %u\n",
			       wgrp->index);
		}

		host1x_client_suspend(wgrp->parent);
	}

	wgrp->usecount--;
	mutex_unlock(&wgrp->lock);
}

int tegra_display_hub_prepare(struct tegra_display_hub *hub)
{
	unsigned int i;

	/*
	 * XXX Enabling/disabling windowgroups needs to happen when the owner
	 * display controller is disabled. There's currently no good point at
	 * which this could be executed, so unconditionally enable all window
	 * groups for now.
	 */
	for (i = 0; i < hub->soc->num_wgrps; i++) {
		struct tegra_windowgroup *wgrp = &hub->wgrps[i];

		/* Skip orphaned window group whose parent DC is disabled */
		if (wgrp->parent)
			tegra_windowgroup_enable(wgrp);
	}

	return 0;
}

void tegra_display_hub_cleanup(struct tegra_display_hub *hub)
{
	unsigned int i;

	/*
	 * XXX Remove this once window groups can be more fine-grainedly
	 * enabled and disabled.
	 */
	for (i = 0; i < hub->soc->num_wgrps; i++) {
		struct tegra_windowgroup *wgrp = &hub->wgrps[i];

		/* Skip orphaned window group whose parent DC is disabled */
		if (wgrp->parent)
			tegra_windowgroup_disable(wgrp);
	}
}

static void tegra_shared_plane_update(struct tegra_plane *plane)
{
	struct tegra_dc *dc = plane->dc;
	unsigned long timeout;
	u32 mask, value;

	mask = COMMON_UPDATE | WIN_A_UPDATE << plane->base.index;
	tegra_dc_writel(dc, mask, DC_CMD_STATE_CONTROL);

	timeout = jiffies + msecs_to_jiffies(1000);

	while (time_before(jiffies, timeout)) {
		value = tegra_dc_readl(dc, DC_CMD_STATE_CONTROL);
		if ((value & mask) == 0)
			break;

		usleep_range(100, 400);
	}
}

static void tegra_shared_plane_activate(struct tegra_plane *plane)
{
	struct tegra_dc *dc = plane->dc;
	unsigned long timeout;
	u32 mask, value;

	mask = COMMON_ACTREQ | WIN_A_ACT_REQ << plane->base.index;
	tegra_dc_writel(dc, mask, DC_CMD_STATE_CONTROL);

	timeout = jiffies + msecs_to_jiffies(1000);

	while (time_before(jiffies, timeout)) {
		value = tegra_dc_readl(dc, DC_CMD_STATE_CONTROL);
		if ((value & mask) == 0)
			break;

		usleep_range(100, 400);
	}
}

static unsigned int
tegra_shared_plane_get_owner(struct tegra_plane *plane, struct tegra_dc *dc)
{
	unsigned int offset =
		tegra_plane_offset(plane, DC_WIN_CORE_WINDOWGROUP_SET_CONTROL);

	return tegra_dc_readl(dc, offset) & OWNER_MASK;
}

static bool tegra_dc_owns_shared_plane(struct tegra_dc *dc,
				       struct tegra_plane *plane)
{
	struct device *dev = dc->dev;

	if (tegra_shared_plane_get_owner(plane, dc) == dc->pipe) {
		if (plane->dc == dc)
			return true;

		dev_WARN(dev, "head %u owns window %u but is not attached\n",
			 dc->pipe, plane->index);
	}

	return false;
}

static int tegra_shared_plane_set_owner(struct tegra_plane *plane,
					struct tegra_dc *new)
{
	unsigned int offset =
		tegra_plane_offset(plane, DC_WIN_CORE_WINDOWGROUP_SET_CONTROL);
	struct tegra_dc *old = plane->dc, *dc = new ? new : old;
	struct device *dev = new ? new->dev : old->dev;
	unsigned int owner, index = plane->index;
	u32 value;

	value = tegra_dc_readl(dc, offset);
	owner = value & OWNER_MASK;

	if (new && (owner != OWNER_MASK && owner != new->pipe)) {
		dev_WARN(dev, "window %u owned by head %u\n", index, owner);
		return -EBUSY;
	}

	/*
	 * This seems to happen whenever the head has been disabled with one
	 * or more windows being active. This is harmless because we'll just
	 * reassign the window to the new head anyway.
	 */
	if (old && owner == OWNER_MASK)
		dev_dbg(dev, "window %u not owned by head %u but %u\n", index,
			old->pipe, owner);

	value &= ~OWNER_MASK;

	if (new)
		value |= OWNER(new->pipe);
	else
		value |= OWNER_MASK;

	tegra_dc_writel(dc, value, offset);

	plane->dc = new;

	return 0;
}

static void tegra_shared_plane_setup_scaler(struct tegra_plane *plane)
{
	static const unsigned int coeffs[192] = {
		0x00000000, 0x3c70e400, 0x3bb037e4, 0x0c51cc9c,
		0x00100001, 0x3bf0dbfa, 0x3d00f406, 0x3fe003ff,
		0x00300002, 0x3b80cbf5, 0x3da1040d, 0x3fb003fe,
		0x00400002, 0x3b20bff1, 0x3e511015, 0x3f9003fc,
		0x00500002, 0x3ad0b3ed, 0x3f21201d, 0x3f5003fb,
		0x00500003, 0x3aa0a3e9, 0x3ff13026, 0x3f2007f9,
		0x00500403, 0x3a7097e6, 0x00e1402f, 0x3ee007f7,
		0x00500403, 0x3a608be4, 0x01d14c38, 0x3ea00bf6,
		0x00500403, 0x3a507fe2, 0x02e15c42, 0x3e500ff4,
		0x00500402, 0x3a6073e1, 0x03f16c4d, 0x3e000ff2,
		0x00400402, 0x3a706be0, 0x05117858, 0x3db013f0,
		0x00300402, 0x3a905fe0, 0x06318863, 0x3d6017ee,
		0x00300402, 0x3ab057e0, 0x0771986e, 0x3d001beb,
		0x00200001, 0x3af04fe1, 0x08a1a47a, 0x3cb023e9,
		0x00100001, 0x3b2047e2, 0x09e1b485, 0x3c6027e7,
		0x00100000, 0x3b703fe2, 0x0b11c091, 0x3c002fe6,
		0x3f203800, 0x0391103f, 0x3ff0a014, 0x0811606c,
		0x3f2037ff, 0x0351083c, 0x03e11842, 0x3f203c00,
		0x3f302fff, 0x03010439, 0x04311c45, 0x3f104401,
		0x3f302fff, 0x02c0fc35, 0x04812448, 0x3f104802,
		0x3f4027ff, 0x0270f832, 0x04c1284b, 0x3f205003,
		0x3f4023ff, 0x0230f030, 0x0511304e, 0x3f205403,
		0x3f601fff, 0x01f0e82d, 0x05613451, 0x3f205c04,
		0x3f701bfe, 0x01b0e02a, 0x05a13c54, 0x3f306006,
		0x3f7017fe, 0x0170d827, 0x05f14057, 0x3f406807,
		0x3f8017ff, 0x0140d424, 0x0641445a, 0x3f406c08,
		0x3fa013ff, 0x0100cc22, 0x0681485d, 0x3f507409,
		0x3fa00fff, 0x00d0c41f, 0x06d14c60, 0x3f607c0b,
		0x3fc00fff, 0x0090bc1c, 0x07115063, 0x3f80840c,
		0x3fd00bff, 0x0070b41a, 0x07515465, 0x3f908c0e,
		0x3fe007ff, 0x0040b018, 0x07915868, 0x3fb0900f,
		0x3ff00400, 0x0010a816, 0x07d15c6a, 0x3fd09811,
		0x00a04c0e, 0x0460f442, 0x0240a827, 0x05c15859,
		0x0090440d, 0x0440f040, 0x0480fc43, 0x00b05010,
		0x0080400c, 0x0410ec3e, 0x04910044, 0x00d05411,
		0x0070380b, 0x03f0e83d, 0x04b10846, 0x00e05812,
		0x0060340a, 0x03d0e43b, 0x04d10c48, 0x00f06013,
		0x00503009, 0x03b0e039, 0x04e11449, 0x01106415,
		0x00402c08, 0x0390d838, 0x05011c4b, 0x01206c16,
		0x00302807, 0x0370d436, 0x0511204c, 0x01407018,
		0x00302406, 0x0340d034, 0x0531244e, 0x01507419,
		0x00202005, 0x0320cc32, 0x05412c50, 0x01707c1b,
		0x00101c04, 0x0300c431, 0x05613451, 0x0180801d,
		0x00101803, 0x02e0c02f, 0x05713853, 0x01a0881e,
		0x00101002, 0x02b0bc2d, 0x05814054, 0x01c08c20,
		0x00000c02, 0x02a0b82c, 0x05914455, 0x01e09421,
		0x00000801, 0x0280b02a, 0x05a14c57, 0x02009c23,
		0x00000400, 0x0260ac28, 0x05b15458, 0x0220a025,
	};
	unsigned int ratio, row, column;

	for (ratio = 0; ratio <= 2; ratio++) {
		for (row = 0; row <= 15; row++) {
			for (column = 0; column <= 3; column++) {
				unsigned int index = (ratio << 6) + (row << 2) + column;
				u32 value;

				value = COEFF_INDEX(index) | COEFF_DATA(coeffs[index]);
				tegra_plane_writel(plane, value,
						   DC_WIN_WINDOWGROUP_SET_INPUT_SCALER_COEFF);
			}
		}
	}
}

static void tegra_dc_assign_shared_plane(struct tegra_dc *dc,
					 struct tegra_plane *plane)
{
	u32 value;
	int err;

	if (!tegra_dc_owns_shared_plane(dc, plane)) {
		err = tegra_shared_plane_set_owner(plane, dc);
		if (err < 0)
			return;
	}

	value = tegra_plane_readl(plane, DC_WIN_CORE_IHUB_LINEBUF_CONFIG);
	value |= MODE_FOUR_LINES;
	tegra_plane_writel(plane, value, DC_WIN_CORE_IHUB_LINEBUF_CONFIG);

	value = tegra_plane_readl(plane, DC_WIN_CORE_IHUB_WGRP_FETCH_METER);
	value = SLOTS(1);
	tegra_plane_writel(plane, value, DC_WIN_CORE_IHUB_WGRP_FETCH_METER);

	/* disable watermark */
	value = tegra_plane_readl(plane, DC_WIN_CORE_IHUB_WGRP_LATENCY_CTLA);
	value &= ~LATENCY_CTL_MODE_ENABLE;
	tegra_plane_writel(plane, value, DC_WIN_CORE_IHUB_WGRP_LATENCY_CTLA);

	value = tegra_plane_readl(plane, DC_WIN_CORE_IHUB_WGRP_LATENCY_CTLB);
	value |= WATERMARK_MASK;
	tegra_plane_writel(plane, value, DC_WIN_CORE_IHUB_WGRP_LATENCY_CTLB);

	/* pipe meter */
	value = tegra_plane_readl(plane, DC_WIN_CORE_PRECOMP_WGRP_PIPE_METER);
	value = PIPE_METER_INT(0) | PIPE_METER_FRAC(0);
	tegra_plane_writel(plane, value, DC_WIN_CORE_PRECOMP_WGRP_PIPE_METER);

	/* mempool entries */
	value = tegra_plane_readl(plane, DC_WIN_CORE_IHUB_WGRP_POOL_CONFIG);
	value = MEMPOOL_ENTRIES(0x331);
	tegra_plane_writel(plane, value, DC_WIN_CORE_IHUB_WGRP_POOL_CONFIG);

	value = tegra_plane_readl(plane, DC_WIN_CORE_IHUB_THREAD_GROUP);
	value &= ~THREAD_NUM_MASK;
	value |= THREAD_NUM(plane->base.index);
	value |= THREAD_GROUP_ENABLE;
	tegra_plane_writel(plane, value, DC_WIN_CORE_IHUB_THREAD_GROUP);

	tegra_shared_plane_setup_scaler(plane);

	tegra_shared_plane_update(plane);
	tegra_shared_plane_activate(plane);
}

static void tegra_dc_remove_shared_plane(struct tegra_dc *dc,
					 struct tegra_plane *plane)
{
	tegra_shared_plane_set_owner(plane, NULL);
}

static int tegra_shared_plane_atomic_check(struct drm_plane *plane,
					   struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct tegra_plane_state *plane_state = to_tegra_plane_state(new_plane_state);
	struct tegra_shared_plane *tegra = to_tegra_shared_plane(plane);
	struct tegra_bo_tiling *tiling = &plane_state->tiling;
	struct tegra_dc *dc = to_tegra_dc(new_plane_state->crtc);
	int err;

	/* no need for further checks if the plane is being disabled */
	if (!new_plane_state->crtc || !new_plane_state->fb)
		return 0;

	err = tegra_plane_format(new_plane_state->fb->format->format,
				 &plane_state->format,
				 &plane_state->swap);
	if (err < 0)
		return err;

	err = tegra_fb_get_tiling(new_plane_state->fb, tiling);
	if (err < 0)
		return err;

	if (tiling->mode == TEGRA_BO_TILING_MODE_BLOCK &&
	    !dc->soc->supports_block_linear) {
		DRM_ERROR("hardware doesn't support block linear mode\n");
		return -EINVAL;
	}

	if (tiling->sector_layout == TEGRA_BO_SECTOR_LAYOUT_GPU &&
	    !dc->soc->supports_sector_layout) {
		DRM_ERROR("hardware doesn't support GPU sector layout\n");
		return -EINVAL;
	}

	/*
	 * Tegra doesn't support different strides for U and V planes so we
	 * error out if the user tries to display a framebuffer with such a
	 * configuration.
	 */
	if (new_plane_state->fb->format->num_planes > 2) {
		if (new_plane_state->fb->pitches[2] != new_plane_state->fb->pitches[1]) {
			DRM_ERROR("unsupported UV-plane configuration\n");
			return -EINVAL;
		}
	}

	/* XXX scaling is not yet supported, add a check here */

	err = tegra_plane_state_add(&tegra->base, new_plane_state);
	if (err < 0)
		return err;

	return 0;
}

static void tegra_shared_plane_atomic_disable(struct drm_plane *plane,
					      struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state,
									   plane);
	struct tegra_plane *p = to_tegra_plane(plane);
	struct tegra_dc *dc;
	u32 value;
	int err;

	/* rien ne va plus */
	if (!old_state || !old_state->crtc)
		return;

	dc = to_tegra_dc(old_state->crtc);

	err = host1x_client_resume(&dc->client);
	if (err < 0) {
		dev_err(dc->dev, "failed to resume: %d\n", err);
		return;
	}

	/*
	 * XXX Legacy helpers seem to sometimes call ->atomic_disable() even
	 * on planes that are already disabled. Make sure we fallback to the
	 * head for this particular state instead of crashing.
	 */
	if (WARN_ON(p->dc == NULL))
		p->dc = dc;

	value = tegra_plane_readl(p, DC_WIN_WIN_OPTIONS);
	value &= ~WIN_ENABLE;
	tegra_plane_writel(p, value, DC_WIN_WIN_OPTIONS);

	tegra_dc_remove_shared_plane(dc, p);

	host1x_client_suspend(&dc->client);
}

static inline u32 compute_phase_incr(fixed20_12 in, unsigned int out)
{
	u64 tmp, tmp1, tmp2;

	tmp = (u64)dfixed_trunc(in);
	tmp2 = (u64)out;
	tmp1 = (tmp << NFB) + (tmp2 >> 1);
	do_div(tmp1, tmp2);

	return lower_32_bits(tmp1);
}

static void tegra_shared_plane_atomic_update(struct drm_plane *plane,
					     struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct tegra_plane_state *tegra_plane_state = to_tegra_plane_state(new_state);
	struct tegra_dc *dc = to_tegra_dc(new_state->crtc);
	unsigned int zpos = new_state->normalized_zpos;
	struct drm_framebuffer *fb = new_state->fb;
	struct tegra_plane *p = to_tegra_plane(plane);
	u32 value, min_width, bypass = 0;
	dma_addr_t base, addr_flag = 0;
	unsigned int bpc, planes;
	bool yuv;
	int err;

	/* rien ne va plus */
	if (!new_state->crtc || !new_state->fb)
		return;

	if (!new_state->visible) {
		tegra_shared_plane_atomic_disable(plane, state);
		return;
	}

	err = host1x_client_resume(&dc->client);
	if (err < 0) {
		dev_err(dc->dev, "failed to resume: %d\n", err);
		return;
	}

	yuv = tegra_plane_format_is_yuv(tegra_plane_state->format, &planes, &bpc);

	tegra_dc_assign_shared_plane(dc, p);

	tegra_plane_writel(p, VCOUNTER, DC_WIN_CORE_ACT_CONTROL);

	/* blending */
	value = BLEND_FACTOR_DST_ALPHA_ZERO | BLEND_FACTOR_SRC_ALPHA_K2 |
		BLEND_FACTOR_DST_COLOR_NEG_K1_TIMES_SRC |
		BLEND_FACTOR_SRC_COLOR_K1_TIMES_SRC;
	tegra_plane_writel(p, value, DC_WIN_BLEND_MATCH_SELECT);

	value = BLEND_FACTOR_DST_ALPHA_ZERO | BLEND_FACTOR_SRC_ALPHA_K2 |
		BLEND_FACTOR_DST_COLOR_NEG_K1_TIMES_SRC |
		BLEND_FACTOR_SRC_COLOR_K1_TIMES_SRC;
	tegra_plane_writel(p, value, DC_WIN_BLEND_NOMATCH_SELECT);

	value = K2(255) | K1(255) | WINDOW_LAYER_DEPTH(255 - zpos);
	tegra_plane_writel(p, value, DC_WIN_BLEND_LAYER_CONTROL);

	/* scaling */
	min_width = min(new_state->src_w >> 16, new_state->crtc_w);

	value = tegra_plane_readl(p, DC_WINC_PRECOMP_WGRP_PIPE_CAPC);

	if (min_width < MAX_PIXELS_5TAP444(value)) {
		value = HORIZONTAL_TAPS_5 | VERTICAL_TAPS_5;
	} else {
		value = tegra_plane_readl(p, DC_WINC_PRECOMP_WGRP_PIPE_CAPE);

		if (min_width < MAX_PIXELS_2TAP444(value))
			value = HORIZONTAL_TAPS_2 | VERTICAL_TAPS_2;
		else
			dev_err(dc->dev, "invalid minimum width: %u\n", min_width);
	}

	value = HORIZONTAL_TAPS_5 | VERTICAL_TAPS_5;
	tegra_plane_writel(p, value, DC_WIN_WINDOWGROUP_SET_CONTROL_INPUT_SCALER);

	if (new_state->src_w != new_state->crtc_w << 16) {
		fixed20_12 width = dfixed_init(new_state->src_w >> 16);
		u32 incr = compute_phase_incr(width, new_state->crtc_w) & ~0x1;
		u32 init = (1 << (NFB - 1)) + (incr >> 1);

		tegra_plane_writel(p, incr, DC_WIN_SET_INPUT_SCALER_HPHASE_INCR);
		tegra_plane_writel(p, init, DC_WIN_SET_INPUT_SCALER_H_START_PHASE);
	} else {
		bypass |= INPUT_SCALER_HBYPASS;
	}

	if (new_state->src_h != new_state->crtc_h << 16) {
		fixed20_12 height = dfixed_init(new_state->src_h >> 16);
		u32 incr = compute_phase_incr(height, new_state->crtc_h) & ~0x1;
		u32 init = (1 << (NFB - 1)) + (incr >> 1);

		tegra_plane_writel(p, incr, DC_WIN_SET_INPUT_SCALER_VPHASE_INCR);
		tegra_plane_writel(p, init, DC_WIN_SET_INPUT_SCALER_V_START_PHASE);
	} else {
		bypass |= INPUT_SCALER_VBYPASS;
	}

	tegra_plane_writel(p, bypass, DC_WIN_WINDOWGROUP_SET_INPUT_SCALER_USAGE);

	/* disable compression */
	tegra_plane_writel(p, 0, DC_WINBUF_CDE_CONTROL);

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	/*
	 * Physical address bit 39 in Tegra194 is used as a switch for special
	 * logic that swizzles the memory using either the legacy Tegra or the
	 * dGPU sector layout.
	 */
	if (tegra_plane_state->tiling.sector_layout == TEGRA_BO_SECTOR_LAYOUT_GPU)
		addr_flag = BIT_ULL(39);
#endif

	base = tegra_plane_state->iova[0] + fb->offsets[0];
	base |= addr_flag;

	tegra_plane_writel(p, tegra_plane_state->format, DC_WIN_COLOR_DEPTH);
	tegra_plane_writel(p, 0, DC_WIN_PRECOMP_WGRP_PARAMS);

	value = V_POSITION(new_state->crtc_y) |
		H_POSITION(new_state->crtc_x);
	tegra_plane_writel(p, value, DC_WIN_POSITION);

	value = V_SIZE(new_state->crtc_h) | H_SIZE(new_state->crtc_w);
	tegra_plane_writel(p, value, DC_WIN_SIZE);

	value = WIN_ENABLE | COLOR_EXPAND;
	tegra_plane_writel(p, value, DC_WIN_WIN_OPTIONS);

	value = V_SIZE(new_state->src_h >> 16) | H_SIZE(new_state->src_w >> 16);
	tegra_plane_writel(p, value, DC_WIN_CROPPED_SIZE);

	tegra_plane_writel(p, upper_32_bits(base), DC_WINBUF_START_ADDR_HI);
	tegra_plane_writel(p, lower_32_bits(base), DC_WINBUF_START_ADDR);

	value = PITCH(fb->pitches[0]);
	tegra_plane_writel(p, value, DC_WIN_PLANAR_STORAGE);

	if (yuv && planes > 1) {
		base = tegra_plane_state->iova[1] + fb->offsets[1];
		base |= addr_flag;

		tegra_plane_writel(p, upper_32_bits(base), DC_WINBUF_START_ADDR_HI_U);
		tegra_plane_writel(p, lower_32_bits(base), DC_WINBUF_START_ADDR_U);

		if (planes > 2) {
			base = tegra_plane_state->iova[2] + fb->offsets[2];
			base |= addr_flag;

			tegra_plane_writel(p, upper_32_bits(base), DC_WINBUF_START_ADDR_HI_V);
			tegra_plane_writel(p, lower_32_bits(base), DC_WINBUF_START_ADDR_V);
		}

		value = PITCH_U(fb->pitches[1]);

		if (planes > 2)
			value |= PITCH_V(fb->pitches[2]);

		tegra_plane_writel(p, value, DC_WIN_PLANAR_STORAGE_UV);
	} else {
		tegra_plane_writel(p, 0, DC_WINBUF_START_ADDR_U);
		tegra_plane_writel(p, 0, DC_WINBUF_START_ADDR_HI_U);
		tegra_plane_writel(p, 0, DC_WINBUF_START_ADDR_V);
		tegra_plane_writel(p, 0, DC_WINBUF_START_ADDR_HI_V);
		tegra_plane_writel(p, 0, DC_WIN_PLANAR_STORAGE_UV);
	}

	value = CLAMP_BEFORE_BLEND | INPUT_RANGE_FULL;

	if (yuv) {
		if (bpc < 12)
			value |= DEGAMMA_YUV8_10;
		else
			value |= DEGAMMA_YUV12;

		/* XXX parameterize */
		value |= COLOR_SPACE_YUV_2020;
	} else {
		if (!tegra_plane_format_is_indexed(tegra_plane_state->format))
			value |= DEGAMMA_SRGB;
	}

	tegra_plane_writel(p, value, DC_WIN_SET_PARAMS);

	value = OFFSET_X(new_state->src_y >> 16) |
		OFFSET_Y(new_state->src_x >> 16);
	tegra_plane_writel(p, value, DC_WINBUF_CROPPED_POINT);

	if (dc->soc->supports_block_linear) {
		unsigned long height = tegra_plane_state->tiling.value;

		/* XXX */
		switch (tegra_plane_state->tiling.mode) {
		case TEGRA_BO_TILING_MODE_PITCH:
			value = DC_WINBUF_SURFACE_KIND_BLOCK_HEIGHT(0) |
				DC_WINBUF_SURFACE_KIND_PITCH;
			break;

		/* XXX not supported on Tegra186 and later */
		case TEGRA_BO_TILING_MODE_TILED:
			value = DC_WINBUF_SURFACE_KIND_TILED;
			break;

		case TEGRA_BO_TILING_MODE_BLOCK:
			value = DC_WINBUF_SURFACE_KIND_BLOCK_HEIGHT(height) |
				DC_WINBUF_SURFACE_KIND_BLOCK;
			break;
		}

		tegra_plane_writel(p, value, DC_WINBUF_SURFACE_KIND);
	}

	/* disable gamut CSC */
	value = tegra_plane_readl(p, DC_WIN_WINDOW_SET_CONTROL);
	value &= ~CONTROL_CSC_ENABLE;
	tegra_plane_writel(p, value, DC_WIN_WINDOW_SET_CONTROL);

	host1x_client_suspend(&dc->client);
}

static const struct drm_plane_helper_funcs tegra_shared_plane_helper_funcs = {
	.prepare_fb = tegra_plane_prepare_fb,
	.cleanup_fb = tegra_plane_cleanup_fb,
	.atomic_check = tegra_shared_plane_atomic_check,
	.atomic_update = tegra_shared_plane_atomic_update,
	.atomic_disable = tegra_shared_plane_atomic_disable,
};

struct drm_plane *tegra_shared_plane_create(struct drm_device *drm,
					    struct tegra_dc *dc,
					    unsigned int wgrp,
					    unsigned int index)
{
	enum drm_plane_type type = DRM_PLANE_TYPE_OVERLAY;
	struct tegra_drm *tegra = drm->dev_private;
	struct tegra_display_hub *hub = tegra->hub;
	struct tegra_shared_plane *plane;
	unsigned int possible_crtcs;
	unsigned int num_formats;
	const u64 *modifiers;
	struct drm_plane *p;
	const u32 *formats;
	int err;

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	plane->base.offset = 0x0a00 + 0x0300 * index;
	plane->base.index = index;

	plane->wgrp = &hub->wgrps[wgrp];
	plane->wgrp->parent = &dc->client;

	p = &plane->base.base;

	/* planes can be assigned to arbitrary CRTCs */
	possible_crtcs = BIT(tegra->num_crtcs) - 1;

	num_formats = ARRAY_SIZE(tegra_shared_plane_formats);
	formats = tegra_shared_plane_formats;
	modifiers = tegra_shared_plane_modifiers;

	err = drm_universal_plane_init(drm, p, possible_crtcs,
				       &tegra_plane_funcs, formats,
				       num_formats, modifiers, type, NULL);
	if (err < 0) {
		kfree(plane);
		return ERR_PTR(err);
	}

	drm_plane_helper_add(p, &tegra_shared_plane_helper_funcs);
	drm_plane_create_zpos_property(p, 0, 0, 255);

	return p;
}

static struct drm_private_state *
tegra_display_hub_duplicate_state(struct drm_private_obj *obj)
{
	struct tegra_display_hub_state *state;

	state = kmemdup(obj->state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &state->base);

	return &state->base;
}

static void tegra_display_hub_destroy_state(struct drm_private_obj *obj,
					    struct drm_private_state *state)
{
	struct tegra_display_hub_state *hub_state =
		to_tegra_display_hub_state(state);

	kfree(hub_state);
}

static const struct drm_private_state_funcs tegra_display_hub_state_funcs = {
	.atomic_duplicate_state = tegra_display_hub_duplicate_state,
	.atomic_destroy_state = tegra_display_hub_destroy_state,
};

static struct tegra_display_hub_state *
tegra_display_hub_get_state(struct tegra_display_hub *hub,
			    struct drm_atomic_state *state)
{
	struct drm_private_state *priv;

	priv = drm_atomic_get_private_obj_state(state, &hub->base);
	if (IS_ERR(priv))
		return ERR_CAST(priv);

	return to_tegra_display_hub_state(priv);
}

int tegra_display_hub_atomic_check(struct drm_device *drm,
				   struct drm_atomic_state *state)
{
	struct tegra_drm *tegra = drm->dev_private;
	struct tegra_display_hub_state *hub_state;
	struct drm_crtc_state *old, *new;
	struct drm_crtc *crtc;
	unsigned int i;

	if (!tegra->hub)
		return 0;

	hub_state = tegra_display_hub_get_state(tegra->hub, state);
	if (IS_ERR(hub_state))
		return PTR_ERR(hub_state);

	/*
	 * The display hub display clock needs to be fed by the display clock
	 * with the highest frequency to ensure proper functioning of all the
	 * displays.
	 *
	 * Note that this isn't used before Tegra186, but it doesn't hurt and
	 * conditionalizing it would make the code less clean.
	 */
	for_each_oldnew_crtc_in_state(state, crtc, old, new, i) {
		struct tegra_dc_state *dc = to_dc_state(new);

		if (new->active) {
			if (!hub_state->clk || dc->pclk > hub_state->rate) {
				hub_state->dc = to_tegra_dc(dc->base.crtc);
				hub_state->clk = hub_state->dc->clk;
				hub_state->rate = dc->pclk;
			}
		}
	}

	return 0;
}

static void tegra_display_hub_update(struct tegra_dc *dc)
{
	u32 value;
	int err;

	err = host1x_client_resume(&dc->client);
	if (err < 0) {
		dev_err(dc->dev, "failed to resume: %d\n", err);
		return;
	}

	value = tegra_dc_readl(dc, DC_CMD_IHUB_COMMON_MISC_CTL);
	value &= ~LATENCY_EVENT;
	tegra_dc_writel(dc, value, DC_CMD_IHUB_COMMON_MISC_CTL);

	value = tegra_dc_readl(dc, DC_DISP_IHUB_COMMON_DISPLAY_FETCH_METER);
	value = CURS_SLOTS(1) | WGRP_SLOTS(1);
	tegra_dc_writel(dc, value, DC_DISP_IHUB_COMMON_DISPLAY_FETCH_METER);

	tegra_dc_writel(dc, COMMON_UPDATE, DC_CMD_STATE_CONTROL);
	tegra_dc_readl(dc, DC_CMD_STATE_CONTROL);
	tegra_dc_writel(dc, COMMON_ACTREQ, DC_CMD_STATE_CONTROL);
	tegra_dc_readl(dc, DC_CMD_STATE_CONTROL);

	host1x_client_suspend(&dc->client);
}

void tegra_display_hub_atomic_commit(struct drm_device *drm,
				     struct drm_atomic_state *state)
{
	struct tegra_drm *tegra = drm->dev_private;
	struct tegra_display_hub *hub = tegra->hub;
	struct tegra_display_hub_state *hub_state;
	struct device *dev = hub->client.dev;
	int err;

	hub_state = to_tegra_display_hub_state(hub->base.state);

	if (hub_state->clk) {
		err = clk_set_rate(hub_state->clk, hub_state->rate);
		if (err < 0)
			dev_err(dev, "failed to set rate of %pC to %lu Hz\n",
				hub_state->clk, hub_state->rate);

		err = clk_set_parent(hub->clk_disp, hub_state->clk);
		if (err < 0)
			dev_err(dev, "failed to set parent of %pC to %pC: %d\n",
				hub->clk_disp, hub_state->clk, err);
	}

	if (hub_state->dc)
		tegra_display_hub_update(hub_state->dc);
}

static int tegra_display_hub_init(struct host1x_client *client)
{
	struct tegra_display_hub *hub = to_tegra_display_hub(client);
	struct drm_device *drm = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = drm->dev_private;
	struct tegra_display_hub_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	drm_atomic_private_obj_init(drm, &hub->base, &state->base,
				    &tegra_display_hub_state_funcs);

	tegra->hub = hub;

	return 0;
}

static int tegra_display_hub_exit(struct host1x_client *client)
{
	struct drm_device *drm = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = drm->dev_private;

	drm_atomic_private_obj_fini(&tegra->hub->base);
	tegra->hub = NULL;

	return 0;
}

static int tegra_display_hub_runtime_suspend(struct host1x_client *client)
{
	struct tegra_display_hub *hub = to_tegra_display_hub(client);
	struct device *dev = client->dev;
	unsigned int i = hub->num_heads;
	int err;

	err = reset_control_assert(hub->rst);
	if (err < 0)
		return err;

	while (i--)
		clk_disable_unprepare(hub->clk_heads[i]);

	clk_disable_unprepare(hub->clk_hub);
	clk_disable_unprepare(hub->clk_dsc);
	clk_disable_unprepare(hub->clk_disp);

	pm_runtime_put_sync(dev);

	return 0;
}

static int tegra_display_hub_runtime_resume(struct host1x_client *client)
{
	struct tegra_display_hub *hub = to_tegra_display_hub(client);
	struct device *dev = client->dev;
	unsigned int i;
	int err;

	err = pm_runtime_resume_and_get(dev);
	if (err < 0) {
		dev_err(dev, "failed to get runtime PM: %d\n", err);
		return err;
	}

	err = clk_prepare_enable(hub->clk_disp);
	if (err < 0)
		goto put_rpm;

	err = clk_prepare_enable(hub->clk_dsc);
	if (err < 0)
		goto disable_disp;

	err = clk_prepare_enable(hub->clk_hub);
	if (err < 0)
		goto disable_dsc;

	for (i = 0; i < hub->num_heads; i++) {
		err = clk_prepare_enable(hub->clk_heads[i]);
		if (err < 0)
			goto disable_heads;
	}

	err = reset_control_deassert(hub->rst);
	if (err < 0)
		goto disable_heads;

	return 0;

disable_heads:
	while (i--)
		clk_disable_unprepare(hub->clk_heads[i]);

	clk_disable_unprepare(hub->clk_hub);
disable_dsc:
	clk_disable_unprepare(hub->clk_dsc);
disable_disp:
	clk_disable_unprepare(hub->clk_disp);
put_rpm:
	pm_runtime_put_sync(dev);
	return err;
}

static const struct host1x_client_ops tegra_display_hub_ops = {
	.init = tegra_display_hub_init,
	.exit = tegra_display_hub_exit,
	.suspend = tegra_display_hub_runtime_suspend,
	.resume = tegra_display_hub_runtime_resume,
};

static int tegra_display_hub_probe(struct platform_device *pdev)
{
	u64 dma_mask = dma_get_mask(pdev->dev.parent);
	struct device_node *child = NULL;
	struct tegra_display_hub *hub;
	struct clk *clk;
	unsigned int i;
	int err;

	err = dma_coerce_mask_and_coherent(&pdev->dev, dma_mask);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set DMA mask: %d\n", err);
		return err;
	}

	hub = devm_kzalloc(&pdev->dev, sizeof(*hub), GFP_KERNEL);
	if (!hub)
		return -ENOMEM;

	hub->soc = of_device_get_match_data(&pdev->dev);

	hub->clk_disp = devm_clk_get(&pdev->dev, "disp");
	if (IS_ERR(hub->clk_disp)) {
		err = PTR_ERR(hub->clk_disp);
		return err;
	}

	if (hub->soc->supports_dsc) {
		hub->clk_dsc = devm_clk_get(&pdev->dev, "dsc");
		if (IS_ERR(hub->clk_dsc)) {
			err = PTR_ERR(hub->clk_dsc);
			return err;
		}
	}

	hub->clk_hub = devm_clk_get(&pdev->dev, "hub");
	if (IS_ERR(hub->clk_hub)) {
		err = PTR_ERR(hub->clk_hub);
		return err;
	}

	hub->rst = devm_reset_control_get(&pdev->dev, "misc");
	if (IS_ERR(hub->rst)) {
		err = PTR_ERR(hub->rst);
		return err;
	}

	hub->wgrps = devm_kcalloc(&pdev->dev, hub->soc->num_wgrps,
				  sizeof(*hub->wgrps), GFP_KERNEL);
	if (!hub->wgrps)
		return -ENOMEM;

	for (i = 0; i < hub->soc->num_wgrps; i++) {
		struct tegra_windowgroup *wgrp = &hub->wgrps[i];
		char id[8];

		snprintf(id, sizeof(id), "wgrp%u", i);
		mutex_init(&wgrp->lock);
		wgrp->usecount = 0;
		wgrp->index = i;

		wgrp->rst = devm_reset_control_get(&pdev->dev, id);
		if (IS_ERR(wgrp->rst))
			return PTR_ERR(wgrp->rst);

		err = reset_control_assert(wgrp->rst);
		if (err < 0)
			return err;
	}

	hub->num_heads = of_get_child_count(pdev->dev.of_node);

	hub->clk_heads = devm_kcalloc(&pdev->dev, hub->num_heads, sizeof(clk),
				      GFP_KERNEL);
	if (!hub->clk_heads)
		return -ENOMEM;

	for (i = 0; i < hub->num_heads; i++) {
		child = of_get_next_child(pdev->dev.of_node, child);
		if (!child) {
			dev_err(&pdev->dev, "failed to find node for head %u\n",
				i);
			return -ENODEV;
		}

		clk = devm_get_clk_from_child(&pdev->dev, child, "dc");
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "failed to get clock for head %u\n",
				i);
			of_node_put(child);
			return PTR_ERR(clk);
		}

		hub->clk_heads[i] = clk;
	}

	of_node_put(child);

	/* XXX: enable clock across reset? */
	err = reset_control_assert(hub->rst);
	if (err < 0)
		return err;

	platform_set_drvdata(pdev, hub);
	pm_runtime_enable(&pdev->dev);

	INIT_LIST_HEAD(&hub->client.list);
	hub->client.ops = &tegra_display_hub_ops;
	hub->client.dev = &pdev->dev;

	err = host1x_client_register(&hub->client);
	if (err < 0)
		dev_err(&pdev->dev, "failed to register host1x client: %d\n",
			err);

	err = devm_of_platform_populate(&pdev->dev);
	if (err < 0)
		goto unregister;

	return err;

unregister:
	host1x_client_unregister(&hub->client);
	pm_runtime_disable(&pdev->dev);
	return err;
}

static int tegra_display_hub_remove(struct platform_device *pdev)
{
	struct tegra_display_hub *hub = platform_get_drvdata(pdev);
	unsigned int i;
	int err;

	err = host1x_client_unregister(&hub->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to unregister host1x client: %d\n",
			err);
	}

	for (i = 0; i < hub->soc->num_wgrps; i++) {
		struct tegra_windowgroup *wgrp = &hub->wgrps[i];

		mutex_destroy(&wgrp->lock);
	}

	pm_runtime_disable(&pdev->dev);

	return err;
}

static const struct tegra_display_hub_soc tegra186_display_hub = {
	.num_wgrps = 6,
	.supports_dsc = true,
};

static const struct tegra_display_hub_soc tegra194_display_hub = {
	.num_wgrps = 6,
	.supports_dsc = false,
};

static const struct of_device_id tegra_display_hub_of_match[] = {
	{
		.compatible = "nvidia,tegra194-display",
		.data = &tegra194_display_hub
	}, {
		.compatible = "nvidia,tegra186-display",
		.data = &tegra186_display_hub
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, tegra_display_hub_of_match);

struct platform_driver tegra_display_hub_driver = {
	.driver = {
		.name = "tegra-display-hub",
		.of_match_table = tegra_display_hub_of_match,
	},
	.probe = tegra_display_hub_probe,
	.remove = tegra_display_hub_remove,
};

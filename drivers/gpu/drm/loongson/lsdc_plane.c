// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <linux/delay.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_plane_helper.h>

#include "lsdc_drv.h"
#include "lsdc_regs.h"
#include "lsdc_ttm.h"

static const u32 lsdc_primary_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const u32 lsdc_cursor_formats[] = {
	DRM_FORMAT_ARGB8888,
};

static const u64 lsdc_fb_format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static unsigned int lsdc_get_fb_offset(struct drm_framebuffer *fb,
				       struct drm_plane_state *state)
{
	unsigned int offset = fb->offsets[0];

	offset += fb->format->cpp[0] * (state->src_x >> 16);
	offset += fb->pitches[0] * (state->src_y >> 16);

	return offset;
}

static u64 lsdc_fb_base_addr(struct drm_framebuffer *fb)
{
	struct lsdc_device *ldev = to_lsdc(fb->dev);
	struct lsdc_bo *lbo = gem_to_lsdc_bo(fb->obj[0]);

	return lsdc_bo_gpu_offset(lbo) + ldev->vram_base;
}

static int lsdc_primary_atomic_check(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc *crtc = new_plane_state->crtc;
	struct drm_crtc_state *new_crtc_state;

	if (!crtc)
		return 0;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	return drm_atomic_helper_check_plane_state(new_plane_state,
						   new_crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   false, true);
}

static void lsdc_primary_atomic_update(struct drm_plane *plane,
				       struct drm_atomic_state *state)
{
	struct lsdc_primary *primary = to_lsdc_primary(plane);
	const struct lsdc_primary_plane_ops *ops = primary->ops;
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_framebuffer *new_fb = new_plane_state->fb;
	struct drm_framebuffer *old_fb = old_plane_state->fb;
	u64 fb_addr = lsdc_fb_base_addr(new_fb);

	fb_addr += lsdc_get_fb_offset(new_fb, new_plane_state);

	ops->update_fb_addr(primary, fb_addr);
	ops->update_fb_stride(primary, new_fb->pitches[0]);

	if (!old_fb || old_fb->format != new_fb->format)
		ops->update_fb_format(primary, new_fb->format);
}

static void lsdc_primary_atomic_disable(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	/*
	 * Do nothing, just prevent call into atomic_update().
	 * Writing the format as LSDC_PF_NONE can disable the primary,
	 * But it seems not necessary...
	 */
	drm_dbg(plane->dev, "%s disabled\n", plane->name);
}

static int lsdc_plane_prepare_fb(struct drm_plane *plane,
				 struct drm_plane_state *new_state)
{
	struct drm_framebuffer *fb = new_state->fb;
	struct lsdc_bo *lbo;
	u64 gpu_vaddr;
	int ret;

	if (!fb)
		return 0;

	lbo = gem_to_lsdc_bo(fb->obj[0]);

	ret = lsdc_bo_reserve(lbo);
	if (unlikely(ret)) {
		drm_err(plane->dev, "bo %p reserve failed\n", lbo);
		return ret;
	}

	ret = lsdc_bo_pin(lbo, LSDC_GEM_DOMAIN_VRAM, &gpu_vaddr);

	lsdc_bo_unreserve(lbo);

	if (unlikely(ret)) {
		drm_err(plane->dev, "bo %p pin failed\n", lbo);
		return ret;
	}

	lsdc_bo_ref(lbo);

	if (plane->type != DRM_PLANE_TYPE_CURSOR)
		drm_dbg(plane->dev,
			"%s[%p] pin at 0x%llx, bo size: %zu\n",
			plane->name, lbo, gpu_vaddr, lsdc_bo_size(lbo));

	return drm_gem_plane_helper_prepare_fb(plane, new_state);
}

static void lsdc_plane_cleanup_fb(struct drm_plane *plane,
				  struct drm_plane_state *old_state)
{
	struct drm_framebuffer *fb = old_state->fb;
	struct lsdc_bo *lbo;
	int ret;

	if (!fb)
		return;

	lbo = gem_to_lsdc_bo(fb->obj[0]);

	ret = lsdc_bo_reserve(lbo);
	if (unlikely(ret)) {
		drm_err(plane->dev, "%p reserve failed\n", lbo);
		return;
	}

	lsdc_bo_unpin(lbo);

	lsdc_bo_unreserve(lbo);

	lsdc_bo_unref(lbo);

	if (plane->type != DRM_PLANE_TYPE_CURSOR)
		drm_dbg(plane->dev, "%s unpin\n", plane->name);
}

static const struct drm_plane_helper_funcs lsdc_primary_helper_funcs = {
	.prepare_fb = lsdc_plane_prepare_fb,
	.cleanup_fb = lsdc_plane_cleanup_fb,
	.atomic_check = lsdc_primary_atomic_check,
	.atomic_update = lsdc_primary_atomic_update,
	.atomic_disable = lsdc_primary_atomic_disable,
};

static int lsdc_cursor_plane_atomic_async_check(struct drm_plane *plane,
						struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state;
	struct drm_crtc_state *crtc_state;

	new_state = drm_atomic_get_new_plane_state(state, plane);

	if (!plane->state || !plane->state->fb) {
		drm_dbg(plane->dev, "%s: state is NULL\n", plane->name);
		return -EINVAL;
	}

	if (new_state->crtc_w != new_state->crtc_h) {
		drm_dbg(plane->dev, "unsupported cursor size: %ux%u\n",
			new_state->crtc_w, new_state->crtc_h);
		return -EINVAL;
	}

	if (new_state->crtc_w != 64 && new_state->crtc_w != 32) {
		drm_dbg(plane->dev, "unsupported cursor size: %ux%u\n",
			new_state->crtc_w, new_state->crtc_h);
		return -EINVAL;
	}

	if (state) {
		crtc_state = drm_atomic_get_existing_crtc_state(state, new_state->crtc);
	} else {
		crtc_state = plane->crtc->state;
		drm_dbg(plane->dev, "%s: atomic state is NULL\n", plane->name);
	}

	if (!crtc_state->active)
		return -EINVAL;

	if (plane->state->crtc != new_state->crtc ||
	    plane->state->src_w != new_state->src_w ||
	    plane->state->src_h != new_state->src_h ||
	    plane->state->crtc_w != new_state->crtc_w ||
	    plane->state->crtc_h != new_state->crtc_h)
		return -EINVAL;

	if (new_state->visible != plane->state->visible)
		return -EINVAL;

	return drm_atomic_helper_check_plane_state(plane->state,
						   crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   true, true);
}

static void lsdc_cursor_plane_atomic_async_update(struct drm_plane *plane,
						  struct drm_atomic_state *state)
{
	struct lsdc_cursor *cursor = to_lsdc_cursor(plane);
	const struct lsdc_cursor_plane_ops *ops = cursor->ops;
	struct drm_framebuffer *old_fb = plane->state->fb;
	struct drm_framebuffer *new_fb;
	struct drm_plane_state *new_state;

	new_state = drm_atomic_get_new_plane_state(state, plane);

	new_fb = plane->state->fb;

	plane->state->crtc_x = new_state->crtc_x;
	plane->state->crtc_y = new_state->crtc_y;
	plane->state->crtc_h = new_state->crtc_h;
	plane->state->crtc_w = new_state->crtc_w;
	plane->state->src_x = new_state->src_x;
	plane->state->src_y = new_state->src_y;
	plane->state->src_h = new_state->src_h;
	plane->state->src_w = new_state->src_w;
	swap(plane->state->fb, new_state->fb);

	if (new_state->visible) {
		enum lsdc_cursor_size cursor_size;

		switch (new_state->crtc_w) {
		case 64:
			cursor_size = CURSOR_SIZE_64X64;
			break;
		case 32:
			cursor_size = CURSOR_SIZE_32X32;
			break;
		default:
			cursor_size = CURSOR_SIZE_32X32;
			break;
		}

		ops->update_position(cursor, new_state->crtc_x, new_state->crtc_y);

		ops->update_cfg(cursor, cursor_size, CURSOR_FORMAT_ARGB8888);

		if (!old_fb || old_fb != new_fb)
			ops->update_bo_addr(cursor, lsdc_fb_base_addr(new_fb));
	}
}

/* ls7a1000 cursor plane helpers */

static int ls7a1000_cursor_plane_atomic_check(struct drm_plane *plane,
					      struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state;
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc *crtc;

	new_plane_state = drm_atomic_get_new_plane_state(state, plane);

	crtc = new_plane_state->crtc;
	if (!crtc) {
		drm_dbg(plane->dev, "%s is not bind to a crtc\n", plane->name);
		return 0;
	}

	if (new_plane_state->crtc_w != 32 || new_plane_state->crtc_h != 32) {
		drm_dbg(plane->dev, "unsupported cursor size: %ux%u\n",
			new_plane_state->crtc_w, new_plane_state->crtc_h);
		return -EINVAL;
	}

	new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	return drm_atomic_helper_check_plane_state(new_plane_state,
						   new_crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   true, true);
}

static void ls7a1000_cursor_plane_atomic_update(struct drm_plane *plane,
						struct drm_atomic_state *state)
{
	struct lsdc_cursor *cursor = to_lsdc_cursor(plane);
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_framebuffer *new_fb = new_plane_state->fb;
	struct drm_framebuffer *old_fb = old_plane_state->fb;
	const struct lsdc_cursor_plane_ops *ops = cursor->ops;
	u64 addr = lsdc_fb_base_addr(new_fb);

	if (!new_plane_state->visible)
		return;

	ops->update_position(cursor, new_plane_state->crtc_x, new_plane_state->crtc_y);

	if (!old_fb || old_fb != new_fb)
		ops->update_bo_addr(cursor, addr);

	ops->update_cfg(cursor, CURSOR_SIZE_32X32, CURSOR_FORMAT_ARGB8888);
}

static void ls7a1000_cursor_plane_atomic_disable(struct drm_plane *plane,
						 struct drm_atomic_state *state)
{
	struct lsdc_cursor *cursor = to_lsdc_cursor(plane);
	const struct lsdc_cursor_plane_ops *ops = cursor->ops;

	ops->update_cfg(cursor, CURSOR_SIZE_32X32, CURSOR_FORMAT_DISABLE);
}

static const struct drm_plane_helper_funcs ls7a1000_cursor_plane_helper_funcs = {
	.prepare_fb = lsdc_plane_prepare_fb,
	.cleanup_fb = lsdc_plane_cleanup_fb,
	.atomic_check = ls7a1000_cursor_plane_atomic_check,
	.atomic_update = ls7a1000_cursor_plane_atomic_update,
	.atomic_disable = ls7a1000_cursor_plane_atomic_disable,
	.atomic_async_check = lsdc_cursor_plane_atomic_async_check,
	.atomic_async_update = lsdc_cursor_plane_atomic_async_update,
};

/* ls7a2000 cursor plane helpers */

static int ls7a2000_cursor_plane_atomic_check(struct drm_plane *plane,
					      struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state;
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc *crtc;

	new_plane_state = drm_atomic_get_new_plane_state(state, plane);

	crtc = new_plane_state->crtc;
	if (!crtc) {
		drm_dbg(plane->dev, "%s is not bind to a crtc\n", plane->name);
		return 0;
	}

	if (new_plane_state->crtc_w != new_plane_state->crtc_h) {
		drm_dbg(plane->dev, "unsupported cursor size: %ux%u\n",
			new_plane_state->crtc_w, new_plane_state->crtc_h);
		return -EINVAL;
	}

	if (new_plane_state->crtc_w != 64 && new_plane_state->crtc_w != 32) {
		drm_dbg(plane->dev, "unsupported cursor size: %ux%u\n",
			new_plane_state->crtc_w, new_plane_state->crtc_h);
		return -EINVAL;
	}

	new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	return drm_atomic_helper_check_plane_state(new_plane_state,
						   new_crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   true, true);
}

/* Update the format, size and location of the cursor */

static void ls7a2000_cursor_plane_atomic_update(struct drm_plane *plane,
						struct drm_atomic_state *state)
{
	struct lsdc_cursor *cursor = to_lsdc_cursor(plane);
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_framebuffer *new_fb = new_plane_state->fb;
	struct drm_framebuffer *old_fb = old_plane_state->fb;
	const struct lsdc_cursor_plane_ops *ops = cursor->ops;
	enum lsdc_cursor_size cursor_size;

	if (!new_plane_state->visible)
		return;

	ops->update_position(cursor, new_plane_state->crtc_x, new_plane_state->crtc_y);

	if (!old_fb || new_fb != old_fb) {
		u64 addr = lsdc_fb_base_addr(new_fb);

		ops->update_bo_addr(cursor, addr);
	}

	switch (new_plane_state->crtc_w) {
	case 64:
		cursor_size = CURSOR_SIZE_64X64;
		break;
	case 32:
		cursor_size = CURSOR_SIZE_32X32;
		break;
	default:
		cursor_size = CURSOR_SIZE_64X64;
		break;
	}

	ops->update_cfg(cursor, cursor_size, CURSOR_FORMAT_ARGB8888);
}

static void ls7a2000_cursor_plane_atomic_disable(struct drm_plane *plane,
						 struct drm_atomic_state *state)
{
	struct lsdc_cursor *cursor = to_lsdc_cursor(plane);
	const struct lsdc_cursor_plane_ops *hw_ops = cursor->ops;

	hw_ops->update_cfg(cursor, CURSOR_SIZE_64X64, CURSOR_FORMAT_DISABLE);
}

static const struct drm_plane_helper_funcs ls7a2000_cursor_plane_helper_funcs = {
	.prepare_fb = lsdc_plane_prepare_fb,
	.cleanup_fb = lsdc_plane_cleanup_fb,
	.atomic_check = ls7a2000_cursor_plane_atomic_check,
	.atomic_update = ls7a2000_cursor_plane_atomic_update,
	.atomic_disable = ls7a2000_cursor_plane_atomic_disable,
	.atomic_async_check = lsdc_cursor_plane_atomic_async_check,
	.atomic_async_update = lsdc_cursor_plane_atomic_async_update,
};

static void lsdc_plane_atomic_print_state(struct drm_printer *p,
					  const struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	u64 addr;

	if (!fb)
		return;

	addr = lsdc_fb_base_addr(fb);

	drm_printf(p, "\tdma addr=%llx\n", addr);
}

static const struct drm_plane_funcs lsdc_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.atomic_print_state = lsdc_plane_atomic_print_state,
};

/* Primary plane 0 hardware related ops  */

static void lsdc_primary0_update_fb_addr(struct lsdc_primary *primary, u64 addr)
{
	struct lsdc_device *ldev = primary->ldev;
	u32 status;
	u32 lo, hi;

	/* 40-bit width physical address bus */
	lo = addr & 0xFFFFFFFF;
	hi = (addr >> 32) & 0xFF;

	status = lsdc_rreg32(ldev, LSDC_CRTC0_CFG_REG);
	if (status & FB_REG_IN_USING) {
		lsdc_wreg32(ldev, LSDC_CRTC0_FB1_ADDR_LO_REG, lo);
		lsdc_wreg32(ldev, LSDC_CRTC0_FB1_ADDR_HI_REG, hi);
	} else {
		lsdc_wreg32(ldev, LSDC_CRTC0_FB0_ADDR_LO_REG, lo);
		lsdc_wreg32(ldev, LSDC_CRTC0_FB0_ADDR_HI_REG, hi);
	}
}

static void lsdc_primary0_update_fb_stride(struct lsdc_primary *primary, u32 stride)
{
	struct lsdc_device *ldev = primary->ldev;

	lsdc_wreg32(ldev, LSDC_CRTC0_STRIDE_REG, stride);
}

static void lsdc_primary0_update_fb_format(struct lsdc_primary *primary,
					   const struct drm_format_info *format)
{
	struct lsdc_device *ldev = primary->ldev;
	u32 status;

	status = lsdc_rreg32(ldev, LSDC_CRTC0_CFG_REG);

	/*
	 * TODO: add RGB565 support, only support XRBG8888 at present
	 */
	status &= ~CFG_PIX_FMT_MASK;
	status |= LSDC_PF_XRGB8888;

	lsdc_wreg32(ldev, LSDC_CRTC0_CFG_REG, status);
}

/* Primary plane 1 hardware related ops */

static void lsdc_primary1_update_fb_addr(struct lsdc_primary *primary, u64 addr)
{
	struct lsdc_device *ldev = primary->ldev;
	u32 status;
	u32 lo, hi;

	/* 40-bit width physical address bus */
	lo = addr & 0xFFFFFFFF;
	hi = (addr >> 32) & 0xFF;

	status = lsdc_rreg32(ldev, LSDC_CRTC1_CFG_REG);
	if (status & FB_REG_IN_USING) {
		lsdc_wreg32(ldev, LSDC_CRTC1_FB1_ADDR_LO_REG, lo);
		lsdc_wreg32(ldev, LSDC_CRTC1_FB1_ADDR_HI_REG, hi);
	} else {
		lsdc_wreg32(ldev, LSDC_CRTC1_FB0_ADDR_LO_REG, lo);
		lsdc_wreg32(ldev, LSDC_CRTC1_FB0_ADDR_HI_REG, hi);
	}
}

static void lsdc_primary1_update_fb_stride(struct lsdc_primary *primary, u32 stride)
{
	struct lsdc_device *ldev = primary->ldev;

	lsdc_wreg32(ldev, LSDC_CRTC1_STRIDE_REG, stride);
}

static void lsdc_primary1_update_fb_format(struct lsdc_primary *primary,
					   const struct drm_format_info *format)
{
	struct lsdc_device *ldev = primary->ldev;
	u32 status;

	status = lsdc_rreg32(ldev, LSDC_CRTC1_CFG_REG);

	/*
	 * TODO: add RGB565 support, only support XRBG8888 at present
	 */
	status &= ~CFG_PIX_FMT_MASK;
	status |= LSDC_PF_XRGB8888;

	lsdc_wreg32(ldev, LSDC_CRTC1_CFG_REG, status);
}

static const struct lsdc_primary_plane_ops lsdc_primary_plane_hw_ops[2] = {
	{
		.update_fb_addr = lsdc_primary0_update_fb_addr,
		.update_fb_stride = lsdc_primary0_update_fb_stride,
		.update_fb_format = lsdc_primary0_update_fb_format,
	},
	{
		.update_fb_addr = lsdc_primary1_update_fb_addr,
		.update_fb_stride = lsdc_primary1_update_fb_stride,
		.update_fb_format = lsdc_primary1_update_fb_format,
	},
};

/*
 * Update location, format, enable and disable state of the cursor,
 * For those who have two hardware cursor, let cursor 0 is attach to CRTC-0,
 * cursor 1 is attach to CRTC-1. Compositing the primary plane and cursor
 * plane is automatically done by hardware, the cursor is alway on the top of
 * the primary plane. In other word, z-order is fixed in hardware and cannot
 * be changed. For those old DC who has only one hardware cursor, we made it
 * shared by the two screen, this works on extend screen mode.
 */

/* cursor plane 0 (for pipe 0) related hardware ops */

static void lsdc_cursor0_update_bo_addr(struct lsdc_cursor *cursor, u64 addr)
{
	struct lsdc_device *ldev = cursor->ldev;

	/* 40-bit width physical address bus */
	lsdc_wreg32(ldev, LSDC_CURSOR0_ADDR_HI_REG, (addr >> 32) & 0xFF);
	lsdc_wreg32(ldev, LSDC_CURSOR0_ADDR_LO_REG, addr);
}

static void lsdc_cursor0_update_position(struct lsdc_cursor *cursor, int x, int y)
{
	struct lsdc_device *ldev = cursor->ldev;

	if (x < 0)
		x = 0;

	if (y < 0)
		y = 0;

	lsdc_wreg32(ldev, LSDC_CURSOR0_POSITION_REG, (y << 16) | x);
}

static void lsdc_cursor0_update_cfg(struct lsdc_cursor *cursor,
				    enum lsdc_cursor_size cursor_size,
				    enum lsdc_cursor_format fmt)
{
	struct lsdc_device *ldev = cursor->ldev;
	u32 cfg;

	cfg = CURSOR_ON_CRTC0 << CURSOR_LOCATION_SHIFT |
	      cursor_size << CURSOR_SIZE_SHIFT |
	      fmt << CURSOR_FORMAT_SHIFT;

	lsdc_wreg32(ldev, LSDC_CURSOR0_CFG_REG, cfg);
}

/* cursor plane 1 (for pipe 1) related hardware ops */

static void lsdc_cursor1_update_bo_addr(struct lsdc_cursor *cursor, u64 addr)
{
	struct lsdc_device *ldev = cursor->ldev;

	/* 40-bit width physical address bus */
	lsdc_wreg32(ldev, LSDC_CURSOR1_ADDR_HI_REG, (addr >> 32) & 0xFF);
	lsdc_wreg32(ldev, LSDC_CURSOR1_ADDR_LO_REG, addr);
}

static void lsdc_cursor1_update_position(struct lsdc_cursor *cursor, int x, int y)
{
	struct lsdc_device *ldev = cursor->ldev;

	if (x < 0)
		x = 0;

	if (y < 0)
		y = 0;

	lsdc_wreg32(ldev, LSDC_CURSOR1_POSITION_REG, (y << 16) | x);
}

static void lsdc_cursor1_update_cfg(struct lsdc_cursor *cursor,
				    enum lsdc_cursor_size cursor_size,
				    enum lsdc_cursor_format fmt)
{
	struct lsdc_device *ldev = cursor->ldev;
	u32 cfg;

	cfg = CURSOR_ON_CRTC1 << CURSOR_LOCATION_SHIFT |
	      cursor_size << CURSOR_SIZE_SHIFT |
	      fmt << CURSOR_FORMAT_SHIFT;

	lsdc_wreg32(ldev, LSDC_CURSOR1_CFG_REG, cfg);
}

/* The hardware cursors become normal since ls7a2000/ls2k2000 */

static const struct lsdc_cursor_plane_ops ls7a2000_cursor_hw_ops[2] = {
	{
		.update_bo_addr = lsdc_cursor0_update_bo_addr,
		.update_cfg = lsdc_cursor0_update_cfg,
		.update_position = lsdc_cursor0_update_position,
	},
	{
		.update_bo_addr = lsdc_cursor1_update_bo_addr,
		.update_cfg = lsdc_cursor1_update_cfg,
		.update_position = lsdc_cursor1_update_position,
	},
};

/* Quirks for cursor 1, only for old loongson display controller */

static void lsdc_cursor1_update_bo_addr_quirk(struct lsdc_cursor *cursor, u64 addr)
{
	struct lsdc_device *ldev = cursor->ldev;

	/* 40-bit width physical address bus */
	lsdc_wreg32(ldev, LSDC_CURSOR0_ADDR_HI_REG, (addr >> 32) & 0xFF);
	lsdc_wreg32(ldev, LSDC_CURSOR0_ADDR_LO_REG, addr);
}

static void lsdc_cursor1_update_position_quirk(struct lsdc_cursor *cursor, int x, int y)
{
	struct lsdc_device *ldev = cursor->ldev;

	if (x < 0)
		x = 0;

	if (y < 0)
		y = 0;

	lsdc_wreg32(ldev, LSDC_CURSOR0_POSITION_REG, (y << 16) | x);
}

static void lsdc_cursor1_update_cfg_quirk(struct lsdc_cursor *cursor,
					  enum lsdc_cursor_size cursor_size,
					  enum lsdc_cursor_format fmt)
{
	struct lsdc_device *ldev = cursor->ldev;
	u32 cfg;

	cfg = CURSOR_ON_CRTC1 << CURSOR_LOCATION_SHIFT |
	      cursor_size << CURSOR_SIZE_SHIFT |
	      fmt << CURSOR_FORMAT_SHIFT;

	lsdc_wreg32(ldev, LSDC_CURSOR0_CFG_REG, cfg);
}

/*
 * The unforgiving LS7A1000/LS2K1000 has only one hardware cursors plane
 */
static const struct lsdc_cursor_plane_ops ls7a1000_cursor_hw_ops[2] = {
	{
		.update_bo_addr = lsdc_cursor0_update_bo_addr,
		.update_cfg = lsdc_cursor0_update_cfg,
		.update_position = lsdc_cursor0_update_position,
	},
	{
		.update_bo_addr = lsdc_cursor1_update_bo_addr_quirk,
		.update_cfg = lsdc_cursor1_update_cfg_quirk,
		.update_position = lsdc_cursor1_update_position_quirk,
	},
};

int lsdc_primary_plane_init(struct drm_device *ddev,
			    struct drm_plane *plane,
			    unsigned int index)
{
	struct lsdc_primary *primary = to_lsdc_primary(plane);
	int ret;

	ret = drm_universal_plane_init(ddev, plane, 1 << index,
				       &lsdc_plane_funcs,
				       lsdc_primary_formats,
				       ARRAY_SIZE(lsdc_primary_formats),
				       lsdc_fb_format_modifiers,
				       DRM_PLANE_TYPE_PRIMARY,
				       "ls-primary-plane-%u", index);
	if (ret)
		return ret;

	drm_plane_helper_add(plane, &lsdc_primary_helper_funcs);

	primary->ldev = to_lsdc(ddev);
	primary->ops = &lsdc_primary_plane_hw_ops[index];

	return 0;
}

int ls7a1000_cursor_plane_init(struct drm_device *ddev,
			       struct drm_plane *plane,
			       unsigned int index)
{
	struct lsdc_cursor *cursor = to_lsdc_cursor(plane);
	int ret;

	ret = drm_universal_plane_init(ddev, plane, 1 << index,
				       &lsdc_plane_funcs,
				       lsdc_cursor_formats,
				       ARRAY_SIZE(lsdc_cursor_formats),
				       lsdc_fb_format_modifiers,
				       DRM_PLANE_TYPE_CURSOR,
				       "ls-cursor-plane-%u", index);
	if (ret)
		return ret;

	cursor->ldev = to_lsdc(ddev);
	cursor->ops = &ls7a1000_cursor_hw_ops[index];

	drm_plane_helper_add(plane, &ls7a1000_cursor_plane_helper_funcs);

	return 0;
}

int ls7a2000_cursor_plane_init(struct drm_device *ddev,
			       struct drm_plane *plane,
			       unsigned int index)
{
	struct lsdc_cursor *cursor = to_lsdc_cursor(plane);
	int ret;

	ret = drm_universal_plane_init(ddev, plane, 1 << index,
				       &lsdc_plane_funcs,
				       lsdc_cursor_formats,
				       ARRAY_SIZE(lsdc_cursor_formats),
				       lsdc_fb_format_modifiers,
				       DRM_PLANE_TYPE_CURSOR,
				       "ls-cursor-plane-%u", index);
	if (ret)
		return ret;

	cursor->ldev = to_lsdc(ddev);
	cursor->ops = &ls7a2000_cursor_hw_ops[index];

	drm_plane_helper_add(plane, &ls7a2000_cursor_plane_helper_funcs);

	return 0;
}

/*
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 */
#include <drm/drmP.h>
#include <drm/radeon_drm.h>
#include "radeon.h"

static void radeon_lock_cursor(struct drm_crtc *crtc, bool lock)
{
	struct radeon_device *rdev = crtc->dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	uint32_t cur_lock;

	if (ASIC_IS_DCE4(rdev)) {
		cur_lock = RREG32(EVERGREEN_CUR_UPDATE + radeon_crtc->crtc_offset);
		if (lock)
			cur_lock |= EVERGREEN_CURSOR_UPDATE_LOCK;
		else
			cur_lock &= ~EVERGREEN_CURSOR_UPDATE_LOCK;
		WREG32(EVERGREEN_CUR_UPDATE + radeon_crtc->crtc_offset, cur_lock);
	} else if (ASIC_IS_AVIVO(rdev)) {
		cur_lock = RREG32(AVIVO_D1CUR_UPDATE + radeon_crtc->crtc_offset);
		if (lock)
			cur_lock |= AVIVO_D1CURSOR_UPDATE_LOCK;
		else
			cur_lock &= ~AVIVO_D1CURSOR_UPDATE_LOCK;
		WREG32(AVIVO_D1CUR_UPDATE + radeon_crtc->crtc_offset, cur_lock);
	} else {
		cur_lock = RREG32(RADEON_CUR_OFFSET + radeon_crtc->crtc_offset);
		if (lock)
			cur_lock |= RADEON_CUR_LOCK;
		else
			cur_lock &= ~RADEON_CUR_LOCK;
		WREG32(RADEON_CUR_OFFSET + radeon_crtc->crtc_offset, cur_lock);
	}
}

static void radeon_hide_cursor(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_device *rdev = crtc->dev->dev_private;

	if (ASIC_IS_DCE4(rdev)) {
		WREG32_IDX(EVERGREEN_CUR_CONTROL + radeon_crtc->crtc_offset,
			   EVERGREEN_CURSOR_MODE(EVERGREEN_CURSOR_24_8_PRE_MULT) |
			   EVERGREEN_CURSOR_URGENT_CONTROL(EVERGREEN_CURSOR_URGENT_1_2));
	} else if (ASIC_IS_AVIVO(rdev)) {
		WREG32_IDX(AVIVO_D1CUR_CONTROL + radeon_crtc->crtc_offset,
			   (AVIVO_D1CURSOR_MODE_24BPP << AVIVO_D1CURSOR_MODE_SHIFT));
	} else {
		u32 reg;
		switch (radeon_crtc->crtc_id) {
		case 0:
			reg = RADEON_CRTC_GEN_CNTL;
			break;
		case 1:
			reg = RADEON_CRTC2_GEN_CNTL;
			break;
		default:
			return;
		}
		WREG32_IDX(reg, RREG32_IDX(reg) & ~RADEON_CRTC_CUR_EN);
	}
}

static void radeon_show_cursor(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_device *rdev = crtc->dev->dev_private;

	if (radeon_crtc->cursor_out_of_bounds)
		return;

	if (ASIC_IS_DCE4(rdev)) {
		WREG32(EVERGREEN_CUR_SURFACE_ADDRESS_HIGH + radeon_crtc->crtc_offset,
		       upper_32_bits(radeon_crtc->cursor_addr));
		WREG32(EVERGREEN_CUR_SURFACE_ADDRESS + radeon_crtc->crtc_offset,
		       lower_32_bits(radeon_crtc->cursor_addr));
		WREG32(RADEON_MM_INDEX, EVERGREEN_CUR_CONTROL + radeon_crtc->crtc_offset);
		WREG32(RADEON_MM_DATA, EVERGREEN_CURSOR_EN |
		       EVERGREEN_CURSOR_MODE(EVERGREEN_CURSOR_24_8_PRE_MULT) |
		       EVERGREEN_CURSOR_URGENT_CONTROL(EVERGREEN_CURSOR_URGENT_1_2));
	} else if (ASIC_IS_AVIVO(rdev)) {
		if (rdev->family >= CHIP_RV770) {
			if (radeon_crtc->crtc_id)
				WREG32(R700_D2CUR_SURFACE_ADDRESS_HIGH,
				       upper_32_bits(radeon_crtc->cursor_addr));
			else
				WREG32(R700_D1CUR_SURFACE_ADDRESS_HIGH,
				       upper_32_bits(radeon_crtc->cursor_addr));
		}

		WREG32(AVIVO_D1CUR_SURFACE_ADDRESS + radeon_crtc->crtc_offset,
		       lower_32_bits(radeon_crtc->cursor_addr));
		WREG32(RADEON_MM_INDEX, AVIVO_D1CUR_CONTROL + radeon_crtc->crtc_offset);
		WREG32(RADEON_MM_DATA, AVIVO_D1CURSOR_EN |
		       (AVIVO_D1CURSOR_MODE_24BPP << AVIVO_D1CURSOR_MODE_SHIFT));
	} else {
		/* offset is from DISP(2)_BASE_ADDRESS */
		WREG32(RADEON_CUR_OFFSET + radeon_crtc->crtc_offset,
		       radeon_crtc->cursor_addr - radeon_crtc->legacy_display_base_addr);

		switch (radeon_crtc->crtc_id) {
		case 0:
			WREG32(RADEON_MM_INDEX, RADEON_CRTC_GEN_CNTL);
			break;
		case 1:
			WREG32(RADEON_MM_INDEX, RADEON_CRTC2_GEN_CNTL);
			break;
		default:
			return;
		}

		WREG32_P(RADEON_MM_DATA, (RADEON_CRTC_CUR_EN |
					  (RADEON_CRTC_CUR_MODE_24BPP << RADEON_CRTC_CUR_MODE_SHIFT)),
			 ~(RADEON_CRTC_CUR_EN | RADEON_CRTC_CUR_MODE_MASK));
	}
}

static int radeon_cursor_move_locked(struct drm_crtc *crtc, int x, int y)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_device *rdev = crtc->dev->dev_private;
	int xorigin = 0, yorigin = 0;
	int w = radeon_crtc->cursor_width;

	radeon_crtc->cursor_x = x;
	radeon_crtc->cursor_y = y;

	if (ASIC_IS_AVIVO(rdev)) {
		/* avivo cursor are offset into the total surface */
		x += crtc->x;
		y += crtc->y;
	}

	if (x < 0)
		xorigin = min(-x, radeon_crtc->max_cursor_width - 1);
	if (y < 0)
		yorigin = min(-y, radeon_crtc->max_cursor_height - 1);

	if (!ASIC_IS_AVIVO(rdev)) {
		x += crtc->x;
		y += crtc->y;
	}
	DRM_DEBUG("x %d y %d c->x %d c->y %d\n", x, y, crtc->x, crtc->y);

	/* fixed on DCE6 and newer */
	if (ASIC_IS_AVIVO(rdev) && !ASIC_IS_DCE6(rdev)) {
		int i = 0;
		struct drm_crtc *crtc_p;

		/*
		 * avivo cursor image can't end on 128 pixel boundary or
		 * go past the end of the frame if both crtcs are enabled
		 *
		 * NOTE: It is safe to access crtc->enabled of other crtcs
		 * without holding either the mode_config lock or the other
		 * crtc's lock as long as write access to this flag _always_
		 * grabs all locks.
		 */
		list_for_each_entry(crtc_p, &crtc->dev->mode_config.crtc_list, head) {
			if (crtc_p->enabled)
				i++;
		}
		if (i > 1) {
			int cursor_end, frame_end;

			cursor_end = x + w;
			frame_end = crtc->x + crtc->mode.crtc_hdisplay;
			if (cursor_end >= frame_end) {
				w = w - (cursor_end - frame_end);
				if (!(frame_end & 0x7f))
					w--;
			} else if (cursor_end <= 0) {
				goto out_of_bounds;
			} else if (!(cursor_end & 0x7f)) {
				w--;
			}
			if (w <= 0) {
				goto out_of_bounds;
			}
		}
	}

	if (x <= (crtc->x - w) || y <= (crtc->y - radeon_crtc->cursor_height) ||
	    x >= (crtc->x + crtc->mode.crtc_hdisplay) ||
	    y >= (crtc->y + crtc->mode.crtc_vdisplay))
		goto out_of_bounds;

	x += xorigin;
	y += yorigin;

	if (ASIC_IS_DCE4(rdev)) {
		WREG32(EVERGREEN_CUR_POSITION + radeon_crtc->crtc_offset, (x << 16) | y);
		WREG32(EVERGREEN_CUR_HOT_SPOT + radeon_crtc->crtc_offset, (xorigin << 16) | yorigin);
		WREG32(EVERGREEN_CUR_SIZE + radeon_crtc->crtc_offset,
		       ((w - 1) << 16) | (radeon_crtc->cursor_height - 1));
	} else if (ASIC_IS_AVIVO(rdev)) {
		WREG32(AVIVO_D1CUR_POSITION + radeon_crtc->crtc_offset, (x << 16) | y);
		WREG32(AVIVO_D1CUR_HOT_SPOT + radeon_crtc->crtc_offset, (xorigin << 16) | yorigin);
		WREG32(AVIVO_D1CUR_SIZE + radeon_crtc->crtc_offset,
		       ((w - 1) << 16) | (radeon_crtc->cursor_height - 1));
	} else {
		x -= crtc->x;
		y -= crtc->y;

		if (crtc->mode.flags & DRM_MODE_FLAG_DBLSCAN)
			y *= 2;

		WREG32(RADEON_CUR_HORZ_VERT_OFF + radeon_crtc->crtc_offset,
		       (RADEON_CUR_LOCK
			| (xorigin << 16)
			| yorigin));
		WREG32(RADEON_CUR_HORZ_VERT_POSN + radeon_crtc->crtc_offset,
		       (RADEON_CUR_LOCK
			| (x << 16)
			| y));
		/* offset is from DISP(2)_BASE_ADDRESS */
		WREG32(RADEON_CUR_OFFSET + radeon_crtc->crtc_offset,
		       radeon_crtc->cursor_addr - radeon_crtc->legacy_display_base_addr +
		       yorigin * 256);
	}

	if (radeon_crtc->cursor_out_of_bounds) {
		radeon_crtc->cursor_out_of_bounds = false;
		if (radeon_crtc->cursor_bo)
			radeon_show_cursor(crtc);
	}

	return 0;

 out_of_bounds:
	if (!radeon_crtc->cursor_out_of_bounds) {
		radeon_hide_cursor(crtc);
		radeon_crtc->cursor_out_of_bounds = true;
	}
	return 0;
}

int radeon_crtc_cursor_move(struct drm_crtc *crtc,
			    int x, int y)
{
	int ret;

	radeon_lock_cursor(crtc, true);
	ret = radeon_cursor_move_locked(crtc, x, y);
	radeon_lock_cursor(crtc, false);

	return ret;
}

int radeon_crtc_cursor_set2(struct drm_crtc *crtc,
			    struct drm_file *file_priv,
			    uint32_t handle,
			    uint32_t width,
			    uint32_t height,
			    int32_t hot_x,
			    int32_t hot_y)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_device *rdev = crtc->dev->dev_private;
	struct drm_gem_object *obj;
	struct radeon_bo *robj;
	int ret;

	if (!handle) {
		/* turn off cursor */
		radeon_hide_cursor(crtc);
		obj = NULL;
		goto unpin;
	}

	if ((width > radeon_crtc->max_cursor_width) ||
	    (height > radeon_crtc->max_cursor_height)) {
		DRM_ERROR("bad cursor width or height %d x %d\n", width, height);
		return -EINVAL;
	}

	obj = drm_gem_object_lookup(crtc->dev, file_priv, handle);
	if (!obj) {
		DRM_ERROR("Cannot find cursor object %x for crtc %d\n", handle, radeon_crtc->crtc_id);
		return -ENOENT;
	}

	robj = gem_to_radeon_bo(obj);
	ret = radeon_bo_reserve(robj, false);
	if (ret != 0) {
		drm_gem_object_unreference_unlocked(obj);
		return ret;
	}
	/* Only 27 bit offset for legacy cursor */
	ret = radeon_bo_pin_restricted(robj, RADEON_GEM_DOMAIN_VRAM,
				       ASIC_IS_AVIVO(rdev) ? 0 : 1 << 27,
				       &radeon_crtc->cursor_addr);
	radeon_bo_unreserve(robj);
	if (ret) {
		DRM_ERROR("Failed to pin new cursor BO (%d)\n", ret);
		drm_gem_object_unreference_unlocked(obj);
		return ret;
	}

	radeon_lock_cursor(crtc, true);

	if (width != radeon_crtc->cursor_width ||
	    height != radeon_crtc->cursor_height ||
	    hot_x != radeon_crtc->cursor_hot_x ||
	    hot_y != radeon_crtc->cursor_hot_y) {
		int x, y;

		x = radeon_crtc->cursor_x + radeon_crtc->cursor_hot_x - hot_x;
		y = radeon_crtc->cursor_y + radeon_crtc->cursor_hot_y - hot_y;

		radeon_crtc->cursor_width = width;
		radeon_crtc->cursor_height = height;
		radeon_crtc->cursor_hot_x = hot_x;
		radeon_crtc->cursor_hot_y = hot_y;

		radeon_cursor_move_locked(crtc, x, y);
	}

	radeon_show_cursor(crtc);

	radeon_lock_cursor(crtc, false);

unpin:
	if (radeon_crtc->cursor_bo) {
		struct radeon_bo *robj = gem_to_radeon_bo(radeon_crtc->cursor_bo);
		ret = radeon_bo_reserve(robj, false);
		if (likely(ret == 0)) {
			radeon_bo_unpin(robj);
			radeon_bo_unreserve(robj);
		}
		drm_gem_object_unreference_unlocked(radeon_crtc->cursor_bo);
	}

	radeon_crtc->cursor_bo = obj;
	return 0;
}

/**
 * radeon_cursor_reset - Re-set the current cursor, if any.
 *
 * @crtc: drm crtc
 *
 * If the CRTC passed in currently has a cursor assigned, this function
 * makes sure it's visible.
 */
void radeon_cursor_reset(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);

	if (radeon_crtc->cursor_bo) {
		radeon_lock_cursor(crtc, true);

		radeon_cursor_move_locked(crtc, radeon_crtc->cursor_x,
					  radeon_crtc->cursor_y);

		radeon_show_cursor(crtc);

		radeon_lock_cursor(crtc, false);
	}
}

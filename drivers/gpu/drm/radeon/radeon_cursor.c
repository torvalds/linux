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
#include "drmP.h"
#include "radeon_drm.h"
#include "radeon.h"

#define CURSOR_WIDTH 64
#define CURSOR_HEIGHT 64

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
		WREG32(RADEON_MM_INDEX, EVERGREEN_CUR_CONTROL + radeon_crtc->crtc_offset);
		WREG32(RADEON_MM_DATA, EVERGREEN_CURSOR_MODE(EVERGREEN_CURSOR_24_8_PRE_MULT));
	} else if (ASIC_IS_AVIVO(rdev)) {
		WREG32(RADEON_MM_INDEX, AVIVO_D1CUR_CONTROL + radeon_crtc->crtc_offset);
		WREG32(RADEON_MM_DATA, (AVIVO_D1CURSOR_MODE_24BPP << AVIVO_D1CURSOR_MODE_SHIFT));
	} else {
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
		WREG32_P(RADEON_MM_DATA, 0, ~RADEON_CRTC_CUR_EN);
	}
}

static void radeon_show_cursor(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_device *rdev = crtc->dev->dev_private;

	if (ASIC_IS_DCE4(rdev)) {
		WREG32(RADEON_MM_INDEX, EVERGREEN_CUR_CONTROL + radeon_crtc->crtc_offset);
		WREG32(RADEON_MM_DATA, EVERGREEN_CURSOR_EN |
		       EVERGREEN_CURSOR_MODE(EVERGREEN_CURSOR_24_8_PRE_MULT));
	} else if (ASIC_IS_AVIVO(rdev)) {
		WREG32(RADEON_MM_INDEX, AVIVO_D1CUR_CONTROL + radeon_crtc->crtc_offset);
		WREG32(RADEON_MM_DATA, AVIVO_D1CURSOR_EN |
		       (AVIVO_D1CURSOR_MODE_24BPP << AVIVO_D1CURSOR_MODE_SHIFT));
	} else {
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

static void radeon_set_cursor(struct drm_crtc *crtc, struct drm_gem_object *obj,
			      uint64_t gpu_addr)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_device *rdev = crtc->dev->dev_private;

	if (ASIC_IS_DCE4(rdev)) {
		WREG32(EVERGREEN_CUR_SURFACE_ADDRESS_HIGH + radeon_crtc->crtc_offset,
		       upper_32_bits(gpu_addr));
		WREG32(EVERGREEN_CUR_SURFACE_ADDRESS + radeon_crtc->crtc_offset,
		       gpu_addr & 0xffffffff);
	} else if (ASIC_IS_AVIVO(rdev)) {
		if (rdev->family >= CHIP_RV770) {
			if (radeon_crtc->crtc_id)
				WREG32(R700_D2CUR_SURFACE_ADDRESS_HIGH, upper_32_bits(gpu_addr));
			else
				WREG32(R700_D1CUR_SURFACE_ADDRESS_HIGH, upper_32_bits(gpu_addr));
		}
		WREG32(AVIVO_D1CUR_SURFACE_ADDRESS + radeon_crtc->crtc_offset,
		       gpu_addr & 0xffffffff);
	} else {
		radeon_crtc->legacy_cursor_offset = gpu_addr - radeon_crtc->legacy_display_base_addr;
		/* offset is from DISP(2)_BASE_ADDRESS */
		WREG32(RADEON_CUR_OFFSET + radeon_crtc->crtc_offset, radeon_crtc->legacy_cursor_offset);
	}
}

int radeon_crtc_cursor_set(struct drm_crtc *crtc,
			   struct drm_file *file_priv,
			   uint32_t handle,
			   uint32_t width,
			   uint32_t height)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_gem_object *obj;
	uint64_t gpu_addr;
	int ret;

	if (!handle) {
		/* turn off cursor */
		radeon_hide_cursor(crtc);
		obj = NULL;
		goto unpin;
	}

	if ((width > CURSOR_WIDTH) || (height > CURSOR_HEIGHT)) {
		DRM_ERROR("bad cursor width or height %d x %d\n", width, height);
		return -EINVAL;
	}

	obj = drm_gem_object_lookup(crtc->dev, file_priv, handle);
	if (!obj) {
		DRM_ERROR("Cannot find cursor object %x for crtc %d\n", handle, radeon_crtc->crtc_id);
		return -ENOENT;
	}

	ret = radeon_gem_object_pin(obj, RADEON_GEM_DOMAIN_VRAM, &gpu_addr);
	if (ret)
		goto fail;

	radeon_crtc->cursor_width = width;
	radeon_crtc->cursor_height = height;

	radeon_lock_cursor(crtc, true);
	/* XXX only 27 bit offset for legacy cursor */
	radeon_set_cursor(crtc, obj, gpu_addr);
	radeon_show_cursor(crtc);
	radeon_lock_cursor(crtc, false);

unpin:
	if (radeon_crtc->cursor_bo) {
		radeon_gem_object_unpin(radeon_crtc->cursor_bo);
		drm_gem_object_unreference_unlocked(radeon_crtc->cursor_bo);
	}

	radeon_crtc->cursor_bo = obj;
	return 0;
fail:
	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

int radeon_crtc_cursor_move(struct drm_crtc *crtc,
			    int x, int y)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_device *rdev = crtc->dev->dev_private;
	int xorigin = 0, yorigin = 0;
	int w = radeon_crtc->cursor_width;

	if (ASIC_IS_AVIVO(rdev)) {
		/* avivo cursor are offset into the total surface */
		x += crtc->x;
		y += crtc->y;
	}
	DRM_DEBUG("x %d y %d c->x %d c->y %d\n", x, y, crtc->x, crtc->y);

	if (x < 0)
		xorigin = -x + 1;
	if (y < 0)
		yorigin = -y + 1;
	if (xorigin >= CURSOR_WIDTH)
		xorigin = CURSOR_WIDTH - 1;
	if (yorigin >= CURSOR_HEIGHT)
		yorigin = CURSOR_HEIGHT - 1;

	if (ASIC_IS_AVIVO(rdev)) {
		int i = 0;
		struct drm_crtc *crtc_p;

		/* avivo cursor image can't end on 128 pixel boundary or
		 * go past the end of the frame if both crtcs are enabled
		 */
		list_for_each_entry(crtc_p, &crtc->dev->mode_config.crtc_list, head) {
			if (crtc_p->enabled)
				i++;
		}
		if (i > 1) {
			int cursor_end, frame_end;

			cursor_end = x - xorigin + w;
			frame_end = crtc->x + crtc->mode.crtc_hdisplay;
			if (cursor_end >= frame_end) {
				w = w - (cursor_end - frame_end);
				if (!(frame_end & 0x7f))
					w--;
			} else {
				if (!(cursor_end & 0x7f))
					w--;
			}
			if (w <= 0)
				w = 1;
		}
	}

	radeon_lock_cursor(crtc, true);
	if (ASIC_IS_DCE4(rdev)) {
		WREG32(EVERGREEN_CUR_POSITION + radeon_crtc->crtc_offset,
		       ((xorigin ? 0 : x) << 16) |
		       (yorigin ? 0 : y));
		WREG32(EVERGREEN_CUR_HOT_SPOT + radeon_crtc->crtc_offset, (xorigin << 16) | yorigin);
		WREG32(EVERGREEN_CUR_SIZE + radeon_crtc->crtc_offset,
		       ((w - 1) << 16) | (radeon_crtc->cursor_height - 1));
	} else if (ASIC_IS_AVIVO(rdev)) {
		WREG32(AVIVO_D1CUR_POSITION + radeon_crtc->crtc_offset,
			     ((xorigin ? 0 : x) << 16) |
			     (yorigin ? 0 : y));
		WREG32(AVIVO_D1CUR_HOT_SPOT + radeon_crtc->crtc_offset, (xorigin << 16) | yorigin);
		WREG32(AVIVO_D1CUR_SIZE + radeon_crtc->crtc_offset,
		       ((w - 1) << 16) | (radeon_crtc->cursor_height - 1));
	} else {
		if (crtc->mode.flags & DRM_MODE_FLAG_DBLSCAN)
			y *= 2;

		WREG32(RADEON_CUR_HORZ_VERT_OFF + radeon_crtc->crtc_offset,
		       (RADEON_CUR_LOCK
			| (xorigin << 16)
			| yorigin));
		WREG32(RADEON_CUR_HORZ_VERT_POSN + radeon_crtc->crtc_offset,
		       (RADEON_CUR_LOCK
			| ((xorigin ? 0 : x) << 16)
			| (yorigin ? 0 : y)));
		/* offset is from DISP(2)_BASE_ADDRESS */
		WREG32(RADEON_CUR_OFFSET + radeon_crtc->crtc_offset, (radeon_crtc->legacy_cursor_offset +
								      (yorigin * 256)));
	}
	radeon_lock_cursor(crtc, false);

	return 0;
}

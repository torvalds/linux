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

#include "atom.h"
#include <asm/div64.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>

static void avivo_crtc_load_lut(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	int i;

	DRM_DEBUG_KMS("%d\n", radeon_crtc->crtc_id);
	WREG32(AVIVO_DC_LUTA_CONTROL + radeon_crtc->crtc_offset, 0);

	WREG32(AVIVO_DC_LUTA_BLACK_OFFSET_BLUE + radeon_crtc->crtc_offset, 0);
	WREG32(AVIVO_DC_LUTA_BLACK_OFFSET_GREEN + radeon_crtc->crtc_offset, 0);
	WREG32(AVIVO_DC_LUTA_BLACK_OFFSET_RED + radeon_crtc->crtc_offset, 0);

	WREG32(AVIVO_DC_LUTA_WHITE_OFFSET_BLUE + radeon_crtc->crtc_offset, 0xffff);
	WREG32(AVIVO_DC_LUTA_WHITE_OFFSET_GREEN + radeon_crtc->crtc_offset, 0xffff);
	WREG32(AVIVO_DC_LUTA_WHITE_OFFSET_RED + radeon_crtc->crtc_offset, 0xffff);

	WREG32(AVIVO_DC_LUT_RW_SELECT, radeon_crtc->crtc_id);
	WREG32(AVIVO_DC_LUT_RW_MODE, 0);
	WREG32(AVIVO_DC_LUT_WRITE_EN_MASK, 0x0000003f);

	WREG8(AVIVO_DC_LUT_RW_INDEX, 0);
	for (i = 0; i < 256; i++) {
		WREG32(AVIVO_DC_LUT_30_COLOR,
			     (radeon_crtc->lut_r[i] << 20) |
			     (radeon_crtc->lut_g[i] << 10) |
			     (radeon_crtc->lut_b[i] << 0));
	}

	WREG32(AVIVO_D1GRPH_LUT_SEL + radeon_crtc->crtc_offset, radeon_crtc->crtc_id);
}

static void dce4_crtc_load_lut(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	int i;

	DRM_DEBUG_KMS("%d\n", radeon_crtc->crtc_id);
	WREG32(EVERGREEN_DC_LUT_CONTROL + radeon_crtc->crtc_offset, 0);

	WREG32(EVERGREEN_DC_LUT_BLACK_OFFSET_BLUE + radeon_crtc->crtc_offset, 0);
	WREG32(EVERGREEN_DC_LUT_BLACK_OFFSET_GREEN + radeon_crtc->crtc_offset, 0);
	WREG32(EVERGREEN_DC_LUT_BLACK_OFFSET_RED + radeon_crtc->crtc_offset, 0);

	WREG32(EVERGREEN_DC_LUT_WHITE_OFFSET_BLUE + radeon_crtc->crtc_offset, 0xffff);
	WREG32(EVERGREEN_DC_LUT_WHITE_OFFSET_GREEN + radeon_crtc->crtc_offset, 0xffff);
	WREG32(EVERGREEN_DC_LUT_WHITE_OFFSET_RED + radeon_crtc->crtc_offset, 0xffff);

	WREG32(EVERGREEN_DC_LUT_RW_MODE + radeon_crtc->crtc_offset, 0);
	WREG32(EVERGREEN_DC_LUT_WRITE_EN_MASK + radeon_crtc->crtc_offset, 0x00000007);

	WREG32(EVERGREEN_DC_LUT_RW_INDEX + radeon_crtc->crtc_offset, 0);
	for (i = 0; i < 256; i++) {
		WREG32(EVERGREEN_DC_LUT_30_COLOR + radeon_crtc->crtc_offset,
		       (radeon_crtc->lut_r[i] << 20) |
		       (radeon_crtc->lut_g[i] << 10) |
		       (radeon_crtc->lut_b[i] << 0));
	}
}

static void dce5_crtc_load_lut(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	int i;

	DRM_DEBUG_KMS("%d\n", radeon_crtc->crtc_id);

	WREG32(NI_INPUT_CSC_CONTROL + radeon_crtc->crtc_offset,
	       (NI_INPUT_CSC_GRPH_MODE(NI_INPUT_CSC_BYPASS) |
		NI_INPUT_CSC_OVL_MODE(NI_INPUT_CSC_BYPASS)));
	WREG32(NI_PRESCALE_GRPH_CONTROL + radeon_crtc->crtc_offset,
	       NI_GRPH_PRESCALE_BYPASS);
	WREG32(NI_PRESCALE_OVL_CONTROL + radeon_crtc->crtc_offset,
	       NI_OVL_PRESCALE_BYPASS);
	WREG32(NI_INPUT_GAMMA_CONTROL + radeon_crtc->crtc_offset,
	       (NI_GRPH_INPUT_GAMMA_MODE(NI_INPUT_GAMMA_USE_LUT) |
		NI_OVL_INPUT_GAMMA_MODE(NI_INPUT_GAMMA_USE_LUT)));

	WREG32(EVERGREEN_DC_LUT_CONTROL + radeon_crtc->crtc_offset, 0);

	WREG32(EVERGREEN_DC_LUT_BLACK_OFFSET_BLUE + radeon_crtc->crtc_offset, 0);
	WREG32(EVERGREEN_DC_LUT_BLACK_OFFSET_GREEN + radeon_crtc->crtc_offset, 0);
	WREG32(EVERGREEN_DC_LUT_BLACK_OFFSET_RED + radeon_crtc->crtc_offset, 0);

	WREG32(EVERGREEN_DC_LUT_WHITE_OFFSET_BLUE + radeon_crtc->crtc_offset, 0xffff);
	WREG32(EVERGREEN_DC_LUT_WHITE_OFFSET_GREEN + radeon_crtc->crtc_offset, 0xffff);
	WREG32(EVERGREEN_DC_LUT_WHITE_OFFSET_RED + radeon_crtc->crtc_offset, 0xffff);

	WREG32(EVERGREEN_DC_LUT_RW_MODE + radeon_crtc->crtc_offset, 0);
	WREG32(EVERGREEN_DC_LUT_WRITE_EN_MASK + radeon_crtc->crtc_offset, 0x00000007);

	WREG32(EVERGREEN_DC_LUT_RW_INDEX + radeon_crtc->crtc_offset, 0);
	for (i = 0; i < 256; i++) {
		WREG32(EVERGREEN_DC_LUT_30_COLOR + radeon_crtc->crtc_offset,
		       (radeon_crtc->lut_r[i] << 20) |
		       (radeon_crtc->lut_g[i] << 10) |
		       (radeon_crtc->lut_b[i] << 0));
	}

	WREG32(NI_DEGAMMA_CONTROL + radeon_crtc->crtc_offset,
	       (NI_GRPH_DEGAMMA_MODE(NI_DEGAMMA_BYPASS) |
		NI_OVL_DEGAMMA_MODE(NI_DEGAMMA_BYPASS) |
		NI_ICON_DEGAMMA_MODE(NI_DEGAMMA_BYPASS) |
		NI_CURSOR_DEGAMMA_MODE(NI_DEGAMMA_BYPASS)));
	WREG32(NI_GAMUT_REMAP_CONTROL + radeon_crtc->crtc_offset,
	       (NI_GRPH_GAMUT_REMAP_MODE(NI_GAMUT_REMAP_BYPASS) |
		NI_OVL_GAMUT_REMAP_MODE(NI_GAMUT_REMAP_BYPASS)));
	WREG32(NI_REGAMMA_CONTROL + radeon_crtc->crtc_offset,
	       (NI_GRPH_REGAMMA_MODE(NI_REGAMMA_BYPASS) |
		NI_OVL_REGAMMA_MODE(NI_REGAMMA_BYPASS)));
	WREG32(NI_OUTPUT_CSC_CONTROL + radeon_crtc->crtc_offset,
	       (NI_OUTPUT_CSC_GRPH_MODE(NI_OUTPUT_CSC_BYPASS) |
		NI_OUTPUT_CSC_OVL_MODE(NI_OUTPUT_CSC_BYPASS)));
	/* XXX match this to the depth of the crtc fmt block, move to modeset? */
	WREG32(0x6940 + radeon_crtc->crtc_offset, 0);

}

static void legacy_crtc_load_lut(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	int i;
	uint32_t dac2_cntl;

	dac2_cntl = RREG32(RADEON_DAC_CNTL2);
	if (radeon_crtc->crtc_id == 0)
		dac2_cntl &= (uint32_t)~RADEON_DAC2_PALETTE_ACC_CTL;
	else
		dac2_cntl |= RADEON_DAC2_PALETTE_ACC_CTL;
	WREG32(RADEON_DAC_CNTL2, dac2_cntl);

	WREG8(RADEON_PALETTE_INDEX, 0);
	for (i = 0; i < 256; i++) {
		WREG32(RADEON_PALETTE_30_DATA,
			     (radeon_crtc->lut_r[i] << 20) |
			     (radeon_crtc->lut_g[i] << 10) |
			     (radeon_crtc->lut_b[i] << 0));
	}
}

void radeon_crtc_load_lut(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;

	if (!crtc->enabled)
		return;

	if (ASIC_IS_DCE5(rdev))
		dce5_crtc_load_lut(crtc);
	else if (ASIC_IS_DCE4(rdev))
		dce4_crtc_load_lut(crtc);
	else if (ASIC_IS_AVIVO(rdev))
		avivo_crtc_load_lut(crtc);
	else
		legacy_crtc_load_lut(crtc);
}

/** Sets the color ramps on behalf of fbcon */
void radeon_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
			      u16 blue, int regno)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);

	radeon_crtc->lut_r[regno] = red >> 6;
	radeon_crtc->lut_g[regno] = green >> 6;
	radeon_crtc->lut_b[regno] = blue >> 6;
}

/** Gets the color ramps on behalf of fbcon */
void radeon_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
			      u16 *blue, int regno)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);

	*red = radeon_crtc->lut_r[regno] << 6;
	*green = radeon_crtc->lut_g[regno] << 6;
	*blue = radeon_crtc->lut_b[regno] << 6;
}

static void radeon_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
				  u16 *blue, uint32_t start, uint32_t size)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	int end = (start + size > 256) ? 256 : start + size, i;

	/* userspace palettes are always correct as is */
	for (i = start; i < end; i++) {
		radeon_crtc->lut_r[i] = red[i] >> 6;
		radeon_crtc->lut_g[i] = green[i] >> 6;
		radeon_crtc->lut_b[i] = blue[i] >> 6;
	}
	radeon_crtc_load_lut(crtc);
}

static void radeon_crtc_destroy(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(radeon_crtc);
}

/*
 * Handle unpin events outside the interrupt handler proper.
 */
static void radeon_unpin_work_func(struct work_struct *__work)
{
	struct radeon_unpin_work *work =
		container_of(__work, struct radeon_unpin_work, work);
	int r;

	/* unpin of the old buffer */
	r = radeon_bo_reserve(work->old_rbo, false);
	if (likely(r == 0)) {
		r = radeon_bo_unpin(work->old_rbo);
		if (unlikely(r != 0)) {
			DRM_ERROR("failed to unpin buffer after flip\n");
		}
		radeon_bo_unreserve(work->old_rbo);
	} else
		DRM_ERROR("failed to reserve buffer after flip\n");

	drm_gem_object_unreference_unlocked(&work->old_rbo->gem_base);
	kfree(work);
}

void radeon_crtc_handle_flip(struct radeon_device *rdev, int crtc_id)
{
	struct radeon_crtc *radeon_crtc = rdev->mode_info.crtcs[crtc_id];
	struct radeon_unpin_work *work;
	struct drm_pending_vblank_event *e;
	struct timeval now;
	unsigned long flags;
	u32 update_pending;
	int vpos, hpos;

	spin_lock_irqsave(&rdev->ddev->event_lock, flags);
	work = radeon_crtc->unpin_work;
	if (work == NULL ||
	    (work->fence && !radeon_fence_signaled(work->fence))) {
		spin_unlock_irqrestore(&rdev->ddev->event_lock, flags);
		return;
	}
	/* New pageflip, or just completion of a previous one? */
	if (!radeon_crtc->deferred_flip_completion) {
		/* do the flip (mmio) */
		update_pending = radeon_page_flip(rdev, crtc_id, work->new_crtc_base);
	} else {
		/* This is just a completion of a flip queued in crtc
		 * at last invocation. Make sure we go directly to
		 * completion routine.
		 */
		update_pending = 0;
		radeon_crtc->deferred_flip_completion = 0;
	}

	/* Has the pageflip already completed in crtc, or is it certain
	 * to complete in this vblank?
	 */
	if (update_pending &&
	    (DRM_SCANOUTPOS_VALID & radeon_get_crtc_scanoutpos(rdev->ddev, crtc_id,
							       &vpos, &hpos)) &&
	    ((vpos >= (99 * rdev->mode_info.crtcs[crtc_id]->base.hwmode.crtc_vdisplay)/100) ||
	     (vpos < 0 && !ASIC_IS_AVIVO(rdev)))) {
		/* crtc didn't flip in this target vblank interval,
		 * but flip is pending in crtc. Based on the current
		 * scanout position we know that the current frame is
		 * (nearly) complete and the flip will (likely)
		 * complete before the start of the next frame.
		 */
		update_pending = 0;
	}
	if (update_pending) {
		/* crtc didn't flip in this target vblank interval,
		 * but flip is pending in crtc. It will complete it
		 * in next vblank interval, so complete the flip at
		 * next vblank irq.
		 */
		radeon_crtc->deferred_flip_completion = 1;
		spin_unlock_irqrestore(&rdev->ddev->event_lock, flags);
		return;
	}

	/* Pageflip (will be) certainly completed in this vblank. Clean up. */
	radeon_crtc->unpin_work = NULL;

	/* wakeup userspace */
	if (work->event) {
		e = work->event;
		e->event.sequence = drm_vblank_count_and_time(rdev->ddev, crtc_id, &now);
		e->event.tv_sec = now.tv_sec;
		e->event.tv_usec = now.tv_usec;
		list_add_tail(&e->base.link, &e->base.file_priv->event_list);
		wake_up_interruptible(&e->base.file_priv->event_wait);
	}
	spin_unlock_irqrestore(&rdev->ddev->event_lock, flags);

	drm_vblank_put(rdev->ddev, radeon_crtc->crtc_id);
	radeon_fence_unref(&work->fence);
	radeon_post_page_flip(work->rdev, work->crtc_id);
	schedule_work(&work->work);
}

static int radeon_crtc_page_flip(struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_pending_vblank_event *event)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_framebuffer *old_radeon_fb;
	struct radeon_framebuffer *new_radeon_fb;
	struct drm_gem_object *obj;
	struct radeon_bo *rbo;
	struct radeon_unpin_work *work;
	unsigned long flags;
	u32 tiling_flags, pitch_pixels;
	u64 base;
	int r;

	work = kzalloc(sizeof *work, GFP_KERNEL);
	if (work == NULL)
		return -ENOMEM;

	work->event = event;
	work->rdev = rdev;
	work->crtc_id = radeon_crtc->crtc_id;
	old_radeon_fb = to_radeon_framebuffer(crtc->fb);
	new_radeon_fb = to_radeon_framebuffer(fb);
	/* schedule unpin of the old buffer */
	obj = old_radeon_fb->obj;
	/* take a reference to the old object */
	drm_gem_object_reference(obj);
	rbo = gem_to_radeon_bo(obj);
	work->old_rbo = rbo;
	obj = new_radeon_fb->obj;
	rbo = gem_to_radeon_bo(obj);

	spin_lock(&rbo->tbo.bdev->fence_lock);
	if (rbo->tbo.sync_obj)
		work->fence = radeon_fence_ref(rbo->tbo.sync_obj);
	spin_unlock(&rbo->tbo.bdev->fence_lock);

	INIT_WORK(&work->work, radeon_unpin_work_func);

	/* We borrow the event spin lock for protecting unpin_work */
	spin_lock_irqsave(&dev->event_lock, flags);
	if (radeon_crtc->unpin_work) {
		DRM_DEBUG_DRIVER("flip queue: crtc already busy\n");
		r = -EBUSY;
		goto unlock_free;
	}
	radeon_crtc->unpin_work = work;
	radeon_crtc->deferred_flip_completion = 0;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	/* pin the new buffer */
	DRM_DEBUG_DRIVER("flip-ioctl() cur_fbo = %p, cur_bbo = %p\n",
			 work->old_rbo, rbo);

	r = radeon_bo_reserve(rbo, false);
	if (unlikely(r != 0)) {
		DRM_ERROR("failed to reserve new rbo buffer before flip\n");
		goto pflip_cleanup;
	}
	/* Only 27 bit offset for legacy CRTC */
	r = radeon_bo_pin_restricted(rbo, RADEON_GEM_DOMAIN_VRAM,
				     ASIC_IS_AVIVO(rdev) ? 0 : 1 << 27, &base);
	if (unlikely(r != 0)) {
		radeon_bo_unreserve(rbo);
		r = -EINVAL;
		DRM_ERROR("failed to pin new rbo buffer before flip\n");
		goto pflip_cleanup;
	}
	radeon_bo_get_tiling_flags(rbo, &tiling_flags, NULL);
	radeon_bo_unreserve(rbo);

	if (!ASIC_IS_AVIVO(rdev)) {
		/* crtc offset is from display base addr not FB location */
		base -= radeon_crtc->legacy_display_base_addr;
		pitch_pixels = fb->pitches[0] / (fb->bits_per_pixel / 8);

		if (tiling_flags & RADEON_TILING_MACRO) {
			if (ASIC_IS_R300(rdev)) {
				base &= ~0x7ff;
			} else {
				int byteshift = fb->bits_per_pixel >> 4;
				int tile_addr = (((crtc->y >> 3) * pitch_pixels +  crtc->x) >> (8 - byteshift)) << 11;
				base += tile_addr + ((crtc->x << byteshift) % 256) + ((crtc->y % 8) << 8);
			}
		} else {
			int offset = crtc->y * pitch_pixels + crtc->x;
			switch (fb->bits_per_pixel) {
			case 8:
			default:
				offset *= 1;
				break;
			case 15:
			case 16:
				offset *= 2;
				break;
			case 24:
				offset *= 3;
				break;
			case 32:
				offset *= 4;
				break;
			}
			base += offset;
		}
		base &= ~7;
	}

	spin_lock_irqsave(&dev->event_lock, flags);
	work->new_crtc_base = base;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	/* update crtc fb */
	crtc->fb = fb;

	r = drm_vblank_get(dev, radeon_crtc->crtc_id);
	if (r) {
		DRM_ERROR("failed to get vblank before flip\n");
		goto pflip_cleanup1;
	}

	/* set the proper interrupt */
	radeon_pre_page_flip(rdev, radeon_crtc->crtc_id);

	return 0;

pflip_cleanup1:
	if (unlikely(radeon_bo_reserve(rbo, false) != 0)) {
		DRM_ERROR("failed to reserve new rbo in error path\n");
		goto pflip_cleanup;
	}
	if (unlikely(radeon_bo_unpin(rbo) != 0)) {
		DRM_ERROR("failed to unpin new rbo in error path\n");
	}
	radeon_bo_unreserve(rbo);

pflip_cleanup:
	spin_lock_irqsave(&dev->event_lock, flags);
	radeon_crtc->unpin_work = NULL;
unlock_free:
	spin_unlock_irqrestore(&dev->event_lock, flags);
	drm_gem_object_unreference_unlocked(old_radeon_fb->obj);
	radeon_fence_unref(&work->fence);
	kfree(work);

	return r;
}

static const struct drm_crtc_funcs radeon_crtc_funcs = {
	.cursor_set = radeon_crtc_cursor_set,
	.cursor_move = radeon_crtc_cursor_move,
	.gamma_set = radeon_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = radeon_crtc_destroy,
	.page_flip = radeon_crtc_page_flip,
};

static void radeon_crtc_init(struct drm_device *dev, int index)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc;
	int i;

	radeon_crtc = kzalloc(sizeof(struct radeon_crtc) + (RADEONFB_CONN_LIMIT * sizeof(struct drm_connector *)), GFP_KERNEL);
	if (radeon_crtc == NULL)
		return;

	drm_crtc_init(dev, &radeon_crtc->base, &radeon_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&radeon_crtc->base, 256);
	radeon_crtc->crtc_id = index;
	rdev->mode_info.crtcs[index] = radeon_crtc;

#if 0
	radeon_crtc->mode_set.crtc = &radeon_crtc->base;
	radeon_crtc->mode_set.connectors = (struct drm_connector **)(radeon_crtc + 1);
	radeon_crtc->mode_set.num_connectors = 0;
#endif

	for (i = 0; i < 256; i++) {
		radeon_crtc->lut_r[i] = i << 2;
		radeon_crtc->lut_g[i] = i << 2;
		radeon_crtc->lut_b[i] = i << 2;
	}

	if (rdev->is_atom_bios && (ASIC_IS_AVIVO(rdev) || radeon_r4xx_atom))
		radeon_atombios_init_crtc(dev, radeon_crtc);
	else
		radeon_legacy_init_crtc(dev, radeon_crtc);
}

static const char *encoder_names[37] = {
	"NONE",
	"INTERNAL_LVDS",
	"INTERNAL_TMDS1",
	"INTERNAL_TMDS2",
	"INTERNAL_DAC1",
	"INTERNAL_DAC2",
	"INTERNAL_SDVOA",
	"INTERNAL_SDVOB",
	"SI170B",
	"CH7303",
	"CH7301",
	"INTERNAL_DVO1",
	"EXTERNAL_SDVOA",
	"EXTERNAL_SDVOB",
	"TITFP513",
	"INTERNAL_LVTM1",
	"VT1623",
	"HDMI_SI1930",
	"HDMI_INTERNAL",
	"INTERNAL_KLDSCP_TMDS1",
	"INTERNAL_KLDSCP_DVO1",
	"INTERNAL_KLDSCP_DAC1",
	"INTERNAL_KLDSCP_DAC2",
	"SI178",
	"MVPU_FPGA",
	"INTERNAL_DDI",
	"VT1625",
	"HDMI_SI1932",
	"DP_AN9801",
	"DP_DP501",
	"INTERNAL_UNIPHY",
	"INTERNAL_KLDSCP_LVTMA",
	"INTERNAL_UNIPHY1",
	"INTERNAL_UNIPHY2",
	"NUTMEG",
	"TRAVIS",
	"INTERNAL_VCE"
};

static const char *hpd_names[6] = {
	"HPD1",
	"HPD2",
	"HPD3",
	"HPD4",
	"HPD5",
	"HPD6",
};

static void radeon_print_display_setup(struct drm_device *dev)
{
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;
	struct drm_encoder *encoder;
	struct radeon_encoder *radeon_encoder;
	uint32_t devices;
	int i = 0;

	DRM_INFO("Radeon Display Connectors\n");
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		radeon_connector = to_radeon_connector(connector);
		DRM_INFO("Connector %d:\n", i);
		DRM_INFO("  %s\n", drm_get_connector_name(connector));
		if (radeon_connector->hpd.hpd != RADEON_HPD_NONE)
			DRM_INFO("  %s\n", hpd_names[radeon_connector->hpd.hpd]);
		if (radeon_connector->ddc_bus) {
			DRM_INFO("  DDC: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
				 radeon_connector->ddc_bus->rec.mask_clk_reg,
				 radeon_connector->ddc_bus->rec.mask_data_reg,
				 radeon_connector->ddc_bus->rec.a_clk_reg,
				 radeon_connector->ddc_bus->rec.a_data_reg,
				 radeon_connector->ddc_bus->rec.en_clk_reg,
				 radeon_connector->ddc_bus->rec.en_data_reg,
				 radeon_connector->ddc_bus->rec.y_clk_reg,
				 radeon_connector->ddc_bus->rec.y_data_reg);
			if (radeon_connector->router.ddc_valid)
				DRM_INFO("  DDC Router 0x%x/0x%x\n",
					 radeon_connector->router.ddc_mux_control_pin,
					 radeon_connector->router.ddc_mux_state);
			if (radeon_connector->router.cd_valid)
				DRM_INFO("  Clock/Data Router 0x%x/0x%x\n",
					 radeon_connector->router.cd_mux_control_pin,
					 radeon_connector->router.cd_mux_state);
		} else {
			if (connector->connector_type == DRM_MODE_CONNECTOR_VGA ||
			    connector->connector_type == DRM_MODE_CONNECTOR_DVII ||
			    connector->connector_type == DRM_MODE_CONNECTOR_DVID ||
			    connector->connector_type == DRM_MODE_CONNECTOR_DVIA ||
			    connector->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
			    connector->connector_type == DRM_MODE_CONNECTOR_HDMIB)
				DRM_INFO("  DDC: no ddc bus - possible BIOS bug - please report to xorg-driver-ati@lists.x.org\n");
		}
		DRM_INFO("  Encoders:\n");
		list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
			radeon_encoder = to_radeon_encoder(encoder);
			devices = radeon_encoder->devices & radeon_connector->devices;
			if (devices) {
				if (devices & ATOM_DEVICE_CRT1_SUPPORT)
					DRM_INFO("    CRT1: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_CRT2_SUPPORT)
					DRM_INFO("    CRT2: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_LCD1_SUPPORT)
					DRM_INFO("    LCD1: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_DFP1_SUPPORT)
					DRM_INFO("    DFP1: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_DFP2_SUPPORT)
					DRM_INFO("    DFP2: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_DFP3_SUPPORT)
					DRM_INFO("    DFP3: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_DFP4_SUPPORT)
					DRM_INFO("    DFP4: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_DFP5_SUPPORT)
					DRM_INFO("    DFP5: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_DFP6_SUPPORT)
					DRM_INFO("    DFP6: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_TV1_SUPPORT)
					DRM_INFO("    TV1: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_CV_SUPPORT)
					DRM_INFO("    CV: %s\n", encoder_names[radeon_encoder->encoder_id]);
			}
		}
		i++;
	}
}

static bool radeon_setup_enc_conn(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	bool ret = false;

	if (rdev->bios) {
		if (rdev->is_atom_bios) {
			ret = radeon_get_atom_connector_info_from_supported_devices_table(dev);
			if (ret == false)
				ret = radeon_get_atom_connector_info_from_object_table(dev);
		} else {
			ret = radeon_get_legacy_connector_info_from_bios(dev);
			if (ret == false)
				ret = radeon_get_legacy_connector_info_from_table(dev);
		}
	} else {
		if (!ASIC_IS_AVIVO(rdev))
			ret = radeon_get_legacy_connector_info_from_table(dev);
	}
	if (ret) {
		radeon_setup_encoder_clones(dev);
		radeon_print_display_setup(dev);
	}

	return ret;
}

int radeon_ddc_get_modes(struct radeon_connector *radeon_connector)
{
	struct drm_device *dev = radeon_connector->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	int ret = 0;

	/* on hw with routers, select right port */
	if (radeon_connector->router.ddc_valid)
		radeon_router_select_ddc_port(radeon_connector);

	if (radeon_connector_encoder_get_dp_bridge_encoder_id(&radeon_connector->base) !=
	    ENCODER_OBJECT_ID_NONE) {
		struct radeon_connector_atom_dig *dig = radeon_connector->con_priv;

		if (dig->dp_i2c_bus)
			radeon_connector->edid = drm_get_edid(&radeon_connector->base,
							      &dig->dp_i2c_bus->adapter);
	} else if ((radeon_connector->base.connector_type == DRM_MODE_CONNECTOR_DisplayPort) ||
		   (radeon_connector->base.connector_type == DRM_MODE_CONNECTOR_eDP)) {
		struct radeon_connector_atom_dig *dig = radeon_connector->con_priv;

		if ((dig->dp_sink_type == CONNECTOR_OBJECT_ID_DISPLAYPORT ||
		     dig->dp_sink_type == CONNECTOR_OBJECT_ID_eDP) && dig->dp_i2c_bus)
			radeon_connector->edid = drm_get_edid(&radeon_connector->base,
							      &dig->dp_i2c_bus->adapter);
		else if (radeon_connector->ddc_bus && !radeon_connector->edid)
			radeon_connector->edid = drm_get_edid(&radeon_connector->base,
							      &radeon_connector->ddc_bus->adapter);
	} else {
		if (radeon_connector->ddc_bus && !radeon_connector->edid)
			radeon_connector->edid = drm_get_edid(&radeon_connector->base,
							      &radeon_connector->ddc_bus->adapter);
	}

	if (!radeon_connector->edid) {
		if (rdev->is_atom_bios) {
			/* some laptops provide a hardcoded edid in rom for LCDs */
			if (((radeon_connector->base.connector_type == DRM_MODE_CONNECTOR_LVDS) ||
			     (radeon_connector->base.connector_type == DRM_MODE_CONNECTOR_eDP)))
				radeon_connector->edid = radeon_bios_get_hardcoded_edid(rdev);
		} else
			/* some servers provide a hardcoded edid in rom for KVMs */
			radeon_connector->edid = radeon_bios_get_hardcoded_edid(rdev);
	}
	if (radeon_connector->edid) {
		drm_mode_connector_update_edid_property(&radeon_connector->base, radeon_connector->edid);
		ret = drm_add_edid_modes(&radeon_connector->base, radeon_connector->edid);
		return ret;
	}
	drm_mode_connector_update_edid_property(&radeon_connector->base, NULL);
	return 0;
}

/* avivo */
static void avivo_get_fb_div(struct radeon_pll *pll,
			     u32 target_clock,
			     u32 post_div,
			     u32 ref_div,
			     u32 *fb_div,
			     u32 *frac_fb_div)
{
	u32 tmp = post_div * ref_div;

	tmp *= target_clock;
	*fb_div = tmp / pll->reference_freq;
	*frac_fb_div = tmp % pll->reference_freq;

        if (*fb_div > pll->max_feedback_div)
		*fb_div = pll->max_feedback_div;
        else if (*fb_div < pll->min_feedback_div)
                *fb_div = pll->min_feedback_div;
}

static u32 avivo_get_post_div(struct radeon_pll *pll,
			      u32 target_clock)
{
	u32 vco, post_div, tmp;

	if (pll->flags & RADEON_PLL_USE_POST_DIV)
		return pll->post_div;

	if (pll->flags & RADEON_PLL_PREFER_MINM_OVER_MAXP) {
		if (pll->flags & RADEON_PLL_IS_LCD)
			vco = pll->lcd_pll_out_min;
		else
			vco = pll->pll_out_min;
	} else {
		if (pll->flags & RADEON_PLL_IS_LCD)
			vco = pll->lcd_pll_out_max;
		else
			vco = pll->pll_out_max;
	}

	post_div = vco / target_clock;
	tmp = vco % target_clock;

	if (pll->flags & RADEON_PLL_PREFER_MINM_OVER_MAXP) {
		if (tmp)
			post_div++;
	} else {
		if (!tmp)
			post_div--;
	}

	if (post_div > pll->max_post_div)
		post_div = pll->max_post_div;
	else if (post_div < pll->min_post_div)
		post_div = pll->min_post_div;

	return post_div;
}

#define MAX_TOLERANCE 10

void radeon_compute_pll_avivo(struct radeon_pll *pll,
			      u32 freq,
			      u32 *dot_clock_p,
			      u32 *fb_div_p,
			      u32 *frac_fb_div_p,
			      u32 *ref_div_p,
			      u32 *post_div_p)
{
	u32 target_clock = freq / 10;
	u32 post_div = avivo_get_post_div(pll, target_clock);
	u32 ref_div = pll->min_ref_div;
	u32 fb_div = 0, frac_fb_div = 0, tmp;

	if (pll->flags & RADEON_PLL_USE_REF_DIV)
		ref_div = pll->reference_div;

	if (pll->flags & RADEON_PLL_USE_FRAC_FB_DIV) {
		avivo_get_fb_div(pll, target_clock, post_div, ref_div, &fb_div, &frac_fb_div);
		frac_fb_div = (100 * frac_fb_div) / pll->reference_freq;
		if (frac_fb_div >= 5) {
			frac_fb_div -= 5;
			frac_fb_div = frac_fb_div / 10;
			frac_fb_div++;
		}
		if (frac_fb_div >= 10) {
			fb_div++;
			frac_fb_div = 0;
		}
	} else {
		while (ref_div <= pll->max_ref_div) {
			avivo_get_fb_div(pll, target_clock, post_div, ref_div,
					 &fb_div, &frac_fb_div);
			if (frac_fb_div >= (pll->reference_freq / 2))
				fb_div++;
			frac_fb_div = 0;
			tmp = (pll->reference_freq * fb_div) / (post_div * ref_div);
			tmp = (tmp * 10000) / target_clock;

			if (tmp > (10000 + MAX_TOLERANCE))
				ref_div++;
			else if (tmp >= (10000 - MAX_TOLERANCE))
				break;
			else
				ref_div++;
		}
	}

	*dot_clock_p = ((pll->reference_freq * fb_div * 10) + (pll->reference_freq * frac_fb_div)) /
		(ref_div * post_div * 10);
	*fb_div_p = fb_div;
	*frac_fb_div_p = frac_fb_div;
	*ref_div_p = ref_div;
	*post_div_p = post_div;
	DRM_DEBUG_KMS("%d, pll dividers - fb: %d.%d ref: %d, post %d\n",
		      *dot_clock_p, fb_div, frac_fb_div, ref_div, post_div);
}

/* pre-avivo */
static inline uint32_t radeon_div(uint64_t n, uint32_t d)
{
	uint64_t mod;

	n += d / 2;

	mod = do_div(n, d);
	return n;
}

void radeon_compute_pll_legacy(struct radeon_pll *pll,
			       uint64_t freq,
			       uint32_t *dot_clock_p,
			       uint32_t *fb_div_p,
			       uint32_t *frac_fb_div_p,
			       uint32_t *ref_div_p,
			       uint32_t *post_div_p)
{
	uint32_t min_ref_div = pll->min_ref_div;
	uint32_t max_ref_div = pll->max_ref_div;
	uint32_t min_post_div = pll->min_post_div;
	uint32_t max_post_div = pll->max_post_div;
	uint32_t min_fractional_feed_div = 0;
	uint32_t max_fractional_feed_div = 0;
	uint32_t best_vco = pll->best_vco;
	uint32_t best_post_div = 1;
	uint32_t best_ref_div = 1;
	uint32_t best_feedback_div = 1;
	uint32_t best_frac_feedback_div = 0;
	uint32_t best_freq = -1;
	uint32_t best_error = 0xffffffff;
	uint32_t best_vco_diff = 1;
	uint32_t post_div;
	u32 pll_out_min, pll_out_max;

	DRM_DEBUG_KMS("PLL freq %llu %u %u\n", freq, pll->min_ref_div, pll->max_ref_div);
	freq = freq * 1000;

	if (pll->flags & RADEON_PLL_IS_LCD) {
		pll_out_min = pll->lcd_pll_out_min;
		pll_out_max = pll->lcd_pll_out_max;
	} else {
		pll_out_min = pll->pll_out_min;
		pll_out_max = pll->pll_out_max;
	}

	if (pll_out_min > 64800)
		pll_out_min = 64800;

	if (pll->flags & RADEON_PLL_USE_REF_DIV)
		min_ref_div = max_ref_div = pll->reference_div;
	else {
		while (min_ref_div < max_ref_div-1) {
			uint32_t mid = (min_ref_div + max_ref_div) / 2;
			uint32_t pll_in = pll->reference_freq / mid;
			if (pll_in < pll->pll_in_min)
				max_ref_div = mid;
			else if (pll_in > pll->pll_in_max)
				min_ref_div = mid;
			else
				break;
		}
	}

	if (pll->flags & RADEON_PLL_USE_POST_DIV)
		min_post_div = max_post_div = pll->post_div;

	if (pll->flags & RADEON_PLL_USE_FRAC_FB_DIV) {
		min_fractional_feed_div = pll->min_frac_feedback_div;
		max_fractional_feed_div = pll->max_frac_feedback_div;
	}

	for (post_div = max_post_div; post_div >= min_post_div; --post_div) {
		uint32_t ref_div;

		if ((pll->flags & RADEON_PLL_NO_ODD_POST_DIV) && (post_div & 1))
			continue;

		/* legacy radeons only have a few post_divs */
		if (pll->flags & RADEON_PLL_LEGACY) {
			if ((post_div == 5) ||
			    (post_div == 7) ||
			    (post_div == 9) ||
			    (post_div == 10) ||
			    (post_div == 11) ||
			    (post_div == 13) ||
			    (post_div == 14) ||
			    (post_div == 15))
				continue;
		}

		for (ref_div = min_ref_div; ref_div <= max_ref_div; ++ref_div) {
			uint32_t feedback_div, current_freq = 0, error, vco_diff;
			uint32_t pll_in = pll->reference_freq / ref_div;
			uint32_t min_feed_div = pll->min_feedback_div;
			uint32_t max_feed_div = pll->max_feedback_div + 1;

			if (pll_in < pll->pll_in_min || pll_in > pll->pll_in_max)
				continue;

			while (min_feed_div < max_feed_div) {
				uint32_t vco;
				uint32_t min_frac_feed_div = min_fractional_feed_div;
				uint32_t max_frac_feed_div = max_fractional_feed_div + 1;
				uint32_t frac_feedback_div;
				uint64_t tmp;

				feedback_div = (min_feed_div + max_feed_div) / 2;

				tmp = (uint64_t)pll->reference_freq * feedback_div;
				vco = radeon_div(tmp, ref_div);

				if (vco < pll_out_min) {
					min_feed_div = feedback_div + 1;
					continue;
				} else if (vco > pll_out_max) {
					max_feed_div = feedback_div;
					continue;
				}

				while (min_frac_feed_div < max_frac_feed_div) {
					frac_feedback_div = (min_frac_feed_div + max_frac_feed_div) / 2;
					tmp = (uint64_t)pll->reference_freq * 10000 * feedback_div;
					tmp += (uint64_t)pll->reference_freq * 1000 * frac_feedback_div;
					current_freq = radeon_div(tmp, ref_div * post_div);

					if (pll->flags & RADEON_PLL_PREFER_CLOSEST_LOWER) {
						if (freq < current_freq)
							error = 0xffffffff;
						else
							error = freq - current_freq;
					} else
						error = abs(current_freq - freq);
					vco_diff = abs(vco - best_vco);

					if ((best_vco == 0 && error < best_error) ||
					    (best_vco != 0 &&
					     ((best_error > 100 && error < best_error - 100) ||
					      (abs(error - best_error) < 100 && vco_diff < best_vco_diff)))) {
						best_post_div = post_div;
						best_ref_div = ref_div;
						best_feedback_div = feedback_div;
						best_frac_feedback_div = frac_feedback_div;
						best_freq = current_freq;
						best_error = error;
						best_vco_diff = vco_diff;
					} else if (current_freq == freq) {
						if (best_freq == -1) {
							best_post_div = post_div;
							best_ref_div = ref_div;
							best_feedback_div = feedback_div;
							best_frac_feedback_div = frac_feedback_div;
							best_freq = current_freq;
							best_error = error;
							best_vco_diff = vco_diff;
						} else if (((pll->flags & RADEON_PLL_PREFER_LOW_REF_DIV) && (ref_div < best_ref_div)) ||
							   ((pll->flags & RADEON_PLL_PREFER_HIGH_REF_DIV) && (ref_div > best_ref_div)) ||
							   ((pll->flags & RADEON_PLL_PREFER_LOW_FB_DIV) && (feedback_div < best_feedback_div)) ||
							   ((pll->flags & RADEON_PLL_PREFER_HIGH_FB_DIV) && (feedback_div > best_feedback_div)) ||
							   ((pll->flags & RADEON_PLL_PREFER_LOW_POST_DIV) && (post_div < best_post_div)) ||
							   ((pll->flags & RADEON_PLL_PREFER_HIGH_POST_DIV) && (post_div > best_post_div))) {
							best_post_div = post_div;
							best_ref_div = ref_div;
							best_feedback_div = feedback_div;
							best_frac_feedback_div = frac_feedback_div;
							best_freq = current_freq;
							best_error = error;
							best_vco_diff = vco_diff;
						}
					}
					if (current_freq < freq)
						min_frac_feed_div = frac_feedback_div + 1;
					else
						max_frac_feed_div = frac_feedback_div;
				}
				if (current_freq < freq)
					min_feed_div = feedback_div + 1;
				else
					max_feed_div = feedback_div;
			}
		}
	}

	*dot_clock_p = best_freq / 10000;
	*fb_div_p = best_feedback_div;
	*frac_fb_div_p = best_frac_feedback_div;
	*ref_div_p = best_ref_div;
	*post_div_p = best_post_div;
	DRM_DEBUG_KMS("%lld %d, pll dividers - fb: %d.%d ref: %d, post %d\n",
		      (long long)freq,
		      best_freq / 1000, best_feedback_div, best_frac_feedback_div,
		      best_ref_div, best_post_div);

}

static void radeon_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct radeon_framebuffer *radeon_fb = to_radeon_framebuffer(fb);

	if (radeon_fb->obj) {
		drm_gem_object_unreference_unlocked(radeon_fb->obj);
	}
	drm_framebuffer_cleanup(fb);
	kfree(radeon_fb);
}

static int radeon_user_framebuffer_create_handle(struct drm_framebuffer *fb,
						  struct drm_file *file_priv,
						  unsigned int *handle)
{
	struct radeon_framebuffer *radeon_fb = to_radeon_framebuffer(fb);

	return drm_gem_handle_create(file_priv, radeon_fb->obj, handle);
}

static const struct drm_framebuffer_funcs radeon_fb_funcs = {
	.destroy = radeon_user_framebuffer_destroy,
	.create_handle = radeon_user_framebuffer_create_handle,
};

int
radeon_framebuffer_init(struct drm_device *dev,
			struct radeon_framebuffer *rfb,
			struct drm_mode_fb_cmd2 *mode_cmd,
			struct drm_gem_object *obj)
{
	int ret;
	rfb->obj = obj;
	drm_helper_mode_fill_fb_struct(&rfb->base, mode_cmd);
	ret = drm_framebuffer_init(dev, &rfb->base, &radeon_fb_funcs);
	if (ret) {
		rfb->obj = NULL;
		return ret;
	}
	return 0;
}

static struct drm_framebuffer *
radeon_user_framebuffer_create(struct drm_device *dev,
			       struct drm_file *file_priv,
			       struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj;
	struct radeon_framebuffer *radeon_fb;
	int ret;

	obj = drm_gem_object_lookup(dev, file_priv, mode_cmd->handles[0]);
	if (obj ==  NULL) {
		dev_err(&dev->pdev->dev, "No GEM object associated to handle 0x%08X, "
			"can't create framebuffer\n", mode_cmd->handles[0]);
		return ERR_PTR(-ENOENT);
	}

	radeon_fb = kzalloc(sizeof(*radeon_fb), GFP_KERNEL);
	if (radeon_fb == NULL) {
		drm_gem_object_unreference_unlocked(obj);
		return ERR_PTR(-ENOMEM);
	}

	ret = radeon_framebuffer_init(dev, radeon_fb, mode_cmd, obj);
	if (ret) {
		kfree(radeon_fb);
		drm_gem_object_unreference_unlocked(obj);
		return ERR_PTR(ret);
	}

	return &radeon_fb->base;
}

static void radeon_output_poll_changed(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	radeon_fb_output_poll_changed(rdev);
}

static const struct drm_mode_config_funcs radeon_mode_funcs = {
	.fb_create = radeon_user_framebuffer_create,
	.output_poll_changed = radeon_output_poll_changed
};

static struct drm_prop_enum_list radeon_tmds_pll_enum_list[] =
{	{ 0, "driver" },
	{ 1, "bios" },
};

static struct drm_prop_enum_list radeon_tv_std_enum_list[] =
{	{ TV_STD_NTSC, "ntsc" },
	{ TV_STD_PAL, "pal" },
	{ TV_STD_PAL_M, "pal-m" },
	{ TV_STD_PAL_60, "pal-60" },
	{ TV_STD_NTSC_J, "ntsc-j" },
	{ TV_STD_SCART_PAL, "scart-pal" },
	{ TV_STD_PAL_CN, "pal-cn" },
	{ TV_STD_SECAM, "secam" },
};

static struct drm_prop_enum_list radeon_underscan_enum_list[] =
{	{ UNDERSCAN_OFF, "off" },
	{ UNDERSCAN_ON, "on" },
	{ UNDERSCAN_AUTO, "auto" },
};

static int radeon_modeset_create_props(struct radeon_device *rdev)
{
	int sz;

	if (rdev->is_atom_bios) {
		rdev->mode_info.coherent_mode_property =
			drm_property_create_range(rdev->ddev, 0 , "coherent", 0, 1);
		if (!rdev->mode_info.coherent_mode_property)
			return -ENOMEM;
	}

	if (!ASIC_IS_AVIVO(rdev)) {
		sz = ARRAY_SIZE(radeon_tmds_pll_enum_list);
		rdev->mode_info.tmds_pll_property =
			drm_property_create_enum(rdev->ddev, 0,
					    "tmds_pll",
					    radeon_tmds_pll_enum_list, sz);
	}

	rdev->mode_info.load_detect_property =
		drm_property_create_range(rdev->ddev, 0, "load detection", 0, 1);
	if (!rdev->mode_info.load_detect_property)
		return -ENOMEM;

	drm_mode_create_scaling_mode_property(rdev->ddev);

	sz = ARRAY_SIZE(radeon_tv_std_enum_list);
	rdev->mode_info.tv_std_property =
		drm_property_create_enum(rdev->ddev, 0,
				    "tv standard",
				    radeon_tv_std_enum_list, sz);

	sz = ARRAY_SIZE(radeon_underscan_enum_list);
	rdev->mode_info.underscan_property =
		drm_property_create_enum(rdev->ddev, 0,
				    "underscan",
				    radeon_underscan_enum_list, sz);

	rdev->mode_info.underscan_hborder_property =
		drm_property_create_range(rdev->ddev, 0,
					"underscan hborder", 0, 128);
	if (!rdev->mode_info.underscan_hborder_property)
		return -ENOMEM;

	rdev->mode_info.underscan_vborder_property =
		drm_property_create_range(rdev->ddev, 0,
					"underscan vborder", 0, 128);
	if (!rdev->mode_info.underscan_vborder_property)
		return -ENOMEM;

	return 0;
}

void radeon_update_display_priority(struct radeon_device *rdev)
{
	/* adjustment options for the display watermarks */
	if ((radeon_disp_priority == 0) || (radeon_disp_priority > 2)) {
		/* set display priority to high for r3xx, rv515 chips
		 * this avoids flickering due to underflow to the
		 * display controllers during heavy acceleration.
		 * Don't force high on rs4xx igp chips as it seems to
		 * affect the sound card.  See kernel bug 15982.
		 */
		if ((ASIC_IS_R300(rdev) || (rdev->family == CHIP_RV515)) &&
		    !(rdev->flags & RADEON_IS_IGP))
			rdev->disp_priority = 2;
		else
			rdev->disp_priority = 0;
	} else
		rdev->disp_priority = radeon_disp_priority;

}

/*
 * Allocate hdmi structs and determine register offsets
 */
static void radeon_afmt_init(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < RADEON_MAX_AFMT_BLOCKS; i++)
		rdev->mode_info.afmt[i] = NULL;

	if (ASIC_IS_DCE6(rdev)) {
		/* todo */
	} else if (ASIC_IS_DCE4(rdev)) {
		/* DCE4/5 has 6 audio blocks tied to DIG encoders */
		/* DCE4.1 has 2 audio blocks tied to DIG encoders */
		rdev->mode_info.afmt[0] = kzalloc(sizeof(struct radeon_afmt), GFP_KERNEL);
		if (rdev->mode_info.afmt[0]) {
			rdev->mode_info.afmt[0]->offset = EVERGREEN_CRTC0_REGISTER_OFFSET;
			rdev->mode_info.afmt[0]->id = 0;
		}
		rdev->mode_info.afmt[1] = kzalloc(sizeof(struct radeon_afmt), GFP_KERNEL);
		if (rdev->mode_info.afmt[1]) {
			rdev->mode_info.afmt[1]->offset = EVERGREEN_CRTC1_REGISTER_OFFSET;
			rdev->mode_info.afmt[1]->id = 1;
		}
		if (!ASIC_IS_DCE41(rdev)) {
			rdev->mode_info.afmt[2] = kzalloc(sizeof(struct radeon_afmt), GFP_KERNEL);
			if (rdev->mode_info.afmt[2]) {
				rdev->mode_info.afmt[2]->offset = EVERGREEN_CRTC2_REGISTER_OFFSET;
				rdev->mode_info.afmt[2]->id = 2;
			}
			rdev->mode_info.afmt[3] = kzalloc(sizeof(struct radeon_afmt), GFP_KERNEL);
			if (rdev->mode_info.afmt[3]) {
				rdev->mode_info.afmt[3]->offset = EVERGREEN_CRTC3_REGISTER_OFFSET;
				rdev->mode_info.afmt[3]->id = 3;
			}
			rdev->mode_info.afmt[4] = kzalloc(sizeof(struct radeon_afmt), GFP_KERNEL);
			if (rdev->mode_info.afmt[4]) {
				rdev->mode_info.afmt[4]->offset = EVERGREEN_CRTC4_REGISTER_OFFSET;
				rdev->mode_info.afmt[4]->id = 4;
			}
			rdev->mode_info.afmt[5] = kzalloc(sizeof(struct radeon_afmt), GFP_KERNEL);
			if (rdev->mode_info.afmt[5]) {
				rdev->mode_info.afmt[5]->offset = EVERGREEN_CRTC5_REGISTER_OFFSET;
				rdev->mode_info.afmt[5]->id = 5;
			}
		}
	} else if (ASIC_IS_DCE3(rdev)) {
		/* DCE3.x has 2 audio blocks tied to DIG encoders */
		rdev->mode_info.afmt[0] = kzalloc(sizeof(struct radeon_afmt), GFP_KERNEL);
		if (rdev->mode_info.afmt[0]) {
			rdev->mode_info.afmt[0]->offset = DCE3_HDMI_OFFSET0;
			rdev->mode_info.afmt[0]->id = 0;
		}
		rdev->mode_info.afmt[1] = kzalloc(sizeof(struct radeon_afmt), GFP_KERNEL);
		if (rdev->mode_info.afmt[1]) {
			rdev->mode_info.afmt[1]->offset = DCE3_HDMI_OFFSET1;
			rdev->mode_info.afmt[1]->id = 1;
		}
	} else if (ASIC_IS_DCE2(rdev)) {
		/* DCE2 has at least 1 routable audio block */
		rdev->mode_info.afmt[0] = kzalloc(sizeof(struct radeon_afmt), GFP_KERNEL);
		if (rdev->mode_info.afmt[0]) {
			rdev->mode_info.afmt[0]->offset = DCE2_HDMI_OFFSET0;
			rdev->mode_info.afmt[0]->id = 0;
		}
		/* r6xx has 2 routable audio blocks */
		if (rdev->family >= CHIP_R600) {
			rdev->mode_info.afmt[1] = kzalloc(sizeof(struct radeon_afmt), GFP_KERNEL);
			if (rdev->mode_info.afmt[1]) {
				rdev->mode_info.afmt[1]->offset = DCE2_HDMI_OFFSET1;
				rdev->mode_info.afmt[1]->id = 1;
			}
		}
	}
}

static void radeon_afmt_fini(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < RADEON_MAX_AFMT_BLOCKS; i++) {
		kfree(rdev->mode_info.afmt[i]);
		rdev->mode_info.afmt[i] = NULL;
	}
}

int radeon_modeset_init(struct radeon_device *rdev)
{
	int i;
	int ret;

	drm_mode_config_init(rdev->ddev);
	rdev->mode_info.mode_config_initialized = true;

	rdev->ddev->mode_config.funcs = &radeon_mode_funcs;

	if (ASIC_IS_DCE5(rdev)) {
		rdev->ddev->mode_config.max_width = 16384;
		rdev->ddev->mode_config.max_height = 16384;
	} else if (ASIC_IS_AVIVO(rdev)) {
		rdev->ddev->mode_config.max_width = 8192;
		rdev->ddev->mode_config.max_height = 8192;
	} else {
		rdev->ddev->mode_config.max_width = 4096;
		rdev->ddev->mode_config.max_height = 4096;
	}

	rdev->ddev->mode_config.preferred_depth = 24;
	rdev->ddev->mode_config.prefer_shadow = 1;

	rdev->ddev->mode_config.fb_base = rdev->mc.aper_base;

	ret = radeon_modeset_create_props(rdev);
	if (ret) {
		return ret;
	}

	/* init i2c buses */
	radeon_i2c_init(rdev);

	/* check combios for a valid hardcoded EDID - Sun servers */
	if (!rdev->is_atom_bios) {
		/* check for hardcoded EDID in BIOS */
		radeon_combios_check_hardcoded_edid(rdev);
	}

	/* allocate crtcs */
	for (i = 0; i < rdev->num_crtc; i++) {
		radeon_crtc_init(rdev->ddev, i);
	}

	/* okay we should have all the bios connectors */
	ret = radeon_setup_enc_conn(rdev->ddev);
	if (!ret) {
		return ret;
	}

	/* init dig PHYs, disp eng pll */
	if (rdev->is_atom_bios) {
		radeon_atom_encoder_init(rdev);
		radeon_atom_disp_eng_pll_init(rdev);
	}

	/* initialize hpd */
	radeon_hpd_init(rdev);

	/* setup afmt */
	radeon_afmt_init(rdev);

	/* Initialize power management */
	radeon_pm_init(rdev);

	radeon_fbdev_init(rdev);
	drm_kms_helper_poll_init(rdev->ddev);

	return 0;
}

void radeon_modeset_fini(struct radeon_device *rdev)
{
	radeon_fbdev_fini(rdev);
	kfree(rdev->mode_info.bios_hardcoded_edid);
	radeon_pm_fini(rdev);

	if (rdev->mode_info.mode_config_initialized) {
		radeon_afmt_fini(rdev);
		drm_kms_helper_poll_fini(rdev->ddev);
		radeon_hpd_fini(rdev);
		drm_mode_config_cleanup(rdev->ddev);
		rdev->mode_info.mode_config_initialized = false;
	}
	/* free i2c buses */
	radeon_i2c_fini(rdev);
}

static bool is_hdtv_mode(const struct drm_display_mode *mode)
{
	/* try and guess if this is a tv or a monitor */
	if ((mode->vdisplay == 480 && mode->hdisplay == 720) || /* 480p */
	    (mode->vdisplay == 576) || /* 576p */
	    (mode->vdisplay == 720) || /* 720p */
	    (mode->vdisplay == 1080)) /* 1080p */
		return true;
	else
		return false;
}

bool radeon_crtc_scaling_mode_fixup(struct drm_crtc *crtc,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct drm_encoder *encoder;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_encoder *radeon_encoder;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;
	bool first = true;
	u32 src_v = 1, dst_v = 1;
	u32 src_h = 1, dst_h = 1;

	radeon_crtc->h_border = 0;
	radeon_crtc->v_border = 0;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->crtc != crtc)
			continue;
		radeon_encoder = to_radeon_encoder(encoder);
		connector = radeon_get_connector_for_encoder(encoder);
		radeon_connector = to_radeon_connector(connector);

		if (first) {
			/* set scaling */
			if (radeon_encoder->rmx_type == RMX_OFF)
				radeon_crtc->rmx_type = RMX_OFF;
			else if (mode->hdisplay < radeon_encoder->native_mode.hdisplay ||
				 mode->vdisplay < radeon_encoder->native_mode.vdisplay)
				radeon_crtc->rmx_type = radeon_encoder->rmx_type;
			else
				radeon_crtc->rmx_type = RMX_OFF;
			/* copy native mode */
			memcpy(&radeon_crtc->native_mode,
			       &radeon_encoder->native_mode,
				sizeof(struct drm_display_mode));
			src_v = crtc->mode.vdisplay;
			dst_v = radeon_crtc->native_mode.vdisplay;
			src_h = crtc->mode.hdisplay;
			dst_h = radeon_crtc->native_mode.hdisplay;

			/* fix up for overscan on hdmi */
			if (ASIC_IS_AVIVO(rdev) &&
			    (!(mode->flags & DRM_MODE_FLAG_INTERLACE)) &&
			    ((radeon_encoder->underscan_type == UNDERSCAN_ON) ||
			     ((radeon_encoder->underscan_type == UNDERSCAN_AUTO) &&
			      drm_detect_hdmi_monitor(radeon_connector->edid) &&
			      is_hdtv_mode(mode)))) {
				if (radeon_encoder->underscan_hborder != 0)
					radeon_crtc->h_border = radeon_encoder->underscan_hborder;
				else
					radeon_crtc->h_border = (mode->hdisplay >> 5) + 16;
				if (radeon_encoder->underscan_vborder != 0)
					radeon_crtc->v_border = radeon_encoder->underscan_vborder;
				else
					radeon_crtc->v_border = (mode->vdisplay >> 5) + 16;
				radeon_crtc->rmx_type = RMX_FULL;
				src_v = crtc->mode.vdisplay;
				dst_v = crtc->mode.vdisplay - (radeon_crtc->v_border * 2);
				src_h = crtc->mode.hdisplay;
				dst_h = crtc->mode.hdisplay - (radeon_crtc->h_border * 2);
			}
			first = false;
		} else {
			if (radeon_crtc->rmx_type != radeon_encoder->rmx_type) {
				/* WARNING: Right now this can't happen but
				 * in the future we need to check that scaling
				 * are consistent across different encoder
				 * (ie all encoder can work with the same
				 *  scaling).
				 */
				DRM_ERROR("Scaling not consistent across encoder.\n");
				return false;
			}
		}
	}
	if (radeon_crtc->rmx_type != RMX_OFF) {
		fixed20_12 a, b;
		a.full = dfixed_const(src_v);
		b.full = dfixed_const(dst_v);
		radeon_crtc->vsc.full = dfixed_div(a, b);
		a.full = dfixed_const(src_h);
		b.full = dfixed_const(dst_h);
		radeon_crtc->hsc.full = dfixed_div(a, b);
	} else {
		radeon_crtc->vsc.full = dfixed_const(1);
		radeon_crtc->hsc.full = dfixed_const(1);
	}
	return true;
}

/*
 * Retrieve current video scanout position of crtc on a given gpu.
 *
 * \param dev Device to query.
 * \param crtc Crtc to query.
 * \param *vpos Location where vertical scanout position should be stored.
 * \param *hpos Location where horizontal scanout position should go.
 *
 * Returns vpos as a positive number while in active scanout area.
 * Returns vpos as a negative number inside vblank, counting the number
 * of scanlines to go until end of vblank, e.g., -1 means "one scanline
 * until start of active scanout / end of vblank."
 *
 * \return Flags, or'ed together as follows:
 *
 * DRM_SCANOUTPOS_VALID = Query successful.
 * DRM_SCANOUTPOS_INVBL = Inside vblank.
 * DRM_SCANOUTPOS_ACCURATE = Returned position is accurate. A lack of
 * this flag means that returned position may be offset by a constant but
 * unknown small number of scanlines wrt. real scanout position.
 *
 */
int radeon_get_crtc_scanoutpos(struct drm_device *dev, int crtc, int *vpos, int *hpos)
{
	u32 stat_crtc = 0, vbl = 0, position = 0;
	int vbl_start, vbl_end, vtotal, ret = 0;
	bool in_vbl = true;

	struct radeon_device *rdev = dev->dev_private;

	if (ASIC_IS_DCE4(rdev)) {
		if (crtc == 0) {
			vbl = RREG32(EVERGREEN_CRTC_V_BLANK_START_END +
				     EVERGREEN_CRTC0_REGISTER_OFFSET);
			position = RREG32(EVERGREEN_CRTC_STATUS_POSITION +
					  EVERGREEN_CRTC0_REGISTER_OFFSET);
			ret |= DRM_SCANOUTPOS_VALID;
		}
		if (crtc == 1) {
			vbl = RREG32(EVERGREEN_CRTC_V_BLANK_START_END +
				     EVERGREEN_CRTC1_REGISTER_OFFSET);
			position = RREG32(EVERGREEN_CRTC_STATUS_POSITION +
					  EVERGREEN_CRTC1_REGISTER_OFFSET);
			ret |= DRM_SCANOUTPOS_VALID;
		}
		if (crtc == 2) {
			vbl = RREG32(EVERGREEN_CRTC_V_BLANK_START_END +
				     EVERGREEN_CRTC2_REGISTER_OFFSET);
			position = RREG32(EVERGREEN_CRTC_STATUS_POSITION +
					  EVERGREEN_CRTC2_REGISTER_OFFSET);
			ret |= DRM_SCANOUTPOS_VALID;
		}
		if (crtc == 3) {
			vbl = RREG32(EVERGREEN_CRTC_V_BLANK_START_END +
				     EVERGREEN_CRTC3_REGISTER_OFFSET);
			position = RREG32(EVERGREEN_CRTC_STATUS_POSITION +
					  EVERGREEN_CRTC3_REGISTER_OFFSET);
			ret |= DRM_SCANOUTPOS_VALID;
		}
		if (crtc == 4) {
			vbl = RREG32(EVERGREEN_CRTC_V_BLANK_START_END +
				     EVERGREEN_CRTC4_REGISTER_OFFSET);
			position = RREG32(EVERGREEN_CRTC_STATUS_POSITION +
					  EVERGREEN_CRTC4_REGISTER_OFFSET);
			ret |= DRM_SCANOUTPOS_VALID;
		}
		if (crtc == 5) {
			vbl = RREG32(EVERGREEN_CRTC_V_BLANK_START_END +
				     EVERGREEN_CRTC5_REGISTER_OFFSET);
			position = RREG32(EVERGREEN_CRTC_STATUS_POSITION +
					  EVERGREEN_CRTC5_REGISTER_OFFSET);
			ret |= DRM_SCANOUTPOS_VALID;
		}
	} else if (ASIC_IS_AVIVO(rdev)) {
		if (crtc == 0) {
			vbl = RREG32(AVIVO_D1CRTC_V_BLANK_START_END);
			position = RREG32(AVIVO_D1CRTC_STATUS_POSITION);
			ret |= DRM_SCANOUTPOS_VALID;
		}
		if (crtc == 1) {
			vbl = RREG32(AVIVO_D2CRTC_V_BLANK_START_END);
			position = RREG32(AVIVO_D2CRTC_STATUS_POSITION);
			ret |= DRM_SCANOUTPOS_VALID;
		}
	} else {
		/* Pre-AVIVO: Different encoding of scanout pos and vblank interval. */
		if (crtc == 0) {
			/* Assume vbl_end == 0, get vbl_start from
			 * upper 16 bits.
			 */
			vbl = (RREG32(RADEON_CRTC_V_TOTAL_DISP) &
				RADEON_CRTC_V_DISP) >> RADEON_CRTC_V_DISP_SHIFT;
			/* Only retrieve vpos from upper 16 bits, set hpos == 0. */
			position = (RREG32(RADEON_CRTC_VLINE_CRNT_VLINE) >> 16) & RADEON_CRTC_V_TOTAL;
			stat_crtc = RREG32(RADEON_CRTC_STATUS);
			if (!(stat_crtc & 1))
				in_vbl = false;

			ret |= DRM_SCANOUTPOS_VALID;
		}
		if (crtc == 1) {
			vbl = (RREG32(RADEON_CRTC2_V_TOTAL_DISP) &
				RADEON_CRTC_V_DISP) >> RADEON_CRTC_V_DISP_SHIFT;
			position = (RREG32(RADEON_CRTC2_VLINE_CRNT_VLINE) >> 16) & RADEON_CRTC_V_TOTAL;
			stat_crtc = RREG32(RADEON_CRTC2_STATUS);
			if (!(stat_crtc & 1))
				in_vbl = false;

			ret |= DRM_SCANOUTPOS_VALID;
		}
	}

	/* Decode into vertical and horizontal scanout position. */
	*vpos = position & 0x1fff;
	*hpos = (position >> 16) & 0x1fff;

	/* Valid vblank area boundaries from gpu retrieved? */
	if (vbl > 0) {
		/* Yes: Decode. */
		ret |= DRM_SCANOUTPOS_ACCURATE;
		vbl_start = vbl & 0x1fff;
		vbl_end = (vbl >> 16) & 0x1fff;
	}
	else {
		/* No: Fake something reasonable which gives at least ok results. */
		vbl_start = rdev->mode_info.crtcs[crtc]->base.hwmode.crtc_vdisplay;
		vbl_end = 0;
	}

	/* Test scanout position against vblank region. */
	if ((*vpos < vbl_start) && (*vpos >= vbl_end))
		in_vbl = false;

	/* Check if inside vblank area and apply corrective offsets:
	 * vpos will then be >=0 in video scanout area, but negative
	 * within vblank area, counting down the number of lines until
	 * start of scanout.
	 */

	/* Inside "upper part" of vblank area? Apply corrective offset if so: */
	if (in_vbl && (*vpos >= vbl_start)) {
		vtotal = rdev->mode_info.crtcs[crtc]->base.hwmode.crtc_vtotal;
		*vpos = *vpos - vtotal;
	}

	/* Correct for shifted end of vbl at vbl_end. */
	*vpos = *vpos - vbl_end;

	/* In vblank? */
	if (in_vbl)
		ret |= DRM_SCANOUTPOS_INVBL;

	return ret;
}

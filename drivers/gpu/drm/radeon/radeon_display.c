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

#include <linux/pm_runtime.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_edid.h>

#include <linux/gcd.h>

static void avivo_crtc_load_lut(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	u16 *r, *g, *b;
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
	r = crtc->gamma_store;
	g = r + crtc->gamma_size;
	b = g + crtc->gamma_size;
	for (i = 0; i < 256; i++) {
		WREG32(AVIVO_DC_LUT_30_COLOR,
		       ((*r++ & 0xffc0) << 14) |
		       ((*g++ & 0xffc0) << 4) |
		       (*b++ >> 6));
	}

	/* Only change bit 0 of LUT_SEL, other bits are set elsewhere */
	WREG32_P(AVIVO_D1GRPH_LUT_SEL + radeon_crtc->crtc_offset, radeon_crtc->crtc_id, ~1);
}

static void dce4_crtc_load_lut(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	u16 *r, *g, *b;
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
	r = crtc->gamma_store;
	g = r + crtc->gamma_size;
	b = g + crtc->gamma_size;
	for (i = 0; i < 256; i++) {
		WREG32(EVERGREEN_DC_LUT_30_COLOR + radeon_crtc->crtc_offset,
		       ((*r++ & 0xffc0) << 14) |
		       ((*g++ & 0xffc0) << 4) |
		       (*b++ >> 6));
	}
}

static void dce5_crtc_load_lut(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	u16 *r, *g, *b;
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
	r = crtc->gamma_store;
	g = r + crtc->gamma_size;
	b = g + crtc->gamma_size;
	for (i = 0; i < 256; i++) {
		WREG32(EVERGREEN_DC_LUT_30_COLOR + radeon_crtc->crtc_offset,
		       ((*r++ & 0xffc0) << 14) |
		       ((*g++ & 0xffc0) << 4) |
		       (*b++ >> 6));
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
	       (NI_OUTPUT_CSC_GRPH_MODE(radeon_crtc->output_csc) |
		NI_OUTPUT_CSC_OVL_MODE(NI_OUTPUT_CSC_BYPASS)));
	/* XXX match this to the depth of the crtc fmt block, move to modeset? */
	WREG32(0x6940 + radeon_crtc->crtc_offset, 0);
	if (ASIC_IS_DCE8(rdev)) {
		/* XXX this only needs to be programmed once per crtc at startup,
		 * not sure where the best place for it is
		 */
		WREG32(CIK_ALPHA_CONTROL + radeon_crtc->crtc_offset,
		       CIK_CURSOR_ALPHA_BLND_ENA);
	}
}

static void legacy_crtc_load_lut(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	u16 *r, *g, *b;
	int i;
	uint32_t dac2_cntl;

	dac2_cntl = RREG32(RADEON_DAC_CNTL2);
	if (radeon_crtc->crtc_id == 0)
		dac2_cntl &= (uint32_t)~RADEON_DAC2_PALETTE_ACC_CTL;
	else
		dac2_cntl |= RADEON_DAC2_PALETTE_ACC_CTL;
	WREG32(RADEON_DAC_CNTL2, dac2_cntl);

	WREG8(RADEON_PALETTE_INDEX, 0);
	r = crtc->gamma_store;
	g = r + crtc->gamma_size;
	b = g + crtc->gamma_size;
	for (i = 0; i < 256; i++) {
		WREG32(RADEON_PALETTE_30_DATA,
		       ((*r++ & 0xffc0) << 14) |
		       ((*g++ & 0xffc0) << 4) |
		       (*b++ >> 6));
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

static int radeon_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
				 u16 *blue, uint32_t size,
				 struct drm_modeset_acquire_ctx *ctx)
{
	radeon_crtc_load_lut(crtc);

	return 0;
}

static void radeon_crtc_destroy(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);

	drm_crtc_cleanup(crtc);
	destroy_workqueue(radeon_crtc->flip_queue);
	kfree(radeon_crtc);
}

/**
 * radeon_unpin_work_func - unpin old buffer object
 *
 * @__work - kernel work item
 *
 * Unpin the old frame buffer object outside of the interrupt handler
 */
static void radeon_unpin_work_func(struct work_struct *__work)
{
	struct radeon_flip_work *work =
		container_of(__work, struct radeon_flip_work, unpin_work);
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

	drm_gem_object_put_unlocked(&work->old_rbo->gem_base);
	kfree(work);
}

void radeon_crtc_handle_vblank(struct radeon_device *rdev, int crtc_id)
{
	struct radeon_crtc *radeon_crtc = rdev->mode_info.crtcs[crtc_id];
	unsigned long flags;
	u32 update_pending;
	int vpos, hpos;

	/* can happen during initialization */
	if (radeon_crtc == NULL)
		return;

	/* Skip the pageflip completion check below (based on polling) on
	 * asics which reliably support hw pageflip completion irqs. pflip
	 * irqs are a reliable and race-free method of handling pageflip
	 * completion detection. A use_pflipirq module parameter < 2 allows
	 * to override this in case of asics with faulty pflip irqs.
	 * A module parameter of 0 would only use this polling based path,
	 * a parameter of 1 would use pflip irq only as a backup to this
	 * path, as in Linux 3.16.
	 */
	if ((radeon_use_pflipirq == 2) && ASIC_IS_DCE4(rdev))
		return;

	spin_lock_irqsave(&rdev->ddev->event_lock, flags);
	if (radeon_crtc->flip_status != RADEON_FLIP_SUBMITTED) {
		DRM_DEBUG_DRIVER("radeon_crtc->flip_status = %d != "
				 "RADEON_FLIP_SUBMITTED(%d)\n",
				 radeon_crtc->flip_status,
				 RADEON_FLIP_SUBMITTED);
		spin_unlock_irqrestore(&rdev->ddev->event_lock, flags);
		return;
	}

	update_pending = radeon_page_flip_pending(rdev, crtc_id);

	/* Has the pageflip already completed in crtc, or is it certain
	 * to complete in this vblank? GET_DISTANCE_TO_VBLANKSTART provides
	 * distance to start of "fudged earlier" vblank in vpos, distance to
	 * start of real vblank in hpos. vpos >= 0 && hpos < 0 means we are in
	 * the last few scanlines before start of real vblank, where the vblank
	 * irq can fire, so we have sampled update_pending a bit too early and
	 * know the flip will complete at leading edge of the upcoming real
	 * vblank. On pre-AVIVO hardware, flips also complete inside the real
	 * vblank, not only at leading edge, so if update_pending for hpos >= 0
	 *  == inside real vblank, the flip will complete almost immediately.
	 * Note that this method of completion handling is still not 100% race
	 * free, as we could execute before the radeon_flip_work_func managed
	 * to run and set the RADEON_FLIP_SUBMITTED status, thereby we no-op,
	 * but the flip still gets programmed into hw and completed during
	 * vblank, leading to a delayed emission of the flip completion event.
	 * This applies at least to pre-AVIVO hardware, where flips are always
	 * completing inside vblank, not only at leading edge of vblank.
	 */
	if (update_pending &&
	    (DRM_SCANOUTPOS_VALID &
	     radeon_get_crtc_scanoutpos(rdev->ddev, crtc_id,
					GET_DISTANCE_TO_VBLANKSTART,
					&vpos, &hpos, NULL, NULL,
					&rdev->mode_info.crtcs[crtc_id]->base.hwmode)) &&
	    ((vpos >= 0 && hpos < 0) || (hpos >= 0 && !ASIC_IS_AVIVO(rdev)))) {
		/* crtc didn't flip in this target vblank interval,
		 * but flip is pending in crtc. Based on the current
		 * scanout position we know that the current frame is
		 * (nearly) complete and the flip will (likely)
		 * complete before the start of the next frame.
		 */
		update_pending = 0;
	}
	spin_unlock_irqrestore(&rdev->ddev->event_lock, flags);
	if (!update_pending)
		radeon_crtc_handle_flip(rdev, crtc_id);
}

/**
 * radeon_crtc_handle_flip - page flip completed
 *
 * @rdev: radeon device pointer
 * @crtc_id: crtc number this event is for
 *
 * Called when we are sure that a page flip for this crtc is completed.
 */
void radeon_crtc_handle_flip(struct radeon_device *rdev, int crtc_id)
{
	struct radeon_crtc *radeon_crtc = rdev->mode_info.crtcs[crtc_id];
	struct radeon_flip_work *work;
	unsigned long flags;

	/* this can happen at init */
	if (radeon_crtc == NULL)
		return;

	spin_lock_irqsave(&rdev->ddev->event_lock, flags);
	work = radeon_crtc->flip_work;
	if (radeon_crtc->flip_status != RADEON_FLIP_SUBMITTED) {
		DRM_DEBUG_DRIVER("radeon_crtc->flip_status = %d != "
				 "RADEON_FLIP_SUBMITTED(%d)\n",
				 radeon_crtc->flip_status,
				 RADEON_FLIP_SUBMITTED);
		spin_unlock_irqrestore(&rdev->ddev->event_lock, flags);
		return;
	}

	/* Pageflip completed. Clean up. */
	radeon_crtc->flip_status = RADEON_FLIP_NONE;
	radeon_crtc->flip_work = NULL;

	/* wakeup userspace */
	if (work->event)
		drm_crtc_send_vblank_event(&radeon_crtc->base, work->event);

	spin_unlock_irqrestore(&rdev->ddev->event_lock, flags);

	drm_crtc_vblank_put(&radeon_crtc->base);
	radeon_irq_kms_pflip_irq_put(rdev, work->crtc_id);
	queue_work(radeon_crtc->flip_queue, &work->unpin_work);
}

/**
 * radeon_flip_work_func - page flip framebuffer
 *
 * @work - kernel work item
 *
 * Wait for the buffer object to become idle and do the actual page flip
 */
static void radeon_flip_work_func(struct work_struct *__work)
{
	struct radeon_flip_work *work =
		container_of(__work, struct radeon_flip_work, flip_work);
	struct radeon_device *rdev = work->rdev;
	struct drm_device *dev = rdev->ddev;
	struct radeon_crtc *radeon_crtc = rdev->mode_info.crtcs[work->crtc_id];

	struct drm_crtc *crtc = &radeon_crtc->base;
	unsigned long flags;
	int r;
	int vpos, hpos;

	down_read(&rdev->exclusive_lock);
	if (work->fence) {
		struct radeon_fence *fence;

		fence = to_radeon_fence(work->fence);
		if (fence && fence->rdev == rdev) {
			r = radeon_fence_wait(fence, false);
			if (r == -EDEADLK) {
				up_read(&rdev->exclusive_lock);
				do {
					r = radeon_gpu_reset(rdev);
				} while (r == -EAGAIN);
				down_read(&rdev->exclusive_lock);
			}
		} else
			r = dma_fence_wait(work->fence, false);

		if (r)
			DRM_ERROR("failed to wait on page flip fence (%d)!\n", r);

		/* We continue with the page flip even if we failed to wait on
		 * the fence, otherwise the DRM core and userspace will be
		 * confused about which BO the CRTC is scanning out
		 */

		dma_fence_put(work->fence);
		work->fence = NULL;
	}

	/* Wait until we're out of the vertical blank period before the one
	 * targeted by the flip. Always wait on pre DCE4 to avoid races with
	 * flip completion handling from vblank irq, as these old asics don't
	 * have reliable pageflip completion interrupts.
	 */
	while (radeon_crtc->enabled &&
		(radeon_get_crtc_scanoutpos(dev, work->crtc_id, 0,
					    &vpos, &hpos, NULL, NULL,
					    &crtc->hwmode)
		& (DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_IN_VBLANK)) ==
		(DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_IN_VBLANK) &&
		(!ASIC_IS_AVIVO(rdev) ||
		((int) (work->target_vblank -
		dev->driver->get_vblank_counter(dev, work->crtc_id)) > 0)))
		usleep_range(1000, 2000);

	/* We borrow the event spin lock for protecting flip_status */
	spin_lock_irqsave(&crtc->dev->event_lock, flags);

	/* set the proper interrupt */
	radeon_irq_kms_pflip_irq_get(rdev, radeon_crtc->crtc_id);

	/* do the flip (mmio) */
	radeon_page_flip(rdev, radeon_crtc->crtc_id, work->base, work->async);

	radeon_crtc->flip_status = RADEON_FLIP_SUBMITTED;
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
	up_read(&rdev->exclusive_lock);
}

static int radeon_crtc_page_flip_target(struct drm_crtc *crtc,
					struct drm_framebuffer *fb,
					struct drm_pending_vblank_event *event,
					uint32_t page_flip_flags,
					uint32_t target,
					struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_framebuffer *old_radeon_fb;
	struct radeon_framebuffer *new_radeon_fb;
	struct drm_gem_object *obj;
	struct radeon_flip_work *work;
	struct radeon_bo *new_rbo;
	uint32_t tiling_flags, pitch_pixels;
	uint64_t base;
	unsigned long flags;
	int r;

	work = kzalloc(sizeof *work, GFP_KERNEL);
	if (work == NULL)
		return -ENOMEM;

	INIT_WORK(&work->flip_work, radeon_flip_work_func);
	INIT_WORK(&work->unpin_work, radeon_unpin_work_func);

	work->rdev = rdev;
	work->crtc_id = radeon_crtc->crtc_id;
	work->event = event;
	work->async = (page_flip_flags & DRM_MODE_PAGE_FLIP_ASYNC) != 0;

	/* schedule unpin of the old buffer */
	old_radeon_fb = to_radeon_framebuffer(crtc->primary->fb);
	obj = old_radeon_fb->base.obj[0];

	/* take a reference to the old object */
	drm_gem_object_get(obj);
	work->old_rbo = gem_to_radeon_bo(obj);

	new_radeon_fb = to_radeon_framebuffer(fb);
	obj = new_radeon_fb->base.obj[0];
	new_rbo = gem_to_radeon_bo(obj);

	/* pin the new buffer */
	DRM_DEBUG_DRIVER("flip-ioctl() cur_rbo = %p, new_rbo = %p\n",
			 work->old_rbo, new_rbo);

	r = radeon_bo_reserve(new_rbo, false);
	if (unlikely(r != 0)) {
		DRM_ERROR("failed to reserve new rbo buffer before flip\n");
		goto cleanup;
	}
	/* Only 27 bit offset for legacy CRTC */
	r = radeon_bo_pin_restricted(new_rbo, RADEON_GEM_DOMAIN_VRAM,
				     ASIC_IS_AVIVO(rdev) ? 0 : 1 << 27, &base);
	if (unlikely(r != 0)) {
		radeon_bo_unreserve(new_rbo);
		r = -EINVAL;
		DRM_ERROR("failed to pin new rbo buffer before flip\n");
		goto cleanup;
	}
	work->fence = dma_fence_get(reservation_object_get_excl(new_rbo->tbo.resv));
	radeon_bo_get_tiling_flags(new_rbo, &tiling_flags, NULL);
	radeon_bo_unreserve(new_rbo);

	if (!ASIC_IS_AVIVO(rdev)) {
		/* crtc offset is from display base addr not FB location */
		base -= radeon_crtc->legacy_display_base_addr;
		pitch_pixels = fb->pitches[0] / fb->format->cpp[0];

		if (tiling_flags & RADEON_TILING_MACRO) {
			if (ASIC_IS_R300(rdev)) {
				base &= ~0x7ff;
			} else {
				int byteshift = fb->format->cpp[0] * 8 >> 4;
				int tile_addr = (((crtc->y >> 3) * pitch_pixels +  crtc->x) >> (8 - byteshift)) << 11;
				base += tile_addr + ((crtc->x << byteshift) % 256) + ((crtc->y % 8) << 8);
			}
		} else {
			int offset = crtc->y * pitch_pixels + crtc->x;
			switch (fb->format->cpp[0] * 8) {
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
	work->base = base;
	work->target_vblank = target - (uint32_t)drm_crtc_vblank_count(crtc) +
		dev->driver->get_vblank_counter(dev, work->crtc_id);

	/* We borrow the event spin lock for protecting flip_work */
	spin_lock_irqsave(&crtc->dev->event_lock, flags);

	if (radeon_crtc->flip_status != RADEON_FLIP_NONE) {
		DRM_DEBUG_DRIVER("flip queue: crtc already busy\n");
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
		r = -EBUSY;
		goto pflip_cleanup;
	}
	radeon_crtc->flip_status = RADEON_FLIP_PENDING;
	radeon_crtc->flip_work = work;

	/* update crtc fb */
	crtc->primary->fb = fb;

	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

	queue_work(radeon_crtc->flip_queue, &work->flip_work);
	return 0;

pflip_cleanup:
	if (unlikely(radeon_bo_reserve(new_rbo, false) != 0)) {
		DRM_ERROR("failed to reserve new rbo in error path\n");
		goto cleanup;
	}
	if (unlikely(radeon_bo_unpin(new_rbo) != 0)) {
		DRM_ERROR("failed to unpin new rbo in error path\n");
	}
	radeon_bo_unreserve(new_rbo);

cleanup:
	drm_gem_object_put_unlocked(&work->old_rbo->gem_base);
	dma_fence_put(work->fence);
	kfree(work);
	return r;
}

static int
radeon_crtc_set_config(struct drm_mode_set *set,
		       struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_device *dev;
	struct radeon_device *rdev;
	struct drm_crtc *crtc;
	bool active = false;
	int ret;

	if (!set || !set->crtc)
		return -EINVAL;

	dev = set->crtc->dev;

	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0)
		return ret;

	ret = drm_crtc_helper_set_config(set, ctx);

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		if (crtc->enabled)
			active = true;

	pm_runtime_mark_last_busy(dev->dev);

	rdev = dev->dev_private;
	/* if we have active crtcs and we don't have a power ref,
	   take the current one */
	if (active && !rdev->have_disp_power_ref) {
		rdev->have_disp_power_ref = true;
		return ret;
	}
	/* if we have no active crtcs, then drop the power ref
	   we got before */
	if (!active && rdev->have_disp_power_ref) {
		pm_runtime_put_autosuspend(dev->dev);
		rdev->have_disp_power_ref = false;
	}

	/* drop the power reference we got coming in here */
	pm_runtime_put_autosuspend(dev->dev);
	return ret;
}

static const struct drm_crtc_funcs radeon_crtc_funcs = {
	.cursor_set2 = radeon_crtc_cursor_set2,
	.cursor_move = radeon_crtc_cursor_move,
	.gamma_set = radeon_crtc_gamma_set,
	.set_config = radeon_crtc_set_config,
	.destroy = radeon_crtc_destroy,
	.page_flip_target = radeon_crtc_page_flip_target,
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
	radeon_crtc->flip_queue = alloc_workqueue("radeon-crtc", WQ_HIGHPRI, 0);
	rdev->mode_info.crtcs[index] = radeon_crtc;

	if (rdev->family >= CHIP_BONAIRE) {
		radeon_crtc->max_cursor_width = CIK_CURSOR_WIDTH;
		radeon_crtc->max_cursor_height = CIK_CURSOR_HEIGHT;
	} else {
		radeon_crtc->max_cursor_width = CURSOR_WIDTH;
		radeon_crtc->max_cursor_height = CURSOR_HEIGHT;
	}
	dev->mode_config.cursor_width = radeon_crtc->max_cursor_width;
	dev->mode_config.cursor_height = radeon_crtc->max_cursor_height;

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

static const char *encoder_names[38] = {
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
	"INTERNAL_VCE",
	"INTERNAL_UNIPHY3",
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
		DRM_INFO("  %s\n", connector->name);
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

/* avivo */

/**
 * avivo_reduce_ratio - fractional number reduction
 *
 * @nom: nominator
 * @den: denominator
 * @nom_min: minimum value for nominator
 * @den_min: minimum value for denominator
 *
 * Find the greatest common divisor and apply it on both nominator and
 * denominator, but make nominator and denominator are at least as large
 * as their minimum values.
 */
static void avivo_reduce_ratio(unsigned *nom, unsigned *den,
			       unsigned nom_min, unsigned den_min)
{
	unsigned tmp;

	/* reduce the numbers to a simpler ratio */
	tmp = gcd(*nom, *den);
	*nom /= tmp;
	*den /= tmp;

	/* make sure nominator is large enough */
	if (*nom < nom_min) {
		tmp = DIV_ROUND_UP(nom_min, *nom);
		*nom *= tmp;
		*den *= tmp;
	}

	/* make sure the denominator is large enough */
	if (*den < den_min) {
		tmp = DIV_ROUND_UP(den_min, *den);
		*nom *= tmp;
		*den *= tmp;
	}
}

/**
 * avivo_get_fb_ref_div - feedback and ref divider calculation
 *
 * @nom: nominator
 * @den: denominator
 * @post_div: post divider
 * @fb_div_max: feedback divider maximum
 * @ref_div_max: reference divider maximum
 * @fb_div: resulting feedback divider
 * @ref_div: resulting reference divider
 *
 * Calculate feedback and reference divider for a given post divider. Makes
 * sure we stay within the limits.
 */
static void avivo_get_fb_ref_div(unsigned nom, unsigned den, unsigned post_div,
				 unsigned fb_div_max, unsigned ref_div_max,
				 unsigned *fb_div, unsigned *ref_div)
{
	/* limit reference * post divider to a maximum */
	ref_div_max = max(min(100 / post_div, ref_div_max), 1u);

	/* get matching reference and feedback divider */
	*ref_div = min(max(DIV_ROUND_CLOSEST(den, post_div), 1u), ref_div_max);
	*fb_div = DIV_ROUND_CLOSEST(nom * *ref_div * post_div, den);

	/* limit fb divider to its maximum */
	if (*fb_div > fb_div_max) {
		*ref_div = DIV_ROUND_CLOSEST(*ref_div * fb_div_max, *fb_div);
		*fb_div = fb_div_max;
	}
}

/**
 * radeon_compute_pll_avivo - compute PLL paramaters
 *
 * @pll: information about the PLL
 * @dot_clock_p: resulting pixel clock
 * fb_div_p: resulting feedback divider
 * frac_fb_div_p: fractional part of the feedback divider
 * ref_div_p: resulting reference divider
 * post_div_p: resulting reference divider
 *
 * Try to calculate the PLL parameters to generate the given frequency:
 * dot_clock = (ref_freq * feedback_div) / (ref_div * post_div)
 */
void radeon_compute_pll_avivo(struct radeon_pll *pll,
			      u32 freq,
			      u32 *dot_clock_p,
			      u32 *fb_div_p,
			      u32 *frac_fb_div_p,
			      u32 *ref_div_p,
			      u32 *post_div_p)
{
	unsigned target_clock = pll->flags & RADEON_PLL_USE_FRAC_FB_DIV ?
		freq : freq / 10;

	unsigned fb_div_min, fb_div_max, fb_div;
	unsigned post_div_min, post_div_max, post_div;
	unsigned ref_div_min, ref_div_max, ref_div;
	unsigned post_div_best, diff_best;
	unsigned nom, den;

	/* determine allowed feedback divider range */
	fb_div_min = pll->min_feedback_div;
	fb_div_max = pll->max_feedback_div;

	if (pll->flags & RADEON_PLL_USE_FRAC_FB_DIV) {
		fb_div_min *= 10;
		fb_div_max *= 10;
	}

	/* determine allowed ref divider range */
	if (pll->flags & RADEON_PLL_USE_REF_DIV)
		ref_div_min = pll->reference_div;
	else
		ref_div_min = pll->min_ref_div;

	if (pll->flags & RADEON_PLL_USE_FRAC_FB_DIV &&
	    pll->flags & RADEON_PLL_USE_REF_DIV)
		ref_div_max = pll->reference_div;
	else if (pll->flags & RADEON_PLL_PREFER_MINM_OVER_MAXP)
		/* fix for problems on RS880 */
		ref_div_max = min(pll->max_ref_div, 7u);
	else
		ref_div_max = pll->max_ref_div;

	/* determine allowed post divider range */
	if (pll->flags & RADEON_PLL_USE_POST_DIV) {
		post_div_min = pll->post_div;
		post_div_max = pll->post_div;
	} else {
		unsigned vco_min, vco_max;

		if (pll->flags & RADEON_PLL_IS_LCD) {
			vco_min = pll->lcd_pll_out_min;
			vco_max = pll->lcd_pll_out_max;
		} else {
			vco_min = pll->pll_out_min;
			vco_max = pll->pll_out_max;
		}

		if (pll->flags & RADEON_PLL_USE_FRAC_FB_DIV) {
			vco_min *= 10;
			vco_max *= 10;
		}

		post_div_min = vco_min / target_clock;
		if ((target_clock * post_div_min) < vco_min)
			++post_div_min;
		if (post_div_min < pll->min_post_div)
			post_div_min = pll->min_post_div;

		post_div_max = vco_max / target_clock;
		if ((target_clock * post_div_max) > vco_max)
			--post_div_max;
		if (post_div_max > pll->max_post_div)
			post_div_max = pll->max_post_div;
	}

	/* represent the searched ratio as fractional number */
	nom = target_clock;
	den = pll->reference_freq;

	/* reduce the numbers to a simpler ratio */
	avivo_reduce_ratio(&nom, &den, fb_div_min, post_div_min);

	/* now search for a post divider */
	if (pll->flags & RADEON_PLL_PREFER_MINM_OVER_MAXP)
		post_div_best = post_div_min;
	else
		post_div_best = post_div_max;
	diff_best = ~0;

	for (post_div = post_div_min; post_div <= post_div_max; ++post_div) {
		unsigned diff;
		avivo_get_fb_ref_div(nom, den, post_div, fb_div_max,
				     ref_div_max, &fb_div, &ref_div);
		diff = abs(target_clock - (pll->reference_freq * fb_div) /
			(ref_div * post_div));

		if (diff < diff_best || (diff == diff_best &&
		    !(pll->flags & RADEON_PLL_PREFER_MINM_OVER_MAXP))) {

			post_div_best = post_div;
			diff_best = diff;
		}
	}
	post_div = post_div_best;

	/* get the feedback and reference divider for the optimal value */
	avivo_get_fb_ref_div(nom, den, post_div, fb_div_max, ref_div_max,
			     &fb_div, &ref_div);

	/* reduce the numbers to a simpler ratio once more */
	/* this also makes sure that the reference divider is large enough */
	avivo_reduce_ratio(&fb_div, &ref_div, fb_div_min, ref_div_min);

	/* avoid high jitter with small fractional dividers */
	if (pll->flags & RADEON_PLL_USE_FRAC_FB_DIV && (fb_div % 10)) {
		fb_div_min = max(fb_div_min, (9 - (fb_div % 10)) * 20 + 50);
		if (fb_div < fb_div_min) {
			unsigned tmp = DIV_ROUND_UP(fb_div_min, fb_div);
			fb_div *= tmp;
			ref_div *= tmp;
		}
	}

	/* and finally save the result */
	if (pll->flags & RADEON_PLL_USE_FRAC_FB_DIV) {
		*fb_div_p = fb_div / 10;
		*frac_fb_div_p = fb_div % 10;
	} else {
		*fb_div_p = fb_div;
		*frac_fb_div_p = 0;
	}

	*dot_clock_p = ((pll->reference_freq * *fb_div_p * 10) +
			(pll->reference_freq * *frac_fb_div_p)) /
		       (ref_div * post_div * 10);
	*ref_div_p = ref_div;
	*post_div_p = post_div;

	DRM_DEBUG_KMS("%d - %d, pll dividers - fb: %d.%d ref: %d, post %d\n",
		      freq, *dot_clock_p * 10, *fb_div_p, *frac_fb_div_p,
		      ref_div, post_div);
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

static const struct drm_framebuffer_funcs radeon_fb_funcs = {
	.destroy = drm_gem_fb_destroy,
	.create_handle = drm_gem_fb_create_handle,
};

int
radeon_framebuffer_init(struct drm_device *dev,
			struct radeon_framebuffer *rfb,
			const struct drm_mode_fb_cmd2 *mode_cmd,
			struct drm_gem_object *obj)
{
	int ret;
	rfb->base.obj[0] = obj;
	drm_helper_mode_fill_fb_struct(dev, &rfb->base, mode_cmd);
	ret = drm_framebuffer_init(dev, &rfb->base, &radeon_fb_funcs);
	if (ret) {
		rfb->base.obj[0] = NULL;
		return ret;
	}
	return 0;
}

static struct drm_framebuffer *
radeon_user_framebuffer_create(struct drm_device *dev,
			       struct drm_file *file_priv,
			       const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj;
	struct radeon_framebuffer *radeon_fb;
	int ret;

	obj = drm_gem_object_lookup(file_priv, mode_cmd->handles[0]);
	if (obj ==  NULL) {
		dev_err(&dev->pdev->dev, "No GEM object associated to handle 0x%08X, "
			"can't create framebuffer\n", mode_cmd->handles[0]);
		return ERR_PTR(-ENOENT);
	}

	/* Handle is imported dma-buf, so cannot be migrated to VRAM for scanout */
	if (obj->import_attach) {
		DRM_DEBUG_KMS("Cannot create framebuffer from imported dma_buf\n");
		return ERR_PTR(-EINVAL);
	}

	radeon_fb = kzalloc(sizeof(*radeon_fb), GFP_KERNEL);
	if (radeon_fb == NULL) {
		drm_gem_object_put_unlocked(obj);
		return ERR_PTR(-ENOMEM);
	}

	ret = radeon_framebuffer_init(dev, radeon_fb, mode_cmd, obj);
	if (ret) {
		kfree(radeon_fb);
		drm_gem_object_put_unlocked(obj);
		return ERR_PTR(ret);
	}

	return &radeon_fb->base;
}

static const struct drm_mode_config_funcs radeon_mode_funcs = {
	.fb_create = radeon_user_framebuffer_create,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
};

static const struct drm_prop_enum_list radeon_tmds_pll_enum_list[] =
{	{ 0, "driver" },
	{ 1, "bios" },
};

static const struct drm_prop_enum_list radeon_tv_std_enum_list[] =
{	{ TV_STD_NTSC, "ntsc" },
	{ TV_STD_PAL, "pal" },
	{ TV_STD_PAL_M, "pal-m" },
	{ TV_STD_PAL_60, "pal-60" },
	{ TV_STD_NTSC_J, "ntsc-j" },
	{ TV_STD_SCART_PAL, "scart-pal" },
	{ TV_STD_PAL_CN, "pal-cn" },
	{ TV_STD_SECAM, "secam" },
};

static const struct drm_prop_enum_list radeon_underscan_enum_list[] =
{	{ UNDERSCAN_OFF, "off" },
	{ UNDERSCAN_ON, "on" },
	{ UNDERSCAN_AUTO, "auto" },
};

static const struct drm_prop_enum_list radeon_audio_enum_list[] =
{	{ RADEON_AUDIO_DISABLE, "off" },
	{ RADEON_AUDIO_ENABLE, "on" },
	{ RADEON_AUDIO_AUTO, "auto" },
};

/* XXX support different dither options? spatial, temporal, both, etc. */
static const struct drm_prop_enum_list radeon_dither_enum_list[] =
{	{ RADEON_FMT_DITHER_DISABLE, "off" },
	{ RADEON_FMT_DITHER_ENABLE, "on" },
};

static const struct drm_prop_enum_list radeon_output_csc_enum_list[] =
{	{ RADEON_OUTPUT_CSC_BYPASS, "bypass" },
	{ RADEON_OUTPUT_CSC_TVRGB, "tvrgb" },
	{ RADEON_OUTPUT_CSC_YCBCR601, "ycbcr601" },
	{ RADEON_OUTPUT_CSC_YCBCR709, "ycbcr709" },
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

	sz = ARRAY_SIZE(radeon_audio_enum_list);
	rdev->mode_info.audio_property =
		drm_property_create_enum(rdev->ddev, 0,
					 "audio",
					 radeon_audio_enum_list, sz);

	sz = ARRAY_SIZE(radeon_dither_enum_list);
	rdev->mode_info.dither_property =
		drm_property_create_enum(rdev->ddev, 0,
					 "dither",
					 radeon_dither_enum_list, sz);

	sz = ARRAY_SIZE(radeon_output_csc_enum_list);
	rdev->mode_info.output_csc_property =
		drm_property_create_enum(rdev->ddev, 0,
					 "output_csc",
					 radeon_output_csc_enum_list, sz);

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

	if (ASIC_IS_NODCE(rdev)) {
		/* nothing to do */
	} else if (ASIC_IS_DCE4(rdev)) {
		static uint32_t eg_offsets[] = {
			EVERGREEN_CRTC0_REGISTER_OFFSET,
			EVERGREEN_CRTC1_REGISTER_OFFSET,
			EVERGREEN_CRTC2_REGISTER_OFFSET,
			EVERGREEN_CRTC3_REGISTER_OFFSET,
			EVERGREEN_CRTC4_REGISTER_OFFSET,
			EVERGREEN_CRTC5_REGISTER_OFFSET,
			0x13830 - 0x7030,
		};
		int num_afmt;

		/* DCE8 has 7 audio blocks tied to DIG encoders */
		/* DCE6 has 6 audio blocks tied to DIG encoders */
		/* DCE4/5 has 6 audio blocks tied to DIG encoders */
		/* DCE4.1 has 2 audio blocks tied to DIG encoders */
		if (ASIC_IS_DCE8(rdev))
			num_afmt = 7;
		else if (ASIC_IS_DCE6(rdev))
			num_afmt = 6;
		else if (ASIC_IS_DCE5(rdev))
			num_afmt = 6;
		else if (ASIC_IS_DCE41(rdev))
			num_afmt = 2;
		else /* DCE4 */
			num_afmt = 6;

		BUG_ON(num_afmt > ARRAY_SIZE(eg_offsets));
		for (i = 0; i < num_afmt; i++) {
			rdev->mode_info.afmt[i] = kzalloc(sizeof(struct radeon_afmt), GFP_KERNEL);
			if (rdev->mode_info.afmt[i]) {
				rdev->mode_info.afmt[i]->offset = eg_offsets[i];
				rdev->mode_info.afmt[i]->id = i;
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

	if (radeon_use_pflipirq == 2 && rdev->family >= CHIP_R600)
		rdev->ddev->mode_config.async_page_flip = true;

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

	radeon_fbdev_init(rdev);
	drm_kms_helper_poll_init(rdev->ddev);

	/* do pm late init */
	ret = radeon_pm_late_init(rdev);

	return 0;
}

void radeon_modeset_fini(struct radeon_device *rdev)
{
	if (rdev->mode_info.mode_config_initialized) {
		drm_kms_helper_poll_fini(rdev->ddev);
		radeon_hpd_fini(rdev);
		drm_crtc_force_disable_all(rdev->ddev);
		radeon_fbdev_fini(rdev);
		radeon_afmt_fini(rdev);
		drm_mode_config_cleanup(rdev->ddev);
		rdev->mode_info.mode_config_initialized = false;
	}

	kfree(rdev->mode_info.bios_hardcoded_edid);

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
			      drm_detect_hdmi_monitor(radeon_connector_edid(connector)) &&
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
 * Retrieve current video scanout position of crtc on a given gpu, and
 * an optional accurate timestamp of when query happened.
 *
 * \param dev Device to query.
 * \param crtc Crtc to query.
 * \param flags Flags from caller (DRM_CALLED_FROM_VBLIRQ or 0).
 *              For driver internal use only also supports these flags:
 *
 *              USE_REAL_VBLANKSTART to use the real start of vblank instead
 *              of a fudged earlier start of vblank.
 *
 *              GET_DISTANCE_TO_VBLANKSTART to return distance to the
 *              fudged earlier start of vblank in *vpos and the distance
 *              to true start of vblank in *hpos.
 *
 * \param *vpos Location where vertical scanout position should be stored.
 * \param *hpos Location where horizontal scanout position should go.
 * \param *stime Target location for timestamp taken immediately before
 *               scanout position query. Can be NULL to skip timestamp.
 * \param *etime Target location for timestamp taken immediately after
 *               scanout position query. Can be NULL to skip timestamp.
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
int radeon_get_crtc_scanoutpos(struct drm_device *dev, unsigned int pipe,
			       unsigned int flags, int *vpos, int *hpos,
			       ktime_t *stime, ktime_t *etime,
			       const struct drm_display_mode *mode)
{
	u32 stat_crtc = 0, vbl = 0, position = 0;
	int vbl_start, vbl_end, vtotal, ret = 0;
	bool in_vbl = true;

	struct radeon_device *rdev = dev->dev_private;

	/* preempt_disable_rt() should go right here in PREEMPT_RT patchset. */

	/* Get optional system timestamp before query. */
	if (stime)
		*stime = ktime_get();

	if (ASIC_IS_DCE4(rdev)) {
		if (pipe == 0) {
			vbl = RREG32(EVERGREEN_CRTC_V_BLANK_START_END +
				     EVERGREEN_CRTC0_REGISTER_OFFSET);
			position = RREG32(EVERGREEN_CRTC_STATUS_POSITION +
					  EVERGREEN_CRTC0_REGISTER_OFFSET);
			ret |= DRM_SCANOUTPOS_VALID;
		}
		if (pipe == 1) {
			vbl = RREG32(EVERGREEN_CRTC_V_BLANK_START_END +
				     EVERGREEN_CRTC1_REGISTER_OFFSET);
			position = RREG32(EVERGREEN_CRTC_STATUS_POSITION +
					  EVERGREEN_CRTC1_REGISTER_OFFSET);
			ret |= DRM_SCANOUTPOS_VALID;
		}
		if (pipe == 2) {
			vbl = RREG32(EVERGREEN_CRTC_V_BLANK_START_END +
				     EVERGREEN_CRTC2_REGISTER_OFFSET);
			position = RREG32(EVERGREEN_CRTC_STATUS_POSITION +
					  EVERGREEN_CRTC2_REGISTER_OFFSET);
			ret |= DRM_SCANOUTPOS_VALID;
		}
		if (pipe == 3) {
			vbl = RREG32(EVERGREEN_CRTC_V_BLANK_START_END +
				     EVERGREEN_CRTC3_REGISTER_OFFSET);
			position = RREG32(EVERGREEN_CRTC_STATUS_POSITION +
					  EVERGREEN_CRTC3_REGISTER_OFFSET);
			ret |= DRM_SCANOUTPOS_VALID;
		}
		if (pipe == 4) {
			vbl = RREG32(EVERGREEN_CRTC_V_BLANK_START_END +
				     EVERGREEN_CRTC4_REGISTER_OFFSET);
			position = RREG32(EVERGREEN_CRTC_STATUS_POSITION +
					  EVERGREEN_CRTC4_REGISTER_OFFSET);
			ret |= DRM_SCANOUTPOS_VALID;
		}
		if (pipe == 5) {
			vbl = RREG32(EVERGREEN_CRTC_V_BLANK_START_END +
				     EVERGREEN_CRTC5_REGISTER_OFFSET);
			position = RREG32(EVERGREEN_CRTC_STATUS_POSITION +
					  EVERGREEN_CRTC5_REGISTER_OFFSET);
			ret |= DRM_SCANOUTPOS_VALID;
		}
	} else if (ASIC_IS_AVIVO(rdev)) {
		if (pipe == 0) {
			vbl = RREG32(AVIVO_D1CRTC_V_BLANK_START_END);
			position = RREG32(AVIVO_D1CRTC_STATUS_POSITION);
			ret |= DRM_SCANOUTPOS_VALID;
		}
		if (pipe == 1) {
			vbl = RREG32(AVIVO_D2CRTC_V_BLANK_START_END);
			position = RREG32(AVIVO_D2CRTC_STATUS_POSITION);
			ret |= DRM_SCANOUTPOS_VALID;
		}
	} else {
		/* Pre-AVIVO: Different encoding of scanout pos and vblank interval. */
		if (pipe == 0) {
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
		if (pipe == 1) {
			vbl = (RREG32(RADEON_CRTC2_V_TOTAL_DISP) &
				RADEON_CRTC_V_DISP) >> RADEON_CRTC_V_DISP_SHIFT;
			position = (RREG32(RADEON_CRTC2_VLINE_CRNT_VLINE) >> 16) & RADEON_CRTC_V_TOTAL;
			stat_crtc = RREG32(RADEON_CRTC2_STATUS);
			if (!(stat_crtc & 1))
				in_vbl = false;

			ret |= DRM_SCANOUTPOS_VALID;
		}
	}

	/* Get optional system timestamp after query. */
	if (etime)
		*etime = ktime_get();

	/* preempt_enable_rt() should go right here in PREEMPT_RT patchset. */

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
		vbl_start = mode->crtc_vdisplay;
		vbl_end = 0;
	}

	/* Called from driver internal vblank counter query code? */
	if (flags & GET_DISTANCE_TO_VBLANKSTART) {
	    /* Caller wants distance from real vbl_start in *hpos */
	    *hpos = *vpos - vbl_start;
	}

	/* Fudge vblank to start a few scanlines earlier to handle the
	 * problem that vblank irqs fire a few scanlines before start
	 * of vblank. Some driver internal callers need the true vblank
	 * start to be used and signal this via the USE_REAL_VBLANKSTART flag.
	 *
	 * The cause of the "early" vblank irq is that the irq is triggered
	 * by the line buffer logic when the line buffer read position enters
	 * the vblank, whereas our crtc scanout position naturally lags the
	 * line buffer read position.
	 */
	if (!(flags & USE_REAL_VBLANKSTART))
		vbl_start -= rdev->mode_info.crtcs[pipe]->lb_vblank_lead_lines;

	/* Test scanout position against vblank region. */
	if ((*vpos < vbl_start) && (*vpos >= vbl_end))
		in_vbl = false;

	/* In vblank? */
	if (in_vbl)
	    ret |= DRM_SCANOUTPOS_IN_VBLANK;

	/* Called from driver internal vblank counter query code? */
	if (flags & GET_DISTANCE_TO_VBLANKSTART) {
		/* Caller wants distance from fudged earlier vbl_start */
		*vpos -= vbl_start;
		return ret;
	}

	/* Check if inside vblank area and apply corrective offsets:
	 * vpos will then be >=0 in video scanout area, but negative
	 * within vblank area, counting down the number of lines until
	 * start of scanout.
	 */

	/* Inside "upper part" of vblank area? Apply corrective offset if so: */
	if (in_vbl && (*vpos >= vbl_start)) {
		vtotal = mode->crtc_vtotal;
		*vpos = *vpos - vtotal;
	}

	/* Correct for shifted end of vbl at vbl_end. */
	*vpos = *vpos - vbl_end;

	return ret;
}

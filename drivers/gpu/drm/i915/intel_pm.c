/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eugeni Dodonov <eugeni.dodonov@intel.com>
 *
 */

#include "i915_drv.h"
#include "intel_drv.h"

/* FBC, or Frame Buffer Compression, is a technique employed to compress the framebuffer contents in-memory, aiming at reducing the required bandwidth during in-memory transfers and, therefore, reduce the power packet.
 *
 * The benefits of FBC are mostly visible with solid backgrounds and variation-less patterns.
 *
 * FBC-related functionality can be enabled by the means of the i915.i915_enable_fbc parameter
 */

void i8xx_disable_fbc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 fbc_ctl;

	/* Disable compression */
	fbc_ctl = I915_READ(FBC_CONTROL);
	if ((fbc_ctl & FBC_CTL_EN) == 0)
		return;

	fbc_ctl &= ~FBC_CTL_EN;
	I915_WRITE(FBC_CONTROL, fbc_ctl);

	/* Wait for compressing bit to clear */
	if (wait_for((I915_READ(FBC_STATUS) & FBC_STAT_COMPRESSING) == 0, 10)) {
		DRM_DEBUG_KMS("FBC idle timed out\n");
		return;
	}

	DRM_DEBUG_KMS("disabled FBC\n");
}

void i8xx_enable_fbc(struct drm_crtc *crtc, unsigned long interval)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int cfb_pitch;
	int plane, i;
	u32 fbc_ctl, fbc_ctl2;

	cfb_pitch = dev_priv->cfb_size / FBC_LL_SIZE;
	if (fb->pitches[0] < cfb_pitch)
		cfb_pitch = fb->pitches[0];

	/* FBC_CTL wants 64B units */
	cfb_pitch = (cfb_pitch / 64) - 1;
	plane = intel_crtc->plane == 0 ? FBC_CTL_PLANEA : FBC_CTL_PLANEB;

	/* Clear old tags */
	for (i = 0; i < (FBC_LL_SIZE / 32) + 1; i++)
		I915_WRITE(FBC_TAG + (i * 4), 0);

	/* Set it up... */
	fbc_ctl2 = FBC_CTL_FENCE_DBL | FBC_CTL_IDLE_IMM | FBC_CTL_CPU_FENCE;
	fbc_ctl2 |= plane;
	I915_WRITE(FBC_CONTROL2, fbc_ctl2);
	I915_WRITE(FBC_FENCE_OFF, crtc->y);

	/* enable it... */
	fbc_ctl = FBC_CTL_EN | FBC_CTL_PERIODIC;
	if (IS_I945GM(dev))
		fbc_ctl |= FBC_CTL_C3_IDLE; /* 945 needs special SR handling */
	fbc_ctl |= (cfb_pitch & 0xff) << FBC_CTL_STRIDE_SHIFT;
	fbc_ctl |= (interval & 0x2fff) << FBC_CTL_INTERVAL_SHIFT;
	fbc_ctl |= obj->fence_reg;
	I915_WRITE(FBC_CONTROL, fbc_ctl);

	DRM_DEBUG_KMS("enabled FBC, pitch %d, yoff %d, plane %d, ",
		      cfb_pitch, crtc->y, intel_crtc->plane);
}

bool i8xx_fbc_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	return I915_READ(FBC_CONTROL) & FBC_CTL_EN;
}

void g4x_enable_fbc(struct drm_crtc *crtc, unsigned long interval)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int plane = intel_crtc->plane == 0 ? DPFC_CTL_PLANEA : DPFC_CTL_PLANEB;
	unsigned long stall_watermark = 200;
	u32 dpfc_ctl;

	dpfc_ctl = plane | DPFC_SR_EN | DPFC_CTL_LIMIT_1X;
	dpfc_ctl |= DPFC_CTL_FENCE_EN | obj->fence_reg;
	I915_WRITE(DPFC_CHICKEN, DPFC_HT_MODIFY);

	I915_WRITE(DPFC_RECOMP_CTL, DPFC_RECOMP_STALL_EN |
		   (stall_watermark << DPFC_RECOMP_STALL_WM_SHIFT) |
		   (interval << DPFC_RECOMP_TIMER_COUNT_SHIFT));
	I915_WRITE(DPFC_FENCE_YOFF, crtc->y);

	/* enable it... */
	I915_WRITE(DPFC_CONTROL, I915_READ(DPFC_CONTROL) | DPFC_CTL_EN);

	DRM_DEBUG_KMS("enabled fbc on plane %d\n", intel_crtc->plane);
}

void g4x_disable_fbc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpfc_ctl;

	/* Disable compression */
	dpfc_ctl = I915_READ(DPFC_CONTROL);
	if (dpfc_ctl & DPFC_CTL_EN) {
		dpfc_ctl &= ~DPFC_CTL_EN;
		I915_WRITE(DPFC_CONTROL, dpfc_ctl);

		DRM_DEBUG_KMS("disabled FBC\n");
	}
}

bool g4x_fbc_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	return I915_READ(DPFC_CONTROL) & DPFC_CTL_EN;
}

static void sandybridge_blit_fbc_update(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 blt_ecoskpd;

	/* Make sure blitter notifies FBC of writes */
	gen6_gt_force_wake_get(dev_priv);
	blt_ecoskpd = I915_READ(GEN6_BLITTER_ECOSKPD);
	blt_ecoskpd |= GEN6_BLITTER_FBC_NOTIFY <<
		GEN6_BLITTER_LOCK_SHIFT;
	I915_WRITE(GEN6_BLITTER_ECOSKPD, blt_ecoskpd);
	blt_ecoskpd |= GEN6_BLITTER_FBC_NOTIFY;
	I915_WRITE(GEN6_BLITTER_ECOSKPD, blt_ecoskpd);
	blt_ecoskpd &= ~(GEN6_BLITTER_FBC_NOTIFY <<
			 GEN6_BLITTER_LOCK_SHIFT);
	I915_WRITE(GEN6_BLITTER_ECOSKPD, blt_ecoskpd);
	POSTING_READ(GEN6_BLITTER_ECOSKPD);
	gen6_gt_force_wake_put(dev_priv);
}

void ironlake_enable_fbc(struct drm_crtc *crtc, unsigned long interval)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int plane = intel_crtc->plane == 0 ? DPFC_CTL_PLANEA : DPFC_CTL_PLANEB;
	unsigned long stall_watermark = 200;
	u32 dpfc_ctl;

	dpfc_ctl = I915_READ(ILK_DPFC_CONTROL);
	dpfc_ctl &= DPFC_RESERVED;
	dpfc_ctl |= (plane | DPFC_CTL_LIMIT_1X);
	/* Set persistent mode for front-buffer rendering, ala X. */
	dpfc_ctl |= DPFC_CTL_PERSISTENT_MODE;
	dpfc_ctl |= (DPFC_CTL_FENCE_EN | obj->fence_reg);
	I915_WRITE(ILK_DPFC_CHICKEN, DPFC_HT_MODIFY);

	I915_WRITE(ILK_DPFC_RECOMP_CTL, DPFC_RECOMP_STALL_EN |
		   (stall_watermark << DPFC_RECOMP_STALL_WM_SHIFT) |
		   (interval << DPFC_RECOMP_TIMER_COUNT_SHIFT));
	I915_WRITE(ILK_DPFC_FENCE_YOFF, crtc->y);
	I915_WRITE(ILK_FBC_RT_BASE, obj->gtt_offset | ILK_FBC_RT_VALID);
	/* enable it... */
	I915_WRITE(ILK_DPFC_CONTROL, dpfc_ctl | DPFC_CTL_EN);

	if (IS_GEN6(dev)) {
		I915_WRITE(SNB_DPFC_CTL_SA,
			   SNB_CPU_FENCE_ENABLE | obj->fence_reg);
		I915_WRITE(DPFC_CPU_FENCE_OFFSET, crtc->y);
		sandybridge_blit_fbc_update(dev);
	}

	DRM_DEBUG_KMS("enabled fbc on plane %d\n", intel_crtc->plane);
}

void ironlake_disable_fbc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpfc_ctl;

	/* Disable compression */
	dpfc_ctl = I915_READ(ILK_DPFC_CONTROL);
	if (dpfc_ctl & DPFC_CTL_EN) {
		dpfc_ctl &= ~DPFC_CTL_EN;
		I915_WRITE(ILK_DPFC_CONTROL, dpfc_ctl);

		DRM_DEBUG_KMS("disabled FBC\n");
	}
}

bool ironlake_fbc_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	return I915_READ(ILK_DPFC_CONTROL) & DPFC_CTL_EN;
}

bool intel_fbc_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!dev_priv->display.fbc_enabled)
		return false;

	return dev_priv->display.fbc_enabled(dev);
}

static void intel_fbc_work_fn(struct work_struct *__work)
{
	struct intel_fbc_work *work =
		container_of(to_delayed_work(__work),
			     struct intel_fbc_work, work);
	struct drm_device *dev = work->crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	mutex_lock(&dev->struct_mutex);
	if (work == dev_priv->fbc_work) {
		/* Double check that we haven't switched fb without cancelling
		 * the prior work.
		 */
		if (work->crtc->fb == work->fb) {
			dev_priv->display.enable_fbc(work->crtc,
						     work->interval);

			dev_priv->cfb_plane = to_intel_crtc(work->crtc)->plane;
			dev_priv->cfb_fb = work->crtc->fb->base.id;
			dev_priv->cfb_y = work->crtc->y;
		}

		dev_priv->fbc_work = NULL;
	}
	mutex_unlock(&dev->struct_mutex);

	kfree(work);
}

static void intel_cancel_fbc_work(struct drm_i915_private *dev_priv)
{
	if (dev_priv->fbc_work == NULL)
		return;

	DRM_DEBUG_KMS("cancelling pending FBC enable\n");

	/* Synchronisation is provided by struct_mutex and checking of
	 * dev_priv->fbc_work, so we can perform the cancellation
	 * entirely asynchronously.
	 */
	if (cancel_delayed_work(&dev_priv->fbc_work->work))
		/* tasklet was killed before being run, clean up */
		kfree(dev_priv->fbc_work);

	/* Mark the work as no longer wanted so that if it does
	 * wake-up (because the work was already running and waiting
	 * for our mutex), it will discover that is no longer
	 * necessary to run.
	 */
	dev_priv->fbc_work = NULL;
}

void intel_enable_fbc(struct drm_crtc *crtc, unsigned long interval)
{
	struct intel_fbc_work *work;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!dev_priv->display.enable_fbc)
		return;

	intel_cancel_fbc_work(dev_priv);

	work = kzalloc(sizeof *work, GFP_KERNEL);
	if (work == NULL) {
		dev_priv->display.enable_fbc(crtc, interval);
		return;
	}

	work->crtc = crtc;
	work->fb = crtc->fb;
	work->interval = interval;
	INIT_DELAYED_WORK(&work->work, intel_fbc_work_fn);

	dev_priv->fbc_work = work;

	DRM_DEBUG_KMS("scheduling delayed FBC enable\n");

	/* Delay the actual enabling to let pageflipping cease and the
	 * display to settle before starting the compression. Note that
	 * this delay also serves a second purpose: it allows for a
	 * vblank to pass after disabling the FBC before we attempt
	 * to modify the control registers.
	 *
	 * A more complicated solution would involve tracking vblanks
	 * following the termination of the page-flipping sequence
	 * and indeed performing the enable as a co-routine and not
	 * waiting synchronously upon the vblank.
	 */
	schedule_delayed_work(&work->work, msecs_to_jiffies(50));
}

void intel_disable_fbc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	intel_cancel_fbc_work(dev_priv);

	if (!dev_priv->display.disable_fbc)
		return;

	dev_priv->display.disable_fbc(dev);
	dev_priv->cfb_plane = -1;
}

/**
 * intel_update_fbc - enable/disable FBC as needed
 * @dev: the drm_device
 *
 * Set up the framebuffer compression hardware at mode set time.  We
 * enable it if possible:
 *   - plane A only (on pre-965)
 *   - no pixel mulitply/line duplication
 *   - no alpha buffer discard
 *   - no dual wide
 *   - framebuffer <= 2048 in width, 1536 in height
 *
 * We can't assume that any compression will take place (worst case),
 * so the compressed buffer has to be the same size as the uncompressed
 * one.  It also must reside (along with the line length buffer) in
 * stolen memory.
 *
 * We need to enable/disable FBC on a global basis.
 */
void intel_update_fbc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = NULL, *tmp_crtc;
	struct intel_crtc *intel_crtc;
	struct drm_framebuffer *fb;
	struct intel_framebuffer *intel_fb;
	struct drm_i915_gem_object *obj;
	int enable_fbc;

	DRM_DEBUG_KMS("\n");

	if (!i915_powersave)
		return;

	if (!I915_HAS_FBC(dev))
		return;

	/*
	 * If FBC is already on, we just have to verify that we can
	 * keep it that way...
	 * Need to disable if:
	 *   - more than one pipe is active
	 *   - changing FBC params (stride, fence, mode)
	 *   - new fb is too large to fit in compressed buffer
	 *   - going to an unsupported config (interlace, pixel multiply, etc.)
	 */
	list_for_each_entry(tmp_crtc, &dev->mode_config.crtc_list, head) {
		if (tmp_crtc->enabled && tmp_crtc->fb) {
			if (crtc) {
				DRM_DEBUG_KMS("more than one pipe active, disabling compression\n");
				dev_priv->no_fbc_reason = FBC_MULTIPLE_PIPES;
				goto out_disable;
			}
			crtc = tmp_crtc;
		}
	}

	if (!crtc || crtc->fb == NULL) {
		DRM_DEBUG_KMS("no output, disabling\n");
		dev_priv->no_fbc_reason = FBC_NO_OUTPUT;
		goto out_disable;
	}

	intel_crtc = to_intel_crtc(crtc);
	fb = crtc->fb;
	intel_fb = to_intel_framebuffer(fb);
	obj = intel_fb->obj;

	enable_fbc = i915_enable_fbc;
	if (enable_fbc < 0) {
		DRM_DEBUG_KMS("fbc set to per-chip default\n");
		enable_fbc = 1;
		if (INTEL_INFO(dev)->gen <= 6)
			enable_fbc = 0;
	}
	if (!enable_fbc) {
		DRM_DEBUG_KMS("fbc disabled per module param\n");
		dev_priv->no_fbc_reason = FBC_MODULE_PARAM;
		goto out_disable;
	}
	if (intel_fb->obj->base.size > dev_priv->cfb_size) {
		DRM_DEBUG_KMS("framebuffer too large, disabling "
			      "compression\n");
		dev_priv->no_fbc_reason = FBC_STOLEN_TOO_SMALL;
		goto out_disable;
	}
	if ((crtc->mode.flags & DRM_MODE_FLAG_INTERLACE) ||
	    (crtc->mode.flags & DRM_MODE_FLAG_DBLSCAN)) {
		DRM_DEBUG_KMS("mode incompatible with compression, "
			      "disabling\n");
		dev_priv->no_fbc_reason = FBC_UNSUPPORTED_MODE;
		goto out_disable;
	}
	if ((crtc->mode.hdisplay > 2048) ||
	    (crtc->mode.vdisplay > 1536)) {
		DRM_DEBUG_KMS("mode too large for compression, disabling\n");
		dev_priv->no_fbc_reason = FBC_MODE_TOO_LARGE;
		goto out_disable;
	}
	if ((IS_I915GM(dev) || IS_I945GM(dev)) && intel_crtc->plane != 0) {
		DRM_DEBUG_KMS("plane not 0, disabling compression\n");
		dev_priv->no_fbc_reason = FBC_BAD_PLANE;
		goto out_disable;
	}

	/* The use of a CPU fence is mandatory in order to detect writes
	 * by the CPU to the scanout and trigger updates to the FBC.
	 */
	if (obj->tiling_mode != I915_TILING_X ||
	    obj->fence_reg == I915_FENCE_REG_NONE) {
		DRM_DEBUG_KMS("framebuffer not tiled or fenced, disabling compression\n");
		dev_priv->no_fbc_reason = FBC_NOT_TILED;
		goto out_disable;
	}

	/* If the kernel debugger is active, always disable compression */
	if (in_dbg_master())
		goto out_disable;

	/* If the scanout has not changed, don't modify the FBC settings.
	 * Note that we make the fundamental assumption that the fb->obj
	 * cannot be unpinned (and have its GTT offset and fence revoked)
	 * without first being decoupled from the scanout and FBC disabled.
	 */
	if (dev_priv->cfb_plane == intel_crtc->plane &&
	    dev_priv->cfb_fb == fb->base.id &&
	    dev_priv->cfb_y == crtc->y)
		return;

	if (intel_fbc_enabled(dev)) {
		/* We update FBC along two paths, after changing fb/crtc
		 * configuration (modeswitching) and after page-flipping
		 * finishes. For the latter, we know that not only did
		 * we disable the FBC at the start of the page-flip
		 * sequence, but also more than one vblank has passed.
		 *
		 * For the former case of modeswitching, it is possible
		 * to switch between two FBC valid configurations
		 * instantaneously so we do need to disable the FBC
		 * before we can modify its control registers. We also
		 * have to wait for the next vblank for that to take
		 * effect. However, since we delay enabling FBC we can
		 * assume that a vblank has passed since disabling and
		 * that we can safely alter the registers in the deferred
		 * callback.
		 *
		 * In the scenario that we go from a valid to invalid
		 * and then back to valid FBC configuration we have
		 * no strict enforcement that a vblank occurred since
		 * disabling the FBC. However, along all current pipe
		 * disabling paths we do need to wait for a vblank at
		 * some point. And we wait before enabling FBC anyway.
		 */
		DRM_DEBUG_KMS("disabling active FBC for update\n");
		intel_disable_fbc(dev);
	}

	intel_enable_fbc(crtc, 500);
	return;

out_disable:
	/* Multiple disables should be harmless */
	if (intel_fbc_enabled(dev)) {
		DRM_DEBUG_KMS("unsupported config, disabling FBC\n");
		intel_disable_fbc(dev);
	}
}


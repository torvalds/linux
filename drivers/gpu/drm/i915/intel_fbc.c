/*
 * Copyright © 2014 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * DOC: Frame Buffer Compression (FBC)
 *
 * FBC tries to save memory bandwidth (and so power consumption) by
 * compressing the amount of memory used by the display. It is total
 * transparent to user space and completely handled in the kernel.
 *
 * The benefits of FBC are mostly visible with solid backgrounds and
 * variation-less patterns. It comes from keeping the memory footprint small
 * and having fewer memory pages opened and accessed for refreshing the display.
 *
 * i915 is responsible to reserve stolen memory for FBC and configure its
 * offset on proper registers. The hardware takes care of all
 * compress/decompress. However there are many known cases where we have to
 * forcibly disable it to allow proper screen updates.
 */

#include "intel_drv.h"
#include "i915_drv.h"

static void i8xx_fbc_disable(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 fbc_ctl;

	dev_priv->fbc.enabled = false;

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

static void i8xx_fbc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->primary->fb;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int cfb_pitch;
	int i;
	u32 fbc_ctl;

	dev_priv->fbc.enabled = true;

	cfb_pitch = dev_priv->fbc.size / FBC_LL_SIZE;
	if (fb->pitches[0] < cfb_pitch)
		cfb_pitch = fb->pitches[0];

	/* FBC_CTL wants 32B or 64B units */
	if (IS_GEN2(dev))
		cfb_pitch = (cfb_pitch / 32) - 1;
	else
		cfb_pitch = (cfb_pitch / 64) - 1;

	/* Clear old tags */
	for (i = 0; i < (FBC_LL_SIZE / 32) + 1; i++)
		I915_WRITE(FBC_TAG + (i * 4), 0);

	if (IS_GEN4(dev)) {
		u32 fbc_ctl2;

		/* Set it up... */
		fbc_ctl2 = FBC_CTL_FENCE_DBL | FBC_CTL_IDLE_IMM | FBC_CTL_CPU_FENCE;
		fbc_ctl2 |= FBC_CTL_PLANE(intel_crtc->plane);
		I915_WRITE(FBC_CONTROL2, fbc_ctl2);
		I915_WRITE(FBC_FENCE_OFF, crtc->y);
	}

	/* enable it... */
	fbc_ctl = I915_READ(FBC_CONTROL);
	fbc_ctl &= 0x3fff << FBC_CTL_INTERVAL_SHIFT;
	fbc_ctl |= FBC_CTL_EN | FBC_CTL_PERIODIC;
	if (IS_I945GM(dev))
		fbc_ctl |= FBC_CTL_C3_IDLE; /* 945 needs special SR handling */
	fbc_ctl |= (cfb_pitch & 0xff) << FBC_CTL_STRIDE_SHIFT;
	fbc_ctl |= obj->fence_reg;
	I915_WRITE(FBC_CONTROL, fbc_ctl);

	DRM_DEBUG_KMS("enabled FBC, pitch %d, yoff %d, plane %c\n",
		      cfb_pitch, crtc->y, plane_name(intel_crtc->plane));
}

static bool i8xx_fbc_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	return I915_READ(FBC_CONTROL) & FBC_CTL_EN;
}

static void g4x_fbc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->primary->fb;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 dpfc_ctl;

	dev_priv->fbc.enabled = true;

	dpfc_ctl = DPFC_CTL_PLANE(intel_crtc->plane) | DPFC_SR_EN;
	if (drm_format_plane_cpp(fb->pixel_format, 0) == 2)
		dpfc_ctl |= DPFC_CTL_LIMIT_2X;
	else
		dpfc_ctl |= DPFC_CTL_LIMIT_1X;
	dpfc_ctl |= DPFC_CTL_FENCE_EN | obj->fence_reg;

	I915_WRITE(DPFC_FENCE_YOFF, crtc->y);

	/* enable it... */
	I915_WRITE(DPFC_CONTROL, dpfc_ctl | DPFC_CTL_EN);

	DRM_DEBUG_KMS("enabled fbc on plane %c\n", plane_name(intel_crtc->plane));
}

static void g4x_fbc_disable(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpfc_ctl;

	dev_priv->fbc.enabled = false;

	/* Disable compression */
	dpfc_ctl = I915_READ(DPFC_CONTROL);
	if (dpfc_ctl & DPFC_CTL_EN) {
		dpfc_ctl &= ~DPFC_CTL_EN;
		I915_WRITE(DPFC_CONTROL, dpfc_ctl);

		DRM_DEBUG_KMS("disabled FBC\n");
	}
}

static bool g4x_fbc_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	return I915_READ(DPFC_CONTROL) & DPFC_CTL_EN;
}

static void snb_fbc_blit_update(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 blt_ecoskpd;

	/* Make sure blitter notifies FBC of writes */

	/* Blitter is part of Media powerwell on VLV. No impact of
	 * his param in other platforms for now */
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_MEDIA);

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

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_MEDIA);
}

static void ilk_fbc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->primary->fb;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 dpfc_ctl;

	dev_priv->fbc.enabled = true;

	dpfc_ctl = DPFC_CTL_PLANE(intel_crtc->plane);
	if (drm_format_plane_cpp(fb->pixel_format, 0) == 2)
		dev_priv->fbc.threshold++;

	switch (dev_priv->fbc.threshold) {
	case 4:
	case 3:
		dpfc_ctl |= DPFC_CTL_LIMIT_4X;
		break;
	case 2:
		dpfc_ctl |= DPFC_CTL_LIMIT_2X;
		break;
	case 1:
		dpfc_ctl |= DPFC_CTL_LIMIT_1X;
		break;
	}
	dpfc_ctl |= DPFC_CTL_FENCE_EN;
	if (IS_GEN5(dev))
		dpfc_ctl |= obj->fence_reg;

	I915_WRITE(ILK_DPFC_FENCE_YOFF, crtc->y);
	I915_WRITE(ILK_FBC_RT_BASE, i915_gem_obj_ggtt_offset(obj) | ILK_FBC_RT_VALID);
	/* enable it... */
	I915_WRITE(ILK_DPFC_CONTROL, dpfc_ctl | DPFC_CTL_EN);

	if (IS_GEN6(dev)) {
		I915_WRITE(SNB_DPFC_CTL_SA,
			   SNB_CPU_FENCE_ENABLE | obj->fence_reg);
		I915_WRITE(DPFC_CPU_FENCE_OFFSET, crtc->y);
		snb_fbc_blit_update(dev);
	}

	DRM_DEBUG_KMS("enabled fbc on plane %c\n", plane_name(intel_crtc->plane));
}

static void ilk_fbc_disable(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpfc_ctl;

	dev_priv->fbc.enabled = false;

	/* Disable compression */
	dpfc_ctl = I915_READ(ILK_DPFC_CONTROL);
	if (dpfc_ctl & DPFC_CTL_EN) {
		dpfc_ctl &= ~DPFC_CTL_EN;
		I915_WRITE(ILK_DPFC_CONTROL, dpfc_ctl);

		DRM_DEBUG_KMS("disabled FBC\n");
	}
}

static bool ilk_fbc_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	return I915_READ(ILK_DPFC_CONTROL) & DPFC_CTL_EN;
}

static void gen7_fbc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->primary->fb;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 dpfc_ctl;

	dev_priv->fbc.enabled = true;

	dpfc_ctl = IVB_DPFC_CTL_PLANE(intel_crtc->plane);
	if (drm_format_plane_cpp(fb->pixel_format, 0) == 2)
		dev_priv->fbc.threshold++;

	switch (dev_priv->fbc.threshold) {
	case 4:
	case 3:
		dpfc_ctl |= DPFC_CTL_LIMIT_4X;
		break;
	case 2:
		dpfc_ctl |= DPFC_CTL_LIMIT_2X;
		break;
	case 1:
		dpfc_ctl |= DPFC_CTL_LIMIT_1X;
		break;
	}

	dpfc_ctl |= IVB_DPFC_CTL_FENCE_EN;

	if (dev_priv->fbc.false_color)
		dpfc_ctl |= FBC_CTL_FALSE_COLOR;

	I915_WRITE(ILK_DPFC_CONTROL, dpfc_ctl | DPFC_CTL_EN);

	if (IS_IVYBRIDGE(dev)) {
		/* WaFbcAsynchFlipDisableFbcQueue:ivb */
		I915_WRITE(ILK_DISPLAY_CHICKEN1,
			   I915_READ(ILK_DISPLAY_CHICKEN1) |
			   ILK_FBCQ_DIS);
	} else {
		/* WaFbcAsynchFlipDisableFbcQueue:hsw,bdw */
		I915_WRITE(CHICKEN_PIPESL_1(intel_crtc->pipe),
			   I915_READ(CHICKEN_PIPESL_1(intel_crtc->pipe)) |
			   HSW_FBCQ_DIS);
	}

	I915_WRITE(SNB_DPFC_CTL_SA,
		   SNB_CPU_FENCE_ENABLE | obj->fence_reg);
	I915_WRITE(DPFC_CPU_FENCE_OFFSET, crtc->y);

	snb_fbc_blit_update(dev);

	DRM_DEBUG_KMS("enabled fbc on plane %c\n", plane_name(intel_crtc->plane));
}

/**
 * intel_fbc_enabled - Is FBC enabled?
 * @dev: the drm_device
 *
 * This function is used to verify the current state of FBC.
 * FIXME: This should be tracked in the plane config eventually
 *        instead of queried at runtime for most callers.
 */
bool intel_fbc_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	return dev_priv->fbc.enabled;
}

void bdw_fbc_sw_flush(struct drm_device *dev, u32 value)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!IS_GEN8(dev))
		return;

	if (!intel_fbc_enabled(dev))
		return;

	I915_WRITE(MSG_FBC_REND_STATE, value);
}

static void intel_fbc_work_fn(struct work_struct *__work)
{
	struct intel_fbc_work *work =
		container_of(to_delayed_work(__work),
			     struct intel_fbc_work, work);
	struct drm_device *dev = work->crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	mutex_lock(&dev->struct_mutex);
	if (work == dev_priv->fbc.fbc_work) {
		/* Double check that we haven't switched fb without cancelling
		 * the prior work.
		 */
		if (work->crtc->primary->fb == work->fb) {
			dev_priv->display.enable_fbc(work->crtc);

			dev_priv->fbc.plane = to_intel_crtc(work->crtc)->plane;
			dev_priv->fbc.fb_id = work->crtc->primary->fb->base.id;
			dev_priv->fbc.y = work->crtc->y;
		}

		dev_priv->fbc.fbc_work = NULL;
	}
	mutex_unlock(&dev->struct_mutex);

	kfree(work);
}

static void intel_fbc_cancel_work(struct drm_i915_private *dev_priv)
{
	if (dev_priv->fbc.fbc_work == NULL)
		return;

	DRM_DEBUG_KMS("cancelling pending FBC enable\n");

	/* Synchronisation is provided by struct_mutex and checking of
	 * dev_priv->fbc.fbc_work, so we can perform the cancellation
	 * entirely asynchronously.
	 */
	if (cancel_delayed_work(&dev_priv->fbc.fbc_work->work))
		/* tasklet was killed before being run, clean up */
		kfree(dev_priv->fbc.fbc_work);

	/* Mark the work as no longer wanted so that if it does
	 * wake-up (because the work was already running and waiting
	 * for our mutex), it will discover that is no longer
	 * necessary to run.
	 */
	dev_priv->fbc.fbc_work = NULL;
}

static void intel_fbc_enable(struct drm_crtc *crtc)
{
	struct intel_fbc_work *work;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!dev_priv->display.enable_fbc)
		return;

	intel_fbc_cancel_work(dev_priv);

	work = kzalloc(sizeof(*work), GFP_KERNEL);
	if (work == NULL) {
		DRM_ERROR("Failed to allocate FBC work structure\n");
		dev_priv->display.enable_fbc(crtc);
		return;
	}

	work->crtc = crtc;
	work->fb = crtc->primary->fb;
	INIT_DELAYED_WORK(&work->work, intel_fbc_work_fn);

	dev_priv->fbc.fbc_work = work;

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
	 *
	 * WaFbcWaitForVBlankBeforeEnable:ilk,snb
	 */
	schedule_delayed_work(&work->work, msecs_to_jiffies(50));
}

/**
 * intel_fbc_disable - disable FBC
 * @dev: the drm_device
 *
 * This function disables FBC.
 */
void intel_fbc_disable(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	intel_fbc_cancel_work(dev_priv);

	if (!dev_priv->display.disable_fbc)
		return;

	dev_priv->display.disable_fbc(dev);
	dev_priv->fbc.plane = -1;
}

static bool set_no_fbc_reason(struct drm_i915_private *dev_priv,
			      enum no_fbc_reason reason)
{
	if (dev_priv->fbc.no_fbc_reason == reason)
		return false;

	dev_priv->fbc.no_fbc_reason = reason;
	return true;
}

/**
 * intel_fbc_update - enable/disable FBC as needed
 * @dev: the drm_device
 *
 * Set up the framebuffer compression hardware at mode set time.  We
 * enable it if possible:
 *   - plane A only (on pre-965)
 *   - no pixel mulitply/line duplication
 *   - no alpha buffer discard
 *   - no dual wide
 *   - framebuffer <= max_hdisplay in width, max_vdisplay in height
 *
 * We can't assume that any compression will take place (worst case),
 * so the compressed buffer has to be the same size as the uncompressed
 * one.  It also must reside (along with the line length buffer) in
 * stolen memory.
 *
 * We need to enable/disable FBC on a global basis.
 */
void intel_fbc_update(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = NULL, *tmp_crtc;
	struct intel_crtc *intel_crtc;
	struct drm_framebuffer *fb;
	struct drm_i915_gem_object *obj;
	const struct drm_display_mode *adjusted_mode;
	unsigned int max_width, max_height;

	if (!HAS_FBC(dev)) {
		set_no_fbc_reason(dev_priv, FBC_UNSUPPORTED);
		return;
	}

	if (!i915.powersave) {
		if (set_no_fbc_reason(dev_priv, FBC_MODULE_PARAM))
			DRM_DEBUG_KMS("fbc disabled per module param\n");
		return;
	}

	/*
	 * If FBC is already on, we just have to verify that we can
	 * keep it that way...
	 * Need to disable if:
	 *   - more than one pipe is active
	 *   - changing FBC params (stride, fence, mode)
	 *   - new fb is too large to fit in compressed buffer
	 *   - going to an unsupported config (interlace, pixel multiply, etc.)
	 */
	for_each_crtc(dev, tmp_crtc) {
		if (intel_crtc_active(tmp_crtc) &&
		    to_intel_crtc(tmp_crtc)->primary_enabled) {
			if (crtc) {
				if (set_no_fbc_reason(dev_priv, FBC_MULTIPLE_PIPES))
					DRM_DEBUG_KMS("more than one pipe active, disabling compression\n");
				goto out_disable;
			}
			crtc = tmp_crtc;
		}
	}

	if (!crtc || crtc->primary->fb == NULL) {
		if (set_no_fbc_reason(dev_priv, FBC_NO_OUTPUT))
			DRM_DEBUG_KMS("no output, disabling\n");
		goto out_disable;
	}

	intel_crtc = to_intel_crtc(crtc);
	fb = crtc->primary->fb;
	obj = intel_fb_obj(fb);
	adjusted_mode = &intel_crtc->config->base.adjusted_mode;

	if (i915.enable_fbc < 0) {
		if (set_no_fbc_reason(dev_priv, FBC_CHIP_DEFAULT))
			DRM_DEBUG_KMS("disabled per chip default\n");
		goto out_disable;
	}
	if (!i915.enable_fbc) {
		if (set_no_fbc_reason(dev_priv, FBC_MODULE_PARAM))
			DRM_DEBUG_KMS("fbc disabled per module param\n");
		goto out_disable;
	}
	if ((adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) ||
	    (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)) {
		if (set_no_fbc_reason(dev_priv, FBC_UNSUPPORTED_MODE))
			DRM_DEBUG_KMS("mode incompatible with compression, "
				      "disabling\n");
		goto out_disable;
	}

	if (INTEL_INFO(dev)->gen >= 8 || IS_HASWELL(dev)) {
		max_width = 4096;
		max_height = 4096;
	} else if (IS_G4X(dev) || INTEL_INFO(dev)->gen >= 5) {
		max_width = 4096;
		max_height = 2048;
	} else {
		max_width = 2048;
		max_height = 1536;
	}
	if (intel_crtc->config->pipe_src_w > max_width ||
	    intel_crtc->config->pipe_src_h > max_height) {
		if (set_no_fbc_reason(dev_priv, FBC_MODE_TOO_LARGE))
			DRM_DEBUG_KMS("mode too large for compression, disabling\n");
		goto out_disable;
	}
	if ((INTEL_INFO(dev)->gen < 4 || HAS_DDI(dev)) &&
	    intel_crtc->plane != PLANE_A) {
		if (set_no_fbc_reason(dev_priv, FBC_BAD_PLANE))
			DRM_DEBUG_KMS("plane not A, disabling compression\n");
		goto out_disable;
	}

	/* The use of a CPU fence is mandatory in order to detect writes
	 * by the CPU to the scanout and trigger updates to the FBC.
	 */
	if (obj->tiling_mode != I915_TILING_X ||
	    obj->fence_reg == I915_FENCE_REG_NONE) {
		if (set_no_fbc_reason(dev_priv, FBC_NOT_TILED))
			DRM_DEBUG_KMS("framebuffer not tiled or fenced, disabling compression\n");
		goto out_disable;
	}
	if (INTEL_INFO(dev)->gen <= 4 && !IS_G4X(dev) &&
	    crtc->primary->state->rotation != BIT(DRM_ROTATE_0)) {
		if (set_no_fbc_reason(dev_priv, FBC_UNSUPPORTED_MODE))
			DRM_DEBUG_KMS("Rotation unsupported, disabling\n");
		goto out_disable;
	}

	/* If the kernel debugger is active, always disable compression */
	if (in_dbg_master())
		goto out_disable;

	if (i915_gem_stolen_setup_compression(dev, obj->base.size,
					      drm_format_plane_cpp(fb->pixel_format, 0))) {
		if (set_no_fbc_reason(dev_priv, FBC_STOLEN_TOO_SMALL))
			DRM_DEBUG_KMS("framebuffer too large, disabling compression\n");
		goto out_disable;
	}

	/* If the scanout has not changed, don't modify the FBC settings.
	 * Note that we make the fundamental assumption that the fb->obj
	 * cannot be unpinned (and have its GTT offset and fence revoked)
	 * without first being decoupled from the scanout and FBC disabled.
	 */
	if (dev_priv->fbc.plane == intel_crtc->plane &&
	    dev_priv->fbc.fb_id == fb->base.id &&
	    dev_priv->fbc.y == crtc->y)
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
		intel_fbc_disable(dev);
	}

	intel_fbc_enable(crtc);
	dev_priv->fbc.no_fbc_reason = FBC_OK;
	return;

out_disable:
	/* Multiple disables should be harmless */
	if (intel_fbc_enabled(dev)) {
		DRM_DEBUG_KMS("unsupported config, disabling FBC\n");
		intel_fbc_disable(dev);
	}
	i915_gem_stolen_cleanup_compression(dev);
}

/**
 * intel_fbc_init - Initialize FBC
 * @dev_priv: the i915 device
 *
 * This function might be called during PM init process.
 */
void intel_fbc_init(struct drm_i915_private *dev_priv)
{
	if (!HAS_FBC(dev_priv)) {
		dev_priv->fbc.enabled = false;
		return;
	}

	if (INTEL_INFO(dev_priv)->gen >= 7) {
		dev_priv->display.fbc_enabled = ilk_fbc_enabled;
		dev_priv->display.enable_fbc = gen7_fbc_enable;
		dev_priv->display.disable_fbc = ilk_fbc_disable;
	} else if (INTEL_INFO(dev_priv)->gen >= 5) {
		dev_priv->display.fbc_enabled = ilk_fbc_enabled;
		dev_priv->display.enable_fbc = ilk_fbc_enable;
		dev_priv->display.disable_fbc = ilk_fbc_disable;
	} else if (IS_GM45(dev_priv)) {
		dev_priv->display.fbc_enabled = g4x_fbc_enabled;
		dev_priv->display.enable_fbc = g4x_fbc_enable;
		dev_priv->display.disable_fbc = g4x_fbc_disable;
	} else {
		dev_priv->display.fbc_enabled = i8xx_fbc_enabled;
		dev_priv->display.enable_fbc = i8xx_fbc_enable;
		dev_priv->display.disable_fbc = i8xx_fbc_disable;

		/* This value was pulled out of someone's hat */
		I915_WRITE(FBC_CONTROL, 500 << FBC_CTL_INTERVAL_SHIFT);
	}

	dev_priv->fbc.enabled = dev_priv->display.fbc_enabled(dev_priv->dev);
}

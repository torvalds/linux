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

#include <linux/cpufreq.h>
#include "i915_drv.h"
#include "intel_drv.h"
#include "../../../platform/x86/intel_ips.h"
#include <linux/module.h>
#include <linux/vgaarb.h>
#include <drm/i915_powerwell.h>
#include <linux/pm_runtime.h>

/**
 * RC6 is a special power stage which allows the GPU to enter an very
 * low-voltage mode when idle, using down to 0V while at this stage.  This
 * stage is entered automatically when the GPU is idle when RC6 support is
 * enabled, and as soon as new workload arises GPU wakes up automatically as well.
 *
 * There are different RC6 modes available in Intel GPU, which differentiate
 * among each other with the latency required to enter and leave RC6 and
 * voltage consumed by the GPU in different states.
 *
 * The combination of the following flags define which states GPU is allowed
 * to enter, while RC6 is the normal RC6 state, RC6p is the deep RC6, and
 * RC6pp is deepest RC6. Their support by hardware varies according to the
 * GPU, BIOS, chipset and platform. RC6 is usually the safest one and the one
 * which brings the most power savings; deeper states save more power, but
 * require higher latency to switch to and wake up.
 */
#define INTEL_RC6_ENABLE			(1<<0)
#define INTEL_RC6p_ENABLE			(1<<1)
#define INTEL_RC6pp_ENABLE			(1<<2)

/* FBC, or Frame Buffer Compression, is a technique employed to compress the
 * framebuffer contents in-memory, aiming at reducing the required bandwidth
 * during in-memory transfers and, therefore, reduce the power packet.
 *
 * The benefits of FBC are mostly visible with solid backgrounds and
 * variation-less patterns.
 *
 * FBC-related functionality can be enabled by the means of the
 * i915.i915_enable_fbc parameter
 */

static void i8xx_disable_fbc(struct drm_device *dev)
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

static void i8xx_enable_fbc(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->primary->fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int cfb_pitch;
	int i;
	u32 fbc_ctl;

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

static void g4x_enable_fbc(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->primary->fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 dpfc_ctl;

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

static void g4x_disable_fbc(struct drm_device *dev)
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

static bool g4x_fbc_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	return I915_READ(DPFC_CONTROL) & DPFC_CTL_EN;
}

static void sandybridge_blit_fbc_update(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 blt_ecoskpd;

	/* Make sure blitter notifies FBC of writes */

	/* Blitter is part of Media powerwell on VLV. No impact of
	 * his param in other platforms for now */
	gen6_gt_force_wake_get(dev_priv, FORCEWAKE_MEDIA);

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

	gen6_gt_force_wake_put(dev_priv, FORCEWAKE_MEDIA);
}

static void ironlake_enable_fbc(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->primary->fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 dpfc_ctl;

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
		sandybridge_blit_fbc_update(dev);
	}

	DRM_DEBUG_KMS("enabled fbc on plane %c\n", plane_name(intel_crtc->plane));
}

static void ironlake_disable_fbc(struct drm_device *dev)
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

static bool ironlake_fbc_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	return I915_READ(ILK_DPFC_CONTROL) & DPFC_CTL_EN;
}

static void gen7_enable_fbc(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->primary->fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 dpfc_ctl;

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

	sandybridge_blit_fbc_update(dev);

	DRM_DEBUG_KMS("enabled fbc on plane %c\n", plane_name(intel_crtc->plane));
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

static void intel_cancel_fbc_work(struct drm_i915_private *dev_priv)
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

static void intel_enable_fbc(struct drm_crtc *crtc)
{
	struct intel_fbc_work *work;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!dev_priv->display.enable_fbc)
		return;

	intel_cancel_fbc_work(dev_priv);

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

void intel_disable_fbc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	intel_cancel_fbc_work(dev_priv);

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
 * intel_update_fbc - enable/disable FBC as needed
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
void intel_update_fbc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = NULL, *tmp_crtc;
	struct intel_crtc *intel_crtc;
	struct drm_framebuffer *fb;
	struct intel_framebuffer *intel_fb;
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
	intel_fb = to_intel_framebuffer(fb);
	obj = intel_fb->obj;
	adjusted_mode = &intel_crtc->config.adjusted_mode;

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
	if (intel_crtc->config.pipe_src_w > max_width ||
	    intel_crtc->config.pipe_src_h > max_height) {
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

	/* If the kernel debugger is active, always disable compression */
	if (in_dbg_master())
		goto out_disable;

	if (i915_gem_stolen_setup_compression(dev, intel_fb->obj->base.size,
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
		intel_disable_fbc(dev);
	}

	intel_enable_fbc(crtc);
	dev_priv->fbc.no_fbc_reason = FBC_OK;
	return;

out_disable:
	/* Multiple disables should be harmless */
	if (intel_fbc_enabled(dev)) {
		DRM_DEBUG_KMS("unsupported config, disabling FBC\n");
		intel_disable_fbc(dev);
	}
	i915_gem_stolen_cleanup_compression(dev);
}

static void i915_pineview_get_mem_freq(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 tmp;

	tmp = I915_READ(CLKCFG);

	switch (tmp & CLKCFG_FSB_MASK) {
	case CLKCFG_FSB_533:
		dev_priv->fsb_freq = 533; /* 133*4 */
		break;
	case CLKCFG_FSB_800:
		dev_priv->fsb_freq = 800; /* 200*4 */
		break;
	case CLKCFG_FSB_667:
		dev_priv->fsb_freq =  667; /* 167*4 */
		break;
	case CLKCFG_FSB_400:
		dev_priv->fsb_freq = 400; /* 100*4 */
		break;
	}

	switch (tmp & CLKCFG_MEM_MASK) {
	case CLKCFG_MEM_533:
		dev_priv->mem_freq = 533;
		break;
	case CLKCFG_MEM_667:
		dev_priv->mem_freq = 667;
		break;
	case CLKCFG_MEM_800:
		dev_priv->mem_freq = 800;
		break;
	}

	/* detect pineview DDR3 setting */
	tmp = I915_READ(CSHRDDR3CTL);
	dev_priv->is_ddr3 = (tmp & CSHRDDR3CTL_DDR3) ? 1 : 0;
}

static void i915_ironlake_get_mem_freq(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u16 ddrpll, csipll;

	ddrpll = I915_READ16(DDRMPLL1);
	csipll = I915_READ16(CSIPLL0);

	switch (ddrpll & 0xff) {
	case 0xc:
		dev_priv->mem_freq = 800;
		break;
	case 0x10:
		dev_priv->mem_freq = 1066;
		break;
	case 0x14:
		dev_priv->mem_freq = 1333;
		break;
	case 0x18:
		dev_priv->mem_freq = 1600;
		break;
	default:
		DRM_DEBUG_DRIVER("unknown memory frequency 0x%02x\n",
				 ddrpll & 0xff);
		dev_priv->mem_freq = 0;
		break;
	}

	dev_priv->ips.r_t = dev_priv->mem_freq;

	switch (csipll & 0x3ff) {
	case 0x00c:
		dev_priv->fsb_freq = 3200;
		break;
	case 0x00e:
		dev_priv->fsb_freq = 3733;
		break;
	case 0x010:
		dev_priv->fsb_freq = 4266;
		break;
	case 0x012:
		dev_priv->fsb_freq = 4800;
		break;
	case 0x014:
		dev_priv->fsb_freq = 5333;
		break;
	case 0x016:
		dev_priv->fsb_freq = 5866;
		break;
	case 0x018:
		dev_priv->fsb_freq = 6400;
		break;
	default:
		DRM_DEBUG_DRIVER("unknown fsb frequency 0x%04x\n",
				 csipll & 0x3ff);
		dev_priv->fsb_freq = 0;
		break;
	}

	if (dev_priv->fsb_freq == 3200) {
		dev_priv->ips.c_m = 0;
	} else if (dev_priv->fsb_freq > 3200 && dev_priv->fsb_freq <= 4800) {
		dev_priv->ips.c_m = 1;
	} else {
		dev_priv->ips.c_m = 2;
	}
}

static const struct cxsr_latency cxsr_latency_table[] = {
	{1, 0, 800, 400, 3382, 33382, 3983, 33983},    /* DDR2-400 SC */
	{1, 0, 800, 667, 3354, 33354, 3807, 33807},    /* DDR2-667 SC */
	{1, 0, 800, 800, 3347, 33347, 3763, 33763},    /* DDR2-800 SC */
	{1, 1, 800, 667, 6420, 36420, 6873, 36873},    /* DDR3-667 SC */
	{1, 1, 800, 800, 5902, 35902, 6318, 36318},    /* DDR3-800 SC */

	{1, 0, 667, 400, 3400, 33400, 4021, 34021},    /* DDR2-400 SC */
	{1, 0, 667, 667, 3372, 33372, 3845, 33845},    /* DDR2-667 SC */
	{1, 0, 667, 800, 3386, 33386, 3822, 33822},    /* DDR2-800 SC */
	{1, 1, 667, 667, 6438, 36438, 6911, 36911},    /* DDR3-667 SC */
	{1, 1, 667, 800, 5941, 35941, 6377, 36377},    /* DDR3-800 SC */

	{1, 0, 400, 400, 3472, 33472, 4173, 34173},    /* DDR2-400 SC */
	{1, 0, 400, 667, 3443, 33443, 3996, 33996},    /* DDR2-667 SC */
	{1, 0, 400, 800, 3430, 33430, 3946, 33946},    /* DDR2-800 SC */
	{1, 1, 400, 667, 6509, 36509, 7062, 37062},    /* DDR3-667 SC */
	{1, 1, 400, 800, 5985, 35985, 6501, 36501},    /* DDR3-800 SC */

	{0, 0, 800, 400, 3438, 33438, 4065, 34065},    /* DDR2-400 SC */
	{0, 0, 800, 667, 3410, 33410, 3889, 33889},    /* DDR2-667 SC */
	{0, 0, 800, 800, 3403, 33403, 3845, 33845},    /* DDR2-800 SC */
	{0, 1, 800, 667, 6476, 36476, 6955, 36955},    /* DDR3-667 SC */
	{0, 1, 800, 800, 5958, 35958, 6400, 36400},    /* DDR3-800 SC */

	{0, 0, 667, 400, 3456, 33456, 4103, 34106},    /* DDR2-400 SC */
	{0, 0, 667, 667, 3428, 33428, 3927, 33927},    /* DDR2-667 SC */
	{0, 0, 667, 800, 3443, 33443, 3905, 33905},    /* DDR2-800 SC */
	{0, 1, 667, 667, 6494, 36494, 6993, 36993},    /* DDR3-667 SC */
	{0, 1, 667, 800, 5998, 35998, 6460, 36460},    /* DDR3-800 SC */

	{0, 0, 400, 400, 3528, 33528, 4255, 34255},    /* DDR2-400 SC */
	{0, 0, 400, 667, 3500, 33500, 4079, 34079},    /* DDR2-667 SC */
	{0, 0, 400, 800, 3487, 33487, 4029, 34029},    /* DDR2-800 SC */
	{0, 1, 400, 667, 6566, 36566, 7145, 37145},    /* DDR3-667 SC */
	{0, 1, 400, 800, 6042, 36042, 6584, 36584},    /* DDR3-800 SC */
};

static const struct cxsr_latency *intel_get_cxsr_latency(int is_desktop,
							 int is_ddr3,
							 int fsb,
							 int mem)
{
	const struct cxsr_latency *latency;
	int i;

	if (fsb == 0 || mem == 0)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(cxsr_latency_table); i++) {
		latency = &cxsr_latency_table[i];
		if (is_desktop == latency->is_desktop &&
		    is_ddr3 == latency->is_ddr3 &&
		    fsb == latency->fsb_freq && mem == latency->mem_freq)
			return latency;
	}

	DRM_DEBUG_KMS("Unknown FSB/MEM found, disable CxSR\n");

	return NULL;
}

void intel_set_memory_cxsr(struct drm_i915_private *dev_priv, bool enable)
{
	struct drm_device *dev = dev_priv->dev;
	u32 val;

	if (IS_VALLEYVIEW(dev)) {
		I915_WRITE(FW_BLC_SELF_VLV, enable ? FW_CSPWRDWNEN : 0);
	} else if (IS_G4X(dev) || IS_CRESTLINE(dev)) {
		I915_WRITE(FW_BLC_SELF, enable ? FW_BLC_SELF_EN : 0);
	} else if (IS_PINEVIEW(dev)) {
		val = I915_READ(DSPFW3) & ~PINEVIEW_SELF_REFRESH_EN;
		val |= enable ? PINEVIEW_SELF_REFRESH_EN : 0;
		I915_WRITE(DSPFW3, val);
	} else if (IS_I945G(dev) || IS_I945GM(dev)) {
		val = enable ? _MASKED_BIT_ENABLE(FW_BLC_SELF_EN) :
			       _MASKED_BIT_DISABLE(FW_BLC_SELF_EN);
		I915_WRITE(FW_BLC_SELF, val);
	} else if (IS_I915GM(dev)) {
		val = enable ? _MASKED_BIT_ENABLE(INSTPM_SELF_EN) :
			       _MASKED_BIT_DISABLE(INSTPM_SELF_EN);
		I915_WRITE(INSTPM, val);
	} else {
		return;
	}

	DRM_DEBUG_KMS("memory self-refresh is %s\n",
		      enable ? "enabled" : "disabled");
}

/*
 * Latency for FIFO fetches is dependent on several factors:
 *   - memory configuration (speed, channels)
 *   - chipset
 *   - current MCH state
 * It can be fairly high in some situations, so here we assume a fairly
 * pessimal value.  It's a tradeoff between extra memory fetches (if we
 * set this value too high, the FIFO will fetch frequently to stay full)
 * and power consumption (set it too low to save power and we might see
 * FIFO underruns and display "flicker").
 *
 * A value of 5us seems to be a good balance; safe for very low end
 * platforms but not overly aggressive on lower latency configs.
 */
static const int latency_ns = 5000;

static int i9xx_get_fifo_size(struct drm_device *dev, int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dsparb = I915_READ(DSPARB);
	int size;

	size = dsparb & 0x7f;
	if (plane)
		size = ((dsparb >> DSPARB_CSTART_SHIFT) & 0x7f) - size;

	DRM_DEBUG_KMS("FIFO size - (0x%08x) %s: %d\n", dsparb,
		      plane ? "B" : "A", size);

	return size;
}

static int i830_get_fifo_size(struct drm_device *dev, int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dsparb = I915_READ(DSPARB);
	int size;

	size = dsparb & 0x1ff;
	if (plane)
		size = ((dsparb >> DSPARB_BEND_SHIFT) & 0x1ff) - size;
	size >>= 1; /* Convert to cachelines */

	DRM_DEBUG_KMS("FIFO size - (0x%08x) %s: %d\n", dsparb,
		      plane ? "B" : "A", size);

	return size;
}

static int i845_get_fifo_size(struct drm_device *dev, int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dsparb = I915_READ(DSPARB);
	int size;

	size = dsparb & 0x7f;
	size >>= 2; /* Convert to cachelines */

	DRM_DEBUG_KMS("FIFO size - (0x%08x) %s: %d\n", dsparb,
		      plane ? "B" : "A",
		      size);

	return size;
}

/* Pineview has different values for various configs */
static const struct intel_watermark_params pineview_display_wm = {
	.fifo_size = PINEVIEW_DISPLAY_FIFO,
	.max_wm = PINEVIEW_MAX_WM,
	.default_wm = PINEVIEW_DFT_WM,
	.guard_size = PINEVIEW_GUARD_WM,
	.cacheline_size = PINEVIEW_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params pineview_display_hplloff_wm = {
	.fifo_size = PINEVIEW_DISPLAY_FIFO,
	.max_wm = PINEVIEW_MAX_WM,
	.default_wm = PINEVIEW_DFT_HPLLOFF_WM,
	.guard_size = PINEVIEW_GUARD_WM,
	.cacheline_size = PINEVIEW_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params pineview_cursor_wm = {
	.fifo_size = PINEVIEW_CURSOR_FIFO,
	.max_wm = PINEVIEW_CURSOR_MAX_WM,
	.default_wm = PINEVIEW_CURSOR_DFT_WM,
	.guard_size = PINEVIEW_CURSOR_GUARD_WM,
	.cacheline_size = PINEVIEW_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params pineview_cursor_hplloff_wm = {
	.fifo_size = PINEVIEW_CURSOR_FIFO,
	.max_wm = PINEVIEW_CURSOR_MAX_WM,
	.default_wm = PINEVIEW_CURSOR_DFT_WM,
	.guard_size = PINEVIEW_CURSOR_GUARD_WM,
	.cacheline_size = PINEVIEW_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params g4x_wm_info = {
	.fifo_size = G4X_FIFO_SIZE,
	.max_wm = G4X_MAX_WM,
	.default_wm = G4X_MAX_WM,
	.guard_size = 2,
	.cacheline_size = G4X_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params g4x_cursor_wm_info = {
	.fifo_size = I965_CURSOR_FIFO,
	.max_wm = I965_CURSOR_MAX_WM,
	.default_wm = I965_CURSOR_DFT_WM,
	.guard_size = 2,
	.cacheline_size = G4X_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params valleyview_wm_info = {
	.fifo_size = VALLEYVIEW_FIFO_SIZE,
	.max_wm = VALLEYVIEW_MAX_WM,
	.default_wm = VALLEYVIEW_MAX_WM,
	.guard_size = 2,
	.cacheline_size = G4X_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params valleyview_cursor_wm_info = {
	.fifo_size = I965_CURSOR_FIFO,
	.max_wm = VALLEYVIEW_CURSOR_MAX_WM,
	.default_wm = I965_CURSOR_DFT_WM,
	.guard_size = 2,
	.cacheline_size = G4X_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params i965_cursor_wm_info = {
	.fifo_size = I965_CURSOR_FIFO,
	.max_wm = I965_CURSOR_MAX_WM,
	.default_wm = I965_CURSOR_DFT_WM,
	.guard_size = 2,
	.cacheline_size = I915_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params i945_wm_info = {
	.fifo_size = I945_FIFO_SIZE,
	.max_wm = I915_MAX_WM,
	.default_wm = 1,
	.guard_size = 2,
	.cacheline_size = I915_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params i915_wm_info = {
	.fifo_size = I915_FIFO_SIZE,
	.max_wm = I915_MAX_WM,
	.default_wm = 1,
	.guard_size = 2,
	.cacheline_size = I915_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params i830_wm_info = {
	.fifo_size = I855GM_FIFO_SIZE,
	.max_wm = I915_MAX_WM,
	.default_wm = 1,
	.guard_size = 2,
	.cacheline_size = I830_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params i845_wm_info = {
	.fifo_size = I830_FIFO_SIZE,
	.max_wm = I915_MAX_WM,
	.default_wm = 1,
	.guard_size = 2,
	.cacheline_size = I830_FIFO_LINE_SIZE,
};

/**
 * intel_calculate_wm - calculate watermark level
 * @clock_in_khz: pixel clock
 * @wm: chip FIFO params
 * @pixel_size: display pixel size
 * @latency_ns: memory latency for the platform
 *
 * Calculate the watermark level (the level at which the display plane will
 * start fetching from memory again).  Each chip has a different display
 * FIFO size and allocation, so the caller needs to figure that out and pass
 * in the correct intel_watermark_params structure.
 *
 * As the pixel clock runs, the FIFO will be drained at a rate that depends
 * on the pixel size.  When it reaches the watermark level, it'll start
 * fetching FIFO line sized based chunks from memory until the FIFO fills
 * past the watermark point.  If the FIFO drains completely, a FIFO underrun
 * will occur, and a display engine hang could result.
 */
static unsigned long intel_calculate_wm(unsigned long clock_in_khz,
					const struct intel_watermark_params *wm,
					int fifo_size,
					int pixel_size,
					unsigned long latency_ns)
{
	long entries_required, wm_size;

	/*
	 * Note: we need to make sure we don't overflow for various clock &
	 * latency values.
	 * clocks go from a few thousand to several hundred thousand.
	 * latency is usually a few thousand
	 */
	entries_required = ((clock_in_khz / 1000) * pixel_size * latency_ns) /
		1000;
	entries_required = DIV_ROUND_UP(entries_required, wm->cacheline_size);

	DRM_DEBUG_KMS("FIFO entries required for mode: %ld\n", entries_required);

	wm_size = fifo_size - (entries_required + wm->guard_size);

	DRM_DEBUG_KMS("FIFO watermark level: %ld\n", wm_size);

	/* Don't promote wm_size to unsigned... */
	if (wm_size > (long)wm->max_wm)
		wm_size = wm->max_wm;
	if (wm_size <= 0)
		wm_size = wm->default_wm;
	return wm_size;
}

static struct drm_crtc *single_enabled_crtc(struct drm_device *dev)
{
	struct drm_crtc *crtc, *enabled = NULL;

	for_each_crtc(dev, crtc) {
		if (intel_crtc_active(crtc)) {
			if (enabled)
				return NULL;
			enabled = crtc;
		}
	}

	return enabled;
}

static void pineview_update_wm(struct drm_crtc *unused_crtc)
{
	struct drm_device *dev = unused_crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	const struct cxsr_latency *latency;
	u32 reg;
	unsigned long wm;

	latency = intel_get_cxsr_latency(IS_PINEVIEW_G(dev), dev_priv->is_ddr3,
					 dev_priv->fsb_freq, dev_priv->mem_freq);
	if (!latency) {
		DRM_DEBUG_KMS("Unknown FSB/MEM found, disable CxSR\n");
		intel_set_memory_cxsr(dev_priv, false);
		return;
	}

	crtc = single_enabled_crtc(dev);
	if (crtc) {
		const struct drm_display_mode *adjusted_mode;
		int pixel_size = crtc->primary->fb->bits_per_pixel / 8;
		int clock;

		adjusted_mode = &to_intel_crtc(crtc)->config.adjusted_mode;
		clock = adjusted_mode->crtc_clock;

		/* Display SR */
		wm = intel_calculate_wm(clock, &pineview_display_wm,
					pineview_display_wm.fifo_size,
					pixel_size, latency->display_sr);
		reg = I915_READ(DSPFW1);
		reg &= ~DSPFW_SR_MASK;
		reg |= wm << DSPFW_SR_SHIFT;
		I915_WRITE(DSPFW1, reg);
		DRM_DEBUG_KMS("DSPFW1 register is %x\n", reg);

		/* cursor SR */
		wm = intel_calculate_wm(clock, &pineview_cursor_wm,
					pineview_display_wm.fifo_size,
					pixel_size, latency->cursor_sr);
		reg = I915_READ(DSPFW3);
		reg &= ~DSPFW_CURSOR_SR_MASK;
		reg |= (wm & 0x3f) << DSPFW_CURSOR_SR_SHIFT;
		I915_WRITE(DSPFW3, reg);

		/* Display HPLL off SR */
		wm = intel_calculate_wm(clock, &pineview_display_hplloff_wm,
					pineview_display_hplloff_wm.fifo_size,
					pixel_size, latency->display_hpll_disable);
		reg = I915_READ(DSPFW3);
		reg &= ~DSPFW_HPLL_SR_MASK;
		reg |= wm & DSPFW_HPLL_SR_MASK;
		I915_WRITE(DSPFW3, reg);

		/* cursor HPLL off SR */
		wm = intel_calculate_wm(clock, &pineview_cursor_hplloff_wm,
					pineview_display_hplloff_wm.fifo_size,
					pixel_size, latency->cursor_hpll_disable);
		reg = I915_READ(DSPFW3);
		reg &= ~DSPFW_HPLL_CURSOR_MASK;
		reg |= (wm & 0x3f) << DSPFW_HPLL_CURSOR_SHIFT;
		I915_WRITE(DSPFW3, reg);
		DRM_DEBUG_KMS("DSPFW3 register is %x\n", reg);

		intel_set_memory_cxsr(dev_priv, true);
	} else {
		intel_set_memory_cxsr(dev_priv, false);
	}
}

static bool g4x_compute_wm0(struct drm_device *dev,
			    int plane,
			    const struct intel_watermark_params *display,
			    int display_latency_ns,
			    const struct intel_watermark_params *cursor,
			    int cursor_latency_ns,
			    int *plane_wm,
			    int *cursor_wm)
{
	struct drm_crtc *crtc;
	const struct drm_display_mode *adjusted_mode;
	int htotal, hdisplay, clock, pixel_size;
	int line_time_us, line_count;
	int entries, tlb_miss;

	crtc = intel_get_crtc_for_plane(dev, plane);
	if (!intel_crtc_active(crtc)) {
		*cursor_wm = cursor->guard_size;
		*plane_wm = display->guard_size;
		return false;
	}

	adjusted_mode = &to_intel_crtc(crtc)->config.adjusted_mode;
	clock = adjusted_mode->crtc_clock;
	htotal = adjusted_mode->crtc_htotal;
	hdisplay = to_intel_crtc(crtc)->config.pipe_src_w;
	pixel_size = crtc->primary->fb->bits_per_pixel / 8;

	/* Use the small buffer method to calculate plane watermark */
	entries = ((clock * pixel_size / 1000) * display_latency_ns) / 1000;
	tlb_miss = display->fifo_size*display->cacheline_size - hdisplay * 8;
	if (tlb_miss > 0)
		entries += tlb_miss;
	entries = DIV_ROUND_UP(entries, display->cacheline_size);
	*plane_wm = entries + display->guard_size;
	if (*plane_wm > (int)display->max_wm)
		*plane_wm = display->max_wm;

	/* Use the large buffer method to calculate cursor watermark */
	line_time_us = max(htotal * 1000 / clock, 1);
	line_count = (cursor_latency_ns / line_time_us + 1000) / 1000;
	entries = line_count * to_intel_crtc(crtc)->cursor_width * pixel_size;
	tlb_miss = cursor->fifo_size*cursor->cacheline_size - hdisplay * 8;
	if (tlb_miss > 0)
		entries += tlb_miss;
	entries = DIV_ROUND_UP(entries, cursor->cacheline_size);
	*cursor_wm = entries + cursor->guard_size;
	if (*cursor_wm > (int)cursor->max_wm)
		*cursor_wm = (int)cursor->max_wm;

	return true;
}

/*
 * Check the wm result.
 *
 * If any calculated watermark values is larger than the maximum value that
 * can be programmed into the associated watermark register, that watermark
 * must be disabled.
 */
static bool g4x_check_srwm(struct drm_device *dev,
			   int display_wm, int cursor_wm,
			   const struct intel_watermark_params *display,
			   const struct intel_watermark_params *cursor)
{
	DRM_DEBUG_KMS("SR watermark: display plane %d, cursor %d\n",
		      display_wm, cursor_wm);

	if (display_wm > display->max_wm) {
		DRM_DEBUG_KMS("display watermark is too large(%d/%ld), disabling\n",
			      display_wm, display->max_wm);
		return false;
	}

	if (cursor_wm > cursor->max_wm) {
		DRM_DEBUG_KMS("cursor watermark is too large(%d/%ld), disabling\n",
			      cursor_wm, cursor->max_wm);
		return false;
	}

	if (!(display_wm || cursor_wm)) {
		DRM_DEBUG_KMS("SR latency is 0, disabling\n");
		return false;
	}

	return true;
}

static bool g4x_compute_srwm(struct drm_device *dev,
			     int plane,
			     int latency_ns,
			     const struct intel_watermark_params *display,
			     const struct intel_watermark_params *cursor,
			     int *display_wm, int *cursor_wm)
{
	struct drm_crtc *crtc;
	const struct drm_display_mode *adjusted_mode;
	int hdisplay, htotal, pixel_size, clock;
	unsigned long line_time_us;
	int line_count, line_size;
	int small, large;
	int entries;

	if (!latency_ns) {
		*display_wm = *cursor_wm = 0;
		return false;
	}

	crtc = intel_get_crtc_for_plane(dev, plane);
	adjusted_mode = &to_intel_crtc(crtc)->config.adjusted_mode;
	clock = adjusted_mode->crtc_clock;
	htotal = adjusted_mode->crtc_htotal;
	hdisplay = to_intel_crtc(crtc)->config.pipe_src_w;
	pixel_size = crtc->primary->fb->bits_per_pixel / 8;

	line_time_us = max(htotal * 1000 / clock, 1);
	line_count = (latency_ns / line_time_us + 1000) / 1000;
	line_size = hdisplay * pixel_size;

	/* Use the minimum of the small and large buffer method for primary */
	small = ((clock * pixel_size / 1000) * latency_ns) / 1000;
	large = line_count * line_size;

	entries = DIV_ROUND_UP(min(small, large), display->cacheline_size);
	*display_wm = entries + display->guard_size;

	/* calculate the self-refresh watermark for display cursor */
	entries = line_count * pixel_size * to_intel_crtc(crtc)->cursor_width;
	entries = DIV_ROUND_UP(entries, cursor->cacheline_size);
	*cursor_wm = entries + cursor->guard_size;

	return g4x_check_srwm(dev,
			      *display_wm, *cursor_wm,
			      display, cursor);
}

static bool vlv_compute_drain_latency(struct drm_device *dev,
				     int plane,
				     int *plane_prec_mult,
				     int *plane_dl,
				     int *cursor_prec_mult,
				     int *cursor_dl)
{
	struct drm_crtc *crtc;
	int clock, pixel_size;
	int entries;

	crtc = intel_get_crtc_for_plane(dev, plane);
	if (!intel_crtc_active(crtc))
		return false;

	clock = to_intel_crtc(crtc)->config.adjusted_mode.crtc_clock;
	pixel_size = crtc->primary->fb->bits_per_pixel / 8;	/* BPP */

	entries = (clock / 1000) * pixel_size;
	*plane_prec_mult = (entries > 256) ?
		DRAIN_LATENCY_PRECISION_32 : DRAIN_LATENCY_PRECISION_16;
	*plane_dl = (64 * (*plane_prec_mult) * 4) / ((clock / 1000) *
						     pixel_size);

	entries = (clock / 1000) * 4;	/* BPP is always 4 for cursor */
	*cursor_prec_mult = (entries > 256) ?
		DRAIN_LATENCY_PRECISION_32 : DRAIN_LATENCY_PRECISION_16;
	*cursor_dl = (64 * (*cursor_prec_mult) * 4) / ((clock / 1000) * 4);

	return true;
}

/*
 * Update drain latency registers of memory arbiter
 *
 * Valleyview SoC has a new memory arbiter and needs drain latency registers
 * to be programmed. Each plane has a drain latency multiplier and a drain
 * latency value.
 */

static void vlv_update_drain_latency(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int planea_prec, planea_dl, planeb_prec, planeb_dl;
	int cursora_prec, cursora_dl, cursorb_prec, cursorb_dl;
	int plane_prec_mult, cursor_prec_mult; /* Precision multiplier is
							either 16 or 32 */

	/* For plane A, Cursor A */
	if (vlv_compute_drain_latency(dev, 0, &plane_prec_mult, &planea_dl,
				      &cursor_prec_mult, &cursora_dl)) {
		cursora_prec = (cursor_prec_mult == DRAIN_LATENCY_PRECISION_32) ?
			DDL_CURSORA_PRECISION_32 : DDL_CURSORA_PRECISION_16;
		planea_prec = (plane_prec_mult == DRAIN_LATENCY_PRECISION_32) ?
			DDL_PLANEA_PRECISION_32 : DDL_PLANEA_PRECISION_16;

		I915_WRITE(VLV_DDL1, cursora_prec |
				(cursora_dl << DDL_CURSORA_SHIFT) |
				planea_prec | planea_dl);
	}

	/* For plane B, Cursor B */
	if (vlv_compute_drain_latency(dev, 1, &plane_prec_mult, &planeb_dl,
				      &cursor_prec_mult, &cursorb_dl)) {
		cursorb_prec = (cursor_prec_mult == DRAIN_LATENCY_PRECISION_32) ?
			DDL_CURSORB_PRECISION_32 : DDL_CURSORB_PRECISION_16;
		planeb_prec = (plane_prec_mult == DRAIN_LATENCY_PRECISION_32) ?
			DDL_PLANEB_PRECISION_32 : DDL_PLANEB_PRECISION_16;

		I915_WRITE(VLV_DDL2, cursorb_prec |
				(cursorb_dl << DDL_CURSORB_SHIFT) |
				planeb_prec | planeb_dl);
	}
}

#define single_plane_enabled(mask) is_power_of_2(mask)

static void valleyview_update_wm(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	static const int sr_latency_ns = 12000;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int planea_wm, planeb_wm, cursora_wm, cursorb_wm;
	int plane_sr, cursor_sr;
	int ignore_plane_sr, ignore_cursor_sr;
	unsigned int enabled = 0;

	vlv_update_drain_latency(dev);

	if (g4x_compute_wm0(dev, PIPE_A,
			    &valleyview_wm_info, latency_ns,
			    &valleyview_cursor_wm_info, latency_ns,
			    &planea_wm, &cursora_wm))
		enabled |= 1 << PIPE_A;

	if (g4x_compute_wm0(dev, PIPE_B,
			    &valleyview_wm_info, latency_ns,
			    &valleyview_cursor_wm_info, latency_ns,
			    &planeb_wm, &cursorb_wm))
		enabled |= 1 << PIPE_B;

	if (single_plane_enabled(enabled) &&
	    g4x_compute_srwm(dev, ffs(enabled) - 1,
			     sr_latency_ns,
			     &valleyview_wm_info,
			     &valleyview_cursor_wm_info,
			     &plane_sr, &ignore_cursor_sr) &&
	    g4x_compute_srwm(dev, ffs(enabled) - 1,
			     2*sr_latency_ns,
			     &valleyview_wm_info,
			     &valleyview_cursor_wm_info,
			     &ignore_plane_sr, &cursor_sr)) {
		intel_set_memory_cxsr(dev_priv, true);
	} else {
		intel_set_memory_cxsr(dev_priv, false);
		plane_sr = cursor_sr = 0;
	}

	DRM_DEBUG_KMS("Setting FIFO watermarks - A: plane=%d, cursor=%d, B: plane=%d, cursor=%d, SR: plane=%d, cursor=%d\n",
		      planea_wm, cursora_wm,
		      planeb_wm, cursorb_wm,
		      plane_sr, cursor_sr);

	I915_WRITE(DSPFW1,
		   (plane_sr << DSPFW_SR_SHIFT) |
		   (cursorb_wm << DSPFW_CURSORB_SHIFT) |
		   (planeb_wm << DSPFW_PLANEB_SHIFT) |
		   planea_wm);
	I915_WRITE(DSPFW2,
		   (I915_READ(DSPFW2) & ~DSPFW_CURSORA_MASK) |
		   (cursora_wm << DSPFW_CURSORA_SHIFT));
	I915_WRITE(DSPFW3,
		   (I915_READ(DSPFW3) & ~DSPFW_CURSOR_SR_MASK) |
		   (cursor_sr << DSPFW_CURSOR_SR_SHIFT));
}

static void g4x_update_wm(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	static const int sr_latency_ns = 12000;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int planea_wm, planeb_wm, cursora_wm, cursorb_wm;
	int plane_sr, cursor_sr;
	unsigned int enabled = 0;

	if (g4x_compute_wm0(dev, PIPE_A,
			    &g4x_wm_info, latency_ns,
			    &g4x_cursor_wm_info, latency_ns,
			    &planea_wm, &cursora_wm))
		enabled |= 1 << PIPE_A;

	if (g4x_compute_wm0(dev, PIPE_B,
			    &g4x_wm_info, latency_ns,
			    &g4x_cursor_wm_info, latency_ns,
			    &planeb_wm, &cursorb_wm))
		enabled |= 1 << PIPE_B;

	if (single_plane_enabled(enabled) &&
	    g4x_compute_srwm(dev, ffs(enabled) - 1,
			     sr_latency_ns,
			     &g4x_wm_info,
			     &g4x_cursor_wm_info,
			     &plane_sr, &cursor_sr)) {
		intel_set_memory_cxsr(dev_priv, true);
	} else {
		intel_set_memory_cxsr(dev_priv, false);
		plane_sr = cursor_sr = 0;
	}

	DRM_DEBUG_KMS("Setting FIFO watermarks - A: plane=%d, cursor=%d, B: plane=%d, cursor=%d, SR: plane=%d, cursor=%d\n",
		      planea_wm, cursora_wm,
		      planeb_wm, cursorb_wm,
		      plane_sr, cursor_sr);

	I915_WRITE(DSPFW1,
		   (plane_sr << DSPFW_SR_SHIFT) |
		   (cursorb_wm << DSPFW_CURSORB_SHIFT) |
		   (planeb_wm << DSPFW_PLANEB_SHIFT) |
		   planea_wm);
	I915_WRITE(DSPFW2,
		   (I915_READ(DSPFW2) & ~DSPFW_CURSORA_MASK) |
		   (cursora_wm << DSPFW_CURSORA_SHIFT));
	/* HPLL off in SR has some issues on G4x... disable it */
	I915_WRITE(DSPFW3,
		   (I915_READ(DSPFW3) & ~(DSPFW_HPLL_SR_EN | DSPFW_CURSOR_SR_MASK)) |
		   (cursor_sr << DSPFW_CURSOR_SR_SHIFT));
}

static void i965_update_wm(struct drm_crtc *unused_crtc)
{
	struct drm_device *dev = unused_crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	int srwm = 1;
	int cursor_sr = 16;

	/* Calc sr entries for one plane configs */
	crtc = single_enabled_crtc(dev);
	if (crtc) {
		/* self-refresh has much higher latency */
		static const int sr_latency_ns = 12000;
		const struct drm_display_mode *adjusted_mode =
			&to_intel_crtc(crtc)->config.adjusted_mode;
		int clock = adjusted_mode->crtc_clock;
		int htotal = adjusted_mode->crtc_htotal;
		int hdisplay = to_intel_crtc(crtc)->config.pipe_src_w;
		int pixel_size = crtc->primary->fb->bits_per_pixel / 8;
		unsigned long line_time_us;
		int entries;

		line_time_us = max(htotal * 1000 / clock, 1);

		/* Use ns/us then divide to preserve precision */
		entries = (((sr_latency_ns / line_time_us) + 1000) / 1000) *
			pixel_size * hdisplay;
		entries = DIV_ROUND_UP(entries, I915_FIFO_LINE_SIZE);
		srwm = I965_FIFO_SIZE - entries;
		if (srwm < 0)
			srwm = 1;
		srwm &= 0x1ff;
		DRM_DEBUG_KMS("self-refresh entries: %d, wm: %d\n",
			      entries, srwm);

		entries = (((sr_latency_ns / line_time_us) + 1000) / 1000) *
			pixel_size * to_intel_crtc(crtc)->cursor_width;
		entries = DIV_ROUND_UP(entries,
					  i965_cursor_wm_info.cacheline_size);
		cursor_sr = i965_cursor_wm_info.fifo_size -
			(entries + i965_cursor_wm_info.guard_size);

		if (cursor_sr > i965_cursor_wm_info.max_wm)
			cursor_sr = i965_cursor_wm_info.max_wm;

		DRM_DEBUG_KMS("self-refresh watermark: display plane %d "
			      "cursor %d\n", srwm, cursor_sr);

		intel_set_memory_cxsr(dev_priv, true);
	} else {
		/* Turn off self refresh if both pipes are enabled */
		intel_set_memory_cxsr(dev_priv, false);
	}

	DRM_DEBUG_KMS("Setting FIFO watermarks - A: 8, B: 8, C: 8, SR %d\n",
		      srwm);

	/* 965 has limitations... */
	I915_WRITE(DSPFW1, (srwm << DSPFW_SR_SHIFT) |
		   (8 << 16) | (8 << 8) | (8 << 0));
	I915_WRITE(DSPFW2, (8 << 8) | (8 << 0));
	/* update cursor SR watermark */
	I915_WRITE(DSPFW3, (cursor_sr << DSPFW_CURSOR_SR_SHIFT));
}

static void i9xx_update_wm(struct drm_crtc *unused_crtc)
{
	struct drm_device *dev = unused_crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	const struct intel_watermark_params *wm_info;
	uint32_t fwater_lo;
	uint32_t fwater_hi;
	int cwm, srwm = 1;
	int fifo_size;
	int planea_wm, planeb_wm;
	struct drm_crtc *crtc, *enabled = NULL;

	if (IS_I945GM(dev))
		wm_info = &i945_wm_info;
	else if (!IS_GEN2(dev))
		wm_info = &i915_wm_info;
	else
		wm_info = &i830_wm_info;

	fifo_size = dev_priv->display.get_fifo_size(dev, 0);
	crtc = intel_get_crtc_for_plane(dev, 0);
	if (intel_crtc_active(crtc)) {
		const struct drm_display_mode *adjusted_mode;
		int cpp = crtc->primary->fb->bits_per_pixel / 8;
		if (IS_GEN2(dev))
			cpp = 4;

		adjusted_mode = &to_intel_crtc(crtc)->config.adjusted_mode;
		planea_wm = intel_calculate_wm(adjusted_mode->crtc_clock,
					       wm_info, fifo_size, cpp,
					       latency_ns);
		enabled = crtc;
	} else
		planea_wm = fifo_size - wm_info->guard_size;

	fifo_size = dev_priv->display.get_fifo_size(dev, 1);
	crtc = intel_get_crtc_for_plane(dev, 1);
	if (intel_crtc_active(crtc)) {
		const struct drm_display_mode *adjusted_mode;
		int cpp = crtc->primary->fb->bits_per_pixel / 8;
		if (IS_GEN2(dev))
			cpp = 4;

		adjusted_mode = &to_intel_crtc(crtc)->config.adjusted_mode;
		planeb_wm = intel_calculate_wm(adjusted_mode->crtc_clock,
					       wm_info, fifo_size, cpp,
					       latency_ns);
		if (enabled == NULL)
			enabled = crtc;
		else
			enabled = NULL;
	} else
		planeb_wm = fifo_size - wm_info->guard_size;

	DRM_DEBUG_KMS("FIFO watermarks - A: %d, B: %d\n", planea_wm, planeb_wm);

	if (IS_I915GM(dev) && enabled) {
		struct intel_framebuffer *fb;

		fb = to_intel_framebuffer(enabled->primary->fb);

		/* self-refresh seems busted with untiled */
		if (fb->obj->tiling_mode == I915_TILING_NONE)
			enabled = NULL;
	}

	/*
	 * Overlay gets an aggressive default since video jitter is bad.
	 */
	cwm = 2;

	/* Play safe and disable self-refresh before adjusting watermarks. */
	intel_set_memory_cxsr(dev_priv, false);

	/* Calc sr entries for one plane configs */
	if (HAS_FW_BLC(dev) && enabled) {
		/* self-refresh has much higher latency */
		static const int sr_latency_ns = 6000;
		const struct drm_display_mode *adjusted_mode =
			&to_intel_crtc(enabled)->config.adjusted_mode;
		int clock = adjusted_mode->crtc_clock;
		int htotal = adjusted_mode->crtc_htotal;
		int hdisplay = to_intel_crtc(enabled)->config.pipe_src_w;
		int pixel_size = enabled->primary->fb->bits_per_pixel / 8;
		unsigned long line_time_us;
		int entries;

		line_time_us = max(htotal * 1000 / clock, 1);

		/* Use ns/us then divide to preserve precision */
		entries = (((sr_latency_ns / line_time_us) + 1000) / 1000) *
			pixel_size * hdisplay;
		entries = DIV_ROUND_UP(entries, wm_info->cacheline_size);
		DRM_DEBUG_KMS("self-refresh entries: %d\n", entries);
		srwm = wm_info->fifo_size - entries;
		if (srwm < 0)
			srwm = 1;

		if (IS_I945G(dev) || IS_I945GM(dev))
			I915_WRITE(FW_BLC_SELF,
				   FW_BLC_SELF_FIFO_MASK | (srwm & 0xff));
		else if (IS_I915GM(dev))
			I915_WRITE(FW_BLC_SELF, srwm & 0x3f);
	}

	DRM_DEBUG_KMS("Setting FIFO watermarks - A: %d, B: %d, C: %d, SR %d\n",
		      planea_wm, planeb_wm, cwm, srwm);

	fwater_lo = ((planeb_wm & 0x3f) << 16) | (planea_wm & 0x3f);
	fwater_hi = (cwm & 0x1f);

	/* Set request length to 8 cachelines per fetch */
	fwater_lo = fwater_lo | (1 << 24) | (1 << 8);
	fwater_hi = fwater_hi | (1 << 8);

	I915_WRITE(FW_BLC, fwater_lo);
	I915_WRITE(FW_BLC2, fwater_hi);

	if (enabled)
		intel_set_memory_cxsr(dev_priv, true);
}

static void i845_update_wm(struct drm_crtc *unused_crtc)
{
	struct drm_device *dev = unused_crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	const struct drm_display_mode *adjusted_mode;
	uint32_t fwater_lo;
	int planea_wm;

	crtc = single_enabled_crtc(dev);
	if (crtc == NULL)
		return;

	adjusted_mode = &to_intel_crtc(crtc)->config.adjusted_mode;
	planea_wm = intel_calculate_wm(adjusted_mode->crtc_clock,
				       &i845_wm_info,
				       dev_priv->display.get_fifo_size(dev, 0),
				       4, latency_ns);
	fwater_lo = I915_READ(FW_BLC) & ~0xfff;
	fwater_lo |= (3<<8) | planea_wm;

	DRM_DEBUG_KMS("Setting FIFO watermarks - A: %d\n", planea_wm);

	I915_WRITE(FW_BLC, fwater_lo);
}

static uint32_t ilk_pipe_pixel_rate(struct drm_device *dev,
				    struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	uint32_t pixel_rate;

	pixel_rate = intel_crtc->config.adjusted_mode.crtc_clock;

	/* We only use IF-ID interlacing. If we ever use PF-ID we'll need to
	 * adjust the pixel_rate here. */

	if (intel_crtc->config.pch_pfit.enabled) {
		uint64_t pipe_w, pipe_h, pfit_w, pfit_h;
		uint32_t pfit_size = intel_crtc->config.pch_pfit.size;

		pipe_w = intel_crtc->config.pipe_src_w;
		pipe_h = intel_crtc->config.pipe_src_h;
		pfit_w = (pfit_size >> 16) & 0xFFFF;
		pfit_h = pfit_size & 0xFFFF;
		if (pipe_w < pfit_w)
			pipe_w = pfit_w;
		if (pipe_h < pfit_h)
			pipe_h = pfit_h;

		pixel_rate = div_u64((uint64_t) pixel_rate * pipe_w * pipe_h,
				     pfit_w * pfit_h);
	}

	return pixel_rate;
}

/* latency must be in 0.1us units. */
static uint32_t ilk_wm_method1(uint32_t pixel_rate, uint8_t bytes_per_pixel,
			       uint32_t latency)
{
	uint64_t ret;

	if (WARN(latency == 0, "Latency value missing\n"))
		return UINT_MAX;

	ret = (uint64_t) pixel_rate * bytes_per_pixel * latency;
	ret = DIV_ROUND_UP_ULL(ret, 64 * 10000) + 2;

	return ret;
}

/* latency must be in 0.1us units. */
static uint32_t ilk_wm_method2(uint32_t pixel_rate, uint32_t pipe_htotal,
			       uint32_t horiz_pixels, uint8_t bytes_per_pixel,
			       uint32_t latency)
{
	uint32_t ret;

	if (WARN(latency == 0, "Latency value missing\n"))
		return UINT_MAX;

	ret = (latency * pixel_rate) / (pipe_htotal * 10000);
	ret = (ret + 1) * horiz_pixels * bytes_per_pixel;
	ret = DIV_ROUND_UP(ret, 64) + 2;
	return ret;
}

static uint32_t ilk_wm_fbc(uint32_t pri_val, uint32_t horiz_pixels,
			   uint8_t bytes_per_pixel)
{
	return DIV_ROUND_UP(pri_val * 64, horiz_pixels * bytes_per_pixel) + 2;
}

struct ilk_pipe_wm_parameters {
	bool active;
	uint32_t pipe_htotal;
	uint32_t pixel_rate;
	struct intel_plane_wm_parameters pri;
	struct intel_plane_wm_parameters spr;
	struct intel_plane_wm_parameters cur;
};

struct ilk_wm_maximums {
	uint16_t pri;
	uint16_t spr;
	uint16_t cur;
	uint16_t fbc;
};

/* used in computing the new watermarks state */
struct intel_wm_config {
	unsigned int num_pipes_active;
	bool sprites_enabled;
	bool sprites_scaled;
};

/*
 * For both WM_PIPE and WM_LP.
 * mem_value must be in 0.1us units.
 */
static uint32_t ilk_compute_pri_wm(const struct ilk_pipe_wm_parameters *params,
				   uint32_t mem_value,
				   bool is_lp)
{
	uint32_t method1, method2;

	if (!params->active || !params->pri.enabled)
		return 0;

	method1 = ilk_wm_method1(params->pixel_rate,
				 params->pri.bytes_per_pixel,
				 mem_value);

	if (!is_lp)
		return method1;

	method2 = ilk_wm_method2(params->pixel_rate,
				 params->pipe_htotal,
				 params->pri.horiz_pixels,
				 params->pri.bytes_per_pixel,
				 mem_value);

	return min(method1, method2);
}

/*
 * For both WM_PIPE and WM_LP.
 * mem_value must be in 0.1us units.
 */
static uint32_t ilk_compute_spr_wm(const struct ilk_pipe_wm_parameters *params,
				   uint32_t mem_value)
{
	uint32_t method1, method2;

	if (!params->active || !params->spr.enabled)
		return 0;

	method1 = ilk_wm_method1(params->pixel_rate,
				 params->spr.bytes_per_pixel,
				 mem_value);
	method2 = ilk_wm_method2(params->pixel_rate,
				 params->pipe_htotal,
				 params->spr.horiz_pixels,
				 params->spr.bytes_per_pixel,
				 mem_value);
	return min(method1, method2);
}

/*
 * For both WM_PIPE and WM_LP.
 * mem_value must be in 0.1us units.
 */
static uint32_t ilk_compute_cur_wm(const struct ilk_pipe_wm_parameters *params,
				   uint32_t mem_value)
{
	if (!params->active || !params->cur.enabled)
		return 0;

	return ilk_wm_method2(params->pixel_rate,
			      params->pipe_htotal,
			      params->cur.horiz_pixels,
			      params->cur.bytes_per_pixel,
			      mem_value);
}

/* Only for WM_LP. */
static uint32_t ilk_compute_fbc_wm(const struct ilk_pipe_wm_parameters *params,
				   uint32_t pri_val)
{
	if (!params->active || !params->pri.enabled)
		return 0;

	return ilk_wm_fbc(pri_val,
			  params->pri.horiz_pixels,
			  params->pri.bytes_per_pixel);
}

static unsigned int ilk_display_fifo_size(const struct drm_device *dev)
{
	if (INTEL_INFO(dev)->gen >= 8)
		return 3072;
	else if (INTEL_INFO(dev)->gen >= 7)
		return 768;
	else
		return 512;
}

static unsigned int ilk_plane_wm_reg_max(const struct drm_device *dev,
					 int level, bool is_sprite)
{
	if (INTEL_INFO(dev)->gen >= 8)
		/* BDW primary/sprite plane watermarks */
		return level == 0 ? 255 : 2047;
	else if (INTEL_INFO(dev)->gen >= 7)
		/* IVB/HSW primary/sprite plane watermarks */
		return level == 0 ? 127 : 1023;
	else if (!is_sprite)
		/* ILK/SNB primary plane watermarks */
		return level == 0 ? 127 : 511;
	else
		/* ILK/SNB sprite plane watermarks */
		return level == 0 ? 63 : 255;
}

static unsigned int ilk_cursor_wm_reg_max(const struct drm_device *dev,
					  int level)
{
	if (INTEL_INFO(dev)->gen >= 7)
		return level == 0 ? 63 : 255;
	else
		return level == 0 ? 31 : 63;
}

static unsigned int ilk_fbc_wm_reg_max(const struct drm_device *dev)
{
	if (INTEL_INFO(dev)->gen >= 8)
		return 31;
	else
		return 15;
}

/* Calculate the maximum primary/sprite plane watermark */
static unsigned int ilk_plane_wm_max(const struct drm_device *dev,
				     int level,
				     const struct intel_wm_config *config,
				     enum intel_ddb_partitioning ddb_partitioning,
				     bool is_sprite)
{
	unsigned int fifo_size = ilk_display_fifo_size(dev);

	/* if sprites aren't enabled, sprites get nothing */
	if (is_sprite && !config->sprites_enabled)
		return 0;

	/* HSW allows LP1+ watermarks even with multiple pipes */
	if (level == 0 || config->num_pipes_active > 1) {
		fifo_size /= INTEL_INFO(dev)->num_pipes;

		/*
		 * For some reason the non self refresh
		 * FIFO size is only half of the self
		 * refresh FIFO size on ILK/SNB.
		 */
		if (INTEL_INFO(dev)->gen <= 6)
			fifo_size /= 2;
	}

	if (config->sprites_enabled) {
		/* level 0 is always calculated with 1:1 split */
		if (level > 0 && ddb_partitioning == INTEL_DDB_PART_5_6) {
			if (is_sprite)
				fifo_size *= 5;
			fifo_size /= 6;
		} else {
			fifo_size /= 2;
		}
	}

	/* clamp to max that the registers can hold */
	return min(fifo_size, ilk_plane_wm_reg_max(dev, level, is_sprite));
}

/* Calculate the maximum cursor plane watermark */
static unsigned int ilk_cursor_wm_max(const struct drm_device *dev,
				      int level,
				      const struct intel_wm_config *config)
{
	/* HSW LP1+ watermarks w/ multiple pipes */
	if (level > 0 && config->num_pipes_active > 1)
		return 64;

	/* otherwise just report max that registers can hold */
	return ilk_cursor_wm_reg_max(dev, level);
}

static void ilk_compute_wm_maximums(const struct drm_device *dev,
				    int level,
				    const struct intel_wm_config *config,
				    enum intel_ddb_partitioning ddb_partitioning,
				    struct ilk_wm_maximums *max)
{
	max->pri = ilk_plane_wm_max(dev, level, config, ddb_partitioning, false);
	max->spr = ilk_plane_wm_max(dev, level, config, ddb_partitioning, true);
	max->cur = ilk_cursor_wm_max(dev, level, config);
	max->fbc = ilk_fbc_wm_reg_max(dev);
}

static void ilk_compute_wm_reg_maximums(struct drm_device *dev,
					int level,
					struct ilk_wm_maximums *max)
{
	max->pri = ilk_plane_wm_reg_max(dev, level, false);
	max->spr = ilk_plane_wm_reg_max(dev, level, true);
	max->cur = ilk_cursor_wm_reg_max(dev, level);
	max->fbc = ilk_fbc_wm_reg_max(dev);
}

static bool ilk_validate_wm_level(int level,
				  const struct ilk_wm_maximums *max,
				  struct intel_wm_level *result)
{
	bool ret;

	/* already determined to be invalid? */
	if (!result->enable)
		return false;

	result->enable = result->pri_val <= max->pri &&
			 result->spr_val <= max->spr &&
			 result->cur_val <= max->cur;

	ret = result->enable;

	/*
	 * HACK until we can pre-compute everything,
	 * and thus fail gracefully if LP0 watermarks
	 * are exceeded...
	 */
	if (level == 0 && !result->enable) {
		if (result->pri_val > max->pri)
			DRM_DEBUG_KMS("Primary WM%d too large %u (max %u)\n",
				      level, result->pri_val, max->pri);
		if (result->spr_val > max->spr)
			DRM_DEBUG_KMS("Sprite WM%d too large %u (max %u)\n",
				      level, result->spr_val, max->spr);
		if (result->cur_val > max->cur)
			DRM_DEBUG_KMS("Cursor WM%d too large %u (max %u)\n",
				      level, result->cur_val, max->cur);

		result->pri_val = min_t(uint32_t, result->pri_val, max->pri);
		result->spr_val = min_t(uint32_t, result->spr_val, max->spr);
		result->cur_val = min_t(uint32_t, result->cur_val, max->cur);
		result->enable = true;
	}

	return ret;
}

static void ilk_compute_wm_level(const struct drm_i915_private *dev_priv,
				 int level,
				 const struct ilk_pipe_wm_parameters *p,
				 struct intel_wm_level *result)
{
	uint16_t pri_latency = dev_priv->wm.pri_latency[level];
	uint16_t spr_latency = dev_priv->wm.spr_latency[level];
	uint16_t cur_latency = dev_priv->wm.cur_latency[level];

	/* WM1+ latency values stored in 0.5us units */
	if (level > 0) {
		pri_latency *= 5;
		spr_latency *= 5;
		cur_latency *= 5;
	}

	result->pri_val = ilk_compute_pri_wm(p, pri_latency, level);
	result->spr_val = ilk_compute_spr_wm(p, spr_latency);
	result->cur_val = ilk_compute_cur_wm(p, cur_latency);
	result->fbc_val = ilk_compute_fbc_wm(p, result->pri_val);
	result->enable = true;
}

static uint32_t
hsw_compute_linetime_wm(struct drm_device *dev, struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_display_mode *mode = &intel_crtc->config.adjusted_mode;
	u32 linetime, ips_linetime;

	if (!intel_crtc_active(crtc))
		return 0;

	/* The WM are computed with base on how long it takes to fill a single
	 * row at the given clock rate, multiplied by 8.
	 * */
	linetime = DIV_ROUND_CLOSEST(mode->crtc_htotal * 1000 * 8,
				     mode->crtc_clock);
	ips_linetime = DIV_ROUND_CLOSEST(mode->crtc_htotal * 1000 * 8,
					 intel_ddi_get_cdclk_freq(dev_priv));

	return PIPE_WM_LINETIME_IPS_LINETIME(ips_linetime) |
	       PIPE_WM_LINETIME_TIME(linetime);
}

static void intel_read_wm_latency(struct drm_device *dev, uint16_t wm[5])
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (IS_HASWELL(dev) || IS_BROADWELL(dev)) {
		uint64_t sskpd = I915_READ64(MCH_SSKPD);

		wm[0] = (sskpd >> 56) & 0xFF;
		if (wm[0] == 0)
			wm[0] = sskpd & 0xF;
		wm[1] = (sskpd >> 4) & 0xFF;
		wm[2] = (sskpd >> 12) & 0xFF;
		wm[3] = (sskpd >> 20) & 0x1FF;
		wm[4] = (sskpd >> 32) & 0x1FF;
	} else if (INTEL_INFO(dev)->gen >= 6) {
		uint32_t sskpd = I915_READ(MCH_SSKPD);

		wm[0] = (sskpd >> SSKPD_WM0_SHIFT) & SSKPD_WM_MASK;
		wm[1] = (sskpd >> SSKPD_WM1_SHIFT) & SSKPD_WM_MASK;
		wm[2] = (sskpd >> SSKPD_WM2_SHIFT) & SSKPD_WM_MASK;
		wm[3] = (sskpd >> SSKPD_WM3_SHIFT) & SSKPD_WM_MASK;
	} else if (INTEL_INFO(dev)->gen >= 5) {
		uint32_t mltr = I915_READ(MLTR_ILK);

		/* ILK primary LP0 latency is 700 ns */
		wm[0] = 7;
		wm[1] = (mltr >> MLTR_WM1_SHIFT) & ILK_SRLT_MASK;
		wm[2] = (mltr >> MLTR_WM2_SHIFT) & ILK_SRLT_MASK;
	}
}

static void intel_fixup_spr_wm_latency(struct drm_device *dev, uint16_t wm[5])
{
	/* ILK sprite LP0 latency is 1300 ns */
	if (INTEL_INFO(dev)->gen == 5)
		wm[0] = 13;
}

static void intel_fixup_cur_wm_latency(struct drm_device *dev, uint16_t wm[5])
{
	/* ILK cursor LP0 latency is 1300 ns */
	if (INTEL_INFO(dev)->gen == 5)
		wm[0] = 13;

	/* WaDoubleCursorLP3Latency:ivb */
	if (IS_IVYBRIDGE(dev))
		wm[3] *= 2;
}

int ilk_wm_max_level(const struct drm_device *dev)
{
	/* how many WM levels are we expecting */
	if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		return 4;
	else if (INTEL_INFO(dev)->gen >= 6)
		return 3;
	else
		return 2;
}

static void intel_print_wm_latency(struct drm_device *dev,
				   const char *name,
				   const uint16_t wm[5])
{
	int level, max_level = ilk_wm_max_level(dev);

	for (level = 0; level <= max_level; level++) {
		unsigned int latency = wm[level];

		if (latency == 0) {
			DRM_ERROR("%s WM%d latency not provided\n",
				  name, level);
			continue;
		}

		/* WM1+ latency values in 0.5us units */
		if (level > 0)
			latency *= 5;

		DRM_DEBUG_KMS("%s WM%d latency %u (%u.%u usec)\n",
			      name, level, wm[level],
			      latency / 10, latency % 10);
	}
}

static bool ilk_increase_wm_latency(struct drm_i915_private *dev_priv,
				    uint16_t wm[5], uint16_t min)
{
	int level, max_level = ilk_wm_max_level(dev_priv->dev);

	if (wm[0] >= min)
		return false;

	wm[0] = max(wm[0], min);
	for (level = 1; level <= max_level; level++)
		wm[level] = max_t(uint16_t, wm[level], DIV_ROUND_UP(min, 5));

	return true;
}

static void snb_wm_latency_quirk(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool changed;

	/*
	 * The BIOS provided WM memory latency values are often
	 * inadequate for high resolution displays. Adjust them.
	 */
	changed = ilk_increase_wm_latency(dev_priv, dev_priv->wm.pri_latency, 12) |
		ilk_increase_wm_latency(dev_priv, dev_priv->wm.spr_latency, 12) |
		ilk_increase_wm_latency(dev_priv, dev_priv->wm.cur_latency, 12);

	if (!changed)
		return;

	DRM_DEBUG_KMS("WM latency values increased to avoid potential underruns\n");
	intel_print_wm_latency(dev, "Primary", dev_priv->wm.pri_latency);
	intel_print_wm_latency(dev, "Sprite", dev_priv->wm.spr_latency);
	intel_print_wm_latency(dev, "Cursor", dev_priv->wm.cur_latency);
}

static void ilk_setup_wm_latency(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	intel_read_wm_latency(dev, dev_priv->wm.pri_latency);

	memcpy(dev_priv->wm.spr_latency, dev_priv->wm.pri_latency,
	       sizeof(dev_priv->wm.pri_latency));
	memcpy(dev_priv->wm.cur_latency, dev_priv->wm.pri_latency,
	       sizeof(dev_priv->wm.pri_latency));

	intel_fixup_spr_wm_latency(dev, dev_priv->wm.spr_latency);
	intel_fixup_cur_wm_latency(dev, dev_priv->wm.cur_latency);

	intel_print_wm_latency(dev, "Primary", dev_priv->wm.pri_latency);
	intel_print_wm_latency(dev, "Sprite", dev_priv->wm.spr_latency);
	intel_print_wm_latency(dev, "Cursor", dev_priv->wm.cur_latency);

	if (IS_GEN6(dev))
		snb_wm_latency_quirk(dev);
}

static void ilk_compute_wm_parameters(struct drm_crtc *crtc,
				      struct ilk_pipe_wm_parameters *p)
{
	struct drm_device *dev = crtc->dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum pipe pipe = intel_crtc->pipe;
	struct drm_plane *plane;

	if (!intel_crtc_active(crtc))
		return;

	p->active = true;
	p->pipe_htotal = intel_crtc->config.adjusted_mode.crtc_htotal;
	p->pixel_rate = ilk_pipe_pixel_rate(dev, crtc);
	p->pri.bytes_per_pixel = crtc->primary->fb->bits_per_pixel / 8;
	p->cur.bytes_per_pixel = 4;
	p->pri.horiz_pixels = intel_crtc->config.pipe_src_w;
	p->cur.horiz_pixels = intel_crtc->cursor_width;
	/* TODO: for now, assume primary and cursor planes are always enabled. */
	p->pri.enabled = true;
	p->cur.enabled = true;

	drm_for_each_legacy_plane(plane, &dev->mode_config.plane_list) {
		struct intel_plane *intel_plane = to_intel_plane(plane);

		if (intel_plane->pipe == pipe) {
			p->spr = intel_plane->wm;
			break;
		}
	}
}

static void ilk_compute_wm_config(struct drm_device *dev,
				  struct intel_wm_config *config)
{
	struct intel_crtc *intel_crtc;

	/* Compute the currently _active_ config */
	for_each_intel_crtc(dev, intel_crtc) {
		const struct intel_pipe_wm *wm = &intel_crtc->wm.active;

		if (!wm->pipe_enabled)
			continue;

		config->sprites_enabled |= wm->sprites_enabled;
		config->sprites_scaled |= wm->sprites_scaled;
		config->num_pipes_active++;
	}
}

/* Compute new watermarks for the pipe */
static bool intel_compute_pipe_wm(struct drm_crtc *crtc,
				  const struct ilk_pipe_wm_parameters *params,
				  struct intel_pipe_wm *pipe_wm)
{
	struct drm_device *dev = crtc->dev;
	const struct drm_i915_private *dev_priv = dev->dev_private;
	int level, max_level = ilk_wm_max_level(dev);
	/* LP0 watermark maximums depend on this pipe alone */
	struct intel_wm_config config = {
		.num_pipes_active = 1,
		.sprites_enabled = params->spr.enabled,
		.sprites_scaled = params->spr.scaled,
	};
	struct ilk_wm_maximums max;

	pipe_wm->pipe_enabled = params->active;
	pipe_wm->sprites_enabled = params->spr.enabled;
	pipe_wm->sprites_scaled = params->spr.scaled;

	/* ILK/SNB: LP2+ watermarks only w/o sprites */
	if (INTEL_INFO(dev)->gen <= 6 && params->spr.enabled)
		max_level = 1;

	/* ILK/SNB/IVB: LP1+ watermarks only w/o scaling */
	if (params->spr.scaled)
		max_level = 0;

	ilk_compute_wm_level(dev_priv, 0, params, &pipe_wm->wm[0]);

	if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		pipe_wm->linetime = hsw_compute_linetime_wm(dev, crtc);

	/* LP0 watermarks always use 1/2 DDB partitioning */
	ilk_compute_wm_maximums(dev, 0, &config, INTEL_DDB_PART_1_2, &max);

	/* At least LP0 must be valid */
	if (!ilk_validate_wm_level(0, &max, &pipe_wm->wm[0]))
		return false;

	ilk_compute_wm_reg_maximums(dev, 1, &max);

	for (level = 1; level <= max_level; level++) {
		struct intel_wm_level wm = {};

		ilk_compute_wm_level(dev_priv, level, params, &wm);

		/*
		 * Disable any watermark level that exceeds the
		 * register maximums since such watermarks are
		 * always invalid.
		 */
		if (!ilk_validate_wm_level(level, &max, &wm))
			break;

		pipe_wm->wm[level] = wm;
	}

	return true;
}

/*
 * Merge the watermarks from all active pipes for a specific level.
 */
static void ilk_merge_wm_level(struct drm_device *dev,
			       int level,
			       struct intel_wm_level *ret_wm)
{
	const struct intel_crtc *intel_crtc;

	ret_wm->enable = true;

	for_each_intel_crtc(dev, intel_crtc) {
		const struct intel_pipe_wm *active = &intel_crtc->wm.active;
		const struct intel_wm_level *wm = &active->wm[level];

		if (!active->pipe_enabled)
			continue;

		/*
		 * The watermark values may have been used in the past,
		 * so we must maintain them in the registers for some
		 * time even if the level is now disabled.
		 */
		if (!wm->enable)
			ret_wm->enable = false;

		ret_wm->pri_val = max(ret_wm->pri_val, wm->pri_val);
		ret_wm->spr_val = max(ret_wm->spr_val, wm->spr_val);
		ret_wm->cur_val = max(ret_wm->cur_val, wm->cur_val);
		ret_wm->fbc_val = max(ret_wm->fbc_val, wm->fbc_val);
	}
}

/*
 * Merge all low power watermarks for all active pipes.
 */
static void ilk_wm_merge(struct drm_device *dev,
			 const struct intel_wm_config *config,
			 const struct ilk_wm_maximums *max,
			 struct intel_pipe_wm *merged)
{
	int level, max_level = ilk_wm_max_level(dev);
	int last_enabled_level = max_level;

	/* ILK/SNB/IVB: LP1+ watermarks only w/ single pipe */
	if ((INTEL_INFO(dev)->gen <= 6 || IS_IVYBRIDGE(dev)) &&
	    config->num_pipes_active > 1)
		return;

	/* ILK: FBC WM must be disabled always */
	merged->fbc_wm_enabled = INTEL_INFO(dev)->gen >= 6;

	/* merge each WM1+ level */
	for (level = 1; level <= max_level; level++) {
		struct intel_wm_level *wm = &merged->wm[level];

		ilk_merge_wm_level(dev, level, wm);

		if (level > last_enabled_level)
			wm->enable = false;
		else if (!ilk_validate_wm_level(level, max, wm))
			/* make sure all following levels get disabled */
			last_enabled_level = level - 1;

		/*
		 * The spec says it is preferred to disable
		 * FBC WMs instead of disabling a WM level.
		 */
		if (wm->fbc_val > max->fbc) {
			if (wm->enable)
				merged->fbc_wm_enabled = false;
			wm->fbc_val = 0;
		}
	}

	/* ILK: LP2+ must be disabled when FBC WM is disabled but FBC enabled */
	/*
	 * FIXME this is racy. FBC might get enabled later.
	 * What we should check here is whether FBC can be
	 * enabled sometime later.
	 */
	if (IS_GEN5(dev) && !merged->fbc_wm_enabled && intel_fbc_enabled(dev)) {
		for (level = 2; level <= max_level; level++) {
			struct intel_wm_level *wm = &merged->wm[level];

			wm->enable = false;
		}
	}
}

static int ilk_wm_lp_to_level(int wm_lp, const struct intel_pipe_wm *pipe_wm)
{
	/* LP1,LP2,LP3 levels are either 1,2,3 or 1,3,4 */
	return wm_lp + (wm_lp >= 2 && pipe_wm->wm[4].enable);
}

/* The value we need to program into the WM_LPx latency field */
static unsigned int ilk_wm_lp_latency(struct drm_device *dev, int level)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		return 2 * level;
	else
		return dev_priv->wm.pri_latency[level];
}

static void ilk_compute_wm_results(struct drm_device *dev,
				   const struct intel_pipe_wm *merged,
				   enum intel_ddb_partitioning partitioning,
				   struct ilk_wm_values *results)
{
	struct intel_crtc *intel_crtc;
	int level, wm_lp;

	results->enable_fbc_wm = merged->fbc_wm_enabled;
	results->partitioning = partitioning;

	/* LP1+ register values */
	for (wm_lp = 1; wm_lp <= 3; wm_lp++) {
		const struct intel_wm_level *r;

		level = ilk_wm_lp_to_level(wm_lp, merged);

		r = &merged->wm[level];

		/*
		 * Maintain the watermark values even if the level is
		 * disabled. Doing otherwise could cause underruns.
		 */
		results->wm_lp[wm_lp - 1] =
			(ilk_wm_lp_latency(dev, level) << WM1_LP_LATENCY_SHIFT) |
			(r->pri_val << WM1_LP_SR_SHIFT) |
			r->cur_val;

		if (r->enable)
			results->wm_lp[wm_lp - 1] |= WM1_LP_SR_EN;

		if (INTEL_INFO(dev)->gen >= 8)
			results->wm_lp[wm_lp - 1] |=
				r->fbc_val << WM1_LP_FBC_SHIFT_BDW;
		else
			results->wm_lp[wm_lp - 1] |=
				r->fbc_val << WM1_LP_FBC_SHIFT;

		/*
		 * Always set WM1S_LP_EN when spr_val != 0, even if the
		 * level is disabled. Doing otherwise could cause underruns.
		 */
		if (INTEL_INFO(dev)->gen <= 6 && r->spr_val) {
			WARN_ON(wm_lp != 1);
			results->wm_lp_spr[wm_lp - 1] = WM1S_LP_EN | r->spr_val;
		} else
			results->wm_lp_spr[wm_lp - 1] = r->spr_val;
	}

	/* LP0 register values */
	for_each_intel_crtc(dev, intel_crtc) {
		enum pipe pipe = intel_crtc->pipe;
		const struct intel_wm_level *r =
			&intel_crtc->wm.active.wm[0];

		if (WARN_ON(!r->enable))
			continue;

		results->wm_linetime[pipe] = intel_crtc->wm.active.linetime;

		results->wm_pipe[pipe] =
			(r->pri_val << WM0_PIPE_PLANE_SHIFT) |
			(r->spr_val << WM0_PIPE_SPRITE_SHIFT) |
			r->cur_val;
	}
}

/* Find the result with the highest level enabled. Check for enable_fbc_wm in
 * case both are at the same level. Prefer r1 in case they're the same. */
static struct intel_pipe_wm *ilk_find_best_result(struct drm_device *dev,
						  struct intel_pipe_wm *r1,
						  struct intel_pipe_wm *r2)
{
	int level, max_level = ilk_wm_max_level(dev);
	int level1 = 0, level2 = 0;

	for (level = 1; level <= max_level; level++) {
		if (r1->wm[level].enable)
			level1 = level;
		if (r2->wm[level].enable)
			level2 = level;
	}

	if (level1 == level2) {
		if (r2->fbc_wm_enabled && !r1->fbc_wm_enabled)
			return r2;
		else
			return r1;
	} else if (level1 > level2) {
		return r1;
	} else {
		return r2;
	}
}

/* dirty bits used to track which watermarks need changes */
#define WM_DIRTY_PIPE(pipe) (1 << (pipe))
#define WM_DIRTY_LINETIME(pipe) (1 << (8 + (pipe)))
#define WM_DIRTY_LP(wm_lp) (1 << (15 + (wm_lp)))
#define WM_DIRTY_LP_ALL (WM_DIRTY_LP(1) | WM_DIRTY_LP(2) | WM_DIRTY_LP(3))
#define WM_DIRTY_FBC (1 << 24)
#define WM_DIRTY_DDB (1 << 25)

static unsigned int ilk_compute_wm_dirty(struct drm_device *dev,
					 const struct ilk_wm_values *old,
					 const struct ilk_wm_values *new)
{
	unsigned int dirty = 0;
	enum pipe pipe;
	int wm_lp;

	for_each_pipe(pipe) {
		if (old->wm_linetime[pipe] != new->wm_linetime[pipe]) {
			dirty |= WM_DIRTY_LINETIME(pipe);
			/* Must disable LP1+ watermarks too */
			dirty |= WM_DIRTY_LP_ALL;
		}

		if (old->wm_pipe[pipe] != new->wm_pipe[pipe]) {
			dirty |= WM_DIRTY_PIPE(pipe);
			/* Must disable LP1+ watermarks too */
			dirty |= WM_DIRTY_LP_ALL;
		}
	}

	if (old->enable_fbc_wm != new->enable_fbc_wm) {
		dirty |= WM_DIRTY_FBC;
		/* Must disable LP1+ watermarks too */
		dirty |= WM_DIRTY_LP_ALL;
	}

	if (old->partitioning != new->partitioning) {
		dirty |= WM_DIRTY_DDB;
		/* Must disable LP1+ watermarks too */
		dirty |= WM_DIRTY_LP_ALL;
	}

	/* LP1+ watermarks already deemed dirty, no need to continue */
	if (dirty & WM_DIRTY_LP_ALL)
		return dirty;

	/* Find the lowest numbered LP1+ watermark in need of an update... */
	for (wm_lp = 1; wm_lp <= 3; wm_lp++) {
		if (old->wm_lp[wm_lp - 1] != new->wm_lp[wm_lp - 1] ||
		    old->wm_lp_spr[wm_lp - 1] != new->wm_lp_spr[wm_lp - 1])
			break;
	}

	/* ...and mark it and all higher numbered LP1+ watermarks as dirty */
	for (; wm_lp <= 3; wm_lp++)
		dirty |= WM_DIRTY_LP(wm_lp);

	return dirty;
}

static bool _ilk_disable_lp_wm(struct drm_i915_private *dev_priv,
			       unsigned int dirty)
{
	struct ilk_wm_values *previous = &dev_priv->wm.hw;
	bool changed = false;

	if (dirty & WM_DIRTY_LP(3) && previous->wm_lp[2] & WM1_LP_SR_EN) {
		previous->wm_lp[2] &= ~WM1_LP_SR_EN;
		I915_WRITE(WM3_LP_ILK, previous->wm_lp[2]);
		changed = true;
	}
	if (dirty & WM_DIRTY_LP(2) && previous->wm_lp[1] & WM1_LP_SR_EN) {
		previous->wm_lp[1] &= ~WM1_LP_SR_EN;
		I915_WRITE(WM2_LP_ILK, previous->wm_lp[1]);
		changed = true;
	}
	if (dirty & WM_DIRTY_LP(1) && previous->wm_lp[0] & WM1_LP_SR_EN) {
		previous->wm_lp[0] &= ~WM1_LP_SR_EN;
		I915_WRITE(WM1_LP_ILK, previous->wm_lp[0]);
		changed = true;
	}

	/*
	 * Don't touch WM1S_LP_EN here.
	 * Doing so could cause underruns.
	 */

	return changed;
}

/*
 * The spec says we shouldn't write when we don't need, because every write
 * causes WMs to be re-evaluated, expending some power.
 */
static void ilk_write_wm_values(struct drm_i915_private *dev_priv,
				struct ilk_wm_values *results)
{
	struct drm_device *dev = dev_priv->dev;
	struct ilk_wm_values *previous = &dev_priv->wm.hw;
	unsigned int dirty;
	uint32_t val;

	dirty = ilk_compute_wm_dirty(dev, previous, results);
	if (!dirty)
		return;

	_ilk_disable_lp_wm(dev_priv, dirty);

	if (dirty & WM_DIRTY_PIPE(PIPE_A))
		I915_WRITE(WM0_PIPEA_ILK, results->wm_pipe[0]);
	if (dirty & WM_DIRTY_PIPE(PIPE_B))
		I915_WRITE(WM0_PIPEB_ILK, results->wm_pipe[1]);
	if (dirty & WM_DIRTY_PIPE(PIPE_C))
		I915_WRITE(WM0_PIPEC_IVB, results->wm_pipe[2]);

	if (dirty & WM_DIRTY_LINETIME(PIPE_A))
		I915_WRITE(PIPE_WM_LINETIME(PIPE_A), results->wm_linetime[0]);
	if (dirty & WM_DIRTY_LINETIME(PIPE_B))
		I915_WRITE(PIPE_WM_LINETIME(PIPE_B), results->wm_linetime[1]);
	if (dirty & WM_DIRTY_LINETIME(PIPE_C))
		I915_WRITE(PIPE_WM_LINETIME(PIPE_C), results->wm_linetime[2]);

	if (dirty & WM_DIRTY_DDB) {
		if (IS_HASWELL(dev) || IS_BROADWELL(dev)) {
			val = I915_READ(WM_MISC);
			if (results->partitioning == INTEL_DDB_PART_1_2)
				val &= ~WM_MISC_DATA_PARTITION_5_6;
			else
				val |= WM_MISC_DATA_PARTITION_5_6;
			I915_WRITE(WM_MISC, val);
		} else {
			val = I915_READ(DISP_ARB_CTL2);
			if (results->partitioning == INTEL_DDB_PART_1_2)
				val &= ~DISP_DATA_PARTITION_5_6;
			else
				val |= DISP_DATA_PARTITION_5_6;
			I915_WRITE(DISP_ARB_CTL2, val);
		}
	}

	if (dirty & WM_DIRTY_FBC) {
		val = I915_READ(DISP_ARB_CTL);
		if (results->enable_fbc_wm)
			val &= ~DISP_FBC_WM_DIS;
		else
			val |= DISP_FBC_WM_DIS;
		I915_WRITE(DISP_ARB_CTL, val);
	}

	if (dirty & WM_DIRTY_LP(1) &&
	    previous->wm_lp_spr[0] != results->wm_lp_spr[0])
		I915_WRITE(WM1S_LP_ILK, results->wm_lp_spr[0]);

	if (INTEL_INFO(dev)->gen >= 7) {
		if (dirty & WM_DIRTY_LP(2) && previous->wm_lp_spr[1] != results->wm_lp_spr[1])
			I915_WRITE(WM2S_LP_IVB, results->wm_lp_spr[1]);
		if (dirty & WM_DIRTY_LP(3) && previous->wm_lp_spr[2] != results->wm_lp_spr[2])
			I915_WRITE(WM3S_LP_IVB, results->wm_lp_spr[2]);
	}

	if (dirty & WM_DIRTY_LP(1) && previous->wm_lp[0] != results->wm_lp[0])
		I915_WRITE(WM1_LP_ILK, results->wm_lp[0]);
	if (dirty & WM_DIRTY_LP(2) && previous->wm_lp[1] != results->wm_lp[1])
		I915_WRITE(WM2_LP_ILK, results->wm_lp[1]);
	if (dirty & WM_DIRTY_LP(3) && previous->wm_lp[2] != results->wm_lp[2])
		I915_WRITE(WM3_LP_ILK, results->wm_lp[2]);

	dev_priv->wm.hw = *results;
}

static bool ilk_disable_lp_wm(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	return _ilk_disable_lp_wm(dev_priv, WM_DIRTY_LP_ALL);
}

static void ilk_update_wm(struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct ilk_wm_maximums max;
	struct ilk_pipe_wm_parameters params = {};
	struct ilk_wm_values results = {};
	enum intel_ddb_partitioning partitioning;
	struct intel_pipe_wm pipe_wm = {};
	struct intel_pipe_wm lp_wm_1_2 = {}, lp_wm_5_6 = {}, *best_lp_wm;
	struct intel_wm_config config = {};

	ilk_compute_wm_parameters(crtc, &params);

	intel_compute_pipe_wm(crtc, &params, &pipe_wm);

	if (!memcmp(&intel_crtc->wm.active, &pipe_wm, sizeof(pipe_wm)))
		return;

	intel_crtc->wm.active = pipe_wm;

	ilk_compute_wm_config(dev, &config);

	ilk_compute_wm_maximums(dev, 1, &config, INTEL_DDB_PART_1_2, &max);
	ilk_wm_merge(dev, &config, &max, &lp_wm_1_2);

	/* 5/6 split only in single pipe config on IVB+ */
	if (INTEL_INFO(dev)->gen >= 7 &&
	    config.num_pipes_active == 1 && config.sprites_enabled) {
		ilk_compute_wm_maximums(dev, 1, &config, INTEL_DDB_PART_5_6, &max);
		ilk_wm_merge(dev, &config, &max, &lp_wm_5_6);

		best_lp_wm = ilk_find_best_result(dev, &lp_wm_1_2, &lp_wm_5_6);
	} else {
		best_lp_wm = &lp_wm_1_2;
	}

	partitioning = (best_lp_wm == &lp_wm_1_2) ?
		       INTEL_DDB_PART_1_2 : INTEL_DDB_PART_5_6;

	ilk_compute_wm_results(dev, best_lp_wm, partitioning, &results);

	ilk_write_wm_values(dev_priv, &results);
}

static void ilk_update_sprite_wm(struct drm_plane *plane,
				     struct drm_crtc *crtc,
				     uint32_t sprite_width, int pixel_size,
				     bool enabled, bool scaled)
{
	struct drm_device *dev = plane->dev;
	struct intel_plane *intel_plane = to_intel_plane(plane);

	intel_plane->wm.enabled = enabled;
	intel_plane->wm.scaled = scaled;
	intel_plane->wm.horiz_pixels = sprite_width;
	intel_plane->wm.bytes_per_pixel = pixel_size;

	/*
	 * IVB workaround: must disable low power watermarks for at least
	 * one frame before enabling scaling.  LP watermarks can be re-enabled
	 * when scaling is disabled.
	 *
	 * WaCxSRDisabledForSpriteScaling:ivb
	 */
	if (IS_IVYBRIDGE(dev) && scaled && ilk_disable_lp_wm(dev))
		intel_wait_for_vblank(dev, intel_plane->pipe);

	ilk_update_wm(crtc);
}

static void ilk_pipe_wm_get_hw_state(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct ilk_wm_values *hw = &dev_priv->wm.hw;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_pipe_wm *active = &intel_crtc->wm.active;
	enum pipe pipe = intel_crtc->pipe;
	static const unsigned int wm0_pipe_reg[] = {
		[PIPE_A] = WM0_PIPEA_ILK,
		[PIPE_B] = WM0_PIPEB_ILK,
		[PIPE_C] = WM0_PIPEC_IVB,
	};

	hw->wm_pipe[pipe] = I915_READ(wm0_pipe_reg[pipe]);
	if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		hw->wm_linetime[pipe] = I915_READ(PIPE_WM_LINETIME(pipe));

	active->pipe_enabled = intel_crtc_active(crtc);

	if (active->pipe_enabled) {
		u32 tmp = hw->wm_pipe[pipe];

		/*
		 * For active pipes LP0 watermark is marked as
		 * enabled, and LP1+ watermaks as disabled since
		 * we can't really reverse compute them in case
		 * multiple pipes are active.
		 */
		active->wm[0].enable = true;
		active->wm[0].pri_val = (tmp & WM0_PIPE_PLANE_MASK) >> WM0_PIPE_PLANE_SHIFT;
		active->wm[0].spr_val = (tmp & WM0_PIPE_SPRITE_MASK) >> WM0_PIPE_SPRITE_SHIFT;
		active->wm[0].cur_val = tmp & WM0_PIPE_CURSOR_MASK;
		active->linetime = hw->wm_linetime[pipe];
	} else {
		int level, max_level = ilk_wm_max_level(dev);

		/*
		 * For inactive pipes, all watermark levels
		 * should be marked as enabled but zeroed,
		 * which is what we'd compute them to.
		 */
		for (level = 0; level <= max_level; level++)
			active->wm[level].enable = true;
	}
}

void ilk_wm_get_hw_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct ilk_wm_values *hw = &dev_priv->wm.hw;
	struct drm_crtc *crtc;

	for_each_crtc(dev, crtc)
		ilk_pipe_wm_get_hw_state(crtc);

	hw->wm_lp[0] = I915_READ(WM1_LP_ILK);
	hw->wm_lp[1] = I915_READ(WM2_LP_ILK);
	hw->wm_lp[2] = I915_READ(WM3_LP_ILK);

	hw->wm_lp_spr[0] = I915_READ(WM1S_LP_ILK);
	if (INTEL_INFO(dev)->gen >= 7) {
		hw->wm_lp_spr[1] = I915_READ(WM2S_LP_IVB);
		hw->wm_lp_spr[2] = I915_READ(WM3S_LP_IVB);
	}

	if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		hw->partitioning = (I915_READ(WM_MISC) & WM_MISC_DATA_PARTITION_5_6) ?
			INTEL_DDB_PART_5_6 : INTEL_DDB_PART_1_2;
	else if (IS_IVYBRIDGE(dev))
		hw->partitioning = (I915_READ(DISP_ARB_CTL2) & DISP_DATA_PARTITION_5_6) ?
			INTEL_DDB_PART_5_6 : INTEL_DDB_PART_1_2;

	hw->enable_fbc_wm =
		!(I915_READ(DISP_ARB_CTL) & DISP_FBC_WM_DIS);
}

/**
 * intel_update_watermarks - update FIFO watermark values based on current modes
 *
 * Calculate watermark values for the various WM regs based on current mode
 * and plane configuration.
 *
 * There are several cases to deal with here:
 *   - normal (i.e. non-self-refresh)
 *   - self-refresh (SR) mode
 *   - lines are large relative to FIFO size (buffer can hold up to 2)
 *   - lines are small relative to FIFO size (buffer can hold more than 2
 *     lines), so need to account for TLB latency
 *
 *   The normal calculation is:
 *     watermark = dotclock * bytes per pixel * latency
 *   where latency is platform & configuration dependent (we assume pessimal
 *   values here).
 *
 *   The SR calculation is:
 *     watermark = (trunc(latency/line time)+1) * surface width *
 *       bytes per pixel
 *   where
 *     line time = htotal / dotclock
 *     surface width = hdisplay for normal plane and 64 for cursor
 *   and latency is assumed to be high, as above.
 *
 * The final value programmed to the register should always be rounded up,
 * and include an extra 2 entries to account for clock crossings.
 *
 * We don't use the sprite, so we can ignore that.  And on Crestline we have
 * to set the non-SR watermarks to 8.
 */
void intel_update_watermarks(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = crtc->dev->dev_private;

	if (dev_priv->display.update_wm)
		dev_priv->display.update_wm(crtc);
}

void intel_update_sprite_watermarks(struct drm_plane *plane,
				    struct drm_crtc *crtc,
				    uint32_t sprite_width, int pixel_size,
				    bool enabled, bool scaled)
{
	struct drm_i915_private *dev_priv = plane->dev->dev_private;

	if (dev_priv->display.update_sprite_wm)
		dev_priv->display.update_sprite_wm(plane, crtc, sprite_width,
						   pixel_size, enabled, scaled);
}

static struct drm_i915_gem_object *
intel_alloc_context_page(struct drm_device *dev)
{
	struct drm_i915_gem_object *ctx;
	int ret;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	ctx = i915_gem_alloc_object(dev, 4096);
	if (!ctx) {
		DRM_DEBUG("failed to alloc power context, RC6 disabled\n");
		return NULL;
	}

	ret = i915_gem_obj_ggtt_pin(ctx, 4096, 0);
	if (ret) {
		DRM_ERROR("failed to pin power context: %d\n", ret);
		goto err_unref;
	}

	ret = i915_gem_object_set_to_gtt_domain(ctx, 1);
	if (ret) {
		DRM_ERROR("failed to set-domain on power context: %d\n", ret);
		goto err_unpin;
	}

	return ctx;

err_unpin:
	i915_gem_object_ggtt_unpin(ctx);
err_unref:
	drm_gem_object_unreference(&ctx->base);
	return NULL;
}

/**
 * Lock protecting IPS related data structures
 */
DEFINE_SPINLOCK(mchdev_lock);

/* Global for IPS driver to get at the current i915 device. Protected by
 * mchdev_lock. */
static struct drm_i915_private *i915_mch_dev;

bool ironlake_set_drps(struct drm_device *dev, u8 val)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u16 rgvswctl;

	assert_spin_locked(&mchdev_lock);

	rgvswctl = I915_READ16(MEMSWCTL);
	if (rgvswctl & MEMCTL_CMD_STS) {
		DRM_DEBUG("gpu busy, RCS change rejected\n");
		return false; /* still busy with another command */
	}

	rgvswctl = (MEMCTL_CMD_CHFREQ << MEMCTL_CMD_SHIFT) |
		(val << MEMCTL_FREQ_SHIFT) | MEMCTL_SFCAVM;
	I915_WRITE16(MEMSWCTL, rgvswctl);
	POSTING_READ16(MEMSWCTL);

	rgvswctl |= MEMCTL_CMD_STS;
	I915_WRITE16(MEMSWCTL, rgvswctl);

	return true;
}

static void ironlake_enable_drps(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 rgvmodectl = I915_READ(MEMMODECTL);
	u8 fmax, fmin, fstart, vstart;

	spin_lock_irq(&mchdev_lock);

	/* Enable temp reporting */
	I915_WRITE16(PMMISC, I915_READ(PMMISC) | MCPPCE_EN);
	I915_WRITE16(TSC1, I915_READ(TSC1) | TSE);

	/* 100ms RC evaluation intervals */
	I915_WRITE(RCUPEI, 100000);
	I915_WRITE(RCDNEI, 100000);

	/* Set max/min thresholds to 90ms and 80ms respectively */
	I915_WRITE(RCBMAXAVG, 90000);
	I915_WRITE(RCBMINAVG, 80000);

	I915_WRITE(MEMIHYST, 1);

	/* Set up min, max, and cur for interrupt handling */
	fmax = (rgvmodectl & MEMMODE_FMAX_MASK) >> MEMMODE_FMAX_SHIFT;
	fmin = (rgvmodectl & MEMMODE_FMIN_MASK);
	fstart = (rgvmodectl & MEMMODE_FSTART_MASK) >>
		MEMMODE_FSTART_SHIFT;

	vstart = (I915_READ(PXVFREQ_BASE + (fstart * 4)) & PXVFREQ_PX_MASK) >>
		PXVFREQ_PX_SHIFT;

	dev_priv->ips.fmax = fmax; /* IPS callback will increase this */
	dev_priv->ips.fstart = fstart;

	dev_priv->ips.max_delay = fstart;
	dev_priv->ips.min_delay = fmin;
	dev_priv->ips.cur_delay = fstart;

	DRM_DEBUG_DRIVER("fmax: %d, fmin: %d, fstart: %d\n",
			 fmax, fmin, fstart);

	I915_WRITE(MEMINTREN, MEMINT_CX_SUPR_EN | MEMINT_EVAL_CHG_EN);

	/*
	 * Interrupts will be enabled in ironlake_irq_postinstall
	 */

	I915_WRITE(VIDSTART, vstart);
	POSTING_READ(VIDSTART);

	rgvmodectl |= MEMMODE_SWMODE_EN;
	I915_WRITE(MEMMODECTL, rgvmodectl);

	if (wait_for_atomic((I915_READ(MEMSWCTL) & MEMCTL_CMD_STS) == 0, 10))
		DRM_ERROR("stuck trying to change perf mode\n");
	mdelay(1);

	ironlake_set_drps(dev, fstart);

	dev_priv->ips.last_count1 = I915_READ(0x112e4) + I915_READ(0x112e8) +
		I915_READ(0x112e0);
	dev_priv->ips.last_time1 = jiffies_to_msecs(jiffies);
	dev_priv->ips.last_count2 = I915_READ(0x112f4);
	getrawmonotonic(&dev_priv->ips.last_time2);

	spin_unlock_irq(&mchdev_lock);
}

static void ironlake_disable_drps(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u16 rgvswctl;

	spin_lock_irq(&mchdev_lock);

	rgvswctl = I915_READ16(MEMSWCTL);

	/* Ack interrupts, disable EFC interrupt */
	I915_WRITE(MEMINTREN, I915_READ(MEMINTREN) & ~MEMINT_EVAL_CHG_EN);
	I915_WRITE(MEMINTRSTS, MEMINT_EVAL_CHG);
	I915_WRITE(DEIER, I915_READ(DEIER) & ~DE_PCU_EVENT);
	I915_WRITE(DEIIR, DE_PCU_EVENT);
	I915_WRITE(DEIMR, I915_READ(DEIMR) | DE_PCU_EVENT);

	/* Go back to the starting frequency */
	ironlake_set_drps(dev, dev_priv->ips.fstart);
	mdelay(1);
	rgvswctl |= MEMCTL_CMD_STS;
	I915_WRITE(MEMSWCTL, rgvswctl);
	mdelay(1);

	spin_unlock_irq(&mchdev_lock);
}

/* There's a funny hw issue where the hw returns all 0 when reading from
 * GEN6_RP_INTERRUPT_LIMITS. Hence we always need to compute the desired value
 * ourselves, instead of doing a rmw cycle (which might result in us clearing
 * all limits and the gpu stuck at whatever frequency it is at atm).
 */
static u32 gen6_rps_limits(struct drm_i915_private *dev_priv, u8 val)
{
	u32 limits;

	/* Only set the down limit when we've reached the lowest level to avoid
	 * getting more interrupts, otherwise leave this clear. This prevents a
	 * race in the hw when coming out of rc6: There's a tiny window where
	 * the hw runs at the minimal clock before selecting the desired
	 * frequency, if the down threshold expires in that window we will not
	 * receive a down interrupt. */
	limits = dev_priv->rps.max_freq_softlimit << 24;
	if (val <= dev_priv->rps.min_freq_softlimit)
		limits |= dev_priv->rps.min_freq_softlimit << 16;

	return limits;
}

static void gen6_set_rps_thresholds(struct drm_i915_private *dev_priv, u8 val)
{
	int new_power;

	new_power = dev_priv->rps.power;
	switch (dev_priv->rps.power) {
	case LOW_POWER:
		if (val > dev_priv->rps.efficient_freq + 1 && val > dev_priv->rps.cur_freq)
			new_power = BETWEEN;
		break;

	case BETWEEN:
		if (val <= dev_priv->rps.efficient_freq && val < dev_priv->rps.cur_freq)
			new_power = LOW_POWER;
		else if (val >= dev_priv->rps.rp0_freq && val > dev_priv->rps.cur_freq)
			new_power = HIGH_POWER;
		break;

	case HIGH_POWER:
		if (val < (dev_priv->rps.rp1_freq + dev_priv->rps.rp0_freq) >> 1 && val < dev_priv->rps.cur_freq)
			new_power = BETWEEN;
		break;
	}
	/* Max/min bins are special */
	if (val == dev_priv->rps.min_freq_softlimit)
		new_power = LOW_POWER;
	if (val == dev_priv->rps.max_freq_softlimit)
		new_power = HIGH_POWER;
	if (new_power == dev_priv->rps.power)
		return;

	/* Note the units here are not exactly 1us, but 1280ns. */
	switch (new_power) {
	case LOW_POWER:
		/* Upclock if more than 95% busy over 16ms */
		I915_WRITE(GEN6_RP_UP_EI, 12500);
		I915_WRITE(GEN6_RP_UP_THRESHOLD, 11800);

		/* Downclock if less than 85% busy over 32ms */
		I915_WRITE(GEN6_RP_DOWN_EI, 25000);
		I915_WRITE(GEN6_RP_DOWN_THRESHOLD, 21250);

		I915_WRITE(GEN6_RP_CONTROL,
			   GEN6_RP_MEDIA_TURBO |
			   GEN6_RP_MEDIA_HW_NORMAL_MODE |
			   GEN6_RP_MEDIA_IS_GFX |
			   GEN6_RP_ENABLE |
			   GEN6_RP_UP_BUSY_AVG |
			   GEN6_RP_DOWN_IDLE_AVG);
		break;

	case BETWEEN:
		/* Upclock if more than 90% busy over 13ms */
		I915_WRITE(GEN6_RP_UP_EI, 10250);
		I915_WRITE(GEN6_RP_UP_THRESHOLD, 9225);

		/* Downclock if less than 75% busy over 32ms */
		I915_WRITE(GEN6_RP_DOWN_EI, 25000);
		I915_WRITE(GEN6_RP_DOWN_THRESHOLD, 18750);

		I915_WRITE(GEN6_RP_CONTROL,
			   GEN6_RP_MEDIA_TURBO |
			   GEN6_RP_MEDIA_HW_NORMAL_MODE |
			   GEN6_RP_MEDIA_IS_GFX |
			   GEN6_RP_ENABLE |
			   GEN6_RP_UP_BUSY_AVG |
			   GEN6_RP_DOWN_IDLE_AVG);
		break;

	case HIGH_POWER:
		/* Upclock if more than 85% busy over 10ms */
		I915_WRITE(GEN6_RP_UP_EI, 8000);
		I915_WRITE(GEN6_RP_UP_THRESHOLD, 6800);

		/* Downclock if less than 60% busy over 32ms */
		I915_WRITE(GEN6_RP_DOWN_EI, 25000);
		I915_WRITE(GEN6_RP_DOWN_THRESHOLD, 15000);

		I915_WRITE(GEN6_RP_CONTROL,
			   GEN6_RP_MEDIA_TURBO |
			   GEN6_RP_MEDIA_HW_NORMAL_MODE |
			   GEN6_RP_MEDIA_IS_GFX |
			   GEN6_RP_ENABLE |
			   GEN6_RP_UP_BUSY_AVG |
			   GEN6_RP_DOWN_IDLE_AVG);
		break;
	}

	dev_priv->rps.power = new_power;
	dev_priv->rps.last_adj = 0;
}

static u32 gen6_rps_pm_mask(struct drm_i915_private *dev_priv, u8 val)
{
	u32 mask = 0;

	if (val > dev_priv->rps.min_freq_softlimit)
		mask |= GEN6_PM_RP_DOWN_THRESHOLD | GEN6_PM_RP_DOWN_TIMEOUT;
	if (val < dev_priv->rps.max_freq_softlimit)
		mask |= GEN6_PM_RP_UP_THRESHOLD;

	/* IVB and SNB hard hangs on looping batchbuffer
	 * if GEN6_PM_UP_EI_EXPIRED is masked.
	 */
	if (INTEL_INFO(dev_priv->dev)->gen <= 7 && !IS_HASWELL(dev_priv->dev))
		mask |= GEN6_PM_RP_UP_EI_EXPIRED;

	if (IS_GEN8(dev_priv->dev))
		mask |= GEN8_PMINTR_REDIRECT_TO_NON_DISP;

	return ~mask;
}

/* gen6_set_rps is called to update the frequency request, but should also be
 * called when the range (min_delay and max_delay) is modified so that we can
 * update the GEN6_RP_INTERRUPT_LIMITS register accordingly. */
void gen6_set_rps(struct drm_device *dev, u8 val)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));
	WARN_ON(val > dev_priv->rps.max_freq_softlimit);
	WARN_ON(val < dev_priv->rps.min_freq_softlimit);

	/* min/max delay may still have been modified so be sure to
	 * write the limits value.
	 */
	if (val != dev_priv->rps.cur_freq) {
		gen6_set_rps_thresholds(dev_priv, val);

		if (IS_HASWELL(dev) || IS_BROADWELL(dev))
			I915_WRITE(GEN6_RPNSWREQ,
				   HSW_FREQUENCY(val));
		else
			I915_WRITE(GEN6_RPNSWREQ,
				   GEN6_FREQUENCY(val) |
				   GEN6_OFFSET(0) |
				   GEN6_AGGRESSIVE_TURBO);
	}

	/* Make sure we continue to get interrupts
	 * until we hit the minimum or maximum frequencies.
	 */
	I915_WRITE(GEN6_RP_INTERRUPT_LIMITS, gen6_rps_limits(dev_priv, val));
	I915_WRITE(GEN6_PMINTRMSK, gen6_rps_pm_mask(dev_priv, val));

	POSTING_READ(GEN6_RPNSWREQ);

	dev_priv->rps.cur_freq = val;
	trace_intel_gpu_freq_change(val * 50);
}

/* vlv_set_rps_idle: Set the frequency to Rpn if Gfx clocks are down
 *
 * * If Gfx is Idle, then
 * 1. Mask Turbo interrupts
 * 2. Bring up Gfx clock
 * 3. Change the freq to Rpn and wait till P-Unit updates freq
 * 4. Clear the Force GFX CLK ON bit so that Gfx can down
 * 5. Unmask Turbo interrupts
*/
static void vlv_set_rps_idle(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;

	/* Latest VLV doesn't need to force the gfx clock */
	if (dev->pdev->revision >= 0xd) {
		valleyview_set_rps(dev_priv->dev, dev_priv->rps.min_freq_softlimit);
		return;
	}

	/*
	 * When we are idle.  Drop to min voltage state.
	 */

	if (dev_priv->rps.cur_freq <= dev_priv->rps.min_freq_softlimit)
		return;

	/* Mask turbo interrupt so that they will not come in between */
	I915_WRITE(GEN6_PMINTRMSK, 0xffffffff);

	vlv_force_gfx_clock(dev_priv, true);

	dev_priv->rps.cur_freq = dev_priv->rps.min_freq_softlimit;

	vlv_punit_write(dev_priv, PUNIT_REG_GPU_FREQ_REQ,
					dev_priv->rps.min_freq_softlimit);

	if (wait_for(((vlv_punit_read(dev_priv, PUNIT_REG_GPU_FREQ_STS))
				& GENFREQSTATUS) == 0, 5))
		DRM_ERROR("timed out waiting for Punit\n");

	vlv_force_gfx_clock(dev_priv, false);

	I915_WRITE(GEN6_PMINTRMSK,
		   gen6_rps_pm_mask(dev_priv, dev_priv->rps.cur_freq));
}

void gen6_rps_idle(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;

	mutex_lock(&dev_priv->rps.hw_lock);
	if (dev_priv->rps.enabled) {
		if (IS_VALLEYVIEW(dev))
			vlv_set_rps_idle(dev_priv);
		else
			gen6_set_rps(dev_priv->dev, dev_priv->rps.min_freq_softlimit);
		dev_priv->rps.last_adj = 0;
	}
	mutex_unlock(&dev_priv->rps.hw_lock);
}

void gen6_rps_boost(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;

	mutex_lock(&dev_priv->rps.hw_lock);
	if (dev_priv->rps.enabled) {
		if (IS_VALLEYVIEW(dev))
			valleyview_set_rps(dev_priv->dev, dev_priv->rps.max_freq_softlimit);
		else
			gen6_set_rps(dev_priv->dev, dev_priv->rps.max_freq_softlimit);
		dev_priv->rps.last_adj = 0;
	}
	mutex_unlock(&dev_priv->rps.hw_lock);
}

void valleyview_set_rps(struct drm_device *dev, u8 val)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));
	WARN_ON(val > dev_priv->rps.max_freq_softlimit);
	WARN_ON(val < dev_priv->rps.min_freq_softlimit);

	DRM_DEBUG_DRIVER("GPU freq request from %d MHz (%u) to %d MHz (%u)\n",
			 vlv_gpu_freq(dev_priv, dev_priv->rps.cur_freq),
			 dev_priv->rps.cur_freq,
			 vlv_gpu_freq(dev_priv, val), val);

	if (val != dev_priv->rps.cur_freq)
		vlv_punit_write(dev_priv, PUNIT_REG_GPU_FREQ_REQ, val);

	I915_WRITE(GEN6_PMINTRMSK, gen6_rps_pm_mask(dev_priv, val));

	dev_priv->rps.cur_freq = val;
	trace_intel_gpu_freq_change(vlv_gpu_freq(dev_priv, val));
}

static void gen8_disable_rps_interrupts(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(GEN6_PMINTRMSK, ~GEN8_PMINTR_REDIRECT_TO_NON_DISP);
	I915_WRITE(GEN8_GT_IER(2), I915_READ(GEN8_GT_IER(2)) &
				   ~dev_priv->pm_rps_events);
	/* Complete PM interrupt masking here doesn't race with the rps work
	 * item again unmasking PM interrupts because that is using a different
	 * register (GEN8_GT_IMR(2)) to mask PM interrupts. The only risk is in
	 * leaving stale bits in GEN8_GT_IIR(2) and GEN8_GT_IMR(2) which
	 * gen8_enable_rps will clean up. */

	spin_lock_irq(&dev_priv->irq_lock);
	dev_priv->rps.pm_iir = 0;
	spin_unlock_irq(&dev_priv->irq_lock);

	I915_WRITE(GEN8_GT_IIR(2), dev_priv->pm_rps_events);
}

static void gen6_disable_rps_interrupts(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(GEN6_PMINTRMSK, 0xffffffff);
	I915_WRITE(GEN6_PMIER, I915_READ(GEN6_PMIER) &
				~dev_priv->pm_rps_events);
	/* Complete PM interrupt masking here doesn't race with the rps work
	 * item again unmasking PM interrupts because that is using a different
	 * register (PMIMR) to mask PM interrupts. The only risk is in leaving
	 * stale bits in PMIIR and PMIMR which gen6_enable_rps will clean up. */

	spin_lock_irq(&dev_priv->irq_lock);
	dev_priv->rps.pm_iir = 0;
	spin_unlock_irq(&dev_priv->irq_lock);

	I915_WRITE(GEN6_PMIIR, dev_priv->pm_rps_events);
}

static void gen6_disable_rps(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(GEN6_RC_CONTROL, 0);
	I915_WRITE(GEN6_RPNSWREQ, 1 << 31);

	if (IS_BROADWELL(dev))
		gen8_disable_rps_interrupts(dev);
	else
		gen6_disable_rps_interrupts(dev);
}

static void cherryview_disable_rps(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(GEN6_RC_CONTROL, 0);
}

static void valleyview_disable_rps(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(GEN6_RC_CONTROL, 0);

	gen6_disable_rps_interrupts(dev);
}

static void intel_print_rc6_info(struct drm_device *dev, u32 mode)
{
	if (IS_VALLEYVIEW(dev)) {
		if (mode & (GEN7_RC_CTL_TO_MODE | GEN6_RC_CTL_EI_MODE(1)))
			mode = GEN6_RC_CTL_RC6_ENABLE;
		else
			mode = 0;
	}
	DRM_INFO("Enabling RC6 states: RC6 %s, RC6p %s, RC6pp %s\n",
		 (mode & GEN6_RC_CTL_RC6_ENABLE) ? "on" : "off",
		 (mode & GEN6_RC_CTL_RC6p_ENABLE) ? "on" : "off",
		 (mode & GEN6_RC_CTL_RC6pp_ENABLE) ? "on" : "off");
}

static int sanitize_rc6_option(const struct drm_device *dev, int enable_rc6)
{
	/* No RC6 before Ironlake */
	if (INTEL_INFO(dev)->gen < 5)
		return 0;

	/* RC6 is only on Ironlake mobile not on desktop */
	if (INTEL_INFO(dev)->gen == 5 && !IS_IRONLAKE_M(dev))
		return 0;

	/* Respect the kernel parameter if it is set */
	if (enable_rc6 >= 0) {
		int mask;

		if (INTEL_INFO(dev)->gen == 6 || IS_IVYBRIDGE(dev))
			mask = INTEL_RC6_ENABLE | INTEL_RC6p_ENABLE |
			       INTEL_RC6pp_ENABLE;
		else
			mask = INTEL_RC6_ENABLE;

		if ((enable_rc6 & mask) != enable_rc6)
			DRM_INFO("Adjusting RC6 mask to %d (requested %d, valid %d)\n",
				 enable_rc6 & mask, enable_rc6, mask);

		return enable_rc6 & mask;
	}

	/* Disable RC6 on Ironlake */
	if (INTEL_INFO(dev)->gen == 5)
		return 0;

	if (IS_IVYBRIDGE(dev))
		return (INTEL_RC6_ENABLE | INTEL_RC6p_ENABLE);

	return INTEL_RC6_ENABLE;
}

int intel_enable_rc6(const struct drm_device *dev)
{
	return i915.enable_rc6;
}

static void gen8_enable_rps_interrupts(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	spin_lock_irq(&dev_priv->irq_lock);
	WARN_ON(dev_priv->rps.pm_iir);
	bdw_enable_pm_irq(dev_priv, dev_priv->pm_rps_events);
	I915_WRITE(GEN8_GT_IIR(2), dev_priv->pm_rps_events);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static void gen6_enable_rps_interrupts(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	spin_lock_irq(&dev_priv->irq_lock);
	WARN_ON(dev_priv->rps.pm_iir);
	snb_enable_pm_irq(dev_priv, dev_priv->pm_rps_events);
	I915_WRITE(GEN6_PMIIR, dev_priv->pm_rps_events);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static void parse_rp_state_cap(struct drm_i915_private *dev_priv, u32 rp_state_cap)
{
	/* All of these values are in units of 50MHz */
	dev_priv->rps.cur_freq		= 0;
	/* static values from HW: RP0 < RPe < RP1 < RPn (min_freq) */
	dev_priv->rps.rp1_freq		= (rp_state_cap >>  8) & 0xff;
	dev_priv->rps.rp0_freq		= (rp_state_cap >>  0) & 0xff;
	dev_priv->rps.min_freq		= (rp_state_cap >> 16) & 0xff;
	/* XXX: only BYT has a special efficient freq */
	dev_priv->rps.efficient_freq	= dev_priv->rps.rp1_freq;
	/* hw_max = RP0 until we check for overclocking */
	dev_priv->rps.max_freq		= dev_priv->rps.rp0_freq;

	/* Preserve min/max settings in case of re-init */
	if (dev_priv->rps.max_freq_softlimit == 0)
		dev_priv->rps.max_freq_softlimit = dev_priv->rps.max_freq;

	if (dev_priv->rps.min_freq_softlimit == 0)
		dev_priv->rps.min_freq_softlimit = dev_priv->rps.min_freq;
}

static void gen8_enable_rps(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring;
	uint32_t rc6_mask = 0, rp_state_cap;
	int unused;

	/* 1a: Software RC state - RC0 */
	I915_WRITE(GEN6_RC_STATE, 0);

	/* 1c & 1d: Get forcewake during program sequence. Although the driver
	 * hasn't enabled a state yet where we need forcewake, BIOS may have.*/
	gen6_gt_force_wake_get(dev_priv, FORCEWAKE_ALL);

	/* 2a: Disable RC states. */
	I915_WRITE(GEN6_RC_CONTROL, 0);

	rp_state_cap = I915_READ(GEN6_RP_STATE_CAP);
	parse_rp_state_cap(dev_priv, rp_state_cap);

	/* 2b: Program RC6 thresholds.*/
	I915_WRITE(GEN6_RC6_WAKE_RATE_LIMIT, 40 << 16);
	I915_WRITE(GEN6_RC_EVALUATION_INTERVAL, 125000); /* 12500 * 1280ns */
	I915_WRITE(GEN6_RC_IDLE_HYSTERSIS, 25); /* 25 * 1280ns */
	for_each_ring(ring, dev_priv, unused)
		I915_WRITE(RING_MAX_IDLE(ring->mmio_base), 10);
	I915_WRITE(GEN6_RC_SLEEP, 0);
	if (IS_BROADWELL(dev))
		I915_WRITE(GEN6_RC6_THRESHOLD, 625); /* 800us/1.28 for TO */
	else
		I915_WRITE(GEN6_RC6_THRESHOLD, 50000); /* 50/125ms per EI */

	/* 3: Enable RC6 */
	if (intel_enable_rc6(dev) & INTEL_RC6_ENABLE)
		rc6_mask = GEN6_RC_CTL_RC6_ENABLE;
	intel_print_rc6_info(dev, rc6_mask);
	if (IS_BROADWELL(dev))
		I915_WRITE(GEN6_RC_CONTROL, GEN6_RC_CTL_HW_ENABLE |
				GEN7_RC_CTL_TO_MODE |
				rc6_mask);
	else
		I915_WRITE(GEN6_RC_CONTROL, GEN6_RC_CTL_HW_ENABLE |
				GEN6_RC_CTL_EI_MODE(1) |
				rc6_mask);

	/* 4 Program defaults and thresholds for RPS*/
	I915_WRITE(GEN6_RPNSWREQ,
		   HSW_FREQUENCY(dev_priv->rps.rp1_freq));
	I915_WRITE(GEN6_RC_VIDEO_FREQ,
		   HSW_FREQUENCY(dev_priv->rps.rp1_freq));
	/* NB: Docs say 1s, and 1000000 - which aren't equivalent */
	I915_WRITE(GEN6_RP_DOWN_TIMEOUT, 100000000 / 128); /* 1 second timeout */

	/* Docs recommend 900MHz, and 300 MHz respectively */
	I915_WRITE(GEN6_RP_INTERRUPT_LIMITS,
		   dev_priv->rps.max_freq_softlimit << 24 |
		   dev_priv->rps.min_freq_softlimit << 16);

	I915_WRITE(GEN6_RP_UP_THRESHOLD, 7600000 / 128); /* 76ms busyness per EI, 90% */
	I915_WRITE(GEN6_RP_DOWN_THRESHOLD, 31300000 / 128); /* 313ms busyness per EI, 70%*/
	I915_WRITE(GEN6_RP_UP_EI, 66000); /* 84.48ms, XXX: random? */
	I915_WRITE(GEN6_RP_DOWN_EI, 350000); /* 448ms, XXX: random? */

	I915_WRITE(GEN6_RP_IDLE_HYSTERSIS, 10);

	/* 5: Enable RPS */
	I915_WRITE(GEN6_RP_CONTROL,
		   GEN6_RP_MEDIA_TURBO |
		   GEN6_RP_MEDIA_HW_NORMAL_MODE |
		   GEN6_RP_MEDIA_IS_GFX |
		   GEN6_RP_ENABLE |
		   GEN6_RP_UP_BUSY_AVG |
		   GEN6_RP_DOWN_IDLE_AVG);

	/* 6: Ring frequency + overclocking (our driver does this later */

	gen6_set_rps(dev, (I915_READ(GEN6_GT_PERF_STATUS) & 0xff00) >> 8);

	gen8_enable_rps_interrupts(dev);

	gen6_gt_force_wake_put(dev_priv, FORCEWAKE_ALL);
}

static void gen6_enable_rps(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring;
	u32 rp_state_cap;
	u32 gt_perf_status;
	u32 rc6vids, pcu_mbox = 0, rc6_mask = 0;
	u32 gtfifodbg;
	int rc6_mode;
	int i, ret;

	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));

	/* Here begins a magic sequence of register writes to enable
	 * auto-downclocking.
	 *
	 * Perhaps there might be some value in exposing these to
	 * userspace...
	 */
	I915_WRITE(GEN6_RC_STATE, 0);

	/* Clear the DBG now so we don't confuse earlier errors */
	if ((gtfifodbg = I915_READ(GTFIFODBG))) {
		DRM_ERROR("GT fifo had a previous error %x\n", gtfifodbg);
		I915_WRITE(GTFIFODBG, gtfifodbg);
	}

	gen6_gt_force_wake_get(dev_priv, FORCEWAKE_ALL);

	rp_state_cap = I915_READ(GEN6_RP_STATE_CAP);
	gt_perf_status = I915_READ(GEN6_GT_PERF_STATUS);

	parse_rp_state_cap(dev_priv, rp_state_cap);

	/* disable the counters and set deterministic thresholds */
	I915_WRITE(GEN6_RC_CONTROL, 0);

	I915_WRITE(GEN6_RC1_WAKE_RATE_LIMIT, 1000 << 16);
	I915_WRITE(GEN6_RC6_WAKE_RATE_LIMIT, 40 << 16 | 30);
	I915_WRITE(GEN6_RC6pp_WAKE_RATE_LIMIT, 30);
	I915_WRITE(GEN6_RC_EVALUATION_INTERVAL, 125000);
	I915_WRITE(GEN6_RC_IDLE_HYSTERSIS, 25);

	for_each_ring(ring, dev_priv, i)
		I915_WRITE(RING_MAX_IDLE(ring->mmio_base), 10);

	I915_WRITE(GEN6_RC_SLEEP, 0);
	I915_WRITE(GEN6_RC1e_THRESHOLD, 1000);
	if (IS_IVYBRIDGE(dev))
		I915_WRITE(GEN6_RC6_THRESHOLD, 125000);
	else
		I915_WRITE(GEN6_RC6_THRESHOLD, 50000);
	I915_WRITE(GEN6_RC6p_THRESHOLD, 150000);
	I915_WRITE(GEN6_RC6pp_THRESHOLD, 64000); /* unused */

	/* Check if we are enabling RC6 */
	rc6_mode = intel_enable_rc6(dev_priv->dev);
	if (rc6_mode & INTEL_RC6_ENABLE)
		rc6_mask |= GEN6_RC_CTL_RC6_ENABLE;

	/* We don't use those on Haswell */
	if (!IS_HASWELL(dev)) {
		if (rc6_mode & INTEL_RC6p_ENABLE)
			rc6_mask |= GEN6_RC_CTL_RC6p_ENABLE;

		if (rc6_mode & INTEL_RC6pp_ENABLE)
			rc6_mask |= GEN6_RC_CTL_RC6pp_ENABLE;
	}

	intel_print_rc6_info(dev, rc6_mask);

	I915_WRITE(GEN6_RC_CONTROL,
		   rc6_mask |
		   GEN6_RC_CTL_EI_MODE(1) |
		   GEN6_RC_CTL_HW_ENABLE);

	/* Power down if completely idle for over 50ms */
	I915_WRITE(GEN6_RP_DOWN_TIMEOUT, 50000);
	I915_WRITE(GEN6_RP_IDLE_HYSTERSIS, 10);

	ret = sandybridge_pcode_write(dev_priv, GEN6_PCODE_WRITE_MIN_FREQ_TABLE, 0);
	if (ret)
		DRM_DEBUG_DRIVER("Failed to set the min frequency\n");

	ret = sandybridge_pcode_read(dev_priv, GEN6_READ_OC_PARAMS, &pcu_mbox);
	if (!ret && (pcu_mbox & (1<<31))) { /* OC supported */
		DRM_DEBUG_DRIVER("Overclocking supported. Max: %dMHz, Overclock max: %dMHz\n",
				 (dev_priv->rps.max_freq_softlimit & 0xff) * 50,
				 (pcu_mbox & 0xff) * 50);
		dev_priv->rps.max_freq = pcu_mbox & 0xff;
	}

	dev_priv->rps.power = HIGH_POWER; /* force a reset */
	gen6_set_rps(dev_priv->dev, dev_priv->rps.min_freq_softlimit);

	gen6_enable_rps_interrupts(dev);

	rc6vids = 0;
	ret = sandybridge_pcode_read(dev_priv, GEN6_PCODE_READ_RC6VIDS, &rc6vids);
	if (IS_GEN6(dev) && ret) {
		DRM_DEBUG_DRIVER("Couldn't check for BIOS workaround\n");
	} else if (IS_GEN6(dev) && (GEN6_DECODE_RC6_VID(rc6vids & 0xff) < 450)) {
		DRM_DEBUG_DRIVER("You should update your BIOS. Correcting minimum rc6 voltage (%dmV->%dmV)\n",
			  GEN6_DECODE_RC6_VID(rc6vids & 0xff), 450);
		rc6vids &= 0xffff00;
		rc6vids |= GEN6_ENCODE_RC6_VID(450);
		ret = sandybridge_pcode_write(dev_priv, GEN6_PCODE_WRITE_RC6VIDS, rc6vids);
		if (ret)
			DRM_ERROR("Couldn't fix incorrect rc6 voltage\n");
	}

	gen6_gt_force_wake_put(dev_priv, FORCEWAKE_ALL);
}

static void __gen6_update_ring_freq(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int min_freq = 15;
	unsigned int gpu_freq;
	unsigned int max_ia_freq, min_ring_freq;
	int scaling_factor = 180;
	struct cpufreq_policy *policy;

	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));

	policy = cpufreq_cpu_get(0);
	if (policy) {
		max_ia_freq = policy->cpuinfo.max_freq;
		cpufreq_cpu_put(policy);
	} else {
		/*
		 * Default to measured freq if none found, PCU will ensure we
		 * don't go over
		 */
		max_ia_freq = tsc_khz;
	}

	/* Convert from kHz to MHz */
	max_ia_freq /= 1000;

	min_ring_freq = I915_READ(DCLK) & 0xf;
	/* convert DDR frequency from units of 266.6MHz to bandwidth */
	min_ring_freq = mult_frac(min_ring_freq, 8, 3);

	/*
	 * For each potential GPU frequency, load a ring frequency we'd like
	 * to use for memory access.  We do this by specifying the IA frequency
	 * the PCU should use as a reference to determine the ring frequency.
	 */
	for (gpu_freq = dev_priv->rps.max_freq_softlimit; gpu_freq >= dev_priv->rps.min_freq_softlimit;
	     gpu_freq--) {
		int diff = dev_priv->rps.max_freq_softlimit - gpu_freq;
		unsigned int ia_freq = 0, ring_freq = 0;

		if (INTEL_INFO(dev)->gen >= 8) {
			/* max(2 * GT, DDR). NB: GT is 50MHz units */
			ring_freq = max(min_ring_freq, gpu_freq);
		} else if (IS_HASWELL(dev)) {
			ring_freq = mult_frac(gpu_freq, 5, 4);
			ring_freq = max(min_ring_freq, ring_freq);
			/* leave ia_freq as the default, chosen by cpufreq */
		} else {
			/* On older processors, there is no separate ring
			 * clock domain, so in order to boost the bandwidth
			 * of the ring, we need to upclock the CPU (ia_freq).
			 *
			 * For GPU frequencies less than 750MHz,
			 * just use the lowest ring freq.
			 */
			if (gpu_freq < min_freq)
				ia_freq = 800;
			else
				ia_freq = max_ia_freq - ((diff * scaling_factor) / 2);
			ia_freq = DIV_ROUND_CLOSEST(ia_freq, 100);
		}

		sandybridge_pcode_write(dev_priv,
					GEN6_PCODE_WRITE_MIN_FREQ_TABLE,
					ia_freq << GEN6_PCODE_FREQ_IA_RATIO_SHIFT |
					ring_freq << GEN6_PCODE_FREQ_RING_RATIO_SHIFT |
					gpu_freq);
	}
}

void gen6_update_ring_freq(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (INTEL_INFO(dev)->gen < 6 || IS_VALLEYVIEW(dev))
		return;

	mutex_lock(&dev_priv->rps.hw_lock);
	__gen6_update_ring_freq(dev);
	mutex_unlock(&dev_priv->rps.hw_lock);
}

int cherryview_rps_max_freq(struct drm_i915_private *dev_priv)
{
	u32 val, rp0;

	val = vlv_punit_read(dev_priv, PUNIT_GPU_STATUS_REG);
	rp0 = (val >> PUNIT_GPU_STATUS_MAX_FREQ_SHIFT) & PUNIT_GPU_STATUS_MAX_FREQ_MASK;

	return rp0;
}

static int cherryview_rps_rpe_freq(struct drm_i915_private *dev_priv)
{
	u32 val, rpe;

	val = vlv_punit_read(dev_priv, PUNIT_GPU_DUTYCYCLE_REG);
	rpe = (val >> PUNIT_GPU_DUTYCYCLE_RPE_FREQ_SHIFT) & PUNIT_GPU_DUTYCYCLE_RPE_FREQ_MASK;

	return rpe;
}

int cherryview_rps_min_freq(struct drm_i915_private *dev_priv)
{
	u32 val, rpn;

	val = vlv_punit_read(dev_priv, PUNIT_GPU_STATUS_REG);
	rpn = (val >> PUNIT_GPU_STATIS_GFX_MIN_FREQ_SHIFT) & PUNIT_GPU_STATUS_GFX_MIN_FREQ_MASK;
	return rpn;
}

int valleyview_rps_max_freq(struct drm_i915_private *dev_priv)
{
	u32 val, rp0;

	val = vlv_nc_read(dev_priv, IOSF_NC_FB_GFX_FREQ_FUSE);

	rp0 = (val & FB_GFX_MAX_FREQ_FUSE_MASK) >> FB_GFX_MAX_FREQ_FUSE_SHIFT;
	/* Clamp to max */
	rp0 = min_t(u32, rp0, 0xea);

	return rp0;
}

static int valleyview_rps_rpe_freq(struct drm_i915_private *dev_priv)
{
	u32 val, rpe;

	val = vlv_nc_read(dev_priv, IOSF_NC_FB_GFX_FMAX_FUSE_LO);
	rpe = (val & FB_FMAX_VMIN_FREQ_LO_MASK) >> FB_FMAX_VMIN_FREQ_LO_SHIFT;
	val = vlv_nc_read(dev_priv, IOSF_NC_FB_GFX_FMAX_FUSE_HI);
	rpe |= (val & FB_FMAX_VMIN_FREQ_HI_MASK) << 5;

	return rpe;
}

int valleyview_rps_min_freq(struct drm_i915_private *dev_priv)
{
	return vlv_punit_read(dev_priv, PUNIT_REG_GPU_LFM) & 0xff;
}

/* Check that the pctx buffer wasn't move under us. */
static void valleyview_check_pctx(struct drm_i915_private *dev_priv)
{
	unsigned long pctx_addr = I915_READ(VLV_PCBR) & ~4095;

	WARN_ON(pctx_addr != dev_priv->mm.stolen_base +
			     dev_priv->vlv_pctx->stolen->start);
}


/* Check that the pcbr address is not empty. */
static void cherryview_check_pctx(struct drm_i915_private *dev_priv)
{
	unsigned long pctx_addr = I915_READ(VLV_PCBR) & ~4095;

	WARN_ON((pctx_addr >> VLV_PCBR_ADDR_SHIFT) == 0);
}

static void cherryview_setup_pctx(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long pctx_paddr, paddr;
	struct i915_gtt *gtt = &dev_priv->gtt;
	u32 pcbr;
	int pctx_size = 32*1024;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	pcbr = I915_READ(VLV_PCBR);
	if ((pcbr >> VLV_PCBR_ADDR_SHIFT) == 0) {
		paddr = (dev_priv->mm.stolen_base +
			 (gtt->stolen_size - pctx_size));

		pctx_paddr = (paddr & (~4095));
		I915_WRITE(VLV_PCBR, pctx_paddr);
	}
}

static void valleyview_setup_pctx(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *pctx;
	unsigned long pctx_paddr;
	u32 pcbr;
	int pctx_size = 24*1024;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	pcbr = I915_READ(VLV_PCBR);
	if (pcbr) {
		/* BIOS set it up already, grab the pre-alloc'd space */
		int pcbr_offset;

		pcbr_offset = (pcbr & (~4095)) - dev_priv->mm.stolen_base;
		pctx = i915_gem_object_create_stolen_for_preallocated(dev_priv->dev,
								      pcbr_offset,
								      I915_GTT_OFFSET_NONE,
								      pctx_size);
		goto out;
	}

	/*
	 * From the Gunit register HAS:
	 * The Gfx driver is expected to program this register and ensure
	 * proper allocation within Gfx stolen memory.  For example, this
	 * register should be programmed such than the PCBR range does not
	 * overlap with other ranges, such as the frame buffer, protected
	 * memory, or any other relevant ranges.
	 */
	pctx = i915_gem_object_create_stolen(dev, pctx_size);
	if (!pctx) {
		DRM_DEBUG("not enough stolen space for PCTX, disabling\n");
		return;
	}

	pctx_paddr = dev_priv->mm.stolen_base + pctx->stolen->start;
	I915_WRITE(VLV_PCBR, pctx_paddr);

out:
	dev_priv->vlv_pctx = pctx;
}

static void valleyview_cleanup_pctx(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (WARN_ON(!dev_priv->vlv_pctx))
		return;

	drm_gem_object_unreference(&dev_priv->vlv_pctx->base);
	dev_priv->vlv_pctx = NULL;
}

static void valleyview_init_gt_powersave(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	valleyview_setup_pctx(dev);

	mutex_lock(&dev_priv->rps.hw_lock);

	dev_priv->rps.max_freq = valleyview_rps_max_freq(dev_priv);
	dev_priv->rps.rp0_freq = dev_priv->rps.max_freq;
	DRM_DEBUG_DRIVER("max GPU freq: %d MHz (%u)\n",
			 vlv_gpu_freq(dev_priv, dev_priv->rps.max_freq),
			 dev_priv->rps.max_freq);

	dev_priv->rps.efficient_freq = valleyview_rps_rpe_freq(dev_priv);
	DRM_DEBUG_DRIVER("RPe GPU freq: %d MHz (%u)\n",
			 vlv_gpu_freq(dev_priv, dev_priv->rps.efficient_freq),
			 dev_priv->rps.efficient_freq);

	dev_priv->rps.min_freq = valleyview_rps_min_freq(dev_priv);
	DRM_DEBUG_DRIVER("min GPU freq: %d MHz (%u)\n",
			 vlv_gpu_freq(dev_priv, dev_priv->rps.min_freq),
			 dev_priv->rps.min_freq);

	/* Preserve min/max settings in case of re-init */
	if (dev_priv->rps.max_freq_softlimit == 0)
		dev_priv->rps.max_freq_softlimit = dev_priv->rps.max_freq;

	if (dev_priv->rps.min_freq_softlimit == 0)
		dev_priv->rps.min_freq_softlimit = dev_priv->rps.min_freq;

	mutex_unlock(&dev_priv->rps.hw_lock);
}

static void cherryview_init_gt_powersave(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	cherryview_setup_pctx(dev);

	mutex_lock(&dev_priv->rps.hw_lock);

	dev_priv->rps.max_freq = cherryview_rps_max_freq(dev_priv);
	dev_priv->rps.rp0_freq = dev_priv->rps.max_freq;
	DRM_DEBUG_DRIVER("max GPU freq: %d MHz (%u)\n",
			 vlv_gpu_freq(dev_priv, dev_priv->rps.max_freq),
			 dev_priv->rps.max_freq);

	dev_priv->rps.efficient_freq = cherryview_rps_rpe_freq(dev_priv);
	DRM_DEBUG_DRIVER("RPe GPU freq: %d MHz (%u)\n",
			 vlv_gpu_freq(dev_priv, dev_priv->rps.efficient_freq),
			 dev_priv->rps.efficient_freq);

	dev_priv->rps.min_freq = cherryview_rps_min_freq(dev_priv);
	DRM_DEBUG_DRIVER("min GPU freq: %d MHz (%u)\n",
			 vlv_gpu_freq(dev_priv, dev_priv->rps.min_freq),
			 dev_priv->rps.min_freq);

	/* Preserve min/max settings in case of re-init */
	if (dev_priv->rps.max_freq_softlimit == 0)
		dev_priv->rps.max_freq_softlimit = dev_priv->rps.max_freq;

	if (dev_priv->rps.min_freq_softlimit == 0)
		dev_priv->rps.min_freq_softlimit = dev_priv->rps.min_freq;

	mutex_unlock(&dev_priv->rps.hw_lock);
}

static void valleyview_cleanup_gt_powersave(struct drm_device *dev)
{
	valleyview_cleanup_pctx(dev);
}

static void cherryview_enable_rps(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring;
	u32 gtfifodbg, val, rc6_mode = 0, pcbr;
	int i;

	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));

	gtfifodbg = I915_READ(GTFIFODBG);
	if (gtfifodbg) {
		DRM_DEBUG_DRIVER("GT fifo had a previous error %x\n",
				 gtfifodbg);
		I915_WRITE(GTFIFODBG, gtfifodbg);
	}

	cherryview_check_pctx(dev_priv);

	/* 1a & 1b: Get forcewake during program sequence. Although the driver
	 * hasn't enabled a state yet where we need forcewake, BIOS may have.*/
	gen6_gt_force_wake_get(dev_priv, FORCEWAKE_ALL);

	/* 2a: Program RC6 thresholds.*/
	I915_WRITE(GEN6_RC6_WAKE_RATE_LIMIT, 40 << 16);
	I915_WRITE(GEN6_RC_EVALUATION_INTERVAL, 125000); /* 12500 * 1280ns */
	I915_WRITE(GEN6_RC_IDLE_HYSTERSIS, 25); /* 25 * 1280ns */

	for_each_ring(ring, dev_priv, i)
		I915_WRITE(RING_MAX_IDLE(ring->mmio_base), 10);
	I915_WRITE(GEN6_RC_SLEEP, 0);

	I915_WRITE(GEN6_RC6_THRESHOLD, 50000); /* 50/125ms per EI */

	/* allows RC6 residency counter to work */
	I915_WRITE(VLV_COUNTER_CONTROL,
		   _MASKED_BIT_ENABLE(VLV_COUNT_RANGE_HIGH |
				      VLV_MEDIA_RC6_COUNT_EN |
				      VLV_RENDER_RC6_COUNT_EN));

	/* For now we assume BIOS is allocating and populating the PCBR  */
	pcbr = I915_READ(VLV_PCBR);

	DRM_DEBUG_DRIVER("PCBR offset : 0x%x\n", pcbr);

	/* 3: Enable RC6 */
	if ((intel_enable_rc6(dev) & INTEL_RC6_ENABLE) &&
						(pcbr >> VLV_PCBR_ADDR_SHIFT))
		rc6_mode = GEN6_RC_CTL_EI_MODE(1);

	I915_WRITE(GEN6_RC_CONTROL, rc6_mode);

	/* 4 Program defaults and thresholds for RPS*/
	I915_WRITE(GEN6_RP_UP_THRESHOLD, 59400);
	I915_WRITE(GEN6_RP_DOWN_THRESHOLD, 245000);
	I915_WRITE(GEN6_RP_UP_EI, 66000);
	I915_WRITE(GEN6_RP_DOWN_EI, 350000);

	I915_WRITE(GEN6_RP_IDLE_HYSTERSIS, 10);

	/* WaDisablePwrmtrEvent:chv (pre-production hw) */
	I915_WRITE(0xA80C, I915_READ(0xA80C) & 0x00ffffff);
	I915_WRITE(0xA810, I915_READ(0xA810) & 0xffffff00);

	/* 5: Enable RPS */
	I915_WRITE(GEN6_RP_CONTROL,
		   GEN6_RP_MEDIA_HW_NORMAL_MODE |
		   GEN6_RP_MEDIA_IS_GFX | /* WaSetMaskForGfxBusyness:chv (pre-production hw ?) */
		   GEN6_RP_ENABLE |
		   GEN6_RP_UP_BUSY_AVG |
		   GEN6_RP_DOWN_IDLE_AVG);

	val = vlv_punit_read(dev_priv, PUNIT_REG_GPU_FREQ_STS);

	DRM_DEBUG_DRIVER("GPLL enabled? %s\n", val & 0x10 ? "yes" : "no");
	DRM_DEBUG_DRIVER("GPU status: 0x%08x\n", val);

	dev_priv->rps.cur_freq = (val >> 8) & 0xff;
	DRM_DEBUG_DRIVER("current GPU freq: %d MHz (%u)\n",
			 vlv_gpu_freq(dev_priv, dev_priv->rps.cur_freq),
			 dev_priv->rps.cur_freq);

	DRM_DEBUG_DRIVER("setting GPU freq to %d MHz (%u)\n",
			 vlv_gpu_freq(dev_priv, dev_priv->rps.efficient_freq),
			 dev_priv->rps.efficient_freq);

	valleyview_set_rps(dev_priv->dev, dev_priv->rps.efficient_freq);

	gen6_gt_force_wake_put(dev_priv, FORCEWAKE_ALL);
}

static void valleyview_enable_rps(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring;
	u32 gtfifodbg, val, rc6_mode = 0;
	int i;

	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));

	valleyview_check_pctx(dev_priv);

	if ((gtfifodbg = I915_READ(GTFIFODBG))) {
		DRM_DEBUG_DRIVER("GT fifo had a previous error %x\n",
				 gtfifodbg);
		I915_WRITE(GTFIFODBG, gtfifodbg);
	}

	/* If VLV, Forcewake all wells, else re-direct to regular path */
	gen6_gt_force_wake_get(dev_priv, FORCEWAKE_ALL);

	I915_WRITE(GEN6_RP_UP_THRESHOLD, 59400);
	I915_WRITE(GEN6_RP_DOWN_THRESHOLD, 245000);
	I915_WRITE(GEN6_RP_UP_EI, 66000);
	I915_WRITE(GEN6_RP_DOWN_EI, 350000);

	I915_WRITE(GEN6_RP_IDLE_HYSTERSIS, 10);

	I915_WRITE(GEN6_RP_CONTROL,
		   GEN6_RP_MEDIA_TURBO |
		   GEN6_RP_MEDIA_HW_NORMAL_MODE |
		   GEN6_RP_MEDIA_IS_GFX |
		   GEN6_RP_ENABLE |
		   GEN6_RP_UP_BUSY_AVG |
		   GEN6_RP_DOWN_IDLE_CONT);

	I915_WRITE(GEN6_RC6_WAKE_RATE_LIMIT, 0x00280000);
	I915_WRITE(GEN6_RC_EVALUATION_INTERVAL, 125000);
	I915_WRITE(GEN6_RC_IDLE_HYSTERSIS, 25);

	for_each_ring(ring, dev_priv, i)
		I915_WRITE(RING_MAX_IDLE(ring->mmio_base), 10);

	I915_WRITE(GEN6_RC6_THRESHOLD, 0x557);

	/* allows RC6 residency counter to work */
	I915_WRITE(VLV_COUNTER_CONTROL,
		   _MASKED_BIT_ENABLE(VLV_COUNT_RANGE_HIGH |
				      VLV_MEDIA_RC6_COUNT_EN |
				      VLV_RENDER_RC6_COUNT_EN));
	if (intel_enable_rc6(dev) & INTEL_RC6_ENABLE)
		rc6_mode = GEN7_RC_CTL_TO_MODE | VLV_RC_CTL_CTX_RST_PARALLEL;

	intel_print_rc6_info(dev, rc6_mode);

	I915_WRITE(GEN6_RC_CONTROL, rc6_mode);

	val = vlv_punit_read(dev_priv, PUNIT_REG_GPU_FREQ_STS);

	DRM_DEBUG_DRIVER("GPLL enabled? %s\n", val & 0x10 ? "yes" : "no");
	DRM_DEBUG_DRIVER("GPU status: 0x%08x\n", val);

	dev_priv->rps.cur_freq = (val >> 8) & 0xff;
	DRM_DEBUG_DRIVER("current GPU freq: %d MHz (%u)\n",
			 vlv_gpu_freq(dev_priv, dev_priv->rps.cur_freq),
			 dev_priv->rps.cur_freq);

	DRM_DEBUG_DRIVER("setting GPU freq to %d MHz (%u)\n",
			 vlv_gpu_freq(dev_priv, dev_priv->rps.efficient_freq),
			 dev_priv->rps.efficient_freq);

	valleyview_set_rps(dev_priv->dev, dev_priv->rps.efficient_freq);

	gen6_enable_rps_interrupts(dev);

	gen6_gt_force_wake_put(dev_priv, FORCEWAKE_ALL);
}

void ironlake_teardown_rc6(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->ips.renderctx) {
		i915_gem_object_ggtt_unpin(dev_priv->ips.renderctx);
		drm_gem_object_unreference(&dev_priv->ips.renderctx->base);
		dev_priv->ips.renderctx = NULL;
	}

	if (dev_priv->ips.pwrctx) {
		i915_gem_object_ggtt_unpin(dev_priv->ips.pwrctx);
		drm_gem_object_unreference(&dev_priv->ips.pwrctx->base);
		dev_priv->ips.pwrctx = NULL;
	}
}

static void ironlake_disable_rc6(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (I915_READ(PWRCTXA)) {
		/* Wake the GPU, prevent RC6, then restore RSTDBYCTL */
		I915_WRITE(RSTDBYCTL, I915_READ(RSTDBYCTL) | RCX_SW_EXIT);
		wait_for(((I915_READ(RSTDBYCTL) & RSX_STATUS_MASK) == RSX_STATUS_ON),
			 50);

		I915_WRITE(PWRCTXA, 0);
		POSTING_READ(PWRCTXA);

		I915_WRITE(RSTDBYCTL, I915_READ(RSTDBYCTL) & ~RCX_SW_EXIT);
		POSTING_READ(RSTDBYCTL);
	}
}

static int ironlake_setup_rc6(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->ips.renderctx == NULL)
		dev_priv->ips.renderctx = intel_alloc_context_page(dev);
	if (!dev_priv->ips.renderctx)
		return -ENOMEM;

	if (dev_priv->ips.pwrctx == NULL)
		dev_priv->ips.pwrctx = intel_alloc_context_page(dev);
	if (!dev_priv->ips.pwrctx) {
		ironlake_teardown_rc6(dev);
		return -ENOMEM;
	}

	return 0;
}

static void ironlake_enable_rc6(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[RCS];
	bool was_interruptible;
	int ret;

	/* rc6 disabled by default due to repeated reports of hanging during
	 * boot and resume.
	 */
	if (!intel_enable_rc6(dev))
		return;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	ret = ironlake_setup_rc6(dev);
	if (ret)
		return;

	was_interruptible = dev_priv->mm.interruptible;
	dev_priv->mm.interruptible = false;

	/*
	 * GPU can automatically power down the render unit if given a page
	 * to save state.
	 */
	ret = intel_ring_begin(ring, 6);
	if (ret) {
		ironlake_teardown_rc6(dev);
		dev_priv->mm.interruptible = was_interruptible;
		return;
	}

	intel_ring_emit(ring, MI_SUSPEND_FLUSH | MI_SUSPEND_FLUSH_EN);
	intel_ring_emit(ring, MI_SET_CONTEXT);
	intel_ring_emit(ring, i915_gem_obj_ggtt_offset(dev_priv->ips.renderctx) |
			MI_MM_SPACE_GTT |
			MI_SAVE_EXT_STATE_EN |
			MI_RESTORE_EXT_STATE_EN |
			MI_RESTORE_INHIBIT);
	intel_ring_emit(ring, MI_SUSPEND_FLUSH);
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_emit(ring, MI_FLUSH);
	intel_ring_advance(ring);

	/*
	 * Wait for the command parser to advance past MI_SET_CONTEXT. The HW
	 * does an implicit flush, combined with MI_FLUSH above, it should be
	 * safe to assume that renderctx is valid
	 */
	ret = intel_ring_idle(ring);
	dev_priv->mm.interruptible = was_interruptible;
	if (ret) {
		DRM_ERROR("failed to enable ironlake power savings\n");
		ironlake_teardown_rc6(dev);
		return;
	}

	I915_WRITE(PWRCTXA, i915_gem_obj_ggtt_offset(dev_priv->ips.pwrctx) | PWRCTX_EN);
	I915_WRITE(RSTDBYCTL, I915_READ(RSTDBYCTL) & ~RCX_SW_EXIT);

	intel_print_rc6_info(dev, GEN6_RC_CTL_RC6_ENABLE);
}

static unsigned long intel_pxfreq(u32 vidfreq)
{
	unsigned long freq;
	int div = (vidfreq & 0x3f0000) >> 16;
	int post = (vidfreq & 0x3000) >> 12;
	int pre = (vidfreq & 0x7);

	if (!pre)
		return 0;

	freq = ((div * 133333) / ((1<<post) * pre));

	return freq;
}

static const struct cparams {
	u16 i;
	u16 t;
	u16 m;
	u16 c;
} cparams[] = {
	{ 1, 1333, 301, 28664 },
	{ 1, 1066, 294, 24460 },
	{ 1, 800, 294, 25192 },
	{ 0, 1333, 276, 27605 },
	{ 0, 1066, 276, 27605 },
	{ 0, 800, 231, 23784 },
};

static unsigned long __i915_chipset_val(struct drm_i915_private *dev_priv)
{
	u64 total_count, diff, ret;
	u32 count1, count2, count3, m = 0, c = 0;
	unsigned long now = jiffies_to_msecs(jiffies), diff1;
	int i;

	assert_spin_locked(&mchdev_lock);

	diff1 = now - dev_priv->ips.last_time1;

	/* Prevent division-by-zero if we are asking too fast.
	 * Also, we don't get interesting results if we are polling
	 * faster than once in 10ms, so just return the saved value
	 * in such cases.
	 */
	if (diff1 <= 10)
		return dev_priv->ips.chipset_power;

	count1 = I915_READ(DMIEC);
	count2 = I915_READ(DDREC);
	count3 = I915_READ(CSIEC);

	total_count = count1 + count2 + count3;

	/* FIXME: handle per-counter overflow */
	if (total_count < dev_priv->ips.last_count1) {
		diff = ~0UL - dev_priv->ips.last_count1;
		diff += total_count;
	} else {
		diff = total_count - dev_priv->ips.last_count1;
	}

	for (i = 0; i < ARRAY_SIZE(cparams); i++) {
		if (cparams[i].i == dev_priv->ips.c_m &&
		    cparams[i].t == dev_priv->ips.r_t) {
			m = cparams[i].m;
			c = cparams[i].c;
			break;
		}
	}

	diff = div_u64(diff, diff1);
	ret = ((m * diff) + c);
	ret = div_u64(ret, 10);

	dev_priv->ips.last_count1 = total_count;
	dev_priv->ips.last_time1 = now;

	dev_priv->ips.chipset_power = ret;

	return ret;
}

unsigned long i915_chipset_val(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	unsigned long val;

	if (INTEL_INFO(dev)->gen != 5)
		return 0;

	spin_lock_irq(&mchdev_lock);

	val = __i915_chipset_val(dev_priv);

	spin_unlock_irq(&mchdev_lock);

	return val;
}

unsigned long i915_mch_val(struct drm_i915_private *dev_priv)
{
	unsigned long m, x, b;
	u32 tsfs;

	tsfs = I915_READ(TSFS);

	m = ((tsfs & TSFS_SLOPE_MASK) >> TSFS_SLOPE_SHIFT);
	x = I915_READ8(TR1);

	b = tsfs & TSFS_INTR_MASK;

	return ((m * x) / 127) - b;
}

static u16 pvid_to_extvid(struct drm_i915_private *dev_priv, u8 pxvid)
{
	struct drm_device *dev = dev_priv->dev;
	static const struct v_table {
		u16 vd; /* in .1 mil */
		u16 vm; /* in .1 mil */
	} v_table[] = {
		{ 0, 0, },
		{ 375, 0, },
		{ 500, 0, },
		{ 625, 0, },
		{ 750, 0, },
		{ 875, 0, },
		{ 1000, 0, },
		{ 1125, 0, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4125, 3000, },
		{ 4250, 3125, },
		{ 4375, 3250, },
		{ 4500, 3375, },
		{ 4625, 3500, },
		{ 4750, 3625, },
		{ 4875, 3750, },
		{ 5000, 3875, },
		{ 5125, 4000, },
		{ 5250, 4125, },
		{ 5375, 4250, },
		{ 5500, 4375, },
		{ 5625, 4500, },
		{ 5750, 4625, },
		{ 5875, 4750, },
		{ 6000, 4875, },
		{ 6125, 5000, },
		{ 6250, 5125, },
		{ 6375, 5250, },
		{ 6500, 5375, },
		{ 6625, 5500, },
		{ 6750, 5625, },
		{ 6875, 5750, },
		{ 7000, 5875, },
		{ 7125, 6000, },
		{ 7250, 6125, },
		{ 7375, 6250, },
		{ 7500, 6375, },
		{ 7625, 6500, },
		{ 7750, 6625, },
		{ 7875, 6750, },
		{ 8000, 6875, },
		{ 8125, 7000, },
		{ 8250, 7125, },
		{ 8375, 7250, },
		{ 8500, 7375, },
		{ 8625, 7500, },
		{ 8750, 7625, },
		{ 8875, 7750, },
		{ 9000, 7875, },
		{ 9125, 8000, },
		{ 9250, 8125, },
		{ 9375, 8250, },
		{ 9500, 8375, },
		{ 9625, 8500, },
		{ 9750, 8625, },
		{ 9875, 8750, },
		{ 10000, 8875, },
		{ 10125, 9000, },
		{ 10250, 9125, },
		{ 10375, 9250, },
		{ 10500, 9375, },
		{ 10625, 9500, },
		{ 10750, 9625, },
		{ 10875, 9750, },
		{ 11000, 9875, },
		{ 11125, 10000, },
		{ 11250, 10125, },
		{ 11375, 10250, },
		{ 11500, 10375, },
		{ 11625, 10500, },
		{ 11750, 10625, },
		{ 11875, 10750, },
		{ 12000, 10875, },
		{ 12125, 11000, },
		{ 12250, 11125, },
		{ 12375, 11250, },
		{ 12500, 11375, },
		{ 12625, 11500, },
		{ 12750, 11625, },
		{ 12875, 11750, },
		{ 13000, 11875, },
		{ 13125, 12000, },
		{ 13250, 12125, },
		{ 13375, 12250, },
		{ 13500, 12375, },
		{ 13625, 12500, },
		{ 13750, 12625, },
		{ 13875, 12750, },
		{ 14000, 12875, },
		{ 14125, 13000, },
		{ 14250, 13125, },
		{ 14375, 13250, },
		{ 14500, 13375, },
		{ 14625, 13500, },
		{ 14750, 13625, },
		{ 14875, 13750, },
		{ 15000, 13875, },
		{ 15125, 14000, },
		{ 15250, 14125, },
		{ 15375, 14250, },
		{ 15500, 14375, },
		{ 15625, 14500, },
		{ 15750, 14625, },
		{ 15875, 14750, },
		{ 16000, 14875, },
		{ 16125, 15000, },
	};
	if (INTEL_INFO(dev)->is_mobile)
		return v_table[pxvid].vm;
	else
		return v_table[pxvid].vd;
}

static void __i915_update_gfx_val(struct drm_i915_private *dev_priv)
{
	struct timespec now, diff1;
	u64 diff;
	unsigned long diffms;
	u32 count;

	assert_spin_locked(&mchdev_lock);

	getrawmonotonic(&now);
	diff1 = timespec_sub(now, dev_priv->ips.last_time2);

	/* Don't divide by 0 */
	diffms = diff1.tv_sec * 1000 + diff1.tv_nsec / 1000000;
	if (!diffms)
		return;

	count = I915_READ(GFXEC);

	if (count < dev_priv->ips.last_count2) {
		diff = ~0UL - dev_priv->ips.last_count2;
		diff += count;
	} else {
		diff = count - dev_priv->ips.last_count2;
	}

	dev_priv->ips.last_count2 = count;
	dev_priv->ips.last_time2 = now;

	/* More magic constants... */
	diff = diff * 1181;
	diff = div_u64(diff, diffms * 10);
	dev_priv->ips.gfx_power = diff;
}

void i915_update_gfx_val(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;

	if (INTEL_INFO(dev)->gen != 5)
		return;

	spin_lock_irq(&mchdev_lock);

	__i915_update_gfx_val(dev_priv);

	spin_unlock_irq(&mchdev_lock);
}

static unsigned long __i915_gfx_val(struct drm_i915_private *dev_priv)
{
	unsigned long t, corr, state1, corr2, state2;
	u32 pxvid, ext_v;

	assert_spin_locked(&mchdev_lock);

	pxvid = I915_READ(PXVFREQ_BASE + (dev_priv->rps.cur_freq * 4));
	pxvid = (pxvid >> 24) & 0x7f;
	ext_v = pvid_to_extvid(dev_priv, pxvid);

	state1 = ext_v;

	t = i915_mch_val(dev_priv);

	/* Revel in the empirically derived constants */

	/* Correction factor in 1/100000 units */
	if (t > 80)
		corr = ((t * 2349) + 135940);
	else if (t >= 50)
		corr = ((t * 964) + 29317);
	else /* < 50 */
		corr = ((t * 301) + 1004);

	corr = corr * ((150142 * state1) / 10000 - 78642);
	corr /= 100000;
	corr2 = (corr * dev_priv->ips.corr);

	state2 = (corr2 * state1) / 10000;
	state2 /= 100; /* convert to mW */

	__i915_update_gfx_val(dev_priv);

	return dev_priv->ips.gfx_power + state2;
}

unsigned long i915_gfx_val(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	unsigned long val;

	if (INTEL_INFO(dev)->gen != 5)
		return 0;

	spin_lock_irq(&mchdev_lock);

	val = __i915_gfx_val(dev_priv);

	spin_unlock_irq(&mchdev_lock);

	return val;
}

/**
 * i915_read_mch_val - return value for IPS use
 *
 * Calculate and return a value for the IPS driver to use when deciding whether
 * we have thermal and power headroom to increase CPU or GPU power budget.
 */
unsigned long i915_read_mch_val(void)
{
	struct drm_i915_private *dev_priv;
	unsigned long chipset_val, graphics_val, ret = 0;

	spin_lock_irq(&mchdev_lock);
	if (!i915_mch_dev)
		goto out_unlock;
	dev_priv = i915_mch_dev;

	chipset_val = __i915_chipset_val(dev_priv);
	graphics_val = __i915_gfx_val(dev_priv);

	ret = chipset_val + graphics_val;

out_unlock:
	spin_unlock_irq(&mchdev_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(i915_read_mch_val);

/**
 * i915_gpu_raise - raise GPU frequency limit
 *
 * Raise the limit; IPS indicates we have thermal headroom.
 */
bool i915_gpu_raise(void)
{
	struct drm_i915_private *dev_priv;
	bool ret = true;

	spin_lock_irq(&mchdev_lock);
	if (!i915_mch_dev) {
		ret = false;
		goto out_unlock;
	}
	dev_priv = i915_mch_dev;

	if (dev_priv->ips.max_delay > dev_priv->ips.fmax)
		dev_priv->ips.max_delay--;

out_unlock:
	spin_unlock_irq(&mchdev_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(i915_gpu_raise);

/**
 * i915_gpu_lower - lower GPU frequency limit
 *
 * IPS indicates we're close to a thermal limit, so throttle back the GPU
 * frequency maximum.
 */
bool i915_gpu_lower(void)
{
	struct drm_i915_private *dev_priv;
	bool ret = true;

	spin_lock_irq(&mchdev_lock);
	if (!i915_mch_dev) {
		ret = false;
		goto out_unlock;
	}
	dev_priv = i915_mch_dev;

	if (dev_priv->ips.max_delay < dev_priv->ips.min_delay)
		dev_priv->ips.max_delay++;

out_unlock:
	spin_unlock_irq(&mchdev_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(i915_gpu_lower);

/**
 * i915_gpu_busy - indicate GPU business to IPS
 *
 * Tell the IPS driver whether or not the GPU is busy.
 */
bool i915_gpu_busy(void)
{
	struct drm_i915_private *dev_priv;
	struct intel_engine_cs *ring;
	bool ret = false;
	int i;

	spin_lock_irq(&mchdev_lock);
	if (!i915_mch_dev)
		goto out_unlock;
	dev_priv = i915_mch_dev;

	for_each_ring(ring, dev_priv, i)
		ret |= !list_empty(&ring->request_list);

out_unlock:
	spin_unlock_irq(&mchdev_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(i915_gpu_busy);

/**
 * i915_gpu_turbo_disable - disable graphics turbo
 *
 * Disable graphics turbo by resetting the max frequency and setting the
 * current frequency to the default.
 */
bool i915_gpu_turbo_disable(void)
{
	struct drm_i915_private *dev_priv;
	bool ret = true;

	spin_lock_irq(&mchdev_lock);
	if (!i915_mch_dev) {
		ret = false;
		goto out_unlock;
	}
	dev_priv = i915_mch_dev;

	dev_priv->ips.max_delay = dev_priv->ips.fstart;

	if (!ironlake_set_drps(dev_priv->dev, dev_priv->ips.fstart))
		ret = false;

out_unlock:
	spin_unlock_irq(&mchdev_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(i915_gpu_turbo_disable);

/**
 * Tells the intel_ips driver that the i915 driver is now loaded, if
 * IPS got loaded first.
 *
 * This awkward dance is so that neither module has to depend on the
 * other in order for IPS to do the appropriate communication of
 * GPU turbo limits to i915.
 */
static void
ips_ping_for_i915_load(void)
{
	void (*link)(void);

	link = symbol_get(ips_link_to_i915_driver);
	if (link) {
		link();
		symbol_put(ips_link_to_i915_driver);
	}
}

void intel_gpu_ips_init(struct drm_i915_private *dev_priv)
{
	/* We only register the i915 ips part with intel-ips once everything is
	 * set up, to avoid intel-ips sneaking in and reading bogus values. */
	spin_lock_irq(&mchdev_lock);
	i915_mch_dev = dev_priv;
	spin_unlock_irq(&mchdev_lock);

	ips_ping_for_i915_load();
}

void intel_gpu_ips_teardown(void)
{
	spin_lock_irq(&mchdev_lock);
	i915_mch_dev = NULL;
	spin_unlock_irq(&mchdev_lock);
}

static void intel_init_emon(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 lcfuse;
	u8 pxw[16];
	int i;

	/* Disable to program */
	I915_WRITE(ECR, 0);
	POSTING_READ(ECR);

	/* Program energy weights for various events */
	I915_WRITE(SDEW, 0x15040d00);
	I915_WRITE(CSIEW0, 0x007f0000);
	I915_WRITE(CSIEW1, 0x1e220004);
	I915_WRITE(CSIEW2, 0x04000004);

	for (i = 0; i < 5; i++)
		I915_WRITE(PEW + (i * 4), 0);
	for (i = 0; i < 3; i++)
		I915_WRITE(DEW + (i * 4), 0);

	/* Program P-state weights to account for frequency power adjustment */
	for (i = 0; i < 16; i++) {
		u32 pxvidfreq = I915_READ(PXVFREQ_BASE + (i * 4));
		unsigned long freq = intel_pxfreq(pxvidfreq);
		unsigned long vid = (pxvidfreq & PXVFREQ_PX_MASK) >>
			PXVFREQ_PX_SHIFT;
		unsigned long val;

		val = vid * vid;
		val *= (freq / 1000);
		val *= 255;
		val /= (127*127*900);
		if (val > 0xff)
			DRM_ERROR("bad pxval: %ld\n", val);
		pxw[i] = val;
	}
	/* Render standby states get 0 weight */
	pxw[14] = 0;
	pxw[15] = 0;

	for (i = 0; i < 4; i++) {
		u32 val = (pxw[i*4] << 24) | (pxw[(i*4)+1] << 16) |
			(pxw[(i*4)+2] << 8) | (pxw[(i*4)+3]);
		I915_WRITE(PXW + (i * 4), val);
	}

	/* Adjust magic regs to magic values (more experimental results) */
	I915_WRITE(OGW0, 0);
	I915_WRITE(OGW1, 0);
	I915_WRITE(EG0, 0x00007f00);
	I915_WRITE(EG1, 0x0000000e);
	I915_WRITE(EG2, 0x000e0000);
	I915_WRITE(EG3, 0x68000300);
	I915_WRITE(EG4, 0x42000000);
	I915_WRITE(EG5, 0x00140031);
	I915_WRITE(EG6, 0);
	I915_WRITE(EG7, 0);

	for (i = 0; i < 8; i++)
		I915_WRITE(PXWL + (i * 4), 0);

	/* Enable PMON + select events */
	I915_WRITE(ECR, 0x80000019);

	lcfuse = I915_READ(LCFUSE02);

	dev_priv->ips.corr = (lcfuse & LCFUSE_HIV_MASK);
}

void intel_init_gt_powersave(struct drm_device *dev)
{
	i915.enable_rc6 = sanitize_rc6_option(dev, i915.enable_rc6);

	if (IS_CHERRYVIEW(dev))
		cherryview_init_gt_powersave(dev);
	else if (IS_VALLEYVIEW(dev))
		valleyview_init_gt_powersave(dev);
}

void intel_cleanup_gt_powersave(struct drm_device *dev)
{
	if (IS_CHERRYVIEW(dev))
		return;
	else if (IS_VALLEYVIEW(dev))
		valleyview_cleanup_gt_powersave(dev);
}

/**
 * intel_suspend_gt_powersave - suspend PM work and helper threads
 * @dev: drm device
 *
 * We don't want to disable RC6 or other features here, we just want
 * to make sure any work we've queued has finished and won't bother
 * us while we're suspended.
 */
void intel_suspend_gt_powersave(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* Interrupts should be disabled already to avoid re-arming. */
	WARN_ON(dev->irq_enabled && !dev_priv->pm.irqs_disabled);

	flush_delayed_work(&dev_priv->rps.delayed_resume_work);

	cancel_work_sync(&dev_priv->rps.work);
}

void intel_disable_gt_powersave(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* Interrupts should be disabled already to avoid re-arming. */
	WARN_ON(dev->irq_enabled && !dev_priv->pm.irqs_disabled);

	if (IS_IRONLAKE_M(dev)) {
		ironlake_disable_drps(dev);
		ironlake_disable_rc6(dev);
	} else if (INTEL_INFO(dev)->gen >= 6) {
		intel_suspend_gt_powersave(dev);

		mutex_lock(&dev_priv->rps.hw_lock);
		if (IS_CHERRYVIEW(dev))
			cherryview_disable_rps(dev);
		else if (IS_VALLEYVIEW(dev))
			valleyview_disable_rps(dev);
		else
			gen6_disable_rps(dev);
		dev_priv->rps.enabled = false;
		mutex_unlock(&dev_priv->rps.hw_lock);
	}
}

static void intel_gen6_powersave_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private,
			     rps.delayed_resume_work.work);
	struct drm_device *dev = dev_priv->dev;

	mutex_lock(&dev_priv->rps.hw_lock);

	if (IS_CHERRYVIEW(dev)) {
		cherryview_enable_rps(dev);
	} else if (IS_VALLEYVIEW(dev)) {
		valleyview_enable_rps(dev);
	} else if (IS_BROADWELL(dev)) {
		gen8_enable_rps(dev);
		__gen6_update_ring_freq(dev);
	} else {
		gen6_enable_rps(dev);
		__gen6_update_ring_freq(dev);
	}
	dev_priv->rps.enabled = true;
	mutex_unlock(&dev_priv->rps.hw_lock);

	intel_runtime_pm_put(dev_priv);
}

void intel_enable_gt_powersave(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (IS_IRONLAKE_M(dev)) {
		mutex_lock(&dev->struct_mutex);
		ironlake_enable_drps(dev);
		ironlake_enable_rc6(dev);
		intel_init_emon(dev);
		mutex_unlock(&dev->struct_mutex);
	} else if (INTEL_INFO(dev)->gen >= 6) {
		/*
		 * PCU communication is slow and this doesn't need to be
		 * done at any specific time, so do this out of our fast path
		 * to make resume and init faster.
		 *
		 * We depend on the HW RC6 power context save/restore
		 * mechanism when entering D3 through runtime PM suspend. So
		 * disable RPM until RPS/RC6 is properly setup. We can only
		 * get here via the driver load/system resume/runtime resume
		 * paths, so the _noresume version is enough (and in case of
		 * runtime resume it's necessary).
		 */
		if (schedule_delayed_work(&dev_priv->rps.delayed_resume_work,
					   round_jiffies_up_relative(HZ)))
			intel_runtime_pm_get_noresume(dev_priv);
	}
}

void intel_reset_gt_powersave(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->rps.enabled = false;
	intel_enable_gt_powersave(dev);
}

static void ibx_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/*
	 * On Ibex Peak and Cougar Point, we need to disable clock
	 * gating for the panel power sequencer or it will fail to
	 * start up when no ports are active.
	 */
	I915_WRITE(SOUTH_DSPCLK_GATE_D, PCH_DPLSUNIT_CLOCK_GATE_DISABLE);
}

static void g4x_disable_trickle_feed(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe;

	for_each_pipe(pipe) {
		I915_WRITE(DSPCNTR(pipe),
			   I915_READ(DSPCNTR(pipe)) |
			   DISPPLANE_TRICKLE_FEED_DISABLE);
		intel_flush_primary_plane(dev_priv, pipe);
	}
}

static void ilk_init_lp_watermarks(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(WM3_LP_ILK, I915_READ(WM3_LP_ILK) & ~WM1_LP_SR_EN);
	I915_WRITE(WM2_LP_ILK, I915_READ(WM2_LP_ILK) & ~WM1_LP_SR_EN);
	I915_WRITE(WM1_LP_ILK, I915_READ(WM1_LP_ILK) & ~WM1_LP_SR_EN);

	/*
	 * Don't touch WM1S_LP_EN here.
	 * Doing so could cause underruns.
	 */
}

static void ironlake_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dspclk_gate = ILK_VRHUNIT_CLOCK_GATE_DISABLE;

	/*
	 * Required for FBC
	 * WaFbcDisableDpfcClockGating:ilk
	 */
	dspclk_gate |= ILK_DPFCRUNIT_CLOCK_GATE_DISABLE |
		   ILK_DPFCUNIT_CLOCK_GATE_DISABLE |
		   ILK_DPFDUNIT_CLOCK_GATE_ENABLE;

	I915_WRITE(PCH_3DCGDIS0,
		   MARIUNIT_CLOCK_GATE_DISABLE |
		   SVSMUNIT_CLOCK_GATE_DISABLE);
	I915_WRITE(PCH_3DCGDIS1,
		   VFMUNIT_CLOCK_GATE_DISABLE);

	/*
	 * According to the spec the following bits should be set in
	 * order to enable memory self-refresh
	 * The bit 22/21 of 0x42004
	 * The bit 5 of 0x42020
	 * The bit 15 of 0x45000
	 */
	I915_WRITE(ILK_DISPLAY_CHICKEN2,
		   (I915_READ(ILK_DISPLAY_CHICKEN2) |
		    ILK_DPARB_GATE | ILK_VSDPFD_FULL));
	dspclk_gate |= ILK_DPARBUNIT_CLOCK_GATE_ENABLE;
	I915_WRITE(DISP_ARB_CTL,
		   (I915_READ(DISP_ARB_CTL) |
		    DISP_FBC_WM_DIS));

	ilk_init_lp_watermarks(dev);

	/*
	 * Based on the document from hardware guys the following bits
	 * should be set unconditionally in order to enable FBC.
	 * The bit 22 of 0x42000
	 * The bit 22 of 0x42004
	 * The bit 7,8,9 of 0x42020.
	 */
	if (IS_IRONLAKE_M(dev)) {
		/* WaFbcAsynchFlipDisableFbcQueue:ilk */
		I915_WRITE(ILK_DISPLAY_CHICKEN1,
			   I915_READ(ILK_DISPLAY_CHICKEN1) |
			   ILK_FBCQ_DIS);
		I915_WRITE(ILK_DISPLAY_CHICKEN2,
			   I915_READ(ILK_DISPLAY_CHICKEN2) |
			   ILK_DPARB_GATE);
	}

	I915_WRITE(ILK_DSPCLK_GATE_D, dspclk_gate);

	I915_WRITE(ILK_DISPLAY_CHICKEN2,
		   I915_READ(ILK_DISPLAY_CHICKEN2) |
		   ILK_ELPIN_409_SELECT);
	I915_WRITE(_3D_CHICKEN2,
		   _3D_CHICKEN2_WM_READ_PIPELINED << 16 |
		   _3D_CHICKEN2_WM_READ_PIPELINED);

	/* WaDisableRenderCachePipelinedFlush:ilk */
	I915_WRITE(CACHE_MODE_0,
		   _MASKED_BIT_ENABLE(CM0_PIPELINED_RENDER_FLUSH_DISABLE));

	/* WaDisable_RenderCache_OperationalFlush:ilk */
	I915_WRITE(CACHE_MODE_0, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));

	g4x_disable_trickle_feed(dev);

	ibx_init_clock_gating(dev);
}

static void cpt_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe;
	uint32_t val;

	/*
	 * On Ibex Peak and Cougar Point, we need to disable clock
	 * gating for the panel power sequencer or it will fail to
	 * start up when no ports are active.
	 */
	I915_WRITE(SOUTH_DSPCLK_GATE_D, PCH_DPLSUNIT_CLOCK_GATE_DISABLE |
		   PCH_DPLUNIT_CLOCK_GATE_DISABLE |
		   PCH_CPUNIT_CLOCK_GATE_DISABLE);
	I915_WRITE(SOUTH_CHICKEN2, I915_READ(SOUTH_CHICKEN2) |
		   DPLS_EDP_PPS_FIX_DIS);
	/* The below fixes the weird display corruption, a few pixels shifted
	 * downward, on (only) LVDS of some HP laptops with IVY.
	 */
	for_each_pipe(pipe) {
		val = I915_READ(TRANS_CHICKEN2(pipe));
		val |= TRANS_CHICKEN2_TIMING_OVERRIDE;
		val &= ~TRANS_CHICKEN2_FDI_POLARITY_REVERSED;
		if (dev_priv->vbt.fdi_rx_polarity_inverted)
			val |= TRANS_CHICKEN2_FDI_POLARITY_REVERSED;
		val &= ~TRANS_CHICKEN2_FRAME_START_DELAY_MASK;
		val &= ~TRANS_CHICKEN2_DISABLE_DEEP_COLOR_COUNTER;
		val &= ~TRANS_CHICKEN2_DISABLE_DEEP_COLOR_MODESWITCH;
		I915_WRITE(TRANS_CHICKEN2(pipe), val);
	}
	/* WADP0ClockGatingDisable */
	for_each_pipe(pipe) {
		I915_WRITE(TRANS_CHICKEN1(pipe),
			   TRANS_CHICKEN1_DP0UNIT_GC_DISABLE);
	}
}

static void gen6_check_mch_setup(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t tmp;

	tmp = I915_READ(MCH_SSKPD);
	if ((tmp & MCH_SSKPD_WM0_MASK) != MCH_SSKPD_WM0_VAL) {
		DRM_INFO("Wrong MCH_SSKPD value: 0x%08x\n", tmp);
		DRM_INFO("This can cause pipe underruns and display issues.\n");
		DRM_INFO("Please upgrade your BIOS to fix this.\n");
	}
}

static void gen6_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dspclk_gate = ILK_VRHUNIT_CLOCK_GATE_DISABLE;

	I915_WRITE(ILK_DSPCLK_GATE_D, dspclk_gate);

	I915_WRITE(ILK_DISPLAY_CHICKEN2,
		   I915_READ(ILK_DISPLAY_CHICKEN2) |
		   ILK_ELPIN_409_SELECT);

	/* WaDisableHiZPlanesWhenMSAAEnabled:snb */
	I915_WRITE(_3D_CHICKEN,
		   _MASKED_BIT_ENABLE(_3D_CHICKEN_HIZ_PLANE_DISABLE_MSAA_4X_SNB));

	/* WaSetupGtModeTdRowDispatch:snb */
	if (IS_SNB_GT1(dev))
		I915_WRITE(GEN6_GT_MODE,
			   _MASKED_BIT_ENABLE(GEN6_TD_FOUR_ROW_DISPATCH_DISABLE));

	/* WaDisable_RenderCache_OperationalFlush:snb */
	I915_WRITE(CACHE_MODE_0, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));

	/*
	 * BSpec recoomends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	I915_WRITE(GEN6_GT_MODE,
		   GEN6_WIZ_HASHING_MASK | GEN6_WIZ_HASHING_16x4);

	ilk_init_lp_watermarks(dev);

	I915_WRITE(CACHE_MODE_0,
		   _MASKED_BIT_DISABLE(CM0_STC_EVICT_DISABLE_LRA_SNB));

	I915_WRITE(GEN6_UCGCTL1,
		   I915_READ(GEN6_UCGCTL1) |
		   GEN6_BLBUNIT_CLOCK_GATE_DISABLE |
		   GEN6_CSUNIT_CLOCK_GATE_DISABLE);

	/* According to the BSpec vol1g, bit 12 (RCPBUNIT) clock
	 * gating disable must be set.  Failure to set it results in
	 * flickering pixels due to Z write ordering failures after
	 * some amount of runtime in the Mesa "fire" demo, and Unigine
	 * Sanctuary and Tropics, and apparently anything else with
	 * alpha test or pixel discard.
	 *
	 * According to the spec, bit 11 (RCCUNIT) must also be set,
	 * but we didn't debug actual testcases to find it out.
	 *
	 * WaDisableRCCUnitClockGating:snb
	 * WaDisableRCPBUnitClockGating:snb
	 */
	I915_WRITE(GEN6_UCGCTL2,
		   GEN6_RCPBUNIT_CLOCK_GATE_DISABLE |
		   GEN6_RCCUNIT_CLOCK_GATE_DISABLE);

	/* WaStripsFansDisableFastClipPerformanceFix:snb */
	I915_WRITE(_3D_CHICKEN3,
		   _MASKED_BIT_ENABLE(_3D_CHICKEN3_SF_DISABLE_FASTCLIP_CULL));

	/*
	 * Bspec says:
	 * "This bit must be set if 3DSTATE_CLIP clip mode is set to normal and
	 * 3DSTATE_SF number of SF output attributes is more than 16."
	 */
	I915_WRITE(_3D_CHICKEN3,
		   _MASKED_BIT_ENABLE(_3D_CHICKEN3_SF_DISABLE_PIPELINED_ATTR_FETCH));

	/*
	 * According to the spec the following bits should be
	 * set in order to enable memory self-refresh and fbc:
	 * The bit21 and bit22 of 0x42000
	 * The bit21 and bit22 of 0x42004
	 * The bit5 and bit7 of 0x42020
	 * The bit14 of 0x70180
	 * The bit14 of 0x71180
	 *
	 * WaFbcAsynchFlipDisableFbcQueue:snb
	 */
	I915_WRITE(ILK_DISPLAY_CHICKEN1,
		   I915_READ(ILK_DISPLAY_CHICKEN1) |
		   ILK_FBCQ_DIS | ILK_PABSTRETCH_DIS);
	I915_WRITE(ILK_DISPLAY_CHICKEN2,
		   I915_READ(ILK_DISPLAY_CHICKEN2) |
		   ILK_DPARB_GATE | ILK_VSDPFD_FULL);
	I915_WRITE(ILK_DSPCLK_GATE_D,
		   I915_READ(ILK_DSPCLK_GATE_D) |
		   ILK_DPARBUNIT_CLOCK_GATE_ENABLE  |
		   ILK_DPFDUNIT_CLOCK_GATE_ENABLE);

	g4x_disable_trickle_feed(dev);

	cpt_init_clock_gating(dev);

	gen6_check_mch_setup(dev);
}

static void gen7_setup_fixed_func_scheduler(struct drm_i915_private *dev_priv)
{
	uint32_t reg = I915_READ(GEN7_FF_THREAD_MODE);

	/*
	 * WaVSThreadDispatchOverride:ivb,vlv
	 *
	 * This actually overrides the dispatch
	 * mode for all thread types.
	 */
	reg &= ~GEN7_FF_SCHED_MASK;
	reg |= GEN7_FF_TS_SCHED_HW;
	reg |= GEN7_FF_VS_SCHED_HW;
	reg |= GEN7_FF_DS_SCHED_HW;

	I915_WRITE(GEN7_FF_THREAD_MODE, reg);
}

static void lpt_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/*
	 * TODO: this bit should only be enabled when really needed, then
	 * disabled when not needed anymore in order to save power.
	 */
	if (dev_priv->pch_id == INTEL_PCH_LPT_LP_DEVICE_ID_TYPE)
		I915_WRITE(SOUTH_DSPCLK_GATE_D,
			   I915_READ(SOUTH_DSPCLK_GATE_D) |
			   PCH_LP_PARTITION_LEVEL_DISABLE);

	/* WADPOClockGatingDisable:hsw */
	I915_WRITE(_TRANSA_CHICKEN1,
		   I915_READ(_TRANSA_CHICKEN1) |
		   TRANS_CHICKEN1_DP0UNIT_GC_DISABLE);
}

static void lpt_suspend_hw(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->pch_id == INTEL_PCH_LPT_LP_DEVICE_ID_TYPE) {
		uint32_t val = I915_READ(SOUTH_DSPCLK_GATE_D);

		val &= ~PCH_LP_PARTITION_LEVEL_DISABLE;
		I915_WRITE(SOUTH_DSPCLK_GATE_D, val);
	}
}

static void gen8_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum pipe pipe;

	I915_WRITE(WM3_LP_ILK, 0);
	I915_WRITE(WM2_LP_ILK, 0);
	I915_WRITE(WM1_LP_ILK, 0);

	/* FIXME(BDW): Check all the w/a, some might only apply to
	 * pre-production hw. */

	/* WaDisablePartialInstShootdown:bdw */
	I915_WRITE(GEN8_ROW_CHICKEN,
		   _MASKED_BIT_ENABLE(PARTIAL_INSTRUCTION_SHOOTDOWN_DISABLE));

	/* WaDisableThreadStallDopClockGating:bdw */
	/* FIXME: Unclear whether we really need this on production bdw. */
	I915_WRITE(GEN8_ROW_CHICKEN,
		   _MASKED_BIT_ENABLE(STALL_DOP_GATING_DISABLE));

	/*
	 * This GEN8_CENTROID_PIXEL_OPT_DIS W/A is only needed for
	 * pre-production hardware
	 */
	I915_WRITE(HALF_SLICE_CHICKEN3,
		   _MASKED_BIT_ENABLE(GEN8_CENTROID_PIXEL_OPT_DIS));
	I915_WRITE(HALF_SLICE_CHICKEN3,
		   _MASKED_BIT_ENABLE(GEN8_SAMPLER_POWER_BYPASS_DIS));
	I915_WRITE(GAMTARBMODE, _MASKED_BIT_ENABLE(ARB_MODE_BWGTLB_DISABLE));

	I915_WRITE(_3D_CHICKEN3,
		   _3D_CHICKEN_SDE_LIMIT_FIFO_POLY_DEPTH(2));

	I915_WRITE(COMMON_SLICE_CHICKEN2,
		   _MASKED_BIT_ENABLE(GEN8_CSC2_SBE_VUE_CACHE_CONSERVATIVE));

	I915_WRITE(GEN7_HALF_SLICE_CHICKEN1,
		   _MASKED_BIT_ENABLE(GEN7_SINGLE_SUBSCAN_DISPATCH_ENABLE));

	/* WaDisableDopClockGating:bdw May not be needed for production */
	I915_WRITE(GEN7_ROW_CHICKEN2,
		   _MASKED_BIT_ENABLE(DOP_CLOCK_GATING_DISABLE));

	/* WaSwitchSolVfFArbitrationPriority:bdw */
	I915_WRITE(GAM_ECOCHK, I915_READ(GAM_ECOCHK) | HSW_ECOCHK_ARB_PRIO_SOL);

	/* WaPsrDPAMaskVBlankInSRD:bdw */
	I915_WRITE(CHICKEN_PAR1_1,
		   I915_READ(CHICKEN_PAR1_1) | DPA_MASK_VBLANK_SRD);

	/* WaPsrDPRSUnmaskVBlankInSRD:bdw */
	for_each_pipe(pipe) {
		I915_WRITE(CHICKEN_PIPESL_1(pipe),
			   I915_READ(CHICKEN_PIPESL_1(pipe)) |
			   BDW_DPRS_MASK_VBLANK_SRD);
	}

	/* Use Force Non-Coherent whenever executing a 3D context. This is a
	 * workaround for for a possible hang in the unlikely event a TLB
	 * invalidation occurs during a PSD flush.
	 */
	I915_WRITE(HDC_CHICKEN0,
		   I915_READ(HDC_CHICKEN0) |
		   _MASKED_BIT_ENABLE(HDC_FORCE_NON_COHERENT));

	/* WaVSRefCountFullforceMissDisable:bdw */
	/* WaDSRefCountFullforceMissDisable:bdw */
	I915_WRITE(GEN7_FF_THREAD_MODE,
		   I915_READ(GEN7_FF_THREAD_MODE) &
		   ~(GEN8_FF_DS_REF_CNT_FFME | GEN7_FF_VS_REF_CNT_FFME));

	/*
	 * BSpec recommends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	I915_WRITE(GEN7_GT_MODE,
		   GEN6_WIZ_HASHING_MASK | GEN6_WIZ_HASHING_16x4);

	I915_WRITE(GEN6_RC_SLEEP_PSMI_CONTROL,
		   _MASKED_BIT_ENABLE(GEN8_RC_SEMA_IDLE_MSG_DISABLE));

	/* WaDisableSDEUnitClockGating:bdw */
	I915_WRITE(GEN8_UCGCTL6, I915_READ(GEN8_UCGCTL6) |
		   GEN8_SDEUNIT_CLOCK_GATE_DISABLE);

	/* Wa4x4STCOptimizationDisable:bdw */
	I915_WRITE(CACHE_MODE_1,
		   _MASKED_BIT_ENABLE(GEN8_4x4_STC_OPTIMIZATION_DISABLE));
}

static void haswell_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	ilk_init_lp_watermarks(dev);

	/* L3 caching of data atomics doesn't work -- disable it. */
	I915_WRITE(HSW_SCRATCH1, HSW_SCRATCH1_L3_DATA_ATOMICS_DISABLE);
	I915_WRITE(HSW_ROW_CHICKEN3,
		   _MASKED_BIT_ENABLE(HSW_ROW_CHICKEN3_L3_GLOBAL_ATOMICS_DISABLE));

	/* This is required by WaCatErrorRejectionIssue:hsw */
	I915_WRITE(GEN7_SQ_CHICKEN_MBCUNIT_CONFIG,
			I915_READ(GEN7_SQ_CHICKEN_MBCUNIT_CONFIG) |
			GEN7_SQ_CHICKEN_MBCUNIT_SQINTMOB);

	/* WaVSRefCountFullforceMissDisable:hsw */
	I915_WRITE(GEN7_FF_THREAD_MODE,
		   I915_READ(GEN7_FF_THREAD_MODE) & ~GEN7_FF_VS_REF_CNT_FFME);

	/* WaDisable_RenderCache_OperationalFlush:hsw */
	I915_WRITE(CACHE_MODE_0_GEN7, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));

	/* enable HiZ Raw Stall Optimization */
	I915_WRITE(CACHE_MODE_0_GEN7,
		   _MASKED_BIT_DISABLE(HIZ_RAW_STALL_OPT_DISABLE));

	/* WaDisable4x2SubspanOptimization:hsw */
	I915_WRITE(CACHE_MODE_1,
		   _MASKED_BIT_ENABLE(PIXEL_SUBSPAN_COLLECT_OPT_DISABLE));

	/*
	 * BSpec recommends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	I915_WRITE(GEN7_GT_MODE,
		   GEN6_WIZ_HASHING_MASK | GEN6_WIZ_HASHING_16x4);

	/* WaSwitchSolVfFArbitrationPriority:hsw */
	I915_WRITE(GAM_ECOCHK, I915_READ(GAM_ECOCHK) | HSW_ECOCHK_ARB_PRIO_SOL);

	/* WaRsPkgCStateDisplayPMReq:hsw */
	I915_WRITE(CHICKEN_PAR1_1,
		   I915_READ(CHICKEN_PAR1_1) | FORCE_ARB_IDLE_PLANES);

	lpt_init_clock_gating(dev);
}

static void ivybridge_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t snpcr;

	ilk_init_lp_watermarks(dev);

	I915_WRITE(ILK_DSPCLK_GATE_D, ILK_VRHUNIT_CLOCK_GATE_DISABLE);

	/* WaDisableEarlyCull:ivb */
	I915_WRITE(_3D_CHICKEN3,
		   _MASKED_BIT_ENABLE(_3D_CHICKEN_SF_DISABLE_OBJEND_CULL));

	/* WaDisableBackToBackFlipFix:ivb */
	I915_WRITE(IVB_CHICKEN3,
		   CHICKEN3_DGMG_REQ_OUT_FIX_DISABLE |
		   CHICKEN3_DGMG_DONE_FIX_DISABLE);

	/* WaDisablePSDDualDispatchEnable:ivb */
	if (IS_IVB_GT1(dev))
		I915_WRITE(GEN7_HALF_SLICE_CHICKEN1,
			   _MASKED_BIT_ENABLE(GEN7_PSD_SINGLE_PORT_DISPATCH_ENABLE));

	/* WaDisable_RenderCache_OperationalFlush:ivb */
	I915_WRITE(CACHE_MODE_0_GEN7, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));

	/* Apply the WaDisableRHWOOptimizationForRenderHang:ivb workaround. */
	I915_WRITE(GEN7_COMMON_SLICE_CHICKEN1,
		   GEN7_CSC1_RHWO_OPT_DISABLE_IN_RCC);

	/* WaApplyL3ControlAndL3ChickenMode:ivb */
	I915_WRITE(GEN7_L3CNTLREG1,
			GEN7_WA_FOR_GEN7_L3_CONTROL);
	I915_WRITE(GEN7_L3_CHICKEN_MODE_REGISTER,
		   GEN7_WA_L3_CHICKEN_MODE);
	if (IS_IVB_GT1(dev))
		I915_WRITE(GEN7_ROW_CHICKEN2,
			   _MASKED_BIT_ENABLE(DOP_CLOCK_GATING_DISABLE));
	else {
		/* must write both registers */
		I915_WRITE(GEN7_ROW_CHICKEN2,
			   _MASKED_BIT_ENABLE(DOP_CLOCK_GATING_DISABLE));
		I915_WRITE(GEN7_ROW_CHICKEN2_GT2,
			   _MASKED_BIT_ENABLE(DOP_CLOCK_GATING_DISABLE));
	}

	/* WaForceL3Serialization:ivb */
	I915_WRITE(GEN7_L3SQCREG4, I915_READ(GEN7_L3SQCREG4) &
		   ~L3SQ_URB_READ_CAM_MATCH_DISABLE);

	/*
	 * According to the spec, bit 13 (RCZUNIT) must be set on IVB.
	 * This implements the WaDisableRCZUnitClockGating:ivb workaround.
	 */
	I915_WRITE(GEN6_UCGCTL2,
		   GEN6_RCZUNIT_CLOCK_GATE_DISABLE);

	/* This is required by WaCatErrorRejectionIssue:ivb */
	I915_WRITE(GEN7_SQ_CHICKEN_MBCUNIT_CONFIG,
			I915_READ(GEN7_SQ_CHICKEN_MBCUNIT_CONFIG) |
			GEN7_SQ_CHICKEN_MBCUNIT_SQINTMOB);

	g4x_disable_trickle_feed(dev);

	gen7_setup_fixed_func_scheduler(dev_priv);

	if (0) { /* causes HiZ corruption on ivb:gt1 */
		/* enable HiZ Raw Stall Optimization */
		I915_WRITE(CACHE_MODE_0_GEN7,
			   _MASKED_BIT_DISABLE(HIZ_RAW_STALL_OPT_DISABLE));
	}

	/* WaDisable4x2SubspanOptimization:ivb */
	I915_WRITE(CACHE_MODE_1,
		   _MASKED_BIT_ENABLE(PIXEL_SUBSPAN_COLLECT_OPT_DISABLE));

	/*
	 * BSpec recommends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	I915_WRITE(GEN7_GT_MODE,
		   GEN6_WIZ_HASHING_MASK | GEN6_WIZ_HASHING_16x4);

	snpcr = I915_READ(GEN6_MBCUNIT_SNPCR);
	snpcr &= ~GEN6_MBC_SNPCR_MASK;
	snpcr |= GEN6_MBC_SNPCR_MED;
	I915_WRITE(GEN6_MBCUNIT_SNPCR, snpcr);

	if (!HAS_PCH_NOP(dev))
		cpt_init_clock_gating(dev);

	gen6_check_mch_setup(dev);
}

static void valleyview_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val;

	mutex_lock(&dev_priv->rps.hw_lock);
	val = vlv_punit_read(dev_priv, PUNIT_REG_GPU_FREQ_STS);
	mutex_unlock(&dev_priv->rps.hw_lock);
	switch ((val >> 6) & 3) {
	case 0:
	case 1:
		dev_priv->mem_freq = 800;
		break;
	case 2:
		dev_priv->mem_freq = 1066;
		break;
	case 3:
		dev_priv->mem_freq = 1333;
		break;
	}
	DRM_DEBUG_DRIVER("DDR speed: %d MHz", dev_priv->mem_freq);

	I915_WRITE(DSPCLK_GATE_D, VRHUNIT_CLOCK_GATE_DISABLE);

	/* WaDisableEarlyCull:vlv */
	I915_WRITE(_3D_CHICKEN3,
		   _MASKED_BIT_ENABLE(_3D_CHICKEN_SF_DISABLE_OBJEND_CULL));

	/* WaDisableBackToBackFlipFix:vlv */
	I915_WRITE(IVB_CHICKEN3,
		   CHICKEN3_DGMG_REQ_OUT_FIX_DISABLE |
		   CHICKEN3_DGMG_DONE_FIX_DISABLE);

	/* WaPsdDispatchEnable:vlv */
	/* WaDisablePSDDualDispatchEnable:vlv */
	I915_WRITE(GEN7_HALF_SLICE_CHICKEN1,
		   _MASKED_BIT_ENABLE(GEN7_MAX_PS_THREAD_DEP |
				      GEN7_PSD_SINGLE_PORT_DISPATCH_ENABLE));

	/* WaDisable_RenderCache_OperationalFlush:vlv */
	I915_WRITE(CACHE_MODE_0_GEN7, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));

	/* WaForceL3Serialization:vlv */
	I915_WRITE(GEN7_L3SQCREG4, I915_READ(GEN7_L3SQCREG4) &
		   ~L3SQ_URB_READ_CAM_MATCH_DISABLE);

	/* WaDisableDopClockGating:vlv */
	I915_WRITE(GEN7_ROW_CHICKEN2,
		   _MASKED_BIT_ENABLE(DOP_CLOCK_GATING_DISABLE));

	/* This is required by WaCatErrorRejectionIssue:vlv */
	I915_WRITE(GEN7_SQ_CHICKEN_MBCUNIT_CONFIG,
		   I915_READ(GEN7_SQ_CHICKEN_MBCUNIT_CONFIG) |
		   GEN7_SQ_CHICKEN_MBCUNIT_SQINTMOB);

	gen7_setup_fixed_func_scheduler(dev_priv);

	/*
	 * According to the spec, bit 13 (RCZUNIT) must be set on IVB.
	 * This implements the WaDisableRCZUnitClockGating:vlv workaround.
	 */
	I915_WRITE(GEN6_UCGCTL2,
		   GEN6_RCZUNIT_CLOCK_GATE_DISABLE);

	/* WaDisableL3Bank2xClockGate:vlv
	 * Disabling L3 clock gating- MMIO 940c[25] = 1
	 * Set bit 25, to disable L3_BANK_2x_CLK_GATING */
	I915_WRITE(GEN7_UCGCTL4,
		   I915_READ(GEN7_UCGCTL4) | GEN7_L3BANK2X_CLOCK_GATE_DISABLE);

	I915_WRITE(MI_ARB_VLV, MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE);

	/*
	 * BSpec says this must be set, even though
	 * WaDisable4x2SubspanOptimization isn't listed for VLV.
	 */
	I915_WRITE(CACHE_MODE_1,
		   _MASKED_BIT_ENABLE(PIXEL_SUBSPAN_COLLECT_OPT_DISABLE));

	/*
	 * WaIncreaseL3CreditsForVLVB0:vlv
	 * This is the hardware default actually.
	 */
	I915_WRITE(GEN7_L3SQCREG1, VLV_B0_WA_L3SQCREG1_VALUE);

	/*
	 * WaDisableVLVClockGating_VBIIssue:vlv
	 * Disable clock gating on th GCFG unit to prevent a delay
	 * in the reporting of vblank events.
	 */
	I915_WRITE(VLV_GUNIT_CLOCK_GATE, GCFG_DIS);
}

static void cherryview_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(DSPCLK_GATE_D, VRHUNIT_CLOCK_GATE_DISABLE);

	I915_WRITE(MI_ARB_VLV, MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE);

	/* WaDisablePartialInstShootdown:chv */
	I915_WRITE(GEN8_ROW_CHICKEN,
		   _MASKED_BIT_ENABLE(PARTIAL_INSTRUCTION_SHOOTDOWN_DISABLE));

	/* WaDisableThreadStallDopClockGating:chv */
	I915_WRITE(GEN8_ROW_CHICKEN,
		   _MASKED_BIT_ENABLE(STALL_DOP_GATING_DISABLE));

	/* WaVSRefCountFullforceMissDisable:chv */
	/* WaDSRefCountFullforceMissDisable:chv */
	I915_WRITE(GEN7_FF_THREAD_MODE,
		   I915_READ(GEN7_FF_THREAD_MODE) &
		   ~(GEN8_FF_DS_REF_CNT_FFME | GEN7_FF_VS_REF_CNT_FFME));

	/* WaDisableSemaphoreAndSyncFlipWait:chv */
	I915_WRITE(GEN6_RC_SLEEP_PSMI_CONTROL,
		   _MASKED_BIT_ENABLE(GEN8_RC_SEMA_IDLE_MSG_DISABLE));

	/* WaDisableCSUnitClockGating:chv */
	I915_WRITE(GEN6_UCGCTL1, I915_READ(GEN6_UCGCTL1) |
		   GEN6_CSUNIT_CLOCK_GATE_DISABLE);

	/* WaDisableSDEUnitClockGating:chv */
	I915_WRITE(GEN8_UCGCTL6, I915_READ(GEN8_UCGCTL6) |
		   GEN8_SDEUNIT_CLOCK_GATE_DISABLE);

	/* WaDisableSamplerPowerBypass:chv (pre-production hw) */
	I915_WRITE(HALF_SLICE_CHICKEN3,
		   _MASKED_BIT_ENABLE(GEN8_SAMPLER_POWER_BYPASS_DIS));

	/* WaDisableGunitClockGating:chv (pre-production hw) */
	I915_WRITE(VLV_GUNIT_CLOCK_GATE, I915_READ(VLV_GUNIT_CLOCK_GATE) |
		   GINT_DIS);

	/* WaDisableFfDopClockGating:chv (pre-production hw) */
	I915_WRITE(GEN6_RC_SLEEP_PSMI_CONTROL,
		   _MASKED_BIT_ENABLE(GEN8_FF_DOP_CLOCK_GATE_DISABLE));

	/* WaDisableDopClockGating:chv (pre-production hw) */
	I915_WRITE(GEN7_ROW_CHICKEN2,
		   _MASKED_BIT_ENABLE(DOP_CLOCK_GATING_DISABLE));
	I915_WRITE(GEN6_UCGCTL1, I915_READ(GEN6_UCGCTL1) |
		   GEN6_EU_TCUNIT_CLOCK_GATE_DISABLE);
}

static void g4x_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dspclk_gate;

	I915_WRITE(RENCLK_GATE_D1, 0);
	I915_WRITE(RENCLK_GATE_D2, VF_UNIT_CLOCK_GATE_DISABLE |
		   GS_UNIT_CLOCK_GATE_DISABLE |
		   CL_UNIT_CLOCK_GATE_DISABLE);
	I915_WRITE(RAMCLK_GATE_D, 0);
	dspclk_gate = VRHUNIT_CLOCK_GATE_DISABLE |
		OVRUNIT_CLOCK_GATE_DISABLE |
		OVCUNIT_CLOCK_GATE_DISABLE;
	if (IS_GM45(dev))
		dspclk_gate |= DSSUNIT_CLOCK_GATE_DISABLE;
	I915_WRITE(DSPCLK_GATE_D, dspclk_gate);

	/* WaDisableRenderCachePipelinedFlush */
	I915_WRITE(CACHE_MODE_0,
		   _MASKED_BIT_ENABLE(CM0_PIPELINED_RENDER_FLUSH_DISABLE));

	/* WaDisable_RenderCache_OperationalFlush:g4x */
	I915_WRITE(CACHE_MODE_0, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));

	g4x_disable_trickle_feed(dev);
}

static void crestline_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(RENCLK_GATE_D1, I965_RCC_CLOCK_GATE_DISABLE);
	I915_WRITE(RENCLK_GATE_D2, 0);
	I915_WRITE(DSPCLK_GATE_D, 0);
	I915_WRITE(RAMCLK_GATE_D, 0);
	I915_WRITE16(DEUC, 0);
	I915_WRITE(MI_ARB_STATE,
		   _MASKED_BIT_ENABLE(MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE));

	/* WaDisable_RenderCache_OperationalFlush:gen4 */
	I915_WRITE(CACHE_MODE_0, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));
}

static void broadwater_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(RENCLK_GATE_D1, I965_RCZ_CLOCK_GATE_DISABLE |
		   I965_RCC_CLOCK_GATE_DISABLE |
		   I965_RCPB_CLOCK_GATE_DISABLE |
		   I965_ISC_CLOCK_GATE_DISABLE |
		   I965_FBC_CLOCK_GATE_DISABLE);
	I915_WRITE(RENCLK_GATE_D2, 0);
	I915_WRITE(MI_ARB_STATE,
		   _MASKED_BIT_ENABLE(MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE));

	/* WaDisable_RenderCache_OperationalFlush:gen4 */
	I915_WRITE(CACHE_MODE_0, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));
}

static void gen3_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dstate = I915_READ(D_STATE);

	dstate |= DSTATE_PLL_D3_OFF | DSTATE_GFX_CLOCK_GATING |
		DSTATE_DOT_CLOCK_GATING;
	I915_WRITE(D_STATE, dstate);

	if (IS_PINEVIEW(dev))
		I915_WRITE(ECOSKPD, _MASKED_BIT_ENABLE(ECO_GATING_CX_ONLY));

	/* IIR "flip pending" means done if this bit is set */
	I915_WRITE(ECOSKPD, _MASKED_BIT_DISABLE(ECO_FLIP_DONE));

	/* interrupts should cause a wake up from C3 */
	I915_WRITE(INSTPM, _MASKED_BIT_ENABLE(INSTPM_AGPBUSY_INT_EN));

	/* On GEN3 we really need to make sure the ARB C3 LP bit is set */
	I915_WRITE(MI_ARB_STATE, _MASKED_BIT_ENABLE(MI_ARB_C3_LP_WRITE_ENABLE));
}

static void i85x_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(RENCLK_GATE_D1, SV_CLOCK_GATE_DISABLE);

	/* interrupts should cause a wake up from C3 */
	I915_WRITE(MI_STATE, _MASKED_BIT_ENABLE(MI_AGPBUSY_INT_EN) |
		   _MASKED_BIT_DISABLE(MI_AGPBUSY_830_MODE));
}

static void i830_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(DSPCLK_GATE_D, OVRUNIT_CLOCK_GATE_DISABLE);
}

void intel_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->display.init_clock_gating(dev);
}

void intel_suspend_hw(struct drm_device *dev)
{
	if (HAS_PCH_LPT(dev))
		lpt_suspend_hw(dev);
}

#define for_each_power_well(i, power_well, domain_mask, power_domains)	\
	for (i = 0;							\
	     i < (power_domains)->power_well_count &&			\
		 ((power_well) = &(power_domains)->power_wells[i]);	\
	     i++)							\
		if ((power_well)->domains & (domain_mask))

#define for_each_power_well_rev(i, power_well, domain_mask, power_domains) \
	for (i = (power_domains)->power_well_count - 1;			 \
	     i >= 0 && ((power_well) = &(power_domains)->power_wells[i]);\
	     i--)							 \
		if ((power_well)->domains & (domain_mask))

/**
 * We should only use the power well if we explicitly asked the hardware to
 * enable it, so check if it's enabled and also check if we've requested it to
 * be enabled.
 */
static bool hsw_power_well_enabled(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	return I915_READ(HSW_PWR_WELL_DRIVER) ==
		     (HSW_PWR_WELL_ENABLE_REQUEST | HSW_PWR_WELL_STATE_ENABLED);
}

bool intel_display_power_enabled_unlocked(struct drm_i915_private *dev_priv,
					  enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains;
	struct i915_power_well *power_well;
	bool is_enabled;
	int i;

	if (dev_priv->pm.suspended)
		return false;

	power_domains = &dev_priv->power_domains;

	is_enabled = true;

	for_each_power_well_rev(i, power_well, BIT(domain), power_domains) {
		if (power_well->always_on)
			continue;

		if (!power_well->hw_enabled) {
			is_enabled = false;
			break;
		}
	}

	return is_enabled;
}

bool intel_display_power_enabled(struct drm_i915_private *dev_priv,
				 enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains;
	bool ret;

	power_domains = &dev_priv->power_domains;

	mutex_lock(&power_domains->lock);
	ret = intel_display_power_enabled_unlocked(dev_priv, domain);
	mutex_unlock(&power_domains->lock);

	return ret;
}

/*
 * Starting with Haswell, we have a "Power Down Well" that can be turned off
 * when not needed anymore. We have 4 registers that can request the power well
 * to be enabled, and it will only be disabled if none of the registers is
 * requesting it to be enabled.
 */
static void hsw_power_well_post_enable(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	unsigned long irqflags;

	/*
	 * After we re-enable the power well, if we touch VGA register 0x3d5
	 * we'll get unclaimed register interrupts. This stops after we write
	 * anything to the VGA MSR register. The vgacon module uses this
	 * register all the time, so if we unbind our driver and, as a
	 * consequence, bind vgacon, we'll get stuck in an infinite loop at
	 * console_unlock(). So make here we touch the VGA MSR register, making
	 * sure vgacon can keep working normally without triggering interrupts
	 * and error messages.
	 */
	vga_get_uninterruptible(dev->pdev, VGA_RSRC_LEGACY_IO);
	outb(inb(VGA_MSR_READ), VGA_MSR_WRITE);
	vga_put(dev->pdev, VGA_RSRC_LEGACY_IO);

	if (IS_BROADWELL(dev)) {
		spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
		I915_WRITE(GEN8_DE_PIPE_IMR(PIPE_B),
			   dev_priv->de_irq_mask[PIPE_B]);
		I915_WRITE(GEN8_DE_PIPE_IER(PIPE_B),
			   ~dev_priv->de_irq_mask[PIPE_B] |
			   GEN8_PIPE_VBLANK);
		I915_WRITE(GEN8_DE_PIPE_IMR(PIPE_C),
			   dev_priv->de_irq_mask[PIPE_C]);
		I915_WRITE(GEN8_DE_PIPE_IER(PIPE_C),
			   ~dev_priv->de_irq_mask[PIPE_C] |
			   GEN8_PIPE_VBLANK);
		POSTING_READ(GEN8_DE_PIPE_IER(PIPE_C));
		spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
	}
}

static void hsw_set_power_well(struct drm_i915_private *dev_priv,
			       struct i915_power_well *power_well, bool enable)
{
	bool is_enabled, enable_requested;
	uint32_t tmp;

	tmp = I915_READ(HSW_PWR_WELL_DRIVER);
	is_enabled = tmp & HSW_PWR_WELL_STATE_ENABLED;
	enable_requested = tmp & HSW_PWR_WELL_ENABLE_REQUEST;

	if (enable) {
		if (!enable_requested)
			I915_WRITE(HSW_PWR_WELL_DRIVER,
				   HSW_PWR_WELL_ENABLE_REQUEST);

		if (!is_enabled) {
			DRM_DEBUG_KMS("Enabling power well\n");
			if (wait_for((I915_READ(HSW_PWR_WELL_DRIVER) &
				      HSW_PWR_WELL_STATE_ENABLED), 20))
				DRM_ERROR("Timeout enabling power well\n");
		}

		hsw_power_well_post_enable(dev_priv);
	} else {
		if (enable_requested) {
			I915_WRITE(HSW_PWR_WELL_DRIVER, 0);
			POSTING_READ(HSW_PWR_WELL_DRIVER);
			DRM_DEBUG_KMS("Requesting to disable the power well\n");
		}
	}
}

static void hsw_power_well_sync_hw(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	hsw_set_power_well(dev_priv, power_well, power_well->count > 0);

	/*
	 * We're taking over the BIOS, so clear any requests made by it since
	 * the driver is in charge now.
	 */
	if (I915_READ(HSW_PWR_WELL_BIOS) & HSW_PWR_WELL_ENABLE_REQUEST)
		I915_WRITE(HSW_PWR_WELL_BIOS, 0);
}

static void hsw_power_well_enable(struct drm_i915_private *dev_priv,
				  struct i915_power_well *power_well)
{
	hsw_set_power_well(dev_priv, power_well, true);
}

static void hsw_power_well_disable(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	hsw_set_power_well(dev_priv, power_well, false);
}

static void i9xx_always_on_power_well_noop(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
}

static bool i9xx_always_on_power_well_enabled(struct drm_i915_private *dev_priv,
					     struct i915_power_well *power_well)
{
	return true;
}

static void vlv_set_power_well(struct drm_i915_private *dev_priv,
			       struct i915_power_well *power_well, bool enable)
{
	enum punit_power_well power_well_id = power_well->data;
	u32 mask;
	u32 state;
	u32 ctrl;

	mask = PUNIT_PWRGT_MASK(power_well_id);
	state = enable ? PUNIT_PWRGT_PWR_ON(power_well_id) :
			 PUNIT_PWRGT_PWR_GATE(power_well_id);

	mutex_lock(&dev_priv->rps.hw_lock);

#define COND \
	((vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_STATUS) & mask) == state)

	if (COND)
		goto out;

	ctrl = vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_CTRL);
	ctrl &= ~mask;
	ctrl |= state;
	vlv_punit_write(dev_priv, PUNIT_REG_PWRGT_CTRL, ctrl);

	if (wait_for(COND, 100))
		DRM_ERROR("timout setting power well state %08x (%08x)\n",
			  state,
			  vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_CTRL));

#undef COND

out:
	mutex_unlock(&dev_priv->rps.hw_lock);
}

static void vlv_power_well_sync_hw(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	vlv_set_power_well(dev_priv, power_well, power_well->count > 0);
}

static void vlv_power_well_enable(struct drm_i915_private *dev_priv,
				  struct i915_power_well *power_well)
{
	vlv_set_power_well(dev_priv, power_well, true);
}

static void vlv_power_well_disable(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	vlv_set_power_well(dev_priv, power_well, false);
}

static bool vlv_power_well_enabled(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	int power_well_id = power_well->data;
	bool enabled = false;
	u32 mask;
	u32 state;
	u32 ctrl;

	mask = PUNIT_PWRGT_MASK(power_well_id);
	ctrl = PUNIT_PWRGT_PWR_ON(power_well_id);

	mutex_lock(&dev_priv->rps.hw_lock);

	state = vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_STATUS) & mask;
	/*
	 * We only ever set the power-on and power-gate states, anything
	 * else is unexpected.
	 */
	WARN_ON(state != PUNIT_PWRGT_PWR_ON(power_well_id) &&
		state != PUNIT_PWRGT_PWR_GATE(power_well_id));
	if (state == ctrl)
		enabled = true;

	/*
	 * A transient state at this point would mean some unexpected party
	 * is poking at the power controls too.
	 */
	ctrl = vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_CTRL) & mask;
	WARN_ON(ctrl != state);

	mutex_unlock(&dev_priv->rps.hw_lock);

	return enabled;
}

static void vlv_display_power_well_enable(struct drm_i915_private *dev_priv,
					  struct i915_power_well *power_well)
{
	WARN_ON_ONCE(power_well->data != PUNIT_POWER_WELL_DISP2D);

	vlv_set_power_well(dev_priv, power_well, true);

	spin_lock_irq(&dev_priv->irq_lock);
	valleyview_enable_display_irqs(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	/*
	 * During driver initialization/resume we can avoid restoring the
	 * part of the HW/SW state that will be inited anyway explicitly.
	 */
	if (dev_priv->power_domains.initializing)
		return;

	intel_hpd_init(dev_priv->dev);

	i915_redisable_vga_power_on(dev_priv->dev);
}

static void vlv_display_power_well_disable(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
	WARN_ON_ONCE(power_well->data != PUNIT_POWER_WELL_DISP2D);

	spin_lock_irq(&dev_priv->irq_lock);
	valleyview_disable_display_irqs(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	vlv_set_power_well(dev_priv, power_well, false);
}

static void vlv_dpio_cmn_power_well_enable(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
	WARN_ON_ONCE(power_well->data != PUNIT_POWER_WELL_DPIO_CMN_BC);

	/*
	 * Enable the CRI clock source so we can get at the
	 * display and the reference clock for VGA
	 * hotplug / manual detection.
	 */
	I915_WRITE(DPLL(PIPE_B), I915_READ(DPLL(PIPE_B)) |
		   DPLL_REFA_CLK_ENABLE_VLV | DPLL_INTEGRATED_CRI_CLK_VLV);
	udelay(1); /* >10ns for cmnreset, >0ns for sidereset */

	vlv_set_power_well(dev_priv, power_well, true);

	/*
	 * From VLV2A0_DP_eDP_DPIO_driver_vbios_notes_10.docx -
	 *  6.	De-assert cmn_reset/side_reset. Same as VLV X0.
	 *   a.	GUnit 0x2110 bit[0] set to 1 (def 0)
	 *   b.	The other bits such as sfr settings / modesel may all
	 *	be set to 0.
	 *
	 * This should only be done on init and resume from S3 with
	 * both PLLs disabled, or we risk losing DPIO and PLL
	 * synchronization.
	 */
	I915_WRITE(DPIO_CTL, I915_READ(DPIO_CTL) | DPIO_CMNRST);
}

static void vlv_dpio_cmn_power_well_disable(struct drm_i915_private *dev_priv,
					    struct i915_power_well *power_well)
{
	struct drm_device *dev = dev_priv->dev;
	enum pipe pipe;

	WARN_ON_ONCE(power_well->data != PUNIT_POWER_WELL_DPIO_CMN_BC);

	for_each_pipe(pipe)
		assert_pll_disabled(dev_priv, pipe);

	/* Assert common reset */
	I915_WRITE(DPIO_CTL, I915_READ(DPIO_CTL) & ~DPIO_CMNRST);

	vlv_set_power_well(dev_priv, power_well, false);
}

static void check_power_well_state(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	bool enabled = power_well->ops->is_enabled(dev_priv, power_well);

	if (power_well->always_on || !i915.disable_power_well) {
		if (!enabled)
			goto mismatch;

		return;
	}

	if (enabled != (power_well->count > 0))
		goto mismatch;

	return;

mismatch:
	WARN(1, "state mismatch for '%s' (always_on %d hw state %d use-count %d disable_power_well %d\n",
		  power_well->name, power_well->always_on, enabled,
		  power_well->count, i915.disable_power_well);
}

void intel_display_power_get(struct drm_i915_private *dev_priv,
			     enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains;
	struct i915_power_well *power_well;
	int i;

	intel_runtime_pm_get(dev_priv);

	power_domains = &dev_priv->power_domains;

	mutex_lock(&power_domains->lock);

	for_each_power_well(i, power_well, BIT(domain), power_domains) {
		if (!power_well->count++) {
			DRM_DEBUG_KMS("enabling %s\n", power_well->name);
			power_well->ops->enable(dev_priv, power_well);
			power_well->hw_enabled = true;
		}

		check_power_well_state(dev_priv, power_well);
	}

	power_domains->domain_use_count[domain]++;

	mutex_unlock(&power_domains->lock);
}

void intel_display_power_put(struct drm_i915_private *dev_priv,
			     enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains;
	struct i915_power_well *power_well;
	int i;

	power_domains = &dev_priv->power_domains;

	mutex_lock(&power_domains->lock);

	WARN_ON(!power_domains->domain_use_count[domain]);
	power_domains->domain_use_count[domain]--;

	for_each_power_well_rev(i, power_well, BIT(domain), power_domains) {
		WARN_ON(!power_well->count);

		if (!--power_well->count && i915.disable_power_well) {
			DRM_DEBUG_KMS("disabling %s\n", power_well->name);
			power_well->hw_enabled = false;
			power_well->ops->disable(dev_priv, power_well);
		}

		check_power_well_state(dev_priv, power_well);
	}

	mutex_unlock(&power_domains->lock);

	intel_runtime_pm_put(dev_priv);
}

static struct i915_power_domains *hsw_pwr;

/* Display audio driver power well request */
int i915_request_power_well(void)
{
	struct drm_i915_private *dev_priv;

	if (!hsw_pwr)
		return -ENODEV;

	dev_priv = container_of(hsw_pwr, struct drm_i915_private,
				power_domains);
	intel_display_power_get(dev_priv, POWER_DOMAIN_AUDIO);
	return 0;
}
EXPORT_SYMBOL_GPL(i915_request_power_well);

/* Display audio driver power well release */
int i915_release_power_well(void)
{
	struct drm_i915_private *dev_priv;

	if (!hsw_pwr)
		return -ENODEV;

	dev_priv = container_of(hsw_pwr, struct drm_i915_private,
				power_domains);
	intel_display_power_put(dev_priv, POWER_DOMAIN_AUDIO);
	return 0;
}
EXPORT_SYMBOL_GPL(i915_release_power_well);

/*
 * Private interface for the audio driver to get CDCLK in kHz.
 *
 * Caller must request power well using i915_request_power_well() prior to
 * making the call.
 */
int i915_get_cdclk_freq(void)
{
	struct drm_i915_private *dev_priv;

	if (!hsw_pwr)
		return -ENODEV;

	dev_priv = container_of(hsw_pwr, struct drm_i915_private,
				power_domains);

	return intel_ddi_get_cdclk_freq(dev_priv);
}
EXPORT_SYMBOL_GPL(i915_get_cdclk_freq);


#define POWER_DOMAIN_MASK (BIT(POWER_DOMAIN_NUM) - 1)

#define HSW_ALWAYS_ON_POWER_DOMAINS (			\
	BIT(POWER_DOMAIN_PIPE_A) |			\
	BIT(POWER_DOMAIN_TRANSCODER_EDP) |		\
	BIT(POWER_DOMAIN_PORT_DDI_A_2_LANES) |		\
	BIT(POWER_DOMAIN_PORT_DDI_A_4_LANES) |		\
	BIT(POWER_DOMAIN_PORT_DDI_B_2_LANES) |		\
	BIT(POWER_DOMAIN_PORT_DDI_B_4_LANES) |		\
	BIT(POWER_DOMAIN_PORT_DDI_C_2_LANES) |		\
	BIT(POWER_DOMAIN_PORT_DDI_C_4_LANES) |		\
	BIT(POWER_DOMAIN_PORT_DDI_D_2_LANES) |		\
	BIT(POWER_DOMAIN_PORT_DDI_D_4_LANES) |		\
	BIT(POWER_DOMAIN_PORT_CRT) |			\
	BIT(POWER_DOMAIN_INIT))
#define HSW_DISPLAY_POWER_DOMAINS (				\
	(POWER_DOMAIN_MASK & ~HSW_ALWAYS_ON_POWER_DOMAINS) |	\
	BIT(POWER_DOMAIN_INIT))

#define BDW_ALWAYS_ON_POWER_DOMAINS (			\
	HSW_ALWAYS_ON_POWER_DOMAINS |			\
	BIT(POWER_DOMAIN_PIPE_A_PANEL_FITTER))
#define BDW_DISPLAY_POWER_DOMAINS (				\
	(POWER_DOMAIN_MASK & ~BDW_ALWAYS_ON_POWER_DOMAINS) |	\
	BIT(POWER_DOMAIN_INIT))

#define VLV_ALWAYS_ON_POWER_DOMAINS	BIT(POWER_DOMAIN_INIT)
#define VLV_DISPLAY_POWER_DOMAINS	POWER_DOMAIN_MASK

#define VLV_DPIO_CMN_BC_POWER_DOMAINS (		\
	BIT(POWER_DOMAIN_PORT_DDI_B_2_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_B_4_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_C_2_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_C_4_LANES) |	\
	BIT(POWER_DOMAIN_PORT_CRT) |		\
	BIT(POWER_DOMAIN_INIT))

#define VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS (	\
	BIT(POWER_DOMAIN_PORT_DDI_B_2_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_B_4_LANES) |	\
	BIT(POWER_DOMAIN_INIT))

#define VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS (	\
	BIT(POWER_DOMAIN_PORT_DDI_B_4_LANES) |	\
	BIT(POWER_DOMAIN_INIT))

#define VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS (	\
	BIT(POWER_DOMAIN_PORT_DDI_C_2_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_C_4_LANES) |	\
	BIT(POWER_DOMAIN_INIT))

#define VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS (	\
	BIT(POWER_DOMAIN_PORT_DDI_C_4_LANES) |	\
	BIT(POWER_DOMAIN_INIT))

static const struct i915_power_well_ops i9xx_always_on_power_well_ops = {
	.sync_hw = i9xx_always_on_power_well_noop,
	.enable = i9xx_always_on_power_well_noop,
	.disable = i9xx_always_on_power_well_noop,
	.is_enabled = i9xx_always_on_power_well_enabled,
};

static struct i915_power_well i9xx_always_on_power_well[] = {
	{
		.name = "always-on",
		.always_on = 1,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
	},
};

static const struct i915_power_well_ops hsw_power_well_ops = {
	.sync_hw = hsw_power_well_sync_hw,
	.enable = hsw_power_well_enable,
	.disable = hsw_power_well_disable,
	.is_enabled = hsw_power_well_enabled,
};

static struct i915_power_well hsw_power_wells[] = {
	{
		.name = "always-on",
		.always_on = 1,
		.domains = HSW_ALWAYS_ON_POWER_DOMAINS,
		.ops = &i9xx_always_on_power_well_ops,
	},
	{
		.name = "display",
		.domains = HSW_DISPLAY_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
	},
};

static struct i915_power_well bdw_power_wells[] = {
	{
		.name = "always-on",
		.always_on = 1,
		.domains = BDW_ALWAYS_ON_POWER_DOMAINS,
		.ops = &i9xx_always_on_power_well_ops,
	},
	{
		.name = "display",
		.domains = BDW_DISPLAY_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
	},
};

static const struct i915_power_well_ops vlv_display_power_well_ops = {
	.sync_hw = vlv_power_well_sync_hw,
	.enable = vlv_display_power_well_enable,
	.disable = vlv_display_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

static const struct i915_power_well_ops vlv_dpio_cmn_power_well_ops = {
	.sync_hw = vlv_power_well_sync_hw,
	.enable = vlv_dpio_cmn_power_well_enable,
	.disable = vlv_dpio_cmn_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

static const struct i915_power_well_ops vlv_dpio_power_well_ops = {
	.sync_hw = vlv_power_well_sync_hw,
	.enable = vlv_power_well_enable,
	.disable = vlv_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

static struct i915_power_well vlv_power_wells[] = {
	{
		.name = "always-on",
		.always_on = 1,
		.domains = VLV_ALWAYS_ON_POWER_DOMAINS,
		.ops = &i9xx_always_on_power_well_ops,
	},
	{
		.name = "display",
		.domains = VLV_DISPLAY_POWER_DOMAINS,
		.data = PUNIT_POWER_WELL_DISP2D,
		.ops = &vlv_display_power_well_ops,
	},
	{
		.name = "dpio-tx-b-01",
		.domains = VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.data = PUNIT_POWER_WELL_DPIO_TX_B_LANES_01,
	},
	{
		.name = "dpio-tx-b-23",
		.domains = VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.data = PUNIT_POWER_WELL_DPIO_TX_B_LANES_23,
	},
	{
		.name = "dpio-tx-c-01",
		.domains = VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.data = PUNIT_POWER_WELL_DPIO_TX_C_LANES_01,
	},
	{
		.name = "dpio-tx-c-23",
		.domains = VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.data = PUNIT_POWER_WELL_DPIO_TX_C_LANES_23,
	},
	{
		.name = "dpio-common",
		.domains = VLV_DPIO_CMN_BC_POWER_DOMAINS,
		.data = PUNIT_POWER_WELL_DPIO_CMN_BC,
		.ops = &vlv_dpio_cmn_power_well_ops,
	},
};

static struct i915_power_well *lookup_power_well(struct drm_i915_private *dev_priv,
						 enum punit_power_well power_well_id)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *power_well;
	int i;

	for_each_power_well(i, power_well, POWER_DOMAIN_MASK, power_domains) {
		if (power_well->data == power_well_id)
			return power_well;
	}

	return NULL;
}

#define set_power_wells(power_domains, __power_wells) ({		\
	(power_domains)->power_wells = (__power_wells);			\
	(power_domains)->power_well_count = ARRAY_SIZE(__power_wells);	\
})

int intel_power_domains_init(struct drm_i915_private *dev_priv)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;

	mutex_init(&power_domains->lock);

	/*
	 * The enabling order will be from lower to higher indexed wells,
	 * the disabling order is reversed.
	 */
	if (IS_HASWELL(dev_priv->dev)) {
		set_power_wells(power_domains, hsw_power_wells);
		hsw_pwr = power_domains;
	} else if (IS_BROADWELL(dev_priv->dev)) {
		set_power_wells(power_domains, bdw_power_wells);
		hsw_pwr = power_domains;
	} else if (IS_VALLEYVIEW(dev_priv->dev)) {
		set_power_wells(power_domains, vlv_power_wells);
	} else {
		set_power_wells(power_domains, i9xx_always_on_power_well);
	}

	return 0;
}

void intel_power_domains_remove(struct drm_i915_private *dev_priv)
{
	hsw_pwr = NULL;
}

static void intel_power_domains_resume(struct drm_i915_private *dev_priv)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *power_well;
	int i;

	mutex_lock(&power_domains->lock);
	for_each_power_well(i, power_well, POWER_DOMAIN_MASK, power_domains) {
		power_well->ops->sync_hw(dev_priv, power_well);
		power_well->hw_enabled = power_well->ops->is_enabled(dev_priv,
								     power_well);
	}
	mutex_unlock(&power_domains->lock);
}

static void vlv_cmnlane_wa(struct drm_i915_private *dev_priv)
{
	struct i915_power_well *cmn =
		lookup_power_well(dev_priv, PUNIT_POWER_WELL_DPIO_CMN_BC);
	struct i915_power_well *disp2d =
		lookup_power_well(dev_priv, PUNIT_POWER_WELL_DISP2D);

	/* nothing to do if common lane is already off */
	if (!cmn->ops->is_enabled(dev_priv, cmn))
		return;

	/* If the display might be already active skip this */
	if (disp2d->ops->is_enabled(dev_priv, disp2d) &&
	    I915_READ(DPIO_CTL) & DPIO_CMNRST)
		return;

	DRM_DEBUG_KMS("toggling display PHY side reset\n");

	/* cmnlane needs DPLL registers */
	disp2d->ops->enable(dev_priv, disp2d);

	/*
	 * From VLV2A0_DP_eDP_HDMI_DPIO_driver_vbios_notes_11.docx:
	 * Need to assert and de-assert PHY SB reset by gating the
	 * common lane power, then un-gating it.
	 * Simply ungating isn't enough to reset the PHY enough to get
	 * ports and lanes running.
	 */
	cmn->ops->disable(dev_priv, cmn);
}

void intel_power_domains_init_hw(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct i915_power_domains *power_domains = &dev_priv->power_domains;

	power_domains->initializing = true;

	if (IS_VALLEYVIEW(dev) && !IS_CHERRYVIEW(dev)) {
		mutex_lock(&power_domains->lock);
		vlv_cmnlane_wa(dev_priv);
		mutex_unlock(&power_domains->lock);
	}

	/* For now, we need the power well to be always enabled. */
	intel_display_set_init_power(dev_priv, true);
	intel_power_domains_resume(dev_priv);
	power_domains->initializing = false;
}

void intel_aux_display_runtime_get(struct drm_i915_private *dev_priv)
{
	intel_runtime_pm_get(dev_priv);
}

void intel_aux_display_runtime_put(struct drm_i915_private *dev_priv)
{
	intel_runtime_pm_put(dev_priv);
}

void intel_runtime_pm_get(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct device *device = &dev->pdev->dev;

	if (!HAS_RUNTIME_PM(dev))
		return;

	pm_runtime_get_sync(device);
	WARN(dev_priv->pm.suspended, "Device still suspended.\n");
}

void intel_runtime_pm_get_noresume(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct device *device = &dev->pdev->dev;

	if (!HAS_RUNTIME_PM(dev))
		return;

	WARN(dev_priv->pm.suspended, "Getting nosync-ref while suspended.\n");
	pm_runtime_get_noresume(device);
}

void intel_runtime_pm_put(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct device *device = &dev->pdev->dev;

	if (!HAS_RUNTIME_PM(dev))
		return;

	pm_runtime_mark_last_busy(device);
	pm_runtime_put_autosuspend(device);
}

void intel_init_runtime_pm(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct device *device = &dev->pdev->dev;

	if (!HAS_RUNTIME_PM(dev))
		return;

	pm_runtime_set_active(device);

	/*
	 * RPM depends on RC6 to save restore the GT HW context, so make RC6 a
	 * requirement.
	 */
	if (!intel_enable_rc6(dev)) {
		DRM_INFO("RC6 disabled, disabling runtime PM support\n");
		return;
	}

	pm_runtime_set_autosuspend_delay(device, 10000); /* 10s */
	pm_runtime_mark_last_busy(device);
	pm_runtime_use_autosuspend(device);

	pm_runtime_put_autosuspend(device);
}

void intel_fini_runtime_pm(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct device *device = &dev->pdev->dev;

	if (!HAS_RUNTIME_PM(dev))
		return;

	if (!intel_enable_rc6(dev))
		return;

	/* Make sure we're not suspended first. */
	pm_runtime_get_sync(device);
	pm_runtime_disable(device);
}

/* Set up chip specific power management-related functions */
void intel_init_pm(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (HAS_FBC(dev)) {
		if (INTEL_INFO(dev)->gen >= 7) {
			dev_priv->display.fbc_enabled = ironlake_fbc_enabled;
			dev_priv->display.enable_fbc = gen7_enable_fbc;
			dev_priv->display.disable_fbc = ironlake_disable_fbc;
		} else if (INTEL_INFO(dev)->gen >= 5) {
			dev_priv->display.fbc_enabled = ironlake_fbc_enabled;
			dev_priv->display.enable_fbc = ironlake_enable_fbc;
			dev_priv->display.disable_fbc = ironlake_disable_fbc;
		} else if (IS_GM45(dev)) {
			dev_priv->display.fbc_enabled = g4x_fbc_enabled;
			dev_priv->display.enable_fbc = g4x_enable_fbc;
			dev_priv->display.disable_fbc = g4x_disable_fbc;
		} else {
			dev_priv->display.fbc_enabled = i8xx_fbc_enabled;
			dev_priv->display.enable_fbc = i8xx_enable_fbc;
			dev_priv->display.disable_fbc = i8xx_disable_fbc;

			/* This value was pulled out of someone's hat */
			I915_WRITE(FBC_CONTROL, 500 << FBC_CTL_INTERVAL_SHIFT);
		}
	}

	/* For cxsr */
	if (IS_PINEVIEW(dev))
		i915_pineview_get_mem_freq(dev);
	else if (IS_GEN5(dev))
		i915_ironlake_get_mem_freq(dev);

	/* For FIFO watermark updates */
	if (HAS_PCH_SPLIT(dev)) {
		ilk_setup_wm_latency(dev);

		if ((IS_GEN5(dev) && dev_priv->wm.pri_latency[1] &&
		     dev_priv->wm.spr_latency[1] && dev_priv->wm.cur_latency[1]) ||
		    (!IS_GEN5(dev) && dev_priv->wm.pri_latency[0] &&
		     dev_priv->wm.spr_latency[0] && dev_priv->wm.cur_latency[0])) {
			dev_priv->display.update_wm = ilk_update_wm;
			dev_priv->display.update_sprite_wm = ilk_update_sprite_wm;
		} else {
			DRM_DEBUG_KMS("Failed to read display plane latency. "
				      "Disable CxSR\n");
		}

		if (IS_GEN5(dev))
			dev_priv->display.init_clock_gating = ironlake_init_clock_gating;
		else if (IS_GEN6(dev))
			dev_priv->display.init_clock_gating = gen6_init_clock_gating;
		else if (IS_IVYBRIDGE(dev))
			dev_priv->display.init_clock_gating = ivybridge_init_clock_gating;
		else if (IS_HASWELL(dev))
			dev_priv->display.init_clock_gating = haswell_init_clock_gating;
		else if (INTEL_INFO(dev)->gen == 8)
			dev_priv->display.init_clock_gating = gen8_init_clock_gating;
	} else if (IS_CHERRYVIEW(dev)) {
		dev_priv->display.update_wm = valleyview_update_wm;
		dev_priv->display.init_clock_gating =
			cherryview_init_clock_gating;
	} else if (IS_VALLEYVIEW(dev)) {
		dev_priv->display.update_wm = valleyview_update_wm;
		dev_priv->display.init_clock_gating =
			valleyview_init_clock_gating;
	} else if (IS_PINEVIEW(dev)) {
		if (!intel_get_cxsr_latency(IS_PINEVIEW_G(dev),
					    dev_priv->is_ddr3,
					    dev_priv->fsb_freq,
					    dev_priv->mem_freq)) {
			DRM_INFO("failed to find known CxSR latency "
				 "(found ddr%s fsb freq %d, mem freq %d), "
				 "disabling CxSR\n",
				 (dev_priv->is_ddr3 == 1) ? "3" : "2",
				 dev_priv->fsb_freq, dev_priv->mem_freq);
			/* Disable CxSR and never update its watermark again */
			intel_set_memory_cxsr(dev_priv, false);
			dev_priv->display.update_wm = NULL;
		} else
			dev_priv->display.update_wm = pineview_update_wm;
		dev_priv->display.init_clock_gating = gen3_init_clock_gating;
	} else if (IS_G4X(dev)) {
		dev_priv->display.update_wm = g4x_update_wm;
		dev_priv->display.init_clock_gating = g4x_init_clock_gating;
	} else if (IS_GEN4(dev)) {
		dev_priv->display.update_wm = i965_update_wm;
		if (IS_CRESTLINE(dev))
			dev_priv->display.init_clock_gating = crestline_init_clock_gating;
		else if (IS_BROADWATER(dev))
			dev_priv->display.init_clock_gating = broadwater_init_clock_gating;
	} else if (IS_GEN3(dev)) {
		dev_priv->display.update_wm = i9xx_update_wm;
		dev_priv->display.get_fifo_size = i9xx_get_fifo_size;
		dev_priv->display.init_clock_gating = gen3_init_clock_gating;
	} else if (IS_GEN2(dev)) {
		if (INTEL_INFO(dev)->num_pipes == 1) {
			dev_priv->display.update_wm = i845_update_wm;
			dev_priv->display.get_fifo_size = i845_get_fifo_size;
		} else {
			dev_priv->display.update_wm = i9xx_update_wm;
			dev_priv->display.get_fifo_size = i830_get_fifo_size;
		}

		if (IS_I85X(dev) || IS_I865G(dev))
			dev_priv->display.init_clock_gating = i85x_init_clock_gating;
		else
			dev_priv->display.init_clock_gating = i830_init_clock_gating;
	} else {
		DRM_ERROR("unexpected fall-through in intel_init_pm\n");
	}
}

int sandybridge_pcode_read(struct drm_i915_private *dev_priv, u8 mbox, u32 *val)
{
	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));

	if (I915_READ(GEN6_PCODE_MAILBOX) & GEN6_PCODE_READY) {
		DRM_DEBUG_DRIVER("warning: pcode (read) mailbox access failed\n");
		return -EAGAIN;
	}

	I915_WRITE(GEN6_PCODE_DATA, *val);
	I915_WRITE(GEN6_PCODE_MAILBOX, GEN6_PCODE_READY | mbox);

	if (wait_for((I915_READ(GEN6_PCODE_MAILBOX) & GEN6_PCODE_READY) == 0,
		     500)) {
		DRM_ERROR("timeout waiting for pcode read (%d) to finish\n", mbox);
		return -ETIMEDOUT;
	}

	*val = I915_READ(GEN6_PCODE_DATA);
	I915_WRITE(GEN6_PCODE_DATA, 0);

	return 0;
}

int sandybridge_pcode_write(struct drm_i915_private *dev_priv, u8 mbox, u32 val)
{
	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));

	if (I915_READ(GEN6_PCODE_MAILBOX) & GEN6_PCODE_READY) {
		DRM_DEBUG_DRIVER("warning: pcode (write) mailbox access failed\n");
		return -EAGAIN;
	}

	I915_WRITE(GEN6_PCODE_DATA, val);
	I915_WRITE(GEN6_PCODE_MAILBOX, GEN6_PCODE_READY | mbox);

	if (wait_for((I915_READ(GEN6_PCODE_MAILBOX) & GEN6_PCODE_READY) == 0,
		     500)) {
		DRM_ERROR("timeout waiting for pcode write (%d) to finish\n", mbox);
		return -ETIMEDOUT;
	}

	I915_WRITE(GEN6_PCODE_DATA, 0);

	return 0;
}

int vlv_gpu_freq(struct drm_i915_private *dev_priv, int val)
{
	int div;

	/* 4 x czclk */
	switch (dev_priv->mem_freq) {
	case 800:
		div = 10;
		break;
	case 1066:
		div = 12;
		break;
	case 1333:
		div = 16;
		break;
	default:
		return -1;
	}

	return DIV_ROUND_CLOSEST(dev_priv->mem_freq * (val + 6 - 0xbd), 4 * div);
}

int vlv_freq_opcode(struct drm_i915_private *dev_priv, int val)
{
	int mul;

	/* 4 x czclk */
	switch (dev_priv->mem_freq) {
	case 800:
		mul = 10;
		break;
	case 1066:
		mul = 12;
		break;
	case 1333:
		mul = 16;
		break;
	default:
		return -1;
	}

	return DIV_ROUND_CLOSEST(4 * mul * val, dev_priv->mem_freq) + 0xbd - 6;
}

void intel_pm_setup(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	mutex_init(&dev_priv->rps.hw_lock);

	INIT_DELAYED_WORK(&dev_priv->rps.delayed_resume_work,
			  intel_gen6_powersave_work);

	dev_priv->pm.suspended = false;
	dev_priv->pm.irqs_disabled = false;
}

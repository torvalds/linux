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

static inline bool fbc_supported(struct drm_i915_private *dev_priv)
{
	return HAS_FBC(dev_priv);
}

static inline bool fbc_on_pipe_a_only(struct drm_i915_private *dev_priv)
{
	return IS_HASWELL(dev_priv) || INTEL_GEN(dev_priv) >= 8;
}

static inline bool fbc_on_plane_a_only(struct drm_i915_private *dev_priv)
{
	return INTEL_GEN(dev_priv) < 4;
}

static inline bool no_fbc_on_multiple_pipes(struct drm_i915_private *dev_priv)
{
	return INTEL_GEN(dev_priv) <= 3;
}

/*
 * In some platforms where the CRTC's x:0/y:0 coordinates doesn't match the
 * frontbuffer's x:0/y:0 coordinates we lie to the hardware about the plane's
 * origin so the x and y offsets can actually fit the registers. As a
 * consequence, the fence doesn't really start exactly at the display plane
 * address we program because it starts at the real start of the buffer, so we
 * have to take this into consideration here.
 */
static unsigned int get_crtc_fence_y_offset(struct intel_fbc *fbc)
{
	return fbc->state_cache.plane.y - fbc->state_cache.plane.adjusted_y;
}

/*
 * For SKL+, the plane source size used by the hardware is based on the value we
 * write to the PLANE_SIZE register. For BDW-, the hardware looks at the value
 * we wrote to PIPESRC.
 */
static void intel_fbc_get_plane_source_size(struct intel_fbc_state_cache *cache,
					    int *width, int *height)
{
	if (width)
		*width = cache->plane.src_w;
	if (height)
		*height = cache->plane.src_h;
}

static int intel_fbc_calculate_cfb_size(struct drm_i915_private *dev_priv,
					struct intel_fbc_state_cache *cache)
{
	int lines;

	intel_fbc_get_plane_source_size(cache, NULL, &lines);
	if (INTEL_GEN(dev_priv) == 7)
		lines = min(lines, 2048);
	else if (INTEL_GEN(dev_priv) >= 8)
		lines = min(lines, 2560);

	/* Hardware needs the full buffer stride, not just the active area. */
	return lines * cache->fb.stride;
}

static void i8xx_fbc_deactivate(struct drm_i915_private *dev_priv)
{
	u32 fbc_ctl;

	/* Disable compression */
	fbc_ctl = I915_READ(FBC_CONTROL);
	if ((fbc_ctl & FBC_CTL_EN) == 0)
		return;

	fbc_ctl &= ~FBC_CTL_EN;
	I915_WRITE(FBC_CONTROL, fbc_ctl);

	/* Wait for compressing bit to clear */
	if (intel_wait_for_register(dev_priv,
				    FBC_STATUS, FBC_STAT_COMPRESSING, 0,
				    10)) {
		DRM_DEBUG_KMS("FBC idle timed out\n");
		return;
	}
}

static void i8xx_fbc_activate(struct drm_i915_private *dev_priv)
{
	struct intel_fbc_reg_params *params = &dev_priv->fbc.params;
	int cfb_pitch;
	int i;
	u32 fbc_ctl;

	/* Note: fbc.threshold == 1 for i8xx */
	cfb_pitch = params->cfb_size / FBC_LL_SIZE;
	if (params->fb.stride < cfb_pitch)
		cfb_pitch = params->fb.stride;

	/* FBC_CTL wants 32B or 64B units */
	if (IS_GEN2(dev_priv))
		cfb_pitch = (cfb_pitch / 32) - 1;
	else
		cfb_pitch = (cfb_pitch / 64) - 1;

	/* Clear old tags */
	for (i = 0; i < (FBC_LL_SIZE / 32) + 1; i++)
		I915_WRITE(FBC_TAG(i), 0);

	if (IS_GEN4(dev_priv)) {
		u32 fbc_ctl2;

		/* Set it up... */
		fbc_ctl2 = FBC_CTL_FENCE_DBL | FBC_CTL_IDLE_IMM | FBC_CTL_CPU_FENCE;
		fbc_ctl2 |= FBC_CTL_PLANE(params->crtc.i9xx_plane);
		I915_WRITE(FBC_CONTROL2, fbc_ctl2);
		I915_WRITE(FBC_FENCE_OFF, params->crtc.fence_y_offset);
	}

	/* enable it... */
	fbc_ctl = I915_READ(FBC_CONTROL);
	fbc_ctl &= 0x3fff << FBC_CTL_INTERVAL_SHIFT;
	fbc_ctl |= FBC_CTL_EN | FBC_CTL_PERIODIC;
	if (IS_I945GM(dev_priv))
		fbc_ctl |= FBC_CTL_C3_IDLE; /* 945 needs special SR handling */
	fbc_ctl |= (cfb_pitch & 0xff) << FBC_CTL_STRIDE_SHIFT;
	fbc_ctl |= params->vma->fence->id;
	I915_WRITE(FBC_CONTROL, fbc_ctl);
}

static bool i8xx_fbc_is_active(struct drm_i915_private *dev_priv)
{
	return I915_READ(FBC_CONTROL) & FBC_CTL_EN;
}

static void g4x_fbc_activate(struct drm_i915_private *dev_priv)
{
	struct intel_fbc_reg_params *params = &dev_priv->fbc.params;
	u32 dpfc_ctl;

	dpfc_ctl = DPFC_CTL_PLANE(params->crtc.i9xx_plane) | DPFC_SR_EN;
	if (params->fb.format->cpp[0] == 2)
		dpfc_ctl |= DPFC_CTL_LIMIT_2X;
	else
		dpfc_ctl |= DPFC_CTL_LIMIT_1X;

	if (params->vma->fence) {
		dpfc_ctl |= DPFC_CTL_FENCE_EN | params->vma->fence->id;
		I915_WRITE(DPFC_FENCE_YOFF, params->crtc.fence_y_offset);
	} else {
		I915_WRITE(DPFC_FENCE_YOFF, 0);
	}

	/* enable it... */
	I915_WRITE(DPFC_CONTROL, dpfc_ctl | DPFC_CTL_EN);
}

static void g4x_fbc_deactivate(struct drm_i915_private *dev_priv)
{
	u32 dpfc_ctl;

	/* Disable compression */
	dpfc_ctl = I915_READ(DPFC_CONTROL);
	if (dpfc_ctl & DPFC_CTL_EN) {
		dpfc_ctl &= ~DPFC_CTL_EN;
		I915_WRITE(DPFC_CONTROL, dpfc_ctl);
	}
}

static bool g4x_fbc_is_active(struct drm_i915_private *dev_priv)
{
	return I915_READ(DPFC_CONTROL) & DPFC_CTL_EN;
}

/* This function forces a CFB recompression through the nuke operation. */
static void intel_fbc_recompress(struct drm_i915_private *dev_priv)
{
	I915_WRITE(MSG_FBC_REND_STATE, FBC_REND_NUKE);
	POSTING_READ(MSG_FBC_REND_STATE);
}

static void ilk_fbc_activate(struct drm_i915_private *dev_priv)
{
	struct intel_fbc_reg_params *params = &dev_priv->fbc.params;
	u32 dpfc_ctl;
	int threshold = dev_priv->fbc.threshold;

	dpfc_ctl = DPFC_CTL_PLANE(params->crtc.i9xx_plane);
	if (params->fb.format->cpp[0] == 2)
		threshold++;

	switch (threshold) {
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

	if (params->vma->fence) {
		dpfc_ctl |= DPFC_CTL_FENCE_EN;
		if (IS_GEN5(dev_priv))
			dpfc_ctl |= params->vma->fence->id;
		if (IS_GEN6(dev_priv)) {
			I915_WRITE(SNB_DPFC_CTL_SA,
				   SNB_CPU_FENCE_ENABLE |
				   params->vma->fence->id);
			I915_WRITE(DPFC_CPU_FENCE_OFFSET,
				   params->crtc.fence_y_offset);
		}
	} else {
		if (IS_GEN6(dev_priv)) {
			I915_WRITE(SNB_DPFC_CTL_SA, 0);
			I915_WRITE(DPFC_CPU_FENCE_OFFSET, 0);
		}
	}

	I915_WRITE(ILK_DPFC_FENCE_YOFF, params->crtc.fence_y_offset);
	I915_WRITE(ILK_FBC_RT_BASE,
		   i915_ggtt_offset(params->vma) | ILK_FBC_RT_VALID);
	/* enable it... */
	I915_WRITE(ILK_DPFC_CONTROL, dpfc_ctl | DPFC_CTL_EN);

	intel_fbc_recompress(dev_priv);
}

static void ilk_fbc_deactivate(struct drm_i915_private *dev_priv)
{
	u32 dpfc_ctl;

	/* Disable compression */
	dpfc_ctl = I915_READ(ILK_DPFC_CONTROL);
	if (dpfc_ctl & DPFC_CTL_EN) {
		dpfc_ctl &= ~DPFC_CTL_EN;
		I915_WRITE(ILK_DPFC_CONTROL, dpfc_ctl);
	}
}

static bool ilk_fbc_is_active(struct drm_i915_private *dev_priv)
{
	return I915_READ(ILK_DPFC_CONTROL) & DPFC_CTL_EN;
}

static void gen7_fbc_activate(struct drm_i915_private *dev_priv)
{
	struct intel_fbc_reg_params *params = &dev_priv->fbc.params;
	u32 dpfc_ctl;
	int threshold = dev_priv->fbc.threshold;

	/* Display WA #0529: skl, kbl, bxt. */
	if (IS_GEN9(dev_priv) && !IS_GEMINILAKE(dev_priv)) {
		u32 val = I915_READ(CHICKEN_MISC_4);

		val &= ~(FBC_STRIDE_OVERRIDE | FBC_STRIDE_MASK);

		if (i915_gem_object_get_tiling(params->vma->obj) !=
		    I915_TILING_X)
			val |= FBC_STRIDE_OVERRIDE | params->gen9_wa_cfb_stride;

		I915_WRITE(CHICKEN_MISC_4, val);
	}

	dpfc_ctl = 0;
	if (IS_IVYBRIDGE(dev_priv))
		dpfc_ctl |= IVB_DPFC_CTL_PLANE(params->crtc.i9xx_plane);

	if (params->fb.format->cpp[0] == 2)
		threshold++;

	switch (threshold) {
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

	if (params->vma->fence) {
		dpfc_ctl |= IVB_DPFC_CTL_FENCE_EN;
		I915_WRITE(SNB_DPFC_CTL_SA,
			   SNB_CPU_FENCE_ENABLE |
			   params->vma->fence->id);
		I915_WRITE(DPFC_CPU_FENCE_OFFSET, params->crtc.fence_y_offset);
	} else {
		I915_WRITE(SNB_DPFC_CTL_SA,0);
		I915_WRITE(DPFC_CPU_FENCE_OFFSET, 0);
	}

	if (dev_priv->fbc.false_color)
		dpfc_ctl |= FBC_CTL_FALSE_COLOR;

	if (IS_IVYBRIDGE(dev_priv)) {
		/* WaFbcAsynchFlipDisableFbcQueue:ivb */
		I915_WRITE(ILK_DISPLAY_CHICKEN1,
			   I915_READ(ILK_DISPLAY_CHICKEN1) |
			   ILK_FBCQ_DIS);
	} else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		/* WaFbcAsynchFlipDisableFbcQueue:hsw,bdw */
		I915_WRITE(CHICKEN_PIPESL_1(params->crtc.pipe),
			   I915_READ(CHICKEN_PIPESL_1(params->crtc.pipe)) |
			   HSW_FBCQ_DIS);
	}

	I915_WRITE(ILK_DPFC_CONTROL, dpfc_ctl | DPFC_CTL_EN);

	intel_fbc_recompress(dev_priv);
}

static bool intel_fbc_hw_is_active(struct drm_i915_private *dev_priv)
{
	if (INTEL_GEN(dev_priv) >= 5)
		return ilk_fbc_is_active(dev_priv);
	else if (IS_GM45(dev_priv))
		return g4x_fbc_is_active(dev_priv);
	else
		return i8xx_fbc_is_active(dev_priv);
}

static void intel_fbc_hw_activate(struct drm_i915_private *dev_priv)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	fbc->active = true;

	if (INTEL_GEN(dev_priv) >= 7)
		gen7_fbc_activate(dev_priv);
	else if (INTEL_GEN(dev_priv) >= 5)
		ilk_fbc_activate(dev_priv);
	else if (IS_GM45(dev_priv))
		g4x_fbc_activate(dev_priv);
	else
		i8xx_fbc_activate(dev_priv);
}

static void intel_fbc_hw_deactivate(struct drm_i915_private *dev_priv)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	fbc->active = false;

	if (INTEL_GEN(dev_priv) >= 5)
		ilk_fbc_deactivate(dev_priv);
	else if (IS_GM45(dev_priv))
		g4x_fbc_deactivate(dev_priv);
	else
		i8xx_fbc_deactivate(dev_priv);
}

/**
 * intel_fbc_is_active - Is FBC active?
 * @dev_priv: i915 device instance
 *
 * This function is used to verify the current state of FBC.
 *
 * FIXME: This should be tracked in the plane config eventually
 * instead of queried at runtime for most callers.
 */
bool intel_fbc_is_active(struct drm_i915_private *dev_priv)
{
	return dev_priv->fbc.active;
}

static void intel_fbc_work_fn(struct work_struct *__work)
{
	struct drm_i915_private *dev_priv =
		container_of(__work, struct drm_i915_private, fbc.work.work);
	struct intel_fbc *fbc = &dev_priv->fbc;
	struct intel_fbc_work *work = &fbc->work;
	struct intel_crtc *crtc = fbc->crtc;
	struct drm_vblank_crtc *vblank = &dev_priv->drm.vblank[crtc->pipe];

	if (drm_crtc_vblank_get(&crtc->base)) {
		/* CRTC is now off, leave FBC deactivated */
		mutex_lock(&fbc->lock);
		work->scheduled = false;
		mutex_unlock(&fbc->lock);
		return;
	}

retry:
	/* Delay the actual enabling to let pageflipping cease and the
	 * display to settle before starting the compression. Note that
	 * this delay also serves a second purpose: it allows for a
	 * vblank to pass after disabling the FBC before we attempt
	 * to modify the control registers.
	 *
	 * WaFbcWaitForVBlankBeforeEnable:ilk,snb
	 *
	 * It is also worth mentioning that since work->scheduled_vblank can be
	 * updated multiple times by the other threads, hitting the timeout is
	 * not an error condition. We'll just end up hitting the "goto retry"
	 * case below.
	 */
	wait_event_timeout(vblank->queue,
		drm_crtc_vblank_count(&crtc->base) != work->scheduled_vblank,
		msecs_to_jiffies(50));

	mutex_lock(&fbc->lock);

	/* Were we cancelled? */
	if (!work->scheduled)
		goto out;

	/* Were we delayed again while this function was sleeping? */
	if (drm_crtc_vblank_count(&crtc->base) == work->scheduled_vblank) {
		mutex_unlock(&fbc->lock);
		goto retry;
	}

	intel_fbc_hw_activate(dev_priv);

	work->scheduled = false;

out:
	mutex_unlock(&fbc->lock);
	drm_crtc_vblank_put(&crtc->base);
}

static void intel_fbc_schedule_activation(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;
	struct intel_fbc_work *work = &fbc->work;

	WARN_ON(!mutex_is_locked(&fbc->lock));
	if (WARN_ON(!fbc->enabled))
		return;

	if (drm_crtc_vblank_get(&crtc->base)) {
		DRM_ERROR("vblank not available for FBC on pipe %c\n",
			  pipe_name(crtc->pipe));
		return;
	}

	/* It is useless to call intel_fbc_cancel_work() or cancel_work() in
	 * this function since we're not releasing fbc.lock, so it won't have an
	 * opportunity to grab it to discover that it was cancelled. So we just
	 * update the expected jiffy count. */
	work->scheduled = true;
	work->scheduled_vblank = drm_crtc_vblank_count(&crtc->base);
	drm_crtc_vblank_put(&crtc->base);

	schedule_work(&work->work);
}

static void intel_fbc_deactivate(struct drm_i915_private *dev_priv)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	WARN_ON(!mutex_is_locked(&fbc->lock));

	/* Calling cancel_work() here won't help due to the fact that the work
	 * function grabs fbc->lock. Just set scheduled to false so the work
	 * function can know it was cancelled. */
	fbc->work.scheduled = false;

	if (fbc->active)
		intel_fbc_hw_deactivate(dev_priv);
}

static bool multiple_pipes_ok(struct intel_crtc *crtc,
			      struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;
	enum pipe pipe = crtc->pipe;

	/* Don't even bother tracking anything we don't need. */
	if (!no_fbc_on_multiple_pipes(dev_priv))
		return true;

	if (plane_state->base.visible)
		fbc->visible_pipes_mask |= (1 << pipe);
	else
		fbc->visible_pipes_mask &= ~(1 << pipe);

	return (fbc->visible_pipes_mask & ~(1 << pipe)) != 0;
}

static int find_compression_threshold(struct drm_i915_private *dev_priv,
				      struct drm_mm_node *node,
				      int size,
				      int fb_cpp)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	int compression_threshold = 1;
	int ret;
	u64 end;

	/* The FBC hardware for BDW/SKL doesn't have access to the stolen
	 * reserved range size, so it always assumes the maximum (8mb) is used.
	 * If we enable FBC using a CFB on that memory range we'll get FIFO
	 * underruns, even if that range is not reserved by the BIOS. */
	if (IS_BROADWELL(dev_priv) || IS_GEN9_BC(dev_priv))
		end = ggtt->stolen_size - 8 * 1024 * 1024;
	else
		end = U64_MAX;

	/* HACK: This code depends on what we will do in *_enable_fbc. If that
	 * code changes, this code needs to change as well.
	 *
	 * The enable_fbc code will attempt to use one of our 2 compression
	 * thresholds, therefore, in that case, we only have 1 resort.
	 */

	/* Try to over-allocate to reduce reallocations and fragmentation. */
	ret = i915_gem_stolen_insert_node_in_range(dev_priv, node, size <<= 1,
						   4096, 0, end);
	if (ret == 0)
		return compression_threshold;

again:
	/* HW's ability to limit the CFB is 1:4 */
	if (compression_threshold > 4 ||
	    (fb_cpp == 2 && compression_threshold == 2))
		return 0;

	ret = i915_gem_stolen_insert_node_in_range(dev_priv, node, size >>= 1,
						   4096, 0, end);
	if (ret && INTEL_GEN(dev_priv) <= 4) {
		return 0;
	} else if (ret) {
		compression_threshold <<= 1;
		goto again;
	} else {
		return compression_threshold;
	}
}

static int intel_fbc_alloc_cfb(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;
	struct drm_mm_node *uninitialized_var(compressed_llb);
	int size, fb_cpp, ret;

	WARN_ON(drm_mm_node_allocated(&fbc->compressed_fb));

	size = intel_fbc_calculate_cfb_size(dev_priv, &fbc->state_cache);
	fb_cpp = fbc->state_cache.fb.format->cpp[0];

	ret = find_compression_threshold(dev_priv, &fbc->compressed_fb,
					 size, fb_cpp);
	if (!ret)
		goto err_llb;
	else if (ret > 1) {
		DRM_INFO("Reducing the compressed framebuffer size. This may lead to less power savings than a non-reduced-size. Try to increase stolen memory size if available in BIOS.\n");

	}

	fbc->threshold = ret;

	if (INTEL_GEN(dev_priv) >= 5)
		I915_WRITE(ILK_DPFC_CB_BASE, fbc->compressed_fb.start);
	else if (IS_GM45(dev_priv)) {
		I915_WRITE(DPFC_CB_BASE, fbc->compressed_fb.start);
	} else {
		compressed_llb = kzalloc(sizeof(*compressed_llb), GFP_KERNEL);
		if (!compressed_llb)
			goto err_fb;

		ret = i915_gem_stolen_insert_node(dev_priv, compressed_llb,
						  4096, 4096);
		if (ret)
			goto err_fb;

		fbc->compressed_llb = compressed_llb;

		I915_WRITE(FBC_CFB_BASE,
			   dev_priv->mm.stolen_base + fbc->compressed_fb.start);
		I915_WRITE(FBC_LL_BASE,
			   dev_priv->mm.stolen_base + compressed_llb->start);
	}

	DRM_DEBUG_KMS("reserved %llu bytes of contiguous stolen space for FBC, threshold: %d\n",
		      fbc->compressed_fb.size, fbc->threshold);

	return 0;

err_fb:
	kfree(compressed_llb);
	i915_gem_stolen_remove_node(dev_priv, &fbc->compressed_fb);
err_llb:
	if (drm_mm_initialized(&dev_priv->mm.stolen))
		pr_info_once("drm: not enough stolen space for compressed buffer (need %d more bytes), disabling. Hint: you may be able to increase stolen memory size in the BIOS to avoid this.\n", size);
	return -ENOSPC;
}

static void __intel_fbc_cleanup_cfb(struct drm_i915_private *dev_priv)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (drm_mm_node_allocated(&fbc->compressed_fb))
		i915_gem_stolen_remove_node(dev_priv, &fbc->compressed_fb);

	if (fbc->compressed_llb) {
		i915_gem_stolen_remove_node(dev_priv, fbc->compressed_llb);
		kfree(fbc->compressed_llb);
	}
}

void intel_fbc_cleanup_cfb(struct drm_i915_private *dev_priv)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!fbc_supported(dev_priv))
		return;

	mutex_lock(&fbc->lock);
	__intel_fbc_cleanup_cfb(dev_priv);
	mutex_unlock(&fbc->lock);
}

static bool stride_is_valid(struct drm_i915_private *dev_priv,
			    unsigned int stride)
{
	/* These should have been caught earlier. */
	WARN_ON(stride < 512);
	WARN_ON((stride & (64 - 1)) != 0);

	/* Below are the additional FBC restrictions. */

	if (IS_GEN2(dev_priv) || IS_GEN3(dev_priv))
		return stride == 4096 || stride == 8192;

	if (IS_GEN4(dev_priv) && !IS_G4X(dev_priv) && stride < 2048)
		return false;

	if (stride > 16384)
		return false;

	return true;
}

static bool pixel_format_is_valid(struct drm_i915_private *dev_priv,
				  uint32_t pixel_format)
{
	switch (pixel_format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
		return true;
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_RGB565:
		/* 16bpp not supported on gen2 */
		if (IS_GEN2(dev_priv))
			return false;
		/* WaFbcOnly1to1Ratio:ctg */
		if (IS_G4X(dev_priv))
			return false;
		return true;
	default:
		return false;
	}
}

/*
 * For some reason, the hardware tracking starts looking at whatever we
 * programmed as the display plane base address register. It does not look at
 * the X and Y offset registers. That's why we look at the crtc->adjusted{x,y}
 * variables instead of just looking at the pipe/plane size.
 */
static bool intel_fbc_hw_tracking_covers_screen(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;
	unsigned int effective_w, effective_h, max_w, max_h;

	if (INTEL_GEN(dev_priv) >= 8 || IS_HASWELL(dev_priv)) {
		max_w = 4096;
		max_h = 4096;
	} else if (IS_G4X(dev_priv) || INTEL_GEN(dev_priv) >= 5) {
		max_w = 4096;
		max_h = 2048;
	} else {
		max_w = 2048;
		max_h = 1536;
	}

	intel_fbc_get_plane_source_size(&fbc->state_cache, &effective_w,
					&effective_h);
	effective_w += fbc->state_cache.plane.adjusted_x;
	effective_h += fbc->state_cache.plane.adjusted_y;

	return effective_w <= max_w && effective_h <= max_h;
}

static void intel_fbc_update_state_cache(struct intel_crtc *crtc,
					 struct intel_crtc_state *crtc_state,
					 struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;
	struct intel_fbc_state_cache *cache = &fbc->state_cache;
	struct drm_framebuffer *fb = plane_state->base.fb;

	cache->vma = NULL;

	cache->crtc.mode_flags = crtc_state->base.adjusted_mode.flags;
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		cache->crtc.hsw_bdw_pixel_rate = crtc_state->pixel_rate;

	cache->plane.rotation = plane_state->base.rotation;
	/*
	 * Src coordinates are already rotated by 270 degrees for
	 * the 90/270 degree plane rotation cases (to match the
	 * GTT mapping), hence no need to account for rotation here.
	 */
	cache->plane.src_w = drm_rect_width(&plane_state->base.src) >> 16;
	cache->plane.src_h = drm_rect_height(&plane_state->base.src) >> 16;
	cache->plane.visible = plane_state->base.visible;
	cache->plane.adjusted_x = plane_state->main.x;
	cache->plane.adjusted_y = plane_state->main.y;
	cache->plane.y = plane_state->base.src.y1 >> 16;

	if (!cache->plane.visible)
		return;

	cache->fb.format = fb->format;
	cache->fb.stride = fb->pitches[0];

	cache->vma = plane_state->vma;
}

static bool intel_fbc_can_activate(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;
	struct intel_fbc_state_cache *cache = &fbc->state_cache;

	/* We don't need to use a state cache here since this information is
	 * global for all CRTC.
	 */
	if (fbc->underrun_detected) {
		fbc->no_fbc_reason = "underrun detected";
		return false;
	}

	if (!cache->vma) {
		fbc->no_fbc_reason = "primary plane not visible";
		return false;
	}

	if ((cache->crtc.mode_flags & DRM_MODE_FLAG_INTERLACE) ||
	    (cache->crtc.mode_flags & DRM_MODE_FLAG_DBLSCAN)) {
		fbc->no_fbc_reason = "incompatible mode";
		return false;
	}

	if (!intel_fbc_hw_tracking_covers_screen(crtc)) {
		fbc->no_fbc_reason = "mode too large for compression";
		return false;
	}

	/* The use of a CPU fence is mandatory in order to detect writes
	 * by the CPU to the scanout and trigger updates to the FBC.
	 *
	 * Note that is possible for a tiled surface to be unmappable (and
	 * so have no fence associated with it) due to aperture constaints
	 * at the time of pinning.
	 */
	if (!cache->vma->fence) {
		fbc->no_fbc_reason = "framebuffer not tiled or fenced";
		return false;
	}
	if (INTEL_GEN(dev_priv) <= 4 && !IS_G4X(dev_priv) &&
	    cache->plane.rotation != DRM_MODE_ROTATE_0) {
		fbc->no_fbc_reason = "rotation unsupported";
		return false;
	}

	if (!stride_is_valid(dev_priv, cache->fb.stride)) {
		fbc->no_fbc_reason = "framebuffer stride not supported";
		return false;
	}

	if (!pixel_format_is_valid(dev_priv, cache->fb.format->format)) {
		fbc->no_fbc_reason = "pixel format is invalid";
		return false;
	}

	/* WaFbcExceedCdClockThreshold:hsw,bdw */
	if ((IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) &&
	    cache->crtc.hsw_bdw_pixel_rate >= dev_priv->cdclk.hw.cdclk * 95 / 100) {
		fbc->no_fbc_reason = "pixel rate is too big";
		return false;
	}

	/* It is possible for the required CFB size change without a
	 * crtc->disable + crtc->enable since it is possible to change the
	 * stride without triggering a full modeset. Since we try to
	 * over-allocate the CFB, there's a chance we may keep FBC enabled even
	 * if this happens, but if we exceed the current CFB size we'll have to
	 * disable FBC. Notice that it would be possible to disable FBC, wait
	 * for a frame, free the stolen node, then try to reenable FBC in case
	 * we didn't get any invalidate/deactivate calls, but this would require
	 * a lot of tracking just for a specific case. If we conclude it's an
	 * important case, we can implement it later. */
	if (intel_fbc_calculate_cfb_size(dev_priv, &fbc->state_cache) >
	    fbc->compressed_fb.size * fbc->threshold) {
		fbc->no_fbc_reason = "CFB requirements changed";
		return false;
	}

	return true;
}

static bool intel_fbc_can_enable(struct drm_i915_private *dev_priv)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (intel_vgpu_active(dev_priv)) {
		fbc->no_fbc_reason = "VGPU is active";
		return false;
	}

	if (!i915_modparams.enable_fbc) {
		fbc->no_fbc_reason = "disabled per module param or by default";
		return false;
	}

	if (fbc->underrun_detected) {
		fbc->no_fbc_reason = "underrun detected";
		return false;
	}

	return true;
}

static void intel_fbc_get_reg_params(struct intel_crtc *crtc,
				     struct intel_fbc_reg_params *params)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;
	struct intel_fbc_state_cache *cache = &fbc->state_cache;

	/* Since all our fields are integer types, use memset here so the
	 * comparison function can rely on memcmp because the padding will be
	 * zero. */
	memset(params, 0, sizeof(*params));

	params->vma = cache->vma;

	params->crtc.pipe = crtc->pipe;
	params->crtc.i9xx_plane = crtc->i9xx_plane;
	params->crtc.fence_y_offset = get_crtc_fence_y_offset(fbc);

	params->fb.format = cache->fb.format;
	params->fb.stride = cache->fb.stride;

	params->cfb_size = intel_fbc_calculate_cfb_size(dev_priv, cache);

	if (IS_GEN9(dev_priv) && !IS_GEMINILAKE(dev_priv))
		params->gen9_wa_cfb_stride = DIV_ROUND_UP(cache->plane.src_w,
						32 * fbc->threshold) * 8;
}

static bool intel_fbc_reg_params_equal(struct intel_fbc_reg_params *params1,
				       struct intel_fbc_reg_params *params2)
{
	/* We can use this since intel_fbc_get_reg_params() does a memset. */
	return memcmp(params1, params2, sizeof(*params1)) == 0;
}

void intel_fbc_pre_update(struct intel_crtc *crtc,
			  struct intel_crtc_state *crtc_state,
			  struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!fbc_supported(dev_priv))
		return;

	mutex_lock(&fbc->lock);

	if (!multiple_pipes_ok(crtc, plane_state)) {
		fbc->no_fbc_reason = "more than one pipe active";
		goto deactivate;
	}

	if (!fbc->enabled || fbc->crtc != crtc)
		goto unlock;

	intel_fbc_update_state_cache(crtc, crtc_state, plane_state);

deactivate:
	intel_fbc_deactivate(dev_priv);
unlock:
	mutex_unlock(&fbc->lock);
}

static void __intel_fbc_post_update(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;
	struct intel_fbc_reg_params old_params;

	WARN_ON(!mutex_is_locked(&fbc->lock));

	if (!fbc->enabled || fbc->crtc != crtc)
		return;

	if (!intel_fbc_can_activate(crtc)) {
		WARN_ON(fbc->active);
		return;
	}

	old_params = fbc->params;
	intel_fbc_get_reg_params(crtc, &fbc->params);

	/* If the scanout has not changed, don't modify the FBC settings.
	 * Note that we make the fundamental assumption that the fb->obj
	 * cannot be unpinned (and have its GTT offset and fence revoked)
	 * without first being decoupled from the scanout and FBC disabled.
	 */
	if (fbc->active &&
	    intel_fbc_reg_params_equal(&old_params, &fbc->params))
		return;

	intel_fbc_deactivate(dev_priv);
	intel_fbc_schedule_activation(crtc);
	fbc->no_fbc_reason = "FBC enabled (active or scheduled)";
}

void intel_fbc_post_update(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!fbc_supported(dev_priv))
		return;

	mutex_lock(&fbc->lock);
	__intel_fbc_post_update(crtc);
	mutex_unlock(&fbc->lock);
}

static unsigned int intel_fbc_get_frontbuffer_bit(struct intel_fbc *fbc)
{
	if (fbc->enabled)
		return to_intel_plane(fbc->crtc->base.primary)->frontbuffer_bit;
	else
		return fbc->possible_framebuffer_bits;
}

void intel_fbc_invalidate(struct drm_i915_private *dev_priv,
			  unsigned int frontbuffer_bits,
			  enum fb_op_origin origin)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!fbc_supported(dev_priv))
		return;

	if (origin == ORIGIN_GTT || origin == ORIGIN_FLIP)
		return;

	mutex_lock(&fbc->lock);

	fbc->busy_bits |= intel_fbc_get_frontbuffer_bit(fbc) & frontbuffer_bits;

	if (fbc->enabled && fbc->busy_bits)
		intel_fbc_deactivate(dev_priv);

	mutex_unlock(&fbc->lock);
}

void intel_fbc_flush(struct drm_i915_private *dev_priv,
		     unsigned int frontbuffer_bits, enum fb_op_origin origin)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!fbc_supported(dev_priv))
		return;

	mutex_lock(&fbc->lock);

	fbc->busy_bits &= ~frontbuffer_bits;

	if (origin == ORIGIN_GTT || origin == ORIGIN_FLIP)
		goto out;

	if (!fbc->busy_bits && fbc->enabled &&
	    (frontbuffer_bits & intel_fbc_get_frontbuffer_bit(fbc))) {
		if (fbc->active)
			intel_fbc_recompress(dev_priv);
		else
			__intel_fbc_post_update(fbc->crtc);
	}

out:
	mutex_unlock(&fbc->lock);
}

/**
 * intel_fbc_choose_crtc - select a CRTC to enable FBC on
 * @dev_priv: i915 device instance
 * @state: the atomic state structure
 *
 * This function looks at the proposed state for CRTCs and planes, then chooses
 * which pipe is going to have FBC by setting intel_crtc_state->enable_fbc to
 * true.
 *
 * Later, intel_fbc_enable is going to look for state->enable_fbc and then maybe
 * enable FBC for the chosen CRTC. If it does, it will set dev_priv->fbc.crtc.
 */
void intel_fbc_choose_crtc(struct drm_i915_private *dev_priv,
			   struct drm_atomic_state *state)
{
	struct intel_fbc *fbc = &dev_priv->fbc;
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	bool crtc_chosen = false;
	int i;

	mutex_lock(&fbc->lock);

	/* Does this atomic commit involve the CRTC currently tied to FBC? */
	if (fbc->crtc &&
	    !drm_atomic_get_existing_crtc_state(state, &fbc->crtc->base))
		goto out;

	if (!intel_fbc_can_enable(dev_priv))
		goto out;

	/* Simply choose the first CRTC that is compatible and has a visible
	 * plane. We could go for fancier schemes such as checking the plane
	 * size, but this would just affect the few platforms that don't tie FBC
	 * to pipe or plane A. */
	for_each_new_plane_in_state(state, plane, plane_state, i) {
		struct intel_plane_state *intel_plane_state =
			to_intel_plane_state(plane_state);
		struct intel_crtc_state *intel_crtc_state;
		struct intel_crtc *crtc = to_intel_crtc(plane_state->crtc);

		if (!intel_plane_state->base.visible)
			continue;

		if (fbc_on_pipe_a_only(dev_priv) && crtc->pipe != PIPE_A)
			continue;

		if (fbc_on_plane_a_only(dev_priv) && crtc->i9xx_plane != PLANE_A)
			continue;

		intel_crtc_state = to_intel_crtc_state(
			drm_atomic_get_existing_crtc_state(state, &crtc->base));

		intel_crtc_state->enable_fbc = true;
		crtc_chosen = true;
		break;
	}

	if (!crtc_chosen)
		fbc->no_fbc_reason = "no suitable CRTC for FBC";

out:
	mutex_unlock(&fbc->lock);
}

/**
 * intel_fbc_enable: tries to enable FBC on the CRTC
 * @crtc: the CRTC
 * @crtc_state: corresponding &drm_crtc_state for @crtc
 * @plane_state: corresponding &drm_plane_state for the primary plane of @crtc
 *
 * This function checks if the given CRTC was chosen for FBC, then enables it if
 * possible. Notice that it doesn't activate FBC. It is valid to call
 * intel_fbc_enable multiple times for the same pipe without an
 * intel_fbc_disable in the middle, as long as it is deactivated.
 */
void intel_fbc_enable(struct intel_crtc *crtc,
		      struct intel_crtc_state *crtc_state,
		      struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!fbc_supported(dev_priv))
		return;

	mutex_lock(&fbc->lock);

	if (fbc->enabled) {
		WARN_ON(fbc->crtc == NULL);
		if (fbc->crtc == crtc) {
			WARN_ON(!crtc_state->enable_fbc);
			WARN_ON(fbc->active);
		}
		goto out;
	}

	if (!crtc_state->enable_fbc)
		goto out;

	WARN_ON(fbc->active);
	WARN_ON(fbc->crtc != NULL);

	intel_fbc_update_state_cache(crtc, crtc_state, plane_state);
	if (intel_fbc_alloc_cfb(crtc)) {
		fbc->no_fbc_reason = "not enough stolen memory";
		goto out;
	}

	DRM_DEBUG_KMS("Enabling FBC on pipe %c\n", pipe_name(crtc->pipe));
	fbc->no_fbc_reason = "FBC enabled but not active yet\n";

	fbc->enabled = true;
	fbc->crtc = crtc;
out:
	mutex_unlock(&fbc->lock);
}

/**
 * __intel_fbc_disable - disable FBC
 * @dev_priv: i915 device instance
 *
 * This is the low level function that actually disables FBC. Callers should
 * grab the FBC lock.
 */
static void __intel_fbc_disable(struct drm_i915_private *dev_priv)
{
	struct intel_fbc *fbc = &dev_priv->fbc;
	struct intel_crtc *crtc = fbc->crtc;

	WARN_ON(!mutex_is_locked(&fbc->lock));
	WARN_ON(!fbc->enabled);
	WARN_ON(fbc->active);
	WARN_ON(crtc->active);

	DRM_DEBUG_KMS("Disabling FBC on pipe %c\n", pipe_name(crtc->pipe));

	__intel_fbc_cleanup_cfb(dev_priv);

	fbc->enabled = false;
	fbc->crtc = NULL;
}

/**
 * intel_fbc_disable - disable FBC if it's associated with crtc
 * @crtc: the CRTC
 *
 * This function disables FBC if it's associated with the provided CRTC.
 */
void intel_fbc_disable(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!fbc_supported(dev_priv))
		return;

	mutex_lock(&fbc->lock);
	if (fbc->crtc == crtc)
		__intel_fbc_disable(dev_priv);
	mutex_unlock(&fbc->lock);

	cancel_work_sync(&fbc->work.work);
}

/**
 * intel_fbc_global_disable - globally disable FBC
 * @dev_priv: i915 device instance
 *
 * This function disables FBC regardless of which CRTC is associated with it.
 */
void intel_fbc_global_disable(struct drm_i915_private *dev_priv)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!fbc_supported(dev_priv))
		return;

	mutex_lock(&fbc->lock);
	if (fbc->enabled)
		__intel_fbc_disable(dev_priv);
	mutex_unlock(&fbc->lock);

	cancel_work_sync(&fbc->work.work);
}

static void intel_fbc_underrun_work_fn(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private, fbc.underrun_work);
	struct intel_fbc *fbc = &dev_priv->fbc;

	mutex_lock(&fbc->lock);

	/* Maybe we were scheduled twice. */
	if (fbc->underrun_detected || !fbc->enabled)
		goto out;

	DRM_DEBUG_KMS("Disabling FBC due to FIFO underrun.\n");
	fbc->underrun_detected = true;

	intel_fbc_deactivate(dev_priv);
out:
	mutex_unlock(&fbc->lock);
}

/**
 * intel_fbc_handle_fifo_underrun_irq - disable FBC when we get a FIFO underrun
 * @dev_priv: i915 device instance
 *
 * Without FBC, most underruns are harmless and don't really cause too many
 * problems, except for an annoying message on dmesg. With FBC, underruns can
 * become black screens or even worse, especially when paired with bad
 * watermarks. So in order for us to be on the safe side, completely disable FBC
 * in case we ever detect a FIFO underrun on any pipe. An underrun on any pipe
 * already suggests that watermarks may be bad, so try to be as safe as
 * possible.
 *
 * This function is called from the IRQ handler.
 */
void intel_fbc_handle_fifo_underrun_irq(struct drm_i915_private *dev_priv)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!fbc_supported(dev_priv))
		return;

	/* There's no guarantee that underrun_detected won't be set to true
	 * right after this check and before the work is scheduled, but that's
	 * not a problem since we'll check it again under the work function
	 * while FBC is locked. This check here is just to prevent us from
	 * unnecessarily scheduling the work, and it relies on the fact that we
	 * never switch underrun_detect back to false after it's true. */
	if (READ_ONCE(fbc->underrun_detected))
		return;

	schedule_work(&fbc->underrun_work);
}

/**
 * intel_fbc_init_pipe_state - initialize FBC's CRTC visibility tracking
 * @dev_priv: i915 device instance
 *
 * The FBC code needs to track CRTC visibility since the older platforms can't
 * have FBC enabled while multiple pipes are used. This function does the
 * initial setup at driver load to make sure FBC is matching the real hardware.
 */
void intel_fbc_init_pipe_state(struct drm_i915_private *dev_priv)
{
	struct intel_crtc *crtc;

	/* Don't even bother tracking anything if we don't need. */
	if (!no_fbc_on_multiple_pipes(dev_priv))
		return;

	for_each_intel_crtc(&dev_priv->drm, crtc)
		if (intel_crtc_active(crtc) &&
		    crtc->base.primary->state->visible)
			dev_priv->fbc.visible_pipes_mask |= (1 << crtc->pipe);
}

/*
 * The DDX driver changes its behavior depending on the value it reads from
 * i915.enable_fbc, so sanitize it by translating the default value into either
 * 0 or 1 in order to allow it to know what's going on.
 *
 * Notice that this is done at driver initialization and we still allow user
 * space to change the value during runtime without sanitizing it again. IGT
 * relies on being able to change i915.enable_fbc at runtime.
 */
static int intel_sanitize_fbc_option(struct drm_i915_private *dev_priv)
{
	if (i915_modparams.enable_fbc >= 0)
		return !!i915_modparams.enable_fbc;

	if (!HAS_FBC(dev_priv))
		return 0;

	if (IS_BROADWELL(dev_priv) || INTEL_GEN(dev_priv) >= 9)
		return 1;

	return 0;
}

static bool need_fbc_vtd_wa(struct drm_i915_private *dev_priv)
{
	/* WaFbcTurnOffFbcWhenHyperVisorIsUsed:skl,bxt */
	if (intel_vtd_active() &&
	    (IS_SKYLAKE(dev_priv) || IS_BROXTON(dev_priv))) {
		DRM_INFO("Disabling framebuffer compression (FBC) to prevent screen flicker with VT-d enabled\n");
		return true;
	}

	return false;
}

/**
 * intel_fbc_init - Initialize FBC
 * @dev_priv: the i915 device
 *
 * This function might be called during PM init process.
 */
void intel_fbc_init(struct drm_i915_private *dev_priv)
{
	struct intel_fbc *fbc = &dev_priv->fbc;
	enum pipe pipe;

	INIT_WORK(&fbc->work.work, intel_fbc_work_fn);
	INIT_WORK(&fbc->underrun_work, intel_fbc_underrun_work_fn);
	mutex_init(&fbc->lock);
	fbc->enabled = false;
	fbc->active = false;
	fbc->work.scheduled = false;

	if (need_fbc_vtd_wa(dev_priv))
		mkwrite_device_info(dev_priv)->has_fbc = false;

	i915_modparams.enable_fbc = intel_sanitize_fbc_option(dev_priv);
	DRM_DEBUG_KMS("Sanitized enable_fbc value: %d\n",
		      i915_modparams.enable_fbc);

	if (!HAS_FBC(dev_priv)) {
		fbc->no_fbc_reason = "unsupported by this chipset";
		return;
	}

	for_each_pipe(dev_priv, pipe) {
		fbc->possible_framebuffer_bits |=
				INTEL_FRONTBUFFER_PRIMARY(pipe);

		if (fbc_on_pipe_a_only(dev_priv))
			break;
	}

	/* This value was pulled out of someone's hat */
	if (INTEL_GEN(dev_priv) <= 4 && !IS_GM45(dev_priv))
		I915_WRITE(FBC_CONTROL, 500 << FBC_CTL_INTERVAL_SHIFT);

	/* We still don't have any sort of hardware state readout for FBC, so
	 * deactivate it in case the BIOS activated it to make sure software
	 * matches the hardware state. */
	if (intel_fbc_hw_is_active(dev_priv))
		intel_fbc_hw_deactivate(dev_priv);
}

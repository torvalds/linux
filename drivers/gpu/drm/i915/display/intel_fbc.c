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

#include <drm/drm_fourcc.h>

#include "i915_drv.h"
#include "i915_trace.h"
#include "i915_vgpu.h"
#include "intel_display_types.h"
#include "intel_fbc.h"
#include "intel_frontbuffer.h"

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
static void intel_fbc_get_plane_source_size(const struct intel_fbc_state_cache *cache,
					    int *width, int *height)
{
	if (width)
		*width = cache->plane.src_w;
	if (height)
		*height = cache->plane.src_h;
}

static int intel_fbc_calculate_cfb_size(struct drm_i915_private *dev_priv,
					const struct intel_fbc_state_cache *cache)
{
	int lines;

	intel_fbc_get_plane_source_size(cache, NULL, &lines);
	if (IS_GEN(dev_priv, 7))
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
	fbc_ctl = intel_de_read(dev_priv, FBC_CONTROL);
	if ((fbc_ctl & FBC_CTL_EN) == 0)
		return;

	fbc_ctl &= ~FBC_CTL_EN;
	intel_de_write(dev_priv, FBC_CONTROL, fbc_ctl);

	/* Wait for compressing bit to clear */
	if (intel_de_wait_for_clear(dev_priv, FBC_STATUS,
				    FBC_STAT_COMPRESSING, 10)) {
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
	if (IS_GEN(dev_priv, 2))
		cfb_pitch = (cfb_pitch / 32) - 1;
	else
		cfb_pitch = (cfb_pitch / 64) - 1;

	/* Clear old tags */
	for (i = 0; i < (FBC_LL_SIZE / 32) + 1; i++)
		intel_de_write(dev_priv, FBC_TAG(i), 0);

	if (IS_GEN(dev_priv, 4)) {
		u32 fbc_ctl2;

		/* Set it up... */
		fbc_ctl2 = FBC_CTL_FENCE_DBL | FBC_CTL_IDLE_IMM;
		fbc_ctl2 |= FBC_CTL_PLANE(params->crtc.i9xx_plane);
		if (params->fence_id >= 0)
			fbc_ctl2 |= FBC_CTL_CPU_FENCE;
		intel_de_write(dev_priv, FBC_CONTROL2, fbc_ctl2);
		intel_de_write(dev_priv, FBC_FENCE_OFF,
			       params->crtc.fence_y_offset);
	}

	/* enable it... */
	fbc_ctl = intel_de_read(dev_priv, FBC_CONTROL);
	fbc_ctl &= 0x3fff << FBC_CTL_INTERVAL_SHIFT;
	fbc_ctl |= FBC_CTL_EN | FBC_CTL_PERIODIC;
	if (IS_I945GM(dev_priv))
		fbc_ctl |= FBC_CTL_C3_IDLE; /* 945 needs special SR handling */
	fbc_ctl |= (cfb_pitch & 0xff) << FBC_CTL_STRIDE_SHIFT;
	if (params->fence_id >= 0)
		fbc_ctl |= params->fence_id;
	intel_de_write(dev_priv, FBC_CONTROL, fbc_ctl);
}

static bool i8xx_fbc_is_active(struct drm_i915_private *dev_priv)
{
	return intel_de_read(dev_priv, FBC_CONTROL) & FBC_CTL_EN;
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

	if (params->fence_id >= 0) {
		dpfc_ctl |= DPFC_CTL_FENCE_EN | params->fence_id;
		intel_de_write(dev_priv, DPFC_FENCE_YOFF,
			       params->crtc.fence_y_offset);
	} else {
		intel_de_write(dev_priv, DPFC_FENCE_YOFF, 0);
	}

	/* enable it... */
	intel_de_write(dev_priv, DPFC_CONTROL, dpfc_ctl | DPFC_CTL_EN);
}

static void g4x_fbc_deactivate(struct drm_i915_private *dev_priv)
{
	u32 dpfc_ctl;

	/* Disable compression */
	dpfc_ctl = intel_de_read(dev_priv, DPFC_CONTROL);
	if (dpfc_ctl & DPFC_CTL_EN) {
		dpfc_ctl &= ~DPFC_CTL_EN;
		intel_de_write(dev_priv, DPFC_CONTROL, dpfc_ctl);
	}
}

static bool g4x_fbc_is_active(struct drm_i915_private *dev_priv)
{
	return intel_de_read(dev_priv, DPFC_CONTROL) & DPFC_CTL_EN;
}

/* This function forces a CFB recompression through the nuke operation. */
static void intel_fbc_recompress(struct drm_i915_private *dev_priv)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	trace_intel_fbc_nuke(fbc->crtc);

	intel_de_write(dev_priv, MSG_FBC_REND_STATE, FBC_REND_NUKE);
	intel_de_posting_read(dev_priv, MSG_FBC_REND_STATE);
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

	if (params->fence_id >= 0) {
		dpfc_ctl |= DPFC_CTL_FENCE_EN;
		if (IS_GEN(dev_priv, 5))
			dpfc_ctl |= params->fence_id;
		if (IS_GEN(dev_priv, 6)) {
			intel_de_write(dev_priv, SNB_DPFC_CTL_SA,
				       SNB_CPU_FENCE_ENABLE | params->fence_id);
			intel_de_write(dev_priv, DPFC_CPU_FENCE_OFFSET,
				       params->crtc.fence_y_offset);
		}
	} else {
		if (IS_GEN(dev_priv, 6)) {
			intel_de_write(dev_priv, SNB_DPFC_CTL_SA, 0);
			intel_de_write(dev_priv, DPFC_CPU_FENCE_OFFSET, 0);
		}
	}

	intel_de_write(dev_priv, ILK_DPFC_FENCE_YOFF,
		       params->crtc.fence_y_offset);
	/* enable it... */
	intel_de_write(dev_priv, ILK_DPFC_CONTROL, dpfc_ctl | DPFC_CTL_EN);

	intel_fbc_recompress(dev_priv);
}

static void ilk_fbc_deactivate(struct drm_i915_private *dev_priv)
{
	u32 dpfc_ctl;

	/* Disable compression */
	dpfc_ctl = intel_de_read(dev_priv, ILK_DPFC_CONTROL);
	if (dpfc_ctl & DPFC_CTL_EN) {
		dpfc_ctl &= ~DPFC_CTL_EN;
		intel_de_write(dev_priv, ILK_DPFC_CONTROL, dpfc_ctl);
	}
}

static bool ilk_fbc_is_active(struct drm_i915_private *dev_priv)
{
	return intel_de_read(dev_priv, ILK_DPFC_CONTROL) & DPFC_CTL_EN;
}

static void gen7_fbc_activate(struct drm_i915_private *dev_priv)
{
	struct intel_fbc_reg_params *params = &dev_priv->fbc.params;
	u32 dpfc_ctl;
	int threshold = dev_priv->fbc.threshold;

	/* Display WA #0529: skl, kbl, bxt. */
	if (IS_GEN9_BC(dev_priv) || IS_BROXTON(dev_priv)) {
		u32 val = intel_de_read(dev_priv, CHICKEN_MISC_4);

		val &= ~(FBC_STRIDE_OVERRIDE | FBC_STRIDE_MASK);

		if (params->gen9_wa_cfb_stride)
			val |= FBC_STRIDE_OVERRIDE | params->gen9_wa_cfb_stride;

		intel_de_write(dev_priv, CHICKEN_MISC_4, val);
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

	if (params->fence_id >= 0) {
		dpfc_ctl |= IVB_DPFC_CTL_FENCE_EN;
		intel_de_write(dev_priv, SNB_DPFC_CTL_SA,
			       SNB_CPU_FENCE_ENABLE | params->fence_id);
		intel_de_write(dev_priv, DPFC_CPU_FENCE_OFFSET,
			       params->crtc.fence_y_offset);
	} else if (dev_priv->ggtt.num_fences) {
		intel_de_write(dev_priv, SNB_DPFC_CTL_SA, 0);
		intel_de_write(dev_priv, DPFC_CPU_FENCE_OFFSET, 0);
	}

	if (dev_priv->fbc.false_color)
		dpfc_ctl |= FBC_CTL_FALSE_COLOR;

	if (IS_IVYBRIDGE(dev_priv)) {
		/* WaFbcAsynchFlipDisableFbcQueue:ivb */
		intel_de_write(dev_priv, ILK_DISPLAY_CHICKEN1,
			       intel_de_read(dev_priv, ILK_DISPLAY_CHICKEN1) | ILK_FBCQ_DIS);
	} else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		/* WaFbcAsynchFlipDisableFbcQueue:hsw,bdw */
		intel_de_write(dev_priv, CHICKEN_PIPESL_1(params->crtc.pipe),
			       intel_de_read(dev_priv, CHICKEN_PIPESL_1(params->crtc.pipe)) | HSW_FBCQ_DIS);
	}

	if (INTEL_GEN(dev_priv) >= 11)
		/* Wa_1409120013:icl,ehl,tgl */
		intel_de_write(dev_priv, ILK_DPFC_CHICKEN,
			       ILK_DPFC_CHICKEN_COMP_DUMMY_PIXEL);

	intel_de_write(dev_priv, ILK_DPFC_CONTROL, dpfc_ctl | DPFC_CTL_EN);

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

	trace_intel_fbc_activate(fbc->crtc);

	fbc->active = true;
	fbc->activated = true;

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

	trace_intel_fbc_deactivate(fbc->crtc);

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

static void intel_fbc_deactivate(struct drm_i915_private *dev_priv,
				 const char *reason)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	drm_WARN_ON(&dev_priv->drm, !mutex_is_locked(&fbc->lock));

	if (fbc->active)
		intel_fbc_hw_deactivate(dev_priv);

	fbc->no_fbc_reason = reason;
}

static int find_compression_threshold(struct drm_i915_private *dev_priv,
				      struct drm_mm_node *node,
				      unsigned int size,
				      unsigned int fb_cpp)
{
	int compression_threshold = 1;
	int ret;
	u64 end;

	/* The FBC hardware for BDW/SKL doesn't have access to the stolen
	 * reserved range size, so it always assumes the maximum (8mb) is used.
	 * If we enable FBC using a CFB on that memory range we'll get FIFO
	 * underruns, even if that range is not reserved by the BIOS. */
	if (IS_BROADWELL(dev_priv) || IS_GEN9_BC(dev_priv))
		end = resource_size(&dev_priv->dsm) - 8 * 1024 * 1024;
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

static int intel_fbc_alloc_cfb(struct drm_i915_private *dev_priv,
			       unsigned int size, unsigned int fb_cpp)
{
	struct intel_fbc *fbc = &dev_priv->fbc;
	struct drm_mm_node *uninitialized_var(compressed_llb);
	int ret;

	drm_WARN_ON(&dev_priv->drm,
		    drm_mm_node_allocated(&fbc->compressed_fb));

	ret = find_compression_threshold(dev_priv, &fbc->compressed_fb,
					 size, fb_cpp);
	if (!ret)
		goto err_llb;
	else if (ret > 1) {
		DRM_INFO("Reducing the compressed framebuffer size. This may lead to less power savings than a non-reduced-size. Try to increase stolen memory size if available in BIOS.\n");

	}

	fbc->threshold = ret;

	if (INTEL_GEN(dev_priv) >= 5)
		intel_de_write(dev_priv, ILK_DPFC_CB_BASE,
			       fbc->compressed_fb.start);
	else if (IS_GM45(dev_priv)) {
		intel_de_write(dev_priv, DPFC_CB_BASE,
			       fbc->compressed_fb.start);
	} else {
		compressed_llb = kzalloc(sizeof(*compressed_llb), GFP_KERNEL);
		if (!compressed_llb)
			goto err_fb;

		ret = i915_gem_stolen_insert_node(dev_priv, compressed_llb,
						  4096, 4096);
		if (ret)
			goto err_fb;

		fbc->compressed_llb = compressed_llb;

		GEM_BUG_ON(range_overflows_end_t(u64, dev_priv->dsm.start,
						 fbc->compressed_fb.start,
						 U32_MAX));
		GEM_BUG_ON(range_overflows_end_t(u64, dev_priv->dsm.start,
						 fbc->compressed_llb->start,
						 U32_MAX));
		intel_de_write(dev_priv, FBC_CFB_BASE,
			       dev_priv->dsm.start + fbc->compressed_fb.start);
		intel_de_write(dev_priv, FBC_LL_BASE,
			       dev_priv->dsm.start + compressed_llb->start);
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

	if (!drm_mm_node_allocated(&fbc->compressed_fb))
		return;

	if (fbc->compressed_llb) {
		i915_gem_stolen_remove_node(dev_priv, fbc->compressed_llb);
		kfree(fbc->compressed_llb);
	}

	i915_gem_stolen_remove_node(dev_priv, &fbc->compressed_fb);
}

void intel_fbc_cleanup_cfb(struct drm_i915_private *dev_priv)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!HAS_FBC(dev_priv))
		return;

	mutex_lock(&fbc->lock);
	__intel_fbc_cleanup_cfb(dev_priv);
	mutex_unlock(&fbc->lock);
}

static bool stride_is_valid(struct drm_i915_private *dev_priv,
			    unsigned int stride)
{
	/* This should have been caught earlier. */
	if (drm_WARN_ON_ONCE(&dev_priv->drm, (stride & (64 - 1)) != 0))
		return false;

	/* Below are the additional FBC restrictions. */
	if (stride < 512)
		return false;

	if (IS_GEN(dev_priv, 2) || IS_GEN(dev_priv, 3))
		return stride == 4096 || stride == 8192;

	if (IS_GEN(dev_priv, 4) && !IS_G4X(dev_priv) && stride < 2048)
		return false;

	if (stride > 16384)
		return false;

	return true;
}

static bool pixel_format_is_valid(struct drm_i915_private *dev_priv,
				  u32 pixel_format)
{
	switch (pixel_format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
		return true;
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_RGB565:
		/* 16bpp not supported on gen2 */
		if (IS_GEN(dev_priv, 2))
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

	if (INTEL_GEN(dev_priv) >= 10 || IS_GEMINILAKE(dev_priv)) {
		max_w = 5120;
		max_h = 4096;
	} else if (INTEL_GEN(dev_priv) >= 8 || IS_HASWELL(dev_priv)) {
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
					 const struct intel_crtc_state *crtc_state,
					 const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;
	struct intel_fbc_state_cache *cache = &fbc->state_cache;
	struct drm_framebuffer *fb = plane_state->hw.fb;

	cache->plane.visible = plane_state->uapi.visible;
	if (!cache->plane.visible)
		return;

	cache->crtc.mode_flags = crtc_state->hw.adjusted_mode.flags;
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		cache->crtc.hsw_bdw_pixel_rate = crtc_state->pixel_rate;

	cache->plane.rotation = plane_state->hw.rotation;
	/*
	 * Src coordinates are already rotated by 270 degrees for
	 * the 90/270 degree plane rotation cases (to match the
	 * GTT mapping), hence no need to account for rotation here.
	 */
	cache->plane.src_w = drm_rect_width(&plane_state->uapi.src) >> 16;
	cache->plane.src_h = drm_rect_height(&plane_state->uapi.src) >> 16;
	cache->plane.adjusted_x = plane_state->color_plane[0].x;
	cache->plane.adjusted_y = plane_state->color_plane[0].y;
	cache->plane.y = plane_state->uapi.src.y1 >> 16;

	cache->plane.pixel_blend_mode = plane_state->hw.pixel_blend_mode;

	cache->fb.format = fb->format;
	cache->fb.stride = fb->pitches[0];

	drm_WARN_ON(&dev_priv->drm, plane_state->flags & PLANE_HAS_FENCE &&
		    !plane_state->vma->fence);

	if (plane_state->flags & PLANE_HAS_FENCE &&
	    plane_state->vma->fence)
		cache->fence_id = plane_state->vma->fence->id;
	else
		cache->fence_id = -1;
}

static bool intel_fbc_cfb_size_changed(struct drm_i915_private *dev_priv)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	return intel_fbc_calculate_cfb_size(dev_priv, &fbc->state_cache) >
		fbc->compressed_fb.size * fbc->threshold;
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

static bool intel_fbc_can_activate(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;
	struct intel_fbc_state_cache *cache = &fbc->state_cache;

	if (!intel_fbc_can_enable(dev_priv))
		return false;

	if (!cache->plane.visible) {
		fbc->no_fbc_reason = "primary plane not visible";
		return false;
	}

	/* We don't need to use a state cache here since this information is
	 * global for all CRTC.
	 */
	if (fbc->underrun_detected) {
		fbc->no_fbc_reason = "underrun detected";
		return false;
	}

	if (cache->crtc.mode_flags & DRM_MODE_FLAG_INTERLACE) {
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
	 *
	 * FIXME with 90/270 degree rotation we should use the fence on
	 * the normal GTT view (the rotated view doesn't even have a
	 * fence). Would need changes to the FBC fence Y offset as well.
	 * For now this will effecively disable FBC with 90/270 degree
	 * rotation.
	 */
	if (cache->fence_id < 0) {
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

	if (cache->plane.pixel_blend_mode != DRM_MODE_BLEND_PIXEL_NONE &&
	    cache->fb.format->has_alpha) {
		fbc->no_fbc_reason = "per-pixel alpha blending is incompatible with FBC";
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
	if (intel_fbc_cfb_size_changed(dev_priv)) {
		fbc->no_fbc_reason = "CFB requirements changed";
		return false;
	}

	/*
	 * Work around a problem on GEN9+ HW, where enabling FBC on a plane
	 * having a Y offset that isn't divisible by 4 causes FIFO underrun
	 * and screen flicker.
	 */
	if (INTEL_GEN(dev_priv) >= 9 &&
	    (fbc->state_cache.plane.adjusted_y & 3)) {
		fbc->no_fbc_reason = "plane Y offset is misaligned";
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

	params->fence_id = cache->fence_id;

	params->crtc.pipe = crtc->pipe;
	params->crtc.i9xx_plane = to_intel_plane(crtc->base.primary)->i9xx_plane;
	params->crtc.fence_y_offset = get_crtc_fence_y_offset(fbc);

	params->fb.format = cache->fb.format;
	params->fb.stride = cache->fb.stride;

	params->cfb_size = intel_fbc_calculate_cfb_size(dev_priv, cache);

	params->gen9_wa_cfb_stride = cache->gen9_wa_cfb_stride;

	params->plane_visible = cache->plane.visible;
}

static bool intel_fbc_can_flip_nuke(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct intel_fbc *fbc = &dev_priv->fbc;
	const struct intel_fbc_state_cache *cache = &fbc->state_cache;
	const struct intel_fbc_reg_params *params = &fbc->params;

	if (drm_atomic_crtc_needs_modeset(&crtc_state->uapi))
		return false;

	if (!params->plane_visible)
		return false;

	if (!intel_fbc_can_activate(crtc))
		return false;

	if (params->fb.format != cache->fb.format)
		return false;

	if (params->fb.stride != cache->fb.stride)
		return false;

	if (params->cfb_size != intel_fbc_calculate_cfb_size(dev_priv, cache))
		return false;

	if (params->gen9_wa_cfb_stride != cache->gen9_wa_cfb_stride)
		return false;

	return true;
}

bool intel_fbc_pre_update(struct intel_atomic_state *state,
			  struct intel_crtc *crtc)
{
	struct intel_plane *plane = to_intel_plane(crtc->base.primary);
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_plane_state *plane_state =
		intel_atomic_get_new_plane_state(state, plane);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;
	const char *reason = "update pending";
	bool need_vblank_wait = false;

	if (!plane->has_fbc || !plane_state)
		return need_vblank_wait;

	mutex_lock(&fbc->lock);

	if (fbc->crtc != crtc)
		goto unlock;

	intel_fbc_update_state_cache(crtc, crtc_state, plane_state);
	fbc->flip_pending = true;

	if (!intel_fbc_can_flip_nuke(crtc_state)) {
		intel_fbc_deactivate(dev_priv, reason);

		/*
		 * Display WA #1198: glk+
		 * Need an extra vblank wait between FBC disable and most plane
		 * updates. Bspec says this is only needed for plane disable, but
		 * that is not true. Touching most plane registers will cause the
		 * corruption to appear. Also SKL/derivatives do not seem to be
		 * affected.
		 *
		 * TODO: could optimize this a bit by sampling the frame
		 * counter when we disable FBC (if it was already done earlier)
		 * and skipping the extra vblank wait before the plane update
		 * if at least one frame has already passed.
		 */
		if (fbc->activated &&
		    (INTEL_GEN(dev_priv) >= 10 || IS_GEMINILAKE(dev_priv)))
			need_vblank_wait = true;
		fbc->activated = false;
	}
unlock:
	mutex_unlock(&fbc->lock);

	return need_vblank_wait;
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

	drm_WARN_ON(&dev_priv->drm, !mutex_is_locked(&fbc->lock));
	drm_WARN_ON(&dev_priv->drm, !fbc->crtc);
	drm_WARN_ON(&dev_priv->drm, fbc->active);

	DRM_DEBUG_KMS("Disabling FBC on pipe %c\n", pipe_name(crtc->pipe));

	__intel_fbc_cleanup_cfb(dev_priv);

	fbc->crtc = NULL;
}

static void __intel_fbc_post_update(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_fbc *fbc = &dev_priv->fbc;

	drm_WARN_ON(&dev_priv->drm, !mutex_is_locked(&fbc->lock));

	if (fbc->crtc != crtc)
		return;

	fbc->flip_pending = false;

	if (!i915_modparams.enable_fbc) {
		intel_fbc_deactivate(dev_priv, "disabled at runtime per module param");
		__intel_fbc_disable(dev_priv);

		return;
	}

	intel_fbc_get_reg_params(crtc, &fbc->params);

	if (!intel_fbc_can_activate(crtc))
		return;

	if (!fbc->busy_bits)
		intel_fbc_hw_activate(dev_priv);
	else
		intel_fbc_deactivate(dev_priv, "frontbuffer write");
}

void intel_fbc_post_update(struct intel_atomic_state *state,
			   struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_plane *plane = to_intel_plane(crtc->base.primary);
	const struct intel_plane_state *plane_state =
		intel_atomic_get_new_plane_state(state, plane);
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!plane->has_fbc || !plane_state)
		return;

	mutex_lock(&fbc->lock);
	__intel_fbc_post_update(crtc);
	mutex_unlock(&fbc->lock);
}

static unsigned int intel_fbc_get_frontbuffer_bit(struct intel_fbc *fbc)
{
	if (fbc->crtc)
		return to_intel_plane(fbc->crtc->base.primary)->frontbuffer_bit;
	else
		return fbc->possible_framebuffer_bits;
}

void intel_fbc_invalidate(struct drm_i915_private *dev_priv,
			  unsigned int frontbuffer_bits,
			  enum fb_op_origin origin)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!HAS_FBC(dev_priv))
		return;

	if (origin == ORIGIN_GTT || origin == ORIGIN_FLIP)
		return;

	mutex_lock(&fbc->lock);

	fbc->busy_bits |= intel_fbc_get_frontbuffer_bit(fbc) & frontbuffer_bits;

	if (fbc->crtc && fbc->busy_bits)
		intel_fbc_deactivate(dev_priv, "frontbuffer write");

	mutex_unlock(&fbc->lock);
}

void intel_fbc_flush(struct drm_i915_private *dev_priv,
		     unsigned int frontbuffer_bits, enum fb_op_origin origin)
{
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!HAS_FBC(dev_priv))
		return;

	mutex_lock(&fbc->lock);

	fbc->busy_bits &= ~frontbuffer_bits;

	if (origin == ORIGIN_GTT || origin == ORIGIN_FLIP)
		goto out;

	if (!fbc->busy_bits && fbc->crtc &&
	    (frontbuffer_bits & intel_fbc_get_frontbuffer_bit(fbc))) {
		if (fbc->active)
			intel_fbc_recompress(dev_priv);
		else if (!fbc->flip_pending)
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
			   struct intel_atomic_state *state)
{
	struct intel_fbc *fbc = &dev_priv->fbc;
	struct intel_plane *plane;
	struct intel_plane_state *plane_state;
	bool crtc_chosen = false;
	int i;

	mutex_lock(&fbc->lock);

	/* Does this atomic commit involve the CRTC currently tied to FBC? */
	if (fbc->crtc &&
	    !intel_atomic_get_new_crtc_state(state, fbc->crtc))
		goto out;

	if (!intel_fbc_can_enable(dev_priv))
		goto out;

	/* Simply choose the first CRTC that is compatible and has a visible
	 * plane. We could go for fancier schemes such as checking the plane
	 * size, but this would just affect the few platforms that don't tie FBC
	 * to pipe or plane A. */
	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		struct intel_crtc_state *crtc_state;
		struct intel_crtc *crtc = to_intel_crtc(plane_state->hw.crtc);

		if (!plane->has_fbc)
			continue;

		if (!plane_state->uapi.visible)
			continue;

		crtc_state = intel_atomic_get_new_crtc_state(state, crtc);

		crtc_state->enable_fbc = true;
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
 * @state: corresponding &drm_crtc_state for @crtc
 *
 * This function checks if the given CRTC was chosen for FBC, then enables it if
 * possible. Notice that it doesn't activate FBC. It is valid to call
 * intel_fbc_enable multiple times for the same pipe without an
 * intel_fbc_disable in the middle, as long as it is deactivated.
 */
void intel_fbc_enable(struct intel_atomic_state *state,
		      struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_plane *plane = to_intel_plane(crtc->base.primary);
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_plane_state *plane_state =
		intel_atomic_get_new_plane_state(state, plane);
	struct intel_fbc *fbc = &dev_priv->fbc;
	struct intel_fbc_state_cache *cache = &fbc->state_cache;

	if (!plane->has_fbc || !plane_state)
		return;

	mutex_lock(&fbc->lock);

	if (fbc->crtc) {
		if (fbc->crtc != crtc ||
		    !intel_fbc_cfb_size_changed(dev_priv))
			goto out;

		__intel_fbc_disable(dev_priv);
	}

	drm_WARN_ON(&dev_priv->drm, fbc->active);

	intel_fbc_update_state_cache(crtc, crtc_state, plane_state);

	/* FIXME crtc_state->enable_fbc lies :( */
	if (!cache->plane.visible)
		goto out;

	if (intel_fbc_alloc_cfb(dev_priv,
				intel_fbc_calculate_cfb_size(dev_priv, cache),
				plane_state->hw.fb->format->cpp[0])) {
		cache->plane.visible = false;
		fbc->no_fbc_reason = "not enough stolen memory";
		goto out;
	}

	if ((IS_GEN9_BC(dev_priv) || IS_BROXTON(dev_priv)) &&
	    plane_state->hw.fb->modifier != I915_FORMAT_MOD_X_TILED)
		cache->gen9_wa_cfb_stride =
			DIV_ROUND_UP(cache->plane.src_w, 32 * fbc->threshold) * 8;
	else
		cache->gen9_wa_cfb_stride = 0;

	DRM_DEBUG_KMS("Enabling FBC on pipe %c\n", pipe_name(crtc->pipe));
	fbc->no_fbc_reason = "FBC enabled but not active yet\n";

	fbc->crtc = crtc;
out:
	mutex_unlock(&fbc->lock);
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
	struct intel_plane *plane = to_intel_plane(crtc->base.primary);
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!plane->has_fbc)
		return;

	mutex_lock(&fbc->lock);
	if (fbc->crtc == crtc)
		__intel_fbc_disable(dev_priv);
	mutex_unlock(&fbc->lock);
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

	if (!HAS_FBC(dev_priv))
		return;

	mutex_lock(&fbc->lock);
	if (fbc->crtc) {
		drm_WARN_ON(&dev_priv->drm, fbc->crtc->active);
		__intel_fbc_disable(dev_priv);
	}
	mutex_unlock(&fbc->lock);
}

static void intel_fbc_underrun_work_fn(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private, fbc.underrun_work);
	struct intel_fbc *fbc = &dev_priv->fbc;

	mutex_lock(&fbc->lock);

	/* Maybe we were scheduled twice. */
	if (fbc->underrun_detected || !fbc->crtc)
		goto out;

	DRM_DEBUG_KMS("Disabling FBC due to FIFO underrun.\n");
	fbc->underrun_detected = true;

	intel_fbc_deactivate(dev_priv, "FIFO underrun");
out:
	mutex_unlock(&fbc->lock);
}

/*
 * intel_fbc_reset_underrun - reset FBC fifo underrun status.
 * @dev_priv: i915 device instance
 *
 * See intel_fbc_handle_fifo_underrun_irq(). For automated testing we
 * want to re-enable FBC after an underrun to increase test coverage.
 */
int intel_fbc_reset_underrun(struct drm_i915_private *dev_priv)
{
	int ret;

	cancel_work_sync(&dev_priv->fbc.underrun_work);

	ret = mutex_lock_interruptible(&dev_priv->fbc.lock);
	if (ret)
		return ret;

	if (dev_priv->fbc.underrun_detected) {
		DRM_DEBUG_KMS("Re-allowing FBC after fifo underrun\n");
		dev_priv->fbc.no_fbc_reason = "FIFO underrun cleared";
	}

	dev_priv->fbc.underrun_detected = false;
	mutex_unlock(&dev_priv->fbc.lock);

	return 0;
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

	if (!HAS_FBC(dev_priv))
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

	INIT_WORK(&fbc->underrun_work, intel_fbc_underrun_work_fn);
	mutex_init(&fbc->lock);
	fbc->active = false;

	if (!drm_mm_initialized(&dev_priv->mm.stolen))
		mkwrite_device_info(dev_priv)->display.has_fbc = false;

	if (need_fbc_vtd_wa(dev_priv))
		mkwrite_device_info(dev_priv)->display.has_fbc = false;

	i915_modparams.enable_fbc = intel_sanitize_fbc_option(dev_priv);
	DRM_DEBUG_KMS("Sanitized enable_fbc value: %d\n",
		      i915_modparams.enable_fbc);

	if (!HAS_FBC(dev_priv)) {
		fbc->no_fbc_reason = "unsupported by this chipset";
		return;
	}

	/* This value was pulled out of someone's hat */
	if (INTEL_GEN(dev_priv) <= 4 && !IS_GM45(dev_priv))
		intel_de_write(dev_priv, FBC_CONTROL,
		               500 << FBC_CTL_INTERVAL_SHIFT);

	/* We still don't have any sort of hardware state readout for FBC, so
	 * deactivate it in case the BIOS activated it to make sure software
	 * matches the hardware state. */
	if (intel_fbc_hw_is_active(dev_priv))
		intel_fbc_hw_deactivate(dev_priv);
}

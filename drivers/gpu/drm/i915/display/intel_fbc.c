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

#include <linux/string_helpers.h>

#include <drm/drm_blend.h>
#include <drm/drm_fourcc.h>

#include "i915_drv.h"
#include "i915_utils.h"
#include "i915_vgpu.h"
#include "intel_cdclk.h"
#include "intel_de.h"
#include "intel_display_trace.h"
#include "intel_display_types.h"
#include "intel_fbc.h"
#include "intel_frontbuffer.h"

#define for_each_fbc_id(__dev_priv, __fbc_id) \
	for ((__fbc_id) = INTEL_FBC_A; (__fbc_id) < I915_MAX_FBCS; (__fbc_id)++) \
		for_each_if(RUNTIME_INFO(__dev_priv)->fbc_mask & BIT(__fbc_id))

#define for_each_intel_fbc(__dev_priv, __fbc, __fbc_id) \
	for_each_fbc_id((__dev_priv), (__fbc_id)) \
		for_each_if((__fbc) = (__dev_priv)->display.fbc[(__fbc_id)])

struct intel_fbc_funcs {
	void (*activate)(struct intel_fbc *fbc);
	void (*deactivate)(struct intel_fbc *fbc);
	bool (*is_active)(struct intel_fbc *fbc);
	bool (*is_compressing)(struct intel_fbc *fbc);
	void (*nuke)(struct intel_fbc *fbc);
	void (*program_cfb)(struct intel_fbc *fbc);
	void (*set_false_color)(struct intel_fbc *fbc, bool enable);
};

struct intel_fbc_state {
	struct intel_plane *plane;
	unsigned int cfb_stride;
	unsigned int cfb_size;
	unsigned int fence_y_offset;
	u16 override_cfb_stride;
	u16 interval;
	s8 fence_id;
};

struct intel_fbc {
	struct drm_i915_private *i915;
	const struct intel_fbc_funcs *funcs;

	/*
	 * This is always the inner lock when overlapping with
	 * struct_mutex and it's the outer lock when overlapping
	 * with stolen_lock.
	 */
	struct mutex lock;
	unsigned int busy_bits;

	struct drm_mm_node compressed_fb;
	struct drm_mm_node compressed_llb;

	enum intel_fbc_id id;

	u8 limit;

	bool false_color;

	bool active;
	bool activated;
	bool flip_pending;

	bool underrun_detected;
	struct work_struct underrun_work;

	/*
	 * This structure contains everything that's relevant to program the
	 * hardware registers. When we want to figure out if we need to disable
	 * and re-enable FBC for a new configuration we just check if there's
	 * something different in the struct. The genx_fbc_activate functions
	 * are supposed to read from it in order to program the registers.
	 */
	struct intel_fbc_state state;
	const char *no_fbc_reason;
};

/* plane stride in pixels */
static unsigned int intel_fbc_plane_stride(const struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int stride;

	stride = plane_state->view.color_plane[0].mapping_stride;
	if (!drm_rotation_90_or_270(plane_state->hw.rotation))
		stride /= fb->format->cpp[0];

	return stride;
}

/* plane stride based cfb stride in bytes, assuming 1:1 compression limit */
static unsigned int _intel_fbc_cfb_stride(const struct intel_plane_state *plane_state)
{
	unsigned int cpp = 4; /* FBC always 4 bytes per pixel */

	return intel_fbc_plane_stride(plane_state) * cpp;
}

/* minimum acceptable cfb stride in bytes, assuming 1:1 compression limit */
static unsigned int skl_fbc_min_cfb_stride(const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	unsigned int limit = 4; /* 1:4 compression limit is the worst case */
	unsigned int cpp = 4; /* FBC always 4 bytes per pixel */
	unsigned int width = drm_rect_width(&plane_state->uapi.src) >> 16;
	unsigned int height = 4; /* FBC segment is 4 lines */
	unsigned int stride;

	/* minimum segment stride we can use */
	stride = width * cpp * height / limit;

	/*
	 * Wa_16011863758: icl+
	 * Avoid some hardware segment address miscalculation.
	 */
	if (DISPLAY_VER(i915) >= 11)
		stride += 64;

	/*
	 * At least some of the platforms require each 4 line segment to
	 * be 512 byte aligned. Just do it always for simplicity.
	 */
	stride = ALIGN(stride, 512);

	/* convert back to single line equivalent with 1:1 compression limit */
	return stride * limit / height;
}

/* properly aligned cfb stride in bytes, assuming 1:1 compression limit */
static unsigned int intel_fbc_cfb_stride(const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	unsigned int stride = _intel_fbc_cfb_stride(plane_state);

	/*
	 * At least some of the platforms require each 4 line segment to
	 * be 512 byte aligned. Aligning each line to 512 bytes guarantees
	 * that regardless of the compression limit we choose later.
	 */
	if (DISPLAY_VER(i915) >= 9)
		return max(ALIGN(stride, 512), skl_fbc_min_cfb_stride(plane_state));
	else
		return stride;
}

static unsigned int intel_fbc_cfb_size(const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	int lines = drm_rect_height(&plane_state->uapi.src) >> 16;

	if (DISPLAY_VER(i915) == 7)
		lines = min(lines, 2048);
	else if (DISPLAY_VER(i915) >= 8)
		lines = min(lines, 2560);

	return lines * intel_fbc_cfb_stride(plane_state);
}

static u16 intel_fbc_override_cfb_stride(const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	unsigned int stride_aligned = intel_fbc_cfb_stride(plane_state);
	unsigned int stride = _intel_fbc_cfb_stride(plane_state);
	const struct drm_framebuffer *fb = plane_state->hw.fb;

	/*
	 * Override stride in 64 byte units per 4 line segment.
	 *
	 * Gen9 hw miscalculates cfb stride for linear as
	 * PLANE_STRIDE*512 instead of PLANE_STRIDE*64, so
	 * we always need to use the override there.
	 */
	if (stride != stride_aligned ||
	    (DISPLAY_VER(i915) == 9 && fb->modifier == DRM_FORMAT_MOD_LINEAR))
		return stride_aligned * 4 / 64;

	return 0;
}

static u32 i8xx_fbc_ctl(struct intel_fbc *fbc)
{
	const struct intel_fbc_state *fbc_state = &fbc->state;
	struct drm_i915_private *i915 = fbc->i915;
	unsigned int cfb_stride;
	u32 fbc_ctl;

	cfb_stride = fbc_state->cfb_stride / fbc->limit;

	/* FBC_CTL wants 32B or 64B units */
	if (DISPLAY_VER(i915) == 2)
		cfb_stride = (cfb_stride / 32) - 1;
	else
		cfb_stride = (cfb_stride / 64) - 1;

	fbc_ctl = FBC_CTL_PERIODIC |
		FBC_CTL_INTERVAL(fbc_state->interval) |
		FBC_CTL_STRIDE(cfb_stride);

	if (IS_I945GM(i915))
		fbc_ctl |= FBC_CTL_C3_IDLE; /* 945 needs special SR handling */

	if (fbc_state->fence_id >= 0)
		fbc_ctl |= FBC_CTL_FENCENO(fbc_state->fence_id);

	return fbc_ctl;
}

static u32 i965_fbc_ctl2(struct intel_fbc *fbc)
{
	const struct intel_fbc_state *fbc_state = &fbc->state;
	u32 fbc_ctl2;

	fbc_ctl2 = FBC_CTL_FENCE_DBL | FBC_CTL_IDLE_IMM |
		FBC_CTL_PLANE(fbc_state->plane->i9xx_plane);

	if (fbc_state->fence_id >= 0)
		fbc_ctl2 |= FBC_CTL_CPU_FENCE_EN;

	return fbc_ctl2;
}

static void i8xx_fbc_deactivate(struct intel_fbc *fbc)
{
	struct drm_i915_private *i915 = fbc->i915;
	u32 fbc_ctl;

	/* Disable compression */
	fbc_ctl = intel_de_read(i915, FBC_CONTROL);
	if ((fbc_ctl & FBC_CTL_EN) == 0)
		return;

	fbc_ctl &= ~FBC_CTL_EN;
	intel_de_write(i915, FBC_CONTROL, fbc_ctl);

	/* Wait for compressing bit to clear */
	if (intel_de_wait_for_clear(i915, FBC_STATUS,
				    FBC_STAT_COMPRESSING, 10)) {
		drm_dbg_kms(&i915->drm, "FBC idle timed out\n");
		return;
	}
}

static void i8xx_fbc_activate(struct intel_fbc *fbc)
{
	const struct intel_fbc_state *fbc_state = &fbc->state;
	struct drm_i915_private *i915 = fbc->i915;
	int i;

	/* Clear old tags */
	for (i = 0; i < (FBC_LL_SIZE / 32) + 1; i++)
		intel_de_write(i915, FBC_TAG(i), 0);

	if (DISPLAY_VER(i915) == 4) {
		intel_de_write(i915, FBC_CONTROL2,
			       i965_fbc_ctl2(fbc));
		intel_de_write(i915, FBC_FENCE_OFF,
			       fbc_state->fence_y_offset);
	}

	intel_de_write(i915, FBC_CONTROL,
		       FBC_CTL_EN | i8xx_fbc_ctl(fbc));
}

static bool i8xx_fbc_is_active(struct intel_fbc *fbc)
{
	return intel_de_read(fbc->i915, FBC_CONTROL) & FBC_CTL_EN;
}

static bool i8xx_fbc_is_compressing(struct intel_fbc *fbc)
{
	return intel_de_read(fbc->i915, FBC_STATUS) &
		(FBC_STAT_COMPRESSING | FBC_STAT_COMPRESSED);
}

static void i8xx_fbc_nuke(struct intel_fbc *fbc)
{
	struct intel_fbc_state *fbc_state = &fbc->state;
	enum i9xx_plane_id i9xx_plane = fbc_state->plane->i9xx_plane;
	struct drm_i915_private *dev_priv = fbc->i915;

	intel_de_write_fw(dev_priv, DSPADDR(i9xx_plane),
			  intel_de_read_fw(dev_priv, DSPADDR(i9xx_plane)));
}

static void i8xx_fbc_program_cfb(struct intel_fbc *fbc)
{
	struct drm_i915_private *i915 = fbc->i915;

	GEM_BUG_ON(range_overflows_end_t(u64, i915->dsm.stolen.start,
					 fbc->compressed_fb.start, U32_MAX));
	GEM_BUG_ON(range_overflows_end_t(u64, i915->dsm.stolen.start,
					 fbc->compressed_llb.start, U32_MAX));

	intel_de_write(i915, FBC_CFB_BASE,
		       i915->dsm.stolen.start + fbc->compressed_fb.start);
	intel_de_write(i915, FBC_LL_BASE,
		       i915->dsm.stolen.start + fbc->compressed_llb.start);
}

static const struct intel_fbc_funcs i8xx_fbc_funcs = {
	.activate = i8xx_fbc_activate,
	.deactivate = i8xx_fbc_deactivate,
	.is_active = i8xx_fbc_is_active,
	.is_compressing = i8xx_fbc_is_compressing,
	.nuke = i8xx_fbc_nuke,
	.program_cfb = i8xx_fbc_program_cfb,
};

static void i965_fbc_nuke(struct intel_fbc *fbc)
{
	struct intel_fbc_state *fbc_state = &fbc->state;
	enum i9xx_plane_id i9xx_plane = fbc_state->plane->i9xx_plane;
	struct drm_i915_private *dev_priv = fbc->i915;

	intel_de_write_fw(dev_priv, DSPSURF(i9xx_plane),
			  intel_de_read_fw(dev_priv, DSPSURF(i9xx_plane)));
}

static const struct intel_fbc_funcs i965_fbc_funcs = {
	.activate = i8xx_fbc_activate,
	.deactivate = i8xx_fbc_deactivate,
	.is_active = i8xx_fbc_is_active,
	.is_compressing = i8xx_fbc_is_compressing,
	.nuke = i965_fbc_nuke,
	.program_cfb = i8xx_fbc_program_cfb,
};

static u32 g4x_dpfc_ctl_limit(struct intel_fbc *fbc)
{
	switch (fbc->limit) {
	default:
		MISSING_CASE(fbc->limit);
		fallthrough;
	case 1:
		return DPFC_CTL_LIMIT_1X;
	case 2:
		return DPFC_CTL_LIMIT_2X;
	case 4:
		return DPFC_CTL_LIMIT_4X;
	}
}

static u32 g4x_dpfc_ctl(struct intel_fbc *fbc)
{
	const struct intel_fbc_state *fbc_state = &fbc->state;
	struct drm_i915_private *i915 = fbc->i915;
	u32 dpfc_ctl;

	dpfc_ctl = g4x_dpfc_ctl_limit(fbc) |
		DPFC_CTL_PLANE_G4X(fbc_state->plane->i9xx_plane);

	if (IS_G4X(i915))
		dpfc_ctl |= DPFC_CTL_SR_EN;

	if (fbc_state->fence_id >= 0) {
		dpfc_ctl |= DPFC_CTL_FENCE_EN_G4X;

		if (DISPLAY_VER(i915) < 6)
			dpfc_ctl |= DPFC_CTL_FENCENO(fbc_state->fence_id);
	}

	return dpfc_ctl;
}

static void g4x_fbc_activate(struct intel_fbc *fbc)
{
	const struct intel_fbc_state *fbc_state = &fbc->state;
	struct drm_i915_private *i915 = fbc->i915;

	intel_de_write(i915, DPFC_FENCE_YOFF,
		       fbc_state->fence_y_offset);

	intel_de_write(i915, DPFC_CONTROL,
		       DPFC_CTL_EN | g4x_dpfc_ctl(fbc));
}

static void g4x_fbc_deactivate(struct intel_fbc *fbc)
{
	struct drm_i915_private *i915 = fbc->i915;
	u32 dpfc_ctl;

	/* Disable compression */
	dpfc_ctl = intel_de_read(i915, DPFC_CONTROL);
	if (dpfc_ctl & DPFC_CTL_EN) {
		dpfc_ctl &= ~DPFC_CTL_EN;
		intel_de_write(i915, DPFC_CONTROL, dpfc_ctl);
	}
}

static bool g4x_fbc_is_active(struct intel_fbc *fbc)
{
	return intel_de_read(fbc->i915, DPFC_CONTROL) & DPFC_CTL_EN;
}

static bool g4x_fbc_is_compressing(struct intel_fbc *fbc)
{
	return intel_de_read(fbc->i915, DPFC_STATUS) & DPFC_COMP_SEG_MASK;
}

static void g4x_fbc_program_cfb(struct intel_fbc *fbc)
{
	struct drm_i915_private *i915 = fbc->i915;

	intel_de_write(i915, DPFC_CB_BASE, fbc->compressed_fb.start);
}

static const struct intel_fbc_funcs g4x_fbc_funcs = {
	.activate = g4x_fbc_activate,
	.deactivate = g4x_fbc_deactivate,
	.is_active = g4x_fbc_is_active,
	.is_compressing = g4x_fbc_is_compressing,
	.nuke = i965_fbc_nuke,
	.program_cfb = g4x_fbc_program_cfb,
};

static void ilk_fbc_activate(struct intel_fbc *fbc)
{
	struct intel_fbc_state *fbc_state = &fbc->state;
	struct drm_i915_private *i915 = fbc->i915;

	intel_de_write(i915, ILK_DPFC_FENCE_YOFF(fbc->id),
		       fbc_state->fence_y_offset);

	intel_de_write(i915, ILK_DPFC_CONTROL(fbc->id),
		       DPFC_CTL_EN | g4x_dpfc_ctl(fbc));
}

static void ilk_fbc_deactivate(struct intel_fbc *fbc)
{
	struct drm_i915_private *i915 = fbc->i915;
	u32 dpfc_ctl;

	/* Disable compression */
	dpfc_ctl = intel_de_read(i915, ILK_DPFC_CONTROL(fbc->id));
	if (dpfc_ctl & DPFC_CTL_EN) {
		dpfc_ctl &= ~DPFC_CTL_EN;
		intel_de_write(i915, ILK_DPFC_CONTROL(fbc->id), dpfc_ctl);
	}
}

static bool ilk_fbc_is_active(struct intel_fbc *fbc)
{
	return intel_de_read(fbc->i915, ILK_DPFC_CONTROL(fbc->id)) & DPFC_CTL_EN;
}

static bool ilk_fbc_is_compressing(struct intel_fbc *fbc)
{
	return intel_de_read(fbc->i915, ILK_DPFC_STATUS(fbc->id)) & DPFC_COMP_SEG_MASK;
}

static void ilk_fbc_program_cfb(struct intel_fbc *fbc)
{
	struct drm_i915_private *i915 = fbc->i915;

	intel_de_write(i915, ILK_DPFC_CB_BASE(fbc->id), fbc->compressed_fb.start);
}

static const struct intel_fbc_funcs ilk_fbc_funcs = {
	.activate = ilk_fbc_activate,
	.deactivate = ilk_fbc_deactivate,
	.is_active = ilk_fbc_is_active,
	.is_compressing = ilk_fbc_is_compressing,
	.nuke = i965_fbc_nuke,
	.program_cfb = ilk_fbc_program_cfb,
};

static void snb_fbc_program_fence(struct intel_fbc *fbc)
{
	const struct intel_fbc_state *fbc_state = &fbc->state;
	struct drm_i915_private *i915 = fbc->i915;
	u32 ctl = 0;

	if (fbc_state->fence_id >= 0)
		ctl = SNB_DPFC_FENCE_EN | SNB_DPFC_FENCENO(fbc_state->fence_id);

	intel_de_write(i915, SNB_DPFC_CTL_SA, ctl);
	intel_de_write(i915, SNB_DPFC_CPU_FENCE_OFFSET, fbc_state->fence_y_offset);
}

static void snb_fbc_activate(struct intel_fbc *fbc)
{
	snb_fbc_program_fence(fbc);

	ilk_fbc_activate(fbc);
}

static void snb_fbc_nuke(struct intel_fbc *fbc)
{
	struct drm_i915_private *i915 = fbc->i915;

	intel_de_write(i915, MSG_FBC_REND_STATE(fbc->id), FBC_REND_NUKE);
	intel_de_posting_read(i915, MSG_FBC_REND_STATE(fbc->id));
}

static const struct intel_fbc_funcs snb_fbc_funcs = {
	.activate = snb_fbc_activate,
	.deactivate = ilk_fbc_deactivate,
	.is_active = ilk_fbc_is_active,
	.is_compressing = ilk_fbc_is_compressing,
	.nuke = snb_fbc_nuke,
	.program_cfb = ilk_fbc_program_cfb,
};

static void glk_fbc_program_cfb_stride(struct intel_fbc *fbc)
{
	const struct intel_fbc_state *fbc_state = &fbc->state;
	struct drm_i915_private *i915 = fbc->i915;
	u32 val = 0;

	if (fbc_state->override_cfb_stride)
		val |= FBC_STRIDE_OVERRIDE |
			FBC_STRIDE(fbc_state->override_cfb_stride / fbc->limit);

	intel_de_write(i915, GLK_FBC_STRIDE(fbc->id), val);
}

static void skl_fbc_program_cfb_stride(struct intel_fbc *fbc)
{
	const struct intel_fbc_state *fbc_state = &fbc->state;
	struct drm_i915_private *i915 = fbc->i915;
	u32 val = 0;

	/* Display WA #0529: skl, kbl, bxt. */
	if (fbc_state->override_cfb_stride)
		val |= CHICKEN_FBC_STRIDE_OVERRIDE |
			CHICKEN_FBC_STRIDE(fbc_state->override_cfb_stride / fbc->limit);

	intel_de_rmw(i915, CHICKEN_MISC_4,
		     CHICKEN_FBC_STRIDE_OVERRIDE |
		     CHICKEN_FBC_STRIDE_MASK, val);
}

static u32 ivb_dpfc_ctl(struct intel_fbc *fbc)
{
	const struct intel_fbc_state *fbc_state = &fbc->state;
	struct drm_i915_private *i915 = fbc->i915;
	u32 dpfc_ctl;

	dpfc_ctl = g4x_dpfc_ctl_limit(fbc);

	if (IS_IVYBRIDGE(i915))
		dpfc_ctl |= DPFC_CTL_PLANE_IVB(fbc_state->plane->i9xx_plane);

	if (fbc_state->fence_id >= 0)
		dpfc_ctl |= DPFC_CTL_FENCE_EN_IVB;

	if (fbc->false_color)
		dpfc_ctl |= DPFC_CTL_FALSE_COLOR;

	return dpfc_ctl;
}

static void ivb_fbc_activate(struct intel_fbc *fbc)
{
	struct drm_i915_private *i915 = fbc->i915;

	if (DISPLAY_VER(i915) >= 10)
		glk_fbc_program_cfb_stride(fbc);
	else if (DISPLAY_VER(i915) == 9)
		skl_fbc_program_cfb_stride(fbc);

	if (to_gt(i915)->ggtt->num_fences)
		snb_fbc_program_fence(fbc);

	intel_de_write(i915, ILK_DPFC_CONTROL(fbc->id),
		       DPFC_CTL_EN | ivb_dpfc_ctl(fbc));
}

static bool ivb_fbc_is_compressing(struct intel_fbc *fbc)
{
	return intel_de_read(fbc->i915, ILK_DPFC_STATUS2(fbc->id)) & DPFC_COMP_SEG_MASK_IVB;
}

static void ivb_fbc_set_false_color(struct intel_fbc *fbc,
				    bool enable)
{
	intel_de_rmw(fbc->i915, ILK_DPFC_CONTROL(fbc->id),
		     DPFC_CTL_FALSE_COLOR, enable ? DPFC_CTL_FALSE_COLOR : 0);
}

static const struct intel_fbc_funcs ivb_fbc_funcs = {
	.activate = ivb_fbc_activate,
	.deactivate = ilk_fbc_deactivate,
	.is_active = ilk_fbc_is_active,
	.is_compressing = ivb_fbc_is_compressing,
	.nuke = snb_fbc_nuke,
	.program_cfb = ilk_fbc_program_cfb,
	.set_false_color = ivb_fbc_set_false_color,
};

static bool intel_fbc_hw_is_active(struct intel_fbc *fbc)
{
	return fbc->funcs->is_active(fbc);
}

static void intel_fbc_hw_activate(struct intel_fbc *fbc)
{
	trace_intel_fbc_activate(fbc->state.plane);

	fbc->active = true;
	fbc->activated = true;

	fbc->funcs->activate(fbc);
}

static void intel_fbc_hw_deactivate(struct intel_fbc *fbc)
{
	trace_intel_fbc_deactivate(fbc->state.plane);

	fbc->active = false;

	fbc->funcs->deactivate(fbc);
}

static bool intel_fbc_is_compressing(struct intel_fbc *fbc)
{
	return fbc->funcs->is_compressing(fbc);
}

static void intel_fbc_nuke(struct intel_fbc *fbc)
{
	struct drm_i915_private *i915 = fbc->i915;

	lockdep_assert_held(&fbc->lock);
	drm_WARN_ON(&i915->drm, fbc->flip_pending);

	trace_intel_fbc_nuke(fbc->state.plane);

	fbc->funcs->nuke(fbc);
}

static void intel_fbc_activate(struct intel_fbc *fbc)
{
	lockdep_assert_held(&fbc->lock);

	intel_fbc_hw_activate(fbc);
	intel_fbc_nuke(fbc);

	fbc->no_fbc_reason = NULL;
}

static void intel_fbc_deactivate(struct intel_fbc *fbc, const char *reason)
{
	lockdep_assert_held(&fbc->lock);

	if (fbc->active)
		intel_fbc_hw_deactivate(fbc);

	fbc->no_fbc_reason = reason;
}

static u64 intel_fbc_cfb_base_max(struct drm_i915_private *i915)
{
	if (DISPLAY_VER(i915) >= 5 || IS_G4X(i915))
		return BIT_ULL(28);
	else
		return BIT_ULL(32);
}

static u64 intel_fbc_stolen_end(struct drm_i915_private *i915)
{
	u64 end;

	/* The FBC hardware for BDW/SKL doesn't have access to the stolen
	 * reserved range size, so it always assumes the maximum (8mb) is used.
	 * If we enable FBC using a CFB on that memory range we'll get FIFO
	 * underruns, even if that range is not reserved by the BIOS. */
	if (IS_BROADWELL(i915) ||
	    (DISPLAY_VER(i915) == 9 && !IS_BROXTON(i915)))
		end = resource_size(&i915->dsm.stolen) - 8 * 1024 * 1024;
	else
		end = U64_MAX;

	return min(end, intel_fbc_cfb_base_max(i915));
}

static int intel_fbc_min_limit(const struct intel_plane_state *plane_state)
{
	return plane_state->hw.fb->format->cpp[0] == 2 ? 2 : 1;
}

static int intel_fbc_max_limit(struct drm_i915_private *i915)
{
	/* WaFbcOnly1to1Ratio:ctg */
	if (IS_G4X(i915))
		return 1;

	/*
	 * FBC2 can only do 1:1, 1:2, 1:4, we limit
	 * FBC1 to the same out of convenience.
	 */
	return 4;
}

static int find_compression_limit(struct intel_fbc *fbc,
				  unsigned int size, int min_limit)
{
	struct drm_i915_private *i915 = fbc->i915;
	u64 end = intel_fbc_stolen_end(i915);
	int ret, limit = min_limit;

	size /= limit;

	/* Try to over-allocate to reduce reallocations and fragmentation. */
	ret = i915_gem_stolen_insert_node_in_range(i915, &fbc->compressed_fb,
						   size <<= 1, 4096, 0, end);
	if (ret == 0)
		return limit;

	for (; limit <= intel_fbc_max_limit(i915); limit <<= 1) {
		ret = i915_gem_stolen_insert_node_in_range(i915, &fbc->compressed_fb,
							   size >>= 1, 4096, 0, end);
		if (ret == 0)
			return limit;
	}

	return 0;
}

static int intel_fbc_alloc_cfb(struct intel_fbc *fbc,
			       unsigned int size, int min_limit)
{
	struct drm_i915_private *i915 = fbc->i915;
	int ret;

	drm_WARN_ON(&i915->drm,
		    drm_mm_node_allocated(&fbc->compressed_fb));
	drm_WARN_ON(&i915->drm,
		    drm_mm_node_allocated(&fbc->compressed_llb));

	if (DISPLAY_VER(i915) < 5 && !IS_G4X(i915)) {
		ret = i915_gem_stolen_insert_node(i915, &fbc->compressed_llb,
						  4096, 4096);
		if (ret)
			goto err;
	}

	ret = find_compression_limit(fbc, size, min_limit);
	if (!ret)
		goto err_llb;
	else if (ret > min_limit)
		drm_info_once(&i915->drm,
			      "Reducing the compressed framebuffer size. This may lead to less power savings than a non-reduced-size. Try to increase stolen memory size if available in BIOS.\n");

	fbc->limit = ret;

	drm_dbg_kms(&i915->drm,
		    "reserved %llu bytes of contiguous stolen space for FBC, limit: %d\n",
		    fbc->compressed_fb.size, fbc->limit);

	return 0;

err_llb:
	if (drm_mm_node_allocated(&fbc->compressed_llb))
		i915_gem_stolen_remove_node(i915, &fbc->compressed_llb);
err:
	if (drm_mm_initialized(&i915->mm.stolen))
		drm_info_once(&i915->drm, "not enough stolen space for compressed buffer (need %d more bytes), disabling. Hint: you may be able to increase stolen memory size in the BIOS to avoid this.\n", size);
	return -ENOSPC;
}

static void intel_fbc_program_cfb(struct intel_fbc *fbc)
{
	fbc->funcs->program_cfb(fbc);
}

static void intel_fbc_program_workarounds(struct intel_fbc *fbc)
{
	/* Wa_22014263786:icl,jsl,tgl,dg1,rkl,adls,adlp,mtl */
	if (DISPLAY_VER(fbc->i915) >= 11 && !IS_DG2(fbc->i915))
		intel_de_rmw(fbc->i915, ILK_DPFC_CHICKEN(fbc->id), 0,
			     DPFC_CHICKEN_FORCE_SLB_INVALIDATION);
}

static void __intel_fbc_cleanup_cfb(struct intel_fbc *fbc)
{
	struct drm_i915_private *i915 = fbc->i915;

	if (WARN_ON(intel_fbc_hw_is_active(fbc)))
		return;

	if (drm_mm_node_allocated(&fbc->compressed_llb))
		i915_gem_stolen_remove_node(i915, &fbc->compressed_llb);
	if (drm_mm_node_allocated(&fbc->compressed_fb))
		i915_gem_stolen_remove_node(i915, &fbc->compressed_fb);
}

void intel_fbc_cleanup(struct drm_i915_private *i915)
{
	struct intel_fbc *fbc;
	enum intel_fbc_id fbc_id;

	for_each_intel_fbc(i915, fbc, fbc_id) {
		mutex_lock(&fbc->lock);
		__intel_fbc_cleanup_cfb(fbc);
		mutex_unlock(&fbc->lock);

		kfree(fbc);
	}
}

static bool stride_is_valid(const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int stride = intel_fbc_plane_stride(plane_state) *
		fb->format->cpp[0];

	/* This should have been caught earlier. */
	if (drm_WARN_ON_ONCE(&i915->drm, (stride & (64 - 1)) != 0))
		return false;

	/* Below are the additional FBC restrictions. */
	if (stride < 512)
		return false;

	if (DISPLAY_VER(i915) == 2 || DISPLAY_VER(i915) == 3)
		return stride == 4096 || stride == 8192;

	if (DISPLAY_VER(i915) == 4 && !IS_G4X(i915) && stride < 2048)
		return false;

	/* Display WA #1105: skl,bxt,kbl,cfl,glk */
	if ((DISPLAY_VER(i915) == 9 || IS_GEMINILAKE(i915)) &&
	    fb->modifier == DRM_FORMAT_MOD_LINEAR && stride & 511)
		return false;

	if (stride > 16384)
		return false;

	return true;
}

static bool pixel_format_is_valid(const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;

	switch (fb->format->format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
		return true;
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_RGB565:
		/* 16bpp not supported on gen2 */
		if (DISPLAY_VER(i915) == 2)
			return false;
		/* WaFbcOnly1to1Ratio:ctg */
		if (IS_G4X(i915))
			return false;
		return true;
	default:
		return false;
	}
}

static bool rotation_is_valid(const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;

	if (DISPLAY_VER(i915) >= 9 && fb->format->format == DRM_FORMAT_RGB565 &&
	    drm_rotation_90_or_270(rotation))
		return false;
	else if (DISPLAY_VER(i915) <= 4 && !IS_G4X(i915) &&
		 rotation != DRM_MODE_ROTATE_0)
		return false;

	return true;
}

/*
 * For some reason, the hardware tracking starts looking at whatever we
 * programmed as the display plane base address register. It does not look at
 * the X and Y offset registers. That's why we include the src x/y offsets
 * instead of just looking at the plane size.
 */
static bool intel_fbc_hw_tracking_covers_screen(const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	unsigned int effective_w, effective_h, max_w, max_h;

	if (DISPLAY_VER(i915) >= 10) {
		max_w = 5120;
		max_h = 4096;
	} else if (DISPLAY_VER(i915) >= 8 || IS_HASWELL(i915)) {
		max_w = 4096;
		max_h = 4096;
	} else if (IS_G4X(i915) || DISPLAY_VER(i915) >= 5) {
		max_w = 4096;
		max_h = 2048;
	} else {
		max_w = 2048;
		max_h = 1536;
	}

	effective_w = plane_state->view.color_plane[0].x +
		(drm_rect_width(&plane_state->uapi.src) >> 16);
	effective_h = plane_state->view.color_plane[0].y +
		(drm_rect_height(&plane_state->uapi.src) >> 16);

	return effective_w <= max_w && effective_h <= max_h;
}

static bool tiling_is_valid(const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;

	switch (fb->modifier) {
	case DRM_FORMAT_MOD_LINEAR:
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_Yf_TILED:
		return DISPLAY_VER(i915) >= 9;
	case I915_FORMAT_MOD_4_TILED:
	case I915_FORMAT_MOD_X_TILED:
		return true;
	default:
		return false;
	}
}

static void intel_fbc_update_state(struct intel_atomic_state *state,
				   struct intel_crtc *crtc,
				   struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_plane_state *plane_state =
		intel_atomic_get_new_plane_state(state, plane);
	struct intel_fbc *fbc = plane->fbc;
	struct intel_fbc_state *fbc_state = &fbc->state;

	WARN_ON(plane_state->no_fbc_reason);
	WARN_ON(fbc_state->plane && fbc_state->plane != plane);

	fbc_state->plane = plane;

	/* FBC1 compression interval: arbitrary choice of 1 second */
	fbc_state->interval = drm_mode_vrefresh(&crtc_state->hw.adjusted_mode);

	fbc_state->fence_y_offset = intel_plane_fence_y_offset(plane_state);

	drm_WARN_ON(&i915->drm, plane_state->flags & PLANE_HAS_FENCE &&
		    !plane_state->ggtt_vma->fence);

	if (plane_state->flags & PLANE_HAS_FENCE &&
	    plane_state->ggtt_vma->fence)
		fbc_state->fence_id = plane_state->ggtt_vma->fence->id;
	else
		fbc_state->fence_id = -1;

	fbc_state->cfb_stride = intel_fbc_cfb_stride(plane_state);
	fbc_state->cfb_size = intel_fbc_cfb_size(plane_state);
	fbc_state->override_cfb_stride = intel_fbc_override_cfb_stride(plane_state);
}

static bool intel_fbc_is_fence_ok(const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);

	/*
	 * The use of a CPU fence is one of two ways to detect writes by the
	 * CPU to the scanout and trigger updates to the FBC.
	 *
	 * The other method is by software tracking (see
	 * intel_fbc_invalidate/flush()), it will manually notify FBC and nuke
	 * the current compressed buffer and recompress it.
	 *
	 * Note that is possible for a tiled surface to be unmappable (and
	 * so have no fence associated with it) due to aperture constraints
	 * at the time of pinning.
	 */
	return DISPLAY_VER(i915) >= 9 ||
		(plane_state->flags & PLANE_HAS_FENCE &&
		 plane_state->ggtt_vma->fence);
}

static bool intel_fbc_is_cfb_ok(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct intel_fbc *fbc = plane->fbc;

	return intel_fbc_min_limit(plane_state) <= fbc->limit &&
		intel_fbc_cfb_size(plane_state) <= fbc->compressed_fb.size * fbc->limit;
}

static bool intel_fbc_is_ok(const struct intel_plane_state *plane_state)
{
	return !plane_state->no_fbc_reason &&
		intel_fbc_is_fence_ok(plane_state) &&
		intel_fbc_is_cfb_ok(plane_state);
}

static int intel_fbc_check_plane(struct intel_atomic_state *state,
				 struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_plane_state *plane_state =
		intel_atomic_get_new_plane_state(state, plane);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	struct intel_crtc *crtc = to_intel_crtc(plane_state->hw.crtc);
	const struct intel_crtc_state *crtc_state;
	struct intel_fbc *fbc = plane->fbc;

	if (!fbc)
		return 0;

	if (intel_vgpu_active(i915)) {
		plane_state->no_fbc_reason = "VGPU active";
		return 0;
	}

	if (!i915->params.enable_fbc) {
		plane_state->no_fbc_reason = "disabled per module param or by default";
		return 0;
	}

	if (!plane_state->uapi.visible) {
		plane_state->no_fbc_reason = "plane not visible";
		return 0;
	}

	crtc_state = intel_atomic_get_new_crtc_state(state, crtc);

	if (crtc_state->hw.adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE) {
		plane_state->no_fbc_reason = "interlaced mode not supported";
		return 0;
	}

	if (crtc_state->double_wide) {
		plane_state->no_fbc_reason = "double wide pipe not supported";
		return 0;
	}

	/*
	 * Display 12+ is not supporting FBC with PSR2.
	 * Recommendation is to keep this combination disabled
	 * Bspec: 50422 HSD: 14010260002
	 */
	if (DISPLAY_VER(i915) >= 12 && crtc_state->has_psr2) {
		plane_state->no_fbc_reason = "PSR2 enabled";
		return 0;
	}

	/* Wa_14016291713 */
	if ((IS_DISPLAY_VER(i915, 12, 13) ||
	     IS_MTL_DISPLAY_STEP(i915, STEP_A0, STEP_C0)) &&
	    crtc_state->has_psr) {
		plane_state->no_fbc_reason = "PSR1 enabled (Wa_14016291713)";
		return 0;
	}

	if (!pixel_format_is_valid(plane_state)) {
		plane_state->no_fbc_reason = "pixel format not supported";
		return 0;
	}

	if (!tiling_is_valid(plane_state)) {
		plane_state->no_fbc_reason = "tiling not supported";
		return 0;
	}

	if (!rotation_is_valid(plane_state)) {
		plane_state->no_fbc_reason = "rotation not supported";
		return 0;
	}

	if (!stride_is_valid(plane_state)) {
		plane_state->no_fbc_reason = "stride not supported";
		return 0;
	}

	if (plane_state->hw.pixel_blend_mode != DRM_MODE_BLEND_PIXEL_NONE &&
	    fb->format->has_alpha) {
		plane_state->no_fbc_reason = "per-pixel alpha not supported";
		return 0;
	}

	if (!intel_fbc_hw_tracking_covers_screen(plane_state)) {
		plane_state->no_fbc_reason = "plane size too big";
		return 0;
	}

	/*
	 * Work around a problem on GEN9+ HW, where enabling FBC on a plane
	 * having a Y offset that isn't divisible by 4 causes FIFO underrun
	 * and screen flicker.
	 */
	if (DISPLAY_VER(i915) >= 9 &&
	    plane_state->view.color_plane[0].y & 3) {
		plane_state->no_fbc_reason = "plane start Y offset misaligned";
		return 0;
	}

	/* Wa_22010751166: icl, ehl, tgl, dg1, rkl */
	if (DISPLAY_VER(i915) >= 11 &&
	    (plane_state->view.color_plane[0].y +
	     (drm_rect_height(&plane_state->uapi.src) >> 16)) & 3) {
		plane_state->no_fbc_reason = "plane end Y offset misaligned";
		return 0;
	}

	/* WaFbcExceedCdClockThreshold:hsw,bdw */
	if (IS_HASWELL(i915) || IS_BROADWELL(i915)) {
		const struct intel_cdclk_state *cdclk_state;

		cdclk_state = intel_atomic_get_cdclk_state(state);
		if (IS_ERR(cdclk_state))
			return PTR_ERR(cdclk_state);

		if (crtc_state->pixel_rate >= cdclk_state->logical.cdclk * 95 / 100) {
			plane_state->no_fbc_reason = "pixel rate too high";
			return 0;
		}
	}

	plane_state->no_fbc_reason = NULL;

	return 0;
}


static bool intel_fbc_can_flip_nuke(struct intel_atomic_state *state,
				    struct intel_crtc *crtc,
				    struct intel_plane *plane)
{
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_plane_state *old_plane_state =
		intel_atomic_get_old_plane_state(state, plane);
	const struct intel_plane_state *new_plane_state =
		intel_atomic_get_new_plane_state(state, plane);
	const struct drm_framebuffer *old_fb = old_plane_state->hw.fb;
	const struct drm_framebuffer *new_fb = new_plane_state->hw.fb;

	if (intel_crtc_needs_modeset(new_crtc_state))
		return false;

	if (!intel_fbc_is_ok(old_plane_state) ||
	    !intel_fbc_is_ok(new_plane_state))
		return false;

	if (old_fb->format->format != new_fb->format->format)
		return false;

	if (old_fb->modifier != new_fb->modifier)
		return false;

	if (intel_fbc_plane_stride(old_plane_state) !=
	    intel_fbc_plane_stride(new_plane_state))
		return false;

	if (intel_fbc_cfb_stride(old_plane_state) !=
	    intel_fbc_cfb_stride(new_plane_state))
		return false;

	if (intel_fbc_cfb_size(old_plane_state) !=
	    intel_fbc_cfb_size(new_plane_state))
		return false;

	if (intel_fbc_override_cfb_stride(old_plane_state) !=
	    intel_fbc_override_cfb_stride(new_plane_state))
		return false;

	return true;
}

static bool __intel_fbc_pre_update(struct intel_atomic_state *state,
				   struct intel_crtc *crtc,
				   struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_fbc *fbc = plane->fbc;
	bool need_vblank_wait = false;

	lockdep_assert_held(&fbc->lock);

	fbc->flip_pending = true;

	if (intel_fbc_can_flip_nuke(state, crtc, plane))
		return need_vblank_wait;

	intel_fbc_deactivate(fbc, "update pending");

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
	if (fbc->activated && DISPLAY_VER(i915) >= 10)
		need_vblank_wait = true;
	fbc->activated = false;

	return need_vblank_wait;
}

bool intel_fbc_pre_update(struct intel_atomic_state *state,
			  struct intel_crtc *crtc)
{
	const struct intel_plane_state *plane_state;
	bool need_vblank_wait = false;
	struct intel_plane *plane;
	int i;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		struct intel_fbc *fbc = plane->fbc;

		if (!fbc || plane->pipe != crtc->pipe)
			continue;

		mutex_lock(&fbc->lock);

		if (fbc->state.plane == plane)
			need_vblank_wait |= __intel_fbc_pre_update(state, crtc, plane);

		mutex_unlock(&fbc->lock);
	}

	return need_vblank_wait;
}

static void __intel_fbc_disable(struct intel_fbc *fbc)
{
	struct drm_i915_private *i915 = fbc->i915;
	struct intel_plane *plane = fbc->state.plane;

	lockdep_assert_held(&fbc->lock);
	drm_WARN_ON(&i915->drm, fbc->active);

	drm_dbg_kms(&i915->drm, "Disabling FBC on [PLANE:%d:%s]\n",
		    plane->base.base.id, plane->base.name);

	__intel_fbc_cleanup_cfb(fbc);

	fbc->state.plane = NULL;
	fbc->flip_pending = false;
	fbc->busy_bits = 0;
}

static void __intel_fbc_post_update(struct intel_fbc *fbc)
{
	lockdep_assert_held(&fbc->lock);

	fbc->flip_pending = false;

	if (!fbc->busy_bits)
		intel_fbc_activate(fbc);
	else
		intel_fbc_deactivate(fbc, "frontbuffer write");
}

void intel_fbc_post_update(struct intel_atomic_state *state,
			   struct intel_crtc *crtc)
{
	const struct intel_plane_state *plane_state;
	struct intel_plane *plane;
	int i;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		struct intel_fbc *fbc = plane->fbc;

		if (!fbc || plane->pipe != crtc->pipe)
			continue;

		mutex_lock(&fbc->lock);

		if (fbc->state.plane == plane)
			__intel_fbc_post_update(fbc);

		mutex_unlock(&fbc->lock);
	}
}

static unsigned int intel_fbc_get_frontbuffer_bit(struct intel_fbc *fbc)
{
	if (fbc->state.plane)
		return fbc->state.plane->frontbuffer_bit;
	else
		return 0;
}

static void __intel_fbc_invalidate(struct intel_fbc *fbc,
				   unsigned int frontbuffer_bits,
				   enum fb_op_origin origin)
{
	if (origin == ORIGIN_FLIP || origin == ORIGIN_CURSOR_UPDATE)
		return;

	mutex_lock(&fbc->lock);

	frontbuffer_bits &= intel_fbc_get_frontbuffer_bit(fbc);
	if (!frontbuffer_bits)
		goto out;

	fbc->busy_bits |= frontbuffer_bits;
	intel_fbc_deactivate(fbc, "frontbuffer write");

out:
	mutex_unlock(&fbc->lock);
}

void intel_fbc_invalidate(struct drm_i915_private *i915,
			  unsigned int frontbuffer_bits,
			  enum fb_op_origin origin)
{
	struct intel_fbc *fbc;
	enum intel_fbc_id fbc_id;

	for_each_intel_fbc(i915, fbc, fbc_id)
		__intel_fbc_invalidate(fbc, frontbuffer_bits, origin);

}

static void __intel_fbc_flush(struct intel_fbc *fbc,
			      unsigned int frontbuffer_bits,
			      enum fb_op_origin origin)
{
	mutex_lock(&fbc->lock);

	frontbuffer_bits &= intel_fbc_get_frontbuffer_bit(fbc);
	if (!frontbuffer_bits)
		goto out;

	fbc->busy_bits &= ~frontbuffer_bits;

	if (origin == ORIGIN_FLIP || origin == ORIGIN_CURSOR_UPDATE)
		goto out;

	if (fbc->busy_bits || fbc->flip_pending)
		goto out;

	if (fbc->active)
		intel_fbc_nuke(fbc);
	else
		intel_fbc_activate(fbc);

out:
	mutex_unlock(&fbc->lock);
}

void intel_fbc_flush(struct drm_i915_private *i915,
		     unsigned int frontbuffer_bits,
		     enum fb_op_origin origin)
{
	struct intel_fbc *fbc;
	enum intel_fbc_id fbc_id;

	for_each_intel_fbc(i915, fbc, fbc_id)
		__intel_fbc_flush(fbc, frontbuffer_bits, origin);
}

int intel_fbc_atomic_check(struct intel_atomic_state *state)
{
	struct intel_plane_state *plane_state;
	struct intel_plane *plane;
	int i;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		int ret;

		ret = intel_fbc_check_plane(state, plane);
		if (ret)
			return ret;
	}

	return 0;
}

static void __intel_fbc_enable(struct intel_atomic_state *state,
			       struct intel_crtc *crtc,
			       struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_plane_state *plane_state =
		intel_atomic_get_new_plane_state(state, plane);
	struct intel_fbc *fbc = plane->fbc;

	lockdep_assert_held(&fbc->lock);

	if (fbc->state.plane) {
		if (fbc->state.plane != plane)
			return;

		if (intel_fbc_is_ok(plane_state)) {
			intel_fbc_update_state(state, crtc, plane);
			return;
		}

		__intel_fbc_disable(fbc);
	}

	drm_WARN_ON(&i915->drm, fbc->active);

	fbc->no_fbc_reason = plane_state->no_fbc_reason;
	if (fbc->no_fbc_reason)
		return;

	if (!intel_fbc_is_fence_ok(plane_state)) {
		fbc->no_fbc_reason = "framebuffer not fenced";
		return;
	}

	if (fbc->underrun_detected) {
		fbc->no_fbc_reason = "FIFO underrun";
		return;
	}

	if (intel_fbc_alloc_cfb(fbc, intel_fbc_cfb_size(plane_state),
				intel_fbc_min_limit(plane_state))) {
		fbc->no_fbc_reason = "not enough stolen memory";
		return;
	}

	drm_dbg_kms(&i915->drm, "Enabling FBC on [PLANE:%d:%s]\n",
		    plane->base.base.id, plane->base.name);
	fbc->no_fbc_reason = "FBC enabled but not active yet\n";

	intel_fbc_update_state(state, crtc, plane);

	intel_fbc_program_workarounds(fbc);
	intel_fbc_program_cfb(fbc);
}

/**
 * intel_fbc_disable - disable FBC if it's associated with crtc
 * @crtc: the CRTC
 *
 * This function disables FBC if it's associated with the provided CRTC.
 */
void intel_fbc_disable(struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct intel_plane *plane;

	for_each_intel_plane(&i915->drm, plane) {
		struct intel_fbc *fbc = plane->fbc;

		if (!fbc || plane->pipe != crtc->pipe)
			continue;

		mutex_lock(&fbc->lock);
		if (fbc->state.plane == plane)
			__intel_fbc_disable(fbc);
		mutex_unlock(&fbc->lock);
	}
}

void intel_fbc_update(struct intel_atomic_state *state,
		      struct intel_crtc *crtc)
{
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_plane_state *plane_state;
	struct intel_plane *plane;
	int i;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		struct intel_fbc *fbc = plane->fbc;

		if (!fbc || plane->pipe != crtc->pipe)
			continue;

		mutex_lock(&fbc->lock);

		if (intel_crtc_needs_fastset(crtc_state) &&
		    plane_state->no_fbc_reason) {
			if (fbc->state.plane == plane)
				__intel_fbc_disable(fbc);
		} else {
			__intel_fbc_enable(state, crtc, plane);
		}

		mutex_unlock(&fbc->lock);
	}
}

static void intel_fbc_underrun_work_fn(struct work_struct *work)
{
	struct intel_fbc *fbc = container_of(work, typeof(*fbc), underrun_work);
	struct drm_i915_private *i915 = fbc->i915;

	mutex_lock(&fbc->lock);

	/* Maybe we were scheduled twice. */
	if (fbc->underrun_detected || !fbc->state.plane)
		goto out;

	drm_dbg_kms(&i915->drm, "Disabling FBC due to FIFO underrun.\n");
	fbc->underrun_detected = true;

	intel_fbc_deactivate(fbc, "FIFO underrun");
	if (!fbc->flip_pending)
		intel_crtc_wait_for_next_vblank(intel_crtc_for_pipe(i915, fbc->state.plane->pipe));
	__intel_fbc_disable(fbc);
out:
	mutex_unlock(&fbc->lock);
}

static void __intel_fbc_reset_underrun(struct intel_fbc *fbc)
{
	struct drm_i915_private *i915 = fbc->i915;

	cancel_work_sync(&fbc->underrun_work);

	mutex_lock(&fbc->lock);

	if (fbc->underrun_detected) {
		drm_dbg_kms(&i915->drm,
			    "Re-allowing FBC after fifo underrun\n");
		fbc->no_fbc_reason = "FIFO underrun cleared";
	}

	fbc->underrun_detected = false;
	mutex_unlock(&fbc->lock);
}

/*
 * intel_fbc_reset_underrun - reset FBC fifo underrun status.
 * @i915: the i915 device
 *
 * See intel_fbc_handle_fifo_underrun_irq(). For automated testing we
 * want to re-enable FBC after an underrun to increase test coverage.
 */
void intel_fbc_reset_underrun(struct drm_i915_private *i915)
{
	struct intel_fbc *fbc;
	enum intel_fbc_id fbc_id;

	for_each_intel_fbc(i915, fbc, fbc_id)
		__intel_fbc_reset_underrun(fbc);
}

static void __intel_fbc_handle_fifo_underrun_irq(struct intel_fbc *fbc)
{
	/*
	 * There's no guarantee that underrun_detected won't be set to true
	 * right after this check and before the work is scheduled, but that's
	 * not a problem since we'll check it again under the work function
	 * while FBC is locked. This check here is just to prevent us from
	 * unnecessarily scheduling the work, and it relies on the fact that we
	 * never switch underrun_detect back to false after it's true.
	 */
	if (READ_ONCE(fbc->underrun_detected))
		return;

	schedule_work(&fbc->underrun_work);
}

/**
 * intel_fbc_handle_fifo_underrun_irq - disable FBC when we get a FIFO underrun
 * @i915: i915 device
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
void intel_fbc_handle_fifo_underrun_irq(struct drm_i915_private *i915)
{
	struct intel_fbc *fbc;
	enum intel_fbc_id fbc_id;

	for_each_intel_fbc(i915, fbc, fbc_id)
		__intel_fbc_handle_fifo_underrun_irq(fbc);
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
static int intel_sanitize_fbc_option(struct drm_i915_private *i915)
{
	if (i915->params.enable_fbc >= 0)
		return !!i915->params.enable_fbc;

	if (!HAS_FBC(i915))
		return 0;

	if (IS_BROADWELL(i915) || DISPLAY_VER(i915) >= 9)
		return 1;

	return 0;
}

static bool need_fbc_vtd_wa(struct drm_i915_private *i915)
{
	/* WaFbcTurnOffFbcWhenHyperVisorIsUsed:skl,bxt */
	if (i915_vtd_active(i915) &&
	    (IS_SKYLAKE(i915) || IS_BROXTON(i915))) {
		drm_info(&i915->drm,
			 "Disabling framebuffer compression (FBC) to prevent screen flicker with VT-d enabled\n");
		return true;
	}

	return false;
}

void intel_fbc_add_plane(struct intel_fbc *fbc, struct intel_plane *plane)
{
	plane->fbc = fbc;
}

static struct intel_fbc *intel_fbc_create(struct drm_i915_private *i915,
					  enum intel_fbc_id fbc_id)
{
	struct intel_fbc *fbc;

	fbc = kzalloc(sizeof(*fbc), GFP_KERNEL);
	if (!fbc)
		return NULL;

	fbc->id = fbc_id;
	fbc->i915 = i915;
	INIT_WORK(&fbc->underrun_work, intel_fbc_underrun_work_fn);
	mutex_init(&fbc->lock);

	if (DISPLAY_VER(i915) >= 7)
		fbc->funcs = &ivb_fbc_funcs;
	else if (DISPLAY_VER(i915) == 6)
		fbc->funcs = &snb_fbc_funcs;
	else if (DISPLAY_VER(i915) == 5)
		fbc->funcs = &ilk_fbc_funcs;
	else if (IS_G4X(i915))
		fbc->funcs = &g4x_fbc_funcs;
	else if (DISPLAY_VER(i915) == 4)
		fbc->funcs = &i965_fbc_funcs;
	else
		fbc->funcs = &i8xx_fbc_funcs;

	return fbc;
}

/**
 * intel_fbc_init - Initialize FBC
 * @i915: the i915 device
 *
 * This function might be called during PM init process.
 */
void intel_fbc_init(struct drm_i915_private *i915)
{
	enum intel_fbc_id fbc_id;

	if (!drm_mm_initialized(&i915->mm.stolen))
		RUNTIME_INFO(i915)->fbc_mask = 0;

	if (need_fbc_vtd_wa(i915))
		RUNTIME_INFO(i915)->fbc_mask = 0;

	i915->params.enable_fbc = intel_sanitize_fbc_option(i915);
	drm_dbg_kms(&i915->drm, "Sanitized enable_fbc value: %d\n",
		    i915->params.enable_fbc);

	for_each_fbc_id(i915, fbc_id)
		i915->display.fbc[fbc_id] = intel_fbc_create(i915, fbc_id);
}

/**
 * intel_fbc_sanitize - Sanitize FBC
 * @i915: the i915 device
 *
 * Make sure FBC is initially disabled since we have no
 * idea eg. into which parts of stolen it might be scribbling
 * into.
 */
void intel_fbc_sanitize(struct drm_i915_private *i915)
{
	struct intel_fbc *fbc;
	enum intel_fbc_id fbc_id;

	for_each_intel_fbc(i915, fbc, fbc_id) {
		if (intel_fbc_hw_is_active(fbc))
			intel_fbc_hw_deactivate(fbc);
	}
}

static int intel_fbc_debugfs_status_show(struct seq_file *m, void *unused)
{
	struct intel_fbc *fbc = m->private;
	struct drm_i915_private *i915 = fbc->i915;
	struct intel_plane *plane;
	intel_wakeref_t wakeref;

	drm_modeset_lock_all(&i915->drm);

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);
	mutex_lock(&fbc->lock);

	if (fbc->active) {
		seq_puts(m, "FBC enabled\n");
		seq_printf(m, "Compressing: %s\n",
			   str_yes_no(intel_fbc_is_compressing(fbc)));
	} else {
		seq_printf(m, "FBC disabled: %s\n", fbc->no_fbc_reason);
	}

	for_each_intel_plane(&i915->drm, plane) {
		const struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);

		if (plane->fbc != fbc)
			continue;

		seq_printf(m, "%c [PLANE:%d:%s]: %s\n",
			   fbc->state.plane == plane ? '*' : ' ',
			   plane->base.base.id, plane->base.name,
			   plane_state->no_fbc_reason ?: "FBC possible");
	}

	mutex_unlock(&fbc->lock);
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	drm_modeset_unlock_all(&i915->drm);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(intel_fbc_debugfs_status);

static int intel_fbc_debugfs_false_color_get(void *data, u64 *val)
{
	struct intel_fbc *fbc = data;

	*val = fbc->false_color;

	return 0;
}

static int intel_fbc_debugfs_false_color_set(void *data, u64 val)
{
	struct intel_fbc *fbc = data;

	mutex_lock(&fbc->lock);

	fbc->false_color = val;

	if (fbc->active)
		fbc->funcs->set_false_color(fbc, fbc->false_color);

	mutex_unlock(&fbc->lock);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(intel_fbc_debugfs_false_color_fops,
			 intel_fbc_debugfs_false_color_get,
			 intel_fbc_debugfs_false_color_set,
			 "%llu\n");

static void intel_fbc_debugfs_add(struct intel_fbc *fbc,
				  struct dentry *parent)
{
	debugfs_create_file("i915_fbc_status", 0444, parent,
			    fbc, &intel_fbc_debugfs_status_fops);

	if (fbc->funcs->set_false_color)
		debugfs_create_file_unsafe("i915_fbc_false_color", 0644, parent,
					   fbc, &intel_fbc_debugfs_false_color_fops);
}

void intel_fbc_crtc_debugfs_add(struct intel_crtc *crtc)
{
	struct intel_plane *plane = to_intel_plane(crtc->base.primary);

	if (plane->fbc)
		intel_fbc_debugfs_add(plane->fbc, crtc->base.debugfs_entry);
}

/* FIXME: remove this once igt is on board with per-crtc stuff */
void intel_fbc_debugfs_register(struct drm_i915_private *i915)
{
	struct drm_minor *minor = i915->drm.primary;
	struct intel_fbc *fbc;

	fbc = i915->display.fbc[INTEL_FBC_A];
	if (fbc)
		intel_fbc_debugfs_add(fbc, minor->debugfs_root);
}

/*
 * (C) COPYRIGHT 2016 ARM Limited. All rights reserved.
 * Author: Liviu Dudau <Liviu.Dudau@arm.com>
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * ARM Mali DP500/DP550/DP650 driver (crtc operations)
 */

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_probe_helper.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <video/videomode.h>

#include "malidp_drv.h"
#include "malidp_hw.h"

static enum drm_mode_status malidp_crtc_mode_valid(struct drm_crtc *crtc,
						   const struct drm_display_mode *mode)
{
	struct malidp_drm *malidp = crtc_to_malidp_device(crtc);
	struct malidp_hw_device *hwdev = malidp->dev;

	/*
	 * check that the hardware can drive the required clock rate,
	 * but skip the check if the clock is meant to be disabled (req_rate = 0)
	 */
	long rate, req_rate = mode->crtc_clock * 1000;

	if (req_rate) {
		rate = clk_round_rate(hwdev->pxlclk, req_rate);
		if (rate != req_rate) {
			DRM_DEBUG_DRIVER("pxlclk doesn't support %ld Hz\n",
					 req_rate);
			return MODE_NOCLOCK;
		}
	}

	return MODE_OK;
}

static void malidp_crtc_atomic_enable(struct drm_crtc *crtc,
				      struct drm_crtc_state *old_state)
{
	struct malidp_drm *malidp = crtc_to_malidp_device(crtc);
	struct malidp_hw_device *hwdev = malidp->dev;
	struct videomode vm;
	int err = pm_runtime_get_sync(crtc->dev->dev);

	if (err < 0) {
		DRM_DEBUG_DRIVER("Failed to enable runtime power management: %d\n", err);
		return;
	}

	drm_display_mode_to_videomode(&crtc->state->adjusted_mode, &vm);
	clk_prepare_enable(hwdev->pxlclk);

	/* We rely on firmware to set mclk to a sensible level. */
	clk_set_rate(hwdev->pxlclk, crtc->state->adjusted_mode.crtc_clock * 1000);

	hwdev->hw->modeset(hwdev, &vm);
	hwdev->hw->leave_config_mode(hwdev);
	drm_crtc_vblank_on(crtc);
}

static void malidp_crtc_atomic_disable(struct drm_crtc *crtc,
				       struct drm_crtc_state *old_state)
{
	struct malidp_drm *malidp = crtc_to_malidp_device(crtc);
	struct malidp_hw_device *hwdev = malidp->dev;
	int err;

	/* always disable planes on the CRTC that is being turned off */
	drm_atomic_helper_disable_planes_on_crtc(old_state, false);

	drm_crtc_vblank_off(crtc);
	hwdev->hw->enter_config_mode(hwdev);

	clk_disable_unprepare(hwdev->pxlclk);

	err = pm_runtime_put(crtc->dev->dev);
	if (err < 0) {
		DRM_DEBUG_DRIVER("Failed to disable runtime power management: %d\n", err);
	}
}

static const struct gamma_curve_segment {
	u16 start;
	u16 end;
} segments[MALIDP_COEFFTAB_NUM_COEFFS] = {
	/* sector 0 */
	{    0,    0 }, {    1,    1 }, {    2,    2 }, {    3,    3 },
	{    4,    4 }, {    5,    5 }, {    6,    6 }, {    7,    7 },
	{    8,    8 }, {    9,    9 }, {   10,   10 }, {   11,   11 },
	{   12,   12 }, {   13,   13 }, {   14,   14 }, {   15,   15 },
	/* sector 1 */
	{   16,   19 }, {   20,   23 }, {   24,   27 }, {   28,   31 },
	/* sector 2 */
	{   32,   39 }, {   40,   47 }, {   48,   55 }, {   56,   63 },
	/* sector 3 */
	{   64,   79 }, {   80,   95 }, {   96,  111 }, {  112,  127 },
	/* sector 4 */
	{  128,  159 }, {  160,  191 }, {  192,  223 }, {  224,  255 },
	/* sector 5 */
	{  256,  319 }, {  320,  383 }, {  384,  447 }, {  448,  511 },
	/* sector 6 */
	{  512,  639 }, {  640,  767 }, {  768,  895 }, {  896, 1023 },
	{ 1024, 1151 }, { 1152, 1279 }, { 1280, 1407 }, { 1408, 1535 },
	{ 1536, 1663 }, { 1664, 1791 }, { 1792, 1919 }, { 1920, 2047 },
	{ 2048, 2175 }, { 2176, 2303 }, { 2304, 2431 }, { 2432, 2559 },
	{ 2560, 2687 }, { 2688, 2815 }, { 2816, 2943 }, { 2944, 3071 },
	{ 3072, 3199 }, { 3200, 3327 }, { 3328, 3455 }, { 3456, 3583 },
	{ 3584, 3711 }, { 3712, 3839 }, { 3840, 3967 }, { 3968, 4095 },
};

#define DE_COEFTAB_DATA(a, b) ((((a) & 0xfff) << 16) | (((b) & 0xfff)))

static void malidp_generate_gamma_table(struct drm_property_blob *lut_blob,
					u32 coeffs[MALIDP_COEFFTAB_NUM_COEFFS])
{
	struct drm_color_lut *lut = (struct drm_color_lut *)lut_blob->data;
	int i;

	for (i = 0; i < MALIDP_COEFFTAB_NUM_COEFFS; ++i) {
		u32 a, b, delta_in, out_start, out_end;

		delta_in = segments[i].end - segments[i].start;
		/* DP has 12-bit internal precision for its LUTs. */
		out_start = drm_color_lut_extract(lut[segments[i].start].green,
						  12);
		out_end = drm_color_lut_extract(lut[segments[i].end].green, 12);
		a = (delta_in == 0) ? 0 : ((out_end - out_start) * 256) / delta_in;
		b = out_start;
		coeffs[i] = DE_COEFTAB_DATA(a, b);
	}
}

/*
 * Check if there is a new gamma LUT and if it is of an acceptable size. Also,
 * reject any LUTs that use distinct red, green, and blue curves.
 */
static int malidp_crtc_atomic_check_gamma(struct drm_crtc *crtc,
					  struct drm_crtc_state *state)
{
	struct malidp_crtc_state *mc = to_malidp_crtc_state(state);
	struct drm_color_lut *lut;
	size_t lut_size;
	int i;

	if (!state->color_mgmt_changed || !state->gamma_lut)
		return 0;

	if (crtc->state->gamma_lut &&
	    (crtc->state->gamma_lut->base.id == state->gamma_lut->base.id))
		return 0;

	if (state->gamma_lut->length % sizeof(struct drm_color_lut))
		return -EINVAL;

	lut_size = state->gamma_lut->length / sizeof(struct drm_color_lut);
	if (lut_size != MALIDP_GAMMA_LUT_SIZE)
		return -EINVAL;

	lut = (struct drm_color_lut *)state->gamma_lut->data;
	for (i = 0; i < lut_size; ++i)
		if (!((lut[i].red == lut[i].green) &&
		      (lut[i].red == lut[i].blue)))
			return -EINVAL;

	if (!state->mode_changed) {
		int ret;

		state->mode_changed = true;
		/*
		 * Kerneldoc for drm_atomic_helper_check_modeset mandates that
		 * it be invoked when the driver sets ->mode_changed. Since
		 * changing the gamma LUT doesn't depend on any external
		 * resources, it is safe to call it only once.
		 */
		ret = drm_atomic_helper_check_modeset(crtc->dev, state->state);
		if (ret)
			return ret;
	}

	malidp_generate_gamma_table(state->gamma_lut, mc->gamma_coeffs);
	return 0;
}

/*
 * Check if there is a new CTM and if it contains valid input. Valid here means
 * that the number is inside the representable range for a Q3.12 number,
 * excluding truncating the fractional part of the input data.
 *
 * The COLORADJ registers can be changed atomically.
 */
static int malidp_crtc_atomic_check_ctm(struct drm_crtc *crtc,
					struct drm_crtc_state *state)
{
	struct malidp_crtc_state *mc = to_malidp_crtc_state(state);
	struct drm_color_ctm *ctm;
	int i;

	if (!state->color_mgmt_changed)
		return 0;

	if (!state->ctm)
		return 0;

	if (crtc->state->ctm && (crtc->state->ctm->base.id ==
				 state->ctm->base.id))
		return 0;

	/*
	 * The size of the ctm is checked in
	 * drm_atomic_replace_property_blob_from_id.
	 */
	ctm = (struct drm_color_ctm *)state->ctm->data;
	for (i = 0; i < ARRAY_SIZE(ctm->matrix); ++i) {
		/* Convert from S31.32 to Q3.12. */
		s64 val = ctm->matrix[i];
		u32 mag = ((((u64)val) & ~BIT_ULL(63)) >> 20) &
			  GENMASK_ULL(14, 0);

		/*
		 * Convert to 2s complement and check the destination's top bit
		 * for overflow. NB: Can't check before converting or it'd
		 * incorrectly reject the case:
		 * sign == 1
		 * mag == 0x2000
		 */
		if (val & BIT_ULL(63))
			mag = ~mag + 1;
		if (!!(val & BIT_ULL(63)) != !!(mag & BIT(14)))
			return -EINVAL;
		mc->coloradj_coeffs[i] = mag;
	}

	return 0;
}

static int malidp_crtc_atomic_check_scaling(struct drm_crtc *crtc,
					    struct drm_crtc_state *state)
{
	struct malidp_drm *malidp = crtc_to_malidp_device(crtc);
	struct malidp_hw_device *hwdev = malidp->dev;
	struct malidp_crtc_state *cs = to_malidp_crtc_state(state);
	struct malidp_se_config *s = &cs->scaler_config;
	struct drm_plane *plane;
	struct videomode vm;
	const struct drm_plane_state *pstate;
	u32 h_upscale_factor = 0; /* U16.16 */
	u32 v_upscale_factor = 0; /* U16.16 */
	u8 scaling = cs->scaled_planes_mask;
	int ret;

	if (!scaling) {
		s->scale_enable = false;
		goto mclk_calc;
	}

	/* The scaling engine can only handle one plane at a time. */
	if (scaling & (scaling - 1))
		return -EINVAL;

	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, state) {
		struct malidp_plane *mp = to_malidp_plane(plane);
		u32 phase;

		if (!(mp->layer->id & scaling))
			continue;

		/*
		 * Convert crtc_[w|h] to U32.32, then divide by U16.16 src_[w|h]
		 * to get the U16.16 result.
		 */
		h_upscale_factor = div_u64((u64)pstate->crtc_w << 32,
					   pstate->src_w);
		v_upscale_factor = div_u64((u64)pstate->crtc_h << 32,
					   pstate->src_h);

		s->enhancer_enable = ((h_upscale_factor >> 16) >= 2 ||
				      (v_upscale_factor >> 16) >= 2);

		if (pstate->rotation & MALIDP_ROTATED_MASK) {
			s->input_w = pstate->src_h >> 16;
			s->input_h = pstate->src_w >> 16;
		} else {
			s->input_w = pstate->src_w >> 16;
			s->input_h = pstate->src_h >> 16;
		}

		s->output_w = pstate->crtc_w;
		s->output_h = pstate->crtc_h;

#define SE_N_PHASE 4
#define SE_SHIFT_N_PHASE 12
		/* Calculate initial_phase and delta_phase for horizontal. */
		phase = s->input_w;
		s->h_init_phase =
				((phase << SE_N_PHASE) / s->output_w + 1) / 2;

		phase = s->input_w;
		phase <<= (SE_SHIFT_N_PHASE + SE_N_PHASE);
		s->h_delta_phase = phase / s->output_w;

		/* Same for vertical. */
		phase = s->input_h;
		s->v_init_phase =
				((phase << SE_N_PHASE) / s->output_h + 1) / 2;

		phase = s->input_h;
		phase <<= (SE_SHIFT_N_PHASE + SE_N_PHASE);
		s->v_delta_phase = phase / s->output_h;
#undef SE_N_PHASE
#undef SE_SHIFT_N_PHASE
		s->plane_src_id = mp->layer->id;
	}

	s->scale_enable = true;
	s->hcoeff = malidp_se_select_coeffs(h_upscale_factor);
	s->vcoeff = malidp_se_select_coeffs(v_upscale_factor);

mclk_calc:
	drm_display_mode_to_videomode(&state->adjusted_mode, &vm);
	ret = hwdev->hw->se_calc_mclk(hwdev, s, &vm);
	if (ret < 0)
		return -EINVAL;
	return 0;
}

static int malidp_crtc_atomic_check(struct drm_crtc *crtc,
				    struct drm_crtc_state *state)
{
	struct malidp_drm *malidp = crtc_to_malidp_device(crtc);
	struct malidp_hw_device *hwdev = malidp->dev;
	struct drm_plane *plane;
	const struct drm_plane_state *pstate;
	u32 rot_mem_free, rot_mem_usable;
	int rotated_planes = 0;
	int ret;

	/*
	 * check if there is enough rotation memory available for planes
	 * that need 90° and 270° rotion or planes that are compressed.
	 * Each plane has set its required memory size in the ->plane_check()
	 * callback, here we only make sure that the sums are less that the
	 * total usable memory.
	 *
	 * The rotation memory allocation algorithm (for each plane):
	 *  a. If no more rotated or compressed planes exist, all remaining
	 *     rotate memory in the bank is available for use by the plane.
	 *  b. If other rotated or compressed planes exist, and plane's
	 *     layer ID is DE_VIDEO1, it can use all the memory from first bank
	 *     if secondary rotation memory bank is available, otherwise it can
	 *     use up to half the bank's memory.
	 *  c. If other rotated or compressed planes exist, and plane's layer ID
	 *     is not DE_VIDEO1, it can use half of the available memory.
	 *
	 * Note: this algorithm assumes that the order in which the planes are
	 * checked always has DE_VIDEO1 plane first in the list if it is
	 * rotated. Because that is how we create the planes in the first
	 * place, under current DRM version things work, but if ever the order
	 * in which drm_atomic_crtc_state_for_each_plane() iterates over planes
	 * changes, we need to pre-sort the planes before validation.
	 */

	/* first count the number of rotated planes */
	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, state) {
		struct drm_framebuffer *fb = pstate->fb;

		if ((pstate->rotation & MALIDP_ROTATED_MASK) || fb->modifier)
			rotated_planes++;
	}

	rot_mem_free = hwdev->rotation_memory[0];
	/*
	 * if we have more than 1 plane using rotation memory, use the second
	 * block of rotation memory as well
	 */
	if (rotated_planes > 1)
		rot_mem_free += hwdev->rotation_memory[1];

	/* now validate the rotation memory requirements */
	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, state) {
		struct malidp_plane *mp = to_malidp_plane(plane);
		struct malidp_plane_state *ms = to_malidp_plane_state(pstate);
		struct drm_framebuffer *fb = pstate->fb;

		if ((pstate->rotation & MALIDP_ROTATED_MASK) || fb->modifier) {
			/* process current plane */
			rotated_planes--;

			if (!rotated_planes) {
				/* no more rotated planes, we can use what's left */
				rot_mem_usable = rot_mem_free;
			} else {
				if ((mp->layer->id != DE_VIDEO1) ||
				    (hwdev->rotation_memory[1] == 0))
					rot_mem_usable = rot_mem_free / 2;
				else
					rot_mem_usable = hwdev->rotation_memory[0];
			}

			rot_mem_free -= rot_mem_usable;

			if (ms->rotmem_size > rot_mem_usable)
				return -EINVAL;
		}
	}

	/* If only the writeback routing has changed, we don't need a modeset */
	if (state->connectors_changed) {
		u32 old_mask = crtc->state->connector_mask;
		u32 new_mask = state->connector_mask;

		if ((old_mask ^ new_mask) ==
		    (1 << drm_connector_index(&malidp->mw_connector.base)))
			state->connectors_changed = false;
	}

	ret = malidp_crtc_atomic_check_gamma(crtc, state);
	ret = ret ? ret : malidp_crtc_atomic_check_ctm(crtc, state);
	ret = ret ? ret : malidp_crtc_atomic_check_scaling(crtc, state);

	return ret;
}

static const struct drm_crtc_helper_funcs malidp_crtc_helper_funcs = {
	.mode_valid = malidp_crtc_mode_valid,
	.atomic_check = malidp_crtc_atomic_check,
	.atomic_enable = malidp_crtc_atomic_enable,
	.atomic_disable = malidp_crtc_atomic_disable,
};

static struct drm_crtc_state *malidp_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct malidp_crtc_state *state, *old_state;

	if (WARN_ON(!crtc->state))
		return NULL;

	old_state = to_malidp_crtc_state(crtc->state);
	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &state->base);
	memcpy(state->gamma_coeffs, old_state->gamma_coeffs,
	       sizeof(state->gamma_coeffs));
	memcpy(state->coloradj_coeffs, old_state->coloradj_coeffs,
	       sizeof(state->coloradj_coeffs));
	memcpy(&state->scaler_config, &old_state->scaler_config,
	       sizeof(state->scaler_config));
	state->scaled_planes_mask = 0;

	return &state->base;
}

static void malidp_crtc_reset(struct drm_crtc *crtc)
{
	struct malidp_crtc_state *state = NULL;

	if (crtc->state) {
		state = to_malidp_crtc_state(crtc->state);
		__drm_atomic_helper_crtc_destroy_state(crtc->state);
	}

	kfree(state);
	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state) {
		crtc->state = &state->base;
		crtc->state->crtc = crtc;
	}
}

static void malidp_crtc_destroy_state(struct drm_crtc *crtc,
				      struct drm_crtc_state *state)
{
	struct malidp_crtc_state *mali_state = NULL;

	if (state) {
		mali_state = to_malidp_crtc_state(state);
		__drm_atomic_helper_crtc_destroy_state(state);
	}

	kfree(mali_state);
}

static int malidp_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct malidp_drm *malidp = crtc_to_malidp_device(crtc);
	struct malidp_hw_device *hwdev = malidp->dev;

	malidp_hw_enable_irq(hwdev, MALIDP_DE_BLOCK,
			     hwdev->hw->map.de_irq_map.vsync_irq);
	return 0;
}

static void malidp_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct malidp_drm *malidp = crtc_to_malidp_device(crtc);
	struct malidp_hw_device *hwdev = malidp->dev;

	malidp_hw_disable_irq(hwdev, MALIDP_DE_BLOCK,
			      hwdev->hw->map.de_irq_map.vsync_irq);
}

static const struct drm_crtc_funcs malidp_crtc_funcs = {
	.gamma_set = drm_atomic_helper_legacy_gamma_set,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = malidp_crtc_reset,
	.atomic_duplicate_state = malidp_crtc_duplicate_state,
	.atomic_destroy_state = malidp_crtc_destroy_state,
	.enable_vblank = malidp_crtc_enable_vblank,
	.disable_vblank = malidp_crtc_disable_vblank,
};

int malidp_crtc_init(struct drm_device *drm)
{
	struct malidp_drm *malidp = drm->dev_private;
	struct drm_plane *primary = NULL, *plane;
	int ret;

	ret = malidp_de_planes_init(drm);
	if (ret < 0) {
		DRM_ERROR("Failed to initialise planes\n");
		return ret;
	}

	drm_for_each_plane(plane, drm) {
		if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
			primary = plane;
			break;
		}
	}

	if (!primary) {
		DRM_ERROR("no primary plane found\n");
		return -EINVAL;
	}

	ret = drm_crtc_init_with_planes(drm, &malidp->crtc, primary, NULL,
					&malidp_crtc_funcs, NULL);
	if (ret)
		return ret;

	drm_crtc_helper_add(&malidp->crtc, &malidp_crtc_helper_funcs);
	drm_mode_crtc_set_gamma_size(&malidp->crtc, MALIDP_GAMMA_LUT_SIZE);
	/* No inverse-gamma: it is per-plane. */
	drm_crtc_enable_color_mgmt(&malidp->crtc, 0, true, MALIDP_GAMMA_LUT_SIZE);

	malidp_se_set_enh_coeffs(malidp->dev);

	return 0;
}

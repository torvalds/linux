/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 */

#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "atom.h"
#include "atombios_encoders.h"
#include "amdgpu_pll.h"
#include <asm/div64.h>
#include <linux/gcd.h>

/**
 * amdgpu_pll_reduce_ratio - fractional number reduction
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
static void amdgpu_pll_reduce_ratio(unsigned *nom, unsigned *den,
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
 * amdgpu_pll_get_fb_ref_div - feedback and ref divider calculation
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
static void amdgpu_pll_get_fb_ref_div(unsigned nom, unsigned den, unsigned post_div,
				      unsigned fb_div_max, unsigned ref_div_max,
				      unsigned *fb_div, unsigned *ref_div)
{
	/* limit reference * post divider to a maximum */
	ref_div_max = min(128 / post_div, ref_div_max);

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
 * amdgpu_pll_compute - compute PLL paramaters
 *
 * @pll: information about the PLL
 * @freq: requested frequency
 * @dot_clock_p: resulting pixel clock
 * @fb_div_p: resulting feedback divider
 * @frac_fb_div_p: fractional part of the feedback divider
 * @ref_div_p: resulting reference divider
 * @post_div_p: resulting reference divider
 *
 * Try to calculate the PLL parameters to generate the given frequency:
 * dot_clock = (ref_freq * feedback_div) / (ref_div * post_div)
 */
void amdgpu_pll_compute(struct amdgpu_pll *pll,
			u32 freq,
			u32 *dot_clock_p,
			u32 *fb_div_p,
			u32 *frac_fb_div_p,
			u32 *ref_div_p,
			u32 *post_div_p)
{
	unsigned target_clock = pll->flags & AMDGPU_PLL_USE_FRAC_FB_DIV ?
		freq : freq / 10;

	unsigned fb_div_min, fb_div_max, fb_div;
	unsigned post_div_min, post_div_max, post_div;
	unsigned ref_div_min, ref_div_max, ref_div;
	unsigned post_div_best, diff_best;
	unsigned nom, den;

	/* determine allowed feedback divider range */
	fb_div_min = pll->min_feedback_div;
	fb_div_max = pll->max_feedback_div;

	if (pll->flags & AMDGPU_PLL_USE_FRAC_FB_DIV) {
		fb_div_min *= 10;
		fb_div_max *= 10;
	}

	/* determine allowed ref divider range */
	if (pll->flags & AMDGPU_PLL_USE_REF_DIV)
		ref_div_min = pll->reference_div;
	else
		ref_div_min = pll->min_ref_div;

	if (pll->flags & AMDGPU_PLL_USE_FRAC_FB_DIV &&
	    pll->flags & AMDGPU_PLL_USE_REF_DIV)
		ref_div_max = pll->reference_div;
	else
		ref_div_max = pll->max_ref_div;

	/* determine allowed post divider range */
	if (pll->flags & AMDGPU_PLL_USE_POST_DIV) {
		post_div_min = pll->post_div;
		post_div_max = pll->post_div;
	} else {
		unsigned vco_min, vco_max;

		if (pll->flags & AMDGPU_PLL_IS_LCD) {
			vco_min = pll->lcd_pll_out_min;
			vco_max = pll->lcd_pll_out_max;
		} else {
			vco_min = pll->pll_out_min;
			vco_max = pll->pll_out_max;
		}

		if (pll->flags & AMDGPU_PLL_USE_FRAC_FB_DIV) {
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
	amdgpu_pll_reduce_ratio(&nom, &den, fb_div_min, post_div_min);

	/* now search for a post divider */
	if (pll->flags & AMDGPU_PLL_PREFER_MINM_OVER_MAXP)
		post_div_best = post_div_min;
	else
		post_div_best = post_div_max;
	diff_best = ~0;

	for (post_div = post_div_min; post_div <= post_div_max; ++post_div) {
		unsigned diff;
		amdgpu_pll_get_fb_ref_div(nom, den, post_div, fb_div_max,
					  ref_div_max, &fb_div, &ref_div);
		diff = abs(target_clock - (pll->reference_freq * fb_div) /
			(ref_div * post_div));

		if (diff < diff_best || (diff == diff_best &&
		    !(pll->flags & AMDGPU_PLL_PREFER_MINM_OVER_MAXP))) {

			post_div_best = post_div;
			diff_best = diff;
		}
	}
	post_div = post_div_best;

	/* get the feedback and reference divider for the optimal value */
	amdgpu_pll_get_fb_ref_div(nom, den, post_div, fb_div_max, ref_div_max,
				  &fb_div, &ref_div);

	/* reduce the numbers to a simpler ratio once more */
	/* this also makes sure that the reference divider is large enough */
	amdgpu_pll_reduce_ratio(&fb_div, &ref_div, fb_div_min, ref_div_min);

	/* avoid high jitter with small fractional dividers */
	if (pll->flags & AMDGPU_PLL_USE_FRAC_FB_DIV && (fb_div % 10)) {
		fb_div_min = max(fb_div_min, (9 - (fb_div % 10)) * 20 + 60);
		if (fb_div < fb_div_min) {
			unsigned tmp = DIV_ROUND_UP(fb_div_min, fb_div);
			fb_div *= tmp;
			ref_div *= tmp;
		}
	}

	/* and finally save the result */
	if (pll->flags & AMDGPU_PLL_USE_FRAC_FB_DIV) {
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

/**
 * amdgpu_pll_get_use_mask - look up a mask of which pplls are in use
 *
 * @crtc: drm crtc
 *
 * Returns the mask of which PPLLs (Pixel PLLs) are in use.
 */
u32 amdgpu_pll_get_use_mask(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_crtc *test_crtc;
	struct amdgpu_crtc *test_amdgpu_crtc;
	u32 pll_in_use = 0;

	list_for_each_entry(test_crtc, &dev->mode_config.crtc_list, head) {
		if (crtc == test_crtc)
			continue;

		test_amdgpu_crtc = to_amdgpu_crtc(test_crtc);
		if (test_amdgpu_crtc->pll_id != ATOM_PPLL_INVALID)
			pll_in_use |= (1 << test_amdgpu_crtc->pll_id);
	}
	return pll_in_use;
}

/**
 * amdgpu_pll_get_shared_dp_ppll - return the PPLL used by another crtc for DP
 *
 * @crtc: drm crtc
 *
 * Returns the PPLL (Pixel PLL) used by another crtc/encoder which is
 * also in DP mode.  For DP, a single PPLL can be used for all DP
 * crtcs/encoders.
 */
int amdgpu_pll_get_shared_dp_ppll(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_crtc *test_crtc;
	struct amdgpu_crtc *test_amdgpu_crtc;

	list_for_each_entry(test_crtc, &dev->mode_config.crtc_list, head) {
		if (crtc == test_crtc)
			continue;
		test_amdgpu_crtc = to_amdgpu_crtc(test_crtc);
		if (test_amdgpu_crtc->encoder &&
		    ENCODER_MODE_IS_DP(amdgpu_atombios_encoder_get_encoder_mode(test_amdgpu_crtc->encoder))) {
			/* for DP use the same PLL for all */
			if (test_amdgpu_crtc->pll_id != ATOM_PPLL_INVALID)
				return test_amdgpu_crtc->pll_id;
		}
	}
	return ATOM_PPLL_INVALID;
}

/**
 * amdgpu_pll_get_shared_nondp_ppll - return the PPLL used by another non-DP crtc
 *
 * @crtc: drm crtc
 *
 * Returns the PPLL (Pixel PLL) used by another non-DP crtc/encoder which can
 * be shared (i.e., same clock).
 */
int amdgpu_pll_get_shared_nondp_ppll(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_crtc *test_crtc;
	struct amdgpu_crtc *test_amdgpu_crtc;
	u32 adjusted_clock, test_adjusted_clock;

	adjusted_clock = amdgpu_crtc->adjusted_clock;

	if (adjusted_clock == 0)
		return ATOM_PPLL_INVALID;

	list_for_each_entry(test_crtc, &dev->mode_config.crtc_list, head) {
		if (crtc == test_crtc)
			continue;
		test_amdgpu_crtc = to_amdgpu_crtc(test_crtc);
		if (test_amdgpu_crtc->encoder &&
		    !ENCODER_MODE_IS_DP(amdgpu_atombios_encoder_get_encoder_mode(test_amdgpu_crtc->encoder))) {
			/* check if we are already driving this connector with another crtc */
			if (test_amdgpu_crtc->connector == amdgpu_crtc->connector) {
				/* if we are, return that pll */
				if (test_amdgpu_crtc->pll_id != ATOM_PPLL_INVALID)
					return test_amdgpu_crtc->pll_id;
			}
			/* for non-DP check the clock */
			test_adjusted_clock = test_amdgpu_crtc->adjusted_clock;
			if ((crtc->mode.clock == test_crtc->mode.clock) &&
			    (adjusted_clock == test_adjusted_clock) &&
			    (amdgpu_crtc->ss_enabled == test_amdgpu_crtc->ss_enabled) &&
			    (test_amdgpu_crtc->pll_id != ATOM_PPLL_INVALID))
				return test_amdgpu_crtc->pll_id;
		}
	}
	return ATOM_PPLL_INVALID;
}

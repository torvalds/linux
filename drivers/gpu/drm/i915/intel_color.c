/*
 * Copyright © 2016 Intel Corporation
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
 *
 */

#include "intel_drv.h"

#define CTM_COEFF_SIGN	(1ULL << 63)

#define CTM_COEFF_1_0	(1ULL << 32)
#define CTM_COEFF_2_0	(CTM_COEFF_1_0 << 1)
#define CTM_COEFF_4_0	(CTM_COEFF_2_0 << 1)
#define CTM_COEFF_8_0	(CTM_COEFF_4_0 << 1)
#define CTM_COEFF_0_5	(CTM_COEFF_1_0 >> 1)
#define CTM_COEFF_0_25	(CTM_COEFF_0_5 >> 1)
#define CTM_COEFF_0_125	(CTM_COEFF_0_25 >> 1)

#define CTM_COEFF_LIMITED_RANGE ((235ULL - 16ULL) * CTM_COEFF_1_0 / 255)

#define CTM_COEFF_NEGATIVE(coeff)	(((coeff) & CTM_COEFF_SIGN) != 0)
#define CTM_COEFF_ABS(coeff)		((coeff) & (CTM_COEFF_SIGN - 1))

#define LEGACY_LUT_LENGTH		256

/* Post offset values for RGB->YCBCR conversion */
#define POSTOFF_RGB_TO_YUV_HI 0x800
#define POSTOFF_RGB_TO_YUV_ME 0x100
#define POSTOFF_RGB_TO_YUV_LO 0x800

/*
 * These values are direct register values specified in the Bspec,
 * for RGB->YUV conversion matrix (colorspace BT709)
 */
#define CSC_RGB_TO_YUV_RU_GU 0x2ba809d8
#define CSC_RGB_TO_YUV_BU 0x37e80000
#define CSC_RGB_TO_YUV_RY_GY 0x1e089cc0
#define CSC_RGB_TO_YUV_BY 0xb5280000
#define CSC_RGB_TO_YUV_RV_GV 0xbce89ad8
#define CSC_RGB_TO_YUV_BV 0x1e080000

/*
 * Extract the CSC coefficient from a CTM coefficient (in U32.32 fixed point
 * format). This macro takes the coefficient we want transformed and the
 * number of fractional bits.
 *
 * We only have a 9 bits precision window which slides depending on the value
 * of the CTM coefficient and we write the value from bit 3. We also round the
 * value.
 */
#define ILK_CSC_COEFF_FP(coeff, fbits)	\
	(clamp_val(((coeff) >> (32 - (fbits) - 3)) + 4, 0, 0xfff) & 0xff8)

#define ILK_CSC_COEFF_LIMITED_RANGE	\
	ILK_CSC_COEFF_FP(CTM_COEFF_LIMITED_RANGE, 9)
#define ILK_CSC_COEFF_1_0		\
	((7 << 12) | ILK_CSC_COEFF_FP(CTM_COEFF_1_0, 8))

static bool lut_is_legacy(const struct drm_property_blob *lut)
{
	return drm_color_lut_size(lut) == LEGACY_LUT_LENGTH;
}

static bool crtc_state_is_legacy_gamma(const struct intel_crtc_state *crtc_state)
{
	return !crtc_state->base.degamma_lut &&
		!crtc_state->base.ctm &&
		crtc_state->base.gamma_lut &&
		lut_is_legacy(crtc_state->base.gamma_lut);
}

/*
 * When using limited range, multiply the matrix given by userspace by
 * the matrix that we would use for the limited range.
 */
static u64 *ctm_mult_by_limited(u64 *result, const u64 *input)
{
	int i;

	for (i = 0; i < 9; i++) {
		u64 user_coeff = input[i];
		u32 limited_coeff = CTM_COEFF_LIMITED_RANGE;
		u32 abs_coeff = clamp_val(CTM_COEFF_ABS(user_coeff), 0,
					  CTM_COEFF_4_0 - 1) >> 2;

		/*
		 * By scaling every co-efficient with limited range (16-235)
		 * vs full range (0-255) the final o/p will be scaled down to
		 * fit in the limited range supported by the panel.
		 */
		result[i] = mul_u32_u32(limited_coeff, abs_coeff) >> 30;
		result[i] |= user_coeff & CTM_COEFF_SIGN;
	}

	return result;
}

static void ilk_load_ycbcr_conversion_matrix(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	I915_WRITE(PIPE_CSC_PREOFF_HI(pipe), 0);
	I915_WRITE(PIPE_CSC_PREOFF_ME(pipe), 0);
	I915_WRITE(PIPE_CSC_PREOFF_LO(pipe), 0);

	I915_WRITE(PIPE_CSC_COEFF_RU_GU(pipe), CSC_RGB_TO_YUV_RU_GU);
	I915_WRITE(PIPE_CSC_COEFF_BU(pipe), CSC_RGB_TO_YUV_BU);

	I915_WRITE(PIPE_CSC_COEFF_RY_GY(pipe), CSC_RGB_TO_YUV_RY_GY);
	I915_WRITE(PIPE_CSC_COEFF_BY(pipe), CSC_RGB_TO_YUV_BY);

	I915_WRITE(PIPE_CSC_COEFF_RV_GV(pipe), CSC_RGB_TO_YUV_RV_GV);
	I915_WRITE(PIPE_CSC_COEFF_BV(pipe), CSC_RGB_TO_YUV_BV);

	I915_WRITE(PIPE_CSC_POSTOFF_HI(pipe), POSTOFF_RGB_TO_YUV_HI);
	I915_WRITE(PIPE_CSC_POSTOFF_ME(pipe), POSTOFF_RGB_TO_YUV_ME);
	I915_WRITE(PIPE_CSC_POSTOFF_LO(pipe), POSTOFF_RGB_TO_YUV_LO);
	I915_WRITE(PIPE_CSC_MODE(pipe), 0);
}

static void ilk_load_csc_matrix(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	bool limited_color_range = false;
	enum pipe pipe = crtc->pipe;
	u16 coeffs[9] = {};
	int i;

	/*
	 * FIXME if there's a gamma LUT after the CSC, we should
	 * do the range compression using the gamma LUT instead.
	 */
	if (INTEL_GEN(dev_priv) >= 8 || IS_HASWELL(dev_priv))
		limited_color_range = crtc_state->limited_color_range;

	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420 ||
	    crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR444) {
		ilk_load_ycbcr_conversion_matrix(crtc);
		return;
	} else if (crtc_state->base.ctm) {
		struct drm_color_ctm *ctm = crtc_state->base.ctm->data;
		const u64 *input;
		u64 temp[9];

		if (limited_color_range)
			input = ctm_mult_by_limited(temp, ctm->matrix);
		else
			input = ctm->matrix;

		/*
		 * Convert fixed point S31.32 input to format supported by the
		 * hardware.
		 */
		for (i = 0; i < ARRAY_SIZE(coeffs); i++) {
			u64 abs_coeff = ((1ULL << 63) - 1) & input[i];

			/*
			 * Clamp input value to min/max supported by
			 * hardware.
			 */
			abs_coeff = clamp_val(abs_coeff, 0, CTM_COEFF_4_0 - 1);

			/* sign bit */
			if (CTM_COEFF_NEGATIVE(input[i]))
				coeffs[i] |= 1 << 15;

			if (abs_coeff < CTM_COEFF_0_125)
				coeffs[i] |= (3 << 12) |
					ILK_CSC_COEFF_FP(abs_coeff, 12);
			else if (abs_coeff < CTM_COEFF_0_25)
				coeffs[i] |= (2 << 12) |
					ILK_CSC_COEFF_FP(abs_coeff, 11);
			else if (abs_coeff < CTM_COEFF_0_5)
				coeffs[i] |= (1 << 12) |
					ILK_CSC_COEFF_FP(abs_coeff, 10);
			else if (abs_coeff < CTM_COEFF_1_0)
				coeffs[i] |= ILK_CSC_COEFF_FP(abs_coeff, 9);
			else if (abs_coeff < CTM_COEFF_2_0)
				coeffs[i] |= (7 << 12) |
					ILK_CSC_COEFF_FP(abs_coeff, 8);
			else
				coeffs[i] |= (6 << 12) |
					ILK_CSC_COEFF_FP(abs_coeff, 7);
		}
	} else {
		/*
		 * Load an identity matrix if no coefficients are provided.
		 *
		 * TODO: Check what kind of values actually come out of the
		 * pipe with these coeff/postoff values and adjust to get the
		 * best accuracy. Perhaps we even need to take the bpc value
		 * into consideration.
		 */
		for (i = 0; i < 3; i++) {
			if (limited_color_range)
				coeffs[i * 3 + i] =
					ILK_CSC_COEFF_LIMITED_RANGE;
			else
				coeffs[i * 3 + i] = ILK_CSC_COEFF_1_0;
		}
	}

	I915_WRITE(PIPE_CSC_COEFF_RY_GY(pipe), coeffs[0] << 16 | coeffs[1]);
	I915_WRITE(PIPE_CSC_COEFF_BY(pipe), coeffs[2] << 16);

	I915_WRITE(PIPE_CSC_COEFF_RU_GU(pipe), coeffs[3] << 16 | coeffs[4]);
	I915_WRITE(PIPE_CSC_COEFF_BU(pipe), coeffs[5] << 16);

	I915_WRITE(PIPE_CSC_COEFF_RV_GV(pipe), coeffs[6] << 16 | coeffs[7]);
	I915_WRITE(PIPE_CSC_COEFF_BV(pipe), coeffs[8] << 16);

	I915_WRITE(PIPE_CSC_PREOFF_HI(pipe), 0);
	I915_WRITE(PIPE_CSC_PREOFF_ME(pipe), 0);
	I915_WRITE(PIPE_CSC_PREOFF_LO(pipe), 0);

	if (INTEL_GEN(dev_priv) > 6) {
		u16 postoff = 0;

		if (limited_color_range)
			postoff = (16 * (1 << 12) / 255) & 0x1fff;

		I915_WRITE(PIPE_CSC_POSTOFF_HI(pipe), postoff);
		I915_WRITE(PIPE_CSC_POSTOFF_ME(pipe), postoff);
		I915_WRITE(PIPE_CSC_POSTOFF_LO(pipe), postoff);

		I915_WRITE(PIPE_CSC_MODE(pipe), 0);
	} else {
		u32 mode = CSC_MODE_YUV_TO_RGB;

		if (limited_color_range)
			mode |= CSC_BLACK_SCREEN_OFFSET;

		I915_WRITE(PIPE_CSC_MODE(pipe), mode);
	}
}

/*
 * Set up the pipe CSC unit on CherryView.
 */
static void cherryview_load_csc_matrix(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 mode;

	if (crtc_state->base.ctm) {
		const struct drm_color_ctm *ctm = crtc_state->base.ctm->data;
		u16 coeffs[9] = {};
		int i;

		for (i = 0; i < ARRAY_SIZE(coeffs); i++) {
			u64 abs_coeff =
				((1ULL << 63) - 1) & ctm->matrix[i];

			/* Round coefficient. */
			abs_coeff += 1 << (32 - 13);
			/* Clamp to hardware limits. */
			abs_coeff = clamp_val(abs_coeff, 0, CTM_COEFF_8_0 - 1);

			/* Write coefficients in S3.12 format. */
			if (ctm->matrix[i] & (1ULL << 63))
				coeffs[i] = 1 << 15;
			coeffs[i] |= ((abs_coeff >> 32) & 7) << 12;
			coeffs[i] |= (abs_coeff >> 20) & 0xfff;
		}

		I915_WRITE(CGM_PIPE_CSC_COEFF01(pipe),
			   coeffs[1] << 16 | coeffs[0]);
		I915_WRITE(CGM_PIPE_CSC_COEFF23(pipe),
			   coeffs[3] << 16 | coeffs[2]);
		I915_WRITE(CGM_PIPE_CSC_COEFF45(pipe),
			   coeffs[5] << 16 | coeffs[4]);
		I915_WRITE(CGM_PIPE_CSC_COEFF67(pipe),
			   coeffs[7] << 16 | coeffs[6]);
		I915_WRITE(CGM_PIPE_CSC_COEFF8(pipe), coeffs[8]);
	}

	mode = (crtc_state->base.ctm ? CGM_PIPE_MODE_CSC : 0);
	if (!crtc_state_is_legacy_gamma(crtc_state)) {
		mode |= (crtc_state->base.degamma_lut ? CGM_PIPE_MODE_DEGAMMA : 0) |
			(crtc_state->base.gamma_lut ? CGM_PIPE_MODE_GAMMA : 0);
	}
	I915_WRITE(CGM_PIPE_MODE(pipe), mode);
}

/* Loads the legacy palette/gamma unit for the CRTC. */
static void i9xx_load_luts_internal(const struct intel_crtc_state *crtc_state,
				    const struct drm_property_blob *blob)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	int i;

	if (HAS_GMCH(dev_priv)) {
		if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI))
			assert_dsi_pll_enabled(dev_priv);
		else
			assert_pll_enabled(dev_priv, pipe);
	}

	if (blob) {
		const struct drm_color_lut *lut = blob->data;

		for (i = 0; i < 256; i++) {
			u32 word =
				(drm_color_lut_extract(lut[i].red, 8) << 16) |
				(drm_color_lut_extract(lut[i].green, 8) << 8) |
				drm_color_lut_extract(lut[i].blue, 8);

			if (HAS_GMCH(dev_priv))
				I915_WRITE(PALETTE(pipe, i), word);
			else
				I915_WRITE(LGC_PALETTE(pipe, i), word);
		}
	} else {
		for (i = 0; i < 256; i++) {
			u32 word = (i << 16) | (i << 8) | i;

			if (HAS_GMCH(dev_priv))
				I915_WRITE(PALETTE(pipe, i), word);
			else
				I915_WRITE(LGC_PALETTE(pipe, i), word);
		}
	}
}

static void i9xx_load_luts(const struct intel_crtc_state *crtc_state)
{
	i9xx_load_luts_internal(crtc_state, crtc_state->base.gamma_lut);
}

static void hsw_color_commit(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	I915_WRITE(GAMMA_MODE(crtc->pipe), crtc_state->gamma_mode);

	ilk_load_csc_matrix(crtc_state);
}

static void bdw_load_degamma_lut(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct drm_property_blob *degamma_lut = crtc_state->base.degamma_lut;
	u32 i, lut_size = INTEL_INFO(dev_priv)->color.degamma_lut_size;
	enum pipe pipe = crtc->pipe;

	I915_WRITE(PREC_PAL_INDEX(pipe),
		   PAL_PREC_SPLIT_MODE | PAL_PREC_AUTO_INCREMENT);

	if (degamma_lut) {
		const struct drm_color_lut *lut = degamma_lut->data;

		for (i = 0; i < lut_size; i++) {
			u32 word =
			drm_color_lut_extract(lut[i].red, 10) << 20 |
			drm_color_lut_extract(lut[i].green, 10) << 10 |
			drm_color_lut_extract(lut[i].blue, 10);

			I915_WRITE(PREC_PAL_DATA(pipe), word);
		}
	} else {
		for (i = 0; i < lut_size; i++) {
			u32 v = (i * ((1 << 10) - 1)) / (lut_size - 1);

			I915_WRITE(PREC_PAL_DATA(pipe),
				   (v << 20) | (v << 10) | v);
		}
	}
}

static void bdw_load_gamma_lut(const struct intel_crtc_state *crtc_state, u32 offset)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct drm_property_blob *gamma_lut = crtc_state->base.gamma_lut;
	u32 i, lut_size = INTEL_INFO(dev_priv)->color.gamma_lut_size;
	enum pipe pipe = crtc->pipe;

	WARN_ON(offset & ~PAL_PREC_INDEX_VALUE_MASK);

	I915_WRITE(PREC_PAL_INDEX(pipe),
		   (offset ? PAL_PREC_SPLIT_MODE : 0) |
		   PAL_PREC_AUTO_INCREMENT |
		   offset);

	if (gamma_lut) {
		const struct drm_color_lut *lut = gamma_lut->data;

		for (i = 0; i < lut_size; i++) {
			u32 word =
			(drm_color_lut_extract(lut[i].red, 10) << 20) |
			(drm_color_lut_extract(lut[i].green, 10) << 10) |
			drm_color_lut_extract(lut[i].blue, 10);

			I915_WRITE(PREC_PAL_DATA(pipe), word);
		}

		/* Program the max register to clamp values > 1.0. */
		i = lut_size - 1;
		I915_WRITE(PREC_PAL_GC_MAX(pipe, 0),
			   drm_color_lut_extract(lut[i].red, 16));
		I915_WRITE(PREC_PAL_GC_MAX(pipe, 1),
			   drm_color_lut_extract(lut[i].green, 16));
		I915_WRITE(PREC_PAL_GC_MAX(pipe, 2),
			   drm_color_lut_extract(lut[i].blue, 16));
	} else {
		for (i = 0; i < lut_size; i++) {
			u32 v = (i * ((1 << 10) - 1)) / (lut_size - 1);

			I915_WRITE(PREC_PAL_DATA(pipe),
				   (v << 20) | (v << 10) | v);
		}

		I915_WRITE(PREC_PAL_GC_MAX(pipe, 0), (1 << 16) - 1);
		I915_WRITE(PREC_PAL_GC_MAX(pipe, 1), (1 << 16) - 1);
		I915_WRITE(PREC_PAL_GC_MAX(pipe, 2), (1 << 16) - 1);
	}
}

/* Loads the palette/gamma unit for the CRTC on Broadwell+. */
static void broadwell_load_luts(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	if (crtc_state_is_legacy_gamma(crtc_state)) {
		i9xx_load_luts(crtc_state);
	} else {
		bdw_load_degamma_lut(crtc_state);
		bdw_load_gamma_lut(crtc_state,
				   INTEL_INFO(dev_priv)->color.degamma_lut_size);

		/*
		 * Reset the index, otherwise it prevents the legacy palette to be
		 * written properly.
		 */
		I915_WRITE(PREC_PAL_INDEX(pipe), 0);
	}
}

static void glk_load_degamma_lut(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	const u32 lut_size = 33;
	u32 i;

	/*
	 * When setting the auto-increment bit, the hardware seems to
	 * ignore the index bits, so we need to reset it to index 0
	 * separately.
	 */
	I915_WRITE(PRE_CSC_GAMC_INDEX(pipe), 0);
	I915_WRITE(PRE_CSC_GAMC_INDEX(pipe), PRE_CSC_GAMC_AUTO_INCREMENT);

	/*
	 *  FIXME: The pipe degamma table in geminilake doesn't support
	 *  different values per channel, so this just loads a linear table.
	 */
	for (i = 0; i < lut_size; i++) {
		u32 v = (i * (1 << 16)) / (lut_size - 1);

		I915_WRITE(PRE_CSC_GAMC_DATA(pipe), v);
	}

	/* Clamp values > 1.0. */
	while (i++ < 35)
		I915_WRITE(PRE_CSC_GAMC_DATA(pipe), (1 << 16));
}

static void glk_load_luts(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	glk_load_degamma_lut(crtc_state);

	if (crtc_state_is_legacy_gamma(crtc_state)) {
		i9xx_load_luts(crtc_state);
	} else {
		bdw_load_gamma_lut(crtc_state, 0);

		/*
		 * Reset the index, otherwise it prevents the legacy palette to be
		 * written properly.
		 */
		I915_WRITE(PREC_PAL_INDEX(pipe), 0);
	}
}

static void cherryview_load_luts(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct drm_property_blob *gamma_lut = crtc_state->base.gamma_lut;
	const struct drm_property_blob *degamma_lut = crtc_state->base.degamma_lut;
	enum pipe pipe = crtc->pipe;

	cherryview_load_csc_matrix(crtc_state);

	if (crtc_state_is_legacy_gamma(crtc_state)) {
		i9xx_load_luts_internal(crtc_state, gamma_lut);
		return;
	}

	if (degamma_lut) {
		const struct drm_color_lut *lut = degamma_lut->data;
		int i, lut_size = INTEL_INFO(dev_priv)->color.degamma_lut_size;

		for (i = 0; i < lut_size; i++) {
			u32 word0, word1;

			/* Write LUT in U0.14 format. */
			word0 =
			(drm_color_lut_extract(lut[i].green, 14) << 16) |
			drm_color_lut_extract(lut[i].blue, 14);
			word1 = drm_color_lut_extract(lut[i].red, 14);

			I915_WRITE(CGM_PIPE_DEGAMMA(pipe, i, 0), word0);
			I915_WRITE(CGM_PIPE_DEGAMMA(pipe, i, 1), word1);
		}
	}

	if (gamma_lut) {
		const struct drm_color_lut *lut = gamma_lut->data;
		int i, lut_size = INTEL_INFO(dev_priv)->color.gamma_lut_size;

		for (i = 0; i < lut_size; i++) {
			u32 word0, word1;

			/* Write LUT in U0.10 format. */
			word0 =
			(drm_color_lut_extract(lut[i].green, 10) << 16) |
			drm_color_lut_extract(lut[i].blue, 10);
			word1 = drm_color_lut_extract(lut[i].red, 10);

			I915_WRITE(CGM_PIPE_GAMMA(pipe, i, 0), word0);
			I915_WRITE(CGM_PIPE_GAMMA(pipe, i, 1), word1);
		}
	}

	/*
	 * Also program a linear LUT in the legacy block (behind the
	 * CGM block).
	 */
	i9xx_load_luts_internal(crtc_state, NULL);
}

void intel_color_load_luts(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->base.crtc->dev);

	dev_priv->display.load_luts(crtc_state);
}

void intel_color_commit(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->base.crtc->dev);

	if (dev_priv->display.color_commit)
		dev_priv->display.color_commit(crtc_state);
}

static int check_lut_size(const struct drm_property_blob *lut, int expected)
{
	int len;

	if (!lut)
		return 0;

	len = drm_color_lut_size(lut);
	if (len != expected) {
		DRM_DEBUG_KMS("Invalid LUT size; got %d, expected %d\n",
			      len, expected);
		return -EINVAL;
	}

	return 0;
}

int intel_color_check(struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->base.crtc->dev);
	const struct drm_property_blob *gamma_lut = crtc_state->base.gamma_lut;
	const struct drm_property_blob *degamma_lut = crtc_state->base.degamma_lut;
	int gamma_length, degamma_length;
	u32 gamma_tests, degamma_tests;

	degamma_length = INTEL_INFO(dev_priv)->color.degamma_lut_size;
	gamma_length = INTEL_INFO(dev_priv)->color.gamma_lut_size;
	degamma_tests = INTEL_INFO(dev_priv)->color.degamma_lut_tests;
	gamma_tests = INTEL_INFO(dev_priv)->color.gamma_lut_tests;

	/* Always allow legacy gamma LUT with no further checking. */
	if (crtc_state_is_legacy_gamma(crtc_state)) {
		crtc_state->gamma_mode = GAMMA_MODE_MODE_8BIT;
		return 0;
	}

	if (check_lut_size(degamma_lut, degamma_length) ||
	    check_lut_size(gamma_lut, gamma_length))
		return -EINVAL;

	if (drm_color_lut_check(degamma_lut, degamma_tests) ||
	    drm_color_lut_check(gamma_lut, gamma_tests))
		return -EINVAL;

	if (INTEL_GEN(dev_priv) >= 10 || IS_GEMINILAKE(dev_priv))
		crtc_state->gamma_mode = GAMMA_MODE_MODE_10BIT;
	else if (INTEL_GEN(dev_priv) >= 9 || IS_BROADWELL(dev_priv))
		crtc_state->gamma_mode = GAMMA_MODE_MODE_SPLIT;
	else
		crtc_state->gamma_mode = GAMMA_MODE_MODE_8BIT;

	return 0;
}

void intel_color_init(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	drm_mode_crtc_set_gamma_size(&crtc->base, 256);

	if (IS_CHERRYVIEW(dev_priv)) {
		dev_priv->display.load_luts = cherryview_load_luts;
	} else if (IS_HASWELL(dev_priv)) {
		dev_priv->display.load_luts = i9xx_load_luts;
		dev_priv->display.color_commit = hsw_color_commit;
	} else if (IS_BROADWELL(dev_priv) || IS_GEN9_BC(dev_priv) ||
		   IS_BROXTON(dev_priv)) {
		dev_priv->display.load_luts = broadwell_load_luts;
		dev_priv->display.color_commit = hsw_color_commit;
	} else if (IS_GEMINILAKE(dev_priv) || IS_CANNONLAKE(dev_priv)) {
		dev_priv->display.load_luts = glk_load_luts;
		dev_priv->display.color_commit = hsw_color_commit;
	} else {
		dev_priv->display.load_luts = i9xx_load_luts;
	}

	/* Enable color management support when we have degamma & gamma LUTs. */
	if (INTEL_INFO(dev_priv)->color.degamma_lut_size != 0 &&
	    INTEL_INFO(dev_priv)->color.gamma_lut_size != 0)
		drm_crtc_enable_color_mgmt(&crtc->base,
					   INTEL_INFO(dev_priv)->color.degamma_lut_size,
					   true,
					   INTEL_INFO(dev_priv)->color.gamma_lut_size);
}

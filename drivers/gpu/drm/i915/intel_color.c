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

#define LEGACY_LUT_LENGTH		(sizeof(struct drm_color_lut) * 256)

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
#define I9XX_CSC_COEFF_FP(coeff, fbits)	\
	(clamp_val(((coeff) >> (32 - (fbits) - 3)) + 4, 0, 0xfff) & 0xff8)

#define I9XX_CSC_COEFF_LIMITED_RANGE	\
	I9XX_CSC_COEFF_FP(CTM_COEFF_LIMITED_RANGE, 9)
#define I9XX_CSC_COEFF_1_0		\
	((7 << 12) | I9XX_CSC_COEFF_FP(CTM_COEFF_1_0, 8))

static bool crtc_state_is_legacy(struct drm_crtc_state *state)
{
	return !state->degamma_lut &&
		!state->ctm &&
		state->gamma_lut &&
		state->gamma_lut->length == LEGACY_LUT_LENGTH;
}

/*
 * When using limited range, multiply the matrix given by userspace by
 * the matrix that we would use for the limited range. We do the
 * multiplication in U2.30 format.
 */
static void ctm_mult_by_limited(uint64_t *result, int64_t *input)
{
	int i;

	for (i = 0; i < 9; i++)
		result[i] = 0;

	for (i = 0; i < 3; i++) {
		int64_t user_coeff = input[i * 3 + i];
		uint64_t limited_coeff = CTM_COEFF_LIMITED_RANGE >> 2;
		uint64_t abs_coeff = clamp_val(CTM_COEFF_ABS(user_coeff),
					       0,
					       CTM_COEFF_4_0 - 1) >> 2;

		result[i * 3 + i] = (limited_coeff * abs_coeff) >> 27;
		if (CTM_COEFF_NEGATIVE(user_coeff))
			result[i * 3 + i] |= CTM_COEFF_SIGN;
	}
}

static void i9xx_load_ycbcr_conversion_matrix(struct intel_crtc *intel_crtc)
{
	int pipe = intel_crtc->pipe;
	struct drm_i915_private *dev_priv = to_i915(intel_crtc->base.dev);

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

/* Set up the pipe CSC unit. */
static void i9xx_load_csc_matrix(struct drm_crtc_state *crtc_state)
{
	struct drm_crtc *crtc = crtc_state->crtc;
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int i, pipe = intel_crtc->pipe;
	uint16_t coeffs[9] = { 0, };
	struct intel_crtc_state *intel_crtc_state = to_intel_crtc_state(crtc_state);

	if (intel_crtc_state->ycbcr420) {
		i9xx_load_ycbcr_conversion_matrix(intel_crtc);
		return;
	} else if (crtc_state->ctm) {
		struct drm_color_ctm *ctm =
			(struct drm_color_ctm *)crtc_state->ctm->data;
		uint64_t input[9] = { 0, };

		if (intel_crtc_state->limited_color_range) {
			ctm_mult_by_limited(input, ctm->matrix);
		} else {
			for (i = 0; i < ARRAY_SIZE(input); i++)
				input[i] = ctm->matrix[i];
		}

		/*
		 * Convert fixed point S31.32 input to format supported by the
		 * hardware.
		 */
		for (i = 0; i < ARRAY_SIZE(coeffs); i++) {
			uint64_t abs_coeff = ((1ULL << 63) - 1) & input[i];

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
					I9XX_CSC_COEFF_FP(abs_coeff, 12);
			else if (abs_coeff < CTM_COEFF_0_25)
				coeffs[i] |= (2 << 12) |
					I9XX_CSC_COEFF_FP(abs_coeff, 11);
			else if (abs_coeff < CTM_COEFF_0_5)
				coeffs[i] |= (1 << 12) |
					I9XX_CSC_COEFF_FP(abs_coeff, 10);
			else if (abs_coeff < CTM_COEFF_1_0)
				coeffs[i] |= I9XX_CSC_COEFF_FP(abs_coeff, 9);
			else if (abs_coeff < CTM_COEFF_2_0)
				coeffs[i] |= (7 << 12) |
					I9XX_CSC_COEFF_FP(abs_coeff, 8);
			else
				coeffs[i] |= (6 << 12) |
					I9XX_CSC_COEFF_FP(abs_coeff, 7);
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
			if (intel_crtc_state->limited_color_range)
				coeffs[i * 3 + i] =
					I9XX_CSC_COEFF_LIMITED_RANGE;
			else
				coeffs[i * 3 + i] = I9XX_CSC_COEFF_1_0;
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
		uint16_t postoff = 0;

		if (intel_crtc_state->limited_color_range)
			postoff = (16 * (1 << 12) / 255) & 0x1fff;

		I915_WRITE(PIPE_CSC_POSTOFF_HI(pipe), postoff);
		I915_WRITE(PIPE_CSC_POSTOFF_ME(pipe), postoff);
		I915_WRITE(PIPE_CSC_POSTOFF_LO(pipe), postoff);

		I915_WRITE(PIPE_CSC_MODE(pipe), 0);
	} else {
		uint32_t mode = CSC_MODE_YUV_TO_RGB;

		if (intel_crtc_state->limited_color_range)
			mode |= CSC_BLACK_SCREEN_OFFSET;

		I915_WRITE(PIPE_CSC_MODE(pipe), mode);
	}
}

/*
 * Set up the pipe CSC unit on CherryView.
 */
static void cherryview_load_csc_matrix(struct drm_crtc_state *state)
{
	struct drm_crtc *crtc = state->crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe = to_intel_crtc(crtc)->pipe;
	uint32_t mode;

	if (state->ctm) {
		struct drm_color_ctm *ctm =
			(struct drm_color_ctm *) state->ctm->data;
		uint16_t coeffs[9] = { 0, };
		int i;

		for (i = 0; i < ARRAY_SIZE(coeffs); i++) {
			uint64_t abs_coeff =
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

	mode = (state->ctm ? CGM_PIPE_MODE_CSC : 0);
	if (!crtc_state_is_legacy(state)) {
		mode |= (state->degamma_lut ? CGM_PIPE_MODE_DEGAMMA : 0) |
			(state->gamma_lut ? CGM_PIPE_MODE_GAMMA : 0);
	}
	I915_WRITE(CGM_PIPE_MODE(pipe), mode);
}

void intel_color_set_csc(struct drm_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc_state->crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (dev_priv->display.load_csc_matrix)
		dev_priv->display.load_csc_matrix(crtc_state);
}

/* Loads the legacy palette/gamma unit for the CRTC. */
static void i9xx_load_luts_internal(struct drm_crtc *crtc,
				    struct drm_property_blob *blob,
				    struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum pipe pipe = intel_crtc->pipe;
	int i;

	if (HAS_GMCH_DISPLAY(dev_priv)) {
		if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI))
			assert_dsi_pll_enabled(dev_priv);
		else
			assert_pll_enabled(dev_priv, pipe);
	}

	if (blob) {
		struct drm_color_lut *lut = (struct drm_color_lut *) blob->data;
		for (i = 0; i < 256; i++) {
			uint32_t word =
				(drm_color_lut_extract(lut[i].red, 8) << 16) |
				(drm_color_lut_extract(lut[i].green, 8) << 8) |
				drm_color_lut_extract(lut[i].blue, 8);

			if (HAS_GMCH_DISPLAY(dev_priv))
				I915_WRITE(PALETTE(pipe, i), word);
			else
				I915_WRITE(LGC_PALETTE(pipe, i), word);
		}
	} else {
		for (i = 0; i < 256; i++) {
			uint32_t word = (i << 16) | (i << 8) | i;

			if (HAS_GMCH_DISPLAY(dev_priv))
				I915_WRITE(PALETTE(pipe, i), word);
			else
				I915_WRITE(LGC_PALETTE(pipe, i), word);
		}
	}
}

static void i9xx_load_luts(struct drm_crtc_state *crtc_state)
{
	i9xx_load_luts_internal(crtc_state->crtc, crtc_state->gamma_lut,
				to_intel_crtc_state(crtc_state));
}

/* Loads the legacy palette/gamma unit for the CRTC on Haswell. */
static void haswell_load_luts(struct drm_crtc_state *crtc_state)
{
	struct drm_crtc *crtc = crtc_state->crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_crtc_state *intel_crtc_state =
		to_intel_crtc_state(crtc_state);
	bool reenable_ips = false;

	/*
	 * Workaround : Do not read or write the pipe palette/gamma data while
	 * GAMMA_MODE is configured for split gamma and IPS_CTL has IPS enabled.
	 */
	if (IS_HASWELL(dev_priv) && intel_crtc_state->ips_enabled &&
	    (intel_crtc_state->gamma_mode == GAMMA_MODE_MODE_SPLIT)) {
		hsw_disable_ips(intel_crtc);
		reenable_ips = true;
	}

	intel_crtc_state->gamma_mode = GAMMA_MODE_MODE_8BIT;
	I915_WRITE(GAMMA_MODE(intel_crtc->pipe), GAMMA_MODE_MODE_8BIT);

	i9xx_load_luts(crtc_state);

	if (reenable_ips)
		hsw_enable_ips(intel_crtc);
}

static void bdw_load_degamma_lut(struct drm_crtc_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->crtc->dev);
	enum pipe pipe = to_intel_crtc(state->crtc)->pipe;
	uint32_t i, lut_size = INTEL_INFO(dev_priv)->color.degamma_lut_size;

	I915_WRITE(PREC_PAL_INDEX(pipe),
		   PAL_PREC_SPLIT_MODE | PAL_PREC_AUTO_INCREMENT);

	if (state->degamma_lut) {
		struct drm_color_lut *lut =
			(struct drm_color_lut *) state->degamma_lut->data;

		for (i = 0; i < lut_size; i++) {
			uint32_t word =
			drm_color_lut_extract(lut[i].red, 10) << 20 |
			drm_color_lut_extract(lut[i].green, 10) << 10 |
			drm_color_lut_extract(lut[i].blue, 10);

			I915_WRITE(PREC_PAL_DATA(pipe), word);
		}
	} else {
		for (i = 0; i < lut_size; i++) {
			uint32_t v = (i * ((1 << 10) - 1)) / (lut_size - 1);

			I915_WRITE(PREC_PAL_DATA(pipe),
				   (v << 20) | (v << 10) | v);
		}
	}
}

static void bdw_load_gamma_lut(struct drm_crtc_state *state, u32 offset)
{
	struct drm_i915_private *dev_priv = to_i915(state->crtc->dev);
	enum pipe pipe = to_intel_crtc(state->crtc)->pipe;
	uint32_t i, lut_size = INTEL_INFO(dev_priv)->color.gamma_lut_size;

	WARN_ON(offset & ~PAL_PREC_INDEX_VALUE_MASK);

	I915_WRITE(PREC_PAL_INDEX(pipe),
		   (offset ? PAL_PREC_SPLIT_MODE : 0) |
		   PAL_PREC_AUTO_INCREMENT |
		   offset);

	if (state->gamma_lut) {
		struct drm_color_lut *lut =
			(struct drm_color_lut *) state->gamma_lut->data;

		for (i = 0; i < lut_size; i++) {
			uint32_t word =
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
			uint32_t v = (i * ((1 << 10) - 1)) / (lut_size - 1);

			I915_WRITE(PREC_PAL_DATA(pipe),
				   (v << 20) | (v << 10) | v);
		}

		I915_WRITE(PREC_PAL_GC_MAX(pipe, 0), (1 << 16) - 1);
		I915_WRITE(PREC_PAL_GC_MAX(pipe, 1), (1 << 16) - 1);
		I915_WRITE(PREC_PAL_GC_MAX(pipe, 2), (1 << 16) - 1);
	}
}

/* Loads the palette/gamma unit for the CRTC on Broadwell+. */
static void broadwell_load_luts(struct drm_crtc_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->crtc->dev);
	struct intel_crtc_state *intel_state = to_intel_crtc_state(state);
	enum pipe pipe = to_intel_crtc(state->crtc)->pipe;

	if (crtc_state_is_legacy(state)) {
		haswell_load_luts(state);
		return;
	}

	bdw_load_degamma_lut(state);
	bdw_load_gamma_lut(state,
			   INTEL_INFO(dev_priv)->color.degamma_lut_size);

	intel_state->gamma_mode = GAMMA_MODE_MODE_SPLIT;
	I915_WRITE(GAMMA_MODE(pipe), GAMMA_MODE_MODE_SPLIT);
	POSTING_READ(GAMMA_MODE(pipe));

	/*
	 * Reset the index, otherwise it prevents the legacy palette to be
	 * written properly.
	 */
	I915_WRITE(PREC_PAL_INDEX(pipe), 0);
}

static void glk_load_degamma_lut(struct drm_crtc_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->crtc->dev);
	enum pipe pipe = to_intel_crtc(state->crtc)->pipe;
	const uint32_t lut_size = 33;
	uint32_t i;

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
		uint32_t v = (i * (1 << 16)) / (lut_size - 1);

		I915_WRITE(PRE_CSC_GAMC_DATA(pipe), v);
	}

	/* Clamp values > 1.0. */
	while (i++ < 35)
		I915_WRITE(PRE_CSC_GAMC_DATA(pipe), (1 << 16));
}

static void glk_load_luts(struct drm_crtc_state *state)
{
	struct drm_crtc *crtc = state->crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc_state *intel_state = to_intel_crtc_state(state);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;

	glk_load_degamma_lut(state);

	if (crtc_state_is_legacy(state)) {
		haswell_load_luts(state);
		return;
	}

	bdw_load_gamma_lut(state, 0);

	intel_state->gamma_mode = GAMMA_MODE_MODE_10BIT;
	I915_WRITE(GAMMA_MODE(pipe), GAMMA_MODE_MODE_10BIT);
	POSTING_READ(GAMMA_MODE(pipe));
}

/* Loads the palette/gamma unit for the CRTC on CherryView. */
static void cherryview_load_luts(struct drm_crtc_state *state)
{
	struct drm_crtc *crtc = state->crtc;
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	struct drm_color_lut *lut;
	uint32_t i, lut_size;
	uint32_t word0, word1;

	if (crtc_state_is_legacy(state)) {
		/* Turn off degamma/gamma on CGM block. */
		I915_WRITE(CGM_PIPE_MODE(pipe),
			   (state->ctm ? CGM_PIPE_MODE_CSC : 0));
		i9xx_load_luts_internal(crtc, state->gamma_lut,
					to_intel_crtc_state(state));
		return;
	}

	if (state->degamma_lut) {
		lut = (struct drm_color_lut *) state->degamma_lut->data;
		lut_size = INTEL_INFO(dev_priv)->color.degamma_lut_size;
		for (i = 0; i < lut_size; i++) {
			/* Write LUT in U0.14 format. */
			word0 =
			(drm_color_lut_extract(lut[i].green, 14) << 16) |
			drm_color_lut_extract(lut[i].blue, 14);
			word1 = drm_color_lut_extract(lut[i].red, 14);

			I915_WRITE(CGM_PIPE_DEGAMMA(pipe, i, 0), word0);
			I915_WRITE(CGM_PIPE_DEGAMMA(pipe, i, 1), word1);
		}
	}

	if (state->gamma_lut) {
		lut = (struct drm_color_lut *) state->gamma_lut->data;
		lut_size = INTEL_INFO(dev_priv)->color.gamma_lut_size;
		for (i = 0; i < lut_size; i++) {
			/* Write LUT in U0.10 format. */
			word0 =
			(drm_color_lut_extract(lut[i].green, 10) << 16) |
			drm_color_lut_extract(lut[i].blue, 10);
			word1 = drm_color_lut_extract(lut[i].red, 10);

			I915_WRITE(CGM_PIPE_GAMMA(pipe, i, 0), word0);
			I915_WRITE(CGM_PIPE_GAMMA(pipe, i, 1), word1);
		}
	}

	I915_WRITE(CGM_PIPE_MODE(pipe),
		   (state->ctm ? CGM_PIPE_MODE_CSC : 0) |
		   (state->degamma_lut ? CGM_PIPE_MODE_DEGAMMA : 0) |
		   (state->gamma_lut ? CGM_PIPE_MODE_GAMMA : 0));

	/*
	 * Also program a linear LUT in the legacy block (behind the
	 * CGM block).
	 */
	i9xx_load_luts_internal(crtc, NULL, to_intel_crtc_state(state));
}

void intel_color_load_luts(struct drm_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc_state->crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	dev_priv->display.load_luts(crtc_state);
}

int intel_color_check(struct drm_crtc *crtc,
		      struct drm_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	size_t gamma_length, degamma_length;

	degamma_length = INTEL_INFO(dev_priv)->color.degamma_lut_size *
		sizeof(struct drm_color_lut);
	gamma_length = INTEL_INFO(dev_priv)->color.gamma_lut_size *
		sizeof(struct drm_color_lut);

	/*
	 * We allow both degamma & gamma luts at the right size or
	 * NULL.
	 */
	if ((!crtc_state->degamma_lut ||
	     crtc_state->degamma_lut->length == degamma_length) &&
	    (!crtc_state->gamma_lut ||
	     crtc_state->gamma_lut->length == gamma_length))
		return 0;

	/*
	 * We also allow no degamma lut and a gamma lut at the legacy
	 * size (256 entries).
	 */
	if (!crtc_state->degamma_lut &&
	    crtc_state->gamma_lut &&
	    crtc_state->gamma_lut->length == LEGACY_LUT_LENGTH)
		return 0;

	return -EINVAL;
}

void intel_color_init(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);

	drm_mode_crtc_set_gamma_size(crtc, 256);

	if (IS_CHERRYVIEW(dev_priv)) {
		dev_priv->display.load_csc_matrix = cherryview_load_csc_matrix;
		dev_priv->display.load_luts = cherryview_load_luts;
	} else if (IS_HASWELL(dev_priv)) {
		dev_priv->display.load_csc_matrix = i9xx_load_csc_matrix;
		dev_priv->display.load_luts = haswell_load_luts;
	} else if (IS_BROADWELL(dev_priv) || IS_GEN9_BC(dev_priv) ||
		   IS_BROXTON(dev_priv)) {
		dev_priv->display.load_csc_matrix = i9xx_load_csc_matrix;
		dev_priv->display.load_luts = broadwell_load_luts;
	} else if (IS_GEMINILAKE(dev_priv) || IS_CANNONLAKE(dev_priv)) {
		dev_priv->display.load_csc_matrix = i9xx_load_csc_matrix;
		dev_priv->display.load_luts = glk_load_luts;
	} else {
		dev_priv->display.load_luts = i9xx_load_luts;
	}

	/* Enable color management support when we have degamma & gamma LUTs. */
	if (INTEL_INFO(dev_priv)->color.degamma_lut_size != 0 &&
	    INTEL_INFO(dev_priv)->color.gamma_lut_size != 0)
		drm_crtc_enable_color_mgmt(crtc,
					   INTEL_INFO(dev_priv)->color.degamma_lut_size,
					   true,
					   INTEL_INFO(dev_priv)->color.gamma_lut_size);
}

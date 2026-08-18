// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <drm/drm_print.h>

#include "intel_casf.h"
#include "intel_casf_regs.h"
#include "intel_de.h"
#include "intel_display_regs.h"
#include "intel_display_types.h"
#include "skl_scaler.h"

#define MAX_PIXELS_FOR_3_TAP_FILTER (1920 * 1080)
#define MAX_PIXELS_FOR_5_TAP_FILTER (3840 * 2160)

#define FILTER_COEFF_0_125 125
#define FILTER_COEFF_0_25 250
#define FILTER_COEFF_0_5 500
#define FILTER_COEFF_1_0 1000
#define FILTER_COEFF_0_0 0
#define SET_POSITIVE_SIGN(x) ((x) & (~SIGN))

/**
 * DOC: Content Adaptive Sharpness Filter (CASF)
 *
 * Starting from LNL the display engine supports an
 * adaptive sharpening filter, enhancing the image
 * quality. The display hardware utilizes the second
 * pipe scaler for implementing CASF.
 * If sharpness is being enabled then pipe scaling
 * cannot be used.
 * This filter operates on a region of pixels based
 * on the tap size. Coefficients are used to generate
 * an alpha value which blends the sharpened image to
 * original image.
 */

/* Default LUT values to be loaded one time. */
static const u16 sharpness_lut[] = {
	4095, 2047, 1364, 1022, 816, 678, 579,
	504, 444, 397, 357, 323, 293, 268, 244, 224,
	204, 187, 170, 154, 139, 125, 111, 98, 85,
	73, 60, 48, 36, 24, 12, 0
};

const u16 filtercoeff_1[] = {
	FILTER_COEFF_0_0, FILTER_COEFF_0_0, FILTER_COEFF_0_5,
	FILTER_COEFF_1_0, FILTER_COEFF_0_5, FILTER_COEFF_0_0,
	FILTER_COEFF_0_0,
};

const u16 filtercoeff_2[] = {
	FILTER_COEFF_0_0, FILTER_COEFF_0_25, FILTER_COEFF_0_5,
	FILTER_COEFF_1_0, FILTER_COEFF_0_5, FILTER_COEFF_0_25,
	FILTER_COEFF_0_0,
};

const u16 filtercoeff_3[] = {
	FILTER_COEFF_0_125, FILTER_COEFF_0_25, FILTER_COEFF_0_5,
	FILTER_COEFF_1_0, FILTER_COEFF_0_5, FILTER_COEFF_0_25,
	FILTER_COEFF_0_125,
};

static void intel_casf_filter_lut_load(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	int i;

	intel_de_write(display, SHRPLUT_INDEX(crtc->pipe),
		       INDEX_AUTO_INCR | INDEX_VALUE(0));

	for (i = 0; i < ARRAY_SIZE(sharpness_lut); i++)
		intel_de_write(display, SHRPLUT_DATA(crtc->pipe),
			       sharpness_lut[i]);
}

static void intel_casf_compute_win_size(struct intel_crtc_state *crtc_state)
{
	const struct drm_display_mode *mode = &crtc_state->hw.adjusted_mode;
	u32 total_pixels = mode->hdisplay * mode->vdisplay;

	if (total_pixels <= MAX_PIXELS_FOR_3_TAP_FILTER)
		crtc_state->pch_pfit.casf.win_size = SHARPNESS_FILTER_SIZE_3X3;
	else if (total_pixels <= MAX_PIXELS_FOR_5_TAP_FILTER)
		crtc_state->pch_pfit.casf.win_size = SHARPNESS_FILTER_SIZE_5X5;
	else
		crtc_state->pch_pfit.casf.win_size = SHARPNESS_FILTER_SIZE_7X7;
}

static void intel_casf_scaler_compute_coeff(struct intel_crtc_state *crtc_state);

int intel_casf_compute_config(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (crtc_state->hw.sharpness_strength == 0)
		return 0;

	/* CASF with joiner not supported in hardware */
	if (crtc_state->joiner_pipes) {
		drm_dbg_kms(display->drm, "CASF not supported with joiner\n");
		return -EINVAL;
	}

	crtc_state->pch_pfit.casf.enable = true;

	/*
	 * HW takes a value in form (1.0 + strength) in 4.4 fixed format.
	 * Strength is from 0.0-14.9375 ie from 0-239.
	 * User can give value from 0-255 but is clamped to 239.
	 * Ex. User gives 85 which is 5.3125 and adding 1.0 gives 6.3125.
	 * 6.3125 in 4.4 format is b01100101 which is equal to 101.
	 * Also 85 + 16 = 101.
	 */
	crtc_state->pch_pfit.casf.strength =
		min(crtc_state->hw.sharpness_strength, 0xEF) + 0x10;

	intel_casf_compute_win_size(crtc_state);

	intel_casf_scaler_compute_coeff(crtc_state);

	return 0;
}

void intel_casf_sharpness_get_config(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	u32 sharp;

	sharp = intel_de_read(display, SHARPNESS_CTL(crtc->pipe));
	if (sharp & FILTER_EN) {
		if (drm_WARN_ON(display->drm,
				REG_FIELD_GET(FILTER_STRENGTH_MASK, sharp) < 16))
			crtc_state->pch_pfit.casf.strength = 0;
		else
			crtc_state->pch_pfit.casf.strength =
				REG_FIELD_GET(FILTER_STRENGTH_MASK, sharp);
		crtc_state->pch_pfit.casf.enable = true;
		crtc_state->pch_pfit.casf.win_size =
			REG_FIELD_GET(FILTER_SIZE_MASK, sharp);
	}
}

static int casf_coeff_tap(int i)
{
	return i % SCALER_FILTER_NUM_TAPS;
}

static u32 casf_coeff(const struct intel_crtc_state *crtc_state, int tap)
{
	struct scaler_filter_coeff value;
	u32 coeff;

	value = crtc_state->pch_pfit.casf.coeff[tap];
	value.sign = 0;

	coeff = value.sign << 15 | value.exp << 12 | value.mantissa << 3;
	return coeff;
}

/*
 * 17 phase of 7 taps requires 119 coefficients in 60 dwords per set.
 * To enable casf: program scaler coefficients with the coefficients
 * that are calculated and stored in pch_pfit.casf.coeff as per
 * SCALER_COEFFICIENT_FORMAT
 */
static void intel_casf_write_coeff(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	int id = crtc_state->scaler_state.scaler_id;
	int i;

	if (id != 1) {
		drm_WARN(display->drm, 0, "Second scaler not enabled\n");
		return;
	}

	intel_de_write_fw(display, GLK_PS_COEF_INDEX_SET(crtc->pipe, id, 0),
			  PS_COEF_INDEX_AUTO_INC);

	for (i = 0; i < 17 * SCALER_FILTER_NUM_TAPS; i += 2) {
		u32 tmp;
		int tap;

		tap = casf_coeff_tap(i);
		tmp = casf_coeff(crtc_state, tap);

		tap = casf_coeff_tap(i + 1);
		tmp |= casf_coeff(crtc_state, tap) << 16;

		intel_de_write_fw(display, GLK_PS_COEF_DATA_SET(crtc->pipe, id, 0),
				  tmp);
	}
}

static void convert_sharpness_coeff_binary(struct scaler_filter_coeff *coeff,
					  u16 coefficient)
{
	if (coefficient < 25) {
		coeff->mantissa = (coefficient * 2048) / 100;
		coeff->exp = 3;
	} else if (coefficient < 50) {
		coeff->mantissa = (coefficient * 1024) / 100;
		coeff->exp = 2;
	} else if (coefficient < 100) {
		coeff->mantissa = (coefficient * 512) / 100;
		coeff->exp = 1;
	} else {
		coeff->mantissa = (coefficient * 256) / 100;
		coeff->exp = 0;
	}
}

static void intel_casf_scaler_compute_coeff(struct intel_crtc_state *crtc_state)
{
	const u16 *filtercoeff;
	u16 filter_coeff[SCALER_FILTER_NUM_TAPS];
	u16 sum_coeff = 0;
	int i;

	if (crtc_state->pch_pfit.casf.win_size == 0)
		filtercoeff = filtercoeff_1;
	else if (crtc_state->pch_pfit.casf.win_size == 1)
		filtercoeff = filtercoeff_2;
	else
		filtercoeff = filtercoeff_3;

	for (i = 0; i < SCALER_FILTER_NUM_TAPS; i++)
		sum_coeff += *(filtercoeff + i);

	for (i = 0; i < SCALER_FILTER_NUM_TAPS; i++) {
		filter_coeff[i] = (*(filtercoeff + i) * 100 / sum_coeff);
		convert_sharpness_coeff_binary(&crtc_state->pch_pfit.casf.coeff[i],
					      filter_coeff[i]);
	}
}

void intel_casf_setup(const struct intel_crtc_state *crtc_state)
{
	intel_casf_filter_lut_load(crtc_state);
	intel_casf_write_coeff(crtc_state);
}

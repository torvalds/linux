// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <drm/drm_print.h>

#include "i915_reg.h"
#include "intel_casf.h"
#include "intel_casf_regs.h"
#include "intel_de.h"
#include "intel_display_regs.h"
#include "intel_display_types.h"

#define MAX_PIXELS_FOR_3_TAP_FILTER (1920 * 1080)
#define MAX_PIXELS_FOR_5_TAP_FILTER (3840 * 2160)

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

void intel_casf_update_strength(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	int win_size;

	intel_de_rmw(display, SHARPNESS_CTL(crtc->pipe), FILTER_STRENGTH_MASK,
		     FILTER_STRENGTH(crtc_state->hw.casf_params.strength));

	win_size = intel_de_read(display, SKL_PS_WIN_SZ(crtc->pipe, 1));

	intel_de_write_fw(display, SKL_PS_WIN_SZ(crtc->pipe, 1), win_size);
}

static void intel_casf_compute_win_size(struct intel_crtc_state *crtc_state)
{
	const struct drm_display_mode *mode = &crtc_state->hw.adjusted_mode;
	u32 total_pixels = mode->hdisplay * mode->vdisplay;

	if (total_pixels <= MAX_PIXELS_FOR_3_TAP_FILTER)
		crtc_state->hw.casf_params.win_size = SHARPNESS_FILTER_SIZE_3X3;
	else if (total_pixels <= MAX_PIXELS_FOR_5_TAP_FILTER)
		crtc_state->hw.casf_params.win_size = SHARPNESS_FILTER_SIZE_5X5;
	else
		crtc_state->hw.casf_params.win_size = SHARPNESS_FILTER_SIZE_7X7;
}

int intel_casf_compute_config(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (!HAS_CASF(display))
		return 0;

	if (crtc_state->uapi.sharpness_strength == 0) {
		crtc_state->hw.casf_params.casf_enable = false;
		crtc_state->hw.casf_params.strength = 0;
		return 0;
	}

	crtc_state->hw.casf_params.casf_enable = true;

	/*
	 * HW takes a value in form (1.0 + strength) in 4.4 fixed format.
	 * Strength is from 0.0-14.9375 ie from 0-239.
	 * User can give value from 0-255 but is clamped to 239.
	 * Ex. User gives 85 which is 5.3125 and adding 1.0 gives 6.3125.
	 * 6.3125 in 4.4 format is b01100101 which is equal to 101.
	 * Also 85 + 16 = 101.
	 */
	crtc_state->hw.casf_params.strength =
		min(crtc_state->uapi.sharpness_strength, 0xEF) + 0x10;

	intel_casf_compute_win_size(crtc_state);

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
			crtc_state->hw.casf_params.strength = 0;
		else
			crtc_state->hw.casf_params.strength =
				REG_FIELD_GET(FILTER_STRENGTH_MASK, sharp);
		crtc_state->hw.casf_params.casf_enable = true;
		crtc_state->hw.casf_params.win_size =
			REG_FIELD_GET(FILTER_SIZE_MASK, sharp);
	}
}

void intel_casf_enable(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	u32 sharpness_ctl;

	sharpness_ctl = FILTER_EN | FILTER_STRENGTH(crtc_state->hw.casf_params.strength);

	sharpness_ctl |= crtc_state->hw.casf_params.win_size;

	intel_de_write(display, SHARPNESS_CTL(crtc->pipe), sharpness_ctl);
}

void intel_casf_disable(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	intel_de_write(display, SHARPNESS_CTL(crtc->pipe), 0);
}

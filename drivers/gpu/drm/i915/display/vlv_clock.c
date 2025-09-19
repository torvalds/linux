// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <drm/drm_print.h>

#include "intel_display_core.h"
#include "intel_display_types.h"
#include "vlv_clock.h"
#include "vlv_sideband.h"

/*
 * FIXME: The caching of hpll_freq and czclk_freq relies on the first calls
 * occurring at a time when they can actually be read. This appears to be the
 * case, but is somewhat fragile. Make the initialization explicit at a point
 * where they can be reliably read.
 */

/* returns HPLL frequency in kHz */
int vlv_clock_get_hpll_vco(struct drm_device *drm)
{
	struct intel_display *display = to_intel_display(drm);
	int hpll_freq, vco_freq[] = { 800, 1600, 2000, 2400 };

	if (!display->vlv_clock.hpll_freq) {
		vlv_cck_get(drm);
		/* Obtain SKU information */
		hpll_freq = vlv_cck_read(drm, CCK_FUSE_REG) &
			CCK_FUSE_HPLL_FREQ_MASK;
		vlv_cck_put(drm);

		display->vlv_clock.hpll_freq = vco_freq[hpll_freq] * 1000;

		drm_dbg_kms(drm, "HPLL frequency: %d kHz\n", display->vlv_clock.hpll_freq);
	}

	return display->vlv_clock.hpll_freq;
}

static int vlv_clock_get_cck(struct drm_device *drm,
			     const char *name, u32 reg, int ref_freq)
{
	u32 val;
	int divider;

	vlv_cck_get(drm);
	val = vlv_cck_read(drm, reg);
	vlv_cck_put(drm);

	divider = val & CCK_FREQUENCY_VALUES;

	drm_WARN(drm, (val & CCK_FREQUENCY_STATUS) !=
		 (divider << CCK_FREQUENCY_STATUS_SHIFT),
		 "%s change in progress\n", name);

	return DIV_ROUND_CLOSEST(ref_freq << 1, divider + 1);
}

int vlv_clock_get_hrawclk(struct drm_device *drm)
{
	/* RAWCLK_FREQ_VLV register updated from power well code */
	return vlv_clock_get_cck(drm, "hrawclk", CCK_DISPLAY_REF_CLOCK_CONTROL,
				 vlv_clock_get_hpll_vco(drm));
}

int vlv_clock_get_czclk(struct drm_device *drm)
{
	struct intel_display *display = to_intel_display(drm);

	if (!display->vlv_clock.czclk_freq) {
		display->vlv_clock.czclk_freq = vlv_clock_get_cck(drm, "czclk", CCK_CZ_CLOCK_CONTROL,
								  vlv_clock_get_hpll_vco(drm));
		drm_dbg_kms(drm, "CZ clock rate: %d kHz\n", display->vlv_clock.czclk_freq);
	}

	return display->vlv_clock.czclk_freq;
}

int vlv_clock_get_cdclk(struct drm_device *drm)
{
	return vlv_clock_get_cck(drm, "cdclk", CCK_DISPLAY_CLOCK_CONTROL,
				 vlv_clock_get_hpll_vco(drm));
}

int vlv_clock_get_gpll(struct drm_device *drm)
{
	return vlv_clock_get_cck(drm, "GPLL ref", CCK_GPLL_CLOCK_CONTROL,
				 vlv_clock_get_czclk(drm));
}

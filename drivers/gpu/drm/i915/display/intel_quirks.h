/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_QUIRKS_H__
#define __INTEL_QUIRKS_H__

#include <linux/types.h>

struct intel_display;

enum intel_quirk_id {
	QUIRK_BACKLIGHT_PRESENT,
	QUIRK_INCREASE_DDI_DISABLED_TIME,
	QUIRK_INCREASE_T12_DELAY,
	QUIRK_INVERT_BRIGHTNESS,
	QUIRK_LVDS_SSC_DISABLE,
	QUIRK_NO_PPS_BACKLIGHT_POWER_HOOK,
};

void intel_init_quirks(struct intel_display *display);
bool intel_has_quirk(struct intel_display *display, enum intel_quirk_id quirk);

#endif /* __INTEL_QUIRKS_H__ */

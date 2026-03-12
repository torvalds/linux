/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_WA_H__
#define __INTEL_DISPLAY_WA_H__

#include <linux/types.h>

struct intel_display;

void intel_display_wa_apply(struct intel_display *display);

#ifdef I915
static inline bool intel_display_needs_wa_16023588340(struct intel_display *display)
{
	return false;
}
#else
bool intel_display_needs_wa_16023588340(struct intel_display *display);
#endif

/*
 * This enum lists display workarounds; each entry here must have a
 * corresponding case in __intel_display_wa().  Keep both sorted by lineage
 * number.
 */
enum intel_display_wa {
	INTEL_DISPLAY_WA_13012396614,
	INTEL_DISPLAY_WA_14011503117,
	INTEL_DISPLAY_WA_14025769978,
	INTEL_DISPLAY_WA_15018326506,
	INTEL_DISPLAY_WA_16023588340,
	INTEL_DISPLAY_WA_16025573575,
	INTEL_DISPLAY_WA_22014263786,
};

bool __intel_display_wa(struct intel_display *display, enum intel_display_wa wa, const char *name);

#define intel_display_wa(__display, __wa) \
	__intel_display_wa((__display), INTEL_DISPLAY_WA_##__wa, __stringify(__wa))

#endif

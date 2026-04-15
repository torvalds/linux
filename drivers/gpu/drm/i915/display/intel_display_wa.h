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
	INTEL_DISPLAY_WA_1409120013,
	INTEL_DISPLAY_WA_1409767108,
	INTEL_DISPLAY_WA_13012396614,
	INTEL_DISPLAY_WA_14010477008,
	INTEL_DISPLAY_WA_14010480278,
	INTEL_DISPLAY_WA_14010547955,
	INTEL_DISPLAY_WA_14010685332,
	INTEL_DISPLAY_WA_14011294188,
	INTEL_DISPLAY_WA_14011503030,
	INTEL_DISPLAY_WA_14011503117,
	INTEL_DISPLAY_WA_14011508470,
	INTEL_DISPLAY_WA_14011765242,
	INTEL_DISPLAY_WA_14014143976,
	INTEL_DISPLAY_WA_14016740474,
	INTEL_DISPLAY_WA_14020863754,
	INTEL_DISPLAY_WA_14025769978,
	INTEL_DISPLAY_WA_15013987218,
	INTEL_DISPLAY_WA_15018326506,
	INTEL_DISPLAY_WA_16011181250,
	INTEL_DISPLAY_WA_16011303918,
	INTEL_DISPLAY_WA_16011342517,
	INTEL_DISPLAY_WA_16011863758,
	INTEL_DISPLAY_WA_16023588340,
	INTEL_DISPLAY_WA_16025573575,
	INTEL_DISPLAY_WA_16025596647,
	INTEL_DISPLAY_WA_18034343758,
	INTEL_DISPLAY_WA_22010178259,
	INTEL_DISPLAY_WA_22010947358,
	INTEL_DISPLAY_WA_22011320316,
	INTEL_DISPLAY_WA_22012278275,
	INTEL_DISPLAY_WA_22012358565,
	INTEL_DISPLAY_WA_22014263786,
	INTEL_DISPLAY_WA_22021048059,
};

bool __intel_display_wa(struct intel_display *display, enum intel_display_wa wa, const char *name);

#define intel_display_wa(__display, __wa) \
	__intel_display_wa((__display), __wa, __stringify(__wa))

#endif

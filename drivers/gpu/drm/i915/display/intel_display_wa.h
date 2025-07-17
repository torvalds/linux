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

#endif

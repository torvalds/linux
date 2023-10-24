// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _INTEL_DISPLAY_PARAMS_H_
#define _INTEL_DISPLAY_PARAMS_H_

#include <linux/types.h>

struct drm_printer;
struct drm_i915_private;

/*
 * Invoke param, a function-like macro, for each intel display param, with
 * arguments:
 *
 * param(type, name, value, mode)
 *
 * type: parameter type, one of {bool, int, unsigned int, unsigned long, char *}
 * name: name of the parameter
 * value: initial/default value of the parameter
 * mode: debugfs file permissions, one of {0400, 0600, 0}, use 0 to not create
 *       debugfs file
 */
#define INTEL_DISPLAY_PARAMS_FOR_EACH(param) \
	param(char *, vbt_firmware, NULL, 0400) \
	param(int, lvds_channel_mode, 0, 0400) \
	param(int, enable_fbc, -1, 0600) \
	param(int, enable_psr, -1, 0600) \
	param(bool, psr_safest_params, false, 0400) \
	param(bool, enable_psr2_sel_fetch, true, 0400) \

#define MEMBER(T, member, ...) T member;
struct intel_display_params {
	INTEL_DISPLAY_PARAMS_FOR_EACH(MEMBER);
};
#undef MEMBER

void intel_display_params_dump(struct drm_i915_private *i915,
			       struct drm_printer *p);
void intel_display_params_copy(struct intel_display_params *dest);
void intel_display_params_free(struct intel_display_params *params);

#endif

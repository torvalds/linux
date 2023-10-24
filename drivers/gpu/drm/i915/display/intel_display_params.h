// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _INTEL_DISPLAY_PARAMS_H_
#define _INTEL_DISPLAY_PARAMS_H_

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
#define INTEL_DISPLAY_PARAMS_FOR_EACH(param) /* empty define to avoid build failure */

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

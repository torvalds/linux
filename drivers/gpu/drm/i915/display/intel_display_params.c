// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "intel_display_params.h"
#include "i915_drv.h"

#define intel_display_param_named(name, T, perm, desc) \
	module_param_named(name, intel_display_modparams.name, T, perm); \
	MODULE_PARM_DESC(name, desc)
#define intel_display_param_named_unsafe(name, T, perm, desc) \
	module_param_named_unsafe(name, intel_display_modparams.name, T, perm); \
	MODULE_PARM_DESC(name, desc)

static struct intel_display_params intel_display_modparams __read_mostly = {
#define MEMBER(T, member, value, ...) .member = (value),
	INTEL_DISPLAY_PARAMS_FOR_EACH(MEMBER)
#undef MEMBER
};
/*
 * Note: As a rule, keep module parameter sysfs permissions read-only
 * 0400. Runtime changes are only supported through i915 debugfs.
 *
 * For any exceptions requiring write access and runtime changes through module
 * parameter sysfs, prevent debugfs file creation by setting the parameter's
 * debugfs mode to 0.
 */

__maybe_unused static void _param_dup_charp(char **valp)
{
	*valp = kstrdup(*valp ? *valp : "", GFP_ATOMIC);
}

__maybe_unused static void _param_nop(void *valp)
{
}

#define _param_dup(valp)				\
	_Generic(valp,					\
		 char ** : _param_dup_charp,		\
		 default : _param_nop)			\
		(valp)

void intel_display_params_copy(struct intel_display_params *dest)
{
	*dest = intel_display_modparams;
#define DUP(T, x, ...) _param_dup(&dest->x);
	INTEL_DISPLAY_PARAMS_FOR_EACH(DUP);
#undef DUP
}

__maybe_unused static void _param_free_charp(char **valp)
{
	kfree(*valp);
	*valp = NULL;
}

#define _param_free(valp)				\
	_Generic(valp,					\
		 char ** : _param_free_charp,		\
		 default : _param_nop)			\
		(valp)

/* free the allocated members, *not* the passed in params itself */
void intel_display_params_free(struct intel_display_params *params)
{
#define FREE(T, x, ...) _param_free(&params->x);
	INTEL_DISPLAY_PARAMS_FOR_EACH(FREE);
#undef FREE
}

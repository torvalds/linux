/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2018 Intel Corporation
 */

#ifndef __INTEL_WORKAROUNDS_TYPES_H__
#define __INTEL_WORKAROUNDS_TYPES_H__

#include <linux/types.h>

#include "i915_reg.h"

struct i915_wa {
	i915_reg_t	  reg;
	u32		  mask;
	u32		  val;
};

struct i915_wa_list {
	const char	*name;
	struct i915_wa	*list;
	unsigned int	count;
	unsigned int	wa_count;
};

#endif /* __INTEL_WORKAROUNDS_TYPES_H__ */

/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_SDVO_H__
#define __INTEL_SDVO_H__

#include <linux/types.h>

#include "i915_reg_defs.h"

enum pipe;
enum port;
struct intel_display;

#ifdef I915
bool intel_sdvo_port_enabled(struct intel_display *display,
			     i915_reg_t sdvo_reg, enum pipe *pipe);
bool intel_sdvo_init(struct intel_display *display,
		     i915_reg_t reg, enum port port);
#else
static inline bool intel_sdvo_port_enabled(struct intel_display *display,
					   i915_reg_t sdvo_reg, enum pipe *pipe)
{
	return false;
}
static inline bool intel_sdvo_init(struct intel_display *display,
				   i915_reg_t reg, enum port port)
{
	return false;
}
#endif

#endif /* __INTEL_SDVO_H__ */

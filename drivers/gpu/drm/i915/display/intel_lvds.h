/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_LVDS_H__
#define __INTEL_LVDS_H__

#include <linux/types.h>

#include "i915_reg_defs.h"

enum pipe;
struct intel_display;

#ifdef I915
bool intel_lvds_port_enabled(struct intel_display *display,
			     i915_reg_t lvds_reg, enum pipe *pipe);
void intel_lvds_init(struct intel_display *display);
struct intel_encoder *intel_get_lvds_encoder(struct intel_display *display);
bool intel_is_dual_link_lvds(struct intel_display *display);
#else
static inline bool intel_lvds_port_enabled(struct intel_display *display,
					   i915_reg_t lvds_reg, enum pipe *pipe)
{
	return false;
}
static inline void intel_lvds_init(struct intel_display *display)
{
}
static inline struct intel_encoder *intel_get_lvds_encoder(struct intel_display *display)
{
	return NULL;
}
static inline bool intel_is_dual_link_lvds(struct intel_display *display)
{
	return false;
}
#endif

#endif /* __INTEL_LVDS_H__ */

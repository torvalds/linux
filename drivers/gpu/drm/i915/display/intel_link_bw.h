/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_LINK_BW_H__
#define __INTEL_LINK_BW_H__

#include <linux/types.h>

#include "intel_display_limits.h"

struct drm_i915_private;

struct intel_atomic_state;
struct intel_crtc_state;

struct intel_link_bw_limits {
	u8 force_fec_pipes;
	u8 bpp_limit_reached_pipes;
	/* in 1/16 bpp units */
	int max_bpp_x16[I915_MAX_PIPES];
};

void intel_link_bw_init_limits(struct intel_atomic_state *state,
			       struct intel_link_bw_limits *limits);
int intel_link_bw_reduce_bpp(struct intel_atomic_state *state,
			     struct intel_link_bw_limits *limits,
			     u8 pipe_mask,
			     const char *reason);
bool intel_link_bw_set_bpp_limit_for_pipe(struct intel_atomic_state *state,
					  const struct intel_link_bw_limits *old_limits,
					  struct intel_link_bw_limits *new_limits,
					  enum pipe pipe);
int intel_link_bw_atomic_check(struct intel_atomic_state *state,
			       struct intel_link_bw_limits *new_limits);

#endif

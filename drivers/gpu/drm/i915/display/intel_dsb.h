/* SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef _INTEL_DSB_H
#define _INTEL_DSB_H

#include <linux/types.h>

#include "i915_reg_defs.h"

struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_dsb;

enum intel_dsb_id {
	INTEL_DSB_0,
	INTEL_DSB_1,
	INTEL_DSB_2,

	I915_MAX_DSBS,
};

struct intel_dsb *intel_dsb_prepare(struct intel_atomic_state *state,
				    struct intel_crtc *crtc,
				    enum intel_dsb_id dsb_id,
				    unsigned int max_cmds);
void intel_dsb_finish(struct intel_dsb *dsb);
void intel_dsb_cleanup(struct intel_dsb *dsb);
void intel_dsb_reg_write(struct intel_dsb *dsb,
			 i915_reg_t reg, u32 val);
void intel_dsb_reg_write_masked(struct intel_dsb *dsb,
				i915_reg_t reg, u32 mask, u32 val);
void intel_dsb_noop(struct intel_dsb *dsb, int count);
void intel_dsb_nonpost_start(struct intel_dsb *dsb);
void intel_dsb_nonpost_end(struct intel_dsb *dsb);

void intel_dsb_commit(struct intel_dsb *dsb,
		      bool wait_for_vblank);
void intel_dsb_wait(struct intel_dsb *dsb);

#endif

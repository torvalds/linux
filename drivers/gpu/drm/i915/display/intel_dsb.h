/* SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef _INTEL_DSB_H
#define _INTEL_DSB_H

#include <linux/types.h>

#include "i915_reg_defs.h"

struct intel_crtc_state;

void intel_dsb_prepare(struct intel_crtc_state *crtc_state);
void intel_dsb_cleanup(struct intel_crtc_state *crtc_state);
void intel_dsb_reg_write(const struct intel_crtc_state *crtc_state,
			 i915_reg_t reg, u32 val);
void intel_dsb_indexed_reg_write(const struct intel_crtc_state *crtc_state,
				 i915_reg_t reg, u32 val);
void intel_dsb_commit(const struct intel_crtc_state *crtc_state);

#endif

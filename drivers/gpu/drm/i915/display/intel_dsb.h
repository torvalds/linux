/* SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef _INTEL_DSB_H
#define _INTEL_DSB_H

#include <linux/types.h>

#include "i915_reg_defs.h"

struct intel_crtc;
struct intel_dsb;

struct intel_dsb *intel_dsb_prepare(struct intel_crtc *crtc,
				    unsigned int max_cmds);
void intel_dsb_finish(struct intel_dsb *dsb);
void intel_dsb_cleanup(struct intel_dsb *dsb);
void intel_dsb_reg_write(struct intel_dsb *dsb,
			 i915_reg_t reg, u32 val);
void intel_dsb_commit(struct intel_dsb *dsb,
		      bool wait_for_vblank);
void intel_dsb_wait(struct intel_dsb *dsb);

#endif

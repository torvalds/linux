/* SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef _INTEL_DSB_H
#define _INTEL_DSB_H

#include <linux/types.h>

struct intel_crtc;
struct i915_vma;

enum dsb_id {
	INVALID_DSB = -1,
	DSB1,
	DSB2,
	DSB3,
	MAX_DSB_PER_PIPE
};

struct intel_dsb {
	atomic_t refcount;
	enum dsb_id id;
	u32 *cmd_buf;
	struct i915_vma *vma;
};

struct intel_dsb *
intel_dsb_get(struct intel_crtc *crtc);
void intel_dsb_put(struct intel_dsb *dsb);

#endif

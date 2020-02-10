/* SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef _INTEL_DSB_H
#define _INTEL_DSB_H

#include <linux/types.h>

#include "i915_reg.h"

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
	long refcount;
	enum dsb_id id;
	u32 *cmd_buf;
	struct i915_vma *vma;

	/*
	 * free_pos will point the first free entry position
	 * and help in calculating tail of command buffer.
	 */
	int free_pos;

	/*
	 * ins_start_offset will help to store start address of the dsb
	 * instuction and help in identifying the batch of auto-increment
	 * register.
	 */
	u32 ins_start_offset;
};

struct intel_dsb *
intel_dsb_get(struct intel_crtc *crtc);
void intel_dsb_put(struct intel_dsb *dsb);
void intel_dsb_reg_write(struct intel_dsb *dsb, i915_reg_t reg, u32 val);
void intel_dsb_indexed_reg_write(struct intel_dsb *dsb, i915_reg_t reg,
				 u32 val);
void intel_dsb_commit(struct intel_dsb *dsb);

#endif

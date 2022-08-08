/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014 Intel Corporation
 */

#ifndef _INTEL_RENDERSTATE_H_
#define _INTEL_RENDERSTATE_H_

#include <linux/types.h>
#include "i915_gem.h"

struct i915_request;
struct intel_context;
struct i915_vma;

struct intel_renderstate_rodata {
	const u32 *reloc;
	const u32 *batch;
	const u32 batch_items;
};

#define RO_RENDERSTATE(_g)						\
	const struct intel_renderstate_rodata gen ## _g ## _null_state = { \
		.reloc = gen ## _g ## _null_state_relocs,		\
		.batch = gen ## _g ## _null_state_batch,		\
		.batch_items = sizeof(gen ## _g ## _null_state_batch)/4, \
	}

extern const struct intel_renderstate_rodata gen6_null_state;
extern const struct intel_renderstate_rodata gen7_null_state;
extern const struct intel_renderstate_rodata gen8_null_state;
extern const struct intel_renderstate_rodata gen9_null_state;

struct intel_renderstate {
	struct i915_gem_ww_ctx ww;
	const struct intel_renderstate_rodata *rodata;
	struct i915_vma *vma;
	u32 batch_offset;
	u32 batch_size;
	u32 aux_offset;
	u32 aux_size;
};

int intel_renderstate_init(struct intel_renderstate *so,
			   struct intel_context *ce);
int intel_renderstate_emit(struct intel_renderstate *so,
			   struct i915_request *rq);
void intel_renderstate_fini(struct intel_renderstate *so,
			    struct intel_context *ce);

#endif /* _INTEL_RENDERSTATE_H_ */

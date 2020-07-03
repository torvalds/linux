/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __GEN6_ENGINE_CS_H__
#define __GEN6_ENGINE_CS_H__

#include <linux/types.h>

#include "intel_gpu_commands.h"

struct i915_request;
struct intel_engine_cs;

int gen6_emit_flush_rcs(struct i915_request *rq, u32 mode);
int gen6_emit_flush_vcs(struct i915_request *rq, u32 mode);
int gen6_emit_flush_xcs(struct i915_request *rq, u32 mode);
u32 *gen6_emit_breadcrumb_rcs(struct i915_request *rq, u32 *cs);
u32 *gen6_emit_breadcrumb_xcs(struct i915_request *rq, u32 *cs);

int gen7_emit_flush_rcs(struct i915_request *rq, u32 mode);
u32 *gen7_emit_breadcrumb_rcs(struct i915_request *rq, u32 *cs);
u32 *gen7_emit_breadcrumb_xcs(struct i915_request *rq, u32 *cs);

int gen6_emit_bb_start(struct i915_request *rq,
		       u64 offset, u32 len,
		       unsigned int dispatch_flags);
int hsw_emit_bb_start(struct i915_request *rq,
		      u64 offset, u32 len,
		      unsigned int dispatch_flags);

void gen6_irq_enable(struct intel_engine_cs *engine);
void gen6_irq_disable(struct intel_engine_cs *engine);

void hsw_irq_enable_vecs(struct intel_engine_cs *engine);
void hsw_irq_disable_vecs(struct intel_engine_cs *engine);

#endif /* __GEN6_ENGINE_CS_H__ */

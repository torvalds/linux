/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014 Intel Corporation
 */

#ifndef __GEN8_ENGINE_CS_H__
#define __GEN8_ENGINE_CS_H__

#include <linux/string.h>
#include <linux/types.h>

#include "i915_gem.h" /* GEM_BUG_ON */
#include "intel_gt_regs.h"
#include "intel_gpu_commands.h"

struct i915_request;

int gen8_emit_flush_rcs(struct i915_request *rq, u32 mode);
int gen11_emit_flush_rcs(struct i915_request *rq, u32 mode);
int gen12_emit_flush_rcs(struct i915_request *rq, u32 mode);

int gen8_emit_flush_xcs(struct i915_request *rq, u32 mode);
int gen12_emit_flush_xcs(struct i915_request *rq, u32 mode);

int gen8_emit_init_breadcrumb(struct i915_request *rq);

int gen8_emit_bb_start_noarb(struct i915_request *rq,
			     u64 offset, u32 len,
			     const unsigned int flags);
int gen8_emit_bb_start(struct i915_request *rq,
		       u64 offset, u32 len,
		       const unsigned int flags);

int gen125_emit_bb_start_noarb(struct i915_request *rq,
			       u64 offset, u32 len,
			       const unsigned int flags);
int gen125_emit_bb_start(struct i915_request *rq,
			 u64 offset, u32 len,
			 const unsigned int flags);

u32 *gen8_emit_fini_breadcrumb_xcs(struct i915_request *rq, u32 *cs);
u32 *gen12_emit_fini_breadcrumb_xcs(struct i915_request *rq, u32 *cs);

u32 *gen8_emit_fini_breadcrumb_rcs(struct i915_request *rq, u32 *cs);
u32 *gen11_emit_fini_breadcrumb_rcs(struct i915_request *rq, u32 *cs);
u32 *gen12_emit_fini_breadcrumb_rcs(struct i915_request *rq, u32 *cs);

u32 *gen12_emit_aux_table_inv(u32 *cs, const i915_reg_t inv_reg);

static inline u32 *
__gen8_emit_pipe_control(u32 *batch, u32 flags0, u32 flags1, u32 offset)
{
	memset(batch, 0, 6 * sizeof(u32));

	batch[0] = GFX_OP_PIPE_CONTROL(6) | flags0;
	batch[1] = flags1;
	batch[2] = offset;

	return batch + 6;
}

static inline u32 *gen8_emit_pipe_control(u32 *batch, u32 flags, u32 offset)
{
	return __gen8_emit_pipe_control(batch, 0, flags, offset);
}

static inline u32 *gen12_emit_pipe_control(u32 *batch, u32 flags0, u32 flags1, u32 offset)
{
	return __gen8_emit_pipe_control(batch, flags0, flags1, offset);
}

static inline u32 *
__gen8_emit_write_rcs(u32 *cs, u32 value, u32 offset, u32 flags0, u32 flags1)
{
	*cs++ = GFX_OP_PIPE_CONTROL(6) | flags0;
	*cs++ = flags1 | PIPE_CONTROL_QW_WRITE;
	*cs++ = offset;
	*cs++ = 0;
	*cs++ = value;
	*cs++ = 0; /* We're thrashing one extra dword. */

	return cs;
}

static inline u32*
gen8_emit_ggtt_write_rcs(u32 *cs, u32 value, u32 gtt_offset, u32 flags)
{
	/* We're using qword write, offset should be aligned to 8 bytes. */
	GEM_BUG_ON(!IS_ALIGNED(gtt_offset, 8));

	return __gen8_emit_write_rcs(cs,
				     value,
				     gtt_offset,
				     0,
				     flags | PIPE_CONTROL_GLOBAL_GTT_IVB);
}

static inline u32*
gen12_emit_ggtt_write_rcs(u32 *cs, u32 value, u32 gtt_offset, u32 flags0, u32 flags1)
{
	/* We're using qword write, offset should be aligned to 8 bytes. */
	GEM_BUG_ON(!IS_ALIGNED(gtt_offset, 8));

	return __gen8_emit_write_rcs(cs,
				     value,
				     gtt_offset,
				     flags0,
				     flags1 | PIPE_CONTROL_GLOBAL_GTT_IVB);
}

static inline u32 *
__gen8_emit_flush_dw(u32 *cs, u32 value, u32 gtt_offset, u32 flags)
{
	*cs++ = (MI_FLUSH_DW + 1) | flags;
	*cs++ = gtt_offset;
	*cs++ = 0;
	*cs++ = value;

	return cs;
}

static inline u32 *
gen8_emit_ggtt_write(u32 *cs, u32 value, u32 gtt_offset, u32 flags)
{
	/* w/a: bit 5 needs to be zero for MI_FLUSH_DW address. */
	GEM_BUG_ON(gtt_offset & (1 << 5));
	/* Offset should be aligned to 8 bytes for both (QW/DW) write types */
	GEM_BUG_ON(!IS_ALIGNED(gtt_offset, 8));

	return __gen8_emit_flush_dw(cs,
				    value,
				    gtt_offset | MI_FLUSH_DW_USE_GTT,
				    flags | MI_FLUSH_DW_OP_STOREDW);
}

#endif /* __GEN8_ENGINE_CS_H__ */

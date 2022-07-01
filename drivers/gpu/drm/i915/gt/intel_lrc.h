/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014 Intel Corporation
 */

#ifndef __INTEL_LRC_H__
#define __INTEL_LRC_H__

#include "i915_priolist_types.h"

#include <linux/bitfield.h>
#include <linux/types.h>

#include "intel_context.h"

struct drm_i915_gem_object;
struct i915_gem_ww_ctx;
struct intel_engine_cs;
struct intel_ring;
struct kref;

/* At the start of the context image is its per-process HWS page */
#define LRC_PPHWSP_PN	(0)
#define LRC_PPHWSP_SZ	(1)
/* After the PPHWSP we have the logical state for the context */
#define LRC_STATE_PN	(LRC_PPHWSP_PN + LRC_PPHWSP_SZ)
#define LRC_STATE_OFFSET (LRC_STATE_PN * PAGE_SIZE)

/* Space within PPHWSP reserved to be used as scratch */
#define LRC_PPHWSP_SCRATCH		0x34
#define LRC_PPHWSP_SCRATCH_ADDR		(LRC_PPHWSP_SCRATCH * sizeof(u32))

void lrc_init_wa_ctx(struct intel_engine_cs *engine);
void lrc_fini_wa_ctx(struct intel_engine_cs *engine);

int lrc_alloc(struct intel_context *ce,
	      struct intel_engine_cs *engine);
void lrc_reset(struct intel_context *ce);
void lrc_fini(struct intel_context *ce);
void lrc_destroy(struct kref *kref);

int
lrc_pre_pin(struct intel_context *ce,
	    struct intel_engine_cs *engine,
	    struct i915_gem_ww_ctx *ww,
	    void **vaddr);
int
lrc_pin(struct intel_context *ce,
	struct intel_engine_cs *engine,
	void *vaddr);
void lrc_unpin(struct intel_context *ce);
void lrc_post_unpin(struct intel_context *ce);

void lrc_init_state(struct intel_context *ce,
		    struct intel_engine_cs *engine,
		    void *state);

void lrc_init_regs(const struct intel_context *ce,
		   const struct intel_engine_cs *engine,
		   bool clear);
void lrc_reset_regs(const struct intel_context *ce,
		    const struct intel_engine_cs *engine);

u32 lrc_update_regs(const struct intel_context *ce,
		    const struct intel_engine_cs *engine,
		    u32 head);
void lrc_update_offsets(struct intel_context *ce,
			struct intel_engine_cs *engine);

void lrc_check_regs(const struct intel_context *ce,
		    const struct intel_engine_cs *engine,
		    const char *when);

void lrc_update_runtime(struct intel_context *ce);

enum {
	INTEL_ADVANCED_CONTEXT = 0,
	INTEL_LEGACY_32B_CONTEXT,
	INTEL_ADVANCED_AD_CONTEXT,
	INTEL_LEGACY_64B_CONTEXT
};

enum {
	FAULT_AND_HANG = 0,
	FAULT_AND_HALT, /* Debug only */
	FAULT_AND_STREAM,
	FAULT_AND_CONTINUE /* Unsupported */
};

#define CTX_GTT_ADDRESS_MASK			GENMASK(31, 12)
#define GEN8_CTX_VALID				(1 << 0)
#define GEN8_CTX_FORCE_PD_RESTORE		(1 << 1)
#define GEN8_CTX_FORCE_RESTORE			(1 << 2)
#define GEN8_CTX_L3LLC_COHERENT			(1 << 5)
#define GEN8_CTX_PRIVILEGE			(1 << 8)
#define GEN8_CTX_ADDRESSING_MODE_SHIFT		3
#define GEN12_CTX_PRIORITY_MASK			GENMASK(10, 9)
#define GEN12_CTX_PRIORITY_HIGH			FIELD_PREP(GEN12_CTX_PRIORITY_MASK, 2)
#define GEN12_CTX_PRIORITY_NORMAL		FIELD_PREP(GEN12_CTX_PRIORITY_MASK, 1)
#define GEN12_CTX_PRIORITY_LOW			FIELD_PREP(GEN12_CTX_PRIORITY_MASK, 0)
#define GEN8_CTX_ID_SHIFT			32
#define GEN8_CTX_ID_WIDTH			21
#define GEN11_SW_CTX_ID_SHIFT			37
#define GEN11_SW_CTX_ID_WIDTH			11
#define GEN11_ENGINE_CLASS_SHIFT		61
#define GEN11_ENGINE_CLASS_WIDTH		3
#define GEN11_ENGINE_INSTANCE_SHIFT		48
#define GEN11_ENGINE_INSTANCE_WIDTH		6
#define XEHP_SW_CTX_ID_SHIFT			39
#define XEHP_SW_CTX_ID_WIDTH			16
#define XEHP_SW_COUNTER_SHIFT			58
#define XEHP_SW_COUNTER_WIDTH			6

static inline void lrc_runtime_start(struct intel_context *ce)
{
	struct intel_context_stats *stats = &ce->stats;

	if (intel_context_is_barrier(ce))
		return;

	if (stats->active)
		return;

	WRITE_ONCE(stats->active, intel_context_clock());
}

static inline void lrc_runtime_stop(struct intel_context *ce)
{
	struct intel_context_stats *stats = &ce->stats;

	if (!stats->active)
		return;

	lrc_update_runtime(ce);
	WRITE_ONCE(stats->active, 0);
}

#define DG2_PREDICATE_RESULT_WA (PAGE_SIZE - sizeof(u64))
#define DG2_PREDICATE_RESULT_BB (2048)

u32 lrc_indirect_bb(const struct intel_context *ce);

#endif /* __INTEL_LRC_H__ */

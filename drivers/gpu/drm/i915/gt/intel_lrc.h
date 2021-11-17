/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014 Intel Corporation
 */

#ifndef __INTEL_LRC_H__
#define __INTEL_LRC_H__

#include <linux/types.h>

#include "intel_context.h"
#include "intel_lrc_reg.h"

struct drm_i915_gem_object;
struct intel_engine_cs;
struct intel_ring;

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
static inline u32 lrc_get_runtime(const struct intel_context *ce)
{
	/*
	 * We can use either ppHWSP[16] which is recorded before the context
	 * switch (and so excludes the cost of context switches) or use the
	 * value from the context image itself, which is saved/restored earlier
	 * and so includes the cost of the save.
	 */
	return READ_ONCE(ce->lrc_reg_state[CTX_TIMESTAMP]);
}

#endif /* __INTEL_LRC_H__ */

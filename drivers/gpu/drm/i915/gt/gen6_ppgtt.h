/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __GEN6_PPGTT_H__
#define __GEN6_PPGTT_H__

#include "intel_gtt.h"

struct i915_gem_ww_ctx;

struct gen6_ppgtt {
	struct i915_ppgtt base;

	struct mutex flush;
	struct i915_vma *vma;
	gen6_pte_t __iomem *pd_addr;
	u32 pp_dir;

	atomic_t pin_count;

	bool scan_for_unused_pt;
};

static inline u32 gen6_pte_index(u32 addr)
{
	return i915_pte_index(addr, GEN6_PDE_SHIFT);
}

static inline u32 gen6_pte_count(u32 addr, u32 length)
{
	return i915_pte_count(addr, length, GEN6_PDE_SHIFT);
}

static inline u32 gen6_pde_index(u32 addr)
{
	return i915_pde_index(addr, GEN6_PDE_SHIFT);
}

#define __to_gen6_ppgtt(base) container_of(base, struct gen6_ppgtt, base)

static inline struct gen6_ppgtt *to_gen6_ppgtt(struct i915_ppgtt *base)
{
	BUILD_BUG_ON(offsetof(struct gen6_ppgtt, base));
	return __to_gen6_ppgtt(base);
}

/*
 * gen6_for_each_pde() iterates over every pde from start until start+length.
 * If start and start+length are not perfectly divisible, the macro will round
 * down and up as needed. Start=0 and length=2G effectively iterates over
 * every PDE in the system. The macro modifies ALL its parameters except 'pd',
 * so each of the other parameters should preferably be a simple variable, or
 * at most an lvalue with no side-effects!
 */
#define gen6_for_each_pde(pt, pd, start, length, iter)			\
	for (iter = gen6_pde_index(start);				\
	     length > 0 && iter < I915_PDES &&				\
		     (pt = i915_pt_entry(pd, iter), true);		\
	     ({ u32 temp = ALIGN(start + 1, 1 << GEN6_PDE_SHIFT);	\
		    temp = min(temp - start, length);			\
		    start += temp; length -= temp; }), ++iter)

#define gen6_for_all_pdes(pt, pd, iter)					\
	for (iter = 0;							\
	     iter < I915_PDES &&					\
		     (pt = i915_pt_entry(pd, iter), true);		\
	     ++iter)

int gen6_ppgtt_pin(struct i915_ppgtt *base, struct i915_gem_ww_ctx *ww);
void gen6_ppgtt_unpin(struct i915_ppgtt *base);
void gen6_ppgtt_enable(struct intel_gt *gt);
void gen7_ppgtt_enable(struct intel_gt *gt);
struct i915_ppgtt *gen6_ppgtt_create(struct intel_gt *gt);

#endif

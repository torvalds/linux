/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __GEN8_PPGTT_H__
#define __GEN8_PPGTT_H__

#include <linux/kernel.h>

struct i915_address_space;
struct intel_gt;
enum i915_cache_level;

struct i915_ppgtt *gen8_ppgtt_create(struct intel_gt *gt);
u64 gen8_ggtt_pte_encode(dma_addr_t addr,
			 enum i915_cache_level level,
			 u32 flags);

#endif

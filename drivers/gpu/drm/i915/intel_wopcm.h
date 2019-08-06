/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2017-2018 Intel Corporation
 */

#ifndef _INTEL_WOPCM_H_
#define _INTEL_WOPCM_H_

#include <linux/types.h>

struct intel_gt;

/**
 * struct intel_wopcm - Overall WOPCM info and WOPCM regions.
 * @size: Size of overall WOPCM.
 * @guc: GuC WOPCM Region info.
 * @guc.base: GuC WOPCM base which is offset from WOPCM base.
 * @guc.size: Size of the GuC WOPCM region.
 */
struct intel_wopcm {
	u32 size;
	struct {
		u32 base;
		u32 size;
	} guc;
};

/**
 * intel_wopcm_guc_size()
 * @wopcm:	intel_wopcm structure
 *
 * Returns size of the WOPCM shadowed region.
 *
 * Returns:
 * 0 if GuC is not present or not in use.
 * Otherwise, the GuC WOPCM size.
 */
static inline u32 intel_wopcm_guc_size(struct intel_wopcm *wopcm)
{
	return wopcm->guc.size;
}

void intel_wopcm_init_early(struct intel_wopcm *wopcm);
int intel_wopcm_init(struct intel_wopcm *wopcm);
int intel_wopcm_init_hw(struct intel_wopcm *wopcm, struct intel_gt *gt);

#endif

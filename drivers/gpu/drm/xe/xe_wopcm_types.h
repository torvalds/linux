/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_WOPCM_TYPES_H_
#define _XE_WOPCM_TYPES_H_

#include <linux/types.h>

/**
 * struct xe_wopcm - Overall WOPCM info and WOPCM regions.
 */
struct xe_wopcm {
	/** @size: Size of overall WOPCM */
	u32 size;
	/** @guc: GuC WOPCM Region info */
	struct {
		/** @guc.base: GuC WOPCM base which is offset from WOPCM base */
		u32 base;
		/** @guc.size: Size of the GuC WOPCM region */
		u32 size;
	} guc;
};

#endif

/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_MOCS_H_
#define _XE_MOCS_H_

#include <linux/types.h>

struct xe_engine;
struct xe_gt;

void xe_mocs_init_early(struct xe_gt *gt);
void xe_mocs_init(struct xe_gt *gt);

/**
 * xe_mocs_index_to_value - Translate mocs index to the mocs value exected by
 * most blitter commands.
 * @mocs_index: index into the mocs tables
 *
 * Return: The corresponding mocs value to be programmed.
 */
static inline u32 xe_mocs_index_to_value(u32 mocs_index)
{
	return mocs_index << 1;
}

#endif

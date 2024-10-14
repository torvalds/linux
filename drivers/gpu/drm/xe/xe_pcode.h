/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_PCODE_H_
#define _XE_PCODE_H_

#include <linux/types.h>
struct xe_tile;
struct xe_device;

void xe_pcode_init(struct xe_tile *tile);
int xe_pcode_probe_early(struct xe_device *xe);
int xe_pcode_ready(struct xe_device *xe, bool locked);
int xe_pcode_init_min_freq_table(struct xe_tile *tile, u32 min_gt_freq,
				 u32 max_gt_freq);
int xe_pcode_read(struct xe_tile *tile, u32 mbox, u32 *val, u32 *val1);
int xe_pcode_write_timeout(struct xe_tile *tile, u32 mbox, u32 val,
			   int timeout_ms);
#define xe_pcode_write(tile, mbox, val) \
	xe_pcode_write_timeout(tile, mbox, val, 1)

int xe_pcode_request(struct xe_tile *tile, u32 mbox, u32 request,
		     u32 reply_mask, u32 reply, int timeout_ms);

#define PCODE_MBOX(mbcmd, param1, param2)\
	(FIELD_PREP(PCODE_MB_COMMAND, mbcmd)\
	| FIELD_PREP(PCODE_MB_PARAM1, param1)\
	| FIELD_PREP(PCODE_MB_PARAM2, param2))

#endif

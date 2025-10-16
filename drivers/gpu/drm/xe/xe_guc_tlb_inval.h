/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_GUC_TLB_INVAL_H_
#define _XE_GUC_TLB_INVAL_H_

#include <linux/types.h>

struct xe_guc;
struct xe_tlb_inval;

void xe_guc_tlb_inval_init_early(struct xe_guc *guc,
				 struct xe_tlb_inval *tlb_inval);

int xe_guc_tlb_inval_done_handler(struct xe_guc *guc, u32 *msg, u32 len);

#endif

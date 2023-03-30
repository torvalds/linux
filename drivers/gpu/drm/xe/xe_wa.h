/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_WA_
#define _XE_WA_

struct xe_gt;
struct xe_hw_engine;

void xe_wa_process_gt(struct xe_gt *gt);
void xe_wa_process_engine(struct xe_hw_engine *hwe);
void xe_wa_process_lrc(struct xe_hw_engine *hwe);

void xe_reg_whitelist_process_engine(struct xe_hw_engine *hwe);

#endif

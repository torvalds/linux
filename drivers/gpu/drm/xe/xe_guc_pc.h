/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_PC_H_
#define _XE_GUC_PC_H_

#include "xe_guc_pc_types.h"

int xe_guc_pc_init(struct xe_guc_pc *pc);
void xe_guc_pc_fini(struct xe_guc_pc *pc);
int xe_guc_pc_start(struct xe_guc_pc *pc);
int xe_guc_pc_stop(struct xe_guc_pc *pc);
int xe_guc_pc_gucrc_disable(struct xe_guc_pc *pc);

enum xe_gt_idle_state xe_guc_pc_c_status(struct xe_guc_pc *pc);
u64 xe_guc_pc_rc6_residency(struct xe_guc_pc *pc);
u64 xe_guc_pc_mc6_residency(struct xe_guc_pc *pc);
#endif /* _XE_GUC_PC_H_ */

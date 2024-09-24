/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_PC_H_
#define _XE_GUC_PC_H_

#include <linux/types.h>

struct xe_guc_pc;
enum slpc_gucrc_mode;

int xe_guc_pc_init(struct xe_guc_pc *pc);
int xe_guc_pc_start(struct xe_guc_pc *pc);
int xe_guc_pc_stop(struct xe_guc_pc *pc);
int xe_guc_pc_gucrc_disable(struct xe_guc_pc *pc);
int xe_guc_pc_override_gucrc_mode(struct xe_guc_pc *pc, enum slpc_gucrc_mode mode);
int xe_guc_pc_unset_gucrc_mode(struct xe_guc_pc *pc);

u32 xe_guc_pc_get_act_freq(struct xe_guc_pc *pc);
int xe_guc_pc_get_cur_freq(struct xe_guc_pc *pc, u32 *freq);
u32 xe_guc_pc_get_rp0_freq(struct xe_guc_pc *pc);
u32 xe_guc_pc_get_rpe_freq(struct xe_guc_pc *pc);
u32 xe_guc_pc_get_rpn_freq(struct xe_guc_pc *pc);
int xe_guc_pc_get_min_freq(struct xe_guc_pc *pc, u32 *freq);
int xe_guc_pc_set_min_freq(struct xe_guc_pc *pc, u32 freq);
int xe_guc_pc_get_max_freq(struct xe_guc_pc *pc, u32 *freq);
int xe_guc_pc_set_max_freq(struct xe_guc_pc *pc, u32 freq);

enum xe_gt_idle_state xe_guc_pc_c_status(struct xe_guc_pc *pc);
u64 xe_guc_pc_rc6_residency(struct xe_guc_pc *pc);
u64 xe_guc_pc_mc6_residency(struct xe_guc_pc *pc);
void xe_guc_pc_init_early(struct xe_guc_pc *pc);
int xe_guc_pc_restore_stashed_freq(struct xe_guc_pc *pc);
void xe_guc_pc_raise_unslice(struct xe_guc_pc *pc);

#endif /* _XE_GUC_PC_H_ */

/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_GUC_ENGINE_ACTIVITY_H_
#define _XE_GUC_ENGINE_ACTIVITY_H_

#include <linux/types.h>

struct xe_hw_engine;
struct xe_guc;

int xe_guc_engine_activity_init(struct xe_guc *guc);
bool xe_guc_engine_activity_supported(struct xe_guc *guc);
void xe_guc_engine_activity_enable_stats(struct xe_guc *guc);
u64 xe_guc_engine_activity_active_ticks(struct xe_guc *guc, struct xe_hw_engine *hwe);
u64 xe_guc_engine_activity_total_ticks(struct xe_guc *guc, struct xe_hw_engine *hwe);
#endif

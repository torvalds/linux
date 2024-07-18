/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_SUBMIT_H_
#define _XE_GUC_SUBMIT_H_

#include <linux/types.h>

struct drm_printer;
struct xe_exec_queue;
struct xe_guc;

int xe_guc_submit_init(struct xe_guc *guc, unsigned int num_ids);

int xe_guc_submit_reset_prepare(struct xe_guc *guc);
void xe_guc_submit_reset_wait(struct xe_guc *guc);
void xe_guc_submit_stop(struct xe_guc *guc);
int xe_guc_submit_start(struct xe_guc *guc);

int xe_guc_sched_done_handler(struct xe_guc *guc, u32 *msg, u32 len);
int xe_guc_deregister_done_handler(struct xe_guc *guc, u32 *msg, u32 len);
int xe_guc_exec_queue_reset_handler(struct xe_guc *guc, u32 *msg, u32 len);
int xe_guc_exec_queue_memory_cat_error_handler(struct xe_guc *guc, u32 *msg,
					       u32 len);
int xe_guc_exec_queue_reset_failure_handler(struct xe_guc *guc, u32 *msg, u32 len);

struct xe_guc_submit_exec_queue_snapshot *
xe_guc_exec_queue_snapshot_capture(struct xe_exec_queue *q);
void
xe_guc_exec_queue_snapshot_capture_delayed(struct xe_guc_submit_exec_queue_snapshot *snapshot);
void
xe_guc_exec_queue_snapshot_print(struct xe_guc_submit_exec_queue_snapshot *snapshot,
				 struct drm_printer *p);
void
xe_guc_exec_queue_snapshot_free(struct xe_guc_submit_exec_queue_snapshot *snapshot);
void xe_guc_submit_print(struct xe_guc *guc, struct drm_printer *p);

#endif

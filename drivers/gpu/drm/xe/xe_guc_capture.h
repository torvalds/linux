/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021-2024 Intel Corporation
 */

#ifndef _XE_GUC_CAPTURE_H
#define _XE_GUC_CAPTURE_H

#include <linux/types.h>
#include "abi/guc_capture_abi.h"
#include "xe_guc.h"
#include "xe_guc_fwif.h"

struct xe_guc;
struct xe_hw_engine;
struct xe_hw_engine_snapshot;
struct xe_sched_job;

static inline enum guc_capture_list_class_type xe_guc_class_to_capture_class(u16 class)
{
	switch (class) {
	case GUC_RENDER_CLASS:
	case GUC_COMPUTE_CLASS:
		return GUC_CAPTURE_LIST_CLASS_RENDER_COMPUTE;
	case GUC_GSC_OTHER_CLASS:
		return GUC_CAPTURE_LIST_CLASS_GSC_OTHER;
	case GUC_VIDEO_CLASS:
	case GUC_VIDEOENHANCE_CLASS:
	case GUC_BLITTER_CLASS:
		return class;
	default:
		XE_WARN_ON(class);
		return GUC_CAPTURE_LIST_CLASS_MAX;
	}
}

static inline enum guc_capture_list_class_type
xe_engine_class_to_guc_capture_class(enum xe_engine_class class)
{
	return xe_guc_class_to_capture_class(xe_engine_class_to_guc_class(class));
}

void xe_guc_capture_process(struct xe_guc *guc);
int xe_guc_capture_getlist(struct xe_guc *guc, u32 owner, u32 type,
			   enum guc_capture_list_class_type capture_class, void **outptr);
int xe_guc_capture_getlistsize(struct xe_guc *guc, u32 owner, u32 type,
			       enum guc_capture_list_class_type capture_class, size_t *size);
int xe_guc_capture_getnullheader(struct xe_guc *guc, void **outptr, size_t *size);
size_t xe_guc_capture_ads_input_worst_size(struct xe_guc *guc);
const struct __guc_mmio_reg_descr_group *
xe_guc_capture_get_reg_desc_list(struct xe_gt *gt, u32 owner, u32 type,
				 enum guc_capture_list_class_type capture_class, bool is_ext);
struct __guc_capture_parsed_output *xe_guc_capture_get_matching_and_lock(struct xe_sched_job *job);
void xe_engine_manual_capture(struct xe_hw_engine *hwe, struct xe_hw_engine_snapshot *snapshot);
void xe_engine_snapshot_print(struct xe_hw_engine_snapshot *snapshot, struct drm_printer *p);
void xe_engine_snapshot_capture_for_job(struct xe_sched_job *job);
void xe_guc_capture_steered_list_init(struct xe_guc *guc);
void xe_guc_capture_put_matched_nodes(struct xe_guc *guc);
int xe_guc_capture_init(struct xe_guc *guc);

#endif

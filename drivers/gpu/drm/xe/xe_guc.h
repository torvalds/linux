/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_H_
#define _XE_GUC_H_

#include "xe_gt.h"
#include "xe_guc_types.h"
#include "xe_hw_engine_types.h"
#include "xe_macros.h"

struct drm_printer;

void xe_guc_comm_init_early(struct xe_guc *guc);
int xe_guc_init(struct xe_guc *guc);
int xe_guc_init_post_hwconfig(struct xe_guc *guc);
int xe_guc_post_load_init(struct xe_guc *guc);
int xe_guc_reset(struct xe_guc *guc);
int xe_guc_upload(struct xe_guc *guc);
int xe_guc_min_load_for_hwconfig(struct xe_guc *guc);
int xe_guc_enable_communication(struct xe_guc *guc);
int xe_guc_suspend(struct xe_guc *guc);
void xe_guc_notify(struct xe_guc *guc);
int xe_guc_auth_huc(struct xe_guc *guc, u32 rsa_addr);
int xe_guc_mmio_send(struct xe_guc *guc, const u32 *request, u32 len);
int xe_guc_mmio_send_recv(struct xe_guc *guc, const u32 *request, u32 len,
			  u32 *response_buf);
int xe_guc_self_cfg32(struct xe_guc *guc, u16 key, u32 val);
int xe_guc_self_cfg64(struct xe_guc *guc, u16 key, u64 val);
void xe_guc_irq_handler(struct xe_guc *guc, const u16 iir);
void xe_guc_sanitize(struct xe_guc *guc);
void xe_guc_print_info(struct xe_guc *guc, struct drm_printer *p);
int xe_guc_reset_prepare(struct xe_guc *guc);
void xe_guc_reset_wait(struct xe_guc *guc);
void xe_guc_stop_prepare(struct xe_guc *guc);
int xe_guc_stop(struct xe_guc *guc);
int xe_guc_start(struct xe_guc *guc);
bool xe_guc_in_reset(struct xe_guc *guc);

static inline u16 xe_engine_class_to_guc_class(enum xe_engine_class class)
{
	switch (class) {
	case XE_ENGINE_CLASS_RENDER:
		return GUC_RENDER_CLASS;
	case XE_ENGINE_CLASS_VIDEO_DECODE:
		return GUC_VIDEO_CLASS;
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
		return GUC_VIDEOENHANCE_CLASS;
	case XE_ENGINE_CLASS_COPY:
		return GUC_BLITTER_CLASS;
	case XE_ENGINE_CLASS_COMPUTE:
		return GUC_COMPUTE_CLASS;
	case XE_ENGINE_CLASS_OTHER:
		return GUC_GSC_OTHER_CLASS;
	default:
		XE_WARN_ON(class);
		return -1;
	}
}

static inline struct xe_gt *guc_to_gt(struct xe_guc *guc)
{
	return container_of(guc, struct xe_gt, uc.guc);
}

static inline struct xe_device *guc_to_xe(struct xe_guc *guc)
{
	return gt_to_xe(guc_to_gt(guc));
}

#endif

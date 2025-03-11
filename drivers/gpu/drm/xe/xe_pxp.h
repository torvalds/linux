/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2024, Intel Corporation. All rights reserved.
 */

#ifndef __XE_PXP_H__
#define __XE_PXP_H__

#include <linux/types.h>

struct drm_gem_object;
struct xe_bo;
struct xe_device;
struct xe_exec_queue;
struct xe_pxp;

bool xe_pxp_is_supported(const struct xe_device *xe);
bool xe_pxp_is_enabled(const struct xe_pxp *pxp);
int xe_pxp_get_readiness_status(struct xe_pxp *pxp);

int xe_pxp_init(struct xe_device *xe);
void xe_pxp_irq_handler(struct xe_device *xe, u16 iir);

int xe_pxp_pm_suspend(struct xe_pxp *pxp);
void xe_pxp_pm_resume(struct xe_pxp *pxp);

int xe_pxp_exec_queue_set_type(struct xe_pxp *pxp, struct xe_exec_queue *q, u8 type);
int xe_pxp_exec_queue_add(struct xe_pxp *pxp, struct xe_exec_queue *q);
void xe_pxp_exec_queue_remove(struct xe_pxp *pxp, struct xe_exec_queue *q);

int xe_pxp_key_assign(struct xe_pxp *pxp, struct xe_bo *bo);
int xe_pxp_bo_key_check(struct xe_pxp *pxp, struct xe_bo *bo);
int xe_pxp_obj_key_check(struct drm_gem_object *obj);

#endif /* __XE_PXP_H__ */

/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2024, Intel Corporation. All rights reserved.
 */

#ifndef __XE_PXP_H__
#define __XE_PXP_H__

#include <linux/types.h>

struct xe_device;
struct xe_exec_queue;
struct xe_pxp;

#define DRM_XE_PXP_HWDRM_DEFAULT_SESSION 0xF /* TODO: move to uapi */

bool xe_pxp_is_supported(const struct xe_device *xe);

int xe_pxp_init(struct xe_device *xe);
void xe_pxp_irq_handler(struct xe_device *xe, u16 iir);

int xe_pxp_exec_queue_add(struct xe_pxp *pxp, struct xe_exec_queue *q);
void xe_pxp_exec_queue_remove(struct xe_pxp *pxp, struct xe_exec_queue *q);

#endif /* __XE_PXP_H__ */

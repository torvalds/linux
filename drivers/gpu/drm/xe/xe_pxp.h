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

bool xe_pxp_is_supported(const struct xe_device *xe);
bool xe_pxp_is_enabled(const struct xe_pxp *pxp);
int xe_pxp_get_readiness_status(struct xe_pxp *pxp);

int xe_pxp_init(struct xe_device *xe);
void xe_pxp_irq_handler(struct xe_device *xe, u16 iir);

int xe_pxp_exec_queue_set_type(struct xe_pxp *pxp, struct xe_exec_queue *q, u8 type);
int xe_pxp_exec_queue_add(struct xe_pxp *pxp, struct xe_exec_queue *q);
void xe_pxp_exec_queue_remove(struct xe_pxp *pxp, struct xe_exec_queue *q);

#endif /* __XE_PXP_H__ */

/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2024, Intel Corporation. All rights reserved.
 */

#ifndef __XE_PXP_SUBMIT_H__
#define __XE_PXP_SUBMIT_H__

#include <linux/types.h>

struct xe_pxp;

int xe_pxp_allocate_execution_resources(struct xe_pxp *pxp);
void xe_pxp_destroy_execution_resources(struct xe_pxp *pxp);

int xe_pxp_submit_session_termination(struct xe_pxp *pxp, u32 id);

#endif /* __XE_PXP_SUBMIT_H__ */

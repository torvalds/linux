/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2024, Intel Corporation. All rights reserved.
 */

#ifndef __XE_PXP_SUBMIT_H__
#define __XE_PXP_SUBMIT_H__

struct xe_pxp;

int xe_pxp_allocate_execution_resources(struct xe_pxp *pxp);
void xe_pxp_destroy_execution_resources(struct xe_pxp *pxp);

#endif /* __XE_PXP_SUBMIT_H__ */

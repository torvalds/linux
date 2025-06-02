/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2024, Intel Corporation. All rights reserved.
 */

#ifndef __XE_PXP_SUBMIT_H__
#define __XE_PXP_SUBMIT_H__

#include <linux/types.h>

struct xe_pxp;
struct xe_pxp_gsc_client_resources;

int xe_pxp_allocate_execution_resources(struct xe_pxp *pxp);
void xe_pxp_destroy_execution_resources(struct xe_pxp *pxp);

int xe_pxp_submit_session_init(struct xe_pxp_gsc_client_resources *gsc_res, u32 id);
int xe_pxp_submit_session_termination(struct xe_pxp *pxp, u32 id);
int xe_pxp_submit_session_invalidation(struct xe_pxp_gsc_client_resources *gsc_res,
				       u32 id);

#endif /* __XE_PXP_SUBMIT_H__ */

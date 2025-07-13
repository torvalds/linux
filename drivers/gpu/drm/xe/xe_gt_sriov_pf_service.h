/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_SERVICE_H_
#define _XE_GT_SRIOV_PF_SERVICE_H_

#include <linux/errno.h>
#include <linux/types.h>

struct drm_printer;
struct xe_gt;

int xe_gt_sriov_pf_service_init(struct xe_gt *gt);
void xe_gt_sriov_pf_service_update(struct xe_gt *gt);

int xe_gt_sriov_pf_service_print_runtime(struct xe_gt *gt, struct drm_printer *p);

#ifdef CONFIG_PCI_IOV
int xe_gt_sriov_pf_service_process_request(struct xe_gt *gt, u32 origin,
					   const u32 *msg, u32 msg_len,
					   u32 *response, u32 resp_size);
#else
static inline int
xe_gt_sriov_pf_service_process_request(struct xe_gt *gt, u32 origin,
				       const u32 *msg, u32 msg_len,
				       u32 *response, u32 resp_size)
{
	return -EPROTO;
}
#endif

#endif

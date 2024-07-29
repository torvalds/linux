/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_MONITOR_H_
#define _XE_GT_SRIOV_PF_MONITOR_H_

#include <linux/errno.h>
#include <linux/types.h>

struct drm_printer;
struct xe_gt;

void xe_gt_sriov_pf_monitor_flr(struct xe_gt *gt, u32 vfid);
void xe_gt_sriov_pf_monitor_print_events(struct xe_gt *gt, struct drm_printer *p);

#ifdef CONFIG_PCI_IOV
int xe_gt_sriov_pf_monitor_process_guc2pf(struct xe_gt *gt, const u32 *msg, u32 len);
#else
static inline int xe_gt_sriov_pf_monitor_process_guc2pf(struct xe_gt *gt, const u32 *msg, u32 len)
{
	return -EPROTO;
}
#endif

#endif

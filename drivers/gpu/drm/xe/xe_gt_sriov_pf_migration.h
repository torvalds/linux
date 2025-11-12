/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_MIGRATION_H_
#define _XE_GT_SRIOV_PF_MIGRATION_H_

#include <linux/types.h>

struct xe_gt;
struct xe_sriov_packet;

/* TODO: get this information by querying GuC in the future */
#define XE_GT_SRIOV_PF_MIGRATION_GUC_DATA_MAX_SIZE SZ_8M

int xe_gt_sriov_pf_migration_init(struct xe_gt *gt);
int xe_gt_sriov_pf_migration_save_guc_state(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_migration_restore_guc_state(struct xe_gt *gt, unsigned int vfid);

ssize_t xe_gt_sriov_pf_migration_size(struct xe_gt *gt, unsigned int vfid);

bool xe_gt_sriov_pf_migration_ring_empty(struct xe_gt *gt, unsigned int vfid);
bool xe_gt_sriov_pf_migration_ring_full(struct xe_gt *gt, unsigned int vfid);
void xe_gt_sriov_pf_migration_ring_free(struct xe_gt *gt, unsigned int vfid);

int xe_gt_sriov_pf_migration_save_produce(struct xe_gt *gt, unsigned int vfid,
					  struct xe_sriov_packet *data);
struct xe_sriov_packet *
xe_gt_sriov_pf_migration_restore_consume(struct xe_gt *gt, unsigned int vfid);

int xe_gt_sriov_pf_migration_restore_produce(struct xe_gt *gt, unsigned int vfid,
					     struct xe_sriov_packet *data);
struct xe_sriov_packet *
xe_gt_sriov_pf_migration_save_consume(struct xe_gt *gt, unsigned int vfid);

#ifdef CONFIG_DEBUG_FS
ssize_t xe_gt_sriov_pf_migration_read_guc_state(struct xe_gt *gt, unsigned int vfid,
						char __user *buf, size_t count, loff_t *pos);
ssize_t xe_gt_sriov_pf_migration_write_guc_state(struct xe_gt *gt, unsigned int vfid,
						 const char __user *buf, size_t count);
#endif

#endif

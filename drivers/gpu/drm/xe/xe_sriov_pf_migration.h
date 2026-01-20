/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_MIGRATION_H_
#define _XE_SRIOV_PF_MIGRATION_H_

#include <linux/types.h>
#include <linux/wait.h>

struct xe_device;
struct xe_sriov_packet;

int xe_sriov_pf_migration_init(struct xe_device *xe);
bool xe_sriov_pf_migration_supported(struct xe_device *xe);
void xe_sriov_pf_migration_disable(struct xe_device *xe, const char *fmt, ...);
int xe_sriov_pf_migration_restore_produce(struct xe_device *xe, unsigned int vfid,
					  struct xe_sriov_packet *data);
struct xe_sriov_packet *
xe_sriov_pf_migration_save_consume(struct xe_device *xe, unsigned int vfid);
ssize_t xe_sriov_pf_migration_size(struct xe_device *xe, unsigned int vfid);
wait_queue_head_t *xe_sriov_pf_migration_waitqueue(struct xe_device *xe, unsigned int vfid);

ssize_t xe_sriov_pf_migration_read(struct xe_device *xe, unsigned int vfid,
				   char __user *buf, size_t len);
ssize_t xe_sriov_pf_migration_write(struct xe_device *xe, unsigned int vfid,
				    const char __user *buf, size_t len);

#endif

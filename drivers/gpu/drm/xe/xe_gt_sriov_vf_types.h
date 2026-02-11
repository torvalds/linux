/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_VF_TYPES_H_
#define _XE_GT_SRIOV_VF_TYPES_H_

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include "xe_uc_fw_types.h"

/**
 * struct xe_gt_sriov_vf_selfconfig - VF configuration data.
 */
struct xe_gt_sriov_vf_selfconfig {
	/** @num_ctxs: assigned number of GuC submission context IDs. */
	u16 num_ctxs;
	/** @num_dbs: assigned number of GuC doorbells IDs. */
	u16 num_dbs;
};

/**
 * struct xe_gt_sriov_vf_runtime - VF runtime data.
 */
struct xe_gt_sriov_vf_runtime {
	/** @gmdid: cached value of the GDMID register. */
	u32 gmdid;
	/** @uses_sched_groups: whether PF enabled sched groups or not. */
	bool uses_sched_groups;
	/** @regs_size: size of runtime register array. */
	u32 regs_size;
	/** @num_regs: number of runtime registers in the array. */
	u32 num_regs;
	/** @regs: pointer to array of register offset/value pairs. */
	struct vf_runtime_reg {
		/** @regs.offset: register offset. */
		u32 offset;
		/** @regs.value: register value. */
		u32 value;
	} *regs;
};

/**
 * struct xe_gt_sriov_vf_migration - VF migration data.
 */
struct xe_gt_sriov_vf_migration {
	/** @worker: VF migration recovery worker */
	struct work_struct worker;
	/** @lock: Protects recovery_queued, teardown */
	spinlock_t lock;
	/** @wq: wait queue for migration fixes */
	wait_queue_head_t wq;
	/** @scratch: Scratch memory for VF recovery */
	void *scratch;
	/** @debug: Debug hooks for delaying migration */
	struct {
		/**
		 * @debug.resfix_stoppers: Stop and wait at different stages
		 * during post migration recovery
		 */
		u8 resfix_stoppers;
	} debug;
	/**
	 * @resfix_marker: Marker sent on start and on end of post-migration
	 * steps.
	 */
	u8 resfix_marker;
	/** @recovery_teardown: VF post migration recovery is being torn down */
	bool recovery_teardown;
	/** @recovery_queued: VF post migration recovery in queued */
	bool recovery_queued;
	/** @recovery_inprogress: VF post migration recovery in progress */
	bool recovery_inprogress;
	/** @ggtt_need_fixes: VF GGTT needs fixes */
	bool ggtt_need_fixes;
};

/**
 * struct xe_gt_sriov_vf - GT level VF virtualization data.
 */
struct xe_gt_sriov_vf {
	/** @wanted_guc_version: minimum wanted GuC ABI version. */
	struct xe_uc_fw_version wanted_guc_version;
	/** @guc_version: negotiated GuC ABI version. */
	struct xe_uc_fw_version guc_version;
	/** @self_config: resource configurations. */
	struct xe_gt_sriov_vf_selfconfig self_config;
	/** @runtime: runtime data retrieved from the PF. */
	struct xe_gt_sriov_vf_runtime runtime;
	/** @migration: migration data for the VF. */
	struct xe_gt_sriov_vf_migration migration;
};

#endif

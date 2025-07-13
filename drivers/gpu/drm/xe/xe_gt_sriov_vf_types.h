/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_VF_TYPES_H_
#define _XE_GT_SRIOV_VF_TYPES_H_

#include <linux/types.h>
#include "xe_uc_fw_types.h"

/**
 * struct xe_gt_sriov_vf_selfconfig - VF configuration data.
 */
struct xe_gt_sriov_vf_selfconfig {
	/** @ggtt_base: assigned base offset of the GGTT region. */
	u64 ggtt_base;
	/** @ggtt_size: assigned size of the GGTT region. */
	u64 ggtt_size;
	/** @ggtt_shift: difference in ggtt_base on last migration */
	s64 ggtt_shift;
	/** @lmem_size: assigned size of the LMEM. */
	u64 lmem_size;
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
};

#endif

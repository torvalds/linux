/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_PC_TYPES_H_
#define _XE_GUC_PC_TYPES_H_

#include <linux/mutex.h>
#include <linux/types.h>

/**
 * struct xe_guc_pc - GuC Power Conservation (PC)
 */
struct xe_guc_pc {
	/** @bo: GGTT buffer object that is shared with GuC PC */
	struct xe_bo *bo;
	/** @rp0_freq: HW RP0 frequency - The Maximum one */
	u32 rp0_freq;
	/** @rpa_freq: HW RPa frequency - The Achievable one */
	u32 rpa_freq;
	/** @rpe_freq: HW RPe frequency - The Efficient one */
	u32 rpe_freq;
	/** @rpn_freq: HW RPN frequency - The Minimum one */
	u32 rpn_freq;
	/** @user_requested_min: Stash the minimum requested freq by user */
	u32 user_requested_min;
	/** @user_requested_max: Stash the maximum requested freq by user */
	u32 user_requested_max;
	/** @stashed_min_freq: Stash the current minimum freq */
	u32 stashed_min_freq;
	/** @stashed_max_freq: Stash the current maximum freq */
	u32 stashed_max_freq;
	/** @freq_lock: Let's protect the frequencies */
	struct mutex freq_lock;
	/** @freq_ready: Only handle freq changes, if they are really ready */
	bool freq_ready;
};

#endif	/* _XE_GUC_PC_TYPES_H_ */

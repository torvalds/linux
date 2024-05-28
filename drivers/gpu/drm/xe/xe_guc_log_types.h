/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_LOG_TYPES_H_
#define _XE_GUC_LOG_TYPES_H_

#include <linux/types.h>

struct xe_bo;

/**
 * struct xe_guc_log - GuC log
 */
struct xe_guc_log {
	/** @level: GuC log level */
	u32 level;
	/** @bo: XE BO for GuC log */
	struct xe_bo *bo;
};

#endif

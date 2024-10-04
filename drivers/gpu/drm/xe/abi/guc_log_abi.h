/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _ABI_GUC_LOG_ABI_H
#define _ABI_GUC_LOG_ABI_H

#include <linux/types.h>

/* GuC logging buffer types */
enum guc_log_buffer_type {
	GUC_LOG_BUFFER_CRASH_DUMP,
	GUC_LOG_BUFFER_DEBUG,
	GUC_LOG_BUFFER_CAPTURE,
};

#define GUC_LOG_BUFFER_TYPE_MAX		3

#endif

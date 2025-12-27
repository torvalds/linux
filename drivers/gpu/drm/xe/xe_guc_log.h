/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_LOG_H_
#define _XE_GUC_LOG_H_

#include "xe_guc_log_types.h"
#include "abi/guc_log_abi.h"

struct drm_printer;
struct xe_device;

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_GUC)
#define XE_GUC_LOG_EVENT_DATA_BUFFER_SIZE	SZ_8M
#define XE_GUC_LOG_CRASH_DUMP_BUFFER_SIZE	SZ_1M
#define XE_GUC_LOG_STATE_CAPTURE_BUFFER_SIZE	SZ_2M
#else
#define XE_GUC_LOG_EVENT_DATA_BUFFER_SIZE	SZ_64K
#define XE_GUC_LOG_CRASH_DUMP_BUFFER_SIZE	SZ_16K
#define XE_GUC_LOG_STATE_CAPTURE_BUFFER_SIZE	SZ_1M
#endif

#define GUC_LOG_SIZE (SZ_4K + \
		      XE_GUC_LOG_EVENT_DATA_BUFFER_SIZE + \
		      XE_GUC_LOG_CRASH_DUMP_BUFFER_SIZE + \
		      XE_GUC_LOG_STATE_CAPTURE_BUFFER_SIZE)

#define XE_GUC_LOG_EVENT_DATA_OFFSET	SZ_4K
#define XE_GUC_LOG_CRASH_DUMP_OFFSET	(XE_GUC_LOG_EVENT_DATA_OFFSET + \
					 XE_GUC_LOG_EVENT_DATA_BUFFER_SIZE)
#define XE_GUC_LOG_STATE_CAPTURE_OFFSET	(XE_GUC_LOG_CRASH_DUMP_OFFSET + \
					 XE_GUC_LOG_CRASH_DUMP_BUFFER_SIZE)

/*
 * While we're using plain log level in i915, GuC controls are much more...
 * "elaborate"? We have a couple of bits for verbosity, separate bit for actual
 * log enabling, and separate bit for default logging - which "conveniently"
 * ignores the enable bit.
 */
#define GUC_LOG_LEVEL_DISABLED		0
#define GUC_LOG_LEVEL_NON_VERBOSE	1
#define GUC_LOG_LEVEL_IS_ENABLED(x)	((x) > GUC_LOG_LEVEL_DISABLED)
#define GUC_LOG_LEVEL_IS_VERBOSE(x)	((x) > GUC_LOG_LEVEL_NON_VERBOSE)
#define GUC_LOG_LEVEL_TO_VERBOSITY(x) ({		\
	typeof(x) _x = (x);				\
	GUC_LOG_LEVEL_IS_VERBOSE(_x) ? _x - 2 : 0;	\
})
#define GUC_VERBOSITY_TO_LOG_LEVEL(x)	((x) + 2)
#define GUC_LOG_LEVEL_MAX GUC_VERBOSITY_TO_LOG_LEVEL(GUC_LOG_VERBOSITY_MAX)

int xe_guc_log_init(struct xe_guc_log *log);
void xe_guc_log_print(struct xe_guc_log *log, struct drm_printer *p);
void xe_guc_log_print_lfd(struct xe_guc_log *log, struct drm_printer *p);
void xe_guc_log_print_dmesg(struct xe_guc_log *log);
struct xe_guc_log_snapshot *xe_guc_log_snapshot_capture(struct xe_guc_log *log, bool atomic);
void xe_guc_log_snapshot_print(struct xe_guc_log_snapshot *snapshot, struct drm_printer *p);
void xe_guc_log_snapshot_free(struct xe_guc_log_snapshot *snapshot);

static inline u32
xe_guc_log_get_level(struct xe_guc_log *log)
{
	return log->level;
}

bool xe_guc_check_log_buf_overflow(struct xe_guc_log *log,
				   enum guc_log_type type,
				   unsigned int full_cnt);

#endif

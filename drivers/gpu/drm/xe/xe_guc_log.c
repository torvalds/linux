// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_guc_log.h"

#include <drm/drm_managed.h>
#include <linux/vmalloc.h>

#include "xe_bo.h"
#include "xe_devcoredump.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_map.h"
#include "xe_module.h"

static struct xe_gt *
log_to_gt(struct xe_guc_log *log)
{
	return container_of(log, struct xe_gt, uc.guc.log);
}

static struct xe_device *
log_to_xe(struct xe_guc_log *log)
{
	return gt_to_xe(log_to_gt(log));
}

static size_t guc_log_size(void)
{
	/*
	 *  GuC Log buffer Layout
	 *
	 *  +===============================+ 00B
	 *  |    Crash dump state header    |
	 *  +-------------------------------+ 32B
	 *  |      Debug state header       |
	 *  +-------------------------------+ 64B
	 *  |     Capture state header      |
	 *  +-------------------------------+ 96B
	 *  |                               |
	 *  +===============================+ PAGE_SIZE (4KB)
	 *  |        Crash Dump logs        |
	 *  +===============================+ + CRASH_SIZE
	 *  |          Debug logs           |
	 *  +===============================+ + DEBUG_SIZE
	 *  |         Capture logs          |
	 *  +===============================+ + CAPTURE_SIZE
	 */
	return PAGE_SIZE + CRASH_BUFFER_SIZE + DEBUG_BUFFER_SIZE +
		CAPTURE_BUFFER_SIZE;
}

/**
 * xe_guc_log_print - dump a copy of the GuC log to some useful location
 * @log: GuC log structure
 * @p: the printer object to output to
 */
void xe_guc_log_print(struct xe_guc_log *log, struct drm_printer *p)
{
	struct xe_device *xe = log_to_xe(log);
	size_t size;
	void *copy;

	if (!log->bo) {
		drm_puts(p, "GuC log buffer not allocated");
		return;
	}

	size = log->bo->size;

	copy = vmalloc(size);
	if (!copy) {
		drm_printf(p, "Failed to allocate %zu", size);
		return;
	}

	xe_map_memcpy_from(xe, copy, &log->bo->vmap, 0, size);

	xe_print_blob_ascii85(p, "Log data", copy, 0, size);

	vfree(copy);
}

int xe_guc_log_init(struct xe_guc_log *log)
{
	struct xe_device *xe = log_to_xe(log);
	struct xe_tile *tile = gt_to_tile(log_to_gt(log));
	struct xe_bo *bo;

	bo = xe_managed_bo_create_pin_map(xe, tile, guc_log_size(),
					  XE_BO_FLAG_SYSTEM |
					  XE_BO_FLAG_GGTT |
					  XE_BO_FLAG_GGTT_INVALIDATE);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	xe_map_memset(xe, &bo->vmap, 0, 0, guc_log_size());
	log->bo = bo;
	log->level = xe_modparam.guc_log_level;

	return 0;
}

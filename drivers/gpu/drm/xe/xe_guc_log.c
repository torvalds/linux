// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_guc_log.h"

#include <drm/drm_managed.h>

#include "xe_bo.h"
#include "xe_gt.h"
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

void xe_guc_log_print(struct xe_guc_log *log, struct drm_printer *p)
{
	struct xe_device *xe = log_to_xe(log);
	size_t size;
	int i, j;

	xe_assert(xe, log->bo);

	size = log->bo->size;

#define DW_PER_READ		128
	xe_assert(xe, !(size % (DW_PER_READ * sizeof(u32))));
	for (i = 0; i < size / sizeof(u32); i += DW_PER_READ) {
		u32 read[DW_PER_READ];

		xe_map_memcpy_from(xe, read, &log->bo->vmap, i * sizeof(u32),
				   DW_PER_READ * sizeof(u32));
#define DW_PER_PRINT		4
		for (j = 0; j < DW_PER_READ / DW_PER_PRINT; ++j) {
			u32 *print = read + j * DW_PER_PRINT;

			drm_printf(p, "0x%08x 0x%08x 0x%08x 0x%08x\n",
				   *(print + 0), *(print + 1),
				   *(print + 2), *(print + 3));
		}
	}
}

int xe_guc_log_init(struct xe_guc_log *log)
{
	struct xe_device *xe = log_to_xe(log);
	struct xe_tile *tile = gt_to_tile(log_to_gt(log));
	struct xe_bo *bo;

	bo = xe_managed_bo_create_pin_map(xe, tile, guc_log_size(),
					  XE_BO_CREATE_VRAM_IF_DGFX(tile) |
					  XE_BO_CREATE_GGTT_BIT);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	xe_map_memset(xe, &bo->vmap, 0, 0, guc_log_size());
	log->bo = bo;
	log->level = xe_modparam.guc_log_level;

	return 0;
}

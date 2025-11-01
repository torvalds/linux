// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_guc_log.h"

#include <linux/fault-inject.h>

#include <drm/drm_managed.h>

#include "regs/xe_guc_regs.h"
#include "xe_bo.h"
#include "xe_devcoredump.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_module.h"

static struct xe_guc *
log_to_guc(struct xe_guc_log *log)
{
	return container_of(log, struct xe_guc, log);
}

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

#define GUC_LOG_CHUNK_SIZE	SZ_2M

static struct xe_guc_log_snapshot *xe_guc_log_snapshot_alloc(struct xe_guc_log *log, bool atomic)
{
	struct xe_guc_log_snapshot *snapshot;
	size_t remain;
	int i;

	snapshot = kzalloc(sizeof(*snapshot), atomic ? GFP_ATOMIC : GFP_KERNEL);
	if (!snapshot)
		return NULL;

	/*
	 * NB: kmalloc has a hard limit well below the maximum GuC log buffer size.
	 * Also, can't use vmalloc as might be called from atomic context. So need
	 * to break the buffer up into smaller chunks that can be allocated.
	 */
	snapshot->size = xe_bo_size(log->bo);
	snapshot->num_chunks = DIV_ROUND_UP(snapshot->size, GUC_LOG_CHUNK_SIZE);

	snapshot->copy = kcalloc(snapshot->num_chunks, sizeof(*snapshot->copy),
				 atomic ? GFP_ATOMIC : GFP_KERNEL);
	if (!snapshot->copy)
		goto fail_snap;

	remain = snapshot->size;
	for (i = 0; i < snapshot->num_chunks; i++) {
		size_t size = min(GUC_LOG_CHUNK_SIZE, remain);

		snapshot->copy[i] = kmalloc(size, atomic ? GFP_ATOMIC : GFP_KERNEL);
		if (!snapshot->copy[i])
			goto fail_copy;
		remain -= size;
	}

	return snapshot;

fail_copy:
	for (i = 0; i < snapshot->num_chunks; i++)
		kfree(snapshot->copy[i]);
	kfree(snapshot->copy);
fail_snap:
	kfree(snapshot);
	return NULL;
}

/**
 * xe_guc_log_snapshot_free - free a previously captured GuC log snapshot
 * @snapshot: GuC log snapshot structure
 *
 * Return: pointer to a newly allocated snapshot object or null if out of memory. Caller is
 * responsible for calling xe_guc_log_snapshot_free when done with the snapshot.
 */
void xe_guc_log_snapshot_free(struct xe_guc_log_snapshot *snapshot)
{
	int i;

	if (!snapshot)
		return;

	if (snapshot->copy) {
		for (i = 0; i < snapshot->num_chunks; i++)
			kfree(snapshot->copy[i]);
		kfree(snapshot->copy);
	}

	kfree(snapshot);
}

/**
 * xe_guc_log_snapshot_capture - create a new snapshot copy the GuC log for later dumping
 * @log: GuC log structure
 * @atomic: is the call inside an atomic section of some kind?
 *
 * Return: pointer to a newly allocated snapshot object or null if out of memory. Caller is
 * responsible for calling xe_guc_log_snapshot_free when done with the snapshot.
 */
struct xe_guc_log_snapshot *xe_guc_log_snapshot_capture(struct xe_guc_log *log, bool atomic)
{
	struct xe_guc_log_snapshot *snapshot;
	struct xe_device *xe = log_to_xe(log);
	struct xe_guc *guc = log_to_guc(log);
	struct xe_gt *gt = log_to_gt(log);
	unsigned int fw_ref;
	size_t remain;
	int i;

	if (!log->bo)
		return NULL;

	snapshot = xe_guc_log_snapshot_alloc(log, atomic);
	if (!snapshot)
		return NULL;

	remain = snapshot->size;
	for (i = 0; i < snapshot->num_chunks; i++) {
		size_t size = min(GUC_LOG_CHUNK_SIZE, remain);

		xe_map_memcpy_from(xe, snapshot->copy[i], &log->bo->vmap,
				   i * GUC_LOG_CHUNK_SIZE, size);
		remain -= size;
	}

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref) {
		snapshot->stamp = ~0ULL;
	} else {
		snapshot->stamp = xe_mmio_read64_2x32(&gt->mmio, GUC_PMTIMESTAMP_LO);
		xe_force_wake_put(gt_to_fw(gt), fw_ref);
	}
	snapshot->ktime = ktime_get_boottime_ns();
	snapshot->level = log->level;
	snapshot->ver_found = guc->fw.versions.found[XE_UC_FW_VER_RELEASE];
	snapshot->ver_want = guc->fw.versions.wanted;
	snapshot->path = guc->fw.path;

	return snapshot;
}

/**
 * xe_guc_log_snapshot_print - dump a previously saved copy of the GuC log to some useful location
 * @snapshot: a snapshot of the GuC log
 * @p: the printer object to output to
 */
void xe_guc_log_snapshot_print(struct xe_guc_log_snapshot *snapshot, struct drm_printer *p)
{
	size_t remain;
	int i;

	if (!snapshot) {
		drm_printf(p, "GuC log snapshot not allocated!\n");
		return;
	}

	drm_printf(p, "GuC firmware: %s\n", snapshot->path);
	drm_printf(p, "GuC version: %u.%u.%u (wanted %u.%u.%u)\n",
		   snapshot->ver_found.major, snapshot->ver_found.minor, snapshot->ver_found.patch,
		   snapshot->ver_want.major, snapshot->ver_want.minor, snapshot->ver_want.patch);
	drm_printf(p, "Kernel timestamp: 0x%08llX [%llu]\n", snapshot->ktime, snapshot->ktime);
	drm_printf(p, "GuC timestamp: 0x%08llX [%llu]\n", snapshot->stamp, snapshot->stamp);
	drm_printf(p, "Log level: %u\n", snapshot->level);

	drm_printf(p, "[LOG].length: 0x%zx\n", snapshot->size);
	remain = snapshot->size;
	for (i = 0; i < snapshot->num_chunks; i++) {
		size_t size = min(GUC_LOG_CHUNK_SIZE, remain);
		const char *prefix = i ? NULL : "[LOG].data";
		char suffix = i == snapshot->num_chunks - 1 ? '\n' : 0;

		xe_print_blob_ascii85(p, prefix, suffix, snapshot->copy[i], 0, size);
		remain -= size;
	}
}

/**
 * xe_guc_log_print_dmesg - dump a copy of the GuC log to dmesg
 * @log: GuC log structure
 */
void xe_guc_log_print_dmesg(struct xe_guc_log *log)
{
	struct xe_gt *gt = log_to_gt(log);
	static int g_count;
	struct drm_printer ip = xe_gt_info_printer(gt);
	struct drm_printer lp = drm_line_printer(&ip, "Capture", ++g_count);

	drm_printf(&lp, "Dumping GuC log for %ps...\n", __builtin_return_address(0));

	xe_guc_log_print(log, &lp);

	drm_printf(&lp, "Done.\n");
}

/**
 * xe_guc_log_print - dump a copy of the GuC log to some useful location
 * @log: GuC log structure
 * @p: the printer object to output to
 */
void xe_guc_log_print(struct xe_guc_log *log, struct drm_printer *p)
{
	struct xe_guc_log_snapshot *snapshot;

	drm_printf(p, "**** GuC Log ****\n");

	snapshot = xe_guc_log_snapshot_capture(log, false);
	drm_printf(p, "CS reference clock: %u\n", log_to_gt(log)->info.reference_clock);
	xe_guc_log_snapshot_print(snapshot, p);
	xe_guc_log_snapshot_free(snapshot);
}

int xe_guc_log_init(struct xe_guc_log *log)
{
	struct xe_device *xe = log_to_xe(log);
	struct xe_tile *tile = gt_to_tile(log_to_gt(log));
	struct xe_bo *bo;

	bo = xe_managed_bo_create_pin_map(xe, tile, guc_log_size(),
					  XE_BO_FLAG_SYSTEM |
					  XE_BO_FLAG_GGTT |
					  XE_BO_FLAG_GGTT_INVALIDATE |
					  XE_BO_FLAG_PINNED_NORESTORE);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	xe_map_memset(xe, &bo->vmap, 0, 0, guc_log_size());
	log->bo = bo;
	log->level = xe_modparam.guc_log_level;

	return 0;
}

ALLOW_ERROR_INJECTION(xe_guc_log_init, ERRNO); /* See xe_pci_probe() */

static u32 xe_guc_log_section_size_crash(struct xe_guc_log *log)
{
	return CRASH_BUFFER_SIZE;
}

static u32 xe_guc_log_section_size_debug(struct xe_guc_log *log)
{
	return DEBUG_BUFFER_SIZE;
}

/**
 * xe_guc_log_section_size_capture - Get capture buffer size within log sections.
 * @log: The log object.
 *
 * This function will return the capture buffer size within log sections.
 *
 * Return: capture buffer size.
 */
u32 xe_guc_log_section_size_capture(struct xe_guc_log *log)
{
	return CAPTURE_BUFFER_SIZE;
}

/**
 * xe_guc_get_log_buffer_size - Get log buffer size for a type.
 * @log: The log object.
 * @type: The log buffer type
 *
 * Return: buffer size.
 */
u32 xe_guc_get_log_buffer_size(struct xe_guc_log *log, enum guc_log_buffer_type type)
{
	switch (type) {
	case GUC_LOG_BUFFER_CRASH_DUMP:
		return xe_guc_log_section_size_crash(log);
	case GUC_LOG_BUFFER_DEBUG:
		return xe_guc_log_section_size_debug(log);
	case GUC_LOG_BUFFER_CAPTURE:
		return xe_guc_log_section_size_capture(log);
	}
	return 0;
}

/**
 * xe_guc_get_log_buffer_offset - Get offset in log buffer for a type.
 * @log: The log object.
 * @type: The log buffer type
 *
 * This function will return the offset in the log buffer for a type.
 * Return: buffer offset.
 */
u32 xe_guc_get_log_buffer_offset(struct xe_guc_log *log, enum guc_log_buffer_type type)
{
	enum guc_log_buffer_type i;
	u32 offset = PAGE_SIZE;/* for the log_buffer_states */

	for (i = GUC_LOG_BUFFER_CRASH_DUMP; i < GUC_LOG_BUFFER_TYPE_MAX; ++i) {
		if (i == type)
			break;
		offset += xe_guc_get_log_buffer_size(log, i);
	}

	return offset;
}

/**
 * xe_guc_check_log_buf_overflow - Check if log buffer overflowed
 * @log: The log object.
 * @type: The log buffer type
 * @full_cnt: The count of buffer full
 *
 * This function will check count of buffer full against previous, mismatch
 * indicate overflowed.
 * Update the sampled_overflow counter, if the 4 bit counter overflowed, add
 * up 16 to correct the value.
 *
 * Return: True if overflowed.
 */
bool xe_guc_check_log_buf_overflow(struct xe_guc_log *log, enum guc_log_buffer_type type,
				   unsigned int full_cnt)
{
	unsigned int prev_full_cnt = log->stats[type].sampled_overflow;
	bool overflow = false;

	if (full_cnt != prev_full_cnt) {
		overflow = true;

		log->stats[type].overflow = full_cnt;
		log->stats[type].sampled_overflow += full_cnt - prev_full_cnt;

		if (full_cnt < prev_full_cnt) {
			/* buffer_full_cnt is a 4 bit counter */
			log->stats[type].sampled_overflow += 16;
		}
		xe_gt_notice(log_to_gt(log), "log buffer overflow\n");
	}

	return overflow;
}

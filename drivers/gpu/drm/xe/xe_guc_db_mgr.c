// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/bitmap.h>
#include <linux/mutex.h>

#include <drm/drm_managed.h>

#include "regs/xe_guc_regs.h"

#include "xe_assert.h"
#include "xe_gt_printk.h"
#include "xe_guc.h"
#include "xe_guc_db_mgr.h"
#include "xe_guc_types.h"

/**
 * DOC: GuC Doorbells
 *
 * The GFX doorbell solution provides a mechanism for submission of workload
 * to the graphics hardware by a ring3 application without the penalty of
 * ring transition for each workload submission.
 *
 * In SR-IOV mode, the doorbells are treated as shared resource and PF must
 * be able to provision exclusive range of IDs across VFs, which may want to
 * use this feature.
 */

static struct xe_guc *dbm_to_guc(struct xe_guc_db_mgr *dbm)
{
	return container_of(dbm, struct xe_guc, dbm);
}

static struct xe_gt *dbm_to_gt(struct xe_guc_db_mgr *dbm)
{
	return guc_to_gt(dbm_to_guc(dbm));
}

static struct xe_device *dbm_to_xe(struct xe_guc_db_mgr *dbm)
{
	return gt_to_xe(dbm_to_gt(dbm));
}

#define dbm_assert(_dbm, _cond)		xe_gt_assert(dbm_to_gt(_dbm), _cond)
#define dbm_mutex(_dbm)			(&dbm_to_guc(_dbm)->submission_state.lock)

static void dbm_print_locked(struct xe_guc_db_mgr *dbm, struct drm_printer *p, int indent);

static void __fini_dbm(struct drm_device *drm, void *arg)
{
	struct xe_guc_db_mgr *dbm = arg;
	unsigned int weight;

	mutex_lock(dbm_mutex(dbm));

	weight = bitmap_weight(dbm->bitmap, dbm->count);
	if (weight) {
		struct drm_printer p = xe_gt_info_printer(dbm_to_gt(dbm));

		xe_gt_err(dbm_to_gt(dbm), "GuC doorbells manager unclean (%u/%u)\n",
			  weight, dbm->count);
		dbm_print_locked(dbm, &p, 1);
	}

	bitmap_free(dbm->bitmap);
	dbm->bitmap = NULL;
	dbm->count = 0;

	mutex_unlock(dbm_mutex(dbm));
}

/**
 * xe_guc_db_mgr_init() - Initialize GuC Doorbells Manager.
 * @dbm: the &xe_guc_db_mgr to initialize
 * @count: number of doorbells to manage
 *
 * The bare-metal or PF driver can pass ~0 as &count to indicate that all
 * doorbells supported by the hardware are available for use.
 *
 * Only VF's drivers will have to provide explicit number of doorbells IDs
 * that they can use.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_guc_db_mgr_init(struct xe_guc_db_mgr *dbm, unsigned int count)
{
	int ret;

	if (count == ~0)
		count = GUC_NUM_DOORBELLS;

	dbm_assert(dbm, !dbm->bitmap);
	dbm_assert(dbm, count <= GUC_NUM_DOORBELLS);

	if (!count)
		goto done;

	dbm->bitmap = bitmap_zalloc(count, GFP_KERNEL);
	if (!dbm->bitmap)
		return -ENOMEM;
	dbm->count = count;

	ret = drmm_add_action_or_reset(&dbm_to_xe(dbm)->drm, __fini_dbm, dbm);
	if (ret)
		return ret;
done:
	xe_gt_dbg(dbm_to_gt(dbm), "using %u doorbell(s)\n", dbm->count);
	return 0;
}

static int dbm_reserve_chunk_locked(struct xe_guc_db_mgr *dbm,
				    unsigned int count, unsigned int spare)
{
	unsigned int used;
	int index;

	dbm_assert(dbm, count);
	dbm_assert(dbm, count <= GUC_NUM_DOORBELLS);
	dbm_assert(dbm, dbm->count <= GUC_NUM_DOORBELLS);
	lockdep_assert_held(dbm_mutex(dbm));

	if (!dbm->count)
		return -ENODATA;

	if (spare) {
		used = bitmap_weight(dbm->bitmap, dbm->count);
		if (used + count + spare > dbm->count)
			return -EDQUOT;
	}

	index = bitmap_find_next_zero_area(dbm->bitmap, dbm->count, 0, count, 0);
	if (index >= dbm->count)
		return -ENOSPC;

	bitmap_set(dbm->bitmap, index, count);

	return index;
}

static void dbm_release_chunk_locked(struct xe_guc_db_mgr *dbm,
				     unsigned int start, unsigned int count)
{
	dbm_assert(dbm, count);
	dbm_assert(dbm, count <= GUC_NUM_DOORBELLS);
	dbm_assert(dbm, dbm->count);
	dbm_assert(dbm, dbm->count <= GUC_NUM_DOORBELLS);
	lockdep_assert_held(dbm_mutex(dbm));

	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG)) {
		unsigned int n;

		for (n = 0; n < count; n++)
			dbm_assert(dbm, test_bit(start + n, dbm->bitmap));
	}
	bitmap_clear(dbm->bitmap, start, count);
}

/**
 * xe_guc_db_mgr_reserve_id_locked() - Reserve a single GuC Doorbell ID.
 * @dbm: the &xe_guc_db_mgr
 *
 * This function expects that submission lock is already taken.
 *
 * Return: ID of the allocated GuC doorbell or a negative error code on failure.
 */
int xe_guc_db_mgr_reserve_id_locked(struct xe_guc_db_mgr *dbm)
{
	return dbm_reserve_chunk_locked(dbm, 1, 0);
}

/**
 * xe_guc_db_mgr_release_id_locked() - Release a single GuC Doorbell ID.
 * @dbm: the &xe_guc_db_mgr
 * @id: the GuC Doorbell ID to release
 *
 * This function expects that submission lock is already taken.
 */
void xe_guc_db_mgr_release_id_locked(struct xe_guc_db_mgr *dbm, unsigned int id)
{
	return dbm_release_chunk_locked(dbm, id, 1);
}

/**
 * xe_guc_db_mgr_reserve_range() - Reserve a range of GuC Doorbell IDs.
 * @dbm: the &xe_guc_db_mgr
 * @count: number of GuC doorbell IDs to reserve
 * @spare: number of GuC doorbell IDs to keep available
 *
 * This function is dedicated for the for use by the PF which expects that
 * allocated range for the VF will be contiguous and that there will be at
 * least &spare IDs still available for the PF use after this reservation.
 *
 * Return: starting ID of the allocated GuC doorbell ID range or
 *         a negative error code on failure.
 */
int xe_guc_db_mgr_reserve_range(struct xe_guc_db_mgr *dbm,
				unsigned int count, unsigned int spare)
{
	int ret;

	mutex_lock(dbm_mutex(dbm));
	ret = dbm_reserve_chunk_locked(dbm, count, spare);
	mutex_unlock(dbm_mutex(dbm));

	return ret;
}

/**
 * xe_guc_db_mgr_release_range() - Release a range of Doorbell IDs.
 * @dbm: the &xe_guc_db_mgr
 * @start: the starting ID of GuC doorbell ID range to release
 * @count: number of GuC doorbell IDs to release
 */
void xe_guc_db_mgr_release_range(struct xe_guc_db_mgr *dbm,
				 unsigned int start, unsigned int count)
{
	mutex_lock(dbm_mutex(dbm));
	dbm_release_chunk_locked(dbm, start, count);
	mutex_unlock(dbm_mutex(dbm));
}

static void dbm_print_locked(struct xe_guc_db_mgr *dbm, struct drm_printer *p, int indent)
{
	unsigned int rs, re;
	unsigned int total;

	drm_printf_indent(p, indent, "count: %u\n", dbm->count);
	if (!dbm->bitmap)
		return;

	total = 0;
	for_each_clear_bitrange(rs, re, dbm->bitmap, dbm->count) {
		drm_printf_indent(p, indent, "available range: %u..%u (%u)\n",
				  rs, re - 1, re - rs);
		total += re - rs;
	}
	drm_printf_indent(p, indent, "available total: %u\n", total);

	total = 0;
	for_each_set_bitrange(rs, re, dbm->bitmap, dbm->count) {
		drm_printf_indent(p, indent, "reserved range: %u..%u (%u)\n",
				  rs, re - 1, re - rs);
		total += re - rs;
	}
	drm_printf_indent(p, indent, "reserved total: %u\n", total);
}

/**
 * xe_guc_db_mgr_print() - Print status of GuC Doorbells Manager.
 * @dbm: the &xe_guc_db_mgr to print
 * @p: the &drm_printer to print to
 * @indent: tab indentation level
 */
void xe_guc_db_mgr_print(struct xe_guc_db_mgr *dbm,
			 struct drm_printer *p, int indent)
{
	mutex_lock(dbm_mutex(dbm));
	dbm_print_locked(dbm, p, indent);
	mutex_unlock(dbm_mutex(dbm));
}

#if IS_BUILTIN(CONFIG_DRM_XE_KUNIT_TEST)
#include "tests/xe_guc_db_mgr_test.c"
#endif

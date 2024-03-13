// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <linux/bitmap.h>
#include <linux/mutex.h>

#include <drm/drm_managed.h>

#include "xe_assert.h"
#include "xe_gt_printk.h"
#include "xe_guc.h"
#include "xe_guc_id_mgr.h"
#include "xe_guc_types.h"

static struct xe_guc *idm_to_guc(struct xe_guc_id_mgr *idm)
{
	return container_of(idm, struct xe_guc, submission_state.idm);
}

static struct xe_gt *idm_to_gt(struct xe_guc_id_mgr *idm)
{
	return guc_to_gt(idm_to_guc(idm));
}

static struct xe_device *idm_to_xe(struct xe_guc_id_mgr *idm)
{
	return gt_to_xe(idm_to_gt(idm));
}

#define idm_assert(idm, cond)		xe_gt_assert(idm_to_gt(idm), cond)
#define idm_mutex(idm)			(&idm_to_guc(idm)->submission_state.lock)

static void idm_print_locked(struct xe_guc_id_mgr *idm, struct drm_printer *p, int indent);

static void __fini_idm(struct drm_device *drm, void *arg)
{
	struct xe_guc_id_mgr *idm = arg;

	mutex_lock(idm_mutex(idm));

	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG)) {
		unsigned int weight = bitmap_weight(idm->bitmap, idm->total);

		if (weight) {
			struct drm_printer p = xe_gt_info_printer(idm_to_gt(idm));

			xe_gt_err(idm_to_gt(idm), "GUC ID manager unclean (%u/%u)\n",
				  weight, idm->total);
			idm_print_locked(idm, &p, 1);
		}
	}

	bitmap_free(idm->bitmap);
	idm->bitmap = NULL;
	idm->total = 0;
	idm->used = 0;

	mutex_unlock(idm_mutex(idm));
}

/**
 * xe_guc_id_mgr_init() - Initialize GuC context ID Manager.
 * @idm: the &xe_guc_id_mgr to initialize
 * @limit: number of IDs to manage
 *
 * The bare-metal or PF driver can pass ~0 as &limit to indicate that all
 * context IDs supported by the GuC firmware are available for use.
 *
 * Only VF drivers will have to provide explicit number of context IDs
 * that they can use.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_guc_id_mgr_init(struct xe_guc_id_mgr *idm, unsigned int limit)
{
	int ret;

	idm_assert(idm, !idm->bitmap);
	idm_assert(idm, !idm->total);
	idm_assert(idm, !idm->used);

	if (limit == ~0)
		limit = GUC_ID_MAX;
	else if (limit > GUC_ID_MAX)
		return -ERANGE;
	else if (!limit)
		return -EINVAL;

	idm->bitmap = bitmap_zalloc(limit, GFP_KERNEL);
	if (!idm->bitmap)
		return -ENOMEM;
	idm->total = limit;

	ret = drmm_add_action_or_reset(&idm_to_xe(idm)->drm, __fini_idm, idm);
	if (ret)
		return ret;

	xe_gt_info(idm_to_gt(idm), "using %u GUC ID(s)\n", idm->total);
	return 0;
}

static unsigned int find_last_zero_area(unsigned long *bitmap,
					unsigned int total,
					unsigned int count)
{
	unsigned int found = total;
	unsigned int rs, re, range;

	for_each_clear_bitrange(rs, re, bitmap, total) {
		range = re - rs;
		if (range < count)
			continue;
		found = rs + (range - count);
	}
	return found;
}

static int idm_reserve_chunk_locked(struct xe_guc_id_mgr *idm,
				    unsigned int count, unsigned int retain)
{
	int id;

	idm_assert(idm, count);
	lockdep_assert_held(idm_mutex(idm));

	if (!idm->total)
		return -ENODATA;

	if (retain) {
		/*
		 * For IDs reservations (used on PF for VFs) we want to make
		 * sure there will be at least 'retain' available for the PF
		 */
		if (idm->used + count + retain > idm->total)
			return -EDQUOT;
		/*
		 * ... and we want to reserve highest IDs close to the end.
		 */
		id = find_last_zero_area(idm->bitmap, idm->total, count);
	} else {
		/*
		 * For regular IDs reservations (used by submission code)
		 * we start searching from the lower range of IDs.
		 */
		id = bitmap_find_next_zero_area(idm->bitmap, idm->total, 0, count, 0);
	}
	if (id >= idm->total)
		return -ENOSPC;

	bitmap_set(idm->bitmap, id, count);
	idm->used += count;

	return id;
}

static void idm_release_chunk_locked(struct xe_guc_id_mgr *idm,
				     unsigned int start, unsigned int count)
{
	idm_assert(idm, count);
	idm_assert(idm, count <= idm->used);
	idm_assert(idm, start < idm->total);
	idm_assert(idm, start + count - 1 < idm->total);
	lockdep_assert_held(idm_mutex(idm));

	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG)) {
		unsigned int n;

		for (n = 0; n < count; n++)
			idm_assert(idm, test_bit(start + n, idm->bitmap));
	}
	bitmap_clear(idm->bitmap, start, count);
	idm->used -= count;
}

/**
 * xe_guc_id_mgr_reserve_locked() - Reserve one or more GuC context IDs.
 * @idm: the &xe_guc_id_mgr
 * @count: number of IDs to allocate (can't be 0)
 *
 * This function is dedicated for the use by the GuC submission code,
 * where submission lock is already taken.
 *
 * Return: ID of allocated GuC context or a negative error code on failure.
 */
int xe_guc_id_mgr_reserve_locked(struct xe_guc_id_mgr *idm, unsigned int count)
{
	return idm_reserve_chunk_locked(idm, count, 0);
}

/**
 * xe_guc_id_mgr_release_locked() - Release one or more GuC context IDs.
 * @idm: the &xe_guc_id_mgr
 * @id: the GuC context ID to release
 * @count: number of IDs to release (can't be 0)
 *
 * This function is dedicated for the use by the GuC submission code,
 * where submission lock is already taken.
 */
void xe_guc_id_mgr_release_locked(struct xe_guc_id_mgr *idm, unsigned int id,
				  unsigned int count)
{
	return idm_release_chunk_locked(idm, id, count);
}

/**
 * xe_guc_id_mgr_reserve() - Reserve a range of GuC context IDs.
 * @idm: the &xe_guc_id_mgr
 * @count: number of GuC context IDs to reserve (can't be 0)
 * @retain: number of GuC context IDs to keep available (can't be 0)
 *
 * This function is dedicated for the use by the PF driver which expects that
 * reserved range of IDs will be contiguous and that there will be at least
 * &retain IDs still available for the PF after this reservation.
 *
 * Return: starting ID of the allocated GuC context ID range or
 *         a negative error code on failure.
 */
int xe_guc_id_mgr_reserve(struct xe_guc_id_mgr *idm,
			  unsigned int count, unsigned int retain)
{
	int ret;

	idm_assert(idm, count);
	idm_assert(idm, retain);

	mutex_lock(idm_mutex(idm));
	ret = idm_reserve_chunk_locked(idm, count, retain);
	mutex_unlock(idm_mutex(idm));

	return ret;
}

/**
 * xe_guc_id_mgr_release() - Release a range of GuC context IDs.
 * @idm: the &xe_guc_id_mgr
 * @start: the starting ID of GuC context range to release
 * @count: number of GuC context IDs to release
 */
void xe_guc_id_mgr_release(struct xe_guc_id_mgr *idm,
			   unsigned int start, unsigned int count)
{
	mutex_lock(idm_mutex(idm));
	idm_release_chunk_locked(idm, start, count);
	mutex_unlock(idm_mutex(idm));
}

static void idm_print_locked(struct xe_guc_id_mgr *idm, struct drm_printer *p, int indent)
{
	unsigned int rs, re;

	lockdep_assert_held(idm_mutex(idm));

	drm_printf_indent(p, indent, "total %u\n", idm->total);
	if (!idm->bitmap)
		return;

	drm_printf_indent(p, indent, "used %u\n", idm->used);
	for_each_set_bitrange(rs, re, idm->bitmap, idm->total)
		drm_printf_indent(p, indent, "range %u..%u (%u)\n", rs, re - 1, re - rs);
}

/**
 * xe_guc_id_mgr_print() - Print status of GuC ID Manager.
 * @idm: the &xe_guc_id_mgr to print
 * @p: the &drm_printer to print to
 * @indent: tab indentation level
 */
void xe_guc_id_mgr_print(struct xe_guc_id_mgr *idm, struct drm_printer *p, int indent)
{
	mutex_lock(idm_mutex(idm));
	idm_print_locked(idm, p, indent);
	mutex_unlock(idm_mutex(idm));
}

#if IS_BUILTIN(CONFIG_DRM_XE_KUNIT_TEST)
#include "tests/xe_guc_id_mgr_test.c"
#endif

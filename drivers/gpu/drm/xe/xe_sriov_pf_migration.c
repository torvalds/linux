// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_gt_sriov_pf_control.h"
#include "xe_gt_sriov_pf_migration.h"
#include "xe_pm.h"
#include "xe_sriov.h"
#include "xe_sriov_packet.h"
#include "xe_sriov_packet_types.h"
#include "xe_sriov_pf_helpers.h"
#include "xe_sriov_pf_migration.h"
#include "xe_sriov_printk.h"

static struct xe_sriov_migration_state *pf_pick_migration(struct xe_device *xe, unsigned int vfid)
{
	xe_assert(xe, IS_SRIOV_PF(xe));
	xe_assert(xe, vfid <= xe_sriov_pf_get_totalvfs(xe));

	return &xe->sriov.pf.vfs[vfid].migration;
}

/**
 * xe_sriov_pf_migration_waitqueue() - Get waitqueue for migration.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * Return: pointer to the migration waitqueue.
 */
wait_queue_head_t *xe_sriov_pf_migration_waitqueue(struct xe_device *xe, unsigned int vfid)
{
	return &pf_pick_migration(xe, vfid)->wq;
}

/**
 * xe_sriov_pf_migration_supported() - Check if SR-IOV VF migration is supported by the device
 * @xe: the &xe_device
 *
 * Return: true if migration is supported, false otherwise
 */
bool xe_sriov_pf_migration_supported(struct xe_device *xe)
{
	xe_assert(xe, IS_SRIOV_PF(xe));

	return IS_ENABLED(CONFIG_DRM_XE_DEBUG) || !xe->sriov.pf.migration.disabled;
}

/**
 * xe_sriov_pf_migration_disable() - Turn off SR-IOV VF migration support on PF.
 * @xe: the &xe_device instance.
 * @fmt: format string for the log message, to be combined with following VAs.
 */
void xe_sriov_pf_migration_disable(struct xe_device *xe, const char *fmt, ...)
{
	struct va_format vaf;
	va_list va_args;

	xe_assert(xe, IS_SRIOV_PF(xe));

	va_start(va_args, fmt);
	vaf.fmt = fmt;
	vaf.va  = &va_args;
	xe_sriov_notice(xe, "migration %s: %pV\n",
			IS_ENABLED(CONFIG_DRM_XE_DEBUG) ?
			"missing prerequisite" : "disabled",
			&vaf);
	va_end(va_args);

	xe->sriov.pf.migration.disabled = true;
}

static void pf_migration_check_support(struct xe_device *xe)
{
	if (!xe_device_has_memirq(xe))
		xe_sriov_pf_migration_disable(xe, "requires memory-based IRQ support");
}

static void pf_migration_cleanup(void *arg)
{
	struct xe_sriov_migration_state *migration = arg;

	xe_sriov_packet_free(migration->pending);
	xe_sriov_packet_free(migration->trailer);
	xe_sriov_packet_free(migration->descriptor);
}

/**
 * xe_sriov_pf_migration_init() - Initialize support for SR-IOV VF migration.
 * @xe: the &xe_device
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_migration_init(struct xe_device *xe)
{
	unsigned int n, totalvfs;
	int err;

	xe_assert(xe, IS_SRIOV_PF(xe));

	pf_migration_check_support(xe);

	if (!xe_sriov_pf_migration_supported(xe))
		return 0;

	totalvfs = xe_sriov_pf_get_totalvfs(xe);
	for (n = 1; n <= totalvfs; n++) {
		struct xe_sriov_migration_state *migration = pf_pick_migration(xe, n);

		err = drmm_mutex_init(&xe->drm, &migration->lock);
		if (err)
			return err;

		init_waitqueue_head(&migration->wq);

		err = devm_add_action_or_reset(xe->drm.dev, pf_migration_cleanup, migration);
		if (err)
			return err;
	}

	return 0;
}

static bool pf_migration_data_ready(struct xe_device *xe, unsigned int vfid)
{
	struct xe_gt *gt;
	u8 gt_id;

	for_each_gt(gt, xe, gt_id) {
		if (xe_gt_sriov_pf_control_check_save_failed(gt, vfid) ||
		    xe_gt_sriov_pf_control_check_save_data_done(gt, vfid) ||
		    !xe_gt_sriov_pf_migration_ring_empty(gt, vfid))
			return true;
	}

	return false;
}

static struct xe_sriov_packet *
pf_migration_consume(struct xe_device *xe, unsigned int vfid)
{
	struct xe_sriov_packet *data;
	bool more_data = false;
	struct xe_gt *gt;
	u8 gt_id;

	for_each_gt(gt, xe, gt_id) {
		data = xe_gt_sriov_pf_migration_save_consume(gt, vfid);
		if (data && PTR_ERR(data) != EAGAIN)
			return data;
		if (PTR_ERR(data) == -EAGAIN)
			more_data = true;
	}

	if (!more_data)
		return NULL;

	return ERR_PTR(-EAGAIN);
}

/**
 * xe_sriov_pf_migration_save_consume() - Consume a VF migration data packet from the device.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * Called by the save migration data consumer (userspace) when
 * processing migration data.
 * If there is no migration data to process, wait until more data is available.
 *
 * Return: Pointer to &xe_sriov_packet on success,
 *	   NULL if ring is empty and no more migration data is expected,
 *	   ERR_PTR value in case of error.
 */
struct xe_sriov_packet *
xe_sriov_pf_migration_save_consume(struct xe_device *xe, unsigned int vfid)
{
	struct xe_sriov_migration_state *migration = pf_pick_migration(xe, vfid);
	struct xe_sriov_packet *data;
	int ret;

	xe_assert(xe, IS_SRIOV_PF(xe));

	for (;;) {
		data = pf_migration_consume(xe, vfid);
		if (PTR_ERR(data) != -EAGAIN)
			break;

		ret = wait_event_interruptible(migration->wq,
					       pf_migration_data_ready(xe, vfid));
		if (ret)
			return ERR_PTR(ret);
	}

	return data;
}

static int pf_handle_descriptor(struct xe_device *xe, unsigned int vfid,
				struct xe_sriov_packet *data)
{
	int ret;

	if (data->hdr.tile_id != 0 || data->hdr.gt_id != 0)
		return -EINVAL;

	ret = xe_sriov_packet_process_descriptor(xe, vfid, data);
	if (ret)
		return ret;

	xe_sriov_packet_free(data);

	return 0;
}

static int pf_handle_trailer(struct xe_device *xe, unsigned int vfid,
			     struct xe_sriov_packet *data)
{
	struct xe_gt *gt;
	u8 gt_id;

	if (data->hdr.tile_id != 0 || data->hdr.gt_id != 0)
		return -EINVAL;
	if (data->hdr.offset != 0 || data->hdr.size != 0 || data->buff || data->bo)
		return -EINVAL;

	xe_sriov_packet_free(data);

	for_each_gt(gt, xe, gt_id)
		xe_gt_sriov_pf_control_restore_data_done(gt, vfid);

	return 0;
}

/**
 * xe_sriov_pf_migration_restore_produce() - Produce a VF migration data packet to the device.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 * @data: Pointer to &xe_sriov_packet
 *
 * Called by the restore migration data producer (userspace) when processing
 * migration data.
 * If the underlying data structure is full, wait until there is space.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_migration_restore_produce(struct xe_device *xe, unsigned int vfid,
					  struct xe_sriov_packet *data)
{
	struct xe_gt *gt;

	xe_assert(xe, IS_SRIOV_PF(xe));

	if (data->hdr.type == XE_SRIOV_PACKET_TYPE_DESCRIPTOR)
		return pf_handle_descriptor(xe, vfid, data);
	if (data->hdr.type == XE_SRIOV_PACKET_TYPE_TRAILER)
		return pf_handle_trailer(xe, vfid, data);

	gt = xe_device_get_gt(xe, data->hdr.gt_id);
	if (!gt || data->hdr.tile_id != gt->tile->id || data->hdr.type == 0) {
		xe_sriov_err_ratelimited(xe, "Received invalid restore packet for VF%u (type:%u, tile:%u, GT:%u)\n",
					 vfid, data->hdr.type, data->hdr.tile_id, data->hdr.gt_id);
		return -EINVAL;
	}

	return xe_gt_sriov_pf_migration_restore_produce(gt, vfid, data);
}

/**
 * xe_sriov_pf_migration_read() - Read migration data from the device.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 * @buf: start address of userspace buffer
 * @len: requested read size from userspace
 *
 * Return: number of bytes that has been successfully read,
 *	   0 if no more migration data is available,
 *	   -errno on failure.
 */
ssize_t xe_sriov_pf_migration_read(struct xe_device *xe, unsigned int vfid,
				   char __user *buf, size_t len)
{
	struct xe_sriov_migration_state *migration = pf_pick_migration(xe, vfid);
	ssize_t ret, consumed = 0;

	xe_assert(xe, IS_SRIOV_PF(xe));

	scoped_cond_guard(mutex_intr, return -EINTR, &migration->lock) {
		while (consumed < len) {
			ret = xe_sriov_packet_read_single(xe, vfid, buf, len - consumed);
			if (ret == -ENODATA)
				break;
			if (ret < 0)
				return ret;

			consumed += ret;
			buf += ret;
		}
	}

	return consumed;
}

/**
 * xe_sriov_pf_migration_write() - Write migration data to the device.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 * @buf: start address of userspace buffer
 * @len: requested write size from userspace
 *
 * Return: number of bytes that has been successfully written,
 *	   -errno on failure.
 */
ssize_t xe_sriov_pf_migration_write(struct xe_device *xe, unsigned int vfid,
				    const char __user *buf, size_t len)
{
	struct xe_sriov_migration_state *migration = pf_pick_migration(xe, vfid);
	ssize_t ret, produced = 0;

	xe_assert(xe, IS_SRIOV_PF(xe));

	scoped_cond_guard(mutex_intr, return -EINTR, &migration->lock) {
		while (produced < len) {
			ret = xe_sriov_packet_write_single(xe, vfid, buf, len - produced);
			if (ret < 0)
				return ret;

			produced += ret;
			buf += ret;
		}
	}

	return produced;
}

/**
 * xe_sriov_pf_migration_size() - Total size of migration data from all components within a device
 * @xe: the &xe_device
 * @vfid: the VF identifier (can't be 0)
 *
 * This function is for PF only.
 *
 * Return: total migration data size in bytes or a negative error code on failure.
 */
ssize_t xe_sriov_pf_migration_size(struct xe_device *xe, unsigned int vfid)
{
	size_t size = 0;
	struct xe_gt *gt;
	ssize_t ret;
	u8 gt_id;

	xe_assert(xe, IS_SRIOV_PF(xe));
	xe_assert(xe, vfid);

	for_each_gt(gt, xe, gt_id) {
		ret = xe_gt_sriov_pf_migration_size(gt, vfid);
		if (ret < 0)
			return ret;

		size += ret;
	}

	return size;
}

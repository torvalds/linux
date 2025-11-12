// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "abi/guc_actions_sriov_abi.h"
#include "xe_bo.h"
#include "xe_ggtt.h"
#include "xe_gt.h"
#include "xe_gt_sriov_pf_config.h"
#include "xe_gt_sriov_pf_control.h"
#include "xe_gt_sriov_pf_helpers.h"
#include "xe_gt_sriov_pf_migration.h"
#include "xe_gt_sriov_printk.h"
#include "xe_guc_buf.h"
#include "xe_guc_ct.h"
#include "xe_sriov.h"
#include "xe_sriov_packet.h"
#include "xe_sriov_packet_types.h"
#include "xe_sriov_pf_migration.h"

#define XE_GT_SRIOV_PF_MIGRATION_RING_SIZE 5

static struct xe_gt_sriov_migration_data *pf_pick_gt_migration(struct xe_gt *gt, unsigned int vfid)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid != PFID);
	xe_gt_assert(gt, vfid <= xe_sriov_pf_get_totalvfs(gt_to_xe(gt)));

	return &gt->sriov.pf.vfs[vfid].migration;
}

static void pf_dump_mig_data(struct xe_gt *gt, unsigned int vfid,
			     struct xe_sriov_packet *data,
			     const char *what)
{
	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG_SRIOV)) {
		struct drm_printer p = xe_gt_dbg_printer(gt);

		drm_printf(&p, "VF%u %s (%llu bytes)\n", vfid, what, data->hdr.size);
		drm_print_hex_dump(&p, "mig_hdr:  ", (void *)&data->hdr, sizeof(data->hdr));
		drm_print_hex_dump(&p, "mig_data: ", data->vaddr, min(SZ_64, data->hdr.size));
	}
}

static ssize_t pf_migration_ggtt_size(struct xe_gt *gt, unsigned int vfid)
{
	if (!xe_gt_is_main_type(gt))
		return 0;

	return xe_gt_sriov_pf_config_ggtt_save(gt, vfid, NULL, 0);
}

static int pf_save_vf_ggtt_mig_data(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_sriov_packet *data;
	size_t size;
	int ret;

	size = pf_migration_ggtt_size(gt, vfid);
	xe_gt_assert(gt, size);

	data = xe_sriov_packet_alloc(gt_to_xe(gt));
	if (!data)
		return -ENOMEM;

	ret = xe_sriov_packet_init(data, gt->tile->id, gt->info.id,
				   XE_SRIOV_PACKET_TYPE_GGTT, 0, size);
	if (ret)
		goto fail;

	ret = xe_gt_sriov_pf_config_ggtt_save(gt, vfid, data->vaddr, size);
	if (ret)
		goto fail;

	pf_dump_mig_data(gt, vfid, data, "GGTT data save");

	ret = xe_gt_sriov_pf_migration_save_produce(gt, vfid, data);
	if (ret)
		goto fail;

	return 0;

fail:
	xe_sriov_packet_free(data);
	xe_gt_sriov_err(gt, "Failed to save VF%u GGTT data (%pe)\n", vfid, ERR_PTR(ret));
	return ret;
}

static int pf_restore_vf_ggtt_mig_data(struct xe_gt *gt, unsigned int vfid,
				       struct xe_sriov_packet *data)
{
	int ret;

	pf_dump_mig_data(gt, vfid, data, "GGTT data restore");

	ret = xe_gt_sriov_pf_config_ggtt_restore(gt, vfid, data->vaddr, data->hdr.size);
	if (ret) {
		xe_gt_sriov_err(gt, "Failed to restore VF%u GGTT data (%pe)\n",
				vfid, ERR_PTR(ret));
		return ret;
	}

	return 0;
}

/**
 * xe_gt_sriov_pf_migration_ggtt_save() - Save VF GGTT migration data.
 * @gt: the &xe_gt
 * @vfid: the VF identifier (can't be 0)
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_migration_ggtt_save(struct xe_gt *gt, unsigned int vfid)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid != PFID);
	xe_gt_assert(gt, vfid <= xe_sriov_pf_get_totalvfs(gt_to_xe(gt)));

	return pf_save_vf_ggtt_mig_data(gt, vfid);
}

/**
 * xe_gt_sriov_pf_migration_ggtt_restore() - Restore VF GGTT migration data.
 * @gt: the &xe_gt
 * @vfid: the VF identifier (can't be 0)
 * @data: the &xe_sriov_packet containing migration data
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_migration_ggtt_restore(struct xe_gt *gt, unsigned int vfid,
					  struct xe_sriov_packet *data)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid != PFID);
	xe_gt_assert(gt, vfid <= xe_sriov_pf_get_totalvfs(gt_to_xe(gt)));

	return pf_restore_vf_ggtt_mig_data(gt, vfid, data);
}

/* Return: number of dwords saved/restored/required or a negative error code on failure */
static int guc_action_vf_save_restore(struct xe_guc *guc, u32 vfid, u32 opcode,
				      u64 addr, u32 ndwords)
{
	u32 request[PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_PF2GUC_SAVE_RESTORE_VF) |
		FIELD_PREP(PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_0_OPCODE, opcode),
		FIELD_PREP(PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_1_VFID, vfid),
		FIELD_PREP(PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_2_ADDR_LO, lower_32_bits(addr)),
		FIELD_PREP(PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_3_ADDR_HI, upper_32_bits(addr)),
		FIELD_PREP(PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_4_SIZE, ndwords),
	};

	return xe_guc_ct_send_block(&guc->ct, request, ARRAY_SIZE(request));
}

/* Return: size of the state in dwords or a negative error code on failure */
static int pf_send_guc_query_vf_mig_data_size(struct xe_gt *gt, unsigned int vfid)
{
	int ret;

	ret = guc_action_vf_save_restore(&gt->uc.guc, vfid, GUC_PF_OPCODE_VF_SAVE, 0, 0);
	return ret ?: -ENODATA;
}

/* Return: number of state dwords saved or a negative error code on failure */
static int pf_send_guc_save_vf_mig_data(struct xe_gt *gt, unsigned int vfid,
					void *dst, size_t size)
{
	const int ndwords = size / sizeof(u32);
	struct xe_guc *guc = &gt->uc.guc;
	CLASS(xe_guc_buf, buf)(&guc->buf, ndwords);
	int ret;

	xe_gt_assert(gt, size % sizeof(u32) == 0);
	xe_gt_assert(gt, size == ndwords * sizeof(u32));

	if (!xe_guc_buf_is_valid(buf))
		return -ENOBUFS;

	/* FW expects this buffer to be zero-initialized */
	memset(xe_guc_buf_cpu_ptr(buf), 0, size);

	ret = guc_action_vf_save_restore(guc, vfid, GUC_PF_OPCODE_VF_SAVE,
					 xe_guc_buf_flush(buf), ndwords);
	if (!ret)
		ret = -ENODATA;
	else if (ret > ndwords)
		ret = -EPROTO;
	else if (ret > 0)
		memcpy(dst, xe_guc_buf_sync_read(buf), ret * sizeof(u32));

	return ret;
}

/* Return: number of state dwords restored or a negative error code on failure */
static int pf_send_guc_restore_vf_mig_data(struct xe_gt *gt, unsigned int vfid,
					   const void *src, size_t size)
{
	const int ndwords = size / sizeof(u32);
	struct xe_guc *guc = &gt->uc.guc;
	CLASS(xe_guc_buf_from_data, buf)(&guc->buf, src, size);
	int ret;

	xe_gt_assert(gt, size % sizeof(u32) == 0);
	xe_gt_assert(gt, size == ndwords * sizeof(u32));

	if (!xe_guc_buf_is_valid(buf))
		return -ENOBUFS;

	ret = guc_action_vf_save_restore(guc, vfid, GUC_PF_OPCODE_VF_RESTORE,
					 xe_guc_buf_flush(buf), ndwords);
	if (!ret)
		ret = -ENODATA;
	else if (ret > ndwords)
		ret = -EPROTO;

	return ret;
}

static bool pf_migration_supported(struct xe_gt *gt)
{
	return xe_sriov_pf_migration_supported(gt_to_xe(gt));
}

static int pf_save_vf_guc_mig_data(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_sriov_packet *data;
	size_t size;
	int ret;

	ret = pf_send_guc_query_vf_mig_data_size(gt, vfid);
	if (ret < 0)
		goto fail;

	size = ret * sizeof(u32);

	data = xe_sriov_packet_alloc(gt_to_xe(gt));
	if (!data) {
		ret = -ENOMEM;
		goto fail;
	}

	ret = xe_sriov_packet_init(data, gt->tile->id, gt->info.id,
				   XE_SRIOV_PACKET_TYPE_GUC, 0, size);
	if (ret)
		goto fail_free;

	ret = pf_send_guc_save_vf_mig_data(gt, vfid, data->vaddr, size);
	if (ret < 0)
		goto fail_free;
	size = ret * sizeof(u32);
	xe_gt_assert(gt, size);
	xe_gt_assert(gt, size <= data->hdr.size);
	data->hdr.size = size;
	data->remaining = size;

	pf_dump_mig_data(gt, vfid, data, "GuC data save");

	ret = xe_gt_sriov_pf_migration_save_produce(gt, vfid, data);
	if (ret)
		goto fail_free;

	return 0;

fail_free:
	xe_sriov_packet_free(data);
fail:
	xe_gt_sriov_err(gt, "Failed to save VF%u GuC data (%pe)\n",
			vfid, ERR_PTR(ret));
	return ret;
}

static ssize_t pf_migration_guc_size(struct xe_gt *gt, unsigned int vfid)
{
	ssize_t size;

	if (!pf_migration_supported(gt))
		return -ENOPKG;

	size = pf_send_guc_query_vf_mig_data_size(gt, vfid);
	if (size >= 0)
		size *= sizeof(u32);

	return size;
}

/**
 * xe_gt_sriov_pf_migration_guc_save() - Save VF GuC migration data.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_migration_guc_save(struct xe_gt *gt, unsigned int vfid)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid != PFID);
	xe_gt_assert(gt, vfid <= xe_sriov_pf_get_totalvfs(gt_to_xe(gt)));

	if (!pf_migration_supported(gt))
		return -ENOPKG;

	return pf_save_vf_guc_mig_data(gt, vfid);
}

static int pf_restore_vf_guc_state(struct xe_gt *gt, unsigned int vfid,
				   struct xe_sriov_packet *data)
{
	int ret;

	xe_gt_assert(gt, data->hdr.size);

	pf_dump_mig_data(gt, vfid, data, "GuC data restore");

	ret = pf_send_guc_restore_vf_mig_data(gt, vfid, data->vaddr, data->hdr.size);
	if (ret < 0)
		goto fail;

	return 0;

fail:
	xe_gt_sriov_err(gt, "Failed to restore VF%u GuC data (%pe)\n",
			vfid, ERR_PTR(ret));
	return ret;
}

/**
 * xe_gt_sriov_pf_migration_guc_restore() - Restore VF GuC migration data.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 * @data: the &xe_sriov_packet containing migration data
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_migration_guc_restore(struct xe_gt *gt, unsigned int vfid,
					 struct xe_sriov_packet *data)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid != PFID);
	xe_gt_assert(gt, vfid <= xe_sriov_pf_get_totalvfs(gt_to_xe(gt)));

	if (!pf_migration_supported(gt))
		return -ENOPKG;

	return pf_restore_vf_guc_state(gt, vfid, data);
}

/**
 * xe_gt_sriov_pf_migration_size() - Total size of migration data from all components within a GT.
 * @gt: the &xe_gt
 * @vfid: the VF identifier (can't be 0)
 *
 * This function is for PF only.
 *
 * Return: total migration data size in bytes or a negative error code on failure.
 */
ssize_t xe_gt_sriov_pf_migration_size(struct xe_gt *gt, unsigned int vfid)
{
	ssize_t total = 0;
	ssize_t size;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid != PFID);
	xe_gt_assert(gt, vfid <= xe_sriov_pf_get_totalvfs(gt_to_xe(gt)));

	size = pf_migration_guc_size(gt, vfid);
	if (size < 0)
		return size;
	if (size > 0)
		size += sizeof(struct xe_sriov_packet_hdr);
	total += size;

	size = pf_migration_ggtt_size(gt, vfid);
	if (size < 0)
		return size;
	if (size > 0)
		size += sizeof(struct xe_sriov_packet_hdr);
	total += size;

	return total;
}

/**
 * xe_gt_sriov_pf_migration_ring_empty() - Check if a migration ring is empty.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * Return: true if the ring is empty, otherwise false.
 */
bool xe_gt_sriov_pf_migration_ring_empty(struct xe_gt *gt, unsigned int vfid)
{
	return ptr_ring_empty(&pf_pick_gt_migration(gt, vfid)->ring);
}

/**
 * xe_gt_sriov_pf_migration_ring_full() - Check if a migration ring is full.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * Return: true if the ring is full, otherwise false.
 */
bool xe_gt_sriov_pf_migration_ring_full(struct xe_gt *gt, unsigned int vfid)
{
	return ptr_ring_full(&pf_pick_gt_migration(gt, vfid)->ring);
}

/**
 * xe_gt_sriov_pf_migration_ring_free() - Consume and free all data in migration ring
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 */
void xe_gt_sriov_pf_migration_ring_free(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_migration_data *migration = pf_pick_gt_migration(gt, vfid);
	struct xe_sriov_packet *data;

	if (ptr_ring_empty(&migration->ring))
		return;

	xe_gt_sriov_notice(gt, "VF%u unprocessed migration data left in the ring!\n", vfid);

	while ((data = ptr_ring_consume(&migration->ring)))
		xe_sriov_packet_free(data);
}

static void pf_migration_save_data_todo(struct xe_gt *gt, unsigned int vfid,
					enum xe_sriov_packet_type type)
{
	set_bit(type, &pf_pick_gt_migration(gt, vfid)->save.data_remaining);
}

/**
 * xe_gt_sriov_pf_migration_save_init() - Initialize per-GT migration related data.
 * @gt: the &xe_gt
 * @vfid: the VF identifier (can't be 0)
 */
void xe_gt_sriov_pf_migration_save_init(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_migration_data *migration = pf_pick_gt_migration(gt, vfid);

	migration->save.data_remaining = 0;

	xe_gt_assert(gt, pf_migration_guc_size(gt, vfid) > 0);
	pf_migration_save_data_todo(gt, vfid, XE_SRIOV_PACKET_TYPE_GUC);

	if (pf_migration_ggtt_size(gt, vfid) > 0)
		pf_migration_save_data_todo(gt, vfid, XE_SRIOV_PACKET_TYPE_GGTT);
}

/**
 * xe_gt_sriov_pf_migration_save_data_pending() - Check if migration data type needs to be saved.
 * @gt: the &xe_gt
 * @vfid: the VF identifier (can't be 0)
 * @type: the &xe_sriov_packet_type of data to be checked
 *
 * Return: true if the data needs saving, otherwise false.
 */
bool xe_gt_sriov_pf_migration_save_data_pending(struct xe_gt *gt, unsigned int vfid,
						enum xe_sriov_packet_type type)
{
	return test_bit(type, &pf_pick_gt_migration(gt, vfid)->save.data_remaining);
}

/**
 * xe_gt_sriov_pf_migration_save_data_complete() - Complete migration data type save.
 * @gt: the &xe_gt
 * @vfid: the VF identifier (can't be 0)
 * @type: the &xe_sriov_packet_type to be marked as completed.
 */
void xe_gt_sriov_pf_migration_save_data_complete(struct xe_gt *gt, unsigned int vfid,
						 enum xe_sriov_packet_type type)
{
	clear_bit(type, &pf_pick_gt_migration(gt, vfid)->save.data_remaining);
}

/**
 * xe_gt_sriov_pf_migration_save_produce() - Add VF save data packet to migration ring.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 * @data: the &xe_sriov_packet
 *
 * Called by the save migration data producer (PF SR-IOV Control worker) when
 * processing migration data.
 * Wakes up the save migration data consumer (userspace), that is potentially
 * waiting for data when the ring was empty.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_migration_save_produce(struct xe_gt *gt, unsigned int vfid,
					  struct xe_sriov_packet *data)
{
	int ret;

	ret = ptr_ring_produce(&pf_pick_gt_migration(gt, vfid)->ring, data);
	if (ret)
		return ret;

	wake_up_all(xe_sriov_pf_migration_waitqueue(gt_to_xe(gt), vfid));

	return 0;
}

/**
 * xe_gt_sriov_pf_migration_restore_consume() - Get VF restore data packet from migration ring.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * Called by the restore migration data consumer (PF SR-IOV Control worker) when
 * processing migration data.
 * Wakes up the restore migration data producer (userspace), that is
 * potentially waiting to add more data when the ring is full.
 *
 * Return: Pointer to &xe_sriov_packet on success,
 *	   NULL if ring is empty.
 */
struct xe_sriov_packet *
xe_gt_sriov_pf_migration_restore_consume(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_migration_data *migration = pf_pick_gt_migration(gt, vfid);
	struct wait_queue_head *wq = xe_sriov_pf_migration_waitqueue(gt_to_xe(gt), vfid);
	struct xe_sriov_packet *data;

	data = ptr_ring_consume(&migration->ring);
	if (data)
		wake_up_all(wq);

	return data;
}

static bool pf_restore_data_ready(struct xe_gt *gt, unsigned int vfid)
{
	if (xe_gt_sriov_pf_control_check_restore_failed(gt, vfid) ||
	    !ptr_ring_full(&pf_pick_gt_migration(gt, vfid)->ring))
		return true;

	return false;
}

/**
 * xe_gt_sriov_pf_migration_restore_produce() - Add VF restore data packet to migration ring.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 * @data: the &xe_sriov_packet
 *
 * Called by the restore migration data producer (userspace) when processing
 * migration data.
 * If the ring is full, waits until there is space.
 * Queues the restore migration data consumer (PF SR-IOV Control worker), that
 * is potentially waiting for data when the ring was empty.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_migration_restore_produce(struct xe_gt *gt, unsigned int vfid,
					     struct xe_sriov_packet *data)
{
	int ret;

	xe_gt_assert(gt, data->hdr.tile_id == gt->tile->id);
	xe_gt_assert(gt, data->hdr.gt_id == gt->info.id);

	for (;;) {
		if (xe_gt_sriov_pf_control_check_restore_failed(gt, vfid))
			return -EIO;

		ret = ptr_ring_produce(&pf_pick_gt_migration(gt, vfid)->ring, data);
		if (!ret)
			break;

		ret = wait_event_interruptible(*xe_sriov_pf_migration_waitqueue(gt_to_xe(gt), vfid),
					       pf_restore_data_ready(gt, vfid));
		if (ret)
			return ret;
	}

	return xe_gt_sriov_pf_control_process_restore_data(gt, vfid);
}

/**
 * xe_gt_sriov_pf_migration_save_consume() - Get VF save data packet from migration ring.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * Called by the save migration data consumer (userspace) when
 * processing migration data.
 * Queues the save migration data producer (PF SR-IOV Control worker), that is
 * potentially waiting to add more data when the ring is full.
 *
 * Return: Pointer to &xe_sriov_packet on success,
 *	   NULL if ring is empty and there's no more data available,
 *	   ERR_PTR(-EAGAIN) if the ring is empty, but data is still produced.
 */
struct xe_sriov_packet *
xe_gt_sriov_pf_migration_save_consume(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_migration_data *migration = pf_pick_gt_migration(gt, vfid);
	struct xe_sriov_packet *data;
	int ret;

	data = ptr_ring_consume(&migration->ring);
	if (data) {
		ret = xe_gt_sriov_pf_control_process_save_data(gt, vfid);
		if (ret) {
			xe_sriov_packet_free(data);
			return ERR_PTR(ret);
		}

		return data;
	}

	if (xe_gt_sriov_pf_control_check_save_data_done(gt, vfid))
		return NULL;

	if (xe_gt_sriov_pf_control_check_save_failed(gt, vfid))
		return ERR_PTR(-EIO);

	return ERR_PTR(-EAGAIN);
}

static void destroy_pf_packet(void *ptr)
{
	struct xe_sriov_packet *data = ptr;

	xe_sriov_packet_free(data);
}

static void action_ring_cleanup(void *arg)
{
	struct ptr_ring *r = arg;

	ptr_ring_cleanup(r, destroy_pf_packet);
}

/**
 * xe_gt_sriov_pf_migration_init() - Initialize support for VF migration.
 * @gt: the &xe_gt
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_migration_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	unsigned int n, totalvfs;
	int err;

	xe_gt_assert(gt, IS_SRIOV_PF(xe));

	if (!pf_migration_supported(gt))
		return 0;

	totalvfs = xe_sriov_pf_get_totalvfs(xe);
	for (n = 1; n <= totalvfs; n++) {
		struct xe_gt_sriov_migration_data *migration = pf_pick_gt_migration(gt, n);

		err = ptr_ring_init(&migration->ring,
				    XE_GT_SRIOV_PF_MIGRATION_RING_SIZE, GFP_KERNEL);
		if (err)
			return err;

		err = devm_add_action_or_reset(xe->drm.dev, action_ring_cleanup, &migration->ring);
		if (err)
			return err;
	}

	return 0;
}

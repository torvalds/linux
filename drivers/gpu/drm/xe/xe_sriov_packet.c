// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_guc_klv_helpers.h"
#include "xe_sriov_packet.h"
#include "xe_sriov_packet_types.h"
#include "xe_sriov_pf_helpers.h"
#include "xe_sriov_pf_migration.h"
#include "xe_sriov_printk.h"

static struct mutex *pf_migration_mutex(struct xe_device *xe, unsigned int vfid)
{
	xe_assert(xe, IS_SRIOV_PF(xe));
	xe_assert(xe, vfid <= xe_sriov_pf_get_totalvfs(xe));

	return &xe->sriov.pf.vfs[vfid].migration.lock;
}

static struct xe_sriov_packet **pf_pick_pending(struct xe_device *xe, unsigned int vfid)
{
	xe_assert(xe, IS_SRIOV_PF(xe));
	xe_assert(xe, vfid <= xe_sriov_pf_get_totalvfs(xe));
	lockdep_assert_held(pf_migration_mutex(xe, vfid));

	return &xe->sriov.pf.vfs[vfid].migration.pending;
}

static struct xe_sriov_packet **
pf_pick_descriptor(struct xe_device *xe, unsigned int vfid)
{
	xe_assert(xe, IS_SRIOV_PF(xe));
	xe_assert(xe, vfid <= xe_sriov_pf_get_totalvfs(xe));
	lockdep_assert_held(pf_migration_mutex(xe, vfid));

	return &xe->sriov.pf.vfs[vfid].migration.descriptor;
}

static struct xe_sriov_packet **pf_pick_trailer(struct xe_device *xe, unsigned int vfid)
{
	xe_assert(xe, IS_SRIOV_PF(xe));
	xe_assert(xe, vfid <= xe_sriov_pf_get_totalvfs(xe));
	lockdep_assert_held(pf_migration_mutex(xe, vfid));

	return &xe->sriov.pf.vfs[vfid].migration.trailer;
}

static struct xe_sriov_packet **pf_pick_read_packet(struct xe_device *xe,
						    unsigned int vfid)
{
	struct xe_sriov_packet **data;

	data = pf_pick_descriptor(xe, vfid);
	if (*data)
		return data;

	data = pf_pick_pending(xe, vfid);
	if (!*data)
		*data = xe_sriov_pf_migration_save_consume(xe, vfid);
	if (*data)
		return data;

	data = pf_pick_trailer(xe, vfid);
	if (*data)
		return data;

	return NULL;
}

static bool pkt_needs_bo(struct xe_sriov_packet *data)
{
	return data->hdr.type == XE_SRIOV_PACKET_TYPE_VRAM;
}

/**
 * xe_sriov_packet_alloc() - Allocate migration data packet
 * @xe: the &xe_device
 *
 * Only allocates the "outer" structure, without initializing the migration
 * data backing storage.
 *
 * Return: Pointer to &xe_sriov_packet on success,
 *         NULL in case of error.
 */
struct xe_sriov_packet *xe_sriov_packet_alloc(struct xe_device *xe)
{
	struct xe_sriov_packet *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	data->xe = xe;
	data->hdr_remaining = sizeof(data->hdr);

	return data;
}

/**
 * xe_sriov_packet_free() - Free migration data packet.
 * @data: the &xe_sriov_packet
 */
void xe_sriov_packet_free(struct xe_sriov_packet *data)
{
	if (IS_ERR_OR_NULL(data))
		return;

	if (pkt_needs_bo(data))
		xe_bo_unpin_map_no_vm(data->bo);
	else
		kvfree(data->buff);

	kfree(data);
}

static int pkt_init(struct xe_sriov_packet *data)
{
	struct xe_gt *gt = xe_device_get_gt(data->xe, data->hdr.gt_id);

	if (!gt)
		return -EINVAL;

	if (data->hdr.size == 0)
		return 0;

	if (pkt_needs_bo(data)) {
		struct xe_bo *bo;

		bo = xe_bo_create_pin_map_novm(data->xe, gt->tile, PAGE_ALIGN(data->hdr.size),
					       ttm_bo_type_kernel,
					       XE_BO_FLAG_SYSTEM | XE_BO_FLAG_PINNED, false);
		if (IS_ERR(bo))
			return PTR_ERR(bo);

		data->bo = bo;
		data->vaddr = bo->vmap.vaddr;
	} else {
		void *buff = kvzalloc(data->hdr.size, GFP_KERNEL);

		if (!buff)
			return -ENOMEM;

		data->buff = buff;
		data->vaddr = buff;
	}

	return 0;
}

#define XE_SRIOV_PACKET_SUPPORTED_VERSION 1

/**
 * xe_sriov_packet_init() - Initialize migration packet header and backing storage.
 * @data: the &xe_sriov_packet
 * @tile_id: tile identifier
 * @gt_id: GT identifier
 * @type: &xe_sriov_packet_type
 * @offset: offset of data packet payload (within wider resource)
 * @size: size of data packet payload
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_packet_init(struct xe_sriov_packet *data, u8 tile_id, u8 gt_id,
			 enum xe_sriov_packet_type type, loff_t offset, size_t size)
{
	data->hdr.version = XE_SRIOV_PACKET_SUPPORTED_VERSION;
	data->hdr.type = type;
	data->hdr.tile_id = tile_id;
	data->hdr.gt_id = gt_id;
	data->hdr.offset = offset;
	data->hdr.size = size;
	data->remaining = size;

	return pkt_init(data);
}

/**
 * xe_sriov_packet_init_from_hdr() - Initialize migration packet backing storage based on header.
 * @data: the &xe_sriov_packet
 *
 * Header data is expected to be filled prior to calling this function.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_packet_init_from_hdr(struct xe_sriov_packet *data)
{
	xe_assert(data->xe, !data->hdr_remaining);

	if (data->hdr.version != XE_SRIOV_PACKET_SUPPORTED_VERSION)
		return -EINVAL;

	data->remaining = data->hdr.size;

	return pkt_init(data);
}

static ssize_t pkt_hdr_read(struct xe_sriov_packet *data,
			    char __user *buf, size_t len)
{
	loff_t offset = sizeof(data->hdr) - data->hdr_remaining;

	if (!data->hdr_remaining)
		return -EINVAL;

	if (len > data->hdr_remaining)
		len = data->hdr_remaining;

	if (copy_to_user(buf, (void *)&data->hdr + offset, len))
		return -EFAULT;

	data->hdr_remaining -= len;

	return len;
}

static ssize_t pkt_data_read(struct xe_sriov_packet *data,
			     char __user *buf, size_t len)
{
	if (len > data->remaining)
		len = data->remaining;

	if (copy_to_user(buf, data->vaddr + (data->hdr.size - data->remaining), len))
		return -EFAULT;

	data->remaining -= len;

	return len;
}

static ssize_t pkt_read_single(struct xe_sriov_packet **data,
			       unsigned int vfid, char __user *buf, size_t len)
{
	ssize_t copied = 0;

	if ((*data)->hdr_remaining)
		copied = pkt_hdr_read(*data, buf, len);
	else
		copied = pkt_data_read(*data, buf, len);

	if ((*data)->remaining == 0 && (*data)->hdr_remaining == 0) {
		xe_sriov_packet_free(*data);
		*data = NULL;
	}

	return copied;
}

/**
 * xe_sriov_packet_read_single() - Read migration data from a single packet.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 * @buf: start address of userspace buffer
 * @len: requested read size from userspace
 *
 * Return: number of bytes that has been successfully read,
 *	   0 if no more migration data is available,
 *	   -errno on failure.
 */
ssize_t xe_sriov_packet_read_single(struct xe_device *xe, unsigned int vfid,
				    char __user *buf, size_t len)
{
	struct xe_sriov_packet **data = pf_pick_read_packet(xe, vfid);

	if (!data)
		return -ENODATA;
	if (IS_ERR(*data))
		return PTR_ERR(*data);

	return pkt_read_single(data, vfid, buf, len);
}

static ssize_t pkt_hdr_write(struct xe_sriov_packet *data,
			     const char __user *buf, size_t len)
{
	loff_t offset = sizeof(data->hdr) - data->hdr_remaining;
	int ret;

	if (len > data->hdr_remaining)
		len = data->hdr_remaining;

	if (copy_from_user((void *)&data->hdr + offset, buf, len))
		return -EFAULT;

	data->hdr_remaining -= len;

	if (!data->hdr_remaining) {
		ret = xe_sriov_packet_init_from_hdr(data);
		if (ret)
			return ret;
	}

	return len;
}

static ssize_t pkt_data_write(struct xe_sriov_packet *data,
			      const char __user *buf, size_t len)
{
	if (len > data->remaining)
		len = data->remaining;

	if (copy_from_user(data->vaddr + (data->hdr.size - data->remaining), buf, len))
		return -EFAULT;

	data->remaining -= len;

	return len;
}

/**
 * xe_sriov_packet_write_single() - Write migration data to a single packet.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 * @buf: start address of userspace buffer
 * @len: requested write size from userspace
 *
 * Return: number of bytes that has been successfully written,
 *	   -errno on failure.
 */
ssize_t xe_sriov_packet_write_single(struct xe_device *xe, unsigned int vfid,
				     const char __user *buf, size_t len)
{
	struct xe_sriov_packet **data = pf_pick_pending(xe, vfid);
	int ret;
	ssize_t copied;

	if (IS_ERR_OR_NULL(*data)) {
		*data = xe_sriov_packet_alloc(xe);
		if (!*data)
			return -ENOMEM;
	}

	if ((*data)->hdr_remaining)
		copied = pkt_hdr_write(*data, buf, len);
	else
		copied = pkt_data_write(*data, buf, len);

	if ((*data)->hdr_remaining == 0 && (*data)->remaining == 0) {
		ret = xe_sriov_pf_migration_restore_produce(xe, vfid, *data);
		if (ret) {
			xe_sriov_packet_free(*data);
			return ret;
		}

		*data = NULL;
	}

	return copied;
}

#define MIGRATION_KLV_DEVICE_DEVID_KEY	0xf001u
#define MIGRATION_KLV_DEVICE_DEVID_LEN	1u
#define MIGRATION_KLV_DEVICE_REVID_KEY	0xf002u
#define MIGRATION_KLV_DEVICE_REVID_LEN	1u

#define MIGRATION_DESCRIPTOR_DWORDS	(GUC_KLV_LEN_MIN + MIGRATION_KLV_DEVICE_DEVID_LEN + \
					 GUC_KLV_LEN_MIN + MIGRATION_KLV_DEVICE_REVID_LEN)
static int pf_descriptor_init(struct xe_device *xe, unsigned int vfid)
{
	struct xe_sriov_packet **desc = pf_pick_descriptor(xe, vfid);
	struct xe_sriov_packet *data;
	unsigned int len = 0;
	u32 *klvs;
	int ret;

	data = xe_sriov_packet_alloc(xe);
	if (!data)
		return -ENOMEM;

	ret = xe_sriov_packet_init(data, 0, 0, XE_SRIOV_PACKET_TYPE_DESCRIPTOR,
				   0, MIGRATION_DESCRIPTOR_DWORDS * sizeof(u32));
	if (ret) {
		xe_sriov_packet_free(data);
		return ret;
	}

	klvs = data->vaddr;
	klvs[len++] = PREP_GUC_KLV_CONST(MIGRATION_KLV_DEVICE_DEVID_KEY,
					 MIGRATION_KLV_DEVICE_DEVID_LEN);
	klvs[len++] = xe->info.devid;
	klvs[len++] = PREP_GUC_KLV_CONST(MIGRATION_KLV_DEVICE_REVID_KEY,
					 MIGRATION_KLV_DEVICE_REVID_LEN);
	klvs[len++] = xe->info.revid;

	xe_assert(xe, len == MIGRATION_DESCRIPTOR_DWORDS);

	*desc = data;

	return 0;
}

/**
 * xe_sriov_packet_process_descriptor() - Process migration data descriptor packet.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 * @data: the &xe_sriov_packet containing the descriptor
 *
 * The descriptor uses the same KLV format as GuC, and contains metadata used for
 * checking migration data compatibility.
 *
 * Return: 0 on success, -errno on failure.
 */
int xe_sriov_packet_process_descriptor(struct xe_device *xe, unsigned int vfid,
				       struct xe_sriov_packet *data)
{
	u32 num_dwords = data->hdr.size / sizeof(u32);
	u32 *klvs = data->vaddr;

	xe_assert(xe, data->hdr.type == XE_SRIOV_PACKET_TYPE_DESCRIPTOR);

	if (data->hdr.size % sizeof(u32)) {
		xe_sriov_warn(xe, "Aborting migration, descriptor not in KLV format (size=%llu)\n",
			      data->hdr.size);
		return -EINVAL;
	}

	while (num_dwords >= GUC_KLV_LEN_MIN) {
		u32 key = FIELD_GET(GUC_KLV_0_KEY, klvs[0]);
		u32 len = FIELD_GET(GUC_KLV_0_LEN, klvs[0]);

		klvs += GUC_KLV_LEN_MIN;
		num_dwords -= GUC_KLV_LEN_MIN;

		if (len > num_dwords) {
			xe_sriov_warn(xe, "Aborting migration, truncated KLV %#x, len %u\n",
				      key, len);
			return -EINVAL;
		}

		switch (key) {
		case MIGRATION_KLV_DEVICE_DEVID_KEY:
			if (*klvs != xe->info.devid) {
				xe_sriov_warn(xe,
					      "Aborting migration, devid mismatch %#06x!=%#06x\n",
					      *klvs, xe->info.devid);
				return -ENODEV;
			}
			break;
		case MIGRATION_KLV_DEVICE_REVID_KEY:
			if (*klvs != xe->info.revid) {
				xe_sriov_warn(xe,
					      "Aborting migration, revid mismatch %#06x!=%#06x\n",
					      *klvs, xe->info.revid);
				return -ENODEV;
			}
			break;
		default:
			xe_sriov_dbg(xe,
				     "Skipping unknown migration KLV %#x, len=%u\n",
				     key, len);
			print_hex_dump_bytes("desc: ", DUMP_PREFIX_OFFSET, klvs,
					     min(SZ_64, len * sizeof(u32)));
			break;
		}

		klvs += len;
		num_dwords -= len;
	}

	return 0;
}

static void pf_pending_init(struct xe_device *xe, unsigned int vfid)
{
	struct xe_sriov_packet **data = pf_pick_pending(xe, vfid);

	*data = NULL;
}

#define MIGRATION_TRAILER_SIZE 0
static int pf_trailer_init(struct xe_device *xe, unsigned int vfid)
{
	struct xe_sriov_packet **trailer = pf_pick_trailer(xe, vfid);
	struct xe_sriov_packet *data;
	int ret;

	data = xe_sriov_packet_alloc(xe);
	if (!data)
		return -ENOMEM;

	ret = xe_sriov_packet_init(data, 0, 0, XE_SRIOV_PACKET_TYPE_TRAILER,
				   0, MIGRATION_TRAILER_SIZE);
	if (ret) {
		xe_sriov_packet_free(data);
		return ret;
	}

	*trailer = data;

	return 0;
}

/**
 * xe_sriov_packet_save_init() - Initialize the pending save migration packets.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * Return: 0 on success, -errno on failure.
 */
int xe_sriov_packet_save_init(struct xe_device *xe, unsigned int vfid)
{
	int ret;

	scoped_cond_guard(mutex_intr, return -EINTR, pf_migration_mutex(xe, vfid)) {
		ret = pf_descriptor_init(xe, vfid);
		if (ret)
			return ret;

		ret = pf_trailer_init(xe, vfid);
		if (ret)
			return ret;

		pf_pending_init(xe, vfid);
	}

	return 0;
}

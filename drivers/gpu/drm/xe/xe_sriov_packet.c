// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_printk.h"
#include "xe_sriov_packet.h"
#include "xe_sriov_packet_types.h"

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

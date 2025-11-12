/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PACKET_TYPES_H_
#define _XE_SRIOV_PACKET_TYPES_H_

#include <linux/types.h>

/**
 * struct xe_sriov_packet_hdr - Xe SR-IOV VF migration data packet header
 */
struct xe_sriov_packet_hdr {
	/** @version: migration data protocol version */
	u8 version;
	/** @type: migration data type */
	u8 type;
	/** @tile_id: migration data tile id */
	u8 tile_id;
	/** @gt_id: migration data gt id */
	u8 gt_id;
	/** @flags: migration data flags */
	u32 flags;
	/**
	 * @offset: offset into the resource;
	 * used when multiple packets of given type are used for migration
	 */
	u64 offset;
	/** @size: migration data size  */
	u64 size;
} __packed;

/**
 * struct xe_sriov_packet - Xe SR-IOV VF migration data packet
 */
struct xe_sriov_packet {
	/** @xe: the PF &xe_device this data packet belongs to */
	struct xe_device *xe;
	/** @vaddr: CPU pointer to payload data */
	void *vaddr;
	/** @remaining: payload data remaining */
	size_t remaining;
	/** @hdr_remaining: header data remaining */
	size_t hdr_remaining;
	union {
		/** @bo: Buffer object with migration data */
		struct xe_bo *bo;
		/** @buff: Buffer with migration data */
		void *buff;
	};
	/** @hdr: data packet header */
	struct xe_sriov_packet_hdr hdr;
};

#endif

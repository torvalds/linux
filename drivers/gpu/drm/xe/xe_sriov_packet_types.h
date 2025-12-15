/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PACKET_TYPES_H_
#define _XE_SRIOV_PACKET_TYPES_H_

#include <linux/types.h>

/**
 * enum xe_sriov_packet_type - Xe SR-IOV VF migration data packet type
 * @XE_SRIOV_PACKET_TYPE_DESCRIPTOR: Descriptor with VF device metadata
 * @XE_SRIOV_PACKET_TYPE_TRAILER: Trailer indicating end-of-stream
 * @XE_SRIOV_PACKET_TYPE_GGTT: Global GTT migration data
 * @XE_SRIOV_PACKET_TYPE_MMIO: MMIO registers migration data
 * @XE_SRIOV_PACKET_TYPE_GUC: GuC firmware migration data
 * @XE_SRIOV_PACKET_TYPE_VRAM: VRAM migration data
 */
enum xe_sriov_packet_type {
	/* Skipping 0 to catch uninitialized data */
	XE_SRIOV_PACKET_TYPE_DESCRIPTOR = 1,
	XE_SRIOV_PACKET_TYPE_TRAILER,
	XE_SRIOV_PACKET_TYPE_GGTT,
	XE_SRIOV_PACKET_TYPE_MMIO,
	XE_SRIOV_PACKET_TYPE_GUC,
	XE_SRIOV_PACKET_TYPE_VRAM,
};

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

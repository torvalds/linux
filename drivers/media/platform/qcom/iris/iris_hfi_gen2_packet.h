/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_HFI_GEN2_PACKET_H__
#define __IRIS_HFI_GEN2_PACKET_H__

#include "iris_hfi_gen2_defines.h"

struct iris_core;

/**
 * struct iris_hfi_header
 *
 * @size: size of the total packet in bytes including hfi_header
 * @session_id: For session level hfi_header session_id is non-zero.
 *                For  system level hfi_header session_id is zero.
 * @header_id: unique header id for each hfi_header
 * @reserved: reserved for future use
 * @num_packets: number of hfi_packet that are included with the hfi_header
 */
struct iris_hfi_header {
	u32 size;
	u32 session_id;
	u32 header_id;
	u32 reserved[4];
	u32 num_packets;
};

/**
 * struct iris_hfi_packet
 *
 * @size: size of the hfi_packet in bytes including payload
 * @type: one of the below hfi_packet types:
 *        HFI_CMD_*,
 *        HFI_PROP_*,
 *        HFI_ERROR_*,
 *        HFI_INFO_*,
 *        HFI_SYS_ERROR_*
 * @flags: hfi_packet flags. It is represented as bit masks.
 *         host packet flags are "enum hfi_packet_host_flags"
 *         firmware packet flags are "enum hfi_packet_firmware_flags"
 * @payload_info: payload information indicated by "enum hfi_packet_payload_info"
 * @port: hfi_packet port type indicated by "enum hfi_packet_port_type"
 *        This is bitmask and may be applicable to multiple ports.
 * @packet_id: host hfi_packet contains unique packet id.
 *             firmware returns host packet id in response packet
 *             wherever applicable. If not applicable firmware sets it to zero.
 * @reserved: reserved for future use.
 * @payload: flexible array of payload having additional packet information.
 */
struct iris_hfi_packet {
	u32 size;
	u32 type;
	u32 flags;
	u32 payload_info;
	u32 port;
	u32 packet_id;
	u32 reserved[2];
	u32 payload[];
};

void iris_hfi_gen2_packet_sys_init(struct iris_core *core, struct iris_hfi_header *hdr);
void iris_hfi_gen2_packet_image_version(struct iris_core *core, struct iris_hfi_header *hdr);
void iris_hfi_gen2_packet_sys_interframe_powercollapse(struct iris_core *core,
						       struct iris_hfi_header *hdr);
void iris_hfi_gen2_packet_sys_pc_prep(struct iris_core *core, struct iris_hfi_header *hdr);

#endif

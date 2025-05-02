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

/**
 * struct iris_hfi_buffer
 *
 * @type: buffer type indicated by "enum hfi_buffer_type"
 *        FW needs to return proper type for any buffer command.
 * @index: index of the buffer
 * @base_address: base address of the buffer.
 *                This buffer address is always 4KBytes aligned.
 * @addr_offset: accessible buffer offset from base address
 *               Decoder bitstream buffer: 256 Bytes aligned
 *               Firmware can uniquely identify a buffer based on
 *               base_address & addr_offset.
 *               HW can read memory only from base_address+addr_offset.
 * @buffer_size: accessible buffer size in bytes starting from addr_offset
 * @data_offset: data starts from "base_address + addr_offset + data_offset"
 *               RAW buffer: data_offset is 0. Restriction: 4KBytes aligned
 *               decoder bitstream buffer: no restriction (can be any value)
 * @data_size: data size in bytes
 * @flags: buffer flags. It is represented as bit masks.
 *         host buffer flags are "enum hfi_buffer_host_flags"
 *         firmware buffer flags are "enum hfi_buffer_firmware_flags"
 * @timestamp: timestamp of the buffer in nano seconds (ns)
 *             It is Presentation timestamp (PTS) for encoder & decoder.
 *             Decoder: it is pass through from bitstream to raw buffer.
 *                      firmware does not need to return as part of input buffer done.
 *             For any internal buffers: there is no timestamp. Host sets as 0.
 * @reserved: reserved for future use
 */
struct iris_hfi_buffer {
	u32 type;
	u32 index;
	u64 base_address;
	u32 addr_offset;
	u32 buffer_size;
	u32 data_offset;
	u32 data_size;
	u64 timestamp;
	u32 flags;
	u32 reserved[5];
};

u32 iris_hfi_gen2_get_color_primaries(u32 primaries);
u32 iris_hfi_gen2_get_transfer_char(u32 characterstics);
u32 iris_hfi_gen2_get_matrix_coefficients(u32 coefficients);
u32 iris_hfi_gen2_get_color_info(u32 matrix_coeff, u32 transfer_char, u32 primaries,
				 u32 colour_description_present_flag, u32 full_range,
				 u32 video_format, u32 video_signal_type_present_flag);

void iris_hfi_gen2_packet_sys_init(struct iris_core *core, struct iris_hfi_header *hdr);
void iris_hfi_gen2_packet_image_version(struct iris_core *core, struct iris_hfi_header *hdr);
void iris_hfi_gen2_packet_session_command(struct iris_inst *inst, u32 pkt_type,
					  u32 flags, u32 port, u32 session_id,
					  u32 payload_type, void *payload,
					  u32 payload_size);
void iris_hfi_gen2_packet_session_property(struct iris_inst *inst,
					   u32 pkt_type, u32 flags, u32 port,
					   u32 payload_type, void *payload, u32 payload_size);
void iris_hfi_gen2_packet_sys_interframe_powercollapse(struct iris_core *core,
						       struct iris_hfi_header *hdr);
void iris_hfi_gen2_packet_sys_pc_prep(struct iris_core *core, struct iris_hfi_header *hdr);

#endif

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Vidtv serves as a reference DVB driver and helps validate the existing APIs
 * in the media subsystem. It can also aid developers working on userspace
 * applications.
 *
 * This file contains the logic to translate the ES data for one access unit
 * from an encoder into MPEG TS packets. It does so by first encapsulating it
 * with a PES header and then splitting it into TS packets.
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#ifndef VIDTV_PES_H
#define VIDTV_PES_H

#include <asm/byteorder.h>
#include <linux/types.h>

#include "vidtv_common.h"

#define PES_MAX_LEN 65536 /* Set 'length' to 0 if greater. Only possible for video. */
#define PES_START_CODE_PREFIX 0x001 /* 00 00 01 */

/* Used when sending PTS, but not DTS */
struct vidtv_pes_optional_pts {
	u8 pts1;
	__be16 pts2;
	__be16 pts3;
} __packed;

/* Used when sending both PTS and DTS */
struct vidtv_pes_optional_pts_dts {
	u8 pts1;
	__be16 pts2;
	__be16 pts3;

	u8 dts1;
	__be16 dts2;
	__be16 dts3;
} __packed;

/* PES optional flags */
struct vidtv_pes_optional {
	/*
	 * These flags show which components are actually
	 * present in the "optional fields" in the optional PES
	 * header and which are not
	 *
	 * u16 two:2;  //0x2
	 * u16 PES_scrambling_control:2;
	 * u16 PES_priority:1;
	 * u16 data_alignment_indicator:1; // unused
	 * u16 copyright:1;
	 * u16 original_or_copy:1;
	 * u16 PTS_DTS:2;
	 * u16 ESCR:1;
	 * u16 ES_rate:1;
	 * u16 DSM_trick_mode:1;
	 * u16 additional_copy_info:1;
	 * u16 PES_CRC:1;
	 * u16 PES_extension:1;
	 */
	__be16 bitfield;
	u8 length;
} __packed;

/* The PES header */
struct vidtv_mpeg_pes {
	__be32 bitfield; /* packet_start_code_prefix:24, stream_id: 8 */
	/* after this field until the end of the PES data payload */
	__be16 length;
	struct vidtv_pes_optional optional[];
} __packed;

/**
 * struct pes_header_write_args - Arguments to write a PES header.
 * @dest_buf: The buffer to write into.
 * @dest_offset: where to start writing in the dest_buffer.
 * @dest_buf_sz: The size of the dest_buffer
 * @encoder_id: Encoder id (see vidtv_encoder.h)
 * @send_pts: Should we send PTS?
 * @pts: PTS value to send.
 * @send_dts: Should we send DTS?
 * @dts: DTS value to send.
 * @stream_id: The stream id to use. Ex: Audio streams (0xc0-0xdf), Video
 * streams (0xe0-0xef).
 * @n_pes_h_s_bytes: Padding bytes. Might be used by an encoder if needed, gets
 * discarded by the decoder.
 * @access_unit_len: The size of _one_ access unit (with any headers it might need)
 */
struct pes_header_write_args {
	void *dest_buf;
	u32 dest_offset;
	u32 dest_buf_sz;
	u32 encoder_id;

	bool send_pts;
	u64 pts;

	bool send_dts;
	u64 dts;

	u16 stream_id;
	/* might be used by an encoder if needed, gets discarded by decoder */
	u32 n_pes_h_s_bytes;
	u32 access_unit_len;
};

/**
 * struct pes_ts_header_write_args - Arguments to write a TS header.
 * @dest_buf: The buffer to write into.
 * @dest_offset: where to start writing in the dest_buffer.
 * @dest_buf_sz: The size of the dest_buffer
 * @pid: The PID to use for the TS packets.
 * @continuity_counter: Incremented on every new TS packet.
 * @n_pes_h_s_bytes: Padding bytes. Might be used by an encoder if needed, gets
 * discarded by the decoder.
 */
struct pes_ts_header_write_args {
	void *dest_buf;
	u32 dest_offset;
	u32 dest_buf_sz;
	u16 pid;
	u8 *continuity_counter;
	bool wrote_pes_header;
	u32 n_stuffing_bytes;
};

/**
 * struct pes_write_args - Arguments for the packetizer.
 * @dest_buf: The buffer to write into.
 * @from: A pointer to the encoder buffer containing one access unit.
 * @access_unit_len: The size of _one_ access unit (with any headers it might need)
 * @dest_offset: where to start writing in the dest_buffer.
 * @dest_buf_sz: The size of the dest_buffer
 * @pid: The PID to use for the TS packets.
 * @encoder_id: Encoder id (see vidtv_encoder.h)
 * @continuity_counter: Incremented on every new TS packet.
 * @stream_id: The stream id to use. Ex: Audio streams (0xc0-0xdf), Video
 * streams (0xe0-0xef).
 * @send_pts: Should we send PTS?
 * @pts: PTS value to send.
 * @send_dts: Should we send DTS?
 * @dts: DTS value to send.
 * @n_pes_h_s_bytes: Padding bytes. Might be used by an encoder if needed, gets
 * discarded by the decoder.
 */
struct pes_write_args {
	void *dest_buf;
	void *from;
	u32 access_unit_len;

	u32 dest_offset;
	u32 dest_buf_sz;
	u16 pid;

	u32 encoder_id;

	u8 *continuity_counter;

	u16 stream_id;

	bool send_pts;
	u64 pts;

	bool send_dts;
	u64 dts;

	u32 n_pes_h_s_bytes;
};

/**
 * vidtv_pes_write_into - Write a PES packet as MPEG-TS packets into a buffer.
 * @args: The args to use when writing
 *
 * This function translate the ES data for one access unit
 * from an encoder into MPEG TS packets. It does so by first encapsulating it
 * with a PES header and then splitting it into TS packets.
 *
 * The data is then written into the buffer pointed to by 'args.buf'
 *
 * Return: The number of bytes written into the buffer. This is usually NOT
 * equal to the size of the access unit, since we need space for PES headers, TS headers
 * and padding bytes, if any.
 */
u32 vidtv_pes_write_into(struct pes_write_args args);

#endif // VIDTV_PES_H

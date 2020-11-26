/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The Virtual DVB test driver serves as a reference DVB driver and helps
 * validate the existing APIs in the media subsystem. It can also aid
 * developers working on userspace applications.
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#ifndef VIDTV_TS_H
#define VIDTV_TS_H

#include <linux/types.h>
#include <asm/byteorder.h>

#define TS_SYNC_BYTE 0x47
#define TS_PACKET_LEN 188
#define TS_PAYLOAD_LEN 184
#define TS_NULL_PACKET_PID 0x1fff
#define TS_CC_MAX_VAL 0x0f /* 4 bits */
#define TS_LAST_VALID_PID 8191
#define TS_FILL_BYTE 0xff /* the byte used in packet stuffing */

struct vidtv_mpeg_ts_adaption {
	u8 length;
	struct {
		u8 extension:1;
		u8 private_data:1;
		u8 splicing_point:1;
		u8 OPCR:1;
		u8 PCR:1;
		u8 priority:1;
		u8 random_access:1;
		u8 discontinued:1;
	} __packed;
	u8 data[];
} __packed;

struct vidtv_mpeg_ts {
	u8 sync_byte;
	__be16 bitfield; /* tei: 1, payload_start:1 priority: 1, pid:13 */
	struct {
		u8 continuity_counter:4;
		u8 payload:1;
		u8 adaptation_field:1;
		u8 scrambling:2;
	} __packed;
	struct vidtv_mpeg_ts_adaption adaption[];
} __packed;

/**
 * struct pcr_write_args - Arguments for the pcr_write_into function.
 * @dest_buf: The buffer to write into.
 * @dest_offset: The byte offset into the buffer.
 * @pid: The TS PID for the PCR packets.
 * @buf_sz: The size of the buffer in bytes.
 * @countinuity_counter: The TS continuity_counter.
 * @pcr: A sample from the system clock.
 */
struct pcr_write_args {
	void *dest_buf;
	u32 dest_offset;
	u16 pid;
	u32 buf_sz;
	u8 *continuity_counter;
	u64 pcr;
};

/**
 * struct null_packet_write_args - Arguments for the null_write_into function
 * @dest_buf: The buffer to write into.
 * @dest_offset: The byte offset into the buffer.
 * @buf_sz: The size of the buffer in bytes.
 * @countinuity_counter: The TS continuity_counter.
 */
struct null_packet_write_args {
	void *dest_buf;
	u32 dest_offset;
	u32 buf_sz;
	u8 *continuity_counter;
};

/* Increment the continuity counter */
void vidtv_ts_inc_cc(u8 *continuity_counter);

/**
 * vidtv_ts_null_write_into - Write a TS null packet into a buffer.
 * @args: the arguments to use when writing.
 *
 * This function will write a null packet into a buffer. This is usually used to
 * pad TS streams.
 *
 * Return: The number of bytes written into the buffer.
 */
u32 vidtv_ts_null_write_into(struct null_packet_write_args args);

/**
 * vidtv_ts_pcr_write_into - Write a PCR  packet into a buffer.
 * @args: the arguments to use when writing.
 *
 * This function will write a PCR packet into a buffer. This is used to
 * synchronize the clocks between encoders and decoders.
 *
 * Return: The number of bytes written into the buffer.
 */
u32 vidtv_ts_pcr_write_into(struct pcr_write_args args);

#endif //VIDTV_TS_H

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Vidtv serves as a reference DVB driver and helps validate the existing APIs
 * in the media subsystem. It can also aid developers working on userspace
 * applications.
 *
 * This file contains the code for an AES3 (also known as AES/EBU) encoder.
 * It is based on EBU Tech 3250 and SMPTE 302M technical documents.
 *
 * This encoder currently supports 16bit AES3 subframes using 16bit signed
 * integers.
 *
 * Note: AU stands for Access Unit, and AAU stands for Audio Access Unit
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#ifndef VIDTV_S302M_H
#define VIDTV_S302M_H

#include <linux/types.h>
#include <asm/byteorder.h>

#include "vidtv_encoder.h"

/* see SMPTE 302M 2007 clause 7.3 */
#define VIDTV_S302M_BUF_SZ 65024

/* see ETSI TS 102 154 v.1.2.1 clause 7.3.5 */
#define VIDTV_S302M_FORMAT_IDENTIFIER 0x42535344

/**
 * struct vidtv_s302m_ctx - s302m encoder context.
 * @enc: A pointer to the containing encoder structure.
 * @frame_index: The current frame in a block
 * @au_count: The total number of access units encoded up to now
 */
struct vidtv_s302m_ctx {
	struct vidtv_encoder *enc;
	u32 frame_index;
	u32 au_count;
};

/**
 * struct vidtv_smpte_s302m_es - s302m MPEG Elementary Stream header.
 *
 * See SMPTE 302M 2007 table 1.
 */
struct vidtv_smpte_s302m_es {
	/*
	 *
	 * audio_packet_size:16;
	 * num_channels:2;
	 * channel_identification:8;
	 * bits_per_sample:2; // 0x0 for 16bits
	 * zero:4;
	 */
	__be32 bitfield;
} __packed;

struct vidtv_s302m_frame_16 {
	u8 data[5];
} __packed;

/**
 * struct vidtv_s302m_encoder_init_args - Args for the s302m encoder.
 *
 * @name: A name to identify this particular instance
 * @src_buf: The source buffer, encoder will default to a sine wave if this is NULL.
 * @src_buf_sz: The size of the source buffer.
 * @es_pid: The MPEG Elementary Stream PID to use.
 * @sync: Attempt to synchronize audio with this video encoder, if not NULL.
 * @last_sample_cb: A callback called when the encoder runs out of data.
 * @head: Add to this chain
 */
struct vidtv_s302m_encoder_init_args {
	char *name;
	void *src_buf;
	u32 src_buf_sz;
	u16 es_pid;
	struct vidtv_encoder *sync;
	void (*last_sample_cb)(u32 sample_no);

	struct vidtv_encoder *head;
};

struct vidtv_encoder
*vidtv_s302m_encoder_init(struct vidtv_s302m_encoder_init_args args);

void vidtv_s302m_encoder_destroy(struct vidtv_encoder *encoder);

#endif /* VIDTV_S302M_H */

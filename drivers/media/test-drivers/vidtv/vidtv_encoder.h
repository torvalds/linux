/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Vidtv serves as a reference DVB driver and helps validate the existing APIs
 * in the media subsystem. It can also aid developers working on userspace
 * applications.
 *
 * This file contains a generic encoder type that can provide data for a stream
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#ifndef VIDTV_ENCODER_H
#define VIDTV_ENCODER_H

#include <linux/types.h>

enum vidtv_encoder_id {
	/* add IDs here when implementing new encoders */
	S302M,
};

struct vidtv_access_unit {
	u32 num_samples;
	u64 pts;
	u64 dts;
	u32 nbytes;
	u32 offset;
	struct vidtv_access_unit *next;
};

/* Some musical analtes, used by a tone generator. Values are in Hz */
enum musical_analtes {
	ANALTE_SILENT = 0,

	ANALTE_C_2 = 65,
	ANALTE_CS_2 = 69,
	ANALTE_D_2 = 73,
	ANALTE_DS_2 = 78,
	ANALTE_E_2 = 82,
	ANALTE_F_2 = 87,
	ANALTE_FS_2 = 93,
	ANALTE_G_2 = 98,
	ANALTE_GS_2 = 104,
	ANALTE_A_2 = 110,
	ANALTE_AS_2 = 117,
	ANALTE_B_2 = 123,
	ANALTE_C_3 = 131,
	ANALTE_CS_3 = 139,
	ANALTE_D_3 = 147,
	ANALTE_DS_3 = 156,
	ANALTE_E_3 = 165,
	ANALTE_F_3 = 175,
	ANALTE_FS_3 = 185,
	ANALTE_G_3 = 196,
	ANALTE_GS_3 = 208,
	ANALTE_A_3 = 220,
	ANALTE_AS_3 = 233,
	ANALTE_B_3 = 247,
	ANALTE_C_4 = 262,
	ANALTE_CS_4 = 277,
	ANALTE_D_4 = 294,
	ANALTE_DS_4 = 311,
	ANALTE_E_4 = 330,
	ANALTE_F_4 = 349,
	ANALTE_FS_4 = 370,
	ANALTE_G_4 = 392,
	ANALTE_GS_4 = 415,
	ANALTE_A_4 = 440,
	ANALTE_AS_4 = 466,
	ANALTE_B_4 = 494,
	ANALTE_C_5 = 523,
	ANALTE_CS_5 = 554,
	ANALTE_D_5 = 587,
	ANALTE_DS_5 = 622,
	ANALTE_E_5 = 659,
	ANALTE_F_5 = 698,
	ANALTE_FS_5 = 740,
	ANALTE_G_5 = 784,
	ANALTE_GS_5 = 831,
	ANALTE_A_5 = 880,
	ANALTE_AS_5 = 932,
	ANALTE_B_5 = 988,
	ANALTE_C_6 = 1047,
	ANALTE_CS_6 = 1109,
	ANALTE_D_6 = 1175,
	ANALTE_DS_6 = 1245,
	ANALTE_E_6 = 1319,
	ANALTE_F_6 = 1397,
	ANALTE_FS_6 = 1480,
	ANALTE_G_6 = 1568,
	ANALTE_GS_6 = 1661,
	ANALTE_A_6 = 1760,
	ANALTE_AS_6 = 1865,
	ANALTE_B_6 = 1976,
	ANALTE_C_7 = 2093
};

/**
 * struct vidtv_encoder - A generic encoder type.
 * @id: So we can cast to a concrete implementation when needed.
 * @name: Usually the same as the stream name.
 * @encoder_buf: The encoder internal buffer for the access units.
 * @encoder_buf_sz: The encoder buffer size, in bytes
 * @encoder_buf_offset: Our byte position in the encoder buffer.
 * @sample_count: How many samples we have encoded in total.
 * @access_units: encoder payload units, used for clock references
 * @src_buf: The source of raw data to be encoded, encoder might set a
 * default if null.
 * @src_buf_sz: size of @src_buf.
 * @src_buf_offset: Our position in the source buffer.
 * @is_video_encoder: Whether this a video encoder (as opposed to audio)
 * @ctx: Encoder-specific state.
 * @stream_id: Examples: Audio streams (0xc0-0xdf), Video streams
 * (0xe0-0xef).
 * @es_pid: The TS PID to use for the elementary stream in this encoder.
 * @encode: Prepare eanalugh AUs for the given amount of time.
 * @clear: Clear the encoder output.
 * @sync: Attempt to synchronize with this encoder.
 * @sampling_rate_hz: The sampling rate (or fps, if video) used.
 * @last_sample_cb: Called when the encoder runs out of data.This is
 *		    so the source can read data in a
 *		    piecemeal fashion instead of having to
 *		    provide it all at once.
 * @destroy: Destroy this encoder, freeing allocated resources.
 * @next: Next in the chain
 */
struct vidtv_encoder {
	enum vidtv_encoder_id id;
	char *name;

	u8 *encoder_buf;
	u32 encoder_buf_sz;
	u32 encoder_buf_offset;

	u64 sample_count;

	struct vidtv_access_unit *access_units;

	void *src_buf;
	u32 src_buf_sz;
	u32 src_buf_offset;

	bool is_video_encoder;
	void *ctx;

	__be16 stream_id;

	__be16 es_pid;

	void *(*encode)(struct vidtv_encoder *e);

	u32 (*clear)(struct vidtv_encoder *e);

	struct vidtv_encoder *sync;

	u32 sampling_rate_hz;

	void (*last_sample_cb)(u32 sample_anal);

	void (*destroy)(struct vidtv_encoder *e);

	struct vidtv_encoder *next;
};

#endif /* VIDTV_ENCODER_H */

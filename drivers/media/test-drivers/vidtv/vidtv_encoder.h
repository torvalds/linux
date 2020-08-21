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

/**
 * struct vidtv_encoder - A generic encoder type.
 * @id: So we can cast to a concrete implementation when needed.
 * @name: Usually the same as the stream name.
 * @encoder_buf: The encoder internal buffer for the access units.
 * @encoder_buf_sz: The encoder buffer size, in bytes
 * @encoder_buf_offset: Our byte position in the encoder buffer.
 * @sample_count: How many samples we have encoded in total.
 * @src_buf: The source of raw data to be encoded, encoder might set a
 * default if null.
 * @src_buf_offset: Our position in the source buffer.
 * @is_video_encoder: Whether this a video encoder (as opposed to audio)
 * @ctx: Encoder-specific state.
 * @stream_id: Examples: Audio streams (0xc0-0xdf), Video streams
 * (0xe0-0xef).
 * @es_id: The TS PID to use for the elementary stream in this encoder.
 * @encode: Prepare enough AUs for the given amount of time.
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

	void *(*encode)(struct vidtv_encoder *e, u64 elapsed_time_usecs);

	u32 (*clear)(struct vidtv_encoder *e);

	struct vidtv_encoder *sync;

	u32 sampling_rate_hz;

	void (*last_sample_cb)(u32 sample_no);

	void (*destroy)(struct vidtv_encoder *e);

	struct vidtv_encoder *next;
};

#endif /* VIDTV_ENCODER_H */

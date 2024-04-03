/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_STREAM_H
#define PVR_STREAM_H

#include <linux/bits.h>
#include <linux/limits.h>
#include <linux/types.h>

struct pvr_device;

struct pvr_job;

enum pvr_stream_type {
	PVR_STREAM_TYPE_GEOM = 0,
	PVR_STREAM_TYPE_FRAG,
	PVR_STREAM_TYPE_COMPUTE,
	PVR_STREAM_TYPE_TRANSFER,
	PVR_STREAM_TYPE_STATIC_RENDER_CONTEXT,
	PVR_STREAM_TYPE_STATIC_COMPUTE_CONTEXT,

	PVR_STREAM_TYPE_MAX
};

enum pvr_stream_size {
	PVR_STREAM_SIZE_8 = 0,
	PVR_STREAM_SIZE_16,
	PVR_STREAM_SIZE_32,
	PVR_STREAM_SIZE_64,
	PVR_STREAM_SIZE_ARRAY,
};

#define PVR_FEATURE_NOT  BIT(31)
#define PVR_FEATURE_NONE U32_MAX

struct pvr_stream_def {
	u32 offset;
	enum pvr_stream_size size;
	u32 array_size;
	u32 feature;
};

struct pvr_stream_ext_def {
	const struct pvr_stream_def *stream;
	u32 stream_len;
	u32 header_mask;
	u32 quirk;
};

struct pvr_stream_ext_header {
	const struct pvr_stream_ext_def *ext_streams;
	u32 ext_streams_num;
	u32 valid_mask;
};

struct pvr_stream_cmd_defs {
	enum pvr_stream_type type;

	const struct pvr_stream_def *main_stream;
	u32 main_stream_len;

	u32 ext_nr_headers;
	const struct pvr_stream_ext_header *ext_headers;

	size_t dest_size;
};

int
pvr_stream_process(struct pvr_device *pvr_dev, const struct pvr_stream_cmd_defs *cmd_defs,
		   void *stream, u32 stream_size, void *dest_out);
void
pvr_stream_create_musthave_masks(struct pvr_device *pvr_dev);

#endif /* PVR_STREAM_H */

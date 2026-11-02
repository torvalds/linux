/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013--2024 Intel Corporation */

#ifndef IPU6_ISYS_VIDEO_H
#define IPU6_ISYS_VIDEO_H

#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include <media/media-entity.h>
#include <media/v4l2-dev.h>

#include "ipu6-isys-queue.h"

#define IPU6_ISYS_OUTPUT_PINS 11
#define IPU6_ISYS_MAX_PARALLEL_SOF 2

struct file;
struct ipu6_isys;
struct ipu6_isys_csi2;
struct ipu6_isys_subdev;
struct v4l2_mbus_frame_desc;

struct ipu6_isys_pixelformat {
	u32 pixelformat;
	u32 bpp;
	u32 bpp_packed;
	u32 code;
	u32 css_pixelformat;
	bool is_meta;
};

struct sequence_info {
	unsigned int sequence;
	u64 timestamp;
};

/*
 * Align with firmware stream. Each stream represents a CSI virtual channel.
 * May map to multiple video devices
 */
struct ipu6_isys_stream {
	struct mutex mutex;
	atomic_t sequence;
	atomic_t buf_id;
	unsigned int seq_index;
	struct sequence_info seq[IPU6_ISYS_MAX_PARALLEL_SOF];
	int stream_handle;
	unsigned int nr_output_pins;
	struct ipu6_isys_subdev *asd;
	struct list_head queues;
	struct list_head csi2_entry;

	struct completion stream_open_completion;
	struct completion stream_close_completion;
	struct completion stream_start_completion;
	struct completion stream_stop_completion;
	struct ipu6_isys *isys;

	struct ipu6_isys_queue *output_pins_queue[IPU6_ISYS_OUTPUT_PINS];
	int error;
	u8 vc;
};

struct ipu6_isys_video {
	struct ipu6_isys_queue aq;
	struct list_head csi2_entry;
	/* Serialise access to other fields in the struct. */
	struct mutex mutex;
	struct media_pad pad;
	struct video_device vdev;
	struct v4l2_pix_format pix_fmt;
	struct v4l2_meta_format meta_fmt;
	struct ipu6_isys *isys;
	struct ipu6_isys_csi2 *csi2;
	struct ipu6_isys_stream *stream;
	unsigned int streaming;
	u32 source_stream;
};

#define ipu6_isys_queue_to_video(__aq) \
	container_of(__aq, struct ipu6_isys_video, aq)

extern const struct ipu6_isys_pixelformat ipu6_isys_pfmts[];
extern const struct ipu6_isys_pixelformat ipu6_isys_pfmts_packed[];

const struct ipu6_isys_pixelformat *
ipu6_isys_get_isys_format(u32 pixelformat, u32 code);
int ipu6_isys_fw_pins_prepare(struct ipu6_isys_stream *stream,
			      struct v4l2_mbus_frame_desc *desc,
			      int (*fw_pin_cfg)(struct ipu6_isys_video *av,
						struct ipu6_isys_stream *stream,
						struct media_pad *src_pad,
						struct v4l2_mbus_frame_desc_entry *entry,
						void *__cfg), void *stream_cfg);
int ipu6_isys_start_stream_firmware(struct ipu6_isys_stream *stream,
				    struct ipu6_isys_buffer_list *bl,
				    struct v4l2_mbus_frame_desc *desc);
void ipu6_isys_stop_stream_firmware(struct ipu6_isys_stream *stream);
void ipu6_isys_close_stream_firmware(struct ipu6_isys_stream *stream);
struct ipu6_isys_stream *
ipu6_isys_find_stream_firmware(struct ipu6_isys_csi2 *csi2, u8 vc);
void ipu6_isys_free_stream_firmware(struct ipu6_isys_stream *stream);
struct ipu6_isys_stream *
ipu6_isys_alloc_stream_firmware(struct ipu6_isys_csi2 *csi2,
				struct v4l2_subdev_state *state,
				struct v4l2_mbus_frame_desc *desc,
				u8 vc);
int ipu6_isys_video_set_streaming(struct ipu6_isys_video *av, int state);
int ipu6_isys_fw_open(struct ipu6_isys *isys);
void ipu6_isys_fw_close(struct ipu6_isys *isys);
int ipu6_isys_setup_video(struct ipu6_isys_video *av,
			  struct media_pad *remote_pad,
			  struct media_pad *source_pad);
int ipu6_isys_video_init(struct ipu6_isys_video *av);
void ipu6_isys_video_cleanup(struct ipu6_isys_video *av);

u32 ipu6_isys_get_format(struct ipu6_isys_video *av);
u32 ipu6_isys_get_data_size(struct ipu6_isys_video *av);
u32 ipu6_isys_get_bytes_per_line(struct ipu6_isys_video *av);
u32 ipu6_isys_get_frame_width(struct ipu6_isys_video *av);
u32 ipu6_isys_get_frame_height(struct ipu6_isys_video *av);

#endif /* IPU6_ISYS_VIDEO_H */

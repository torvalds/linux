/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 - 2025 Intel Corporation
 */

#ifndef IPU7_ISYS_VIDEO_H
#define IPU7_ISYS_VIDEO_H

#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include <media/media-entity.h>
#include <media/v4l2-dev.h>

#include "ipu7-isys-queue.h"

#define IPU_INSYS_OUTPUT_PINS		11U
#define IPU_ISYS_MAX_PARALLEL_SOF	2U

struct file;
struct ipu7_isys;
struct ipu7_isys_csi2;
struct ipu7_insys_stream_cfg;
struct ipu7_isys_subdev;

struct ipu7_isys_pixelformat {
	u32 pixelformat;
	u32 bpp;
	u32 bpp_packed;
	u32 code;
	u32 css_pixelformat;
};

struct sequence_info {
	unsigned int sequence;
	u64 timestamp;
};

struct output_pin_data {
	void (*pin_ready)(struct ipu7_isys_stream *stream,
			  struct ipu7_insys_resp *info);
	struct ipu7_isys_queue *aq;
};

/*
 * Align with firmware stream. Each stream represents a CSI virtual channel.
 * May map to multiple video devices
 */
struct ipu7_isys_stream {
	struct mutex mutex;
	struct media_entity *source_entity;
	atomic_t sequence;
	atomic_t buf_id;
	unsigned int seq_index;
	struct sequence_info seq[IPU_ISYS_MAX_PARALLEL_SOF];
	int stream_source;
	int stream_handle;
	unsigned int nr_output_pins;
	struct ipu7_isys_subdev *asd;

	int nr_queues;  /* Number of capture queues */
	int nr_streaming;
	int streaming;
	struct list_head queues;
	struct completion stream_open_completion;
	struct completion stream_close_completion;
	struct completion stream_start_completion;
	struct completion stream_stop_completion;
	struct ipu7_isys *isys;

	struct output_pin_data output_pins[IPU_INSYS_OUTPUT_PINS];
	int error;
	u8 vc;
};

struct ipu7_isys_video {
	struct ipu7_isys_queue aq;
	/* Serialise access to other fields in the struct. */
	struct mutex mutex;
	struct media_pad pad;
	struct video_device vdev;
	struct v4l2_pix_format pix_fmt;
	struct ipu7_isys *isys;
	struct ipu7_isys_csi2 *csi2;
	struct ipu7_isys_stream *stream;
	unsigned int streaming;
	u8 vc;
	u8 dt;
};

#define ipu7_isys_queue_to_video(__aq)			\
	container_of(__aq, struct ipu7_isys_video, aq)

extern const struct ipu7_isys_pixelformat ipu7_isys_pfmts[];

const struct ipu7_isys_pixelformat *ipu7_isys_get_isys_format(u32 pixelformat);
int ipu7_isys_video_prepare_stream(struct ipu7_isys_video *av,
				   struct media_entity *source_entity,
				   int nr_queues);
int ipu7_isys_video_set_streaming(struct ipu7_isys_video *av, int state,
				  struct ipu7_isys_buffer_list *bl);
int ipu7_isys_fw_open(struct ipu7_isys *isys);
void ipu7_isys_fw_close(struct ipu7_isys *isys);
int ipu7_isys_setup_video(struct ipu7_isys_video *av,
			  struct media_entity **source_entity, int *nr_queues);
int ipu7_isys_video_init(struct ipu7_isys_video *av);
void ipu7_isys_video_cleanup(struct ipu7_isys_video *av);
void ipu7_isys_put_stream(struct ipu7_isys_stream *stream);
struct ipu7_isys_stream *
ipu7_isys_query_stream_by_handle(struct ipu7_isys *isys,
				 u8 stream_handle);
struct ipu7_isys_stream *
ipu7_isys_query_stream_by_source(struct ipu7_isys *isys, int source, u8 vc);
#endif /* IPU7_ISYS_VIDEO_H */

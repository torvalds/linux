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

struct output_pin_data {
	void (*pin_ready)(struct ipu6_isys_stream *stream,
			  struct ipu6_fw_isys_resp_info_abi *info);
	struct ipu6_isys_queue *aq;
};

/*
 * Align with firmware stream. Each stream represents a CSI virtual channel.
 * May map to multiple video devices
 */
struct ipu6_isys_stream {
	struct mutex mutex;
	struct media_entity *source_entity;
	atomic_t sequence;
	unsigned int seq_index;
	struct sequence_info seq[IPU6_ISYS_MAX_PARALLEL_SOF];
	int stream_source;
	int stream_handle;
	unsigned int nr_output_pins;
	struct ipu6_isys_subdev *asd;

	int nr_queues;	/* Number of capture queues */
	int nr_streaming;
	int streaming;	/* Has streaming been really started? */
	struct list_head queues;
	struct completion stream_open_completion;
	struct completion stream_close_completion;
	struct completion stream_start_completion;
	struct completion stream_stop_completion;
	struct ipu6_isys *isys;

	struct output_pin_data output_pins[IPU6_ISYS_OUTPUT_PINS];
	int error;
	u8 vc;
};

struct video_stream_watermark {
	u32 width;
	u32 height;
	u32 hblank;
	u32 frame_rate;
	u64 pixel_rate;
	u64 stream_data_rate;
	u16 sram_gran_shift;
	u16 sram_gran_size;
	struct list_head stream_node;
};

struct ipu6_isys_video {
	struct ipu6_isys_queue aq;
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
	struct video_stream_watermark watermark;
	u32 source_stream;
	u8 vc;
	u8 dt;
};

#define ipu6_isys_queue_to_video(__aq) \
	container_of(__aq, struct ipu6_isys_video, aq)

extern const struct ipu6_isys_pixelformat ipu6_isys_pfmts[];
extern const struct ipu6_isys_pixelformat ipu6_isys_pfmts_packed[];

const struct ipu6_isys_pixelformat *
ipu6_isys_get_isys_format(u32 pixelformat, u32 code);
int ipu6_isys_video_prepare_stream(struct ipu6_isys_video *av,
				   struct media_entity *source_entity,
				   int nr_queues);
int ipu6_isys_video_set_streaming(struct ipu6_isys_video *av, int state,
				  struct ipu6_isys_buffer_list *bl);
int ipu6_isys_fw_open(struct ipu6_isys *isys);
void ipu6_isys_fw_close(struct ipu6_isys *isys);
int ipu6_isys_setup_video(struct ipu6_isys_video *av,
			  struct media_entity **source_entity, int *nr_queues);
int ipu6_isys_video_init(struct ipu6_isys_video *av);
void ipu6_isys_video_cleanup(struct ipu6_isys_video *av);
void ipu6_isys_put_stream(struct ipu6_isys_stream *stream);
struct ipu6_isys_stream *
ipu6_isys_query_stream_by_handle(struct ipu6_isys *isys, u8 stream_handle);
struct ipu6_isys_stream *
ipu6_isys_query_stream_by_source(struct ipu6_isys *isys, int source, u8 vc);

void ipu6_isys_configure_stream_watermark(struct ipu6_isys_video *av,
					  bool state);
void ipu6_isys_update_stream_watermark(struct ipu6_isys_video *av, bool state);

u32 ipu6_isys_get_format(struct ipu6_isys_video *av);
u32 ipu6_isys_get_data_size(struct ipu6_isys_video *av);
u32 ipu6_isys_get_bytes_per_line(struct ipu6_isys_video *av);
u32 ipu6_isys_get_frame_width(struct ipu6_isys_video *av);
u32 ipu6_isys_get_frame_height(struct ipu6_isys_video *av);

#endif /* IPU6_ISYS_VIDEO_H */

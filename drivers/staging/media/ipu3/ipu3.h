/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Intel Corporation */

#ifndef __IPU3_H
#define __IPU3_H

#include <linux/iova.h>
#include <linux/pci.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-dma-sg.h>

#include "ipu3-css.h"

#define IMGU_NAME			"ipu3-imgu"

/*
 * The semantics of the driver is that whenever there is a buffer available in
 * master queue, the driver queues a buffer also to all other active nodes.
 * If user space hasn't provided a buffer to all other video nodes first,
 * the driver gets an internal dummy buffer and queues it.
 */
#define IMGU_QUEUE_MASTER		IPU3_CSS_QUEUE_IN
#define IMGU_QUEUE_FIRST_INPUT		IPU3_CSS_QUEUE_OUT
#define IMGU_MAX_QUEUE_DEPTH		(2 + 2)

#define IMGU_NODE_IN			0 /* Input RAW image */
#define IMGU_NODE_PARAMS		1 /* Input parameters */
#define IMGU_NODE_OUT			2 /* Main output for still or video */
#define IMGU_NODE_VF			3 /* Preview */
#define IMGU_NODE_STAT_3A		4 /* 3A statistics */
#define IMGU_NODE_NUM			5

#define file_to_intel_ipu3_node(__file) \
	container_of(video_devdata(__file), struct imgu_video_device, vdev)

#define IPU3_INPUT_MIN_WIDTH		0U
#define IPU3_INPUT_MIN_HEIGHT		0U
#define IPU3_INPUT_MAX_WIDTH		5120U
#define IPU3_INPUT_MAX_HEIGHT		38404U
#define IPU3_OUTPUT_MIN_WIDTH		2U
#define IPU3_OUTPUT_MIN_HEIGHT		2U
#define IPU3_OUTPUT_MAX_WIDTH		4480U
#define IPU3_OUTPUT_MAX_HEIGHT		34004U

struct ipu3_vb2_buffer {
	/* Public fields */
	struct vb2_v4l2_buffer vbb;	/* Must be the first field */

	/* Private fields */
	struct list_head list;
};

struct imgu_buffer {
	struct ipu3_vb2_buffer vid_buf;	/* Must be the first field */
	struct ipu3_css_buffer css_buf;
	struct ipu3_css_map map;
};

struct imgu_node_mapping {
	unsigned int css_queue;
	const char *name;
};

/**
 * struct imgu_video_device
 * each node registers as video device and maintains its
 * own vb2_queue.
 */
struct imgu_video_device {
	const char *name;
	bool output;
	bool enabled;
	struct v4l2_format vdev_fmt;	/* Currently set format */

	/* Private fields */
	struct video_device vdev;
	struct media_pad vdev_pad;
	struct v4l2_mbus_framefmt pad_fmt;
	struct vb2_queue vbq;
	struct list_head buffers;
	/* Protect vb2_queue and vdev structs*/
	struct mutex lock;
	atomic_t sequence;
	unsigned int id;
	unsigned int pipe;
};

struct imgu_v4l2_subdev {
	unsigned int pipe;
	struct v4l2_subdev subdev;
	struct media_pad subdev_pads[IMGU_NODE_NUM];
	struct {
		struct v4l2_rect eff; /* effective resolution */
		struct v4l2_rect bds; /* bayer-domain scaled resolution*/
		struct v4l2_rect gdc; /* gdc output resolution */
	} rect;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *ctrl;
	atomic_t running_mode;
	bool active;
};

struct imgu_media_pipe {
	unsigned int pipe;

	/* Internally enabled queues */
	struct {
		struct ipu3_css_map dmap;
		struct ipu3_css_buffer dummybufs[IMGU_MAX_QUEUE_DEPTH];
	} queues[IPU3_CSS_QUEUES];
	struct imgu_video_device nodes[IMGU_NODE_NUM];
	bool queue_enabled[IMGU_NODE_NUM];
	struct media_pipeline pipeline;
	struct imgu_v4l2_subdev imgu_sd;
};

/*
 * imgu_device -- ImgU (Imaging Unit) driver
 */
struct imgu_device {
	struct pci_dev *pci_dev;
	void __iomem *base;

	/* Public fields, fill before registering */
	unsigned int buf_struct_size;
	bool streaming;		/* Public read only */

	struct imgu_media_pipe imgu_pipe[IMGU_MAX_PIPE_NUM];

	/* Private fields */
	struct v4l2_device v4l2_dev;
	struct media_device media_dev;
	struct v4l2_file_operations v4l2_file_ops;

	/* MMU driver for css */
	struct ipu3_mmu_info *mmu;
	struct iova_domain iova_domain;

	/* css - Camera Sub-System */
	struct ipu3_css css;

	/*
	 * Coarse-grained lock to protect
	 * vid_buf.list and css->queue
	 */
	struct mutex lock;
	/* Forbit streaming and buffer queuing during system suspend. */
	atomic_t qbuf_barrier;
	/* Indicate if system suspend take place while imgu is streaming. */
	bool suspend_in_stream;
	/* Used to wait for FW buffer queue drain. */
	wait_queue_head_t buf_drain_wq;
};

unsigned int imgu_node_to_queue(unsigned int node);
unsigned int imgu_map_node(struct imgu_device *imgu, unsigned int css_queue);
int imgu_queue_buffers(struct imgu_device *imgu, bool initial,
		       unsigned int pipe);

int imgu_v4l2_register(struct imgu_device *dev);
int imgu_v4l2_unregister(struct imgu_device *dev);
void imgu_v4l2_buffer_done(struct vb2_buffer *vb, enum vb2_buffer_state state);

int imgu_s_stream(struct imgu_device *imgu, int enable);

#endif

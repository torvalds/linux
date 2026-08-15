/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#ifndef _ISP4_VIDEO_H_
#define _ISP4_VIDEO_H_

#include <media/v4l2-dev.h>
#include <media/videobuf2-memops.h>

#include "isp4_interface.h"

struct isp4vid_capture_buffer {
	/*
	 * struct vb2_v4l2_buffer must be the first element
	 * the videobuf2 framework will allocate this struct based on
	 * buf_struct_size and use the first sizeof(struct vb2_buffer) bytes of
	 * memory as a vb2_buffer
	 */
	struct vb2_v4l2_buffer vb2;
	struct isp4if_img_buf_info img_buf;
	struct list_head list;
	struct dma_buf *dbuf;
	void *bo;
	u64 gpu_addr;
};

struct isp4vid_dev {
	struct video_device vdev;
	struct media_pad vdev_pad;
	struct v4l2_pix_format format;

	/* mutex that protects vbq */
	struct mutex vbq_lock;
	struct vb2_queue vbq;

	/* mutex that protects buf_list */
	struct mutex buf_list_lock;
	struct list_head buf_list;

	u32 sequence;
	bool stream_started;

	struct device *dev;
	struct v4l2_subdev *isp_sdev;
	struct v4l2_fract timeperframe;
};

int isp4vid_dev_init(struct isp4vid_dev *isp_vdev, struct v4l2_subdev *isp_sd);

void isp4vid_dev_deinit(struct isp4vid_dev *isp_vdev);

void isp4vid_handle_frame_done(struct isp4vid_dev *isp_vdev,
			       const struct isp4if_img_buf_info *img_buf);

#endif /* _ISP4_VIDEO_H_ */

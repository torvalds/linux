/* linux/drivers/media/video/samsung/fimc/fimc_v4l2.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * V4L2 interface support file for Samsung Camera Interface (FIMC) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/videodev2.h>
#include <linux/videodev2_samsung.h>
#include <media/v4l2-ioctl.h>
#include <plat/fimc.h>

#include "fimc.h"

static int fimc_querycap(struct file *filp, void *fh,
			 struct v4l2_capability *cap)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;

	fimc_info1("%s: called\n", __func__);

	strcpy(cap->driver, "SEC FIMC Driver");
	strlcpy(cap->card, ctrl->vd->name, sizeof(cap->card));
	sprintf(cap->bus_info, "FIMC AHB-bus");

	cap->version = 0;
	cap->capabilities = (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT |
				V4L2_CAP_VIDEO_OVERLAY | V4L2_CAP_STREAMING);

	return 0;
}

static int fimc_reqbufs(struct file *filp, void *fh,
			struct v4l2_requestbuffers *b)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = -1;

	if (b->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		ret = fimc_reqbufs_capture(ctrl, b);
	} else if (b->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		ret = fimc_reqbufs_output(fh, b);
	} else {
		fimc_err("V4L2_BUF_TYPE_VIDEO_CAPTURE and "
			"V4L2_BUF_TYPE_VIDEO_OUTPUT are only supported\n");
		ret = -EINVAL;
	}

	return ret;
}

static int fimc_querybuf(struct file *filp, void *fh, struct v4l2_buffer *b)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = -1;

	if (b->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		ret = fimc_querybuf_capture(ctrl, b);
	} else if (b->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		ret = fimc_querybuf_output(fh, b);
	} else {
		fimc_err("V4L2_BUF_TYPE_VIDEO_CAPTURE and "
			"V4L2_BUF_TYPE_VIDEO_OUTPUT are only supported\n");
		ret = -EINVAL;
	}

	return ret;
}

static int fimc_g_ctrl(struct file *filp, void *fh, struct v4l2_control *c)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = -1;

	if (ctrl->cap != NULL) {
		ret = fimc_g_ctrl_capture(ctrl, c);
	} else if (ctrl->out != NULL) {
		ret = fimc_g_ctrl_output(fh, c);
	} else {
		fimc_err("%s: Invalid case\n", __func__);
		return -EINVAL;
	}

	return ret;
}

static int fimc_s_ctrl(struct file *filp, void *fh, struct v4l2_control *c)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = -1;

	if (ctrl->cap != NULL) {
		ret = fimc_s_ctrl_capture(ctrl, c);
	} else if (ctrl->out != NULL) {
		ret = fimc_s_ctrl_output(filp, fh, c);
	} else {
		fimc_err("%s: Invalid case\n", __func__);
		return -EINVAL;
	}

	return ret;
}

static int fimc_g_ext_ctrls(struct file *filp, void *fh, struct v4l2_ext_controls *c)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = -1;

	if (ctrl->cap != NULL) {
		ret = fimc_g_ext_ctrls_capture(fh, c);
	} else {
		fimc_err("%s: Invalid case\n", __func__);
		return -EINVAL;
	}
	return ret;
}

static int fimc_s_ext_ctrls(struct file *filp, void *fh, struct v4l2_ext_controls *c)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = -1;

	if (ctrl->cap != NULL) {
		ret = fimc_s_ext_ctrls_capture(fh, c);
	} else if (ctrl->out != NULL) {
		/* How about "ret = fimc_s_ext_ctrls_output(fh, c);"? */
	} else {
		fimc_err("%s: Invalid case\n", __func__);
		return -EINVAL;
	}

	return ret;
}

static int fimc_cropcap(struct file *filp, void *fh, struct v4l2_cropcap *a)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = -1;

	if (a->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		ret = fimc_cropcap_capture(ctrl, a);
	} else if (a->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		ret = fimc_cropcap_output(fh, a);
	} else {
		fimc_err("V4L2_BUF_TYPE_VIDEO_CAPTURE and "
			"V4L2_BUF_TYPE_VIDEO_OUTPUT are only supported\n");
		ret = -EINVAL;
	}

	return ret;
}

static int fimc_g_crop(struct file *filp, void *fh, struct v4l2_crop *a)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = -1;

	if (a->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		ret = fimc_g_crop_capture(ctrl, a);
	} else if (a->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		ret = fimc_g_crop_output(fh, a);
	} else {
		fimc_err("V4L2_BUF_TYPE_VIDEO_CAPTURE and "
			"V4L2_BUF_TYPE_VIDEO_OUTPUT are only supported\n");
		ret = -EINVAL;
	}

	return ret;
}

static int fimc_s_crop(struct file *filp, void *fh, struct v4l2_crop *a)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = -1;

	if (a->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		ret = fimc_s_crop_capture(ctrl, a);
	} else if (a->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		ret = fimc_s_crop_output(fh, a);
	} else {
		fimc_err("V4L2_BUF_TYPE_VIDEO_CAPTURE and "
			"V4L2_BUF_TYPE_VIDEO_OUTPUT are only supported\n");
		ret = -EINVAL;
	}

	return ret;
}

static int fimc_streamon(struct file *filp, void *fh, enum v4l2_buf_type i)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = -1;

	if (i == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		ret = fimc_streamon_capture(ctrl);
	} else if (i == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		ret = fimc_streamon_output(fh);
	} else {
		fimc_err("V4L2_BUF_TYPE_VIDEO_CAPTURE and "
			"V4L2_BUF_TYPE_VIDEO_OUTPUT are only supported\n");
		ret = -EINVAL;
	}

	return ret;
}

static int fimc_streamoff(struct file *filp, void *fh, enum v4l2_buf_type i)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = -1;

	if (i == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		ret = fimc_streamoff_capture(ctrl);
	} else if (i == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		ret = fimc_streamoff_output(fh);
	} else {
		fimc_err("V4L2_BUF_TYPE_VIDEO_CAPTURE and "
			"V4L2_BUF_TYPE_VIDEO_OUTPUT are only supported\n");
		ret = -EINVAL;
	}

	return ret;
}

static int fimc_qbuf(struct file *filp, void *fh, struct v4l2_buffer *b)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = -1;

	if (b->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		ret = fimc_qbuf_capture(ctrl, b);
	} else if (b->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		ret = fimc_qbuf_output(fh, b);
	} else {
		fimc_err("V4L2_BUF_TYPE_VIDEO_CAPTURE and "
			"V4L2_BUF_TYPE_VIDEO_OUTPUT are only supported\n");
		ret = -EINVAL;
	}

	return ret;
}

static int fimc_dqbuf(struct file *filp, void *fh, struct v4l2_buffer *b)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = -1;

	if (b->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		ret = fimc_dqbuf_capture(ctrl, b);
	} else if (b->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		ret = fimc_dqbuf_output(fh, b);
	} else {
		fimc_err("V4L2_BUF_TYPE_VIDEO_CAPTURE and "
			"V4L2_BUF_TYPE_VIDEO_OUTPUT are only supported\n");
		ret = -EINVAL;
	}

	return ret;
}

const struct v4l2_ioctl_ops fimc_v4l2_ops = {
	.vidioc_querycap		= fimc_querycap,
	.vidioc_reqbufs			= fimc_reqbufs,
	.vidioc_querybuf		= fimc_querybuf,
	.vidioc_g_ctrl			= fimc_g_ctrl,
	.vidioc_g_ext_ctrls		= fimc_g_ext_ctrls,
	.vidioc_s_ctrl			= fimc_s_ctrl,
	.vidioc_s_ext_ctrls		= fimc_s_ext_ctrls,
	.vidioc_cropcap			= fimc_cropcap,
	.vidioc_g_crop			= fimc_g_crop,
	.vidioc_s_crop			= fimc_s_crop,
	.vidioc_streamon		= fimc_streamon,
	.vidioc_streamoff		= fimc_streamoff,
	.vidioc_qbuf			= fimc_qbuf,
	.vidioc_dqbuf			= fimc_dqbuf,
	.vidioc_enum_fmt_vid_cap	= fimc_enum_fmt_vid_capture,
	.vidioc_g_fmt_vid_cap		= fimc_g_fmt_vid_capture,
	.vidioc_s_fmt_vid_cap		= fimc_s_fmt_vid_capture,
	.vidioc_s_fmt_type_private	= fimc_s_fmt_vid_private,
	.vidioc_try_fmt_vid_cap		= fimc_try_fmt_vid_capture,
	.vidioc_enum_input		= fimc_enum_input,
	.vidioc_g_input			= fimc_g_input,
	.vidioc_s_input			= fimc_s_input,
	.vidioc_g_parm			= fimc_g_parm,
	.vidioc_s_parm			= fimc_s_parm,
	.vidioc_queryctrl		= fimc_queryctrl,
	.vidioc_querymenu		= fimc_querymenu,
	.vidioc_g_fmt_vid_out		= fimc_g_fmt_vid_out,
	.vidioc_s_fmt_vid_out		= fimc_s_fmt_vid_out,
	.vidioc_try_fmt_vid_out		= fimc_try_fmt_vid_out,
	.vidioc_g_fbuf			= fimc_g_fbuf,
	.vidioc_s_fbuf			= fimc_s_fbuf,
	.vidioc_try_fmt_vid_overlay	= fimc_try_fmt_overlay,
	.vidioc_g_fmt_vid_overlay	= fimc_g_fmt_vid_overlay,
	.vidioc_s_fmt_vid_overlay	= fimc_s_fmt_vid_overlay,
	.vidioc_enum_framesizes		= fimc_enum_framesizes,
	.vidioc_enum_frameintervals	= fimc_enum_frameintervals,
};

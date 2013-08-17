/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is video functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <mach/videonode.h>
#include <media/exynos_mc.h>
#include <linux/cma.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/videodev2_exynos_media.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/v4l2-mediabus.h>
#include <linux/bug.h>

#include <media/videobuf2-core.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-mediabus.h>
#include <media/exynos_mc.h>

#include "fimc-is-core.h"
#include "fimc-is-param.h"
#include "fimc-is-cmd.h"
#include "fimc-is-regs.h"
#include "fimc-is-err.h"
#include "fimc-is-video.h"
#include "fimc-is-metadata.h"

const struct v4l2_file_operations fimc_is_3a0_video_fops;
const struct v4l2_ioctl_ops fimc_is_3a0_video_ioctl_ops;
const struct vb2_ops fimc_is_3a0_qops;
struct v4l2_m2m_ops fimc_is_3a0_m2m_ops;

int fimc_is_3a0_video_probe(void *data)
{
	int ret = 0;
	struct fimc_is_core *core = (struct fimc_is_core *)data;
	struct fimc_is_video *video = &core->video_3a0;

	dbg_isp("%s\n", __func__);

	ret = fimc_is_video_probe(video,
		data,
		FIMC_IS_VIDEO_3A0_NAME,
		FIMC_IS_VIDEO_3A0_NUM,
		&video->lock,
		&fimc_is_3a0_video_fops,
		&fimc_is_3a0_video_ioctl_ops);

	if (ret != 0)
		dev_err(&(core->pdev->dev),
			"%s::Failed to fimc_is_video_probe()\n", __func__);

	return ret;
}

/*
 * =============================================================================
 * Video File Opertation
 * =============================================================================
 */

static int fimc_is_3a0_video_open(struct file *file)
{
	int ret = 0;
	u32 refcount;
	struct fimc_is_core *core = video_drvdata(file);
	struct fimc_is_video *video = &core->video_3a0;
	struct fimc_is_video_ctx *vctx = NULL;
	struct fimc_is_device_ischain *device = NULL;

	BUG_ON(!core);

	ret = open_vctx(file, video, &vctx, FRAMEMGR_ID_3A0_GRP, FRAMEMGR_ID_3A0);
	if (ret) {
		err("open_vctx is fail(%d)", ret);
		goto p_err;
	}

	pr_info("[3A0:V:%d] %s\n", vctx->instance, __func__);

	refcount = atomic_read(&core->video_isp.refcount);
	if (refcount > FIMC_IS_MAX_NODES) {
		err("invalid ischain refcount(%d)", refcount);
		close_vctx(file, video, vctx);
		ret = -EINVAL;
		goto p_err;
	}

	device = &core->ischain[refcount - 1];

	ret = fimc_is_ischain_3a0_open(device, vctx);
	if (ret) {
		err("fimc_is_ischain_3a0_open is fail");
		close_vctx(file, video, vctx);
		goto p_err;
	}

	fimc_is_video_open(vctx,
		device,
		VIDEO_3A0_READY_BUFFERS,
		video,
		FIMC_IS_VIDEO_TYPE_M2M,
		&fimc_is_3a0_qops,
		&fimc_is_ischain_3a0_ops,
		&fimc_is_ischain_sub_ops,
		core->mem.vb2->ops);

p_err:
	return ret;
}

static int fimc_is_3a0_video_close(struct file *file)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = NULL;
	struct fimc_is_video *video;
	struct fimc_is_device_ischain *device;

	BUG_ON(!file);

	vctx = file->private_data;
	if (!vctx) {
		err("vctx is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	video = vctx->video;
	if (!video) {
		err("video is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	pr_info("[3A0:V:%d] %s\n", vctx->instance, __func__);

	device = vctx->device;
	if (!device) {
		err("device is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	fimc_is_ischain_3a0_close(device, vctx);
	fimc_is_video_close(vctx);

	ret = close_vctx(file, video, vctx);
	if (ret < 0)
		err("close_vctx is fail(%d)", ret);

p_err:
	return ret;
}

static unsigned int fimc_is_3a0_video_poll(struct file *file,
	struct poll_table_struct *wait)
{
	u32 ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	ret = fimc_is_video_poll(file, vctx, wait);
	if (ret)
		merr("fimc_is_video_poll is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_3a0_video_mmap(struct file *file,
	struct vm_area_struct *vma)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	ret = fimc_is_video_mmap(file, vctx, vma);
	if (ret)
		merr("fimc_is_video_mmap is fail(%d)", vctx, ret);

	return ret;
}

const struct v4l2_file_operations fimc_is_3a0_video_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_is_3a0_video_open,
	.release	= fimc_is_3a0_video_close,
	.poll		= fimc_is_3a0_video_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= fimc_is_3a0_video_mmap,
};

/*
 * =============================================================================
 * Video Ioctl Opertation
 * =============================================================================
 */

static int fimc_is_3a0_video_querycap(struct file *file, void *fh,
	struct v4l2_capability *cap)
{
	/* Todo : add to query capability code */
	return 0;
}

static int fimc_is_3a0_video_enum_fmt_mplane(struct file *file, void *priv,
	struct v4l2_fmtdesc *f)
{
	/* Todo : add to enumerate format code */
	return 0;
}

static int fimc_is_3a0_video_get_format_mplane(struct file *file, void *fh,
	struct v4l2_format *format)
{
	/* Todo : add to get format code */
	return 0;
}

static int fimc_is_3a0_video_set_format_mplane(struct file *file, void *fh,
	struct v4l2_format *format)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_device_ischain *device;
	struct fimc_is_group *group;
	struct fimc_is_subdev *subdev;
	struct fimc_is_queue *queue;

	BUG_ON(!vctx);

	mdbgv_3a0("%s\n", vctx, __func__);

	device = vctx->device;
	BUG_ON(!device);
	group = &device->group_3ax;
	BUG_ON(!group);
	subdev = &group->leader;

	ret = fimc_is_video_set_format_mplane(file, vctx, format);
	if (ret)
		merr("fimc_is_video_set_format_mplane is fail(%d)", vctx, ret);

	if (V4L2_TYPE_IS_OUTPUT(format->type)) {
		queue = &vctx->q_src;
		fimc_is_ischain_3a0_s_format(device,
			queue->framecfg.width,
			queue->framecfg.height);
	} else {
		queue = &vctx->q_dst;
		fimc_is_subdev_s_format(subdev,
			queue->framecfg.width,
			queue->framecfg.height);
	}

	return ret;
}

static int fimc_is_3a0_video_cropcap(struct file *file, void *fh,
	struct v4l2_cropcap *cropcap)
{
	/* Todo : add to crop capability code */
	return 0;
}

static int fimc_is_3a0_video_get_crop(struct file *file, void *fh,
	struct v4l2_crop *crop)
{
	/* Todo : add to get crop control code */
	return 0;
}

static int fimc_is_3a0_video_set_crop(struct file *file, void *fh,
	struct v4l2_crop *crop)
{
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_device_ischain *ischain;
	struct fimc_is_group *group;
	struct fimc_is_subdev *subdev;

	BUG_ON(!vctx);

	mdbgv_3a0("%s\n", vctx, __func__);

	ischain = vctx->device;
	BUG_ON(!ischain);
	group = &ischain->group_3ax;
	BUG_ON(!group);
	subdev = &group->leader;

	if (V4L2_TYPE_IS_OUTPUT(crop->type))
		fimc_is_ischain_3a0_s_format(ischain,
			crop->c.width, crop->c.height);
	else
		fimc_is_subdev_s_format(subdev,
			crop->c.width, crop->c.height);

	return 0;
}

static int fimc_is_3a0_video_reqbufs(struct file *file, void *priv,
	struct v4l2_requestbuffers *buf)
{
	int ret;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_3a0("%s(buffers : %d)\n", vctx, __func__, buf->count);

	ret = fimc_is_video_reqbufs(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_reqbufs is fail(error %d)", vctx, ret);

	return ret;
}

static int fimc_is_3a0_video_querybuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_3a0("%s\n", vctx, __func__);

	ret = fimc_is_video_querybuf(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_querybuf is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_3a0_video_qbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	BUG_ON(!vctx);

#ifdef DBG_STREAMING
	mdbgv_3a0("%s(%02d:%d)\n", vctx, __func__, buf->type, buf->index);
#endif

	ret = fimc_is_video_qbuf(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_qbuf(type %d) is fail(%d)", vctx, buf->type, ret);

	return ret;
}

static int fimc_is_3a0_video_dqbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	BUG_ON(!vctx);

#ifdef DBG_STREAMING
	mdbgv_3a0("%s\n", vctx, __func__);
#endif

	ret = fimc_is_video_dqbuf(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_dqbuf is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_3a0_video_streamon(struct file *file, void *priv,
	enum v4l2_buf_type type)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_3a0("%s\n", vctx, __func__);

	ret = fimc_is_video_streamon(file, vctx, type);
	if (ret)
		merr("fimc_is_video_streamon is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_3a0_video_streamoff(struct file *file, void *priv,
	enum v4l2_buf_type type)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_3a0("%s\n", vctx, __func__);

	ret = fimc_is_video_streamoff(file, vctx, type);
	if (ret)
		merr("fimc_is_video_streamoff is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_3a0_video_enum_input(struct file *file, void *priv,
	struct v4l2_input *input)
{
	/* Todo: add enum input control code */
	return 0;
}

static int fimc_is_3a0_video_g_input(struct file *file, void *priv,
	unsigned int *input)
{
	/* Todo: add to get input control code */
	return 0;
}

static int fimc_is_3a0_video_s_input(struct file *file, void *priv,
	unsigned int input)
{
	/* Todo: add to set input control code */
	return 0;
}

static int fimc_is_3a0_video_s_ctrl(struct file *file, void *priv,
	struct v4l2_control *ctrl)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_device_ischain *device;

	BUG_ON(!vctx);

	dbg_isp("%s\n", __func__);

	device = vctx->device;
	if (!device) {
		merr("device is NULL", vctx);
		ret = -EINVAL;
		goto p_err;
	}

	switch (ctrl->id) {
	case V4L2_CID_IS_FORCE_DONE:
		set_bit(FIMC_IS_GROUP_REQUEST_FSTOP, &device->group_3ax.state);
		break;
	default:
		err("unsupported ioctl(%d)\n", ctrl->id);
		ret = -EINVAL;
		break;
	}

p_err:
	return ret;
}

static int fimc_is_3a0_video_g_ctrl(struct file *file, void *priv,
	struct v4l2_control *ctrl)
{
	/* Todo: add to get control code */
	return 0;
}

static int fimc_is_3a0_video_g_ext_ctrl(struct file *file, void *priv,
	struct v4l2_ext_controls *ctrls)
{
	/* Todo: add to get extra control code */
	return 0;
}

const struct v4l2_ioctl_ops fimc_is_3a0_video_ioctl_ops = {
	.vidioc_querycap		= fimc_is_3a0_video_querycap,

	.vidioc_enum_fmt_vid_out_mplane	= fimc_is_3a0_video_enum_fmt_mplane,
	.vidioc_enum_fmt_vid_cap_mplane	= fimc_is_3a0_video_enum_fmt_mplane,

	.vidioc_g_fmt_vid_out_mplane	= fimc_is_3a0_video_get_format_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= fimc_is_3a0_video_get_format_mplane,

	.vidioc_s_fmt_vid_out_mplane	= fimc_is_3a0_video_set_format_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= fimc_is_3a0_video_set_format_mplane,

	.vidioc_querybuf		= fimc_is_3a0_video_querybuf,
	.vidioc_reqbufs			= fimc_is_3a0_video_reqbufs,

	.vidioc_qbuf			= fimc_is_3a0_video_qbuf,
	.vidioc_dqbuf			= fimc_is_3a0_video_dqbuf,

	.vidioc_streamon		= fimc_is_3a0_video_streamon,
	.vidioc_streamoff		= fimc_is_3a0_video_streamoff,

	.vidioc_enum_input		= fimc_is_3a0_video_enum_input,
	.vidioc_g_input			= fimc_is_3a0_video_g_input,
	.vidioc_s_input			= fimc_is_3a0_video_s_input,

	.vidioc_s_ctrl			= fimc_is_3a0_video_s_ctrl,
	.vidioc_g_ctrl			= fimc_is_3a0_video_g_ctrl,
	.vidioc_g_ext_ctrls		= fimc_is_3a0_video_g_ext_ctrl,

	.vidioc_cropcap			= fimc_is_3a0_video_cropcap,
	.vidioc_g_crop			= fimc_is_3a0_video_get_crop,
	.vidioc_s_crop			= fimc_is_3a0_video_set_crop,
};

static int fimc_is_3a0_queue_setup(struct vb2_queue *vbq,
	const struct v4l2_format *fmt,
	unsigned int *num_buffers,
	unsigned int *num_planes,
	unsigned int sizes[],
	void *allocators[])
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = vbq->drv_priv;
	struct fimc_is_video *video;
	struct fimc_is_queue *queue;
	struct fimc_is_core *core;
	void *alloc_ctx;

	BUG_ON(!vctx);

	mdbgv_3a0("%s\n", vctx, __func__);

	queue = GET_VCTX_QUEUE(vctx, vbq);
	video = vctx->video;
	core = video->core;
	alloc_ctx = core->mem.alloc_ctx;

	ret = fimc_is_queue_setup(queue,
		alloc_ctx,
		num_planes,
		sizes,
		allocators);
	if (ret)
		merr("fimc_is_queue_setup is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_3a0_buffer_prepare(struct vb2_buffer *vb)
{
	/* Todo : add to prepare buffer */
	return 0;
}

static inline void fimc_is_3a0_wait_prepare(struct vb2_queue *vbq)
{
	fimc_is_queue_wait_prepare(vbq);
}

static inline void fimc_is_3a0_wait_finish(struct vb2_queue *vbq)
{
	fimc_is_queue_wait_finish(vbq);
}

static int fimc_is_3a0_start_streaming(struct vb2_queue *vbq,
	unsigned int count)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = vbq->drv_priv;
	struct fimc_is_queue *queue;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader;

	BUG_ON(!vctx);

	mdbgv_3a0("%s\n", vctx, __func__);

	queue = GET_VCTX_QUEUE(vctx, vbq);
	device = vctx->device;
	leader = &device->group_3ax.leader;

	ret = fimc_is_queue_start_streaming(queue, device, leader, vctx);
	if (ret)
		merr("fimc_is_queue_start_streaming is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_3a0_stop_streaming(struct vb2_queue *vbq)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = vbq->drv_priv;
	struct fimc_is_queue *queue;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader;

	BUG_ON(!vctx);

	mdbgv_3a0("%s\n", vctx, __func__);

	queue = GET_VCTX_QUEUE(vctx, vbq);
	device = vctx->device;
	if (!device) {
		err("device is NULL");
		ret = -EINVAL;
		goto p_err;
	}
	leader = &device->group_3ax.leader;

	ret = fimc_is_queue_stop_streaming(queue, device, leader, vctx);
	if (ret)
		merr("fimc_is_queue_stop_streaming is fail(%d)", vctx, ret);

p_err:
	return ret;
}

static void fimc_is_3a0_buffer_queue(struct vb2_buffer *vb)
{
	u32 index;
	struct fimc_is_video_ctx *vctx = vb->vb2_queue->drv_priv;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader;
	struct fimc_is_video *video;
	struct fimc_is_queue *queue;

	BUG_ON(!vctx);
	index = vb->v4l2_buf.index;

#ifdef DBG_STREAMING
	mdbgv_3a0("%s(%02d:%d)\n", vctx, __func__, vb->v4l2_buf.type, index);
#endif

	video = vctx->video;
	device = vctx->device;
	leader = &device->group_3ax.leader;

	if (V4L2_TYPE_IS_OUTPUT(vb->v4l2_buf.type)) {
		queue = GET_SRC_QUEUE(vctx);
		fimc_is_queue_buffer_queue(queue, video->vb2, vb);
		fimc_is_ischain_3a0_buffer_queue(device, queue, index);
	} else {
		queue = GET_DST_QUEUE(vctx);
		fimc_is_queue_buffer_queue(queue, video->vb2, vb);
		fimc_is_subdev_buffer_queue(leader, index);
	}
}

static int fimc_is_3a0_buffer_finish(struct vb2_buffer *vb)
{
	int ret = 0;
	u32 index = vb->v4l2_buf.index;
	struct fimc_is_video_ctx *vctx = vb->vb2_queue->drv_priv;
	struct fimc_is_device_ischain *device = vctx->device;
	struct fimc_is_group *group = &device->group_3ax;
	struct fimc_is_subdev *subdev = &group->leader;
	struct fimc_is_queue *queue;

	BUG_ON(!vctx);
	BUG_ON(!device);

#ifdef DBG_STREAMING
	mdbgv_3a0("%s(%02d:%d)\n", vctx, __func__, vb->v4l2_buf.type, index);
#endif

	if (V4L2_TYPE_IS_OUTPUT(vb->v4l2_buf.type)) {
		queue = &vctx->q_src;
		fimc_is_ischain_3a0_buffer_finish(device, index);
	} else {
		queue = &vctx->q_dst;
		fimc_is_subdev_buffer_finish(subdev, index);
	}

	return ret;
}

const struct vb2_ops fimc_is_3a0_qops = {
	.queue_setup		= fimc_is_3a0_queue_setup,
	.buf_prepare		= fimc_is_3a0_buffer_prepare,
	.buf_queue		= fimc_is_3a0_buffer_queue,
	.buf_finish		= fimc_is_3a0_buffer_finish,
	.wait_prepare		= fimc_is_3a0_wait_prepare,
	.wait_finish		= fimc_is_3a0_wait_finish,
	.start_streaming	= fimc_is_3a0_start_streaming,
	.stop_streaming		= fimc_is_3a0_stop_streaming,
};


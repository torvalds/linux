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

#include "fimc-is-core.h"
#include "fimc-is-param.h"
#include "fimc-is-cmd.h"
#include "fimc-is-regs.h"
#include "fimc-is-err.h"
#include "fimc-is-video.h"
#include "fimc-is-metadata.h"

const struct v4l2_file_operations fimc_is_vdo_video_fops;
const struct v4l2_ioctl_ops fimc_is_vdo_video_ioctl_ops;
const struct vb2_ops fimc_is_vdo_qops;

int fimc_is_vdo_video_probe(void *data)
{
	int ret = 0;
	struct fimc_is_core *core = (struct fimc_is_core *)data;
	struct fimc_is_video *video = &core->video_vdo;

	dbg_vdiso("%s\n", __func__);

	ret = fimc_is_video_probe(video,
		data,
		FIMC_IS_VIDEO_VDISO_NAME,
		FIMC_IS_VIDEO_VDO_NUM,
		&video->lock,
		&fimc_is_vdo_video_fops,
		&fimc_is_vdo_video_ioctl_ops);

	if (ret != 0)
		dev_err(&(core->pdev->dev),
		"%s::Failed to fimc_is_video_probe()\n", __func__);

	return ret;
}

static int fimc_is_vdo_video_open(struct file *file)
{
	int ret = 0;
	u32 refcount;
	struct fimc_is_core *core = video_drvdata(file);
	struct fimc_is_video *video = &core->video_vdo;
	struct fimc_is_video_ctx *vctx = NULL;
	struct fimc_is_device_ischain *device = NULL;

	ret = open_vctx(file, video, &vctx, FRAMEMGR_ID_DIS_GRP, FRAMEMGR_ID_INVALID);
	if (ret) {
		err("open_vctx is fail(%d)", ret);
		goto p_err;
	}

	pr_info("[VDO:V:%d] %s\n", vctx->instance, __func__);

	refcount = atomic_read(&core->video_isp.refcount);
	if (refcount > FIMC_IS_MAX_NODES) {
		err("invalid ischain refcount(%d)", refcount);
		close_vctx(file, video, vctx);
		ret = -EINVAL;
		goto p_err;
	}

	device = &core->ischain[refcount - 1];

	ret = fimc_is_ischain_vdo_open(device, vctx);
	if (ret) {
		err("fimc_is_ischain_vdo_open is fail");
		close_vctx(file, video, vctx);
		goto p_err;
	}

	fimc_is_video_open(vctx,
		device,
		VIDEO_VDISO_READY_BUFFERS,
		&core->video_vdo,
		FIMC_IS_VIDEO_TYPE_OUTPUT,
		&fimc_is_vdo_qops,
		&fimc_is_ischain_vdo_ops,
		&fimc_is_ischain_sub_ops,
		core->mem.vb2->ops);

p_err:
	return ret;
}

static int fimc_is_vdo_video_close(struct file *file)
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

	pr_info("[VDO:V:%d] %s\n", vctx->instance, __func__);

	device = vctx->device;
	if (!device) {
		err("device is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	fimc_is_ischain_vdo_close(device, vctx);
	fimc_is_video_close(vctx);

	ret = close_vctx(file, video, vctx);
	if (ret < 0)
		err("close_vctx is fail(%d)", ret);

p_err:
	return ret;
}

static unsigned int fimc_is_vdo_video_poll(struct file *file,
	struct poll_table_struct *wait)
{
	u32 ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	ret = fimc_is_video_poll(file, vctx, wait);
	if (ret)
		merr("fimc_is_video_poll is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_vdo_video_mmap(struct file *file,
	struct vm_area_struct *vma)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	ret = fimc_is_video_mmap(file, vctx, vma);
	if (ret)
		merr("fimc_is_video_mmap is fail(%d)", vctx, ret);

	return ret;
}

const struct v4l2_file_operations fimc_is_vdo_video_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_is_vdo_video_open,
	.release	= fimc_is_vdo_video_close,
	.poll		= fimc_is_vdo_video_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= fimc_is_vdo_video_mmap,
};

static int fimc_is_vdo_video_querycap(struct file *file, void *fh,
					struct v4l2_capability *cap)
{
	struct fimc_is_core *core = video_drvdata(file);

	strncpy(cap->driver, core->pdev->name, sizeof(cap->driver) - 1);

	dbg_vdiso("%s(devname : %s)\n", __func__, cap->driver);
	strncpy(cap->card, core->pdev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(1, 0, 0);
	cap->capabilities = V4L2_CAP_STREAMING
				| V4L2_CAP_VIDEO_CAPTURE
				| V4L2_CAP_VIDEO_CAPTURE_MPLANE;

	return 0;
}

static int fimc_is_vdo_video_enum_fmt_mplane(struct file *file, void *priv,
	struct v4l2_fmtdesc *f)
{
	/* Todo: add enum format control code */
	return 0;
}

static int fimc_is_vdo_video_get_format_mplane(struct file *file, void *fh,
	struct v4l2_format *format)
{
	/* Todo: add get format control code */
	return 0;
}

static int fimc_is_vdo_video_set_format_mplane(struct file *file, void *fh,
	struct v4l2_format *format)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_vdo("%s\n", vctx, __func__);

	ret = fimc_is_video_set_format_mplane(file, vctx, format);
	if (ret)
		merr("fimc_is_video_set_format_mplane is fail(%d)", vctx, ret);

	dbg_vdiso("req w : %d req h : %d\n",
		vctx->q_src.framecfg.width,
		vctx->q_src.framecfg.height);

	return ret;
}

static int fimc_is_vdo_video_reqbufs(struct file *file, void *priv,
	struct v4l2_requestbuffers *buf)
{
	int ret;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_vdo("%s(buffers : %d)\n", vctx, __func__, buf->count);

	ret = fimc_is_video_reqbufs(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_reqbufs is fail(error %d)", vctx, ret);

	return ret;
}

static int fimc_is_vdo_video_querybuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_vdo("%s\n", vctx, __func__);

	ret = fimc_is_video_querybuf(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_querybuf is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_vdo_video_qbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_queue *queue;

#ifdef DBG_STREAMING
	dbg_vdiso("%s\n", __func__);
#endif

	queue = GET_SRC_QUEUE(vctx);

	if (!test_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state)) {
		merr("stream off state, can NOT qbuf", vctx);
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_video_qbuf(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_qbuf is fail(%d)", vctx, ret);

p_err:
	return ret;
}

static int fimc_is_vdo_video_dqbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

#ifdef DBG_STREAMING
	mdbgv_vdo("%s\n", vctx, __func__);
#endif

	ret = fimc_is_video_dqbuf(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_dqbuf is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_vdo_video_streamon(struct file *file, void *priv,
	enum v4l2_buf_type type)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_vdo("%s\n", vctx, __func__);

	ret = fimc_is_video_streamon(file, vctx, type);
	if (ret)
		merr("fimc_is_vdo_video_streamon is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_vdo_video_streamoff(struct file *file, void *priv,
	enum v4l2_buf_type type)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_vdo("%s\n", vctx, __func__);

	ret = fimc_is_video_streamoff(file, vctx, type);
	if (ret)
		merr("fimc_is_video_streamoff is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_vdo_video_enum_input(struct file *file, void *priv,
						struct v4l2_input *input)
{
	struct fimc_is_core *isp = video_drvdata(file);
	struct exynos5_fimc_is_sensor_info *sensor_info;

	sensor_info = isp->pdata->sensor_info[input->index];

	dbg_vdiso("index(%d) sensor(%s)\n",
		input->index, sensor_info->sensor_name);
	dbg_vdiso("pos(%d) sensor_id(%d)\n",
		sensor_info->sensor_position, sensor_info->sensor_id);
	dbg_vdiso("csi_id(%d) flite_id(%d)\n",
		sensor_info->csi_id, sensor_info->flite_id);
	dbg_vdiso("i2c_ch(%d)\n", sensor_info->i2c_channel);

	if (input->index >= FIMC_IS_MAX_CAMIF_CLIENTS)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;

	strncpy(input->name, sensor_info->sensor_name,
					FIMC_IS_MAX_SENSOR_NAME_LEN);
	return 0;
}

static int fimc_is_vdo_video_g_input(struct file *file, void *priv,
	unsigned int *input)
{
	/* Todo: add get input control code */
	return 0;
}

static int fimc_is_vdo_video_s_input(struct file *file, void *priv,
	unsigned int input)
{
	/* Todo: add set input control code */
	return 0;
}

static int fimc_is_vdo_video_s_ctrl(struct file *file, void *priv,
	struct v4l2_control *ctrl)
{
	/* Todo: add set control code */
	return 0;
}

static int fimc_is_vdo_video_g_ctrl(struct file *file, void *priv,
	struct v4l2_control *ctrl)
{
	/* Todo: add get control code */
	return 0;
}

static int fimc_is_vdo_video_g_ext_ctrl(struct file *file, void *priv,
	struct v4l2_ext_controls *ctrls)
{
	/* Todo: add get extra control code */
	return 0;
}

const struct v4l2_ioctl_ops fimc_is_vdo_video_ioctl_ops = {
	.vidioc_querycap		= fimc_is_vdo_video_querycap,
	.vidioc_enum_fmt_vid_out_mplane	= fimc_is_vdo_video_enum_fmt_mplane,
	.vidioc_g_fmt_vid_out_mplane	= fimc_is_vdo_video_get_format_mplane,
	.vidioc_s_fmt_vid_out_mplane	= fimc_is_vdo_video_set_format_mplane,
	.vidioc_reqbufs			= fimc_is_vdo_video_reqbufs,
	.vidioc_querybuf		= fimc_is_vdo_video_querybuf,
	.vidioc_qbuf			= fimc_is_vdo_video_qbuf,
	.vidioc_dqbuf			= fimc_is_vdo_video_dqbuf,
	.vidioc_streamon		= fimc_is_vdo_video_streamon,
	.vidioc_streamoff		= fimc_is_vdo_video_streamoff,
	.vidioc_enum_input		= fimc_is_vdo_video_enum_input,
	.vidioc_g_input			= fimc_is_vdo_video_g_input,
	.vidioc_s_input			= fimc_is_vdo_video_s_input,
	.vidioc_s_ctrl			= fimc_is_vdo_video_s_ctrl,
	.vidioc_g_ctrl			= fimc_is_vdo_video_g_ctrl,
	.vidioc_g_ext_ctrls		= fimc_is_vdo_video_g_ext_ctrl,
};

static int fimc_is_vdo_queue_setup(struct vb2_queue *vbq,
	const struct v4l2_format *fmt,
	unsigned int *num_buffers, unsigned int *num_planes,
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

	mdbgv_isp("%s\n", vctx, __func__);

	video = vctx->video;
	queue = GET_SRC_QUEUE(vctx);
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

static int fimc_is_vdo_buffer_prepare(struct vb2_buffer *vb)
{
	/* Todo: add buffer prepare control code */
	return 0;
}

static inline void fimc_is_vdo_wait_prepare(struct vb2_queue *vbq)
{
	fimc_is_queue_wait_prepare(vbq);
}

static inline void fimc_is_vdo_wait_finish(struct vb2_queue *vbq)
{
	fimc_is_queue_wait_finish(vbq);
}

static int fimc_is_vdo_start_streaming(struct vb2_queue *q,
	unsigned int count)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = q->drv_priv;
	struct fimc_is_queue *queue;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader;

	BUG_ON(!vctx);

	mdbgv_vdo("%s\n", vctx, __func__);

	queue = GET_SRC_QUEUE(vctx);
	device = vctx->device;
	leader = &device->group_dis.leader;

	ret = fimc_is_queue_start_streaming(queue, device, leader, vctx);
	if (ret)
		merr("fimc_is_queue_start_streaming is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_vdo_stop_streaming(struct vb2_queue *q)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = q->drv_priv;
	struct fimc_is_queue *queue;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader;

	BUG_ON(!vctx);

	mdbgv_vdo("%s\n", vctx, __func__);

	queue = GET_SRC_QUEUE(vctx);
	device = vctx->device;
	if (!device) {
		err("device is NULL");
		ret = -EINVAL;
		goto p_err;
	}
	leader = &device->group_dis.leader;

	ret = fimc_is_queue_stop_streaming(queue, device, leader, vctx);
	if (ret)
		merr("fimc_is_queue_stop_streaming is fail(%d)", vctx, ret);

p_err:
	return ret;
}

static void fimc_is_vdo_buffer_queue(struct vb2_buffer *vb)
{
	u32 index;
	struct fimc_is_video_ctx *vctx = vb->vb2_queue->drv_priv;
	struct fimc_is_queue *queue;
	struct fimc_is_video *video;
	struct fimc_is_device_ischain *device;

	BUG_ON(!vctx);
	index = vb->v4l2_buf.index;

#ifdef DBG_STREAMING
	dbg_vdiso("%s(%d)\n", __func__, index);
#endif

	queue = GET_SRC_QUEUE(vctx);
	video = vctx->video;
	device = vctx->device;

	fimc_is_queue_buffer_queue(queue, video->vb2, vb);
	fimc_is_ischain_vdo_buffer_queue(device, queue, index);
}

static int fimc_is_vdo_buffer_finish(struct vb2_buffer *vb)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = vb->vb2_queue->drv_priv;
	struct fimc_is_device_ischain *device = vctx->device;

#ifdef DBG_STREAMING
	mdbgv_vdo("%s(%d)\n", vctx, __func__, vb->v4l2_buf.index);
#endif

	ret = fimc_is_ischain_vdo_buffer_finish(device, vb->v4l2_buf.index);

	return ret;
}

const struct vb2_ops fimc_is_vdo_qops = {
	.queue_setup		= fimc_is_vdo_queue_setup,
	.buf_prepare		= fimc_is_vdo_buffer_prepare,
	.buf_queue		= fimc_is_vdo_buffer_queue,
	.buf_finish		= fimc_is_vdo_buffer_finish,
	.wait_prepare		= fimc_is_vdo_wait_prepare,
	.wait_finish		= fimc_is_vdo_wait_finish,
	.start_streaming	= fimc_is_vdo_start_streaming,
	.stop_streaming		= fimc_is_vdo_stop_streaming,
};


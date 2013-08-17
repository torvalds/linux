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

const struct v4l2_file_operations fimc_is_ss0_video_fops;
const struct v4l2_ioctl_ops fimc_is_ss0_video_ioctl_ops;
const struct vb2_ops fimc_is_ss0_qops;
#define USE_FOR_DTP
#ifdef USE_FOR_DTP
static int first_act = 0;
#endif

int fimc_is_ss0_video_probe(void *data)
{
	int ret = 0;
	struct fimc_is_core *core = (struct fimc_is_core *)data;
	struct fimc_is_video *video = &core->video_ss0;

	dbg_sensor("%s\n", __func__);

	ret = fimc_is_video_probe(video,
		data,
		FIMC_IS_VIDEO_SEN0_NAME,
		FIMC_IS_VIDEO_SS0_NUM,
		&video->lock,
		&fimc_is_ss0_video_fops,
		&fimc_is_ss0_video_ioctl_ops);

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

static int fimc_is_ss0_video_open(struct file *file)
{
	int ret = 0;
	struct fimc_is_core *core = video_drvdata(file);
	struct fimc_is_video *video = &core->video_ss0;
	struct fimc_is_video_ctx *vctx = NULL;
	struct fimc_is_device_sensor *device = NULL;

#ifdef USE_FOR_DTP
	first_act = 0;
#endif

	ret = open_vctx(file, video, &vctx, FRAMEMGR_ID_INVALID, FRAMEMGR_ID_SS0);
	if (ret) {
		err("open_vctx is fail(%d)", ret);
		goto p_err;
	}

	pr_info("[SS0:V:%d] %s\n", vctx->instance, __func__);

	device = &core->sensor[0];
	device->instance = 0;

	fimc_is_video_open(vctx,
		device,
		VIDEO_SENSOR_READY_BUFFERS,
		video,
		FIMC_IS_VIDEO_TYPE_CAPTURE,
		&fimc_is_ss0_qops,
		NULL,
		&fimc_is_ischain_sub_ops,
		core->mem.vb2->ops);

	ret = fimc_is_sensor_open(device, vctx);
	if (ret) {
		err("fimc_is_sensor_open is fail");
		close_vctx(file, video, vctx);
		goto p_err;
	}

p_err:
	return ret;
}

static int fimc_is_ss0_video_close(struct file *file)
{
	int ret = 0;
	struct fimc_is_video *video;
	struct fimc_is_video_ctx *vctx = NULL;
	struct fimc_is_device_sensor *device;

	BUG_ON(!file);

	vctx = file->private_data;
	if (!vctx) {
		err("vctx is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	video = vctx->video;
	if (!video) {
		merr("video is NULL", vctx);
		ret = -EINVAL;
		goto p_err;
	}

	pr_info("[SS0:V:%d] %s\n", vctx->instance, __func__);

	device = vctx->device;
	if (!device) {
		merr("device is NULL", vctx);
		ret = -EINVAL;
		goto p_err;
	}

	fimc_is_sensor_close(device);
	fimc_is_video_close(vctx);

	ret = close_vctx(file, video, vctx);
	if (ret < 0)
		err("close_vctx is fail(%d)", ret);

p_err:
	return ret;
}

static unsigned int fimc_is_ss0_video_poll(struct file *file,
	struct poll_table_struct *wait)
{
	u32 ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	ret = fimc_is_video_poll(file, vctx, wait);
	if (ret)
		merr("fimc_is_video_poll is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_ss0_video_mmap(struct file *file,
	struct vm_area_struct *vma)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	ret = fimc_is_video_mmap(file, vctx, vma);
	if (ret)
		merr("fimc_is_video_mmap is fail(%d)", vctx, ret);

	return ret;
}

const struct v4l2_file_operations fimc_is_ss0_video_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_is_ss0_video_open,
	.release	= fimc_is_ss0_video_close,
	.poll		= fimc_is_ss0_video_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= fimc_is_ss0_video_mmap,
};

/*
 * =============================================================================
 * Video Ioctl Opertation
 * =============================================================================
 */

static int fimc_is_ss0_video_querycap(struct file *file, void *fh,
					struct v4l2_capability *cap)
{
	/* Todo : add to query capability code */
	return 0;
}

static int fimc_is_ss0_video_enum_fmt_mplane(struct file *file, void *priv,
				    struct v4l2_fmtdesc *f)
{
	/* Todo : add to enumerate format code */
	return 0;
}

static int fimc_is_ss0_video_get_format_mplane(struct file *file, void *fh,
						struct v4l2_format *format)
{
	/* Todo : add to get format code */
	return 0;
}

static int fimc_is_ss0_video_set_format_mplane(struct file *file, void *fh,
	struct v4l2_format *format)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_queue *queue;
	struct fimc_is_device_sensor *device;

	BUG_ON(!vctx);

	mdbgv_ss0("%s\n", vctx, __func__);

	queue = GET_DST_QUEUE(vctx);
	device = vctx->device;

	ret = fimc_is_video_set_format_mplane(file, vctx, format);
	if (ret)
		merr("fimc_is_video_set_format_mplane is fail(%d)", vctx, ret);

	fimc_is_sensor_s_format(device,
		queue->framecfg.width,
		queue->framecfg.height);

	return ret;
}

static int fimc_is_ss0_video_cropcap(struct file *file, void *fh,
	struct v4l2_cropcap *cropcap)
{
	/* Todo : add to crop capability code */
	return 0;
}

static int fimc_is_ss0_video_get_crop(struct file *file, void *fh,
	struct v4l2_crop *crop)
{
	/* Todo : add to get crop control code */
	return 0;
}

static int fimc_is_ss0_video_set_crop(struct file *file, void *fh,
	struct v4l2_crop *crop)
{
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_device_sensor *sensor;

	BUG_ON(!vctx);

	mdbgv_ss0("%s\n", vctx, __func__);

	sensor = vctx->device;
	BUG_ON(!sensor);

	fimc_is_sensor_s_format(sensor, crop->c.width, crop->c.height);

	return 0;
}

static int fimc_is_ss0_video_reqbufs(struct file *file, void *priv,
	struct v4l2_requestbuffers *buf)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	BUG_ON(!vctx);

	dbg_sensor("%s(buffers : %d)\n", __func__, buf->count);

	ret = fimc_is_video_reqbufs(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_reqbufs is fail(error %d)", vctx, ret);

	return ret;
}

static int fimc_is_ss0_video_querybuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_ss0("%s\n", vctx, __func__);

	ret = fimc_is_video_querybuf(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_querybuf is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_ss0_video_qbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

#ifdef DBG_STREAMING
	/*dbg_sensor("%s\n", __func__);*/
#endif

	ret = fimc_is_video_qbuf(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_qbuf is fail(%d)", vctx, ret);

	return ret;
}

#ifdef USE_FOR_DTP
void fimc_is_ss0_video_force_dqbuf(unsigned long data) {
	struct vb2_queue *vbq = (struct vb2_queue *)data;
	int i;

	for(i=0; i<VIDEO_MAX_FRAME; i++) {
		if (vbq->bufs[i] &&
			vbq->bufs[i]->state == VB2_BUF_STATE_ACTIVE) {
			vb2_buffer_done(vbq->bufs[i], VB2_BUF_STATE_ERROR);
		}
	}
}
#endif

static int fimc_is_ss0_video_dqbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	bool blocking;
	struct fimc_is_video_ctx *vctx = file->private_data;

#ifdef USE_FOR_DTP
	static struct timer_list        timer;

	if (!first_act) {
		setup_timer(&timer, fimc_is_ss0_video_force_dqbuf, (unsigned long)vctx->q_dst.vbq);
		mod_timer(&timer, jiffies +  msecs_to_jiffies(4000));
	}
#endif

#ifdef DBG_STREAMING
	mdbgv_ss0("%s\n", vctx, __func__);
#endif

	ret = fimc_is_video_dqbuf(file, vctx, buf);

#ifdef USE_FOR_DTP
	if (!first_act) {
		first_act = 1;
 	del_timer(&timer);
 }
#endif

	if (ret) {
		blocking = file->f_flags & O_NONBLOCK;
		if (!blocking || (ret != -EAGAIN))
			merr("fimc_is_video_dqbuf is fail(%d)", vctx, ret);
	}

	return ret;
}

static int fimc_is_ss0_video_streamon(struct file *file, void *priv,
	enum v4l2_buf_type type)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_ss0("%s\n", vctx, __func__);

	ret = fimc_is_video_streamon(file, vctx, type);
	if (ret)
		merr("fimc_is_video_streamon is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_ss0_video_streamoff(struct file *file, void *priv,
	enum v4l2_buf_type type)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_ss0("%s\n", vctx, __func__);

	ret = fimc_is_video_streamoff(file, vctx, type);
	if (ret)
		merr("fimc_is_video_streamoff is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_ss0_video_enum_input(struct file *file, void *priv,
	struct v4l2_input *input)
{
	struct fimc_is_core *isp = video_drvdata(file);
	struct exynos5_fimc_is_sensor_info *sensor_info;

	sensor_info = isp->pdata->sensor_info[input->index];

	dbg_sensor("index(%d) sensor(%s)\n",
		input->index, sensor_info->sensor_name);
	dbg_sensor("pos(%d) sensor_id(%d)\n",
		sensor_info->sensor_position, sensor_info->sensor_id);
	dbg_sensor("csi_id(%d) flite_id(%d)\n",
		sensor_info->csi_id, sensor_info->flite_id);
	dbg_sensor("i2c_ch(%d)\n", sensor_info->i2c_channel);

	if (input->index >= FIMC_IS_MAX_CAMIF_CLIENTS)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;

	strncpy(input->name, sensor_info->sensor_name,
					FIMC_IS_MAX_SENSOR_NAME_LEN);
	return 0;
}

static int fimc_is_ss0_video_g_input(struct file *file, void *priv,
	unsigned int *input)
{
	/* Todo: add to get input control code */
	return 0;
}

static int fimc_is_ss0_video_s_input(struct file *file, void *priv,
	unsigned int input)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_device_sensor *device;
	struct fimc_is_framemgr *framemgr;

	BUG_ON(!vctx);

	mdbgv_ss0("%s(input : %d)\n", vctx, __func__, input);

	device = vctx->device;
	framemgr = GET_DST_FRAMEMGR(vctx);

	fimc_is_sensor_s_active_sensor(device, vctx, framemgr, input);

	return ret;
}

static int fimc_is_ss0_video_s_ctrl(struct file *file, void *priv,
	struct v4l2_control *ctrl)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_device_sensor *sensor = vctx->device;

	switch (ctrl->id) {
	case V4L2_CID_IS_S_STREAM:
		if (ctrl->value == IS_ENABLE_STREAM)
			ret = fimc_is_sensor_front_start(sensor);
		else
			ret = fimc_is_sensor_front_stop(sensor);
		break;
	}

	return ret;
}

static int fimc_is_ss0_video_g_parm(struct file *file, void *priv,
	struct v4l2_streamparm *parm)
{
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_device_sensor *sensor = vctx->device;
	struct v4l2_captureparm *cp = &parm->parm.capture;
	struct v4l2_fract *tfp = &cp->timeperframe;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	cp->capability |= V4L2_CAP_TIMEPERFRAME;
	tfp->numerator = 1;
	tfp->denominator = sensor->framerate;

	return 0;
}

static int fimc_is_ss0_video_s_parm(struct file *file, void *priv,
	struct v4l2_streamparm *parm)
{
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_device_sensor *sensor = vctx->device;
	struct v4l2_captureparm *cp = &parm->parm.capture;
	struct v4l2_fract *tfp = &cp->timeperframe;
	unsigned int framerate;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	if (!tfp->numerator)
		return -EINVAL;

	framerate = tfp->denominator / tfp->numerator;
	mdbgv_ss0("%s(framerate : %d)\n", vctx, __func__, framerate);

	if (framerate > sensor->active_sensor->max_framerate)
		return -EINVAL;

	sensor->framerate = framerate;

	pr_info("# sensor framerate: %dfps\n", framerate);

	return 0;
}

const struct v4l2_ioctl_ops fimc_is_ss0_video_ioctl_ops = {
	.vidioc_querycap		= fimc_is_ss0_video_querycap,
	.vidioc_enum_fmt_vid_cap_mplane	= fimc_is_ss0_video_enum_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= fimc_is_ss0_video_get_format_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= fimc_is_ss0_video_set_format_mplane,
	.vidioc_cropcap			= fimc_is_ss0_video_cropcap,
	.vidioc_g_crop			= fimc_is_ss0_video_get_crop,
	.vidioc_s_crop			= fimc_is_ss0_video_set_crop,
	.vidioc_reqbufs			= fimc_is_ss0_video_reqbufs,
	.vidioc_querybuf		= fimc_is_ss0_video_querybuf,
	.vidioc_qbuf			= fimc_is_ss0_video_qbuf,
	.vidioc_dqbuf			= fimc_is_ss0_video_dqbuf,
	.vidioc_streamon		= fimc_is_ss0_video_streamon,
	.vidioc_streamoff		= fimc_is_ss0_video_streamoff,
	.vidioc_enum_input		= fimc_is_ss0_video_enum_input,
	.vidioc_g_input			= fimc_is_ss0_video_g_input,
	.vidioc_s_input			= fimc_is_ss0_video_s_input,
	.vidioc_s_ctrl			= fimc_is_ss0_video_s_ctrl,
	.vidioc_g_parm			= fimc_is_ss0_video_g_parm,
	.vidioc_s_parm			= fimc_is_ss0_video_s_parm,
};

static int fimc_is_ss0_queue_setup(struct vb2_queue *vbq,
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

	mdbgv_ss0("%s\n", vctx, __func__);

	video = vctx->video;
	queue = GET_DST_QUEUE(vctx);
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

static int fimc_is_ss0_buffer_prepare(struct vb2_buffer *vb)
{
	return 0;
}

static inline void fimc_is_ss0_wait_prepare(struct vb2_queue *vbq)
{
	fimc_is_queue_wait_prepare(vbq);
}

static inline void fimc_is_ss0_wait_finish(struct vb2_queue *vbq)
{
	fimc_is_queue_wait_finish(vbq);
}

static int fimc_is_ss0_start_streaming(struct vb2_queue *q,
	unsigned int count)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = q->drv_priv;
	struct fimc_is_queue *queue;
	struct fimc_is_device_sensor *device;

	BUG_ON(!vctx);

	mdbgv_ss0("%s\n", vctx, __func__);

	queue = GET_DST_QUEUE(vctx);
	device = vctx->device;

	if (!test_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state) &&
		test_bit(FIMC_IS_QUEUE_BUFFER_READY, &queue->state)) {
		set_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state);
		fimc_is_sensor_back_start(device, vctx);
	} else {
		err("already stream on or buffer is not ready(%ld)",
			queue->state);
		clear_bit(FIMC_IS_QUEUE_BUFFER_READY, &queue->state);
		clear_bit(FIMC_IS_QUEUE_BUFFER_PREPARED, &queue->state);
		fimc_is_sensor_back_stop(device);
		ret = -EINVAL;
	}

	return 0;
}

static int fimc_is_ss0_stop_streaming(struct vb2_queue *q)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = q->drv_priv;
	struct fimc_is_queue *queue;
	struct fimc_is_device_sensor *device;

	BUG_ON(!vctx);

	mdbgv_ss0("%s\n", vctx, __func__);

	queue = GET_DST_QUEUE(vctx);
	device = vctx->device;

	if (test_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state)) {
		clear_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state);
		clear_bit(FIMC_IS_QUEUE_BUFFER_READY, &queue->state);
		clear_bit(FIMC_IS_QUEUE_BUFFER_PREPARED, &queue->state);
		ret = fimc_is_sensor_back_stop(device);
	} else {
		err("already stream off");
		ret = -EINVAL;
	}

	return ret;
}

static void fimc_is_ss0_buffer_queue(struct vb2_buffer *vb)
{
	struct fimc_is_video_ctx *vctx = vb->vb2_queue->drv_priv;
	struct fimc_is_queue *queue = &vctx->q_dst;
	struct fimc_is_video *video = vctx->video;
	struct fimc_is_device_sensor *sensor = vctx->device;

#ifdef DBG_STREAMING
	dbg_sensor("%s(%d)\n", __func__, vb->v4l2_buf.index);
#endif

	fimc_is_queue_buffer_queue(queue, video->vb2, vb);
	fimc_is_sensor_buffer_queue(sensor, vb->v4l2_buf.index);
}

static int fimc_is_ss0_buffer_finish(struct vb2_buffer *vb)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = vb->vb2_queue->drv_priv;
	struct fimc_is_device_sensor *sensor = vctx->device;

#ifdef DBG_STREAMING
	dbg_sensor("%s(%d)\n", __func__, vb->v4l2_buf.index);
#endif

	ret = fimc_is_sensor_buffer_finish(
		sensor,
		vb->v4l2_buf.index);

	return 0;
}

const struct vb2_ops fimc_is_ss0_qops = {
	.queue_setup		= fimc_is_ss0_queue_setup,
	.buf_prepare		= fimc_is_ss0_buffer_prepare,
	.buf_queue		= fimc_is_ss0_buffer_queue,
	.buf_finish		= fimc_is_ss0_buffer_finish,
	.wait_prepare		= fimc_is_ss0_wait_prepare,
	.wait_finish		= fimc_is_ss0_wait_finish,
	.start_streaming	= fimc_is_ss0_start_streaming,
	.stop_streaming		= fimc_is_ss0_stop_streaming,
};

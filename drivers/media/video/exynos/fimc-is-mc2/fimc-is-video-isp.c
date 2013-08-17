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
#include <linux/pm_qos.h>
#include <linux/bug.h>

#include "fimc-is-core.h"
#include "fimc-is-param.h"
#include "fimc-is-cmd.h"
#include "fimc-is-regs.h"
#include "fimc-is-err.h"
#include "fimc-is-video.h"
#include "fimc-is-metadata.h"

const struct v4l2_file_operations fimc_is_isp_video_fops;
const struct v4l2_ioctl_ops fimc_is_isp_video_ioctl_ops;
const struct vb2_ops fimc_is_isp_qops;

extern struct fimc_is_from_info		*sysfs_finfo;
extern struct fimc_is_from_info		*sysfs_pinfo;
extern bool is_dumped_fw_loading_needed;



int fimc_is_isp_video_probe(void *data)
{
	int ret = 0;
	struct fimc_is_core *core = (struct fimc_is_core *)data;
	struct fimc_is_video *video = &core->video_isp;

	dbg_isp("%s\n", __func__);

	ret = fimc_is_video_probe(video,
		data,
		FIMC_IS_VIDEO_ISP_NAME,
		FIMC_IS_VIDEO_ISP_NUM,
		&video->lock,
		&fimc_is_isp_video_fops,
		&fimc_is_isp_video_ioctl_ops);

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

static int fimc_is_isp_video_open(struct file *file)
{
	int ret = 0;
	struct fimc_is_core *core = video_drvdata(file);
	struct fimc_is_video *video = &core->video_isp;
	struct fimc_is_video_ctx *vctx = NULL;
	struct fimc_is_device_ischain *device = NULL;

	ret = open_vctx(file, video, &vctx, FRAMEMGR_ID_ISP_GRP, FRAMEMGR_ID_INVALID);
	if (ret) {
		err("open_vctx is fail(%d)", ret);
		goto p_err;
	}

	pr_info("[ISP:V:%d] %s\n", vctx->instance, __func__);

	device = &core->ischain[vctx->instance];

	/* for resource manager */
	if (vctx->instance == 0) {
		core->debug_cnt = 0;
		memset(&core->clock.msk_cnt[0], 0, sizeof(int[GROUP_ID_MAX]));
		core->clock.msk_state = 0;
		core->clock.state_3a0 = 0;
		core->clock.dvfs_level = DVFS_L0;
		core->clock.dvfs_skipcnt = 0;
		core->clock.dvfs_state = 0;
	}

	ret = fimc_is_ischain_open(device, vctx, &core->minfo);
	if (ret) {
		err("fimc_is_ischain_open is fail");
		close_vctx(file, video, vctx);
		goto p_err;
	}

	fimc_is_video_open(vctx,
		device,
		VIDEO_ISP_READY_BUFFERS,
		&core->video_isp,
		FIMC_IS_VIDEO_TYPE_OUTPUT,
		&fimc_is_isp_qops,
		&fimc_is_ischain_isp_ops,
		NULL,
		core->mem.vb2->ops);

p_err:
	return ret;
}

static int fimc_is_isp_video_close(struct file *file)
{
	int ret = 0;
	struct fimc_is_video *video;
	struct fimc_is_video_ctx *vctx = NULL;
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

	pr_info("[ISP:V:%d] %s\n", vctx->instance, __func__);

	device = vctx->device;
	if (!device) {
		err("device is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	if (pm_qos_request_active(&device->user_qos))
		pm_qos_remove_request(&device->user_qos);

	fimc_is_ischain_close(device, vctx);
	fimc_is_video_close(vctx);

	ret = close_vctx(file, video, vctx);
	if (ret < 0)
		err("close_vctx is fail(%d)", ret);

p_err:
	return ret;
}

static unsigned int fimc_is_isp_video_poll(struct file *file,
	struct poll_table_struct *wait)
{
	u32 ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	ret = fimc_is_video_poll(file, vctx, wait);
	if (ret)
		merr("fimc_is_video_poll is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_isp_video_mmap(struct file *file,
	struct vm_area_struct *vma)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	ret = fimc_is_video_mmap(file, vctx, vma);
	if (ret)
		merr("fimc_is_video_mmap is fail(%d)", vctx, ret);

	return ret;
}

const struct v4l2_file_operations fimc_is_isp_video_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_is_isp_video_open,
	.release	= fimc_is_isp_video_close,
	.poll		= fimc_is_isp_video_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= fimc_is_isp_video_mmap,
};

/*
 * =============================================================================
 * Video Ioctl Opertation
 * =============================================================================
 */

static int fimc_is_isp_video_querycap(struct file *file, void *fh,
					struct v4l2_capability *cap)
{
	struct fimc_is_core *isp = video_drvdata(file);

	strncpy(cap->driver, isp->pdev->name, sizeof(cap->driver) - 1);

	dbg_isp("%s(devname : %s)\n", __func__, cap->driver);
	strncpy(cap->card, isp->pdev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(1, 0, 0);
	cap->capabilities = V4L2_CAP_STREAMING
				| V4L2_CAP_VIDEO_CAPTURE
				| V4L2_CAP_VIDEO_CAPTURE_MPLANE;

	return 0;
}

static int fimc_is_isp_video_enum_fmt_mplane(struct file *file, void *priv,
	struct v4l2_fmtdesc *f)
{
	dbg_isp("%s\n", __func__);
	return 0;
}

static int fimc_is_isp_video_get_format_mplane(struct file *file, void *fh,
	struct v4l2_format *format)
{
	dbg_isp("%s\n", __func__);
	return 0;
}

static int fimc_is_isp_video_set_format_mplane(struct file *file, void *fh,
	struct v4l2_format *format)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_queue *queue;
	struct fimc_is_device_ischain *device;

	BUG_ON(!vctx);

	mdbgv_isp("%s\n", vctx, __func__);

	queue = GET_VCTX_QUEUE(vctx, format);
	device = vctx->device;

	ret = fimc_is_video_set_format_mplane(file, vctx, format);
	if (ret)
		merr("fimc_is_video_set_format_mplane is fail(%d)", vctx, ret);

	fimc_is_ischain_isp_s_format(device,
		queue->framecfg.width,
		queue->framecfg.height);

	return ret;
}

static int fimc_is_isp_video_cropcap(struct file *file, void *fh,
						struct v4l2_cropcap *cropcap)
{
	dbg_isp("%s\n", __func__);
	return 0;
}

static int fimc_is_isp_video_get_crop(struct file *file, void *fh,
						struct v4l2_crop *crop)
{
	dbg_isp("%s\n", __func__);
	return 0;
}

static int fimc_is_isp_video_set_crop(struct file *file, void *fh,
	struct v4l2_crop *crop)
{
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_device_ischain *ischain;

	BUG_ON(!vctx);

	mdbgv_isp("%s\n", vctx, __func__);

	ischain = vctx->device;
	BUG_ON(!ischain);

	fimc_is_ischain_isp_s_format(ischain,
		crop->c.width, crop->c.height);

	return 0;
}

static int fimc_is_isp_video_reqbufs(struct file *file, void *priv,
	struct v4l2_requestbuffers *buf)
{
	int ret;
	struct fimc_is_video_ctx *vctx = file->private_data;

	BUG_ON(!vctx);

	mdbgv_isp("%s(buffers : %d)\n", vctx, __func__, buf->count);

	ret = fimc_is_video_reqbufs(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_reqbufs is fail(error %d)", vctx, ret);

	return ret;
}

static int fimc_is_isp_video_querybuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_isp("%s\n", vctx, __func__);

	ret = fimc_is_video_querybuf(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_querybuf is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_isp_video_qbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_queue *queue;

	BUG_ON(!vctx);

#ifdef DBG_STREAMING
	mdbgv_isp("%s(index : %d)\n", vctx, __func__, buf->index);
#endif

	queue = GET_VCTX_QUEUE(vctx, buf);

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

static int fimc_is_isp_video_dqbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

#ifdef DBG_STREAMING
	mdbgv_isp("%s\n", vctx, __func__);
#endif

	ret = fimc_is_video_dqbuf(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_dqbuf is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_isp_video_streamon(struct file *file, void *priv,
	enum v4l2_buf_type type)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	dbg_isp("%s\n", __func__);

	ret = fimc_is_video_streamon(file, vctx, type);
	if (ret)
		merr("fimc_is_video_streamon is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_isp_video_streamoff(struct file *file, void *priv,
	enum v4l2_buf_type type)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	dbg_isp("%s\n", __func__);

	ret = fimc_is_video_streamoff(file, vctx, type);
	if (ret)
		merr("fimc_is_video_streamoff is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_isp_video_enum_input(struct file *file, void *priv,
						struct v4l2_input *input)
{
	struct fimc_is_core *isp = video_drvdata(file);
	struct exynos5_fimc_is_sensor_info *sensor_info;

	sensor_info = isp->pdata->sensor_info[input->index];

	dbg_isp("index(%d) sensor(%s)\n",
		input->index, sensor_info->sensor_name);
	dbg_isp("pos(%d) sensor_id(%d)\n",
		sensor_info->sensor_position, sensor_info->sensor_id);
	dbg_isp("csi_id(%d) flite_id(%d)\n",
		sensor_info->csi_id, sensor_info->flite_id);
	dbg_isp("i2c_ch(%d)\n", sensor_info->i2c_channel);

	if (input->index >= FIMC_IS_MAX_CAMIF_CLIENTS)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;

	strncpy(input->name, sensor_info->sensor_name,
					FIMC_IS_MAX_SENSOR_NAME_LEN);
	return 0;
}

static int fimc_is_isp_video_g_input(struct file *file, void *priv,
	unsigned int *input)
{
	/* Todo : add to get input control code */
	return 0;
}

static int fimc_is_isp_video_s_input(struct file *file, void *priv,
	unsigned int input)
{
	int ret = 0;
	u32 vindex, module;
	bool reprocessing;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_core *core;
	struct fimc_is_device_ischain *device;
	struct fimc_is_device_sensor *sensor;

	BUG_ON(!vctx);

	mdbgv_isp("%s(input : %08X)\n", vctx, __func__, input);

	core = vctx->video->core;
	device = vctx->device;
	module = input & MODULE_MASK;
	vindex = (input & SSX_VINDEX_MASK) >> SSX_VINDEX_SHIFT;
	reprocessing = input & REPROCESSING_MASK;
	input = input & ~SSX_VINDEX_MASK;
	sensor = NULL;

	if (vindex == FIMC_IS_VIDEO_SS0_NUM) {
		sensor = &core->sensor[0];
		pr_info("[ISP:V:%d] <-> [SEN:V:0]\n", vctx->instance);
	} else if (vindex == FIMC_IS_VIDEO_SS1_NUM) {
		sensor = &core->sensor[1];
		pr_info("[ISP:V:%d] <-> [SEN:V:1]\n", vctx->instance);
	} else {
		sensor = NULL;
		merr("sensor is not matched", vctx);
		ret = -EINVAL;
		goto p_err;
	}

	if (!test_bit(FIMC_IS_SENSOR_OPEN, &sensor->state)) {
		merr("sensor%d is not opened", vctx, vindex);
		ret = -EINVAL;
		goto p_err;
	}

	device->instance_sensor = sensor->instance;
	device->sensor = sensor;
	if (!reprocessing)
		sensor->ischain = device;

	ret = fimc_is_ischain_init(device, input,
		sensor->enum_sensor[module].i2c_ch,
		&sensor->enum_sensor[module].ext,
		sensor->enum_sensor[module].setfile_name);
	if (ret)
		merr("fimc_is_device_init is fail\n", vctx);

p_err:
	return ret;
}

static int fimc_is_isp_video_s_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	int ret = 0;
	int i2c_clk;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_device_ischain *device;
	struct fimc_is_core *core;

	BUG_ON(!vctx);

	dbg_isp("%s\n", __func__);

	device = vctx->device;
	if (!device) {
		merr("device is NULL", vctx);
		ret = -EINVAL;
		goto p_err;
	}

	core = vctx->video->core;
	if (!core) {
		merr("core is NULL", vctx);
		ret = -EINVAL;
		goto p_err;
	}
	if (core->clock.dvfs_level == DVFS_L0)
		i2c_clk = I2C_L0;
	else
		i2c_clk = I2C_L1;

	switch (ctrl->id) {
	case V4L2_CID_IS_G_CAPABILITY:
		ret = fimc_is_ischain_g_capability(device, ctrl->value);
		dbg_isp("V4L2_CID_IS_G_CAPABILITY : %X\n", ctrl->value);
		break;
	case V4L2_CID_IS_FORCE_DONE:
		set_bit(FIMC_IS_GROUP_REQUEST_FSTOP, &device->group_isp.state);
		break;
	case V4L2_CID_IS_DVFS_LOCK:
		ret = fimc_is_itf_i2c_lock(device, I2C_L0, true);
		if (ret) {
			err("fimc_is_itf_i2_clock fail\n");
			break;
		}
		pm_qos_add_request(&device->user_qos, PM_QOS_DEVICE_THROUGHPUT,
					ctrl->value);
		ret = fimc_is_itf_i2c_lock(device, I2C_L0, false);
		if (ret) {
			err("fimc_is_itf_i2c_unlock fail\n");
			break;
		}
		dbg_isp("V4L2_CID_IS_DVFS_LOCK : %d\n", ctrl->value);
		break;
	case V4L2_CID_IS_DVFS_UNLOCK:
		ret = fimc_is_itf_i2c_lock(device, i2c_clk, true);
		if (ret) {
			err("fimc_is_itf_i2_clock fail\n");
			break;
		}
		pm_qos_remove_request(&device->user_qos);
		ret = fimc_is_itf_i2c_lock(device, i2c_clk, false);
		if (ret) {
			err("fimc_is_itf_i2c_unlock fail\n");
			break;
		}
		dbg_isp("V4L2_CID_IS_DVFS_UNLOCK : %d I2C(%d)\n", ctrl->value, i2c_clk);
		break;
	default:
		err("unsupported ioctl(%d)\n", ctrl->id);
		ret = -EINVAL;
		break;
	}

p_err:
	return ret;
}

static int fimc_is_isp_video_g_ctrl(struct file *file, void *priv,
	struct v4l2_control *ctrl)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_device_ischain *ischain = vctx->device;

	dbg_isp("%s\n", __func__);

	switch (ctrl->id) {
	case V4L2_CID_IS_BDS_WIDTH:
		ctrl->value = ischain->chain0_width;
		break;
	case V4L2_CID_IS_BDS_HEIGHT:
		ctrl->value = ischain->chain0_height;
		break;
	default:
		err("unsupported ioctl(%d)\n", ctrl->id);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int fimc_is_isp_video_g_ext_ctrl(struct file *file, void *priv,
	struct v4l2_ext_controls *ctrls)
{
	int ret = 0;
	struct v4l2_ext_control *ctrl;

	dbg_isp("%s\n", __func__);


	ctrl = ctrls->controls;

	switch (ctrl->id) {
	case V4L2_CID_CAM_SENSOR_FW_VER:
		break;

	default:
		err("unsupported ioctl(%d)\n", ctrl->id);
		ret = -EINVAL;
		break;
	}

	return ret;
}

const struct v4l2_ioctl_ops fimc_is_isp_video_ioctl_ops = {
	.vidioc_querycap		= fimc_is_isp_video_querycap,
	.vidioc_enum_fmt_vid_out_mplane	= fimc_is_isp_video_enum_fmt_mplane,
	.vidioc_g_fmt_vid_out_mplane	= fimc_is_isp_video_get_format_mplane,
	.vidioc_s_fmt_vid_out_mplane	= fimc_is_isp_video_set_format_mplane,
	.vidioc_cropcap			= fimc_is_isp_video_cropcap,
	.vidioc_g_crop			= fimc_is_isp_video_get_crop,
	.vidioc_s_crop			= fimc_is_isp_video_set_crop,
	.vidioc_reqbufs			= fimc_is_isp_video_reqbufs,
	.vidioc_querybuf		= fimc_is_isp_video_querybuf,
	.vidioc_qbuf			= fimc_is_isp_video_qbuf,
	.vidioc_dqbuf			= fimc_is_isp_video_dqbuf,
	.vidioc_streamon		= fimc_is_isp_video_streamon,
	.vidioc_streamoff		= fimc_is_isp_video_streamoff,
	.vidioc_enum_input		= fimc_is_isp_video_enum_input,
	.vidioc_g_input			= fimc_is_isp_video_g_input,
	.vidioc_s_input			= fimc_is_isp_video_s_input,
	.vidioc_s_ctrl			= fimc_is_isp_video_s_ctrl,
	.vidioc_g_ctrl			= fimc_is_isp_video_g_ctrl,
	.vidioc_g_ext_ctrls		= fimc_is_isp_video_g_ext_ctrl,
};

static int fimc_is_isp_queue_setup(struct vb2_queue *vbq,
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

static int fimc_is_isp_buffer_prepare(struct vb2_buffer *vb)
{
	/*dbg_isp("%s\n", __func__);*/
	return 0;
}

static inline void fimc_is_isp_wait_prepare(struct vb2_queue *vbq)
{
	fimc_is_queue_wait_prepare(vbq);
}

static inline void fimc_is_isp_wait_finish(struct vb2_queue *vbq)
{
	fimc_is_queue_wait_finish(vbq);
}

static int fimc_is_isp_start_streaming(struct vb2_queue *vbq,
	unsigned int count)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = vbq->drv_priv;
	struct fimc_is_queue *queue;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader;

	BUG_ON(!vctx);

	mdbgv_isp("%s\n", vctx, __func__);

	queue = GET_SRC_QUEUE(vctx);
	device = vctx->device;
	leader = &device->group_isp.leader;

	ret = fimc_is_queue_start_streaming(queue, device, leader, vctx);
	if (ret)
		merr("fimc_is_queue_start_streaming is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_isp_stop_streaming(struct vb2_queue *q)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = q->drv_priv;
	struct fimc_is_queue *queue;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader;

	BUG_ON(!vctx);

	mdbgv_isp("%s\n", vctx, __func__);

	queue = GET_SRC_QUEUE(vctx);
	device = vctx->device;
	if (!device) {
		err("device is NULL");
		ret = -EINVAL;
		goto p_err;
	}
	leader = &device->group_isp.leader;

	ret = fimc_is_queue_stop_streaming(queue, device, leader, vctx);
	if (ret)
		merr("fimc_is_queue_stop_streaming is fail(%d)", vctx, ret);

p_err:
	return ret;
}

static void fimc_is_isp_buffer_queue(struct vb2_buffer *vb)
{
	u32 index;
	struct fimc_is_video_ctx *vctx = vb->vb2_queue->drv_priv;
	struct fimc_is_queue *queue;
	struct fimc_is_video *video;
	struct fimc_is_device_ischain *device;

	BUG_ON(!vctx);
	index = vb->v4l2_buf.index;

#ifdef DBG_STREAMING
	mdbgv_isp("%s(%d)\n", vctx, __func__, index);
#endif

	queue = GET_SRC_QUEUE(vctx);
	video = vctx->video;
	device = vctx->device;

	fimc_is_queue_buffer_queue(queue, video->vb2, vb);
	fimc_is_ischain_isp_buffer_queue(device, queue, index);
}

static int fimc_is_isp_buffer_finish(struct vb2_buffer *vb)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = vb->vb2_queue->drv_priv;
	struct fimc_is_device_ischain *device = vctx->device;

#ifdef DBG_STREAMING
	mdbgv_isp("%s(%d)\n", vctx, __func__, vb->v4l2_buf.index);
#endif

	ret = fimc_is_ischain_isp_buffer_finish(device, vb->v4l2_buf.index);

	return ret;
}

const struct vb2_ops fimc_is_isp_qops = {
	.queue_setup		= fimc_is_isp_queue_setup,
	.buf_prepare		= fimc_is_isp_buffer_prepare,
	.buf_queue		= fimc_is_isp_buffer_queue,
	.buf_finish		= fimc_is_isp_buffer_finish,
	.wait_prepare		= fimc_is_isp_wait_prepare,
	.wait_finish		= fimc_is_isp_wait_finish,
	.start_streaming	= fimc_is_isp_start_streaming,
	.stop_streaming		= fimc_is_isp_stop_streaming,
};

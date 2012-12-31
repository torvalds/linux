/*
 * Samsung Exynos4 SoC series FIMC-IS slave interface driver
 *
 * v4l2 subdev driver interface
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 * Contact: Younghwan Joo, <yhwan.joo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/memory.h>
#include <linux/workqueue.h>

#include <linux/videodev2.h>
#include <linux/videodev2_samsung.h>
#include <media/videobuf2-core.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-mediabus.h>

#include "fimc-is-core.h"
#include "fimc-is-regs.h"
#include "fimc-is-param.h"
#include "fimc-is-cmd.h"

#if defined(CONFIG_VIDEO_EXYNOS_FIMC_IS_BAYER)
#define V4L2_PIX_FMT_SGRBG14 v4l2_fourcc('B', 'A', '1', '4')

static struct fimc_is_fmt fimc_is_formats[] = {
	{
		.name	= "Bayer10",
		.fourcc	= V4L2_PIX_FMT_SGRBG10,
		.flags = 1,
	}, {
		.name	= "Bayer12",
		.fourcc	= V4L2_PIX_FMT_SGRBG12,
		.flags = 1,
	}, {
		.name	= "Bayer14",
		.fourcc	= V4L2_PIX_FMT_SGRBG14,
		.flags = 1,
	},
};

/************************************************************************/
/* video file opertation						*/
/************************************************************************/
static int fimc_is_isp_open(struct file *file)
{
	struct fimc_is_dev *is_dev;

	dbg("FIMC_IS_IS_OPEN\n");
	is_dev = video_drvdata(file);
	file->private_data = &is_dev->video[FIMC_IS_VIDEO_NUM_BAYER];
	dbg("pid: %d, state: 0x%lx", task_pid_nr(current), is_dev->power);

	is_dev->vb_state = FIMC_IS_STATE_IDLE;
	/* Check FIMC-IS ready */
	if (!test_bit(IS_ST_INIT_DONE, &is_dev->state))
		return -EINVAL;

	set_bit(FIMC_IS_STATE_READY, &is_dev->vb_state);
	return 0;
}

static int fimc_is_isp_close(struct file *file)
{
	struct fimc_is_dev *is_dev;

	dbg("(FIMC-IS cap_fops)\n");
	is_dev = video_drvdata(file);

	dbg("pid: %d, state: 0x%lx", task_pid_nr(current), is_dev->power);

	/* If FIMC-ISP dma output is still running,
				must be stopped before closing */
	vb2_queue_release(&is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].vbq);
	return 0;
}

static unsigned int fimc_is_isp_poll(struct file *file,
					struct poll_table_struct *wait)
{
	struct fimc_is_dev *is_dev;

	is_dev = video_drvdata(file);
	return vb2_poll(&is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].vbq,
								file, wait);
}

static int fimc_is_isp_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct fimc_is_dev *is_dev;

	dbg("(FIMC-IS cap_fops)\n");
	is_dev = video_drvdata(file);
	return vb2_mmap(&is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].vbq, vma);
}

/* video device file operations */
const struct v4l2_file_operations fimc_is_isp_video_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_is_isp_open,
	.release	= fimc_is_isp_close,
	.poll		= fimc_is_isp_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= fimc_is_isp_mmap,
};

/************************************************************************/
/* video ioctl operation						*/
/************************************************************************/
static int fimc_is_isp_video_querycap(struct file *file, void *priv,
					struct v4l2_capability *cap)
{
	struct fimc_is_dev *is_dev = video_drvdata(file);

	strncpy(cap->driver, is_dev->pdev->name, sizeof(cap->driver) - 1);
	strncpy(cap->card, is_dev->pdev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(1, 0, 0);
	cap->capabilities = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE
						| V4L2_CAP_VIDEO_CAPTURE_MPLANE;
	return 0;
}

static int fimc_is_isp_video_enum_input(struct file *file, void *priv,
				  struct v4l2_input *input)
{
	int ret = 0;

	if (input->index >= FIMC_IS_SENSOR_NUM)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strncpy(input->name, "ISP Camera", 32);
	return ret;
}

static int fimc_is_isp_video_s_input(struct file *file, void *priv,
				  unsigned int i)
{
	int ret = 0;

	dbg("fimc_is_isp_video_s_input\n");
	return ret;
}

int fimc_is_isp_video_enum_fmt_mplane(struct file *file, void *priv,
				struct v4l2_fmtdesc *f)
{
	struct fimc_is_fmt *fmt;

	if (f->index >= ARRAY_SIZE(fimc_is_formats))
		return -EINVAL;

	fmt = &fimc_is_formats[f->index];
	strncpy(f->description, fmt->name, sizeof(f->description) - 1);
	f->pixelformat = fmt->fourcc;

	return 0;
}

int fimc_is_isp_video_g_fmt_mplane(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct fimc_is_dev *is_dev;
	dbg("fimc_is_isp_video_g_fmt_mplane\n");

	is_dev = video_drvdata(file);

	switch (is_dev->scenario_id) {
	case ISS_PREVIEW_STILL:
		f->fmt.pix.width = is_dev->sensor.width_prev;
		f->fmt.pix.height = is_dev->sensor.height_prev;
		break;
	case ISS_PREVIEW_VIDEO:
		f->fmt.pix.width = is_dev->sensor.width_prev_cam;
		f->fmt.pix.height = is_dev->sensor.height_prev_cam;
		break;
	case ISS_CAPTURE_STILL:
		f->fmt.pix.width = is_dev->sensor.width_cap;
		f->fmt.pix.height = is_dev->sensor.height_cap;
		break;
	case ISS_CAPTURE_VIDEO:
		f->fmt.pix.width = is_dev->sensor.width_cam;
		f->fmt.pix.height = is_dev->sensor.height_cam;
		break;
	default:
		f->fmt.pix.width = 0;
		f->fmt.pix.height = 0;
		break;
	}

	f->fmt.pix.field	= V4L2_FIELD_NONE;
	/* FIXME */
	f->fmt.pix.pixelformat	= V4L2_PIX_FMT_SGRBG12;
	return 0;
}

static int fimc_is_isp_video_s_fmt_mplane(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct fimc_is_dev *is_dev;
	struct v4l2_pix_format_mplane *pix;
	u32 width = 0, height = 0;
	int i, ret = 0;
	int size_mismatch_flg = 0;

	dbg("fimc_is_video_s_fmt_mplane\n");
	is_dev = video_drvdata(file);

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE
		&& f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	pix = &f->fmt.pix_mp;
	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_SGRBG10:
		width = pix->width - is_dev->sensor.offset_x;
		height = pix->height - is_dev->sensor.offset_y;
		IS_ISP_SET_PARAM_DMA_OUTPUT2_CMD(is_dev,
						DMA_OUTPUT_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_WIDTH(is_dev, width);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_HEIGHT(is_dev, height);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_FORMAT(is_dev,
						DMA_OUTPUT_FORMAT_BAYER);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BITWIDTH(is_dev,
						DMA_OUTPUT_BIT_WIDTH_10BIT);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_PLANE(is_dev, DMA_OUTPUT_PLANE_1);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_ORDER(is_dev,
						DMA_OUTPUT_ORDER_GB_BG);
		break;
	case V4L2_PIX_FMT_SGRBG12:
		width = pix->width;
		height = pix->height;
		IS_ISP_SET_PARAM_DMA_OUTPUT2_CMD(is_dev,
						DMA_OUTPUT_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_WIDTH(is_dev, width);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_HEIGHT(is_dev, height);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_FORMAT(is_dev,
						DMA_OUTPUT_FORMAT_BAYER);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BITWIDTH(is_dev,
						DMA_OUTPUT_BIT_WIDTH_12BIT);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_PLANE(is_dev, DMA_OUTPUT_PLANE_1);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_ORDER(is_dev,
						DMA_OUTPUT_ORDER_GB_BG);
		break;
	case V4L2_PIX_FMT_SGRBG14:
		width = pix->width - is_dev->sensor.offset_x;
		height = pix->height - is_dev->sensor.offset_y;
		IS_ISP_SET_PARAM_DMA_OUTPUT2_CMD(is_dev,
						DMA_OUTPUT_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_WIDTH(is_dev, width);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_HEIGHT(is_dev, height);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_FORMAT(is_dev,
						DMA_OUTPUT_FORMAT_BAYER);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BITWIDTH(is_dev,
						DMA_OUTPUT_BIT_WIDTH_14BIT);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_PLANE(is_dev, DMA_OUTPUT_PLANE_1);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_ORDER(is_dev,
						DMA_OUTPUT_ORDER_GB_BG);
		break;
	}

	switch (is_dev->scenario_id) {
	case ISS_PREVIEW_STILL:
		if (width != is_dev->sensor.width_prev ||
			height != is_dev->sensor.height_prev)
			size_mismatch_flg = 1;
		break;
	case ISS_PREVIEW_VIDEO:
		if (width != is_dev->sensor.width_prev_cam ||
			height != is_dev->sensor.height_prev_cam)
			size_mismatch_flg = 1;
		break;
	case ISS_CAPTURE_STILL:
		if (width != is_dev->sensor.width_cap ||
			height != is_dev->sensor.height_cap)
			size_mismatch_flg = 1;
		break;
	case ISS_CAPTURE_VIDEO:
		if (width != is_dev->sensor.width_cam ||
			height != is_dev->sensor.height_cam)
			size_mismatch_flg = 1;
		break;
	default:
		break;
	}
	if (size_mismatch_flg)
		err(" Size mismatching - ISP otfoutput and ISP bayer output\n");

	is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].num_plane = pix->num_planes;
	for (i = 0; i < pix->num_planes; i++) {
		is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].plane_size[i] =
						pix->plane_fmt[i].sizeimage;
	}

	dbg("S_FMT : %d,%d - %d\n", width, height, pix->num_planes);
	return ret;
}

static int fimc_is_isp_video_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	int ret = 0;

	dbg("fimc_is_isp_video_g_ctrl\n");
	return ret;
}

static int fimc_is_isp_video_s_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	int ret = 0;

	dbg("fimc_is_isp_video_s_ctrl\n");
	return ret;
}

static int fimc_is_isp_video_reqbufs(struct file *file, void *priv,
			    struct v4l2_requestbuffers *buf)
{
	struct fimc_is_dev *is_dev = video_drvdata(file);
	struct fimc_is_video_dev *video = file->private_data;
	int ret;

	ret = vb2_reqbufs(&video->vbq, buf);

	if (!ret)
		is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].num_buf = buf->count;

	if (buf->count == 0)
		is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].buf_ref_cnt = 0;
	printk(KERN_INFO "%s(num_buf : %d)\n", __func__,
		is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].num_buf);
	return ret;
}

static int fimc_is_isp_video_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct fimc_is_video_dev *video = file->private_data;

	printk(KERN_DEBUG "%s\n", __func__);
	return vb2_querybuf(&video->vbq, buf);
}

static int fimc_is_isp_video_qbuf(struct file *file, void *priv,
			  struct v4l2_buffer *buf)
{
	struct fimc_is_video_dev *video = file->private_data;

	printk(KERN_DEBUG "%s\n", __func__);
	return vb2_qbuf(&video->vbq, buf);
}

static int fimc_is_isp_video_dqbuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct fimc_is_video_dev *video = file->private_data;

	printk(KERN_DEBUG "%s\n", __func__);
	return vb2_dqbuf(&video->vbq, buf, file->f_flags & O_NONBLOCK);
}

static int fimc_is_isp_video_streamon(struct file *file, void *priv,
			     enum v4l2_buf_type type)
{
	struct fimc_is_dev *is_dev = video_drvdata(file);

	printk(KERN_DEBUG "%s\n", __func__);
	return vb2_streamon(&is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].vbq, type);
}

static int fimc_is_isp_video_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct fimc_is_dev *is_dev = video_drvdata(file);

	printk(KERN_DEBUG "%s\n", __func__);
	return vb2_streamoff(&is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].vbq, type);
}

const struct v4l2_ioctl_ops fimc_is_isp_video_ioctl_ops = {
	.vidioc_querycap		= fimc_is_isp_video_querycap,

	.vidioc_enum_fmt_vid_cap_mplane	= fimc_is_isp_video_enum_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= fimc_is_isp_video_s_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= fimc_is_isp_video_g_fmt_mplane,

	.vidioc_reqbufs			= fimc_is_isp_video_reqbufs,
	.vidioc_querybuf		= fimc_is_isp_video_querybuf,

	.vidioc_qbuf			= fimc_is_isp_video_qbuf,
	.vidioc_dqbuf			= fimc_is_isp_video_dqbuf,

	.vidioc_streamon		= fimc_is_isp_video_streamon,
	.vidioc_streamoff		= fimc_is_isp_video_streamoff,

	.vidioc_g_ctrl			= fimc_is_isp_video_g_ctrl,
	.vidioc_s_ctrl			= fimc_is_isp_video_s_ctrl,

	.vidioc_enum_input		= fimc_is_isp_video_enum_input,
	.vidioc_s_input			= fimc_is_isp_video_s_input,
};

static int fimc_is_isp_queue_setup(struct vb2_queue *vq,
			unsigned int *num_buffers, unsigned int *num_planes,
			unsigned long sizes[], void *allocators[])
{
	struct fimc_is_video_dev *video = vq->drv_priv;
	struct fimc_is_dev	*is_dev = video->dev;
	int i;

	*num_planes = is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].num_plane;

	for (i = 0; i < *num_planes; i++) {
		sizes[i] = is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].plane_size[i];
		allocators[i] =  is_dev->alloc_ctx;
	}

	dbg("%s(num_planes : %d)(size : %d)\n", __func__, (int)*num_planes,
								(int)sizes[0]);

	return 0;
}

static int fimc_is_isp_buf_prepare(struct vb2_buffer *vb)
{
	struct fimc_is_video_dev *video = vb->vb2_queue->drv_priv;
	struct fimc_is_dev *is_dev = video->dev;
	unsigned long size;
	int i;

	for (i = 0; i < is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].num_plane; i++) {
		size = is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].plane_size[i];

		if (vb2_plane_size(vb, i) < size) {
			err("User buffer too small(%ld < %ld)\n",
					 vb2_plane_size(vb, i), size);
			return -EINVAL;
		}

		vb2_set_plane_payload(vb, i, size);
	}

	return 0;
}

static void fimc_is_isp_lock(struct vb2_queue *q)
{
	struct fimc_is_video_dev *video = vb2_get_drv_priv(q);
	struct fimc_is_dev	*is_dev = video->dev;
	mutex_lock(&is_dev->lock);
}

static void fimc_is_isp_unlock(struct vb2_queue *q)
{
	struct fimc_is_video_dev *video = vb2_get_drv_priv(q);
	struct fimc_is_dev	*is_dev = video->dev;
	mutex_unlock(&is_dev->lock);
}

static int fimc_is_isp_start_streaming(struct vb2_queue *q)
{
	struct fimc_is_video_dev *video = vb2_get_drv_priv(q);
	struct fimc_is_dev	*is_dev = video->dev;
	int i, j;
	int buf_num, buf_plane, buf_index;

	if (test_bit(FIMC_IS_STATE_ISP_BUFFER_PREPARED, &is_dev->vb_state) &&
		!test_bit(FIMC_IS_STATE_ISP_STREAM_ON, &is_dev->vb_state)) {

		dbg("Start streaming!!\n");
		/* buffer addr setting */
		buf_num = is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].num_buf;
		buf_plane = is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].num_plane;
		for (i = 0; i < buf_num; i++)
			for (j = 0; j < buf_plane; j++) {
				buf_index = i*buf_plane + j;
				printk(KERN_INFO "(%d)set buf(%d:%d)= 0x%08x\n"
					, buf_index, i, j,
			is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].buf[i][j]);
				is_dev->is_p_region->shared[32+buf_index]
			= is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].buf[i][j];
			}

		printk(KERN_INFO "buf_num:%d buf_plane:%d shared[32]: 0x%08x\n",
			is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].num_buf,
			is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].num_plane,
			virt_to_phys(is_dev->is_p_region->shared) +
							32*sizeof(u32));

		IS_ISP_SET_PARAM_DMA_OUTPUT2_CMD(is_dev,
					DMA_OUTPUT_COMMAND_ENABLE);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_NUMBER(is_dev,
				is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].num_buf);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_ADDRESS(is_dev,
				(u32)virt_to_phys(is_dev->is_p_region->shared)
				+ 32 * sizeof(u32));
		IS_ISP_SET_PARAM_DMA_OUTPUT2_NODIFY_DMA_DONE(is_dev,
				DMA_OUTPUT_NOTIFY_DMA_DONE_ENBABLE);
		IS_SET_PARAM_BIT(is_dev, PARAM_ISP_DMA2_OUTPUT);
		IS_INC_PARAM_NUM(is_dev);

		fimc_is_mem_cache_clean((void *)is_dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(is_dev);
		set_bit(FIMC_IS_STATE_ISP_STREAM_ON, &is_dev->vb_state);
	}
	return 0;
}

static void fimc_is_isp_buf_queue(struct vb2_buffer *vb)
{
	struct fimc_is_video_dev *video = vb->vb2_queue->drv_priv;
	struct fimc_is_dev *is_dev = video->dev;
#if defined(CONFIG_VIDEOBUF2_ION)
	dma_addr_t kvaddr;
#endif
	unsigned int i;

	if (is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].num_plane != vb->num_planes)
		return;

	if (!test_bit(FIMC_IS_STATE_ISP_BUFFER_PREPARED, &is_dev->vb_state)) {
		for (i = 0; i < vb->num_planes; i++) {
			is_dev->video[FIMC_IS_VIDEO_NUM_BAYER]
				.buf[vb->v4l2_buf.index][i] =
						is_dev->vb2->plane_addr(vb, i);
			dbg("index(%d)(%d) deviceVaddr(0x%08x)\n",
				vb->v4l2_buf.index, i,
				is_dev->video[FIMC_IS_VIDEO_NUM_BAYER]
						.buf[vb->v4l2_buf.index][i]);
		}

		is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].buf_ref_cnt++;

		if (is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].num_buf ==
			is_dev->video[FIMC_IS_VIDEO_NUM_BAYER].buf_ref_cnt)
			set_bit(FIMC_IS_STATE_ISP_BUFFER_PREPARED,
							&is_dev->vb_state);
	}

	if (!test_bit(FIMC_IS_STATE_ISP_STREAM_ON, &is_dev->vb_state))
		fimc_is_isp_start_streaming(vb->vb2_queue);
}

static int fimc_is_isp_stop_streaming(struct vb2_queue *q)
{
	struct fimc_is_video_dev *video = vb2_get_drv_priv(q);
	struct fimc_is_dev	*is_dev = video->dev;

	IS_ISP_SET_PARAM_DMA_OUTPUT2_CMD(is_dev, DMA_OUTPUT_COMMAND_DISABLE);
	IS_ISP_SET_PARAM_DMA_OUTPUT2_NODIFY_DMA_DONE(is_dev,
					DMA_OUTPUT_NOTIFY_DMA_DONE_DISABLE);
	IS_SET_PARAM_BIT(is_dev, PARAM_ISP_DMA2_OUTPUT);
	IS_INC_PARAM_NUM(is_dev);

	fimc_is_mem_cache_clean((void *)is_dev->is_p_region,
			IS_PARAM_SIZE);
	fimc_is_hw_set_param(is_dev);

	clear_bit(FIMC_IS_STATE_ISP_STREAM_ON, &is_dev->vb_state);
	clear_bit(FIMC_IS_STATE_ISP_BUFFER_PREPARED, &is_dev->vb_state);
	return 0;
}

const struct vb2_ops fimc_is_isp_qops = {
	.queue_setup	 = fimc_is_isp_queue_setup,
	.buf_prepare	 = fimc_is_isp_buf_prepare,
	.buf_queue	 = fimc_is_isp_buf_queue,
	.wait_prepare	 = fimc_is_isp_unlock,
	.wait_finish	 = fimc_is_isp_lock,
	.start_streaming = fimc_is_isp_start_streaming,
	.stop_streaming	 = fimc_is_isp_stop_streaming,
};
#endif

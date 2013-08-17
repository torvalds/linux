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
#if defined(CONFIG_BUSFREQ_OPP) && defined(CONFIG_CPU_EXYNOS5250)
#include <mach/dev.h>
#endif
#include <plat/bts.h>
#include <media/exynos_mc.h>
#include <linux/cma.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/scatterlist.h>
#include <linux/videodev2_exynos_media.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/v4l2-mediabus.h>

#include "fimc-is-core.h"
#include "fimc-is-helper.h"
#include "fimc-is-param.h"
#include "fimc-is-cmd.h"
#include "fimc-is-regs.h"
#include "fimc-is-err.h"
#include "fimc-is-misc.h"

static struct fimc_is_fmt fimc_is_formats[] = {
	 {
		.name		= "YUV 4:2:2 packed, YCbYCr",
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.num_planes	= 1,
		.mbus_code	= V4L2_MBUS_FMT_YUYV8_2X8,
	}, {
		.name		= "YUV 4:2:2 packed, CbYCrY",
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.num_planes	= 1,
		.mbus_code	= V4L2_MBUS_FMT_UYVY8_2X8,
	}, {
		.name		= "YUV 4:2:2 planar, Y/Cb/Cr",
		.pixelformat	= V4L2_PIX_FMT_YUV422P,
		.num_planes	= 1,
	}, {
		.name		= "YUV 4:2:0 planar, YCbCr",
		.pixelformat	= V4L2_PIX_FMT_YUV420,
		.num_planes	= 1,
	}, {
		.name		= "YUV 4:2:0 planar, YCbCr",
		.pixelformat	= V4L2_PIX_FMT_YVU420,
		.num_planes	= 1,
	}, {
		.name		= "YUV 4:2:0 planar, Y/CbCr",
		.pixelformat	= V4L2_PIX_FMT_NV12,
		.num_planes	= 1,
	}, {
		.name		= "YUV 4:2:0 planar, Y/CrCb",
		.pixelformat	= V4L2_PIX_FMT_NV21,
		.num_planes	= 1,
	}, {
		.name		= "YUV 4:2:0 non-contiguous 2-planar, Y/CbCr",
		.pixelformat	= V4L2_PIX_FMT_NV12M,
		.num_planes	= 2,
	}, {
		.name		= "YVU 4:2:0 non-contiguous 2-planar, Y/CrCb",
		.pixelformat	= V4L2_PIX_FMT_NV21M,
		.num_planes	= 2,
	}, {
		.name		= "YUV 4:2:0 non-contiguous 3-planar, Y/Cb/Cr",
		.pixelformat	= V4L2_PIX_FMT_YUV420M,
		.num_planes	= 3,
	}, {
		.name		= "YUV 4:2:0 non-contiguous 3-planar, Y/Cr/Cb",
		.pixelformat	= V4L2_PIX_FMT_YVU420M,
		.num_planes	= 3,
	}, {
		.name		= "BAYER 10 bit",
		.pixelformat	= V4L2_PIX_FMT_SBGGR10,
		.num_planes	= 1,
	}, {
		.name		= "BAYER 12 bit",
		.pixelformat	= V4L2_PIX_FMT_SBGGR12,
		.num_planes	= 1,
	},
};


static struct fimc_is_fmt *find_format(u32 *pixelformat,
					u32 *mbus_code,
					int index)
{
	struct fimc_is_fmt *fmt, *def_fmt = NULL;
	unsigned int i;

	if (index >= ARRAY_SIZE(fimc_is_formats))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(fimc_is_formats); ++i) {
		fmt = &fimc_is_formats[i];
		if (pixelformat && fmt->pixelformat == *pixelformat)
			return fmt;
		if (mbus_code && fmt->mbus_code == *mbus_code)
			return fmt;
		if (index == i)
			def_fmt = fmt;
	}
	return def_fmt;

}

static void set_plane_size(struct fimc_is_frame *frame, unsigned int sizes[])
{
	dbg(" ");
	switch (frame->format.pixelformat) {
	case V4L2_PIX_FMT_YUYV:
		dbg("V4L2_PIX_FMT_YUYV(w:%d)(h:%d)\n",
				frame->width, frame->height);
		sizes[0] =  frame->width*frame->height*2;
		break;
	case V4L2_PIX_FMT_NV12M:
		dbg("V4L2_PIX_FMT_NV12M(w:%d)(h:%d)\n",
				frame->width, frame->height);
		sizes[0] =  frame->width*frame->height;
		sizes[1] =  frame->width*frame->height/2;
		break;
	case V4L2_PIX_FMT_YVU420M:
		dbg("V4L2_PIX_FMT_YVU420M(w:%d)(h:%d)\n",
				frame->width, frame->height);
		sizes[0] =  frame->width*frame->height;
		sizes[1] =  frame->width*frame->height/4;
		sizes[2] =  frame->width*frame->height/4;
		break;
	case  V4L2_PIX_FMT_SBGGR10:
		dbg("V4L2_PIX_FMT_SBGGR10(w:%d)(h:%d)\n",
				frame->width, frame->height);
		sizes[0] =  frame->width*frame->height*2;
		break;
	case V4L2_PIX_FMT_SBGGR12:
		dbg("V4L2_PIX_FMT_SBGGR12(w:%d)(h:%d)\n",
				frame->width, frame->height);
		sizes[0] =  frame->width*frame->height*2;
		break;
	}
}

/*************************************************************************/
/* video file opertation */
/************************************************************************/

static int fimc_is_scalerc_video_open(struct file *file)
{
	struct fimc_is_dev *isp = video_drvdata(file);

#if defined(CONFIG_BUSFREQ_OPP) && defined(CONFIG_CPU_EXYNOS5250)
	mutex_lock(&isp->busfreq_lock);
	isp->busfreq_num++;
	if (isp->busfreq_num == 1) {
		dev_lock(isp->bus_dev, &isp->pdev->dev,
			(FIMC_IS_FREQ_MIF * 1000) + FIMC_IS_FREQ_INT);
		dbg("busfreq locked on <%d/%d>MHz\n",
			FIMC_IS_FREQ_MIF, FIMC_IS_FREQ_INT);
	}
	mutex_unlock(&isp->busfreq_lock);
#endif

	dbg("%s\n", __func__);
	file->private_data = &isp->video[FIMC_IS_VIDEO_NUM_SCALERC];
	isp->video[FIMC_IS_VIDEO_NUM_SCALERC].num_buf = 0;
	isp->video[FIMC_IS_VIDEO_NUM_SCALERC].buf_ref_cnt = 0;

	if (!test_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state)) {
		isp->sensor_num = 1;
		dbg("++++ IS load fw (Scaler C open)\n");
		mutex_unlock(&isp->lock);
		fimc_is_load_fw(isp);

		set_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state);
		clear_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state);
		clear_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state);
		dbg("---- IS load fw (Scaler C open)\n");
	} else {
		mutex_unlock(&isp->lock);
	}

	clear_bit(FIMC_IS_STATE_SCALERC_STREAM_ON, &isp->pipe_state);
	return 0;

}

static int fimc_is_scalerc_video_close(struct file *file)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	int ret;

	dbg("%s\n", __func__);
	vb2_queue_release(&isp->video[FIMC_IS_VIDEO_NUM_SCALERC].vbq);

	mutex_lock(&isp->lock);
	if (!test_bit(FIMC_IS_STATE_SCALERP_STREAM_ON, &isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_SCALERC_STREAM_ON, &isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_3DNR_STREAM_ON, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state)) {

		dbg("++++ IS local power off (Scaler C close)\n");
		mutex_unlock(&isp->lock);
		clear_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state);
		fimc_is_hw_subip_poweroff(isp);
		ret = wait_event_timeout(isp->irq_queue,
			!test_bit(FIMC_IS_PWR_ST_POWER_ON_OFF, &isp->power),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout FIMC_IS_PWR_ST_POWER_ON_OFF\n");
			fimc_is_hw_set_low_poweroff(isp, true);
			clear_bit(FIMC_IS_PWR_ST_POWER_ON_OFF, &isp->power);
			ret = 0;
		}

		dbg("stop flite & mipi (pos:%d) (port:%d)\n",
			isp->sensor.id_position,
			isp->pdata->
			sensor_info[isp->sensor.id_position]->flite_id);
		stop_fimc_lite(isp->pdata->
			sensor_info[isp->sensor.id_position]->flite_id);
		stop_mipi_csi(isp->pdata->
			sensor_info[isp->sensor.id_position]->csi_id);

		fimc_is_hw_a5_power(isp, 0);
		clear_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state);
		dbg("---- IS local power off (Scaler C close)\n");
	} else {
		mutex_unlock(&isp->lock);
	}

#if defined(CONFIG_BUSFREQ_OPP) && defined(CONFIG_CPU_EXYNOS5250)
	mutex_lock(&isp->busfreq_lock);
	if (isp->busfreq_num == 1) {
		dev_unlock(isp->bus_dev, &isp->pdev->dev);
		printk(KERN_DEBUG "busfreq locked off\n");
	}
	isp->busfreq_num--;
	if (isp->busfreq_num < 0)
		isp->busfreq_num = 0;
	mutex_unlock(&isp->busfreq_lock);
#endif

	return 0;
}

static unsigned int fimc_is_scalerc_video_poll(struct file *file,
				      struct poll_table_struct *wait)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_poll(&isp->video[FIMC_IS_VIDEO_NUM_SCALERC].vbq, file, wait);

}

static int fimc_is_scalerc_video_mmap(struct file *file,
					struct vm_area_struct *vma)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_mmap(&isp->video[FIMC_IS_VIDEO_NUM_SCALERC].vbq, vma);

}

/*************************************************************************/
/* video ioctl operation						*/
/************************************************************************/

static int fimc_is_scalerc_video_querycap(struct file *file, void *fh,
						struct v4l2_capability *cap)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	strncpy(cap->driver, isp->pdev->name, sizeof(cap->driver) - 1);

	dbg("(devname : %s)\n", cap->driver);
	strncpy(cap->card, isp->pdev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(1, 0, 0);
	cap->capabilities = V4L2_CAP_STREAMING
				| V4L2_CAP_VIDEO_CAPTURE
				| V4L2_CAP_VIDEO_CAPTURE_MPLANE;

	return 0;
}

static int fimc_is_scalerc_video_enum_fmt_mplane(struct file *file, void *priv,
				    struct v4l2_fmtdesc *f)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_scalerc_video_get_format_mplane(struct file *file, void *fh,
						struct v4l2_format *format)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_scalerc_video_set_format_mplane(struct file *file, void *fh,
						struct v4l2_format *format)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	struct v4l2_pix_format_mplane *pix;
	struct fimc_is_fmt *frame;

	dbg("%s\n", __func__);

	pix = &format->fmt.pix_mp;
	frame = find_format(&pix->pixelformat, NULL, 0);

	if (!frame)
		return -EINVAL;

	isp->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.format.pixelformat
							= frame->pixelformat;
	isp->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.format.mbus_code
							= frame->mbus_code;
	isp->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.format.num_planes
							= frame->num_planes;
	isp->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.width = pix->width;
	isp->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.height = pix->height;
	dbg("num_planes : %d\n", frame->num_planes);
	dbg("width : %d\n", pix->width);
	dbg("height : %d\n", pix->height);

	return 0;
}

static int fimc_is_scalerc_video_try_format_mplane(struct file *file, void *fh,
						struct v4l2_format *format)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_scalerc_video_cropcap(struct file *file, void *fh,
						struct v4l2_cropcap *cropcap)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_scalerc_video_get_crop(struct file *file, void *fh,
						struct v4l2_crop *crop)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_scalerc_video_set_crop(struct file *file, void *fh,
						struct v4l2_crop *crop)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_scalerc_video_reqbufs(struct file *file, void *priv,
					struct v4l2_requestbuffers *buf)
{
	int ret;
	struct fimc_is_video_dev *video = file->private_data;
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("(buf->count : %d)\n", buf->count);

	ret = vb2_reqbufs(&video->vbq, buf);
	if (!ret)
		isp->video[FIMC_IS_VIDEO_NUM_SCALERC].num_buf = buf->count;

	if (buf->count == 0)
		isp->video[FIMC_IS_VIDEO_NUM_SCALERC].buf_ref_cnt = 0;
	dbg("(num_buf : %d)\n",
		isp->video[FIMC_IS_VIDEO_NUM_SCALERC].num_buf);

	return ret;
}

static int fimc_is_scalerc_video_querybuf(struct file *file, void *priv,
						struct v4l2_buffer *buf)
{
	int ret;
	struct fimc_is_video_dev *video = file->private_data;

	dbg("%s\n", __func__);
	ret = vb2_querybuf(&video->vbq, buf);

	return ret;
}

static int fimc_is_scalerc_video_qbuf(struct file *file, void *priv,
						struct v4l2_buffer *buf)
{
	int vb_ret;
	struct fimc_is_video_dev *video = file->private_data;
	struct fimc_is_dev *isp = video_drvdata(file);

	if (test_bit(FIMC_IS_STATE_SCALERC_BUFFER_PREPARED, &isp->pipe_state)) {
		video->buf_mask |= (1<<buf->index);
		IS_INC_PARAM_NUM(isp);

		dbg("index(%d) mask(0x%08x)\n", buf->index, video->buf_mask);
	} else
		dbg("index(%d)\n", buf->index);

	vb_ret = vb2_qbuf(&video->vbq, buf);

	return vb_ret;
}

static int fimc_is_scalerc_video_dqbuf(struct file *file, void *priv,
						struct v4l2_buffer *buf)
{
	int vb_ret;
	struct fimc_is_video_dev *video = file->private_data;
	struct fimc_is_dev *isp = video_drvdata(file);

	vb_ret = vb2_dqbuf(&video->vbq, buf, file->f_flags & O_NONBLOCK);

	video->buf_mask &= ~(1<<buf->index);
	isp->video[FIMC_IS_VIDEO_NUM_SCALERC].buf_mask = video->buf_mask;

	dbg("index(%d) mask(0x%08x)\n", buf->index, video->buf_mask);

	return vb_ret;
}

static int fimc_is_scalerc_video_streamon(struct file *file, void *priv,
						enum v4l2_buf_type type)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_streamon(&isp->video[FIMC_IS_VIDEO_NUM_SCALERC].vbq, type);
}

static int fimc_is_scalerc_video_streamoff(struct file *file, void *priv,
						enum v4l2_buf_type type)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_streamoff(&isp->video[FIMC_IS_VIDEO_NUM_SCALERC].vbq, type);
}

static int fimc_is_scalerc_video_enum_input(struct file *file, void *priv,
						struct v4l2_input *input)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	struct exynos5_fimc_is_sensor_info *sensor_info
			= isp->pdata->sensor_info[input->index];

	dbg("index(%d) sensor(%s)\n",
		input->index, sensor_info->sensor_name);
	dbg("pos(%d) sensor_id(%d)\n",
		sensor_info->sensor_position, sensor_info->sensor_id);
	dbg("csi_id(%d) flite_id(%d)\n",
		sensor_info->csi_id, sensor_info->flite_id);
	dbg("i2c_ch(%d)\n", sensor_info->i2c_channel);

	if (input->index >= FIMC_IS_MAX_CAMIF_CLIENTS)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;

	strncpy(input->name, sensor_info->sensor_name,
					FIMC_IS_MAX_NAME_LEN);
	return 0;
}

static int fimc_is_scalerc_video_g_input(struct file *file, void *priv,
						unsigned int *input)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_scalerc_video_s_input(struct file *file, void *priv,
						unsigned int input)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	struct exynos5_fimc_is_sensor_info *sensor_info
			= isp->pdata->sensor_info[input];

	isp->sensor.id_position = input;
	isp->sensor.sensor_type
		= fimc_is_hw_get_sensor_type(sensor_info->sensor_id,
						sensor_info->flite_id);

	fimc_is_hw_set_default_size(isp, sensor_info->sensor_id);

	dbg("sensor info : pos(%d) type(%d)\n", input, isp->sensor.sensor_type);


	return 0;
}

const struct v4l2_file_operations fimc_is_scalerc_video_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_is_scalerc_video_open,
	.release	= fimc_is_scalerc_video_close,
	.poll		= fimc_is_scalerc_video_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= fimc_is_scalerc_video_mmap,
};

const struct v4l2_ioctl_ops fimc_is_scalerc_video_ioctl_ops = {
	.vidioc_querycap		= fimc_is_scalerc_video_querycap,
	.vidioc_enum_fmt_vid_cap_mplane
				= fimc_is_scalerc_video_enum_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane
				= fimc_is_scalerc_video_get_format_mplane,
	.vidioc_s_fmt_vid_cap_mplane
				= fimc_is_scalerc_video_set_format_mplane,
	.vidioc_try_fmt_vid_cap_mplane
				= fimc_is_scalerc_video_try_format_mplane,
	.vidioc_cropcap			= fimc_is_scalerc_video_cropcap,
	.vidioc_g_crop			= fimc_is_scalerc_video_get_crop,
	.vidioc_s_crop			= fimc_is_scalerc_video_set_crop,
	.vidioc_reqbufs			= fimc_is_scalerc_video_reqbufs,
	.vidioc_querybuf		= fimc_is_scalerc_video_querybuf,
	.vidioc_qbuf			= fimc_is_scalerc_video_qbuf,
	.vidioc_dqbuf			= fimc_is_scalerc_video_dqbuf,
	.vidioc_streamon		= fimc_is_scalerc_video_streamon,
	.vidioc_streamoff		= fimc_is_scalerc_video_streamoff,
	.vidioc_enum_input		= fimc_is_scalerc_video_enum_input,
	.vidioc_g_input			= fimc_is_scalerc_video_g_input,
	.vidioc_s_input			= fimc_is_scalerc_video_s_input,
};

static int fimc_is_scalerc_queue_setup(struct vb2_queue *vq,
			const struct v4l2_format *fmt,
			unsigned int *num_buffers,
			unsigned int *num_planes, unsigned int sizes[],
			void *allocators[])
{

	struct fimc_is_video_dev *video = vq->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	int i;


	*num_planes = isp->video[FIMC_IS_VIDEO_NUM_SCALERC].
					frame.format.num_planes;
	set_plane_size(&isp->video[FIMC_IS_VIDEO_NUM_SCALERC].frame, sizes);

	for (i = 0; i < *num_planes; i++)
		allocators[i] =  isp->alloc_ctx;

	dbg("(num_planes : %d)(size : %d)\n", (int)*num_planes, (int)sizes[0]);
	return 0;
}
static int fimc_is_scalerc_buffer_prepare(struct vb2_buffer *vb)
{
	dbg("--%s\n", __func__);
	return 0;
}


static inline void fimc_is_scalerc_lock(struct vb2_queue *vq)
{
	dbg("%s\n", __func__);
}

static inline void fimc_is_scalerc_unlock(struct vb2_queue *vq)
{
	dbg("%s\n", __func__);
}

static int fimc_is_scalerc_start_streaming(struct vb2_queue *q,
						unsigned int count)
{
	struct fimc_is_video_dev *video = q->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	int ret;
	int i, j;
	int buf_index;

	dbg("(pipe_state : %d)\n", (int)isp->pipe_state);

	if (test_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state)) {

		dbg("IS change mode\n");
		set_bit(IS_ST_CHANGE_MODE, &isp->state);
		fimc_is_hw_change_mode(isp, IS_MODE_PREVIEW_STILL);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_CHANGE_MODE_DONE,
			&isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"Mode change timeout:%s\n", __func__);
			return -EBUSY;
		}

		set_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state);
	}

	if (test_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_SCALERC_BUFFER_PREPARED,
			&isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state)) {
		dbg("IS Stream On");
		fimc_is_hw_set_stream(isp, 1);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_STREAM_ON, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
			return -EBUSY;
		}
		clear_bit(IS_ST_STREAM_ON, &isp->state);

		set_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state);
	}

	if (test_bit(FIMC_IS_STATE_SCALERC_BUFFER_PREPARED, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state)) {

		/* buffer addr setting */
		for (i = 0; i < isp->video[FIMC_IS_VIDEO_NUM_SCALERC].
							num_buf; i++)
			for (j = 0; j < isp->video[FIMC_IS_VIDEO_NUM_SCALERC].
						frame.format.num_planes; j++) {
				buf_index
				= i * isp->video[FIMC_IS_VIDEO_NUM_SCALERC].
						frame.format.num_planes + j;

				dbg("(%d)set buf(%d:%d) = 0x%08x\n",
					buf_index, i, j,
					isp->video[FIMC_IS_VIDEO_NUM_SCALERC].
					buf[i][j]);

				isp->is_p_region->shared[447+buf_index]
					= isp->video[FIMC_IS_VIDEO_NUM_SCALERC].
								buf[i][j];
		}

		dbg("buf_num:%d buf_plane:%d shared[447] : 0x%p\n",
			isp->video[FIMC_IS_VIDEO_NUM_SCALERC].num_buf,
			isp->video[FIMC_IS_VIDEO_NUM_SCALERC].
			frame.format.num_planes,
			isp->mem.kvaddr_shared + 447 * sizeof(u32));

		for (i = 0; i < isp->video[FIMC_IS_VIDEO_NUM_SCALERC].
							num_buf; i++)
			isp->video[FIMC_IS_VIDEO_NUM_SCALERC].buf_mask
								|= (1 << i);

		dbg("initial buffer mask : 0x%08x\n",
			isp->video[FIMC_IS_VIDEO_NUM_SCALERC].buf_mask);

		IS_SCALERC_SET_PARAM_DMA_OUTPUT_CMD(isp,
			DMA_OUTPUT_COMMAND_ENABLE);
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_MASK(isp,
			isp->video[FIMC_IS_VIDEO_NUM_SCALERC].buf_mask);
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_BUFFERNUM(isp,
			isp->video[FIMC_IS_VIDEO_NUM_SCALERC].num_buf);
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_BUFFERADDR(isp,
			(u32)isp->mem.dvaddr_shared + 447*sizeof(u32));

		IS_SET_PARAM_BIT(isp, PARAM_SCALERC_DMA_OUTPUT);
		IS_INC_PARAM_NUM(isp);

		fimc_is_mem_cache_clean((void *)isp->is_p_region,
			IS_PARAM_SIZE);

		isp->scenario_id = ISS_PREVIEW_STILL;
		set_bit(IS_ST_INIT_PREVIEW_STILL,	&isp->state);
		clear_bit(IS_ST_INIT_CAPTURE_STILL, &isp->state);
		clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state);
		fimc_is_hw_set_param(isp);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
			return -EBUSY;
		}

		set_bit(FIMC_IS_STATE_SCALERC_STREAM_ON, &isp->pipe_state);
	}
	return 0;
}

static int fimc_is_scalerc_stop_streaming(struct vb2_queue *q)
{
	struct fimc_is_video_dev *video = q->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	int ret;

	clear_bit(IS_ST_STREAM_OFF, &isp->state);
	fimc_is_hw_set_stream(isp, 0);
	mutex_lock(&isp->lock);
	ret = wait_event_timeout(isp->irq_queue,
		test_bit(IS_ST_STREAM_OFF, &isp->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	mutex_unlock(&isp->lock);
	if (!ret) {
		dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
		if (!ret)
			err("s_power off failed!!\n");
		return -EBUSY;
	}

	IS_SCALERC_SET_PARAM_DMA_OUTPUT_CMD(isp,
		DMA_OUTPUT_COMMAND_DISABLE);
	IS_SCALERC_SET_PARAM_DMA_OUTPUT_BUFFERNUM(isp,
		0);
	IS_SCALERC_SET_PARAM_DMA_OUTPUT_BUFFERADDR(isp,
		0);

	IS_SET_PARAM_BIT(isp, PARAM_SCALERC_DMA_OUTPUT);
	IS_INC_PARAM_NUM(isp);

	fimc_is_mem_cache_clean((void *)isp->is_p_region,
		IS_PARAM_SIZE);

	isp->scenario_id = ISS_PREVIEW_STILL;
	set_bit(IS_ST_INIT_PREVIEW_STILL,	&isp->state);
	clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state);
	fimc_is_hw_set_param(isp);

	mutex_lock(&isp->lock);
	ret = wait_event_timeout(isp->irq_queue,
		test_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	mutex_unlock(&isp->lock);
	if (!ret) {
		dev_err(&isp->pdev->dev,
			"wait timeout 2: %s\n", __func__);
		return -EBUSY;
	}

	dbg("IS change mode\n");
	clear_bit(IS_ST_RUN, &isp->state);
	set_bit(IS_ST_CHANGE_MODE, &isp->state);
	fimc_is_hw_change_mode(isp, IS_MODE_PREVIEW_STILL);
	mutex_lock(&isp->lock);
	ret = wait_event_timeout(isp->irq_queue,
		test_bit(IS_ST_CHANGE_MODE_DONE, &isp->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	mutex_unlock(&isp->lock);
	if (!ret) {
		dev_err(&isp->pdev->dev,
			"Mode change timeout:%s\n", __func__);
		return -EBUSY;
	}

	dbg("IS Stream On");
	fimc_is_hw_set_stream(isp, 1);

	mutex_lock(&isp->lock);
	ret = wait_event_timeout(isp->irq_queue,
		test_bit(IS_ST_STREAM_ON, &isp->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	mutex_unlock(&isp->lock);
	if (!ret) {
		dev_err(&isp->pdev->dev,
			"wait timeout : %s\n", __func__);
		return -EBUSY;
	}
	clear_bit(IS_ST_STREAM_ON, &isp->state);

	if (!test_bit(FIMC_IS_STATE_SCALERP_STREAM_ON, &isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_3DNR_STREAM_ON, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state)) {
		clear_bit(IS_ST_STREAM_OFF, &isp->state);

		fimc_is_hw_set_stream(isp, 0);
		dbg("IS Stream Off");
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_STREAM_OFF, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout4 : %s\n", __func__);
			return -EBUSY;
		}
		clear_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state);
	}

	clear_bit(IS_ST_RUN, &isp->state);
	clear_bit(IS_ST_STREAM_ON, &isp->state);
	clear_bit(FIMC_IS_STATE_SCALERC_BUFFER_PREPARED, &isp->pipe_state);
	clear_bit(FIMC_IS_STATE_SCALERC_STREAM_ON, &isp->pipe_state);

	return 0;
}

static void fimc_is_scalerc_buffer_queue(struct vb2_buffer *vb)
{
	struct fimc_is_video_dev *video = vb->vb2_queue->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	unsigned int i;

	dbg("%s\n", __func__);
	isp->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.format.num_planes
							= vb->num_planes;

	if (!test_bit(FIMC_IS_STATE_SCALERC_BUFFER_PREPARED,
					&isp->pipe_state)) {
		for (i = 0; i < vb->num_planes; i++) {
			isp->video[FIMC_IS_VIDEO_NUM_SCALERC].
				buf[vb->v4l2_buf.index][i]
				= isp->vb2->plane_addr(vb, i);

			dbg("index(%d)(%d) deviceVaddr(0x%08x)\n",
				vb->v4l2_buf.index, i,
				isp->video[FIMC_IS_VIDEO_NUM_SCALERC].
				buf[vb->v4l2_buf.index][i]);
		}

		isp->video[FIMC_IS_VIDEO_NUM_SCALERC].buf_ref_cnt++;

		if (isp->video[FIMC_IS_VIDEO_NUM_SCALERC].num_buf
			== isp->video[FIMC_IS_VIDEO_NUM_SCALERC].buf_ref_cnt)
			set_bit(FIMC_IS_STATE_SCALERC_BUFFER_PREPARED,
				&isp->pipe_state);
	}

	if (!test_bit(FIMC_IS_STATE_SCALERC_STREAM_ON, &isp->pipe_state))
		fimc_is_scalerc_start_streaming(vb->vb2_queue,
			isp->video[FIMC_IS_VIDEO_NUM_SCALERC].num_buf);

	return;
}

const struct vb2_ops fimc_is_scalerc_qops = {
	.queue_setup		= fimc_is_scalerc_queue_setup,
	.buf_prepare		= fimc_is_scalerc_buffer_prepare,
	.buf_queue		= fimc_is_scalerc_buffer_queue,
	.wait_prepare		= fimc_is_scalerc_unlock,
	.wait_finish		= fimc_is_scalerc_lock,
	.start_streaming	= fimc_is_scalerc_start_streaming,
	.stop_streaming	= fimc_is_scalerc_stop_streaming,
};

/*************************************************************************/
/* video file opertation						 */
/************************************************************************/

static int fimc_is_scalerp_video_open(struct file *file)
{
	struct fimc_is_dev *isp = video_drvdata(file);

#if defined(CONFIG_BUSFREQ_OPP) && defined(CONFIG_CPU_EXYNOS5250)
	mutex_lock(&isp->busfreq_lock);
	isp->busfreq_num++;
	if (isp->busfreq_num == 1) {
		dev_lock(isp->bus_dev, &isp->pdev->dev,
			(FIMC_IS_FREQ_MIF * 1000) + FIMC_IS_FREQ_INT);
		dbg("busfreq locked on <%d/%d>MHz\n",
			FIMC_IS_FREQ_MIF, FIMC_IS_FREQ_INT);
	}
	mutex_unlock(&isp->busfreq_lock);
#endif

	dbg("%s\n", __func__);
	file->private_data = &isp->video[FIMC_IS_VIDEO_NUM_SCALERP];
	isp->video[FIMC_IS_VIDEO_NUM_SCALERP].num_buf = 0;
	isp->video[FIMC_IS_VIDEO_NUM_SCALERP].buf_ref_cnt = 0;

	mutex_lock(&isp->lock);
	if (!test_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state)) {
		isp->sensor_num = 1;
		dbg("++++ IS load fw (Scaler P open)\n");
		mutex_unlock(&isp->lock);
		fimc_is_load_fw(isp);
		bts_change_bus_traffic(&isp->pdev->dev, BTS_INCREASE_BW);

		set_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state);
		clear_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state);
		clear_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state);
		dbg("---- IS load fw (Scaler P open)\n");
	} else {
		mutex_unlock(&isp->lock);
	}

	clear_bit(FIMC_IS_STATE_SCALERP_STREAM_ON, &isp->pipe_state);
	return 0;

}

static int fimc_is_scalerp_video_close(struct file *file)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	int ret;

	dbg("%s\n", __func__);
	vb2_queue_release(&isp->video[FIMC_IS_VIDEO_NUM_SCALERP].vbq);

	mutex_lock(&isp->lock);
	if (!test_bit(FIMC_IS_STATE_SCALERP_STREAM_ON, &isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_SCALERC_STREAM_ON, &isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_3DNR_STREAM_ON, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state)) {

		dbg("++++ IS local power off (Scaler P close)\n");
		mutex_unlock(&isp->lock);
		clear_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state);
		fimc_is_hw_subip_poweroff(isp);
		ret = wait_event_timeout(isp->irq_queue,
			!test_bit(FIMC_IS_PWR_ST_POWER_ON_OFF, &isp->power),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout FIMC_IS_PWR_ST_POWER_ON_OFF\n");
			fimc_is_hw_set_low_poweroff(isp, true);
			clear_bit(FIMC_IS_PWR_ST_POWER_ON_OFF, &isp->power);
			ret = 0;
		}

		dbg("staop flite & mipi (pos:%d) (port:%d)\n",
			isp->sensor.id_position,
			isp->pdata->
			sensor_info[isp->sensor.id_position]->flite_id);
		stop_fimc_lite(isp->pdata->
			sensor_info[isp->sensor.id_position]->flite_id);
		stop_mipi_csi(isp->pdata->
			sensor_info[isp->sensor.id_position]->csi_id);

		fimc_is_hw_a5_power(isp, 0);
		bts_change_bus_traffic(&isp->pdev->dev, BTS_DECREASE_BW);
		clear_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state);
		dbg("---- IS local power off (Scaler P close)\n");
	} else {
		mutex_unlock(&isp->lock);
	}

#if defined(CONFIG_BUSFREQ_OPP) && defined(CONFIG_CPU_EXYNOS5250)
	mutex_lock(&isp->busfreq_lock);
	if (isp->busfreq_num == 1) {
		dev_unlock(isp->bus_dev, &isp->pdev->dev);
		printk(KERN_DEBUG "busfreq locked off\n");
	}
	isp->busfreq_num--;
	if (isp->busfreq_num < 0)
		isp->busfreq_num = 0;
	mutex_unlock(&isp->busfreq_lock);
#endif

	return 0;

}

static unsigned int fimc_is_scalerp_video_poll(struct file *file,
				      struct poll_table_struct *wait)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_poll(&isp->video[FIMC_IS_VIDEO_NUM_SCALERP].vbq, file, wait);

}

static int fimc_is_scalerp_video_mmap(struct file *file,
					struct vm_area_struct *vma)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_mmap(&isp->video[FIMC_IS_VIDEO_NUM_SCALERP].vbq, vma);

}

/*************************************************************************/
/* video ioctl operation						*/
/************************************************************************/

static int fimc_is_scalerp_video_querycap(struct file *file, void *fh,
						struct v4l2_capability *cap)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	strncpy(cap->driver, isp->pdev->name, sizeof(cap->driver) - 1);

	dbg("%s(devname : %s)\n", __func__, cap->driver);
	strncpy(cap->card, isp->pdev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(1, 0, 0);
	cap->capabilities = V4L2_CAP_STREAMING
					| V4L2_CAP_VIDEO_CAPTURE
					| V4L2_CAP_VIDEO_CAPTURE_MPLANE;

	return 0;
}

static int fimc_is_scalerp_video_enum_fmt_mplane(struct file *file, void *priv,
				    struct v4l2_fmtdesc *f)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_scalerp_video_get_format_mplane(struct file *file, void *fh,
						struct v4l2_format *format)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_scalerp_video_set_format_mplane(struct file *file, void *fh,
						struct v4l2_format *format)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	struct v4l2_pix_format_mplane *pix;
	struct fimc_is_fmt *frame;

	dbg("%s\n", __func__);

	pix = &format->fmt.pix_mp;
	frame = find_format(&pix->pixelformat, NULL, 0);

	if (!frame)
		return -EINVAL;

	isp->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.format.pixelformat
							= frame->pixelformat;
	isp->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.format.mbus_code
							= frame->mbus_code;
	isp->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.format.num_planes
							= frame->num_planes;
	isp->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.width = pix->width;
	isp->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.height = pix->height;
	dbg("num_planes : %d\n", frame->num_planes);
	dbg("width : %d\n", pix->width);
	dbg("height : %d\n", pix->height);

	return 0;
}

static int fimc_is_scalerp_video_try_format_mplane(struct file *file, void *fh,
						struct v4l2_format *format)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_scalerp_video_cropcap(struct file *file, void *fh,
						struct v4l2_cropcap *cropcap)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_scalerp_video_get_crop(struct file *file, void *fh,
						struct v4l2_crop *crop)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_scalerp_video_set_crop(struct file *file, void *fh,
						struct v4l2_crop *crop)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_scalerp_video_reqbufs(struct file *file, void *priv,
					struct v4l2_requestbuffers *buf)
{
	int ret;
	struct fimc_is_video_dev *video = file->private_data;
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("(buf->count : %d)\n", buf->count);
	ret = vb2_reqbufs(&video->vbq, buf);
	if (!ret)
		isp->video[FIMC_IS_VIDEO_NUM_SCALERP].num_buf = buf->count;

	if (buf->count == 0)
		isp->video[FIMC_IS_VIDEO_NUM_SCALERP].buf_ref_cnt = 0;

	dbg("(num_buf | %d)\n",
		isp->video[FIMC_IS_VIDEO_NUM_SCALERP].num_buf);

	return ret;
}

static int fimc_is_scalerp_video_querybuf(struct file *file, void *priv,
						struct v4l2_buffer *buf)
{
	int ret;
	struct fimc_is_video_dev *video = file->private_data;

	dbg("%s\n", __func__);
	ret = vb2_querybuf(&video->vbq, buf);

	return ret;
}

static int fimc_is_scalerp_video_qbuf(struct file *file, void *priv,
						struct v4l2_buffer *buf)
{
	int vb_ret;
	struct fimc_is_video_dev *video = file->private_data;
	struct fimc_is_dev *isp = video_drvdata(file);

	if (test_bit(FIMC_IS_STATE_SCALERP_BUFFER_PREPARED, &isp->pipe_state)) {
		video->buf_mask |= (1<<buf->index);
		isp->video[FIMC_IS_VIDEO_NUM_SCALERP].buf_mask
						= video->buf_mask;

		dbg("index(%d) mask(0x%08x)\n", buf->index, video->buf_mask);
	} else
		dbg("index(%d)\n", buf->index);

	vb_ret = vb2_qbuf(&video->vbq, buf);

	return vb_ret;
}

static int fimc_is_scalerp_video_dqbuf(struct file *file, void *priv,
						struct v4l2_buffer *buf)
{
	int vb_ret;
	struct fimc_is_video_dev *video = file->private_data;
	struct fimc_is_dev *isp = video_drvdata(file);

	vb_ret = vb2_dqbuf(&video->vbq, buf, file->f_flags & O_NONBLOCK);

	video->buf_mask &= ~(1<<buf->index);
	isp->video[FIMC_IS_VIDEO_NUM_SCALERP].buf_mask = video->buf_mask;

	dbg("index(%d) mask(0x%08x)\n", buf->index, video->buf_mask);

	return vb_ret;
}

static int fimc_is_scalerp_video_streamon(struct file *file, void *priv,
						enum v4l2_buf_type type)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_streamon(&isp->video[FIMC_IS_VIDEO_NUM_SCALERP].vbq, type);
}

static int fimc_is_scalerp_video_streamoff(struct file *file, void *priv,
						enum v4l2_buf_type type)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_streamoff(&isp->video[FIMC_IS_VIDEO_NUM_SCALERP].vbq, type);
}

static int fimc_is_scalerp_video_enum_input(struct file *file, void *priv,
						struct v4l2_input *input)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	struct exynos5_fimc_is_sensor_info *sensor_info
			= isp->pdata->sensor_info[input->index];

	dbg("index(%d) sensor(%s)\n",
		input->index, sensor_info->sensor_name);
	dbg("pos(%d) sensor_id(%d)\n",
		sensor_info->sensor_position, sensor_info->sensor_id);
	dbg("csi_id(%d) flite_id(%d)\n",
		sensor_info->csi_id, sensor_info->flite_id);
	dbg("i2c_ch(%d)\n", sensor_info->i2c_channel);

	if (input->index >= FIMC_IS_MAX_CAMIF_CLIENTS)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;

	strncpy(input->name, sensor_info->sensor_name,
					FIMC_IS_MAX_NAME_LEN);
	return 0;
}

static int fimc_is_scalerp_video_g_input(struct file *file, void *priv,
						unsigned int *input)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_scalerp_video_s_input(struct file *file, void *priv,
					unsigned int input)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	struct exynos5_fimc_is_sensor_info *sensor_info
			= isp->pdata->sensor_info[input];

	isp->sensor.id_position = input;
	isp->sensor.sensor_type
		= fimc_is_hw_get_sensor_type(sensor_info->sensor_id,
					sensor_info->flite_id);

	fimc_is_hw_set_default_size(isp, sensor_info->sensor_id);
	printk(KERN_INFO "fimc_is_init_set - %d\n", isp->sensor.sensor_type);
	fimc_is_init_set(isp, isp->sensor.sensor_type);

	dbg("sensor info : pos(%d) type(%d)\n", input, isp->sensor.sensor_type);


	return 0;
}
static int fimc_is_scalerp_video_g_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	int ret = 0;

	switch (ctrl->id) {
	/* EXIF information */
	case V4L2_CID_IS_CAMERA_EXIF_EXPTIME:
	case V4L2_CID_CAMERA_EXIF_EXPTIME: /* Exposure Time */
		fimc_is_mem_cache_inv((void *)IS_HEADER(isp),
			(unsigned long)(sizeof(struct is_frame_header)*4));
		ctrl->value = isp->is_p_region->header[0].
			exif.exposure_time.den;
		break;
	case V4L2_CID_IS_CAMERA_EXIF_FLASH:
	case V4L2_CID_CAMERA_EXIF_FLASH: /* Flash */
		fimc_is_mem_cache_inv((void *)IS_HEADER(isp),
			(unsigned long)(sizeof(struct is_frame_header)*4));
		ctrl->value = isp->is_p_region->header[0].exif.flash;
		break;
	case V4L2_CID_IS_CAMERA_EXIF_ISO:
	case V4L2_CID_CAMERA_EXIF_ISO: /* ISO Speed Rating */
		fimc_is_mem_cache_inv((void *)IS_HEADER(isp),
			(unsigned long)(sizeof(struct is_frame_header)*4));
		ctrl->value = isp->is_p_region->header[0].
			exif.iso_speed_rating;
		break;
	case V4L2_CID_IS_CAMERA_EXIF_SHUTTERSPEED:
	case V4L2_CID_CAMERA_EXIF_TV: /* Shutter Speed */
		fimc_is_mem_cache_inv((void *)IS_HEADER(isp),
			(unsigned long)(sizeof(struct is_frame_header)*4));
		/* Exposure time = shutter speed by FW */
		ctrl->value = isp->is_p_region->header[0].
			exif.exposure_time.den;
		break;
	case V4L2_CID_IS_CAMERA_EXIF_BRIGHTNESS:
	case V4L2_CID_CAMERA_EXIF_BV: /* Brightness */
		fimc_is_mem_cache_inv((void *)IS_HEADER(isp),
			(unsigned long)(sizeof(struct is_frame_header)*4));
		ctrl->value = isp->is_p_region->header[0].exif.brightness.num;
		break;
	case V4L2_CID_CAMERA_EXIF_EBV: /* exposure bias */
		fimc_is_mem_cache_inv((void *)IS_HEADER(isp),
			(unsigned long)(sizeof(struct is_frame_header)*4));
		ctrl->value = isp->is_p_region->header[0].exif.brightness.den;
		break;
	/* Get x and y offset of sensor  */
	case V4L2_CID_IS_GET_SENSOR_OFFSET_X:
		ctrl->value = isp->sensor.offset_x;
		break;
	case V4L2_CID_IS_GET_SENSOR_OFFSET_Y:
		ctrl->value = isp->sensor.offset_y;
		break;
	case V4L2_CID_IS_FD_GET_DATA:
		ctrl->value = isp->fd_header.count;
		fimc_is_mem_cache_inv((void *)IS_FACE(isp),
		(unsigned long)(sizeof(struct is_face_marker)*MAX_FACE_COUNT));
		memcpy((void *)isp->fd_header.target_addr,
			&isp->is_p_region->face[isp->fd_header.index],
			(sizeof(struct is_face_marker)*isp->fd_header.count));
		break;
	/* AF result */
	case V4L2_CID_CAMERA_AUTO_FOCUS_RESULT:
		if (!is_af_use(isp))
			ctrl->value = 0x02;
		else
			ctrl->value = isp->af.af_lock_state;
		break;
	/* F/W debug region address */
	case V4L2_CID_IS_FW_DEBUG_REGION_ADDR:
		ctrl->value = isp->mem.base + FIMC_IS_DEBUG_REGION_ADDR;
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static int fimc_is_g_ext_ctrls_handler(struct fimc_is_dev *dev,
	struct v4l2_ext_control *ctrl, int index)
{
	int ret = 0;
	switch (ctrl->id) {
	/* Face Detection CID handler */
	/* 1. Overall information */
	case V4L2_CID_IS_FD_GET_FACE_COUNT:
		ctrl->value = dev->fd_header.count;
		break;
	case V4L2_CID_IS_FD_GET_FACE_FRAME_NUMBER:
		if (dev->fd_header.offset < dev->fd_header.count) {
			ctrl->value =
				dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].frame_number;
		} else {
			ctrl->value = 0;
			return -255;
		}
		break;
	case V4L2_CID_IS_FD_GET_FACE_CONFIDENCE:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].confidence;
		break;
	case V4L2_CID_IS_FD_GET_FACE_SMILE_LEVEL:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].smile_level;
		break;
	case V4L2_CID_IS_FD_GET_FACE_BLINK_LEVEL:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].blink_level;
		break;
	/* 2. Face information */
	case V4L2_CID_IS_FD_GET_FACE_TOPLEFT_X:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].face.offset_x;
		break;
	case V4L2_CID_IS_FD_GET_FACE_TOPLEFT_Y:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].face.offset_y;
		break;
	case V4L2_CID_IS_FD_GET_FACE_BOTTOMRIGHT_X:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].face.offset_x
			+ dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].face.width;
		break;
	case V4L2_CID_IS_FD_GET_FACE_BOTTOMRIGHT_Y:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].face.offset_y
			+ dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].face.height;
		break;
	/* 3. Left eye information */
	case V4L2_CID_IS_FD_GET_LEFT_EYE_TOPLEFT_X:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].left_eye.offset_x;
		break;
	case V4L2_CID_IS_FD_GET_LEFT_EYE_TOPLEFT_Y:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].left_eye.offset_y;
		break;
	case V4L2_CID_IS_FD_GET_LEFT_EYE_BOTTOMRIGHT_X:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].left_eye.offset_x
			+ dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].left_eye.width;
		break;
	case V4L2_CID_IS_FD_GET_LEFT_EYE_BOTTOMRIGHT_Y:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].left_eye.offset_y
			+ dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].left_eye.height;
		break;
	/* 4. Right eye information */
	case V4L2_CID_IS_FD_GET_RIGHT_EYE_TOPLEFT_X:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].right_eye.offset_x;
		break;
	case V4L2_CID_IS_FD_GET_RIGHT_EYE_TOPLEFT_Y:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].right_eye.offset_y;
		break;
	case V4L2_CID_IS_FD_GET_RIGHT_EYE_BOTTOMRIGHT_X:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].right_eye.offset_x
			+ dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].right_eye.width;
		break;
	case V4L2_CID_IS_FD_GET_RIGHT_EYE_BOTTOMRIGHT_Y:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].right_eye.offset_y
			+ dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].right_eye.height;
		break;
	/* 5. Mouth eye information */
	case V4L2_CID_IS_FD_GET_MOUTH_TOPLEFT_X:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].mouth.offset_x;
		break;
	case V4L2_CID_IS_FD_GET_MOUTH_TOPLEFT_Y:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].mouth.offset_y;
		break;
	case V4L2_CID_IS_FD_GET_MOUTH_BOTTOMRIGHT_X:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].mouth.offset_x
			+ dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].mouth.width;
		break;
	case V4L2_CID_IS_FD_GET_MOUTH_BOTTOMRIGHT_Y:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].mouth.offset_y
			+ dev->is_p_region->face[dev->fd_header.index
			+ dev->fd_header.offset].mouth.height;
		break;
	/* 6. Angle information */
	case V4L2_CID_IS_FD_GET_ANGLE:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].roll_angle;
		break;
	case V4L2_CID_IS_FD_GET_YAW_ANGLE:
		ctrl->value = dev->is_p_region->face[dev->fd_header.index
				+ dev->fd_header.offset].yaw_angle;
		break;
	/* 7. Update next face information */
	case V4L2_CID_IS_FD_GET_NEXT:
		dev->fd_header.offset++;
		break;
	default:
		return 255;
		break;
	}
	return ret;
}

static int fimc_is_scalerp_video_g_ext_ctrl(struct file *file, void *priv,
					struct v4l2_ext_controls *ctrls)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	struct v4l2_ext_control *ctrl;
	int i, ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&isp->slock, flags);
	ctrl = ctrls->controls;
	if (!ctrls->ctrl_class == V4L2_CTRL_CLASS_CAMERA)
		return -EINVAL;

	fimc_is_mem_cache_inv((void *)IS_FACE(isp),
		(unsigned long)(sizeof(struct is_face_marker)*MAX_FACE_COUNT));

	isp->fd_header.offset = 0;

	isp->fd_header.width = (s32)isp->sensor.width ;
	isp->fd_header.height = (s32)isp->sensor.height ;

	for (i = 0; i < ctrls->count; i++) {
		ctrl = ctrls->controls + i;
		ret = fimc_is_g_ext_ctrls_handler(isp, ctrl, i);
		if (ret > 0) {
			ctrls->error_idx = i;
			break;
		} else if (ret < 0) {
			ret = 0;
			break;
		}
	}

	isp->fd_header.index = 0;
	isp->fd_header.count = 0;
	spin_unlock_irqrestore(&isp->slock, flags);
	return ret;
}

static int fimc_is_scalerp_video_s_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	int ret = 0;

	dbg("fimc_is_scalerp_video_s_ctrl(%d)(%d)\n", ctrl->id, ctrl->value);
	switch (ctrl->id) {
	case V4L2_CID_IS_CAMERA_SHOT_MODE_NORMAL:
		ret = fimc_is_v4l2_shot_mode(isp, ctrl->value);
		break;
	case V4L2_CID_CAMERA_FRAME_RATE:
#ifdef FRAME_RATE_ENABLE
		/* FW partially supported it */
		ret = fimc_is_v4l2_frame_rate(isp, ctrl->value);
#else
		err("ERR(%s) disabled FRAME_RATE\n", __func__);
#endif
		break;
	/* Focus */
	case V4L2_CID_IS_CAMERA_OBJECT_POSITION_X:
	case V4L2_CID_CAMERA_OBJECT_POSITION_X:
		isp->af.pos_x = ctrl->value;
		break;
	case V4L2_CID_IS_CAMERA_OBJECT_POSITION_Y:
	case V4L2_CID_CAMERA_OBJECT_POSITION_Y:
		isp->af.pos_y = ctrl->value;
		break;
	case V4L2_CID_CAMERA_FOCUS_MODE:
		ret = fimc_is_v4l2_af_mode(isp, ctrl->value);
		break;
	case V4L2_CID_CAMERA_SET_AUTO_FOCUS:
		ret = fimc_is_v4l2_af_start_stop(isp, ctrl->value);
		break;
	case V4L2_CID_CAMERA_TOUCH_AF_START_STOP:
		ret = fimc_is_v4l2_touch_af_start_stop(isp, ctrl->value);
		break;
	case V4L2_CID_CAMERA_CAF_START_STOP:
		ret = fimc_is_v4l2_caf_start_stop(isp, ctrl->value);
		break;
	/* AWB, AE Lock/Unlock */
	case V4L2_CID_CAMERA_AEAWB_LOCK_UNLOCK:
		ret = fimc_is_v4l2_ae_awb_lockunlock(isp, ctrl->value);
		break;
	/* FLASH */
	case V4L2_CID_CAMERA_FLASH_MODE:
		ret = fimc_is_v4l2_isp_flash_mode(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_AWB_MODE:
		ret = fimc_is_v4l2_awb_mode(isp, ctrl->value);
		break;
	case V4L2_CID_CAMERA_WHITE_BALANCE:
		ret = fimc_is_v4l2_awb_mode_legacy(isp, ctrl->value);
		break;
	case V4L2_CID_CAMERA_EFFECT:
		ret = fimc_is_v4l2_isp_effect_legacy(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_IMAGE_EFFECT:
		ret = fimc_is_v4l2_isp_effect(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_ISO:
	case V4L2_CID_CAMERA_ISO:
		ret = fimc_is_v4l2_isp_iso(isp, ctrl->value);
		break;
	case V4L2_CID_CAMERA_CONTRAST:
		ret = fimc_is_v4l2_isp_contrast_legacy(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_CONTRAST:
		ret = fimc_is_v4l2_isp_contrast(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_SATURATION:
	case V4L2_CID_CAMERA_SATURATION:
		ret = fimc_is_v4l2_isp_saturation(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_SHARPNESS:
	case V4L2_CID_CAMERA_SHARPNESS:
		ret = fimc_is_v4l2_isp_sharpness(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_EXPOSURE:
		ret = fimc_is_v4l2_isp_exposure(isp, ctrl->value);
		break;
	case V4L2_CID_CAMERA_BRIGHTNESS:
		ret = fimc_is_v4l2_isp_exposure_legacy(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_BRIGHTNESS:
		ret = fimc_is_v4l2_isp_brightness(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_HUE:
		ret = fimc_is_v4l2_isp_hue(isp, ctrl->value);
		break;
	case V4L2_CID_CAMERA_METERING:
		ret = fimc_is_v4l2_isp_metering_legacy(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_METERING:
		ret = fimc_is_v4l2_isp_metering(isp, ctrl->value);
		break;
	/* Ony valid at SPOT Mode */
	case V4L2_CID_IS_CAMERA_METERING_POSITION_X:
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_METERING_POSITION_Y:
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_METERING_WINDOW_X:
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_METERING_WINDOW_Y:
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(isp, ctrl->value);
		break;
	case V4L2_CID_CAMERA_ANTI_BANDING:
		ret = fimc_is_v4l2_isp_afc_legacy(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_AFC_MODE:
		ret = fimc_is_v4l2_isp_afc(isp, ctrl->value);
		break;
	case V4L2_CID_IS_FD_SET_MAX_FACE_NUMBER:
		/* TODO */
		/*
		if (ctrl->value >= 0) {
			IS_FD_SET_PARAM_FD_CONFIG_CMD(isp,
				FD_CONFIG_COMMAND_MAXIMUM_NUMBER);
			IS_FD_SET_PARAM_FD_CONFIG_MAX_NUMBER(isp, ctrl->value);
			IS_SET_PARAM_BIT(isp, PARAM_FD_CONFIG);
			IS_INC_PARAM_NUM(isp);
			fimc_is_mem_cache_clean((void *)isp->is_p_region,
				IS_PARAM_SIZE);
			fimc_is_hw_set_param(isp);
		}
		*/
		break;
	case V4L2_CID_IS_FD_SET_ROLL_ANGLE:
		ret = fimc_is_v4l2_fd_angle_mode(isp, ctrl->value);
		break;
	case V4L2_CID_IS_FD_SET_DATA_ADDRESS:
		isp->fd_header.target_addr = ctrl->value;
		break;
	case V4L2_CID_IS_SET_ISP:
		ret = fimc_is_v4l2_set_isp(isp, ctrl->value);
		break;
	case V4L2_CID_IS_SET_DRC:
		ret = fimc_is_v4l2_set_drc(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CMD_ISP:
		ret = fimc_is_v4l2_cmd_isp(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CMD_DRC:
		ret = fimc_is_v4l2_cmd_drc(isp, ctrl->value);
		break;
	case V4L2_CID_IS_CMD_FD:
		ret = fimc_is_v4l2_cmd_fd(isp, ctrl->value);
		break;
	case V4L2_CID_CAMERA_SCENE_MODE:
		ret = fimc_is_v4l2_isp_scene_mode(isp, ctrl->value);
		break;
	case V4L2_CID_CAMERA_VT_MODE:
		isp->setfile.sub_index = ctrl->value;
		if (ctrl->value == 1)
			printk(KERN_INFO "VT mode is selected\n");
		break;
	case V4L2_CID_CAMERA_SET_ODC:
#ifdef ODC_ENABLE
		ret = fimc_is_ctrl_odc(isp, ctrl->value);
#else
		err("ERR(%s) disabled ODC\n", __func__);
#endif
		break;
	case V4L2_CID_CAMERA_SET_3DNR:
#ifdef TDNR_ENABLE
		ret = fimc_is_ctrl_3dnr(isp, ctrl->value);
#else
		err("ERR(%s) disabled 3DNR\n", __func__);
#endif
		break;
	case V4L2_CID_CAMERA_ZOOM:
#ifdef DZOOM_ENABLE
		/* FW partially supported it */
		ret = fimc_is_digital_zoom(isp, ctrl->value);
#else
		err("ERR(%s) disabled DZOOM\n", __func__);
#endif
		break;
	case V4L2_CID_CAMERA_SET_DIS:
#ifdef DIS_ENABLE
		/* FW partially supported it */
		ret = fimc_is_ctrl_dis(isp, ctrl->value);
#else
		err("ERR(%s) disabled DIS\n", __func__);
#endif
		break;
	case V4L2_CID_CAMERA_VGA_BLUR:
		break;
	default:
		err("Invalid control\n");
		return -EINVAL;
	}

	return ret;
}

const struct v4l2_file_operations fimc_is_scalerp_video_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_is_scalerp_video_open,
	.release	= fimc_is_scalerp_video_close,
	.poll		= fimc_is_scalerp_video_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= fimc_is_scalerp_video_mmap,
};

const struct v4l2_ioctl_ops fimc_is_scalerp_video_ioctl_ops = {
	.vidioc_querycap		= fimc_is_scalerp_video_querycap,
	.vidioc_enum_fmt_vid_cap_mplane
			= fimc_is_scalerp_video_enum_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane
			= fimc_is_scalerp_video_get_format_mplane,
	.vidioc_s_fmt_vid_cap_mplane
			= fimc_is_scalerp_video_set_format_mplane,
	.vidioc_try_fmt_vid_cap_mplane
			= fimc_is_scalerp_video_try_format_mplane,
	.vidioc_cropcap			= fimc_is_scalerp_video_cropcap,
	.vidioc_g_crop			= fimc_is_scalerp_video_get_crop,
	.vidioc_s_crop			= fimc_is_scalerp_video_set_crop,
	.vidioc_reqbufs			= fimc_is_scalerp_video_reqbufs,
	.vidioc_querybuf		= fimc_is_scalerp_video_querybuf,
	.vidioc_qbuf			= fimc_is_scalerp_video_qbuf,
	.vidioc_dqbuf			= fimc_is_scalerp_video_dqbuf,
	.vidioc_streamon		= fimc_is_scalerp_video_streamon,
	.vidioc_streamoff		= fimc_is_scalerp_video_streamoff,
	.vidioc_enum_input		= fimc_is_scalerp_video_enum_input,
	.vidioc_g_input			= fimc_is_scalerp_video_g_input,
	.vidioc_s_input			= fimc_is_scalerp_video_s_input,
	.vidioc_g_ctrl			= fimc_is_scalerp_video_g_ctrl,
	.vidioc_s_ctrl			= fimc_is_scalerp_video_s_ctrl,
	.vidioc_g_ext_ctrls		= fimc_is_scalerp_video_g_ext_ctrl,
};

static int fimc_is_scalerp_queue_setup(struct vb2_queue *vq,
			const struct v4l2_format *fmt,
			unsigned int *num_buffers,
			unsigned int *num_planes, unsigned int sizes[],
			void *allocators[])
{

	struct fimc_is_video_dev *video = vq->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	int i;


	*num_planes = isp->video[FIMC_IS_VIDEO_NUM_SCALERP].
					frame.format.num_planes;
	set_plane_size(&isp->video[FIMC_IS_VIDEO_NUM_SCALERP].frame, sizes);

	for (i = 0; i < *num_planes; i++)
		allocators[i] =  isp->alloc_ctx;

	dbg("(num_planes : %d)(size : %d)\n", (int)*num_planes, (int)sizes[0]);

	return 0;
}
static int fimc_is_scalerp_buffer_prepare(struct vb2_buffer *vb)
{
	dbg("--%s\n", __func__);
	return 0;
}


static inline void fimc_is_scalerp_lock(struct vb2_queue *vq)
{
	dbg("%s\n", __func__);
}

static inline void fimc_is_scalerp_unlock(struct vb2_queue *vq)
{
	dbg("%s\n", __func__);
}

static int fimc_is_scalerp_start_streaming(struct vb2_queue *q,
						unsigned int count)
{
	struct fimc_is_video_dev *video = q->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	int ret;
	int i, j;
	int buf_index;

	dbg("%s(pipe_state : %d)\n", __func__, (int)isp->pipe_state);

	if (test_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state)) {

		dbg("IS change mode\n");
		clear_bit(IS_ST_RUN, &isp->state);
		set_bit(IS_ST_CHANGE_MODE, &isp->state);
		fimc_is_hw_change_mode(isp, IS_MODE_PREVIEW_STILL);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_CHANGE_MODE_DONE,
			&isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"Mode change timeout:%s\n", __func__);
			return -EBUSY;
		}

		set_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state);
	}

	if (test_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_SCALERP_BUFFER_PREPARED,
			&isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state)) {
		dbg("IS Stream On");
		fimc_is_hw_set_stream(isp, 1);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_STREAM_ON, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
			return -EBUSY;
		}
		clear_bit(IS_ST_STREAM_ON, &isp->state);

		set_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state);
	}

	if (test_bit(FIMC_IS_STATE_SCALERP_BUFFER_PREPARED, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state)) {

		/* buffer addr setting */
		for (i = 0; i < isp->video[FIMC_IS_VIDEO_NUM_SCALERP].
				num_buf; i++)
			for (j = 0; j < isp->video[FIMC_IS_VIDEO_NUM_SCALERP].
					frame.format.num_planes; j++) {
				buf_index = i*isp->
					video[FIMC_IS_VIDEO_NUM_SCALERP].
					frame.format.num_planes + j;

				dbg("(%d)set buf(%d:%d) = 0x%08x\n",
					buf_index, i, j,
					isp->video[FIMC_IS_VIDEO_NUM_SCALERP].
					buf[i][j]);

				isp->is_p_region->shared[400+buf_index]
					= isp->video[FIMC_IS_VIDEO_NUM_SCALERP].
					buf[i][j];
		}

		dbg("buf_num:%d buf_plane:%d shared[400] : 0x%p\n",
			isp->video[FIMC_IS_VIDEO_NUM_SCALERP].num_buf,
			isp->video[FIMC_IS_VIDEO_NUM_SCALERP].
			frame.format.num_planes,
			isp->mem.kvaddr_shared + 400 * sizeof(u32));

		isp->video[FIMC_IS_VIDEO_NUM_SCALERP].buf_mask = 0;
		for (i = 0; i < isp->video[FIMC_IS_VIDEO_NUM_SCALERP].
				num_buf; i++)
			isp->video[FIMC_IS_VIDEO_NUM_SCALERP].buf_mask
								|= (1 << i);
		dbg("initial buffer mask : 0x%08x\n",
			isp->video[FIMC_IS_VIDEO_NUM_SCALERP].buf_mask);

		IS_SCALERP_SET_PARAM_DMA_OUTPUT_CMD(isp,
			DMA_OUTPUT_COMMAND_ENABLE);
		IS_SCALERP_SET_PARAM_DMA_OUTPUT_MASK(isp,
			isp->video[FIMC_IS_VIDEO_NUM_SCALERP].buf_mask);
		IS_SCALERP_SET_PARAM_DMA_OUTPUT_BUFFERNUM(isp,
			isp->video[FIMC_IS_VIDEO_NUM_SCALERP].num_buf);
		IS_SCALERP_SET_PARAM_DMA_OUTPUT_BUFFERADDR(isp,
			(u32)isp->mem.dvaddr_shared + 400*sizeof(u32));
		IS_SET_PARAM_BIT(isp, PARAM_SCALERP_DMA_OUTPUT);
		IS_INC_PARAM_NUM(isp);

		fimc_is_mem_cache_clean((void *)isp->is_p_region,
			IS_PARAM_SIZE);

		isp->scenario_id = ISS_PREVIEW_STILL;
		set_bit(IS_ST_INIT_PREVIEW_STILL,	&isp->state);
		clear_bit(IS_ST_INIT_CAPTURE_STILL, &isp->state);
		clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state);
		fimc_is_hw_set_param(isp);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
			return -EBUSY;
		}

		set_bit(FIMC_IS_STATE_SCALERP_STREAM_ON, &isp->pipe_state);

#ifdef DZOOM_EVT0
		printk(KERN_INFO "DZOOM_EVT0 is enabled\n");
		clear_bit(IS_ST_SCALERP_FRAME_DONE, &isp->state);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_SCALERP_FRAME_DONE, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
			return -EBUSY;
		}
		dbg("DRC stop\n");
		IS_DRC_SET_PARAM_CONTROL_CMD(isp,
			CONTROL_COMMAND_STOP);
		IS_SET_PARAM_BIT(isp, PARAM_DRC_CONTROL);
		IS_INC_PARAM_NUM(isp);
		fimc_is_mem_cache_clean((void *)isp->is_p_region,
			IS_PARAM_SIZE);

		isp->scenario_id = ISS_PREVIEW_STILL;
		set_bit(IS_ST_INIT_PREVIEW_STILL,	&isp->state);
		clear_bit(IS_ST_INIT_CAPTURE_STILL, &isp->state);
		clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state);
		fimc_is_hw_set_param(isp);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
			return -EBUSY;
		}

		dbg("DRC change path\n");
		IS_DRC_SET_PARAM_OTF_INPUT_CMD(isp,
			OTF_INPUT_COMMAND_DISABLE);
		IS_SET_PARAM_BIT(isp, PARAM_DRC_OTF_INPUT);
		IS_INC_PARAM_NUM(isp);

		IS_DRC_SET_PARAM_DMA_INPUT_CMD(isp,
			DMA_INPUT_COMMAND_ENABLE);
		IS_DRC_SET_PARAM_DMA_INPUT_BUFFERNUM(isp,
			1);
		isp->is_p_region->shared[100] = (u32)isp->mem.dvaddr_isp;
		IS_DRC_SET_PARAM_DMA_INPUT_BUFFERADDR(isp,
			(u32)isp->mem.dvaddr_shared + 100*sizeof(u32));
		dbg("isp phy addr : 0x%08x\n",
			(long unsigned int)virt_to_phys(isp->mem.kvaddr_isp));
		dbg("isp dvaddr : 0x%08x\n",
			(long unsigned int)isp->mem.dvaddr_isp);
		IS_SET_PARAM_BIT(isp, PARAM_DRC_DMA_INPUT);
		IS_INC_PARAM_NUM(isp);

		fimc_is_mem_cache_clean((void *)isp->is_p_region,
			IS_PARAM_SIZE);

		isp->scenario_id = ISS_PREVIEW_STILL;
		set_bit(IS_ST_INIT_PREVIEW_STILL,	&isp->state);
		clear_bit(IS_ST_INIT_CAPTURE_STILL, &isp->state);
		clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state);
		fimc_is_hw_set_param(isp);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
			return -EBUSY;
		}

		dbg("DRC start\n");
		IS_DRC_SET_PARAM_CONTROL_CMD(isp,
			CONTROL_COMMAND_START);
		IS_SET_PARAM_BIT(isp, PARAM_DRC_CONTROL);
		IS_INC_PARAM_NUM(isp);
		fimc_is_mem_cache_clean((void *)isp->is_p_region,
			IS_PARAM_SIZE);

		isp->scenario_id = ISS_PREVIEW_STILL;
		set_bit(IS_ST_INIT_PREVIEW_STILL,	&isp->state);
		clear_bit(IS_ST_INIT_CAPTURE_STILL, &isp->state);
		clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state);
		fimc_is_hw_set_param(isp);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
			return -EBUSY;
		}
#endif
	}

	return 0;
}

static int fimc_is_scalerp_stop_streaming(struct vb2_queue *q)
{
	struct fimc_is_video_dev *video = q->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	int ret;


	clear_bit(IS_ST_STREAM_OFF, &isp->state);
	fimc_is_hw_set_stream(isp, 0);
	mutex_lock(&isp->lock);
	ret = wait_event_timeout(isp->irq_queue,
		test_bit(IS_ST_STREAM_OFF, &isp->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	mutex_unlock(&isp->lock);
	if (!ret) {
		dev_err(&isp->pdev->dev,
			"wait timeout : %s\n", __func__);
		if (!ret)
			err("s_power off failed!!\n");
		return -EBUSY;
	}

	IS_SCALERP_SET_PARAM_DMA_OUTPUT_CMD(isp,
		DMA_OUTPUT_COMMAND_DISABLE);
	IS_SCALERP_SET_PARAM_DMA_OUTPUT_BUFFERNUM(isp,
		0);
	IS_SCALERP_SET_PARAM_DMA_OUTPUT_BUFFERADDR(isp,
		0);

	IS_SET_PARAM_BIT(isp, PARAM_SCALERP_DMA_OUTPUT);
	IS_INC_PARAM_NUM(isp);

	fimc_is_mem_cache_clean((void *)isp->is_p_region,
		IS_PARAM_SIZE);

	isp->scenario_id = ISS_PREVIEW_STILL;
	set_bit(IS_ST_INIT_PREVIEW_STILL,	&isp->state);
	clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state);
	fimc_is_hw_set_param(isp);

	mutex_lock(&isp->lock);
	ret = wait_event_timeout(isp->irq_queue,
		test_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	mutex_unlock(&isp->lock);
	if (!ret) {
		dev_err(&isp->pdev->dev,
			"wait timeout 2: %s\n", __func__);
		return -EBUSY;
	}

	dbg("IS change mode\n");
	clear_bit(IS_ST_RUN, &isp->state);
	set_bit(IS_ST_CHANGE_MODE, &isp->state);
	fimc_is_hw_change_mode(isp, IS_MODE_PREVIEW_STILL);
	mutex_lock(&isp->lock);
	ret = wait_event_timeout(isp->irq_queue,
		test_bit(IS_ST_CHANGE_MODE_DONE, &isp->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	mutex_unlock(&isp->lock);
	if (!ret) {
		dev_err(&isp->pdev->dev,
			"Mode change timeout:%s\n", __func__);
		return -EBUSY;
	}

	dbg("IS Stream On\n");
	fimc_is_hw_set_stream(isp, 1);

	mutex_lock(&isp->lock);
	ret = wait_event_timeout(isp->irq_queue,
		test_bit(IS_ST_STREAM_ON, &isp->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	mutex_unlock(&isp->lock);
	if (!ret) {
		dev_err(&isp->pdev->dev,
			"wait timeout : %s\n", __func__);
		return -EBUSY;
	}
	clear_bit(IS_ST_STREAM_ON, &isp->state);

	if (!test_bit(FIMC_IS_STATE_SCALERC_STREAM_ON, &isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_3DNR_STREAM_ON, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state)) {
		clear_bit(IS_ST_STREAM_OFF, &isp->state);
		dbg("IS Stream Off");
		fimc_is_hw_set_stream(isp, 0);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_STREAM_OFF, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout 4: %s\n", __func__);
			return -EBUSY;
		}
		clear_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state);
	}
	clear_bit(IS_ST_RUN, &isp->state);
	clear_bit(IS_ST_STREAM_ON, &isp->state);
	clear_bit(FIMC_IS_STATE_SCALERP_BUFFER_PREPARED, &isp->pipe_state);
	clear_bit(FIMC_IS_STATE_SCALERP_STREAM_ON, &isp->pipe_state);

	isp->setfile.sub_index = 0;

	return 0;
}

static void fimc_is_scalerp_buffer_queue(struct vb2_buffer *vb)
{
	struct fimc_is_video_dev *video = vb->vb2_queue->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	unsigned int i;

	dbg("%s\n", __func__);
	isp->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.format.num_planes
							= vb->num_planes;

	if (!test_bit(FIMC_IS_STATE_SCALERP_BUFFER_PREPARED,
							&isp->pipe_state)) {
		for (i = 0; i < vb->num_planes; i++) {
			isp->video[FIMC_IS_VIDEO_NUM_SCALERP].
				buf[vb->v4l2_buf.index][i]
				= isp->vb2->plane_addr(vb, i);
			dbg("index(%d)(%d) deviceVaddr(0x%08x)\n",
				vb->v4l2_buf.index, i,
				isp->video[FIMC_IS_VIDEO_NUM_SCALERP].
				buf[vb->v4l2_buf.index][i]);
		}

		isp->video[FIMC_IS_VIDEO_NUM_SCALERP].buf_ref_cnt++;

		if (isp->video[FIMC_IS_VIDEO_NUM_SCALERP].num_buf
			== isp->video[FIMC_IS_VIDEO_NUM_SCALERP].buf_ref_cnt) {
			set_bit(FIMC_IS_STATE_SCALERP_BUFFER_PREPARED,
				&isp->pipe_state);
			dbg("FIMC_IS_STATE_SCALERP_BUFFER_PREPARED\n");
		}
	}

	if (!test_bit(FIMC_IS_STATE_SCALERP_STREAM_ON, &isp->pipe_state))
		fimc_is_scalerp_start_streaming(vb->vb2_queue,
			isp->video[FIMC_IS_VIDEO_NUM_SCALERP].num_buf);

	return;
}

const struct vb2_ops fimc_is_scalerp_qops = {
	.queue_setup		= fimc_is_scalerp_queue_setup,
	.buf_prepare		= fimc_is_scalerp_buffer_prepare,
	.buf_queue		= fimc_is_scalerp_buffer_queue,
	.wait_prepare		= fimc_is_scalerp_unlock,
	.wait_finish		= fimc_is_scalerp_lock,
	.start_streaming	= fimc_is_scalerp_start_streaming,
	.stop_streaming	= fimc_is_scalerp_stop_streaming,
};


/*************************************************************************/
/* video file opertation						 */
/************************************************************************/

static int fimc_is_3dnr_video_open(struct file *file)
{
	struct fimc_is_dev *isp = video_drvdata(file);

#if defined(CONFIG_BUSFREQ_OPP) && defined(CONFIG_CPU_EXYNOS5250)
	mutex_lock(&isp->busfreq_lock);
	isp->busfreq_num++;
	if (isp->busfreq_num == 1) {
		dev_lock(isp->bus_dev, &isp->pdev->dev,
			(FIMC_IS_FREQ_MIF * 1000) + FIMC_IS_FREQ_INT);
		dbg("busfreq locked on <%d/%d>MHz\n",
			FIMC_IS_FREQ_MIF, FIMC_IS_FREQ_INT);
	}
	mutex_unlock(&isp->busfreq_lock);
#endif

	dbg("%s\n", __func__);
	file->private_data = &isp->video[FIMC_IS_VIDEO_NUM_3DNR];
	isp->video[FIMC_IS_VIDEO_NUM_3DNR].num_buf = 0;
	isp->video[FIMC_IS_VIDEO_NUM_3DNR].buf_ref_cnt = 0;

	clear_bit(FIMC_IS_STATE_3DNR_STREAM_ON, &isp->pipe_state);
	return 0;

}

static int fimc_is_3dnr_video_close(struct file *file)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	int ret = 0;

	dbg("%s\n", __func__);
	vb2_queue_release(&isp->video[FIMC_IS_VIDEO_NUM_3DNR].vbq);

	return ret;
}

static unsigned int fimc_is_3dnr_video_poll(struct file *file,
				      struct poll_table_struct *wait)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_poll(&isp->video[FIMC_IS_VIDEO_NUM_3DNR].vbq, file, wait);

}

static int fimc_is_3dnr_video_mmap(struct file *file,
					struct vm_area_struct *vma)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_mmap(&isp->video[FIMC_IS_VIDEO_NUM_3DNR].vbq, vma);

}

/*************************************************************************/
/* video ioctl operation						*/
/************************************************************************/

static int fimc_is_3dnr_video_querycap(struct file *file, void *fh,
					struct v4l2_capability *cap)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	strncpy(cap->driver, isp->pdev->name, sizeof(cap->driver) - 1);

	dbg("%s(devname : %s)\n", __func__, cap->driver);
	strncpy(cap->card, isp->pdev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(1, 0, 0);
	cap->capabilities = V4L2_CAP_STREAMING
				| V4L2_CAP_VIDEO_CAPTURE
				| V4L2_CAP_VIDEO_CAPTURE_MPLANE;

	return 0;
}

static int fimc_is_3dnr_video_enum_fmt_mplane(struct file *file, void *priv,
				    struct v4l2_fmtdesc *f)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_3dnr_video_get_format_mplane(struct file *file, void *fh,
						struct v4l2_format *format)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_3dnr_video_set_format_mplane(struct file *file, void *fh,
						struct v4l2_format *format)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	struct v4l2_pix_format_mplane *pix;
	struct fimc_is_fmt *frame;

	dbg("%s\n", __func__);

	pix = &format->fmt.pix_mp;
	frame = find_format(&pix->pixelformat, NULL, 0);

	if (!frame)
		return -EINVAL;

	isp->video[FIMC_IS_VIDEO_NUM_3DNR].frame.format.pixelformat
						= frame->pixelformat;
	isp->video[FIMC_IS_VIDEO_NUM_3DNR].frame.format.mbus_code
						= frame->mbus_code;
	isp->video[FIMC_IS_VIDEO_NUM_3DNR].frame.format.num_planes
						= frame->num_planes;
	isp->video[FIMC_IS_VIDEO_NUM_3DNR].frame.width = pix->width;
	isp->video[FIMC_IS_VIDEO_NUM_3DNR].frame.height = pix->height;
	dbg("num_planes : %d\n", frame->num_planes);
	dbg("width : %d\n", pix->width);
	dbg("height : %d\n", pix->height);

	return 0;
}

static int fimc_is_3dnr_video_try_format_mplane(struct file *file, void *fh,
						struct v4l2_format *format)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_3dnr_video_cropcap(struct file *file, void *fh,
						struct v4l2_cropcap *cropcap)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_3dnr_video_get_crop(struct file *file, void *fh,
						struct v4l2_crop *crop)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_3dnr_video_set_crop(struct file *file, void *fh,
						struct v4l2_crop *crop)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_3dnr_video_reqbufs(struct file *file, void *priv,
					struct v4l2_requestbuffers *buf)
{
	int ret;
	struct fimc_is_video_dev *video = file->private_data;
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	ret = vb2_reqbufs(&video->vbq, buf);
	if (!ret)
		isp->video[FIMC_IS_VIDEO_NUM_3DNR].num_buf = buf->count;

	if (buf->count == 0)
		isp->video[FIMC_IS_VIDEO_NUM_3DNR].buf_ref_cnt = 0;

	dbg("%s(num_buf | %d)\n", __func__,
		isp->video[FIMC_IS_VIDEO_NUM_3DNR].num_buf);

	return ret;
}

static int fimc_is_3dnr_video_querybuf(struct file *file, void *priv,
						struct v4l2_buffer *buf)
{
	int ret;
	struct fimc_is_video_dev *video = file->private_data;

	dbg("%s\n", __func__);
	ret = vb2_querybuf(&video->vbq, buf);

	return ret;
}

static int fimc_is_3dnr_video_qbuf(struct file *file, void *priv,
						struct v4l2_buffer *buf)
{
	int vb_ret;
	struct fimc_is_video_dev *video = file->private_data;
	struct fimc_is_dev *isp = video_drvdata(file);

	if (test_bit(FIMC_IS_STATE_3DNR_BUFFER_PREPARED, &isp->pipe_state)) {
		video->buf_mask |= (1<<buf->index);
		isp->video[FIMC_IS_VIDEO_NUM_3DNR].buf_mask = video->buf_mask;

		dbg("index(%d) mask(0x%08x)\n", buf->index, video->buf_mask);
	} else {
		dbg("%s :: index(%d)\n", __func__, buf->index);
	}
	vb_ret = vb2_qbuf(&video->vbq, buf);

	return vb_ret;
}

static int fimc_is_3dnr_video_dqbuf(struct file *file, void *priv,
						struct v4l2_buffer *buf)
{
	int vb_ret;
	struct fimc_is_video_dev *video = file->private_data;
	struct fimc_is_dev *isp = video_drvdata(file);

	vb_ret = vb2_dqbuf(&video->vbq, buf, file->f_flags & O_NONBLOCK);

	video->buf_mask &= ~(1<<buf->index);
	isp->video[FIMC_IS_VIDEO_NUM_3DNR].buf_mask = video->buf_mask;

	dbg("index(%d) mask(0x%08x)\n", buf->index, video->buf_mask);

	return vb_ret;
}

static int fimc_is_3dnr_video_streamon(struct file *file, void *priv,
						enum v4l2_buf_type type)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_streamon(&isp->video[FIMC_IS_VIDEO_NUM_3DNR].vbq, type);
}

static int fimc_is_3dnr_video_streamoff(struct file *file, void *priv,
						enum v4l2_buf_type type)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_streamoff(&isp->video[FIMC_IS_VIDEO_NUM_3DNR].vbq, type);
}

static int fimc_is_3dnr_video_enum_input(struct file *file, void *priv,
						struct v4l2_input *input)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	struct exynos5_fimc_is_sensor_info *sensor_info
				= isp->pdata->sensor_info[input->index];

	dbg("index(%d) sensor(%s)\n",
		input->index, sensor_info->sensor_name);
	dbg("pos(%d) sensor_id(%d)\n",
		sensor_info->sensor_position, sensor_info->sensor_id);
	dbg("csi_id(%d) flite_id(%d)\n",
		sensor_info->csi_id, sensor_info->flite_id);
	dbg("i2c_ch(%d)\n", sensor_info->i2c_channel);

	if (input->index >= FIMC_IS_MAX_CAMIF_CLIENTS)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;

	strncpy(input->name, sensor_info->sensor_name,
					FIMC_IS_MAX_NAME_LEN);
	return 0;
}

static int fimc_is_3dnr_video_g_input(struct file *file, void *priv,
						unsigned int *input)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_3dnr_video_s_input(struct file *file, void *priv,
						unsigned int input)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	struct exynos5_fimc_is_sensor_info *sensor_info
			= isp->pdata->sensor_info[input];

	isp->sensor.id_position = input;
	isp->sensor.sensor_type
		= fimc_is_hw_get_sensor_type(sensor_info->sensor_id,
						sensor_info->flite_id);

	fimc_is_hw_set_default_size(isp, sensor_info->sensor_id);

	dbg("sensor info : pos(%d) type(%d)\n", input, isp->sensor.sensor_type);


	return 0;
}

const struct v4l2_file_operations fimc_is_3dnr_video_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_is_3dnr_video_open,
	.release	= fimc_is_3dnr_video_close,
	.poll		= fimc_is_3dnr_video_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= fimc_is_3dnr_video_mmap,
};

const struct v4l2_ioctl_ops fimc_is_3dnr_video_ioctl_ops = {
	.vidioc_querycap		= fimc_is_3dnr_video_querycap,
	.vidioc_enum_fmt_vid_cap_mplane	= fimc_is_3dnr_video_enum_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= fimc_is_3dnr_video_get_format_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= fimc_is_3dnr_video_set_format_mplane,
	.vidioc_try_fmt_vid_cap_mplane	= fimc_is_3dnr_video_try_format_mplane,
	.vidioc_cropcap			= fimc_is_3dnr_video_cropcap,
	.vidioc_g_crop			= fimc_is_3dnr_video_get_crop,
	.vidioc_s_crop			= fimc_is_3dnr_video_set_crop,
	.vidioc_reqbufs			= fimc_is_3dnr_video_reqbufs,
	.vidioc_querybuf		= fimc_is_3dnr_video_querybuf,
	.vidioc_qbuf			= fimc_is_3dnr_video_qbuf,
	.vidioc_dqbuf			= fimc_is_3dnr_video_dqbuf,
	.vidioc_streamon		= fimc_is_3dnr_video_streamon,
	.vidioc_streamoff		= fimc_is_3dnr_video_streamoff,
	.vidioc_enum_input		= fimc_is_3dnr_video_enum_input,
	.vidioc_g_input			= fimc_is_3dnr_video_g_input,
	.vidioc_s_input			= fimc_is_3dnr_video_s_input,
};

static int fimc_is_3dnr_queue_setup(struct vb2_queue *vq,
			const struct v4l2_format *fmt,
			unsigned int *num_buffers,
			unsigned int *num_planes, unsigned int sizes[],
			void *allocators[])
{

	struct fimc_is_video_dev *video = vq->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	int i;

	*num_planes = isp->video[FIMC_IS_VIDEO_NUM_3DNR].
			frame.format.num_planes;
	set_plane_size(&isp->video[FIMC_IS_VIDEO_NUM_3DNR].frame, sizes);

	for (i = 0; i < *num_planes; i++)
		allocators[i] =  isp->alloc_ctx;

	dbg("(num_planes : %d)(size : %d)\n", (int)*num_planes, (int)sizes[0]);

	return 0;
}

static int fimc_is_3dnr_buffer_prepare(struct vb2_buffer *vb)
{
	dbg("--%s\n", __func__);
	return 0;
}

static inline void fimc_is_3dnr_lock(struct vb2_queue *vq)
{
	dbg("%s\n", __func__);
}

static inline void fimc_is_3dnr_unlock(struct vb2_queue *vq)
{
	dbg("%s\n", __func__);
}

static int fimc_is_3dnr_start_streaming(struct vb2_queue *q,
						unsigned int count)
{
	struct fimc_is_video_dev *video = q->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	int ret;
	int i, j;
	int buf_index;

	dbg("%s(pipe_state : %d)\n", __func__, (int)isp->pipe_state);

	if (test_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state)) {

		dbg("IS_ST_CHANGE_MODE\n");
		set_bit(IS_ST_CHANGE_MODE, &isp->state);
		fimc_is_hw_change_mode(isp, IS_MODE_PREVIEW_STILL);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_CHANGE_MODE_DONE,
			&isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"Mode change timeout:%s\n", __func__);
			return -EBUSY;
		}

		set_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state);
	}

	if (test_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_3DNR_BUFFER_PREPARED,
			&isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state)) {
		dbg("IS Stream On\n");
		fimc_is_hw_set_stream(isp, 1);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_STREAM_ON, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
			return -EBUSY;
		}
		clear_bit(IS_ST_STREAM_ON, &isp->state);

		set_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state);
	}

	if (test_bit(FIMC_IS_STATE_3DNR_BUFFER_PREPARED, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state)) {
		/* buffer addr setting */
		for (i = 0; i < isp->video[FIMC_IS_VIDEO_NUM_3DNR].num_buf; i++)
			for (j = 0; j < isp->video[FIMC_IS_VIDEO_NUM_3DNR].
					frame.format.num_planes; j++) {
				buf_index
				= i * isp->video[FIMC_IS_VIDEO_NUM_3DNR].
						frame.format.num_planes + j;
				dbg("(%d)set buf(%d:%d) = 0x%08x\n",
					buf_index, i, j,
					isp->video[FIMC_IS_VIDEO_NUM_3DNR].
					buf[i][j]);
				isp->is_p_region->shared[350+buf_index]
				= isp->video[FIMC_IS_VIDEO_NUM_3DNR].buf[i][j];
		}

		dbg("buf_num:%d buf_plane:%d shared[350] : 0x%p\n",
			isp->video[FIMC_IS_VIDEO_NUM_3DNR].num_buf,
			isp->video[FIMC_IS_VIDEO_NUM_3DNR].
			frame.format.num_planes,
			isp->mem.kvaddr_shared + 350 * sizeof(u32));
		for (i = 0; i < isp->video[FIMC_IS_VIDEO_NUM_3DNR].num_buf; i++)
			isp->video[FIMC_IS_VIDEO_NUM_3DNR].buf_mask |= (1 << i);
		dbg("initial buffer mask : 0x%08x\n",
			isp->video[FIMC_IS_VIDEO_NUM_3DNR].buf_mask);

		IS_TDNR_SET_PARAM_DMA_OUTPUT_CMD(isp,
			DMA_OUTPUT_COMMAND_ENABLE);
		IS_TDNR_SET_PARAM_DMA_OUTPUT_MASK(isp,
			isp->video[FIMC_IS_VIDEO_NUM_3DNR].buf_mask);
		IS_TDNR_SET_PARAM_DMA_OUTPUT_BUFFERNUM(isp,
			isp->video[FIMC_IS_VIDEO_NUM_3DNR].num_buf);
		IS_TDNR_SET_PARAM_DMA_OUTPUT_BUFFERADDR(isp,
			(u32)isp->mem.dvaddr_shared + 350*sizeof(u32));

		IS_SET_PARAM_BIT(isp, PARAM_TDNR_DMA_OUTPUT);
		IS_INC_PARAM_NUM(isp);

		fimc_is_mem_cache_clean((void *)isp->is_p_region,
			IS_PARAM_SIZE);

		isp->scenario_id = ISS_PREVIEW_STILL;
		set_bit(IS_ST_INIT_PREVIEW_STILL,	&isp->state);
		clear_bit(IS_ST_INIT_CAPTURE_STILL, &isp->state);
		clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state);
		fimc_is_hw_set_param(isp);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
			return -EBUSY;
		}

		set_bit(FIMC_IS_STATE_3DNR_STREAM_ON, &isp->pipe_state);
	}

	return 0;
}

static int fimc_is_3dnr_stop_streaming(struct vb2_queue *q)
{
	struct fimc_is_video_dev *video = q->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	int ret;



	clear_bit(IS_ST_STREAM_OFF, &isp->state);
	fimc_is_hw_set_stream(isp, 0);
	mutex_lock(&isp->lock);
	ret = wait_event_timeout(isp->irq_queue,
		test_bit(IS_ST_STREAM_OFF, &isp->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	mutex_unlock(&isp->lock);
	if (!ret) {
		dev_err(&isp->pdev->dev,
			"wait timeout : %s\n", __func__);
		if (!ret)
			err("s_power off failed!!\n");
		return -EBUSY;
	}

	IS_TDNR_SET_PARAM_DMA_OUTPUT_CMD(isp,
		DMA_OUTPUT_COMMAND_DISABLE);
	IS_TDNR_SET_PARAM_DMA_OUTPUT_BUFFERNUM(isp,
		0);
	IS_TDNR_SET_PARAM_DMA_OUTPUT_BUFFERADDR(isp,
		0);

	IS_SET_PARAM_BIT(isp, PARAM_TDNR_DMA_OUTPUT);
	IS_INC_PARAM_NUM(isp);

	fimc_is_mem_cache_clean((void *)isp->is_p_region,
		IS_PARAM_SIZE);

	isp->scenario_id = ISS_PREVIEW_STILL;
	set_bit(IS_ST_INIT_PREVIEW_STILL,	&isp->state);
	clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state);
	fimc_is_hw_set_param(isp);

	mutex_lock(&isp->lock);
	ret = wait_event_timeout(isp->irq_queue,
		test_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	mutex_unlock(&isp->lock);
	if (!ret) {
		dev_err(&isp->pdev->dev,
			"wait timeout 1: %s\n", __func__);
		return -EBUSY;
	}


	dbg("IS change mode\n");
	clear_bit(IS_ST_RUN, &isp->state);
	set_bit(IS_ST_CHANGE_MODE, &isp->state);
	fimc_is_hw_change_mode(isp, IS_MODE_PREVIEW_STILL);
	mutex_lock(&isp->lock);
	ret = wait_event_timeout(isp->irq_queue,
		test_bit(IS_ST_CHANGE_MODE_DONE, &isp->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	mutex_unlock(&isp->lock);
	if (!ret) {
		dev_err(&isp->pdev->dev,
			"Mode change timeout:%s\n", __func__);
		return -EBUSY;
	}

	dbg("IS Stream On");
	fimc_is_hw_set_stream(isp, 1);

	mutex_lock(&isp->lock);
	ret = wait_event_timeout(isp->irq_queue,
		test_bit(IS_ST_STREAM_ON, &isp->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	mutex_unlock(&isp->lock);
	if (!ret) {
		dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
		return -EBUSY;
	}
	clear_bit(IS_ST_STREAM_ON, &isp->state);


	if (!test_bit(FIMC_IS_STATE_SCALERC_STREAM_ON, &isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_SCALERP_STREAM_ON, &isp->pipe_state)) {
		clear_bit(IS_ST_STREAM_OFF, &isp->state);
		fimc_is_hw_set_stream(isp, 0);
		dbg("IS Stream Off");
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_STREAM_OFF, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout 4: %s\n", __func__);
			return -EBUSY;
		}
		clear_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state);
	}

	clear_bit(IS_ST_RUN, &isp->state);
	clear_bit(IS_ST_STREAM_ON, &isp->state);
	clear_bit(FIMC_IS_STATE_3DNR_BUFFER_PREPARED, &isp->pipe_state);
	clear_bit(FIMC_IS_STATE_3DNR_STREAM_ON, &isp->pipe_state);


	return 0;
}

static void fimc_is_3dnr_buffer_queue(struct vb2_buffer *vb)
{
	struct fimc_is_video_dev *video = vb->vb2_queue->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	unsigned int i;

	dbg("%s\n", __func__);
	isp->video[FIMC_IS_VIDEO_NUM_3DNR].frame.format.num_planes
						= vb->num_planes;

	if (!test_bit(FIMC_IS_STATE_3DNR_BUFFER_PREPARED, &isp->pipe_state)) {
		for (i = 0; i < vb->num_planes; i++) {
			isp->video[FIMC_IS_VIDEO_NUM_3DNR].
				buf[vb->v4l2_buf.index][i]
				= isp->vb2->plane_addr(vb, i);
			dbg("index(%d)(%d) deviceVaddr(0x%08x)\n",
				vb->v4l2_buf.index, i,
				isp->video[FIMC_IS_VIDEO_NUM_3DNR].
				buf[vb->v4l2_buf.index][i]);
		}

		isp->video[FIMC_IS_VIDEO_NUM_3DNR].buf_ref_cnt++;

		if (isp->video[FIMC_IS_VIDEO_NUM_3DNR].num_buf
			== isp->video[FIMC_IS_VIDEO_NUM_3DNR].buf_ref_cnt)
			set_bit(FIMC_IS_STATE_3DNR_BUFFER_PREPARED,
				&isp->pipe_state);
	}

	if (!test_bit(FIMC_IS_STATE_3DNR_STREAM_ON, &isp->pipe_state))
		fimc_is_3dnr_start_streaming(vb->vb2_queue,
			isp->video[FIMC_IS_VIDEO_NUM_3DNR].num_buf);

	return;
}

const struct vb2_ops fimc_is_3dnr_qops = {
	.queue_setup		= fimc_is_3dnr_queue_setup,
	.buf_prepare		= fimc_is_3dnr_buffer_prepare,
	.buf_queue		= fimc_is_3dnr_buffer_queue,
	.wait_prepare		= fimc_is_3dnr_unlock,
	.wait_finish		= fimc_is_3dnr_lock,
	.start_streaming	= fimc_is_3dnr_start_streaming,
	.stop_streaming	= fimc_is_3dnr_stop_streaming,
};


/*************************************************************************/
/* video file opertation						 */
/************************************************************************/

static int fimc_is_bayer_video_open(struct file *file)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	file->private_data = &isp->video[FIMC_IS_VIDEO_NUM_BAYER];
	isp->video[FIMC_IS_VIDEO_NUM_BAYER].num_buf = 0;
	isp->video[FIMC_IS_VIDEO_NUM_BAYER].buf_ref_cnt = 0;

	if (!test_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state)) {
		isp->sensor_num = 1;

		fimc_is_load_fw(isp);

		set_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state);
		clear_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state);
		clear_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state);
	}

	clear_bit(FIMC_IS_STATE_BAYER_STREAM_ON, &isp->pipe_state);
	fimc_is_fw_clear_irq1_all(isp);
	return 0;

}

static int fimc_is_bayer_video_close(struct file *file)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	int ret;

	dbg("%s\n", __func__);
	vb2_queue_release(&isp->video[FIMC_IS_VIDEO_NUM_BAYER].vbq);

	if (!test_bit(FIMC_IS_STATE_SCALERP_STREAM_ON, &isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_SCALERC_STREAM_ON, &isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_3DNR_STREAM_ON, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->power)) {
		clear_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state);
		fimc_is_hw_subip_poweroff(isp);

		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			!test_bit(FIMC_IS_PWR_ST_POWER_ON_OFF, &isp->power),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		mutex_unlock(&isp->lock);

		if (!ret) {
			err("wait timeout : %s\n", __func__);
			ret = -EINVAL;
		}

		dbg("staop flite & mipi (pos:%d) (port:%d)\n",
			isp->sensor.id_position,
			isp->pdata->
			sensor_info[isp->sensor.id_position]->flite_id);

		stop_fimc_lite(isp->pdata->
				sensor_info[isp->sensor.id_position]->flite_id);
		stop_mipi_csi(isp->pdata->
				sensor_info[isp->sensor.id_position]->csi_id);

		fimc_is_hw_a5_power(isp, 0);
		clear_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state);
	}
	return 0;

}

static unsigned int fimc_is_bayer_video_poll(struct file *file,
				      struct poll_table_struct *wait)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_poll(&isp->video[FIMC_IS_VIDEO_NUM_BAYER].vbq, file, wait);

}

static int fimc_is_bayer_video_mmap(struct file *file,
					struct vm_area_struct *vma)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_mmap(&isp->video[FIMC_IS_VIDEO_NUM_BAYER].vbq, vma);

}

/*************************************************************************/
/* video ioctl operation						*/
/************************************************************************/

static int fimc_is_bayer_video_querycap(struct file *file, void *fh,
					struct v4l2_capability *cap)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	strncpy(cap->driver, isp->pdev->name, sizeof(cap->driver) - 1);

	dbg("%s(devname : %s)\n", __func__, cap->driver);
	strncpy(cap->card, isp->pdev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(1, 0, 0);
	cap->capabilities = V4L2_CAP_STREAMING
				| V4L2_CAP_VIDEO_CAPTURE
				| V4L2_CAP_VIDEO_CAPTURE_MPLANE;

	return 0;
}

static int fimc_is_bayer_video_enum_fmt_mplane(struct file *file, void *priv,
				    struct v4l2_fmtdesc *f)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_bayer_video_get_format_mplane(struct file *file, void *fh,
						struct v4l2_format *format)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_bayer_video_set_format_mplane(struct file *file, void *fh,
						struct v4l2_format *format)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	struct v4l2_pix_format_mplane *pix;
	struct fimc_is_fmt *frame;

	dbg("%s\n", __func__);

	pix = &format->fmt.pix_mp;
	frame = find_format(&pix->pixelformat, NULL, 0);

	if (!frame)
		return -EINVAL;

	isp->video[FIMC_IS_VIDEO_NUM_BAYER].frame.format.pixelformat
							= frame->pixelformat;
	isp->video[FIMC_IS_VIDEO_NUM_BAYER].frame.format.mbus_code
							= frame->mbus_code;
	isp->video[FIMC_IS_VIDEO_NUM_BAYER].frame.format.num_planes
							= frame->num_planes;
	isp->video[FIMC_IS_VIDEO_NUM_BAYER].frame.width = pix->width;
	isp->video[FIMC_IS_VIDEO_NUM_BAYER].frame.height = pix->height;
	dbg("num_planes : %d\n", frame->num_planes);
	dbg("width : %d\n", pix->width);
	dbg("height : %d\n", pix->height);

	return 0;
}

static int fimc_is_bayer_video_try_format_mplane(struct file *file, void *fh,
						struct v4l2_format *format)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_bayer_video_cropcap(struct file *file, void *fh,
						struct v4l2_cropcap *cropcap)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_bayer_video_get_crop(struct file *file, void *fh,
						struct v4l2_crop *crop)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_bayer_video_set_crop(struct file *file, void *fh,
						struct v4l2_crop *crop)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_bayer_video_reqbufs(struct file *file, void *priv,
					struct v4l2_requestbuffers *buf)
{
	int ret;
	struct fimc_is_video_dev *video = file->private_data;
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	ret = vb2_reqbufs(&video->vbq, buf);
	if (!ret)
		isp->video[FIMC_IS_VIDEO_NUM_BAYER].num_buf = buf->count;

	if (buf->count == 0)
		isp->video[FIMC_IS_VIDEO_NUM_BAYER].buf_ref_cnt = 0;

	dbg("%s(num_buf | %d)\n", __func__,
		isp->video[FIMC_IS_VIDEO_NUM_BAYER].num_buf);

	return ret;
}

static int fimc_is_bayer_video_querybuf(struct file *file, void *priv,
						struct v4l2_buffer *buf)
{
	int ret;
	struct fimc_is_video_dev *video = file->private_data;

	dbg("%s\n", __func__);
	ret = vb2_querybuf(&video->vbq, buf);

	return ret;
}

static int fimc_is_bayer_video_qbuf(struct file *file, void *priv,
						struct v4l2_buffer *buf)
{
	int vb_ret;
	struct fimc_is_video_dev *video = file->private_data;
	struct fimc_is_dev *isp = video_drvdata(file);

	if (test_bit(FIMC_IS_STATE_BAYER_BUFFER_PREPARED, &isp->pipe_state)) {
		video->buf_mask |= (1<<buf->index);
		isp->video[FIMC_IS_VIDEO_NUM_BAYER].buf_mask = video->buf_mask;

		dbg("index(%d) mask(0x%08x)\n", buf->index, video->buf_mask);
	} else {
		dbg("index(%d)\n", buf->index);
	}
	vb_ret = vb2_qbuf(&video->vbq, buf);

	return vb_ret;
}

static int fimc_is_bayer_video_dqbuf(struct file *file, void *priv,
						struct v4l2_buffer *buf)
{
	int vb_ret;
	struct fimc_is_video_dev *video = file->private_data;
	struct fimc_is_dev *isp = video_drvdata(file);

	vb_ret = vb2_dqbuf(&video->vbq, buf, file->f_flags & O_NONBLOCK);

	video->buf_mask &= ~(1<<buf->index);
	isp->video[FIMC_IS_VIDEO_NUM_BAYER].buf_mask = video->buf_mask;

	dbg("index(%d) mask(0x%08x)\n", buf->index, video->buf_mask);

	return vb_ret;
}

static int fimc_is_bayer_video_streamon(struct file *file, void *priv,
						enum v4l2_buf_type type)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_streamon(&isp->video[FIMC_IS_VIDEO_NUM_BAYER].vbq, type);
}

static int fimc_is_bayer_video_streamoff(struct file *file, void *priv,
						enum v4l2_buf_type type)
{
	struct fimc_is_dev *isp = video_drvdata(file);

	dbg("%s\n", __func__);
	return vb2_streamoff(&isp->video[FIMC_IS_VIDEO_NUM_BAYER].vbq, type);
}

static int fimc_is_bayer_video_enum_input(struct file *file, void *priv,
						struct v4l2_input *input)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	struct exynos5_fimc_is_sensor_info *sensor_info
				= isp->pdata->sensor_info[input->index];

	dbg("index(%d) sensor(%s)\n",
		input->index, sensor_info->sensor_name);
	dbg("pos(%d) sensor_id(%d)\n",
		sensor_info->sensor_position, sensor_info->sensor_id);
	dbg("csi_id(%d) flite_id(%d)\n",
		sensor_info->csi_id, sensor_info->flite_id);
	dbg("i2c_ch(%d)\n", sensor_info->i2c_channel);

	if (input->index >= FIMC_IS_MAX_CAMIF_CLIENTS)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;

	strncpy(input->name, sensor_info->sensor_name,
					FIMC_IS_MAX_NAME_LEN);
	return 0;
}

static int fimc_is_bayer_video_g_input(struct file *file, void *priv,
						unsigned int *input)
{
	dbg("%s\n", __func__);
	return 0;
}

static int fimc_is_bayer_video_s_input(struct file *file, void *priv,
						unsigned int input)
{
	struct fimc_is_dev *isp = video_drvdata(file);
	struct exynos5_fimc_is_sensor_info *sensor_info
				= isp->pdata->sensor_info[input];

	isp->sensor.id_position = input;
	isp->sensor.sensor_type
		= fimc_is_hw_get_sensor_type(sensor_info->sensor_id,
						sensor_info->flite_id);

	fimc_is_hw_set_default_size(isp, sensor_info->sensor_id);

	dbg("sensor info : pos(%d) type(%d)\n", input, isp->sensor.sensor_type);


	return 0;
}

const struct v4l2_file_operations fimc_is_bayer_video_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_is_bayer_video_open,
	.release	= fimc_is_bayer_video_close,
	.poll		= fimc_is_bayer_video_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= fimc_is_bayer_video_mmap,
};

const struct v4l2_ioctl_ops fimc_is_bayer_video_ioctl_ops = {
	.vidioc_querycap		= fimc_is_bayer_video_querycap,
	.vidioc_enum_fmt_vid_cap_mplane	= fimc_is_bayer_video_enum_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= fimc_is_bayer_video_get_format_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= fimc_is_bayer_video_set_format_mplane,
	.vidioc_try_fmt_vid_cap_mplane	= fimc_is_bayer_video_try_format_mplane,
	.vidioc_cropcap			= fimc_is_bayer_video_cropcap,
	.vidioc_g_crop			= fimc_is_bayer_video_get_crop,
	.vidioc_s_crop			= fimc_is_bayer_video_set_crop,
	.vidioc_reqbufs			= fimc_is_bayer_video_reqbufs,
	.vidioc_querybuf		= fimc_is_bayer_video_querybuf,
	.vidioc_qbuf			= fimc_is_bayer_video_qbuf,
	.vidioc_dqbuf			= fimc_is_bayer_video_dqbuf,
	.vidioc_streamon		= fimc_is_bayer_video_streamon,
	.vidioc_streamoff		= fimc_is_bayer_video_streamoff,
	.vidioc_enum_input		= fimc_is_bayer_video_enum_input,
	.vidioc_g_input			= fimc_is_bayer_video_g_input,
	.vidioc_s_input			= fimc_is_bayer_video_s_input,
};

static int fimc_is_bayer_queue_setup(struct vb2_queue *vq,
			const struct v4l2_format *fmt,
			unsigned int *num_buffers,
			unsigned int *num_planes, unsigned int sizes[],
			void *allocators[])
{

	struct fimc_is_video_dev *video = vq->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	int i;

	*num_planes = isp->video[FIMC_IS_VIDEO_NUM_BAYER].
					frame.format.num_planes;
	set_plane_size(&isp->video[FIMC_IS_VIDEO_NUM_BAYER].frame, sizes);

	for (i = 0; i < *num_planes; i++)
		allocators[i] =  isp->alloc_ctx;

	dbg("(num_planes : %d)(size : %d)\n", (int)*num_planes, (int)sizes[0]);

	return 0;
}

static int fimc_is_bayer_buffer_prepare(struct vb2_buffer *vb)
{
	dbg("--%s\n", __func__);
	return 0;
}

static inline void fimc_is_bayer_lock(struct vb2_queue *vq)
{
	dbg("%s\n", __func__);
}

static inline void fimc_is_bayer_unlock(struct vb2_queue *vq)
{
	dbg("%s\n", __func__);
}

static int fimc_is_bayer_start_streaming(struct vb2_queue *q,
						unsigned int count)
{
	struct fimc_is_video_dev *video = q->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	int ret;
	int i, j;
	int buf_index;

	dbg("%s(pipe_state : %d)\n", __func__, (int)isp->pipe_state);

	if (test_bit(FIMC_IS_STATE_FW_DOWNLOADED, &isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_SENSOR_INITIALIZED,
						&isp->pipe_state)) {

		dbg("IS_ST_CHANGE_MODE\n");
		set_bit(IS_ST_CHANGE_MODE, &isp->state);
		fimc_is_hw_change_mode(isp, IS_MODE_PREVIEW_STILL);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_CHANGE_MODE_DONE,
			&isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"Mode change timeout:%s\n", __func__);
			return -EBUSY;
		}

		set_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state);
	}

	if (test_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_BAYER_BUFFER_PREPARED,
			&isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state)) {
		dbg("IS Stream On\n");
		fimc_is_hw_set_stream(isp, 1);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_STREAM_ON, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
			return -EBUSY;
		}
		clear_bit(IS_ST_STREAM_ON, &isp->state);

		set_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state);
	}

	if (test_bit(FIMC_IS_STATE_BAYER_BUFFER_PREPARED, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state) &&
		test_bit(FIMC_IS_STATE_SENSOR_INITIALIZED, &isp->pipe_state)) {
		/* buffer addr setting */
		for (i = 0; i < isp->
			video[FIMC_IS_VIDEO_NUM_BAYER].num_buf; i++)
			for (j = 0; j < isp->video[FIMC_IS_VIDEO_NUM_BAYER].
						frame.format.num_planes; j++) {
				buf_index = i * isp->
					video[FIMC_IS_VIDEO_NUM_BAYER].
					frame.format.num_planes + j;

				dbg("(%d)set buf(%d:%d) = 0x%08x\n",
					buf_index, i, j,
					isp->video[FIMC_IS_VIDEO_NUM_BAYER].
					buf[i][j]);

				isp->is_p_region->shared[116 + buf_index]
				= isp->video[FIMC_IS_VIDEO_NUM_BAYER].buf[i][j];
		}

		dbg("buf_num:%d buf_plane:%d shared[116] : 0x%p\n",
		isp->video[FIMC_IS_VIDEO_NUM_BAYER].num_buf,
		isp->video[FIMC_IS_VIDEO_NUM_BAYER].frame.format.num_planes,
		isp->mem.kvaddr_shared + 116 * sizeof(u32));

		for (i = 0; i < isp->
			video[FIMC_IS_VIDEO_NUM_BAYER].num_buf; i++)
			isp->video[FIMC_IS_VIDEO_NUM_BAYER].buf_mask
							|= (1 << i);

		dbg("initial buffer mask : 0x%08x\n",
			isp->video[FIMC_IS_VIDEO_NUM_BAYER].buf_mask);


		IS_ISP_SET_PARAM_DMA_OUTPUT2_CMD(isp,
			DMA_OUTPUT_COMMAND_ENABLE);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_MASK(isp,
			isp->video[FIMC_IS_VIDEO_NUM_BAYER].buf_mask);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_NUMBER(isp,
			isp->video[FIMC_IS_VIDEO_NUM_BAYER].num_buf);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_ADDRESS(isp,
			(u32)isp->mem.dvaddr_shared + 116 * sizeof(u32));
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_ADDRESS(isp,
			(u32)isp->mem.dvaddr_shared + 116 * sizeof(u32));
		IS_ISP_SET_PARAM_DMA_OUTPUT2_DMA_DONE(isp,
			DMA_OUTPUT_NOTIFY_DMA_DONE_ENBABLE);

		IS_SET_PARAM_BIT(isp, PARAM_ISP_DMA2_OUTPUT);
		IS_INC_PARAM_NUM(isp);

		fimc_is_mem_cache_clean((void *)isp->is_p_region,
			IS_PARAM_SIZE);

		isp->scenario_id = ISS_PREVIEW_STILL;
		set_bit(IS_ST_INIT_PREVIEW_STILL,	&isp->state);
		clear_bit(IS_ST_INIT_CAPTURE_STILL, &isp->state);
		clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state);
		fimc_is_hw_set_param(isp);
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
			return -EBUSY;
		}

		set_bit(FIMC_IS_STATE_BAYER_STREAM_ON, &isp->pipe_state);
	}

	return 0;
}

static int fimc_is_bayer_stop_streaming(struct vb2_queue *q)
{
	struct fimc_is_video_dev *video = q->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	int ret;


	IS_ISP_SET_PARAM_DMA_OUTPUT2_CMD(isp,
			DMA_OUTPUT_COMMAND_DISABLE);
	IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_NUMBER(isp,
		0);
	IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_ADDRESS(isp,
		0);

	IS_SET_PARAM_BIT(isp, PARAM_ISP_DMA2_OUTPUT);
	IS_INC_PARAM_NUM(isp);

	fimc_is_mem_cache_clean((void *)isp->is_p_region,
		IS_PARAM_SIZE);

	isp->scenario_id = ISS_PREVIEW_STILL;
	set_bit(IS_ST_INIT_PREVIEW_STILL,	&isp->state);
	clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state);
	fimc_is_hw_set_param(isp);
	mutex_lock(&isp->lock);
	ret = wait_event_timeout(isp->irq_queue,
		test_bit(IS_ST_INIT_PREVIEW_VIDEO, &isp->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	mutex_unlock(&isp->lock);
	if (!ret) {
		dev_err(&isp->pdev->dev,
			"wait timeout : %s\n", __func__);
		return -EBUSY;
	}


	if (!test_bit(FIMC_IS_STATE_SCALERC_STREAM_ON, &isp->pipe_state) &&
		!test_bit(FIMC_IS_STATE_SCALERP_STREAM_ON, &isp->pipe_state)) {
		clear_bit(IS_ST_STREAM_OFF, &isp->state);
		fimc_is_hw_set_stream(isp, 0);
		dbg("IS Stream Off");
		mutex_lock(&isp->lock);
		ret = wait_event_timeout(isp->irq_queue,
			test_bit(IS_ST_STREAM_OFF, &isp->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		mutex_unlock(&isp->lock);
		if (!ret) {
			dev_err(&isp->pdev->dev,
				"wait timeout : %s\n", __func__);
			return -EBUSY;
		}
		clear_bit(FIMC_IS_STATE_HW_STREAM_ON, &isp->pipe_state);
	}

	clear_bit(IS_ST_RUN, &isp->state);
	clear_bit(IS_ST_STREAM_ON, &isp->state);
	clear_bit(FIMC_IS_STATE_BAYER_BUFFER_PREPARED, &isp->pipe_state);
	clear_bit(FIMC_IS_STATE_BAYER_STREAM_ON, &isp->pipe_state);


	return 0;
}

static void fimc_is_bayer_buffer_queue(struct vb2_buffer *vb)
{
	struct fimc_is_video_dev *video = vb->vb2_queue->drv_priv;
	struct fimc_is_dev	*isp = video->dev;
	unsigned int i;

	dbg("%s\n", __func__);
	isp->video[FIMC_IS_VIDEO_NUM_BAYER].frame.format.num_planes
							= vb->num_planes;

	if (!test_bit(FIMC_IS_STATE_BAYER_BUFFER_PREPARED, &isp->pipe_state)) {
		for (i = 0; i < vb->num_planes; i++) {
			isp->video[FIMC_IS_VIDEO_NUM_BAYER].
			buf[vb->v4l2_buf.index][i]
				= isp->vb2->plane_addr(vb, i);

			dbg("index(%d)(%d) deviceVaddr(0x%08x)\n",
				vb->v4l2_buf.index, i,
				isp->video[FIMC_IS_VIDEO_NUM_BAYER].
				buf[vb->v4l2_buf.index][i]);
		}

		isp->video[FIMC_IS_VIDEO_NUM_BAYER].buf_ref_cnt++;

		if (isp->video[FIMC_IS_VIDEO_NUM_BAYER].num_buf
			== isp->video[FIMC_IS_VIDEO_NUM_BAYER].buf_ref_cnt)
			set_bit(FIMC_IS_STATE_BAYER_BUFFER_PREPARED,
						&isp->pipe_state);
	}

	if (!test_bit(FIMC_IS_STATE_BAYER_STREAM_ON, &isp->pipe_state))
		fimc_is_bayer_start_streaming(vb->vb2_queue,
			isp->video[FIMC_IS_VIDEO_NUM_BAYER].num_buf);

	return;
}

const struct vb2_ops fimc_is_bayer_qops = {
	.queue_setup		= fimc_is_bayer_queue_setup,
	.buf_prepare		= fimc_is_bayer_buffer_prepare,
	.buf_queue		= fimc_is_bayer_buffer_queue,
	.wait_prepare		= fimc_is_bayer_unlock,
	.wait_finish		= fimc_is_bayer_lock,
	.start_streaming	= fimc_is_bayer_start_streaming,
	.stop_streaming	= fimc_is_bayer_stop_streaming,
};

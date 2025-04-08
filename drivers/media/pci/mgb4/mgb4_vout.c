// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2023 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 *
 * This is the v4l2 output device module. It initializes the signal serializers
 * and creates the v4l2 video devices.
 *
 * When the device is in loopback mode (a direct, in HW, in->out frame passing
 * mode) we disable the v4l2 output by returning EBUSY in the open() syscall.
 */

#include <linux/pci.h>
#include <linux/align.h>
#include <linux/dma/amd_xdma.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-sg.h>
#include <media/v4l2-dv-timings.h>
#include "mgb4_core.h"
#include "mgb4_dma.h"
#include "mgb4_sysfs.h"
#include "mgb4_io.h"
#include "mgb4_cmt.h"
#include "mgb4_vout.h"

ATTRIBUTE_GROUPS(mgb4_fpdl3_out);
ATTRIBUTE_GROUPS(mgb4_gmsl_out);

static const struct mgb4_vout_config vout_cfg[] = {
	{0, 0, 8, {0x78, 0x60, 0x64, 0x68, 0x74, 0x6C, 0x70, 0x7C, 0xE0}},
	{1, 1, 9, {0x98, 0x80, 0x84, 0x88, 0x94, 0x8C, 0x90, 0x9C, 0xE4}}
};

static const struct i2c_board_info fpdl3_ser_info[] = {
	{I2C_BOARD_INFO("serializer1", 0x14)},
	{I2C_BOARD_INFO("serializer2", 0x16)},
};

static const struct mgb4_i2c_kv fpdl3_i2c[] = {
	{0x05, 0xFF, 0x04}, {0x06, 0xFF, 0x01}, {0xC2, 0xFF, 0x80}
};

static const struct v4l2_dv_timings_cap video_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	.bt = {
		.min_width = 320,
		.max_width = 4096,
		.min_height = 240,
		.max_height = 2160,
		.min_pixelclock = 1843200, /* 320 x 240 x 24Hz */
		.max_pixelclock = 530841600, /* 4096 x 2160 x 60Hz */
		.standards = V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_CVT | V4L2_DV_BT_STD_GTF,
		.capabilities = V4L2_DV_BT_CAP_PROGRESSIVE |
			V4L2_DV_BT_CAP_CUSTOM,
	},
};

static void get_timings(struct mgb4_vout_dev *voutdev,
			struct v4l2_dv_timings *timings)
{
	struct mgb4_regs *video = &voutdev->mgbdev->video;
	const struct mgb4_vout_regs *regs = &voutdev->config->regs;

	u32 hsync = mgb4_read_reg(video, regs->hsync);
	u32 vsync = mgb4_read_reg(video, regs->vsync);
	u32 resolution = mgb4_read_reg(video, regs->resolution);

	memset(timings, 0, sizeof(*timings));
	timings->type = V4L2_DV_BT_656_1120;
	timings->bt.width = resolution >> 16;
	timings->bt.height = resolution & 0xFFFF;
	if (hsync & (1U << 31))
		timings->bt.polarities |= V4L2_DV_HSYNC_POS_POL;
	if (vsync & (1U << 31))
		timings->bt.polarities |= V4L2_DV_VSYNC_POS_POL;
	timings->bt.pixelclock = voutdev->freq * 1000;
	timings->bt.hsync = (hsync & 0x00FF0000) >> 16;
	timings->bt.vsync = (vsync & 0x00FF0000) >> 16;
	timings->bt.hbackporch = (hsync & 0x0000FF00) >> 8;
	timings->bt.hfrontporch = hsync & 0x000000FF;
	timings->bt.vbackporch = (vsync & 0x0000FF00) >> 8;
	timings->bt.vfrontporch = vsync & 0x000000FF;
}

static void return_all_buffers(struct mgb4_vout_dev *voutdev,
			       enum vb2_buffer_state state)
{
	struct mgb4_frame_buffer *buf, *node;
	unsigned long flags;

	spin_lock_irqsave(&voutdev->qlock, flags);
	list_for_each_entry_safe(buf, node, &voutdev->buf_list, list) {
		vb2_buffer_done(&buf->vb.vb2_buf, state);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&voutdev->qlock, flags);
}

static int queue_setup(struct vb2_queue *q, unsigned int *nbuffers,
		       unsigned int *nplanes, unsigned int sizes[],
		       struct device *alloc_devs[])
{
	struct mgb4_vout_dev *voutdev = vb2_get_drv_priv(q);
	struct mgb4_regs *video = &voutdev->mgbdev->video;
	u32 config = mgb4_read_reg(video, voutdev->config->regs.config);
	u32 pixelsize = (config & (1U << 16)) ? 2 : 4;
	unsigned int size = (voutdev->width + voutdev->padding) * voutdev->height
			    * pixelsize;

	/*
	 * If I/O reconfiguration is in process, do not allow to start
	 * the queue. See video_source_store() in mgb4_sysfs_out.c for
	 * details.
	 */
	if (test_bit(0, &voutdev->mgbdev->io_reconfig))
		return -EBUSY;

	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;
	*nplanes = 1;
	sizes[0] = size;

	return 0;
}

static int buffer_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct mgb4_frame_buffer *buf = to_frame_buffer(vbuf);

	INIT_LIST_HEAD(&buf->list);

	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct mgb4_vout_dev *voutdev = vb2_get_drv_priv(vb->vb2_queue);
	struct device *dev = &voutdev->mgbdev->pdev->dev;
	struct mgb4_regs *video = &voutdev->mgbdev->video;
	u32 config = mgb4_read_reg(video, voutdev->config->regs.config);
	u32 pixelsize = (config & (1U << 16)) ? 2 : 4;
	unsigned int size = (voutdev->width + voutdev->padding) * voutdev->height
			    * pixelsize;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(dev, "buffer too small (%lu < %u)\n",
			vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);

	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct mgb4_vout_dev *vindev = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct mgb4_frame_buffer *buf = to_frame_buffer(vbuf);
	unsigned long flags;

	spin_lock_irqsave(&vindev->qlock, flags);
	list_add_tail(&buf->list, &vindev->buf_list);
	spin_unlock_irqrestore(&vindev->qlock, flags);
}

static void stop_streaming(struct vb2_queue *vq)
{
	struct mgb4_vout_dev *voutdev = vb2_get_drv_priv(vq);
	struct mgb4_dev *mgbdev = voutdev->mgbdev;
	int irq = xdma_get_user_irq(mgbdev->xdev, voutdev->config->irq);

	xdma_disable_user_irq(mgbdev->xdev, irq);
	cancel_work_sync(&voutdev->dma_work);

	mgb4_mask_reg(&mgbdev->video, voutdev->config->regs.config, 0x2, 0x0);
	mgb4_write_reg(&mgbdev->video, voutdev->config->regs.padding, 0);

	return_all_buffers(voutdev, VB2_BUF_STATE_ERROR);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct mgb4_vout_dev *voutdev = vb2_get_drv_priv(vq);
	struct mgb4_dev *mgbdev = voutdev->mgbdev;
	struct device *dev = &mgbdev->pdev->dev;
	struct mgb4_frame_buffer *buf;
	struct mgb4_regs *video = &mgbdev->video;
	const struct mgb4_vout_config *config = voutdev->config;
	int irq = xdma_get_user_irq(mgbdev->xdev, config->irq);
	int rv;
	u32 addr;

	mgb4_write_reg(video, config->regs.padding, voutdev->padding);
	mgb4_mask_reg(video, config->regs.config, 0x2, 0x2);

	addr = mgb4_read_reg(video, config->regs.address);
	if (addr >= MGB4_ERR_QUEUE_FULL) {
		dev_dbg(dev, "frame queue error (%d)\n", (int)addr);
		return_all_buffers(voutdev, VB2_BUF_STATE_QUEUED);
		return -EBUSY;
	}

	buf = list_first_entry(&voutdev->buf_list, struct mgb4_frame_buffer,
			       list);
	list_del_init(voutdev->buf_list.next);

	rv = mgb4_dma_transfer(mgbdev, config->dma_channel, true, addr,
			       vb2_dma_sg_plane_desc(&buf->vb.vb2_buf, 0));
	if (rv < 0) {
		dev_warn(dev, "DMA transfer error\n");
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	} else {
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}

	xdma_enable_user_irq(mgbdev->xdev, irq);

	return 0;
}

static const struct vb2_ops queue_ops = {
	.queue_setup = queue_setup,
	.buf_init = buffer_init,
	.buf_prepare = buffer_prepare,
	.buf_queue = buffer_queue,
	.start_streaming = start_streaming,
	.stop_streaming = stop_streaming,
};

static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	strscpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strscpy(cap->card, "MGB4 PCIe Card", sizeof(cap->card));

	return 0;
}

static int vidioc_enum_fmt(struct file *file, void *priv,
			   struct v4l2_fmtdesc *f)
{
	struct mgb4_vin_dev *voutdev = video_drvdata(file);
	struct mgb4_regs *video = &voutdev->mgbdev->video;

	if (f->index == 0) {
		f->pixelformat = V4L2_PIX_FMT_ABGR32;
		return 0;
	} else if (f->index == 1 && has_yuv(video)) {
		f->pixelformat = V4L2_PIX_FMT_YUYV;
		return 0;
	} else {
		return -EINVAL;
	}
}

static int vidioc_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct mgb4_vout_dev *voutdev = video_drvdata(file);
	struct mgb4_regs *video = &voutdev->mgbdev->video;
	u32 config = mgb4_read_reg(video, voutdev->config->regs.config);

	f->fmt.pix.width = voutdev->width;
	f->fmt.pix.height = voutdev->height;
	f->fmt.pix.field = V4L2_FIELD_NONE;

	if (config & (1U << 16)) {
		f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
		if (config & (1U << 20)) {
			f->fmt.pix.colorspace = V4L2_COLORSPACE_REC709;
		} else {
			if (config & (1U << 19))
				f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
			else
				f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
		}
		f->fmt.pix.bytesperline = (f->fmt.pix.width + voutdev->padding) * 2;
	} else {
		f->fmt.pix.pixelformat = V4L2_PIX_FMT_ABGR32;
		f->fmt.pix.colorspace = V4L2_COLORSPACE_RAW;
		f->fmt.pix.bytesperline = (f->fmt.pix.width + voutdev->padding) * 4;
	}

	f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;

	return 0;
}

static int vidioc_try_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct mgb4_vout_dev *voutdev = video_drvdata(file);
	struct mgb4_regs *video = &voutdev->mgbdev->video;
	u32 pixelsize;

	f->fmt.pix.width = voutdev->width;
	f->fmt.pix.height = voutdev->height;
	f->fmt.pix.field = V4L2_FIELD_NONE;

	if (has_yuv(video) && f->fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
		pixelsize = 2;
		if (!(f->fmt.pix.colorspace == V4L2_COLORSPACE_REC709 ||
		      f->fmt.pix.colorspace == V4L2_COLORSPACE_SMPTE170M))
			f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	} else {
		pixelsize = 4;
		f->fmt.pix.pixelformat = V4L2_PIX_FMT_ABGR32;
		f->fmt.pix.colorspace = V4L2_COLORSPACE_RAW;
	}

	if (f->fmt.pix.bytesperline > f->fmt.pix.width * pixelsize &&
	    f->fmt.pix.bytesperline < f->fmt.pix.width * pixelsize * 2)
		f->fmt.pix.bytesperline = ALIGN(f->fmt.pix.bytesperline,
						pixelsize);
	else
		f->fmt.pix.bytesperline = f->fmt.pix.width * pixelsize;
	f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;

	return 0;
}

static int vidioc_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct mgb4_vout_dev *voutdev = video_drvdata(file);
	struct mgb4_regs *video = &voutdev->mgbdev->video;
	u32 config, pixelsize;
	int ret;

	if (vb2_is_busy(&voutdev->queue))
		return -EBUSY;

	ret = vidioc_try_fmt(file, priv, f);
	if (ret < 0)
		return ret;

	config = mgb4_read_reg(video, voutdev->config->regs.config);
	if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
		pixelsize = 2;
		config |= 1U << 16;

		if (f->fmt.pix.colorspace == V4L2_COLORSPACE_REC709) {
			config |= 1U << 20;
			config |= 1U << 19;
		} else if (f->fmt.pix.colorspace == V4L2_COLORSPACE_SMPTE170M) {
			config &= ~(1U << 20);
			config |= 1U << 19;
		} else {
			config &= ~(1U << 20);
			config &= ~(1U << 19);
		}
	} else {
		pixelsize = 4;
		config &= ~(1U << 16);
	}
	mgb4_write_reg(video, voutdev->config->regs.config, config);

	voutdev->padding = (f->fmt.pix.bytesperline - (f->fmt.pix.width
			    * pixelsize)) / pixelsize;

	return 0;
}

static int vidioc_g_output(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vidioc_s_output(struct file *file, void *priv, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static int vidioc_enum_output(struct file *file, void *priv,
			      struct v4l2_output *out)
{
	if (out->index != 0)
		return -EINVAL;

	out->type = V4L2_OUTPUT_TYPE_ANALOG;
	out->capabilities = V4L2_OUT_CAP_DV_TIMINGS;
	strscpy(out->name, "MGB4", sizeof(out->name));

	return 0;
}

static int vidioc_enum_frameintervals(struct file *file, void *priv,
				      struct v4l2_frmivalenum *ival)
{
	struct mgb4_vout_dev *voutdev = video_drvdata(file);
	struct mgb4_regs *video = &voutdev->mgbdev->video;
	struct v4l2_dv_timings timings;

	if (ival->index != 0)
		return -EINVAL;
	if (!(ival->pixel_format == V4L2_PIX_FMT_ABGR32 ||
	      ((has_yuv(video) && ival->pixel_format == V4L2_PIX_FMT_YUYV))))
		return -EINVAL;
	if (ival->width != voutdev->width || ival->height != voutdev->height)
		return -EINVAL;

	get_timings(voutdev, &timings);

	ival->type = V4L2_FRMIVAL_TYPE_STEPWISE;
	ival->stepwise.max.denominator = MGB4_HW_FREQ;
	ival->stepwise.max.numerator = 0xFFFFFFFF;
	ival->stepwise.min.denominator = timings.bt.pixelclock;
	ival->stepwise.min.numerator = pixel_size(&timings);
	ival->stepwise.step.denominator = MGB4_HW_FREQ;
	ival->stepwise.step.numerator = 1;

	return 0;
}

static int vidioc_g_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parm)
{
	struct mgb4_vout_dev *voutdev = video_drvdata(file);
	struct mgb4_regs *video = &voutdev->mgbdev->video;
	struct v4l2_fract *tpf = &parm->parm.output.timeperframe;
	struct v4l2_dv_timings timings;
	u32 timer;

	parm->parm.output.writebuffers = 2;

	if (has_timeperframe(video)) {
		timer = mgb4_read_reg(video, voutdev->config->regs.timer);
		if (timer < 0xFFFF) {
			get_timings(voutdev, &timings);
			tpf->numerator = pixel_size(&timings);
			tpf->denominator = timings.bt.pixelclock;
		} else {
			tpf->numerator = timer;
			tpf->denominator = MGB4_HW_FREQ;
		}

		parm->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	}

	return 0;
}

static int vidioc_s_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parm)
{
	struct mgb4_vout_dev *voutdev = video_drvdata(file);
	struct mgb4_regs *video = &voutdev->mgbdev->video;
	struct v4l2_fract *tpf = &parm->parm.output.timeperframe;
	struct v4l2_dv_timings timings;
	u32 timer, period;

	if (has_timeperframe(video)) {
		timer = tpf->denominator ?
			MGB4_PERIOD(tpf->numerator, tpf->denominator) : 0;
		if (timer) {
			get_timings(voutdev, &timings);
			period = MGB4_PERIOD(pixel_size(&timings),
					     timings.bt.pixelclock);
			if (timer < period)
				timer = 0;
		}

		mgb4_write_reg(video, voutdev->config->regs.timer, timer);
	}

	return vidioc_g_parm(file, priv, parm);
}

static int vidioc_g_dv_timings(struct file *file, void *fh,
			       struct v4l2_dv_timings *timings)
{
	struct mgb4_vout_dev *voutdev = video_drvdata(file);

	get_timings(voutdev, timings);

	return 0;
}

static int vidioc_s_dv_timings(struct file *file, void *fh,
			       struct v4l2_dv_timings *timings)
{
	struct mgb4_vout_dev *voutdev = video_drvdata(file);

	get_timings(voutdev, timings);

	return 0;
}

static int vidioc_enum_dv_timings(struct file *file, void *fh,
				  struct v4l2_enum_dv_timings *timings)
{
	return v4l2_enum_dv_timings_cap(timings, &video_timings_cap, NULL, NULL);
}

static int vidioc_dv_timings_cap(struct file *file, void *fh,
				 struct v4l2_dv_timings_cap *cap)
{
	*cap = video_timings_cap;

	return 0;
}

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	.vidioc_enum_fmt_vid_out = vidioc_enum_fmt,
	.vidioc_try_fmt_vid_out = vidioc_try_fmt,
	.vidioc_s_fmt_vid_out = vidioc_s_fmt,
	.vidioc_g_fmt_vid_out = vidioc_g_fmt,
	.vidioc_enum_output = vidioc_enum_output,
	.vidioc_enum_frameintervals = vidioc_enum_frameintervals,
	.vidioc_g_output = vidioc_g_output,
	.vidioc_s_output = vidioc_s_output,
	.vidioc_g_parm = vidioc_g_parm,
	.vidioc_s_parm = vidioc_s_parm,
	.vidioc_dv_timings_cap = vidioc_dv_timings_cap,
	.vidioc_enum_dv_timings = vidioc_enum_dv_timings,
	.vidioc_g_dv_timings = vidioc_g_dv_timings,
	.vidioc_s_dv_timings = vidioc_s_dv_timings,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

static int fh_open(struct file *file)
{
	struct mgb4_vout_dev *voutdev = video_drvdata(file);
	struct mgb4_regs *video = &voutdev->mgbdev->video;
	struct device *dev = &voutdev->mgbdev->pdev->dev;
	u32 config, resolution;
	int rv;

	/* Return EBUSY when the device is in loopback mode */
	config = mgb4_read_reg(video, voutdev->config->regs.config);
	if ((config & 0xc) >> 2 != voutdev->config->id + MGB4_VIN_DEVICES) {
		dev_dbg(dev, "can not open - device in loopback mode");
		return -EBUSY;
	}

	mutex_lock(&voutdev->lock);

	rv = v4l2_fh_open(file);
	if (rv)
		goto out;

	if (!v4l2_fh_is_singular_file(file))
		goto out;

	resolution = mgb4_read_reg(video, voutdev->config->regs.resolution);
	voutdev->width = resolution >> 16;
	voutdev->height = resolution & 0xFFFF;

out:
	mutex_unlock(&voutdev->lock);
	return rv;
}

static const struct v4l2_file_operations video_fops = {
	.owner = THIS_MODULE,
	.open = fh_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.write = vb2_fop_write,
	.mmap = vb2_fop_mmap,
	.poll = vb2_fop_poll,
};

static void dma_transfer(struct work_struct *work)
{
	struct mgb4_vout_dev *voutdev = container_of(work, struct mgb4_vout_dev,
						     dma_work);
	struct device *dev = &voutdev->mgbdev->pdev->dev;
	struct mgb4_regs *video = &voutdev->mgbdev->video;
	struct mgb4_frame_buffer *buf = NULL;
	unsigned long flags;
	u32 addr;
	int rv;

	spin_lock_irqsave(&voutdev->qlock, flags);
	if (!list_empty(&voutdev->buf_list)) {
		buf = list_first_entry(&voutdev->buf_list,
				       struct mgb4_frame_buffer, list);
		list_del_init(voutdev->buf_list.next);
	}
	spin_unlock_irqrestore(&voutdev->qlock, flags);

	if (!buf)
		return;

	addr = mgb4_read_reg(video, voutdev->config->regs.address);
	if (addr >= MGB4_ERR_QUEUE_FULL) {
		dev_dbg(dev, "frame queue error (%d)\n", (int)addr);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		return;
	}

	rv = mgb4_dma_transfer(voutdev->mgbdev, voutdev->config->dma_channel,
			       true, addr,
			       vb2_dma_sg_plane_desc(&buf->vb.vb2_buf, 0));
	if (rv < 0) {
		dev_warn(dev, "DMA transfer error\n");
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	} else {
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}
}

static irqreturn_t handler(int irq, void *ctx)
{
	struct mgb4_vout_dev *voutdev = (struct mgb4_vout_dev *)ctx;
	struct mgb4_regs *video = &voutdev->mgbdev->video;

	schedule_work(&voutdev->dma_work);

	mgb4_write_reg(video, 0xB4, 1U << voutdev->config->irq);

	return IRQ_HANDLED;
}

static int ser_init(struct mgb4_vout_dev *voutdev, int id)
{
	int rv;
	const struct i2c_board_info *info = &fpdl3_ser_info[id];
	struct mgb4_i2c_client *ser = &voutdev->ser;
	struct device *dev = &voutdev->mgbdev->pdev->dev;

	if (MGB4_IS_GMSL(voutdev->mgbdev))
		return 0;

	rv = mgb4_i2c_init(ser, voutdev->mgbdev->i2c_adap, info, 8);
	if (rv < 0) {
		dev_err(dev, "failed to create serializer\n");
		return rv;
	}
	rv = mgb4_i2c_configure(ser, fpdl3_i2c, ARRAY_SIZE(fpdl3_i2c));
	if (rv < 0) {
		dev_err(dev, "failed to configure serializer\n");
		goto err_i2c_dev;
	}

	return 0;

err_i2c_dev:
	mgb4_i2c_free(ser);

	return rv;
}

static void fpga_init(struct mgb4_vout_dev *voutdev)
{
	struct mgb4_regs *video = &voutdev->mgbdev->video;
	const struct mgb4_vout_regs *regs = &voutdev->config->regs;

	mgb4_write_reg(video, regs->config, 0x00000011);
	mgb4_write_reg(video, regs->resolution, (1280 << 16) | 640);
	mgb4_write_reg(video, regs->hsync, 0x00283232);
	mgb4_write_reg(video, regs->vsync, 0x40141F1E);
	mgb4_write_reg(video, regs->frame_limit, MGB4_HW_FREQ / 60);
	mgb4_write_reg(video, regs->padding, 0x00000000);

	voutdev->freq = mgb4_cmt_set_vout_freq(voutdev, 61150 >> 1) << 1;

	mgb4_write_reg(video, regs->config,
		       (voutdev->config->id + MGB4_VIN_DEVICES) << 2 | 1 << 4);
}

static void create_debugfs(struct mgb4_vout_dev *voutdev)
{
#ifdef CONFIG_DEBUG_FS
	struct mgb4_regs *video = &voutdev->mgbdev->video;
	struct dentry *entry;

	if (IS_ERR_OR_NULL(voutdev->mgbdev->debugfs))
		return;
	entry = debugfs_create_dir(voutdev->vdev.name, voutdev->mgbdev->debugfs);
	if (IS_ERR(entry))
		return;

	voutdev->regs[0].name = "CONFIG";
	voutdev->regs[0].offset = voutdev->config->regs.config;
	voutdev->regs[1].name = "STATUS";
	voutdev->regs[1].offset = voutdev->config->regs.status;
	voutdev->regs[2].name = "RESOLUTION";
	voutdev->regs[2].offset = voutdev->config->regs.resolution;
	voutdev->regs[3].name = "VIDEO_PARAMS_1";
	voutdev->regs[3].offset = voutdev->config->regs.hsync;
	voutdev->regs[4].name = "VIDEO_PARAMS_2";
	voutdev->regs[4].offset = voutdev->config->regs.vsync;
	voutdev->regs[5].name = "FRAME_LIMIT";
	voutdev->regs[5].offset = voutdev->config->regs.frame_limit;
	voutdev->regs[6].name = "PADDING_PIXELS";
	voutdev->regs[6].offset = voutdev->config->regs.padding;
	if (has_timeperframe(video)) {
		voutdev->regs[7].name = "TIMER";
		voutdev->regs[7].offset = voutdev->config->regs.timer;
		voutdev->regset.nregs = 8;
	} else {
		voutdev->regset.nregs = 7;
	}

	voutdev->regset.base = video->membase;
	voutdev->regset.regs = voutdev->regs;

	debugfs_create_regset32("registers", 0444, entry, &voutdev->regset);
#endif
}

struct mgb4_vout_dev *mgb4_vout_create(struct mgb4_dev *mgbdev, int id)
{
	int rv, irq;
	const struct attribute_group **groups;
	struct mgb4_vout_dev *voutdev;
	struct pci_dev *pdev = mgbdev->pdev;
	struct device *dev = &pdev->dev;

	voutdev = kzalloc(sizeof(*voutdev), GFP_KERNEL);
	if (!voutdev)
		return NULL;

	voutdev->mgbdev = mgbdev;
	voutdev->config = &vout_cfg[id];

	/* Frame queue */
	INIT_LIST_HEAD(&voutdev->buf_list);
	spin_lock_init(&voutdev->qlock);

	/* DMA transfer stuff */
	INIT_WORK(&voutdev->dma_work, dma_transfer);

	/* IRQ callback */
	irq = xdma_get_user_irq(mgbdev->xdev, voutdev->config->irq);
	rv = request_irq(irq, handler, 0, "mgb4-vout", voutdev);
	if (rv) {
		dev_err(dev, "failed to register irq handler\n");
		goto err_alloc;
	}

	/* Set the FPGA registers default values */
	fpga_init(voutdev);

	/* Set the serializer default values */
	rv = ser_init(voutdev, id);
	if (rv)
		goto err_irq;

	/* V4L2 stuff init  */
	rv = v4l2_device_register(dev, &voutdev->v4l2dev);
	if (rv) {
		dev_err(dev, "failed to register v4l2 device\n");
		goto err_irq;
	}

	mutex_init(&voutdev->lock);

	voutdev->queue.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	voutdev->queue.io_modes = VB2_MMAP | VB2_DMABUF | VB2_WRITE;
	voutdev->queue.buf_struct_size = sizeof(struct mgb4_frame_buffer);
	voutdev->queue.ops = &queue_ops;
	voutdev->queue.mem_ops = &vb2_dma_sg_memops;
	voutdev->queue.gfp_flags = GFP_DMA32;
	voutdev->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	voutdev->queue.min_queued_buffers = 2;
	voutdev->queue.drv_priv = voutdev;
	voutdev->queue.lock = &voutdev->lock;
	voutdev->queue.dev = dev;
	rv = vb2_queue_init(&voutdev->queue);
	if (rv) {
		dev_err(dev, "failed to initialize vb2 queue\n");
		goto err_v4l2_dev;
	}

	snprintf(voutdev->vdev.name, sizeof(voutdev->vdev.name), "mgb4-out%d",
		 id + 1);
	voutdev->vdev.device_caps = V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_READWRITE
	  | V4L2_CAP_STREAMING;
	voutdev->vdev.vfl_dir = VFL_DIR_TX;
	voutdev->vdev.fops = &video_fops;
	voutdev->vdev.ioctl_ops = &video_ioctl_ops;
	voutdev->vdev.release = video_device_release_empty;
	voutdev->vdev.v4l2_dev = &voutdev->v4l2dev;
	voutdev->vdev.lock = &voutdev->lock;
	voutdev->vdev.queue = &voutdev->queue;
	video_set_drvdata(&voutdev->vdev, voutdev);

	rv = video_register_device(&voutdev->vdev, VFL_TYPE_VIDEO, -1);
	if (rv) {
		dev_err(dev, "failed to register video device\n");
		goto err_v4l2_dev;
	}

	/* Module sysfs attributes */
	groups = MGB4_IS_GMSL(mgbdev)
	  ? mgb4_gmsl_out_groups : mgb4_fpdl3_out_groups;
	rv = device_add_groups(&voutdev->vdev.dev, groups);
	if (rv) {
		dev_err(dev, "failed to create sysfs attributes\n");
		goto err_video_dev;
	}

	create_debugfs(voutdev);

	return voutdev;

err_video_dev:
	video_unregister_device(&voutdev->vdev);
err_v4l2_dev:
	v4l2_device_unregister(&voutdev->v4l2dev);
err_irq:
	free_irq(irq, voutdev);
err_alloc:
	kfree(voutdev);

	return NULL;
}

void mgb4_vout_free(struct mgb4_vout_dev *voutdev)
{
	const struct attribute_group **groups;
	int irq = xdma_get_user_irq(voutdev->mgbdev->xdev, voutdev->config->irq);

	free_irq(irq, voutdev);

	groups = MGB4_IS_GMSL(voutdev->mgbdev)
	  ? mgb4_gmsl_out_groups : mgb4_fpdl3_out_groups;
	device_remove_groups(&voutdev->vdev.dev, groups);

	mgb4_i2c_free(&voutdev->ser);
	video_unregister_device(&voutdev->vdev);
	v4l2_device_unregister(&voutdev->v4l2dev);

	kfree(voutdev);
}

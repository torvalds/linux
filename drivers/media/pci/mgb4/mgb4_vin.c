// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2023 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 *
 * This is the v4l2 input device module. It initializes the signal deserializers
 * and creates the v4l2 video devices. The input signal can change at any time
 * which is handled by the "timings" callbacks and an IRQ based watcher, that
 * emits the V4L2_EVENT_SOURCE_CHANGE event in case of a signal source change.
 *
 * When the device is in loopback mode (a direct, in HW, in->out frame passing
 * mode) the card's frame queue must be running regardless of whether a v4l2
 * stream is running and the output parameters like frame buffers padding must
 * be in sync with the input parameters.
 */

#include <linux/pci.h>
#include <linux/workqueue.h>
#include <linux/align.h>
#include <linux/dma/amd_xdma.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-sg.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include "mgb4_core.h"
#include "mgb4_dma.h"
#include "mgb4_sysfs.h"
#include "mgb4_io.h"
#include "mgb4_vout.h"
#include "mgb4_vin.h"

ATTRIBUTE_GROUPS(mgb4_fpdl3_in);
ATTRIBUTE_GROUPS(mgb4_gmsl_in);

static const struct mgb4_vin_config vin_cfg[] = {
	{0, 0, 0, 6, {0x10, 0x00, 0x04, 0x08, 0x1C, 0x14, 0x18, 0x20, 0x24, 0x28, 0xE8}},
	{1, 1, 1, 7, {0x40, 0x30, 0x34, 0x38, 0x4C, 0x44, 0x48, 0x50, 0x54, 0x58, 0xEC}}
};

static const struct i2c_board_info fpdl3_deser_info[] = {
	{I2C_BOARD_INFO("deserializer1", 0x38)},
	{I2C_BOARD_INFO("deserializer2", 0x36)},
};

static const struct i2c_board_info gmsl_deser_info[] = {
	{I2C_BOARD_INFO("deserializer1", 0x4C)},
	{I2C_BOARD_INFO("deserializer2", 0x2A)},
};

static const struct mgb4_i2c_kv fpdl3_i2c[] = {
	{0x06, 0xFF, 0x04}, {0x07, 0xFF, 0x01}, {0x45, 0xFF, 0xE8},
	{0x49, 0xFF, 0x00}, {0x34, 0xFF, 0x00}, {0x23, 0xFF, 0x00}
};

static const struct mgb4_i2c_kv gmsl_i2c[] = {
	{0x01, 0x03, 0x03}, {0x300, 0x0C, 0x0C}, {0x03, 0xC0, 0xC0},
	{0x1CE, 0x0E, 0x0E}, {0x11, 0x05, 0x00}, {0x05, 0xC0, 0x40},
	{0x307, 0x0F, 0x00}, {0xA0, 0x03, 0x00}, {0x3E0, 0x07, 0x07},
	{0x308, 0x01, 0x01}, {0x10, 0x20, 0x20}, {0x300, 0x40, 0x40}
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

/* Dummy timings when no signal present */
static const struct v4l2_dv_timings cea1080p60 = V4L2_DV_BT_CEA_1920X1080P60;

/*
 * Returns the video output connected with the given video input if the input
 * is in loopback mode.
 */
static struct mgb4_vout_dev *loopback_dev(struct mgb4_vin_dev *vindev, int i)
{
	struct mgb4_vout_dev *voutdev;
	u32 config;

	voutdev = vindev->mgbdev->vout[i];
	if (!voutdev)
		return NULL;

	config = mgb4_read_reg(&voutdev->mgbdev->video,
			       voutdev->config->regs.config);
	if ((config & 0xc) >> 2 == vindev->config->id)
		return voutdev;

	return NULL;
}

/*
 * Check, whether the loopback mode - a HW INPUT->OUTPUT transmission - is
 * enabled on the given input.
 */
static int loopback_active(struct mgb4_vin_dev *vindev)
{
	int i;

	for (i = 0; i < MGB4_VOUT_DEVICES; i++)
		if (loopback_dev(vindev, i))
			return 1;

	return 0;
}

/*
 * Set the output frame buffer padding of all outputs connected with the given
 * input when the video input is set to loopback mode. The paddings must be
 * the same for the loopback to work properly.
 */
static void set_loopback_padding(struct mgb4_vin_dev *vindev, u32 padding)
{
	struct mgb4_regs *video = &vindev->mgbdev->video;
	struct mgb4_vout_dev *voutdev;
	int i;

	for (i = 0; i < MGB4_VOUT_DEVICES; i++) {
		voutdev = loopback_dev(vindev, i);
		if (voutdev)
			mgb4_write_reg(video, voutdev->config->regs.padding,
				       padding);
	}
}

static int get_timings(struct mgb4_vin_dev *vindev,
		       struct v4l2_dv_timings *timings)
{
	struct mgb4_regs *video = &vindev->mgbdev->video;
	const struct mgb4_vin_regs *regs = &vindev->config->regs;

	u32 status = mgb4_read_reg(video, regs->status);
	u32 pclk = mgb4_read_reg(video, regs->pclk);
	u32 hsync = mgb4_read_reg(video, regs->hsync);
	u32 vsync = mgb4_read_reg(video, regs->vsync);
	u32 resolution = mgb4_read_reg(video, regs->resolution);

	if (!(status & (1U << 2)))
		return -ENOLCK;
	if (!(status & (3 << 9)))
		return -ENOLINK;

	memset(timings, 0, sizeof(*timings));
	timings->type = V4L2_DV_BT_656_1120;
	timings->bt.width = resolution >> 16;
	timings->bt.height = resolution & 0xFFFF;
	if (status & (1U << 12))
		timings->bt.polarities |= V4L2_DV_HSYNC_POS_POL;
	if (status & (1U << 13))
		timings->bt.polarities |= V4L2_DV_VSYNC_POS_POL;
	timings->bt.pixelclock = pclk * 1000;
	timings->bt.hsync = (hsync & 0x00FF0000) >> 16;
	timings->bt.vsync = (vsync & 0x00FF0000) >> 16;
	timings->bt.hbackporch = (hsync & 0x0000FF00) >> 8;
	timings->bt.hfrontporch = hsync & 0x000000FF;
	timings->bt.vbackporch = (vsync & 0x0000FF00) >> 8;
	timings->bt.vfrontporch = vsync & 0x000000FF;

	return 0;
}

static void return_all_buffers(struct mgb4_vin_dev *vindev,
			       enum vb2_buffer_state state)
{
	struct mgb4_frame_buffer *buf, *node;
	unsigned long flags;

	spin_lock_irqsave(&vindev->qlock, flags);
	list_for_each_entry_safe(buf, node, &vindev->buf_list, list) {
		vb2_buffer_done(&buf->vb.vb2_buf, state);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&vindev->qlock, flags);
}

static int queue_setup(struct vb2_queue *q, unsigned int *nbuffers,
		       unsigned int *nplanes, unsigned int sizes[],
		       struct device *alloc_devs[])
{
	struct mgb4_vin_dev *vindev = vb2_get_drv_priv(q);
	struct mgb4_regs *video = &vindev->mgbdev->video;
	u32 config = mgb4_read_reg(video, vindev->config->regs.config);
	u32 pixelsize = (config & (1U << 16)) ? 2 : 4;
	unsigned int size = (vindev->timings.bt.width + vindev->padding)
			    * vindev->timings.bt.height * pixelsize;

	/*
	 * If I/O reconfiguration is in process, do not allow to start
	 * the queue. See video_source_store() in mgb4_sysfs_out.c for
	 * details.
	 */
	if (test_bit(0, &vindev->mgbdev->io_reconfig))
		return -EBUSY;

	if (!size)
		return -EINVAL;
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
	struct mgb4_vin_dev *vindev = vb2_get_drv_priv(vb->vb2_queue);
	struct mgb4_regs *video = &vindev->mgbdev->video;
	struct device *dev = &vindev->mgbdev->pdev->dev;
	u32 config = mgb4_read_reg(video, vindev->config->regs.config);
	u32 pixelsize = (config & (1U << 16)) ? 2 : 4;
	unsigned int size = (vindev->timings.bt.width + vindev->padding)
			    * vindev->timings.bt.height * pixelsize;

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
	struct mgb4_vin_dev *vindev = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct mgb4_frame_buffer *buf = to_frame_buffer(vbuf);
	unsigned long flags;

	spin_lock_irqsave(&vindev->qlock, flags);
	list_add_tail(&buf->list, &vindev->buf_list);
	spin_unlock_irqrestore(&vindev->qlock, flags);
}

static void stop_streaming(struct vb2_queue *vq)
{
	struct mgb4_vin_dev *vindev = vb2_get_drv_priv(vq);
	struct mgb4_regs *video = &vindev->mgbdev->video;
	const struct mgb4_vin_config *config = vindev->config;
	int irq = xdma_get_user_irq(vindev->mgbdev->xdev, config->vin_irq);

	xdma_disable_user_irq(vindev->mgbdev->xdev, irq);

	/*
	 * In loopback mode, the HW frame queue must be left running for
	 * the IN->OUT transmission to work!
	 */
	if (!loopback_active(vindev))
		mgb4_mask_reg(&vindev->mgbdev->video, config->regs.config, 0x2,
			      0x0);

	mgb4_write_reg(video, vindev->config->regs.padding, 0);
	set_loopback_padding(vindev, 0);

	cancel_work_sync(&vindev->dma_work);
	return_all_buffers(vindev, VB2_BUF_STATE_ERROR);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct mgb4_vin_dev *vindev = vb2_get_drv_priv(vq);
	struct mgb4_regs *video = &vindev->mgbdev->video;
	const struct mgb4_vin_config *config = vindev->config;
	int irq = xdma_get_user_irq(vindev->mgbdev->xdev, config->vin_irq);

	vindev->sequence = 0;

	/*
	 * In loopback mode, the HW frame queue is already running.
	 */
	if (!loopback_active(vindev))
		mgb4_mask_reg(&vindev->mgbdev->video, config->regs.config, 0x2,
			      0x2);

	mgb4_write_reg(video, vindev->config->regs.padding, vindev->padding);
	set_loopback_padding(vindev, vindev->padding);

	xdma_enable_user_irq(vindev->mgbdev->xdev, irq);

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

static int fh_open(struct file *file)
{
	struct mgb4_vin_dev *vindev = video_drvdata(file);
	int rv;

	mutex_lock(&vindev->lock);

	rv = v4l2_fh_open(file);
	if (rv)
		goto out;

	if (!v4l2_fh_is_singular_file(file))
		goto out;

	if (get_timings(vindev, &vindev->timings) < 0)
		vindev->timings = cea1080p60;

out:
	mutex_unlock(&vindev->lock);
	return rv;
}

static const struct v4l2_file_operations video_fops = {
	.owner = THIS_MODULE,
	.open = fh_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.read = vb2_fop_read,
	.mmap = vb2_fop_mmap,
	.poll = vb2_fop_poll,
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
	struct mgb4_vin_dev *vindev = video_drvdata(file);
	struct mgb4_regs *video = &vindev->mgbdev->video;

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

static int vidioc_enum_frameintervals(struct file *file, void *priv,
				      struct v4l2_frmivalenum *ival)
{
	struct mgb4_vin_dev *vindev = video_drvdata(file);
	struct mgb4_regs *video = &vindev->mgbdev->video;

	if (ival->index != 0)
		return -EINVAL;
	if (!(ival->pixel_format == V4L2_PIX_FMT_ABGR32 ||
	      ((has_yuv(video) && ival->pixel_format == V4L2_PIX_FMT_YUYV))))
		return -EINVAL;
	if (ival->width != vindev->timings.bt.width ||
	    ival->height != vindev->timings.bt.height)
		return -EINVAL;

	ival->type = V4L2_FRMIVAL_TYPE_STEPWISE;
	ival->stepwise.max.denominator = MGB4_HW_FREQ;
	ival->stepwise.max.numerator = 0xFFFFFFFF;
	ival->stepwise.min.denominator = vindev->timings.bt.pixelclock;
	ival->stepwise.min.numerator = pixel_size(&vindev->timings);
	ival->stepwise.step.denominator = MGB4_HW_FREQ;
	ival->stepwise.step.numerator = 1;

	return 0;
}

static int vidioc_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct mgb4_vin_dev *vindev = video_drvdata(file);
	struct mgb4_regs *video = &vindev->mgbdev->video;
	u32 config = mgb4_read_reg(video, vindev->config->regs.config);

	f->fmt.pix.width = vindev->timings.bt.width;
	f->fmt.pix.height = vindev->timings.bt.height;
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
		f->fmt.pix.bytesperline = (f->fmt.pix.width + vindev->padding) * 2;
	} else {
		f->fmt.pix.pixelformat = V4L2_PIX_FMT_ABGR32;
		f->fmt.pix.colorspace = V4L2_COLORSPACE_RAW;
		f->fmt.pix.bytesperline = (f->fmt.pix.width + vindev->padding) * 4;
	}
	f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;

	return 0;
}

static int vidioc_try_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct mgb4_vin_dev *vindev = video_drvdata(file);
	struct mgb4_regs *video = &vindev->mgbdev->video;
	u32 pixelsize;

	f->fmt.pix.width = vindev->timings.bt.width;
	f->fmt.pix.height = vindev->timings.bt.height;
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
	struct mgb4_vin_dev *vindev = video_drvdata(file);
	struct mgb4_regs *video = &vindev->mgbdev->video;
	u32 config, pixelsize;

	if (vb2_is_busy(&vindev->queue))
		return -EBUSY;

	vidioc_try_fmt(file, priv, f);

	config = mgb4_read_reg(video, vindev->config->regs.config);
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
	mgb4_write_reg(video, vindev->config->regs.config, config);

	vindev->padding = (f->fmt.pix.bytesperline - (f->fmt.pix.width
			   * pixelsize)) / pixelsize;

	return 0;
}

static int vidioc_enum_input(struct file *file, void *priv,
			     struct v4l2_input *i)
{
	struct mgb4_vin_dev *vindev = video_drvdata(file);
	struct mgb4_regs *video = &vindev->mgbdev->video;
	u32 status;

	if (i->index != 0)
		return -EINVAL;

	strscpy(i->name, "MGB4", sizeof(i->name));
	i->type = V4L2_INPUT_TYPE_CAMERA;
	i->capabilities = V4L2_IN_CAP_DV_TIMINGS;
	i->status = 0;

	status = mgb4_read_reg(video, vindev->config->regs.status);
	if (!(status & (1U << 2)))
		i->status |= V4L2_IN_ST_NO_SYNC;
	if (!(status & (3 << 9)))
		i->status |= V4L2_IN_ST_NO_SIGNAL;

	return 0;
}

static int vidioc_enum_framesizes(struct file *file, void *fh,
				  struct v4l2_frmsizeenum *fsize)
{
	struct mgb4_vin_dev *vindev = video_drvdata(file);

	if (fsize->index != 0 || !(fsize->pixel_format == V4L2_PIX_FMT_ABGR32 ||
				   fsize->pixel_format == V4L2_PIX_FMT_YUYV))
		return -EINVAL;

	fsize->discrete.width = vindev->timings.bt.width;
	fsize->discrete.height = vindev->timings.bt.height;
	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;

	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	return (i == 0) ? 0 : -EINVAL;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vidioc_g_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parm)
{
	struct mgb4_vin_dev *vindev = video_drvdata(file);
	struct mgb4_regs *video = &vindev->mgbdev->video;
	struct v4l2_fract *tpf = &parm->parm.output.timeperframe;
	u32 timer;

	parm->parm.capture.readbuffers = 2;

	if (has_timeperframe(video)) {
		timer = mgb4_read_reg(video, vindev->config->regs.timer);
		if (timer < 0xFFFF) {
			tpf->numerator = pixel_size(&vindev->timings);
			tpf->denominator = vindev->timings.bt.pixelclock;
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
	struct mgb4_vin_dev *vindev = video_drvdata(file);
	struct mgb4_regs *video = &vindev->mgbdev->video;
	struct v4l2_fract *tpf = &parm->parm.output.timeperframe;
	u32 period, timer;

	if (has_timeperframe(video)) {
		timer = tpf->denominator ?
			MGB4_PERIOD(tpf->numerator, tpf->denominator) : 0;
		if (timer) {
			period = MGB4_PERIOD(pixel_size(&vindev->timings),
					     vindev->timings.bt.pixelclock);
			if (timer < period)
				timer = 0;
		}

		mgb4_write_reg(video, vindev->config->regs.timer, timer);
	}

	return vidioc_g_parm(file, priv, parm);
}

static int vidioc_s_dv_timings(struct file *file, void *fh,
			       struct v4l2_dv_timings *timings)
{
	struct mgb4_vin_dev *vindev = video_drvdata(file);

	if (timings->bt.width < video_timings_cap.bt.min_width ||
	    timings->bt.width > video_timings_cap.bt.max_width ||
	    timings->bt.height < video_timings_cap.bt.min_height ||
	    timings->bt.height > video_timings_cap.bt.max_height)
		return -EINVAL;
	if (timings->bt.width == vindev->timings.bt.width &&
	    timings->bt.height == vindev->timings.bt.height)
		return 0;
	if (vb2_is_busy(&vindev->queue))
		return -EBUSY;

	vindev->timings = *timings;

	return 0;
}

static int vidioc_g_dv_timings(struct file *file, void *fh,
			       struct v4l2_dv_timings *timings)
{
	struct mgb4_vin_dev *vindev = video_drvdata(file);
	*timings = vindev->timings;

	return 0;
}

static int vidioc_query_dv_timings(struct file *file, void *fh,
				   struct v4l2_dv_timings *timings)
{
	struct mgb4_vin_dev *vindev = video_drvdata(file);

	return get_timings(vindev, timings);
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

static int vidioc_subscribe_event(struct v4l2_fh *fh,
				  const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	}

	return v4l2_ctrl_subscribe_event(fh, sub);
}

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt,
	.vidioc_try_fmt_vid_cap = vidioc_try_fmt,
	.vidioc_s_fmt_vid_cap = vidioc_s_fmt,
	.vidioc_g_fmt_vid_cap = vidioc_g_fmt,
	.vidioc_enum_framesizes = vidioc_enum_framesizes,
	.vidioc_enum_frameintervals = vidioc_enum_frameintervals,
	.vidioc_enum_input = vidioc_enum_input,
	.vidioc_g_input = vidioc_g_input,
	.vidioc_s_input = vidioc_s_input,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_g_parm = vidioc_g_parm,
	.vidioc_s_parm = vidioc_s_parm,
	.vidioc_dv_timings_cap = vidioc_dv_timings_cap,
	.vidioc_enum_dv_timings = vidioc_enum_dv_timings,
	.vidioc_g_dv_timings = vidioc_g_dv_timings,
	.vidioc_s_dv_timings = vidioc_s_dv_timings,
	.vidioc_query_dv_timings = vidioc_query_dv_timings,
	.vidioc_subscribe_event = vidioc_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static void dma_transfer(struct work_struct *work)
{
	struct mgb4_vin_dev *vindev = container_of(work, struct mgb4_vin_dev,
						   dma_work);
	struct mgb4_regs *video = &vindev->mgbdev->video;
	struct device *dev = &vindev->mgbdev->pdev->dev;
	struct mgb4_frame_buffer *buf = NULL;
	unsigned long flags;
	u32 addr;
	int rv;

	spin_lock_irqsave(&vindev->qlock, flags);
	if (!list_empty(&vindev->buf_list)) {
		buf = list_first_entry(&vindev->buf_list,
				       struct mgb4_frame_buffer, list);
		list_del_init(vindev->buf_list.next);
	}
	spin_unlock_irqrestore(&vindev->qlock, flags);

	if (!buf)
		return;

	addr = mgb4_read_reg(video, vindev->config->regs.address);
	if (addr >= MGB4_ERR_QUEUE_FULL) {
		dev_dbg(dev, "frame queue error (%d)\n", (int)addr);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		return;
	}

	rv = mgb4_dma_transfer(vindev->mgbdev, vindev->config->dma_channel,
			       false, addr,
			       vb2_dma_sg_plane_desc(&buf->vb.vb2_buf, 0));
	if (rv < 0) {
		dev_warn(dev, "DMA transfer error\n");
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	} else {
		buf->vb.vb2_buf.timestamp = ktime_get_ns();
		buf->vb.sequence = vindev->sequence++;
		buf->vb.field = V4L2_FIELD_NONE;
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}
}

static void signal_change(struct work_struct *work)
{
	struct mgb4_vin_dev *vindev = container_of(work, struct mgb4_vin_dev,
						   err_work);
	struct mgb4_regs *video = &vindev->mgbdev->video;
	struct v4l2_bt_timings *timings = &vindev->timings.bt;
	struct device *dev = &vindev->mgbdev->pdev->dev;

	u32 resolution = mgb4_read_reg(video, vindev->config->regs.resolution);
	u32 width = resolution >> 16;
	u32 height = resolution & 0xFFFF;

	if (timings->width != width || timings->height != height) {
		static const struct v4l2_event ev = {
			.type = V4L2_EVENT_SOURCE_CHANGE,
			.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
		};

		v4l2_event_queue(&vindev->vdev, &ev);

		if (vb2_is_streaming(&vindev->queue))
			vb2_queue_error(&vindev->queue);
	}

	dev_dbg(dev, "stream changed to %ux%u\n", width, height);
}

static irqreturn_t vin_handler(int irq, void *ctx)
{
	struct mgb4_vin_dev *vindev = (struct mgb4_vin_dev *)ctx;
	struct mgb4_regs *video = &vindev->mgbdev->video;

	schedule_work(&vindev->dma_work);

	mgb4_write_reg(video, 0xB4, 1U << vindev->config->vin_irq);

	return IRQ_HANDLED;
}

static irqreturn_t err_handler(int irq, void *ctx)
{
	struct mgb4_vin_dev *vindev = (struct mgb4_vin_dev *)ctx;
	struct mgb4_regs *video = &vindev->mgbdev->video;

	schedule_work(&vindev->err_work);

	mgb4_write_reg(video, 0xB4, 1U << vindev->config->err_irq);

	return IRQ_HANDLED;
}

static int deser_init(struct mgb4_vin_dev *vindev, int id)
{
	int rv, addr_size;
	size_t values_count;
	const struct mgb4_i2c_kv *values;
	const struct i2c_board_info *info;
	struct device *dev = &vindev->mgbdev->pdev->dev;

	if (MGB4_IS_GMSL(vindev->mgbdev)) {
		info = &gmsl_deser_info[id];
		addr_size = 16;
		values = gmsl_i2c;
		values_count = ARRAY_SIZE(gmsl_i2c);
	} else {
		info = &fpdl3_deser_info[id];
		addr_size = 8;
		values = fpdl3_i2c;
		values_count = ARRAY_SIZE(fpdl3_i2c);
	}

	rv = mgb4_i2c_init(&vindev->deser, vindev->mgbdev->i2c_adap, info,
			   addr_size);
	if (rv < 0) {
		dev_err(dev, "failed to create deserializer\n");
		return rv;
	}
	rv = mgb4_i2c_configure(&vindev->deser, values, values_count);
	if (rv < 0) {
		dev_err(dev, "failed to configure deserializer\n");
		goto err_i2c_dev;
	}

	return 0;

err_i2c_dev:
	mgb4_i2c_free(&vindev->deser);

	return rv;
}

static void fpga_init(struct mgb4_vin_dev *vindev)
{
	struct mgb4_regs *video = &vindev->mgbdev->video;
	const struct mgb4_vin_regs *regs = &vindev->config->regs;

	mgb4_write_reg(video, regs->config, 0x00000001);
	mgb4_write_reg(video, regs->sync, 0x03E80002);
	mgb4_write_reg(video, regs->padding, 0x00000000);
	mgb4_write_reg(video, regs->config, 1U << 9);
}

static void create_debugfs(struct mgb4_vin_dev *vindev)
{
#ifdef CONFIG_DEBUG_FS
	struct mgb4_regs *video = &vindev->mgbdev->video;
	struct dentry *entry;

	if (IS_ERR_OR_NULL(vindev->mgbdev->debugfs))
		return;
	entry = debugfs_create_dir(vindev->vdev.name, vindev->mgbdev->debugfs);
	if (IS_ERR(entry))
		return;

	vindev->regs[0].name = "CONFIG";
	vindev->regs[0].offset = vindev->config->regs.config;
	vindev->regs[1].name = "STATUS";
	vindev->regs[1].offset = vindev->config->regs.status;
	vindev->regs[2].name = "RESOLUTION";
	vindev->regs[2].offset = vindev->config->regs.resolution;
	vindev->regs[3].name = "FRAME_PERIOD";
	vindev->regs[3].offset = vindev->config->regs.frame_period;
	vindev->regs[4].name = "HS_VS_GENER_SETTINGS";
	vindev->regs[4].offset = vindev->config->regs.sync;
	vindev->regs[5].name = "PCLK_FREQUENCY";
	vindev->regs[5].offset = vindev->config->regs.pclk;
	vindev->regs[6].name = "VIDEO_PARAMS_1";
	vindev->regs[6].offset = vindev->config->regs.hsync;
	vindev->regs[7].name = "VIDEO_PARAMS_2";
	vindev->regs[7].offset = vindev->config->regs.vsync;
	vindev->regs[8].name = "PADDING_PIXELS";
	vindev->regs[8].offset = vindev->config->regs.padding;
	if (has_timeperframe(video)) {
		vindev->regs[9].name = "TIMER";
		vindev->regs[9].offset = vindev->config->regs.timer;
		vindev->regset.nregs = 10;
	} else {
		vindev->regset.nregs = 9;
	}

	vindev->regset.base = video->membase;
	vindev->regset.regs = vindev->regs;

	debugfs_create_regset32("registers", 0444, entry, &vindev->regset);
#endif
}

struct mgb4_vin_dev *mgb4_vin_create(struct mgb4_dev *mgbdev, int id)
{
	int rv;
	const struct attribute_group **groups;
	struct mgb4_vin_dev *vindev;
	struct pci_dev *pdev = mgbdev->pdev;
	struct device *dev = &pdev->dev;
	int vin_irq, err_irq;

	vindev = kzalloc(sizeof(*vindev), GFP_KERNEL);
	if (!vindev)
		return NULL;

	vindev->mgbdev = mgbdev;
	vindev->config = &vin_cfg[id];

	/* Frame queue*/
	INIT_LIST_HEAD(&vindev->buf_list);
	spin_lock_init(&vindev->qlock);

	/* Work queues */
	INIT_WORK(&vindev->dma_work, dma_transfer);
	INIT_WORK(&vindev->err_work, signal_change);

	/* IRQ callback */
	vin_irq = xdma_get_user_irq(mgbdev->xdev, vindev->config->vin_irq);
	rv = request_irq(vin_irq, vin_handler, 0, "mgb4-vin", vindev);
	if (rv) {
		dev_err(dev, "failed to register vin irq handler\n");
		goto err_alloc;
	}
	/* Error IRQ callback */
	err_irq = xdma_get_user_irq(mgbdev->xdev, vindev->config->err_irq);
	rv = request_irq(err_irq, err_handler, 0, "mgb4-err", vindev);
	if (rv) {
		dev_err(dev, "failed to register err irq handler\n");
		goto err_vin_irq;
	}

	/* Set the FPGA registers default values */
	fpga_init(vindev);

	/* Set the deserializer default values */
	rv = deser_init(vindev, id);
	if (rv)
		goto err_err_irq;

	/* V4L2 stuff init */
	rv = v4l2_device_register(dev, &vindev->v4l2dev);
	if (rv) {
		dev_err(dev, "failed to register v4l2 device\n");
		goto err_err_irq;
	}

	mutex_init(&vindev->lock);

	vindev->queue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vindev->queue.io_modes = VB2_MMAP | VB2_DMABUF | VB2_READ;
	vindev->queue.buf_struct_size = sizeof(struct mgb4_frame_buffer);
	vindev->queue.ops = &queue_ops;
	vindev->queue.mem_ops = &vb2_dma_sg_memops;
	vindev->queue.gfp_flags = GFP_DMA32;
	vindev->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vindev->queue.min_queued_buffers = 2;
	vindev->queue.drv_priv = vindev;
	vindev->queue.lock = &vindev->lock;
	vindev->queue.dev = dev;
	rv = vb2_queue_init(&vindev->queue);
	if (rv) {
		dev_err(dev, "failed to initialize vb2 queue\n");
		goto err_v4l2_dev;
	}

	snprintf(vindev->vdev.name, sizeof(vindev->vdev.name), "mgb4-in%d",
		 id + 1);
	vindev->vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE
	  | V4L2_CAP_STREAMING;
	vindev->vdev.fops = &video_fops;
	vindev->vdev.ioctl_ops = &video_ioctl_ops;
	vindev->vdev.release = video_device_release_empty;
	vindev->vdev.v4l2_dev = &vindev->v4l2dev;
	vindev->vdev.lock = &vindev->lock;
	vindev->vdev.queue = &vindev->queue;
	video_set_drvdata(&vindev->vdev, vindev);

	/* Enable the video signal change watcher */
	xdma_enable_user_irq(vindev->mgbdev->xdev, err_irq);

	/* Register the video device */
	rv = video_register_device(&vindev->vdev, VFL_TYPE_VIDEO, -1);
	if (rv) {
		dev_err(dev, "failed to register video device\n");
		goto err_v4l2_dev;
	}

	/* Module sysfs attributes */
	groups = MGB4_IS_GMSL(mgbdev)
	  ? mgb4_gmsl_in_groups : mgb4_fpdl3_in_groups;
	rv = device_add_groups(&vindev->vdev.dev, groups);
	if (rv) {
		dev_err(dev, "failed to create sysfs attributes\n");
		goto err_video_dev;
	}

	create_debugfs(vindev);

	return vindev;

err_video_dev:
	video_unregister_device(&vindev->vdev);
err_v4l2_dev:
	v4l2_device_unregister(&vindev->v4l2dev);
err_err_irq:
	free_irq(err_irq, vindev);
err_vin_irq:
	free_irq(vin_irq, vindev);
err_alloc:
	kfree(vindev);

	return NULL;
}

void mgb4_vin_free(struct mgb4_vin_dev *vindev)
{
	const struct attribute_group **groups;
	int vin_irq = xdma_get_user_irq(vindev->mgbdev->xdev,
					vindev->config->vin_irq);
	int err_irq = xdma_get_user_irq(vindev->mgbdev->xdev,
					vindev->config->err_irq);

	xdma_disable_user_irq(vindev->mgbdev->xdev, err_irq);

	free_irq(vin_irq, vindev);
	free_irq(err_irq, vindev);

	groups = MGB4_IS_GMSL(vindev->mgbdev)
	  ? mgb4_gmsl_in_groups : mgb4_fpdl3_in_groups;
	device_remove_groups(&vindev->vdev.dev, groups);

	mgb4_i2c_free(&vindev->deser);
	video_unregister_device(&vindev->vdev);
	v4l2_device_unregister(&vindev->v4l2dev);

	kfree(vindev);
}

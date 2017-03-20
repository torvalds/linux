/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 *  Based on Steven Toth <stoth@linuxtv.org> cx25821 driver
 *  Parts adapted/taken from Eduardo Moscoso Rubino
 *  Copyright (C) 2009 Eduardo Moscoso Rubino <moscoso@TopoLogica.com>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "cx25821-video.h"

MODULE_DESCRIPTION("v4l2 driver module for cx25821 based TV cards");
MODULE_AUTHOR("Hiep Huynh <hiep.huynh@conexant.com>");
MODULE_LICENSE("GPL");

static unsigned int video_nr[] = {[0 ... (CX25821_MAXBOARDS - 1)] = UNSET };

module_param_array(video_nr, int, NULL, 0444);

MODULE_PARM_DESC(video_nr, "video device numbers");

static unsigned int video_debug = VIDEO_DEBUG;
module_param(video_debug, int, 0644);
MODULE_PARM_DESC(video_debug, "enable debug messages [video]");

static unsigned int irq_debug;
module_param(irq_debug, int, 0644);
MODULE_PARM_DESC(irq_debug, "enable debug messages [IRQ handler]");

#define FORMAT_FLAGS_PACKED       0x01

static const struct cx25821_fmt formats[] = {
	{
		.name = "4:1:1, packed, Y41P",
		.fourcc = V4L2_PIX_FMT_Y41P,
		.depth = 12,
		.flags = FORMAT_FLAGS_PACKED,
	}, {
		.name = "4:2:2, packed, YUYV",
		.fourcc = V4L2_PIX_FMT_YUYV,
		.depth = 16,
		.flags = FORMAT_FLAGS_PACKED,
	},
};

static const struct cx25821_fmt *cx25821_format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (formats[i].fourcc == fourcc)
			return formats + i;
	return NULL;
}

int cx25821_start_video_dma(struct cx25821_dev *dev,
			    struct cx25821_dmaqueue *q,
			    struct cx25821_buffer *buf,
			    const struct sram_channel *channel)
{
	int tmp = 0;

	/* setup fifo + format */
	cx25821_sram_channel_setup(dev, channel, buf->bpl, buf->risc.dma);

	/* reset counter */
	cx_write(channel->gpcnt_ctl, 3);

	/* enable irq */
	cx_set(PCI_INT_MSK, cx_read(PCI_INT_MSK) | (1 << channel->i));
	cx_set(channel->int_msk, 0x11);

	/* start dma */
	cx_write(channel->dma_ctl, 0x11);	/* FIFO and RISC enable */

	/* make sure upstream setting if any is reversed */
	tmp = cx_read(VID_CH_MODE_SEL);
	cx_write(VID_CH_MODE_SEL, tmp & 0xFFFFFE00);

	return 0;
}

int cx25821_video_irq(struct cx25821_dev *dev, int chan_num, u32 status)
{
	int handled = 0;
	u32 mask;
	const struct sram_channel *channel = dev->channels[chan_num].sram_channels;

	mask = cx_read(channel->int_msk);
	if (0 == (status & mask))
		return handled;

	cx_write(channel->int_stat, status);

	/* risc op code error */
	if (status & (1 << 16)) {
		pr_warn("%s, %s: video risc op code error\n",
			dev->name, channel->name);
		cx_clear(channel->dma_ctl, 0x11);
		cx25821_sram_channel_dump(dev, channel);
	}

	/* risc1 y */
	if (status & FLD_VID_DST_RISC1) {
		struct cx25821_dmaqueue *dmaq =
			&dev->channels[channel->i].dma_vidq;
		struct cx25821_buffer *buf;

		spin_lock(&dev->slock);
		if (!list_empty(&dmaq->active)) {
			buf = list_entry(dmaq->active.next,
					 struct cx25821_buffer, queue);

			buf->vb.vb2_buf.timestamp = ktime_get_ns();
			buf->vb.sequence = dmaq->count++;
			list_del(&buf->queue);
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		}
		spin_unlock(&dev->slock);
		handled++;
	}
	return handled;
}

static int cx25821_queue_setup(struct vb2_queue *q,
			   unsigned int *num_buffers, unsigned int *num_planes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct cx25821_channel *chan = q->drv_priv;
	unsigned size = (chan->fmt->depth * chan->width * chan->height) >> 3;

	if (*num_planes)
		return sizes[0] < size ? -EINVAL : 0;

	*num_planes = 1;
	sizes[0] = size;
	return 0;
}

static int cx25821_buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cx25821_channel *chan = vb->vb2_queue->drv_priv;
	struct cx25821_dev *dev = chan->dev;
	struct cx25821_buffer *buf =
		container_of(vbuf, struct cx25821_buffer, vb);
	struct sg_table *sgt = vb2_dma_sg_plane_desc(vb, 0);
	u32 line0_offset;
	int bpl_local = LINE_SIZE_D1;
	int ret;

	if (chan->pixel_formats == PIXEL_FRMT_411)
		buf->bpl = (chan->fmt->depth * chan->width) >> 3;
	else
		buf->bpl = (chan->fmt->depth >> 3) * chan->width;

	if (vb2_plane_size(vb, 0) < chan->height * buf->bpl)
		return -EINVAL;
	vb2_set_plane_payload(vb, 0, chan->height * buf->bpl);
	buf->vb.field = chan->field;

	if (chan->pixel_formats == PIXEL_FRMT_411) {
		bpl_local = buf->bpl;
	} else {
		bpl_local = buf->bpl;   /* Default */

		if (chan->use_cif_resolution) {
			if (dev->tvnorm & V4L2_STD_625_50)
				bpl_local = 352 << 1;
			else
				bpl_local = chan->cif_width << 1;
		}
	}

	switch (chan->field) {
	case V4L2_FIELD_TOP:
		ret = cx25821_risc_buffer(dev->pci, &buf->risc,
				sgt->sgl, 0, UNSET,
				buf->bpl, 0, chan->height);
		break;
	case V4L2_FIELD_BOTTOM:
		ret = cx25821_risc_buffer(dev->pci, &buf->risc,
				sgt->sgl, UNSET, 0,
				buf->bpl, 0, chan->height);
		break;
	case V4L2_FIELD_INTERLACED:
		/* All other formats are top field first */
		line0_offset = 0;
		dprintk(1, "top field first\n");

		ret = cx25821_risc_buffer(dev->pci, &buf->risc,
				sgt->sgl, line0_offset,
				bpl_local, bpl_local, bpl_local,
				chan->height >> 1);
		break;
	case V4L2_FIELD_SEQ_TB:
		ret = cx25821_risc_buffer(dev->pci, &buf->risc,
				sgt->sgl,
				0, buf->bpl * (chan->height >> 1),
				buf->bpl, 0, chan->height >> 1);
		break;
	case V4L2_FIELD_SEQ_BT:
		ret = cx25821_risc_buffer(dev->pci, &buf->risc,
				sgt->sgl,
				buf->bpl * (chan->height >> 1), 0,
				buf->bpl, 0, chan->height >> 1);
		break;
	default:
		WARN_ON(1);
		ret = -EINVAL;
		break;
	}

	dprintk(2, "[%p/%d] buffer_prep - %dx%d %dbpp \"%s\" - dma=0x%08lx\n",
		buf, buf->vb.vb2_buf.index, chan->width, chan->height,
		chan->fmt->depth, chan->fmt->name,
		(unsigned long)buf->risc.dma);

	return ret;
}

static void cx25821_buffer_finish(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cx25821_buffer *buf =
		container_of(vbuf, struct cx25821_buffer, vb);
	struct cx25821_channel *chan = vb->vb2_queue->drv_priv;
	struct cx25821_dev *dev = chan->dev;

	cx25821_free_buffer(dev, buf);
}

static void cx25821_buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cx25821_buffer *buf =
		container_of(vbuf, struct cx25821_buffer, vb);
	struct cx25821_channel *chan = vb->vb2_queue->drv_priv;
	struct cx25821_dev *dev = chan->dev;
	struct cx25821_buffer *prev;
	struct cx25821_dmaqueue *q = &dev->channels[chan->id].dma_vidq;

	buf->risc.cpu[1] = cpu_to_le32(buf->risc.dma + 12);
	buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_CNT_INC);
	buf->risc.jmp[1] = cpu_to_le32(buf->risc.dma + 12);
	buf->risc.jmp[2] = cpu_to_le32(0); /* bits 63-32 */

	if (list_empty(&q->active)) {
		list_add_tail(&buf->queue, &q->active);
	} else {
		buf->risc.cpu[0] |= cpu_to_le32(RISC_IRQ1);
		prev = list_entry(q->active.prev, struct cx25821_buffer,
				queue);
		list_add_tail(&buf->queue, &q->active);
		prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
	}
}

static int cx25821_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct cx25821_channel *chan = q->drv_priv;
	struct cx25821_dev *dev = chan->dev;
	struct cx25821_dmaqueue *dmaq = &dev->channels[chan->id].dma_vidq;
	struct cx25821_buffer *buf = list_entry(dmaq->active.next,
			struct cx25821_buffer, queue);

	dmaq->count = 0;
	cx25821_start_video_dma(dev, dmaq, buf, chan->sram_channels);
	return 0;
}

static void cx25821_stop_streaming(struct vb2_queue *q)
{
	struct cx25821_channel *chan = q->drv_priv;
	struct cx25821_dev *dev = chan->dev;
	struct cx25821_dmaqueue *dmaq = &dev->channels[chan->id].dma_vidq;
	unsigned long flags;

	cx_write(chan->sram_channels->dma_ctl, 0); /* FIFO and RISC disable */
	spin_lock_irqsave(&dev->slock, flags);
	while (!list_empty(&dmaq->active)) {
		struct cx25821_buffer *buf = list_entry(dmaq->active.next,
			struct cx25821_buffer, queue);

		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&dev->slock, flags);
}

static const struct vb2_ops cx25821_video_qops = {
	.queue_setup    = cx25821_queue_setup,
	.buf_prepare  = cx25821_buffer_prepare,
	.buf_finish = cx25821_buffer_finish,
	.buf_queue    = cx25821_buffer_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = cx25821_start_streaming,
	.stop_streaming = cx25821_stop_streaming,
};

/* VIDEO IOCTLS */

static int cx25821_vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
			    struct v4l2_fmtdesc *f)
{
	if (unlikely(f->index >= ARRAY_SIZE(formats)))
		return -EINVAL;

	strlcpy(f->description, formats[f->index].name, sizeof(f->description));
	f->pixelformat = formats[f->index].fourcc;

	return 0;
}

static int cx25821_vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct cx25821_channel *chan = video_drvdata(file);

	f->fmt.pix.width = chan->width;
	f->fmt.pix.height = chan->height;
	f->fmt.pix.field = chan->field;
	f->fmt.pix.pixelformat = chan->fmt->fourcc;
	f->fmt.pix.bytesperline = (chan->width * chan->fmt->depth) >> 3;
	f->fmt.pix.sizeimage = chan->height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int cx25821_vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct cx25821_channel *chan = video_drvdata(file);
	struct cx25821_dev *dev = chan->dev;
	const struct cx25821_fmt *fmt;
	enum v4l2_field field = f->fmt.pix.field;
	unsigned int maxh;
	unsigned w;

	fmt = cx25821_format_by_fourcc(f->fmt.pix.pixelformat);
	if (NULL == fmt)
		return -EINVAL;
	maxh = (dev->tvnorm & V4L2_STD_625_50) ? 576 : 480;

	w = f->fmt.pix.width;
	if (field != V4L2_FIELD_BOTTOM)
		field = V4L2_FIELD_TOP;
	if (w < 352) {
		w = 176;
		f->fmt.pix.height = maxh / 4;
	} else if (w < 720) {
		w = 352;
		f->fmt.pix.height = maxh / 2;
	} else {
		w = 720;
		f->fmt.pix.height = maxh;
		field = V4L2_FIELD_INTERLACED;
	}
	f->fmt.pix.field = field;
	f->fmt.pix.width = w;
	f->fmt.pix.bytesperline = (f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cx25821_channel *chan = video_drvdata(file);
	struct cx25821_dev *dev = chan->dev;
	int pix_format = PIXEL_FRMT_422;
	int err;

	err = cx25821_vidioc_try_fmt_vid_cap(file, priv, f);

	if (0 != err)
		return err;

	chan->fmt = cx25821_format_by_fourcc(f->fmt.pix.pixelformat);
	chan->field = f->fmt.pix.field;
	chan->width = f->fmt.pix.width;
	chan->height = f->fmt.pix.height;

	if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_Y41P)
		pix_format = PIXEL_FRMT_411;
	else
		pix_format = PIXEL_FRMT_422;

	cx25821_set_pixel_format(dev, SRAM_CH00, pix_format);

	/* check if cif resolution */
	if (chan->width == 320 || chan->width == 352)
		chan->use_cif_resolution = 1;
	else
		chan->use_cif_resolution = 0;

	chan->cif_width = chan->width;
	medusa_set_resolution(dev, chan->width, SRAM_CH00);
	return 0;
}

static int vidioc_log_status(struct file *file, void *priv)
{
	struct cx25821_channel *chan = video_drvdata(file);
	struct cx25821_dev *dev = chan->dev;
	const struct sram_channel *sram_ch = chan->sram_channels;
	u32 tmp = 0;

	tmp = cx_read(sram_ch->dma_ctl);
	pr_info("Video input 0 is %s\n",
		(tmp & 0x11) ? "streaming" : "stopped");
	return 0;
}


static int cx25821_vidioc_querycap(struct file *file, void *priv,
			    struct v4l2_capability *cap)
{
	struct cx25821_channel *chan = video_drvdata(file);
	struct cx25821_dev *dev = chan->dev;
	const u32 cap_input = V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	const u32 cap_output = V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_READWRITE;

	strcpy(cap->driver, "cx25821");
	strlcpy(cap->card, cx25821_boards[dev->board].name, sizeof(cap->card));
	sprintf(cap->bus_info, "PCIe:%s", pci_name(dev->pci));
	if (chan->id >= VID_CHANNEL_NUM)
		cap->device_caps = cap_output;
	else
		cap->device_caps = cap_input;
	cap->capabilities = cap_input | cap_output | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int cx25821_vidioc_g_std(struct file *file, void *priv, v4l2_std_id *tvnorms)
{
	struct cx25821_channel *chan = video_drvdata(file);

	*tvnorms = chan->dev->tvnorm;
	return 0;
}

static int cx25821_vidioc_s_std(struct file *file, void *priv,
				v4l2_std_id tvnorms)
{
	struct cx25821_channel *chan = video_drvdata(file);
	struct cx25821_dev *dev = chan->dev;

	if (dev->tvnorm == tvnorms)
		return 0;

	dev->tvnorm = tvnorms;
	chan->width = 720;
	chan->height = (dev->tvnorm & V4L2_STD_625_50) ? 576 : 480;

	medusa_set_videostandard(dev);

	return 0;
}

static int cx25821_vidioc_enum_input(struct file *file, void *priv,
			      struct v4l2_input *i)
{
	if (i->index)
		return -EINVAL;

	i->type = V4L2_INPUT_TYPE_CAMERA;
	i->std = CX25821_NORMS;
	strcpy(i->name, "Composite");
	return 0;
}

static int cx25821_vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int cx25821_vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static int cx25821_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct cx25821_channel *chan =
		container_of(ctrl->handler, struct cx25821_channel, hdl);
	struct cx25821_dev *dev = chan->dev;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		medusa_set_brightness(dev, ctrl->val, chan->id);
		break;
	case V4L2_CID_HUE:
		medusa_set_hue(dev, ctrl->val, chan->id);
		break;
	case V4L2_CID_CONTRAST:
		medusa_set_contrast(dev, ctrl->val, chan->id);
		break;
	case V4L2_CID_SATURATION:
		medusa_set_saturation(dev, ctrl->val, chan->id);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int cx25821_vidioc_enum_output(struct file *file, void *priv,
			      struct v4l2_output *o)
{
	if (o->index)
		return -EINVAL;

	o->type = V4L2_INPUT_TYPE_CAMERA;
	o->std = CX25821_NORMS;
	strcpy(o->name, "Composite");
	return 0;
}

static int cx25821_vidioc_g_output(struct file *file, void *priv, unsigned int *o)
{
	*o = 0;
	return 0;
}

static int cx25821_vidioc_s_output(struct file *file, void *priv, unsigned int o)
{
	return o ? -EINVAL : 0;
}

static int cx25821_vidioc_try_fmt_vid_out(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct cx25821_channel *chan = video_drvdata(file);
	struct cx25821_dev *dev = chan->dev;
	const struct cx25821_fmt *fmt;

	fmt = cx25821_format_by_fourcc(f->fmt.pix.pixelformat);
	if (NULL == fmt)
		return -EINVAL;
	f->fmt.pix.width = 720;
	f->fmt.pix.height = (dev->tvnorm & V4L2_STD_625_50) ? 576 : 480;
	f->fmt.pix.field = V4L2_FIELD_INTERLACED;
	f->fmt.pix.bytesperline = (f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	return 0;
}

static int vidioc_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cx25821_channel *chan = video_drvdata(file);
	int err;

	err = cx25821_vidioc_try_fmt_vid_out(file, priv, f);

	if (0 != err)
		return err;

	chan->fmt = cx25821_format_by_fourcc(f->fmt.pix.pixelformat);
	chan->field = f->fmt.pix.field;
	chan->width = f->fmt.pix.width;
	chan->height = f->fmt.pix.height;
	if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_Y41P)
		chan->pixel_formats = PIXEL_FRMT_411;
	else
		chan->pixel_formats = PIXEL_FRMT_422;
	return 0;
}

static const struct v4l2_ctrl_ops cx25821_ctrl_ops = {
	.s_ctrl = cx25821_s_ctrl,
};

static const struct v4l2_file_operations video_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release        = vb2_fop_release,
	.read           = vb2_fop_read,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap = cx25821_vidioc_querycap,
	.vidioc_enum_fmt_vid_cap = cx25821_vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = cx25821_vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = cx25821_vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs       = vb2_ioctl_reqbufs,
	.vidioc_prepare_buf   = vb2_ioctl_prepare_buf,
	.vidioc_create_bufs   = vb2_ioctl_create_bufs,
	.vidioc_querybuf      = vb2_ioctl_querybuf,
	.vidioc_qbuf          = vb2_ioctl_qbuf,
	.vidioc_dqbuf         = vb2_ioctl_dqbuf,
	.vidioc_streamon      = vb2_ioctl_streamon,
	.vidioc_streamoff     = vb2_ioctl_streamoff,
	.vidioc_g_std = cx25821_vidioc_g_std,
	.vidioc_s_std = cx25821_vidioc_s_std,
	.vidioc_enum_input = cx25821_vidioc_enum_input,
	.vidioc_g_input = cx25821_vidioc_g_input,
	.vidioc_s_input = cx25821_vidioc_s_input,
	.vidioc_log_status = vidioc_log_status,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct video_device cx25821_video_device = {
	.name = "cx25821-video",
	.fops = &video_fops,
	.release = video_device_release_empty,
	.minor = -1,
	.ioctl_ops = &video_ioctl_ops,
	.tvnorms = CX25821_NORMS,
};

static const struct v4l2_file_operations video_out_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release        = vb2_fop_release,
	.write          = vb2_fop_write,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops video_out_ioctl_ops = {
	.vidioc_querycap = cx25821_vidioc_querycap,
	.vidioc_enum_fmt_vid_out = cx25821_vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_out = cx25821_vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_out = cx25821_vidioc_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out = vidioc_s_fmt_vid_out,
	.vidioc_g_std = cx25821_vidioc_g_std,
	.vidioc_s_std = cx25821_vidioc_s_std,
	.vidioc_enum_output = cx25821_vidioc_enum_output,
	.vidioc_g_output = cx25821_vidioc_g_output,
	.vidioc_s_output = cx25821_vidioc_s_output,
	.vidioc_log_status = vidioc_log_status,
};

static const struct video_device cx25821_video_out_device = {
	.name = "cx25821-video",
	.fops = &video_out_fops,
	.release = video_device_release_empty,
	.minor = -1,
	.ioctl_ops = &video_out_ioctl_ops,
	.tvnorms = CX25821_NORMS,
};

void cx25821_video_unregister(struct cx25821_dev *dev, int chan_num)
{
	cx_clear(PCI_INT_MSK, 1);

	if (video_is_registered(&dev->channels[chan_num].vdev)) {
		video_unregister_device(&dev->channels[chan_num].vdev);
		v4l2_ctrl_handler_free(&dev->channels[chan_num].hdl);
	}
}

int cx25821_video_register(struct cx25821_dev *dev)
{
	int err;
	int i;

	/* initial device configuration */
	dev->tvnorm = V4L2_STD_NTSC_M;

	spin_lock_init(&dev->slock);

	for (i = 0; i < MAX_VID_CAP_CHANNEL_NUM - 1; ++i) {
		struct cx25821_channel *chan = &dev->channels[i];
		struct video_device *vdev = &chan->vdev;
		struct v4l2_ctrl_handler *hdl = &chan->hdl;
		struct vb2_queue *q;
		bool is_output = i > SRAM_CH08;

		if (i == SRAM_CH08) /* audio channel */
			continue;

		if (!is_output) {
			v4l2_ctrl_handler_init(hdl, 4);
			v4l2_ctrl_new_std(hdl, &cx25821_ctrl_ops,
					V4L2_CID_BRIGHTNESS, 0, 10000, 1, 6200);
			v4l2_ctrl_new_std(hdl, &cx25821_ctrl_ops,
					V4L2_CID_CONTRAST, 0, 10000, 1, 5000);
			v4l2_ctrl_new_std(hdl, &cx25821_ctrl_ops,
					V4L2_CID_SATURATION, 0, 10000, 1, 5000);
			v4l2_ctrl_new_std(hdl, &cx25821_ctrl_ops,
					V4L2_CID_HUE, 0, 10000, 1, 5000);
			if (hdl->error) {
				err = hdl->error;
				goto fail_unreg;
			}
			err = v4l2_ctrl_handler_setup(hdl);
			if (err)
				goto fail_unreg;
		} else {
			chan->out = &dev->vid_out_data[i - SRAM_CH09];
			chan->out->chan = chan;
		}

		chan->sram_channels = &cx25821_sram_channels[i];
		chan->width = 720;
		chan->field = V4L2_FIELD_INTERLACED;
		if (dev->tvnorm & V4L2_STD_625_50)
			chan->height = 576;
		else
			chan->height = 480;

		if (chan->pixel_formats == PIXEL_FRMT_411)
			chan->fmt = cx25821_format_by_fourcc(V4L2_PIX_FMT_Y41P);
		else
			chan->fmt = cx25821_format_by_fourcc(V4L2_PIX_FMT_YUYV);

		cx_write(chan->sram_channels->int_stat, 0xffffffff);

		INIT_LIST_HEAD(&chan->dma_vidq.active);

		q = &chan->vidq;

		q->type = is_output ? V4L2_BUF_TYPE_VIDEO_OUTPUT :
				      V4L2_BUF_TYPE_VIDEO_CAPTURE;
		q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
		q->io_modes |= is_output ? VB2_WRITE : VB2_READ;
		q->gfp_flags = GFP_DMA32;
		q->min_buffers_needed = 2;
		q->drv_priv = chan;
		q->buf_struct_size = sizeof(struct cx25821_buffer);
		q->ops = &cx25821_video_qops;
		q->mem_ops = &vb2_dma_sg_memops;
		q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
		q->lock = &dev->lock;
		q->dev = &dev->pci->dev;

		if (!is_output) {
			err = vb2_queue_init(q);
			if (err < 0)
				goto fail_unreg;
		}

		/* register v4l devices */
		*vdev = is_output ? cx25821_video_out_device : cx25821_video_device;
		vdev->v4l2_dev = &dev->v4l2_dev;
		if (!is_output)
			vdev->ctrl_handler = hdl;
		else
			vdev->vfl_dir = VFL_DIR_TX;
		vdev->lock = &dev->lock;
		vdev->queue = q;
		snprintf(vdev->name, sizeof(vdev->name), "%s #%d", dev->name, i);
		video_set_drvdata(vdev, chan);

		err = video_register_device(vdev, VFL_TYPE_GRABBER,
					    video_nr[dev->nr]);

		if (err < 0)
			goto fail_unreg;
	}

	/* set PCI interrupt */
	cx_set(PCI_INT_MSK, 0xff);

	return 0;

fail_unreg:
	while (i >= 0)
		cx25821_video_unregister(dev, i--);
	return err;
}

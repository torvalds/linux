/*
 * Copyright (C) 2015 Industrial Research Institute for Automation
 * and Measurements PIAP
 *
 * Written by Krzysztof Ha?asa.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include "tw686x-kh.h"
#include "tw686x-kh-regs.h"

#define MAX_SG_ENTRY_SIZE (/* 8192 - 128 */ 4096)
#define MAX_SG_DESC_COUNT 256 /* PAL 704x576 needs up to 198 4-KB pages */

static const struct tw686x_format formats[] = {
	{
		.name = "4:2:2 packed, UYVY", /* aka Y422 */
		.fourcc = V4L2_PIX_FMT_UYVY,
		.mode = 0,
		.depth = 16,
	}, {
#if 0
		.name = "4:2:0 packed, YUV",
		.mode = 1,	/* non-standard */
		.depth = 12,
	}, {
		.name = "4:1:1 packed, YUV",
		.mode = 2,	/* non-standard */
		.depth = 12,
	}, {
#endif
		.name = "4:1:1 packed, YUV",
		.fourcc = V4L2_PIX_FMT_Y41P,
		.mode = 3,
		.depth = 12,
	}, {
		.name = "15 bpp RGB",
		.fourcc = V4L2_PIX_FMT_RGB555,
		.mode = 4,
		.depth = 16,
	}, {
		.name = "16 bpp RGB",
		.fourcc = V4L2_PIX_FMT_RGB565,
		.mode = 5,
		.depth = 16,
	}, {
		.name = "4:2:2 packed, YUYV",
		.fourcc = V4L2_PIX_FMT_YUYV,
		.mode = 6,
		.depth = 16,
	}
	/* mode 7 is "reserved" */
};

static const v4l2_std_id video_standards[7] = {
	V4L2_STD_NTSC,
	V4L2_STD_PAL,
	V4L2_STD_SECAM,
	V4L2_STD_NTSC_443,
	V4L2_STD_PAL_M,
	V4L2_STD_PAL_N,
	V4L2_STD_PAL_60,
};

static const struct tw686x_format *format_by_fourcc(unsigned int fourcc)
{
	unsigned int cnt;

	for (cnt = 0; cnt < ARRAY_SIZE(formats); cnt++)
		if (formats[cnt].fourcc == fourcc)
			return &formats[cnt];
	return NULL;
}

static void tw686x_get_format(struct tw686x_video_channel *vc,
			      struct v4l2_format *f)
{
	const struct tw686x_format *format;
	unsigned int width, height, height_div = 1;

	format = format_by_fourcc(f->fmt.pix.pixelformat);
	if (!format) {
		format = &formats[0];
		f->fmt.pix.pixelformat = format->fourcc;
	}

	width = 704;
	if (f->fmt.pix.width < width * 3 / 4 /* halfway */)
		width /= 2;

	height = (vc->video_standard & V4L2_STD_625_50) ? 576 : 480;
	if (f->fmt.pix.height < height * 3 / 4 /* halfway */)
		height_div = 2;

	switch (f->fmt.pix.field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
		height_div = 2;
		break;
	case V4L2_FIELD_SEQ_BT:
		if (height_div > 1)
			f->fmt.pix.field = V4L2_FIELD_BOTTOM;
		break;
	default:
		if (height_div > 1)
			f->fmt.pix.field = V4L2_FIELD_TOP;
		else
			f->fmt.pix.field = V4L2_FIELD_SEQ_TB;
	}
	height /= height_div;

	f->fmt.pix.width = width;
	f->fmt.pix.height = height;
	f->fmt.pix.bytesperline = f->fmt.pix.width * format->depth / 8;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
}

/* video queue operations */

static int tw686x_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
			      unsigned int *nplanes, unsigned int sizes[],
			      void *alloc_ctxs[])
{
	struct tw686x_video_channel *vc = vb2_get_drv_priv(vq);
	unsigned int size = vc->width * vc->height * vc->format->depth / 8;

	alloc_ctxs[0] = vc->alloc_ctx;
	if (*nbuffers < 2)
		*nbuffers = 2;

	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;

	sizes[0] = size;
	*nplanes = 1;		/* packed formats only */
	return 0;
}

static void tw686x_buf_queue(struct vb2_buffer *vb)
{
	struct tw686x_video_channel *vc = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct tw686x_vb2_buf *buf;

	buf = container_of(vbuf, struct tw686x_vb2_buf, vb);

	spin_lock(&vc->qlock);
	list_add_tail(&buf->list, &vc->vidq_queued);
	spin_unlock(&vc->qlock);
}

static void setup_descs(struct tw686x_video_channel *vc, unsigned int n)
{
loop:
	while (!list_empty(&vc->vidq_queued)) {
		struct vdma_desc *descs = vc->sg_descs[n];
		struct tw686x_vb2_buf *buf;
		struct sg_table *vbuf;
		struct scatterlist *sg;
		unsigned int buf_len, count = 0;
		int i;

		buf = list_first_entry(&vc->vidq_queued, struct tw686x_vb2_buf,
				       list);
		list_del(&buf->list);

		buf_len = vc->width * vc->height * vc->format->depth / 8;
		if (vb2_plane_size(&buf->vb.vb2_buf, 0) < buf_len) {
			pr_err("Video buffer size too small\n");
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
			goto loop; /* try another */
		}

		vbuf = vb2_dma_sg_plane_desc(&buf->vb.vb2_buf, 0);
		for_each_sg(vbuf->sgl, sg, vbuf->nents, i) {
			dma_addr_t phys = sg_dma_address(sg);
			unsigned int len = sg_dma_len(sg);

			while (len && buf_len) {
				unsigned int entry_len = min_t(unsigned int, len,
							   MAX_SG_ENTRY_SIZE);
				entry_len = min(entry_len, buf_len);
				if (count == MAX_SG_DESC_COUNT) {
					pr_err("Video buffer size too fragmented\n");
					vb2_buffer_done(&buf->vb.vb2_buf,
							VB2_BUF_STATE_ERROR);
					goto loop;
				}
				descs[count].phys = cpu_to_le32(phys);
				descs[count++].flags_length =
					cpu_to_le32(0x40000000 /* available */ |
						    entry_len);
				phys += entry_len;
				len -= entry_len;
				buf_len -= entry_len;
			}
			if (!buf_len)
				break;
		}

		/* clear the remaining entries */
		while (count < MAX_SG_DESC_COUNT) {
			descs[count].phys = 0;
			descs[count++].flags_length = 0; /* unavailable */
		}

		buf->vb.vb2_buf.state = VB2_BUF_STATE_ACTIVE;
		vc->curr_bufs[n] = buf;
		return;
	}
	vc->curr_bufs[n] = NULL;
}

/* On TW6864 and TW6868, all channels share the pair of video DMA SG tables,
   with 10-bit start_idx and end_idx determining start and end of frame buffer
   for particular channel.
   TW6868 with all its 8 channels would be problematic (only 127 SG entries per
   channel) but we support only 4 channels on this chip anyway (the first
   4 channels are driven with internal video decoder, the other 4 would require
   an external TW286x part).

   On TW6865 and TW6869, each channel has its own DMA SG table, with indexes
   starting with 0. Both chips have complete sets of internal video decoders
   (respectively 4 or 8-channel).

   All chips have separate SG tables for two video frames. */

static void setup_dma_cfg(struct tw686x_video_channel *vc)
{
	unsigned int field_width = 704;
	unsigned int field_height = (vc->video_standard & V4L2_STD_625_50) ?
		288 : 240;
	unsigned int start_idx = is_second_gen(vc->dev) ? 0 :
		vc->ch * MAX_SG_DESC_COUNT;
	unsigned int end_idx = start_idx + MAX_SG_DESC_COUNT - 1;
	u32 dma_cfg = (0 << 30) /* input selection */ |
		(1 << 29) /* field2 dropped (if any) */ |
		((vc->height < 300) << 28) /* field dropping */ |
		(1 << 27) /* master */ |
		(0 << 25) /* master channel (for slave only) */ |
		(0 << 24) /* (no) vertical (line) decimation */ |
		((vc->width < 400) << 23) /* horizontal decimation */ |
		(vc->format->mode << 20) /* output video format */ |
		(end_idx << 10) /* DMA end index */ |
		start_idx /* DMA start index */;
	u32 reg;

	reg_write(vc->dev, VDMA_CHANNEL_CONFIG[vc->ch], dma_cfg);
	reg_write(vc->dev, VIDEO_SIZE[vc->ch], (1 << 31) | (field_height << 16)
		  | field_width);
	reg = reg_read(vc->dev, VIDEO_CONTROL1);
	if (vc->video_standard & V4L2_STD_625_50)
		reg |= 1 << (vc->ch + 13);
	else
		reg &= ~(1 << (vc->ch + 13));
	reg_write(vc->dev, VIDEO_CONTROL1, reg);
}

static int tw686x_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct tw686x_video_channel *vc = vb2_get_drv_priv(vq);
	struct tw686x_dev *dev = vc->dev;
	u32 dma_ch_mask;
	unsigned int n;

	setup_dma_cfg(vc);

	/* queue video buffers if available */
	spin_lock(&vc->qlock);
	for (n = 0; n < 2; n++)
		setup_descs(vc, n);
	spin_unlock(&vc->qlock);

	dev->video_active |= 1 << vc->ch;
	vc->seq = 0;
	dma_ch_mask = reg_read(dev, DMA_CHANNEL_ENABLE) | (1 << vc->ch);
	reg_write(dev, DMA_CHANNEL_ENABLE, dma_ch_mask);
	reg_write(dev, DMA_CMD, (1 << 31) | dma_ch_mask);
	return 0;
}

static void tw686x_stop_streaming(struct vb2_queue *vq)
{
	struct tw686x_video_channel *vc = vb2_get_drv_priv(vq);
	struct tw686x_dev *dev = vc->dev;
	u32 dma_ch_mask = reg_read(dev, DMA_CHANNEL_ENABLE);
	u32 dma_cmd = reg_read(dev, DMA_CMD);
	unsigned int n;

	dma_ch_mask &= ~(1 << vc->ch);
	reg_write(dev, DMA_CHANNEL_ENABLE, dma_ch_mask);

	dev->video_active &= ~(1 << vc->ch);

	dma_cmd &= ~(1 << vc->ch);
	reg_write(dev, DMA_CMD, dma_cmd);

	if (!dev->video_active) {
		reg_write(dev, DMA_CMD, 0);
		reg_write(dev, DMA_CHANNEL_ENABLE, 0);
	}

	spin_lock(&vc->qlock);
	while (!list_empty(&vc->vidq_queued)) {
		struct tw686x_vb2_buf *buf;

		buf = list_entry(vc->vidq_queued.next, struct tw686x_vb2_buf,
				 list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	for (n = 0; n < 2; n++)
		if (vc->curr_bufs[n])
			vb2_buffer_done(&vc->curr_bufs[n]->vb.vb2_buf,
					VB2_BUF_STATE_ERROR);

	spin_unlock(&vc->qlock);
}

static struct vb2_ops tw686x_video_qops = {
	.queue_setup		= tw686x_queue_setup,
	.buf_queue		= tw686x_buf_queue,
	.start_streaming	= tw686x_start_streaming,
	.stop_streaming		= tw686x_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static int tw686x_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct tw686x_video_channel *vc;
	struct tw686x_dev *dev;
	unsigned int ch;

	vc = container_of(ctrl->handler, struct tw686x_video_channel,
			  ctrl_handler);
	dev = vc->dev;
	ch = vc->ch;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		reg_write(dev, BRIGHT[ch], ctrl->val & 0xFF);
		return 0;

	case V4L2_CID_CONTRAST:
		reg_write(dev, CONTRAST[ch], ctrl->val);
		return 0;

	case V4L2_CID_SATURATION:
		reg_write(dev, SAT_U[ch], ctrl->val);
		reg_write(dev, SAT_V[ch], ctrl->val);
		return 0;

	case V4L2_CID_HUE:
		reg_write(dev, HUE[ch], ctrl->val & 0xFF);
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops ctrl_ops = {
	.s_ctrl = tw686x_s_ctrl,
};

static int tw686x_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct tw686x_video_channel *vc = video_drvdata(file);

	f->fmt.pix.width = vc->width;
	f->fmt.pix.height = vc->height;
	f->fmt.pix.field = vc->field;
	f->fmt.pix.pixelformat = vc->format->fourcc;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.bytesperline = f->fmt.pix.width * vc->format->depth / 8;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	return 0;
}

static int tw686x_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	tw686x_get_format(video_drvdata(file), f);
	return 0;
}

static int tw686x_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct tw686x_video_channel *vc = video_drvdata(file);

	tw686x_get_format(vc, f);
	vc->format = format_by_fourcc(f->fmt.pix.pixelformat);
	vc->field = f->fmt.pix.field;
	vc->width = f->fmt.pix.width;
	vc->height = f->fmt.pix.height;
	return 0;
}

static int tw686x_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct tw686x_video_channel *vc = video_drvdata(file);
	struct tw686x_dev *dev = vc->dev;

	strcpy(cap->driver, "tw686x-kh");
	strcpy(cap->card, dev->name);
	sprintf(cap->bus_info, "PCI:%s", pci_name(dev->pci_dev));
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int tw686x_s_std(struct file *file, void *priv, v4l2_std_id id)
{
	struct tw686x_video_channel *vc = video_drvdata(file);
	unsigned int cnt;
	u32 sdt = 0; /* default */

	for (cnt = 0; cnt < ARRAY_SIZE(video_standards); cnt++)
		if (id & video_standards[cnt]) {
			sdt = cnt;
			break;
		}

	reg_write(vc->dev, SDT[vc->ch], sdt);
	vc->video_standard = video_standards[sdt];
	return 0;
}

static int tw686x_g_std(struct file *file, void *priv, v4l2_std_id *id)
{
	struct tw686x_video_channel *vc = video_drvdata(file);

	*id = vc->video_standard;
	return 0;
}

static int tw686x_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(formats))
		return -EINVAL;

	strlcpy(f->description, formats[f->index].name, sizeof(f->description));
	f->pixelformat = formats[f->index].fourcc;
	return 0;
}

static int tw686x_g_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *sp)
{
	struct tw686x_video_channel *vc = video_drvdata(file);

	if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	memset(&sp->parm.capture, 0, sizeof(sp->parm.capture));
	sp->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	v4l2_video_std_frame_period(vc->video_standard,
				    &sp->parm.capture.timeperframe);

	return 0;
}

static int tw686x_enum_input(struct file *file, void *priv,
			     struct v4l2_input *inp)
{
	/* the chip has internal multiplexer, support can be added
	   if the actual hw uses it */
	if (inp->index)
		return -EINVAL;

	snprintf(inp->name, sizeof(inp->name), "Composite");
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_ALL;
	inp->capabilities = V4L2_IN_CAP_STD;
	return 0;
}

static int tw686x_g_input(struct file *file, void *priv, unsigned int *v)
{
	*v = 0;
	return 0;
}

static int tw686x_s_input(struct file *file, void *priv, unsigned int v)
{
	if (v)
		return -EINVAL;
	return 0;
}

static const struct v4l2_file_operations tw686x_video_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.unlocked_ioctl	= video_ioctl2,
	.release	= vb2_fop_release,
	.poll		= vb2_fop_poll,
	.read		= vb2_fop_read,
	.mmap		= vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops tw686x_video_ioctl_ops = {
	.vidioc_querycap		= tw686x_querycap,
	.vidioc_enum_fmt_vid_cap	= tw686x_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= tw686x_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= tw686x_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= tw686x_try_fmt_vid_cap,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	.vidioc_g_std			= tw686x_g_std,
	.vidioc_s_std			= tw686x_s_std,
	.vidioc_g_parm			= tw686x_g_parm,
	.vidioc_enum_input		= tw686x_enum_input,
	.vidioc_g_input			= tw686x_g_input,
	.vidioc_s_input			= tw686x_s_input,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static int video_thread(void *arg)
{
	struct tw686x_dev *dev = arg;
	DECLARE_WAITQUEUE(wait, current);

	set_freezable();
	add_wait_queue(&dev->video_thread_wait, &wait);

	while (1) {
		long timeout = schedule_timeout_interruptible(HZ);
		unsigned int ch;

		if (timeout == -ERESTARTSYS || kthread_should_stop())
			break;

		for (ch = 0; ch < max_channels(dev); ch++) {
			struct tw686x_video_channel *vc;
			unsigned long flags;
			u32 request, n, stat = VB2_BUF_STATE_DONE;

			vc = &dev->video_channels[ch];
			if (!(dev->video_active & (1 << ch)))
				continue;

			spin_lock_irq(&dev->irq_lock);
			request = dev->dma_requests & (0x01000001 << ch);
			if (request)
				dev->dma_requests &= ~request;
			spin_unlock_irq(&dev->irq_lock);

			if (!request)
				continue;

			request >>= ch;

			/* handle channel events */
			if ((request & 0x01000000) |
			    (reg_read(dev, VIDEO_FIFO_STATUS) & (0x01010001 << ch)) |
			    (reg_read(dev, VIDEO_PARSER_STATUS) & (0x00000101 << ch))) {
				/* DMA Errors - reset channel */
				u32 reg;

				spin_lock_irqsave(&dev->irq_lock, flags);
				reg = reg_read(dev, DMA_CMD);
				/* Reset DMA channel */
				reg_write(dev, DMA_CMD, reg & ~(1 << ch));
				reg_write(dev, DMA_CMD, reg);
				spin_unlock_irqrestore(&dev->irq_lock, flags);
				stat = VB2_BUF_STATE_ERROR;
			}

			/* handle video stream */
			mutex_lock(&vc->vb_mutex);
			spin_lock(&vc->qlock);
			n = !!(reg_read(dev, PB_STATUS) & (1 << ch));
			if (vc->curr_bufs[n]) {
				struct vb2_v4l2_buffer *vb;

				vb = &vc->curr_bufs[n]->vb;
				vb->vb2_buf.timestamp = ktime_get_ns();
				vb->field = vc->field;
				if (V4L2_FIELD_HAS_BOTH(vc->field))
					vb->sequence = vc->seq++;
				else
					vb->sequence = (vc->seq++) / 2;
				vb2_set_plane_payload(&vb->vb2_buf, 0,
				      vc->width * vc->height * vc->format->depth / 8);
				vb2_buffer_done(&vb->vb2_buf, stat);
			}
			setup_descs(vc, n);
			spin_unlock(&vc->qlock);
			mutex_unlock(&vc->vb_mutex);
		}
		try_to_freeze();
	}

	remove_wait_queue(&dev->video_thread_wait, &wait);
	return 0;
}

int tw686x_kh_video_irq(struct tw686x_dev *dev)
{
	unsigned long flags, handled = 0;
	u32 requests;

	spin_lock_irqsave(&dev->irq_lock, flags);
	requests = dev->dma_requests;
	spin_unlock_irqrestore(&dev->irq_lock, flags);

	if (requests & dev->video_active) {
		wake_up_interruptible_all(&dev->video_thread_wait);
		handled = 1;
	}
	return handled;
}

void tw686x_kh_video_free(struct tw686x_dev *dev)
{
	unsigned int ch, n;

	if (dev->video_thread)
		kthread_stop(dev->video_thread);

	for (ch = 0; ch < max_channels(dev); ch++) {
		struct tw686x_video_channel *vc = &dev->video_channels[ch];

		v4l2_ctrl_handler_free(&vc->ctrl_handler);
		if (vc->device)
			video_unregister_device(vc->device);
		vb2_dma_sg_cleanup_ctx(vc->alloc_ctx);
		for (n = 0; n < 2; n++) {
			struct dma_desc *descs = &vc->sg_tables[n];

			if (descs->virt)
				pci_free_consistent(dev->pci_dev, descs->size,
						    descs->virt, descs->phys);
		}
	}

	v4l2_device_unregister(&dev->v4l2_dev);
}

#define SG_TABLE_SIZE (MAX_SG_DESC_COUNT * sizeof(struct vdma_desc))

int tw686x_kh_video_init(struct tw686x_dev *dev)
{
	unsigned int ch, n;
	int err;

	init_waitqueue_head(&dev->video_thread_wait);

	err = v4l2_device_register(&dev->pci_dev->dev, &dev->v4l2_dev);
	if (err)
		return err;

	reg_write(dev, VIDEO_CONTROL1, 0); /* NTSC, disable scaler */
	reg_write(dev, PHASE_REF, 0x00001518); /* Scatter-gather DMA mode */

	/* setup required SG table sizes */
	for (n = 0; n < 2; n++)
		if (is_second_gen(dev)) {
			/* TW 6865, TW6869 - each channel needs a pair of
			   descriptor tables */
			for (ch = 0; ch < max_channels(dev); ch++)
				dev->video_channels[ch].sg_tables[n].size =
					SG_TABLE_SIZE;

		} else
			/* TW 6864, TW6868 - we need to allocate a pair of
			   descriptor tables, common for all channels.
			   Each table will be bigger than 4 KB. */
			dev->video_channels[0].sg_tables[n].size =
				max_channels(dev) * SG_TABLE_SIZE;

	/* allocate SG tables and initialize video channels */
	for (ch = 0; ch < max_channels(dev); ch++) {
		struct tw686x_video_channel *vc = &dev->video_channels[ch];
		struct video_device *vdev;

		mutex_init(&vc->vb_mutex);
		spin_lock_init(&vc->qlock);
		INIT_LIST_HEAD(&vc->vidq_queued);

		vc->dev = dev;
		vc->ch = ch;

		/* default settings: NTSC */
		vc->format = &formats[0];
		vc->video_standard = V4L2_STD_NTSC;
		reg_write(vc->dev, SDT[vc->ch], 0);
		vc->field = V4L2_FIELD_SEQ_BT;
		vc->width = 704;
		vc->height = 480;

		for (n = 0; n < 2; n++) {
			void *cpu;

			if (vc->sg_tables[n].size) {
				unsigned int reg = n ? DMA_PAGE_TABLE1_ADDR[ch] :
					DMA_PAGE_TABLE0_ADDR[ch];

				cpu = pci_alloc_consistent(dev->pci_dev,
							   vc->sg_tables[n].size,
							   &vc->sg_tables[n].phys);
				if (!cpu) {
					pr_err("Error allocating video DMA scatter-gather tables\n");
					err = -ENOMEM;
					goto error;
				}
				vc->sg_tables[n].virt = cpu;
				reg_write(dev, reg, vc->sg_tables[n].phys);
			} else
				cpu = dev->video_channels[0].sg_tables[n].virt +
					ch * SG_TABLE_SIZE;

			vc->sg_descs[n] = cpu;
		}

		reg_write(dev, VCTRL1[0], 0x24);
		reg_write(dev, LOOP[0], 0xA5);
		if (max_channels(dev) > 4) {
			reg_write(dev, VCTRL1[1], 0x24);
			reg_write(dev, LOOP[1], 0xA5);
		}
		reg_write(dev, VIDEO_FIELD_CTRL[ch], 0);
		reg_write(dev, VDELAY_LO[ch], 0x14);

		vdev = video_device_alloc();
		if (!vdev) {
			pr_warn("Unable to allocate video device\n");
			err = -ENOMEM;
			goto error;
		}

		vc->alloc_ctx = vb2_dma_sg_init_ctx(&dev->pci_dev->dev);
		if (IS_ERR(vc->alloc_ctx)) {
			pr_warn("Unable to initialize DMA scatter-gather context\n");
			err = PTR_ERR(vc->alloc_ctx);
			goto error;
		}

		vc->vidq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		vc->vidq.io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
		vc->vidq.drv_priv = vc;
		vc->vidq.buf_struct_size = sizeof(struct tw686x_vb2_buf);
		vc->vidq.ops = &tw686x_video_qops;
		vc->vidq.mem_ops = &vb2_dma_sg_memops;
		vc->vidq.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
		vc->vidq.min_buffers_needed = 2;
		vc->vidq.lock = &vc->vb_mutex;
		vc->vidq.gfp_flags = GFP_DMA32;

		err = vb2_queue_init(&vc->vidq);
		if (err)
			goto error;

		strcpy(vdev->name, "TW686x-video");
		snprintf(vdev->name, sizeof(vdev->name), "%s video", dev->name);
		vdev->fops = &tw686x_video_fops;
		vdev->ioctl_ops = &tw686x_video_ioctl_ops;
		vdev->release = video_device_release;
		vdev->v4l2_dev = &dev->v4l2_dev;
		vdev->queue = &vc->vidq;
		vdev->tvnorms = V4L2_STD_ALL;
		vdev->minor = -1;
		vdev->lock = &vc->vb_mutex;

		dev->video_channels[ch].device = vdev;
		video_set_drvdata(vdev, vc);
		err = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
		if (err < 0)
			goto error;

		v4l2_ctrl_handler_init(&vc->ctrl_handler,
				       4 /* number of controls */);
		vdev->ctrl_handler = &vc->ctrl_handler;
		v4l2_ctrl_new_std(&vc->ctrl_handler, &ctrl_ops,
				  V4L2_CID_BRIGHTNESS, -128, 127, 1, 0);
		v4l2_ctrl_new_std(&vc->ctrl_handler, &ctrl_ops,
				  V4L2_CID_CONTRAST, 0, 255, 1, 64);
		v4l2_ctrl_new_std(&vc->ctrl_handler, &ctrl_ops,
				  V4L2_CID_SATURATION, 0, 255, 1, 128);
		v4l2_ctrl_new_std(&vc->ctrl_handler, &ctrl_ops, V4L2_CID_HUE,
				  -124, 127, 1, 0);
		err = vc->ctrl_handler.error;
		if (err)
			goto error;

		v4l2_ctrl_handler_setup(&vc->ctrl_handler);
	}

	dev->video_thread = kthread_run(video_thread, dev, "tw686x_video");
	if (IS_ERR(dev->video_thread)) {
		err = PTR_ERR(dev->video_thread);
		goto error;
	}

	return 0;

error:
	tw686x_kh_video_free(dev);
	return err;
}

/*
 * Copyright (C) 2015 VanguardiaSur - www.vanguardiasur.com.ar
 *
 * Based on original driver by Krzysztof Ha?asa:
 * Copyright (C) 2015 Industrial Research Institute for Automation
 * and Measurements PIAP
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-vmalloc.h>
#include "tw686x.h"
#include "tw686x-regs.h"

#define TW686X_INPUTS_PER_CH		4
#define TW686X_VIDEO_WIDTH		720
#define TW686X_VIDEO_HEIGHT(id)		((id & V4L2_STD_525_60) ? 480 : 576)

static const struct tw686x_format formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_UYVY,
		.mode = 0,
		.depth = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_RGB565,
		.mode = 5,
		.depth = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_YUYV,
		.mode = 6,
		.depth = 16,
	}
};

static unsigned int tw686x_fields_map(v4l2_std_id std, unsigned int fps)
{
	static const unsigned int map[15] = {
		0x00000000, 0x00000001, 0x00004001, 0x00104001, 0x00404041,
		0x01041041, 0x01104411, 0x01111111, 0x04444445, 0x04511445,
		0x05145145, 0x05151515, 0x05515455, 0x05551555, 0x05555555
	};

	static const unsigned int std_625_50[26] = {
		0, 1, 1, 2,  3,  3,  4,  4,  5,  5,  6,  7,  7,
		   8, 8, 9, 10, 10, 11, 11, 12, 13, 13, 14, 14, 0
	};

	static const unsigned int std_525_60[31] = {
		0, 1, 1, 1, 2,  2,  3,  3,  4,  4,  5,  5,  6,  6, 7, 7,
		   8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 0, 0
	};

	unsigned int i;

	if (std & V4L2_STD_525_60) {
		if (fps >= ARRAY_SIZE(std_525_60))
			fps = 30;
		i = std_525_60[fps];
	} else {
		if (fps >= ARRAY_SIZE(std_625_50))
			fps = 25;
		i = std_625_50[fps];
	}

	return map[i];
}

static void tw686x_set_framerate(struct tw686x_video_channel *vc,
				 unsigned int fps)
{
	unsigned int map;

	if (vc->fps == fps)
		return;

	map = tw686x_fields_map(vc->video_standard, fps) << 1;
	map |= map << 1;
	if (map > 0)
		map |= BIT(31);
	reg_write(vc->dev, VIDEO_FIELD_CTRL[vc->ch], map);
	vc->fps = fps;
}

static const struct tw686x_format *format_by_fourcc(unsigned int fourcc)
{
	unsigned int cnt;

	for (cnt = 0; cnt < ARRAY_SIZE(formats); cnt++)
		if (formats[cnt].fourcc == fourcc)
			return &formats[cnt];
	return NULL;
}

static int tw686x_queue_setup(struct vb2_queue *vq,
			      unsigned int *nbuffers, unsigned int *nplanes,
			      unsigned int sizes[], void *alloc_ctxs[])
{
	struct tw686x_video_channel *vc = vb2_get_drv_priv(vq);
	unsigned int szimage =
		(vc->width * vc->height * vc->format->depth) >> 3;

	/*
	 * Let's request at least three buffers: two for the
	 * DMA engine and one for userspace.
	 */
	if (vq->num_buffers + *nbuffers < 3)
		*nbuffers = 3 - vq->num_buffers;

	if (*nplanes) {
		if (*nplanes != 1 || sizes[0] < szimage)
			return -EINVAL;
		return 0;
	}

	sizes[0] = szimage;
	*nplanes = 1;
	return 0;
}

static void tw686x_buf_queue(struct vb2_buffer *vb)
{
	struct tw686x_video_channel *vc = vb2_get_drv_priv(vb->vb2_queue);
	struct tw686x_dev *dev = vc->dev;
	struct pci_dev *pci_dev;
	unsigned long flags;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct tw686x_v4l2_buf *buf =
		container_of(vbuf, struct tw686x_v4l2_buf, vb);

	/* Check device presence */
	spin_lock_irqsave(&dev->lock, flags);
	pci_dev = dev->pci_dev;
	spin_unlock_irqrestore(&dev->lock, flags);
	if (!pci_dev) {
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		return;
	}

	spin_lock_irqsave(&vc->qlock, flags);
	list_add_tail(&buf->list, &vc->vidq_queued);
	spin_unlock_irqrestore(&vc->qlock, flags);
}

/*
 * We can call this even when alloc_dma failed for the given channel
 */
static void tw686x_free_dma(struct tw686x_video_channel *vc, unsigned int pb)
{
	struct tw686x_dma_desc *desc = &vc->dma_descs[pb];
	struct tw686x_dev *dev = vc->dev;
	struct pci_dev *pci_dev;
	unsigned long flags;

	/* Check device presence. Shouldn't really happen! */
	spin_lock_irqsave(&dev->lock, flags);
	pci_dev = dev->pci_dev;
	spin_unlock_irqrestore(&dev->lock, flags);
	if (!pci_dev) {
		WARN(1, "trying to deallocate on missing device\n");
		return;
	}

	if (desc->virt) {
		pci_free_consistent(dev->pci_dev, desc->size,
				    desc->virt, desc->phys);
		desc->virt = NULL;
	}
}

static int tw686x_alloc_dma(struct tw686x_video_channel *vc, unsigned int pb)
{
	struct tw686x_dev *dev = vc->dev;
	u32 reg = pb ? VDMA_B_ADDR[vc->ch] : VDMA_P_ADDR[vc->ch];
	unsigned int len;
	void *virt;

	WARN(vc->dma_descs[pb].virt,
	     "Allocating buffer but previous still here\n");

	len = (vc->width * vc->height * vc->format->depth) >> 3;
	virt = pci_alloc_consistent(dev->pci_dev, len,
				    &vc->dma_descs[pb].phys);
	if (!virt) {
		v4l2_err(&dev->v4l2_dev,
			 "dma%d: unable to allocate %s-buffer\n",
			 vc->ch, pb ? "B" : "P");
		return -ENOMEM;
	}
	vc->dma_descs[pb].size = len;
	vc->dma_descs[pb].virt = virt;
	reg_write(dev, reg, vc->dma_descs[pb].phys);

	return 0;
}

static void tw686x_buffer_refill(struct tw686x_video_channel *vc,
				 unsigned int pb)
{
	struct tw686x_v4l2_buf *buf;

	while (!list_empty(&vc->vidq_queued)) {

		buf = list_first_entry(&vc->vidq_queued,
			struct tw686x_v4l2_buf, list);
		list_del(&buf->list);

		vc->curr_bufs[pb] = buf;
		return;
	}
	vc->curr_bufs[pb] = NULL;
}

static void tw686x_clear_queue(struct tw686x_video_channel *vc,
			       enum vb2_buffer_state state)
{
	unsigned int pb;

	while (!list_empty(&vc->vidq_queued)) {
		struct tw686x_v4l2_buf *buf;

		buf = list_first_entry(&vc->vidq_queued,
			struct tw686x_v4l2_buf, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}

	for (pb = 0; pb < 2; pb++) {
		if (vc->curr_bufs[pb])
			vb2_buffer_done(&vc->curr_bufs[pb]->vb.vb2_buf, state);
		vc->curr_bufs[pb] = NULL;
	}
}

static int tw686x_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct tw686x_video_channel *vc = vb2_get_drv_priv(vq);
	struct tw686x_dev *dev = vc->dev;
	struct pci_dev *pci_dev;
	unsigned long flags;
	int pb, err;

	/* Check device presence */
	spin_lock_irqsave(&dev->lock, flags);
	pci_dev = dev->pci_dev;
	spin_unlock_irqrestore(&dev->lock, flags);
	if (!pci_dev) {
		err = -ENODEV;
		goto err_clear_queue;
	}

	spin_lock_irqsave(&vc->qlock, flags);

	/* Sanity check */
	if (!vc->dma_descs[0].virt || !vc->dma_descs[1].virt) {
		spin_unlock_irqrestore(&vc->qlock, flags);
		v4l2_err(&dev->v4l2_dev,
			 "video%d: refusing to start without DMA buffers\n",
			 vc->num);
		err = -ENOMEM;
		goto err_clear_queue;
	}

	for (pb = 0; pb < 2; pb++)
		tw686x_buffer_refill(vc, pb);
	spin_unlock_irqrestore(&vc->qlock, flags);

	vc->sequence = 0;
	vc->pb = 0;

	spin_lock_irqsave(&dev->lock, flags);
	tw686x_enable_channel(dev, vc->ch);
	spin_unlock_irqrestore(&dev->lock, flags);

	mod_timer(&dev->dma_delay_timer, jiffies + msecs_to_jiffies(100));

	return 0;

err_clear_queue:
	spin_lock_irqsave(&vc->qlock, flags);
	tw686x_clear_queue(vc, VB2_BUF_STATE_QUEUED);
	spin_unlock_irqrestore(&vc->qlock, flags);
	return err;
}

static void tw686x_stop_streaming(struct vb2_queue *vq)
{
	struct tw686x_video_channel *vc = vb2_get_drv_priv(vq);
	struct tw686x_dev *dev = vc->dev;
	struct pci_dev *pci_dev;
	unsigned long flags;

	/* Check device presence */
	spin_lock_irqsave(&dev->lock, flags);
	pci_dev = dev->pci_dev;
	spin_unlock_irqrestore(&dev->lock, flags);
	if (pci_dev)
		tw686x_disable_channel(dev, vc->ch);

	spin_lock_irqsave(&vc->qlock, flags);
	tw686x_clear_queue(vc, VB2_BUF_STATE_ERROR);
	spin_unlock_irqrestore(&vc->qlock, flags);
}

static int tw686x_buf_prepare(struct vb2_buffer *vb)
{
	struct tw686x_video_channel *vc = vb2_get_drv_priv(vb->vb2_queue);
	unsigned int size =
		(vc->width * vc->height * vc->format->depth) >> 3;

	if (vb2_plane_size(vb, 0) < size)
		return -EINVAL;
	vb2_set_plane_payload(vb, 0, size);
	return 0;
}

static struct vb2_ops tw686x_video_qops = {
	.queue_setup		= tw686x_queue_setup,
	.buf_queue		= tw686x_buf_queue,
	.buf_prepare		= tw686x_buf_prepare,
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
		reg_write(dev, BRIGHT[ch], ctrl->val & 0xff);
		return 0;

	case V4L2_CID_CONTRAST:
		reg_write(dev, CONTRAST[ch], ctrl->val);
		return 0;

	case V4L2_CID_SATURATION:
		reg_write(dev, SAT_U[ch], ctrl->val);
		reg_write(dev, SAT_V[ch], ctrl->val);
		return 0;

	case V4L2_CID_HUE:
		reg_write(dev, HUE[ch], ctrl->val & 0xff);
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
	f->fmt.pix.field = V4L2_FIELD_INTERLACED;
	f->fmt.pix.pixelformat = vc->format->fourcc;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.bytesperline = (f->fmt.pix.width * vc->format->depth) / 8;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	return 0;
}

static int tw686x_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct tw686x_video_channel *vc = video_drvdata(file);
	unsigned int video_height = TW686X_VIDEO_HEIGHT(vc->video_standard);
	const struct tw686x_format *format;

	format = format_by_fourcc(f->fmt.pix.pixelformat);
	if (!format) {
		format = &formats[0];
		f->fmt.pix.pixelformat = format->fourcc;
	}

	if (f->fmt.pix.width <= TW686X_VIDEO_WIDTH / 2)
		f->fmt.pix.width = TW686X_VIDEO_WIDTH / 2;
	else
		f->fmt.pix.width = TW686X_VIDEO_WIDTH;

	if (f->fmt.pix.height <= video_height / 2)
		f->fmt.pix.height = video_height / 2;
	else
		f->fmt.pix.height = video_height;

	f->fmt.pix.bytesperline = (f->fmt.pix.width * format->depth) / 8;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.field = V4L2_FIELD_INTERLACED;

	return 0;
}

static int tw686x_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct tw686x_video_channel *vc = video_drvdata(file);
	u32 val, width, line_width, height;
	unsigned long bitsperframe;
	int err, pb;

	if (vb2_is_busy(&vc->vidq))
		return -EBUSY;

	bitsperframe = vc->width * vc->height * vc->format->depth;
	err = tw686x_try_fmt_vid_cap(file, priv, f);
	if (err)
		return err;

	vc->format = format_by_fourcc(f->fmt.pix.pixelformat);
	vc->width = f->fmt.pix.width;
	vc->height = f->fmt.pix.height;

	/* We need new DMA buffers if the framesize has changed */
	if (bitsperframe != vc->width * vc->height * vc->format->depth) {
		for (pb = 0; pb < 2; pb++)
			tw686x_free_dma(vc, pb);

		for (pb = 0; pb < 2; pb++) {
			err = tw686x_alloc_dma(vc, pb);
			if (err) {
				if (pb > 0)
					tw686x_free_dma(vc, 0);
				return err;
			}
		}
	}

	val = reg_read(vc->dev, VDMA_CHANNEL_CONFIG[vc->ch]);

	if (vc->width <= TW686X_VIDEO_WIDTH / 2)
		val |= BIT(23);
	else
		val &= ~BIT(23);

	if (vc->height <= TW686X_VIDEO_HEIGHT(vc->video_standard) / 2)
		val |= BIT(24);
	else
		val &= ~BIT(24);

	val &= ~(0x7 << 20);
	val |= vc->format->mode << 20;
	reg_write(vc->dev, VDMA_CHANNEL_CONFIG[vc->ch], val);

	/* Program the DMA frame size */
	width = (vc->width * 2) & 0x7ff;
	height = vc->height / 2;
	line_width = (vc->width * 2) & 0x7ff;
	val = (height << 22) | (line_width << 11)  | width;
	reg_write(vc->dev, VDMA_WHP[vc->ch], val);
	return 0;
}

static int tw686x_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct tw686x_video_channel *vc = video_drvdata(file);
	struct tw686x_dev *dev = vc->dev;

	strlcpy(cap->driver, "tw686x", sizeof(cap->driver));
	strlcpy(cap->card, dev->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "PCI:%s", pci_name(dev->pci_dev));
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
			   V4L2_CAP_READWRITE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int tw686x_s_std(struct file *file, void *priv, v4l2_std_id id)
{
	struct tw686x_video_channel *vc = video_drvdata(file);
	struct v4l2_format f;
	u32 val, ret;

	if (vc->video_standard == id)
		return 0;

	if (vb2_is_busy(&vc->vidq))
		return -EBUSY;

	if (id & V4L2_STD_NTSC)
		val = 0;
	else if (id & V4L2_STD_PAL)
		val = 1;
	else if (id & V4L2_STD_SECAM)
		val = 2;
	else if (id & V4L2_STD_NTSC_443)
		val = 3;
	else if (id & V4L2_STD_PAL_M)
		val = 4;
	else if (id & V4L2_STD_PAL_Nc)
		val = 5;
	else if (id & V4L2_STD_PAL_60)
		val = 6;
	else
		return -EINVAL;

	vc->video_standard = id;
	reg_write(vc->dev, SDT[vc->ch], val);

	val = reg_read(vc->dev, VIDEO_CONTROL1);
	if (id & V4L2_STD_525_60)
		val &= ~(1 << (SYS_MODE_DMA_SHIFT + vc->ch));
	else
		val |= (1 << (SYS_MODE_DMA_SHIFT + vc->ch));
	reg_write(vc->dev, VIDEO_CONTROL1, val);

	/*
	 * Adjust format after V4L2_STD_525_60/V4L2_STD_625_50 change,
	 * calling g_fmt and s_fmt will sanitize the height
	 * according to the standard.
	 */
	ret = tw686x_g_fmt_vid_cap(file, priv, &f);
	if (!ret)
		tw686x_s_fmt_vid_cap(file, priv, &f);
	return 0;
}

static int tw686x_querystd(struct file *file, void *priv, v4l2_std_id *std)
{
	struct tw686x_video_channel *vc = video_drvdata(file);
	struct tw686x_dev *dev = vc->dev;
	unsigned int old_std, detected_std = 0;
	unsigned long end;

	if (vb2_is_streaming(&vc->vidq))
		return -EBUSY;

	/* Enable and start standard detection */
	old_std = reg_read(dev, SDT[vc->ch]);
	reg_write(dev, SDT[vc->ch], 0x7);
	reg_write(dev, SDT_EN[vc->ch], 0xff);

	end = jiffies + msecs_to_jiffies(500);
	while (time_is_after_jiffies(end)) {

		detected_std = reg_read(dev, SDT[vc->ch]);
		if (!(detected_std & BIT(7)))
			break;
		msleep(100);
	}
	reg_write(dev, SDT[vc->ch], old_std);

	/* Exit if still busy */
	if (detected_std & BIT(7))
		return 0;

	detected_std = (detected_std >> 4) & 0x7;
	switch (detected_std) {
	case TW686X_STD_NTSC_M:
		*std &= V4L2_STD_NTSC;
		break;
	case TW686X_STD_NTSC_443:
		*std &= V4L2_STD_NTSC_443;
		break;
	case TW686X_STD_PAL_M:
		*std &= V4L2_STD_PAL_M;
		break;
	case TW686X_STD_PAL_60:
		*std &= V4L2_STD_PAL_60;
		break;
	case TW686X_STD_PAL:
		*std &= V4L2_STD_PAL;
		break;
	case TW686X_STD_PAL_CN:
		*std &= V4L2_STD_PAL_Nc;
		break;
	case TW686X_STD_SECAM:
		*std &= V4L2_STD_SECAM;
		break;
	default:
		*std = 0;
	}
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
	f->pixelformat = formats[f->index].fourcc;
	return 0;
}

static int tw686x_s_input(struct file *file, void *priv, unsigned int i)
{
	struct tw686x_video_channel *vc = video_drvdata(file);
	u32 val;

	if (i >= TW686X_INPUTS_PER_CH)
		return -EINVAL;
	if (i == vc->input)
		return 0;
	/*
	 * Not sure we are able to support on the fly input change
	 */
	if (vb2_is_busy(&vc->vidq))
		return -EBUSY;

	vc->input = i;

	val = reg_read(vc->dev, VDMA_CHANNEL_CONFIG[vc->ch]);
	val &= ~(0x3 << 30);
	val |= i << 30;
	reg_write(vc->dev, VDMA_CHANNEL_CONFIG[vc->ch], val);
	return 0;
}

static int tw686x_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct tw686x_video_channel *vc = video_drvdata(file);

	*i = vc->input;
	return 0;
}

static int tw686x_enum_input(struct file *file, void *priv,
			     struct v4l2_input *i)
{
	struct tw686x_video_channel *vc = video_drvdata(file);
	unsigned int vidstat;

	if (i->index >= TW686X_INPUTS_PER_CH)
		return -EINVAL;

	snprintf(i->name, sizeof(i->name), "Composite%d", i->index);
	i->type = V4L2_INPUT_TYPE_CAMERA;
	i->std = vc->device->tvnorms;
	i->capabilities = V4L2_IN_CAP_STD;

	vidstat = reg_read(vc->dev, VIDSTAT[vc->ch]);
	i->status = 0;
	if (vidstat & TW686X_VIDSTAT_VDLOSS)
		i->status |= V4L2_IN_ST_NO_SIGNAL;
	if (!(vidstat & TW686X_VIDSTAT_HLOCK))
		i->status |= V4L2_IN_ST_NO_H_LOCK;

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
	.vidioc_g_fmt_vid_cap		= tw686x_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= tw686x_s_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap	= tw686x_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= tw686x_try_fmt_vid_cap,

	.vidioc_querystd		= tw686x_querystd,
	.vidioc_g_std			= tw686x_g_std,
	.vidioc_s_std			= tw686x_s_std,

	.vidioc_enum_input		= tw686x_enum_input,
	.vidioc_g_input			= tw686x_g_input,
	.vidioc_s_input			= tw686x_s_input,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,

	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static void tw686x_buffer_copy(struct tw686x_video_channel *vc,
			       unsigned int pb, struct vb2_v4l2_buffer *vb)
{
	struct tw686x_dma_desc *desc = &vc->dma_descs[pb];
	struct vb2_buffer *vb2_buf = &vb->vb2_buf;

	vb->field = V4L2_FIELD_INTERLACED;
	vb->sequence = vc->sequence++;

	memcpy(vb2_plane_vaddr(vb2_buf, 0), desc->virt, desc->size);
	vb2_buf->timestamp = ktime_get_ns();
	vb2_buffer_done(vb2_buf, VB2_BUF_STATE_DONE);
}

void tw686x_video_irq(struct tw686x_dev *dev, unsigned long requests,
		      unsigned int pb_status, unsigned int fifo_status,
		      unsigned int *reset_ch)
{
	struct tw686x_video_channel *vc;
	struct vb2_v4l2_buffer *vb;
	unsigned long flags;
	unsigned int ch, pb;

	for_each_set_bit(ch, &requests, max_channels(dev)) {
		vc = &dev->video_channels[ch];

		/*
		 * This can either be a blue frame (with signal-lost bit set)
		 * or a good frame (with signal-lost bit clear). If we have just
		 * got signal, then this channel needs resetting.
		 */
		if (vc->no_signal && !(fifo_status & BIT(ch))) {
			v4l2_printk(KERN_DEBUG, &dev->v4l2_dev,
				    "video%d: signal recovered\n", vc->num);
			vc->no_signal = false;
			*reset_ch |= BIT(ch);
			vc->pb = 0;
			continue;
		}
		vc->no_signal = !!(fifo_status & BIT(ch));

		/* Check FIFO errors only if there's signal */
		if (!vc->no_signal) {
			u32 fifo_ov, fifo_bad;

			fifo_ov = (fifo_status >> 24) & BIT(ch);
			fifo_bad = (fifo_status >> 16) & BIT(ch);
			if (fifo_ov || fifo_bad) {
				/* Mark this channel for reset */
				v4l2_printk(KERN_DEBUG, &dev->v4l2_dev,
					    "video%d: FIFO error\n", vc->num);
				*reset_ch |= BIT(ch);
				vc->pb = 0;
				continue;
			}
		}

		pb = !!(pb_status & BIT(ch));
		if (vc->pb != pb) {
			/* Mark this channel for reset */
			v4l2_printk(KERN_DEBUG, &dev->v4l2_dev,
				    "video%d: unexpected p-b buffer!\n",
				    vc->num);
			*reset_ch |= BIT(ch);
			vc->pb = 0;
			continue;
		}

		/* handle video stream */
		spin_lock_irqsave(&vc->qlock, flags);
		if (vc->curr_bufs[pb]) {
			vb = &vc->curr_bufs[pb]->vb;
			tw686x_buffer_copy(vc, pb, vb);
		}
		vc->pb = !pb;
		tw686x_buffer_refill(vc, pb);
		spin_unlock_irqrestore(&vc->qlock, flags);
	}
}

void tw686x_video_free(struct tw686x_dev *dev)
{
	unsigned int ch, pb;

	for (ch = 0; ch < max_channels(dev); ch++) {
		struct tw686x_video_channel *vc = &dev->video_channels[ch];

		if (vc->device)
			video_unregister_device(vc->device);

		for (pb = 0; pb < 2; pb++)
			tw686x_free_dma(vc, pb);
	}
}

int tw686x_video_init(struct tw686x_dev *dev)
{
	unsigned int ch, val, pb;
	int err;

	err = v4l2_device_register(&dev->pci_dev->dev, &dev->v4l2_dev);
	if (err)
		return err;

	for (ch = 0; ch < max_channels(dev); ch++) {
		struct tw686x_video_channel *vc = &dev->video_channels[ch];
		struct video_device *vdev;

		mutex_init(&vc->vb_mutex);
		spin_lock_init(&vc->qlock);
		INIT_LIST_HEAD(&vc->vidq_queued);

		vc->dev = dev;
		vc->ch = ch;

		/* default settings */
		vc->format = &formats[0];
		vc->video_standard = V4L2_STD_NTSC;
		vc->width = TW686X_VIDEO_WIDTH;
		vc->height = TW686X_VIDEO_HEIGHT(vc->video_standard);
		vc->input = 0;

		reg_write(vc->dev, SDT[ch], 0);
		tw686x_set_framerate(vc, 30);

		reg_write(dev, VDELAY_LO[ch], 0x14);
		reg_write(dev, HACTIVE_LO[ch], 0xd0);
		reg_write(dev, VIDEO_SIZE[ch], 0);

		for (pb = 0; pb < 2; pb++) {
			err = tw686x_alloc_dma(vc, pb);
			if (err)
				goto error;
		}

		vc->vidq.io_modes = VB2_READ | VB2_MMAP | VB2_DMABUF;
		vc->vidq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		vc->vidq.drv_priv = vc;
		vc->vidq.buf_struct_size = sizeof(struct tw686x_v4l2_buf);
		vc->vidq.ops = &tw686x_video_qops;
		vc->vidq.mem_ops = &vb2_vmalloc_memops;
		vc->vidq.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
		vc->vidq.min_buffers_needed = 2;
		vc->vidq.lock = &vc->vb_mutex;
		vc->vidq.gfp_flags = GFP_DMA32;

		err = vb2_queue_init(&vc->vidq);
		if (err) {
			v4l2_err(&dev->v4l2_dev,
				 "dma%d: cannot init vb2 queue\n", ch);
			goto error;
		}

		err = v4l2_ctrl_handler_init(&vc->ctrl_handler, 4);
		if (err) {
			v4l2_err(&dev->v4l2_dev,
				 "dma%d: cannot init ctrl handler\n", ch);
			goto error;
		}
		v4l2_ctrl_new_std(&vc->ctrl_handler, &ctrl_ops,
				  V4L2_CID_BRIGHTNESS, -128, 127, 1, 0);
		v4l2_ctrl_new_std(&vc->ctrl_handler, &ctrl_ops,
				  V4L2_CID_CONTRAST, 0, 255, 1, 100);
		v4l2_ctrl_new_std(&vc->ctrl_handler, &ctrl_ops,
				  V4L2_CID_SATURATION, 0, 255, 1, 128);
		v4l2_ctrl_new_std(&vc->ctrl_handler, &ctrl_ops,
				  V4L2_CID_HUE, -128, 127, 1, 0);
		err = vc->ctrl_handler.error;
		if (err)
			goto error;

		err = v4l2_ctrl_handler_setup(&vc->ctrl_handler);
		if (err)
			goto error;

		vdev = video_device_alloc();
		if (!vdev) {
			v4l2_err(&dev->v4l2_dev,
				 "dma%d: unable to allocate device\n", ch);
			err = -ENOMEM;
			goto error;
		}

		snprintf(vdev->name, sizeof(vdev->name), "%s video", dev->name);
		vdev->fops = &tw686x_video_fops;
		vdev->ioctl_ops = &tw686x_video_ioctl_ops;
		vdev->release = video_device_release;
		vdev->v4l2_dev = &dev->v4l2_dev;
		vdev->queue = &vc->vidq;
		vdev->tvnorms = V4L2_STD_525_60 | V4L2_STD_625_50;
		vdev->minor = -1;
		vdev->lock = &vc->vb_mutex;
		vdev->ctrl_handler = &vc->ctrl_handler;
		vc->device = vdev;
		video_set_drvdata(vdev, vc);

		err = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
		if (err < 0)
			goto error;
		vc->num = vdev->num;
	}

	/* Set DMA frame mode on all channels. Only supported mode for now. */
	val = TW686X_DEF_PHASE_REF;
	for (ch = 0; ch < max_channels(dev); ch++)
		val |= TW686X_FRAME_MODE << (16 + ch * 2);
	reg_write(dev, PHASE_REF, val);

	reg_write(dev, MISC2[0], 0xe7);
	reg_write(dev, VCTRL1[0], 0xcc);
	reg_write(dev, LOOP[0], 0xa5);
	if (max_channels(dev) > 4) {
		reg_write(dev, VCTRL1[1], 0xcc);
		reg_write(dev, LOOP[1], 0xa5);
		reg_write(dev, MISC2[1], 0xe7);
	}
	return 0;

error:
	tw686x_video_free(dev);
	return err;
}

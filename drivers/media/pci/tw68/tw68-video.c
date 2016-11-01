/*
 *  tw68 functions to handle video data
 *
 *  Much of this code is derived from the cx88 and sa7134 drivers, which
 *  were in turn derived from the bt87x driver.  The original work was by
 *  Gerd Knorr; more recently the code was enhanced by Mauro Carvalho Chehab,
 *  Hans Verkuil, Andy Walls and many others.  Their work is gratefully
 *  acknowledged.  Full credit goes to them - any problems within this code
 *  are mine.
 *
 *  Copyright (C) 2009  William M. Brack
 *
 *  Refactored and updated to the latest v4l core frameworks:
 *
 *  Copyright (C) 2014 Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/module.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-dma-sg.h>

#include "tw68.h"
#include "tw68-reg.h"

/* ------------------------------------------------------------------ */
/* data structs for video                                             */
/*
 * FIXME -
 * Note that the saa7134 has formats, e.g. YUV420, which are classified
 * as "planar".  These affect overlay mode, and are flagged with a field
 * ".planar" in the format.  Do we need to implement this in this driver?
 */
static const struct tw68_format formats[] = {
	{
		.name		= "15 bpp RGB, le",
		.fourcc		= V4L2_PIX_FMT_RGB555,
		.depth		= 16,
		.twformat	= ColorFormatRGB15,
	}, {
		.name		= "15 bpp RGB, be",
		.fourcc		= V4L2_PIX_FMT_RGB555X,
		.depth		= 16,
		.twformat	= ColorFormatRGB15 | ColorFormatBSWAP,
	}, {
		.name		= "16 bpp RGB, le",
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.depth		= 16,
		.twformat	= ColorFormatRGB16,
	}, {
		.name		= "16 bpp RGB, be",
		.fourcc		= V4L2_PIX_FMT_RGB565X,
		.depth		= 16,
		.twformat	= ColorFormatRGB16 | ColorFormatBSWAP,
	}, {
		.name		= "24 bpp RGB, le",
		.fourcc		= V4L2_PIX_FMT_BGR24,
		.depth		= 24,
		.twformat	= ColorFormatRGB24,
	}, {
		.name		= "24 bpp RGB, be",
		.fourcc		= V4L2_PIX_FMT_RGB24,
		.depth		= 24,
		.twformat	= ColorFormatRGB24 | ColorFormatBSWAP,
	}, {
		.name		= "32 bpp RGB, le",
		.fourcc		= V4L2_PIX_FMT_BGR32,
		.depth		= 32,
		.twformat	= ColorFormatRGB32,
	}, {
		.name		= "32 bpp RGB, be",
		.fourcc		= V4L2_PIX_FMT_RGB32,
		.depth		= 32,
		.twformat	= ColorFormatRGB32 | ColorFormatBSWAP |
				  ColorFormatWSWAP,
	}, {
		.name		= "4:2:2 packed, YUYV",
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.depth		= 16,
		.twformat	= ColorFormatYUY2,
	}, {
		.name		= "4:2:2 packed, UYVY",
		.fourcc		= V4L2_PIX_FMT_UYVY,
		.depth		= 16,
		.twformat	= ColorFormatYUY2 | ColorFormatBSWAP,
	}
};
#define FORMATS ARRAY_SIZE(formats)

#define NORM_625_50			\
		.h_delay	= 3,	\
		.h_delay0	= 133,	\
		.h_start	= 0,	\
		.h_stop		= 719,	\
		.v_delay	= 24,	\
		.vbi_v_start_0	= 7,	\
		.vbi_v_stop_0	= 22,	\
		.video_v_start	= 24,	\
		.video_v_stop	= 311,	\
		.vbi_v_start_1	= 319

#define NORM_525_60			\
		.h_delay	= 8,	\
		.h_delay0	= 138,	\
		.h_start	= 0,	\
		.h_stop		= 719,	\
		.v_delay	= 22,	\
		.vbi_v_start_0	= 10,	\
		.vbi_v_stop_0	= 21,	\
		.video_v_start	= 22,	\
		.video_v_stop	= 262,	\
		.vbi_v_start_1	= 273

/*
 * The following table is searched by tw68_s_std, first for a specific
 * match, then for an entry which contains the desired id.  The table
 * entries should therefore be ordered in ascending order of specificity.
 */
static const struct tw68_tvnorm tvnorms[] = {
	{
		.name		= "PAL", /* autodetect */
		.id		= V4L2_STD_PAL,
		NORM_625_50,

		.sync_control	= 0x18,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0x81,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x06,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatPALBDGHI,
	}, {
		.name		= "NTSC",
		.id		= V4L2_STD_NTSC,
		NORM_525_60,

		.sync_control	= 0x59,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0x89,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x0e,
		.vgate_misc	= 0x18,
		.format		= VideoFormatNTSC,
	}, {
		.name		= "SECAM",
		.id		= V4L2_STD_SECAM,
		NORM_625_50,

		.sync_control	= 0x18,
		.luma_control	= 0x1b,
		.chroma_ctrl1	= 0xd1,
		.chroma_gain	= 0x80,
		.chroma_ctrl2	= 0x00,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatSECAM,
	}, {
		.name		= "PAL-M",
		.id		= V4L2_STD_PAL_M,
		NORM_525_60,

		.sync_control	= 0x59,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0xb9,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x0e,
		.vgate_misc	= 0x18,
		.format		= VideoFormatPALM,
	}, {
		.name		= "PAL-Nc",
		.id		= V4L2_STD_PAL_Nc,
		NORM_625_50,

		.sync_control	= 0x18,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0xa1,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x06,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatPALNC,
	}, {
		.name		= "PAL-60",
		.id		= V4L2_STD_PAL_60,
		.h_delay	= 186,
		.h_start	= 0,
		.h_stop		= 719,
		.v_delay	= 26,
		.video_v_start	= 23,
		.video_v_stop	= 262,
		.vbi_v_start_0	= 10,
		.vbi_v_stop_0	= 21,
		.vbi_v_start_1	= 273,

		.sync_control	= 0x18,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0x81,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x06,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatPAL60,
	}
};
#define TVNORMS ARRAY_SIZE(tvnorms)

static const struct tw68_format *format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < FORMATS; i++)
		if (formats[i].fourcc == fourcc)
			return formats+i;
	return NULL;
}


/* ------------------------------------------------------------------ */
/*
 * Note that the cropping rectangles are described in terms of a single
 * frame, i.e. line positions are only 1/2 the interlaced equivalent
 */
static void set_tvnorm(struct tw68_dev *dev, const struct tw68_tvnorm *norm)
{
	if (norm != dev->tvnorm) {
		dev->width = 720;
		dev->height = (norm->id & V4L2_STD_525_60) ? 480 : 576;
		dev->tvnorm = norm;
		tw68_set_tvnorm_hw(dev);
	}
}

/*
 * tw68_set_scale
 *
 * Scaling and Cropping for video decoding
 *
 * We are working with 3 values for horizontal and vertical - scale,
 * delay and active.
 *
 * HACTIVE represent the actual number of pixels in the "usable" image,
 * before scaling.  HDELAY represents the number of pixels skipped
 * between the start of the horizontal sync and the start of the image.
 * HSCALE is calculated using the formula
 *	HSCALE = (HACTIVE / (#pixels desired)) * 256
 *
 * The vertical registers are similar, except based upon the total number
 * of lines in the image, and the first line of the image (i.e. ignoring
 * vertical sync and VBI).
 *
 * Note that the number of bytes reaching the FIFO (and hence needing
 * to be processed by the DMAP program) is completely dependent upon
 * these values, especially HSCALE.
 *
 * Parameters:
 *	@dev		pointer to the device structure, needed for
 *			getting current norm (as well as debug print)
 *	@width		actual image width (from user buffer)
 *	@height		actual image height
 *	@field		indicates Top, Bottom or Interlaced
 */
static int tw68_set_scale(struct tw68_dev *dev, unsigned int width,
			  unsigned int height, enum v4l2_field field)
{
	const struct tw68_tvnorm *norm = dev->tvnorm;
	/* set individually for debugging clarity */
	int hactive, hdelay, hscale;
	int vactive, vdelay, vscale;
	int comb;

	if (V4L2_FIELD_HAS_BOTH(field))	/* if field is interlaced */
		height /= 2;		/* we must set for 1-frame */

	pr_debug("%s: width=%d, height=%d, both=%d\n"
		 "  tvnorm h_delay=%d, h_start=%d, h_stop=%d, "
		 "v_delay=%d, v_start=%d, v_stop=%d\n" , __func__,
		width, height, V4L2_FIELD_HAS_BOTH(field),
		norm->h_delay, norm->h_start, norm->h_stop,
		norm->v_delay, norm->video_v_start,
		norm->video_v_stop);

	switch (dev->vdecoder) {
	case TW6800:
		hdelay = norm->h_delay0;
		break;
	default:
		hdelay = norm->h_delay;
		break;
	}

	hdelay += norm->h_start;
	hactive = norm->h_stop - norm->h_start + 1;

	hscale = (hactive * 256) / (width);

	vdelay = norm->v_delay;
	vactive = ((norm->id & V4L2_STD_525_60) ? 524 : 624) / 2 - norm->video_v_start;
	vscale = (vactive * 256) / height;

	pr_debug("%s: %dx%d [%s%s,%s]\n", __func__,
		width, height,
		V4L2_FIELD_HAS_TOP(field)    ? "T" : "",
		V4L2_FIELD_HAS_BOTTOM(field) ? "B" : "",
		v4l2_norm_to_name(dev->tvnorm->id));
	pr_debug("%s: hactive=%d, hdelay=%d, hscale=%d; "
		"vactive=%d, vdelay=%d, vscale=%d\n", __func__,
		hactive, hdelay, hscale, vactive, vdelay, vscale);

	comb =	((vdelay & 0x300)  >> 2) |
		((vactive & 0x300) >> 4) |
		((hdelay & 0x300)  >> 6) |
		((hactive & 0x300) >> 8);
	pr_debug("%s: setting CROP_HI=%02x, VDELAY_LO=%02x, "
		"VACTIVE_LO=%02x, HDELAY_LO=%02x, HACTIVE_LO=%02x\n",
		__func__, comb, vdelay, vactive, hdelay, hactive);
	tw_writeb(TW68_CROP_HI, comb);
	tw_writeb(TW68_VDELAY_LO, vdelay & 0xff);
	tw_writeb(TW68_VACTIVE_LO, vactive & 0xff);
	tw_writeb(TW68_HDELAY_LO, hdelay & 0xff);
	tw_writeb(TW68_HACTIVE_LO, hactive & 0xff);

	comb = ((vscale & 0xf00) >> 4) | ((hscale & 0xf00) >> 8);
	pr_debug("%s: setting SCALE_HI=%02x, VSCALE_LO=%02x, "
		"HSCALE_LO=%02x\n", __func__, comb, vscale, hscale);
	tw_writeb(TW68_SCALE_HI, comb);
	tw_writeb(TW68_VSCALE_LO, vscale);
	tw_writeb(TW68_HSCALE_LO, hscale);

	return 0;
}

/* ------------------------------------------------------------------ */

int tw68_video_start_dma(struct tw68_dev *dev, struct tw68_buf *buf)
{
	/* Set cropping and scaling */
	tw68_set_scale(dev, dev->width, dev->height, dev->field);
	/*
	 *  Set start address for RISC program.  Note that if the DMAP
	 *  processor is currently running, it must be stopped before
	 *  a new address can be set.
	 */
	tw_clearl(TW68_DMAC, TW68_DMAP_EN);
	tw_writel(TW68_DMAP_SA, buf->dma);
	/* Clear any pending interrupts */
	tw_writel(TW68_INTSTAT, dev->board_virqmask);
	/* Enable the risc engine and the fifo */
	tw_andorl(TW68_DMAC, 0xff, dev->fmt->twformat |
		ColorFormatGamma | TW68_DMAP_EN | TW68_FIFO_EN);
	dev->pci_irqmask |= dev->board_virqmask;
	tw_setl(TW68_INTMASK, dev->pci_irqmask);
	return 0;
}

/* ------------------------------------------------------------------ */

/* calc max # of buffers from size (must not exceed the 4MB virtual
 * address space per DMA channel) */
static int tw68_buffer_count(unsigned int size, unsigned int count)
{
	unsigned int maxcount;

	maxcount = (4 * 1024 * 1024) / roundup(size, PAGE_SIZE);
	if (count > maxcount)
		count = maxcount;
	return count;
}

/* ------------------------------------------------------------- */
/* vb2 queue operations                                          */

static int tw68_queue_setup(struct vb2_queue *q,
			   unsigned int *num_buffers, unsigned int *num_planes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct tw68_dev *dev = vb2_get_drv_priv(q);
	unsigned tot_bufs = q->num_buffers + *num_buffers;
	unsigned size = (dev->fmt->depth * dev->width * dev->height) >> 3;

	if (tot_bufs < 2)
		tot_bufs = 2;
	tot_bufs = tw68_buffer_count(size, tot_bufs);
	*num_buffers = tot_bufs - q->num_buffers;
	/*
	 * We allow create_bufs, but only if the sizeimage is >= as the
	 * current sizeimage. The tw68_buffer_count calculation becomes quite
	 * difficult otherwise.
	 */
	if (*num_planes)
		return sizes[0] < size ? -EINVAL : 0;
	*num_planes = 1;
	sizes[0] = size;

	return 0;
}

/*
 * The risc program for each buffers works as follows: it starts with a simple
 * 'JUMP to addr + 8', which is effectively a NOP. Then the program to DMA the
 * buffer follows and at the end we have a JUMP back to the start + 8 (skipping
 * the initial JUMP).
 *
 * This is the program of the first buffer to be queued if the active list is
 * empty and it just keeps DMAing this buffer without generating any interrupts.
 *
 * If a new buffer is added then the initial JUMP in the program generates an
 * interrupt as well which signals that the previous buffer has been DMAed
 * successfully and that it can be returned to userspace.
 *
 * It also sets the final jump of the previous buffer to the start of the new
 * buffer, thus chaining the new buffer into the DMA chain. This is a single
 * atomic u32 write, so there is no race condition.
 *
 * The end-result of all this that you only get an interrupt when a buffer
 * is ready, so the control flow is very easy.
 */
static void tw68_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_queue *vq = vb->vb2_queue;
	struct tw68_dev *dev = vb2_get_drv_priv(vq);
	struct tw68_buf *buf = container_of(vbuf, struct tw68_buf, vb);
	struct tw68_buf *prev;
	unsigned long flags;

	spin_lock_irqsave(&dev->slock, flags);

	/* append a 'JUMP to start of buffer' to the buffer risc program */
	buf->jmp[0] = cpu_to_le32(RISC_JUMP);
	buf->jmp[1] = cpu_to_le32(buf->dma + 8);

	if (!list_empty(&dev->active)) {
		prev = list_entry(dev->active.prev, struct tw68_buf, list);
		buf->cpu[0] |= cpu_to_le32(RISC_INT_BIT);
		prev->jmp[1] = cpu_to_le32(buf->dma);
	}
	list_add_tail(&buf->list, &dev->active);
	spin_unlock_irqrestore(&dev->slock, flags);
}

/*
 * buffer_prepare
 *
 * Set the ancilliary information into the buffer structure.  This
 * includes generating the necessary risc program if it hasn't already
 * been done for the current buffer format.
 * The structure fh contains the details of the format requested by the
 * user - type, width, height and #fields.  This is compared with the
 * last format set for the current buffer.  If they differ, the risc
 * code (which controls the filling of the buffer) is (re-)generated.
 */
static int tw68_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_queue *vq = vb->vb2_queue;
	struct tw68_dev *dev = vb2_get_drv_priv(vq);
	struct tw68_buf *buf = container_of(vbuf, struct tw68_buf, vb);
	struct sg_table *dma = vb2_dma_sg_plane_desc(vb, 0);
	unsigned size, bpl;

	size = (dev->width * dev->height * dev->fmt->depth) >> 3;
	if (vb2_plane_size(vb, 0) < size)
		return -EINVAL;
	vb2_set_plane_payload(vb, 0, size);

	bpl = (dev->width * dev->fmt->depth) >> 3;
	switch (dev->field) {
	case V4L2_FIELD_TOP:
		tw68_risc_buffer(dev->pci, buf, dma->sgl,
				 0, UNSET, bpl, 0, dev->height);
		break;
	case V4L2_FIELD_BOTTOM:
		tw68_risc_buffer(dev->pci, buf, dma->sgl,
				 UNSET, 0, bpl, 0, dev->height);
		break;
	case V4L2_FIELD_SEQ_TB:
		tw68_risc_buffer(dev->pci, buf, dma->sgl,
				 0, bpl * (dev->height >> 1),
				 bpl, 0, dev->height >> 1);
		break;
	case V4L2_FIELD_SEQ_BT:
		tw68_risc_buffer(dev->pci, buf, dma->sgl,
				 bpl * (dev->height >> 1), 0,
				 bpl, 0, dev->height >> 1);
		break;
	case V4L2_FIELD_INTERLACED:
	default:
		tw68_risc_buffer(dev->pci, buf, dma->sgl,
				 0, bpl, bpl, bpl, dev->height >> 1);
		break;
	}
	return 0;
}

static void tw68_buf_finish(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_queue *vq = vb->vb2_queue;
	struct tw68_dev *dev = vb2_get_drv_priv(vq);
	struct tw68_buf *buf = container_of(vbuf, struct tw68_buf, vb);

	pci_free_consistent(dev->pci, buf->size, buf->cpu, buf->dma);
}

static int tw68_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct tw68_dev *dev = vb2_get_drv_priv(q);
	struct tw68_buf *buf =
		container_of(dev->active.next, struct tw68_buf, list);

	dev->seqnr = 0;
	tw68_video_start_dma(dev, buf);
	return 0;
}

static void tw68_stop_streaming(struct vb2_queue *q)
{
	struct tw68_dev *dev = vb2_get_drv_priv(q);

	/* Stop risc & fifo */
	tw_clearl(TW68_DMAC, TW68_DMAP_EN | TW68_FIFO_EN);
	while (!list_empty(&dev->active)) {
		struct tw68_buf *buf =
			container_of(dev->active.next, struct tw68_buf, list);

		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
}

static struct vb2_ops tw68_video_qops = {
	.queue_setup	= tw68_queue_setup,
	.buf_queue	= tw68_buf_queue,
	.buf_prepare	= tw68_buf_prepare,
	.buf_finish	= tw68_buf_finish,
	.start_streaming = tw68_start_streaming,
	.stop_streaming = tw68_stop_streaming,
	.wait_prepare	= vb2_ops_wait_prepare,
	.wait_finish	= vb2_ops_wait_finish,
};

/* ------------------------------------------------------------------ */

static int tw68_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct tw68_dev *dev =
		container_of(ctrl->handler, struct tw68_dev, hdl);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		tw_writeb(TW68_BRIGHT, ctrl->val);
		break;
	case V4L2_CID_HUE:
		tw_writeb(TW68_HUE, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		tw_writeb(TW68_CONTRAST, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		tw_writeb(TW68_SAT_U, ctrl->val);
		tw_writeb(TW68_SAT_V, ctrl->val);
		break;
	case V4L2_CID_COLOR_KILLER:
		if (ctrl->val)
			tw_andorb(TW68_MISC2, 0xe0, 0xe0);
		else
			tw_andorb(TW68_MISC2, 0xe0, 0x00);
		break;
	case V4L2_CID_CHROMA_AGC:
		if (ctrl->val)
			tw_andorb(TW68_LOOP, 0x30, 0x20);
		else
			tw_andorb(TW68_LOOP, 0x30, 0x00);
		break;
	}
	return 0;
}

/* ------------------------------------------------------------------ */

/*
 * Note that this routine returns what is stored in the fh structure, and
 * does not interrogate any of the device registers.
 */
static int tw68_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct tw68_dev *dev = video_drvdata(file);

	f->fmt.pix.width        = dev->width;
	f->fmt.pix.height       = dev->height;
	f->fmt.pix.field        = dev->field;
	f->fmt.pix.pixelformat  = dev->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * (dev->fmt->depth)) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace	= V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.priv = 0;
	return 0;
}

static int tw68_try_fmt_vid_cap(struct file *file, void *priv,
						struct v4l2_format *f)
{
	struct tw68_dev *dev = video_drvdata(file);
	const struct tw68_format *fmt;
	enum v4l2_field field;
	unsigned int maxh;

	fmt = format_by_fourcc(f->fmt.pix.pixelformat);
	if (NULL == fmt)
		return -EINVAL;

	field = f->fmt.pix.field;
	maxh  = (dev->tvnorm->id & V4L2_STD_525_60) ? 480 : 576;

	switch (field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
		break;
	case V4L2_FIELD_INTERLACED:
	case V4L2_FIELD_SEQ_BT:
	case V4L2_FIELD_SEQ_TB:
		maxh = maxh * 2;
		break;
	default:
		field = (f->fmt.pix.height > maxh / 2)
			? V4L2_FIELD_INTERLACED
			: V4L2_FIELD_BOTTOM;
		break;
	}

	f->fmt.pix.field = field;
	if (f->fmt.pix.width  < 48)
		f->fmt.pix.width  = 48;
	if (f->fmt.pix.height < 32)
		f->fmt.pix.height = 32;
	if (f->fmt.pix.width > 720)
		f->fmt.pix.width = 720;
	if (f->fmt.pix.height > maxh)
		f->fmt.pix.height = maxh;
	f->fmt.pix.width &= ~0x03;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * (fmt->depth)) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	return 0;
}

/*
 * Note that tw68_s_fmt_vid_cap sets the information into the fh structure,
 * and it will be used for all future new buffers.  However, there could be
 * some number of buffers on the "active" chain which will be filled before
 * the change takes place.
 */
static int tw68_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct tw68_dev *dev = video_drvdata(file);
	int err;

	err = tw68_try_fmt_vid_cap(file, priv, f);
	if (0 != err)
		return err;

	dev->fmt = format_by_fourcc(f->fmt.pix.pixelformat);
	dev->width = f->fmt.pix.width;
	dev->height = f->fmt.pix.height;
	dev->field = f->fmt.pix.field;
	return 0;
}

static int tw68_enum_input(struct file *file, void *priv,
					struct v4l2_input *i)
{
	struct tw68_dev *dev = video_drvdata(file);
	unsigned int n;

	n = i->index;
	if (n >= TW68_INPUT_MAX)
		return -EINVAL;
	i->index = n;
	i->type = V4L2_INPUT_TYPE_CAMERA;
	snprintf(i->name, sizeof(i->name), "Composite %d", n);

	/* If the query is for the current input, get live data */
	if (n == dev->input) {
		int v1 = tw_readb(TW68_STATUS1);
		int v2 = tw_readb(TW68_MVSN);

		if (0 != (v1 & (1 << 7)))
			i->status |= V4L2_IN_ST_NO_SYNC;
		if (0 != (v1 & (1 << 6)))
			i->status |= V4L2_IN_ST_NO_H_LOCK;
		if (0 != (v1 & (1 << 2)))
			i->status |= V4L2_IN_ST_NO_SIGNAL;
		if (0 != (v1 & 1 << 1))
			i->status |= V4L2_IN_ST_NO_COLOR;
		if (0 != (v2 & (1 << 2)))
			i->status |= V4L2_IN_ST_MACROVISION;
	}
	i->std = video_devdata(file)->tvnorms;
	return 0;
}

static int tw68_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct tw68_dev *dev = video_drvdata(file);

	*i = dev->input;
	return 0;
}

static int tw68_s_input(struct file *file, void *priv, unsigned int i)
{
	struct tw68_dev *dev = video_drvdata(file);

	if (i >= TW68_INPUT_MAX)
		return -EINVAL;
	dev->input = i;
	tw_andorb(TW68_INFORM, 0x03 << 2, dev->input << 2);
	return 0;
}

static int tw68_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct tw68_dev *dev = video_drvdata(file);

	strcpy(cap->driver, "tw68");
	strlcpy(cap->card, "Techwell Capture Card",
		sizeof(cap->card));
	sprintf(cap->bus_info, "PCI:%s", pci_name(dev->pci));
	cap->device_caps =
		V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_READWRITE |
		V4L2_CAP_STREAMING;

	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int tw68_s_std(struct file *file, void *priv, v4l2_std_id id)
{
	struct tw68_dev *dev = video_drvdata(file);
	unsigned int i;

	if (vb2_is_busy(&dev->vidq))
		return -EBUSY;

	/* Look for match on complete norm id (may have mult bits) */
	for (i = 0; i < TVNORMS; i++) {
		if (id == tvnorms[i].id)
			break;
	}

	/* If no exact match, look for norm which contains this one */
	if (i == TVNORMS) {
		for (i = 0; i < TVNORMS; i++)
			if (id & tvnorms[i].id)
				break;
	}
	/* If still not matched, give up */
	if (i == TVNORMS)
		return -EINVAL;

	set_tvnorm(dev, &tvnorms[i]);	/* do the actual setting */
	return 0;
}

static int tw68_g_std(struct file *file, void *priv, v4l2_std_id *id)
{
	struct tw68_dev *dev = video_drvdata(file);

	*id = dev->tvnorm->id;
	return 0;
}

static int tw68_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (f->index >= FORMATS)
		return -EINVAL;

	strlcpy(f->description, formats[f->index].name,
		sizeof(f->description));

	f->pixelformat = formats[f->index].fourcc;

	return 0;
}

/*
 * Used strictly for internal development and debugging, this routine
 * prints out the current register contents for the tw68xx device.
 */
static void tw68_dump_regs(struct tw68_dev *dev)
{
	unsigned char line[80];
	int i, j, k;
	unsigned char *cptr;

	pr_info("Full dump of TW68 registers:\n");
	/* First we do the PCI regs, 8 4-byte regs per line */
	for (i = 0; i < 0x100; i += 32) {
		cptr = line;
		cptr += sprintf(cptr, "%03x  ", i);
		/* j steps through the next 4 words */
		for (j = i; j < i + 16; j += 4)
			cptr += sprintf(cptr, "%08x ", tw_readl(j));
		*cptr++ = ' ';
		for (; j < i + 32; j += 4)
			cptr += sprintf(cptr, "%08x ", tw_readl(j));
		*cptr++ = '\n';
		*cptr = 0;
		pr_info("%s", line);
	}
	/* Next the control regs, which are single-byte, address mod 4 */
	while (i < 0x400) {
		cptr = line;
		cptr += sprintf(cptr, "%03x ", i);
		/* Print out 4 groups of 4 bytes */
		for (j = 0; j < 4; j++) {
			for (k = 0; k < 4; k++) {
				cptr += sprintf(cptr, "%02x ",
					tw_readb(i));
				i += 4;
			}
			*cptr++ = ' ';
		}
		*cptr++ = '\n';
		*cptr = 0;
		pr_info("%s", line);
	}
}

static int vidioc_log_status(struct file *file, void *priv)
{
	struct tw68_dev *dev = video_drvdata(file);

	tw68_dump_regs(dev);
	return v4l2_ctrl_log_status(file, priv);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vidioc_g_register(struct file *file, void *priv,
			      struct v4l2_dbg_register *reg)
{
	struct tw68_dev *dev = video_drvdata(file);

	if (reg->size == 1)
		reg->val = tw_readb(reg->reg);
	else
		reg->val = tw_readl(reg->reg);
	return 0;
}

static int vidioc_s_register(struct file *file, void *priv,
				const struct v4l2_dbg_register *reg)
{
	struct tw68_dev *dev = video_drvdata(file);

	if (reg->size == 1)
		tw_writeb(reg->reg, reg->val);
	else
		tw_writel(reg->reg & 0xffff, reg->val);
	return 0;
}
#endif

static const struct v4l2_ctrl_ops tw68_ctrl_ops = {
	.s_ctrl = tw68_s_ctrl,
};

static const struct v4l2_file_operations video_fops = {
	.owner			= THIS_MODULE,
	.open			= v4l2_fh_open,
	.release		= vb2_fop_release,
	.read			= vb2_fop_read,
	.poll			= vb2_fop_poll,
	.mmap			= vb2_fop_mmap,
	.unlocked_ioctl		= video_ioctl2,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap		= tw68_querycap,
	.vidioc_enum_fmt_vid_cap	= tw68_enum_fmt_vid_cap,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_s_std			= tw68_s_std,
	.vidioc_g_std			= tw68_g_std,
	.vidioc_enum_input		= tw68_enum_input,
	.vidioc_g_input			= tw68_g_input,
	.vidioc_s_input			= tw68_s_input,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	.vidioc_g_fmt_vid_cap		= tw68_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= tw68_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= tw68_s_fmt_vid_cap,
	.vidioc_log_status		= vidioc_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register              = vidioc_g_register,
	.vidioc_s_register              = vidioc_s_register,
#endif
};

static struct video_device tw68_video_template = {
	.name			= "tw68_video",
	.fops			= &video_fops,
	.ioctl_ops		= &video_ioctl_ops,
	.release		= video_device_release_empty,
	.tvnorms		= TW68_NORMS,
};

/* ------------------------------------------------------------------ */
/* exported stuff                                                     */
void tw68_set_tvnorm_hw(struct tw68_dev *dev)
{
	tw_andorb(TW68_SDT, 0x07, dev->tvnorm->format);
}

int tw68_video_init1(struct tw68_dev *dev)
{
	struct v4l2_ctrl_handler *hdl = &dev->hdl;

	v4l2_ctrl_handler_init(hdl, 6);
	v4l2_ctrl_new_std(hdl, &tw68_ctrl_ops,
			V4L2_CID_BRIGHTNESS, -128, 127, 1, 20);
	v4l2_ctrl_new_std(hdl, &tw68_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 255, 1, 100);
	v4l2_ctrl_new_std(hdl, &tw68_ctrl_ops,
			V4L2_CID_SATURATION, 0, 255, 1, 128);
	/* NTSC only */
	v4l2_ctrl_new_std(hdl, &tw68_ctrl_ops,
			V4L2_CID_HUE, -128, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, &tw68_ctrl_ops,
			V4L2_CID_COLOR_KILLER, 0, 1, 1, 0);
	v4l2_ctrl_new_std(hdl, &tw68_ctrl_ops,
			V4L2_CID_CHROMA_AGC, 0, 1, 1, 1);
	if (hdl->error) {
		v4l2_ctrl_handler_free(hdl);
		return hdl->error;
	}
	dev->v4l2_dev.ctrl_handler = hdl;
	v4l2_ctrl_handler_setup(hdl);
	return 0;
}

int tw68_video_init2(struct tw68_dev *dev, int video_nr)
{
	int ret;

	set_tvnorm(dev, &tvnorms[0]);

	dev->fmt      = format_by_fourcc(V4L2_PIX_FMT_BGR24);
	dev->width    = 720;
	dev->height   = 576;
	dev->field    = V4L2_FIELD_INTERLACED;

	INIT_LIST_HEAD(&dev->active);
	dev->vidq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dev->vidq.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	dev->vidq.io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ | VB2_DMABUF;
	dev->vidq.ops = &tw68_video_qops;
	dev->vidq.mem_ops = &vb2_dma_sg_memops;
	dev->vidq.drv_priv = dev;
	dev->vidq.gfp_flags = __GFP_DMA32 | __GFP_KSWAPD_RECLAIM;
	dev->vidq.buf_struct_size = sizeof(struct tw68_buf);
	dev->vidq.lock = &dev->lock;
	dev->vidq.min_buffers_needed = 2;
	dev->vidq.dev = &dev->pci->dev;
	ret = vb2_queue_init(&dev->vidq);
	if (ret)
		return ret;
	dev->vdev = tw68_video_template;
	dev->vdev.v4l2_dev = &dev->v4l2_dev;
	dev->vdev.lock = &dev->lock;
	dev->vdev.queue = &dev->vidq;
	video_set_drvdata(&dev->vdev, dev);
	return video_register_device(&dev->vdev, VFL_TYPE_GRABBER, video_nr);
}

/*
 * tw68_irq_video_done
 */
void tw68_irq_video_done(struct tw68_dev *dev, unsigned long status)
{
	__u32 reg;

	/* reset interrupts handled by this routine */
	tw_writel(TW68_INTSTAT, status);
	/*
	 * Check most likely first
	 *
	 * DMAPI shows we have reached the end of the risc code
	 * for the current buffer.
	 */
	if (status & TW68_DMAPI) {
		struct tw68_buf *buf;

		spin_lock(&dev->slock);
		buf = list_entry(dev->active.next, struct tw68_buf, list);
		list_del(&buf->list);
		spin_unlock(&dev->slock);
		buf->vb.vb2_buf.timestamp = ktime_get_ns();
		buf->vb.field = dev->field;
		buf->vb.sequence = dev->seqnr++;
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		status &= ~(TW68_DMAPI);
		if (0 == status)
			return;
	}
	if (status & (TW68_VLOCK | TW68_HLOCK))
		dev_dbg(&dev->pci->dev, "Lost sync\n");
	if (status & TW68_PABORT)
		dev_err(&dev->pci->dev, "PABORT interrupt\n");
	if (status & TW68_DMAPERR)
		dev_err(&dev->pci->dev, "DMAPERR interrupt\n");
	/*
	 * On TW6800, FDMIS is apparently generated if video input is switched
	 * during operation.  Therefore, it is not enabled for that chip.
	 */
	if (status & TW68_FDMIS)
		dev_dbg(&dev->pci->dev, "FDMIS interrupt\n");
	if (status & TW68_FFOF) {
		/* probably a logic error */
		reg = tw_readl(TW68_DMAC) & TW68_FIFO_EN;
		tw_clearl(TW68_DMAC, TW68_FIFO_EN);
		dev_dbg(&dev->pci->dev, "FFOF interrupt\n");
		tw_setl(TW68_DMAC, reg);
	}
	if (status & TW68_FFERR)
		dev_dbg(&dev->pci->dev, "FFERR interrupt\n");
}

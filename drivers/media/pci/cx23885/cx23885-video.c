// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for the Conexant CX23885 PCIe bridge
 *
 *  Copyright (c) 2007 Steven Toth <stoth@linuxtv.org>
 */

#include "cx23885.h"
#include "cx23885-video.h"

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <asm/div64.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include "cx23885-ioctl.h"
#include "xc2028.h"

#include <media/drv-intf/cx25840.h>

MODULE_DESCRIPTION("v4l2 driver module for cx23885 based TV cards");
MODULE_AUTHOR("Steven Toth <stoth@linuxtv.org>");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------------ */

static unsigned int video_nr[] = {[0 ... (CX23885_MAXBOARDS - 1)] = UNSET };
static unsigned int vbi_nr[]   = {[0 ... (CX23885_MAXBOARDS - 1)] = UNSET };

module_param_array(video_nr, int, NULL, 0444);
module_param_array(vbi_nr,   int, NULL, 0444);

MODULE_PARM_DESC(video_nr, "video device numbers");
MODULE_PARM_DESC(vbi_nr, "vbi device numbers");

static unsigned int video_debug;
module_param(video_debug, int, 0644);
MODULE_PARM_DESC(video_debug, "enable debug messages [video]");

static unsigned int irq_debug;
module_param(irq_debug, int, 0644);
MODULE_PARM_DESC(irq_debug, "enable debug messages [IRQ handler]");

static unsigned int vid_limit = 16;
module_param(vid_limit, int, 0644);
MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

#define dprintk(level, fmt, arg...)\
	do { if (video_debug >= level)\
		printk(KERN_DEBUG pr_fmt("%s: video:" fmt), \
			__func__, ##arg); \
	} while (0)

/* ------------------------------------------------------------------- */
/* static data                                                         */

#define FORMAT_FLAGS_PACKED       0x01
static struct cx23885_fmt formats[] = {
	{
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	}
};

static struct cx23885_fmt *format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (formats[i].fourcc == fourcc)
			return formats+i;
	return NULL;
}

/* ------------------------------------------------------------------- */

void cx23885_video_wakeup(struct cx23885_dev *dev,
	struct cx23885_dmaqueue *q, u32 count)
{
	struct cx23885_buffer *buf;

	if (list_empty(&q->active))
		return;
	buf = list_entry(q->active.next,
			struct cx23885_buffer, queue);

	buf->vb.sequence = q->count++;
	buf->vb.vb2_buf.timestamp = ktime_get_ns();
	dprintk(2, "[%p/%d] wakeup reg=%d buf=%d\n", buf,
			buf->vb.vb2_buf.index, count, q->count);
	list_del(&buf->queue);
	vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

int cx23885_set_tvnorm(struct cx23885_dev *dev, v4l2_std_id norm)
{
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.format.code = MEDIA_BUS_FMT_FIXED,
	};

	dprintk(1, "%s(norm = 0x%08x) name: [%s]\n",
		__func__,
		(unsigned int)norm,
		v4l2_norm_to_name(norm));

	if (dev->tvnorm == norm)
		return 0;

	if (dev->tvnorm != norm) {
		if (vb2_is_busy(&dev->vb2_vidq) || vb2_is_busy(&dev->vb2_vbiq) ||
		    vb2_is_busy(&dev->vb2_mpegq))
			return -EBUSY;
	}

	dev->tvnorm = norm;
	dev->width = 720;
	dev->height = norm_maxh(norm);
	dev->field = V4L2_FIELD_INTERLACED;

	call_all(dev, video, s_std, norm);

	format.format.width = dev->width;
	format.format.height = dev->height;
	format.format.field = dev->field;
	call_all(dev, pad, set_fmt, NULL, &format);

	return 0;
}

static struct video_device *cx23885_vdev_init(struct cx23885_dev *dev,
				    struct pci_dev *pci,
				    struct video_device *template,
				    char *type)
{
	struct video_device *vfd;
	dprintk(1, "%s()\n", __func__);

	vfd = video_device_alloc();
	if (NULL == vfd)
		return NULL;
	*vfd = *template;
	vfd->v4l2_dev = &dev->v4l2_dev;
	vfd->release = video_device_release;
	vfd->lock = &dev->lock;
	snprintf(vfd->name, sizeof(vfd->name), "%s (%s)",
		 cx23885_boards[dev->board].name, type);
	video_set_drvdata(vfd, dev);
	return vfd;
}

int cx23885_flatiron_write(struct cx23885_dev *dev, u8 reg, u8 data)
{
	/* 8 bit registers, 8 bit values */
	u8 buf[] = { reg, data };

	struct i2c_msg msg = { .addr = 0x98 >> 1,
		.flags = 0, .buf = buf, .len = 2 };

	return i2c_transfer(&dev->i2c_bus[2].i2c_adap, &msg, 1);
}

u8 cx23885_flatiron_read(struct cx23885_dev *dev, u8 reg)
{
	/* 8 bit registers, 8 bit values */
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };

	struct i2c_msg msg[] = {
		{ .addr = 0x98 >> 1, .flags = 0, .buf = b0, .len = 1 },
		{ .addr = 0x98 >> 1, .flags = I2C_M_RD, .buf = b1, .len = 1 }
	};

	ret = i2c_transfer(&dev->i2c_bus[2].i2c_adap, &msg[0], 2);
	if (ret != 2)
		pr_err("%s() error\n", __func__);

	return b1[0];
}

static void cx23885_flatiron_dump(struct cx23885_dev *dev)
{
	int i;
	dprintk(1, "Flatiron dump\n");
	for (i = 0; i < 0x24; i++) {
		dprintk(1, "FI[%02x] = %02x\n", i,
			cx23885_flatiron_read(dev, i));
	}
}

static int cx23885_flatiron_mux(struct cx23885_dev *dev, int input)
{
	u8 val;
	dprintk(1, "%s(input = %d)\n", __func__, input);

	if (input == 1)
		val = cx23885_flatiron_read(dev, CH_PWR_CTRL1) & ~FLD_CH_SEL;
	else if (input == 2)
		val = cx23885_flatiron_read(dev, CH_PWR_CTRL1) | FLD_CH_SEL;
	else
		return -EINVAL;

	val |= 0x20; /* Enable clock to delta-sigma and dec filter */

	cx23885_flatiron_write(dev, CH_PWR_CTRL1, val);

	/* Wake up */
	cx23885_flatiron_write(dev, CH_PWR_CTRL2, 0);

	if (video_debug)
		cx23885_flatiron_dump(dev);

	return 0;
}

static int cx23885_video_mux(struct cx23885_dev *dev, unsigned int input)
{
	dprintk(1, "%s() video_mux: %d [vmux=%d, gpio=0x%x,0x%x,0x%x,0x%x]\n",
		__func__,
		input, INPUT(input)->vmux,
		INPUT(input)->gpio0, INPUT(input)->gpio1,
		INPUT(input)->gpio2, INPUT(input)->gpio3);
	dev->input = input;

	if (dev->board == CX23885_BOARD_MYGICA_X8506 ||
		dev->board == CX23885_BOARD_MAGICPRO_PROHDTVE2 ||
		dev->board == CX23885_BOARD_MYGICA_X8507) {
		/* Select Analog TV */
		if (INPUT(input)->type == CX23885_VMUX_TELEVISION)
			cx23885_gpio_clear(dev, GPIO_0);
	}

	/* Tell the internal A/V decoder */
	v4l2_subdev_call(dev->sd_cx25840, video, s_routing,
			INPUT(input)->vmux, 0, 0);

	if ((dev->board == CX23885_BOARD_HAUPPAUGE_HVR1800) ||
		(dev->board == CX23885_BOARD_MPX885) ||
		(dev->board == CX23885_BOARD_HAUPPAUGE_HVR1250) ||
		(dev->board == CX23885_BOARD_HAUPPAUGE_IMPACTVCBE) ||
		(dev->board == CX23885_BOARD_HAUPPAUGE_HVR1255) ||
		(dev->board == CX23885_BOARD_HAUPPAUGE_HVR1255_22111) ||
		(dev->board == CX23885_BOARD_HAUPPAUGE_HVR1265_K4) ||
		(dev->board == CX23885_BOARD_HAUPPAUGE_QUADHD_ATSC) ||
		(dev->board == CX23885_BOARD_HAUPPAUGE_QUADHD_DVB) ||
		(dev->board == CX23885_BOARD_HAUPPAUGE_HVR1850) ||
		(dev->board == CX23885_BOARD_HAUPPAUGE_HVR5525) ||
		(dev->board == CX23885_BOARD_MYGICA_X8507) ||
		(dev->board == CX23885_BOARD_AVERMEDIA_HC81R) ||
		(dev->board == CX23885_BOARD_VIEWCAST_260E) ||
		(dev->board == CX23885_BOARD_VIEWCAST_460E) ||
		(dev->board == CX23885_BOARD_AVERMEDIA_CE310B)) {
		/* Configure audio routing */
		v4l2_subdev_call(dev->sd_cx25840, audio, s_routing,
			INPUT(input)->amux, 0, 0);

		if (INPUT(input)->amux == CX25840_AUDIO7)
			cx23885_flatiron_mux(dev, 1);
		else if (INPUT(input)->amux == CX25840_AUDIO6)
			cx23885_flatiron_mux(dev, 2);
	}

	return 0;
}

static int cx23885_audio_mux(struct cx23885_dev *dev, unsigned int input)
{
	dprintk(1, "%s(input=%d)\n", __func__, input);

	/* The baseband video core of the cx23885 has two audio inputs.
	 * LR1 and LR2. In almost every single case so far only HVR1xxx
	 * cards we've only ever supported LR1. Time to support LR2,
	 * which is available via the optional white breakout header on
	 * the board.
	 * We'll use a could of existing enums in the card struct to allow
	 * devs to specify which baseband input they need, or just default
	 * to what we've always used.
	 */
	if (INPUT(input)->amux == CX25840_AUDIO7)
		cx23885_flatiron_mux(dev, 1);
	else if (INPUT(input)->amux == CX25840_AUDIO6)
		cx23885_flatiron_mux(dev, 2);
	else {
		/* Not specifically defined, assume the default. */
		cx23885_flatiron_mux(dev, 1);
	}

	return 0;
}

/* ------------------------------------------------------------------ */
static int cx23885_start_video_dma(struct cx23885_dev *dev,
			   struct cx23885_dmaqueue *q,
			   struct cx23885_buffer *buf)
{
	dprintk(1, "%s()\n", __func__);

	/* Stop the dma/fifo before we tamper with it's risc programs */
	cx_clear(VID_A_DMA_CTL, 0x11);

	/* setup fifo + format */
	cx23885_sram_channel_setup(dev, &dev->sram_channels[SRAM_CH01],
				buf->bpl, buf->risc.dma);

	/* reset counter */
	cx_write(VID_A_GPCNT_CTL, 3);
	q->count = 0;

	/* enable irq */
	cx23885_irq_add_enable(dev, 0x01);
	cx_set(VID_A_INT_MSK, 0x000011);

	/* start dma */
	cx_set(DEV_CNTRL2, (1<<5));
	cx_set(VID_A_DMA_CTL, 0x11); /* FIFO and RISC enable */

	return 0;
}

static int queue_setup(struct vb2_queue *q,
			   unsigned int *num_buffers, unsigned int *num_planes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct cx23885_dev *dev = q->drv_priv;

	*num_planes = 1;
	sizes[0] = (dev->fmt->depth * dev->width * dev->height) >> 3;
	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	int ret;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cx23885_dev *dev = vb->vb2_queue->drv_priv;
	struct cx23885_buffer *buf =
		container_of(vbuf, struct cx23885_buffer, vb);
	u32 line0_offset, line1_offset;
	struct sg_table *sgt = vb2_dma_sg_plane_desc(vb, 0);
	int field_tff;

	buf->bpl = (dev->width * dev->fmt->depth) >> 3;

	if (vb2_plane_size(vb, 0) < dev->height * buf->bpl)
		return -EINVAL;
	vb2_set_plane_payload(vb, 0, dev->height * buf->bpl);

	switch (dev->field) {
	case V4L2_FIELD_TOP:
		ret = cx23885_risc_buffer(dev->pci, &buf->risc,
				sgt->sgl, 0, UNSET,
				buf->bpl, 0, dev->height);
		break;
	case V4L2_FIELD_BOTTOM:
		ret = cx23885_risc_buffer(dev->pci, &buf->risc,
				sgt->sgl, UNSET, 0,
				buf->bpl, 0, dev->height);
		break;
	case V4L2_FIELD_INTERLACED:
		if (dev->tvnorm & V4L2_STD_525_60)
			/* NTSC or  */
			field_tff = 1;
		else
			field_tff = 0;

		if (cx23885_boards[dev->board].force_bff)
			/* PAL / SECAM OR 888 in NTSC MODE */
			field_tff = 0;

		if (field_tff) {
			/* cx25840 transmits NTSC bottom field first */
			dprintk(1, "%s() Creating TFF/NTSC risc\n",
					__func__);
			line0_offset = buf->bpl;
			line1_offset = 0;
		} else {
			/* All other formats are top field first */
			dprintk(1, "%s() Creating BFF/PAL/SECAM risc\n",
					__func__);
			line0_offset = 0;
			line1_offset = buf->bpl;
		}
		ret = cx23885_risc_buffer(dev->pci, &buf->risc,
				sgt->sgl, line0_offset,
				line1_offset,
				buf->bpl, buf->bpl,
				dev->height >> 1);
		break;
	case V4L2_FIELD_SEQ_TB:
		ret = cx23885_risc_buffer(dev->pci, &buf->risc,
				sgt->sgl,
				0, buf->bpl * (dev->height >> 1),
				buf->bpl, 0,
				dev->height >> 1);
		break;
	case V4L2_FIELD_SEQ_BT:
		ret = cx23885_risc_buffer(dev->pci, &buf->risc,
				sgt->sgl,
				buf->bpl * (dev->height >> 1), 0,
				buf->bpl, 0,
				dev->height >> 1);
		break;
	default:
		return -EINVAL; /* should not happen */
	}
	dprintk(2, "[%p/%d] buffer_init - %dx%d %dbpp 0x%08x - dma=0x%08lx\n",
		buf, buf->vb.vb2_buf.index,
		dev->width, dev->height, dev->fmt->depth, dev->fmt->fourcc,
		(unsigned long)buf->risc.dma);
	return ret;
}

static void buffer_finish(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cx23885_buffer *buf = container_of(vbuf,
		struct cx23885_buffer, vb);

	cx23885_free_buffer(vb->vb2_queue->drv_priv, buf);
}

/*
 * The risc program for each buffer works as follows: it starts with a simple
 * 'JUMP to addr + 12', which is effectively a NOP. Then the code to DMA the
 * buffer follows and at the end we have a JUMP back to the start + 12 (skipping
 * the initial JUMP).
 *
 * This is the risc program of the first buffer to be queued if the active list
 * is empty and it just keeps DMAing this buffer without generating any
 * interrupts.
 *
 * If a new buffer is added then the initial JUMP in the code for that buffer
 * will generate an interrupt which signals that the previous buffer has been
 * DMAed successfully and that it can be returned to userspace.
 *
 * It also sets the final jump of the previous buffer to the start of the new
 * buffer, thus chaining the new buffer into the DMA chain. This is a single
 * atomic u32 write, so there is no race condition.
 *
 * The end-result of all this that you only get an interrupt when a buffer
 * is ready, so the control flow is very easy.
 */
static void buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cx23885_dev *dev = vb->vb2_queue->drv_priv;
	struct cx23885_buffer   *buf = container_of(vbuf,
		struct cx23885_buffer, vb);
	struct cx23885_buffer   *prev;
	struct cx23885_dmaqueue *q    = &dev->vidq;
	unsigned long flags;

	/* add jump to start */
	buf->risc.cpu[1] = cpu_to_le32(buf->risc.dma + 12);
	buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_CNT_INC);
	buf->risc.jmp[1] = cpu_to_le32(buf->risc.dma + 12);
	buf->risc.jmp[2] = cpu_to_le32(0); /* bits 63-32 */

	spin_lock_irqsave(&dev->slock, flags);
	if (list_empty(&q->active)) {
		list_add_tail(&buf->queue, &q->active);
		dprintk(2, "[%p/%d] buffer_queue - first active\n",
			buf, buf->vb.vb2_buf.index);
	} else {
		buf->risc.cpu[0] |= cpu_to_le32(RISC_IRQ1);
		prev = list_entry(q->active.prev, struct cx23885_buffer,
			queue);
		list_add_tail(&buf->queue, &q->active);
		prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
		dprintk(2, "[%p/%d] buffer_queue - append to active\n",
				buf, buf->vb.vb2_buf.index);
	}
	spin_unlock_irqrestore(&dev->slock, flags);
}

static int cx23885_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct cx23885_dev *dev = q->drv_priv;
	struct cx23885_dmaqueue *dmaq = &dev->vidq;
	struct cx23885_buffer *buf = list_entry(dmaq->active.next,
			struct cx23885_buffer, queue);

	cx23885_start_video_dma(dev, dmaq, buf);
	return 0;
}

static void cx23885_stop_streaming(struct vb2_queue *q)
{
	struct cx23885_dev *dev = q->drv_priv;
	struct cx23885_dmaqueue *dmaq = &dev->vidq;
	unsigned long flags;

	cx_clear(VID_A_DMA_CTL, 0x11);
	spin_lock_irqsave(&dev->slock, flags);
	while (!list_empty(&dmaq->active)) {
		struct cx23885_buffer *buf = list_entry(dmaq->active.next,
			struct cx23885_buffer, queue);

		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&dev->slock, flags);
}

static const struct vb2_ops cx23885_video_qops = {
	.queue_setup    = queue_setup,
	.buf_prepare  = buffer_prepare,
	.buf_finish = buffer_finish,
	.buf_queue    = buffer_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = cx23885_start_streaming,
	.stop_streaming = cx23885_stop_streaming,
};

/* ------------------------------------------------------------------ */
/* VIDEO IOCTLS                                                       */

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
	struct v4l2_format *f)
{
	struct cx23885_dev *dev = video_drvdata(file);

	f->fmt.pix.width        = dev->width;
	f->fmt.pix.height       = dev->height;
	f->fmt.pix.field        = dev->field;
	f->fmt.pix.pixelformat  = dev->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * dev->fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace   = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
	struct v4l2_format *f)
{
	struct cx23885_dev *dev = video_drvdata(file);
	struct cx23885_fmt *fmt;
	enum v4l2_field   field;
	unsigned int      maxw, maxh;

	fmt = format_by_fourcc(f->fmt.pix.pixelformat);
	if (NULL == fmt)
		return -EINVAL;

	field = f->fmt.pix.field;
	maxw  = 720;
	maxh  = norm_maxh(dev->tvnorm);

	if (V4L2_FIELD_ANY == field) {
		field = (f->fmt.pix.height > maxh/2)
			? V4L2_FIELD_INTERLACED
			: V4L2_FIELD_BOTTOM;
	}

	switch (field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
		maxh = maxh / 2;
		break;
	case V4L2_FIELD_INTERLACED:
	case V4L2_FIELD_SEQ_TB:
	case V4L2_FIELD_SEQ_BT:
		break;
	default:
		field = V4L2_FIELD_INTERLACED;
		break;
	}

	f->fmt.pix.field = field;
	v4l_bound_align_image(&f->fmt.pix.width, 48, maxw, 2,
			      &f->fmt.pix.height, 32, maxh, 0, 0);
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
	struct v4l2_format *f)
{
	struct cx23885_dev *dev = video_drvdata(file);
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int err;

	dprintk(2, "%s()\n", __func__);
	err = vidioc_try_fmt_vid_cap(file, priv, f);

	if (0 != err)
		return err;

	if (vb2_is_busy(&dev->vb2_vidq) || vb2_is_busy(&dev->vb2_vbiq) ||
	    vb2_is_busy(&dev->vb2_mpegq))
		return -EBUSY;

	dev->fmt        = format_by_fourcc(f->fmt.pix.pixelformat);
	dev->width      = f->fmt.pix.width;
	dev->height     = f->fmt.pix.height;
	dev->field	= f->fmt.pix.field;
	dprintk(2, "%s() width=%d height=%d field=%d\n", __func__,
		dev->width, dev->height, dev->field);
	v4l2_fill_mbus_format(&format.format, &f->fmt.pix, MEDIA_BUS_FMT_FIXED);
	call_all(dev, pad, set_fmt, NULL, &format);
	v4l2_fill_pix_format(&f->fmt.pix, &format.format);
	/* set_fmt overwrites f->fmt.pix.field, restore it */
	f->fmt.pix.field = dev->field;
	return 0;
}

static int vidioc_querycap(struct file *file, void  *priv,
	struct v4l2_capability *cap)
{
	struct cx23885_dev *dev = video_drvdata(file);

	strscpy(cap->driver, "cx23885", sizeof(cap->driver));
	strscpy(cap->card, cx23885_boards[dev->board].name,
		sizeof(cap->card));
	sprintf(cap->bus_info, "PCIe:%s", pci_name(dev->pci));
	cap->capabilities = V4L2_CAP_READWRITE | V4L2_CAP_STREAMING |
			    V4L2_CAP_AUDIO | V4L2_CAP_VBI_CAPTURE |
			    V4L2_CAP_VIDEO_CAPTURE |
			    V4L2_CAP_DEVICE_CAPS;
	switch (dev->board) { /* i2c device tuners */
	case CX23885_BOARD_HAUPPAUGE_HVR1265_K4:
	case CX23885_BOARD_HAUPPAUGE_HVR5525:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_DVB:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_ATSC:
		cap->capabilities |= V4L2_CAP_TUNER;
		break;
	default:
		if (dev->tuner_type != TUNER_ABSENT)
			cap->capabilities |= V4L2_CAP_TUNER;
		break;
	}
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
	struct v4l2_fmtdesc *f)
{
	if (unlikely(f->index >= ARRAY_SIZE(formats)))
		return -EINVAL;

	f->pixelformat = formats[f->index].fourcc;

	return 0;
}

static int vidioc_g_pixelaspect(struct file *file, void *priv,
				int type, struct v4l2_fract *f)
{
	struct cx23885_dev *dev = video_drvdata(file);
	bool is_50hz = dev->tvnorm & V4L2_STD_625_50;

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	f->numerator = is_50hz ? 54 : 11;
	f->denominator = is_50hz ? 59 : 10;

	return 0;
}

static int vidioc_g_selection(struct file *file, void *fh,
			      struct v4l2_selection *sel)
{
	struct cx23885_dev *dev = video_drvdata(file);

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = 720;
		sel->r.height = norm_maxh(dev->tvnorm);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *id)
{
	struct cx23885_dev *dev = video_drvdata(file);
	dprintk(1, "%s()\n", __func__);

	*id = dev->tvnorm;
	return 0;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id tvnorms)
{
	struct cx23885_dev *dev = video_drvdata(file);
	dprintk(1, "%s()\n", __func__);

	return cx23885_set_tvnorm(dev, tvnorms);
}

int cx23885_enum_input(struct cx23885_dev *dev, struct v4l2_input *i)
{
	static const char *iname[] = {
		[CX23885_VMUX_COMPOSITE1] = "Composite1",
		[CX23885_VMUX_COMPOSITE2] = "Composite2",
		[CX23885_VMUX_COMPOSITE3] = "Composite3",
		[CX23885_VMUX_COMPOSITE4] = "Composite4",
		[CX23885_VMUX_SVIDEO]     = "S-Video",
		[CX23885_VMUX_COMPONENT]  = "Component",
		[CX23885_VMUX_TELEVISION] = "Television",
		[CX23885_VMUX_CABLE]      = "Cable TV",
		[CX23885_VMUX_DVB]        = "DVB",
		[CX23885_VMUX_DEBUG]      = "for debug only",
	};
	unsigned int n;
	dprintk(1, "%s()\n", __func__);

	n = i->index;
	if (n >= MAX_CX23885_INPUT)
		return -EINVAL;

	if (0 == INPUT(n)->type)
		return -EINVAL;

	i->index = n;
	i->type  = V4L2_INPUT_TYPE_CAMERA;
	strscpy(i->name, iname[INPUT(n)->type], sizeof(i->name));
	i->std = CX23885_NORMS;
	if ((CX23885_VMUX_TELEVISION == INPUT(n)->type) ||
		(CX23885_VMUX_CABLE == INPUT(n)->type)) {
		i->type = V4L2_INPUT_TYPE_TUNER;
		i->audioset = 4;
	} else {
		/* Two selectable audio inputs for non-tv inputs */
		i->audioset = 3;
	}

	if (dev->input == n) {
		/* enum'd input matches our configured input.
		 * Ask the video decoder to process the call
		 * and give it an oppertunity to update the
		 * status field.
		 */
		call_all(dev, video, g_input_status, &i->status);
	}

	return 0;
}

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *i)
{
	struct cx23885_dev *dev = video_drvdata(file);
	dprintk(1, "%s()\n", __func__);
	return cx23885_enum_input(dev, i);
}

int cx23885_get_input(struct file *file, void *priv, unsigned int *i)
{
	struct cx23885_dev *dev = video_drvdata(file);

	*i = dev->input;
	dprintk(1, "%s() returns %d\n", __func__, *i);
	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	return cx23885_get_input(file, priv, i);
}

int cx23885_set_input(struct file *file, void *priv, unsigned int i)
{
	struct cx23885_dev *dev = video_drvdata(file);

	dprintk(1, "%s(%d)\n", __func__, i);

	if (i >= MAX_CX23885_INPUT) {
		dprintk(1, "%s() -EINVAL\n", __func__);
		return -EINVAL;
	}

	if (INPUT(i)->type == 0)
		return -EINVAL;

	cx23885_video_mux(dev, i);

	/* By default establish the default audio input for the card also */
	/* Caller is free to use VIDIOC_S_AUDIO to override afterwards */
	cx23885_audio_mux(dev, i);
	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	return cx23885_set_input(file, priv, i);
}

static int vidioc_log_status(struct file *file, void *priv)
{
	struct cx23885_dev *dev = video_drvdata(file);

	call_all(dev, core, log_status);
	return 0;
}

static int cx23885_query_audinput(struct file *file, void *priv,
	struct v4l2_audio *i)
{
	static const char *iname[] = {
		[0] = "Baseband L/R 1",
		[1] = "Baseband L/R 2",
		[2] = "TV",
	};
	unsigned int n;
	dprintk(1, "%s()\n", __func__);

	n = i->index;
	if (n >= 3)
		return -EINVAL;

	memset(i, 0, sizeof(*i));
	i->index = n;
	strscpy(i->name, iname[n], sizeof(i->name));
	i->capability = V4L2_AUDCAP_STEREO;
	return 0;

}

static int vidioc_enum_audinput(struct file *file, void *priv,
				struct v4l2_audio *i)
{
	return cx23885_query_audinput(file, priv, i);
}

static int vidioc_g_audinput(struct file *file, void *priv,
	struct v4l2_audio *i)
{
	struct cx23885_dev *dev = video_drvdata(file);

	if ((CX23885_VMUX_TELEVISION == INPUT(dev->input)->type) ||
		(CX23885_VMUX_CABLE == INPUT(dev->input)->type))
		i->index = 2;
	else
		i->index = dev->audinput;
	dprintk(1, "%s(input=%d)\n", __func__, i->index);

	return cx23885_query_audinput(file, priv, i);
}

static int vidioc_s_audinput(struct file *file, void *priv,
	const struct v4l2_audio *i)
{
	struct cx23885_dev *dev = video_drvdata(file);

	if ((CX23885_VMUX_TELEVISION == INPUT(dev->input)->type) ||
		(CX23885_VMUX_CABLE == INPUT(dev->input)->type)) {
		return i->index != 2 ? -EINVAL : 0;
	}
	if (i->index > 1)
		return -EINVAL;

	dprintk(1, "%s(%d)\n", __func__, i->index);

	dev->audinput = i->index;

	/* Skip the audio defaults from the cards struct, caller wants
	 * directly touch the audio mux hardware. */
	cx23885_flatiron_mux(dev, dev->audinput + 1);
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct cx23885_dev *dev = video_drvdata(file);

	switch (dev->board) { /* i2c device tuners */
	case CX23885_BOARD_HAUPPAUGE_HVR1265_K4:
	case CX23885_BOARD_HAUPPAUGE_HVR5525:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_DVB:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_ATSC:
		break;
	default:
		if (dev->tuner_type == TUNER_ABSENT)
			return -EINVAL;
		break;
	}
	if (0 != t->index)
		return -EINVAL;

	strscpy(t->name, "Television", sizeof(t->name));

	call_all(dev, tuner, g_tuner, t);
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
				const struct v4l2_tuner *t)
{
	struct cx23885_dev *dev = video_drvdata(file);

	switch (dev->board) { /* i2c device tuners */
	case CX23885_BOARD_HAUPPAUGE_HVR1265_K4:
	case CX23885_BOARD_HAUPPAUGE_HVR5525:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_DVB:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_ATSC:
		break;
	default:
		if (dev->tuner_type == TUNER_ABSENT)
			return -EINVAL;
		break;
	}
	if (0 != t->index)
		return -EINVAL;
	/* Update the A/V core */
	call_all(dev, tuner, s_tuner, t);

	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct cx23885_dev *dev = video_drvdata(file);

	switch (dev->board) { /* i2c device tuners */
	case CX23885_BOARD_HAUPPAUGE_HVR1265_K4:
	case CX23885_BOARD_HAUPPAUGE_HVR5525:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_DVB:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_ATSC:
		break;
	default:
		if (dev->tuner_type == TUNER_ABSENT)
			return -EINVAL;
		break;
	}
	f->type = V4L2_TUNER_ANALOG_TV;
	f->frequency = dev->freq;

	call_all(dev, tuner, g_frequency, f);

	return 0;
}

static int cx23885_set_freq(struct cx23885_dev *dev, const struct v4l2_frequency *f)
{
	struct v4l2_ctrl *mute;
	int old_mute_val = 1;

	switch (dev->board) { /* i2c device tuners */
	case CX23885_BOARD_HAUPPAUGE_HVR1265_K4:
	case CX23885_BOARD_HAUPPAUGE_HVR5525:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_DVB:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_ATSC:
		break;
	default:
		if (dev->tuner_type == TUNER_ABSENT)
			return -EINVAL;
		break;
	}
	if (unlikely(f->tuner != 0))
		return -EINVAL;

	dev->freq = f->frequency;

	/* I need to mute audio here */
	mute = v4l2_ctrl_find(&dev->ctrl_handler, V4L2_CID_AUDIO_MUTE);
	if (mute) {
		old_mute_val = v4l2_ctrl_g_ctrl(mute);
		if (!old_mute_val)
			v4l2_ctrl_s_ctrl(mute, 1);
	}

	call_all(dev, tuner, s_frequency, f);

	/* When changing channels it is required to reset TVAUDIO */
	msleep(100);

	/* I need to unmute audio here */
	if (old_mute_val == 0)
		v4l2_ctrl_s_ctrl(mute, old_mute_val);

	return 0;
}

static int cx23885_set_freq_via_ops(struct cx23885_dev *dev,
	const struct v4l2_frequency *f)
{
	struct v4l2_ctrl *mute;
	int old_mute_val = 1;
	struct vb2_dvb_frontend *vfe;
	struct dvb_frontend *fe;

	struct analog_parameters params = {
		.mode      = V4L2_TUNER_ANALOG_TV,
		.audmode   = V4L2_TUNER_MODE_STEREO,
		.std       = dev->tvnorm,
		.frequency = f->frequency
	};

	dev->freq = f->frequency;

	/* I need to mute audio here */
	mute = v4l2_ctrl_find(&dev->ctrl_handler, V4L2_CID_AUDIO_MUTE);
	if (mute) {
		old_mute_val = v4l2_ctrl_g_ctrl(mute);
		if (!old_mute_val)
			v4l2_ctrl_s_ctrl(mute, 1);
	}

	/* If HVR1850 */
	dprintk(1, "%s() frequency=%d tuner=%d std=0x%llx\n", __func__,
		params.frequency, f->tuner, params.std);

	vfe = vb2_dvb_get_frontend(&dev->ts2.frontends, 1);
	if (!vfe) {
		return -EINVAL;
	}

	fe = vfe->dvb.frontend;

	if ((dev->board == CX23885_BOARD_HAUPPAUGE_HVR1850) ||
	    (dev->board == CX23885_BOARD_HAUPPAUGE_HVR1255) ||
	    (dev->board == CX23885_BOARD_HAUPPAUGE_HVR1255_22111) ||
	    (dev->board == CX23885_BOARD_HAUPPAUGE_HVR1265_K4) ||
	    (dev->board == CX23885_BOARD_HAUPPAUGE_HVR5525) ||
	    (dev->board == CX23885_BOARD_HAUPPAUGE_QUADHD_DVB) ||
	    (dev->board == CX23885_BOARD_HAUPPAUGE_QUADHD_ATSC))
		fe = &dev->ts1.analog_fe;

	if (fe && fe->ops.tuner_ops.set_analog_params) {
		call_all(dev, video, s_std, dev->tvnorm);
		fe->ops.tuner_ops.set_analog_params(fe, &params);
	}
	else
		pr_err("%s() No analog tuner, aborting\n", __func__);

	/* When changing channels it is required to reset TVAUDIO */
	msleep(100);

	/* I need to unmute audio here */
	if (old_mute_val == 0)
		v4l2_ctrl_s_ctrl(mute, old_mute_val);

	return 0;
}

int cx23885_set_frequency(struct file *file, void *priv,
	const struct v4l2_frequency *f)
{
	struct cx23885_dev *dev = video_drvdata(file);
	int ret;

	switch (dev->board) {
	case CX23885_BOARD_HAUPPAUGE_HVR1255:
	case CX23885_BOARD_HAUPPAUGE_HVR1255_22111:
	case CX23885_BOARD_HAUPPAUGE_HVR1265_K4:
	case CX23885_BOARD_HAUPPAUGE_HVR1850:
	case CX23885_BOARD_HAUPPAUGE_HVR5525:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_DVB:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_ATSC:
		ret = cx23885_set_freq_via_ops(dev, f);
		break;
	default:
		ret = cx23885_set_freq(dev, f);
	}

	return ret;
}

static int vidioc_s_frequency(struct file *file, void *priv,
	const struct v4l2_frequency *f)
{
	return cx23885_set_frequency(file, priv, f);
}

/* ----------------------------------------------------------- */

int cx23885_video_irq(struct cx23885_dev *dev, u32 status)
{
	u32 mask, count;
	int handled = 0;

	mask   = cx_read(VID_A_INT_MSK);
	if (0 == (status & mask))
		return handled;

	cx_write(VID_A_INT_STAT, status);

	/* risc op code error, fifo overflow or line sync detection error */
	if ((status & VID_BC_MSK_OPC_ERR) ||
		(status & VID_BC_MSK_SYNC) ||
		(status & VID_BC_MSK_OF)) {

		if (status & VID_BC_MSK_OPC_ERR) {
			dprintk(7, " (VID_BC_MSK_OPC_ERR 0x%08x)\n",
				VID_BC_MSK_OPC_ERR);
			pr_warn("%s: video risc op code error\n",
				dev->name);
			cx23885_sram_channel_dump(dev,
				&dev->sram_channels[SRAM_CH01]);
		}

		if (status & VID_BC_MSK_SYNC)
			dprintk(7, " (VID_BC_MSK_SYNC 0x%08x) video lines miss-match\n",
				VID_BC_MSK_SYNC);

		if (status & VID_BC_MSK_OF)
			dprintk(7, " (VID_BC_MSK_OF 0x%08x) fifo overflow\n",
				VID_BC_MSK_OF);

	}

	/* Video */
	if (status & VID_BC_MSK_RISCI1) {
		spin_lock(&dev->slock);
		count = cx_read(VID_A_GPCNT);
		cx23885_video_wakeup(dev, &dev->vidq, count);
		spin_unlock(&dev->slock);
		handled++;
	}

	/* Allow the VBI framework to process it's payload */
	handled += cx23885_vbi_irq(dev, status);

	return handled;
}

/* ----------------------------------------------------------- */
/* exported stuff                                              */

static const struct v4l2_file_operations video_fops = {
	.owner	       = THIS_MODULE,
	.open           = v4l2_fh_open,
	.release        = vb2_fop_release,
	.read           = vb2_fop_read,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_g_fmt_vbi_cap     = cx23885_vbi_fmt,
	.vidioc_try_fmt_vbi_cap   = cx23885_vbi_fmt,
	.vidioc_s_fmt_vbi_cap     = cx23885_vbi_fmt,
	.vidioc_reqbufs       = vb2_ioctl_reqbufs,
	.vidioc_prepare_buf   = vb2_ioctl_prepare_buf,
	.vidioc_querybuf      = vb2_ioctl_querybuf,
	.vidioc_qbuf          = vb2_ioctl_qbuf,
	.vidioc_dqbuf         = vb2_ioctl_dqbuf,
	.vidioc_streamon      = vb2_ioctl_streamon,
	.vidioc_streamoff     = vb2_ioctl_streamoff,
	.vidioc_g_pixelaspect = vidioc_g_pixelaspect,
	.vidioc_g_selection   = vidioc_g_selection,
	.vidioc_s_std         = vidioc_s_std,
	.vidioc_g_std         = vidioc_g_std,
	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
	.vidioc_log_status    = vidioc_log_status,
	.vidioc_g_tuner       = vidioc_g_tuner,
	.vidioc_s_tuner       = vidioc_s_tuner,
	.vidioc_g_frequency   = vidioc_g_frequency,
	.vidioc_s_frequency   = vidioc_s_frequency,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_chip_info   = cx23885_g_chip_info,
	.vidioc_g_register    = cx23885_g_register,
	.vidioc_s_register    = cx23885_s_register,
#endif
	.vidioc_enumaudio     = vidioc_enum_audinput,
	.vidioc_g_audio       = vidioc_g_audinput,
	.vidioc_s_audio       = vidioc_s_audinput,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static struct video_device cx23885_vbi_template;
static struct video_device cx23885_video_template = {
	.name                 = "cx23885-video",
	.fops                 = &video_fops,
	.ioctl_ops	      = &video_ioctl_ops,
	.tvnorms              = CX23885_NORMS,
};

void cx23885_video_unregister(struct cx23885_dev *dev)
{
	dprintk(1, "%s()\n", __func__);
	cx23885_irq_remove(dev, 0x01);

	if (dev->vbi_dev) {
		if (video_is_registered(dev->vbi_dev))
			video_unregister_device(dev->vbi_dev);
		else
			video_device_release(dev->vbi_dev);
		dev->vbi_dev = NULL;
	}
	if (dev->video_dev) {
		if (video_is_registered(dev->video_dev))
			video_unregister_device(dev->video_dev);
		else
			video_device_release(dev->video_dev);
		dev->video_dev = NULL;
	}

	if (dev->audio_dev)
		cx23885_audio_unregister(dev);
}

int cx23885_video_register(struct cx23885_dev *dev)
{
	struct vb2_queue *q;
	int err;

	dprintk(1, "%s()\n", __func__);

	/* Initialize VBI template */
	cx23885_vbi_template = cx23885_video_template;
	strscpy(cx23885_vbi_template.name, "cx23885-vbi",
		sizeof(cx23885_vbi_template.name));

	dev->tvnorm = V4L2_STD_NTSC_M;
	dev->fmt = format_by_fourcc(V4L2_PIX_FMT_YUYV);
	dev->field = V4L2_FIELD_INTERLACED;
	dev->width = 720;
	dev->height = norm_maxh(dev->tvnorm);

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);

	/* init vbi dma queues */
	INIT_LIST_HEAD(&dev->vbiq.active);

	cx23885_irq_add_enable(dev, 0x01);

	if ((TUNER_ABSENT != dev->tuner_type) &&
			((dev->tuner_bus == 0) || (dev->tuner_bus == 1))) {
		struct v4l2_subdev *sd = NULL;

		if (dev->tuner_addr)
			sd = v4l2_i2c_new_subdev(&dev->v4l2_dev,
				&dev->i2c_bus[dev->tuner_bus].i2c_adap,
				"tuner", dev->tuner_addr, NULL);
		else
			sd = v4l2_i2c_new_subdev(&dev->v4l2_dev,
				&dev->i2c_bus[dev->tuner_bus].i2c_adap,
				"tuner", 0, v4l2_i2c_tuner_addrs(ADDRS_TV));
		if (sd) {
			struct tuner_setup tun_setup;

			memset(&tun_setup, 0, sizeof(tun_setup));
			tun_setup.mode_mask = T_ANALOG_TV;
			tun_setup.type = dev->tuner_type;
			tun_setup.addr = v4l2_i2c_subdev_addr(sd);
			tun_setup.tuner_callback = cx23885_tuner_callback;

			v4l2_subdev_call(sd, tuner, s_type_addr, &tun_setup);

			if ((dev->board == CX23885_BOARD_LEADTEK_WINFAST_PXTV1200) ||
			    (dev->board == CX23885_BOARD_LEADTEK_WINFAST_PXPVR2200)) {
				struct xc2028_ctrl ctrl = {
					.fname = XC2028_DEFAULT_FIRMWARE,
					.max_len = 64
				};
				struct v4l2_priv_tun_config cfg = {
					.tuner = dev->tuner_type,
					.priv = &ctrl
				};
				v4l2_subdev_call(sd, tuner, s_config, &cfg);
			}

			if (dev->board == CX23885_BOARD_AVERMEDIA_HC81R) {
				struct xc2028_ctrl ctrl = {
					.fname = "xc3028L-v36.fw",
					.max_len = 64
				};
				struct v4l2_priv_tun_config cfg = {
					.tuner = dev->tuner_type,
					.priv = &ctrl
				};
				v4l2_subdev_call(sd, tuner, s_config, &cfg);
			}
		}
	}

	/* initial device configuration */
	mutex_lock(&dev->lock);
	cx23885_set_tvnorm(dev, dev->tvnorm);
	cx23885_video_mux(dev, 0);
	cx23885_audio_mux(dev, 0);
	mutex_unlock(&dev->lock);

	q = &dev->vb2_vidq;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_READ;
	q->gfp_flags = GFP_DMA32;
	q->min_queued_buffers = 2;
	q->drv_priv = dev;
	q->buf_struct_size = sizeof(struct cx23885_buffer);
	q->ops = &cx23885_video_qops;
	q->mem_ops = &vb2_dma_sg_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &dev->lock;
	q->dev = &dev->pci->dev;

	err = vb2_queue_init(q);
	if (err < 0)
		goto fail_unreg;

	q = &dev->vb2_vbiq;
	q->type = V4L2_BUF_TYPE_VBI_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_READ;
	q->gfp_flags = GFP_DMA32;
	q->min_queued_buffers = 2;
	q->drv_priv = dev;
	q->buf_struct_size = sizeof(struct cx23885_buffer);
	q->ops = &cx23885_vbi_qops;
	q->mem_ops = &vb2_dma_sg_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &dev->lock;
	q->dev = &dev->pci->dev;

	err = vb2_queue_init(q);
	if (err < 0)
		goto fail_unreg;

	/* register Video device */
	dev->video_dev = cx23885_vdev_init(dev, dev->pci,
		&cx23885_video_template, "video");
	dev->video_dev->queue = &dev->vb2_vidq;
	dev->video_dev->device_caps = V4L2_CAP_READWRITE | V4L2_CAP_STREAMING |
				      V4L2_CAP_AUDIO | V4L2_CAP_VIDEO_CAPTURE;
	switch (dev->board) { /* i2c device tuners */
	case CX23885_BOARD_HAUPPAUGE_HVR1265_K4:
	case CX23885_BOARD_HAUPPAUGE_HVR5525:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_DVB:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_ATSC:
		dev->video_dev->device_caps |= V4L2_CAP_TUNER;
		break;
	default:
		if (dev->tuner_type != TUNER_ABSENT)
			dev->video_dev->device_caps |= V4L2_CAP_TUNER;
	}

	err = video_register_device(dev->video_dev, VFL_TYPE_VIDEO,
				    video_nr[dev->nr]);
	if (err < 0) {
		pr_info("%s: can't register video device\n",
			dev->name);
		goto fail_unreg;
	}
	pr_info("%s: registered device %s [v4l2]\n",
	       dev->name, video_device_node_name(dev->video_dev));

	/* register VBI device */
	dev->vbi_dev = cx23885_vdev_init(dev, dev->pci,
		&cx23885_vbi_template, "vbi");
	dev->vbi_dev->queue = &dev->vb2_vbiq;
	dev->vbi_dev->device_caps = V4L2_CAP_READWRITE | V4L2_CAP_STREAMING |
				    V4L2_CAP_AUDIO | V4L2_CAP_VBI_CAPTURE;
	switch (dev->board) { /* i2c device tuners */
	case CX23885_BOARD_HAUPPAUGE_HVR1265_K4:
	case CX23885_BOARD_HAUPPAUGE_HVR5525:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_DVB:
	case CX23885_BOARD_HAUPPAUGE_QUADHD_ATSC:
		dev->vbi_dev->device_caps |= V4L2_CAP_TUNER;
		break;
	default:
		if (dev->tuner_type != TUNER_ABSENT)
			dev->vbi_dev->device_caps |= V4L2_CAP_TUNER;
	}
	err = video_register_device(dev->vbi_dev, VFL_TYPE_VBI,
				    vbi_nr[dev->nr]);
	if (err < 0) {
		pr_info("%s: can't register vbi device\n",
			dev->name);
		goto fail_unreg;
	}
	pr_info("%s: registered device %s\n",
	       dev->name, video_device_node_name(dev->vbi_dev));

	/* Register ALSA audio device */
	dev->audio_dev = cx23885_audio_register(dev);

	return 0;

fail_unreg:
	cx23885_video_unregister(dev);
	return err;
}

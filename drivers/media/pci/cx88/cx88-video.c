/*
 *
 * device driver for Conexant 2388x based TV cards
 * video4linux video interface
 *
 * (c) 2003-04 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 * (c) 2005-2006 Mauro Carvalho Chehab <mchehab@infradead.org>
 *	- Multituner support
 *	- video_ioctl2 conversion
 *	- PAL/M fixes
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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <asm/div64.h>

#include "cx88.h"
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/wm8775.h>

MODULE_DESCRIPTION("v4l2 driver module for cx2388x based TV cards");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");
MODULE_VERSION(CX88_VERSION);

/* ------------------------------------------------------------------ */

static unsigned int video_nr[] = {[0 ... (CX88_MAXBOARDS - 1)] = UNSET };
static unsigned int vbi_nr[]   = {[0 ... (CX88_MAXBOARDS - 1)] = UNSET };
static unsigned int radio_nr[] = {[0 ... (CX88_MAXBOARDS - 1)] = UNSET };

module_param_array(video_nr, int, NULL, 0444);
module_param_array(vbi_nr,   int, NULL, 0444);
module_param_array(radio_nr, int, NULL, 0444);

MODULE_PARM_DESC(video_nr,"video device numbers");
MODULE_PARM_DESC(vbi_nr,"vbi device numbers");
MODULE_PARM_DESC(radio_nr,"radio device numbers");

static unsigned int video_debug;
module_param(video_debug,int,0644);
MODULE_PARM_DESC(video_debug,"enable debug messages [video]");

static unsigned int irq_debug;
module_param(irq_debug,int,0644);
MODULE_PARM_DESC(irq_debug,"enable debug messages [IRQ handler]");

static unsigned int vid_limit = 16;
module_param(vid_limit,int,0644);
MODULE_PARM_DESC(vid_limit,"capture memory limit in megabytes");

#define dprintk(level,fmt, arg...)	if (video_debug >= level) \
	printk(KERN_DEBUG "%s/0: " fmt, core->name , ## arg)

/* ------------------------------------------------------------------- */
/* static data                                                         */

static const struct cx8800_fmt formats[] = {
	{
		.name     = "8 bpp, gray",
		.fourcc   = V4L2_PIX_FMT_GREY,
		.cxformat = ColorFormatY8,
		.depth    = 8,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "15 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_RGB555,
		.cxformat = ColorFormatRGB15,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "15 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB555X,
		.cxformat = ColorFormatRGB15 | ColorFormatBSWAP,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "16 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_RGB565,
		.cxformat = ColorFormatRGB16,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "16 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB565X,
		.cxformat = ColorFormatRGB16 | ColorFormatBSWAP,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "24 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_BGR24,
		.cxformat = ColorFormatRGB24,
		.depth    = 24,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "32 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_BGR32,
		.cxformat = ColorFormatRGB32,
		.depth    = 32,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "32 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB32,
		.cxformat = ColorFormatRGB32 | ColorFormatBSWAP | ColorFormatWSWAP,
		.depth    = 32,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "4:2:2, packed, YUYV",
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.cxformat = ColorFormatYUY2,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "4:2:2, packed, UYVY",
		.fourcc   = V4L2_PIX_FMT_UYVY,
		.cxformat = ColorFormatYUY2 | ColorFormatBSWAP,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},
};

static const struct cx8800_fmt* format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (formats[i].fourcc == fourcc)
			return formats+i;
	return NULL;
}

/* ------------------------------------------------------------------- */

struct cx88_ctrl {
	/* control information */
	u32 id;
	s32 minimum;
	s32 maximum;
	u32 step;
	s32 default_value;

	/* control register information */
	u32 off;
	u32 reg;
	u32 sreg;
	u32 mask;
	u32 shift;
};

static const struct cx88_ctrl cx8800_vid_ctls[] = {
	/* --- video --- */
	{
		.id            = V4L2_CID_BRIGHTNESS,
		.minimum       = 0x00,
		.maximum       = 0xff,
		.step          = 1,
		.default_value = 0x7f,
		.off           = 128,
		.reg           = MO_CONTR_BRIGHT,
		.mask          = 0x00ff,
		.shift         = 0,
	},{
		.id            = V4L2_CID_CONTRAST,
		.minimum       = 0,
		.maximum       = 0xff,
		.step          = 1,
		.default_value = 0x3f,
		.off           = 0,
		.reg           = MO_CONTR_BRIGHT,
		.mask          = 0xff00,
		.shift         = 8,
	},{
		.id            = V4L2_CID_HUE,
		.minimum       = 0,
		.maximum       = 0xff,
		.step          = 1,
		.default_value = 0x7f,
		.off           = 128,
		.reg           = MO_HUE,
		.mask          = 0x00ff,
		.shift         = 0,
	},{
		/* strictly, this only describes only U saturation.
		 * V saturation is handled specially through code.
		 */
		.id            = V4L2_CID_SATURATION,
		.minimum       = 0,
		.maximum       = 0xff,
		.step          = 1,
		.default_value = 0x7f,
		.off           = 0,
		.reg           = MO_UV_SATURATION,
		.mask          = 0x00ff,
		.shift         = 0,
	}, {
		.id            = V4L2_CID_SHARPNESS,
		.minimum       = 0,
		.maximum       = 4,
		.step          = 1,
		.default_value = 0x0,
		.off           = 0,
		/* NOTE: the value is converted and written to both even
		   and odd registers in the code */
		.reg           = MO_FILTER_ODD,
		.mask          = 7 << 7,
		.shift         = 7,
	}, {
		.id            = V4L2_CID_CHROMA_AGC,
		.minimum       = 0,
		.maximum       = 1,
		.default_value = 0x1,
		.reg           = MO_INPUT_FORMAT,
		.mask          = 1 << 10,
		.shift         = 10,
	}, {
		.id            = V4L2_CID_COLOR_KILLER,
		.minimum       = 0,
		.maximum       = 1,
		.default_value = 0x1,
		.reg           = MO_INPUT_FORMAT,
		.mask          = 1 << 9,
		.shift         = 9,
	}, {
		.id            = V4L2_CID_BAND_STOP_FILTER,
		.minimum       = 0,
		.maximum       = 1,
		.step          = 1,
		.default_value = 0x0,
		.off           = 0,
		.reg           = MO_HTOTAL,
		.mask          = 3 << 11,
		.shift         = 11,
	}
};

static const struct cx88_ctrl cx8800_aud_ctls[] = {
	{
		/* --- audio --- */
		.id            = V4L2_CID_AUDIO_MUTE,
		.minimum       = 0,
		.maximum       = 1,
		.default_value = 1,
		.reg           = AUD_VOL_CTL,
		.sreg          = SHADOW_AUD_VOL_CTL,
		.mask          = (1 << 6),
		.shift         = 6,
	},{
		.id            = V4L2_CID_AUDIO_VOLUME,
		.minimum       = 0,
		.maximum       = 0x3f,
		.step          = 1,
		.default_value = 0x3f,
		.reg           = AUD_VOL_CTL,
		.sreg          = SHADOW_AUD_VOL_CTL,
		.mask          = 0x3f,
		.shift         = 0,
	},{
		.id            = V4L2_CID_AUDIO_BALANCE,
		.minimum       = 0,
		.maximum       = 0x7f,
		.step          = 1,
		.default_value = 0x40,
		.reg           = AUD_BAL_CTL,
		.sreg          = SHADOW_AUD_BAL_CTL,
		.mask          = 0x7f,
		.shift         = 0,
	}
};

enum {
	CX8800_VID_CTLS = ARRAY_SIZE(cx8800_vid_ctls),
	CX8800_AUD_CTLS = ARRAY_SIZE(cx8800_aud_ctls),
};

/* ------------------------------------------------------------------- */
/* resource management                                                 */

static int res_get(struct cx8800_dev *dev, struct cx8800_fh *fh, unsigned int bit)
{
	struct cx88_core *core = dev->core;
	if (fh->resources & bit)
		/* have it already allocated */
		return 1;

	/* is it free? */
	mutex_lock(&core->lock);
	if (dev->resources & bit) {
		/* no, someone else uses it */
		mutex_unlock(&core->lock);
		return 0;
	}
	/* it's free, grab it */
	fh->resources  |= bit;
	dev->resources |= bit;
	dprintk(1,"res: get %d\n",bit);
	mutex_unlock(&core->lock);
	return 1;
}

static
int res_check(struct cx8800_fh *fh, unsigned int bit)
{
	return (fh->resources & bit);
}

static
int res_locked(struct cx8800_dev *dev, unsigned int bit)
{
	return (dev->resources & bit);
}

static
void res_free(struct cx8800_dev *dev, struct cx8800_fh *fh, unsigned int bits)
{
	struct cx88_core *core = dev->core;
	BUG_ON((fh->resources & bits) != bits);

	mutex_lock(&core->lock);
	fh->resources  &= ~bits;
	dev->resources &= ~bits;
	dprintk(1,"res: put %d\n",bits);
	mutex_unlock(&core->lock);
}

/* ------------------------------------------------------------------ */

int cx88_video_mux(struct cx88_core *core, unsigned int input)
{
	/* struct cx88_core *core = dev->core; */

	dprintk(1,"video_mux: %d [vmux=%d,gpio=0x%x,0x%x,0x%x,0x%x]\n",
		input, INPUT(input).vmux,
		INPUT(input).gpio0,INPUT(input).gpio1,
		INPUT(input).gpio2,INPUT(input).gpio3);
	core->input = input;
	cx_andor(MO_INPUT_FORMAT, 0x03 << 14, INPUT(input).vmux << 14);
	cx_write(MO_GP3_IO, INPUT(input).gpio3);
	cx_write(MO_GP0_IO, INPUT(input).gpio0);
	cx_write(MO_GP1_IO, INPUT(input).gpio1);
	cx_write(MO_GP2_IO, INPUT(input).gpio2);

	switch (INPUT(input).type) {
	case CX88_VMUX_SVIDEO:
		cx_set(MO_AFECFG_IO,    0x00000001);
		cx_set(MO_INPUT_FORMAT, 0x00010010);
		cx_set(MO_FILTER_EVEN,  0x00002020);
		cx_set(MO_FILTER_ODD,   0x00002020);
		break;
	default:
		cx_clear(MO_AFECFG_IO,    0x00000001);
		cx_clear(MO_INPUT_FORMAT, 0x00010010);
		cx_clear(MO_FILTER_EVEN,  0x00002020);
		cx_clear(MO_FILTER_ODD,   0x00002020);
		break;
	}

	/* if there are audioroutes defined, we have an external
	   ADC to deal with audio */
	if (INPUT(input).audioroute) {
		/* The wm8775 module has the "2" route hardwired into
		   the initialization. Some boards may use different
		   routes for different inputs. HVR-1300 surely does */
		if (core->board.audio_chip &&
		    core->board.audio_chip == V4L2_IDENT_WM8775) {
			call_all(core, audio, s_routing,
				 INPUT(input).audioroute, 0, 0);
		}
		/* cx2388's C-ADC is connected to the tuner only.
		   When used with S-Video, that ADC is busy dealing with
		   chroma, so an external must be used for baseband audio */
		if (INPUT(input).type != CX88_VMUX_TELEVISION &&
		    INPUT(input).type != CX88_VMUX_CABLE) {
			/* "I2S ADC mode" */
			core->tvaudio = WW_I2SADC;
			cx88_set_tvaudio(core);
		} else {
			/* Normal mode */
			cx_write(AUD_I2SCNTL, 0x0);
			cx_clear(AUD_CTL, EN_I2SIN_ENABLE);
		}
	}

	return 0;
}
EXPORT_SYMBOL(cx88_video_mux);

/* ------------------------------------------------------------------ */

static int start_video_dma(struct cx8800_dev    *dev,
			   struct cx88_dmaqueue *q,
			   struct cx88_buffer   *buf)
{
	struct cx88_core *core = dev->core;

	/* setup fifo + format */
	cx88_sram_channel_setup(core, &cx88_sram_channels[SRAM_CH21],
				buf->bpl, buf->risc.dma);
	cx88_set_scale(core, buf->vb.width, buf->vb.height, buf->vb.field);
	cx_write(MO_COLOR_CTRL, buf->fmt->cxformat | ColorFormatGamma);

	/* reset counter */
	cx_write(MO_VIDY_GPCNTRL,GP_COUNT_CONTROL_RESET);
	q->count = 1;

	/* enable irqs */
	cx_set(MO_PCI_INTMSK, core->pci_irqmask | PCI_INT_VIDINT);

	/* Enables corresponding bits at PCI_INT_STAT:
		bits 0 to 4: video, audio, transport stream, VIP, Host
		bit 7: timer
		bits 8 and 9: DMA complete for: SRC, DST
		bits 10 and 11: BERR signal asserted for RISC: RD, WR
		bits 12 to 15: BERR signal asserted for: BRDG, SRC, DST, IPB
	 */
	cx_set(MO_VID_INTMSK, 0x0f0011);

	/* enable capture */
	cx_set(VID_CAPTURE_CONTROL,0x06);

	/* start dma */
	cx_set(MO_DEV_CNTRL2, (1<<5));
	cx_set(MO_VID_DMACNTRL, 0x11); /* Planar Y and packed FIFO and RISC enable */

	return 0;
}

#ifdef CONFIG_PM
static int stop_video_dma(struct cx8800_dev    *dev)
{
	struct cx88_core *core = dev->core;

	/* stop dma */
	cx_clear(MO_VID_DMACNTRL, 0x11);

	/* disable capture */
	cx_clear(VID_CAPTURE_CONTROL,0x06);

	/* disable irqs */
	cx_clear(MO_PCI_INTMSK, PCI_INT_VIDINT);
	cx_clear(MO_VID_INTMSK, 0x0f0011);
	return 0;
}
#endif

static int restart_video_queue(struct cx8800_dev    *dev,
			       struct cx88_dmaqueue *q)
{
	struct cx88_core *core = dev->core;
	struct cx88_buffer *buf, *prev;

	if (!list_empty(&q->active)) {
		buf = list_entry(q->active.next, struct cx88_buffer, vb.queue);
		dprintk(2,"restart_queue [%p/%d]: restart dma\n",
			buf, buf->vb.i);
		start_video_dma(dev, q, buf);
		list_for_each_entry(buf, &q->active, vb.queue)
			buf->count = q->count++;
		mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
		return 0;
	}

	prev = NULL;
	for (;;) {
		if (list_empty(&q->queued))
			return 0;
		buf = list_entry(q->queued.next, struct cx88_buffer, vb.queue);
		if (NULL == prev) {
			list_move_tail(&buf->vb.queue, &q->active);
			start_video_dma(dev, q, buf);
			buf->vb.state = VIDEOBUF_ACTIVE;
			buf->count    = q->count++;
			mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
			dprintk(2,"[%p/%d] restart_queue - first active\n",
				buf,buf->vb.i);

		} else if (prev->vb.width  == buf->vb.width  &&
			   prev->vb.height == buf->vb.height &&
			   prev->fmt       == buf->fmt) {
			list_move_tail(&buf->vb.queue, &q->active);
			buf->vb.state = VIDEOBUF_ACTIVE;
			buf->count    = q->count++;
			prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
			dprintk(2,"[%p/%d] restart_queue - move to active\n",
				buf,buf->vb.i);
		} else {
			return 0;
		}
		prev = buf;
	}
}

/* ------------------------------------------------------------------ */

static int
buffer_setup(struct videobuf_queue *q, unsigned int *count, unsigned int *size)
{
	struct cx8800_fh *fh = q->priv_data;
	struct cx8800_dev  *dev = fh->dev;

	*size = dev->fmt->depth * dev->width * dev->height >> 3;
	if (0 == *count)
		*count = 32;
	if (*size * *count > vid_limit * 1024 * 1024)
		*count = (vid_limit * 1024 * 1024) / *size;
	return 0;
}

static int
buffer_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
	       enum v4l2_field field)
{
	struct cx8800_fh   *fh  = q->priv_data;
	struct cx8800_dev  *dev = fh->dev;
	struct cx88_core *core = dev->core;
	struct cx88_buffer *buf = container_of(vb,struct cx88_buffer,vb);
	struct videobuf_dmabuf *dma=videobuf_to_dma(&buf->vb);
	int rc, init_buffer = 0;

	BUG_ON(NULL == dev->fmt);
	if (dev->width  < 48 || dev->width  > norm_maxw(core->tvnorm) ||
	    dev->height < 32 || dev->height > norm_maxh(core->tvnorm))
		return -EINVAL;
	buf->vb.size = (dev->width * dev->height * dev->fmt->depth) >> 3;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	if (buf->fmt       != dev->fmt    ||
	    buf->vb.width  != dev->width  ||
	    buf->vb.height != dev->height ||
	    buf->vb.field  != field) {
		buf->fmt       = dev->fmt;
		buf->vb.width  = dev->width;
		buf->vb.height = dev->height;
		buf->vb.field  = field;
		init_buffer = 1;
	}

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		init_buffer = 1;
		if (0 != (rc = videobuf_iolock(q,&buf->vb,NULL)))
			goto fail;
	}

	if (init_buffer) {
		buf->bpl = buf->vb.width * buf->fmt->depth >> 3;
		switch (buf->vb.field) {
		case V4L2_FIELD_TOP:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 dma->sglist, 0, UNSET,
					 buf->bpl, 0, buf->vb.height);
			break;
		case V4L2_FIELD_BOTTOM:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 dma->sglist, UNSET, 0,
					 buf->bpl, 0, buf->vb.height);
			break;
		case V4L2_FIELD_INTERLACED:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 dma->sglist, 0, buf->bpl,
					 buf->bpl, buf->bpl,
					 buf->vb.height >> 1);
			break;
		case V4L2_FIELD_SEQ_TB:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 dma->sglist,
					 0, buf->bpl * (buf->vb.height >> 1),
					 buf->bpl, 0,
					 buf->vb.height >> 1);
			break;
		case V4L2_FIELD_SEQ_BT:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 dma->sglist,
					 buf->bpl * (buf->vb.height >> 1), 0,
					 buf->bpl, 0,
					 buf->vb.height >> 1);
			break;
		default:
			BUG();
		}
	}
	dprintk(2,"[%p/%d] buffer_prepare - %dx%d %dbpp \"%s\" - dma=0x%08lx\n",
		buf, buf->vb.i,
		dev->width, dev->height, dev->fmt->depth, dev->fmt->name,
		(unsigned long)buf->risc.dma);

	buf->vb.state = VIDEOBUF_PREPARED;
	return 0;

 fail:
	cx88_free_buffer(q,buf);
	return rc;
}

static void
buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct cx88_buffer    *buf = container_of(vb,struct cx88_buffer,vb);
	struct cx88_buffer    *prev;
	struct cx8800_fh      *fh   = vq->priv_data;
	struct cx8800_dev     *dev  = fh->dev;
	struct cx88_core      *core = dev->core;
	struct cx88_dmaqueue  *q    = &dev->vidq;

	/* add jump to stopper */
	buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_IRQ1 | RISC_CNT_INC);
	buf->risc.jmp[1] = cpu_to_le32(q->stopper.dma);

	if (!list_empty(&q->queued)) {
		list_add_tail(&buf->vb.queue,&q->queued);
		buf->vb.state = VIDEOBUF_QUEUED;
		dprintk(2,"[%p/%d] buffer_queue - append to queued\n",
			buf, buf->vb.i);

	} else if (list_empty(&q->active)) {
		list_add_tail(&buf->vb.queue,&q->active);
		start_video_dma(dev, q, buf);
		buf->vb.state = VIDEOBUF_ACTIVE;
		buf->count    = q->count++;
		mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
		dprintk(2,"[%p/%d] buffer_queue - first active\n",
			buf, buf->vb.i);

	} else {
		prev = list_entry(q->active.prev, struct cx88_buffer, vb.queue);
		if (prev->vb.width  == buf->vb.width  &&
		    prev->vb.height == buf->vb.height &&
		    prev->fmt       == buf->fmt) {
			list_add_tail(&buf->vb.queue,&q->active);
			buf->vb.state = VIDEOBUF_ACTIVE;
			buf->count    = q->count++;
			prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
			dprintk(2,"[%p/%d] buffer_queue - append to active\n",
				buf, buf->vb.i);

		} else {
			list_add_tail(&buf->vb.queue,&q->queued);
			buf->vb.state = VIDEOBUF_QUEUED;
			dprintk(2,"[%p/%d] buffer_queue - first queued\n",
				buf, buf->vb.i);
		}
	}
}

static void buffer_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct cx88_buffer *buf = container_of(vb,struct cx88_buffer,vb);

	cx88_free_buffer(q,buf);
}

static const struct videobuf_queue_ops cx8800_video_qops = {
	.buf_setup    = buffer_setup,
	.buf_prepare  = buffer_prepare,
	.buf_queue    = buffer_queue,
	.buf_release  = buffer_release,
};

/* ------------------------------------------------------------------ */


/* ------------------------------------------------------------------ */

static struct videobuf_queue *get_queue(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct cx8800_fh *fh = file->private_data;

	switch (vdev->vfl_type) {
	case VFL_TYPE_GRABBER:
		return &fh->vidq;
	case VFL_TYPE_VBI:
		return &fh->vbiq;
	default:
		BUG();
		return NULL;
	}
}

static int get_resource(struct file *file)
{
	struct video_device *vdev = video_devdata(file);

	switch (vdev->vfl_type) {
	case VFL_TYPE_GRABBER:
		return RESOURCE_VIDEO;
	case VFL_TYPE_VBI:
		return RESOURCE_VBI;
	default:
		BUG();
		return 0;
	}
}

static int video_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct cx8800_dev *dev = video_drvdata(file);
	struct cx88_core *core = dev->core;
	struct cx8800_fh *fh;
	enum v4l2_buf_type type = 0;
	int radio = 0;

	switch (vdev->vfl_type) {
	case VFL_TYPE_GRABBER:
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		break;
	case VFL_TYPE_VBI:
		type = V4L2_BUF_TYPE_VBI_CAPTURE;
		break;
	case VFL_TYPE_RADIO:
		radio = 1;
		break;
	}

	dprintk(1, "open dev=%s radio=%d type=%s\n",
		video_device_node_name(vdev), radio, v4l2_type_names[type]);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh),GFP_KERNEL);
	if (unlikely(!fh))
		return -ENOMEM;

	v4l2_fh_init(&fh->fh, vdev);
	file->private_data = fh;
	fh->dev      = dev;

	mutex_lock(&core->lock);

	videobuf_queue_sg_init(&fh->vidq, &cx8800_video_qops,
			    &dev->pci->dev, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_INTERLACED,
			    sizeof(struct cx88_buffer),
			    fh, NULL);
	videobuf_queue_sg_init(&fh->vbiq, &cx8800_vbi_qops,
			    &dev->pci->dev, &dev->slock,
			    V4L2_BUF_TYPE_VBI_CAPTURE,
			    V4L2_FIELD_SEQ_TB,
			    sizeof(struct cx88_buffer),
			    fh, NULL);

	if (vdev->vfl_type == VFL_TYPE_RADIO) {
		dprintk(1,"video_open: setting radio device\n");
		cx_write(MO_GP3_IO, core->board.radio.gpio3);
		cx_write(MO_GP0_IO, core->board.radio.gpio0);
		cx_write(MO_GP1_IO, core->board.radio.gpio1);
		cx_write(MO_GP2_IO, core->board.radio.gpio2);
		if (core->board.radio.audioroute) {
			if(core->board.audio_chip &&
				core->board.audio_chip == V4L2_IDENT_WM8775) {
				call_all(core, audio, s_routing,
					core->board.radio.audioroute, 0, 0);
			}
			/* "I2S ADC mode" */
			core->tvaudio = WW_I2SADC;
			cx88_set_tvaudio(core);
		} else {
			/* FM Mode */
			core->tvaudio = WW_FM;
			cx88_set_tvaudio(core);
			cx88_set_stereo(core,V4L2_TUNER_MODE_STEREO,1);
		}
		call_all(core, tuner, s_radio);
	}

	core->users++;
	mutex_unlock(&core->lock);
	v4l2_fh_add(&fh->fh);

	return 0;
}

static ssize_t
video_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct video_device *vdev = video_devdata(file);
	struct cx8800_fh *fh = file->private_data;

	switch (vdev->vfl_type) {
	case VFL_TYPE_GRABBER:
		if (res_locked(fh->dev,RESOURCE_VIDEO))
			return -EBUSY;
		return videobuf_read_one(&fh->vidq, data, count, ppos,
					 file->f_flags & O_NONBLOCK);
	case VFL_TYPE_VBI:
		if (!res_get(fh->dev,fh,RESOURCE_VBI))
			return -EBUSY;
		return videobuf_read_stream(&fh->vbiq, data, count, ppos, 1,
					    file->f_flags & O_NONBLOCK);
	default:
		BUG();
		return 0;
	}
}

static unsigned int
video_poll(struct file *file, struct poll_table_struct *wait)
{
	struct video_device *vdev = video_devdata(file);
	struct cx8800_fh *fh = file->private_data;
	struct cx88_buffer *buf;
	unsigned int rc = v4l2_ctrl_poll(file, wait);

	if (vdev->vfl_type == VFL_TYPE_VBI) {
		if (!res_get(fh->dev,fh,RESOURCE_VBI))
			return rc | POLLERR;
		return rc | videobuf_poll_stream(file, &fh->vbiq, wait);
	}
	mutex_lock(&fh->vidq.vb_lock);
	if (res_check(fh,RESOURCE_VIDEO)) {
		/* streaming capture */
		if (list_empty(&fh->vidq.stream))
			goto done;
		buf = list_entry(fh->vidq.stream.next,struct cx88_buffer,vb.stream);
	} else {
		/* read() capture */
		buf = (struct cx88_buffer*)fh->vidq.read_buf;
		if (NULL == buf)
			goto done;
	}
	poll_wait(file, &buf->vb.done, wait);
	if (buf->vb.state == VIDEOBUF_DONE ||
	    buf->vb.state == VIDEOBUF_ERROR)
		rc |= POLLIN|POLLRDNORM;
done:
	mutex_unlock(&fh->vidq.vb_lock);
	return rc;
}

static int video_release(struct file *file)
{
	struct cx8800_fh  *fh  = file->private_data;
	struct cx8800_dev *dev = fh->dev;

	/* turn off overlay */
	if (res_check(fh, RESOURCE_OVERLAY)) {
		/* FIXME */
		res_free(dev,fh,RESOURCE_OVERLAY);
	}

	/* stop video capture */
	if (res_check(fh, RESOURCE_VIDEO)) {
		videobuf_queue_cancel(&fh->vidq);
		res_free(dev,fh,RESOURCE_VIDEO);
	}
	if (fh->vidq.read_buf) {
		buffer_release(&fh->vidq,fh->vidq.read_buf);
		kfree(fh->vidq.read_buf);
	}

	/* stop vbi capture */
	if (res_check(fh, RESOURCE_VBI)) {
		videobuf_stop(&fh->vbiq);
		res_free(dev,fh,RESOURCE_VBI);
	}

	videobuf_mmap_free(&fh->vidq);
	videobuf_mmap_free(&fh->vbiq);

	mutex_lock(&dev->core->lock);
	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);
	file->private_data = NULL;
	kfree(fh);

	dev->core->users--;
	if (!dev->core->users)
		call_all(dev->core, core, s_power, 0);
	mutex_unlock(&dev->core->lock);

	return 0;
}

static int
video_mmap(struct file *file, struct vm_area_struct * vma)
{
	return videobuf_mmap_mapper(get_queue(file), vma);
}

/* ------------------------------------------------------------------ */
/* VIDEO CTRL IOCTLS                                                  */

static int cx8800_s_vid_ctrl(struct v4l2_ctrl *ctrl)
{
	struct cx88_core *core =
		container_of(ctrl->handler, struct cx88_core, video_hdl);
	const struct cx88_ctrl *cc = ctrl->priv;
	u32 value, mask;

	mask = cc->mask;
	switch (ctrl->id) {
	case V4L2_CID_SATURATION:
		/* special v_sat handling */

		value = ((ctrl->val - cc->off) << cc->shift) & cc->mask;

		if (core->tvnorm & V4L2_STD_SECAM) {
			/* For SECAM, both U and V sat should be equal */
			value = value << 8 | value;
		} else {
			/* Keeps U Saturation proportional to V Sat */
			value = (value * 0x5a) / 0x7f << 8 | value;
		}
		mask = 0xffff;
		break;
	case V4L2_CID_SHARPNESS:
		/* 0b000, 0b100, 0b101, 0b110, or 0b111 */
		value = (ctrl->val < 1 ? 0 : ((ctrl->val + 3) << 7));
		/* needs to be set for both fields */
		cx_andor(MO_FILTER_EVEN, mask, value);
		break;
	case V4L2_CID_CHROMA_AGC:
		value = ((ctrl->val - cc->off) << cc->shift) & cc->mask;
		break;
	default:
		value = ((ctrl->val - cc->off) << cc->shift) & cc->mask;
		break;
	}
	dprintk(1, "set_control id=0x%X(%s) ctrl=0x%02x, reg=0x%02x val=0x%02x (mask 0x%02x)%s\n",
				ctrl->id, ctrl->name, ctrl->val, cc->reg, value,
				mask, cc->sreg ? " [shadowed]" : "");
	if (cc->sreg)
		cx_sandor(cc->sreg, cc->reg, mask, value);
	else
		cx_andor(cc->reg, mask, value);
	return 0;
}

static int cx8800_s_aud_ctrl(struct v4l2_ctrl *ctrl)
{
	struct cx88_core *core =
		container_of(ctrl->handler, struct cx88_core, audio_hdl);
	const struct cx88_ctrl *cc = ctrl->priv;
	u32 value,mask;

	/* Pass changes onto any WM8775 */
	if (core->board.audio_chip == V4L2_IDENT_WM8775) {
		switch (ctrl->id) {
		case V4L2_CID_AUDIO_MUTE:
			wm8775_s_ctrl(core, ctrl->id, ctrl->val);
			break;
		case V4L2_CID_AUDIO_VOLUME:
			wm8775_s_ctrl(core, ctrl->id, (ctrl->val) ?
						(0x90 + ctrl->val) << 8 : 0);
			break;
		case V4L2_CID_AUDIO_BALANCE:
			wm8775_s_ctrl(core, ctrl->id, ctrl->val << 9);
			break;
		default:
			break;
		}
	}

	mask = cc->mask;
	switch (ctrl->id) {
	case V4L2_CID_AUDIO_BALANCE:
		value = (ctrl->val < 0x40) ? (0x7f - ctrl->val) : (ctrl->val - 0x40);
		break;
	case V4L2_CID_AUDIO_VOLUME:
		value = 0x3f - (ctrl->val & 0x3f);
		break;
	default:
		value = ((ctrl->val - cc->off) << cc->shift) & cc->mask;
		break;
	}
	dprintk(1,"set_control id=0x%X(%s) ctrl=0x%02x, reg=0x%02x val=0x%02x (mask 0x%02x)%s\n",
				ctrl->id, ctrl->name, ctrl->val, cc->reg, value,
				mask, cc->sreg ? " [shadowed]" : "");
	if (cc->sreg)
		cx_sandor(cc->sreg, cc->reg, mask, value);
	else
		cx_andor(cc->reg, mask, value);
	return 0;
}

/* ------------------------------------------------------------------ */
/* VIDEO IOCTLS                                                       */

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct cx8800_fh  *fh   = priv;
	struct cx8800_dev *dev = fh->dev;

	f->fmt.pix.width        = dev->width;
	f->fmt.pix.height       = dev->height;
	f->fmt.pix.field        = fh->vidq.field;
	f->fmt.pix.pixelformat  = dev->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * dev->fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;
	const struct cx8800_fmt *fmt;
	enum v4l2_field   field;
	unsigned int      maxw, maxh;

	fmt = format_by_fourcc(f->fmt.pix.pixelformat);
	if (NULL == fmt)
		return -EINVAL;

	field = f->fmt.pix.field;
	maxw  = norm_maxw(core->tvnorm);
	maxh  = norm_maxh(core->tvnorm);

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
		break;
	default:
		return -EINVAL;
	}

	f->fmt.pix.field = field;
	v4l_bound_align_image(&f->fmt.pix.width, 48, maxw, 2,
			      &f->fmt.pix.height, 32, maxh, 0, 0);
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct cx8800_fh  *fh   = priv;
	struct cx8800_dev *dev = fh->dev;
	int err = vidioc_try_fmt_vid_cap (file,priv,f);

	if (0 != err)
		return err;
	dev->fmt        = format_by_fourcc(f->fmt.pix.pixelformat);
	dev->width      = f->fmt.pix.width;
	dev->height     = f->fmt.pix.height;
	fh->vidq.field = f->fmt.pix.field;
	return 0;
}

void cx88_querycap(struct file *file, struct cx88_core *core,
		struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);

	strlcpy(cap->card, core->board.name, sizeof(cap->card));
	cap->device_caps = V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	if (UNSET != core->board.tuner_type)
		cap->device_caps |= V4L2_CAP_TUNER;
	switch (vdev->vfl_type) {
	case VFL_TYPE_RADIO:
		cap->device_caps = V4L2_CAP_RADIO | V4L2_CAP_TUNER;
		break;
	case VFL_TYPE_GRABBER:
		cap->device_caps |= V4L2_CAP_VIDEO_CAPTURE;
		break;
	case VFL_TYPE_VBI:
		cap->device_caps |= V4L2_CAP_VBI_CAPTURE;
		break;
	}
	cap->capabilities = cap->device_caps | V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_VBI_CAPTURE | V4L2_CAP_DEVICE_CAPS;
	if (core->board.radio.type == CX88_RADIO)
		cap->capabilities |= V4L2_CAP_RADIO;
}
EXPORT_SYMBOL(cx88_querycap);

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct cx8800_dev *dev  = ((struct cx8800_fh *)priv)->dev;
	struct cx88_core  *core = dev->core;

	strcpy(cap->driver, "cx8800");
	sprintf(cap->bus_info, "PCI:%s", pci_name(dev->pci));
	cx88_querycap(file, core, cap);
	return 0;
}

static int vidioc_enum_fmt_vid_cap (struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (unlikely(f->index >= ARRAY_SIZE(formats)))
		return -EINVAL;

	strlcpy(f->description,formats[f->index].name,sizeof(f->description));
	f->pixelformat = formats[f->index].fourcc;

	return 0;
}

static int vidioc_reqbufs (struct file *file, void *priv, struct v4l2_requestbuffers *p)
{
	return videobuf_reqbufs(get_queue(file), p);
}

static int vidioc_querybuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	return videobuf_querybuf(get_queue(file), p);
}

static int vidioc_qbuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	return videobuf_qbuf(get_queue(file), p);
}

static int vidioc_dqbuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	return videobuf_dqbuf(get_queue(file), p,
				file->f_flags & O_NONBLOCK);
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct video_device *vdev = video_devdata(file);
	struct cx8800_fh  *fh   = priv;
	struct cx8800_dev *dev  = fh->dev;

	if ((vdev->vfl_type == VFL_TYPE_GRABBER && i != V4L2_BUF_TYPE_VIDEO_CAPTURE) ||
	    (vdev->vfl_type == VFL_TYPE_VBI && i != V4L2_BUF_TYPE_VBI_CAPTURE))
		return -EINVAL;

	if (unlikely(!res_get(dev, fh, get_resource(file))))
		return -EBUSY;
	return videobuf_streamon(get_queue(file));
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct video_device *vdev = video_devdata(file);
	struct cx8800_fh  *fh   = priv;
	struct cx8800_dev *dev  = fh->dev;
	int               err, res;

	if ((vdev->vfl_type == VFL_TYPE_GRABBER && i != V4L2_BUF_TYPE_VIDEO_CAPTURE) ||
	    (vdev->vfl_type == VFL_TYPE_VBI && i != V4L2_BUF_TYPE_VBI_CAPTURE))
		return -EINVAL;

	res = get_resource(file);
	err = videobuf_streamoff(get_queue(file));
	if (err < 0)
		return err;
	res_free(dev,fh,res);
	return 0;
}

static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *tvnorm)
{
	struct cx88_core *core = ((struct cx8800_fh *)priv)->dev->core;

	*tvnorm = core->tvnorm;
	return 0;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id tvnorms)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;

	mutex_lock(&core->lock);
	cx88_set_tvnorm(core, tvnorms);
	mutex_unlock(&core->lock);

	return 0;
}

/* only one input in this sample driver */
int cx88_enum_input (struct cx88_core  *core,struct v4l2_input *i)
{
	static const char * const iname[] = {
		[ CX88_VMUX_COMPOSITE1 ] = "Composite1",
		[ CX88_VMUX_COMPOSITE2 ] = "Composite2",
		[ CX88_VMUX_COMPOSITE3 ] = "Composite3",
		[ CX88_VMUX_COMPOSITE4 ] = "Composite4",
		[ CX88_VMUX_SVIDEO     ] = "S-Video",
		[ CX88_VMUX_TELEVISION ] = "Television",
		[ CX88_VMUX_CABLE      ] = "Cable TV",
		[ CX88_VMUX_DVB        ] = "DVB",
		[ CX88_VMUX_DEBUG      ] = "for debug only",
	};
	unsigned int n = i->index;

	if (n >= 4)
		return -EINVAL;
	if (0 == INPUT(n).type)
		return -EINVAL;
	i->type  = V4L2_INPUT_TYPE_CAMERA;
	strcpy(i->name,iname[INPUT(n).type]);
	if ((CX88_VMUX_TELEVISION == INPUT(n).type) ||
	    (CX88_VMUX_CABLE      == INPUT(n).type)) {
		i->type = V4L2_INPUT_TYPE_TUNER;
	}
	i->std = CX88_NORMS;
	return 0;
}
EXPORT_SYMBOL(cx88_enum_input);

static int vidioc_enum_input (struct file *file, void *priv,
				struct v4l2_input *i)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;
	return cx88_enum_input (core,i);
}

static int vidioc_g_input (struct file *file, void *priv, unsigned int *i)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;

	*i = core->input;
	return 0;
}

static int vidioc_s_input (struct file *file, void *priv, unsigned int i)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;

	if (i >= 4)
		return -EINVAL;
	if (0 == INPUT(i).type)
		return -EINVAL;

	mutex_lock(&core->lock);
	cx88_newstation(core);
	cx88_video_mux(core,i);
	mutex_unlock(&core->lock);
	return 0;
}

static int vidioc_g_tuner (struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;
	u32 reg;

	if (unlikely(UNSET == core->board.tuner_type))
		return -EINVAL;
	if (0 != t->index)
		return -EINVAL;

	strcpy(t->name, "Television");
	t->capability = V4L2_TUNER_CAP_NORM;
	t->rangehigh  = 0xffffffffUL;
	call_all(core, tuner, g_tuner, t);

	cx88_get_stereo(core ,t);
	reg = cx_read(MO_DEVICE_STATUS);
	t->signal = (reg & (1<<5)) ? 0xffff : 0x0000;
	return 0;
}

static int vidioc_s_tuner (struct file *file, void *priv,
				const struct v4l2_tuner *t)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;

	if (UNSET == core->board.tuner_type)
		return -EINVAL;
	if (0 != t->index)
		return -EINVAL;

	cx88_set_stereo(core, t->audmode, 1);
	return 0;
}

static int vidioc_g_frequency (struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct cx8800_fh  *fh   = priv;
	struct cx88_core  *core = fh->dev->core;

	if (unlikely(UNSET == core->board.tuner_type))
		return -EINVAL;
	if (f->tuner)
		return -EINVAL;

	f->frequency = core->freq;

	call_all(core, tuner, g_frequency, f);

	return 0;
}

int cx88_set_freq (struct cx88_core  *core,
				const struct v4l2_frequency *f)
{
	struct v4l2_frequency new_freq = *f;

	if (unlikely(UNSET == core->board.tuner_type))
		return -EINVAL;
	if (unlikely(f->tuner != 0))
		return -EINVAL;

	mutex_lock(&core->lock);
	cx88_newstation(core);
	call_all(core, tuner, s_frequency, f);
	call_all(core, tuner, g_frequency, &new_freq);
	core->freq = new_freq.frequency;

	/* When changing channels it is required to reset TVAUDIO */
	msleep (10);
	cx88_set_tvaudio(core);

	mutex_unlock(&core->lock);

	return 0;
}
EXPORT_SYMBOL(cx88_set_freq);

static int vidioc_s_frequency (struct file *file, void *priv,
				const struct v4l2_frequency *f)
{
	struct cx8800_fh  *fh   = priv;
	struct cx88_core  *core = fh->dev->core;

	return cx88_set_freq(core, f);
}

static int vidioc_g_chip_ident(struct file *file, void *priv,
				struct v4l2_dbg_chip_ident *chip)
{
	if (!v4l2_chip_match_host(&chip->match))
		return -EINVAL;
	chip->revision = 0;
	chip->ident = V4L2_IDENT_UNKNOWN;
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vidioc_g_register (struct file *file, void *fh,
				struct v4l2_dbg_register *reg)
{
	struct cx88_core *core = ((struct cx8800_fh*)fh)->dev->core;

	if (!v4l2_chip_match_host(&reg->match))
		return -EINVAL;
	/* cx2388x has a 24-bit register space */
	reg->val = cx_read(reg->reg & 0xffffff);
	reg->size = 4;
	return 0;
}

static int vidioc_s_register (struct file *file, void *fh,
				const struct v4l2_dbg_register *reg)
{
	struct cx88_core *core = ((struct cx8800_fh*)fh)->dev->core;

	if (!v4l2_chip_match_host(&reg->match))
		return -EINVAL;
	cx_write(reg->reg & 0xffffff, reg->val);
	return 0;
}
#endif

/* ----------------------------------------------------------- */
/* RADIO ESPECIFIC IOCTLS                                      */
/* ----------------------------------------------------------- */

static int radio_g_tuner (struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;

	if (unlikely(t->index > 0))
		return -EINVAL;

	strcpy(t->name, "Radio");

	call_all(core, tuner, g_tuner, t);
	return 0;
}

static int radio_s_tuner (struct file *file, void *priv,
				const struct v4l2_tuner *t)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;

	if (0 != t->index)
		return -EINVAL;

	call_all(core, tuner, s_tuner, t);
	return 0;
}

/* ----------------------------------------------------------- */

static void cx8800_vid_timeout(unsigned long data)
{
	struct cx8800_dev *dev = (struct cx8800_dev*)data;
	struct cx88_core *core = dev->core;
	struct cx88_dmaqueue *q = &dev->vidq;
	struct cx88_buffer *buf;
	unsigned long flags;

	cx88_sram_channel_dump(core, &cx88_sram_channels[SRAM_CH21]);

	cx_clear(MO_VID_DMACNTRL, 0x11);
	cx_clear(VID_CAPTURE_CONTROL, 0x06);

	spin_lock_irqsave(&dev->slock,flags);
	while (!list_empty(&q->active)) {
		buf = list_entry(q->active.next, struct cx88_buffer, vb.queue);
		list_del(&buf->vb.queue);
		buf->vb.state = VIDEOBUF_ERROR;
		wake_up(&buf->vb.done);
		printk("%s/0: [%p/%d] timeout - dma=0x%08lx\n", core->name,
		       buf, buf->vb.i, (unsigned long)buf->risc.dma);
	}
	restart_video_queue(dev,q);
	spin_unlock_irqrestore(&dev->slock,flags);
}

static const char *cx88_vid_irqs[32] = {
	"y_risci1", "u_risci1", "v_risci1", "vbi_risc1",
	"y_risci2", "u_risci2", "v_risci2", "vbi_risc2",
	"y_oflow",  "u_oflow",  "v_oflow",  "vbi_oflow",
	"y_sync",   "u_sync",   "v_sync",   "vbi_sync",
	"opc_err",  "par_err",  "rip_err",  "pci_abort",
};

static void cx8800_vid_irq(struct cx8800_dev *dev)
{
	struct cx88_core *core = dev->core;
	u32 status, mask, count;

	status = cx_read(MO_VID_INTSTAT);
	mask   = cx_read(MO_VID_INTMSK);
	if (0 == (status & mask))
		return;
	cx_write(MO_VID_INTSTAT, status);
	if (irq_debug  ||  (status & mask & ~0xff))
		cx88_print_irqbits(core->name, "irq vid",
				   cx88_vid_irqs, ARRAY_SIZE(cx88_vid_irqs),
				   status, mask);

	/* risc op code error */
	if (status & (1 << 16)) {
		printk(KERN_WARNING "%s/0: video risc op code error\n",core->name);
		cx_clear(MO_VID_DMACNTRL, 0x11);
		cx_clear(VID_CAPTURE_CONTROL, 0x06);
		cx88_sram_channel_dump(core, &cx88_sram_channels[SRAM_CH21]);
	}

	/* risc1 y */
	if (status & 0x01) {
		spin_lock(&dev->slock);
		count = cx_read(MO_VIDY_GPCNT);
		cx88_wakeup(core, &dev->vidq, count);
		spin_unlock(&dev->slock);
	}

	/* risc1 vbi */
	if (status & 0x08) {
		spin_lock(&dev->slock);
		count = cx_read(MO_VBI_GPCNT);
		cx88_wakeup(core, &dev->vbiq, count);
		spin_unlock(&dev->slock);
	}

	/* risc2 y */
	if (status & 0x10) {
		dprintk(2,"stopper video\n");
		spin_lock(&dev->slock);
		restart_video_queue(dev,&dev->vidq);
		spin_unlock(&dev->slock);
	}

	/* risc2 vbi */
	if (status & 0x80) {
		dprintk(2,"stopper vbi\n");
		spin_lock(&dev->slock);
		cx8800_restart_vbi_queue(dev,&dev->vbiq);
		spin_unlock(&dev->slock);
	}
}

static irqreturn_t cx8800_irq(int irq, void *dev_id)
{
	struct cx8800_dev *dev = dev_id;
	struct cx88_core *core = dev->core;
	u32 status;
	int loop, handled = 0;

	for (loop = 0; loop < 10; loop++) {
		status = cx_read(MO_PCI_INTSTAT) &
			(core->pci_irqmask | PCI_INT_VIDINT);
		if (0 == status)
			goto out;
		cx_write(MO_PCI_INTSTAT, status);
		handled = 1;

		if (status & core->pci_irqmask)
			cx88_core_irq(core,status);
		if (status & PCI_INT_VIDINT)
			cx8800_vid_irq(dev);
	}
	if (10 == loop) {
		printk(KERN_WARNING "%s/0: irq loop -- clearing mask\n",
		       core->name);
		cx_write(MO_PCI_INTMSK,0);
	}

 out:
	return IRQ_RETVAL(handled);
}

/* ----------------------------------------------------------- */
/* exported stuff                                              */

static const struct v4l2_file_operations video_fops =
{
	.owner	       = THIS_MODULE,
	.open	       = video_open,
	.release       = video_release,
	.read	       = video_read,
	.poll          = video_poll,
	.mmap	       = video_mmap,
	.unlocked_ioctl = video_ioctl2,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs       = vidioc_reqbufs,
	.vidioc_querybuf      = vidioc_querybuf,
	.vidioc_qbuf          = vidioc_qbuf,
	.vidioc_dqbuf         = vidioc_dqbuf,
	.vidioc_g_std         = vidioc_g_std,
	.vidioc_s_std         = vidioc_s_std,
	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
	.vidioc_streamon      = vidioc_streamon,
	.vidioc_streamoff     = vidioc_streamoff,
	.vidioc_g_tuner       = vidioc_g_tuner,
	.vidioc_s_tuner       = vidioc_s_tuner,
	.vidioc_g_frequency   = vidioc_g_frequency,
	.vidioc_s_frequency   = vidioc_s_frequency,
	.vidioc_subscribe_event      = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event    = v4l2_event_unsubscribe,
	.vidioc_g_chip_ident  = vidioc_g_chip_ident,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register    = vidioc_g_register,
	.vidioc_s_register    = vidioc_s_register,
#endif
};

static const struct video_device cx8800_video_template = {
	.name                 = "cx8800-video",
	.fops                 = &video_fops,
	.ioctl_ops 	      = &video_ioctl_ops,
	.tvnorms              = CX88_NORMS,
};

static const struct v4l2_ioctl_ops vbi_ioctl_ops = {
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_g_fmt_vbi_cap     = cx8800_vbi_fmt,
	.vidioc_try_fmt_vbi_cap   = cx8800_vbi_fmt,
	.vidioc_s_fmt_vbi_cap     = cx8800_vbi_fmt,
	.vidioc_reqbufs       = vidioc_reqbufs,
	.vidioc_querybuf      = vidioc_querybuf,
	.vidioc_qbuf          = vidioc_qbuf,
	.vidioc_dqbuf         = vidioc_dqbuf,
	.vidioc_g_std         = vidioc_g_std,
	.vidioc_s_std         = vidioc_s_std,
	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
	.vidioc_streamon      = vidioc_streamon,
	.vidioc_streamoff     = vidioc_streamoff,
	.vidioc_g_tuner       = vidioc_g_tuner,
	.vidioc_s_tuner       = vidioc_s_tuner,
	.vidioc_g_frequency   = vidioc_g_frequency,
	.vidioc_s_frequency   = vidioc_s_frequency,
	.vidioc_g_chip_ident  = vidioc_g_chip_ident,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register    = vidioc_g_register,
	.vidioc_s_register    = vidioc_s_register,
#endif
};

static const struct video_device cx8800_vbi_template = {
	.name                 = "cx8800-vbi",
	.fops                 = &video_fops,
	.ioctl_ops	      = &vbi_ioctl_ops,
	.tvnorms              = CX88_NORMS,
};

static const struct v4l2_file_operations radio_fops =
{
	.owner         = THIS_MODULE,
	.open          = video_open,
	.poll          = v4l2_ctrl_poll,
	.release       = video_release,
	.unlocked_ioctl = video_ioctl2,
};

static const struct v4l2_ioctl_ops radio_ioctl_ops = {
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_g_tuner       = radio_g_tuner,
	.vidioc_s_tuner       = radio_s_tuner,
	.vidioc_g_frequency   = vidioc_g_frequency,
	.vidioc_s_frequency   = vidioc_s_frequency,
	.vidioc_subscribe_event      = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event    = v4l2_event_unsubscribe,
	.vidioc_g_chip_ident  = vidioc_g_chip_ident,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register    = vidioc_g_register,
	.vidioc_s_register    = vidioc_s_register,
#endif
};

static const struct video_device cx8800_radio_template = {
	.name                 = "cx8800-radio",
	.fops                 = &radio_fops,
	.ioctl_ops 	      = &radio_ioctl_ops,
};

static const struct v4l2_ctrl_ops cx8800_ctrl_vid_ops = {
	.s_ctrl = cx8800_s_vid_ctrl,
};

static const struct v4l2_ctrl_ops cx8800_ctrl_aud_ops = {
	.s_ctrl = cx8800_s_aud_ctrl,
};

/* ----------------------------------------------------------- */

static void cx8800_unregister_video(struct cx8800_dev *dev)
{
	if (dev->radio_dev) {
		if (video_is_registered(dev->radio_dev))
			video_unregister_device(dev->radio_dev);
		else
			video_device_release(dev->radio_dev);
		dev->radio_dev = NULL;
	}
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
}

static int cx8800_initdev(struct pci_dev *pci_dev,
			  const struct pci_device_id *pci_id)
{
	struct cx8800_dev *dev;
	struct cx88_core *core;
	int err;
	int i;

	dev = kzalloc(sizeof(*dev),GFP_KERNEL);
	if (NULL == dev)
		return -ENOMEM;

	/* pci init */
	dev->pci = pci_dev;
	if (pci_enable_device(pci_dev)) {
		err = -EIO;
		goto fail_free;
	}
	core = cx88_core_get(dev->pci);
	if (NULL == core) {
		err = -EINVAL;
		goto fail_free;
	}
	dev->core = core;

	/* print pci info */
	dev->pci_rev = pci_dev->revision;
	pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER,  &dev->pci_lat);
	printk(KERN_INFO "%s/0: found at %s, rev: %d, irq: %d, "
	       "latency: %d, mmio: 0x%llx\n", core->name,
	       pci_name(pci_dev), dev->pci_rev, pci_dev->irq,
	       dev->pci_lat,(unsigned long long)pci_resource_start(pci_dev,0));

	pci_set_master(pci_dev);
	if (!pci_dma_supported(pci_dev,DMA_BIT_MASK(32))) {
		printk("%s/0: Oops: no 32bit PCI DMA ???\n",core->name);
		err = -EIO;
		goto fail_core;
	}

	/* initialize driver struct */
	spin_lock_init(&dev->slock);
	core->tvnorm = V4L2_STD_NTSC_M;

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	INIT_LIST_HEAD(&dev->vidq.queued);
	dev->vidq.timeout.function = cx8800_vid_timeout;
	dev->vidq.timeout.data     = (unsigned long)dev;
	init_timer(&dev->vidq.timeout);
	cx88_risc_stopper(dev->pci,&dev->vidq.stopper,
			  MO_VID_DMACNTRL,0x11,0x00);

	/* init vbi dma queues */
	INIT_LIST_HEAD(&dev->vbiq.active);
	INIT_LIST_HEAD(&dev->vbiq.queued);
	dev->vbiq.timeout.function = cx8800_vbi_timeout;
	dev->vbiq.timeout.data     = (unsigned long)dev;
	init_timer(&dev->vbiq.timeout);
	cx88_risc_stopper(dev->pci,&dev->vbiq.stopper,
			  MO_VID_DMACNTRL,0x88,0x00);

	/* get irq */
	err = request_irq(pci_dev->irq, cx8800_irq,
			  IRQF_SHARED | IRQF_DISABLED, core->name, dev);
	if (err < 0) {
		printk(KERN_ERR "%s/0: can't get IRQ %d\n",
		       core->name,pci_dev->irq);
		goto fail_core;
	}
	cx_set(MO_PCI_INTMSK, core->pci_irqmask);

	for (i = 0; i < CX8800_AUD_CTLS; i++) {
		const struct cx88_ctrl *cc = &cx8800_aud_ctls[i];
		struct v4l2_ctrl *vc;

		vc = v4l2_ctrl_new_std(&core->audio_hdl, &cx8800_ctrl_aud_ops,
			cc->id, cc->minimum, cc->maximum, cc->step, cc->default_value);
		if (vc == NULL) {
			err = core->audio_hdl.error;
			goto fail_core;
		}
		vc->priv = (void *)cc;
	}

	for (i = 0; i < CX8800_VID_CTLS; i++) {
		const struct cx88_ctrl *cc = &cx8800_vid_ctls[i];
		struct v4l2_ctrl *vc;

		vc = v4l2_ctrl_new_std(&core->video_hdl, &cx8800_ctrl_vid_ops,
			cc->id, cc->minimum, cc->maximum, cc->step, cc->default_value);
		if (vc == NULL) {
			err = core->video_hdl.error;
			goto fail_core;
		}
		vc->priv = (void *)cc;
		if (vc->id == V4L2_CID_CHROMA_AGC)
			core->chroma_agc = vc;
	}
	v4l2_ctrl_add_handler(&core->video_hdl, &core->audio_hdl, NULL);

	/* load and configure helper modules */

	if (core->board.audio_chip == V4L2_IDENT_WM8775) {
		struct i2c_board_info wm8775_info = {
			.type = "wm8775",
			.addr = 0x36 >> 1,
			.platform_data = &core->wm8775_data,
		};
		struct v4l2_subdev *sd;

		if (core->boardnr == CX88_BOARD_HAUPPAUGE_NOVASPLUS_S1)
			core->wm8775_data.is_nova_s = true;
		else
			core->wm8775_data.is_nova_s = false;

		sd = v4l2_i2c_new_subdev_board(&core->v4l2_dev, &core->i2c_adap,
				&wm8775_info, NULL);
		if (sd != NULL) {
			core->sd_wm8775 = sd;
			sd->grp_id = WM8775_GID;
		}
	}

	if (core->board.audio_chip == V4L2_IDENT_TVAUDIO) {
		/* This probes for a tda9874 as is used on some
		   Pixelview Ultra boards. */
		v4l2_i2c_new_subdev(&core->v4l2_dev, &core->i2c_adap,
				"tvaudio", 0, I2C_ADDRS(0xb0 >> 1));
	}

	switch (core->boardnr) {
	case CX88_BOARD_DVICO_FUSIONHDTV_5_GOLD:
	case CX88_BOARD_DVICO_FUSIONHDTV_7_GOLD: {
		static const struct i2c_board_info rtc_info = {
			I2C_BOARD_INFO("isl1208", 0x6f)
		};

		request_module("rtc-isl1208");
		core->i2c_rtc = i2c_new_device(&core->i2c_adap, &rtc_info);
	}
		/* break intentionally omitted */
	case CX88_BOARD_DVICO_FUSIONHDTV_5_PCI_NANO:
		request_module("ir-kbd-i2c");
	}

	/* Sets device info at pci_dev */
	pci_set_drvdata(pci_dev, dev);

	dev->width   = 320;
	dev->height  = 240;
	dev->fmt     = format_by_fourcc(V4L2_PIX_FMT_BGR24);

	/* initial device configuration */
	mutex_lock(&core->lock);
	cx88_set_tvnorm(core, core->tvnorm);
	v4l2_ctrl_handler_setup(&core->video_hdl);
	v4l2_ctrl_handler_setup(&core->audio_hdl);
	cx88_video_mux(core, 0);

	/* register v4l devices */
	dev->video_dev = cx88_vdev_init(core,dev->pci,
					&cx8800_video_template,"video");
	video_set_drvdata(dev->video_dev, dev);
	dev->video_dev->ctrl_handler = &core->video_hdl;
	err = video_register_device(dev->video_dev,VFL_TYPE_GRABBER,
				    video_nr[core->nr]);
	if (err < 0) {
		printk(KERN_ERR "%s/0: can't register video device\n",
		       core->name);
		goto fail_unreg;
	}
	printk(KERN_INFO "%s/0: registered device %s [v4l2]\n",
	       core->name, video_device_node_name(dev->video_dev));

	dev->vbi_dev = cx88_vdev_init(core,dev->pci,&cx8800_vbi_template,"vbi");
	video_set_drvdata(dev->vbi_dev, dev);
	err = video_register_device(dev->vbi_dev,VFL_TYPE_VBI,
				    vbi_nr[core->nr]);
	if (err < 0) {
		printk(KERN_ERR "%s/0: can't register vbi device\n",
		       core->name);
		goto fail_unreg;
	}
	printk(KERN_INFO "%s/0: registered device %s\n",
	       core->name, video_device_node_name(dev->vbi_dev));

	if (core->board.radio.type == CX88_RADIO) {
		dev->radio_dev = cx88_vdev_init(core,dev->pci,
						&cx8800_radio_template,"radio");
		video_set_drvdata(dev->radio_dev, dev);
		dev->radio_dev->ctrl_handler = &core->audio_hdl;
		err = video_register_device(dev->radio_dev,VFL_TYPE_RADIO,
					    radio_nr[core->nr]);
		if (err < 0) {
			printk(KERN_ERR "%s/0: can't register radio device\n",
			       core->name);
			goto fail_unreg;
		}
		printk(KERN_INFO "%s/0: registered device %s\n",
		       core->name, video_device_node_name(dev->radio_dev));
	}

	/* start tvaudio thread */
	if (core->board.tuner_type != TUNER_ABSENT) {
		core->kthread = kthread_run(cx88_audio_thread, core, "cx88 tvaudio");
		if (IS_ERR(core->kthread)) {
			err = PTR_ERR(core->kthread);
			printk(KERN_ERR "%s/0: failed to create cx88 audio thread, err=%d\n",
			       core->name, err);
		}
	}
	mutex_unlock(&core->lock);

	return 0;

fail_unreg:
	cx8800_unregister_video(dev);
	free_irq(pci_dev->irq, dev);
	mutex_unlock(&core->lock);
fail_core:
	cx88_core_put(core,dev->pci);
fail_free:
	kfree(dev);
	return err;
}

static void cx8800_finidev(struct pci_dev *pci_dev)
{
	struct cx8800_dev *dev = pci_get_drvdata(pci_dev);
	struct cx88_core *core = dev->core;

	/* stop thread */
	if (core->kthread) {
		kthread_stop(core->kthread);
		core->kthread = NULL;
	}

	if (core->ir)
		cx88_ir_stop(core);

	cx88_shutdown(core); /* FIXME */
	pci_disable_device(pci_dev);

	/* unregister stuff */

	free_irq(pci_dev->irq, dev);
	cx8800_unregister_video(dev);
	pci_set_drvdata(pci_dev, NULL);

	/* free memory */
	btcx_riscmem_free(dev->pci,&dev->vidq.stopper);
	cx88_core_put(core,dev->pci);
	kfree(dev);
}

#ifdef CONFIG_PM
static int cx8800_suspend(struct pci_dev *pci_dev, pm_message_t state)
{
	struct cx8800_dev *dev = pci_get_drvdata(pci_dev);
	struct cx88_core *core = dev->core;
	unsigned long flags;

	/* stop video+vbi capture */
	spin_lock_irqsave(&dev->slock, flags);
	if (!list_empty(&dev->vidq.active)) {
		printk("%s/0: suspend video\n", core->name);
		stop_video_dma(dev);
		del_timer(&dev->vidq.timeout);
	}
	if (!list_empty(&dev->vbiq.active)) {
		printk("%s/0: suspend vbi\n", core->name);
		cx8800_stop_vbi_dma(dev);
		del_timer(&dev->vbiq.timeout);
	}
	spin_unlock_irqrestore(&dev->slock, flags);

	if (core->ir)
		cx88_ir_stop(core);
	/* FIXME -- shutdown device */
	cx88_shutdown(core);

	pci_save_state(pci_dev);
	if (0 != pci_set_power_state(pci_dev, pci_choose_state(pci_dev, state))) {
		pci_disable_device(pci_dev);
		dev->state.disabled = 1;
	}
	return 0;
}

static int cx8800_resume(struct pci_dev *pci_dev)
{
	struct cx8800_dev *dev = pci_get_drvdata(pci_dev);
	struct cx88_core *core = dev->core;
	unsigned long flags;
	int err;

	if (dev->state.disabled) {
		err=pci_enable_device(pci_dev);
		if (err) {
			printk(KERN_ERR "%s/0: can't enable device\n",
			       core->name);
			return err;
		}

		dev->state.disabled = 0;
	}
	err= pci_set_power_state(pci_dev, PCI_D0);
	if (err) {
		printk(KERN_ERR "%s/0: can't set power state\n", core->name);
		pci_disable_device(pci_dev);
		dev->state.disabled = 1;

		return err;
	}
	pci_restore_state(pci_dev);

	/* FIXME: re-initialize hardware */
	cx88_reset(core);
	if (core->ir)
		cx88_ir_start(core);

	cx_set(MO_PCI_INTMSK, core->pci_irqmask);

	/* restart video+vbi capture */
	spin_lock_irqsave(&dev->slock, flags);
	if (!list_empty(&dev->vidq.active)) {
		printk("%s/0: resume video\n", core->name);
		restart_video_queue(dev,&dev->vidq);
	}
	if (!list_empty(&dev->vbiq.active)) {
		printk("%s/0: resume vbi\n", core->name);
		cx8800_restart_vbi_queue(dev,&dev->vbiq);
	}
	spin_unlock_irqrestore(&dev->slock, flags);

	return 0;
}
#endif

/* ----------------------------------------------------------- */

static const struct pci_device_id cx8800_pci_tbl[] = {
	{
		.vendor       = 0x14f1,
		.device       = 0x8800,
		.subvendor    = PCI_ANY_ID,
		.subdevice    = PCI_ANY_ID,
	},{
		/* --- end of list --- */
	}
};
MODULE_DEVICE_TABLE(pci, cx8800_pci_tbl);

static struct pci_driver cx8800_pci_driver = {
	.name     = "cx8800",
	.id_table = cx8800_pci_tbl,
	.probe    = cx8800_initdev,
	.remove   = cx8800_finidev,
#ifdef CONFIG_PM
	.suspend  = cx8800_suspend,
	.resume   = cx8800_resume,
#endif
};

static int __init cx8800_init(void)
{
	printk(KERN_INFO "cx88/0: cx2388x v4l2 driver version %s loaded\n",
	       CX88_VERSION);
	return pci_register_driver(&cx8800_pci_driver);
}

static void __exit cx8800_fini(void)
{
	pci_unregister_driver(&cx8800_pci_driver);
}

module_init(cx8800_init);
module_exit(cx8800_fini);

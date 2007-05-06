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
#include <linux/moduleparam.h>
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

#ifdef CONFIG_VIDEO_V4L1_COMPAT
/* Include V4L1 specific functions. Should be removed soon */
#include <linux/videodev.h>
#endif

MODULE_DESCRIPTION("v4l2 driver module for cx2388x based TV cards");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

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

static unsigned int video_debug = 0;
module_param(video_debug,int,0644);
MODULE_PARM_DESC(video_debug,"enable debug messages [video]");

static unsigned int irq_debug = 0;
module_param(irq_debug,int,0644);
MODULE_PARM_DESC(irq_debug,"enable debug messages [IRQ handler]");

static unsigned int vid_limit = 16;
module_param(vid_limit,int,0644);
MODULE_PARM_DESC(vid_limit,"capture memory limit in megabytes");

#define dprintk(level,fmt, arg...)	if (video_debug >= level) \
	printk(KERN_DEBUG "%s/0: " fmt, core->name , ## arg)

/* ------------------------------------------------------------------ */

static LIST_HEAD(cx8800_devlist);

/* ------------------------------------------------------------------- */
/* static data                                                         */

static struct cx8800_fmt formats[] = {
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

static struct cx8800_fmt* format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (formats[i].fourcc == fourcc)
			return formats+i;
	return NULL;
}

/* ------------------------------------------------------------------- */

static const struct v4l2_queryctrl no_ctl = {
	.name  = "42",
	.flags = V4L2_CTRL_FLAG_DISABLED,
};

static struct cx88_ctrl cx8800_ctls[] = {
	/* --- video --- */
	{
		.v = {
			.id            = V4L2_CID_BRIGHTNESS,
			.name          = "Brightness",
			.minimum       = 0x00,
			.maximum       = 0xff,
			.step          = 1,
			.default_value = 0x7f,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.off                   = 128,
		.reg                   = MO_CONTR_BRIGHT,
		.mask                  = 0x00ff,
		.shift                 = 0,
	},{
		.v = {
			.id            = V4L2_CID_CONTRAST,
			.name          = "Contrast",
			.minimum       = 0,
			.maximum       = 0xff,
			.step          = 1,
			.default_value = 0x3f,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.off                   = 0,
		.reg                   = MO_CONTR_BRIGHT,
		.mask                  = 0xff00,
		.shift                 = 8,
	},{
		.v = {
			.id            = V4L2_CID_HUE,
			.name          = "Hue",
			.minimum       = 0,
			.maximum       = 0xff,
			.step          = 1,
			.default_value = 0x7f,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.off                   = 128,
		.reg                   = MO_HUE,
		.mask                  = 0x00ff,
		.shift                 = 0,
	},{
		/* strictly, this only describes only U saturation.
		 * V saturation is handled specially through code.
		 */
		.v = {
			.id            = V4L2_CID_SATURATION,
			.name          = "Saturation",
			.minimum       = 0,
			.maximum       = 0xff,
			.step          = 1,
			.default_value = 0x7f,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.off                   = 0,
		.reg                   = MO_UV_SATURATION,
		.mask                  = 0x00ff,
		.shift                 = 0,
	},{
	/* --- audio --- */
		.v = {
			.id            = V4L2_CID_AUDIO_MUTE,
			.name          = "Mute",
			.minimum       = 0,
			.maximum       = 1,
			.default_value = 1,
			.type          = V4L2_CTRL_TYPE_BOOLEAN,
		},
		.reg                   = AUD_VOL_CTL,
		.sreg                  = SHADOW_AUD_VOL_CTL,
		.mask                  = (1 << 6),
		.shift                 = 6,
	},{
		.v = {
			.id            = V4L2_CID_AUDIO_VOLUME,
			.name          = "Volume",
			.minimum       = 0,
			.maximum       = 0x3f,
			.step          = 1,
			.default_value = 0x3f,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.reg                   = AUD_VOL_CTL,
		.sreg                  = SHADOW_AUD_VOL_CTL,
		.mask                  = 0x3f,
		.shift                 = 0,
	},{
		.v = {
			.id            = V4L2_CID_AUDIO_BALANCE,
			.name          = "Balance",
			.minimum       = 0,
			.maximum       = 0x7f,
			.step          = 1,
			.default_value = 0x40,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.reg                   = AUD_BAL_CTL,
		.sreg                  = SHADOW_AUD_BAL_CTL,
		.mask                  = 0x7f,
		.shift                 = 0,
	}
};
static const int CX8800_CTLS = ARRAY_SIZE(cx8800_ctls);

const u32 cx88_user_ctrls[] = {
	V4L2_CID_USER_CLASS,
	V4L2_CID_BRIGHTNESS,
	V4L2_CID_CONTRAST,
	V4L2_CID_SATURATION,
	V4L2_CID_HUE,
	V4L2_CID_AUDIO_VOLUME,
	V4L2_CID_AUDIO_BALANCE,
	V4L2_CID_AUDIO_MUTE,
	0
};
EXPORT_SYMBOL(cx88_user_ctrls);

static const u32 *ctrl_classes[] = {
	cx88_user_ctrls,
	NULL
};

int cx8800_ctrl_query(struct v4l2_queryctrl *qctrl)
{
	int i;

	if (qctrl->id < V4L2_CID_BASE ||
	    qctrl->id >= V4L2_CID_LASTP1)
		return -EINVAL;
	for (i = 0; i < CX8800_CTLS; i++)
		if (cx8800_ctls[i].v.id == qctrl->id)
			break;
	if (i == CX8800_CTLS) {
		*qctrl = no_ctl;
		return 0;
	}
	*qctrl = cx8800_ctls[i].v;
	return 0;
}
EXPORT_SYMBOL(cx8800_ctrl_query);

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
		input, INPUT(input)->vmux,
		INPUT(input)->gpio0,INPUT(input)->gpio1,
		INPUT(input)->gpio2,INPUT(input)->gpio3);
	core->input = input;
	cx_andor(MO_INPUT_FORMAT, 0x03 << 14, INPUT(input)->vmux << 14);
	cx_write(MO_GP3_IO, INPUT(input)->gpio3);
	cx_write(MO_GP0_IO, INPUT(input)->gpio0);
	cx_write(MO_GP1_IO, INPUT(input)->gpio1);
	cx_write(MO_GP2_IO, INPUT(input)->gpio2);

	switch (INPUT(input)->type) {
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

	if (cx88_boards[core->board].mpeg & CX88_MPEG_BLACKBIRD) {
		/* sets sound input from external adc */
		if (INPUT(input)->extadc)
			cx_set(AUD_CTL, EN_I2SIN_ENABLE);
		else
			cx_clear(AUD_CTL, EN_I2SIN_ENABLE);
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
	cx_set(MO_PCI_INTMSK, core->pci_irqmask | 0x01);

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
	cx_clear(MO_PCI_INTMSK, 0x000001);
	cx_clear(MO_VID_INTMSK, 0x0f0011);
	return 0;
}
#endif

static int restart_video_queue(struct cx8800_dev    *dev,
			       struct cx88_dmaqueue *q)
{
	struct cx88_core *core = dev->core;
	struct cx88_buffer *buf, *prev;
	struct list_head *item;

	if (!list_empty(&q->active)) {
		buf = list_entry(q->active.next, struct cx88_buffer, vb.queue);
		dprintk(2,"restart_queue [%p/%d]: restart dma\n",
			buf, buf->vb.i);
		start_video_dma(dev, q, buf);
		list_for_each(item,&q->active) {
			buf = list_entry(item, struct cx88_buffer, vb.queue);
			buf->count    = q->count++;
		}
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
			buf->vb.state = STATE_ACTIVE;
			buf->count    = q->count++;
			mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
			dprintk(2,"[%p/%d] restart_queue - first active\n",
				buf,buf->vb.i);

		} else if (prev->vb.width  == buf->vb.width  &&
			   prev->vb.height == buf->vb.height &&
			   prev->fmt       == buf->fmt) {
			list_move_tail(&buf->vb.queue, &q->active);
			buf->vb.state = STATE_ACTIVE;
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

	*size = fh->fmt->depth*fh->width*fh->height >> 3;
	if (0 == *count)
		*count = 32;
	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;
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
	int rc, init_buffer = 0;

	BUG_ON(NULL == fh->fmt);
	if (fh->width  < 48 || fh->width  > norm_maxw(core->tvnorm) ||
	    fh->height < 32 || fh->height > norm_maxh(core->tvnorm))
		return -EINVAL;
	buf->vb.size = (fh->width * fh->height * fh->fmt->depth) >> 3;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	if (buf->fmt       != fh->fmt    ||
	    buf->vb.width  != fh->width  ||
	    buf->vb.height != fh->height ||
	    buf->vb.field  != field) {
		buf->fmt       = fh->fmt;
		buf->vb.width  = fh->width;
		buf->vb.height = fh->height;
		buf->vb.field  = field;
		init_buffer = 1;
	}

	if (STATE_NEEDS_INIT == buf->vb.state) {
		init_buffer = 1;
		if (0 != (rc = videobuf_iolock(q,&buf->vb,NULL)))
			goto fail;
	}

	if (init_buffer) {
		buf->bpl = buf->vb.width * buf->fmt->depth >> 3;
		switch (buf->vb.field) {
		case V4L2_FIELD_TOP:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist, 0, UNSET,
					 buf->bpl, 0, buf->vb.height);
			break;
		case V4L2_FIELD_BOTTOM:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist, UNSET, 0,
					 buf->bpl, 0, buf->vb.height);
			break;
		case V4L2_FIELD_INTERLACED:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist, 0, buf->bpl,
					 buf->bpl, buf->bpl,
					 buf->vb.height >> 1);
			break;
		case V4L2_FIELD_SEQ_TB:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist,
					 0, buf->bpl * (buf->vb.height >> 1),
					 buf->bpl, 0,
					 buf->vb.height >> 1);
			break;
		case V4L2_FIELD_SEQ_BT:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist,
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
		fh->width, fh->height, fh->fmt->depth, fh->fmt->name,
		(unsigned long)buf->risc.dma);

	buf->vb.state = STATE_PREPARED;
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
		buf->vb.state = STATE_QUEUED;
		dprintk(2,"[%p/%d] buffer_queue - append to queued\n",
			buf, buf->vb.i);

	} else if (list_empty(&q->active)) {
		list_add_tail(&buf->vb.queue,&q->active);
		start_video_dma(dev, q, buf);
		buf->vb.state = STATE_ACTIVE;
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
			buf->vb.state = STATE_ACTIVE;
			buf->count    = q->count++;
			prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
			dprintk(2,"[%p/%d] buffer_queue - append to active\n",
				buf, buf->vb.i);

		} else {
			list_add_tail(&buf->vb.queue,&q->queued);
			buf->vb.state = STATE_QUEUED;
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

static struct videobuf_queue_ops cx8800_video_qops = {
	.buf_setup    = buffer_setup,
	.buf_prepare  = buffer_prepare,
	.buf_queue    = buffer_queue,
	.buf_release  = buffer_release,
};

/* ------------------------------------------------------------------ */


/* ------------------------------------------------------------------ */

static struct videobuf_queue* get_queue(struct cx8800_fh *fh)
{
	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &fh->vidq;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return &fh->vbiq;
	default:
		BUG();
		return NULL;
	}
}

static int get_ressource(struct cx8800_fh *fh)
{
	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return RESOURCE_VIDEO;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return RESOURCE_VBI;
	default:
		BUG();
		return 0;
	}
}

static int video_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct cx8800_dev *h,*dev = NULL;
	struct cx88_core *core;
	struct cx8800_fh *fh;
	struct list_head *list;
	enum v4l2_buf_type type = 0;
	int radio = 0;

	list_for_each(list,&cx8800_devlist) {
		h = list_entry(list, struct cx8800_dev, devlist);
		if (h->video_dev->minor == minor) {
			dev  = h;
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		}
		if (h->vbi_dev->minor == minor) {
			dev  = h;
			type = V4L2_BUF_TYPE_VBI_CAPTURE;
		}
		if (h->radio_dev &&
		    h->radio_dev->minor == minor) {
			radio = 1;
			dev   = h;
		}
	}
	if (NULL == dev)
		return -ENODEV;

	core = dev->core;

	dprintk(1,"open minor=%d radio=%d type=%s\n",
		minor,radio,v4l2_type_names[type]);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh),GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;
	file->private_data = fh;
	fh->dev      = dev;
	fh->radio    = radio;
	fh->type     = type;
	fh->width    = 320;
	fh->height   = 240;
	fh->fmt      = format_by_fourcc(V4L2_PIX_FMT_BGR24);

	videobuf_queue_init(&fh->vidq, &cx8800_video_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_INTERLACED,
			    sizeof(struct cx88_buffer),
			    fh);
	videobuf_queue_init(&fh->vbiq, &cx8800_vbi_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VBI_CAPTURE,
			    V4L2_FIELD_SEQ_TB,
			    sizeof(struct cx88_buffer),
			    fh);

	if (fh->radio) {
		int board = core->board;
		dprintk(1,"video_open: setting radio device\n");
		cx_write(MO_GP3_IO, cx88_boards[board].radio.gpio3);
		cx_write(MO_GP0_IO, cx88_boards[board].radio.gpio0);
		cx_write(MO_GP1_IO, cx88_boards[board].radio.gpio1);
		cx_write(MO_GP2_IO, cx88_boards[board].radio.gpio2);
		core->tvaudio = WW_FM;
		cx88_set_tvaudio(core);
		cx88_set_stereo(core,V4L2_TUNER_MODE_STEREO,1);
		cx88_call_i2c_clients(core,AUDC_SET_RADIO,NULL);
	}

	return 0;
}

static ssize_t
video_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct cx8800_fh *fh = file->private_data;

	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (res_locked(fh->dev,RESOURCE_VIDEO))
			return -EBUSY;
		return videobuf_read_one(&fh->vidq, data, count, ppos,
					 file->f_flags & O_NONBLOCK);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
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
	struct cx8800_fh *fh = file->private_data;
	struct cx88_buffer *buf;

	if (V4L2_BUF_TYPE_VBI_CAPTURE == fh->type) {
		if (!res_get(fh->dev,fh,RESOURCE_VBI))
			return POLLERR;
		return videobuf_poll_stream(file, &fh->vbiq, wait);
	}

	if (res_check(fh,RESOURCE_VIDEO)) {
		/* streaming capture */
		if (list_empty(&fh->vidq.stream))
			return POLLERR;
		buf = list_entry(fh->vidq.stream.next,struct cx88_buffer,vb.stream);
	} else {
		/* read() capture */
		buf = (struct cx88_buffer*)fh->vidq.read_buf;
		if (NULL == buf)
			return POLLERR;
	}
	poll_wait(file, &buf->vb.done, wait);
	if (buf->vb.state == STATE_DONE ||
	    buf->vb.state == STATE_ERROR)
		return POLLIN|POLLRDNORM;
	return 0;
}

static int video_release(struct inode *inode, struct file *file)
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
		if (fh->vbiq.streaming)
			videobuf_streamoff(&fh->vbiq);
		if (fh->vbiq.reading)
			videobuf_read_stop(&fh->vbiq);
		res_free(dev,fh,RESOURCE_VBI);
	}

	videobuf_mmap_free(&fh->vidq);
	videobuf_mmap_free(&fh->vbiq);
	file->private_data = NULL;
	kfree(fh);

	cx88_call_i2c_clients (dev->core, TUNER_SET_STANDBY, NULL);

	return 0;
}

static int
video_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct cx8800_fh *fh = file->private_data;

	return videobuf_mmap_mapper(get_queue(fh), vma);
}

/* ------------------------------------------------------------------ */
/* VIDEO CTRL IOCTLS                                                  */

int cx88_get_control (struct cx88_core  *core, struct v4l2_control *ctl)
{
	struct cx88_ctrl  *c    = NULL;
	u32 value;
	int i;

	for (i = 0; i < CX8800_CTLS; i++)
		if (cx8800_ctls[i].v.id == ctl->id)
			c = &cx8800_ctls[i];
	if (unlikely(NULL == c))
		return -EINVAL;

	value = c->sreg ? cx_sread(c->sreg) : cx_read(c->reg);
	switch (ctl->id) {
	case V4L2_CID_AUDIO_BALANCE:
		ctl->value = ((value & 0x7f) < 0x40) ? ((value & 0x7f) + 0x40)
					: (0x7f - (value & 0x7f));
		break;
	case V4L2_CID_AUDIO_VOLUME:
		ctl->value = 0x3f - (value & 0x3f);
		break;
	default:
		ctl->value = ((value + (c->off << c->shift)) & c->mask) >> c->shift;
		break;
	}
	dprintk(1,"get_control id=0x%X(%s) ctrl=0x%02x, reg=0x%02x val=0x%02x (mask 0x%02x)%s\n",
				ctl->id, c->v.name, ctl->value, c->reg,
				value,c->mask, c->sreg ? " [shadowed]" : "");
	return 0;
}
EXPORT_SYMBOL(cx88_get_control);

int cx88_set_control(struct cx88_core *core, struct v4l2_control *ctl)
{
	struct cx88_ctrl *c = NULL;
	u32 value,mask;
	int i;

	for (i = 0; i < CX8800_CTLS; i++) {
		if (cx8800_ctls[i].v.id == ctl->id) {
			c = &cx8800_ctls[i];
		}
	}
	if (unlikely(NULL == c))
		return -EINVAL;

	if (ctl->value < c->v.minimum)
		ctl->value = c->v.minimum;
	if (ctl->value > c->v.maximum)
		ctl->value = c->v.maximum;
	mask=c->mask;
	switch (ctl->id) {
	case V4L2_CID_AUDIO_BALANCE:
		value = (ctl->value < 0x40) ? (0x7f - ctl->value) : (ctl->value - 0x40);
		break;
	case V4L2_CID_AUDIO_VOLUME:
		value = 0x3f - (ctl->value & 0x3f);
		break;
	case V4L2_CID_SATURATION:
		/* special v_sat handling */

		value = ((ctl->value - c->off) << c->shift) & c->mask;

		if (core->tvnorm & V4L2_STD_SECAM) {
			/* For SECAM, both U and V sat should be equal */
			value=value<<8|value;
		} else {
			/* Keeps U Saturation proportional to V Sat */
			value=(value*0x5a)/0x7f<<8|value;
		}
		mask=0xffff;
		break;
	default:
		value = ((ctl->value - c->off) << c->shift) & c->mask;
		break;
	}
	dprintk(1,"set_control id=0x%X(%s) ctrl=0x%02x, reg=0x%02x val=0x%02x (mask 0x%02x)%s\n",
				ctl->id, c->v.name, ctl->value, c->reg, value,
				mask, c->sreg ? " [shadowed]" : "");
	if (c->sreg) {
		cx_sandor(c->sreg, c->reg, mask, value);
	} else {
		cx_andor(c->reg, mask, value);
	}
	return 0;
}
EXPORT_SYMBOL(cx88_set_control);

static void init_controls(struct cx88_core *core)
{
	struct v4l2_control ctrl;
	int i;

	for (i = 0; i < CX8800_CTLS; i++) {
		ctrl.id=cx8800_ctls[i].v.id;
		ctrl.value=cx8800_ctls[i].v.default_value;

		cx88_set_control(core, &ctrl);
	}
}

/* ------------------------------------------------------------------ */
/* VIDEO IOCTLS                                                       */

static int vidioc_g_fmt_cap (struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct cx8800_fh  *fh   = priv;

	f->fmt.pix.width        = fh->width;
	f->fmt.pix.height       = fh->height;
	f->fmt.pix.field        = fh->vidq.field;
	f->fmt.pix.pixelformat  = fh->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fh->fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	return 0;
}

static int vidioc_try_fmt_cap (struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;
	struct cx8800_fmt *fmt;
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
	if (f->fmt.pix.height < 32)
		f->fmt.pix.height = 32;
	if (f->fmt.pix.height > maxh)
		f->fmt.pix.height = maxh;
	if (f->fmt.pix.width < 48)
		f->fmt.pix.width = 48;
	if (f->fmt.pix.width > maxw)
		f->fmt.pix.width = maxw;
	f->fmt.pix.width &= ~0x03;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

static int vidioc_s_fmt_cap (struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct cx8800_fh  *fh   = priv;
	int err = vidioc_try_fmt_cap (file,priv,f);

	if (0 != err)
		return err;
	fh->fmt        = format_by_fourcc(f->fmt.pix.pixelformat);
	fh->width      = f->fmt.pix.width;
	fh->height     = f->fmt.pix.height;
	fh->vidq.field = f->fmt.pix.field;
	return 0;
}

static int vidioc_querycap (struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct cx8800_dev *dev  = ((struct cx8800_fh *)priv)->dev;
	struct cx88_core  *core = dev->core;

	strcpy(cap->driver, "cx8800");
	strlcpy(cap->card, cx88_boards[core->board].name,
		sizeof(cap->card));
	sprintf(cap->bus_info,"PCI:%s",pci_name(dev->pci));
	cap->version = CX88_VERSION_CODE;
	cap->capabilities =
		V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_READWRITE     |
		V4L2_CAP_STREAMING     |
		V4L2_CAP_VBI_CAPTURE;
	if (UNSET != core->tuner_type)
		cap->capabilities |= V4L2_CAP_TUNER;
	return 0;
}

static int vidioc_enum_fmt_cap (struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (unlikely(f->index >= ARRAY_SIZE(formats)))
		return -EINVAL;

	strlcpy(f->description,formats[f->index].name,sizeof(f->description));
	f->pixelformat = formats[f->index].fourcc;

	return 0;
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf (struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct cx8800_fh           *fh   = priv;
	struct videobuf_queue      *q;
	struct v4l2_requestbuffers req;
	unsigned int i;
	int err;

	q = get_queue(fh);
	memset(&req,0,sizeof(req));
	req.type   = q->type;
	req.count  = 8;
	req.memory = V4L2_MEMORY_MMAP;
	err = videobuf_reqbufs(q,&req);
	if (err < 0)
		return err;

	mbuf->frames = req.count;
	mbuf->size   = 0;
	for (i = 0; i < mbuf->frames; i++) {
		mbuf->offsets[i]  = q->bufs[i]->boff;
		mbuf->size       += q->bufs[i]->bsize;
	}
	return 0;
}
#endif

static int vidioc_reqbufs (struct file *file, void *priv, struct v4l2_requestbuffers *p)
{
	struct cx8800_fh  *fh   = priv;
	return (videobuf_reqbufs(get_queue(fh), p));
}

static int vidioc_querybuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct cx8800_fh  *fh   = priv;
	return (videobuf_querybuf(get_queue(fh), p));
}

static int vidioc_qbuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct cx8800_fh  *fh   = priv;
	return (videobuf_qbuf(get_queue(fh), p));
}

static int vidioc_dqbuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct cx8800_fh  *fh   = priv;
	return (videobuf_dqbuf(get_queue(fh), p,
				file->f_flags & O_NONBLOCK));
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct cx8800_fh  *fh   = priv;
	struct cx8800_dev *dev  = fh->dev;

	if (unlikely(fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE))
		return -EINVAL;
	if (unlikely(i != fh->type))
		return -EINVAL;

	if (unlikely(!res_get(dev,fh,get_ressource(fh))))
		return -EBUSY;
	return videobuf_streamon(get_queue(fh));
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct cx8800_fh  *fh   = priv;
	struct cx8800_dev *dev  = fh->dev;
	int               err, res;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	res = get_ressource(fh);
	err = videobuf_streamoff(get_queue(fh));
	if (err < 0)
		return err;
	res_free(dev,fh,res);
	return 0;
}

static int vidioc_s_std (struct file *file, void *priv, v4l2_std_id *tvnorms)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;

	mutex_lock(&core->lock);
	cx88_set_tvnorm(core,*tvnorms);
	mutex_unlock(&core->lock);

	return 0;
}

/* only one input in this sample driver */
int cx88_enum_input (struct cx88_core  *core,struct v4l2_input *i)
{
	static const char *iname[] = {
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
	unsigned int n;

	n = i->index;
	if (n >= 4)
		return -EINVAL;
	if (0 == INPUT(n)->type)
		return -EINVAL;
	memset(i,0,sizeof(*i));
	i->index = n;
	i->type  = V4L2_INPUT_TYPE_CAMERA;
	strcpy(i->name,iname[INPUT(n)->type]);
	if ((CX88_VMUX_TELEVISION == INPUT(n)->type) ||
		(CX88_VMUX_CABLE      == INPUT(n)->type))
		i->type = V4L2_INPUT_TYPE_TUNER;
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

	mutex_lock(&core->lock);
	cx88_newstation(core);
	cx88_video_mux(core,i);
	mutex_unlock(&core->lock);
	return 0;
}



static int vidioc_queryctrl (struct file *file, void *priv,
				struct v4l2_queryctrl *qctrl)
{
	qctrl->id = v4l2_ctrl_next(ctrl_classes, qctrl->id);
	if (unlikely(qctrl->id == 0))
		return -EINVAL;
	return cx8800_ctrl_query(qctrl);
}

static int vidioc_g_ctrl (struct file *file, void *priv,
				struct v4l2_control *ctl)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;
	return
		cx88_get_control(core,ctl);
}

static int vidioc_s_ctrl (struct file *file, void *priv,
				struct v4l2_control *ctl)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;
	return
		cx88_set_control(core,ctl);
}

static int vidioc_g_tuner (struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;
	u32 reg;

	if (unlikely(UNSET == core->tuner_type))
		return -EINVAL;
	if (0 != t->index)
		return -EINVAL;

	strcpy(t->name, "Television");
	t->type       = V4L2_TUNER_ANALOG_TV;
	t->capability = V4L2_TUNER_CAP_NORM;
	t->rangehigh  = 0xffffffffUL;

	cx88_get_stereo(core ,t);
	reg = cx_read(MO_DEVICE_STATUS);
	t->signal = (reg & (1<<5)) ? 0xffff : 0x0000;
	return 0;
}

static int vidioc_s_tuner (struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;

	if (UNSET == core->tuner_type)
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

	if (unlikely(UNSET == core->tuner_type))
		return -EINVAL;

	/* f->type = fh->radio ? V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV; */
	f->type = fh->radio ? V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
	f->frequency = core->freq;

	cx88_call_i2c_clients(core,VIDIOC_G_FREQUENCY,f);

	return 0;
}

int cx88_set_freq (struct cx88_core  *core,
				struct v4l2_frequency *f)
{
	if (unlikely(UNSET == core->tuner_type))
		return -EINVAL;
	if (unlikely(f->tuner != 0))
		return -EINVAL;

	mutex_lock(&core->lock);
	core->freq = f->frequency;
	cx88_newstation(core);
	cx88_call_i2c_clients(core,VIDIOC_S_FREQUENCY,f);

	/* When changing channels it is required to reset TVAUDIO */
	msleep (10);
	cx88_set_tvaudio(core);

	mutex_unlock(&core->lock);

	return 0;
}
EXPORT_SYMBOL(cx88_set_freq);

static int vidioc_s_frequency (struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct cx8800_fh  *fh   = priv;
	struct cx88_core  *core = fh->dev->core;

	if (unlikely(0 == fh->radio && f->type != V4L2_TUNER_ANALOG_TV))
		return -EINVAL;
	if (unlikely(1 == fh->radio && f->type != V4L2_TUNER_RADIO))
		return -EINVAL;

	return
		cx88_set_freq (core,f);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vidioc_g_register (struct file *file, void *fh,
				struct v4l2_register *reg)
{
	struct cx88_core *core = ((struct cx8800_fh*)fh)->dev->core;

	if (!v4l2_chip_match_host(reg->match_type, reg->match_chip))
		return -EINVAL;
	/* cx2388x has a 24-bit register space */
	reg->val = cx_read(reg->reg&0xffffff);
	return 0;
}

static int vidioc_s_register (struct file *file, void *fh,
				struct v4l2_register *reg)
{
	struct cx88_core *core = ((struct cx8800_fh*)fh)->dev->core;

	if (!v4l2_chip_match_host(reg->match_type, reg->match_chip))
		return -EINVAL;
	cx_write(reg->reg&0xffffff, reg->val);
	return 0;
}
#endif

/* ----------------------------------------------------------- */
/* RADIO ESPECIFIC IOCTLS                                      */
/* ----------------------------------------------------------- */

static int radio_querycap (struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct cx8800_dev *dev  = ((struct cx8800_fh *)priv)->dev;
	struct cx88_core  *core = dev->core;

	strcpy(cap->driver, "cx8800");
	strlcpy(cap->card, cx88_boards[core->board].name,
		sizeof(cap->card));
	sprintf(cap->bus_info,"PCI:%s", pci_name(dev->pci));
	cap->version = CX88_VERSION_CODE;
	cap->capabilities = V4L2_CAP_TUNER;
	return 0;
}

static int radio_g_tuner (struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;

	if (unlikely(t->index > 0))
		return -EINVAL;

	strcpy(t->name, "Radio");
	t->type = V4L2_TUNER_RADIO;

	cx88_call_i2c_clients(core,VIDIOC_G_TUNER,t);
	return 0;
}

static int radio_enum_input (struct file *file, void *priv,
				struct v4l2_input *i)
{
	if (i->index != 0)
		return -EINVAL;
	strcpy(i->name,"Radio");
	i->type = V4L2_INPUT_TYPE_TUNER;

	return 0;
}

static int radio_g_audio (struct file *file, void *priv, struct v4l2_audio *a)
{
	if (unlikely(a->index))
		return -EINVAL;

	memset(a,0,sizeof(*a));
	strcpy(a->name,"Radio");
	return 0;
}

/* FIXME: Should add a standard for radio */

static int radio_s_tuner (struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct cx88_core  *core = ((struct cx8800_fh *)priv)->dev->core;

	if (0 != t->index)
		return -EINVAL;

	cx88_call_i2c_clients(core,VIDIOC_S_TUNER,t);

	return 0;
}

static int radio_s_audio (struct file *file, void *fh,
			  struct v4l2_audio *a)
{
	return 0;
}

static int radio_s_input (struct file *file, void *fh, unsigned int i)
{
	return 0;
}

static int radio_queryctrl (struct file *file, void *priv,
			    struct v4l2_queryctrl *c)
{
	int i;

	if (c->id <  V4L2_CID_BASE ||
		c->id >= V4L2_CID_LASTP1)
		return -EINVAL;
	if (c->id == V4L2_CID_AUDIO_MUTE) {
		for (i = 0; i < CX8800_CTLS; i++)
			if (cx8800_ctls[i].v.id == c->id)
				break;
		*c = cx8800_ctls[i].v;
	} else
		*c = no_ctl;
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
		buf->vb.state = STATE_ERROR;
		wake_up(&buf->vb.done);
		printk("%s/0: [%p/%d] timeout - dma=0x%08lx\n", core->name,
		       buf, buf->vb.i, (unsigned long)buf->risc.dma);
	}
	restart_video_queue(dev,q);
	spin_unlock_irqrestore(&dev->slock,flags);
}

static char *cx88_vid_irqs[32] = {
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
		status = cx_read(MO_PCI_INTSTAT) & (core->pci_irqmask | 0x01);
		if (0 == status)
			goto out;
		cx_write(MO_PCI_INTSTAT, status);
		handled = 1;

		if (status & core->pci_irqmask)
			cx88_core_irq(core,status);
		if (status & 0x01)
			cx8800_vid_irq(dev);
	};
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

static const struct file_operations video_fops =
{
	.owner	       = THIS_MODULE,
	.open	       = video_open,
	.release       = video_release,
	.read	       = video_read,
	.poll          = video_poll,
	.mmap	       = video_mmap,
	.ioctl	       = video_ioctl2,
	.compat_ioctl  = v4l_compat_ioctl32,
	.llseek        = no_llseek,
};

static struct video_device cx8800_vbi_template;
static struct video_device cx8800_video_template =
{
	.name                 = "cx8800-video",
	.type                 = VID_TYPE_CAPTURE|VID_TYPE_TUNER|VID_TYPE_SCALES,
	.fops                 = &video_fops,
	.minor                = -1,
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_cap  = vidioc_enum_fmt_cap,
	.vidioc_g_fmt_cap     = vidioc_g_fmt_cap,
	.vidioc_try_fmt_cap   = vidioc_try_fmt_cap,
	.vidioc_s_fmt_cap     = vidioc_s_fmt_cap,
	.vidioc_g_fmt_vbi     = cx8800_vbi_fmt,
	.vidioc_try_fmt_vbi   = cx8800_vbi_fmt,
	.vidioc_s_fmt_vbi     = cx8800_vbi_fmt,
	.vidioc_reqbufs       = vidioc_reqbufs,
	.vidioc_querybuf      = vidioc_querybuf,
	.vidioc_qbuf          = vidioc_qbuf,
	.vidioc_dqbuf         = vidioc_dqbuf,
	.vidioc_s_std         = vidioc_s_std,
	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
	.vidioc_queryctrl     = vidioc_queryctrl,
	.vidioc_g_ctrl        = vidioc_g_ctrl,
	.vidioc_s_ctrl        = vidioc_s_ctrl,
	.vidioc_streamon      = vidioc_streamon,
	.vidioc_streamoff     = vidioc_streamoff,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = vidiocgmbuf,
#endif
	.vidioc_g_tuner       = vidioc_g_tuner,
	.vidioc_s_tuner       = vidioc_s_tuner,
	.vidioc_g_frequency   = vidioc_g_frequency,
	.vidioc_s_frequency   = vidioc_s_frequency,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register    = vidioc_g_register,
	.vidioc_s_register    = vidioc_s_register,
#endif
	.tvnorms              = CX88_NORMS,
	.current_norm         = V4L2_STD_NTSC_M,
};

static const struct file_operations radio_fops =
{
	.owner         = THIS_MODULE,
	.open          = video_open,
	.release       = video_release,
	.ioctl         = video_ioctl2,
	.compat_ioctl  = v4l_compat_ioctl32,
	.llseek        = no_llseek,
};

static struct video_device cx8800_radio_template =
{
	.name                 = "cx8800-radio",
	.type                 = VID_TYPE_TUNER,
	.hardware             = 0,
	.fops                 = &radio_fops,
	.minor                = -1,
	.vidioc_querycap      = radio_querycap,
	.vidioc_g_tuner       = radio_g_tuner,
	.vidioc_enum_input    = radio_enum_input,
	.vidioc_g_audio       = radio_g_audio,
	.vidioc_s_tuner       = radio_s_tuner,
	.vidioc_s_audio       = radio_s_audio,
	.vidioc_s_input       = radio_s_input,
	.vidioc_queryctrl     = radio_queryctrl,
	.vidioc_g_ctrl        = vidioc_g_ctrl,
	.vidioc_s_ctrl        = vidioc_s_ctrl,
	.vidioc_g_frequency   = vidioc_g_frequency,
	.vidioc_s_frequency   = vidioc_s_frequency,
};

/* ----------------------------------------------------------- */

static void cx8800_unregister_video(struct cx8800_dev *dev)
{
	if (dev->radio_dev) {
		if (-1 != dev->radio_dev->minor)
			video_unregister_device(dev->radio_dev);
		else
			video_device_release(dev->radio_dev);
		dev->radio_dev = NULL;
	}
	if (dev->vbi_dev) {
		if (-1 != dev->vbi_dev->minor)
			video_unregister_device(dev->vbi_dev);
		else
			video_device_release(dev->vbi_dev);
		dev->vbi_dev = NULL;
	}
	if (dev->video_dev) {
		if (-1 != dev->video_dev->minor)
			video_unregister_device(dev->video_dev);
		else
			video_device_release(dev->video_dev);
		dev->video_dev = NULL;
	}
}

static int __devinit cx8800_initdev(struct pci_dev *pci_dev,
				    const struct pci_device_id *pci_id)
{
	struct cx8800_dev *dev;
	struct cx88_core *core;

	int err;

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
	pci_read_config_byte(pci_dev, PCI_CLASS_REVISION, &dev->pci_rev);
	pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER,  &dev->pci_lat);
	printk(KERN_INFO "%s/0: found at %s, rev: %d, irq: %d, "
	       "latency: %d, mmio: 0x%llx\n", core->name,
	       pci_name(pci_dev), dev->pci_rev, pci_dev->irq,
	       dev->pci_lat,(unsigned long long)pci_resource_start(pci_dev,0));

	pci_set_master(pci_dev);
	if (!pci_dma_supported(pci_dev,DMA_32BIT_MASK)) {
		printk("%s/0: Oops: no 32bit PCI DMA ???\n",core->name);
		err = -EIO;
		goto fail_core;
	}

	/* Initialize VBI template */
	memcpy( &cx8800_vbi_template, &cx8800_video_template,
		sizeof(cx8800_vbi_template) );
	strcpy(cx8800_vbi_template.name,"cx8800-vbi");
	cx8800_vbi_template.type = VID_TYPE_TELETEXT|VID_TYPE_TUNER;

	/* initialize driver struct */
	spin_lock_init(&dev->slock);
	core->tvnorm = cx8800_video_template.current_norm;

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
		printk(KERN_ERR "%s: can't get IRQ %d\n",
		       core->name,pci_dev->irq);
		goto fail_core;
	}
	cx_set(MO_PCI_INTMSK, core->pci_irqmask);

	/* load and configure helper modules */
	if (TUNER_ABSENT != core->tuner_type)
		request_module("tuner");

	if (cx88_boards[ core->board ].audio_chip == AUDIO_CHIP_WM8775)
		request_module("wm8775");

	/* register v4l devices */
	dev->video_dev = cx88_vdev_init(core,dev->pci,
					&cx8800_video_template,"video");
	err = video_register_device(dev->video_dev,VFL_TYPE_GRABBER,
				    video_nr[core->nr]);
	if (err < 0) {
		printk(KERN_INFO "%s: can't register video device\n",
		       core->name);
		goto fail_unreg;
	}
	printk(KERN_INFO "%s/0: registered device video%d [v4l2]\n",
	       core->name,dev->video_dev->minor & 0x1f);

	dev->vbi_dev = cx88_vdev_init(core,dev->pci,&cx8800_vbi_template,"vbi");
	err = video_register_device(dev->vbi_dev,VFL_TYPE_VBI,
				    vbi_nr[core->nr]);
	if (err < 0) {
		printk(KERN_INFO "%s/0: can't register vbi device\n",
		       core->name);
		goto fail_unreg;
	}
	printk(KERN_INFO "%s/0: registered device vbi%d\n",
	       core->name,dev->vbi_dev->minor & 0x1f);

	if (core->has_radio) {
		dev->radio_dev = cx88_vdev_init(core,dev->pci,
						&cx8800_radio_template,"radio");
		err = video_register_device(dev->radio_dev,VFL_TYPE_RADIO,
					    radio_nr[core->nr]);
		if (err < 0) {
			printk(KERN_INFO "%s/0: can't register radio device\n",
			       core->name);
			goto fail_unreg;
		}
		printk(KERN_INFO "%s/0: registered device radio%d\n",
		       core->name,dev->radio_dev->minor & 0x1f);
	}

	/* everything worked */
	list_add_tail(&dev->devlist,&cx8800_devlist);
	pci_set_drvdata(pci_dev,dev);

	/* initial device configuration */
	mutex_lock(&core->lock);
	cx88_set_tvnorm(core,core->tvnorm);
	init_controls(core);
	cx88_video_mux(core,0);
	mutex_unlock(&core->lock);

	/* start tvaudio thread */
	if (core->tuner_type != TUNER_ABSENT)
		core->kthread = kthread_run(cx88_audio_thread, core, "cx88 tvaudio");
	return 0;

fail_unreg:
	cx8800_unregister_video(dev);
	free_irq(pci_dev->irq, dev);
fail_core:
	cx88_core_put(core,dev->pci);
fail_free:
	kfree(dev);
	return err;
}

static void __devexit cx8800_finidev(struct pci_dev *pci_dev)
{
	struct cx8800_dev *dev = pci_get_drvdata(pci_dev);
	struct cx88_core *core = dev->core;

	/* stop thread */
	if (core->kthread) {
		kthread_stop(core->kthread);
		core->kthread = NULL;
	}

	cx88_shutdown(core); /* FIXME */
	pci_disable_device(pci_dev);

	/* unregister stuff */

	free_irq(pci_dev->irq, dev);
	cx8800_unregister_video(dev);
	pci_set_drvdata(pci_dev, NULL);

	/* free memory */
	btcx_riscmem_free(dev->pci,&dev->vidq.stopper);
	list_del(&dev->devlist);
	cx88_core_put(core,dev->pci);
	kfree(dev);
}

#ifdef CONFIG_PM
static int cx8800_suspend(struct pci_dev *pci_dev, pm_message_t state)
{
	struct cx8800_dev *dev = pci_get_drvdata(pci_dev);
	struct cx88_core *core = dev->core;

	/* stop video+vbi capture */
	spin_lock(&dev->slock);
	if (!list_empty(&dev->vidq.active)) {
		printk("%s: suspend video\n", core->name);
		stop_video_dma(dev);
		del_timer(&dev->vidq.timeout);
	}
	if (!list_empty(&dev->vbiq.active)) {
		printk("%s: suspend vbi\n", core->name);
		cx8800_stop_vbi_dma(dev);
		del_timer(&dev->vbiq.timeout);
	}
	spin_unlock(&dev->slock);

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
	int err;

	if (dev->state.disabled) {
		err=pci_enable_device(pci_dev);
		if (err) {
			printk(KERN_ERR "%s: can't enable device\n",
						       core->name);
			return err;
		}

		dev->state.disabled = 0;
	}
	err= pci_set_power_state(pci_dev, PCI_D0);
	if (err) {
		printk(KERN_ERR "%s: can't enable device\n",
				       core->name);

		pci_disable_device(pci_dev);
		dev->state.disabled = 1;

		return err;
	}
	pci_restore_state(pci_dev);

	/* FIXME: re-initialize hardware */
	cx88_reset(core);

	/* restart video+vbi capture */
	spin_lock(&dev->slock);
	if (!list_empty(&dev->vidq.active)) {
		printk("%s: resume video\n", core->name);
		restart_video_queue(dev,&dev->vidq);
	}
	if (!list_empty(&dev->vbiq.active)) {
		printk("%s: resume vbi\n", core->name);
		cx8800_restart_vbi_queue(dev,&dev->vbiq);
	}
	spin_unlock(&dev->slock);

	return 0;
}
#endif

/* ----------------------------------------------------------- */

static struct pci_device_id cx8800_pci_tbl[] = {
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
	.remove   = __devexit_p(cx8800_finidev),
#ifdef CONFIG_PM
	.suspend  = cx8800_suspend,
	.resume   = cx8800_resume,
#endif
};

static int cx8800_init(void)
{
	printk(KERN_INFO "cx2388x v4l2 driver version %d.%d.%d loaded\n",
	       (CX88_VERSION_CODE >> 16) & 0xff,
	       (CX88_VERSION_CODE >>  8) & 0xff,
	       CX88_VERSION_CODE & 0xff);
#ifdef SNAPSHOT
	printk(KERN_INFO "cx2388x: snapshot date %04d-%02d-%02d\n",
	       SNAPSHOT/10000, (SNAPSHOT/100)%100, SNAPSHOT%100);
#endif
	return pci_register_driver(&cx8800_pci_driver);
}

static void cx8800_fini(void)
{
	pci_unregister_driver(&cx8800_pci_driver);
}

module_init(cx8800_init);
module_exit(cx8800_fini);

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 * kate: eol "unix"; indent-width 3; remove-trailing-space on; replace-trailing-space-save on; tab-width 8; replace-tabs off; space-indent off; mixed-indent off
 */

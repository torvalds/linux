/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 *  Based on Steven Toth <stoth@linuxtv.org> cx23885 driver
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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "cx25821-video.h"

MODULE_DESCRIPTION("v4l2 driver module for cx25821 based TV cards");
MODULE_AUTHOR("Steven Toth <stoth@linuxtv.org>");
MODULE_LICENSE("GPL");

static unsigned int video_nr[] = {[0 ... (CX25821_MAXBOARDS - 1)] = UNSET };
static unsigned int radio_nr[] = {[0 ... (CX25821_MAXBOARDS - 1)] = UNSET };

module_param_array(video_nr, int, NULL, 0444);
module_param_array(radio_nr, int, NULL, 0444);

MODULE_PARM_DESC(video_nr, "video device numbers");
MODULE_PARM_DESC(radio_nr, "radio device numbers");

static unsigned int video_debug = VIDEO_DEBUG;
module_param(video_debug, int, 0644);
MODULE_PARM_DESC(video_debug, "enable debug messages [video]");

static unsigned int irq_debug;
module_param(irq_debug, int, 0644);
MODULE_PARM_DESC(irq_debug, "enable debug messages [IRQ handler]");

unsigned int vid_limit = 16;
module_param(vid_limit, int, 0644);
MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static void init_controls(struct cx25821_dev *dev, int chan_num);

#define FORMAT_FLAGS_PACKED       0x01

struct cx25821_fmt formats[] = {
	{
	 .name = "8 bpp, gray",
	 .fourcc = V4L2_PIX_FMT_GREY,
	 .depth = 8,
	 .flags = FORMAT_FLAGS_PACKED,
	 }, {
	     .name = "4:1:1, packed, Y41P",
	     .fourcc = V4L2_PIX_FMT_Y41P,
	     .depth = 12,
	     .flags = FORMAT_FLAGS_PACKED,
	     }, {
		 .name = "4:2:2, packed, YUYV",
		 .fourcc = V4L2_PIX_FMT_YUYV,
		 .depth = 16,
		 .flags = FORMAT_FLAGS_PACKED,
		 }, {
		     .name = "4:2:2, packed, UYVY",
		     .fourcc = V4L2_PIX_FMT_UYVY,
		     .depth = 16,
		     .flags = FORMAT_FLAGS_PACKED,
		     }, {
			 .name = "4:2:0, YUV",
			 .fourcc = V4L2_PIX_FMT_YUV420,
			 .depth = 12,
			 .flags = FORMAT_FLAGS_PACKED,
			 },
};

int get_format_size(void)
{
	return ARRAY_SIZE(formats);
}

struct cx25821_fmt *format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;

	if (fourcc == V4L2_PIX_FMT_Y41P || fourcc == V4L2_PIX_FMT_YUV411P) {
		return formats + 1;
	}

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (formats[i].fourcc == fourcc)
			return formats + i;

	printk(KERN_ERR "%s(0x%08x) NOT FOUND\n", __func__, fourcc);
	return NULL;
}

void dump_video_queue(struct cx25821_dev *dev, struct cx25821_dmaqueue *q)
{
	struct cx25821_buffer *buf;
	struct list_head *item;
	dprintk(1, "%s()\n", __func__);

	if (!list_empty(&q->active)) {
		list_for_each(item, &q->active)
		    buf = list_entry(item, struct cx25821_buffer, vb.queue);
	}

	if (!list_empty(&q->queued)) {
		list_for_each(item, &q->queued)
		    buf = list_entry(item, struct cx25821_buffer, vb.queue);
	}

}

void cx25821_video_wakeup(struct cx25821_dev *dev, struct cx25821_dmaqueue *q,
			  u32 count)
{
	struct cx25821_buffer *buf;
	int bc;

	for (bc = 0;; bc++) {
		if (list_empty(&q->active)) {
			dprintk(1, "bc=%d (=0: active empty)\n", bc);
			break;
		}

		buf =
		    list_entry(q->active.next, struct cx25821_buffer, vb.queue);

		/* count comes from the hw and it is 16bit wide --
		 * this trick handles wrap-arounds correctly for
		 * up to 32767 buffers in flight... */
		if ((s16) (count - buf->count) < 0) {
			break;
		}

		do_gettimeofday(&buf->vb.ts);
		buf->vb.state = VIDEOBUF_DONE;
		list_del(&buf->vb.queue);
		wake_up(&buf->vb.done);
	}

	if (list_empty(&q->active))
		del_timer(&q->timeout);
	else
		mod_timer(&q->timeout, jiffies + BUFFER_TIMEOUT);
	if (bc != 1)
		printk(KERN_ERR "%s: %d buffers handled (should be 1)\n",
		       __func__, bc);
}

#ifdef TUNER_FLAG
int cx25821_set_tvnorm(struct cx25821_dev *dev, v4l2_std_id norm)
{
	dprintk(1, "%s(norm = 0x%08x) name: [%s]\n", __func__,
		(unsigned int)norm, v4l2_norm_to_name(norm));

	dev->tvnorm = norm;

	/* Tell the internal A/V decoder */
	cx25821_call_all(dev, core, s_std, norm);

	return 0;
}
#endif

struct video_device *cx25821_vdev_init(struct cx25821_dev *dev,
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
	snprintf(vfd->name, sizeof(vfd->name), "%s %s (%s)", dev->name, type,
		 cx25821_boards[dev->board].name);
	video_set_drvdata(vfd, dev);
	return vfd;
}

/*
static int cx25821_ctrl_query(struct v4l2_queryctrl *qctrl)
{
    int i;

    if (qctrl->id < V4L2_CID_BASE || qctrl->id >= V4L2_CID_LASTP1)
	return -EINVAL;
    for (i = 0; i < CX25821_CTLS; i++)
	if (cx25821_ctls[i].v.id == qctrl->id)
	    break;
    if (i == CX25821_CTLS) {
	*qctrl = no_ctl;
	return 0;
    }
    *qctrl = cx25821_ctls[i].v;
    return 0;
}
*/

// resource management
int res_get(struct cx25821_dev *dev, struct cx25821_fh *fh, unsigned int bit)
{
	dprintk(1, "%s()\n", __func__);
	if (fh->resources & bit)
		/* have it already allocated */
		return 1;

	/* is it free? */
	mutex_lock(&dev->lock);
	if (dev->resources & bit) {
		/* no, someone else uses it */
		mutex_unlock(&dev->lock);
		return 0;
	}
	/* it's free, grab it */
	fh->resources |= bit;
	dev->resources |= bit;
	dprintk(1, "res: get %d\n", bit);
	mutex_unlock(&dev->lock);
	return 1;
}

int res_check(struct cx25821_fh *fh, unsigned int bit)
{
	return fh->resources & bit;
}

int res_locked(struct cx25821_dev *dev, unsigned int bit)
{
	return dev->resources & bit;
}

void res_free(struct cx25821_dev *dev, struct cx25821_fh *fh, unsigned int bits)
{
	BUG_ON((fh->resources & bits) != bits);
	dprintk(1, "%s()\n", __func__);

	mutex_lock(&dev->lock);
	fh->resources &= ~bits;
	dev->resources &= ~bits;
	dprintk(1, "res: put %d\n", bits);
	mutex_unlock(&dev->lock);
}

int cx25821_video_mux(struct cx25821_dev *dev, unsigned int input)
{
	struct v4l2_routing route;
	memset(&route, 0, sizeof(route));

	dprintk(1, "%s() video_mux: %d [vmux=%d, gpio=0x%x,0x%x,0x%x,0x%x]\n",
		__func__, input, INPUT(input)->vmux, INPUT(input)->gpio0,
		INPUT(input)->gpio1, INPUT(input)->gpio2, INPUT(input)->gpio3);
	dev->input = input;

	route.input = INPUT(input)->vmux;

	/* Tell the internal A/V decoder */
	cx25821_call_all(dev, video, s_routing, INPUT(input)->vmux, 0, 0);

	return 0;
}

int cx25821_start_video_dma(struct cx25821_dev *dev,
			    struct cx25821_dmaqueue *q,
			    struct cx25821_buffer *buf,
			    struct sram_channel *channel)
{
	int tmp = 0;

	/* setup fifo + format */
	cx25821_sram_channel_setup(dev, channel, buf->bpl, buf->risc.dma);

	/* reset counter */
	cx_write(channel->gpcnt_ctl, 3);
	q->count = 1;

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

int cx25821_restart_video_queue(struct cx25821_dev *dev,
				struct cx25821_dmaqueue *q,
				struct sram_channel *channel)
{
	struct cx25821_buffer *buf, *prev;
	struct list_head *item;

	if (!list_empty(&q->active)) {
		buf =
		    list_entry(q->active.next, struct cx25821_buffer, vb.queue);

		cx25821_start_video_dma(dev, q, buf, channel);

		list_for_each(item, &q->active) {
			buf = list_entry(item, struct cx25821_buffer, vb.queue);
			buf->count = q->count++;
		}

		mod_timer(&q->timeout, jiffies + BUFFER_TIMEOUT);
		return 0;
	}

	prev = NULL;
	for (;;) {
		if (list_empty(&q->queued))
			return 0;

		buf =
		    list_entry(q->queued.next, struct cx25821_buffer, vb.queue);

		if (NULL == prev) {
			list_move_tail(&buf->vb.queue, &q->active);
			cx25821_start_video_dma(dev, q, buf, channel);
			buf->vb.state = VIDEOBUF_ACTIVE;
			buf->count = q->count++;
			mod_timer(&q->timeout, jiffies + BUFFER_TIMEOUT);
		} else if (prev->vb.width == buf->vb.width &&
			   prev->vb.height == buf->vb.height &&
			   prev->fmt == buf->fmt) {
			list_move_tail(&buf->vb.queue, &q->active);
			buf->vb.state = VIDEOBUF_ACTIVE;
			buf->count = q->count++;
			prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
			prev->risc.jmp[2] = cpu_to_le32(0);	/* Bits 63 - 32 */
		} else {
			return 0;
		}
		prev = buf;
	}
}

void cx25821_vid_timeout(unsigned long data)
{
	struct cx25821_data *timeout_data = (struct cx25821_data *)data;
	struct cx25821_dev *dev = timeout_data->dev;
	struct sram_channel *channel = timeout_data->channel;
	struct cx25821_dmaqueue *q = &dev->vidq[channel->i];
	struct cx25821_buffer *buf;
	unsigned long flags;

	//cx25821_sram_channel_dump(dev, channel);
	cx_clear(channel->dma_ctl, 0x11);

	spin_lock_irqsave(&dev->slock, flags);
	while (!list_empty(&q->active)) {
		buf =
		    list_entry(q->active.next, struct cx25821_buffer, vb.queue);
		list_del(&buf->vb.queue);

		buf->vb.state = VIDEOBUF_ERROR;
		wake_up(&buf->vb.done);
	}

	cx25821_restart_video_queue(dev, q, channel);
	spin_unlock_irqrestore(&dev->slock, flags);
}

int cx25821_video_irq(struct cx25821_dev *dev, int chan_num, u32 status)
{
	u32 count = 0;
	int handled = 0;
	u32 mask;
	struct sram_channel *channel = &dev->sram_channels[chan_num];

	mask = cx_read(channel->int_msk);
	if (0 == (status & mask))
		return handled;

	cx_write(channel->int_stat, status);

	/* risc op code error */
	if (status & (1 << 16)) {
		printk(KERN_WARNING "%s, %s: video risc op code error\n",
		       dev->name, channel->name);
		cx_clear(channel->dma_ctl, 0x11);
		cx25821_sram_channel_dump(dev, channel);
	}

	/* risc1 y */
	if (status & FLD_VID_DST_RISC1) {
		spin_lock(&dev->slock);
		count = cx_read(channel->gpcnt);
		cx25821_video_wakeup(dev, &dev->vidq[channel->i], count);
		spin_unlock(&dev->slock);
		handled++;
	}

	/* risc2 y */
	if (status & 0x10) {
		dprintk(2, "stopper video\n");
		spin_lock(&dev->slock);
		cx25821_restart_video_queue(dev, &dev->vidq[channel->i],
					    channel);
		spin_unlock(&dev->slock);
		handled++;
	}
	return handled;
}

void cx25821_videoioctl_unregister(struct cx25821_dev *dev)
{
	if (dev->ioctl_dev) {
		if (video_is_registered(dev->ioctl_dev))
			video_unregister_device(dev->ioctl_dev);
		else
			video_device_release(dev->ioctl_dev);

		dev->ioctl_dev = NULL;
	}
}

void cx25821_video_unregister(struct cx25821_dev *dev, int chan_num)
{
	cx_clear(PCI_INT_MSK, 1);

	if (dev->video_dev[chan_num]) {
		if (video_is_registered(dev->video_dev[chan_num]))
			video_unregister_device(dev->video_dev[chan_num]);
		else
			video_device_release(dev->video_dev[chan_num]);

		dev->video_dev[chan_num] = NULL;

		btcx_riscmem_free(dev->pci, &dev->vidq[chan_num].stopper);

		printk(KERN_WARNING "device %d released!\n", chan_num);
	}

}

int cx25821_video_register(struct cx25821_dev *dev, int chan_num,
			   struct video_device *video_template)
{
	int err;

	spin_lock_init(&dev->slock);

	//printk(KERN_WARNING "Channel %d\n", chan_num);

#ifdef TUNER_FLAG
	dev->tvnorm = video_template->current_norm;
#endif

	/* init video dma queues */
	dev->timeout_data[chan_num].dev = dev;
	dev->timeout_data[chan_num].channel = &dev->sram_channels[chan_num];
	INIT_LIST_HEAD(&dev->vidq[chan_num].active);
	INIT_LIST_HEAD(&dev->vidq[chan_num].queued);
	dev->vidq[chan_num].timeout.function = cx25821_vid_timeout;
	dev->vidq[chan_num].timeout.data =
	    (unsigned long)&dev->timeout_data[chan_num];
	init_timer(&dev->vidq[chan_num].timeout);
	cx25821_risc_stopper(dev->pci, &dev->vidq[chan_num].stopper,
			     dev->sram_channels[chan_num].dma_ctl, 0x11, 0);

	/* register v4l devices */
	dev->video_dev[chan_num] =
	    cx25821_vdev_init(dev, dev->pci, video_template, "video");
	err =
	    video_register_device(dev->video_dev[chan_num], VFL_TYPE_GRABBER,
				  video_nr[dev->nr]);

	if (err < 0) {
		goto fail_unreg;
	}
	//set PCI interrupt
	cx_set(PCI_INT_MSK, 0xff);

	/* initial device configuration */
	mutex_lock(&dev->lock);
#ifdef TUNER_FLAG
	cx25821_set_tvnorm(dev, dev->tvnorm);
#endif
	mutex_unlock(&dev->lock);

	init_controls(dev, chan_num);

	return 0;

      fail_unreg:
	cx25821_video_unregister(dev, chan_num);
	return err;
}

int buffer_setup(struct videobuf_queue *q, unsigned int *count,
		 unsigned int *size)
{
	struct cx25821_fh *fh = q->priv_data;

	*size = fh->fmt->depth * fh->width * fh->height >> 3;

	if (0 == *count)
		*count = 32;

	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;

	return 0;
}

int buffer_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
		   enum v4l2_field field)
{
	struct cx25821_fh *fh = q->priv_data;
	struct cx25821_dev *dev = fh->dev;
	struct cx25821_buffer *buf =
	    container_of(vb, struct cx25821_buffer, vb);
	int rc, init_buffer = 0;
	u32 line0_offset, line1_offset;
	struct videobuf_dmabuf *dma = videobuf_to_dma(&buf->vb);
	int bpl_local = LINE_SIZE_D1;
	int channel_opened = 0;

	BUG_ON(NULL == fh->fmt);
	if (fh->width < 48 || fh->width > 720 ||
	    fh->height < 32 || fh->height > 576)
		return -EINVAL;

	buf->vb.size = (fh->width * fh->height * fh->fmt->depth) >> 3;

	if (0 != buf->vb.baddr && buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	if (buf->fmt != fh->fmt ||
	    buf->vb.width != fh->width ||
	    buf->vb.height != fh->height || buf->vb.field != field) {
		buf->fmt = fh->fmt;
		buf->vb.width = fh->width;
		buf->vb.height = fh->height;
		buf->vb.field = field;
		init_buffer = 1;
	}

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		init_buffer = 1;
		rc = videobuf_iolock(q, &buf->vb, NULL);
		if (0 != rc) {
			printk(KERN_DEBUG "videobuf_iolock failed!\n");
			goto fail;
		}
	}

	dprintk(1, "init_buffer=%d\n", init_buffer);

	if (init_buffer) {

		channel_opened = dev->channel_opened;
		channel_opened = (channel_opened < 0
				  || channel_opened > 7) ? 7 : channel_opened;

		if (dev->pixel_formats[channel_opened] == PIXEL_FRMT_411)
			buf->bpl = (buf->fmt->depth * buf->vb.width) >> 3;
		else
			buf->bpl = (buf->fmt->depth >> 3) * (buf->vb.width);

		if (dev->pixel_formats[channel_opened] == PIXEL_FRMT_411) {
			bpl_local = buf->bpl;
		} else {
			bpl_local = buf->bpl;	//Default

			if (channel_opened >= 0 && channel_opened <= 7) {
				if (dev->use_cif_resolution[channel_opened]) {
					if (dev->tvnorm & V4L2_STD_PAL_BG
					    || dev->tvnorm & V4L2_STD_PAL_DK)
						bpl_local = 352 << 1;
					else
						bpl_local =
						    dev->
						    cif_width[channel_opened] <<
						    1;
				}
			}
		}

		switch (buf->vb.field) {
		case V4L2_FIELD_TOP:
			cx25821_risc_buffer(dev->pci, &buf->risc,
					    dma->sglist, 0, UNSET,
					    buf->bpl, 0, buf->vb.height);
			break;
		case V4L2_FIELD_BOTTOM:
			cx25821_risc_buffer(dev->pci, &buf->risc,
					    dma->sglist, UNSET, 0,
					    buf->bpl, 0, buf->vb.height);
			break;
		case V4L2_FIELD_INTERLACED:
			/* All other formats are top field first */
			line0_offset = 0;
			line1_offset = buf->bpl;
			dprintk(1, "top field first\n");

			cx25821_risc_buffer(dev->pci, &buf->risc,
					    dma->sglist, line0_offset,
					    bpl_local, bpl_local, bpl_local,
					    buf->vb.height >> 1);
			break;
		case V4L2_FIELD_SEQ_TB:
			cx25821_risc_buffer(dev->pci, &buf->risc,
					    dma->sglist,
					    0, buf->bpl * (buf->vb.height >> 1),
					    buf->bpl, 0, buf->vb.height >> 1);
			break;
		case V4L2_FIELD_SEQ_BT:
			cx25821_risc_buffer(dev->pci, &buf->risc,
					    dma->sglist,
					    buf->bpl * (buf->vb.height >> 1), 0,
					    buf->bpl, 0, buf->vb.height >> 1);
			break;
		default:
			BUG();
		}
	}

	dprintk(2, "[%p/%d] buffer_prep - %dx%d %dbpp \"%s\" - dma=0x%08lx\n",
		buf, buf->vb.i, fh->width, fh->height, fh->fmt->depth,
		fh->fmt->name, (unsigned long)buf->risc.dma);

	buf->vb.state = VIDEOBUF_PREPARED;

	return 0;

      fail:
	cx25821_free_buffer(q, buf);
	return rc;
}

void buffer_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct cx25821_buffer *buf =
	    container_of(vb, struct cx25821_buffer, vb);

	cx25821_free_buffer(q, buf);
}

struct videobuf_queue *get_queue(struct cx25821_fh *fh)
{
	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &fh->vidq;
	default:
		BUG();
		return NULL;
	}
}

int get_resource(struct cx25821_fh *fh, int resource)
{
	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return resource;
	default:
		BUG();
		return 0;
	}
}

int video_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct cx25821_fh *fh = file->private_data;

	return videobuf_mmap_mapper(get_queue(fh), vma);
}

/* VIDEO IOCTLS                                                       */
int vidioc_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct cx25821_fh *fh = priv;

	f->fmt.pix.width = fh->width;
	f->fmt.pix.height = fh->height;
	f->fmt.pix.field = fh->vidq.field;
	f->fmt.pix.pixelformat = fh->fmt->fourcc;
	f->fmt.pix.bytesperline = (f->fmt.pix.width * fh->fmt->depth) >> 3;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

int vidioc_try_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct cx25821_fmt *fmt;
	enum v4l2_field field;
	unsigned int maxw, maxh;

	fmt = format_by_fourcc(f->fmt.pix.pixelformat);
	if (NULL == fmt)
		return -EINVAL;

	field = f->fmt.pix.field;
	maxw = 720;
	maxh = 576;

	if (V4L2_FIELD_ANY == field) {
		field = (f->fmt.pix.height > maxh / 2)
		    ? V4L2_FIELD_INTERLACED : V4L2_FIELD_TOP;
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
	f->fmt.pix.bytesperline = (f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

int vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;

	strcpy(cap->driver, "cx25821");
	strlcpy(cap->card, cx25821_boards[dev->board].name, sizeof(cap->card));
	sprintf(cap->bus_info, "PCIe:%s", pci_name(dev->pci));
	cap->version = CX25821_VERSION_CODE;
	cap->capabilities =
	    V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	if (UNSET != dev->tuner_type)
		cap->capabilities |= V4L2_CAP_TUNER;
	return 0;
}

int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
			    struct v4l2_fmtdesc *f)
{
	if (unlikely(f->index >= ARRAY_SIZE(formats)))
		return -EINVAL;

	strlcpy(f->description, formats[f->index].name, sizeof(f->description));
	f->pixelformat = formats[f->index].fourcc;

	return 0;
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct cx25821_fh *fh = priv;
	struct videobuf_queue *q;
	struct v4l2_requestbuffers req;
	unsigned int i;
	int err;

	q = get_queue(fh);
	memset(&req, 0, sizeof(req));
	req.type = q->type;
	req.count = 8;
	req.memory = V4L2_MEMORY_MMAP;
	err = videobuf_reqbufs(q, &req);
	if (err < 0)
		return err;

	mbuf->frames = req.count;
	mbuf->size = 0;
	for (i = 0; i < mbuf->frames; i++) {
		mbuf->offsets[i] = q->bufs[i]->boff;
		mbuf->size += q->bufs[i]->bsize;
	}
	return 0;
}
#endif

int vidioc_reqbufs(struct file *file, void *priv, struct v4l2_requestbuffers *p)
{
	struct cx25821_fh *fh = priv;
	return videobuf_reqbufs(get_queue(fh), p);
}

int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct cx25821_fh *fh = priv;
	return videobuf_querybuf(get_queue(fh), p);
}

int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct cx25821_fh *fh = priv;
	return videobuf_qbuf(get_queue(fh), p);
}

int vidioc_g_priority(struct file *file, void *f, enum v4l2_priority *p)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)f)->dev;

	*p = v4l2_prio_max(&dev->prio);

	return 0;
}

int vidioc_s_priority(struct file *file, void *f, enum v4l2_priority prio)
{
	struct cx25821_fh *fh = f;
	struct cx25821_dev *dev = ((struct cx25821_fh *)f)->dev;

	return v4l2_prio_change(&dev->prio, &fh->prio, prio);
}

#ifdef TUNER_FLAG
int vidioc_s_std(struct file *file, void *priv, v4l2_std_id * tvnorms)
{
	struct cx25821_fh *fh = priv;
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
	int err;

	dprintk(1, "%s()\n", __func__);

	if (fh) {
		err = v4l2_prio_check(&dev->prio, &fh->prio);
		if (0 != err)
			return err;
	}

	if (dev->tvnorm == *tvnorms) {
		return 0;
	}

	mutex_lock(&dev->lock);
	cx25821_set_tvnorm(dev, *tvnorms);
	mutex_unlock(&dev->lock);

	medusa_set_videostandard(dev);

	return 0;
}
#endif

int cx25821_enum_input(struct cx25821_dev *dev, struct v4l2_input *i)
{
	static const char *iname[] = {
		[CX25821_VMUX_COMPOSITE] = "Composite",
		[CX25821_VMUX_SVIDEO] = "S-Video",
		[CX25821_VMUX_DEBUG] = "for debug only",
	};
	unsigned int n;
	dprintk(1, "%s()\n", __func__);

	n = i->index;
	if (n >= 2)
		return -EINVAL;

	if (0 == INPUT(n)->type)
		return -EINVAL;

	memset(i, 0, sizeof(*i));
	i->index = n;
	i->type = V4L2_INPUT_TYPE_CAMERA;
	strcpy(i->name, iname[INPUT(n)->type]);

	i->std = CX25821_NORMS;
	return 0;
}

int vidioc_enum_input(struct file *file, void *priv, struct v4l2_input *i)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
	dprintk(1, "%s()\n", __func__);
	return cx25821_enum_input(dev, i);
}

int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;

	*i = dev->input;
	dprintk(1, "%s() returns %d\n", __func__, *i);
	return 0;
}

int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct cx25821_fh *fh = priv;
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
	int err;

	dprintk(1, "%s(%d)\n", __func__, i);

	if (fh) {
		err = v4l2_prio_check(&dev->prio, &fh->prio);
		if (0 != err)
			return err;
	}

	if (i > 2) {
		dprintk(1, "%s() -EINVAL\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&dev->lock);
	cx25821_video_mux(dev, i);
	mutex_unlock(&dev->lock);
	return 0;
}

#ifdef TUNER_FLAG
int vidioc_g_frequency(struct file *file, void *priv, struct v4l2_frequency *f)
{
	struct cx25821_fh *fh = priv;
	struct cx25821_dev *dev = fh->dev;

	f->frequency = dev->freq;

	cx25821_call_all(dev, tuner, g_frequency, f);

	return 0;
}

int cx25821_set_freq(struct cx25821_dev *dev, struct v4l2_frequency *f)
{
	mutex_lock(&dev->lock);
	dev->freq = f->frequency;

	cx25821_call_all(dev, tuner, s_frequency, f);

	/* When changing channels it is required to reset TVAUDIO */
	msleep(10);

	mutex_unlock(&dev->lock);

	return 0;
}

int vidioc_s_frequency(struct file *file, void *priv, struct v4l2_frequency *f)
{
	struct cx25821_fh *fh = priv;
	struct cx25821_dev *dev = fh->dev;
	int err;

	if (fh) {
		err = v4l2_prio_check(&dev->prio, &fh->prio);
		if (0 != err)
			return err;
	}

	return cx25821_set_freq(dev, f);
}
#endif

#ifdef CONFIG_VIDEO_ADV_DEBUG
int vidioc_g_register(struct file *file, void *fh,
		      struct v4l2_dbg_register *reg)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)fh)->dev;

	if (!v4l2_chip_match_host(&reg->match))
		return -EINVAL;

	cx25821_call_all(dev, core, g_register, reg);

	return 0;
}

int vidioc_s_register(struct file *file, void *fh,
		      struct v4l2_dbg_register *reg)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)fh)->dev;

	if (!v4l2_chip_match_host(&reg->match))
		return -EINVAL;

	cx25821_call_all(dev, core, s_register, reg);

	return 0;
}

#endif

#ifdef TUNER_FLAG
int vidioc_g_tuner(struct file *file, void *priv, struct v4l2_tuner *t)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;

	if (unlikely(UNSET == dev->tuner_type))
		return -EINVAL;
	if (0 != t->index)
		return -EINVAL;

	strcpy(t->name, "Television");
	t->type = V4L2_TUNER_ANALOG_TV;
	t->capability = V4L2_TUNER_CAP_NORM;
	t->rangehigh = 0xffffffffUL;

	t->signal = 0xffff;	/* LOCKED */
	return 0;
}

int vidioc_s_tuner(struct file *file, void *priv, struct v4l2_tuner *t)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
	struct cx25821_fh *fh = priv;
	int err;

	if (fh) {
		err = v4l2_prio_check(&dev->prio, &fh->prio);
		if (0 != err)
			return err;
	}

	dprintk(1, "%s()\n", __func__);
	if (UNSET == dev->tuner_type)
		return -EINVAL;
	if (0 != t->index)
		return -EINVAL;

	return 0;
}

#endif
// ******************************************************************************************
static const struct v4l2_queryctrl no_ctl = {
	.name = "42",
	.flags = V4L2_CTRL_FLAG_DISABLED,
};

static struct v4l2_queryctrl cx25821_ctls[] = {
	/* --- video --- */
	{
	 .id = V4L2_CID_BRIGHTNESS,
	 .name = "Brightness",
	 .minimum = 0,
	 .maximum = 10000,
	 .step = 1,
	 .default_value = 6200,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 }, {
	     .id = V4L2_CID_CONTRAST,
	     .name = "Contrast",
	     .minimum = 0,
	     .maximum = 10000,
	     .step = 1,
	     .default_value = 5000,
	     .type = V4L2_CTRL_TYPE_INTEGER,
	     }, {
		 .id = V4L2_CID_SATURATION,
		 .name = "Saturation",
		 .minimum = 0,
		 .maximum = 10000,
		 .step = 1,
		 .default_value = 5000,
		 .type = V4L2_CTRL_TYPE_INTEGER,
		 }, {
		     .id = V4L2_CID_HUE,
		     .name = "Hue",
		     .minimum = 0,
		     .maximum = 10000,
		     .step = 1,
		     .default_value = 5000,
		     .type = V4L2_CTRL_TYPE_INTEGER,
		     }
};
static const int CX25821_CTLS = ARRAY_SIZE(cx25821_ctls);

static int cx25821_ctrl_query(struct v4l2_queryctrl *qctrl)
{
	int i;

	if (qctrl->id < V4L2_CID_BASE || qctrl->id >= V4L2_CID_LASTP1)
		return -EINVAL;
	for (i = 0; i < CX25821_CTLS; i++)
		if (cx25821_ctls[i].id == qctrl->id)
			break;
	if (i == CX25821_CTLS) {
		*qctrl = no_ctl;
		return 0;
	}
	*qctrl = cx25821_ctls[i];
	return 0;
}

int vidioc_queryctrl(struct file *file, void *priv,
		     struct v4l2_queryctrl *qctrl)
{
	return cx25821_ctrl_query(qctrl);
}

/* ------------------------------------------------------------------ */
/* VIDEO CTRL IOCTLS                                                  */

static const struct v4l2_queryctrl *ctrl_by_id(unsigned int id)
{
	unsigned int i;

	for (i = 0; i < CX25821_CTLS; i++)
		if (cx25821_ctls[i].id == id)
			return cx25821_ctls + i;
	return NULL;
}

int vidioc_g_ctrl(struct file *file, void *priv, struct v4l2_control *ctl)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;

	const struct v4l2_queryctrl *ctrl;

	ctrl = ctrl_by_id(ctl->id);

	if (NULL == ctrl)
		return -EINVAL;
	switch (ctl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctl->value = dev->ctl_bright;
		break;
	case V4L2_CID_HUE:
		ctl->value = dev->ctl_hue;
		break;
	case V4L2_CID_CONTRAST:
		ctl->value = dev->ctl_contrast;
		break;
	case V4L2_CID_SATURATION:
		ctl->value = dev->ctl_saturation;
		break;
	}
	return 0;
}

int cx25821_set_control(struct cx25821_dev *dev,
			struct v4l2_control *ctl, int chan_num)
{
	int err;
	const struct v4l2_queryctrl *ctrl;

	err = -EINVAL;

	ctrl = ctrl_by_id(ctl->id);

	if (NULL == ctrl)
		return err;

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_BOOLEAN:
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER:
		if (ctl->value < ctrl->minimum)
			ctl->value = ctrl->minimum;
		if (ctl->value > ctrl->maximum)
			ctl->value = ctrl->maximum;
		break;
	default:
		/* nothing */ ;
	};

	switch (ctl->id) {
	case V4L2_CID_BRIGHTNESS:
		dev->ctl_bright = ctl->value;
		medusa_set_brightness(dev, ctl->value, chan_num);
		break;
	case V4L2_CID_HUE:
		dev->ctl_hue = ctl->value;
		medusa_set_hue(dev, ctl->value, chan_num);
		break;
	case V4L2_CID_CONTRAST:
		dev->ctl_contrast = ctl->value;
		medusa_set_contrast(dev, ctl->value, chan_num);
		break;
	case V4L2_CID_SATURATION:
		dev->ctl_saturation = ctl->value;
		medusa_set_saturation(dev, ctl->value, chan_num);
		break;
	}

	err = 0;

	return err;
}

static void init_controls(struct cx25821_dev *dev, int chan_num)
{
	struct v4l2_control ctrl;
	int i;
	for (i = 0; i < CX25821_CTLS; i++) {
		ctrl.id = cx25821_ctls[i].id;
		ctrl.value = cx25821_ctls[i].default_value;

		cx25821_set_control(dev, &ctrl, chan_num);
	}
}

int vidioc_cropcap(struct file *file, void *priv, struct v4l2_cropcap *cropcap)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;

	if (cropcap->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	cropcap->bounds.top = cropcap->bounds.left = 0;
	cropcap->bounds.width = 720;
	cropcap->bounds.height = dev->tvnorm == V4L2_STD_PAL_BG ? 576 : 480;
	cropcap->pixelaspect.numerator =
	    dev->tvnorm == V4L2_STD_PAL_BG ? 59 : 10;
	cropcap->pixelaspect.denominator =
	    dev->tvnorm == V4L2_STD_PAL_BG ? 54 : 11;
	cropcap->defrect = cropcap->bounds;
	return 0;
}

int vidioc_s_crop(struct file *file, void *priv, struct v4l2_crop *crop)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
	struct cx25821_fh *fh = priv;
	int err;

	if (fh) {
		err = v4l2_prio_check(&dev->prio, &fh->prio);
		if (0 != err)
			return err;
	}
	// vidioc_s_crop not supported
	return -EINVAL;
}

int vidioc_g_crop(struct file *file, void *priv, struct v4l2_crop *crop)
{
	// vidioc_g_crop not supported
	return -EINVAL;
}

int vidioc_querystd(struct file *file, void *priv, v4l2_std_id * norm)
{
	// medusa does not support video standard sensing of current input
	*norm = CX25821_NORMS;

	return 0;
}

int is_valid_width(u32 width, v4l2_std_id tvnorm)
{
	if (tvnorm == V4L2_STD_PAL_BG) {
		if (width == 352 || width == 720)
			return 1;
		else
			return 0;
	}

	if (tvnorm == V4L2_STD_NTSC_M) {
		if (width == 320 || width == 352 || width == 720)
			return 1;
		else
			return 0;
	}
	return 0;
}

int is_valid_height(u32 height, v4l2_std_id tvnorm)
{
	if (tvnorm == V4L2_STD_PAL_BG) {
		if (height == 576 || height == 288)
			return 1;
		else
			return 0;
	}

	if (tvnorm == V4L2_STD_NTSC_M) {
		if (height == 480 || height == 240)
			return 1;
		else
			return 0;
	}

	return 0;
}

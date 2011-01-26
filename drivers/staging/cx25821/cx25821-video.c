/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 *  Based on Steven Toth <stoth@linuxtv.org> cx23885 driver
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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "cx25821-video.h"
#include <linux/smp_lock.h>

MODULE_DESCRIPTION("v4l2 driver module for cx25821 based TV cards");
MODULE_AUTHOR("Hiep Huynh <hiep.huynh@conexant.com>");
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

static void cx25821_init_controls(struct cx25821_dev *dev, int chan_num);

static const struct v4l2_file_operations video_fops;
static const struct v4l2_ioctl_ops video_ioctl_ops;

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

int cx25821_get_format_size(void)
{
	return ARRAY_SIZE(formats);
}

struct cx25821_fmt *cx25821_format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;

	if (fourcc == V4L2_PIX_FMT_Y41P || fourcc == V4L2_PIX_FMT_YUV411P) {
		return formats + 1;
	}

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (formats[i].fourcc == fourcc)
			return formats + i;

	pr_err("%s(0x%08x) NOT FOUND\n", __func__, fourcc);
	return NULL;
}

void cx25821_dump_video_queue(struct cx25821_dev *dev, struct cx25821_dmaqueue *q)
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
		pr_err("%s: %d buffers handled (should be 1)\n",
		       __func__, bc);
}

#ifdef TUNER_FLAG
int cx25821_set_tvnorm(struct cx25821_dev *dev, v4l2_std_id norm)
{
	dprintk(1, "%s(norm = 0x%08x) name: [%s]\n",
		__func__, (unsigned int)norm, v4l2_norm_to_name(norm));

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

/* resource management */
int cx25821_res_get(struct cx25821_dev *dev, struct cx25821_fh *fh, unsigned int bit)
{
	dprintk(1, "%s()\n", __func__);
	if (fh->resources & bit)
		/* have it already allocated */
		return 1;

	/* is it free? */
	mutex_lock(&dev->lock);
       if (dev->channels[fh->channel_id].resources & bit) {
		/* no, someone else uses it */
		mutex_unlock(&dev->lock);
		return 0;
	}
	/* it's free, grab it */
	fh->resources |= bit;
       dev->channels[fh->channel_id].resources |= bit;
	dprintk(1, "res: get %d\n", bit);
	mutex_unlock(&dev->lock);
	return 1;
}

int cx25821_res_check(struct cx25821_fh *fh, unsigned int bit)
{
	return fh->resources & bit;
}

int cx25821_res_locked(struct cx25821_fh *fh, unsigned int bit)
{
       return fh->dev->channels[fh->channel_id].resources & bit;
}

void cx25821_res_free(struct cx25821_dev *dev, struct cx25821_fh *fh, unsigned int bits)
{
	BUG_ON((fh->resources & bits) != bits);
	dprintk(1, "%s()\n", __func__);

	mutex_lock(&dev->lock);
	fh->resources &= ~bits;
       dev->channels[fh->channel_id].resources &= ~bits;
	dprintk(1, "res: put %d\n", bits);
	mutex_unlock(&dev->lock);
}

int cx25821_video_mux(struct cx25821_dev *dev, unsigned int input)
{
	struct v4l2_routing route;
	memset(&route, 0, sizeof(route));

	dprintk(1, "%s(): video_mux: %d [vmux=%d, gpio=0x%x,0x%x,0x%x,0x%x]\n",
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
       struct cx25821_dmaqueue *q = &dev->channels[channel->i].vidq;
	struct cx25821_buffer *buf;
	unsigned long flags;

       /* cx25821_sram_channel_dump(dev, channel); */
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
       struct sram_channel *channel = dev->channels[chan_num].sram_channels;

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
		spin_lock(&dev->slock);
		count = cx_read(channel->gpcnt);
	       cx25821_video_wakeup(dev,
		       &dev->channels[channel->i].vidq, count);
		spin_unlock(&dev->slock);
		handled++;
	}

	/* risc2 y */
	if (status & 0x10) {
		dprintk(2, "stopper video\n");
		spin_lock(&dev->slock);
	       cx25821_restart_video_queue(dev,
			       &dev->channels[channel->i].vidq,
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

       if (dev->channels[chan_num].video_dev) {
	       if (video_is_registered(dev->channels[chan_num].video_dev))
		       video_unregister_device(
			       dev->channels[chan_num].video_dev);
		else
		       video_device_release(
			       dev->channels[chan_num].video_dev);

	       dev->channels[chan_num].video_dev = NULL;

	       btcx_riscmem_free(dev->pci,
		       &dev->channels[chan_num].vidq.stopper);

		pr_warn("device %d released!\n", chan_num);
	}

}

int cx25821_video_register(struct cx25821_dev *dev)
{
	int err;
       int i;

       struct video_device cx25821_video_device = {
	       .name = "cx25821-video",
	       .fops = &video_fops,
	       .minor = -1,
	       .ioctl_ops = &video_ioctl_ops,
	       .tvnorms = CX25821_NORMS,
	       .current_norm = V4L2_STD_NTSC_M,
       };

	spin_lock_init(&dev->slock);

    for (i = 0; i < MAX_VID_CHANNEL_NUM - 1; ++i) {
	       cx25821_init_controls(dev, i);

	       cx25821_risc_stopper(dev->pci,
			       &dev->channels[i].vidq.stopper,
			       dev->channels[i].sram_channels->dma_ctl,
			       0x11, 0);

	       dev->channels[i].sram_channels = &cx25821_sram_channels[i];
	       dev->channels[i].video_dev = NULL;
	       dev->channels[i].resources = 0;

	       cx_write(dev->channels[i].sram_channels->int_stat,
			       0xffffffff);

	       INIT_LIST_HEAD(&dev->channels[i].vidq.active);
	       INIT_LIST_HEAD(&dev->channels[i].vidq.queued);

	       dev->channels[i].timeout_data.dev = dev;
	       dev->channels[i].timeout_data.channel =
				       &cx25821_sram_channels[i];
	       dev->channels[i].vidq.timeout.function =
				       cx25821_vid_timeout;
	       dev->channels[i].vidq.timeout.data =
		       (unsigned long)&dev->channels[i].timeout_data;
	       init_timer(&dev->channels[i].vidq.timeout);

	       /* register v4l devices */
	       dev->channels[i].video_dev = cx25821_vdev_init(dev,
		       dev->pci, &cx25821_video_device, "video");

	       err = video_register_device(dev->channels[i].video_dev,
			       VFL_TYPE_GRABBER, video_nr[dev->nr]);

	       if (err < 0)
		       goto fail_unreg;

	}

    /* set PCI interrupt */
	cx_set(PCI_INT_MSK, 0xff);

	/* initial device configuration */
	mutex_lock(&dev->lock);
#ifdef TUNER_FLAG
       dev->tvnorm = cx25821_video_device.current_norm;
	cx25821_set_tvnorm(dev, dev->tvnorm);
#endif
	mutex_unlock(&dev->lock);


    return 0;

fail_unreg:
       cx25821_video_unregister(dev, i);
	return err;
}

int cx25821_buffer_setup(struct videobuf_queue *q, unsigned int *count,
		 unsigned int *size)
{
	struct cx25821_fh *fh = q->priv_data;

	*size = fh->fmt->depth * fh->width * fh->height >> 3;

	if (0 == *count)
		*count = 32;

	if (*size * *count > vid_limit * 1024 * 1024)
		*count = (vid_limit * 1024 * 1024) / *size;

	return 0;
}

int cx25821_buffer_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
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
       int channel_opened = fh->channel_id;

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
			printk(KERN_DEBUG pr_fmt("videobuf_iolock failed!\n"));
			goto fail;
		}
	}

	dprintk(1, "init_buffer=%d\n", init_buffer);

	if (init_buffer) {

		channel_opened = dev->channel_opened;
		channel_opened = (channel_opened < 0
				  || channel_opened > 7) ? 7 : channel_opened;

	       if (dev->channels[channel_opened]
		       .pixel_formats == PIXEL_FRMT_411)
			buf->bpl = (buf->fmt->depth * buf->vb.width) >> 3;
		else
			buf->bpl = (buf->fmt->depth >> 3) * (buf->vb.width);

	       if (dev->channels[channel_opened]
		       .pixel_formats == PIXEL_FRMT_411) {
			bpl_local = buf->bpl;
		} else {
		       bpl_local = buf->bpl;   /* Default */

			if (channel_opened >= 0 && channel_opened <= 7) {
			       if (dev->channels[channel_opened]
					       .use_cif_resolution) {
					if (dev->tvnorm & V4L2_STD_PAL_BG
					    || dev->tvnorm & V4L2_STD_PAL_DK)
						bpl_local = 352 << 1;
					else
						bpl_local =
						 dev->channels[channel_opened].
						 cif_width <<
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

void cx25821_buffer_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
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

int cx25821_get_resource(struct cx25821_fh *fh, int resource)
{
	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return resource;
	default:
		BUG();
		return 0;
	}
}

int cx25821_video_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct cx25821_fh *fh = file->private_data;

	return videobuf_mmap_mapper(get_queue(fh), vma);
}


static void buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
       struct cx25821_buffer *buf =
	   container_of(vb, struct cx25821_buffer, vb);
       struct cx25821_buffer *prev;
       struct cx25821_fh *fh = vq->priv_data;
       struct cx25821_dev *dev = fh->dev;
       struct cx25821_dmaqueue *q = &dev->channels[fh->channel_id].vidq;

       /* add jump to stopper */
       buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_IRQ1 | RISC_CNT_INC);
       buf->risc.jmp[1] = cpu_to_le32(q->stopper.dma);
       buf->risc.jmp[2] = cpu_to_le32(0);      /* bits 63-32 */

       dprintk(2, "jmp to stopper (0x%x)\n", buf->risc.jmp[1]);

       if (!list_empty(&q->queued)) {
	       list_add_tail(&buf->vb.queue, &q->queued);
	       buf->vb.state = VIDEOBUF_QUEUED;
	       dprintk(2, "[%p/%d] buffer_queue - append to queued\n", buf,
		       buf->vb.i);

       } else if (list_empty(&q->active)) {
	       list_add_tail(&buf->vb.queue, &q->active);
	       cx25821_start_video_dma(dev, q, buf,
				       dev->channels[fh->channel_id].
				       sram_channels);
	       buf->vb.state = VIDEOBUF_ACTIVE;
	       buf->count = q->count++;
	       mod_timer(&q->timeout, jiffies + BUFFER_TIMEOUT);
	       dprintk(2,
		       "[%p/%d] buffer_queue - first active, buf cnt = %d, \
		       q->count = %d\n",
		       buf, buf->vb.i, buf->count, q->count);
       } else {
	       prev =
		   list_entry(q->active.prev, struct cx25821_buffer, vb.queue);
	       if (prev->vb.width == buf->vb.width
		   && prev->vb.height == buf->vb.height
		   && prev->fmt == buf->fmt) {
		       list_add_tail(&buf->vb.queue, &q->active);
		       buf->vb.state = VIDEOBUF_ACTIVE;
		       buf->count = q->count++;
		       prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);

		       /* 64 bit bits 63-32 */
		       prev->risc.jmp[2] = cpu_to_le32(0);
		       dprintk(2,
			       "[%p/%d] buffer_queue - append to active, \
			       buf->count=%d\n",
			       buf, buf->vb.i, buf->count);

	       } else {
		       list_add_tail(&buf->vb.queue, &q->queued);
		       buf->vb.state = VIDEOBUF_QUEUED;
		       dprintk(2, "[%p/%d] buffer_queue - first queued\n", buf,
			       buf->vb.i);
	       }
       }

       if (list_empty(&q->active))
	       dprintk(2, "active queue empty!\n");
}

static struct videobuf_queue_ops cx25821_video_qops = {
       .buf_setup = cx25821_buffer_setup,
       .buf_prepare = cx25821_buffer_prepare,
       .buf_queue = buffer_queue,
       .buf_release = cx25821_buffer_release,
};

static int video_open(struct file *file)
{
       struct video_device *vdev = video_devdata(file);
       struct cx25821_dev *h, *dev = video_drvdata(file);
       struct cx25821_fh *fh;
       struct list_head *list;
       int minor = video_devdata(file)->minor;
       enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
       u32 pix_format;
       int ch_id = 0;
       int i;

       dprintk(1, "open dev=%s type=%s\n",
		       video_device_node_name(vdev),
		       v4l2_type_names[type]);

       /* allocate + initialize per filehandle data */
       fh = kzalloc(sizeof(*fh), GFP_KERNEL);
       if (NULL == fh)
	       return -ENOMEM;

       lock_kernel();

       list_for_each(list, &cx25821_devlist)
       {
	       h = list_entry(list, struct cx25821_dev, devlist);

	       for (i = 0; i < MAX_VID_CHANNEL_NUM; i++) {
		       if (h->channels[i].video_dev &&
			   h->channels[i].video_dev->minor == minor) {
			       dev = h;
			       ch_id = i;
			       type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		       }
	       }
       }

       if (NULL == dev) {
	       unlock_kernel();
	       return -ENODEV;
       }

       file->private_data = fh;
       fh->dev = dev;
       fh->type = type;
       fh->width = 720;
    fh->channel_id = ch_id;

       if (dev->tvnorm & V4L2_STD_PAL_BG || dev->tvnorm & V4L2_STD_PAL_DK)
	       fh->height = 576;
       else
	       fh->height = 480;

       dev->channel_opened = fh->channel_id;
       pix_format =
	   (dev->channels[ch_id].pixel_formats ==
	    PIXEL_FRMT_411) ? V4L2_PIX_FMT_Y41P : V4L2_PIX_FMT_YUYV;
       fh->fmt = cx25821_format_by_fourcc(pix_format);

       v4l2_prio_open(&dev->channels[ch_id].prio, &fh->prio);

       videobuf_queue_sg_init(&fh->vidq, &cx25821_video_qops,
			      &dev->pci->dev, &dev->slock,
			      V4L2_BUF_TYPE_VIDEO_CAPTURE,
			      V4L2_FIELD_INTERLACED,
			      sizeof(struct cx25821_buffer), fh, NULL);

       dprintk(1, "post videobuf_queue_init()\n");
       unlock_kernel();

       return 0;
}

static ssize_t video_read(struct file *file, char __user * data, size_t count,
			 loff_t *ppos)
{
       struct cx25821_fh *fh = file->private_data;

       switch (fh->type) {
       case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	       if (cx25821_res_locked(fh, RESOURCE_VIDEO0))
		       return -EBUSY;

	       return videobuf_read_one(&fh->vidq, data, count, ppos,
					file->f_flags & O_NONBLOCK);

       default:
	       BUG();
	       return 0;
       }
}

static unsigned int video_poll(struct file *file,
			      struct poll_table_struct *wait)
{
       struct cx25821_fh *fh = file->private_data;
       struct cx25821_buffer *buf;

       if (cx25821_res_check(fh, RESOURCE_VIDEO0)) {
	       /* streaming capture */
	       if (list_empty(&fh->vidq.stream))
		       return POLLERR;
	       buf = list_entry(fh->vidq.stream.next,
				struct cx25821_buffer, vb.stream);
       } else {
	       /* read() capture */
	       buf = (struct cx25821_buffer *)fh->vidq.read_buf;
	       if (NULL == buf)
		       return POLLERR;
       }

       poll_wait(file, &buf->vb.done, wait);
       if (buf->vb.state == VIDEOBUF_DONE || buf->vb.state == VIDEOBUF_ERROR) {
	       if (buf->vb.state == VIDEOBUF_DONE) {
		       struct cx25821_dev *dev = fh->dev;

		       if (dev && dev->channels[fh->channel_id]
					       .use_cif_resolution) {
			       u8 cam_id = *((char *)buf->vb.baddr + 3);
			       memcpy((char *)buf->vb.baddr,
				      (char *)buf->vb.baddr + (fh->width * 2),
				      (fh->width * 2));
			       *((char *)buf->vb.baddr + 3) = cam_id;
		       }
	       }

	       return POLLIN | POLLRDNORM;
       }

       return 0;
}

static int video_release(struct file *file)
{
       struct cx25821_fh *fh = file->private_data;
       struct cx25821_dev *dev = fh->dev;

       /* stop the risc engine and fifo */
       cx_write(channel0->dma_ctl, 0); /* FIFO and RISC disable */

       /* stop video capture */
       if (cx25821_res_check(fh, RESOURCE_VIDEO0)) {
	       videobuf_queue_cancel(&fh->vidq);
	       cx25821_res_free(dev, fh, RESOURCE_VIDEO0);
       }

       if (fh->vidq.read_buf) {
	       cx25821_buffer_release(&fh->vidq, fh->vidq.read_buf);
	       kfree(fh->vidq.read_buf);
       }

       videobuf_mmap_free(&fh->vidq);

       v4l2_prio_close(&dev->channels[fh->channel_id].prio, fh->prio);
       file->private_data = NULL;
       kfree(fh);

       return 0;
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
       struct cx25821_fh *fh = priv;
       struct cx25821_dev *dev = fh->dev;

       if (unlikely(fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE))
	       return -EINVAL;

       if (unlikely(i != fh->type))
	       return -EINVAL;

       if (unlikely(!cx25821_res_get(dev, fh,
		       cx25821_get_resource(fh, RESOURCE_VIDEO0))))
	       return -EBUSY;

       return videobuf_streamon(get_queue(fh));
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
       struct cx25821_fh *fh = priv;
       struct cx25821_dev *dev = fh->dev;
       int err, res;

       if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
	       return -EINVAL;
       if (i != fh->type)
	       return -EINVAL;

       res = cx25821_get_resource(fh, RESOURCE_VIDEO0);
       err = videobuf_streamoff(get_queue(fh));
       if (err < 0)
	       return err;
       cx25821_res_free(dev, fh, res);
       return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
			       struct v4l2_format *f)
{
       struct cx25821_fh *fh = priv;
       struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
	struct v4l2_mbus_framefmt mbus_fmt;
       int err;
       int pix_format = PIXEL_FRMT_422;

       if (fh) {
	       err = v4l2_prio_check(&dev->channels[fh->channel_id]
					       .prio, fh->prio);
	       if (0 != err)
		       return err;
       }

       dprintk(2, "%s()\n", __func__);
       err = cx25821_vidioc_try_fmt_vid_cap(file, priv, f);

       if (0 != err)
	       return err;

       fh->fmt = cx25821_format_by_fourcc(f->fmt.pix.pixelformat);
       fh->vidq.field = f->fmt.pix.field;

       /* check if width and height is valid based on set standard */
       if (cx25821_is_valid_width(f->fmt.pix.width, dev->tvnorm))
	       fh->width = f->fmt.pix.width;

       if (cx25821_is_valid_height(f->fmt.pix.height, dev->tvnorm))
	       fh->height = f->fmt.pix.height;

       if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_Y41P)
	       pix_format = PIXEL_FRMT_411;
       else if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
	       pix_format = PIXEL_FRMT_422;
       else
	       return -EINVAL;

       cx25821_set_pixel_format(dev, SRAM_CH00, pix_format);

       /* check if cif resolution */
       if (fh->width == 320 || fh->width == 352)
	       dev->channels[fh->channel_id].use_cif_resolution = 1;
       else
	       dev->channels[fh->channel_id].use_cif_resolution = 0;

       dev->channels[fh->channel_id].cif_width = fh->width;
       medusa_set_resolution(dev, fh->width, SRAM_CH00);

	dprintk(2, "%s(): width=%d height=%d field=%d\n", __func__, fh->width,
		fh->height, fh->vidq.field);
	v4l2_fill_mbus_format(&mbus_fmt, &f->fmt.pix, V4L2_MBUS_FMT_FIXED);
	cx25821_call_all(dev, video, s_mbus_fmt, &mbus_fmt);

       return 0;
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
       int ret_val = 0;
       struct cx25821_fh *fh = priv;
       struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;

       ret_val = videobuf_dqbuf(get_queue(fh), p, file->f_flags & O_NONBLOCK);

    p->sequence = dev->channels[fh->channel_id].vidq.count;

       return ret_val;
}

static int vidioc_log_status(struct file *file, void *priv)
{
       struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
       struct cx25821_fh *fh = priv;
       char name[32 + 2];

       struct sram_channel *sram_ch = dev->channels[fh->channel_id]
						       .sram_channels;
       u32 tmp = 0;

       snprintf(name, sizeof(name), "%s/2", dev->name);
	pr_info("%s/2: ============  START LOG STATUS  ============\n",
		dev->name);
       cx25821_call_all(dev, core, log_status);
       tmp = cx_read(sram_ch->dma_ctl);
	pr_info("Video input 0 is %s\n",
		(tmp & 0x11) ? "streaming" : "stopped");
	pr_info("%s/2: =============  END LOG STATUS  =============\n",
		dev->name);
       return 0;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
			struct v4l2_control *ctl)
{
       struct cx25821_fh *fh = priv;
       struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
       int err;

       if (fh) {
	       err = v4l2_prio_check(&dev->channels[fh->channel_id]
					       .prio, fh->prio);
	       if (0 != err)
		       return err;
       }

       return cx25821_set_control(dev, ctl, fh->channel_id);
}

/* VIDEO IOCTLS                                                       */
int cx25821_vidioc_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
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

int cx25821_vidioc_try_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct cx25821_fmt *fmt;
	enum v4l2_field field;
	unsigned int maxw, maxh;

	fmt = cx25821_format_by_fourcc(f->fmt.pix.pixelformat);
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

int cx25821_vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
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

int cx25821_vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
			    struct v4l2_fmtdesc *f)
{
	if (unlikely(f->index >= ARRAY_SIZE(formats)))
		return -EINVAL;

	strlcpy(f->description, formats[f->index].name, sizeof(f->description));
	f->pixelformat = formats[f->index].fourcc;

	return 0;
}

int cx25821_vidioc_reqbufs(struct file *file, void *priv, struct v4l2_requestbuffers *p)
{
	struct cx25821_fh *fh = priv;
	return videobuf_reqbufs(get_queue(fh), p);
}

int cx25821_vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct cx25821_fh *fh = priv;
	return videobuf_querybuf(get_queue(fh), p);
}

int cx25821_vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct cx25821_fh *fh = priv;
	return videobuf_qbuf(get_queue(fh), p);
}

int cx25821_vidioc_g_priority(struct file *file, void *f, enum v4l2_priority *p)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)f)->dev;
       struct cx25821_fh *fh = f;

       *p = v4l2_prio_max(&dev->channels[fh->channel_id].prio);

	return 0;
}

int cx25821_vidioc_s_priority(struct file *file, void *f, enum v4l2_priority prio)
{
	struct cx25821_fh *fh = f;
	struct cx25821_dev *dev = ((struct cx25821_fh *)f)->dev;

       return v4l2_prio_change(&dev->channels[fh->channel_id]
				       .prio, &fh->prio, prio);
}

#ifdef TUNER_FLAG
int cx25821_vidioc_s_std(struct file *file, void *priv, v4l2_std_id * tvnorms)
{
	struct cx25821_fh *fh = priv;
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
	int err;

	dprintk(1, "%s()\n", __func__);

	if (fh) {
	       err = v4l2_prio_check(&dev->channels[fh->channel_id]
					       .prio, fh->prio);
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

	i->type = V4L2_INPUT_TYPE_CAMERA;
	strcpy(i->name, iname[INPUT(n)->type]);

	i->std = CX25821_NORMS;
	return 0;
}

int cx25821_vidioc_enum_input(struct file *file, void *priv, struct v4l2_input *i)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
	dprintk(1, "%s()\n", __func__);
	return cx25821_enum_input(dev, i);
}

int cx25821_vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;

	*i = dev->input;
	dprintk(1, "%s(): returns %d\n", __func__, *i);
	return 0;
}

int cx25821_vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct cx25821_fh *fh = priv;
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
	int err;

	dprintk(1, "%s(%d)\n", __func__, i);

	if (fh) {
	       err = v4l2_prio_check(&dev->channels[fh->channel_id]
					       .prio, fh->prio);
		if (0 != err)
			return err;
	}

	if (i > 2) {
		dprintk(1, "%s(): -EINVAL\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&dev->lock);
	cx25821_video_mux(dev, i);
	mutex_unlock(&dev->lock);
	return 0;
}

#ifdef TUNER_FLAG
int cx25821_vidioc_g_frequency(struct file *file, void *priv, struct v4l2_frequency *f)
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

int cx25821_vidioc_s_frequency(struct file *file, void *priv, struct v4l2_frequency *f)
{
	struct cx25821_fh *fh = priv;
	struct cx25821_dev *dev;
	int err;

	if (fh) {
	       dev = fh->dev;
	       err = v4l2_prio_check(&dev->channels[fh->channel_id]
					       .prio, fh->prio);
		if (0 != err)
			return err;
       } else {
	       pr_err("Invalid fh pointer!\n");
	       return -EINVAL;
	}

	return cx25821_set_freq(dev, f);
}
#endif

#ifdef CONFIG_VIDEO_ADV_DEBUG
int cx25821_vidioc_g_register(struct file *file, void *fh,
		      struct v4l2_dbg_register *reg)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)fh)->dev;

	if (!v4l2_chip_match_host(&reg->match))
		return -EINVAL;

	cx25821_call_all(dev, core, g_register, reg);

	return 0;
}

int cx25821_vidioc_s_register(struct file *file, void *fh,
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
int cx25821_vidioc_g_tuner(struct file *file, void *priv, struct v4l2_tuner *t)
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

int cx25821_vidioc_s_tuner(struct file *file, void *priv, struct v4l2_tuner *t)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
	struct cx25821_fh *fh = priv;
	int err;

	if (fh) {
	       err = v4l2_prio_check(&dev->channels[fh->channel_id]
					       .prio, fh->prio);
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
/*****************************************************************************/
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

int cx25821_vidioc_queryctrl(struct file *file, void *priv,
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

int cx25821_vidioc_g_ctrl(struct file *file, void *priv, struct v4l2_control *ctl)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
       struct cx25821_fh *fh = priv;

	const struct v4l2_queryctrl *ctrl;

	ctrl = ctrl_by_id(ctl->id);

	if (NULL == ctrl)
		return -EINVAL;
	switch (ctl->id) {
	case V4L2_CID_BRIGHTNESS:
	       ctl->value = dev->channels[fh->channel_id].ctl_bright;
		break;
	case V4L2_CID_HUE:
	       ctl->value = dev->channels[fh->channel_id].ctl_hue;
		break;
	case V4L2_CID_CONTRAST:
	       ctl->value = dev->channels[fh->channel_id].ctl_contrast;
		break;
	case V4L2_CID_SATURATION:
	       ctl->value = dev->channels[fh->channel_id].ctl_saturation;
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
	       dev->channels[chan_num].ctl_bright = ctl->value;
		medusa_set_brightness(dev, ctl->value, chan_num);
		break;
	case V4L2_CID_HUE:
	       dev->channels[chan_num].ctl_hue = ctl->value;
		medusa_set_hue(dev, ctl->value, chan_num);
		break;
	case V4L2_CID_CONTRAST:
	       dev->channels[chan_num].ctl_contrast = ctl->value;
		medusa_set_contrast(dev, ctl->value, chan_num);
		break;
	case V4L2_CID_SATURATION:
	       dev->channels[chan_num].ctl_saturation = ctl->value;
		medusa_set_saturation(dev, ctl->value, chan_num);
		break;
	}

	err = 0;

	return err;
}

static void cx25821_init_controls(struct cx25821_dev *dev, int chan_num)
{
	struct v4l2_control ctrl;
	int i;
	for (i = 0; i < CX25821_CTLS; i++) {
		ctrl.id = cx25821_ctls[i].id;
		ctrl.value = cx25821_ctls[i].default_value;

		cx25821_set_control(dev, &ctrl, chan_num);
	}
}

int cx25821_vidioc_cropcap(struct file *file, void *priv, struct v4l2_cropcap *cropcap)
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

int cx25821_vidioc_s_crop(struct file *file, void *priv, struct v4l2_crop *crop)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
	struct cx25821_fh *fh = priv;
	int err;

	if (fh) {
	       err = v4l2_prio_check(&dev->channels[fh->channel_id].
					       prio, fh->prio);
		if (0 != err)
			return err;
	}
       /* cx25821_vidioc_s_crop not supported */
	return -EINVAL;
}

int cx25821_vidioc_g_crop(struct file *file, void *priv, struct v4l2_crop *crop)
{
       /* cx25821_vidioc_g_crop not supported */
	return -EINVAL;
}

int cx25821_vidioc_querystd(struct file *file, void *priv, v4l2_std_id * norm)
{
       /* medusa does not support video standard sensing of current input */
	*norm = CX25821_NORMS;

	return 0;
}

int cx25821_is_valid_width(u32 width, v4l2_std_id tvnorm)
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

int cx25821_is_valid_height(u32 height, v4l2_std_id tvnorm)
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

static long video_ioctl_upstream9(struct file *file, unsigned int cmd,
				 unsigned long arg)
{
       struct cx25821_fh *fh = file->private_data;
       struct cx25821_dev *dev = fh->dev;
       int command = 0;
       struct upstream_user_struct *data_from_user;

       data_from_user = (struct upstream_user_struct *)arg;

	if (!data_from_user) {
		pr_err("%s(): Upstream data is INVALID. Returning\n", __func__);
		return 0;
	}

       command = data_from_user->command;

       if (command != UPSTREAM_START_VIDEO &&
	       command != UPSTREAM_STOP_VIDEO)
	       return 0;

       dev->input_filename = data_from_user->input_filename;
       dev->input_audiofilename = data_from_user->input_filename;
       dev->vid_stdname = data_from_user->vid_stdname;
       dev->pixel_format = data_from_user->pixel_format;
       dev->channel_select = data_from_user->channel_select;
       dev->command = data_from_user->command;

       switch (command) {
       case UPSTREAM_START_VIDEO:
	       cx25821_start_upstream_video_ch1(dev, data_from_user);
	       break;

       case UPSTREAM_STOP_VIDEO:
	       cx25821_stop_upstream_video_ch1(dev);
	       break;
       }

       return 0;
}

static long video_ioctl_upstream10(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
       struct cx25821_fh *fh = file->private_data;
       struct cx25821_dev *dev = fh->dev;
       int command = 0;
       struct upstream_user_struct *data_from_user;

       data_from_user = (struct upstream_user_struct *)arg;

	if (!data_from_user) {
		pr_err("%s(): Upstream data is INVALID. Returning\n", __func__);
		return 0;
	}

       command = data_from_user->command;

       if (command != UPSTREAM_START_VIDEO &&
	       command != UPSTREAM_STOP_VIDEO)
	       return 0;

       dev->input_filename_ch2 = data_from_user->input_filename;
       dev->input_audiofilename = data_from_user->input_filename;
       dev->vid_stdname_ch2 = data_from_user->vid_stdname;
       dev->pixel_format_ch2 = data_from_user->pixel_format;
       dev->channel_select_ch2 = data_from_user->channel_select;
       dev->command_ch2 = data_from_user->command;

       switch (command) {
       case UPSTREAM_START_VIDEO:
	       cx25821_start_upstream_video_ch2(dev, data_from_user);
	       break;

       case UPSTREAM_STOP_VIDEO:
	       cx25821_stop_upstream_video_ch2(dev);
	       break;
       }

       return 0;
}

static long video_ioctl_upstream11(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
       struct cx25821_fh *fh = file->private_data;
       struct cx25821_dev *dev = fh->dev;
       int command = 0;
       struct upstream_user_struct *data_from_user;

       data_from_user = (struct upstream_user_struct *)arg;

	if (!data_from_user) {
		pr_err("%s(): Upstream data is INVALID. Returning\n", __func__);
		return 0;
	}

       command = data_from_user->command;

       if (command != UPSTREAM_START_AUDIO &&
	       command != UPSTREAM_STOP_AUDIO)
	       return 0;

       dev->input_filename = data_from_user->input_filename;
       dev->input_audiofilename = data_from_user->input_filename;
       dev->vid_stdname = data_from_user->vid_stdname;
       dev->pixel_format = data_from_user->pixel_format;
       dev->channel_select = data_from_user->channel_select;
       dev->command = data_from_user->command;

       switch (command) {
       case UPSTREAM_START_AUDIO:
	       cx25821_start_upstream_audio(dev, data_from_user);
	       break;

       case UPSTREAM_STOP_AUDIO:
	       cx25821_stop_upstream_audio(dev);
	       break;
       }

       return 0;
}

static long video_ioctl_set(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
       struct cx25821_fh *fh = file->private_data;
       struct cx25821_dev *dev = fh->dev;
       struct downstream_user_struct *data_from_user;
       int command;
       int width = 720;
       int selected_channel = 0, pix_format = 0, i = 0;
       int cif_enable = 0, cif_width = 0;
       u32 value = 0;

       data_from_user = (struct downstream_user_struct *)arg;

	if (!data_from_user) {
		pr_err("%s(): User data is INVALID. Returning\n", __func__);
		return 0;
	}

       command = data_from_user->command;

       if (command != SET_VIDEO_STD && command != SET_PIXEL_FORMAT
	   && command != ENABLE_CIF_RESOLUTION && command != REG_READ
	   && command != REG_WRITE && command != MEDUSA_READ
	   && command != MEDUSA_WRITE) {
	       return 0;
       }

       switch (command) {
       case SET_VIDEO_STD:
	       dev->tvnorm =
		   !strcmp(data_from_user->vid_stdname,
			   "PAL") ? V4L2_STD_PAL_BG : V4L2_STD_NTSC_M;
	       medusa_set_videostandard(dev);
	       break;

       case SET_PIXEL_FORMAT:
	       selected_channel = data_from_user->decoder_select;
	       pix_format = data_from_user->pixel_format;

	       if (!(selected_channel <= 7 && selected_channel >= 0)) {
		       selected_channel -= 4;
		       selected_channel = selected_channel % 8;
	       }

	       if (selected_channel >= 0)
		       cx25821_set_pixel_format(dev, selected_channel,
						pix_format);

	       break;

       case ENABLE_CIF_RESOLUTION:
	       selected_channel = data_from_user->decoder_select;
	       cif_enable = data_from_user->cif_resolution_enable;
	       cif_width = data_from_user->cif_width;

	       if (cif_enable) {
		       if (dev->tvnorm & V4L2_STD_PAL_BG
			   || dev->tvnorm & V4L2_STD_PAL_DK)
			       width = 352;
		       else
			       width = (cif_width == 320
					|| cif_width == 352) ? cif_width : 320;
	       }

	       if (!(selected_channel <= 7 && selected_channel >= 0)) {
		       selected_channel -= 4;
		       selected_channel = selected_channel % 8;
	       }

	       if (selected_channel <= 7 && selected_channel >= 0) {
		       dev->channels[selected_channel].
			       use_cif_resolution = cif_enable;
		       dev->channels[selected_channel].cif_width = width;
	       } else {
		       for (i = 0; i < VID_CHANNEL_NUM; i++) {
			       dev->channels[i].use_cif_resolution =
				       cif_enable;
			       dev->channels[i].cif_width = width;
		       }
	       }

	       medusa_set_resolution(dev, width, selected_channel);
	       break;
       case REG_READ:
	       data_from_user->reg_data = cx_read(data_from_user->reg_address);
	       break;
       case REG_WRITE:
	       cx_write(data_from_user->reg_address, data_from_user->reg_data);
	       break;
       case MEDUSA_READ:
	       value =
		   cx25821_i2c_read(&dev->i2c_bus[0],
				    (u16) data_from_user->reg_address,
				    &data_from_user->reg_data);
	       break;
       case MEDUSA_WRITE:
	       cx25821_i2c_write(&dev->i2c_bus[0],
				 (u16) data_from_user->reg_address,
				 data_from_user->reg_data);
	       break;
       }

       return 0;
}

static long cx25821_video_ioctl(struct file *file,
			       unsigned int cmd, unsigned long arg)
{
       int  ret = 0;

       struct cx25821_fh  *fh  = file->private_data;

       /* check to see if it's the video upstream */
       if (fh->channel_id == SRAM_CH09) {
	       ret = video_ioctl_upstream9(file, cmd, arg);
	       return ret;
       } else if (fh->channel_id == SRAM_CH10) {
	       ret = video_ioctl_upstream10(file, cmd, arg);
	       return ret;
       } else if (fh->channel_id == SRAM_CH11) {
	       ret = video_ioctl_upstream11(file, cmd, arg);
	       ret = video_ioctl_set(file, cmd, arg);
	       return ret;
       }

    return video_ioctl2(file, cmd, arg);
}

/* exported stuff */
static const struct v4l2_file_operations video_fops = {
       .owner = THIS_MODULE,
       .open = video_open,
       .release = video_release,
       .read = video_read,
       .poll = video_poll,
       .mmap = cx25821_video_mmap,
       .ioctl = cx25821_video_ioctl,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
       .vidioc_querycap = cx25821_vidioc_querycap,
       .vidioc_enum_fmt_vid_cap = cx25821_vidioc_enum_fmt_vid_cap,
       .vidioc_g_fmt_vid_cap = cx25821_vidioc_g_fmt_vid_cap,
       .vidioc_try_fmt_vid_cap = cx25821_vidioc_try_fmt_vid_cap,
       .vidioc_s_fmt_vid_cap = vidioc_s_fmt_vid_cap,
       .vidioc_reqbufs = cx25821_vidioc_reqbufs,
       .vidioc_querybuf = cx25821_vidioc_querybuf,
       .vidioc_qbuf = cx25821_vidioc_qbuf,
       .vidioc_dqbuf = vidioc_dqbuf,
#ifdef TUNER_FLAG
       .vidioc_s_std = cx25821_vidioc_s_std,
       .vidioc_querystd = cx25821_vidioc_querystd,
#endif
       .vidioc_cropcap = cx25821_vidioc_cropcap,
       .vidioc_s_crop = cx25821_vidioc_s_crop,
       .vidioc_g_crop = cx25821_vidioc_g_crop,
       .vidioc_enum_input = cx25821_vidioc_enum_input,
       .vidioc_g_input = cx25821_vidioc_g_input,
       .vidioc_s_input = cx25821_vidioc_s_input,
       .vidioc_g_ctrl = cx25821_vidioc_g_ctrl,
       .vidioc_s_ctrl = vidioc_s_ctrl,
       .vidioc_queryctrl = cx25821_vidioc_queryctrl,
       .vidioc_streamon = vidioc_streamon,
       .vidioc_streamoff = vidioc_streamoff,
       .vidioc_log_status = vidioc_log_status,
       .vidioc_g_priority = cx25821_vidioc_g_priority,
       .vidioc_s_priority = cx25821_vidioc_s_priority,
#ifdef TUNER_FLAG
       .vidioc_g_tuner = cx25821_vidioc_g_tuner,
       .vidioc_s_tuner = cx25821_vidioc_s_tuner,
       .vidioc_g_frequency = cx25821_vidioc_g_frequency,
       .vidioc_s_frequency = cx25821_vidioc_s_frequency,
#endif
#ifdef CONFIG_VIDEO_ADV_DEBUG
       .vidioc_g_register = cx25821_vidioc_g_register,
       .vidioc_s_register = cx25821_vidioc_s_register,
#endif
};

struct video_device cx25821_videoioctl_template = {
	       .name = "cx25821-videoioctl",
	       .fops = &video_fops,
	       .ioctl_ops = &video_ioctl_ops,
	       .tvnorms = CX25821_NORMS,
	       .current_norm = V4L2_STD_NTSC_M,
};

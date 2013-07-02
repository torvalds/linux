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

static unsigned int vid_limit = 16;
module_param(vid_limit, int, 0644);
MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

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

		buf = list_entry(q->active.next, struct cx25821_buffer,
				vb.queue);

		/* count comes from the hw and it is 16bit wide --
		 * this trick handles wrap-arounds correctly for
		 * up to 32767 buffers in flight... */
		if ((s16) (count - buf->count) < 0)
			break;

		v4l2_get_timestamp(&buf->vb.ts);
		buf->vb.state = VIDEOBUF_DONE;
		list_del(&buf->vb.queue);
		wake_up(&buf->vb.done);
	}

	if (list_empty(&q->active))
		del_timer(&q->timeout);
	else
		mod_timer(&q->timeout, jiffies + BUFFER_TIMEOUT);
	if (bc != 1)
		pr_err("%s: %d buffers handled (should be 1)\n", __func__, bc);
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

static int cx25821_restart_video_queue(struct cx25821_dev *dev,
				       struct cx25821_dmaqueue *q,
				       const struct sram_channel *channel)
{
	struct cx25821_buffer *buf, *prev;
	struct list_head *item;

	if (!list_empty(&q->active)) {
		buf = list_entry(q->active.next, struct cx25821_buffer,
				vb.queue);

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

		buf = list_entry(q->queued.next, struct cx25821_buffer,
				vb.queue);

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
			prev->risc.jmp[2] = cpu_to_le32(0); /* Bits 63 - 32 */
		} else {
			return 0;
		}
		prev = buf;
	}
}

static void cx25821_vid_timeout(unsigned long data)
{
	struct cx25821_data *timeout_data = (struct cx25821_data *)data;
	struct cx25821_dev *dev = timeout_data->dev;
	const struct sram_channel *channel = timeout_data->channel;
	struct cx25821_dmaqueue *q = &dev->channels[channel->i].dma_vidq;
	struct cx25821_buffer *buf;
	unsigned long flags;

	/* cx25821_sram_channel_dump(dev, channel); */
	cx_clear(channel->dma_ctl, 0x11);

	spin_lock_irqsave(&dev->slock, flags);
	while (!list_empty(&q->active)) {
		buf = list_entry(q->active.next, struct cx25821_buffer,
				vb.queue);
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
		spin_lock(&dev->slock);
		count = cx_read(channel->gpcnt);
		cx25821_video_wakeup(dev, &dev->channels[channel->i].dma_vidq,
				count);
		spin_unlock(&dev->slock);
		handled++;
	}

	/* risc2 y */
	if (status & 0x10) {
		dprintk(2, "stopper video\n");
		spin_lock(&dev->slock);
		cx25821_restart_video_queue(dev,
				&dev->channels[channel->i].dma_vidq, channel);
		spin_unlock(&dev->slock);
		handled++;
	}
	return handled;
}

static int cx25821_buffer_setup(struct videobuf_queue *q, unsigned int *count,
		 unsigned int *size)
{
	struct cx25821_channel *chan = q->priv_data;

	*size = chan->fmt->depth * chan->width * chan->height >> 3;

	if (0 == *count)
		*count = 32;

	if (*size * *count > vid_limit * 1024 * 1024)
		*count = (vid_limit * 1024 * 1024) / *size;

	return 0;
}

static int cx25821_buffer_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
		   enum v4l2_field field)
{
	struct cx25821_channel *chan = q->priv_data;
	struct cx25821_dev *dev = chan->dev;
	struct cx25821_buffer *buf =
		container_of(vb, struct cx25821_buffer, vb);
	int rc, init_buffer = 0;
	u32 line0_offset;
	struct videobuf_dmabuf *dma = videobuf_to_dma(&buf->vb);
	int bpl_local = LINE_SIZE_D1;

	BUG_ON(NULL == chan->fmt);
	if (chan->width < 48 || chan->width > 720 ||
	    chan->height < 32 || chan->height > 576)
		return -EINVAL;

	buf->vb.size = (chan->width * chan->height * chan->fmt->depth) >> 3;

	if (0 != buf->vb.baddr && buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	if (buf->fmt != chan->fmt ||
	    buf->vb.width != chan->width ||
	    buf->vb.height != chan->height || buf->vb.field != field) {
		buf->fmt = chan->fmt;
		buf->vb.width = chan->width;
		buf->vb.height = chan->height;
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
		if (chan->pixel_formats == PIXEL_FRMT_411)
			buf->bpl = (buf->fmt->depth * buf->vb.width) >> 3;
		else
			buf->bpl = (buf->fmt->depth >> 3) * (buf->vb.width);

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
		buf, buf->vb.i, chan->width, chan->height, chan->fmt->depth,
		chan->fmt->name, (unsigned long)buf->risc.dma);

	buf->vb.state = VIDEOBUF_PREPARED;

	return 0;

fail:
	cx25821_free_buffer(q, buf);
	return rc;
}

static void cx25821_buffer_release(struct videobuf_queue *q,
			    struct videobuf_buffer *vb)
{
	struct cx25821_buffer *buf =
		container_of(vb, struct cx25821_buffer, vb);

	cx25821_free_buffer(q, buf);
}

static int cx25821_video_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct cx25821_channel *chan = video_drvdata(file);

	return videobuf_mmap_mapper(&chan->vidq, vma);
}


static void buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct cx25821_buffer *buf =
		container_of(vb, struct cx25821_buffer, vb);
	struct cx25821_buffer *prev;
	struct cx25821_channel *chan = vq->priv_data;
	struct cx25821_dev *dev = chan->dev;
	struct cx25821_dmaqueue *q = &dev->channels[chan->id].dma_vidq;

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
		cx25821_start_video_dma(dev, q, buf, chan->sram_channels);
		buf->vb.state = VIDEOBUF_ACTIVE;
		buf->count = q->count++;
		mod_timer(&q->timeout, jiffies + BUFFER_TIMEOUT);
		dprintk(2, "[%p/%d] buffer_queue - first active, buf cnt = %d, q->count = %d\n",
				buf, buf->vb.i, buf->count, q->count);
	} else {
		prev = list_entry(q->active.prev, struct cx25821_buffer,
				vb.queue);
		if (prev->vb.width == buf->vb.width
		   && prev->vb.height == buf->vb.height
		   && prev->fmt == buf->fmt) {
			list_add_tail(&buf->vb.queue, &q->active);
			buf->vb.state = VIDEOBUF_ACTIVE;
			buf->count = q->count++;
			prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);

			/* 64 bit bits 63-32 */
			prev->risc.jmp[2] = cpu_to_le32(0);
			dprintk(2, "[%p/%d] buffer_queue - append to active, buf->count=%d\n",
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

static ssize_t video_read(struct file *file, char __user * data, size_t count,
			 loff_t *ppos)
{
	struct v4l2_fh *fh = file->private_data;
	struct cx25821_channel *chan = video_drvdata(file);
	struct cx25821_dev *dev = chan->dev;
	int err = 0;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (chan->streaming_fh && chan->streaming_fh != fh) {
		err = -EBUSY;
		goto unlock;
	}
	chan->streaming_fh = fh;

	err = videobuf_read_one(&chan->vidq, data, count, ppos,
				file->f_flags & O_NONBLOCK);
unlock:
	mutex_unlock(&dev->lock);
	return err;
}

static unsigned int video_poll(struct file *file,
			      struct poll_table_struct *wait)
{
	struct cx25821_channel *chan = video_drvdata(file);
	unsigned long req_events = poll_requested_events(wait);
	unsigned int res = v4l2_ctrl_poll(file, wait);

	if (req_events & (POLLIN | POLLRDNORM))
		res |= videobuf_poll_stream(file, &chan->vidq, wait);
	return res;

	/* This doesn't belong in poll(). This can be done
	 * much better with vb2. We keep this code here as a
	 * reminder.
	if ((res & POLLIN) && buf->vb.state == VIDEOBUF_DONE) {
		struct cx25821_dev *dev = chan->dev;

		if (dev && chan->use_cif_resolution) {
			u8 cam_id = *((char *)buf->vb.baddr + 3);
			memcpy((char *)buf->vb.baddr,
					(char *)buf->vb.baddr + (chan->width * 2),
					(chan->width * 2));
			*((char *)buf->vb.baddr + 3) = cam_id;
		}
	}
	 */
}

static int video_release(struct file *file)
{
	struct cx25821_channel *chan = video_drvdata(file);
	struct v4l2_fh *fh = file->private_data;
	struct cx25821_dev *dev = chan->dev;
	const struct sram_channel *sram_ch =
		dev->channels[0].sram_channels;

	mutex_lock(&dev->lock);
	/* stop the risc engine and fifo */
	cx_write(sram_ch->dma_ctl, 0); /* FIFO and RISC disable */

	/* stop video capture */
	if (chan->streaming_fh == fh) {
		videobuf_queue_cancel(&chan->vidq);
		chan->streaming_fh = NULL;
	}

	if (chan->vidq.read_buf) {
		cx25821_buffer_release(&chan->vidq, chan->vidq.read_buf);
		kfree(chan->vidq.read_buf);
	}

	videobuf_mmap_free(&chan->vidq);
	mutex_unlock(&dev->lock);

	return v4l2_fh_release(file);
}

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
	f->fmt.pix.field = chan->vidq.field;
	f->fmt.pix.pixelformat = chan->fmt->fourcc;
	f->fmt.pix.bytesperline = (chan->width * chan->fmt->depth) >> 3;
	f->fmt.pix.sizeimage = chan->height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.priv = 0;

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
	f->fmt.pix.priv = 0;

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
	chan->vidq.field = f->fmt.pix.field;
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

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct cx25821_channel *chan = video_drvdata(file);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (chan->streaming_fh && chan->streaming_fh != priv)
		return -EBUSY;
	chan->streaming_fh = priv;

	return videobuf_streamon(&chan->vidq);
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct cx25821_channel *chan = video_drvdata(file);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (chan->streaming_fh && chan->streaming_fh != priv)
		return -EBUSY;
	if (chan->streaming_fh == NULL)
		return 0;

	chan->streaming_fh = NULL;
	return videobuf_streamoff(&chan->vidq);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	int ret_val = 0;
	struct cx25821_channel *chan = video_drvdata(file);

	ret_val = videobuf_dqbuf(&chan->vidq, p, file->f_flags & O_NONBLOCK);
	p->sequence = chan->dma_vidq.count;

	return ret_val;
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

static int cx25821_vidioc_reqbufs(struct file *file, void *priv,
			   struct v4l2_requestbuffers *p)
{
	struct cx25821_channel *chan = video_drvdata(file);

	return videobuf_reqbufs(&chan->vidq, p);
}

static int cx25821_vidioc_querybuf(struct file *file, void *priv,
			    struct v4l2_buffer *p)
{
	struct cx25821_channel *chan = video_drvdata(file);

	return videobuf_querybuf(&chan->vidq, p);
}

static int cx25821_vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct cx25821_channel *chan = video_drvdata(file);

	return videobuf_qbuf(&chan->vidq, p);
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
	f->fmt.pix.priv = 0;
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
	chan->vidq.field = f->fmt.pix.field;
	chan->width = f->fmt.pix.width;
	chan->height = f->fmt.pix.height;
	if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_Y41P)
		chan->pixel_formats = PIXEL_FRMT_411;
	else
		chan->pixel_formats = PIXEL_FRMT_422;
	return 0;
}

static ssize_t video_write(struct file *file, const char __user *data, size_t count,
			 loff_t *ppos)
{
	struct cx25821_channel *chan = video_drvdata(file);
	struct cx25821_dev *dev = chan->dev;
	struct v4l2_fh *fh = file->private_data;
	int err = 0;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (chan->streaming_fh && chan->streaming_fh != fh) {
		err = -EBUSY;
		goto unlock;
	}
	if (!chan->streaming_fh) {
		err = cx25821_vidupstream_init(chan, chan->pixel_formats);
		if (err)
			goto unlock;
		chan->streaming_fh = fh;
	}

	err = cx25821_write_frame(chan, data, count);
	count -= err;
	*ppos += err;

unlock:
	mutex_unlock(&dev->lock);
	return err;
}

static int video_out_release(struct file *file)
{
	struct cx25821_channel *chan = video_drvdata(file);
	struct cx25821_dev *dev = chan->dev;
	struct v4l2_fh *fh = file->private_data;

	mutex_lock(&dev->lock);
	if (chan->streaming_fh == fh) {
		cx25821_stop_upstream_video(chan);
		chan->streaming_fh = NULL;
	}
	mutex_unlock(&dev->lock);

	return v4l2_fh_release(file);
}

static const struct v4l2_ctrl_ops cx25821_ctrl_ops = {
	.s_ctrl = cx25821_s_ctrl,
};

static const struct v4l2_file_operations video_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = video_release,
	.read = video_read,
	.poll = video_poll,
	.mmap = cx25821_video_mmap,
	.unlocked_ioctl = video_ioctl2,
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
	.vidioc_g_std = cx25821_vidioc_g_std,
	.vidioc_s_std = cx25821_vidioc_s_std,
	.vidioc_enum_input = cx25821_vidioc_enum_input,
	.vidioc_g_input = cx25821_vidioc_g_input,
	.vidioc_s_input = cx25821_vidioc_s_input,
	.vidioc_streamon = vidioc_streamon,
	.vidioc_streamoff = vidioc_streamoff,
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
	.write = video_write,
	.release = video_out_release,
	.unlocked_ioctl = video_ioctl2,
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

		btcx_riscmem_free(dev->pci,
				&dev->channels[chan_num].dma_vidq.stopper);
	}
}

int cx25821_video_register(struct cx25821_dev *dev)
{
	int err;
	int i;

	/* initial device configuration */
	dev->tvnorm = V4L2_STD_NTSC_M;

	spin_lock_init(&dev->slock);

	for (i = 0; i < MAX_VID_CHANNEL_NUM - 1; ++i) {
		struct cx25821_channel *chan = &dev->channels[i];
		struct video_device *vdev = &chan->vdev;
		struct v4l2_ctrl_handler *hdl = &chan->hdl;
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

		cx25821_risc_stopper(dev->pci, &chan->dma_vidq.stopper,
			chan->sram_channels->dma_ctl, 0x11, 0);

		chan->sram_channels = &cx25821_sram_channels[i];
		chan->width = 720;
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
		INIT_LIST_HEAD(&chan->dma_vidq.queued);

		chan->timeout_data.dev = dev;
		chan->timeout_data.channel = &cx25821_sram_channels[i];
		chan->dma_vidq.timeout.function = cx25821_vid_timeout;
		chan->dma_vidq.timeout.data = (unsigned long)&chan->timeout_data;
		init_timer(&chan->dma_vidq.timeout);

		if (!is_output)
			videobuf_queue_sg_init(&chan->vidq, &cx25821_video_qops, &dev->pci->dev,
				&dev->slock, V4L2_BUF_TYPE_VIDEO_CAPTURE,
				V4L2_FIELD_INTERLACED, sizeof(struct cx25821_buffer),
				chan, &dev->lock);

		/* register v4l devices */
		*vdev = is_output ? cx25821_video_out_device : cx25821_video_device;
		vdev->v4l2_dev = &dev->v4l2_dev;
		if (!is_output)
			vdev->ctrl_handler = hdl;
		else
			vdev->vfl_dir = VFL_DIR_TX;
		vdev->lock = &dev->lock;
		set_bit(V4L2_FL_USE_FH_PRIO, &vdev->flags);
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

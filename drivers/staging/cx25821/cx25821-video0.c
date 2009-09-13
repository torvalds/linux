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

static void buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct cx25821_buffer *buf =
	    container_of(vb, struct cx25821_buffer, vb);
	struct cx25821_buffer *prev;
	struct cx25821_fh *fh = vq->priv_data;
	struct cx25821_dev *dev = fh->dev;
	struct cx25821_dmaqueue *q = &dev->vidq[SRAM_CH00];

	/* add jump to stopper */
	buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_IRQ1 | RISC_CNT_INC);
	buf->risc.jmp[1] = cpu_to_le32(q->stopper.dma);
	buf->risc.jmp[2] = cpu_to_le32(0);	/* bits 63-32 */

	dprintk(2, "jmp to stopper (0x%x)\n", buf->risc.jmp[1]);

	if (!list_empty(&q->queued)) {
		list_add_tail(&buf->vb.queue, &q->queued);
		buf->vb.state = VIDEOBUF_QUEUED;
		dprintk(2, "[%p/%d] buffer_queue - append to queued\n", buf,
			buf->vb.i);

	} else if (list_empty(&q->active)) {
		list_add_tail(&buf->vb.queue, &q->active);
		cx25821_start_video_dma(dev, q, buf,
					&dev->sram_channels[SRAM_CH00]);
		buf->vb.state = VIDEOBUF_ACTIVE;
		buf->count = q->count++;
		mod_timer(&q->timeout, jiffies + BUFFER_TIMEOUT);
		dprintk(2,
			"[%p/%d] buffer_queue - first active, buf cnt = %d, q->count = %d\n",
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
				"[%p/%d] buffer_queue - append to active, buf->count=%d\n",
				buf, buf->vb.i, buf->count);

		} else {
			list_add_tail(&buf->vb.queue, &q->queued);
			buf->vb.state = VIDEOBUF_QUEUED;
			dprintk(2, "[%p/%d] buffer_queue - first queued\n", buf,
				buf->vb.i);
		}
	}

	if (list_empty(&q->active)) {
		dprintk(2, "active queue empty!\n");
	}
}

static struct videobuf_queue_ops cx25821_video_qops = {
	.buf_setup = buffer_setup,
	.buf_prepare = buffer_prepare,
	.buf_queue = buffer_queue,
	.buf_release = buffer_release,
};

static int video_open(struct file *file)
{
	int minor = video_devdata(file)->minor;
	struct cx25821_dev *h, *dev = NULL;
	struct cx25821_fh *fh;
	struct list_head *list;
	enum v4l2_buf_type type = 0;
	u32 pix_format;

	lock_kernel();
	list_for_each(list, &cx25821_devlist) {
		h = list_entry(list, struct cx25821_dev, devlist);

		if (h->video_dev[SRAM_CH00]
		    && h->video_dev[SRAM_CH00]->minor == minor) {
			dev = h;
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		}
	}

	if (NULL == dev) {
		unlock_kernel();
		return -ENODEV;
	}

	printk("open minor=%d type=%s\n", minor, v4l2_type_names[type]);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh) {
		unlock_kernel();
		return -ENOMEM;
	}

	file->private_data = fh;
	fh->dev = dev;
	fh->type = type;
	fh->width = 720;

	if (dev->tvnorm & V4L2_STD_PAL_BG || dev->tvnorm & V4L2_STD_PAL_DK)
		fh->height = 576;
	else
		fh->height = 480;

	dev->channel_opened = SRAM_CH00;
	pix_format =
	    (dev->pixel_formats[dev->channel_opened] ==
	     PIXEL_FRMT_411) ? V4L2_PIX_FMT_Y41P : V4L2_PIX_FMT_YUYV;
	fh->fmt = format_by_fourcc(pix_format);

	v4l2_prio_open(&dev->prio, &fh->prio);

	videobuf_queue_sg_init(&fh->vidq, &cx25821_video_qops,
			       &dev->pci->dev, &dev->slock,
			       V4L2_BUF_TYPE_VIDEO_CAPTURE,
			       V4L2_FIELD_INTERLACED,
			       sizeof(struct cx25821_buffer), fh);

	dprintk(1, "post videobuf_queue_init()\n");
	unlock_kernel();

	return 0;
}

static ssize_t video_read(struct file *file, char __user * data, size_t count,
			  loff_t * ppos)
{
	struct cx25821_fh *fh = file->private_data;

	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (res_locked(fh->dev, RESOURCE_VIDEO0))
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

	if (res_check(fh, RESOURCE_VIDEO0)) {
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

			if (dev && dev->use_cif_resolution[SRAM_CH00]) {
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

	//stop the risc engine and fifo
	cx_write(channel0->dma_ctl, 0);	/* FIFO and RISC disable */

	/* stop video capture */
	if (res_check(fh, RESOURCE_VIDEO0)) {
		videobuf_queue_cancel(&fh->vidq);
		res_free(dev, fh, RESOURCE_VIDEO0);
	}

	if (fh->vidq.read_buf) {
		buffer_release(&fh->vidq, fh->vidq.read_buf);
		kfree(fh->vidq.read_buf);
	}

	videobuf_mmap_free(&fh->vidq);

	v4l2_prio_close(&dev->prio, &fh->prio);
	file->private_data = NULL;
	kfree(fh);

	return 0;
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct cx25821_fh *fh = priv;
	struct cx25821_dev *dev = fh->dev;

	if (unlikely(fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)) {
		return -EINVAL;
	}

	if (unlikely(i != fh->type)) {
		return -EINVAL;
	}

	if (unlikely(!res_get(dev, fh, get_resource(fh, RESOURCE_VIDEO0)))) {
		return -EBUSY;
	}

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

	res = get_resource(fh, RESOURCE_VIDEO0);
	err = videobuf_streamoff(get_queue(fh));
	if (err < 0)
		return err;
	res_free(dev, fh, res);
	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cx25821_fh *fh = priv;
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
	int err;
	int pix_format = PIXEL_FRMT_422;

	if (fh) {
		err = v4l2_prio_check(&dev->prio, &fh->prio);
		if (0 != err)
			return err;
	}

	dprintk(2, "%s()\n", __func__);
	err = vidioc_try_fmt_vid_cap(file, priv, f);

	if (0 != err)
		return err;

	fh->fmt = format_by_fourcc(f->fmt.pix.pixelformat);
	fh->vidq.field = f->fmt.pix.field;

	// check if width and height is valid based on set standard
	if (is_valid_width(f->fmt.pix.width, dev->tvnorm)) {
		fh->width = f->fmt.pix.width;
	}

	if (is_valid_height(f->fmt.pix.height, dev->tvnorm)) {
		fh->height = f->fmt.pix.height;
	}

	if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_Y41P)
		pix_format = PIXEL_FRMT_411;
	else if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
		pix_format = PIXEL_FRMT_422;
	else
		return -EINVAL;

	cx25821_set_pixel_format(dev, SRAM_CH00, pix_format);

	// check if cif resolution
	if (fh->width == 320 || fh->width == 352) {
		dev->use_cif_resolution[SRAM_CH00] = 1;
	} else {
		dev->use_cif_resolution[SRAM_CH00] = 0;
	}
	dev->cif_width[SRAM_CH00] = fh->width;
	medusa_set_resolution(dev, fh->width, SRAM_CH00);

	dprintk(2, "%s() width=%d height=%d field=%d\n", __func__, fh->width,
		fh->height, fh->vidq.field);
	cx25821_call_all(dev, video, s_fmt, f);

	return 0;
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	int ret_val = 0;
	struct cx25821_fh *fh = priv;
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;

	ret_val = videobuf_dqbuf(get_queue(fh), p, file->f_flags & O_NONBLOCK);

	p->sequence = dev->vidq[SRAM_CH00].count;

	return ret_val;
}

static int vidioc_log_status(struct file *file, void *priv)
{
	struct cx25821_dev *dev = ((struct cx25821_fh *)priv)->dev;
	char name[32 + 2];

	struct sram_channel *sram_ch = &dev->sram_channels[SRAM_CH00];
	u32 tmp = 0;

	snprintf(name, sizeof(name), "%s/2", dev->name);
	printk(KERN_INFO "%s/2: ============  START LOG STATUS  ============\n",
	       dev->name);
	cx25821_call_all(dev, core, log_status);
	tmp = cx_read(sram_ch->dma_ctl);
	printk(KERN_INFO "Video input 0 is %s\n",
	       (tmp & 0x11) ? "streaming" : "stopped");
	printk(KERN_INFO "%s/2: =============  END LOG STATUS  =============\n",
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
		err = v4l2_prio_check(&dev->prio, &fh->prio);
		if (0 != err)
			return err;
	}

	return cx25821_set_control(dev, ctl, SRAM_CH00);
}

// exported stuff
static const struct v4l2_file_operations video_fops = {
	.owner = THIS_MODULE,
	.open = video_open,
	.release = video_release,
	.read = video_read,
	.poll = video_poll,
	.mmap = video_mmap,
	.ioctl = video_ioctl2,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs = vidioc_reqbufs,
	.vidioc_querybuf = vidioc_querybuf,
	.vidioc_qbuf = vidioc_qbuf,
	.vidioc_dqbuf = vidioc_dqbuf,
#ifdef TUNER_FLAG
	.vidioc_s_std = vidioc_s_std,
	.vidioc_querystd = vidioc_querystd,
#endif
	.vidioc_cropcap = vidioc_cropcap,
	.vidioc_s_crop = vidioc_s_crop,
	.vidioc_g_crop = vidioc_g_crop,
	.vidioc_enum_input = vidioc_enum_input,
	.vidioc_g_input = vidioc_g_input,
	.vidioc_s_input = vidioc_s_input,
	.vidioc_g_ctrl = vidioc_g_ctrl,
	.vidioc_s_ctrl = vidioc_s_ctrl,
	.vidioc_queryctrl = vidioc_queryctrl,
	.vidioc_streamon = vidioc_streamon,
	.vidioc_streamoff = vidioc_streamoff,
	.vidioc_log_status = vidioc_log_status,
	.vidioc_g_priority = vidioc_g_priority,
	.vidioc_s_priority = vidioc_s_priority,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf = vidiocgmbuf,
#endif
#ifdef TUNER_FLAG
	.vidioc_g_tuner = vidioc_g_tuner,
	.vidioc_s_tuner = vidioc_s_tuner,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
#endif
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register = vidioc_g_register,
	.vidioc_s_register = vidioc_s_register,
#endif
};

struct video_device cx25821_video_template0 = {
	.name = "cx25821-video",
	.fops = &video_fops,
	.minor = -1,
	.ioctl_ops = &video_ioctl_ops,
	.tvnorms = CX25821_NORMS,
	.current_norm = V4L2_STD_NTSC_M,
};

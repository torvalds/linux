/*
 * Virtual Video driver - This code emulates a real video device with v4l2 api
 *
 * Copyright (c) 2006 by:
 *      Mauro Carvalho Chehab <mchehab--a.t--infradead.org>
 *      Ted Walther <ted--a.t--enumera.com>
 *      John Sokol <sokol--a.t--videotechnology.com>
 *      http://v4l.videotechnology.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD Licence, GNU General Public License
 * as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/dma-mapping.h>
#ifdef CONFIG_VIDEO_V4L1_COMPAT
/* Include V4L1 specific functions. Should be removed soon */
#include <linux/videodev.h>
#endif
#include <linux/interrupt.h>
#include <media/videobuf-vmalloc.h>
#include <media/v4l2-common.h>
#include <linux/kthread.h>
#include <linux/highmem.h>
#include <linux/freezer.h>

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#include "font.h"

#define VIVI_MAJOR_VERSION 0
#define VIVI_MINOR_VERSION 4
#define VIVI_RELEASE 0
#define VIVI_VERSION \
	KERNEL_VERSION(VIVI_MAJOR_VERSION, VIVI_MINOR_VERSION, VIVI_RELEASE)

/* Declare static vars that will be used as parameters */
static unsigned int vid_limit = 16;	/* Video memory limit, in Mb */
static int video_nr = -1;		/* /dev/videoN, -1 for autodetect */
static int n_devs = 1;			/* Number of virtual devices */

/* supported controls */
static struct v4l2_queryctrl vivi_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_VOLUME,
		.name          = "Volume",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 65535,
		.flags         = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}, {
		.id            = V4L2_CID_BRIGHTNESS,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Brightness",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 127,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_CONTRAST,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Contrast",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 0x1,
		.default_value = 0x10,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_SATURATION,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Saturation",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 0x1,
		.default_value = 127,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_HUE,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Hue",
		.minimum       = -128,
		.maximum       = 127,
		.step          = 0x1,
		.default_value = 0,
		.flags         = 0,
	}
};

static int qctl_regs[ARRAY_SIZE(vivi_qctrl)];

#define dprintk(dev, level, fmt, arg...)				\
	do {								\
		if (dev->vfd->debug >= (level))				\
			printk(KERN_DEBUG "vivi: " fmt , ## arg);	\
	} while (0)

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct vivi_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct vivi_fmt format = {
	.name     = "4:2:2, packed, YUYV",
	.fourcc   = V4L2_PIX_FMT_YUYV,
	.depth    = 16,
};

struct sg_to_addr {
	int pos;
	struct scatterlist *sg;
};

/* buffer for one video frame */
struct vivi_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct vivi_fmt        *fmt;
};

struct vivi_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(vivi_devlist);

struct vivi_dev {
	struct list_head           vivi_devlist;

	spinlock_t                 slock;
	struct mutex		   mutex;

	int                        users;

	/* various device info */
	struct video_device        *vfd;

	struct vivi_dmaqueue       vidq;

	/* Several counters */
	int                        h, m, s, ms;
	unsigned long              jiffies;
	char                       timestr[13];

	int			   mv_count;	/* Controls bars movement */
};

struct vivi_fh {
	struct vivi_dev            *dev;

	/* video capture */
	struct vivi_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
};

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

/* Bars and Colors should match positions */

enum colors {
	WHITE,
	AMBAR,
	CYAN,
	GREEN,
	MAGENTA,
	RED,
	BLUE,
	BLACK,
};

static u8 bars[8][3] = {
	/* R   G   B */
	{204, 204, 204},  /* white */
	{208, 208,   0},  /* ambar */
	{  0, 206, 206},  /* cyan */
	{  0, 239,   0},  /* green */
	{239,   0, 239},  /* magenta */
	{205,   0,   0},  /* red */
	{  0,   0, 255},  /* blue */
	{  0,   0,   0},  /* black */
};

#define TO_Y(r, g, b) \
	(((16829 * r + 33039 * g + 6416 * b  + 32768) >> 16) + 16)
/* RGB to  V(Cr) Color transform */
#define TO_V(r, g, b) \
	(((28784 * r - 24103 * g - 4681 * b  + 32768) >> 16) + 128)
/* RGB to  U(Cb) Color transform */
#define TO_U(r, g, b) \
	(((-9714 * r - 19070 * g + 28784 * b + 32768) >> 16) + 128)

#define TSTAMP_MIN_Y 24
#define TSTAMP_MAX_Y TSTAMP_MIN_Y+15
#define TSTAMP_MIN_X 64

static void gen_line(char *basep, int inipos, int wmax,
		int hmax, int line, int count, char *timestr)
{
	int  w, i, j, y;
	int pos = inipos;
	char *p, *s;
	u8   chr, r, g, b, color;

	/* We will just duplicate the second pixel at the packet */
	wmax /= 2;

	/* Generate a standard color bar pattern */
	for (w = 0; w < wmax; w++) {
		int colorpos = ((w + count) * 8/(wmax + 1)) % 8;
		r = bars[colorpos][0];
		g = bars[colorpos][1];
		b = bars[colorpos][2];

		for (color = 0; color < 4; color++) {
			p = basep + pos;

			switch (color) {
			case 0:
			case 2:
				*p = TO_Y(r, g, b);	/* Luma */
				break;
			case 1:
				*p = TO_U(r, g, b);	/* Cb */
				break;
			case 3:
				*p = TO_V(r, g, b);	/* Cr */
				break;
			}
			pos++;
		}
	}

	/* Checks if it is possible to show timestamp */
	if (TSTAMP_MAX_Y >= hmax)
		goto end;
	if (TSTAMP_MIN_X + strlen(timestr) >= wmax)
		goto end;

	/* Print stream time */
	if (line >= TSTAMP_MIN_Y && line <= TSTAMP_MAX_Y) {
		j = TSTAMP_MIN_X;
		for (s = timestr; *s; s++) {
			chr = rom8x16_bits[(*s-0x30)*16+line-TSTAMP_MIN_Y];
			for (i = 0; i < 7; i++) {
				if (chr & 1 << (7 - i)) {
					/* Font color*/
					r = 0;
					g = 198;
					b = 0;
				} else {
					/* Background color */
					r = bars[BLACK][0];
					g = bars[BLACK][1];
					b = bars[BLACK][2];
				}

				pos = inipos + j * 2;
				for (color = 0; color < 4; color++) {
					p = basep + pos;

					y = TO_Y(r, g, b);

					switch (color) {
					case 0:
					case 2:
						*p = TO_Y(r, g, b); /* Luma */
						break;
					case 1:
						*p = TO_U(r, g, b); /* Cb */
						break;
					case 3:
						*p = TO_V(r, g, b); /* Cr */
						break;
					}
					pos++;
				}
				j++;
			}
		}
	}

end:
	return;
}

static void vivi_fillbuff(struct vivi_dev *dev, struct vivi_buffer *buf)
{
	int h , pos = 0;
	int hmax  = buf->vb.height;
	int wmax  = buf->vb.width;
	struct timeval ts;
	char *tmpbuf = kmalloc(wmax * 2, GFP_ATOMIC);
	void *vbuf = videobuf_to_vmalloc(&buf->vb);

	if (!tmpbuf)
		return;

	if (!vbuf)
		return;

	for (h = 0; h < hmax; h++) {
		gen_line(tmpbuf, 0, wmax, hmax, h, dev->mv_count,
			 dev->timestr);
		memcpy(vbuf + pos, tmpbuf, wmax * 2);
		pos += wmax*2;
	}

	dev->mv_count++;

	kfree(tmpbuf);

	/* Updates stream time */

	dev->ms += jiffies_to_msecs(jiffies-dev->jiffies);
	dev->jiffies = jiffies;
	if (dev->ms >= 1000) {
		dev->ms -= 1000;
		dev->s++;
		if (dev->s >= 60) {
			dev->s -= 60;
			dev->m++;
			if (dev->m > 60) {
				dev->m -= 60;
				dev->h++;
				if (dev->h > 24)
					dev->h -= 24;
			}
		}
	}
	sprintf(dev->timestr, "%02d:%02d:%02d:%03d",
			dev->h, dev->m, dev->s, dev->ms);

	dprintk(dev, 2, "vivifill at %s: Buffer 0x%08lx size= %d\n",
			dev->timestr, (unsigned long)tmpbuf, pos);

	/* Advice that buffer was filled */
	buf->vb.field_count++;
	do_gettimeofday(&ts);
	buf->vb.ts = ts;
	buf->vb.state = VIDEOBUF_DONE;
}

static void vivi_thread_tick(struct vivi_fh *fh)
{
	struct vivi_buffer *buf;
	struct vivi_dev *dev = fh->dev;
	struct vivi_dmaqueue *dma_q = &dev->vidq;

	unsigned long flags = 0;

	dprintk(dev, 1, "Thread tick\n");

	spin_lock_irqsave(&dev->slock, flags);
	if (list_empty(&dma_q->active)) {
		dprintk(dev, 1, "No active queue to serve\n");
		goto unlock;
	}

	buf = list_entry(dma_q->active.next,
			 struct vivi_buffer, vb.queue);

	/* Nobody is waiting on this buffer, return */
	if (!waitqueue_active(&buf->vb.done))
		goto unlock;

	list_del(&buf->vb.queue);

	do_gettimeofday(&buf->vb.ts);

	/* Fill buffer */
	vivi_fillbuff(dev, buf);
	dprintk(dev, 1, "filled buffer %p\n", buf);

	wake_up(&buf->vb.done);
	dprintk(dev, 2, "[%p/%d] wakeup\n", buf, buf->vb. i);
unlock:
	spin_unlock_irqrestore(&dev->slock, flags);
	return;
}

#define frames_to_ms(frames)					\
	((frames * WAKE_NUMERATOR * 1000) / WAKE_DENOMINATOR)

static void vivi_sleep(struct vivi_fh *fh)
{
	struct vivi_dev *dev = fh->dev;
	struct vivi_dmaqueue *dma_q = &dev->vidq;
	int timeout;
	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	timeout = msecs_to_jiffies(frames_to_ms(1));

	vivi_thread_tick(fh);

	schedule_timeout_interruptible(timeout);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int vivi_thread(void *data)
{
	struct vivi_fh  *fh = data;
	struct vivi_dev *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		vivi_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int vivi_start_thread(struct vivi_fh *fh)
{
	struct vivi_dev *dev = fh->dev;
	struct vivi_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(vivi_thread, fh, "vivi");

	if (IS_ERR(dma_q->kthread)) {
		printk(KERN_ERR "vivi: kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void vivi_stop_thread(struct vivi_dmaqueue  *dma_q)
{
	struct vivi_dev *dev = container_of(dma_q, struct vivi_dev, vidq);

	dprintk(dev, 1, "%s\n", __func__);
	/* shutdown control thread */
	if (dma_q->kthread) {
		kthread_stop(dma_q->kthread);
		dma_q->kthread = NULL;
	}
}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
static int
buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct vivi_fh  *fh = vq->priv_data;
	struct vivi_dev *dev  = fh->dev;

	*size = fh->width*fh->height*2;

	if (0 == *count)
		*count = 32;

	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;

	dprintk(dev, 1, "%s, count=%d, size=%d\n", __func__,
		*count, *size);

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct vivi_buffer *buf)
{
	struct vivi_fh  *fh = vq->priv_data;
	struct vivi_dev *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);

	if (in_interrupt())
		BUG();

	videobuf_vmalloc_free(&buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_maxw() 1024
#define norm_maxh() 768
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct vivi_fh     *fh  = vq->priv_data;
	struct vivi_dev    *dev = fh->dev;
	struct vivi_buffer *buf = container_of(vb, struct vivi_buffer, vb);
	int rc;

	dprintk(dev, 1, "%s, field=%d\n", __func__, field);

	BUG_ON(NULL == fh->fmt);

	if (fh->width  < 48 || fh->width  > norm_maxw() ||
	    fh->height < 32 || fh->height > norm_maxh())
		return -EINVAL;

	buf->vb.size = fh->width*fh->height*2;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	/* These properties only change when queue is idle, see s_fmt */
	buf->fmt       = fh->fmt;
	buf->vb.width  = fh->width;
	buf->vb.height = fh->height;
	buf->vb.field  = field;

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		rc = videobuf_iolock(vq, &buf->vb, NULL);
		if (rc < 0)
			goto fail;
	}

	buf->vb.state = VIDEOBUF_PREPARED;

	return 0;

fail:
	free_buffer(vq, buf);
	return rc;
}

static void
buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct vivi_buffer    *buf  = container_of(vb, struct vivi_buffer, vb);
	struct vivi_fh        *fh   = vq->priv_data;
	struct vivi_dev       *dev  = fh->dev;
	struct vivi_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct vivi_buffer   *buf  = container_of(vb, struct vivi_buffer, vb);
	struct vivi_fh       *fh   = vq->priv_data;
	struct vivi_dev      *dev  = (struct vivi_dev *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops vivi_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

/* ------------------------------------------------------------------
	IOCTL vidioc handling
   ------------------------------------------------------------------*/
static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	strcpy(cap->driver, "vivi");
	strcpy(cap->card, "vivi");
	cap->version = VIVI_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (f->index > 0)
		return -EINVAL;

	strlcpy(f->description, format.name, sizeof(f->description));
	f->pixelformat = format.fourcc;
	return 0;
}

static int vidioc_g_fmt_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct vivi_fh *fh = priv;

	f->fmt.pix.width        = fh->width;
	f->fmt.pix.height       = fh->height;
	f->fmt.pix.field        = fh->vb_vidq.field;
	f->fmt.pix.pixelformat  = fh->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fh->fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return (0);
}

static int vidioc_try_fmt_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct vivi_fh  *fh  = priv;
	struct vivi_dev *dev = fh->dev;
	struct vivi_fmt *fmt;
	enum v4l2_field field;
	unsigned int maxw, maxh;

	if (format.fourcc != f->fmt.pix.pixelformat) {
		dprintk(dev, 1, "Fourcc format (0x%08x) invalid. "
			"Driver accepts only 0x%08x\n",
			f->fmt.pix.pixelformat, format.fourcc);
		return -EINVAL;
	}
	fmt = &format;

	field = f->fmt.pix.field;

	if (field == V4L2_FIELD_ANY) {
		field = V4L2_FIELD_INTERLACED;
	} else if (V4L2_FIELD_INTERLACED != field) {
		dprintk(dev, 1, "Field type invalid.\n");
		return -EINVAL;
	}

	maxw  = norm_maxw();
	maxh  = norm_maxh();

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

/*FIXME: This seems to be generic enough to be at videodev2 */
static int vidioc_s_fmt_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct vivi_fh  *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;

	int ret = vidioc_try_fmt_cap(file, fh, f);
	if (ret < 0)
		return (ret);

	mutex_lock(&q->vb_lock);

	if (videobuf_queue_is_busy(&fh->vb_vidq)) {
		dprintk(fh->dev, 1, "%s queue busy\n", __func__);
		ret = -EBUSY;
		goto out;
	}

	fh->fmt           = &format;
	fh->width         = f->fmt.pix.width;
	fh->height        = f->fmt.pix.height;
	fh->vb_vidq.field = f->fmt.pix.field;
	fh->type          = f->type;

	ret = 0;
out:
	mutex_unlock(&q->vb_lock);

	return (ret);
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct vivi_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vivi_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vivi_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vivi_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct vivi_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct vivi_fh  *fh = priv;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	return videobuf_streamon(&fh->vb_vidq);
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct vivi_fh  *fh = priv;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	return videobuf_streamoff(&fh->vb_vidq);
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id *i)
{
	return 0;
}

/* only one input in this sample driver */
static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_525_60;
	strcpy(inp->name, "Camera");

	return (0);
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;

	return (0);
}
static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i > 0)
		return -EINVAL;

	return (0);
}

	/* --- controls ---------------------------------------------- */
static int vidioc_queryctrl(struct file *file, void *priv,
			    struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vivi_qctrl); i++)
		if (qc->id && qc->id == vivi_qctrl[i].id) {
			memcpy(qc, &(vivi_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vivi_qctrl); i++)
		if (ctrl->id == vivi_qctrl[i].id) {
			ctrl->value = qctl_regs[i];
			return (0);
		}

	return -EINVAL;
}
static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vivi_qctrl); i++)
		if (ctrl->id == vivi_qctrl[i].id) {
			if (ctrl->value < vivi_qctrl[i].minimum
			    || ctrl->value > vivi_qctrl[i].maximum) {
					return (-ERANGE);
				}
			qctl_regs[i] = ctrl->value;
			return (0);
		}
	return -EINVAL;
}

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

#define line_buf_size(norm) (norm_maxw(norm)*(format.depth+7)/8)

static int vivi_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct vivi_dev *dev;
	struct vivi_fh *fh = NULL;
	int i;
	int retval = 0;

	printk(KERN_DEBUG "vivi: open called (minor=%d)\n", minor);

	list_for_each_entry(dev, &vivi_devlist, vivi_devlist)
		if (dev->vfd->minor == minor)
			goto found;
	return -ENODEV;

found:
	mutex_lock(&dev->mutex);
	dev->users++;

	if (dev->users > 1) {
		dev->users--;
		retval = -EBUSY;
		goto unlock;
	}

	dprintk(dev, 1, "open minor=%d type=%s users=%d\n", minor,
		v4l2_type_names[V4L2_BUF_TYPE_VIDEO_CAPTURE], dev->users);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh) {
		dev->users--;
		retval = -ENOMEM;
		goto unlock;
	}
unlock:
	mutex_unlock(&dev->mutex);
	if (retval)
		return retval;

	file->private_data = fh;
	fh->dev      = dev;

	fh->type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fh->fmt      = &format;
	fh->width    = 640;
	fh->height   = 480;

	/* Put all controls at a sane state */
	for (i = 0; i < ARRAY_SIZE(vivi_qctrl); i++)
		qctl_regs[i] = vivi_qctrl[i].default_value;

	/* Resets frame counters */
	dev->h = 0;
	dev->m = 0;
	dev->s = 0;
	dev->ms = 0;
	dev->mv_count = 0;
	dev->jiffies = jiffies;
	sprintf(dev->timestr, "%02d:%02d:%02d:%03d",
			dev->h, dev->m, dev->s, dev->ms);

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &vivi_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct vivi_buffer), fh);

	vivi_start_thread(fh);

	return 0;
}

static ssize_t
vivi_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct vivi_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
vivi_poll(struct file *file, struct poll_table_struct *wait)
{
	struct vivi_fh        *fh = file->private_data;
	struct vivi_dev       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int vivi_close(struct inode *inode, struct file *file)
{
	struct vivi_fh         *fh = file->private_data;
	struct vivi_dev *dev       = fh->dev;
	struct vivi_dmaqueue *vidq = &dev->vidq;

	int minor = iminor(inode);

	vivi_stop_thread(vidq);
	videobuf_stop(&fh->vb_vidq);
	videobuf_mmap_free(&fh->vb_vidq);

	kfree(fh);

	mutex_lock(&dev->mutex);
	dev->users--;
	mutex_unlock(&dev->mutex);

	dprintk(dev, 1, "close called (minor=%d, users=%d)\n",
		minor, dev->users);

	return 0;
}

static int vivi_release(void)
{
	struct vivi_dev *dev;
	struct list_head *list;

	while (!list_empty(&vivi_devlist)) {
		list = vivi_devlist.next;
		list_del(list);
		dev = list_entry(list, struct vivi_dev, vivi_devlist);

		if (-1 != dev->vfd->minor)
			video_unregister_device(dev->vfd);
		else
			video_device_release(dev->vfd);

		kfree(dev);
	}

	return 0;
}

static int vivi_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct vivi_fh  *fh = file->private_data;
	struct vivi_dev *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct file_operations vivi_fops = {
	.owner		= THIS_MODULE,
	.open           = vivi_open,
	.release        = vivi_close,
	.read           = vivi_read,
	.poll		= vivi_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.compat_ioctl   = v4l_compat_ioctl32,
	.mmap           = vivi_mmap,
	.llseek         = no_llseek,
};

static struct video_device vivi_template = {
	.name		= "vivi",
	.type		= VID_TYPE_CAPTURE,
	.fops           = &vivi_fops,
	.minor		= -1,
	.release	= video_device_release,

	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_cap  = vidioc_enum_fmt_cap,
	.vidioc_g_fmt_cap     = vidioc_g_fmt_cap,
	.vidioc_try_fmt_cap   = vidioc_try_fmt_cap,
	.vidioc_s_fmt_cap     = vidioc_s_fmt_cap,
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
	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};
/* -----------------------------------------------------------------
	Initialization and module stuff
   ------------------------------------------------------------------*/

static int __init vivi_init(void)
{
	int ret = -ENOMEM, i;
	struct vivi_dev *dev;
	struct video_device *vfd;

	for (i = 0; i < n_devs; i++) {
		dev = kzalloc(sizeof(*dev), GFP_KERNEL);
		if (NULL == dev)
			break;

		list_add_tail(&dev->vivi_devlist, &vivi_devlist);

		/* init video dma queues */
		INIT_LIST_HEAD(&dev->vidq.active);
		init_waitqueue_head(&dev->vidq.wq);

		/* initialize locks */
		spin_lock_init(&dev->slock);
		mutex_init(&dev->mutex);

		vfd = video_device_alloc();
		if (NULL == vfd)
			break;

		*vfd = vivi_template;

		ret = video_register_device(vfd, VFL_TYPE_GRABBER, video_nr);
		if (ret < 0)
			break;

		snprintf(vfd->name, sizeof(vfd->name), "%s (%i)",
			 vivi_template.name, vfd->minor);

		if (video_nr >= 0)
			video_nr++;

		dev->vfd = vfd;
	}

	if (ret < 0) {
		vivi_release();
		printk(KERN_INFO "Error %d while loading vivi driver\n", ret);
	} else
		printk(KERN_INFO "Video Technology Magazine Virtual Video "
				 "Capture Board successfully loaded.\n");
	return ret;
}

static void __exit vivi_exit(void)
{
	vivi_release();
}

module_init(vivi_init);
module_exit(vivi_exit);

MODULE_DESCRIPTION("Video Technology Magazine Virtual Video Capture Board");
MODULE_AUTHOR("Mauro Carvalho Chehab, Ted Walther and John Sokol");
MODULE_LICENSE("Dual BSD/GPL");

module_param(video_nr, int, 0);
MODULE_PARM_DESC(video_nr, "video iminor start number");

module_param(n_devs, int, 0);
MODULE_PARM_DESC(n_devs, "number of video devices to create");

module_param_named(debug, vivi_template.debug, int, 0444);
MODULE_PARM_DESC(debug, "activates debug info");

module_param(vid_limit, int, 0644);
MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

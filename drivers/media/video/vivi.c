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
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/font.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/kthread.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 20)
#include <linux/freezer.h>
#endif
#include <media/videobuf-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>

#define VIVI_MODULE_NAME "vivi"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define MAX_WIDTH 1920
#define MAX_HEIGHT 1200

#define VIVI_MAJOR_VERSION 0
#define VIVI_MINOR_VERSION 7
#define VIVI_RELEASE 0
#define VIVI_VERSION \
	KERNEL_VERSION(VIVI_MAJOR_VERSION, VIVI_MINOR_VERSION, VIVI_RELEASE)

MODULE_DESCRIPTION("Video Technology Magazine Virtual Video Capture Board");
MODULE_AUTHOR("Mauro Carvalho Chehab, Ted Walther and John Sokol");
MODULE_LICENSE("Dual BSD/GPL");

static unsigned video_nr = -1;
module_param(video_nr, uint, 0644);
MODULE_PARM_DESC(video_nr, "videoX start number, -1 is autodetect");

static unsigned n_devs = 1;
module_param(n_devs, uint, 0644);
MODULE_PARM_DESC(n_devs, "number of video devices to create");

static unsigned debug;
module_param(debug, uint, 0644);
MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
module_param(vid_limit, uint, 0644);
MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

/* Global font descriptor */
static const u8 *font8x16;

#define dprintk(dev, level, fmt, arg...) \
	v4l2_dbg(level, debug, &dev->v4l2_dev, fmt, ## arg)

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct vivi_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct vivi_fmt formats[] = {
	{
		.name     = "4:2:2, packed, YUYV",
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.depth    = 16,
	},
	{
		.name     = "4:2:2, packed, UYVY",
		.fourcc   = V4L2_PIX_FMT_UYVY,
		.depth    = 16,
	},
	{
		.name     = "RGB565 (LE)",
		.fourcc   = V4L2_PIX_FMT_RGB565, /* gggbbbbb rrrrrggg */
		.depth    = 16,
	},
	{
		.name     = "RGB565 (BE)",
		.fourcc   = V4L2_PIX_FMT_RGB565X, /* rrrrrggg gggbbbbb */
		.depth    = 16,
	},
	{
		.name     = "RGB555 (LE)",
		.fourcc   = V4L2_PIX_FMT_RGB555, /* gggbbbbb arrrrrgg */
		.depth    = 16,
	},
	{
		.name     = "RGB555 (BE)",
		.fourcc   = V4L2_PIX_FMT_RGB555X, /* arrrrrgg gggbbbbb */
		.depth    = 16,
	},
};

static struct vivi_fmt *get_format(struct v4l2_format *f)
{
	struct vivi_fmt *fmt;
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(formats); k++) {
		fmt = &formats[k];
		if (fmt->fourcc == f->fmt.pix.pixelformat)
			break;
	}

	if (k == ARRAY_SIZE(formats))
		return NULL;

	return &formats[k];
}

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
	struct v4l2_device 	   v4l2_dev;

	/* controls */
	int 			   brightness;
	int 			   contrast;
	int 			   saturation;
	int 			   hue;
	int 			   volume;

	spinlock_t                 slock;
	struct mutex		   mutex;

	/* various device info */
	struct video_device        *vfd;

	struct vivi_dmaqueue       vidq;

	/* Several counters */
	unsigned 		   ms;
	unsigned long              jiffies;

	int			   mv_count;	/* Controls bars movement */

	/* Input Number */
	int			   input;

	/* video capture */
	struct vivi_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	unsigned long 		   generating;
	u8 			   bars[9][3];
	u8 			   line[MAX_WIDTH * 4];
};

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

/* Bars and Colors should match positions */

enum colors {
	WHITE,
	AMBER,
	CYAN,
	GREEN,
	MAGENTA,
	RED,
	BLUE,
	BLACK,
	TEXT_BLACK,
};

/* R   G   B */
#define COLOR_WHITE	{204, 204, 204}
#define COLOR_AMBER	{208, 208,   0}
#define COLOR_CYAN	{  0, 206, 206}
#define	COLOR_GREEN	{  0, 239,   0}
#define COLOR_MAGENTA	{239,   0, 239}
#define COLOR_RED	{205,   0,   0}
#define COLOR_BLUE	{  0,   0, 255}
#define COLOR_BLACK	{  0,   0,   0}

struct bar_std {
	u8 bar[9][3];
};

/* Maximum number of bars are 10 - otherwise, the input print code
   should be modified */
static struct bar_std bars[] = {
	{	/* Standard ITU-R color bar sequence */
		{ COLOR_WHITE, COLOR_AMBER, COLOR_CYAN, COLOR_GREEN,
		  COLOR_MAGENTA, COLOR_RED, COLOR_BLUE, COLOR_BLACK, COLOR_BLACK }
	}, {
		{ COLOR_WHITE, COLOR_AMBER, COLOR_BLACK, COLOR_WHITE,
		  COLOR_AMBER, COLOR_BLACK, COLOR_WHITE, COLOR_AMBER, COLOR_BLACK }
	}, {
		{ COLOR_WHITE, COLOR_CYAN, COLOR_BLACK, COLOR_WHITE,
		  COLOR_CYAN, COLOR_BLACK, COLOR_WHITE, COLOR_CYAN, COLOR_BLACK }
	}, {
		{ COLOR_WHITE, COLOR_GREEN, COLOR_BLACK, COLOR_WHITE,
		  COLOR_GREEN, COLOR_BLACK, COLOR_WHITE, COLOR_GREEN, COLOR_BLACK }
	},
};

#define NUM_INPUTS ARRAY_SIZE(bars)

#define TO_Y(r, g, b) \
	(((16829 * r + 33039 * g + 6416 * b  + 32768) >> 16) + 16)
/* RGB to  V(Cr) Color transform */
#define TO_V(r, g, b) \
	(((28784 * r - 24103 * g - 4681 * b  + 32768) >> 16) + 128)
/* RGB to  U(Cb) Color transform */
#define TO_U(r, g, b) \
	(((-9714 * r - 19070 * g + 28784 * b + 32768) >> 16) + 128)

/* precalculate color bar values to speed up rendering */
static void precalculate_bars(struct vivi_dev *dev)
{
	u8 r, g, b;
	int k, is_yuv;

	for (k = 0; k < 9; k++) {
		r = bars[dev->input].bar[k][0];
		g = bars[dev->input].bar[k][1];
		b = bars[dev->input].bar[k][2];
		is_yuv = 0;

		switch (dev->fmt->fourcc) {
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_UYVY:
			is_yuv = 1;
			break;
		case V4L2_PIX_FMT_RGB565:
		case V4L2_PIX_FMT_RGB565X:
			r >>= 3;
			g >>= 2;
			b >>= 3;
			break;
		case V4L2_PIX_FMT_RGB555:
		case V4L2_PIX_FMT_RGB555X:
			r >>= 3;
			g >>= 3;
			b >>= 3;
			break;
		}

		if (is_yuv) {
			dev->bars[k][0] = TO_Y(r, g, b);	/* Luma */
			dev->bars[k][1] = TO_U(r, g, b);	/* Cb */
			dev->bars[k][2] = TO_V(r, g, b);	/* Cr */
		} else {
			dev->bars[k][0] = r;
			dev->bars[k][1] = g;
			dev->bars[k][2] = b;
		}
	}
}

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void gen_twopix(struct vivi_dev *dev, u8 *buf, int colorpos)
{
	u8 r_y, g_u, b_v;
	int color;
	u8 *p;

	r_y = dev->bars[colorpos][0]; /* R or precalculated Y */
	g_u = dev->bars[colorpos][1]; /* G or precalculated U */
	b_v = dev->bars[colorpos][2]; /* B or precalculated V */

	for (color = 0; color < 4; color++) {
		p = buf + color;

		switch (dev->fmt->fourcc) {
		case V4L2_PIX_FMT_YUYV:
			switch (color) {
			case 0:
			case 2:
				*p = r_y;
				break;
			case 1:
				*p = g_u;
				break;
			case 3:
				*p = b_v;
				break;
			}
			break;
		case V4L2_PIX_FMT_UYVY:
			switch (color) {
			case 1:
			case 3:
				*p = r_y;
				break;
			case 0:
				*p = g_u;
				break;
			case 2:
				*p = b_v;
				break;
			}
			break;
		case V4L2_PIX_FMT_RGB565:
			switch (color) {
			case 0:
			case 2:
				*p = (g_u << 5) | b_v;
				break;
			case 1:
			case 3:
				*p = (r_y << 3) | (g_u >> 3);
				break;
			}
			break;
		case V4L2_PIX_FMT_RGB565X:
			switch (color) {
			case 0:
			case 2:
				*p = (r_y << 3) | (g_u >> 3);
				break;
			case 1:
			case 3:
				*p = (g_u << 5) | b_v;
				break;
			}
			break;
		case V4L2_PIX_FMT_RGB555:
			switch (color) {
			case 0:
			case 2:
				*p = (g_u << 5) | b_v;
				break;
			case 1:
			case 3:
				*p = (r_y << 2) | (g_u >> 3);
				break;
			}
			break;
		case V4L2_PIX_FMT_RGB555X:
			switch (color) {
			case 0:
			case 2:
				*p = (r_y << 2) | (g_u >> 3);
				break;
			case 1:
			case 3:
				*p = (g_u << 5) | b_v;
				break;
			}
			break;
		}
	}
}

static void precalculate_line(struct vivi_dev *dev)
{
	int w;

	for (w = 0; w < dev->width * 2; w += 2) {
		int colorpos = (w / (dev->width / 8) % 8);

		gen_twopix(dev, dev->line + w * 2, colorpos);
	}
}

static void gen_text(struct vivi_dev *dev, char *basep,
					int y, int x, char *text)
{
	int line;

	/* Checks if it is possible to show string */
	if (y + 16 >= dev->height || x + strlen(text) * 8 >= dev->width)
		return;

	/* Print stream time */
	for (line = y; line < y + 16; line++) {
		int j = 0;
		char *pos = basep + line * dev->width * 2 + x * 2;
		char *s;

		for (s = text; *s; s++) {
			u8 chr = font8x16[*s * 16 + line - y];
			int i;

			for (i = 0; i < 7; i++, j++) {
				/* Draw white font on black background */
				if (chr & (1 << (7 - i)))
					gen_twopix(dev, pos + j * 2, WHITE);
				else
					gen_twopix(dev, pos + j * 2, TEXT_BLACK);
			}
		}
	}
}

static void vivi_fillbuff(struct vivi_dev *dev, struct vivi_buffer *buf)
{
	int hmax = buf->vb.height;
	int wmax = buf->vb.width;
	struct timeval ts;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	unsigned ms;
	char str[100];
	int h, line = 1;

	if (!vbuf)
		return;

	for (h = 0; h < hmax; h++)
		memcpy(vbuf + h * wmax * 2, dev->line + (dev->mv_count % wmax) * 2, wmax * 2);

	/* Updates stream time */

	dev->ms += jiffies_to_msecs(jiffies - dev->jiffies);
	dev->jiffies = jiffies;
	ms = dev->ms;
	snprintf(str, sizeof(str), " %02d:%02d:%02d:%03d ",
			(ms / (60 * 60 * 1000)) % 24,
			(ms / (60 * 1000)) % 60,
			(ms / 1000) % 60,
			ms % 1000);
	gen_text(dev, vbuf, line++ * 16, 16, str);
	snprintf(str, sizeof(str), " %dx%d, input %d ",
			dev->width, dev->height, dev->input);
	gen_text(dev, vbuf, line++ * 16, 16, str);

	snprintf(str, sizeof(str), " brightness %3d, contrast %3d, saturation %3d, hue %d ",
			dev->brightness,
			dev->contrast,
			dev->saturation,
			dev->hue);
	gen_text(dev, vbuf, line++ * 16, 16, str);
	snprintf(str, sizeof(str), " volume %3d ", dev->volume);
	gen_text(dev, vbuf, line++ * 16, 16, str);

	dev->mv_count += 2;

	/* Advice that buffer was filled */
	buf->vb.field_count++;
	do_gettimeofday(&ts);
	buf->vb.ts = ts;
	buf->vb.state = VIDEOBUF_DONE;
}

static void vivi_thread_tick(struct vivi_dev *dev)
{
	struct vivi_dmaqueue *dma_q = &dev->vidq;
	struct vivi_buffer *buf;
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
}

#define frames_to_ms(frames)					\
	((frames * WAKE_NUMERATOR * 1000) / WAKE_DENOMINATOR)

static void vivi_sleep(struct vivi_dev *dev)
{
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

	vivi_thread_tick(dev);

	schedule_timeout_interruptible(timeout);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int vivi_thread(void *data)
{
	struct vivi_dev *dev = data;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		vivi_sleep(dev);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static void vivi_start_generating(struct file *file)
{
	struct vivi_dev *dev = video_drvdata(file);
	struct vivi_dmaqueue *dma_q = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (test_and_set_bit(0, &dev->generating))
		return;
	file->private_data = dev;

	/* Resets frame counters */
	dev->ms = 0;
	dev->mv_count = 0;
	dev->jiffies = jiffies;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;
	dma_q->kthread = kthread_run(vivi_thread, dev, dev->v4l2_dev.name);

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		clear_bit(0, &dev->generating);
		return;
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
}

static void vivi_stop_generating(struct file *file)
{
	struct vivi_dev *dev = video_drvdata(file);
	struct vivi_dmaqueue *dma_q = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (!file->private_data)
		return;
	if (!test_and_clear_bit(0, &dev->generating))
		return;

	/* shutdown control thread */
	if (dma_q->kthread) {
		kthread_stop(dma_q->kthread);
		dma_q->kthread = NULL;
	}
	videobuf_stop(&dev->vb_vidq);
	videobuf_mmap_free(&dev->vb_vidq);
}

static int vivi_is_generating(struct vivi_dev *dev)
{
	return test_bit(0, &dev->generating);
}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
static int
buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct vivi_dev *dev = vq->priv_data;

	*size = dev->width * dev->height * 2;

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
	struct vivi_dev *dev = vq->priv_data;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);

	videobuf_vmalloc_free(&buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct vivi_dev *dev = vq->priv_data;
	struct vivi_buffer *buf = container_of(vb, struct vivi_buffer, vb);
	int rc;

	dprintk(dev, 1, "%s, field=%d\n", __func__, field);

	BUG_ON(NULL == dev->fmt);

	if (dev->width  < 48 || dev->width  > MAX_WIDTH ||
	    dev->height < 32 || dev->height > MAX_HEIGHT)
		return -EINVAL;

	buf->vb.size = dev->width * dev->height * 2;
	if (0 != buf->vb.baddr && buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	/* These properties only change when queue is idle, see s_fmt */
	buf->fmt       = dev->fmt;
	buf->vb.width  = dev->width;
	buf->vb.height = dev->height;
	buf->vb.field  = field;

	precalculate_bars(dev);
	precalculate_line(dev);

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
	struct vivi_dev *dev = vq->priv_data;
	struct vivi_buffer *buf = container_of(vb, struct vivi_buffer, vb);
	struct vivi_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct vivi_dev *dev = vq->priv_data;
	struct vivi_buffer *buf  = container_of(vb, struct vivi_buffer, vb);

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
	struct vivi_dev *dev = video_drvdata(file);

	strcpy(cap->driver, "vivi");
	strcpy(cap->card, "vivi");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = VIVI_VERSION;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | \
			    V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct vivi_fmt *fmt;

	if (f->index >= ARRAY_SIZE(formats))
		return -EINVAL;

	fmt = &formats[f->index];

	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->fourcc;
	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct vivi_dev *dev = video_drvdata(file);

	f->fmt.pix.width        = dev->width;
	f->fmt.pix.height       = dev->height;
	f->fmt.pix.field        = dev->vb_vidq.field;
	f->fmt.pix.pixelformat  = dev->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * dev->fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct vivi_dev *dev = video_drvdata(file);
	struct vivi_fmt *fmt;
	enum v4l2_field field;

	fmt = get_format(f);
	if (!fmt) {
		dprintk(dev, 1, "Fourcc format (0x%08x) invalid.\n",
			f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	field = f->fmt.pix.field;

	if (field == V4L2_FIELD_ANY) {
		field = V4L2_FIELD_INTERLACED;
	} else if (V4L2_FIELD_INTERLACED != field) {
		dprintk(dev, 1, "Field type invalid.\n");
		return -EINVAL;
	}

	f->fmt.pix.field = field;
	v4l_bound_align_image(&f->fmt.pix.width, 48, MAX_WIDTH, 2,
			      &f->fmt.pix.height, 32, MAX_HEIGHT, 0, 0);
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct vivi_dev *dev = video_drvdata(file);

	int ret = vidioc_try_fmt_vid_cap(file, priv, f);
	if (ret < 0)
		return ret;

	if (vivi_is_generating(dev)) {
		dprintk(dev, 1, "%s device busy\n", __func__);
		ret = -EBUSY;
		goto out;
	}

	dev->fmt = get_format(f);
	dev->width = f->fmt.pix.width;
	dev->height = f->fmt.pix.height;
	dev->vb_vidq.field = f->fmt.pix.field;
	ret = 0;
out:
	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct vivi_dev *dev = video_drvdata(file);

	return videobuf_reqbufs(&dev->vb_vidq, p);
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vivi_dev *dev = video_drvdata(file);

	return videobuf_querybuf(&dev->vb_vidq, p);
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vivi_dev *dev = video_drvdata(file);

	return videobuf_qbuf(&dev->vb_vidq, p);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vivi_dev *dev = video_drvdata(file);

	return videobuf_dqbuf(&dev->vb_vidq, p,
				file->f_flags & O_NONBLOCK);
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct vivi_dev *dev = video_drvdata(file);

	return videobuf_cgmbuf(&dev->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct vivi_dev *dev = video_drvdata(file);
	int ret;

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	ret = videobuf_streamon(&dev->vb_vidq);
	if (ret)
		return ret;

	vivi_start_generating(file);
	return 0;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct vivi_dev *dev = video_drvdata(file);
	int ret;

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	ret = videobuf_streamoff(&dev->vb_vidq);
	if (!ret)
		vivi_stop_generating(file);
	return ret;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id *i)
{
	return 0;
}

/* only one input in this sample driver */
static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	if (inp->index >= NUM_INPUTS)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_525_60;
	sprintf(inp->name, "Camera %u", inp->index);
	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct vivi_dev *dev = video_drvdata(file);

	*i = dev->input;
	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct vivi_dev *dev = video_drvdata(file);

	if (i >= NUM_INPUTS)
		return -EINVAL;

	dev->input = i;
	precalculate_bars(dev);
	precalculate_line(dev);
	return 0;
}

/* --- controls ---------------------------------------------- */
static int vidioc_queryctrl(struct file *file, void *priv,
			    struct v4l2_queryctrl *qc)
{
	switch (qc->id) {
	case V4L2_CID_AUDIO_VOLUME:
		return v4l2_ctrl_query_fill(qc, 0, 255, 1, 200);
	case V4L2_CID_BRIGHTNESS:
		return v4l2_ctrl_query_fill(qc, 0, 255, 1, 127);
	case V4L2_CID_CONTRAST:
		return v4l2_ctrl_query_fill(qc, 0, 255, 1, 16);
	case V4L2_CID_SATURATION:
		return v4l2_ctrl_query_fill(qc, 0, 255, 1, 127);
	case V4L2_CID_HUE:
		return v4l2_ctrl_query_fill(qc, -128, 127, 1, 0);
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct vivi_dev *dev = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = dev->volume;
		return 0;
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = dev->brightness;
		return 0;
	case V4L2_CID_CONTRAST:
		ctrl->value = dev->contrast;
		return 0;
	case V4L2_CID_SATURATION:
		ctrl->value = dev->saturation;
		return 0;
	case V4L2_CID_HUE:
		ctrl->value = dev->hue;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct vivi_dev *dev = video_drvdata(file);
	struct v4l2_queryctrl qc;
	int err;

	qc.id = ctrl->id;
	err = vidioc_queryctrl(file, priv, &qc);
	if (err < 0)
		return err;
	if (ctrl->value < qc.minimum || ctrl->value > qc.maximum)
		return -ERANGE;
	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:
		dev->volume = ctrl->value;
		return 0;
	case V4L2_CID_BRIGHTNESS:
		dev->brightness = ctrl->value;
		return 0;
	case V4L2_CID_CONTRAST:
		dev->contrast = ctrl->value;
		return 0;
	case V4L2_CID_SATURATION:
		dev->saturation = ctrl->value;
		return 0;
	case V4L2_CID_HUE:
		dev->hue = ctrl->value;
		return 0;
	}
	return -EINVAL;
}

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

static ssize_t
vivi_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct vivi_dev *dev = video_drvdata(file);

	vivi_start_generating(file);
	return videobuf_read_stream(&dev->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
}

static unsigned int
vivi_poll(struct file *file, struct poll_table_struct *wait)
{
	struct vivi_dev *dev = video_drvdata(file);
	struct videobuf_queue *q = &dev->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	vivi_start_generating(file);
	return videobuf_poll_stream(file, q, wait);
}

static int vivi_close(struct file *file)
{
	struct video_device  *vdev = video_devdata(file);
	struct vivi_dev *dev = video_drvdata(file);

	vivi_stop_generating(file);

	dprintk(dev, 1, "close called (dev=%s)\n",
		video_device_node_name(vdev));
	return 0;
}

static int vivi_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct vivi_dev *dev = video_drvdata(file);
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&dev->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end - (unsigned long)vma->vm_start,
		ret);
	return ret;
}

static const struct v4l2_file_operations vivi_fops = {
	.owner		= THIS_MODULE,
	.release        = vivi_close,
	.read           = vivi_read,
	.poll		= vivi_poll,
	.unlocked_ioctl = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = vivi_mmap,
};

static const struct v4l2_ioctl_ops vivi_ioctl_ops = {
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs       = vidioc_reqbufs,
	.vidioc_querybuf      = vidioc_querybuf,
	.vidioc_qbuf          = vidioc_qbuf,
	.vidioc_dqbuf         = vidioc_dqbuf,
	.vidioc_s_std         = vidioc_s_std,
	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
	.vidioc_streamon      = vidioc_streamon,
	.vidioc_streamoff     = vidioc_streamoff,
	.vidioc_queryctrl     = vidioc_queryctrl,
	.vidioc_g_ctrl        = vidioc_g_ctrl,
	.vidioc_s_ctrl        = vidioc_s_ctrl,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = vidiocgmbuf,
#endif
};

static struct video_device vivi_template = {
	.name		= "vivi",
	.fops           = &vivi_fops,
	.ioctl_ops 	= &vivi_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

/* -----------------------------------------------------------------
	Initialization and module stuff
   ------------------------------------------------------------------*/

static int vivi_release(void)
{
	struct vivi_dev *dev;
	struct list_head *list;

	while (!list_empty(&vivi_devlist)) {
		list = vivi_devlist.next;
		list_del(list);
		dev = list_entry(list, struct vivi_dev, vivi_devlist);

		v4l2_info(&dev->v4l2_dev, "unregistering %s\n",
			video_device_node_name(dev->vfd));
		video_unregister_device(dev->vfd);
		v4l2_device_unregister(&dev->v4l2_dev);
		kfree(dev);
	}

	return 0;
}

static int __init vivi_create_instance(int inst)
{
	struct vivi_dev *dev;
	struct video_device *vfd;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	snprintf(dev->v4l2_dev.name, sizeof(dev->v4l2_dev.name),
			"%s-%03d", VIVI_MODULE_NAME, inst);
	ret = v4l2_device_register(NULL, &dev->v4l2_dev);
	if (ret)
		goto free_dev;

	dev->fmt = &formats[0];
	dev->width = 640;
	dev->height = 480;
	dev->volume = 200;
	dev->brightness = 127;
	dev->contrast = 16;
	dev->saturation = 127;
	dev->hue = 0;

	/* initialize locks */
	spin_lock_init(&dev->slock);
	mutex_init(&dev->mutex);

	videobuf_queue_vmalloc_init(&dev->vb_vidq, &vivi_video_qops,
			NULL, &dev->slock, V4L2_BUF_TYPE_VIDEO_CAPTURE,
			V4L2_FIELD_INTERLACED,
			sizeof(struct vivi_buffer), dev, &dev->mutex);

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	init_waitqueue_head(&dev->vidq.wq);

	ret = -ENOMEM;
	vfd = video_device_alloc();
	if (!vfd)
		goto unreg_dev;

	*vfd = vivi_template;
	vfd->debug = debug;
	vfd->v4l2_dev = &dev->v4l2_dev;
	vfd->lock = &dev->mutex;

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, video_nr);
	if (ret < 0)
		goto rel_vdev;

	video_set_drvdata(vfd, dev);

	/* Now that everything is fine, let's add it to device list */
	list_add_tail(&dev->vivi_devlist, &vivi_devlist);

	if (video_nr != -1)
		video_nr++;

	dev->vfd = vfd;
	v4l2_info(&dev->v4l2_dev, "V4L2 device registered as %s\n",
		  video_device_node_name(vfd));
	return 0;

rel_vdev:
	video_device_release(vfd);
unreg_dev:
	v4l2_device_unregister(&dev->v4l2_dev);
free_dev:
	kfree(dev);
	return ret;
}

/* This routine allocates from 1 to n_devs virtual drivers.

   The real maximum number of virtual drivers will depend on how many drivers
   will succeed. This is limited to the maximum number of devices that
   videodev supports, which is equal to VIDEO_NUM_DEVICES.
 */
static int __init vivi_init(void)
{
	const struct font_desc *font = find_font("VGA8x16");
	int ret = 0, i;

	if (font == NULL) {
		printk(KERN_ERR "vivi: could not find font\n");
		return -ENODEV;
	}
	font8x16 = font->data;

	if (n_devs <= 0)
		n_devs = 1;

	for (i = 0; i < n_devs; i++) {
		ret = vivi_create_instance(i);
		if (ret) {
			/* If some instantiations succeeded, keep driver */
			if (i)
				ret = 0;
			break;
		}
	}

	if (ret < 0) {
		printk(KERN_ERR "vivi: error %d while loading driver\n", ret);
		return ret;
	}

	printk(KERN_INFO "Video Technology Magazine Virtual Video "
			"Capture Board ver %u.%u.%u successfully loaded.\n",
			(VIVI_VERSION >> 16) & 0xFF, (VIVI_VERSION >> 8) & 0xFF,
			VIVI_VERSION & 0xFF);

	/* n_devs will reflect the actual number of allocated devices */
	n_devs = i;

	return ret;
}

static void __exit vivi_exit(void)
{
	vivi_release();
}

module_init(vivi_init);
module_exit(vivi_exit);

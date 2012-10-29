/*
 * Virtual Video driver - This code emulates a real video device with v4l2 api
 *
 * Copyright (c) 2006 by:
 *      Mauro Carvalho Chehab <mchehab--a.t--infradead.org>
 *      Ted Walther <ted--a.t--enumera.com>
 *      John Sokol <sokol--a.t--videotechnology.com>
 *      http://v4l.videotechnology.com/
 *
 *      Conversion to videobuf2 by Pawel Osciak & Marek Szyprowski
 *      Copyright (c) 2010 Samsung Electronics
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
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/v4l2-common.h>

#define VIVI_MODULE_NAME "vivi"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define MAX_WIDTH 1920
#define MAX_HEIGHT 1200

#define VIVI_VERSION "0.8.1"

MODULE_DESCRIPTION("Video Technology Magazine Virtual Video Capture Board");
MODULE_AUTHOR("Mauro Carvalho Chehab, Ted Walther and John Sokol");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(VIVI_VERSION);

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
	u8    depth;
	bool  is_yuv;
};

static struct vivi_fmt formats[] = {
	{
		.name     = "4:2:2, packed, YUYV",
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.depth    = 16,
		.is_yuv   = true,
	},
	{
		.name     = "4:2:2, packed, UYVY",
		.fourcc   = V4L2_PIX_FMT_UYVY,
		.depth    = 16,
		.is_yuv   = true,
	},
	{
		.name     = "4:2:2, packed, YVYU",
		.fourcc   = V4L2_PIX_FMT_YVYU,
		.depth    = 16,
		.is_yuv   = true,
	},
	{
		.name     = "4:2:2, packed, VYUY",
		.fourcc   = V4L2_PIX_FMT_VYUY,
		.depth    = 16,
		.is_yuv   = true,
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
	{
		.name     = "RGB24 (LE)",
		.fourcc   = V4L2_PIX_FMT_RGB24, /* rgb */
		.depth    = 24,
	},
	{
		.name     = "RGB24 (BE)",
		.fourcc   = V4L2_PIX_FMT_BGR24, /* bgr */
		.depth    = 24,
	},
	{
		.name     = "RGB32 (LE)",
		.fourcc   = V4L2_PIX_FMT_RGB32, /* argb */
		.depth    = 32,
	},
	{
		.name     = "RGB32 (BE)",
		.fourcc   = V4L2_PIX_FMT_BGR32, /* bgra */
		.depth    = 32,
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

/* buffer for one video frame */
struct vivi_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_buffer	vb;
	struct list_head	list;
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
	struct v4l2_ctrl_handler   ctrl_handler;
	struct video_device	   vdev;

	/* controls */
	struct v4l2_ctrl	   *brightness;
	struct v4l2_ctrl	   *contrast;
	struct v4l2_ctrl	   *saturation;
	struct v4l2_ctrl	   *hue;
	struct {
		/* autogain/gain cluster */
		struct v4l2_ctrl	   *autogain;
		struct v4l2_ctrl	   *gain;
	};
	struct v4l2_ctrl	   *volume;
	struct v4l2_ctrl	   *alpha;
	struct v4l2_ctrl	   *button;
	struct v4l2_ctrl	   *boolean;
	struct v4l2_ctrl	   *int32;
	struct v4l2_ctrl	   *int64;
	struct v4l2_ctrl	   *menu;
	struct v4l2_ctrl	   *string;
	struct v4l2_ctrl	   *bitmask;
	struct v4l2_ctrl	   *int_menu;

	spinlock_t                 slock;
	struct mutex		   mutex;

	struct vivi_dmaqueue       vidq;

	/* Several counters */
	unsigned 		   ms;
	unsigned long              jiffies;
	unsigned		   button_pressed;

	int			   mv_count;	/* Controls bars movement */

	/* Input Number */
	int			   input;

	/* video capture */
	struct vivi_fmt            *fmt;
	unsigned int               width, height;
	struct vb2_queue	   vb_vidq;
	unsigned int		   field_count;

	u8			   bars[9][3];
	u8			   line[MAX_WIDTH * 8];
	unsigned int		   pixelsize;
	u8			   alpha_component;
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
		is_yuv = dev->fmt->is_yuv;

		switch (dev->fmt->fourcc) {
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
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_YVYU:
		case V4L2_PIX_FMT_VYUY:
		case V4L2_PIX_FMT_RGB24:
		case V4L2_PIX_FMT_BGR24:
		case V4L2_PIX_FMT_RGB32:
		case V4L2_PIX_FMT_BGR32:
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

/* 'odd' is true for pixels 1, 3, 5, etc. and false for pixels 0, 2, 4, etc. */
static void gen_twopix(struct vivi_dev *dev, u8 *buf, int colorpos, bool odd)
{
	u8 r_y, g_u, b_v;
	u8 alpha = dev->alpha_component;
	int color;
	u8 *p;

	r_y = dev->bars[colorpos][0]; /* R or precalculated Y */
	g_u = dev->bars[colorpos][1]; /* G or precalculated U */
	b_v = dev->bars[colorpos][2]; /* B or precalculated V */

	for (color = 0; color < dev->pixelsize; color++) {
		p = buf + color;

		switch (dev->fmt->fourcc) {
		case V4L2_PIX_FMT_YUYV:
			switch (color) {
			case 0:
				*p = r_y;
				break;
			case 1:
				*p = odd ? b_v : g_u;
				break;
			}
			break;
		case V4L2_PIX_FMT_UYVY:
			switch (color) {
			case 0:
				*p = odd ? b_v : g_u;
				break;
			case 1:
				*p = r_y;
				break;
			}
			break;
		case V4L2_PIX_FMT_YVYU:
			switch (color) {
			case 0:
				*p = r_y;
				break;
			case 1:
				*p = odd ? g_u : b_v;
				break;
			}
			break;
		case V4L2_PIX_FMT_VYUY:
			switch (color) {
			case 0:
				*p = odd ? g_u : b_v;
				break;
			case 1:
				*p = r_y;
				break;
			}
			break;
		case V4L2_PIX_FMT_RGB565:
			switch (color) {
			case 0:
				*p = (g_u << 5) | b_v;
				break;
			case 1:
				*p = (r_y << 3) | (g_u >> 3);
				break;
			}
			break;
		case V4L2_PIX_FMT_RGB565X:
			switch (color) {
			case 0:
				*p = (r_y << 3) | (g_u >> 3);
				break;
			case 1:
				*p = (g_u << 5) | b_v;
				break;
			}
			break;
		case V4L2_PIX_FMT_RGB555:
			switch (color) {
			case 0:
				*p = (g_u << 5) | b_v;
				break;
			case 1:
				*p = (alpha & 0x80) | (r_y << 2) | (g_u >> 3);
				break;
			}
			break;
		case V4L2_PIX_FMT_RGB555X:
			switch (color) {
			case 0:
				*p = (alpha & 0x80) | (r_y << 2) | (g_u >> 3);
				break;
			case 1:
				*p = (g_u << 5) | b_v;
				break;
			}
			break;
		case V4L2_PIX_FMT_RGB24:
			switch (color) {
			case 0:
				*p = r_y;
				break;
			case 1:
				*p = g_u;
				break;
			case 2:
				*p = b_v;
				break;
			}
			break;
		case V4L2_PIX_FMT_BGR24:
			switch (color) {
			case 0:
				*p = b_v;
				break;
			case 1:
				*p = g_u;
				break;
			case 2:
				*p = r_y;
				break;
			}
			break;
		case V4L2_PIX_FMT_RGB32:
			switch (color) {
			case 0:
				*p = alpha;
				break;
			case 1:
				*p = r_y;
				break;
			case 2:
				*p = g_u;
				break;
			case 3:
				*p = b_v;
				break;
			}
			break;
		case V4L2_PIX_FMT_BGR32:
			switch (color) {
			case 0:
				*p = b_v;
				break;
			case 1:
				*p = g_u;
				break;
			case 2:
				*p = r_y;
				break;
			case 3:
				*p = alpha;
				break;
			}
			break;
		}
	}
}

static void precalculate_line(struct vivi_dev *dev)
{
	int w;

	for (w = 0; w < dev->width * 2; w++) {
		int colorpos = w / (dev->width / 8) % 8;

		gen_twopix(dev, dev->line + w * dev->pixelsize, colorpos, w & 1);
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
		char *pos = basep + line * dev->width * dev->pixelsize + x * dev->pixelsize;
		char *s;

		for (s = text; *s; s++) {
			u8 chr = font8x16[*s * 16 + line - y];
			int i;

			for (i = 0; i < 7; i++, j++) {
				/* Draw white font on black background */
				if (chr & (1 << (7 - i)))
					gen_twopix(dev, pos + j * dev->pixelsize, WHITE, (x+y) & 1);
				else
					gen_twopix(dev, pos + j * dev->pixelsize, TEXT_BLACK, (x+y) & 1);
			}
		}
	}
}

static void vivi_fillbuff(struct vivi_dev *dev, struct vivi_buffer *buf)
{
	int wmax = dev->width;
	int hmax = dev->height;
	struct timeval ts;
	void *vbuf = vb2_plane_vaddr(&buf->vb, 0);
	unsigned ms;
	char str[100];
	int h, line = 1;
	s32 gain;

	if (!vbuf)
		return;

	for (h = 0; h < hmax; h++)
		memcpy(vbuf + h * wmax * dev->pixelsize,
		       dev->line + (dev->mv_count % wmax) * dev->pixelsize,
		       wmax * dev->pixelsize);

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

	gain = v4l2_ctrl_g_ctrl(dev->gain);
	mutex_lock(dev->ctrl_handler.lock);
	snprintf(str, sizeof(str), " brightness %3d, contrast %3d, saturation %3d, hue %d ",
			dev->brightness->cur.val,
			dev->contrast->cur.val,
			dev->saturation->cur.val,
			dev->hue->cur.val);
	gen_text(dev, vbuf, line++ * 16, 16, str);
	snprintf(str, sizeof(str), " autogain %d, gain %3d, volume %3d, alpha 0x%02x ",
			dev->autogain->cur.val, gain, dev->volume->cur.val,
			dev->alpha->cur.val);
	gen_text(dev, vbuf, line++ * 16, 16, str);
	snprintf(str, sizeof(str), " int32 %d, int64 %lld, bitmask %08x ",
			dev->int32->cur.val,
			dev->int64->cur.val64,
			dev->bitmask->cur.val);
	gen_text(dev, vbuf, line++ * 16, 16, str);
	snprintf(str, sizeof(str), " boolean %d, menu %s, string \"%s\" ",
			dev->boolean->cur.val,
			dev->menu->qmenu[dev->menu->cur.val],
			dev->string->cur.string);
	gen_text(dev, vbuf, line++ * 16, 16, str);
	snprintf(str, sizeof(str), " integer_menu %lld, value %d ",
			dev->int_menu->qmenu_int[dev->int_menu->cur.val],
			dev->int_menu->cur.val);
	gen_text(dev, vbuf, line++ * 16, 16, str);
	mutex_unlock(dev->ctrl_handler.lock);
	if (dev->button_pressed) {
		dev->button_pressed--;
		snprintf(str, sizeof(str), " button pressed!");
		gen_text(dev, vbuf, line++ * 16, 16, str);
	}

	dev->mv_count += 2;

	buf->vb.v4l2_buf.field = V4L2_FIELD_INTERLACED;
	dev->field_count++;
	buf->vb.v4l2_buf.sequence = dev->field_count >> 1;
	do_gettimeofday(&ts);
	buf->vb.v4l2_buf.timestamp = ts;
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
		spin_unlock_irqrestore(&dev->slock, flags);
		return;
	}

	buf = list_entry(dma_q->active.next, struct vivi_buffer, list);
	list_del(&buf->list);
	spin_unlock_irqrestore(&dev->slock, flags);

	do_gettimeofday(&buf->vb.v4l2_buf.timestamp);

	/* Fill buffer */
	vivi_fillbuff(dev, buf);
	dprintk(dev, 1, "filled buffer %p\n", buf);

	vb2_buffer_done(&buf->vb, VB2_BUF_STATE_DONE);
	dprintk(dev, 2, "[%p/%d] done\n", buf, buf->vb.v4l2_buf.index);
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

static int vivi_start_generating(struct vivi_dev *dev)
{
	struct vivi_dmaqueue *dma_q = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);

	/* Resets frame counters */
	dev->ms = 0;
	dev->mv_count = 0;
	dev->jiffies = jiffies;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;
	dma_q->kthread = kthread_run(vivi_thread, dev, dev->v4l2_dev.name);

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void vivi_stop_generating(struct vivi_dev *dev)
{
	struct vivi_dmaqueue *dma_q = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);

	/* shutdown control thread */
	if (dma_q->kthread) {
		kthread_stop(dma_q->kthread);
		dma_q->kthread = NULL;
	}

	/*
	 * Typical driver might need to wait here until dma engine stops.
	 * In this case we can abort imiedetly, so it's just a noop.
	 */

	/* Release all active buffers */
	while (!list_empty(&dma_q->active)) {
		struct vivi_buffer *buf;
		buf = list_entry(dma_q->active.next, struct vivi_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
		dprintk(dev, 2, "[%p/%d] done\n", buf, buf->vb.v4l2_buf.index);
	}
}
/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
static int queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], void *alloc_ctxs[])
{
	struct vivi_dev *dev = vb2_get_drv_priv(vq);
	unsigned long size;

	if (fmt)
		size = fmt->fmt.pix.sizeimage;
	else
		size = dev->width * dev->height * dev->pixelsize;

	if (size == 0)
		return -EINVAL;

	if (0 == *nbuffers)
		*nbuffers = 32;

	while (size * *nbuffers > vid_limit * 1024 * 1024)
		(*nbuffers)--;

	*nplanes = 1;

	sizes[0] = size;

	/*
	 * videobuf2-vmalloc allocator is context-less so no need to set
	 * alloc_ctxs array.
	 */

	dprintk(dev, 1, "%s, count=%d, size=%ld\n", __func__,
		*nbuffers, size);

	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct vivi_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct vivi_buffer *buf = container_of(vb, struct vivi_buffer, vb);
	unsigned long size;

	dprintk(dev, 1, "%s, field=%d\n", __func__, vb->v4l2_buf.field);

	BUG_ON(NULL == dev->fmt);

	/*
	 * Theses properties only change when queue is idle, see s_fmt.
	 * The below checks should not be performed here, on each
	 * buffer_prepare (i.e. on each qbuf). Most of the code in this function
	 * should thus be moved to buffer_init and s_fmt.
	 */
	if (dev->width  < 48 || dev->width  > MAX_WIDTH ||
	    dev->height < 32 || dev->height > MAX_HEIGHT)
		return -EINVAL;

	size = dev->width * dev->height * dev->pixelsize;
	if (vb2_plane_size(vb, 0) < size) {
		dprintk(dev, 1, "%s data will not fit into plane (%lu < %lu)\n",
				__func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(&buf->vb, 0, size);

	buf->fmt = dev->fmt;

	precalculate_bars(dev);
	precalculate_line(dev);

	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct vivi_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct vivi_buffer *buf = container_of(vb, struct vivi_buffer, vb);
	struct vivi_dmaqueue *vidq = &dev->vidq;
	unsigned long flags = 0;

	dprintk(dev, 1, "%s\n", __func__);

	spin_lock_irqsave(&dev->slock, flags);
	list_add_tail(&buf->list, &vidq->active);
	spin_unlock_irqrestore(&dev->slock, flags);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vivi_dev *dev = vb2_get_drv_priv(vq);
	dprintk(dev, 1, "%s\n", __func__);
	return vivi_start_generating(dev);
}

/* abort streaming and wait for last buffer */
static int stop_streaming(struct vb2_queue *vq)
{
	struct vivi_dev *dev = vb2_get_drv_priv(vq);
	dprintk(dev, 1, "%s\n", __func__);
	vivi_stop_generating(dev);
	return 0;
}

static void vivi_lock(struct vb2_queue *vq)
{
	struct vivi_dev *dev = vb2_get_drv_priv(vq);
	mutex_lock(&dev->mutex);
}

static void vivi_unlock(struct vb2_queue *vq)
{
	struct vivi_dev *dev = vb2_get_drv_priv(vq);
	mutex_unlock(&dev->mutex);
}


static struct vb2_ops vivi_video_qops = {
	.queue_setup		= queue_setup,
	.buf_prepare		= buffer_prepare,
	.buf_queue		= buffer_queue,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
	.wait_prepare		= vivi_unlock,
	.wait_finish		= vivi_lock,
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
	snprintf(cap->bus_info, sizeof(cap->bus_info),
			"platform:%s", dev->v4l2_dev.name);
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
			    V4L2_CAP_READWRITE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
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
	f->fmt.pix.field        = V4L2_FIELD_INTERLACED;
	f->fmt.pix.pixelformat  = dev->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * dev->fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	if (dev->fmt->is_yuv)
		f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	else
		f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct vivi_dev *dev = video_drvdata(file);
	struct vivi_fmt *fmt;

	fmt = get_format(f);
	if (!fmt) {
		dprintk(dev, 1, "Fourcc format (0x%08x) unknown.\n",
			f->fmt.pix.pixelformat);
		f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
		fmt = get_format(f);
	}

	f->fmt.pix.field = V4L2_FIELD_INTERLACED;
	v4l_bound_align_image(&f->fmt.pix.width, 48, MAX_WIDTH, 2,
			      &f->fmt.pix.height, 32, MAX_HEIGHT, 0, 0);
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	if (fmt->is_yuv)
		f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	else
		f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	f->fmt.pix.priv = 0;
	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct vivi_dev *dev = video_drvdata(file);
	struct vb2_queue *q = &dev->vb_vidq;

	int ret = vidioc_try_fmt_vid_cap(file, priv, f);
	if (ret < 0)
		return ret;

	if (vb2_is_busy(q)) {
		dprintk(dev, 1, "%s device busy\n", __func__);
		return -EBUSY;
	}

	dev->fmt = get_format(f);
	dev->pixelsize = dev->fmt->depth / 8;
	dev->width = f->fmt.pix.width;
	dev->height = f->fmt.pix.height;

	return 0;
}

static int vidioc_enum_framesizes(struct file *file, void *fh,
					 struct v4l2_frmsizeenum *fsize)
{
	static const struct v4l2_frmsize_stepwise sizes = {
		48, MAX_WIDTH, 4,
		32, MAX_HEIGHT, 1
	};
	int i;

	if (fsize->index)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (formats[i].fourcc == fsize->pixel_format)
			break;
	if (i == ARRAY_SIZE(formats))
		return -EINVAL;
	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise = sizes;
	return 0;
}

/* only one input in this sample driver */
static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	if (inp->index >= NUM_INPUTS)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
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

	if (i == dev->input)
		return 0;

	dev->input = i;
	precalculate_bars(dev);
	precalculate_line(dev);
	return 0;
}

/* --- controls ---------------------------------------------- */

static int vivi_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vivi_dev *dev = container_of(ctrl->handler, struct vivi_dev, ctrl_handler);

	if (ctrl == dev->autogain)
		dev->gain->val = jiffies & 0xff;
	return 0;
}

static int vivi_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vivi_dev *dev = container_of(ctrl->handler, struct vivi_dev, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_ALPHA_COMPONENT:
		dev->alpha_component = ctrl->val;
		break;
	default:
		if (ctrl == dev->button)
			dev->button_pressed = 30;
		break;
	}
	return 0;
}

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

static const struct v4l2_ctrl_ops vivi_ctrl_ops = {
	.g_volatile_ctrl = vivi_g_volatile_ctrl,
	.s_ctrl = vivi_s_ctrl,
};

#define VIVI_CID_CUSTOM_BASE	(V4L2_CID_USER_BASE | 0xf000)

static const struct v4l2_ctrl_config vivi_ctrl_button = {
	.ops = &vivi_ctrl_ops,
	.id = VIVI_CID_CUSTOM_BASE + 0,
	.name = "Button",
	.type = V4L2_CTRL_TYPE_BUTTON,
};

static const struct v4l2_ctrl_config vivi_ctrl_boolean = {
	.ops = &vivi_ctrl_ops,
	.id = VIVI_CID_CUSTOM_BASE + 1,
	.name = "Boolean",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 1,
};

static const struct v4l2_ctrl_config vivi_ctrl_int32 = {
	.ops = &vivi_ctrl_ops,
	.id = VIVI_CID_CUSTOM_BASE + 2,
	.name = "Integer 32 Bits",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0x80000000,
	.max = 0x7fffffff,
	.step = 1,
};

static const struct v4l2_ctrl_config vivi_ctrl_int64 = {
	.ops = &vivi_ctrl_ops,
	.id = VIVI_CID_CUSTOM_BASE + 3,
	.name = "Integer 64 Bits",
	.type = V4L2_CTRL_TYPE_INTEGER64,
};

static const char * const vivi_ctrl_menu_strings[] = {
	"Menu Item 0 (Skipped)",
	"Menu Item 1",
	"Menu Item 2 (Skipped)",
	"Menu Item 3",
	"Menu Item 4",
	"Menu Item 5 (Skipped)",
	NULL,
};

static const struct v4l2_ctrl_config vivi_ctrl_menu = {
	.ops = &vivi_ctrl_ops,
	.id = VIVI_CID_CUSTOM_BASE + 4,
	.name = "Menu",
	.type = V4L2_CTRL_TYPE_MENU,
	.min = 1,
	.max = 4,
	.def = 3,
	.menu_skip_mask = 0x04,
	.qmenu = vivi_ctrl_menu_strings,
};

static const struct v4l2_ctrl_config vivi_ctrl_string = {
	.ops = &vivi_ctrl_ops,
	.id = VIVI_CID_CUSTOM_BASE + 5,
	.name = "String",
	.type = V4L2_CTRL_TYPE_STRING,
	.min = 2,
	.max = 4,
	.step = 1,
};

static const struct v4l2_ctrl_config vivi_ctrl_bitmask = {
	.ops = &vivi_ctrl_ops,
	.id = VIVI_CID_CUSTOM_BASE + 6,
	.name = "Bitmask",
	.type = V4L2_CTRL_TYPE_BITMASK,
	.def = 0x80002000,
	.min = 0,
	.max = 0x80402010,
	.step = 0,
};

static const s64 vivi_ctrl_int_menu_values[] = {
	1, 1, 2, 3, 5, 8, 13, 21, 42,
};

static const struct v4l2_ctrl_config vivi_ctrl_int_menu = {
	.ops = &vivi_ctrl_ops,
	.id = VIVI_CID_CUSTOM_BASE + 7,
	.name = "Integer menu",
	.type = V4L2_CTRL_TYPE_INTEGER_MENU,
	.min = 1,
	.max = 8,
	.def = 4,
	.menu_skip_mask = 0x02,
	.qmenu_int = vivi_ctrl_int_menu_values,
};

static const struct v4l2_file_operations vivi_fops = {
	.owner		= THIS_MODULE,
	.open           = v4l2_fh_open,
	.release        = vb2_fop_release,
	.read           = vb2_fop_read,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops vivi_ioctl_ops = {
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_enum_framesizes   = vidioc_enum_framesizes,
	.vidioc_reqbufs       = vb2_ioctl_reqbufs,
	.vidioc_create_bufs   = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf   = vb2_ioctl_prepare_buf,
	.vidioc_querybuf      = vb2_ioctl_querybuf,
	.vidioc_qbuf          = vb2_ioctl_qbuf,
	.vidioc_dqbuf         = vb2_ioctl_dqbuf,
	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
	.vidioc_streamon      = vb2_ioctl_streamon,
	.vidioc_streamoff     = vb2_ioctl_streamoff,
	.vidioc_log_status    = v4l2_ctrl_log_status,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static struct video_device vivi_template = {
	.name		= "vivi",
	.fops           = &vivi_fops,
	.ioctl_ops 	= &vivi_ioctl_ops,
	.release	= video_device_release_empty,
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
			video_device_node_name(&dev->vdev));
		video_unregister_device(&dev->vdev);
		v4l2_device_unregister(&dev->v4l2_dev);
		v4l2_ctrl_handler_free(&dev->ctrl_handler);
		kfree(dev);
	}

	return 0;
}

static int __init vivi_create_instance(int inst)
{
	struct vivi_dev *dev;
	struct video_device *vfd;
	struct v4l2_ctrl_handler *hdl;
	struct vb2_queue *q;
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
	dev->pixelsize = dev->fmt->depth / 8;
	hdl = &dev->ctrl_handler;
	v4l2_ctrl_handler_init(hdl, 11);
	dev->volume = v4l2_ctrl_new_std(hdl, &vivi_ctrl_ops,
			V4L2_CID_AUDIO_VOLUME, 0, 255, 1, 200);
	dev->brightness = v4l2_ctrl_new_std(hdl, &vivi_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 127);
	dev->contrast = v4l2_ctrl_new_std(hdl, &vivi_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 255, 1, 16);
	dev->saturation = v4l2_ctrl_new_std(hdl, &vivi_ctrl_ops,
			V4L2_CID_SATURATION, 0, 255, 1, 127);
	dev->hue = v4l2_ctrl_new_std(hdl, &vivi_ctrl_ops,
			V4L2_CID_HUE, -128, 127, 1, 0);
	dev->autogain = v4l2_ctrl_new_std(hdl, &vivi_ctrl_ops,
			V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
	dev->gain = v4l2_ctrl_new_std(hdl, &vivi_ctrl_ops,
			V4L2_CID_GAIN, 0, 255, 1, 100);
	dev->alpha = v4l2_ctrl_new_std(hdl, &vivi_ctrl_ops,
			V4L2_CID_ALPHA_COMPONENT, 0, 255, 1, 0);
	dev->button = v4l2_ctrl_new_custom(hdl, &vivi_ctrl_button, NULL);
	dev->int32 = v4l2_ctrl_new_custom(hdl, &vivi_ctrl_int32, NULL);
	dev->int64 = v4l2_ctrl_new_custom(hdl, &vivi_ctrl_int64, NULL);
	dev->boolean = v4l2_ctrl_new_custom(hdl, &vivi_ctrl_boolean, NULL);
	dev->menu = v4l2_ctrl_new_custom(hdl, &vivi_ctrl_menu, NULL);
	dev->string = v4l2_ctrl_new_custom(hdl, &vivi_ctrl_string, NULL);
	dev->bitmask = v4l2_ctrl_new_custom(hdl, &vivi_ctrl_bitmask, NULL);
	dev->int_menu = v4l2_ctrl_new_custom(hdl, &vivi_ctrl_int_menu, NULL);
	if (hdl->error) {
		ret = hdl->error;
		goto unreg_dev;
	}
	v4l2_ctrl_auto_cluster(2, &dev->autogain, 0, true);
	dev->v4l2_dev.ctrl_handler = hdl;

	/* initialize locks */
	spin_lock_init(&dev->slock);

	/* initialize queue */
	q = &dev->vb_vidq;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	q->drv_priv = dev;
	q->buf_struct_size = sizeof(struct vivi_buffer);
	q->ops = &vivi_video_qops;
	q->mem_ops = &vb2_vmalloc_memops;

	ret = vb2_queue_init(q);
	if (ret)
		goto unreg_dev;

	mutex_init(&dev->mutex);

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	init_waitqueue_head(&dev->vidq.wq);

	vfd = &dev->vdev;
	*vfd = vivi_template;
	vfd->debug = debug;
	vfd->v4l2_dev = &dev->v4l2_dev;
	vfd->queue = q;
	set_bit(V4L2_FL_USE_FH_PRIO, &vfd->flags);

	/*
	 * Provide a mutex to v4l2 core. It will be used to protect
	 * all fops and v4l2 ioctls.
	 */
	vfd->lock = &dev->mutex;
	video_set_drvdata(vfd, dev);

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, video_nr);
	if (ret < 0)
		goto unreg_dev;

	/* Now that everything is fine, let's add it to device list */
	list_add_tail(&dev->vivi_devlist, &vivi_devlist);

	v4l2_info(&dev->v4l2_dev, "V4L2 device registered as %s\n",
		  video_device_node_name(vfd));
	return 0;

unreg_dev:
	v4l2_ctrl_handler_free(hdl);
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
			"Capture Board ver %s successfully loaded.\n",
			VIVI_VERSION);

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

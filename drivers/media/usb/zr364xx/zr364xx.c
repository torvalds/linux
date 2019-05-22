// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Zoran 364xx based USB webcam module version 0.73
 *
 * Allows you to use your USB webcam with V4L2 applications
 * This is still in heavy development !
 *
 * Copyright (C) 2004  Antoine Jacquet <royale@zerezo.com>
 * http://royale.zerezo.com/zr364xx/
 *
 * Heavily inspired by usb-skeleton.c, vicam.c, cpia.c and spca50x.c drivers
 * V4L2 version inspired by meye.c driver
 *
 * Some video buffer code by Lamarque based on s2255drv.c and vivi.c drivers.
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/highmem.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/videobuf-vmalloc.h>


/* Version Information */
#define DRIVER_VERSION "0.7.4"
#define DRIVER_AUTHOR "Antoine Jacquet, http://royale.zerezo.com/"
#define DRIVER_DESC "Zoran 364xx"


/* Camera */
#define FRAMES 1
#define MAX_FRAME_SIZE 200000
#define BUFFER_SIZE 0x1000
#define CTRL_TIMEOUT 500

#define ZR364XX_DEF_BUFS	4
#define ZR364XX_READ_IDLE	0
#define ZR364XX_READ_FRAME	1

/* Debug macro */
#define DBG(fmt, args...) \
	do { \
		if (debug) { \
			printk(KERN_INFO KBUILD_MODNAME " " fmt, ##args); \
		} \
	} while (0)

/*#define FULL_DEBUG 1*/
#ifdef FULL_DEBUG
#define _DBG DBG
#else
#define _DBG(fmt, args...)
#endif

/* Init methods, need to find nicer names for these
 * the exact names of the chipsets would be the best if someone finds it */
#define METHOD0 0
#define METHOD1 1
#define METHOD2 2
#define METHOD3 3


/* Module parameters */
static int debug;
static int mode;


/* Module parameters interface */
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level");
module_param(mode, int, 0644);
MODULE_PARM_DESC(mode, "0 = 320x240, 1 = 160x120, 2 = 640x480");


/* Devices supported by this driver
 * .driver_info contains the init method used by the camera */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x08ca, 0x0109), .driver_info = METHOD0 },
	{USB_DEVICE(0x041e, 0x4024), .driver_info = METHOD0 },
	{USB_DEVICE(0x0d64, 0x0108), .driver_info = METHOD0 },
	{USB_DEVICE(0x0546, 0x3187), .driver_info = METHOD0 },
	{USB_DEVICE(0x0d64, 0x3108), .driver_info = METHOD0 },
	{USB_DEVICE(0x0595, 0x4343), .driver_info = METHOD0 },
	{USB_DEVICE(0x0bb0, 0x500d), .driver_info = METHOD0 },
	{USB_DEVICE(0x0feb, 0x2004), .driver_info = METHOD0 },
	{USB_DEVICE(0x055f, 0xb500), .driver_info = METHOD0 },
	{USB_DEVICE(0x08ca, 0x2062), .driver_info = METHOD2 },
	{USB_DEVICE(0x052b, 0x1a18), .driver_info = METHOD1 },
	{USB_DEVICE(0x04c8, 0x0729), .driver_info = METHOD0 },
	{USB_DEVICE(0x04f2, 0xa208), .driver_info = METHOD0 },
	{USB_DEVICE(0x0784, 0x0040), .driver_info = METHOD1 },
	{USB_DEVICE(0x06d6, 0x0034), .driver_info = METHOD0 },
	{USB_DEVICE(0x0a17, 0x0062), .driver_info = METHOD2 },
	{USB_DEVICE(0x06d6, 0x003b), .driver_info = METHOD0 },
	{USB_DEVICE(0x0a17, 0x004e), .driver_info = METHOD2 },
	{USB_DEVICE(0x041e, 0x405d), .driver_info = METHOD2 },
	{USB_DEVICE(0x08ca, 0x2102), .driver_info = METHOD3 },
	{USB_DEVICE(0x06d6, 0x003d), .driver_info = METHOD0 },
	{}			/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, device_table);

/* frame structure */
struct zr364xx_framei {
	unsigned long ulState;	/* ulState:ZR364XX_READ_IDLE,
					   ZR364XX_READ_FRAME */
	void *lpvbits;		/* image data */
	unsigned long cur_size;	/* current data copied to it */
};

/* image buffer structure */
struct zr364xx_bufferi {
	unsigned long dwFrames;			/* number of frames in buffer */
	struct zr364xx_framei frame[FRAMES];	/* array of FRAME structures */
};

struct zr364xx_dmaqueue {
	struct list_head	active;
	struct zr364xx_camera	*cam;
};

struct zr364xx_pipeinfo {
	u32 transfer_size;
	u8 *transfer_buffer;
	u32 state;
	void *stream_urb;
	void *cam;	/* back pointer to zr364xx_camera struct */
	u32 err_count;
	u32 idx;
};

struct zr364xx_fmt {
	char *name;
	u32 fourcc;
	int depth;
};

/* image formats.  */
static const struct zr364xx_fmt formats[] = {
	{
		.name = "JPG",
		.fourcc = V4L2_PIX_FMT_JPEG,
		.depth = 24
	}
};

/* Camera stuff */
struct zr364xx_camera {
	struct usb_device *udev;	/* save off the usb device pointer */
	struct usb_interface *interface;/* the interface for this device */
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct video_device vdev;	/* v4l video device */
	struct v4l2_fh *owner;		/* owns the streaming */
	int nb;
	struct zr364xx_bufferi		buffer;
	int skip;
	int width;
	int height;
	int method;
	struct mutex lock;

	spinlock_t		slock;
	struct zr364xx_dmaqueue	vidq;
	int			last_frame;
	int			cur_frame;
	unsigned long		frame_count;
	int			b_acquire;
	struct zr364xx_pipeinfo	pipe[1];

	u8			read_endpoint;

	const struct zr364xx_fmt *fmt;
	struct videobuf_queue	vb_vidq;
	bool was_streaming;
};

/* buffer for one video frame */
struct zr364xx_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;
	const struct zr364xx_fmt *fmt;
};

/* function used to send initialisation commands to the camera */
static int send_control_msg(struct usb_device *udev, u8 request, u16 value,
			    u16 index, unsigned char *cp, u16 size)
{
	int status;

	unsigned char *transfer_buffer = kmalloc(size, GFP_KERNEL);
	if (!transfer_buffer)
		return -ENOMEM;

	memcpy(transfer_buffer, cp, size);

	status = usb_control_msg(udev,
				 usb_sndctrlpipe(udev, 0),
				 request,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_DEVICE, value, index,
				 transfer_buffer, size, CTRL_TIMEOUT);

	kfree(transfer_buffer);
	return status;
}


/* Control messages sent to the camera to initialize it
 * and launch the capture */
typedef struct {
	unsigned int value;
	unsigned int size;
	unsigned char *bytes;
} message;

/* method 0 */
static unsigned char m0d1[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static unsigned char m0d2[] = { 0, 0, 0, 0, 0, 0 };
static unsigned char m0d3[] = { 0, 0 };
static message m0[] = {
	{0x1f30, 0, NULL},
	{0xd000, 0, NULL},
	{0x3370, sizeof(m0d1), m0d1},
	{0x2000, 0, NULL},
	{0x2f0f, 0, NULL},
	{0x2610, sizeof(m0d2), m0d2},
	{0xe107, 0, NULL},
	{0x2502, 0, NULL},
	{0x1f70, 0, NULL},
	{0xd000, 0, NULL},
	{0x9a01, sizeof(m0d3), m0d3},
	{-1, -1, NULL}
};

/* method 1 */
static unsigned char m1d1[] = { 0xff, 0xff };
static unsigned char m1d2[] = { 0x00, 0x00 };
static message m1[] = {
	{0x1f30, 0, NULL},
	{0xd000, 0, NULL},
	{0xf000, 0, NULL},
	{0x2000, 0, NULL},
	{0x2f0f, 0, NULL},
	{0x2650, 0, NULL},
	{0xe107, 0, NULL},
	{0x2502, sizeof(m1d1), m1d1},
	{0x1f70, 0, NULL},
	{0xd000, 0, NULL},
	{0xd000, 0, NULL},
	{0xd000, 0, NULL},
	{0x9a01, sizeof(m1d2), m1d2},
	{-1, -1, NULL}
};

/* method 2 */
static unsigned char m2d1[] = { 0xff, 0xff };
static message m2[] = {
	{0x1f30, 0, NULL},
	{0xf000, 0, NULL},
	{0x2000, 0, NULL},
	{0x2f0f, 0, NULL},
	{0x2650, 0, NULL},
	{0xe107, 0, NULL},
	{0x2502, sizeof(m2d1), m2d1},
	{0x1f70, 0, NULL},
	{-1, -1, NULL}
};

/* init table */
static message *init[4] = { m0, m1, m2, m2 };


/* JPEG static data in header (Huffman table, etc) */
static unsigned char header1[] = {
	0xFF, 0xD8,
	/*
	0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F',
	0x00, 0x01, 0x01, 0x00, 0x33, 0x8A, 0x00, 0x00, 0x33, 0x88,
	*/
	0xFF, 0xDB, 0x00, 0x84
};
static unsigned char header2[] = {
	0xFF, 0xC4, 0x00, 0x1F, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
	0xFF, 0xC4, 0x00, 0xB5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02,
	0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D, 0x01,
	0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
	0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1,
	0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33,
	0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25,
	0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
	0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54,
	0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
	0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94,
	0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
	0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8,
	0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA,
	0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
	0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3,
	0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xC4, 0x00, 0x1F,
	0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
	0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5,
	0x11, 0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05,
	0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11,
	0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
	0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1,
	0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16,
	0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27,
	0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44,
	0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57,
	0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A,
	0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84,
	0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96,
	0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8,
	0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA,
	0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3,
	0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5,
	0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
	0xF8, 0xF9, 0xFA, 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00, 0xF0, 0x01,
	0x40, 0x03, 0x01, 0x21, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01,
	0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11,
	0x00, 0x3F, 0x00
};
static unsigned char header3;

/* ------------------------------------------------------------------
   Videobuf operations
   ------------------------------------------------------------------*/

static int buffer_setup(struct videobuf_queue *vq, unsigned int *count,
			unsigned int *size)
{
	struct zr364xx_camera *cam = vq->priv_data;

	*size = cam->width * cam->height * (cam->fmt->depth >> 3);

	if (*count == 0)
		*count = ZR364XX_DEF_BUFS;

	if (*size * *count > ZR364XX_DEF_BUFS * 1024 * 1024)
		*count = (ZR364XX_DEF_BUFS * 1024 * 1024) / *size;

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct zr364xx_buffer *buf)
{
	_DBG("%s\n", __func__);

	BUG_ON(in_interrupt());

	videobuf_vmalloc_free(&buf->vb);
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static int buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
			  enum v4l2_field field)
{
	struct zr364xx_camera *cam = vq->priv_data;
	struct zr364xx_buffer *buf = container_of(vb, struct zr364xx_buffer,
						  vb);
	int rc;

	DBG("%s, field=%d, fmt name = %s\n", __func__, field,
	    cam->fmt ? cam->fmt->name : "");
	if (!cam->fmt)
		return -EINVAL;

	buf->vb.size = cam->width * cam->height * (cam->fmt->depth >> 3);

	if (buf->vb.baddr != 0 && buf->vb.bsize < buf->vb.size) {
		DBG("invalid buffer prepare\n");
		return -EINVAL;
	}

	buf->fmt = cam->fmt;
	buf->vb.width = cam->width;
	buf->vb.height = cam->height;
	buf->vb.field = field;

	if (buf->vb.state == VIDEOBUF_NEEDS_INIT) {
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

static void buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct zr364xx_buffer *buf = container_of(vb, struct zr364xx_buffer,
						  vb);
	struct zr364xx_camera *cam = vq->priv_data;

	_DBG("%s\n", __func__);

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &cam->vidq.active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct zr364xx_buffer *buf = container_of(vb, struct zr364xx_buffer,
						  vb);

	_DBG("%s\n", __func__);
	free_buffer(vq, buf);
}

static const struct videobuf_queue_ops zr364xx_video_qops = {
	.buf_setup = buffer_setup,
	.buf_prepare = buffer_prepare,
	.buf_queue = buffer_queue,
	.buf_release = buffer_release,
};

/********************/
/* V4L2 integration */
/********************/
static int zr364xx_vidioc_streamon(struct file *file, void *priv,
				   enum v4l2_buf_type type);

static ssize_t zr364xx_read(struct file *file, char __user *buf, size_t count,
			    loff_t * ppos)
{
	struct zr364xx_camera *cam = video_drvdata(file);
	int err = 0;

	_DBG("%s\n", __func__);

	if (!buf)
		return -EINVAL;

	if (!count)
		return -EINVAL;

	if (mutex_lock_interruptible(&cam->lock))
		return -ERESTARTSYS;

	err = zr364xx_vidioc_streamon(file, file->private_data,
				V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (err == 0) {
		DBG("%s: reading %d bytes at pos %d.\n", __func__,
				(int) count, (int) *ppos);

		/* NoMan Sux ! */
		err = videobuf_read_one(&cam->vb_vidq, buf, count, ppos,
					file->f_flags & O_NONBLOCK);
	}
	mutex_unlock(&cam->lock);
	return err;
}

/* video buffer vmalloc implementation based partly on VIVI driver which is
 *          Copyright (c) 2006 by
 *                  Mauro Carvalho Chehab <mchehab--a.t--infradead.org>
 *                  Ted Walther <ted--a.t--enumera.com>
 *                  John Sokol <sokol--a.t--videotechnology.com>
 *                  http://v4l.videotechnology.com/
 *
 */
static void zr364xx_fillbuff(struct zr364xx_camera *cam,
			     struct zr364xx_buffer *buf,
			     int jpgsize)
{
	int pos = 0;
	const char *tmpbuf;
	char *vbuf = videobuf_to_vmalloc(&buf->vb);
	unsigned long last_frame;

	if (!vbuf)
		return;

	last_frame = cam->last_frame;
	if (last_frame != -1) {
		tmpbuf = (const char *)cam->buffer.frame[last_frame].lpvbits;
		switch (buf->fmt->fourcc) {
		case V4L2_PIX_FMT_JPEG:
			buf->vb.size = jpgsize;
			memcpy(vbuf, tmpbuf, buf->vb.size);
			break;
		default:
			printk(KERN_DEBUG KBUILD_MODNAME ": unknown format?\n");
		}
		cam->last_frame = -1;
	} else {
		printk(KERN_ERR KBUILD_MODNAME ": =======no frame\n");
		return;
	}
	DBG("%s: Buffer %p size= %d\n", __func__, vbuf, pos);
	/* tell v4l buffer was filled */

	buf->vb.field_count = cam->frame_count * 2;
	buf->vb.ts = ktime_get_ns();
	buf->vb.state = VIDEOBUF_DONE;
}

static int zr364xx_got_frame(struct zr364xx_camera *cam, int jpgsize)
{
	struct zr364xx_dmaqueue *dma_q = &cam->vidq;
	struct zr364xx_buffer *buf;
	unsigned long flags = 0;
	int rc = 0;

	DBG("wakeup: %p\n", &dma_q);
	spin_lock_irqsave(&cam->slock, flags);

	if (list_empty(&dma_q->active)) {
		DBG("No active queue to serve\n");
		rc = -1;
		goto unlock;
	}
	buf = list_entry(dma_q->active.next,
			 struct zr364xx_buffer, vb.queue);

	if (!waitqueue_active(&buf->vb.done)) {
		/* no one active */
		rc = -1;
		goto unlock;
	}
	list_del(&buf->vb.queue);
	buf->vb.ts = ktime_get_ns();
	DBG("[%p/%d] wakeup\n", buf, buf->vb.i);
	zr364xx_fillbuff(cam, buf, jpgsize);
	wake_up(&buf->vb.done);
	DBG("wakeup [buf/i] [%p/%d]\n", buf, buf->vb.i);
unlock:
	spin_unlock_irqrestore(&cam->slock, flags);
	return rc;
}

/* this function moves the usb stream read pipe data
 * into the system buffers.
 * returns 0 on success, EAGAIN if more data to process (call this
 * function again).
 */
static int zr364xx_read_video_callback(struct zr364xx_camera *cam,
					struct zr364xx_pipeinfo *pipe_info,
					struct urb *purb)
{
	unsigned char *pdest;
	unsigned char *psrc;
	s32 idx = -1;
	struct zr364xx_framei *frm;
	int i = 0;
	unsigned char *ptr = NULL;

	_DBG("buffer to user\n");
	idx = cam->cur_frame;
	frm = &cam->buffer.frame[idx];

	/* swap bytes if camera needs it */
	if (cam->method == METHOD0) {
		u16 *buf = (u16 *)pipe_info->transfer_buffer;
		for (i = 0; i < purb->actual_length/2; i++)
			swab16s(buf + i);
	}

	/* search done.  now find out if should be acquiring */
	if (!cam->b_acquire) {
		/* we found a frame, but this channel is turned off */
		frm->ulState = ZR364XX_READ_IDLE;
		return -EINVAL;
	}

	psrc = (u8 *)pipe_info->transfer_buffer;
	ptr = pdest = frm->lpvbits;

	if (frm->ulState == ZR364XX_READ_IDLE) {
		if (purb->actual_length < 128) {
			/* header incomplete */
			dev_info(&cam->udev->dev,
				 "%s: buffer (%d bytes) too small to hold jpeg header. Discarding.\n",
				 __func__, purb->actual_length);
			return -EINVAL;
		}

		frm->ulState = ZR364XX_READ_FRAME;
		frm->cur_size = 0;

		_DBG("jpeg header, ");
		memcpy(ptr, header1, sizeof(header1));
		ptr += sizeof(header1);
		header3 = 0;
		memcpy(ptr, &header3, 1);
		ptr++;
		memcpy(ptr, psrc, 64);
		ptr += 64;
		header3 = 1;
		memcpy(ptr, &header3, 1);
		ptr++;
		memcpy(ptr, psrc + 64, 64);
		ptr += 64;
		memcpy(ptr, header2, sizeof(header2));
		ptr += sizeof(header2);
		memcpy(ptr, psrc + 128,
		       purb->actual_length - 128);
		ptr += purb->actual_length - 128;
		_DBG("header : %d %d %d %d %d %d %d %d %d\n",
		    psrc[0], psrc[1], psrc[2],
		    psrc[3], psrc[4], psrc[5],
		    psrc[6], psrc[7], psrc[8]);
		frm->cur_size = ptr - pdest;
	} else {
		if (frm->cur_size + purb->actual_length > MAX_FRAME_SIZE) {
			dev_info(&cam->udev->dev,
				 "%s: buffer (%d bytes) too small to hold frame data. Discarding frame data.\n",
				 __func__, MAX_FRAME_SIZE);
		} else {
			pdest += frm->cur_size;
			memcpy(pdest, psrc, purb->actual_length);
			frm->cur_size += purb->actual_length;
		}
	}
	/*_DBG("cur_size %lu urb size %d\n", frm->cur_size,
		purb->actual_length);*/

	if (purb->actual_length < pipe_info->transfer_size) {
		_DBG("****************Buffer[%d]full*************\n", idx);
		cam->last_frame = cam->cur_frame;
		cam->cur_frame++;
		/* end of system frame ring buffer, start at zero */
		if (cam->cur_frame == cam->buffer.dwFrames)
			cam->cur_frame = 0;

		/* frame ready */
		/* go back to find the JPEG EOI marker */
		ptr = pdest = frm->lpvbits;
		ptr += frm->cur_size - 2;
		while (ptr > pdest) {
			if (*ptr == 0xFF && *(ptr + 1) == 0xD9
			    && *(ptr + 2) == 0xFF)
				break;
			ptr--;
		}
		if (ptr == pdest)
			DBG("No EOI marker\n");

		/* Sometimes there is junk data in the middle of the picture,
		 * we want to skip this bogus frames */
		while (ptr > pdest) {
			if (*ptr == 0xFF && *(ptr + 1) == 0xFF
			    && *(ptr + 2) == 0xFF)
				break;
			ptr--;
		}
		if (ptr != pdest) {
			DBG("Bogus frame ? %d\n", ++(cam->nb));
		} else if (cam->b_acquire) {
			/* we skip the 2 first frames which are usually buggy */
			if (cam->skip)
				cam->skip--;
			else {
				_DBG("jpeg(%lu): %d %d %d %d %d %d %d %d\n",
				    frm->cur_size,
				    pdest[0], pdest[1], pdest[2], pdest[3],
				    pdest[4], pdest[5], pdest[6], pdest[7]);

				zr364xx_got_frame(cam, frm->cur_size);
			}
		}
		cam->frame_count++;
		frm->ulState = ZR364XX_READ_IDLE;
		frm->cur_size = 0;
	}
	/* done successfully */
	return 0;
}

static int zr364xx_vidioc_querycap(struct file *file, void *priv,
				   struct v4l2_capability *cap)
{
	struct zr364xx_camera *cam = video_drvdata(file);

	strscpy(cap->driver, DRIVER_DESC, sizeof(cap->driver));
	if (cam->udev->product)
		strscpy(cap->card, cam->udev->product, sizeof(cap->card));
	strscpy(cap->bus_info, dev_name(&cam->udev->dev),
		sizeof(cap->bus_info));
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE |
			    V4L2_CAP_READWRITE |
			    V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int zr364xx_vidioc_enum_input(struct file *file, void *priv,
				     struct v4l2_input *i)
{
	if (i->index != 0)
		return -EINVAL;
	strscpy(i->name, DRIVER_DESC " Camera", sizeof(i->name));
	i->type = V4L2_INPUT_TYPE_CAMERA;
	return 0;
}

static int zr364xx_vidioc_g_input(struct file *file, void *priv,
				  unsigned int *i)
{
	*i = 0;
	return 0;
}

static int zr364xx_vidioc_s_input(struct file *file, void *priv,
				  unsigned int i)
{
	if (i != 0)
		return -EINVAL;
	return 0;
}

static int zr364xx_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct zr364xx_camera *cam =
		container_of(ctrl->handler, struct zr364xx_camera, ctrl_handler);
	int temp;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		/* hardware brightness */
		send_control_msg(cam->udev, 1, 0x2001, 0, NULL, 0);
		temp = (0x60 << 8) + 127 - ctrl->val;
		send_control_msg(cam->udev, 1, temp, 0, NULL, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int zr364xx_vidioc_enum_fmt_vid_cap(struct file *file,
				       void *priv, struct v4l2_fmtdesc *f)
{
	if (f->index > 0)
		return -EINVAL;
	f->flags = V4L2_FMT_FLAG_COMPRESSED;
	strscpy(f->description, formats[0].name, sizeof(f->description));
	f->pixelformat = formats[0].fourcc;
	return 0;
}

static char *decode_fourcc(__u32 pixelformat, char *buf)
{
	buf[0] = pixelformat & 0xff;
	buf[1] = (pixelformat >> 8) & 0xff;
	buf[2] = (pixelformat >> 16) & 0xff;
	buf[3] = (pixelformat >> 24) & 0xff;
	buf[4] = '\0';
	return buf;
}

static int zr364xx_vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_format *f)
{
	struct zr364xx_camera *cam = video_drvdata(file);
	char pixelformat_name[5];

	if (!cam)
		return -ENODEV;

	if (f->fmt.pix.pixelformat != V4L2_PIX_FMT_JPEG) {
		DBG("%s: unsupported pixelformat V4L2_PIX_FMT_%s\n", __func__,
		    decode_fourcc(f->fmt.pix.pixelformat, pixelformat_name));
		return -EINVAL;
	}

	if (!(f->fmt.pix.width == 160 && f->fmt.pix.height == 120) &&
	    !(f->fmt.pix.width == 640 && f->fmt.pix.height == 480)) {
		f->fmt.pix.width = 320;
		f->fmt.pix.height = 240;
	}

	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	DBG("%s: V4L2_PIX_FMT_%s (%d) ok!\n", __func__,
	    decode_fourcc(f->fmt.pix.pixelformat, pixelformat_name),
	    f->fmt.pix.field);
	return 0;
}

static int zr364xx_vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct zr364xx_camera *cam;

	if (!file)
		return -ENODEV;
	cam = video_drvdata(file);

	f->fmt.pix.pixelformat = formats[0].fourcc;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.width = cam->width;
	f->fmt.pix.height = cam->height;
	f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	return 0;
}

static int zr364xx_vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct zr364xx_camera *cam = video_drvdata(file);
	struct videobuf_queue *q = &cam->vb_vidq;
	char pixelformat_name[5];
	int ret = zr364xx_vidioc_try_fmt_vid_cap(file, cam, f);
	int i;

	if (ret < 0)
		return ret;

	mutex_lock(&q->vb_lock);

	if (videobuf_queue_is_busy(&cam->vb_vidq)) {
		DBG("%s queue busy\n", __func__);
		ret = -EBUSY;
		goto out;
	}

	if (cam->owner) {
		DBG("%s can't change format after started\n", __func__);
		ret = -EBUSY;
		goto out;
	}

	cam->width = f->fmt.pix.width;
	cam->height = f->fmt.pix.height;
	DBG("%s: %dx%d mode selected\n", __func__,
		 cam->width, cam->height);
	f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	cam->vb_vidq.field = f->fmt.pix.field;

	if (f->fmt.pix.width == 160 && f->fmt.pix.height == 120)
		mode = 1;
	else if (f->fmt.pix.width == 640 && f->fmt.pix.height == 480)
		mode = 2;
	else
		mode = 0;

	m0d1[0] = mode;
	m1[2].value = 0xf000 + mode;
	m2[1].value = 0xf000 + mode;

	/* special case for METHOD3, the modes are different */
	if (cam->method == METHOD3) {
		switch (mode) {
		case 1:
			m2[1].value = 0xf000 + 4;
			break;
		case 2:
			m2[1].value = 0xf000 + 0;
			break;
		default:
			m2[1].value = 0xf000 + 1;
			break;
		}
	}

	header2[437] = cam->height / 256;
	header2[438] = cam->height % 256;
	header2[439] = cam->width / 256;
	header2[440] = cam->width % 256;

	for (i = 0; init[cam->method][i].size != -1; i++) {
		ret =
		    send_control_msg(cam->udev, 1, init[cam->method][i].value,
				     0, init[cam->method][i].bytes,
				     init[cam->method][i].size);
		if (ret < 0) {
			dev_err(&cam->udev->dev,
			   "error during resolution change sequence: %d\n", i);
			goto out;
		}
	}

	/* Added some delay here, since opening/closing the camera quickly,
	 * like Ekiga does during its startup, can crash the webcam
	 */
	mdelay(100);
	cam->skip = 2;
	ret = 0;

out:
	mutex_unlock(&q->vb_lock);

	DBG("%s: V4L2_PIX_FMT_%s (%d) ok!\n", __func__,
	    decode_fourcc(f->fmt.pix.pixelformat, pixelformat_name),
	    f->fmt.pix.field);
	return ret;
}

static int zr364xx_vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct zr364xx_camera *cam = video_drvdata(file);

	if (cam->owner && cam->owner != priv)
		return -EBUSY;
	return videobuf_reqbufs(&cam->vb_vidq, p);
}

static int zr364xx_vidioc_querybuf(struct file *file,
				void *priv,
				struct v4l2_buffer *p)
{
	int rc;
	struct zr364xx_camera *cam = video_drvdata(file);
	rc = videobuf_querybuf(&cam->vb_vidq, p);
	return rc;
}

static int zr364xx_vidioc_qbuf(struct file *file,
				void *priv,
				struct v4l2_buffer *p)
{
	int rc;
	struct zr364xx_camera *cam = video_drvdata(file);
	_DBG("%s\n", __func__);
	if (cam->owner && cam->owner != priv)
		return -EBUSY;
	rc = videobuf_qbuf(&cam->vb_vidq, p);
	return rc;
}

static int zr364xx_vidioc_dqbuf(struct file *file,
				void *priv,
				struct v4l2_buffer *p)
{
	int rc;
	struct zr364xx_camera *cam = video_drvdata(file);
	_DBG("%s\n", __func__);
	if (cam->owner && cam->owner != priv)
		return -EBUSY;
	rc = videobuf_dqbuf(&cam->vb_vidq, p, file->f_flags & O_NONBLOCK);
	return rc;
}

static void read_pipe_completion(struct urb *purb)
{
	struct zr364xx_pipeinfo *pipe_info;
	struct zr364xx_camera *cam;
	int pipe;

	pipe_info = purb->context;
	_DBG("%s %p, status %d\n", __func__, purb, purb->status);
	if (!pipe_info) {
		printk(KERN_ERR KBUILD_MODNAME ": no context!\n");
		return;
	}

	cam = pipe_info->cam;
	if (!cam) {
		printk(KERN_ERR KBUILD_MODNAME ": no context!\n");
		return;
	}

	/* if shutting down, do not resubmit, exit immediately */
	if (purb->status == -ESHUTDOWN) {
		DBG("%s, err shutdown\n", __func__);
		pipe_info->err_count++;
		return;
	}

	if (pipe_info->state == 0) {
		DBG("exiting USB pipe\n");
		return;
	}

	if (purb->actual_length > pipe_info->transfer_size) {
		dev_err(&cam->udev->dev, "wrong number of bytes\n");
		return;
	}

	if (purb->status == 0)
		zr364xx_read_video_callback(cam, pipe_info, purb);
	else {
		pipe_info->err_count++;
		DBG("%s: failed URB %d\n", __func__, purb->status);
	}

	pipe = usb_rcvbulkpipe(cam->udev, cam->read_endpoint);

	/* reuse urb */
	usb_fill_bulk_urb(pipe_info->stream_urb, cam->udev,
			  pipe,
			  pipe_info->transfer_buffer,
			  pipe_info->transfer_size,
			  read_pipe_completion, pipe_info);

	if (pipe_info->state != 0) {
		purb->status = usb_submit_urb(pipe_info->stream_urb,
					      GFP_ATOMIC);

		if (purb->status)
			dev_err(&cam->udev->dev,
				"error submitting urb (error=%i)\n",
				purb->status);
	} else
		DBG("read pipe complete state 0\n");
}

static int zr364xx_start_readpipe(struct zr364xx_camera *cam)
{
	int pipe;
	int retval;
	struct zr364xx_pipeinfo *pipe_info = cam->pipe;
	pipe = usb_rcvbulkpipe(cam->udev, cam->read_endpoint);
	DBG("%s: start pipe IN x%x\n", __func__, cam->read_endpoint);

	pipe_info->state = 1;
	pipe_info->err_count = 0;
	pipe_info->stream_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pipe_info->stream_urb)
		return -ENOMEM;
	/* transfer buffer allocated in board_init */
	usb_fill_bulk_urb(pipe_info->stream_urb, cam->udev,
			  pipe,
			  pipe_info->transfer_buffer,
			  pipe_info->transfer_size,
			  read_pipe_completion, pipe_info);

	DBG("submitting URB %p\n", pipe_info->stream_urb);
	retval = usb_submit_urb(pipe_info->stream_urb, GFP_KERNEL);
	if (retval) {
		printk(KERN_ERR KBUILD_MODNAME ": start read pipe failed\n");
		return retval;
	}

	return 0;
}

static void zr364xx_stop_readpipe(struct zr364xx_camera *cam)
{
	struct zr364xx_pipeinfo *pipe_info;

	if (!cam) {
		printk(KERN_ERR KBUILD_MODNAME ": invalid device\n");
		return;
	}
	DBG("stop read pipe\n");
	pipe_info = cam->pipe;
	if (pipe_info) {
		if (pipe_info->state != 0)
			pipe_info->state = 0;

		if (pipe_info->stream_urb) {
			/* cancel urb */
			usb_kill_urb(pipe_info->stream_urb);
			usb_free_urb(pipe_info->stream_urb);
			pipe_info->stream_urb = NULL;
		}
	}
	return;
}

/* starts acquisition process */
static int zr364xx_start_acquire(struct zr364xx_camera *cam)
{
	int j;

	DBG("start acquire\n");

	cam->last_frame = -1;
	cam->cur_frame = 0;
	for (j = 0; j < FRAMES; j++) {
		cam->buffer.frame[j].ulState = ZR364XX_READ_IDLE;
		cam->buffer.frame[j].cur_size = 0;
	}
	cam->b_acquire = 1;
	return 0;
}

static inline int zr364xx_stop_acquire(struct zr364xx_camera *cam)
{
	cam->b_acquire = 0;
	return 0;
}

static int zr364xx_prepare(struct zr364xx_camera *cam)
{
	int res;
	int i, j;

	for (i = 0; init[cam->method][i].size != -1; i++) {
		res = send_control_msg(cam->udev, 1, init[cam->method][i].value,
				     0, init[cam->method][i].bytes,
				     init[cam->method][i].size);
		if (res < 0) {
			dev_err(&cam->udev->dev,
				"error during open sequence: %d\n", i);
			return res;
		}
	}

	cam->skip = 2;
	cam->last_frame = -1;
	cam->cur_frame = 0;
	cam->frame_count = 0;
	for (j = 0; j < FRAMES; j++) {
		cam->buffer.frame[j].ulState = ZR364XX_READ_IDLE;
		cam->buffer.frame[j].cur_size = 0;
	}
	v4l2_ctrl_handler_setup(&cam->ctrl_handler);
	return 0;
}

static int zr364xx_vidioc_streamon(struct file *file, void *priv,
				   enum v4l2_buf_type type)
{
	struct zr364xx_camera *cam = video_drvdata(file);
	int res;

	DBG("%s\n", __func__);

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (cam->owner && cam->owner != priv)
		return -EBUSY;

	res = zr364xx_prepare(cam);
	if (res)
		return res;
	res = videobuf_streamon(&cam->vb_vidq);
	if (res == 0) {
		zr364xx_start_acquire(cam);
		cam->owner = file->private_data;
	}
	return res;
}

static int zr364xx_vidioc_streamoff(struct file *file, void *priv,
				    enum v4l2_buf_type type)
{
	struct zr364xx_camera *cam = video_drvdata(file);

	DBG("%s\n", __func__);
	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (cam->owner && cam->owner != priv)
		return -EBUSY;
	zr364xx_stop_acquire(cam);
	return videobuf_streamoff(&cam->vb_vidq);
}


/* open the camera */
static int zr364xx_open(struct file *file)
{
	struct zr364xx_camera *cam = video_drvdata(file);
	int err;

	DBG("%s\n", __func__);

	if (mutex_lock_interruptible(&cam->lock))
		return -ERESTARTSYS;

	err = v4l2_fh_open(file);
	if (err)
		goto out;

	/* Added some delay here, since opening/closing the camera quickly,
	 * like Ekiga does during its startup, can crash the webcam
	 */
	mdelay(100);
	err = 0;

out:
	mutex_unlock(&cam->lock);
	DBG("%s: %d\n", __func__, err);
	return err;
}

static void zr364xx_release(struct v4l2_device *v4l2_dev)
{
	struct zr364xx_camera *cam =
		container_of(v4l2_dev, struct zr364xx_camera, v4l2_dev);
	unsigned long i;

	v4l2_device_unregister(&cam->v4l2_dev);

	videobuf_mmap_free(&cam->vb_vidq);

	/* release sys buffers */
	for (i = 0; i < FRAMES; i++) {
		if (cam->buffer.frame[i].lpvbits) {
			DBG("vfree %p\n", cam->buffer.frame[i].lpvbits);
			vfree(cam->buffer.frame[i].lpvbits);
		}
		cam->buffer.frame[i].lpvbits = NULL;
	}

	v4l2_ctrl_handler_free(&cam->ctrl_handler);
	/* release transfer buffer */
	kfree(cam->pipe->transfer_buffer);
	kfree(cam);
}

/* release the camera */
static int zr364xx_close(struct file *file)
{
	struct zr364xx_camera *cam;
	struct usb_device *udev;
	int i;

	DBG("%s\n", __func__);
	cam = video_drvdata(file);

	mutex_lock(&cam->lock);
	udev = cam->udev;

	if (file->private_data == cam->owner) {
		/* turn off stream */
		if (cam->b_acquire)
			zr364xx_stop_acquire(cam);
		videobuf_streamoff(&cam->vb_vidq);

		for (i = 0; i < 2; i++) {
			send_control_msg(udev, 1, init[cam->method][i].value,
					0, init[cam->method][i].bytes,
					init[cam->method][i].size);
		}
		cam->owner = NULL;
	}

	/* Added some delay here, since opening/closing the camera quickly,
	 * like Ekiga does during its startup, can crash the webcam
	 */
	mdelay(100);
	mutex_unlock(&cam->lock);
	return v4l2_fh_release(file);
}


static int zr364xx_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct zr364xx_camera *cam = video_drvdata(file);
	int ret;

	if (!cam) {
		DBG("%s: cam == NULL\n", __func__);
		return -ENODEV;
	}
	DBG("mmap called, vma=%p\n", vma);

	ret = videobuf_mmap_mapper(&cam->vb_vidq, vma);

	DBG("vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end - (unsigned long)vma->vm_start, ret);
	return ret;
}

static __poll_t zr364xx_poll(struct file *file,
			       struct poll_table_struct *wait)
{
	struct zr364xx_camera *cam = video_drvdata(file);
	struct videobuf_queue *q = &cam->vb_vidq;
	__poll_t res = v4l2_ctrl_poll(file, wait);

	_DBG("%s\n", __func__);

	return res | videobuf_poll_stream(file, q, wait);
}

static const struct v4l2_ctrl_ops zr364xx_ctrl_ops = {
	.s_ctrl = zr364xx_s_ctrl,
};

static const struct v4l2_file_operations zr364xx_fops = {
	.owner = THIS_MODULE,
	.open = zr364xx_open,
	.release = zr364xx_close,
	.read = zr364xx_read,
	.mmap = zr364xx_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = zr364xx_poll,
};

static const struct v4l2_ioctl_ops zr364xx_ioctl_ops = {
	.vidioc_querycap	= zr364xx_vidioc_querycap,
	.vidioc_enum_fmt_vid_cap = zr364xx_vidioc_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	= zr364xx_vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= zr364xx_vidioc_s_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	= zr364xx_vidioc_g_fmt_vid_cap,
	.vidioc_enum_input	= zr364xx_vidioc_enum_input,
	.vidioc_g_input		= zr364xx_vidioc_g_input,
	.vidioc_s_input		= zr364xx_vidioc_s_input,
	.vidioc_streamon	= zr364xx_vidioc_streamon,
	.vidioc_streamoff	= zr364xx_vidioc_streamoff,
	.vidioc_reqbufs         = zr364xx_vidioc_reqbufs,
	.vidioc_querybuf        = zr364xx_vidioc_querybuf,
	.vidioc_qbuf            = zr364xx_vidioc_qbuf,
	.vidioc_dqbuf           = zr364xx_vidioc_dqbuf,
	.vidioc_log_status      = v4l2_ctrl_log_status,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct video_device zr364xx_template = {
	.name = DRIVER_DESC,
	.fops = &zr364xx_fops,
	.ioctl_ops = &zr364xx_ioctl_ops,
	.release = video_device_release_empty,
};



/*******************/
/* USB integration */
/*******************/
static int zr364xx_board_init(struct zr364xx_camera *cam)
{
	struct zr364xx_pipeinfo *pipe = cam->pipe;
	unsigned long i;

	DBG("board init: %p\n", cam);
	memset(pipe, 0, sizeof(*pipe));
	pipe->cam = cam;
	pipe->transfer_size = BUFFER_SIZE;

	pipe->transfer_buffer = kzalloc(pipe->transfer_size,
					GFP_KERNEL);
	if (!pipe->transfer_buffer) {
		DBG("out of memory!\n");
		return -ENOMEM;
	}

	cam->b_acquire = 0;
	cam->frame_count = 0;

	/*** start create system buffers ***/
	for (i = 0; i < FRAMES; i++) {
		/* always allocate maximum size for system buffers */
		cam->buffer.frame[i].lpvbits = vmalloc(MAX_FRAME_SIZE);

		DBG("valloc %p, idx %lu, pdata %p\n",
			&cam->buffer.frame[i], i,
			cam->buffer.frame[i].lpvbits);
		if (!cam->buffer.frame[i].lpvbits) {
			printk(KERN_INFO KBUILD_MODNAME ": out of memory. Using less frames\n");
			break;
		}
	}

	if (i == 0) {
		printk(KERN_INFO KBUILD_MODNAME ": out of memory. Aborting\n");
		kfree(cam->pipe->transfer_buffer);
		cam->pipe->transfer_buffer = NULL;
		return -ENOMEM;
	} else
		cam->buffer.dwFrames = i;

	/* make sure internal states are set */
	for (i = 0; i < FRAMES; i++) {
		cam->buffer.frame[i].ulState = ZR364XX_READ_IDLE;
		cam->buffer.frame[i].cur_size = 0;
	}

	cam->cur_frame = 0;
	cam->last_frame = -1;
	/*** end create system buffers ***/

	/* start read pipe */
	zr364xx_start_readpipe(cam);
	DBG(": board initialized\n");
	return 0;
}

static int zr364xx_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct zr364xx_camera *cam = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	struct v4l2_ctrl_handler *hdl;
	int err;
	int i;

	DBG("probing...\n");

	dev_info(&intf->dev, DRIVER_DESC " compatible webcam plugged\n");
	dev_info(&intf->dev, "model %04x:%04x detected\n",
		 le16_to_cpu(udev->descriptor.idVendor),
		 le16_to_cpu(udev->descriptor.idProduct));

	cam = kzalloc(sizeof(*cam), GFP_KERNEL);
	if (!cam)
		return -ENOMEM;

	cam->v4l2_dev.release = zr364xx_release;
	err = v4l2_device_register(&intf->dev, &cam->v4l2_dev);
	if (err < 0) {
		dev_err(&udev->dev, "couldn't register v4l2_device\n");
		kfree(cam);
		return err;
	}
	hdl = &cam->ctrl_handler;
	v4l2_ctrl_handler_init(hdl, 1);
	v4l2_ctrl_new_std(hdl, &zr364xx_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 127, 1, 64);
	if (hdl->error) {
		err = hdl->error;
		dev_err(&udev->dev, "couldn't register control\n");
		goto fail;
	}
	/* save the init method used by this camera */
	cam->method = id->driver_info;
	mutex_init(&cam->lock);
	cam->vdev = zr364xx_template;
	cam->vdev.lock = &cam->lock;
	cam->vdev.v4l2_dev = &cam->v4l2_dev;
	cam->vdev.ctrl_handler = &cam->ctrl_handler;
	video_set_drvdata(&cam->vdev, cam);

	cam->udev = udev;

	switch (mode) {
	case 1:
		dev_info(&udev->dev, "160x120 mode selected\n");
		cam->width = 160;
		cam->height = 120;
		break;
	case 2:
		dev_info(&udev->dev, "640x480 mode selected\n");
		cam->width = 640;
		cam->height = 480;
		break;
	default:
		dev_info(&udev->dev, "320x240 mode selected\n");
		cam->width = 320;
		cam->height = 240;
		break;
	}

	m0d1[0] = mode;
	m1[2].value = 0xf000 + mode;
	m2[1].value = 0xf000 + mode;

	/* special case for METHOD3, the modes are different */
	if (cam->method == METHOD3) {
		switch (mode) {
		case 1:
			m2[1].value = 0xf000 + 4;
			break;
		case 2:
			m2[1].value = 0xf000 + 0;
			break;
		default:
			m2[1].value = 0xf000 + 1;
			break;
		}
	}

	header2[437] = cam->height / 256;
	header2[438] = cam->height % 256;
	header2[439] = cam->width / 256;
	header2[440] = cam->width % 256;

	cam->nb = 0;

	DBG("dev: %p, udev %p interface %p\n", cam, cam->udev, intf);

	/* set up the endpoint information  */
	iface_desc = intf->cur_altsetting;
	DBG("num endpoints %d\n", iface_desc->desc.bNumEndpoints);
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		if (!cam->read_endpoint && usb_endpoint_is_bulk_in(endpoint)) {
			/* we found the bulk in endpoint */
			cam->read_endpoint = endpoint->bEndpointAddress;
		}
	}

	if (!cam->read_endpoint) {
		err = -ENOMEM;
		dev_err(&intf->dev, "Could not find bulk-in endpoint\n");
		goto fail;
	}

	/* v4l */
	INIT_LIST_HEAD(&cam->vidq.active);
	cam->vidq.cam = cam;

	usb_set_intfdata(intf, cam);

	/* load zr364xx board specific */
	err = zr364xx_board_init(cam);
	if (!err)
		err = v4l2_ctrl_handler_setup(hdl);
	if (err)
		goto fail;

	spin_lock_init(&cam->slock);

	cam->fmt = formats;

	videobuf_queue_vmalloc_init(&cam->vb_vidq, &zr364xx_video_qops,
				    NULL, &cam->slock,
				    V4L2_BUF_TYPE_VIDEO_CAPTURE,
				    V4L2_FIELD_NONE,
				    sizeof(struct zr364xx_buffer), cam, &cam->lock);

	err = video_register_device(&cam->vdev, VFL_TYPE_GRABBER, -1);
	if (err) {
		dev_err(&udev->dev, "video_register_device failed\n");
		goto fail;
	}

	dev_info(&udev->dev, DRIVER_DESC " controlling device %s\n",
		 video_device_node_name(&cam->vdev));
	return 0;

fail:
	v4l2_ctrl_handler_free(hdl);
	v4l2_device_unregister(&cam->v4l2_dev);
	kfree(cam);
	return err;
}


static void zr364xx_disconnect(struct usb_interface *intf)
{
	struct zr364xx_camera *cam = usb_get_intfdata(intf);

	mutex_lock(&cam->lock);
	usb_set_intfdata(intf, NULL);
	dev_info(&intf->dev, DRIVER_DESC " webcam unplugged\n");
	video_unregister_device(&cam->vdev);
	v4l2_device_disconnect(&cam->v4l2_dev);

	/* stops the read pipe if it is running */
	if (cam->b_acquire)
		zr364xx_stop_acquire(cam);

	zr364xx_stop_readpipe(cam);
	mutex_unlock(&cam->lock);
	v4l2_device_put(&cam->v4l2_dev);
}


#ifdef CONFIG_PM
static int zr364xx_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct zr364xx_camera *cam = usb_get_intfdata(intf);

	cam->was_streaming = cam->b_acquire;
	if (!cam->was_streaming)
		return 0;
	zr364xx_stop_acquire(cam);
	zr364xx_stop_readpipe(cam);
	return 0;
}

static int zr364xx_resume(struct usb_interface *intf)
{
	struct zr364xx_camera *cam = usb_get_intfdata(intf);
	int res;

	if (!cam->was_streaming)
		return 0;

	zr364xx_start_readpipe(cam);
	res = zr364xx_prepare(cam);
	if (!res)
		zr364xx_start_acquire(cam);
	return res;
}
#endif

/**********************/
/* Module integration */
/**********************/

static struct usb_driver zr364xx_driver = {
	.name = "zr364xx",
	.probe = zr364xx_probe,
	.disconnect = zr364xx_disconnect,
#ifdef CONFIG_PM
	.suspend = zr364xx_suspend,
	.resume = zr364xx_resume,
	.reset_resume = zr364xx_resume,
#endif
	.id_table = device_table
};

module_usb_driver(zr364xx_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

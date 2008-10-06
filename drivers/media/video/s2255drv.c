/*
 *  s2255drv.c - a driver for the Sensoray 2255 USB video capture device
 *
 *   Copyright (C) 2007-2008 by Sensoray Company Inc.
 *                              Dean Anderson
 *
 * Some video buffer code based on vivi driver:
 *
 * Sensoray 2255 device supports 4 simultaneous channels.
 * The channels are not "crossbar" inputs, they are physically
 * attached to separate video decoders.
 *
 * Because of USB2.0 bandwidth limitations. There is only a
 * certain amount of data which may be transferred at one time.
 *
 * Example maximum bandwidth utilization:
 *
 * -full size, color mode YUYV or YUV422P: 2 channels at once
 *
 * -full or half size Grey scale: all 4 channels at once
 *
 * -half size, color mode YUYV or YUV422P: all 4 channels at once
 *
 * -full size, color mode YUYV or YUV422P 1/2 frame rate: all 4 channels
 *  at once.
 *  (TODO: Incorporate videodev2 frame rate(FR) enumeration,
 *  which is currently experimental.)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/version.h>
#include <linux/mm.h>
#include <media/videobuf-vmalloc.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <linux/vmalloc.h>
#include <linux/usb.h>

#define FIRMWARE_FILE_NAME "f2255usb.bin"



/* vendor request in */
#define S2255_VR_IN		0
/* vendor request out */
#define S2255_VR_OUT		1
/* firmware query */
#define S2255_VR_FW		0x30
/* USB endpoint number for configuring the device */
#define S2255_CONFIG_EP         2
/* maximum time for DSP to start responding after last FW word loaded(ms) */
#define S2255_DSP_BOOTTIME      400
/* maximum time to wait for firmware to load (ms) */
#define S2255_LOAD_TIMEOUT      (5000 + S2255_DSP_BOOTTIME)
#define S2255_DEF_BUFS          16
#define MAX_CHANNELS		4
#define FRAME_MARKER		0x2255DA4AL
#define MAX_PIPE_USBBLOCK	(40 * 1024)
#define DEFAULT_PIPE_USBBLOCK	(16 * 1024)
#define MAX_CHANNELS		4
#define MAX_PIPE_BUFFERS	1
#define SYS_FRAMES		4
/* maximum size is PAL full size plus room for the marker header(s) */
#define SYS_FRAMES_MAXSIZE	(720 * 288 * 2 * 2 + 4096)
#define DEF_USB_BLOCK		(4096)
#define LINE_SZ_4CIFS_NTSC	640
#define LINE_SZ_2CIFS_NTSC	640
#define LINE_SZ_1CIFS_NTSC	320
#define LINE_SZ_4CIFS_PAL	704
#define LINE_SZ_2CIFS_PAL	704
#define LINE_SZ_1CIFS_PAL	352
#define NUM_LINES_4CIFS_NTSC	240
#define NUM_LINES_2CIFS_NTSC	240
#define NUM_LINES_1CIFS_NTSC	240
#define NUM_LINES_4CIFS_PAL	288
#define NUM_LINES_2CIFS_PAL	288
#define NUM_LINES_1CIFS_PAL	288
#define LINE_SZ_DEF		640
#define NUM_LINES_DEF		240


/* predefined settings */
#define FORMAT_NTSC	1
#define FORMAT_PAL	2

#define SCALE_4CIFS	1	/* 640x480(NTSC) or 704x576(PAL) */
#define SCALE_2CIFS	2	/* 640x240(NTSC) or 704x288(PAL) */
#define SCALE_1CIFS	3	/* 320x240(NTSC) or 352x288(PAL) */

#define COLOR_YUVPL	1	/* YUV planar */
#define COLOR_YUVPK	2	/* YUV packed */
#define COLOR_Y8	4	/* monochrome */

/* frame decimation. Not implemented by V4L yet(experimental in V4L) */
#define FDEC_1		1	/* capture every frame. default */
#define FDEC_2		2	/* capture every 2nd frame */
#define FDEC_3		3	/* capture every 3rd frame */
#define FDEC_5		5	/* capture every 5th frame */

/*-------------------------------------------------------
 * Default mode parameters.
 *-------------------------------------------------------*/
#define DEF_SCALE	SCALE_4CIFS
#define DEF_COLOR	COLOR_YUVPL
#define DEF_FDEC	FDEC_1
#define DEF_BRIGHT	0
#define DEF_CONTRAST	0x5c
#define DEF_SATURATION	0x80
#define DEF_HUE		0

/* usb config commands */
#define IN_DATA_TOKEN	0x2255c0de
#define CMD_2255	0xc2255000
#define CMD_SET_MODE	(CMD_2255 | 0x10)
#define CMD_START	(CMD_2255 | 0x20)
#define CMD_STOP	(CMD_2255 | 0x30)
#define CMD_STATUS	(CMD_2255 | 0x40)

struct s2255_mode {
	u32 format;	/* input video format (NTSC, PAL) */
	u32 scale;	/* output video scale */
	u32 color;	/* output video color format */
	u32 fdec;	/* frame decimation */
	u32 bright;	/* brightness */
	u32 contrast;	/* contrast */
	u32 saturation;	/* saturation */
	u32 hue;	/* hue (NTSC only)*/
	u32 single;	/* capture 1 frame at a time (!=0), continuously (==0)*/
	u32 usb_block;	/* block size. should be 4096 of DEF_USB_BLOCK */
	u32 restart;	/* if DSP requires restart */
};

/* frame structure */
#define FRAME_STATE_UNUSED	0
#define FRAME_STATE_FILLING	1
#define FRAME_STATE_FULL	2


struct s2255_framei {
	unsigned long size;

	unsigned long ulState;	/* ulState ==0 unused, 1 being filled, 2 full */
	void *lpvbits;		/* image data */
	unsigned long cur_size;	/* current data copied to it */
};

/* image buffer structure */
struct s2255_bufferi {
	unsigned long dwFrames;			/* number of frames in buffer */
	struct s2255_framei frame[SYS_FRAMES];	/* array of FRAME structures */
};

#define DEF_MODEI_NTSC_CONT	{FORMAT_NTSC, DEF_SCALE, DEF_COLOR,	\
			DEF_FDEC, DEF_BRIGHT, DEF_CONTRAST, DEF_SATURATION, \
			DEF_HUE, 0, DEF_USB_BLOCK, 0}

struct s2255_dmaqueue {
	struct list_head	active;
	/* thread for acquisition */
	struct task_struct	*kthread;
	int			frame;
	struct s2255_dev	*dev;
	int			channel;
};

/* for firmware loading, fw_state */
#define S2255_FW_NOTLOADED	0
#define S2255_FW_LOADED_DSPWAIT	1
#define S2255_FW_SUCCESS	2
#define S2255_FW_FAILED		3
#define S2255_FW_DISCONNECTING  4

struct s2255_fw {
	int		      fw_loaded;
	int		      fw_size;
	struct urb	      *fw_urb;
	atomic_t	      fw_state;
	void		      *pfw_data;
	wait_queue_head_t     wait_fw;
	struct timer_list     dsp_wait;
	const struct firmware *fw;
};

struct s2255_pipeinfo {
	u32 max_transfer_size;
	u32 cur_transfer_size;
	u8 *transfer_buffer;
	u32 transfer_flags;;
	u32 state;
	u32 prev_state;
	u32 urb_size;
	void *stream_urb;
	void *dev;	/* back pointer to s2255_dev struct*/
	u32 err_count;
	u32 buf_index;
	u32 idx;
	u32 priority_set;
};

struct s2255_fmt; /*forward declaration */

struct s2255_dev {
	int			frames;
	int			users[MAX_CHANNELS];
	struct mutex		lock;
	struct mutex		open_lock;
	int			resources[MAX_CHANNELS];
	struct usb_device	*udev;
	struct usb_interface	*interface;
	u8			read_endpoint;

	struct s2255_dmaqueue	vidq[MAX_CHANNELS];
	struct video_device	*vdev[MAX_CHANNELS];
	struct list_head	s2255_devlist;
	struct timer_list	timer;
	struct s2255_fw	*fw_data;
	int			board_num;
	int			is_open;
	struct s2255_pipeinfo	pipes[MAX_PIPE_BUFFERS];
	struct s2255_bufferi		buffer[MAX_CHANNELS];
	struct s2255_mode	mode[MAX_CHANNELS];
	const struct s2255_fmt	*cur_fmt[MAX_CHANNELS];
	int			cur_frame[MAX_CHANNELS];
	int			last_frame[MAX_CHANNELS];
	u32			cc;	/* current channel */
	int			b_acquire[MAX_CHANNELS];
	unsigned long		req_image_size[MAX_CHANNELS];
	int			bad_payload[MAX_CHANNELS];
	unsigned long		frame_count[MAX_CHANNELS];
	int			frame_ready;
	struct kref		kref;
	spinlock_t              slock;
};
#define to_s2255_dev(d) container_of(d, struct s2255_dev, kref)

struct s2255_fmt {
	char *name;
	u32 fourcc;
	int depth;
};

/* buffer for one video frame */
struct s2255_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;
	const struct s2255_fmt *fmt;
};

struct s2255_fh {
	struct s2255_dev	*dev;
	const struct s2255_fmt	*fmt;
	unsigned int		width;
	unsigned int		height;
	struct videobuf_queue	vb_vidq;
	enum v4l2_buf_type	type;
	int			channel;
	/* mode below is the desired mode.
	   mode in s2255_dev is the current mode that was last set */
	struct s2255_mode	mode;
	int			resources[MAX_CHANNELS];
};

#define CUR_USB_FWVER	774	/* current cypress EEPROM firmware version */
#define S2255_MAJOR_VERSION	1
#define S2255_MINOR_VERSION	13
#define S2255_RELEASE		0
#define S2255_VERSION		KERNEL_VERSION(S2255_MAJOR_VERSION, \
					       S2255_MINOR_VERSION, \
					       S2255_RELEASE)

/* vendor ids */
#define USB_S2255_VENDOR_ID	0x1943
#define USB_S2255_PRODUCT_ID	0x2255
#define S2255_NORMS		(V4L2_STD_PAL | V4L2_STD_NTSC)
/* frame prefix size (sent once every frame) */
#define PREFIX_SIZE		512

/* Channels on box are in reverse order */
static unsigned long G_chnmap[MAX_CHANNELS] = {3, 2, 1, 0};

static LIST_HEAD(s2255_devlist);

static int debug;
static int *s2255_debug = &debug;

static int s2255_start_readpipe(struct s2255_dev *dev);
static void s2255_stop_readpipe(struct s2255_dev *dev);
static int s2255_start_acquire(struct s2255_dev *dev, unsigned long chn);
static int s2255_stop_acquire(struct s2255_dev *dev, unsigned long chn);
static void s2255_fillbuff(struct s2255_dev *dev, struct s2255_buffer *buf,
			   int chn);
static int s2255_set_mode(struct s2255_dev *dev, unsigned long chn,
			  struct s2255_mode *mode);
static int s2255_board_shutdown(struct s2255_dev *dev);
static void s2255_exit_v4l(struct s2255_dev *dev);
static void s2255_fwload_start(struct s2255_dev *dev);

#define dprintk(level, fmt, arg...)					\
	do {								\
		if (*s2255_debug >= (level)) {				\
			printk(KERN_DEBUG "s2255: " fmt, ##arg);	\
		}							\
	} while (0)


static struct usb_driver s2255_driver;


/* Declare static vars that will be used as parameters */
static unsigned int vid_limit = 16;	/* Video memory limit, in Mb */

/* start video number */
static int video_nr = -1;	/* /dev/videoN, -1 for autodetect */

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level(0-100) default 0");
module_param(vid_limit, int, 0644);
MODULE_PARM_DESC(vid_limit, "video memory limit(Mb)");
module_param(video_nr, int, 0644);
MODULE_PARM_DESC(video_nr, "start video minor(-1 default autodetect)");

/* USB device table */
static struct usb_device_id s2255_table[] = {
	{USB_DEVICE(USB_S2255_VENDOR_ID, USB_S2255_PRODUCT_ID)},
	{ }			/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, s2255_table);


#define BUFFER_TIMEOUT msecs_to_jiffies(400)

/* supported controls */
static struct v4l2_queryctrl s2255_qctrl[] = {
	{
	.id = V4L2_CID_BRIGHTNESS,
	.type = V4L2_CTRL_TYPE_INTEGER,
	.name = "Brightness",
	.minimum = -127,
	.maximum = 128,
	.step = 1,
	.default_value = 0,
	.flags = 0,
	}, {
	.id = V4L2_CID_CONTRAST,
	.type = V4L2_CTRL_TYPE_INTEGER,
	.name = "Contrast",
	.minimum = 0,
	.maximum = 255,
	.step = 0x1,
	.default_value = DEF_CONTRAST,
	.flags = 0,
	}, {
	.id = V4L2_CID_SATURATION,
	.type = V4L2_CTRL_TYPE_INTEGER,
	.name = "Saturation",
	.minimum = 0,
	.maximum = 255,
	.step = 0x1,
	.default_value = DEF_SATURATION,
	.flags = 0,
	}, {
	.id = V4L2_CID_HUE,
	.type = V4L2_CTRL_TYPE_INTEGER,
	.name = "Hue",
	.minimum = 0,
	.maximum = 255,
	.step = 0x1,
	.default_value = DEF_HUE,
	.flags = 0,
	}
};

static int qctl_regs[ARRAY_SIZE(s2255_qctrl)];

/* image formats.  */
static const struct s2255_fmt formats[] = {
	{
		.name = "4:2:2, planar, YUV422P",
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.depth = 16

	}, {
		.name = "4:2:2, packed, YUYV",
		.fourcc = V4L2_PIX_FMT_YUYV,
		.depth = 16

	}, {
		.name = "4:2:2, packed, UYVY",
		.fourcc = V4L2_PIX_FMT_UYVY,
		.depth = 16
	}, {
		.name = "8bpp GREY",
		.fourcc = V4L2_PIX_FMT_GREY,
		.depth = 8
	}
};

static int norm_maxw(struct video_device *vdev)
{
	return (vdev->current_norm & V4L2_STD_NTSC) ?
	    LINE_SZ_4CIFS_NTSC : LINE_SZ_4CIFS_PAL;
}

static int norm_maxh(struct video_device *vdev)
{
	return (vdev->current_norm & V4L2_STD_NTSC) ?
	    (NUM_LINES_1CIFS_NTSC * 2) : (NUM_LINES_1CIFS_PAL * 2);
}

static int norm_minw(struct video_device *vdev)
{
	return (vdev->current_norm & V4L2_STD_NTSC) ?
	    LINE_SZ_1CIFS_NTSC : LINE_SZ_1CIFS_PAL;
}

static int norm_minh(struct video_device *vdev)
{
	return (vdev->current_norm & V4L2_STD_NTSC) ?
	    (NUM_LINES_1CIFS_NTSC) : (NUM_LINES_1CIFS_PAL);
}


/*
 * TODO: fixme: move YUV reordering to hardware
 * converts 2255 planar format to yuyv or uyvy
 */
static void planar422p_to_yuv_packed(const unsigned char *in,
				     unsigned char *out,
				     int width, int height,
				     int fmt)
{
	unsigned char *pY;
	unsigned char *pCb;
	unsigned char *pCr;
	unsigned long size = height * width;
	unsigned int i;
	pY = (unsigned char *)in;
	pCr = (unsigned char *)in + height * width;
	pCb = (unsigned char *)in + height * width + (height * width / 2);
	for (i = 0; i < size * 2; i += 4) {
		out[i] = (fmt == V4L2_PIX_FMT_YUYV) ? *pY++ : *pCr++;
		out[i + 1] = (fmt == V4L2_PIX_FMT_YUYV) ? *pCr++ : *pY++;
		out[i + 2] = (fmt == V4L2_PIX_FMT_YUYV) ? *pY++ : *pCb++;
		out[i + 3] = (fmt == V4L2_PIX_FMT_YUYV) ? *pCb++ : *pY++;
	}
	return;
}


/* kickstarts the firmware loading. from probe
 */
static void s2255_timer(unsigned long user_data)
{
	struct s2255_fw *data = (struct s2255_fw *)user_data;
	dprintk(100, "s2255 timer\n");
	if (usb_submit_urb(data->fw_urb, GFP_ATOMIC) < 0) {
		printk(KERN_ERR "s2255: can't submit urb\n");
		atomic_set(&data->fw_state, S2255_FW_FAILED);
		/* wake up anything waiting for the firmware */
		wake_up(&data->wait_fw);
		return;
	}
}

/* called when DSP is up and running.  DSP is guaranteed to
   be running after S2255_DSP_BOOTTIME */
static void s2255_dsp_running(unsigned long user_data)
{
	struct s2255_fw *data = (struct s2255_fw *)user_data;
	dprintk(1, "dsp running\n");
	atomic_set(&data->fw_state, S2255_FW_SUCCESS);
	wake_up(&data->wait_fw);
	printk(KERN_INFO "s2255: firmware loaded successfully\n");
	return;
}


/* this loads the firmware asynchronously.
   Originally this was done synchroously in probe.
   But it is better to load it asynchronously here than block
   inside the probe function. Blocking inside probe affects boot time.
   FW loading is triggered by the timer in the probe function
*/
static void s2255_fwchunk_complete(struct urb *urb)
{
	struct s2255_fw *data = urb->context;
	struct usb_device *udev = urb->dev;
	int len;
	dprintk(100, "udev %p urb %p", udev, urb);
	if (urb->status) {
		dev_err(&udev->dev, "URB failed with status %d", urb->status);
		atomic_set(&data->fw_state, S2255_FW_FAILED);
		/* wake up anything waiting for the firmware */
		wake_up(&data->wait_fw);
		return;
	}
	if (data->fw_urb == NULL) {
		dev_err(&udev->dev, "s2255 disconnected\n");
		atomic_set(&data->fw_state, S2255_FW_FAILED);
		/* wake up anything waiting for the firmware */
		wake_up(&data->wait_fw);
		return;
	}
#define CHUNK_SIZE 512
	/* all USB transfers must be done with continuous kernel memory.
	   can't allocate more than 128k in current linux kernel, so
	   upload the firmware in chunks
	 */
	if (data->fw_loaded < data->fw_size) {
		len = (data->fw_loaded + CHUNK_SIZE) > data->fw_size ?
		    data->fw_size % CHUNK_SIZE : CHUNK_SIZE;

		if (len < CHUNK_SIZE)
			memset(data->pfw_data, 0, CHUNK_SIZE);

		dprintk(100, "completed len %d, loaded %d \n", len,
			data->fw_loaded);

		memcpy(data->pfw_data,
		       (char *) data->fw->data + data->fw_loaded, len);

		usb_fill_bulk_urb(data->fw_urb, udev, usb_sndbulkpipe(udev, 2),
				  data->pfw_data, CHUNK_SIZE,
				  s2255_fwchunk_complete, data);
		if (usb_submit_urb(data->fw_urb, GFP_ATOMIC) < 0) {
			dev_err(&udev->dev, "failed submit URB\n");
			atomic_set(&data->fw_state, S2255_FW_FAILED);
			/* wake up anything waiting for the firmware */
			wake_up(&data->wait_fw);
			return;
		}
		data->fw_loaded += len;
	} else {
		init_timer(&data->dsp_wait);
		data->dsp_wait.function = s2255_dsp_running;
		data->dsp_wait.data = (unsigned long)data;
		atomic_set(&data->fw_state, S2255_FW_LOADED_DSPWAIT);
		mod_timer(&data->dsp_wait, msecs_to_jiffies(S2255_DSP_BOOTTIME)
			  + jiffies);
	}
	dprintk(100, "2255 complete done\n");
	return;

}

static int s2255_got_frame(struct s2255_dev *dev, int chn)
{
	struct s2255_dmaqueue *dma_q = &dev->vidq[chn];
	struct s2255_buffer *buf;
	unsigned long flags = 0;
	int rc = 0;
	dprintk(2, "wakeup: %p channel: %d\n", &dma_q, chn);
	spin_lock_irqsave(&dev->slock, flags);

	if (list_empty(&dma_q->active)) {
		dprintk(1, "No active queue to serve\n");
		rc = -1;
		goto unlock;
	}
	buf = list_entry(dma_q->active.next,
			 struct s2255_buffer, vb.queue);

	if (!waitqueue_active(&buf->vb.done)) {
		/* no one active */
		rc = -1;
		goto unlock;
	}
	list_del(&buf->vb.queue);
	do_gettimeofday(&buf->vb.ts);
	dprintk(100, "[%p/%d] wakeup\n", buf, buf->vb.i);

	s2255_fillbuff(dev, buf, dma_q->channel);
	wake_up(&buf->vb.done);
	dprintk(2, "wakeup [buf/i] [%p/%d]\n", buf, buf->vb.i);
unlock:
	spin_unlock_irqrestore(&dev->slock, flags);
	return 0;
}


static const struct s2255_fmt *format_by_fourcc(int fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (-1 == formats[i].fourcc)
			continue;
		if (formats[i].fourcc == fourcc)
			return formats + i;
	}
	return NULL;
}




/* video buffer vmalloc implementation based partly on VIVI driver which is
 *          Copyright (c) 2006 by
 *                  Mauro Carvalho Chehab <mchehab--a.t--infradead.org>
 *                  Ted Walther <ted--a.t--enumera.com>
 *                  John Sokol <sokol--a.t--videotechnology.com>
 *                  http://v4l.videotechnology.com/
 *
 */
static void s2255_fillbuff(struct s2255_dev *dev, struct s2255_buffer *buf,
			   int chn)
{
	int pos = 0;
	struct timeval ts;
	const char *tmpbuf;
	char *vbuf = videobuf_to_vmalloc(&buf->vb);
	unsigned long last_frame;
	struct s2255_framei *frm;

	if (!vbuf)
		return;

	last_frame = dev->last_frame[chn];
	if (last_frame != -1) {
		frm = &dev->buffer[chn].frame[last_frame];
		tmpbuf =
		    (const char *)dev->buffer[chn].frame[last_frame].lpvbits;
		switch (buf->fmt->fourcc) {
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_UYVY:
			planar422p_to_yuv_packed((const unsigned char *)tmpbuf,
						 vbuf, buf->vb.width,
						 buf->vb.height,
						 buf->fmt->fourcc);
			break;
		case V4L2_PIX_FMT_GREY:
			memcpy(vbuf, tmpbuf, buf->vb.width * buf->vb.height);
			break;
		case V4L2_PIX_FMT_YUV422P:
			memcpy(vbuf, tmpbuf,
			       buf->vb.width * buf->vb.height * 2);
			break;
		default:
			printk(KERN_DEBUG "s2255: unknown format?\n");
		}
		dev->last_frame[chn] = -1;
		/* done with the frame, free it */
		frm->ulState = 0;
		dprintk(4, "freeing buffer\n");
	} else {
		printk(KERN_ERR "s2255: =======no frame\n");
		return;

	}
	dprintk(2, "s2255fill at : Buffer 0x%08lx size= %d\n",
		(unsigned long)vbuf, pos);
	/* tell v4l buffer was filled */

	buf->vb.field_count = dev->frame_count[chn] * 2;
	do_gettimeofday(&ts);
	buf->vb.ts = ts;
	buf->vb.state = VIDEOBUF_DONE;
}


/* ------------------------------------------------------------------
   Videobuf operations
   ------------------------------------------------------------------*/

static int buffer_setup(struct videobuf_queue *vq, unsigned int *count,
			unsigned int *size)
{
	struct s2255_fh *fh = vq->priv_data;

	*size = fh->width * fh->height * (fh->fmt->depth >> 3);

	if (0 == *count)
		*count = S2255_DEF_BUFS;

	while (*size * (*count) > vid_limit * 1024 * 1024)
		(*count)--;

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct s2255_buffer *buf)
{
	dprintk(4, "%s\n", __func__);

	videobuf_waiton(&buf->vb, 0, 0);
	videobuf_vmalloc_free(&buf->vb);
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static int buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
			  enum v4l2_field field)
{
	struct s2255_fh *fh = vq->priv_data;
	struct s2255_buffer *buf = container_of(vb, struct s2255_buffer, vb);
	int rc;
	dprintk(4, "%s, field=%d\n", __func__, field);
	if (fh->fmt == NULL)
		return -EINVAL;

	if ((fh->width < norm_minw(fh->dev->vdev[fh->channel])) ||
	    (fh->width > norm_maxw(fh->dev->vdev[fh->channel])) ||
	    (fh->height < norm_minh(fh->dev->vdev[fh->channel])) ||
	    (fh->height > norm_maxh(fh->dev->vdev[fh->channel]))) {
		dprintk(4, "invalid buffer prepare\n");
		return -EINVAL;
	}

	buf->vb.size = fh->width * fh->height * (fh->fmt->depth >> 3);

	if (0 != buf->vb.baddr && buf->vb.bsize < buf->vb.size) {
		dprintk(4, "invalid buffer prepare\n");
		return -EINVAL;
	}

	buf->fmt = fh->fmt;
	buf->vb.width = fh->width;
	buf->vb.height = fh->height;
	buf->vb.field = field;


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

static void buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct s2255_buffer *buf = container_of(vb, struct s2255_buffer, vb);
	struct s2255_fh *fh = vq->priv_data;
	struct s2255_dev *dev = fh->dev;
	struct s2255_dmaqueue *vidq = &dev->vidq[fh->channel];

	dprintk(1, "%s\n", __func__);

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct s2255_buffer *buf = container_of(vb, struct s2255_buffer, vb);
	struct s2255_fh *fh = vq->priv_data;
	dprintk(4, "%s %d\n", __func__, fh->channel);
	free_buffer(vq, buf);
}

static struct videobuf_queue_ops s2255_video_qops = {
	.buf_setup = buffer_setup,
	.buf_prepare = buffer_prepare,
	.buf_queue = buffer_queue,
	.buf_release = buffer_release,
};


static int res_get(struct s2255_dev *dev, struct s2255_fh *fh)
{
	/* is it free? */
	mutex_lock(&dev->lock);
	if (dev->resources[fh->channel]) {
		/* no, someone else uses it */
		mutex_unlock(&dev->lock);
		return 0;
	}
	/* it's free, grab it */
	dev->resources[fh->channel] = 1;
	fh->resources[fh->channel] = 1;
	dprintk(1, "s2255: res: get\n");
	mutex_unlock(&dev->lock);
	return 1;
}

static int res_locked(struct s2255_dev *dev, struct s2255_fh *fh)
{
	return dev->resources[fh->channel];
}

static int res_check(struct s2255_fh *fh)
{
	return fh->resources[fh->channel];
}


static void res_free(struct s2255_dev *dev, struct s2255_fh *fh)
{
	mutex_lock(&dev->lock);
	dev->resources[fh->channel] = 0;
	fh->resources[fh->channel] = 0;
	mutex_unlock(&dev->lock);
	dprintk(1, "res: put\n");
}


static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct s2255_fh *fh = file->private_data;
	struct s2255_dev *dev = fh->dev;
	strlcpy(cap->driver, "s2255", sizeof(cap->driver));
	strlcpy(cap->card, "s2255", sizeof(cap->card));
	strlcpy(cap->bus_info, dev_name(&dev->udev->dev),
		sizeof(cap->bus_info));
	cap->version = S2255_VERSION;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
			       struct v4l2_fmtdesc *f)
{
	int index = 0;
	if (f)
		index = f->index;

	if (index >= ARRAY_SIZE(formats))
		return -EINVAL;

	dprintk(4, "name %s\n", formats[index].name);
	strlcpy(f->description, formats[index].name, sizeof(f->description));
	f->pixelformat = formats[index].fourcc;
	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct s2255_fh *fh = priv;

	f->fmt.pix.width = fh->width;
	f->fmt.pix.height = fh->height;
	f->fmt.pix.field = fh->vb_vidq.field;
	f->fmt.pix.pixelformat = fh->fmt->fourcc;
	f->fmt.pix.bytesperline = f->fmt.pix.width * (fh->fmt->depth >> 3);
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	const struct s2255_fmt *fmt;
	enum v4l2_field field;
	int  b_any_field = 0;
	struct s2255_fh *fh = priv;
	struct s2255_dev *dev = fh->dev;
	int is_ntsc;

	is_ntsc =
	    (dev->vdev[fh->channel]->current_norm & V4L2_STD_NTSC) ? 1 : 0;

	fmt = format_by_fourcc(f->fmt.pix.pixelformat);

	if (fmt == NULL)
		return -EINVAL;

	field = f->fmt.pix.field;
	if (field == V4L2_FIELD_ANY)
		b_any_field = 1;

	dprintk(4, "try format %d \n", is_ntsc);
	/* supports 3 sizes. see s2255drv.h */
	dprintk(50, "width test %d, height %d\n",
		f->fmt.pix.width, f->fmt.pix.height);
	if (is_ntsc) {
		/* NTSC */
		if (f->fmt.pix.height >= NUM_LINES_1CIFS_NTSC * 2) {
			f->fmt.pix.height = NUM_LINES_1CIFS_NTSC * 2;
			if (b_any_field) {
				field = V4L2_FIELD_SEQ_TB;
			} else if (!((field == V4L2_FIELD_INTERLACED) ||
				      (field == V4L2_FIELD_SEQ_TB) ||
				      (field == V4L2_FIELD_INTERLACED_TB))) {
				dprintk(1, "unsupported field setting\n");
				return -EINVAL;
			}
		} else {
			f->fmt.pix.height = NUM_LINES_1CIFS_NTSC;
			if (b_any_field) {
				field = V4L2_FIELD_TOP;
			} else if (!((field == V4L2_FIELD_TOP) ||
				      (field == V4L2_FIELD_BOTTOM))) {
				dprintk(1, "unsupported field setting\n");
				return -EINVAL;
			}

		}
		if (f->fmt.pix.width >= LINE_SZ_4CIFS_NTSC)
			f->fmt.pix.width = LINE_SZ_4CIFS_NTSC;
		else if (f->fmt.pix.width >= LINE_SZ_2CIFS_NTSC)
			f->fmt.pix.width = LINE_SZ_2CIFS_NTSC;
		else if (f->fmt.pix.width >= LINE_SZ_1CIFS_NTSC)
			f->fmt.pix.width = LINE_SZ_1CIFS_NTSC;
		else
			f->fmt.pix.width = LINE_SZ_1CIFS_NTSC;
	} else {
		/* PAL */
		if (f->fmt.pix.height >= NUM_LINES_1CIFS_PAL * 2) {
			f->fmt.pix.height = NUM_LINES_1CIFS_PAL * 2;
			if (b_any_field) {
				field = V4L2_FIELD_SEQ_TB;
			} else if (!((field == V4L2_FIELD_INTERLACED) ||
				      (field == V4L2_FIELD_SEQ_TB) ||
				      (field == V4L2_FIELD_INTERLACED_TB))) {
				dprintk(1, "unsupported field setting\n");
				return -EINVAL;
			}
		} else {
			f->fmt.pix.height = NUM_LINES_1CIFS_PAL;
			if (b_any_field) {
				field = V4L2_FIELD_TOP;
			} else if (!((field == V4L2_FIELD_TOP) ||
				     (field == V4L2_FIELD_BOTTOM))) {
				dprintk(1, "unsupported field setting\n");
				return -EINVAL;
			}
		}
		if (f->fmt.pix.width >= LINE_SZ_4CIFS_PAL) {
			dprintk(50, "pal 704\n");
			f->fmt.pix.width = LINE_SZ_4CIFS_PAL;
			field = V4L2_FIELD_SEQ_TB;
		} else if (f->fmt.pix.width >= LINE_SZ_2CIFS_PAL) {
			dprintk(50, "pal 352A\n");
			f->fmt.pix.width = LINE_SZ_2CIFS_PAL;
			field = V4L2_FIELD_TOP;
		} else if (f->fmt.pix.width >= LINE_SZ_1CIFS_PAL) {
			dprintk(50, "pal 352B\n");
			f->fmt.pix.width = LINE_SZ_1CIFS_PAL;
			field = V4L2_FIELD_TOP;
		} else {
			dprintk(50, "pal 352C\n");
			f->fmt.pix.width = LINE_SZ_1CIFS_PAL;
			field = V4L2_FIELD_TOP;
		}
	}

	dprintk(50, "width %d height %d field %d \n", f->fmt.pix.width,
		f->fmt.pix.height, f->fmt.pix.field);
	f->fmt.pix.field = field;
	f->fmt.pix.bytesperline = (f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct s2255_fh *fh = priv;
	const struct s2255_fmt *fmt;
	struct videobuf_queue *q = &fh->vb_vidq;
	int ret;
	int norm;

	ret = vidioc_try_fmt_vid_cap(file, fh, f);

	if (ret < 0)
		return ret;

	fmt = format_by_fourcc(f->fmt.pix.pixelformat);

	if (fmt == NULL)
		return -EINVAL;

	mutex_lock(&q->vb_lock);

	if (videobuf_queue_is_busy(&fh->vb_vidq)) {
		dprintk(1, "queue busy\n");
		ret = -EBUSY;
		goto out_s_fmt;
	}

	if (res_locked(fh->dev, fh)) {
		dprintk(1, "can't change format after started\n");
		ret = -EBUSY;
		goto out_s_fmt;
	}

	fh->fmt = fmt;
	fh->width = f->fmt.pix.width;
	fh->height = f->fmt.pix.height;
	fh->vb_vidq.field = f->fmt.pix.field;
	fh->type = f->type;
	norm = norm_minw(fh->dev->vdev[fh->channel]);
	if (fh->width > norm_minw(fh->dev->vdev[fh->channel])) {
		if (fh->height > norm_minh(fh->dev->vdev[fh->channel]))
			fh->mode.scale = SCALE_4CIFS;
		else
			fh->mode.scale = SCALE_2CIFS;

	} else {
		fh->mode.scale = SCALE_1CIFS;
	}

	/* color mode */
	switch (fh->fmt->fourcc) {
	case V4L2_PIX_FMT_GREY:
		fh->mode.color = COLOR_Y8;
		break;
	case V4L2_PIX_FMT_YUV422P:
		fh->mode.color = COLOR_YUVPL;
		break;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	default:
		fh->mode.color = COLOR_YUVPK;
		break;
	}
	ret = 0;
out_s_fmt:
	mutex_unlock(&q->vb_lock);
	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	int rc;
	struct s2255_fh *fh = priv;
	rc = videobuf_reqbufs(&fh->vb_vidq, p);
	return rc;
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	int rc;
	struct s2255_fh *fh = priv;
	rc = videobuf_querybuf(&fh->vb_vidq, p);
	return rc;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	int rc;
	struct s2255_fh *fh = priv;
	rc = videobuf_qbuf(&fh->vb_vidq, p);
	return rc;
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	int rc;
	struct s2255_fh *fh = priv;
	rc = videobuf_dqbuf(&fh->vb_vidq, p, file->f_flags & O_NONBLOCK);
	return rc;
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidioc_cgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct s2255_fh *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

/* write to the configuration pipe, synchronously */
static int s2255_write_config(struct usb_device *udev, unsigned char *pbuf,
			      int size)
{
	int pipe;
	int done;
	long retval = -1;
	if (udev) {
		pipe = usb_sndbulkpipe(udev, S2255_CONFIG_EP);
		retval = usb_bulk_msg(udev, pipe, pbuf, size, &done, 500);
	}
	return retval;
}

static u32 get_transfer_size(struct s2255_mode *mode)
{
	int linesPerFrame = LINE_SZ_DEF;
	int pixelsPerLine = NUM_LINES_DEF;
	u32 outImageSize;
	u32 usbInSize;
	unsigned int mask_mult;

	if (mode == NULL)
		return 0;

	if (mode->format == FORMAT_NTSC) {
		switch (mode->scale) {
		case SCALE_4CIFS:
			linesPerFrame = NUM_LINES_4CIFS_NTSC * 2;
			pixelsPerLine = LINE_SZ_4CIFS_NTSC;
			break;
		case SCALE_2CIFS:
			linesPerFrame = NUM_LINES_2CIFS_NTSC;
			pixelsPerLine = LINE_SZ_2CIFS_NTSC;
			break;
		case SCALE_1CIFS:
			linesPerFrame = NUM_LINES_1CIFS_NTSC;
			pixelsPerLine = LINE_SZ_1CIFS_NTSC;
			break;
		default:
			break;
		}
	} else if (mode->format == FORMAT_PAL) {
		switch (mode->scale) {
		case SCALE_4CIFS:
			linesPerFrame = NUM_LINES_4CIFS_PAL * 2;
			pixelsPerLine = LINE_SZ_4CIFS_PAL;
			break;
		case SCALE_2CIFS:
			linesPerFrame = NUM_LINES_2CIFS_PAL;
			pixelsPerLine = LINE_SZ_2CIFS_PAL;
			break;
		case SCALE_1CIFS:
			linesPerFrame = NUM_LINES_1CIFS_PAL;
			pixelsPerLine = LINE_SZ_1CIFS_PAL;
			break;
		default:
			break;
		}
	}
	outImageSize = linesPerFrame * pixelsPerLine;
	if (mode->color != COLOR_Y8) {
		/* 2 bytes/pixel if not monochrome */
		outImageSize *= 2;
	}

	/* total bytes to send including prefix and 4K padding;
	   must be a multiple of USB_READ_SIZE */
	usbInSize = outImageSize + PREFIX_SIZE;	/* always send prefix */
	mask_mult = 0xffffffffUL - DEF_USB_BLOCK + 1;
	/* if size not a multiple of USB_READ_SIZE */
	if (usbInSize & ~mask_mult)
		usbInSize = (usbInSize & mask_mult) + (DEF_USB_BLOCK);
	return usbInSize;
}

static void dump_verify_mode(struct s2255_dev *sdev, struct s2255_mode *mode)
{
	struct device *dev = &sdev->udev->dev;
	dev_info(dev, "------------------------------------------------\n");
	dev_info(dev, "verify mode\n");
	dev_info(dev, "format: %d\n", mode->format);
	dev_info(dev, "scale: %d\n", mode->scale);
	dev_info(dev, "fdec: %d\n", mode->fdec);
	dev_info(dev, "color: %d\n", mode->color);
	dev_info(dev, "bright: 0x%x\n", mode->bright);
	dev_info(dev, "restart: 0x%x\n", mode->restart);
	dev_info(dev, "usb_block: 0x%x\n", mode->usb_block);
	dev_info(dev, "single: 0x%x\n", mode->single);
	dev_info(dev, "------------------------------------------------\n");
}

/*
 * set mode is the function which controls the DSP.
 * the restart parameter in struct s2255_mode should be set whenever
 * the image size could change via color format, video system or image
 * size.
 * When the restart parameter is set, we sleep for ONE frame to allow the
 * DSP time to get the new frame
 */
static int s2255_set_mode(struct s2255_dev *dev, unsigned long chn,
			  struct s2255_mode *mode)
{
	int res;
	u32 *buffer;
	unsigned long chn_rev;

	chn_rev = G_chnmap[chn];
	dprintk(3, "mode scale [%ld] %p %d\n", chn, mode, mode->scale);
	dprintk(3, "mode scale [%ld] %p %d\n", chn, &dev->mode[chn],
		dev->mode[chn].scale);
	dprintk(2, "mode contrast %x\n", mode->contrast);

	/* save the mode */
	dev->mode[chn] = *mode;
	dev->req_image_size[chn] = get_transfer_size(mode);
	dprintk(1, "transfer size %ld\n", dev->req_image_size[chn]);

	buffer = kzalloc(512, GFP_KERNEL);
	if (buffer == NULL) {
		dev_err(&dev->udev->dev, "out of mem\n");
		return -ENOMEM;
	}

	/* set the mode */
	buffer[0] = IN_DATA_TOKEN;
	buffer[1] = (u32) chn_rev;
	buffer[2] = CMD_SET_MODE;
	memcpy(&buffer[3], &dev->mode[chn], sizeof(struct s2255_mode));
	res = s2255_write_config(dev->udev, (unsigned char *)buffer, 512);
	if (debug)
		dump_verify_mode(dev, mode);
	kfree(buffer);
	dprintk(1, "set mode done chn %lu, %d\n", chn, res);

	/* wait at least 3 frames before continuing */
	if (mode->restart)
		msleep(125);

	/* clear the restart flag */
	dev->mode[chn].restart = 0;

	return res;
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	int res;
	struct s2255_fh *fh = priv;
	struct s2255_dev *dev = fh->dev;
	struct s2255_mode *new_mode;
	struct s2255_mode *old_mode;
	int chn;
	int j;
	dprintk(4, "%s\n", __func__);
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		dev_err(&dev->udev->dev, "invalid fh type0\n");
		return -EINVAL;
	}
	if (i != fh->type) {
		dev_err(&dev->udev->dev, "invalid fh type1\n");
		return -EINVAL;
	}

	if (!res_get(dev, fh)) {
		dev_err(&dev->udev->dev, "s2255: stream busy\n");
		return -EBUSY;
	}

	/* send a set mode command everytime with restart.
	   in case we switch resolutions or other parameters */
	chn = fh->channel;
	new_mode = &fh->mode;
	old_mode = &fh->dev->mode[chn];

	if (new_mode->color != old_mode->color)
		new_mode->restart = 1;
	else if (new_mode->scale != old_mode->scale)
		new_mode->restart = 1;
	else if (new_mode->format != old_mode->format)
		new_mode->restart = 1;

	s2255_set_mode(dev, chn, new_mode);
	new_mode->restart = 0;
	*old_mode = *new_mode;
	dev->cur_fmt[chn] = fh->fmt;
	dprintk(1, "%s[%d]\n", __func__, chn);
	dev->last_frame[chn] = -1;
	dev->bad_payload[chn] = 0;
	dev->cur_frame[chn] = 0;
	dev->frame_count[chn] = 0;
	for (j = 0; j < SYS_FRAMES; j++) {
		dev->buffer[chn].frame[j].ulState = 0;
		dev->buffer[chn].frame[j].cur_size = 0;
	}
	res = videobuf_streamon(&fh->vb_vidq);
	if (res == 0) {
		s2255_start_acquire(dev, chn);
		dev->b_acquire[chn] = 1;
	} else {
		res_free(dev, fh);
	}
	return res;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	int res;
	struct s2255_fh *fh = priv;
	struct s2255_dev *dev = fh->dev;

	dprintk(4, "%s\n, channel: %d", __func__, fh->channel);
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		printk(KERN_ERR "invalid fh type0\n");
		return -EINVAL;
	}
	if (i != fh->type) {
		printk(KERN_ERR "invalid type i\n");
		return -EINVAL;
	}
	s2255_stop_acquire(dev, fh->channel);
	res = videobuf_streamoff(&fh->vb_vidq);
	if (res < 0)
		return res;
	res_free(dev, fh);
	return 0;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id *i)
{
	struct s2255_fh *fh = priv;
	struct s2255_mode *mode;
	struct videobuf_queue *q = &fh->vb_vidq;
	int ret = 0;

	mutex_lock(&q->vb_lock);
	if (videobuf_queue_is_busy(q)) {
		dprintk(1, "queue busy\n");
		ret = -EBUSY;
		goto out_s_std;
	}

	if (res_locked(fh->dev, fh)) {
		dprintk(1, "can't change standard after started\n");
		ret = -EBUSY;
		goto out_s_std;
	}
	mode = &fh->mode;

	if (*i & V4L2_STD_NTSC) {
		dprintk(4, "vidioc_s_std NTSC\n");
		mode->format = FORMAT_NTSC;
	} else if (*i & V4L2_STD_PAL) {
		dprintk(4, "vidioc_s_std PAL\n");
		mode->format = FORMAT_PAL;
	} else {
		ret = -EINVAL;
	}
out_s_std:
	mutex_unlock(&q->vb_lock);
	return ret;
}

/* Sensoray 2255 is a multiple channel capture device.
   It does not have a "crossbar" of inputs.
   We use one V4L device per channel. The user must
   be aware that certain combinations are not allowed.
   For instance, you cannot do full FPS on more than 2 channels(2 videodevs)
   at once in color(you can do full fps on 4 channels with greyscale.
*/
static int vidioc_enum_input(struct file *file, void *priv,
			     struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = S2255_NORMS;
	strlcpy(inp->name, "Camera", sizeof(inp->name));
	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}
static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i > 0)
		return -EINVAL;
	return 0;
}

/* --- controls ---------------------------------------------- */
static int vidioc_queryctrl(struct file *file, void *priv,
			    struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(s2255_qctrl); i++)
		if (qc->id && qc->id == s2255_qctrl[i].id) {
			memcpy(qc, &(s2255_qctrl[i]), sizeof(*qc));
			return 0;
		}

	dprintk(4, "query_ctrl -EINVAL %d\n", qc->id);
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(s2255_qctrl); i++)
		if (ctrl->id == s2255_qctrl[i].id) {
			ctrl->value = qctl_regs[i];
			return 0;
		}
	dprintk(4, "g_ctrl -EINVAL\n");

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	int i;
	struct s2255_fh *fh = priv;
	struct s2255_dev *dev = fh->dev;
	struct s2255_mode *mode;
	mode = &fh->mode;
	dprintk(4, "vidioc_s_ctrl\n");
	for (i = 0; i < ARRAY_SIZE(s2255_qctrl); i++) {
		if (ctrl->id == s2255_qctrl[i].id) {
			if (ctrl->value < s2255_qctrl[i].minimum ||
			    ctrl->value > s2255_qctrl[i].maximum)
				return -ERANGE;

			qctl_regs[i] = ctrl->value;
			/* update the mode to the corresponding value */
			switch (ctrl->id) {
			case V4L2_CID_BRIGHTNESS:
				mode->bright = ctrl->value;
				break;
			case V4L2_CID_CONTRAST:
				mode->contrast = ctrl->value;
				break;
			case V4L2_CID_HUE:
				mode->hue = ctrl->value;
				break;
			case V4L2_CID_SATURATION:
				mode->saturation = ctrl->value;
				break;
			}
			mode->restart = 0;
			/* set mode here.  Note: stream does not need restarted.
			   some V4L programs restart stream unnecessarily
			   after a s_crtl.
			 */
			s2255_set_mode(dev, fh->channel, mode);
			return 0;
		}
	}
	return -EINVAL;
}

static int s2255_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct s2255_dev *h, *dev = NULL;
	struct s2255_fh *fh;
	struct list_head *list;
	enum v4l2_buf_type type = 0;
	int i = 0;
	int cur_channel = -1;
	dprintk(1, "s2255: open called (minor=%d)\n", minor);

	list_for_each(list, &s2255_devlist) {
		h = list_entry(list, struct s2255_dev, s2255_devlist);
		for (i = 0; i < MAX_CHANNELS; i++) {
			if (h->vdev[i]->minor == minor) {
				cur_channel = i;
				dev = h;
				type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			}
		}
	}

	if ((NULL == dev) || (cur_channel == -1)) {
		dprintk(1, "s2255: openv4l no dev\n");
		return -ENODEV;
	}

	mutex_lock(&dev->open_lock);

	dev->users[cur_channel]++;
	dprintk(4, "s2255: open_handles %d\n", dev->users[cur_channel]);

	if (atomic_read(&dev->fw_data->fw_state) == S2255_FW_FAILED) {
		err("2255 firmware load failed. retrying.\n");
		s2255_fwload_start(dev);
		wait_event_timeout(dev->fw_data->wait_fw,
				   (atomic_read(&dev->fw_data->fw_state)
				    != S2255_FW_NOTLOADED),
				   msecs_to_jiffies(S2255_LOAD_TIMEOUT));
		if (atomic_read(&dev->fw_data->fw_state)
		    != S2255_FW_SUCCESS) {
			printk(KERN_INFO "2255 FW load failed.\n");
			dev->users[cur_channel]--;
			mutex_unlock(&dev->open_lock);
			return -EFAULT;
		}
	} else if (atomic_read(&dev->fw_data->fw_state) == S2255_FW_NOTLOADED) {
		/* give S2255_LOAD_TIMEOUT time for firmware to load in case
		   driver loaded and then device immediately opened */
		printk(KERN_INFO "%s waiting for firmware load\n", __func__);
		wait_event_timeout(dev->fw_data->wait_fw,
				   (atomic_read(&dev->fw_data->fw_state)
				   != S2255_FW_NOTLOADED),
				   msecs_to_jiffies(S2255_LOAD_TIMEOUT));
		if (atomic_read(&dev->fw_data->fw_state)
		    != S2255_FW_SUCCESS) {
			printk(KERN_INFO "2255 firmware not loaded"
			       "try again\n");
			dev->users[cur_channel]--;
			mutex_unlock(&dev->open_lock);
			return -EBUSY;
		}
	}

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh) {
		dev->users[cur_channel]--;
		mutex_unlock(&dev->open_lock);
		return -ENOMEM;
	}

	file->private_data = fh;
	fh->dev = dev;
	fh->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fh->mode = dev->mode[cur_channel];
	fh->fmt = dev->cur_fmt[cur_channel];
	/* default 4CIF NTSC */
	fh->width = LINE_SZ_4CIFS_NTSC;
	fh->height = NUM_LINES_4CIFS_NTSC * 2;
	fh->channel = cur_channel;

	/* Put all controls at a sane state */
	for (i = 0; i < ARRAY_SIZE(s2255_qctrl); i++)
		qctl_regs[i] = s2255_qctrl[i].default_value;

	dprintk(1, "s2255drv: open minor=%d type=%s users=%d\n",
		minor, v4l2_type_names[type], dev->users[cur_channel]);
	dprintk(2, "s2255drv: open: fh=0x%08lx, dev=0x%08lx, vidq=0x%08lx\n",
		(unsigned long)fh, (unsigned long)dev,
		(unsigned long)&dev->vidq[cur_channel]);
	dprintk(4, "s2255drv: open: list_empty active=%d\n",
		list_empty(&dev->vidq[cur_channel].active));

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &s2255_video_qops,
				    NULL, &dev->slock,
				    fh->type,
				    V4L2_FIELD_INTERLACED,
				    sizeof(struct s2255_buffer), fh);

	kref_get(&dev->kref);
	mutex_unlock(&dev->open_lock);
	return 0;
}


static unsigned int s2255_poll(struct file *file,
			       struct poll_table_struct *wait)
{
	struct s2255_fh *fh = file->private_data;
	int rc;
	dprintk(100, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	rc = videobuf_poll_stream(file, &fh->vb_vidq, wait);
	return rc;
}

static void s2255_destroy(struct kref *kref)
{
	struct s2255_dev *dev = to_s2255_dev(kref);
	if (!dev) {
		printk(KERN_ERR "s2255drv: kref problem\n");
		return;
	}

	/*
	 * Wake up any firmware load waiting (only done in .open,
	 * which holds the open_lock mutex)
	 */
	atomic_set(&dev->fw_data->fw_state, S2255_FW_DISCONNECTING);
	wake_up(&dev->fw_data->wait_fw);

	/* prevent s2255_disconnect from racing s2255_open */
	mutex_lock(&dev->open_lock);
	s2255_exit_v4l(dev);
	/*
	 * device unregistered so no longer possible to open. open_mutex
	 *  can be unlocked and timers deleted afterwards.
	 */
	mutex_unlock(&dev->open_lock);

	/* board shutdown stops the read pipe if it is running */
	s2255_board_shutdown(dev);

	/* make sure firmware still not trying to load */
	del_timer(&dev->timer);  /* only started in .probe and .open */

	if (dev->fw_data->fw_urb) {
		dprintk(2, "kill fw_urb\n");
		usb_kill_urb(dev->fw_data->fw_urb);
		usb_free_urb(dev->fw_data->fw_urb);
		dev->fw_data->fw_urb = NULL;
	}

	/*
	 * delete the dsp_wait timer, which sets the firmware
	 * state on completion.  This is done before fw_data
	 * is freed below.
	 */

	del_timer(&dev->fw_data->dsp_wait); /* only started in .open */

	if (dev->fw_data->fw)
		release_firmware(dev->fw_data->fw);
	kfree(dev->fw_data->pfw_data);
	kfree(dev->fw_data);

	usb_put_dev(dev->udev);
	dprintk(1, "%s", __func__);
	kfree(dev);
}

static int s2255_close(struct inode *inode, struct file *file)
{
	struct s2255_fh *fh = file->private_data;
	struct s2255_dev *dev = fh->dev;
	int minor = iminor(inode);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->open_lock);

	/* turn off stream */
	if (res_check(fh)) {
		if (dev->b_acquire[fh->channel])
			s2255_stop_acquire(dev, fh->channel);
		videobuf_streamoff(&fh->vb_vidq);
		res_free(dev, fh);
	}

	videobuf_mmap_free(&fh->vb_vidq);
	dev->users[fh->channel]--;

	mutex_unlock(&dev->open_lock);

	kref_put(&dev->kref, s2255_destroy);
	dprintk(1, "s2255: close called (minor=%d, users=%d)\n",
		minor, dev->users[fh->channel]);
	kfree(fh);
	return 0;
}

static int s2255_mmap_v4l(struct file *file, struct vm_area_struct *vma)
{
	struct s2255_fh *fh = file->private_data;
	int ret;

	if (!fh)
		return -ENODEV;
	dprintk(4, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(4, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end - (unsigned long)vma->vm_start, ret);

	return ret;
}

static const struct file_operations s2255_fops_v4l = {
	.owner = THIS_MODULE,
	.open = s2255_open,
	.release = s2255_close,
	.poll = s2255_poll,
	.ioctl = video_ioctl2,	/* V4L2 ioctl handler */
	.compat_ioctl = v4l_compat_ioctl32,
	.mmap = s2255_mmap_v4l,
	.llseek = no_llseek,
};

static const struct v4l2_ioctl_ops s2255_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs = vidioc_reqbufs,
	.vidioc_querybuf = vidioc_querybuf,
	.vidioc_qbuf = vidioc_qbuf,
	.vidioc_dqbuf = vidioc_dqbuf,
	.vidioc_s_std = vidioc_s_std,
	.vidioc_enum_input = vidioc_enum_input,
	.vidioc_g_input = vidioc_g_input,
	.vidioc_s_input = vidioc_s_input,
	.vidioc_queryctrl = vidioc_queryctrl,
	.vidioc_g_ctrl = vidioc_g_ctrl,
	.vidioc_s_ctrl = vidioc_s_ctrl,
	.vidioc_streamon = vidioc_streamon,
	.vidioc_streamoff = vidioc_streamoff,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf = vidioc_cgmbuf,
#endif
};

static struct video_device template = {
	.name = "s2255v",
	.fops = &s2255_fops_v4l,
	.ioctl_ops = &s2255_ioctl_ops,
	.minor = -1,
	.release = video_device_release,
	.tvnorms = S2255_NORMS,
	.current_norm = V4L2_STD_NTSC_M,
};

static int s2255_probe_v4l(struct s2255_dev *dev)
{
	int ret;
	int i;
	int cur_nr = video_nr;

	/* initialize all video 4 linux */
	list_add_tail(&dev->s2255_devlist, &s2255_devlist);
	/* register 4 video devices */
	for (i = 0; i < MAX_CHANNELS; i++) {
		INIT_LIST_HEAD(&dev->vidq[i].active);
		dev->vidq[i].dev = dev;
		dev->vidq[i].channel = i;
		dev->vidq[i].kthread = NULL;
		/* register 4 video devices */
		dev->vdev[i] = video_device_alloc();
		memcpy(dev->vdev[i], &template, sizeof(struct video_device));
		dev->vdev[i]->parent = &dev->interface->dev;
		if (video_nr == -1)
			ret = video_register_device(dev->vdev[i],
						    VFL_TYPE_GRABBER,
						    video_nr);
		else
			ret = video_register_device(dev->vdev[i],
						    VFL_TYPE_GRABBER,
						    cur_nr + i);
		dev->vdev[i]->priv = dev;

		if (ret != 0) {
			dev_err(&dev->udev->dev,
				"failed to register video device!\n");
			return ret;
		}
	}
	printk(KERN_INFO "Sensoray 2255 V4L driver\n");
	return ret;
}

static void s2255_exit_v4l(struct s2255_dev *dev)
{
	struct list_head *list;
	int i;
	/* unregister the video devices */
	while (!list_empty(&s2255_devlist)) {
		list = s2255_devlist.next;
		list_del(list);
	}
	for (i = 0; i < MAX_CHANNELS; i++) {
		if (-1 != dev->vdev[i]->minor)
			video_unregister_device(dev->vdev[i]);
		else
			video_device_release(dev->vdev[i]);
	}
}

/* this function moves the usb stream read pipe data
 * into the system buffers.
 * returns 0 on success, EAGAIN if more data to process( call this
 * function again).
 *
 * Received frame structure:
 * bytes 0-3:  marker : 0x2255DA4AL (FRAME_MARKER)
 * bytes 4-7:  channel: 0-3
 * bytes 8-11: payload size:  size of the frame
 * bytes 12-payloadsize+12:  frame data
 */
static int save_frame(struct s2255_dev *dev, struct s2255_pipeinfo *pipe_info)
{
	static int dbgsync; /* = 0; */
	char *pdest;
	u32 offset = 0;
	int bsync = 0;
	int btrunc = 0;
	char *psrc;
	unsigned long copy_size;
	unsigned long size;
	s32 idx = -1;
	struct s2255_framei *frm;
	unsigned char *pdata;
	unsigned long cur_size;
	int bsearch = 0;
	struct s2255_bufferi *buf;
	dprintk(100, "buffer to user\n");

	idx = dev->cur_frame[dev->cc];
	buf = &dev->buffer[dev->cc];
	frm = &buf->frame[idx];

	if (frm->ulState == 0) {
		frm->ulState = 1;
		frm->cur_size = 0;
		bsearch = 1;
	} else if (frm->ulState == 2) {
		/* system frame was not freed */
		dprintk(2, "sys frame not free.  overrun ringbuf\n");
		bsearch = 1;
		frm->ulState = 1;
		frm->cur_size = 0;
	}

	if (bsearch) {
		if (*(s32 *) pipe_info->transfer_buffer != FRAME_MARKER) {
			u32 jj;
			if (dbgsync == 0) {
				dprintk(3, "not synched, discarding all packets"
					"until marker\n");

				dbgsync++;
			}
			pdata = (unsigned char *)pipe_info->transfer_buffer;
			for (jj = 0; jj < (pipe_info->cur_transfer_size - 12);
			     jj++) {
				if (*(s32 *) pdata == FRAME_MARKER) {
					int cc;
					dprintk(3,
						"found frame marker at offset:"
						" %d [%x %x]\n", jj, pdata[0],
						pdata[1]);
					offset = jj;
					bsync = 1;
					cc = *(u32 *) (pdata + sizeof(u32));
					if (cc >= MAX_CHANNELS) {
						printk(KERN_ERR
						       "bad channel\n");
						return -EINVAL;
					}
					/* reverse it */
					dev->cc = G_chnmap[cc];
					break;
				}
				pdata++;
			}
			if (bsync == 0)
				return -EINVAL;
		} else {
			u32 *pword;
			u32 payload;
			int cc;
			dbgsync = 0;
			bsync = 1;
			pword = (u32 *) pipe_info->transfer_buffer;
			cc = pword[1];

			if (cc >= MAX_CHANNELS) {
				printk("invalid channel found. "
					"throwing out data!\n");
				return -EINVAL;
			}
			dev->cc = G_chnmap[cc];
			payload = pword[2];
			if (payload != dev->req_image_size[dev->cc]) {
				dprintk(1, "[%d][%d]unexpected payload: %d"
					"required: %lu \n", cc, dev->cc,
					payload, dev->req_image_size[dev->cc]);
				dev->bad_payload[dev->cc]++;
				/* discard the bad frame */
				return -EINVAL;
			}

		}
	}
	/* search done.  now find out if should be acquiring
	   on this channel */
	if (!dev->b_acquire[dev->cc]) {
		frm->ulState = 0;
		return -EINVAL;
	}

	idx = dev->cur_frame[dev->cc];
	frm = &dev->buffer[dev->cc].frame[idx];

	if (frm->ulState == 0) {
		frm->ulState = 1;
		frm->cur_size = 0;
	} else if (frm->ulState == 2) {
		/* system frame ring buffer overrun */
		dprintk(2, "sys frame overrun.  overwriting frame %d %d\n",
			dev->cc, idx);
		frm->ulState = 1;
		frm->cur_size = 0;
	}

	if (bsync) {
		/* skip the marker 512 bytes (and offset if out of sync) */
		psrc = (u8 *)pipe_info->transfer_buffer + offset + PREFIX_SIZE;
	} else {
		psrc = (u8 *)pipe_info->transfer_buffer;
	}

	if (frm->lpvbits == NULL) {
		dprintk(1, "s2255 frame buffer == NULL.%p %p %d %d",
			frm, dev, dev->cc, idx);
		return -ENOMEM;
	}

	pdest = frm->lpvbits + frm->cur_size;

	if (bsync) {
		copy_size =
		    (pipe_info->cur_transfer_size - offset) - PREFIX_SIZE;
		if (copy_size > pipe_info->cur_transfer_size) {
			printk("invalid copy size, overflow!\n");
			return -ENOMEM;
		}
	} else {
		copy_size = pipe_info->cur_transfer_size;
	}

	cur_size = frm->cur_size;
	size = dev->req_image_size[dev->cc];

	if ((copy_size + cur_size) > size) {
		copy_size = size - cur_size;
		btrunc = 1;
	}

	memcpy(pdest, psrc, copy_size);
	cur_size += copy_size;
	frm->cur_size += copy_size;
	dprintk(50, "cur_size size %lu size %lu \n", cur_size, size);

	if (cur_size >= (size - PREFIX_SIZE)) {
		u32 cc = dev->cc;
		frm->ulState = 2;
		dprintk(2, "****************[%d]Buffer[%d]full*************\n",
			cc, idx);
		dev->last_frame[cc] = dev->cur_frame[cc];
		dev->cur_frame[cc]++;
		/* end of system frame ring buffer, start at zero */
		if ((dev->cur_frame[cc] == SYS_FRAMES) ||
		    (dev->cur_frame[cc] == dev->buffer[cc].dwFrames))
			dev->cur_frame[cc] = 0;

		/* signal the semaphore for this channel */
		if (dev->b_acquire[cc])
			s2255_got_frame(dev, cc);
		dev->frame_count[cc]++;
	}
	/* frame was truncated */
	if (btrunc) {
		/* return more data to process */
		return EAGAIN;
	}
	/* done successfully */
	return 0;
}

static void s2255_read_video_callback(struct s2255_dev *dev,
				      struct s2255_pipeinfo *pipe_info)
{
	int res;
	dprintk(50, "callback read video \n");

	if (dev->cc >= MAX_CHANNELS) {
		dev->cc = 0;
		dev_err(&dev->udev->dev, "invalid channel\n");
		return;
	}
	/* otherwise copy to the system buffers */
	res = save_frame(dev, pipe_info);
	if (res == EAGAIN)
		save_frame(dev, pipe_info);

	dprintk(50, "callback read video done\n");
	return;
}

static long s2255_vendor_req(struct s2255_dev *dev, unsigned char Request,
			     u16 Index, u16 Value, void *TransferBuffer,
			     s32 TransferBufferLength, int bOut)
{
	int r;
	if (!bOut) {
		r = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0),
				    Request,
				    USB_TYPE_VENDOR | USB_RECIP_DEVICE |
				    USB_DIR_IN,
				    Value, Index, TransferBuffer,
				    TransferBufferLength, HZ * 5);
	} else {
		r = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
				    Request, USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				    Value, Index, TransferBuffer,
				    TransferBufferLength, HZ * 5);
	}
	return r;
}

/*
 * retrieve FX2 firmware version. future use.
 * @param dev pointer to device extension
 * @return -1 for fail, else returns firmware version as an int(16 bits)
 */
static int s2255_get_fx2fw(struct s2255_dev *dev)
{
	int fw;
	int ret;
	unsigned char transBuffer[64];
	ret = s2255_vendor_req(dev, S2255_VR_FW, 0, 0, transBuffer, 2,
			       S2255_VR_IN);
	if (ret < 0)
		dprintk(2, "get fw error: %x\n", ret);
	fw = transBuffer[0] + (transBuffer[1] << 8);
	dprintk(2, "Get FW %x %x\n", transBuffer[0], transBuffer[1]);
	return fw;
}

/*
 * Create the system ring buffer to copy frames into from the
 * usb read pipe.
 */
static int s2255_create_sys_buffers(struct s2255_dev *dev, unsigned long chn)
{
	unsigned long i;
	unsigned long reqsize;
	dprintk(1, "create sys buffers\n");
	if (chn >= MAX_CHANNELS)
		return -1;

	dev->buffer[chn].dwFrames = SYS_FRAMES;

	/* always allocate maximum size(PAL) for system buffers */
	reqsize = SYS_FRAMES_MAXSIZE;

	if (reqsize > SYS_FRAMES_MAXSIZE)
		reqsize = SYS_FRAMES_MAXSIZE;

	for (i = 0; i < SYS_FRAMES; i++) {
		/* allocate the frames */
		dev->buffer[chn].frame[i].lpvbits = vmalloc(reqsize);

		dprintk(1, "valloc %p chan %lu, idx %lu, pdata %p\n",
			&dev->buffer[chn].frame[i], chn, i,
			dev->buffer[chn].frame[i].lpvbits);
		dev->buffer[chn].frame[i].size = reqsize;
		if (dev->buffer[chn].frame[i].lpvbits == NULL) {
			printk(KERN_INFO "out of memory.  using less frames\n");
			dev->buffer[chn].dwFrames = i;
			break;
		}
	}

	/* make sure internal states are set */
	for (i = 0; i < SYS_FRAMES; i++) {
		dev->buffer[chn].frame[i].ulState = 0;
		dev->buffer[chn].frame[i].cur_size = 0;
	}

	dev->cur_frame[chn] = 0;
	dev->last_frame[chn] = -1;
	return 0;
}

static int s2255_release_sys_buffers(struct s2255_dev *dev,
				     unsigned long channel)
{
	unsigned long i;
	dprintk(1, "release sys buffers\n");
	for (i = 0; i < SYS_FRAMES; i++) {
		if (dev->buffer[channel].frame[i].lpvbits) {
			dprintk(1, "vfree %p\n",
				dev->buffer[channel].frame[i].lpvbits);
			vfree(dev->buffer[channel].frame[i].lpvbits);
		}
		dev->buffer[channel].frame[i].lpvbits = NULL;
	}
	return 0;
}

static int s2255_board_init(struct s2255_dev *dev)
{
	int j;
	struct s2255_mode mode_def = DEF_MODEI_NTSC_CONT;
	int fw_ver;
	dprintk(4, "board init: %p", dev);

	for (j = 0; j < MAX_PIPE_BUFFERS; j++) {
		struct s2255_pipeinfo *pipe = &dev->pipes[j];

		memset(pipe, 0, sizeof(*pipe));
		pipe->dev = dev;
		pipe->cur_transfer_size = DEFAULT_PIPE_USBBLOCK;
		pipe->max_transfer_size = MAX_PIPE_USBBLOCK;

		if (pipe->cur_transfer_size > pipe->max_transfer_size)
			pipe->cur_transfer_size = pipe->max_transfer_size;
		pipe->transfer_buffer = kzalloc(pipe->max_transfer_size,
						GFP_KERNEL);
		if (pipe->transfer_buffer == NULL) {
			dprintk(1, "out of memory!\n");
			return -ENOMEM;
		}

	}

	/* query the firmware */
	fw_ver = s2255_get_fx2fw(dev);

	printk(KERN_INFO "2255 usb firmware version %d \n", fw_ver);
	if (fw_ver < CUR_USB_FWVER)
		err("usb firmware not up to date %d\n", fw_ver);

	for (j = 0; j < MAX_CHANNELS; j++) {
		dev->b_acquire[j] = 0;
		dev->mode[j] = mode_def;
		dev->cur_fmt[j] = &formats[0];
		dev->mode[j].restart = 1;
		dev->req_image_size[j] = get_transfer_size(&mode_def);
		dev->frame_count[j] = 0;
		/* create the system buffers */
		s2255_create_sys_buffers(dev, j);
	}
	/* start read pipe */
	s2255_start_readpipe(dev);

	dprintk(1, "S2255: board initialized\n");
	return 0;
}

static int s2255_board_shutdown(struct s2255_dev *dev)
{
	u32 i;

	dprintk(1, "S2255: board shutdown: %p", dev);

	for (i = 0; i < MAX_CHANNELS; i++) {
		if (dev->b_acquire[i])
			s2255_stop_acquire(dev, i);
	}

	s2255_stop_readpipe(dev);

	for (i = 0; i < MAX_CHANNELS; i++)
		s2255_release_sys_buffers(dev, i);

	/* release transfer buffers */
	for (i = 0; i < MAX_PIPE_BUFFERS; i++) {
		struct s2255_pipeinfo *pipe = &dev->pipes[i];
		kfree(pipe->transfer_buffer);
	}
	return 0;
}

static void read_pipe_completion(struct urb *purb)
{
	struct s2255_pipeinfo *pipe_info;
	struct s2255_dev *dev;
	int status;
	int pipe;

	pipe_info = purb->context;
	dprintk(100, "read pipe completion %p, status %d\n", purb,
		purb->status);
	if (pipe_info == NULL) {
		err("no context !");
		return;
	}

	dev = pipe_info->dev;
	if (dev == NULL) {
		err("no context !");
		return;
	}
	status = purb->status;
	if (status != 0) {
		dprintk(2, "read_pipe_completion: err\n");
		return;
	}

	if (pipe_info->state == 0) {
		dprintk(2, "exiting USB pipe");
		return;
	}

	s2255_read_video_callback(dev, pipe_info);

	pipe_info->err_count = 0;
	pipe = usb_rcvbulkpipe(dev->udev, dev->read_endpoint);
	/* reuse urb */
	usb_fill_bulk_urb(pipe_info->stream_urb, dev->udev,
			  pipe,
			  pipe_info->transfer_buffer,
			  pipe_info->cur_transfer_size,
			  read_pipe_completion, pipe_info);

	if (pipe_info->state != 0) {
		if (usb_submit_urb(pipe_info->stream_urb, GFP_KERNEL)) {
			dev_err(&dev->udev->dev, "error submitting urb\n");
			usb_free_urb(pipe_info->stream_urb);
		}
	} else {
		dprintk(2, "read pipe complete state 0\n");
	}
	return;
}

static int s2255_start_readpipe(struct s2255_dev *dev)
{
	int pipe;
	int retval;
	int i;
	struct s2255_pipeinfo *pipe_info = dev->pipes;
	pipe = usb_rcvbulkpipe(dev->udev, dev->read_endpoint);
	dprintk(2, "start pipe IN %d\n", dev->read_endpoint);

	for (i = 0; i < MAX_PIPE_BUFFERS; i++) {
		pipe_info->state = 1;
		pipe_info->buf_index = (u32) i;
		pipe_info->priority_set = 0;
		pipe_info->stream_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!pipe_info->stream_urb) {
			dev_err(&dev->udev->dev,
				"ReadStream: Unable to alloc URB");
			return -ENOMEM;
		}
		/* transfer buffer allocated in board_init */
		usb_fill_bulk_urb(pipe_info->stream_urb, dev->udev,
				  pipe,
				  pipe_info->transfer_buffer,
				  pipe_info->cur_transfer_size,
				  read_pipe_completion, pipe_info);

		pipe_info->urb_size = sizeof(pipe_info->stream_urb);
		dprintk(4, "submitting URB %p\n", pipe_info->stream_urb);
		retval = usb_submit_urb(pipe_info->stream_urb, GFP_KERNEL);
		if (retval) {
			printk(KERN_ERR "s2255: start read pipe failed\n");
			return retval;
		}
	}

	return 0;
}

/* starts acquisition process */
static int s2255_start_acquire(struct s2255_dev *dev, unsigned long chn)
{
	unsigned char *buffer;
	int res;
	unsigned long chn_rev;
	int j;
	if (chn >= MAX_CHANNELS) {
		dprintk(2, "start acquire failed, bad channel %lu\n", chn);
		return -1;
	}

	chn_rev = G_chnmap[chn];
	dprintk(1, "S2255: start acquire %lu \n", chn);

	buffer = kzalloc(512, GFP_KERNEL);
	if (buffer == NULL) {
		dev_err(&dev->udev->dev, "out of mem\n");
		return -ENOMEM;
	}

	dev->last_frame[chn] = -1;
	dev->bad_payload[chn] = 0;
	dev->cur_frame[chn] = 0;
	for (j = 0; j < SYS_FRAMES; j++) {
		dev->buffer[chn].frame[j].ulState = 0;
		dev->buffer[chn].frame[j].cur_size = 0;
	}

	/* send the start command */
	*(u32 *) buffer = IN_DATA_TOKEN;
	*((u32 *) buffer + 1) = (u32) chn_rev;
	*((u32 *) buffer + 2) = (u32) CMD_START;
	res = s2255_write_config(dev->udev, (unsigned char *)buffer, 512);
	if (res != 0)
		dev_err(&dev->udev->dev, "CMD_START error\n");

	dprintk(2, "start acquire exit[%lu] %d \n", chn, res);
	kfree(buffer);
	return 0;
}

static int s2255_stop_acquire(struct s2255_dev *dev, unsigned long chn)
{
	unsigned char *buffer;
	int res;
	unsigned long chn_rev;

	if (chn >= MAX_CHANNELS) {
		dprintk(2, "stop acquire failed, bad channel %lu\n", chn);
		return -1;
	}
	chn_rev = G_chnmap[chn];

	buffer = kzalloc(512, GFP_KERNEL);
	if (buffer == NULL) {
		dev_err(&dev->udev->dev, "out of mem\n");
		return -ENOMEM;
	}

	/* send the stop command */
	dprintk(4, "stop acquire %lu\n", chn);
	*(u32 *) buffer = IN_DATA_TOKEN;
	*((u32 *) buffer + 1) = (u32) chn_rev;
	*((u32 *) buffer + 2) = CMD_STOP;
	res = s2255_write_config(dev->udev, (unsigned char *)buffer, 512);

	if (res != 0)
		dev_err(&dev->udev->dev, "CMD_STOP error\n");

	dprintk(4, "stop acquire: releasing states \n");

	kfree(buffer);
	dev->b_acquire[chn] = 0;

	return 0;
}

static void s2255_stop_readpipe(struct s2255_dev *dev)
{
	int j;

	if (dev == NULL) {
		err("s2255: invalid device");
		return;
	}
	dprintk(4, "stop read pipe\n");
	for (j = 0; j < MAX_PIPE_BUFFERS; j++) {
		struct s2255_pipeinfo *pipe_info = &dev->pipes[j];
		if (pipe_info) {
			if (pipe_info->state == 0)
				continue;
			pipe_info->state = 0;
			pipe_info->prev_state = 1;

		}
	}

	for (j = 0; j < MAX_PIPE_BUFFERS; j++) {
		struct s2255_pipeinfo *pipe_info = &dev->pipes[j];
		if (pipe_info->stream_urb) {
			/* cancel urb */
			usb_kill_urb(pipe_info->stream_urb);
			usb_free_urb(pipe_info->stream_urb);
			pipe_info->stream_urb = NULL;
		}
	}
	dprintk(2, "s2255 stop read pipe: %d\n", j);
	return;
}

static void s2255_fwload_start(struct s2255_dev *dev)
{
	dev->fw_data->fw_size = dev->fw_data->fw->size;
	atomic_set(&dev->fw_data->fw_state, S2255_FW_NOTLOADED);
	memcpy(dev->fw_data->pfw_data,
	       dev->fw_data->fw->data, CHUNK_SIZE);
	dev->fw_data->fw_loaded = CHUNK_SIZE;
	usb_fill_bulk_urb(dev->fw_data->fw_urb, dev->udev,
			  usb_sndbulkpipe(dev->udev, 2),
			  dev->fw_data->pfw_data,
			  CHUNK_SIZE, s2255_fwchunk_complete,
			  dev->fw_data);
	mod_timer(&dev->timer, jiffies + HZ);
}

/* standard usb probe function */
static int s2255_probe(struct usb_interface *interface,
		       const struct usb_device_id *id)
{
	struct s2255_dev *dev = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i;
	int retval = -ENOMEM;

	dprintk(2, "s2255: probe\n");

	/* allocate memory for our device state and initialize it to zero */
	dev = kzalloc(sizeof(struct s2255_dev), GFP_KERNEL);
	if (dev == NULL) {
		err("s2255: out of memory");
		goto error;
	}

	dev->fw_data = kzalloc(sizeof(struct s2255_fw), GFP_KERNEL);
	if (!dev->fw_data)
		goto error;

	mutex_init(&dev->lock);
	mutex_init(&dev->open_lock);

	/* grab usb_device and save it */
	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	if (dev->udev == NULL) {
		dev_err(&interface->dev, "null usb device\n");
		retval = -ENODEV;
		goto error;
	}
	kref_init(&dev->kref);
	dprintk(1, "dev: %p, kref: %p udev %p interface %p\n", dev, &dev->kref,
		dev->udev, interface);
	dev->interface = interface;
	/* set up the endpoint information  */
	iface_desc = interface->cur_altsetting;
	dprintk(1, "num endpoints %d\n", iface_desc->desc.bNumEndpoints);
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		if (!dev->read_endpoint && usb_endpoint_is_bulk_in(endpoint)) {
			/* we found the bulk in endpoint */
			dev->read_endpoint = endpoint->bEndpointAddress;
		}
	}

	if (!dev->read_endpoint) {
		dev_err(&interface->dev, "Could not find bulk-in endpoint");
		goto error;
	}

	/* set intfdata */
	usb_set_intfdata(interface, dev);

	dprintk(100, "after intfdata %p\n", dev);

	init_timer(&dev->timer);
	dev->timer.function = s2255_timer;
	dev->timer.data = (unsigned long)dev->fw_data;

	init_waitqueue_head(&dev->fw_data->wait_fw);


	dev->fw_data->fw_urb = usb_alloc_urb(0, GFP_KERNEL);

	if (!dev->fw_data->fw_urb) {
		dev_err(&interface->dev, "out of memory!\n");
		goto error;
	}
	dev->fw_data->pfw_data = kzalloc(CHUNK_SIZE, GFP_KERNEL);
	if (!dev->fw_data->pfw_data) {
		dev_err(&interface->dev, "out of memory!\n");
		goto error;
	}
	/* load the first chunk */
	if (request_firmware(&dev->fw_data->fw,
			     FIRMWARE_FILE_NAME, &dev->udev->dev)) {
		printk(KERN_ERR "sensoray 2255 failed to get firmware\n");
		goto error;
	}

	/* loads v4l specific */
	s2255_probe_v4l(dev);
	/* load 2255 board specific */
	s2255_board_init(dev);

	dprintk(4, "before probe done %p\n", dev);
	spin_lock_init(&dev->slock);

	s2255_fwload_start(dev);
	dev_info(&interface->dev, "Sensoray 2255 detected\n");
	return 0;
error:
	return retval;
}

/* disconnect routine. when board is removed physically or with rmmod */
static void s2255_disconnect(struct usb_interface *interface)
{
	struct s2255_dev *dev = NULL;
	dprintk(1, "s2255: disconnect interface %p\n", interface);
	dev = usb_get_intfdata(interface);
	if (dev) {
		kref_put(&dev->kref, s2255_destroy);
		dprintk(1, "s2255drv: disconnect\n");
		dev_info(&interface->dev, "s2255usb now disconnected\n");
	}
	usb_set_intfdata(interface, NULL);
}

static struct usb_driver s2255_driver = {
	.name = "s2255",
	.probe = s2255_probe,
	.disconnect = s2255_disconnect,
	.id_table = s2255_table,
};

static int __init usb_s2255_init(void)
{
	int result;

	/* register this driver with the USB subsystem */
	result = usb_register(&s2255_driver);

	if (result)
		err("usb_register failed. Error number %d", result);

	dprintk(2, "s2255_init: done\n");
	return result;
}

static void __exit usb_s2255_exit(void)
{
	usb_deregister(&s2255_driver);
}

module_init(usb_s2255_init);
module_exit(usb_s2255_exit);

MODULE_DESCRIPTION("Sensoray 2255 Video for Linux driver");
MODULE_AUTHOR("Dean Anderson (Sensoray Company Inc.)");
MODULE_LICENSE("GPL");

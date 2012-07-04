/*
 *  s2255drv.c - a driver for the Sensoray 2255 USB video capture device
 *
 *   Copyright (C) 2007-2010 by Sensoray Company Inc.
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
 * -full or half size Grey scale: all 4 channels at once
 * -half size, color mode YUYV or YUV422P: all 4 channels at once
 * -full size, color mode YUYV or YUV422P 1/2 frame rate: all 4 channels
 *  at once.
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
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/mm.h>
#include <media/videobuf-vmalloc.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <linux/vmalloc.h>
#include <linux/usb.h>

#define S2255_VERSION		"1.22.1"
#define FIRMWARE_FILE_NAME "f2255usb.bin"

/* default JPEG quality */
#define S2255_DEF_JPEG_QUAL     50
/* vendor request in */
#define S2255_VR_IN		0
/* vendor request out */
#define S2255_VR_OUT		1
/* firmware query */
#define S2255_VR_FW		0x30
/* USB endpoint number for configuring the device */
#define S2255_CONFIG_EP         2
/* maximum time for DSP to start responding after last FW word loaded(ms) */
#define S2255_DSP_BOOTTIME      800
/* maximum time to wait for firmware to load (ms) */
#define S2255_LOAD_TIMEOUT      (5000 + S2255_DSP_BOOTTIME)
#define S2255_DEF_BUFS          16
#define S2255_SETMODE_TIMEOUT   500
#define S2255_VIDSTATUS_TIMEOUT 350
#define S2255_MARKER_FRAME	cpu_to_le32(0x2255DA4AL)
#define S2255_MARKER_RESPONSE	cpu_to_le32(0x2255ACACL)
#define S2255_RESPONSE_SETMODE  cpu_to_le32(0x01)
#define S2255_RESPONSE_FW       cpu_to_le32(0x10)
#define S2255_RESPONSE_STATUS   cpu_to_le32(0x20)
#define S2255_USB_XFER_SIZE	(16 * 1024)
#define MAX_CHANNELS		4
#define SYS_FRAMES		4
/* maximum size is PAL full size plus room for the marker header(s) */
#define SYS_FRAMES_MAXSIZE	(720*288*2*2 + 4096)
#define DEF_USB_BLOCK		S2255_USB_XFER_SIZE
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
/* SCALE_4CIFSI is the 2 fields interpolated into one */
#define SCALE_4CIFSI	4	/* 640x480(NTSC) or 704x576(PAL) high quality */

#define COLOR_YUVPL	1	/* YUV planar */
#define COLOR_YUVPK	2	/* YUV packed */
#define COLOR_Y8	4	/* monochrome */
#define COLOR_JPG       5       /* JPEG */

#define MASK_COLOR       0x000000ff
#define MASK_JPG_QUALITY 0x0000ff00
#define MASK_INPUT_TYPE  0x000f0000
/* frame decimation. */
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
#define IN_DATA_TOKEN	cpu_to_le32(0x2255c0de)
#define CMD_2255	0xc2255000
#define CMD_SET_MODE	cpu_to_le32((CMD_2255 | 0x10))
#define CMD_START	cpu_to_le32((CMD_2255 | 0x20))
#define CMD_STOP	cpu_to_le32((CMD_2255 | 0x30))
#define CMD_STATUS	cpu_to_le32((CMD_2255 | 0x40))

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


#define S2255_READ_IDLE		0
#define S2255_READ_FRAME	1

/* frame structure */
struct s2255_framei {
	unsigned long size;
	unsigned long ulState;	/* ulState:S2255_READ_IDLE, S2255_READ_FRAME*/
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
	struct s2255_dev	*dev;
};

/* for firmware loading, fw_state */
#define S2255_FW_NOTLOADED	0
#define S2255_FW_LOADED_DSPWAIT	1
#define S2255_FW_SUCCESS	2
#define S2255_FW_FAILED		3
#define S2255_FW_DISCONNECTING  4
#define S2255_FW_MARKER		cpu_to_le32(0x22552f2f)
/* 2255 read states */
#define S2255_READ_IDLE         0
#define S2255_READ_FRAME        1
struct s2255_fw {
	int		      fw_loaded;
	int		      fw_size;
	struct urb	      *fw_urb;
	atomic_t	      fw_state;
	void		      *pfw_data;
	wait_queue_head_t     wait_fw;
	const struct firmware *fw;
};

struct s2255_pipeinfo {
	u32 max_transfer_size;
	u32 cur_transfer_size;
	u8 *transfer_buffer;
	u32 state;
	void *stream_urb;
	void *dev;	/* back pointer to s2255_dev struct*/
	u32 err_count;
	u32 idx;
};

struct s2255_fmt; /*forward declaration */
struct s2255_dev;

struct s2255_channel {
	struct video_device	vdev;
	int			resources;
	struct s2255_dmaqueue	vidq;
	struct s2255_bufferi	buffer;
	struct s2255_mode	mode;
	/* jpeg compression */
	struct v4l2_jpegcompression jc;
	/* capture parameters (for high quality mode full size) */
	struct v4l2_captureparm cap_parm;
	int			cur_frame;
	int			last_frame;

	int			b_acquire;
	/* allocated image size */
	unsigned long		req_image_size;
	/* received packet size */
	unsigned long		pkt_size;
	int			bad_payload;
	unsigned long		frame_count;
	/* if JPEG image */
	int                     jpg_size;
	/* if channel configured to default state */
	int                     configured;
	wait_queue_head_t       wait_setmode;
	int                     setmode_ready;
	/* video status items */
	int                     vidstatus;
	wait_queue_head_t       wait_vidstatus;
	int                     vidstatus_ready;
	unsigned int		width;
	unsigned int		height;
	const struct s2255_fmt	*fmt;
	int idx; /* channel number on device, 0-3 */
};


struct s2255_dev {
	struct s2255_channel    channel[MAX_CHANNELS];
	struct v4l2_device 	v4l2_dev;
	atomic_t                num_channels;
	int			frames;
	struct mutex		lock;	/* channels[].vdev.lock */
	struct mutex		open_lock;
	struct usb_device	*udev;
	struct usb_interface	*interface;
	u8			read_endpoint;
	struct timer_list	timer;
	struct s2255_fw	*fw_data;
	struct s2255_pipeinfo	pipe;
	u32			cc;	/* current channel */
	int			frame_ready;
	int                     chn_ready;
	spinlock_t              slock;
	/* dsp firmware version (f2255usb.bin) */
	int                     dsp_fw_ver;
	u16                     pid; /* product id */
};

static inline struct s2255_dev *to_s2255_dev(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct s2255_dev, v4l2_dev);
}

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
	struct videobuf_queue	vb_vidq;
	enum v4l2_buf_type	type;
	struct s2255_channel	*channel;
	int			resources;
};

/* current cypress EEPROM firmware version */
#define S2255_CUR_USB_FWVER	((3 << 8) | 12)
/* current DSP FW version */
#define S2255_CUR_DSP_FWVER     10104
/* Need DSP version 5+ for video status feature */
#define S2255_MIN_DSP_STATUS      5
#define S2255_MIN_DSP_COLORFILTER 8
#define S2255_NORMS		(V4L2_STD_PAL | V4L2_STD_NTSC)

/* private V4L2 controls */

/*
 * The following chart displays how COLORFILTER should be set
 *  =========================================================
 *  =     fourcc              =     COLORFILTER             =
 *  =                         ===============================
 *  =                         =   0             =    1      =
 *  =========================================================
 *  =  V4L2_PIX_FMT_GREY(Y8)  = monochrome from = monochrome=
 *  =                         = s-video or      = composite =
 *  =                         = B/W camera      = input     =
 *  =========================================================
 *  =    other                = color, svideo   = color,    =
 *  =                         =                 = composite =
 *  =========================================================
 *
 * Notes:
 *   channels 0-3 on 2255 are composite
 *   channels 0-1 on 2257 are composite, 2-3 are s-video
 * If COLORFILTER is 0 with a composite color camera connected,
 * the output will appear monochrome but hatching
 * will occur.
 * COLORFILTER is different from "color killer" and "color effects"
 * for reasons above.
 */
#define S2255_V4L2_YC_ON  1
#define S2255_V4L2_YC_OFF 0
#define V4L2_CID_PRIVATE_COLORFILTER (V4L2_CID_PRIVATE_BASE + 0)

/* frame prefix size (sent once every frame) */
#define PREFIX_SIZE		512

/* Channels on box are in reverse order */
static unsigned long G_chnmap[MAX_CHANNELS] = {3, 2, 1, 0};

static int debug;
static int *s2255_debug = &debug;

static int s2255_start_readpipe(struct s2255_dev *dev);
static void s2255_stop_readpipe(struct s2255_dev *dev);
static int s2255_start_acquire(struct s2255_channel *channel);
static int s2255_stop_acquire(struct s2255_channel *channel);
static void s2255_fillbuff(struct s2255_channel *chn, struct s2255_buffer *buf,
			   int jpgsize);
static int s2255_set_mode(struct s2255_channel *chan, struct s2255_mode *mode);
static int s2255_board_shutdown(struct s2255_dev *dev);
static void s2255_fwload_start(struct s2255_dev *dev, int reset);
static void s2255_destroy(struct s2255_dev *dev);
static long s2255_vendor_req(struct s2255_dev *dev, unsigned char req,
			     u16 index, u16 value, void *buf,
			     s32 buf_len, int bOut);

/* dev_err macro with driver name */
#define S2255_DRIVER_NAME "s2255"
#define s2255_dev_err(dev, fmt, arg...)					\
		dev_err(dev, S2255_DRIVER_NAME " - " fmt, ##arg)

#define dprintk(level, fmt, arg...)					\
	do {								\
		if (*s2255_debug >= (level)) {				\
			printk(KERN_DEBUG S2255_DRIVER_NAME		\
				": " fmt, ##arg);			\
		}							\
	} while (0)

static struct usb_driver s2255_driver;

/* Declare static vars that will be used as parameters */
static unsigned int vid_limit = 16;	/* Video memory limit, in Mb */

/* start video number */
static int video_nr = -1;	/* /dev/videoN, -1 for autodetect */

/* Enable jpeg capture. */
static int jpeg_enable = 1;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level(0-100) default 0");
module_param(vid_limit, int, 0644);
MODULE_PARM_DESC(vid_limit, "video memory limit(Mb)");
module_param(video_nr, int, 0644);
MODULE_PARM_DESC(video_nr, "start video minor(-1 default autodetect)");
module_param(jpeg_enable, int, 0644);
MODULE_PARM_DESC(jpeg_enable, "Jpeg enable(1-on 0-off) default 1");

/* USB device table */
#define USB_SENSORAY_VID	0x1943
static struct usb_device_id s2255_table[] = {
	{USB_DEVICE(USB_SENSORAY_VID, 0x2255)},
	{USB_DEVICE(USB_SENSORAY_VID, 0x2257)}, /*same family as 2255*/
	{ }			/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, s2255_table);

#define BUFFER_TIMEOUT msecs_to_jiffies(400)

/* image formats.  */
/* JPEG formats must be defined last to support jpeg_enable parameter */
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
	}, {
		.name = "JPG",
		.fourcc = V4L2_PIX_FMT_JPEG,
		.depth = 24
	}, {
		.name = "MJPG",
		.fourcc = V4L2_PIX_FMT_MJPEG,
		.depth = 24
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

static void s2255_reset_dsppower(struct s2255_dev *dev)
{
	s2255_vendor_req(dev, 0x40, 0x0000, 0x0001, NULL, 0, 1);
	msleep(10);
	s2255_vendor_req(dev, 0x50, 0x0000, 0x0000, NULL, 0, 1);
	msleep(600);
	s2255_vendor_req(dev, 0x10, 0x0000, 0x0000, NULL, 0, 1);
	return;
}

/* kickstarts the firmware loading. from probe
 */
static void s2255_timer(unsigned long user_data)
{
	struct s2255_fw *data = (struct s2255_fw *)user_data;
	dprintk(100, "%s\n", __func__);
	if (usb_submit_urb(data->fw_urb, GFP_ATOMIC) < 0) {
		printk(KERN_ERR "s2255: can't submit urb\n");
		atomic_set(&data->fw_state, S2255_FW_FAILED);
		/* wake up anything waiting for the firmware */
		wake_up(&data->wait_fw);
		return;
	}
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
	dprintk(100, "%s: udev %p urb %p", __func__, udev, urb);
	if (urb->status) {
		dev_err(&udev->dev, "URB failed with status %d\n", urb->status);
		atomic_set(&data->fw_state, S2255_FW_FAILED);
		/* wake up anything waiting for the firmware */
		wake_up(&data->wait_fw);
		return;
	}
	if (data->fw_urb == NULL) {
		s2255_dev_err(&udev->dev, "disconnected\n");
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
		atomic_set(&data->fw_state, S2255_FW_LOADED_DSPWAIT);
		dprintk(100, "%s: firmware upload complete\n", __func__);
	}
	return;

}

static int s2255_got_frame(struct s2255_channel *channel, int jpgsize)
{
	struct s2255_dmaqueue *dma_q = &channel->vidq;
	struct s2255_buffer *buf;
	struct s2255_dev *dev = to_s2255_dev(channel->vdev.v4l2_dev);
	unsigned long flags = 0;
	int rc = 0;
	spin_lock_irqsave(&dev->slock, flags);
	if (list_empty(&dma_q->active)) {
		dprintk(1, "No active queue to serve\n");
		rc = -1;
		goto unlock;
	}
	buf = list_entry(dma_q->active.next,
			 struct s2255_buffer, vb.queue);
	list_del(&buf->vb.queue);
	do_gettimeofday(&buf->vb.ts);
	s2255_fillbuff(channel, buf, jpgsize);
	wake_up(&buf->vb.done);
	dprintk(2, "%s: [buf/i] [%p/%d]\n", __func__, buf, buf->vb.i);
unlock:
	spin_unlock_irqrestore(&dev->slock, flags);
	return rc;
}

static const struct s2255_fmt *format_by_fourcc(int fourcc)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (-1 == formats[i].fourcc)
			continue;
	if (!jpeg_enable && ((formats[i].fourcc == V4L2_PIX_FMT_JPEG) ||
			     (formats[i].fourcc == V4L2_PIX_FMT_MJPEG)))
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
static void s2255_fillbuff(struct s2255_channel *channel,
			   struct s2255_buffer *buf, int jpgsize)
{
	int pos = 0;
	struct timeval ts;
	const char *tmpbuf;
	char *vbuf = videobuf_to_vmalloc(&buf->vb);
	unsigned long last_frame;

	if (!vbuf)
		return;
	last_frame = channel->last_frame;
	if (last_frame != -1) {
		tmpbuf =
		    (const char *)channel->buffer.frame[last_frame].lpvbits;
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
		case V4L2_PIX_FMT_JPEG:
		case V4L2_PIX_FMT_MJPEG:
			buf->vb.size = jpgsize;
			memcpy(vbuf, tmpbuf, buf->vb.size);
			break;
		case V4L2_PIX_FMT_YUV422P:
			memcpy(vbuf, tmpbuf,
			       buf->vb.width * buf->vb.height * 2);
			break;
		default:
			printk(KERN_DEBUG "s2255: unknown format?\n");
		}
		channel->last_frame = -1;
	} else {
		printk(KERN_ERR "s2255: =======no frame\n");
		return;

	}
	dprintk(2, "s2255fill at : Buffer 0x%08lx size= %d\n",
		(unsigned long)vbuf, pos);
	/* tell v4l buffer was filled */

	buf->vb.field_count = channel->frame_count * 2;
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
	struct s2255_channel *channel = fh->channel;
	*size = channel->width * channel->height * (channel->fmt->depth >> 3);

	if (0 == *count)
		*count = S2255_DEF_BUFS;

	if (*size * *count > vid_limit * 1024 * 1024)
		*count = (vid_limit * 1024 * 1024) / *size;

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct s2255_buffer *buf)
{
	dprintk(4, "%s\n", __func__);

	videobuf_vmalloc_free(&buf->vb);
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static int buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
			  enum v4l2_field field)
{
	struct s2255_fh *fh = vq->priv_data;
	struct s2255_channel *channel = fh->channel;
	struct s2255_buffer *buf = container_of(vb, struct s2255_buffer, vb);
	int rc;
	int w = channel->width;
	int h = channel->height;
	dprintk(4, "%s, field=%d\n", __func__, field);
	if (channel->fmt == NULL)
		return -EINVAL;

	if ((w < norm_minw(&channel->vdev)) ||
	    (w > norm_maxw(&channel->vdev)) ||
	    (h < norm_minh(&channel->vdev)) ||
	    (h > norm_maxh(&channel->vdev))) {
		dprintk(4, "invalid buffer prepare\n");
		return -EINVAL;
	}
	buf->vb.size = w * h * (channel->fmt->depth >> 3);
	if (0 != buf->vb.baddr && buf->vb.bsize < buf->vb.size) {
		dprintk(4, "invalid buffer prepare\n");
		return -EINVAL;
	}

	buf->fmt = channel->fmt;
	buf->vb.width = w;
	buf->vb.height = h;
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
	struct s2255_channel *channel = fh->channel;
	struct s2255_dmaqueue *vidq = &channel->vidq;
	dprintk(1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct s2255_buffer *buf = container_of(vb, struct s2255_buffer, vb);
	struct s2255_fh *fh = vq->priv_data;
	dprintk(4, "%s %d\n", __func__, fh->channel->idx);
	free_buffer(vq, buf);
}

static struct videobuf_queue_ops s2255_video_qops = {
	.buf_setup = buffer_setup,
	.buf_prepare = buffer_prepare,
	.buf_queue = buffer_queue,
	.buf_release = buffer_release,
};


static int res_get(struct s2255_fh *fh)
{
	struct s2255_channel *channel = fh->channel;
	/* is it free? */
	if (channel->resources)
		return 0; /* no, someone else uses it */
	/* it's free, grab it */
	channel->resources = 1;
	fh->resources = 1;
	dprintk(1, "s2255: res: get\n");
	return 1;
}

static int res_locked(struct s2255_fh *fh)
{
	return fh->channel->resources;
}

static int res_check(struct s2255_fh *fh)
{
	return fh->resources;
}


static void res_free(struct s2255_fh *fh)
{
	struct s2255_channel *channel = fh->channel;
	channel->resources = 0;
	fh->resources = 0;
	dprintk(1, "res: put\n");
}

static int vidioc_querymenu(struct file *file, void *priv,
			    struct v4l2_querymenu *qmenu)
{
	static const char *colorfilter[] = {
		"Off",
		"On",
		NULL
	};
	if (qmenu->id == V4L2_CID_PRIVATE_COLORFILTER) {
		int i;
		const char **menu_items = colorfilter;
		for (i = 0; i < qmenu->index && menu_items[i]; i++)
			; /* do nothing (from v4l2-common.c) */
		if (menu_items[i] == NULL || menu_items[i][0] == '\0')
			return -EINVAL;
		strlcpy(qmenu->name, menu_items[qmenu->index],
			sizeof(qmenu->name));
		return 0;
	}
	return v4l2_ctrl_query_menu(qmenu, NULL, NULL);
}

static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct s2255_fh *fh = file->private_data;
	struct s2255_dev *dev = fh->dev;
	strlcpy(cap->driver, "s2255", sizeof(cap->driver));
	strlcpy(cap->card, "s2255", sizeof(cap->card));
	usb_make_path(dev->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
			       struct v4l2_fmtdesc *f)
{
	int index = f->index;

	if (index >= ARRAY_SIZE(formats))
		return -EINVAL;
	if (!jpeg_enable && ((formats[index].fourcc == V4L2_PIX_FMT_JPEG) ||
			(formats[index].fourcc == V4L2_PIX_FMT_MJPEG)))
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
	struct s2255_channel *channel = fh->channel;

	f->fmt.pix.width = channel->width;
	f->fmt.pix.height = channel->height;
	f->fmt.pix.field = fh->vb_vidq.field;
	f->fmt.pix.pixelformat = channel->fmt->fourcc;
	f->fmt.pix.bytesperline = f->fmt.pix.width * (channel->fmt->depth >> 3);
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
	struct s2255_channel *channel = fh->channel;
	int is_ntsc;
	is_ntsc =
		(channel->vdev.current_norm & V4L2_STD_NTSC) ? 1 : 0;

	fmt = format_by_fourcc(f->fmt.pix.pixelformat);

	if (fmt == NULL)
		return -EINVAL;

	field = f->fmt.pix.field;
	if (field == V4L2_FIELD_ANY)
		b_any_field = 1;

	dprintk(50, "%s NTSC: %d suggested width: %d, height: %d\n",
		__func__, is_ntsc, f->fmt.pix.width, f->fmt.pix.height);
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
			f->fmt.pix.width = LINE_SZ_4CIFS_PAL;
			field = V4L2_FIELD_SEQ_TB;
		} else if (f->fmt.pix.width >= LINE_SZ_2CIFS_PAL) {
			f->fmt.pix.width = LINE_SZ_2CIFS_PAL;
			field = V4L2_FIELD_TOP;
		} else if (f->fmt.pix.width >= LINE_SZ_1CIFS_PAL) {
			f->fmt.pix.width = LINE_SZ_1CIFS_PAL;
			field = V4L2_FIELD_TOP;
		} else {
			f->fmt.pix.width = LINE_SZ_1CIFS_PAL;
			field = V4L2_FIELD_TOP;
		}
	}
	f->fmt.pix.field = field;
	f->fmt.pix.bytesperline = (f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	dprintk(50, "%s: set width %d height %d field %d\n", __func__,
		f->fmt.pix.width, f->fmt.pix.height, f->fmt.pix.field);
	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct s2255_fh *fh = priv;
	struct s2255_channel *channel = fh->channel;
	const struct s2255_fmt *fmt;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct s2255_mode mode;
	int ret;

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

	if (res_locked(fh)) {
		dprintk(1, "%s: channel busy\n", __func__);
		ret = -EBUSY;
		goto out_s_fmt;
	}
	mode = channel->mode;
	channel->fmt = fmt;
	channel->width = f->fmt.pix.width;
	channel->height = f->fmt.pix.height;
	fh->vb_vidq.field = f->fmt.pix.field;
	fh->type = f->type;
	if (channel->width > norm_minw(&channel->vdev)) {
		if (channel->height > norm_minh(&channel->vdev)) {
			if (channel->cap_parm.capturemode &
			    V4L2_MODE_HIGHQUALITY)
				mode.scale = SCALE_4CIFSI;
			else
				mode.scale = SCALE_4CIFS;
		} else
			mode.scale = SCALE_2CIFS;

	} else {
		mode.scale = SCALE_1CIFS;
	}
	/* color mode */
	switch (channel->fmt->fourcc) {
	case V4L2_PIX_FMT_GREY:
		mode.color &= ~MASK_COLOR;
		mode.color |= COLOR_Y8;
		break;
	case V4L2_PIX_FMT_JPEG:
	case V4L2_PIX_FMT_MJPEG:
		mode.color &= ~MASK_COLOR;
		mode.color |= COLOR_JPG;
		mode.color |= (channel->jc.quality << 8);
		break;
	case V4L2_PIX_FMT_YUV422P:
		mode.color &= ~MASK_COLOR;
		mode.color |= COLOR_YUVPL;
		break;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	default:
		mode.color &= ~MASK_COLOR;
		mode.color |= COLOR_YUVPK;
		break;
	}
	if ((mode.color & MASK_COLOR) != (channel->mode.color & MASK_COLOR))
		mode.restart = 1;
	else if (mode.scale != channel->mode.scale)
		mode.restart = 1;
	else if (mode.format != channel->mode.format)
		mode.restart = 1;
	channel->mode = mode;
	(void) s2255_set_mode(channel, &mode);
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
		case SCALE_4CIFSI:
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
		case SCALE_4CIFSI:
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
	if ((mode->color & MASK_COLOR) != COLOR_Y8) {
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

static void s2255_print_cfg(struct s2255_dev *sdev, struct s2255_mode *mode)
{
	struct device *dev = &sdev->udev->dev;
	dev_info(dev, "------------------------------------------------\n");
	dev_info(dev, "format: %d\nscale %d\n", mode->format, mode->scale);
	dev_info(dev, "fdec: %d\ncolor %d\n", mode->fdec, mode->color);
	dev_info(dev, "bright: 0x%x\n", mode->bright);
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
static int s2255_set_mode(struct s2255_channel *channel,
			  struct s2255_mode *mode)
{
	int res;
	__le32 *buffer;
	unsigned long chn_rev;
	struct s2255_dev *dev = to_s2255_dev(channel->vdev.v4l2_dev);
	chn_rev = G_chnmap[channel->idx];
	dprintk(3, "%s channel: %d\n", __func__, channel->idx);
	/* if JPEG, set the quality */
	if ((mode->color & MASK_COLOR) == COLOR_JPG) {
		mode->color &= ~MASK_COLOR;
		mode->color |= COLOR_JPG;
		mode->color &= ~MASK_JPG_QUALITY;
		mode->color |= (channel->jc.quality << 8);
	}
	/* save the mode */
	channel->mode = *mode;
	channel->req_image_size = get_transfer_size(mode);
	dprintk(1, "%s: reqsize %ld\n", __func__, channel->req_image_size);
	buffer = kzalloc(512, GFP_KERNEL);
	if (buffer == NULL) {
		dev_err(&dev->udev->dev, "out of mem\n");
		return -ENOMEM;
	}
	/* set the mode */
	buffer[0] = IN_DATA_TOKEN;
	buffer[1] = (__le32) cpu_to_le32(chn_rev);
	buffer[2] = CMD_SET_MODE;
	memcpy(&buffer[3], &channel->mode, sizeof(struct s2255_mode));
	channel->setmode_ready = 0;
	res = s2255_write_config(dev->udev, (unsigned char *)buffer, 512);
	if (debug)
		s2255_print_cfg(dev, mode);
	kfree(buffer);
	/* wait at least 3 frames before continuing */
	if (mode->restart) {
		wait_event_timeout(channel->wait_setmode,
				   (channel->setmode_ready != 0),
				   msecs_to_jiffies(S2255_SETMODE_TIMEOUT));
		if (channel->setmode_ready != 1) {
			printk(KERN_DEBUG "s2255: no set mode response\n");
			res = -EFAULT;
		}
	}
	/* clear the restart flag */
	channel->mode.restart = 0;
	dprintk(1, "%s chn %d, result: %d\n", __func__, channel->idx, res);
	return res;
}

static int s2255_cmd_status(struct s2255_channel *channel, u32 *pstatus)
{
	int res;
	__le32 *buffer;
	u32 chn_rev;
	struct s2255_dev *dev = to_s2255_dev(channel->vdev.v4l2_dev);
	chn_rev = G_chnmap[channel->idx];
	dprintk(4, "%s chan %d\n", __func__, channel->idx);
	buffer = kzalloc(512, GFP_KERNEL);
	if (buffer == NULL) {
		dev_err(&dev->udev->dev, "out of mem\n");
		return -ENOMEM;
	}
	/* form the get vid status command */
	buffer[0] = IN_DATA_TOKEN;
	buffer[1] = (__le32) cpu_to_le32(chn_rev);
	buffer[2] = CMD_STATUS;
	*pstatus = 0;
	channel->vidstatus_ready = 0;
	res = s2255_write_config(dev->udev, (unsigned char *)buffer, 512);
	kfree(buffer);
	wait_event_timeout(channel->wait_vidstatus,
			   (channel->vidstatus_ready != 0),
			   msecs_to_jiffies(S2255_VIDSTATUS_TIMEOUT));
	if (channel->vidstatus_ready != 1) {
		printk(KERN_DEBUG "s2255: no vidstatus response\n");
		res = -EFAULT;
	}
	*pstatus = channel->vidstatus;
	dprintk(4, "%s, vid status %d\n", __func__, *pstatus);
	return res;
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	int res;
	struct s2255_fh *fh = priv;
	struct s2255_dev *dev = fh->dev;
	struct s2255_channel *channel = fh->channel;
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

	if (!res_get(fh)) {
		s2255_dev_err(&dev->udev->dev, "stream busy\n");
		return -EBUSY;
	}
	channel->last_frame = -1;
	channel->bad_payload = 0;
	channel->cur_frame = 0;
	channel->frame_count = 0;
	for (j = 0; j < SYS_FRAMES; j++) {
		channel->buffer.frame[j].ulState = S2255_READ_IDLE;
		channel->buffer.frame[j].cur_size = 0;
	}
	res = videobuf_streamon(&fh->vb_vidq);
	if (res == 0) {
		s2255_start_acquire(channel);
		channel->b_acquire = 1;
	} else
		res_free(fh);

	return res;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct s2255_fh *fh = priv;
	dprintk(4, "%s\n, channel: %d", __func__, fh->channel->idx);
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		printk(KERN_ERR "invalid fh type0\n");
		return -EINVAL;
	}
	if (i != fh->type) {
		printk(KERN_ERR "invalid type i\n");
		return -EINVAL;
	}
	s2255_stop_acquire(fh->channel);
	videobuf_streamoff(&fh->vb_vidq);
	res_free(fh);
	return 0;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id *i)
{
	struct s2255_fh *fh = priv;
	struct s2255_mode mode;
	struct videobuf_queue *q = &fh->vb_vidq;
	int ret = 0;
	mutex_lock(&q->vb_lock);
	if (videobuf_queue_is_busy(q)) {
		dprintk(1, "queue busy\n");
		ret = -EBUSY;
		goto out_s_std;
	}
	if (res_locked(fh)) {
		dprintk(1, "can't change standard after started\n");
		ret = -EBUSY;
		goto out_s_std;
	}
	mode = fh->channel->mode;
	if (*i & V4L2_STD_NTSC) {
		dprintk(4, "%s NTSC\n", __func__);
		/* if changing format, reset frame decimation/intervals */
		if (mode.format != FORMAT_NTSC) {
			mode.restart = 1;
			mode.format = FORMAT_NTSC;
			mode.fdec = FDEC_1;
		}
	} else if (*i & V4L2_STD_PAL) {
		dprintk(4, "%s PAL\n", __func__);
		if (mode.format != FORMAT_PAL) {
			mode.restart = 1;
			mode.format = FORMAT_PAL;
			mode.fdec = FDEC_1;
		}
	} else {
		ret = -EINVAL;
	}
	if (mode.restart)
		s2255_set_mode(fh->channel, &mode);
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
	struct s2255_fh *fh = priv;
	struct s2255_dev *dev = fh->dev;
	struct s2255_channel *channel = fh->channel;
	u32 status = 0;
	if (inp->index != 0)
		return -EINVAL;
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = S2255_NORMS;
	inp->status = 0;
	if (dev->dsp_fw_ver >= S2255_MIN_DSP_STATUS) {
		int rc;
		rc = s2255_cmd_status(fh->channel, &status);
		dprintk(4, "s2255_cmd_status rc: %d status %x\n", rc, status);
		if (rc == 0)
			inp->status =  (status & 0x01) ? 0
				: V4L2_IN_ST_NO_SIGNAL;
	}
	switch (dev->pid) {
	case 0x2255:
	default:
		strlcpy(inp->name, "Composite", sizeof(inp->name));
		break;
	case 0x2257:
		strlcpy(inp->name, (channel->idx < 2) ? "Composite" : "S-Video",
			sizeof(inp->name));
		break;
	}
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
	struct s2255_fh *fh = priv;
	struct s2255_channel *channel = fh->channel;
	struct s2255_dev *dev = fh->dev;
	switch (qc->id) {
	case V4L2_CID_BRIGHTNESS:
		v4l2_ctrl_query_fill(qc, -127, 127, 1, DEF_BRIGHT);
		break;
	case V4L2_CID_CONTRAST:
		v4l2_ctrl_query_fill(qc, 0, 255, 1, DEF_CONTRAST);
		break;
	case V4L2_CID_SATURATION:
		v4l2_ctrl_query_fill(qc, 0, 255, 1, DEF_SATURATION);
		break;
	case V4L2_CID_HUE:
		v4l2_ctrl_query_fill(qc, 0, 255, 1, DEF_HUE);
		break;
	case V4L2_CID_PRIVATE_COLORFILTER:
		if (dev->dsp_fw_ver < S2255_MIN_DSP_COLORFILTER)
			return -EINVAL;
		if ((dev->pid == 0x2257) && (channel->idx > 1))
			return -EINVAL;
		strlcpy(qc->name, "Color Filter", sizeof(qc->name));
		qc->type = V4L2_CTRL_TYPE_MENU;
		qc->minimum = 0;
		qc->maximum = 1;
		qc->step = 1;
		qc->default_value = 1;
		qc->flags = 0;
		break;
	default:
		return -EINVAL;
	}
	dprintk(4, "%s, id %d\n", __func__, qc->id);
	return 0;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct s2255_fh *fh = priv;
	struct s2255_dev *dev = fh->dev;
	struct s2255_channel *channel = fh->channel;
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = channel->mode.bright;
		break;
	case V4L2_CID_CONTRAST:
		ctrl->value = channel->mode.contrast;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = channel->mode.saturation;
		break;
	case V4L2_CID_HUE:
		ctrl->value = channel->mode.hue;
		break;
	case V4L2_CID_PRIVATE_COLORFILTER:
		if (dev->dsp_fw_ver < S2255_MIN_DSP_COLORFILTER)
			return -EINVAL;
		if ((dev->pid == 0x2257) && (channel->idx > 1))
			return -EINVAL;
		ctrl->value = !((channel->mode.color & MASK_INPUT_TYPE) >> 16);
		break;
	default:
		return -EINVAL;
	}
	dprintk(4, "%s, id %d val %d\n", __func__, ctrl->id, ctrl->value);
	return 0;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct s2255_fh *fh = priv;
	struct s2255_channel *channel = fh->channel;
	struct s2255_dev *dev = to_s2255_dev(channel->vdev.v4l2_dev);
	struct s2255_mode mode;
	mode = channel->mode;
	dprintk(4, "%s\n", __func__);
	/* update the mode to the corresponding value */
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		mode.bright = ctrl->value;
		break;
	case V4L2_CID_CONTRAST:
		mode.contrast = ctrl->value;
		break;
	case V4L2_CID_HUE:
		mode.hue = ctrl->value;
		break;
	case V4L2_CID_SATURATION:
		mode.saturation = ctrl->value;
		break;
	case V4L2_CID_PRIVATE_COLORFILTER:
		if (dev->dsp_fw_ver < S2255_MIN_DSP_COLORFILTER)
			return -EINVAL;
		if ((dev->pid == 0x2257) && (channel->idx > 1))
			return -EINVAL;
		mode.color &= ~MASK_INPUT_TYPE;
		mode.color |= ((ctrl->value ? 0 : 1) << 16);
		break;
	default:
		return -EINVAL;
	}
	mode.restart = 0;
	/* set mode here.  Note: stream does not need restarted.
	   some V4L programs restart stream unnecessarily
	   after a s_crtl.
	*/
	s2255_set_mode(fh->channel, &mode);
	return 0;
}

static int vidioc_g_jpegcomp(struct file *file, void *priv,
			 struct v4l2_jpegcompression *jc)
{
	struct s2255_fh *fh = priv;
	struct s2255_channel *channel = fh->channel;
	*jc = channel->jc;
	dprintk(2, "%s: quality %d\n", __func__, jc->quality);
	return 0;
}

static int vidioc_s_jpegcomp(struct file *file, void *priv,
			 struct v4l2_jpegcompression *jc)
{
	struct s2255_fh *fh = priv;
	struct s2255_channel *channel = fh->channel;
	if (jc->quality < 0 || jc->quality > 100)
		return -EINVAL;
	channel->jc.quality = jc->quality;
	dprintk(2, "%s: quality %d\n", __func__, jc->quality);
	return 0;
}

static int vidioc_g_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *sp)
{
	struct s2255_fh *fh = priv;
	__u32 def_num, def_dem;
	struct s2255_channel *channel = fh->channel;
	if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	memset(sp, 0, sizeof(struct v4l2_streamparm));
	sp->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	sp->parm.capture.capturemode = channel->cap_parm.capturemode;
	def_num = (channel->mode.format == FORMAT_NTSC) ? 1001 : 1000;
	def_dem = (channel->mode.format == FORMAT_NTSC) ? 30000 : 25000;
	sp->parm.capture.timeperframe.denominator = def_dem;
	switch (channel->mode.fdec) {
	default:
	case FDEC_1:
		sp->parm.capture.timeperframe.numerator = def_num;
		break;
	case FDEC_2:
		sp->parm.capture.timeperframe.numerator = def_num * 2;
		break;
	case FDEC_3:
		sp->parm.capture.timeperframe.numerator = def_num * 3;
		break;
	case FDEC_5:
		sp->parm.capture.timeperframe.numerator = def_num * 5;
		break;
	}
	dprintk(4, "%s capture mode, %d timeperframe %d/%d\n", __func__,
		sp->parm.capture.capturemode,
		sp->parm.capture.timeperframe.numerator,
		sp->parm.capture.timeperframe.denominator);
	return 0;
}

static int vidioc_s_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *sp)
{
	struct s2255_fh *fh = priv;
	struct s2255_channel *channel = fh->channel;
	struct s2255_mode mode;
	int fdec = FDEC_1;
	__u32 def_num, def_dem;
	if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	mode = channel->mode;
	/* high quality capture mode requires a stream restart */
	if (channel->cap_parm.capturemode
	    != sp->parm.capture.capturemode && res_locked(fh))
		return -EBUSY;
	def_num = (mode.format == FORMAT_NTSC) ? 1001 : 1000;
	def_dem = (mode.format == FORMAT_NTSC) ? 30000 : 25000;
	if (def_dem != sp->parm.capture.timeperframe.denominator)
		sp->parm.capture.timeperframe.numerator = def_num;
	else if (sp->parm.capture.timeperframe.numerator <= def_num)
		sp->parm.capture.timeperframe.numerator = def_num;
	else if (sp->parm.capture.timeperframe.numerator <= (def_num * 2)) {
		sp->parm.capture.timeperframe.numerator = def_num * 2;
		fdec = FDEC_2;
	} else if (sp->parm.capture.timeperframe.numerator <= (def_num * 3)) {
		sp->parm.capture.timeperframe.numerator = def_num * 3;
		fdec = FDEC_3;
	} else {
		sp->parm.capture.timeperframe.numerator = def_num * 5;
		fdec = FDEC_5;
	}
	mode.fdec = fdec;
	sp->parm.capture.timeperframe.denominator = def_dem;
	s2255_set_mode(channel, &mode);
	dprintk(4, "%s capture mode, %d timeperframe %d/%d, fdec %d\n",
		__func__,
		sp->parm.capture.capturemode,
		sp->parm.capture.timeperframe.numerator,
		sp->parm.capture.timeperframe.denominator, fdec);
	return 0;
}

static int vidioc_enum_frameintervals(struct file *file, void *priv,
			    struct v4l2_frmivalenum *fe)
{
	int is_ntsc = 0;
#define NUM_FRAME_ENUMS 4
	int frm_dec[NUM_FRAME_ENUMS] = {1, 2, 3, 5};
	if (fe->index < 0 || fe->index >= NUM_FRAME_ENUMS)
		return -EINVAL;
	switch (fe->width) {
	case 640:
		if (fe->height != 240 && fe->height != 480)
			return -EINVAL;
		is_ntsc = 1;
		break;
	case 320:
		if (fe->height != 240)
			return -EINVAL;
		is_ntsc = 1;
		break;
	case 704:
		if (fe->height != 288 && fe->height != 576)
			return -EINVAL;
		break;
	case 352:
		if (fe->height != 288)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	fe->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fe->discrete.denominator = is_ntsc ? 30000 : 25000;
	fe->discrete.numerator = (is_ntsc ? 1001 : 1000) * frm_dec[fe->index];
	dprintk(4, "%s discrete %d/%d\n", __func__, fe->discrete.numerator,
		fe->discrete.denominator);
	return 0;
}

static int s2255_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct s2255_channel *channel = video_drvdata(file);
	struct s2255_dev *dev = to_s2255_dev(vdev->v4l2_dev);
	struct s2255_fh *fh;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int state;
	dprintk(1, "s2255: open called (dev=%s)\n",
		video_device_node_name(vdev));
	/*
	 * open lock necessary to prevent multiple instances
	 * of v4l-conf (or other programs) from simultaneously
	 * reloading firmware.
	 */
	mutex_lock(&dev->open_lock);
	state = atomic_read(&dev->fw_data->fw_state);
	switch (state) {
	case S2255_FW_DISCONNECTING:
		mutex_unlock(&dev->open_lock);
		return -ENODEV;
	case S2255_FW_FAILED:
		s2255_dev_err(&dev->udev->dev,
			"firmware load failed. retrying.\n");
		s2255_fwload_start(dev, 1);
		wait_event_timeout(dev->fw_data->wait_fw,
				   ((atomic_read(&dev->fw_data->fw_state)
				     == S2255_FW_SUCCESS) ||
				    (atomic_read(&dev->fw_data->fw_state)
				     == S2255_FW_DISCONNECTING)),
				   msecs_to_jiffies(S2255_LOAD_TIMEOUT));
		/* state may have changed, re-read */
		state = atomic_read(&dev->fw_data->fw_state);
		break;
	case S2255_FW_NOTLOADED:
	case S2255_FW_LOADED_DSPWAIT:
		/* give S2255_LOAD_TIMEOUT time for firmware to load in case
		   driver loaded and then device immediately opened */
		printk(KERN_INFO "%s waiting for firmware load\n", __func__);
		wait_event_timeout(dev->fw_data->wait_fw,
				   ((atomic_read(&dev->fw_data->fw_state)
				     == S2255_FW_SUCCESS) ||
				    (atomic_read(&dev->fw_data->fw_state)
				     == S2255_FW_DISCONNECTING)),
				   msecs_to_jiffies(S2255_LOAD_TIMEOUT));
		/* state may have changed, re-read */
		state = atomic_read(&dev->fw_data->fw_state);
		break;
	case S2255_FW_SUCCESS:
	default:
		break;
	}
	/* state may have changed in above switch statement */
	switch (state) {
	case S2255_FW_SUCCESS:
		break;
	case S2255_FW_FAILED:
		printk(KERN_INFO "2255 firmware load failed.\n");
		mutex_unlock(&dev->open_lock);
		return -ENODEV;
	case S2255_FW_DISCONNECTING:
		printk(KERN_INFO "%s: disconnecting\n", __func__);
		mutex_unlock(&dev->open_lock);
		return -ENODEV;
	case S2255_FW_LOADED_DSPWAIT:
	case S2255_FW_NOTLOADED:
		printk(KERN_INFO "%s: firmware not loaded yet"
		       "please try again later\n",
		       __func__);
		/*
		 * Timeout on firmware load means device unusable.
		 * Set firmware failure state.
		 * On next s2255_open the firmware will be reloaded.
		 */
		atomic_set(&dev->fw_data->fw_state,
			   S2255_FW_FAILED);
		mutex_unlock(&dev->open_lock);
		return -EAGAIN;
	default:
		printk(KERN_INFO "%s: unknown state\n", __func__);
		mutex_unlock(&dev->open_lock);
		return -EFAULT;
	}
	mutex_unlock(&dev->open_lock);
	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;
	file->private_data = fh;
	fh->dev = dev;
	fh->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fh->channel = channel;
	if (!channel->configured) {
		/* configure channel to default state */
		channel->fmt = &formats[0];
		s2255_set_mode(channel, &channel->mode);
		channel->configured = 1;
	}
	dprintk(1, "%s: dev=%s type=%s\n", __func__,
		video_device_node_name(vdev), v4l2_type_names[type]);
	dprintk(2, "%s: fh=0x%08lx, dev=0x%08lx, vidq=0x%08lx\n", __func__,
		(unsigned long)fh, (unsigned long)dev,
		(unsigned long)&channel->vidq);
	dprintk(4, "%s: list_empty active=%d\n", __func__,
		list_empty(&channel->vidq.active));
	videobuf_queue_vmalloc_init(&fh->vb_vidq, &s2255_video_qops,
				    NULL, &dev->slock,
				    fh->type,
				    V4L2_FIELD_INTERLACED,
				    sizeof(struct s2255_buffer),
				    fh, vdev->lock);
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

static void s2255_destroy(struct s2255_dev *dev)
{
	/* board shutdown stops the read pipe if it is running */
	s2255_board_shutdown(dev);
	/* make sure firmware still not trying to load */
	del_timer(&dev->timer);  /* only started in .probe and .open */
	if (dev->fw_data->fw_urb) {
		usb_kill_urb(dev->fw_data->fw_urb);
		usb_free_urb(dev->fw_data->fw_urb);
		dev->fw_data->fw_urb = NULL;
	}
	release_firmware(dev->fw_data->fw);
	kfree(dev->fw_data->pfw_data);
	kfree(dev->fw_data);
	/* reset the DSP so firmware can be reloaded next time */
	s2255_reset_dsppower(dev);
	mutex_destroy(&dev->open_lock);
	mutex_destroy(&dev->lock);
	usb_put_dev(dev->udev);
	v4l2_device_unregister(&dev->v4l2_dev);
	dprintk(1, "%s", __func__);
	kfree(dev);
}

static int s2255_release(struct file *file)
{
	struct s2255_fh *fh = file->private_data;
	struct s2255_dev *dev = fh->dev;
	struct video_device *vdev = video_devdata(file);
	struct s2255_channel *channel = fh->channel;
	if (!dev)
		return -ENODEV;
	/* turn off stream */
	if (res_check(fh)) {
		if (channel->b_acquire)
			s2255_stop_acquire(fh->channel);
		videobuf_streamoff(&fh->vb_vidq);
		res_free(fh);
	}
	videobuf_mmap_free(&fh->vb_vidq);
	dprintk(1, "%s (dev=%s)\n", __func__, video_device_node_name(vdev));
	kfree(fh);
	return 0;
}

static int s2255_mmap_v4l(struct file *file, struct vm_area_struct *vma)
{
	struct s2255_fh *fh = file->private_data;
	int ret;

	if (!fh)
		return -ENODEV;
	dprintk(4, "%s, vma=0x%08lx\n", __func__, (unsigned long)vma);
	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);
	dprintk(4, "%s vma start=0x%08lx, size=%ld, ret=%d\n", __func__,
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end - (unsigned long)vma->vm_start, ret);
	return ret;
}

static const struct v4l2_file_operations s2255_fops_v4l = {
	.owner = THIS_MODULE,
	.open = s2255_open,
	.release = s2255_release,
	.poll = s2255_poll,
	.unlocked_ioctl = video_ioctl2,	/* V4L2 ioctl handler */
	.mmap = s2255_mmap_v4l,
};

static const struct v4l2_ioctl_ops s2255_ioctl_ops = {
	.vidioc_querymenu = vidioc_querymenu,
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
	.vidioc_s_jpegcomp = vidioc_s_jpegcomp,
	.vidioc_g_jpegcomp = vidioc_g_jpegcomp,
	.vidioc_s_parm = vidioc_s_parm,
	.vidioc_g_parm = vidioc_g_parm,
	.vidioc_enum_frameintervals = vidioc_enum_frameintervals,
};

static void s2255_video_device_release(struct video_device *vdev)
{
	struct s2255_dev *dev = to_s2255_dev(vdev->v4l2_dev);
	dprintk(4, "%s, chnls: %d \n", __func__,
		atomic_read(&dev->num_channels));
	if (atomic_dec_and_test(&dev->num_channels))
		s2255_destroy(dev);
	return;
}

static struct video_device template = {
	.name = "s2255v",
	.fops = &s2255_fops_v4l,
	.ioctl_ops = &s2255_ioctl_ops,
	.release = s2255_video_device_release,
	.tvnorms = S2255_NORMS,
	.current_norm = V4L2_STD_NTSC_M,
};

static int s2255_probe_v4l(struct s2255_dev *dev)
{
	int ret;
	int i;
	int cur_nr = video_nr;
	struct s2255_channel *channel;
	ret = v4l2_device_register(&dev->interface->dev, &dev->v4l2_dev);
	if (ret)
		return ret;
	/* initialize all video 4 linux */
	/* register 4 video devices */
	for (i = 0; i < MAX_CHANNELS; i++) {
		channel = &dev->channel[i];
		INIT_LIST_HEAD(&channel->vidq.active);
		channel->vidq.dev = dev;
		/* register 4 video devices */
		channel->vdev = template;
		channel->vdev.lock = &dev->lock;
		/* Locking in file operations other than ioctl should be done
		   by the driver, not the V4L2 core.
		   This driver needs auditing so that this flag can be removed. */
		set_bit(V4L2_FL_LOCK_ALL_FOPS, &channel->vdev.flags);
		channel->vdev.v4l2_dev = &dev->v4l2_dev;
		video_set_drvdata(&channel->vdev, channel);
		if (video_nr == -1)
			ret = video_register_device(&channel->vdev,
						    VFL_TYPE_GRABBER,
						    video_nr);
		else
			ret = video_register_device(&channel->vdev,
						    VFL_TYPE_GRABBER,
						    cur_nr + i);

		if (ret) {
			dev_err(&dev->udev->dev,
				"failed to register video device!\n");
			break;
		}
		atomic_inc(&dev->num_channels);
		v4l2_info(&dev->v4l2_dev, "V4L2 device registered as %s\n",
			  video_device_node_name(&channel->vdev));

	}
	printk(KERN_INFO "Sensoray 2255 V4L driver Revision: %s\n",
	       S2255_VERSION);
	/* if no channels registered, return error and probe will fail*/
	if (atomic_read(&dev->num_channels) == 0) {
		v4l2_device_unregister(&dev->v4l2_dev);
		return ret;
	}
	if (atomic_read(&dev->num_channels) != MAX_CHANNELS)
		printk(KERN_WARNING "s2255: Not all channels available.\n");
	return 0;
}

/* this function moves the usb stream read pipe data
 * into the system buffers.
 * returns 0 on success, EAGAIN if more data to process( call this
 * function again).
 *
 * Received frame structure:
 * bytes 0-3:  marker : 0x2255DA4AL (S2255_MARKER_FRAME)
 * bytes 4-7:  channel: 0-3
 * bytes 8-11: payload size:  size of the frame
 * bytes 12-payloadsize+12:  frame data
 */
static int save_frame(struct s2255_dev *dev, struct s2255_pipeinfo *pipe_info)
{
	char *pdest;
	u32 offset = 0;
	int bframe = 0;
	char *psrc;
	unsigned long copy_size;
	unsigned long size;
	s32 idx = -1;
	struct s2255_framei *frm;
	unsigned char *pdata;
	struct s2255_channel *channel;
	dprintk(100, "buffer to user\n");
	channel = &dev->channel[dev->cc];
	idx = channel->cur_frame;
	frm = &channel->buffer.frame[idx];
	if (frm->ulState == S2255_READ_IDLE) {
		int jj;
		unsigned int cc;
		__le32 *pdword; /*data from dsp is little endian */
		int payload;
		/* search for marker codes */
		pdata = (unsigned char *)pipe_info->transfer_buffer;
		pdword = (__le32 *)pdata;
		for (jj = 0; jj < (pipe_info->cur_transfer_size - 12); jj++) {
			switch (*pdword) {
			case S2255_MARKER_FRAME:
				dprintk(4, "found frame marker at offset:"
					" %d [%x %x]\n", jj, pdata[0],
					pdata[1]);
				offset = jj + PREFIX_SIZE;
				bframe = 1;
				cc = le32_to_cpu(pdword[1]);
				if (cc >= MAX_CHANNELS) {
					printk(KERN_ERR
					       "bad channel\n");
					return -EINVAL;
				}
				/* reverse it */
				dev->cc = G_chnmap[cc];
				channel = &dev->channel[dev->cc];
				payload =  le32_to_cpu(pdword[3]);
				if (payload > channel->req_image_size) {
					channel->bad_payload++;
					/* discard the bad frame */
					return -EINVAL;
				}
				channel->pkt_size = payload;
				channel->jpg_size = le32_to_cpu(pdword[4]);
				break;
			case S2255_MARKER_RESPONSE:

				pdata += DEF_USB_BLOCK;
				jj += DEF_USB_BLOCK;
				if (le32_to_cpu(pdword[1]) >= MAX_CHANNELS)
					break;
				cc = G_chnmap[le32_to_cpu(pdword[1])];
				if (cc >= MAX_CHANNELS)
					break;
				channel = &dev->channel[cc];
				switch (pdword[2]) {
				case S2255_RESPONSE_SETMODE:
					/* check if channel valid */
					/* set mode ready */
					channel->setmode_ready = 1;
					wake_up(&channel->wait_setmode);
					dprintk(5, "setmode ready %d\n", cc);
					break;
				case S2255_RESPONSE_FW:
					dev->chn_ready |= (1 << cc);
					if ((dev->chn_ready & 0x0f) != 0x0f)
						break;
					/* all channels ready */
					printk(KERN_INFO "s2255: fw loaded\n");
					atomic_set(&dev->fw_data->fw_state,
						   S2255_FW_SUCCESS);
					wake_up(&dev->fw_data->wait_fw);
					break;
				case S2255_RESPONSE_STATUS:
					channel->vidstatus = le32_to_cpu(pdword[3]);
					channel->vidstatus_ready = 1;
					wake_up(&channel->wait_vidstatus);
					dprintk(5, "got vidstatus %x chan %d\n",
						le32_to_cpu(pdword[3]), cc);
					break;
				default:
					printk(KERN_INFO "s2255 unknown resp\n");
				}
			default:
				pdata++;
				break;
			}
			if (bframe)
				break;
		} /* for */
		if (!bframe)
			return -EINVAL;
	}
	channel = &dev->channel[dev->cc];
	idx = channel->cur_frame;
	frm = &channel->buffer.frame[idx];
	/* search done.  now find out if should be acquiring on this channel */
	if (!channel->b_acquire) {
		/* we found a frame, but this channel is turned off */
		frm->ulState = S2255_READ_IDLE;
		return -EINVAL;
	}

	if (frm->ulState == S2255_READ_IDLE) {
		frm->ulState = S2255_READ_FRAME;
		frm->cur_size = 0;
	}

	/* skip the marker 512 bytes (and offset if out of sync) */
	psrc = (u8 *)pipe_info->transfer_buffer + offset;


	if (frm->lpvbits == NULL) {
		dprintk(1, "s2255 frame buffer == NULL.%p %p %d %d",
			frm, dev, dev->cc, idx);
		return -ENOMEM;
	}

	pdest = frm->lpvbits + frm->cur_size;

	copy_size = (pipe_info->cur_transfer_size - offset);

	size = channel->pkt_size - PREFIX_SIZE;

	/* sanity check on pdest */
	if ((copy_size + frm->cur_size) < channel->req_image_size)
		memcpy(pdest, psrc, copy_size);

	frm->cur_size += copy_size;
	dprintk(4, "cur_size size %lu size %lu \n", frm->cur_size, size);

	if (frm->cur_size >= size) {
		dprintk(2, "****************[%d]Buffer[%d]full*************\n",
			dev->cc, idx);
		channel->last_frame = channel->cur_frame;
		channel->cur_frame++;
		/* end of system frame ring buffer, start at zero */
		if ((channel->cur_frame == SYS_FRAMES) ||
		    (channel->cur_frame == channel->buffer.dwFrames))
			channel->cur_frame = 0;
		/* frame ready */
		if (channel->b_acquire)
			s2255_got_frame(channel, channel->jpg_size);
		channel->frame_count++;
		frm->ulState = S2255_READ_IDLE;
		frm->cur_size = 0;

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
	if (res != 0)
		dprintk(4, "s2255: read callback failed\n");

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
static int s2255_create_sys_buffers(struct s2255_channel *channel)
{
	unsigned long i;
	unsigned long reqsize;
	dprintk(1, "create sys buffers\n");
	channel->buffer.dwFrames = SYS_FRAMES;
	/* always allocate maximum size(PAL) for system buffers */
	reqsize = SYS_FRAMES_MAXSIZE;

	if (reqsize > SYS_FRAMES_MAXSIZE)
		reqsize = SYS_FRAMES_MAXSIZE;

	for (i = 0; i < SYS_FRAMES; i++) {
		/* allocate the frames */
		channel->buffer.frame[i].lpvbits = vmalloc(reqsize);
		dprintk(1, "valloc %p chan %d, idx %lu, pdata %p\n",
			&channel->buffer.frame[i], channel->idx, i,
			channel->buffer.frame[i].lpvbits);
		channel->buffer.frame[i].size = reqsize;
		if (channel->buffer.frame[i].lpvbits == NULL) {
			printk(KERN_INFO "out of memory.  using less frames\n");
			channel->buffer.dwFrames = i;
			break;
		}
	}

	/* make sure internal states are set */
	for (i = 0; i < SYS_FRAMES; i++) {
		channel->buffer.frame[i].ulState = 0;
		channel->buffer.frame[i].cur_size = 0;
	}

	channel->cur_frame = 0;
	channel->last_frame = -1;
	return 0;
}

static int s2255_release_sys_buffers(struct s2255_channel *channel)
{
	unsigned long i;
	dprintk(1, "release sys buffers\n");
	for (i = 0; i < SYS_FRAMES; i++) {
		if (channel->buffer.frame[i].lpvbits) {
			dprintk(1, "vfree %p\n",
				channel->buffer.frame[i].lpvbits);
			vfree(channel->buffer.frame[i].lpvbits);
		}
		channel->buffer.frame[i].lpvbits = NULL;
	}
	return 0;
}

static int s2255_board_init(struct s2255_dev *dev)
{
	struct s2255_mode mode_def = DEF_MODEI_NTSC_CONT;
	int fw_ver;
	int j;
	struct s2255_pipeinfo *pipe = &dev->pipe;
	dprintk(4, "board init: %p", dev);
	memset(pipe, 0, sizeof(*pipe));
	pipe->dev = dev;
	pipe->cur_transfer_size = S2255_USB_XFER_SIZE;
	pipe->max_transfer_size = S2255_USB_XFER_SIZE;

	pipe->transfer_buffer = kzalloc(pipe->max_transfer_size,
					GFP_KERNEL);
	if (pipe->transfer_buffer == NULL) {
		dprintk(1, "out of memory!\n");
		return -ENOMEM;
	}
	/* query the firmware */
	fw_ver = s2255_get_fx2fw(dev);

	printk(KERN_INFO "s2255: usb firmware version %d.%d\n",
	       (fw_ver >> 8) & 0xff,
	       fw_ver & 0xff);

	if (fw_ver < S2255_CUR_USB_FWVER)
		printk(KERN_INFO "s2255: newer USB firmware available\n");

	for (j = 0; j < MAX_CHANNELS; j++) {
		struct s2255_channel *channel = &dev->channel[j];
		channel->b_acquire = 0;
		channel->mode = mode_def;
		if (dev->pid == 0x2257 && j > 1)
			channel->mode.color |= (1 << 16);
		channel->jc.quality = S2255_DEF_JPEG_QUAL;
		channel->width = LINE_SZ_4CIFS_NTSC;
		channel->height = NUM_LINES_4CIFS_NTSC * 2;
		channel->fmt = &formats[0];
		channel->mode.restart = 1;
		channel->req_image_size = get_transfer_size(&mode_def);
		channel->frame_count = 0;
		/* create the system buffers */
		s2255_create_sys_buffers(channel);
	}
	/* start read pipe */
	s2255_start_readpipe(dev);
	dprintk(1, "%s: success\n", __func__);
	return 0;
}

static int s2255_board_shutdown(struct s2255_dev *dev)
{
	u32 i;
	dprintk(1, "%s: dev: %p", __func__,  dev);

	for (i = 0; i < MAX_CHANNELS; i++) {
		if (dev->channel[i].b_acquire)
			s2255_stop_acquire(&dev->channel[i]);
	}
	s2255_stop_readpipe(dev);
	for (i = 0; i < MAX_CHANNELS; i++)
		s2255_release_sys_buffers(&dev->channel[i]);
	/* release transfer buffer */
	kfree(dev->pipe.transfer_buffer);
	return 0;
}

static void read_pipe_completion(struct urb *purb)
{
	struct s2255_pipeinfo *pipe_info;
	struct s2255_dev *dev;
	int status;
	int pipe;
	pipe_info = purb->context;
	dprintk(100, "%s: urb:%p, status %d\n", __func__, purb,
		purb->status);
	if (pipe_info == NULL) {
		dev_err(&purb->dev->dev, "no context!\n");
		return;
	}

	dev = pipe_info->dev;
	if (dev == NULL) {
		dev_err(&purb->dev->dev, "no context!\n");
		return;
	}
	status = purb->status;
	/* if shutting down, do not resubmit, exit immediately */
	if (status == -ESHUTDOWN) {
		dprintk(2, "%s: err shutdown\n", __func__);
		pipe_info->err_count++;
		return;
	}

	if (pipe_info->state == 0) {
		dprintk(2, "%s: exiting USB pipe", __func__);
		return;
	}

	if (status == 0)
		s2255_read_video_callback(dev, pipe_info);
	else {
		pipe_info->err_count++;
		dprintk(1, "%s: failed URB %d\n", __func__, status);
	}

	pipe = usb_rcvbulkpipe(dev->udev, dev->read_endpoint);
	/* reuse urb */
	usb_fill_bulk_urb(pipe_info->stream_urb, dev->udev,
			  pipe,
			  pipe_info->transfer_buffer,
			  pipe_info->cur_transfer_size,
			  read_pipe_completion, pipe_info);

	if (pipe_info->state != 0) {
		if (usb_submit_urb(pipe_info->stream_urb, GFP_ATOMIC)) {
			dev_err(&dev->udev->dev, "error submitting urb\n");
		}
	} else {
		dprintk(2, "%s :complete state 0\n", __func__);
	}
	return;
}

static int s2255_start_readpipe(struct s2255_dev *dev)
{
	int pipe;
	int retval;
	struct s2255_pipeinfo *pipe_info = &dev->pipe;
	pipe = usb_rcvbulkpipe(dev->udev, dev->read_endpoint);
	dprintk(2, "%s: IN %d\n", __func__, dev->read_endpoint);
	pipe_info->state = 1;
	pipe_info->err_count = 0;
	pipe_info->stream_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pipe_info->stream_urb) {
		dev_err(&dev->udev->dev,
			"ReadStream: Unable to alloc URB\n");
		return -ENOMEM;
	}
	/* transfer buffer allocated in board_init */
	usb_fill_bulk_urb(pipe_info->stream_urb, dev->udev,
			  pipe,
			  pipe_info->transfer_buffer,
			  pipe_info->cur_transfer_size,
			  read_pipe_completion, pipe_info);
	retval = usb_submit_urb(pipe_info->stream_urb, GFP_KERNEL);
	if (retval) {
		printk(KERN_ERR "s2255: start read pipe failed\n");
		return retval;
	}
	return 0;
}

/* starts acquisition process */
static int s2255_start_acquire(struct s2255_channel *channel)
{
	unsigned char *buffer;
	int res;
	unsigned long chn_rev;
	int j;
	struct s2255_dev *dev = to_s2255_dev(channel->vdev.v4l2_dev);
	chn_rev = G_chnmap[channel->idx];
	buffer = kzalloc(512, GFP_KERNEL);
	if (buffer == NULL) {
		dev_err(&dev->udev->dev, "out of mem\n");
		return -ENOMEM;
	}

	channel->last_frame = -1;
	channel->bad_payload = 0;
	channel->cur_frame = 0;
	for (j = 0; j < SYS_FRAMES; j++) {
		channel->buffer.frame[j].ulState = 0;
		channel->buffer.frame[j].cur_size = 0;
	}

	/* send the start command */
	*(__le32 *) buffer = IN_DATA_TOKEN;
	*((__le32 *) buffer + 1) = (__le32) cpu_to_le32(chn_rev);
	*((__le32 *) buffer + 2) = CMD_START;
	res = s2255_write_config(dev->udev, (unsigned char *)buffer, 512);
	if (res != 0)
		dev_err(&dev->udev->dev, "CMD_START error\n");

	dprintk(2, "start acquire exit[%d] %d \n", channel->idx, res);
	kfree(buffer);
	return 0;
}

static int s2255_stop_acquire(struct s2255_channel *channel)
{
	unsigned char *buffer;
	int res;
	unsigned long chn_rev;
	struct s2255_dev *dev = to_s2255_dev(channel->vdev.v4l2_dev);
	chn_rev = G_chnmap[channel->idx];
	buffer = kzalloc(512, GFP_KERNEL);
	if (buffer == NULL) {
		dev_err(&dev->udev->dev, "out of mem\n");
		return -ENOMEM;
	}
	/* send the stop command */
	*(__le32 *) buffer = IN_DATA_TOKEN;
	*((__le32 *) buffer + 1) = (__le32) cpu_to_le32(chn_rev);
	*((__le32 *) buffer + 2) = CMD_STOP;
	res = s2255_write_config(dev->udev, (unsigned char *)buffer, 512);
	if (res != 0)
		dev_err(&dev->udev->dev, "CMD_STOP error\n");
	kfree(buffer);
	channel->b_acquire = 0;
	dprintk(4, "%s: chn %d, res %d\n", __func__, channel->idx, res);
	return res;
}

static void s2255_stop_readpipe(struct s2255_dev *dev)
{
	struct s2255_pipeinfo *pipe = &dev->pipe;

	pipe->state = 0;
	if (pipe->stream_urb) {
		/* cancel urb */
		usb_kill_urb(pipe->stream_urb);
		usb_free_urb(pipe->stream_urb);
		pipe->stream_urb = NULL;
	}
	dprintk(4, "%s", __func__);
	return;
}

static void s2255_fwload_start(struct s2255_dev *dev, int reset)
{
	if (reset)
		s2255_reset_dsppower(dev);
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
	__le32 *pdata;
	int fw_size;
	dprintk(2, "%s\n", __func__);
	/* allocate memory for our device state and initialize it to zero */
	dev = kzalloc(sizeof(struct s2255_dev), GFP_KERNEL);
	if (dev == NULL) {
		s2255_dev_err(&interface->dev, "out of memory\n");
		return -ENOMEM;
	}
	atomic_set(&dev->num_channels, 0);
	dev->pid = id->idProduct;
	dev->fw_data = kzalloc(sizeof(struct s2255_fw), GFP_KERNEL);
	if (!dev->fw_data)
		goto errorFWDATA1;
	mutex_init(&dev->lock);
	mutex_init(&dev->open_lock);
	/* grab usb_device and save it */
	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	if (dev->udev == NULL) {
		dev_err(&interface->dev, "null usb device\n");
		retval = -ENODEV;
		goto errorUDEV;
	}
	dprintk(1, "dev: %p, udev %p interface %p\n", dev,
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
		dev_err(&interface->dev, "Could not find bulk-in endpoint\n");
		goto errorEP;
	}
	init_timer(&dev->timer);
	dev->timer.function = s2255_timer;
	dev->timer.data = (unsigned long)dev->fw_data;
	init_waitqueue_head(&dev->fw_data->wait_fw);
	for (i = 0; i < MAX_CHANNELS; i++) {
		struct s2255_channel *channel = &dev->channel[i];
		dev->channel[i].idx = i;
		init_waitqueue_head(&channel->wait_setmode);
		init_waitqueue_head(&channel->wait_vidstatus);
	}

	dev->fw_data->fw_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->fw_data->fw_urb) {
		dev_err(&interface->dev, "out of memory!\n");
		goto errorFWURB;
	}

	dev->fw_data->pfw_data = kzalloc(CHUNK_SIZE, GFP_KERNEL);
	if (!dev->fw_data->pfw_data) {
		dev_err(&interface->dev, "out of memory!\n");
		goto errorFWDATA2;
	}
	/* load the first chunk */
	if (request_firmware(&dev->fw_data->fw,
			     FIRMWARE_FILE_NAME, &dev->udev->dev)) {
		printk(KERN_ERR "sensoray 2255 failed to get firmware\n");
		goto errorREQFW;
	}
	/* check the firmware is valid */
	fw_size = dev->fw_data->fw->size;
	pdata = (__le32 *) &dev->fw_data->fw->data[fw_size - 8];

	if (*pdata != S2255_FW_MARKER) {
		printk(KERN_INFO "Firmware invalid.\n");
		retval = -ENODEV;
		goto errorFWMARKER;
	} else {
		/* make sure firmware is the latest */
		__le32 *pRel;
		pRel = (__le32 *) &dev->fw_data->fw->data[fw_size - 4];
		printk(KERN_INFO "s2255 dsp fw version %x\n", *pRel);
		dev->dsp_fw_ver = le32_to_cpu(*pRel);
		if (dev->dsp_fw_ver < S2255_CUR_DSP_FWVER)
			printk(KERN_INFO "s2255: f2255usb.bin out of date.\n");
		if (dev->pid == 0x2257 &&
				dev->dsp_fw_ver < S2255_MIN_DSP_COLORFILTER)
			printk(KERN_WARNING "s2255: 2257 requires firmware %d"
			       " or above.\n", S2255_MIN_DSP_COLORFILTER);
	}
	usb_reset_device(dev->udev);
	/* load 2255 board specific */
	retval = s2255_board_init(dev);
	if (retval)
		goto errorBOARDINIT;
	spin_lock_init(&dev->slock);
	s2255_fwload_start(dev, 0);
	/* loads v4l specific */
	retval = s2255_probe_v4l(dev);
	if (retval)
		goto errorBOARDINIT;
	dev_info(&interface->dev, "Sensoray 2255 detected\n");
	return 0;
errorBOARDINIT:
	s2255_board_shutdown(dev);
errorFWMARKER:
	release_firmware(dev->fw_data->fw);
errorREQFW:
	kfree(dev->fw_data->pfw_data);
errorFWDATA2:
	usb_free_urb(dev->fw_data->fw_urb);
errorFWURB:
	del_timer(&dev->timer);
errorEP:
	usb_put_dev(dev->udev);
errorUDEV:
	kfree(dev->fw_data);
	mutex_destroy(&dev->open_lock);
	mutex_destroy(&dev->lock);
errorFWDATA1:
	kfree(dev);
	printk(KERN_WARNING "Sensoray 2255 driver load failed: 0x%x\n", retval);
	return retval;
}

/* disconnect routine. when board is removed physically or with rmmod */
static void s2255_disconnect(struct usb_interface *interface)
{
	struct s2255_dev *dev = to_s2255_dev(usb_get_intfdata(interface));
	int i;
	int channels = atomic_read(&dev->num_channels);
	mutex_lock(&dev->lock);
	v4l2_device_disconnect(&dev->v4l2_dev);
	mutex_unlock(&dev->lock);
	/*see comments in the uvc_driver.c usb disconnect function */
	atomic_inc(&dev->num_channels);
	/* unregister each video device. */
	for (i = 0; i < channels; i++)
		video_unregister_device(&dev->channel[i].vdev);
	/* wake up any of our timers */
	atomic_set(&dev->fw_data->fw_state, S2255_FW_DISCONNECTING);
	wake_up(&dev->fw_data->wait_fw);
	for (i = 0; i < MAX_CHANNELS; i++) {
		dev->channel[i].setmode_ready = 1;
		wake_up(&dev->channel[i].wait_setmode);
		dev->channel[i].vidstatus_ready = 1;
		wake_up(&dev->channel[i].wait_vidstatus);
	}
	if (atomic_dec_and_test(&dev->num_channels))
		s2255_destroy(dev);
	dev_info(&interface->dev, "%s\n", __func__);
}

static struct usb_driver s2255_driver = {
	.name = S2255_DRIVER_NAME,
	.probe = s2255_probe,
	.disconnect = s2255_disconnect,
	.id_table = s2255_table,
};

module_usb_driver(s2255_driver);

MODULE_DESCRIPTION("Sensoray 2255 Video for Linux driver");
MODULE_AUTHOR("Dean Anderson (Sensoray Company Inc.)");
MODULE_LICENSE("GPL");
MODULE_VERSION(S2255_VERSION);

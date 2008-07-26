/*
   em28xx-video.c - driver for Empia EM2800/EM2820/2840 USB
		    video capture devices

   Copyright (C) 2005 Ludovico Cavedon <cavedon@sssup.it>
		      Markus Rechberger <mrechberger@gmail.com>
		      Mauro Carvalho Chehab <mchehab@infradead.org>
		      Sascha Sommer <saschasommer@freenet.de>

	Some parts based on SN9C10x PC Camera Controllers GPL driver made
		by Luca Risolia <luca.risolia@studio.unibo.it>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bitmap.h>
#include <linux/usb.h>
#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/mm.h>
#include <linux/mutex.h>

#include "em28xx.h"
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/msp3400.h>
#include <media/tuner.h>

#define DRIVER_AUTHOR "Ludovico Cavedon <cavedon@sssup.it>, " \
		      "Markus Rechberger <mrechberger@gmail.com>, " \
		      "Mauro Carvalho Chehab <mchehab@infradead.org>, " \
		      "Sascha Sommer <saschasommer@freenet.de>"

#define DRIVER_NAME         "em28xx"
#define DRIVER_DESC         "Empia em28xx based USB video device driver"
#define EM28XX_VERSION_CODE  KERNEL_VERSION(0, 1, 0)

#define em28xx_videodbg(fmt, arg...) do {\
	if (video_debug) \
		printk(KERN_INFO "%s %s :"fmt, \
			 dev->name, __func__ , ##arg); } while (0)

static unsigned int isoc_debug;
module_param(isoc_debug, int, 0644);
MODULE_PARM_DESC(isoc_debug, "enable debug messages [isoc transfers]");

#define em28xx_isocdbg(fmt, arg...) \
do {\
	if (isoc_debug) { \
		printk(KERN_INFO "%s %s :"fmt, \
			 dev->name, __func__ , ##arg); \
	} \
  } while (0)

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

static LIST_HEAD(em28xx_devlist);

static unsigned int card[]     = {[0 ... (EM28XX_MAXBOARDS - 1)] = UNSET };
static unsigned int video_nr[] = {[0 ... (EM28XX_MAXBOARDS - 1)] = UNSET };
static unsigned int vbi_nr[]   = {[0 ... (EM28XX_MAXBOARDS - 1)] = UNSET };
static unsigned int radio_nr[] = {[0 ... (EM28XX_MAXBOARDS - 1)] = UNSET };

module_param_array(card,  int, NULL, 0444);
module_param_array(video_nr, int, NULL, 0444);
module_param_array(vbi_nr, int, NULL, 0444);
module_param_array(radio_nr, int, NULL, 0444);
MODULE_PARM_DESC(card,     "card type");
MODULE_PARM_DESC(video_nr, "video device numbers");
MODULE_PARM_DESC(vbi_nr,   "vbi device numbers");
MODULE_PARM_DESC(radio_nr, "radio device numbers");

static unsigned int video_debug;
module_param(video_debug, int, 0644);
MODULE_PARM_DESC(video_debug, "enable debug messages [video]");

/* Bitmask marking allocated devices from 0 to EM28XX_MAXBOARDS */
static unsigned long em28xx_devused;

/* supported controls */
/* Common to all boards */
static struct v4l2_queryctrl em28xx_qctrl[] = {
	{
		.id = V4L2_CID_AUDIO_VOLUME,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Volume",
		.minimum = 0x0,
		.maximum = 0x1f,
		.step = 0x1,
		.default_value = 0x1f,
		.flags = 0,
	}, {
		.id = V4L2_CID_AUDIO_MUTE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Mute",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 1,
		.flags = 0,
	}
};

static struct usb_driver em28xx_usb_driver;

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

/*
 * Announces that a buffer were filled and request the next
 */
static inline void buffer_filled(struct em28xx *dev,
				  struct em28xx_dmaqueue *dma_q,
				  struct em28xx_buffer *buf)
{
	/* Advice that buffer was filled */
	em28xx_isocdbg("[%p/%d] wakeup\n", buf, buf->vb.i);
	buf->vb.state = VIDEOBUF_DONE;
	buf->vb.field_count++;
	do_gettimeofday(&buf->vb.ts);

	dev->isoc_ctl.buf = NULL;

	list_del(&buf->vb.queue);
	wake_up(&buf->vb.done);
}

/*
 * Identify the buffer header type and properly handles
 */
static void em28xx_copy_video(struct em28xx *dev,
			      struct em28xx_dmaqueue  *dma_q,
			      struct em28xx_buffer *buf,
			      unsigned char *p,
			      unsigned char *outp, unsigned long len)
{
	void *fieldstart, *startwrite, *startread;
	int  linesdone, currlinedone, offset, lencopy, remain;
	int bytesperline = dev->width << 1;

	if (dma_q->pos + len > buf->vb.size)
		len = buf->vb.size - dma_q->pos;

	if (p[0] != 0x88 && p[0] != 0x22) {
		em28xx_isocdbg("frame is not complete\n");
		len += 4;
	} else
		p += 4;

	startread = p;
	remain = len;

	/* Interlaces frame */
	if (buf->top_field)
		fieldstart = outp;
	else
		fieldstart = outp + bytesperline;

	linesdone = dma_q->pos / bytesperline;
	currlinedone = dma_q->pos % bytesperline;
	offset = linesdone * bytesperline * 2 + currlinedone;
	startwrite = fieldstart + offset;
	lencopy = bytesperline - currlinedone;
	lencopy = lencopy > remain ? remain : lencopy;

	if ((char *)startwrite + lencopy > (char *)outp + buf->vb.size) {
		em28xx_isocdbg("Overflow of %zi bytes past buffer end (1)\n",
			       ((char *)startwrite + lencopy) -
			       ((char *)outp + buf->vb.size));
		lencopy = remain = (char *)outp + buf->vb.size - (char *)startwrite;
	}
	if (lencopy <= 0)
		return;
	memcpy(startwrite, startread, lencopy);

	remain -= lencopy;

	while (remain > 0) {
		startwrite += lencopy + bytesperline;
		startread += lencopy;
		if (bytesperline > remain)
			lencopy = remain;
		else
			lencopy = bytesperline;

		if ((char *)startwrite + lencopy > (char *)outp + buf->vb.size) {
			em28xx_isocdbg("Overflow of %zi bytes past buffer end (2)\n",
				       ((char *)startwrite + lencopy) -
				       ((char *)outp + buf->vb.size));
			lencopy = remain = (char *)outp + buf->vb.size -
					   (char *)startwrite;
		}
		if (lencopy <= 0)
			break;

		memcpy(startwrite, startread, lencopy);

		remain -= lencopy;
	}

	dma_q->pos += len;
}

static inline void print_err_status(struct em28xx *dev,
				     int packet, int status)
{
	char *errmsg = "Unknown";

	switch (status) {
	case -ENOENT:
		errmsg = "unlinked synchronuously";
		break;
	case -ECONNRESET:
		errmsg = "unlinked asynchronuously";
		break;
	case -ENOSR:
		errmsg = "Buffer error (overrun)";
		break;
	case -EPIPE:
		errmsg = "Stalled (device not responding)";
		break;
	case -EOVERFLOW:
		errmsg = "Babble (bad cable?)";
		break;
	case -EPROTO:
		errmsg = "Bit-stuff error (bad cable?)";
		break;
	case -EILSEQ:
		errmsg = "CRC/Timeout (could be anything)";
		break;
	case -ETIME:
		errmsg = "Device does not respond";
		break;
	}
	if (packet < 0) {
		em28xx_isocdbg("URB status %d [%s].\n",	status, errmsg);
	} else {
		em28xx_isocdbg("URB packet %d, status %d [%s].\n",
			       packet, status, errmsg);
	}
}

/*
 * video-buf generic routine to get the next available buffer
 */
static inline void get_next_buf(struct em28xx_dmaqueue *dma_q,
					  struct em28xx_buffer **buf)
{
	struct em28xx *dev = container_of(dma_q, struct em28xx, vidq);
	char *outp;

	if (list_empty(&dma_q->active)) {
		em28xx_isocdbg("No active queue to serve\n");
		dev->isoc_ctl.buf = NULL;
		*buf = NULL;
		return;
	}

	/* Get the next buffer */
	*buf = list_entry(dma_q->active.next, struct em28xx_buffer, vb.queue);

	/* Cleans up buffer - Usefull for testing for frame/URB loss */
	outp = videobuf_to_vmalloc(&(*buf)->vb);
	memset(outp, 0, (*buf)->vb.size);

	dev->isoc_ctl.buf = *buf;

	return;
}

/*
 * Controls the isoc copy of each urb packet
 */
static inline int em28xx_isoc_copy(struct em28xx *dev, struct urb *urb)
{
	struct em28xx_buffer    *buf;
	struct em28xx_dmaqueue  *dma_q = urb->context;
	unsigned char *outp = NULL;
	int i, len = 0, rc = 1;
	unsigned char *p;

	if (!dev)
		return 0;

	if ((dev->state & DEV_DISCONNECTED) || (dev->state & DEV_MISCONFIGURED))
		return 0;

	if (urb->status < 0) {
		print_err_status(dev, -1, urb->status);
		if (urb->status == -ENOENT)
			return 0;
	}

	buf = dev->isoc_ctl.buf;
	if (buf != NULL)
		outp = videobuf_to_vmalloc(&buf->vb);

	for (i = 0; i < urb->number_of_packets; i++) {
		int status = urb->iso_frame_desc[i].status;

		if (status < 0) {
			print_err_status(dev, i, status);
			if (urb->iso_frame_desc[i].status != -EPROTO)
				continue;
		}

		len = urb->iso_frame_desc[i].actual_length - 4;

		if (urb->iso_frame_desc[i].actual_length <= 0) {
			/* em28xx_isocdbg("packet %d is empty",i); - spammy */
			continue;
		}
		if (urb->iso_frame_desc[i].actual_length >
						dev->max_pkt_size) {
			em28xx_isocdbg("packet bigger than packet size");
			continue;
		}

		p = urb->transfer_buffer + urb->iso_frame_desc[i].offset;

		/* FIXME: incomplete buffer checks where removed to make
		   logic simpler. Impacts of those changes should be evaluated
		 */
		if (p[0] == 0x33 && p[1] == 0x95 && p[2] == 0x00) {
			em28xx_isocdbg("VBI HEADER!!!\n");
			/* FIXME: Should add vbi copy */
			continue;
		}
		if (p[0] == 0x22 && p[1] == 0x5a) {
			em28xx_isocdbg("Video frame %d, length=%i, %s\n", p[2],
				       len, (p[2] & 1)? "odd" : "even");

			if (!(p[2] & 1)) {
				if (buf != NULL)
					buffer_filled(dev, dma_q, buf);
				get_next_buf(dma_q, &buf);
				if (buf == NULL)
					outp = NULL;
				else
					outp = videobuf_to_vmalloc(&buf->vb);
			}

			if (buf != NULL) {
				if (p[2] & 1)
					buf->top_field = 0;
				else
					buf->top_field = 1;
			}

			dma_q->pos = 0;
		}
		if (buf != NULL)
			em28xx_copy_video(dev, dma_q, buf, p, outp, len);
	}
	return rc;
}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/

static int
buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct em28xx_fh *fh = vq->priv_data;
	struct em28xx        *dev = fh->dev;
	struct v4l2_frequency f;

	*size = 16 * fh->dev->width * fh->dev->height >> 3;
	if (0 == *count)
		*count = EM28XX_DEF_BUF;

	if (*count < EM28XX_MIN_BUF)
		*count = EM28XX_MIN_BUF;

	/* Ask tuner to go to analog mode */
	memset(&f, 0, sizeof(f));
	f.frequency = dev->ctl_freq;

	em28xx_i2c_call_clients(dev, VIDIOC_S_FREQUENCY, &f);

	return 0;
}

/* This is called *without* dev->slock held; please keep it that way */
static void free_buffer(struct videobuf_queue *vq, struct em28xx_buffer *buf)
{
	struct em28xx_fh     *fh  = vq->priv_data;
	struct em28xx        *dev = fh->dev;
	unsigned long flags = 0;
	if (in_interrupt())
		BUG();

	/* We used to wait for the buffer to finish here, but this didn't work
	   because, as we were keeping the state as VIDEOBUF_QUEUED,
	   videobuf_queue_cancel marked it as finished for us.
	   (Also, it could wedge forever if the hardware was misconfigured.)

	   This should be safe; by the time we get here, the buffer isn't
	   queued anymore. If we ever start marking the buffers as
	   VIDEOBUF_ACTIVE, it won't be, though.
	*/
	spin_lock_irqsave(&dev->slock, flags);
	if (dev->isoc_ctl.buf == buf)
		dev->isoc_ctl.buf = NULL;
	spin_unlock_irqrestore(&dev->slock, flags);

	videobuf_vmalloc_free(&buf->vb);
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct em28xx_fh     *fh  = vq->priv_data;
	struct em28xx_buffer *buf = container_of(vb, struct em28xx_buffer, vb);
	struct em28xx        *dev = fh->dev;
	int                  rc = 0, urb_init = 0;

	/* FIXME: It assumes depth = 16 */
	/* The only currently supported format is 16 bits/pixel */
	buf->vb.size = 16 * dev->width * dev->height >> 3;

	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	buf->vb.width  = dev->width;
	buf->vb.height = dev->height;
	buf->vb.field  = field;

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		rc = videobuf_iolock(vq, &buf->vb, NULL);
		if (rc < 0)
			goto fail;
	}

	if (!dev->isoc_ctl.num_bufs)
		urb_init = 1;

	if (urb_init) {
		rc = em28xx_init_isoc(dev, EM28XX_NUM_PACKETS,
				      EM28XX_NUM_BUFS, dev->max_pkt_size,
				      em28xx_isoc_copy);
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
	struct em28xx_buffer    *buf     = container_of(vb, struct em28xx_buffer, vb);
	struct em28xx_fh        *fh      = vq->priv_data;
	struct em28xx           *dev     = fh->dev;
	struct em28xx_dmaqueue  *vidq    = &dev->vidq;

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);

}

static void buffer_release(struct videobuf_queue *vq,
				struct videobuf_buffer *vb)
{
	struct em28xx_buffer   *buf  = container_of(vb, struct em28xx_buffer, vb);
	struct em28xx_fh       *fh   = vq->priv_data;
	struct em28xx          *dev  = (struct em28xx *)fh->dev;

	em28xx_isocdbg("em28xx: called buffer_release\n");

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops em28xx_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

/*********************  v4l2 interface  **************************************/

/*
 * em28xx_config()
 * inits registers with sane defaults
 */
static int em28xx_config(struct em28xx *dev)
{

	/* Sets I2C speed to 100 KHz */
	if (!dev->is_em2800)
		em28xx_write_regs_req(dev, 0x00, 0x06, "\x40", 1);

	/* enable vbi capturing */

/*	em28xx_write_regs_req(dev, 0x00, 0x0e, "\xC0", 1); audio register */
/*	em28xx_write_regs_req(dev, 0x00, 0x0f, "\x80", 1); clk register */
	em28xx_write_regs_req(dev, 0x00, 0x11, "\x51", 1);

	dev->mute = 1;		/* maybe not the right place... */
	dev->volume = 0x1f;

	em28xx_outfmt_set_yuv422(dev);
	em28xx_colorlevels_set_default(dev);
	em28xx_compression_disable(dev);

	return 0;
}

/*
 * em28xx_config_i2c()
 * configure i2c attached devices
 */
static void em28xx_config_i2c(struct em28xx *dev)
{
	struct v4l2_routing route;

	route.input = INPUT(dev->ctl_input)->vmux;
	route.output = 0;
	em28xx_i2c_call_clients(dev, VIDIOC_INT_RESET, NULL);
	em28xx_i2c_call_clients(dev, VIDIOC_INT_S_VIDEO_ROUTING, &route);
	em28xx_i2c_call_clients(dev, VIDIOC_STREAMON, NULL);
}

static void video_mux(struct em28xx *dev, int index)
{
	struct v4l2_routing route;

	route.input = INPUT(index)->vmux;
	route.output = 0;
	dev->ctl_input = index;
	dev->ctl_ainput = INPUT(index)->amux;

	em28xx_i2c_call_clients(dev, VIDIOC_INT_S_VIDEO_ROUTING, &route);

	if (dev->has_msp34xx) {
		if (dev->i2s_speed) {
			em28xx_i2c_call_clients(dev, VIDIOC_INT_I2S_CLOCK_FREQ,
				&dev->i2s_speed);
		}
		route.input = dev->ctl_ainput;
		route.output = MSP_OUTPUT(MSP_SC_IN_DSP_SCART1);
		/* Note: this is msp3400 specific */
		em28xx_i2c_call_clients(dev, VIDIOC_INT_S_AUDIO_ROUTING,
			&route);
	}

	em28xx_audio_analog_set(dev);
}

/* Usage lock check functions */
static int res_get(struct em28xx_fh *fh)
{
	struct em28xx    *dev = fh->dev;
	int		 rc   = 0;

	/* This instance already has stream_on */
	if (fh->stream_on)
		return rc;

	if (dev->stream_on)
		return -EINVAL;

	mutex_lock(&dev->lock);
	dev->stream_on = 1;
	fh->stream_on  = 1;
	mutex_unlock(&dev->lock);
	return rc;
}

static int res_check(struct em28xx_fh *fh)
{
	return (fh->stream_on);
}

static void res_free(struct em28xx_fh *fh)
{
	struct em28xx    *dev = fh->dev;

	mutex_lock(&dev->lock);
	fh->stream_on = 0;
	dev->stream_on = 0;
	mutex_unlock(&dev->lock);
}

/*
 * em28xx_get_ctrl()
 * return the current saturation, brightness or contrast, mute state
 */
static int em28xx_get_ctrl(struct em28xx *dev, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = dev->mute;
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = dev->volume;
		return 0;
	default:
		return -EINVAL;
	}
}

/*
 * em28xx_set_ctrl()
 * mute or set new saturation, brightness or contrast
 */
static int em28xx_set_ctrl(struct em28xx *dev, const struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value != dev->mute) {
			dev->mute = ctrl->value;
			return em28xx_audio_analog_set(dev);
		}
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		dev->volume = ctrl->value;
		return em28xx_audio_analog_set(dev);
	default:
		return -EINVAL;
	}
}

static int check_dev(struct em28xx *dev)
{
	if (dev->state & DEV_DISCONNECTED) {
		em28xx_errdev("v4l2 ioctl: device not present\n");
		return -ENODEV;
	}

	if (dev->state & DEV_MISCONFIGURED) {
		em28xx_errdev("v4l2 ioctl: device is misconfigured; "
			      "close and open it again\n");
		return -EIO;
	}
	return 0;
}

static void get_scale(struct em28xx *dev,
			unsigned int width, unsigned int height,
			unsigned int *hscale, unsigned int *vscale)
{
	unsigned int          maxw   = norm_maxw(dev);
	unsigned int          maxh   = norm_maxh(dev);

	*hscale = (((unsigned long)maxw) << 12) / width - 4096L;
	if (*hscale >= 0x4000)
		*hscale = 0x3fff;

	*vscale = (((unsigned long)maxh) << 12) / height - 4096L;
	if (*vscale >= 0x4000)
		*vscale = 0x3fff;
}

/* ------------------------------------------------------------------
	IOCTL vidioc handling
   ------------------------------------------------------------------*/

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;

	mutex_lock(&dev->lock);

	f->fmt.pix.width = dev->width;
	f->fmt.pix.height = dev->height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	f->fmt.pix.bytesperline = dev->width * 2;
	f->fmt.pix.sizeimage = f->fmt.pix.bytesperline  * dev->height;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	/* FIXME: TOP? NONE? BOTTOM? ALTENATE? */
	f->fmt.pix.field = dev->interlaced ?
			   V4L2_FIELD_INTERLACED : V4L2_FIELD_TOP;

	mutex_unlock(&dev->lock);
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct em28xx_fh      *fh    = priv;
	struct em28xx         *dev   = fh->dev;
	int                   width  = f->fmt.pix.width;
	int                   height = f->fmt.pix.height;
	unsigned int          maxw   = norm_maxw(dev);
	unsigned int          maxh   = norm_maxh(dev);
	unsigned int          hscale, vscale;

	/* width must even because of the YUYV format
	   height must be even because of interlacing */
	height &= 0xfffe;
	width &= 0xfffe;

	if (height < 32)
		height = 32;
	if (height > maxh)
		height = maxh;
	if (width < 48)
		width = 48;
	if (width > maxw)
		width = maxw;

	mutex_lock(&dev->lock);

	if (dev->is_em2800) {
		/* the em2800 can only scale down to 50% */
		if (height % (maxh / 2))
			height = maxh;
		if (width % (maxw / 2))
			width = maxw;
		/* according to empiatech support */
		/* the MaxPacketSize is to small to support */
		/* framesizes larger than 640x480 @ 30 fps */
		/* or 640x576 @ 25 fps. As this would cut */
		/* of a part of the image we prefer */
		/* 360x576 or 360x480 for now */
		if (width == maxw && height == maxh)
			width /= 2;
	}

	get_scale(dev, width, height, &hscale, &vscale);

	width = (((unsigned long)maxw) << 12) / (hscale + 4096L);
	height = (((unsigned long)maxh) << 12) / (vscale + 4096L);

	f->fmt.pix.width = width;
	f->fmt.pix.height = height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	f->fmt.pix.bytesperline = width * 2;
	f->fmt.pix.sizeimage = width * 2 * height;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.field = V4L2_FIELD_INTERLACED;

	mutex_unlock(&dev->lock);
	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	vidioc_try_fmt_vid_cap(file, priv, f);

	mutex_lock(&dev->lock);

	if (videobuf_queue_is_busy(&fh->vb_vidq)) {
		em28xx_errdev("%s queue busy\n", __func__);
		rc = -EBUSY;
		goto out;
	}

	if (dev->stream_on && !fh->stream_on) {
		em28xx_errdev("%s device in use by another fh\n", __func__);
		rc = -EBUSY;
		goto out;
	}

	/* set new image size */
	dev->width = f->fmt.pix.width;
	dev->height = f->fmt.pix.height;
	get_scale(dev, dev->width, dev->height, &dev->hscale, &dev->vscale);

	em28xx_set_alternate(dev);
	em28xx_resolution_set(dev);

	rc = 0;

out:
	mutex_unlock(&dev->lock);
	return rc;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id * norm)
{
	struct em28xx_fh   *fh  = priv;
	struct em28xx      *dev = fh->dev;
	struct v4l2_format f;
	int                rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	mutex_lock(&dev->lock);
	dev->norm = *norm;
	mutex_unlock(&dev->lock);

	/* Adjusts width/height, if needed */
	f.fmt.pix.width = dev->width;
	f.fmt.pix.height = dev->height;
	vidioc_try_fmt_vid_cap(file, priv, &f);

	mutex_lock(&dev->lock);

	/* set new image size */
	dev->width = f.fmt.pix.width;
	dev->height = f.fmt.pix.height;
	get_scale(dev, dev->width, dev->height, &dev->hscale, &dev->vscale);

	em28xx_resolution_set(dev);
	em28xx_i2c_call_clients(dev, VIDIOC_S_STD, &dev->norm);

	mutex_unlock(&dev->lock);
	return 0;
}

static const char *iname[] = {
	[EM28XX_VMUX_COMPOSITE1] = "Composite1",
	[EM28XX_VMUX_COMPOSITE2] = "Composite2",
	[EM28XX_VMUX_COMPOSITE3] = "Composite3",
	[EM28XX_VMUX_COMPOSITE4] = "Composite4",
	[EM28XX_VMUX_SVIDEO]     = "S-Video",
	[EM28XX_VMUX_TELEVISION] = "Television",
	[EM28XX_VMUX_CABLE]      = "Cable TV",
	[EM28XX_VMUX_DVB]        = "DVB",
	[EM28XX_VMUX_DEBUG]      = "for debug only",
};

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *i)
{
	struct em28xx_fh   *fh  = priv;
	struct em28xx      *dev = fh->dev;
	unsigned int       n;

	n = i->index;
	if (n >= MAX_EM28XX_INPUT)
		return -EINVAL;
	if (0 == INPUT(n)->type)
		return -EINVAL;

	i->index = n;
	i->type = V4L2_INPUT_TYPE_CAMERA;

	strcpy(i->name, iname[INPUT(n)->type]);

	if ((EM28XX_VMUX_TELEVISION == INPUT(n)->type) ||
		(EM28XX_VMUX_CABLE == INPUT(n)->type))
		i->type = V4L2_INPUT_TYPE_TUNER;

	i->std = dev->vdev->tvnorms;

	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct em28xx_fh   *fh  = priv;
	struct em28xx      *dev = fh->dev;

	*i = dev->ctl_input;

	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct em28xx_fh   *fh  = priv;
	struct em28xx      *dev = fh->dev;
	int                rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (i >= MAX_EM28XX_INPUT)
		return -EINVAL;
	if (0 == INPUT(i)->type)
		return -EINVAL;

	mutex_lock(&dev->lock);

	video_mux(dev, i);

	mutex_unlock(&dev->lock);
	return 0;
}

static int vidioc_g_audio(struct file *file, void *priv, struct v4l2_audio *a)
{
	struct em28xx_fh   *fh    = priv;
	struct em28xx      *dev   = fh->dev;
	unsigned int        index = a->index;

	if (a->index > 1)
		return -EINVAL;

	index = dev->ctl_ainput;

	if (index == 0)
		strcpy(a->name, "Television");
	else
		strcpy(a->name, "Line In");

	a->capability = V4L2_AUDCAP_STEREO;
	a->index = index;

	return 0;
}

static int vidioc_s_audio(struct file *file, void *priv, struct v4l2_audio *a)
{
	struct em28xx_fh   *fh  = priv;
	struct em28xx      *dev = fh->dev;

	if (a->index != dev->ctl_ainput)
		return -EINVAL;

	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
				struct v4l2_queryctrl *qc)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   id  = qc->id;
	int                   i;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	memset(qc, 0, sizeof(*qc));

	qc->id = id;

	if (!dev->has_msp34xx) {
		for (i = 0; i < ARRAY_SIZE(em28xx_qctrl); i++) {
			if (qc->id && qc->id == em28xx_qctrl[i].id) {
				memcpy(qc, &(em28xx_qctrl[i]), sizeof(*qc));
				return 0;
			}
		}
	}
	mutex_lock(&dev->lock);
	em28xx_i2c_call_clients(dev, VIDIOC_QUERYCTRL, qc);
	mutex_unlock(&dev->lock);

	if (qc->type)
		return 0;
	else
		return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;
	mutex_lock(&dev->lock);

	if (!dev->has_msp34xx)
		rc = em28xx_get_ctrl(dev, ctrl);
	else
		rc = -EINVAL;

	if (rc == -EINVAL) {
		em28xx_i2c_call_clients(dev, VIDIOC_G_CTRL, ctrl);
		rc = 0;
	}

	mutex_unlock(&dev->lock);
	return rc;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	u8                    i;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	mutex_lock(&dev->lock);

	if (dev->has_msp34xx)
		em28xx_i2c_call_clients(dev, VIDIOC_S_CTRL, ctrl);
	else {
		rc = 1;
		for (i = 0; i < ARRAY_SIZE(em28xx_qctrl); i++) {
			if (ctrl->id == em28xx_qctrl[i].id) {
				if (ctrl->value < em28xx_qctrl[i].minimum ||
				    ctrl->value > em28xx_qctrl[i].maximum) {
					rc = -ERANGE;
					break;
				}

				rc = em28xx_set_ctrl(dev, ctrl);
				break;
			}
		}
	}

	/* Control not found - try to send it to the attached devices */
	if (rc == 1) {
		em28xx_i2c_call_clients(dev, VIDIOC_S_CTRL, ctrl);
		rc = 0;
	}

	mutex_unlock(&dev->lock);
	return rc;
}

static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (0 != t->index)
		return -EINVAL;

	strcpy(t->name, "Tuner");

	mutex_lock(&dev->lock);

	em28xx_i2c_call_clients(dev, VIDIOC_G_TUNER, t);

	mutex_unlock(&dev->lock);
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (0 != t->index)
		return -EINVAL;

	mutex_lock(&dev->lock);

	em28xx_i2c_call_clients(dev, VIDIOC_S_TUNER, t);

	mutex_unlock(&dev->lock);
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;

	f->type = fh->radio ? V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
	f->frequency = dev->ctl_freq;

	return 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (0 != f->tuner)
		return -EINVAL;

	if (unlikely(0 == fh->radio && f->type != V4L2_TUNER_ANALOG_TV))
		return -EINVAL;
	if (unlikely(1 == fh->radio && f->type != V4L2_TUNER_RADIO))
		return -EINVAL;

	mutex_lock(&dev->lock);

	dev->ctl_freq = f->frequency;
	em28xx_i2c_call_clients(dev, VIDIOC_S_FREQUENCY, f);

	mutex_unlock(&dev->lock);
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int em28xx_reg_len(int reg)
{
	switch (reg) {
	case EM28XX_R40_AC97LSB:
	case EM28XX_R30_HSCALELOW:
	case EM28XX_R32_VSCALELOW:
		return 2;
	default:
		return 1;
	}
}

static int vidioc_g_register(struct file *file, void *priv,
			     struct v4l2_register *reg)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int ret;

	if (!v4l2_chip_match_host(reg->match_type, reg->match_chip))
		return -EINVAL;

	if (em28xx_reg_len(reg->reg) == 1) {
		ret = em28xx_read_reg(dev, reg->reg);
		if (ret < 0)
			return ret;

		reg->val = ret;
	} else {
		__le64 val = 0;
		ret = em28xx_read_reg_req_len(dev, USB_REQ_GET_STATUS,
						   reg->reg, (char *)&val, 2);
		if (ret < 0)
			return ret;

		reg->val = le64_to_cpu(val);
	}

	return 0;
}

static int vidioc_s_register(struct file *file, void *priv,
			     struct v4l2_register *reg)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	__le64 buf;

	buf = cpu_to_le64(reg->val);

	return em28xx_write_regs(dev, reg->reg, (char *)&buf,
				 em28xx_reg_len(reg->reg));
}
#endif


static int vidioc_cropcap(struct file *file, void *priv,
					struct v4l2_cropcap *cc)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;

	if (cc->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	cc->bounds.left = 0;
	cc->bounds.top = 0;
	cc->bounds.width = dev->width;
	cc->bounds.height = dev->height;
	cc->defrect = cc->bounds;
	cc->pixelaspect.numerator = 54;	/* 4:3 FIXME: remove magic numbers */
	cc->pixelaspect.denominator = 59;

	return 0;
}

static int vidioc_streamon(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;


	if (unlikely(res_get(fh) < 0))
		return -EBUSY;

	return (videobuf_streamon(&fh->vb_vidq));
}

static int vidioc_streamoff(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (type != fh->type)
		return -EINVAL;

	videobuf_streamoff(&fh->vb_vidq);
	res_free(fh);

	return 0;
}

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;

	strlcpy(cap->driver, "em28xx", sizeof(cap->driver));
	strlcpy(cap->card, em28xx_boards[dev->model].name, sizeof(cap->card));
	strlcpy(cap->bus_info, dev->udev->dev.bus_id, sizeof(cap->bus_info));

	cap->version = EM28XX_VERSION_CODE;

	cap->capabilities =
			V4L2_CAP_SLICED_VBI_CAPTURE |
			V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_AUDIO |
			V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;

	if (dev->tuner_type != TUNER_ABSENT)
		cap->capabilities |= V4L2_CAP_TUNER;

	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *fmtd)
{
	if (fmtd->index != 0)
		return -EINVAL;

	fmtd->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	strcpy(fmtd->description, "Packed YUY2");
	fmtd->pixelformat = V4L2_PIX_FMT_YUYV;
	memset(fmtd->reserved, 0, sizeof(fmtd->reserved));

	return 0;
}

/* Sliced VBI ioctls */
static int vidioc_g_fmt_sliced_vbi_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	mutex_lock(&dev->lock);

	f->fmt.sliced.service_set = 0;

	em28xx_i2c_call_clients(dev, VIDIOC_G_FMT, f);

	if (f->fmt.sliced.service_set == 0)
		rc = -EINVAL;

	mutex_unlock(&dev->lock);
	return rc;
}

static int vidioc_try_set_sliced_vbi_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	mutex_lock(&dev->lock);
	em28xx_i2c_call_clients(dev, VIDIOC_G_FMT, f);
	mutex_unlock(&dev->lock);

	if (f->fmt.sliced.service_set == 0)
		return -EINVAL;

	return 0;
}


static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *rb)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	return (videobuf_reqbufs(&fh->vb_vidq, rb));
}

static int vidioc_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *b)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	return (videobuf_querybuf(&fh->vb_vidq, b));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	return (videobuf_qbuf(&fh->vb_vidq, b));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	return (videobuf_dqbuf(&fh->vb_vidq, b,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct em28xx_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif


/* ----------------------------------------------------------- */
/* RADIO ESPECIFIC IOCTLS                                      */
/* ----------------------------------------------------------- */

static int radio_querycap(struct file *file, void  *priv,
			  struct v4l2_capability *cap)
{
	struct em28xx *dev = ((struct em28xx_fh *)priv)->dev;

	strlcpy(cap->driver, "em28xx", sizeof(cap->driver));
	strlcpy(cap->card, em28xx_boards[dev->model].name, sizeof(cap->card));
	strlcpy(cap->bus_info, dev->udev->dev.bus_id, sizeof(cap->bus_info));

	cap->version = EM28XX_VERSION_CODE;
	cap->capabilities = V4L2_CAP_TUNER;
	return 0;
}

static int radio_g_tuner(struct file *file, void *priv,
			 struct v4l2_tuner *t)
{
	struct em28xx *dev = ((struct em28xx_fh *)priv)->dev;

	if (unlikely(t->index > 0))
		return -EINVAL;

	strcpy(t->name, "Radio");
	t->type = V4L2_TUNER_RADIO;

	em28xx_i2c_call_clients(dev, VIDIOC_G_TUNER, t);
	return 0;
}

static int radio_enum_input(struct file *file, void *priv,
			    struct v4l2_input *i)
{
	if (i->index != 0)
		return -EINVAL;
	strcpy(i->name, "Radio");
	i->type = V4L2_INPUT_TYPE_TUNER;

	return 0;
}

static int radio_g_audio(struct file *file, void *priv, struct v4l2_audio *a)
{
	if (unlikely(a->index))
		return -EINVAL;

	strcpy(a->name, "Radio");
	return 0;
}

static int radio_s_tuner(struct file *file, void *priv,
			 struct v4l2_tuner *t)
{
	struct em28xx *dev = ((struct em28xx_fh *)priv)->dev;

	if (0 != t->index)
		return -EINVAL;

	em28xx_i2c_call_clients(dev, VIDIOC_S_TUNER, t);

	return 0;
}

static int radio_s_audio(struct file *file, void *fh,
			 struct v4l2_audio *a)
{
	return 0;
}

static int radio_s_input(struct file *file, void *fh, unsigned int i)
{
	return 0;
}

static int radio_queryctrl(struct file *file, void *priv,
			   struct v4l2_queryctrl *qc)
{
	int i;

	if (qc->id <  V4L2_CID_BASE ||
		qc->id >= V4L2_CID_LASTP1)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(em28xx_qctrl); i++) {
		if (qc->id && qc->id == em28xx_qctrl[i].id) {
			memcpy(qc, &(em28xx_qctrl[i]), sizeof(*qc));
			return 0;
		}
	}

	return -EINVAL;
}

/*
 * em28xx_v4l2_open()
 * inits the device and starts isoc transfer
 */
static int em28xx_v4l2_open(struct inode *inode, struct file *filp)
{
	int minor = iminor(inode);
	int errCode = 0, radio = 0;
	struct em28xx *h, *dev = NULL;
	struct em28xx_fh *fh;
	enum v4l2_buf_type fh_type = 0;

	list_for_each_entry(h, &em28xx_devlist, devlist) {
		if (h->vdev->minor == minor) {
			dev  = h;
			fh_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		}
		if (h->vbi_dev->minor == minor) {
			dev  = h;
			fh_type = V4L2_BUF_TYPE_VBI_CAPTURE;
		}
		if (h->radio_dev &&
		    h->radio_dev->minor == minor) {
			radio = 1;
			dev   = h;
		}
	}
	if (NULL == dev)
		return -ENODEV;

	em28xx_videodbg("open minor=%d type=%s users=%d\n",
				minor, v4l2_type_names[fh_type], dev->users);


	fh = kzalloc(sizeof(struct em28xx_fh), GFP_KERNEL);
	if (!fh) {
		em28xx_errdev("em28xx-video.c: Out of memory?!\n");
		return -ENOMEM;
	}
	mutex_lock(&dev->lock);
	fh->dev = dev;
	fh->radio = radio;
	fh->type = fh_type;
	filp->private_data = fh;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE && dev->users == 0) {
		dev->width = norm_maxw(dev);
		dev->height = norm_maxh(dev);
		dev->hscale = 0;
		dev->vscale = 0;

		em28xx_set_mode(dev, EM28XX_ANALOG_MODE);
		em28xx_set_alternate(dev);
		em28xx_resolution_set(dev);

		/* Needed, since GPIO might have disabled power of
		   some i2c device
		 */
		em28xx_config_i2c(dev);

	}
	if (fh->radio) {
		em28xx_videodbg("video_open: setting radio device\n");
		em28xx_i2c_call_clients(dev, AUDC_SET_RADIO, NULL);
	}

	dev->users++;

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &em28xx_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct em28xx_buffer), fh);

	mutex_unlock(&dev->lock);

	return errCode;
}

/*
 * em28xx_realease_resources()
 * unregisters the v4l2,i2c and usb devices
 * called when the device gets disconected or at module unload
*/
static void em28xx_release_resources(struct em28xx *dev)
{

	/*FIXME: I2C IR should be disconnected */

	em28xx_info("V4L2 devices /dev/video%d and /dev/vbi%d deregistered\n",
				dev->vdev->minor-MINOR_VFL_TYPE_GRABBER_MIN,
				dev->vbi_dev->minor-MINOR_VFL_TYPE_VBI_MIN);
	list_del(&dev->devlist);
	if (dev->sbutton_input_dev)
		em28xx_deregister_snapshot_button(dev);
	if (dev->radio_dev) {
		if (-1 != dev->radio_dev->minor)
			video_unregister_device(dev->radio_dev);
		else
			video_device_release(dev->radio_dev);
		dev->radio_dev = NULL;
	}
	if (dev->vbi_dev) {
		if (-1 != dev->vbi_dev->minor)
			video_unregister_device(dev->vbi_dev);
		else
			video_device_release(dev->vbi_dev);
		dev->vbi_dev = NULL;
	}
	if (dev->vdev) {
		if (-1 != dev->vdev->minor)
			video_unregister_device(dev->vdev);
		else
			video_device_release(dev->vdev);
		dev->vdev = NULL;
	}
	em28xx_i2c_unregister(dev);
	usb_put_dev(dev->udev);

	/* Mark device as unused */
	em28xx_devused &= ~(1<<dev->devno);
}

/*
 * em28xx_v4l2_close()
 * stops streaming and deallocates all resources allocated by the v4l2
 * calls and ioctls
 */
static int em28xx_v4l2_close(struct inode *inode, struct file *filp)
{
	struct em28xx_fh *fh  = filp->private_data;
	struct em28xx    *dev = fh->dev;
	int              errCode;

	em28xx_videodbg("users=%d\n", dev->users);


	if (res_check(fh))
		res_free(fh);

	mutex_lock(&dev->lock);

	if (dev->users == 1) {
		videobuf_stop(&fh->vb_vidq);
		videobuf_mmap_free(&fh->vb_vidq);

		/* the device is already disconnect,
		   free the remaining resources */
		if (dev->state & DEV_DISCONNECTED) {
			em28xx_release_resources(dev);
			mutex_unlock(&dev->lock);
			kfree(dev);
			return 0;
		}

		/* do this before setting alternate! */
		em28xx_uninit_isoc(dev);
		em28xx_set_mode(dev, EM28XX_MODE_UNDEFINED);

		/* set alternate 0 */
		dev->alt = 0;
		em28xx_videodbg("setting alternate 0\n");
		errCode = usb_set_interface(dev->udev, 0, 0);
		if (errCode < 0) {
			em28xx_errdev("cannot change alternate number to "
					"0 (error=%i)\n", errCode);
		}
	}
	kfree(fh);
	dev->users--;
	wake_up_interruptible_nr(&dev->open, 1);
	mutex_unlock(&dev->lock);
	return 0;
}

/*
 * em28xx_v4l2_read()
 * will allocate buffers when called for the first time
 */
static ssize_t
em28xx_v4l2_read(struct file *filp, char __user *buf, size_t count,
		 loff_t *pos)
{
	struct em28xx_fh *fh = filp->private_data;
	struct em28xx *dev = fh->dev;
	int rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	/* FIXME: read() is not prepared to allow changing the video
	   resolution while streaming. Seems a bug at em28xx_set_fmt
	 */

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		if (unlikely(res_get(fh)))
			return -EBUSY;

		return videobuf_read_stream(&fh->vb_vidq, buf, count, pos, 0,
					filp->f_flags & O_NONBLOCK);
	}
	return 0;
}

/*
 * em28xx_v4l2_poll()
 * will allocate buffers when called for the first time
 */
static unsigned int em28xx_v4l2_poll(struct file *filp, poll_table * wait)
{
	struct em28xx_fh *fh = filp->private_data;
	struct em28xx *dev = fh->dev;
	int rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (unlikely(res_get(fh) < 0))
		return POLLERR;

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(filp, &fh->vb_vidq, wait);
}

/*
 * em28xx_v4l2_mmap()
 */
static int em28xx_v4l2_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct em28xx_fh *fh    = filp->private_data;
	struct em28xx	 *dev   = fh->dev;
	int		 rc;

	if (unlikely(res_get(fh) < 0))
		return -EBUSY;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	rc = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	em28xx_videodbg("vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		rc);

	return rc;
}

static const struct file_operations em28xx_v4l_fops = {
	.owner         = THIS_MODULE,
	.open          = em28xx_v4l2_open,
	.release       = em28xx_v4l2_close,
	.read          = em28xx_v4l2_read,
	.poll          = em28xx_v4l2_poll,
	.mmap          = em28xx_v4l2_mmap,
	.ioctl	       = video_ioctl2,
	.llseek        = no_llseek,
	.compat_ioctl  = v4l_compat_ioctl32,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap            = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap    = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap       = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap     = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap       = vidioc_s_fmt_vid_cap,
	.vidioc_g_audio             = vidioc_g_audio,
	.vidioc_s_audio             = vidioc_s_audio,
	.vidioc_cropcap             = vidioc_cropcap,

	.vidioc_g_fmt_sliced_vbi_cap   = vidioc_g_fmt_sliced_vbi_cap,
	.vidioc_try_fmt_sliced_vbi_cap = vidioc_try_set_sliced_vbi_cap,
	.vidioc_s_fmt_sliced_vbi_cap   = vidioc_try_set_sliced_vbi_cap,

	.vidioc_reqbufs             = vidioc_reqbufs,
	.vidioc_querybuf            = vidioc_querybuf,
	.vidioc_qbuf                = vidioc_qbuf,
	.vidioc_dqbuf               = vidioc_dqbuf,
	.vidioc_s_std               = vidioc_s_std,
	.vidioc_enum_input          = vidioc_enum_input,
	.vidioc_g_input             = vidioc_g_input,
	.vidioc_s_input             = vidioc_s_input,
	.vidioc_queryctrl           = vidioc_queryctrl,
	.vidioc_g_ctrl              = vidioc_g_ctrl,
	.vidioc_s_ctrl              = vidioc_s_ctrl,
	.vidioc_streamon            = vidioc_streamon,
	.vidioc_streamoff           = vidioc_streamoff,
	.vidioc_g_tuner             = vidioc_g_tuner,
	.vidioc_s_tuner             = vidioc_s_tuner,
	.vidioc_g_frequency         = vidioc_g_frequency,
	.vidioc_s_frequency         = vidioc_s_frequency,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register          = vidioc_g_register,
	.vidioc_s_register          = vidioc_s_register,
#endif
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf                = vidiocgmbuf,
#endif
};

static const struct video_device em28xx_video_template = {
	.fops                       = &em28xx_v4l_fops,
	.release                    = video_device_release,
	.ioctl_ops 		    = &video_ioctl_ops,

	.minor                      = -1,

	.tvnorms                    = V4L2_STD_ALL,
	.current_norm               = V4L2_STD_PAL,
};

static const struct file_operations radio_fops = {
	.owner         = THIS_MODULE,
	.open          = em28xx_v4l2_open,
	.release       = em28xx_v4l2_close,
	.ioctl	       = video_ioctl2,
	.compat_ioctl  = v4l_compat_ioctl32,
	.llseek        = no_llseek,
};

static const struct v4l2_ioctl_ops radio_ioctl_ops = {
	.vidioc_querycap      = radio_querycap,
	.vidioc_g_tuner       = radio_g_tuner,
	.vidioc_enum_input    = radio_enum_input,
	.vidioc_g_audio       = radio_g_audio,
	.vidioc_s_tuner       = radio_s_tuner,
	.vidioc_s_audio       = radio_s_audio,
	.vidioc_s_input       = radio_s_input,
	.vidioc_queryctrl     = radio_queryctrl,
	.vidioc_g_ctrl        = vidioc_g_ctrl,
	.vidioc_s_ctrl        = vidioc_s_ctrl,
	.vidioc_g_frequency   = vidioc_g_frequency,
	.vidioc_s_frequency   = vidioc_s_frequency,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register    = vidioc_g_register,
	.vidioc_s_register    = vidioc_s_register,
#endif
};

static struct video_device em28xx_radio_template = {
	.name                 = "em28xx-radio",
	.fops                 = &radio_fops,
	.ioctl_ops 	      = &radio_ioctl_ops,
	.minor                = -1,
};

/******************************** usb interface ******************************/


static LIST_HEAD(em28xx_extension_devlist);
static DEFINE_MUTEX(em28xx_extension_devlist_lock);

int em28xx_register_extension(struct em28xx_ops *ops)
{
	struct em28xx *dev = NULL;

	mutex_lock(&em28xx_extension_devlist_lock);
	list_add_tail(&ops->next, &em28xx_extension_devlist);
	list_for_each_entry(dev, &em28xx_devlist, devlist) {
		if (dev)
			ops->init(dev);
	}
	printk(KERN_INFO "Em28xx: Initialized (%s) extension\n", ops->name);
	mutex_unlock(&em28xx_extension_devlist_lock);
	return 0;
}
EXPORT_SYMBOL(em28xx_register_extension);

void em28xx_unregister_extension(struct em28xx_ops *ops)
{
	struct em28xx *dev = NULL;

	list_for_each_entry(dev, &em28xx_devlist, devlist) {
		if (dev)
			ops->fini(dev);
	}

	mutex_lock(&em28xx_extension_devlist_lock);
	printk(KERN_INFO "Em28xx: Removed (%s) extension\n", ops->name);
	list_del(&ops->next);
	mutex_unlock(&em28xx_extension_devlist_lock);
}
EXPORT_SYMBOL(em28xx_unregister_extension);

static struct video_device *em28xx_vdev_init(struct em28xx *dev,
					     const struct video_device *template,
					     const char *type_name)
{
	struct video_device *vfd;

	vfd = video_device_alloc();
	if (NULL == vfd)
		return NULL;
	*vfd = *template;
	vfd->minor   = -1;
	vfd->parent = &dev->udev->dev;
	vfd->release = video_device_release;
	vfd->debug = video_debug;

	snprintf(vfd->name, sizeof(vfd->name), "%s %s",
		 dev->name, type_name);

	return vfd;
}


/*
 * em28xx_init_dev()
 * allocates and inits the device structs, registers i2c bus and v4l device
 */
static int em28xx_init_dev(struct em28xx **devhandle, struct usb_device *udev,
			   int minor)
{
	struct em28xx_ops *ops = NULL;
	struct em28xx *dev = *devhandle;
	int retval = -ENOMEM;
	int errCode;
	unsigned int maxh, maxw;

	dev->udev = udev;
	mutex_init(&dev->lock);
	spin_lock_init(&dev->slock);
	init_waitqueue_head(&dev->open);
	init_waitqueue_head(&dev->wait_frame);
	init_waitqueue_head(&dev->wait_stream);

	dev->em28xx_write_regs = em28xx_write_regs;
	dev->em28xx_read_reg = em28xx_read_reg;
	dev->em28xx_read_reg_req_len = em28xx_read_reg_req_len;
	dev->em28xx_write_regs_req = em28xx_write_regs_req;
	dev->em28xx_read_reg_req = em28xx_read_reg_req;
	dev->is_em2800 = em28xx_boards[dev->model].is_em2800;

	em28xx_pre_card_setup(dev);

	errCode = em28xx_config(dev);
	if (errCode) {
		em28xx_errdev("error configuring device\n");
		em28xx_devused &= ~(1<<dev->devno);
		kfree(dev);
		return -ENOMEM;
	}

	/* register i2c bus */
	em28xx_i2c_register(dev);

	/* Do board specific init and eeprom reading */
	em28xx_card_setup(dev);

	/* Configure audio */
	em28xx_audio_analog_set(dev);

	/* configure the device */
	em28xx_config_i2c(dev);

	/* set default norm */
	dev->norm = em28xx_video_template.current_norm;

	maxw = norm_maxw(dev);
	maxh = norm_maxh(dev);

	/* set default image size */
	dev->width = maxw;
	dev->height = maxh;
	dev->interlaced = EM28XX_INTERLACED_DEFAULT;
	dev->hscale = 0;
	dev->vscale = 0;
	dev->ctl_input = 2;

	errCode = em28xx_config(dev);

	list_add_tail(&dev->devlist, &em28xx_devlist);

	/* allocate and fill video video_device struct */
	dev->vdev = em28xx_vdev_init(dev, &em28xx_video_template, "video");
	if (NULL == dev->vdev) {
		em28xx_errdev("cannot allocate video_device.\n");
		goto fail_unreg;
	}

	/* register v4l2 video video_device */
	retval = video_register_device(dev->vdev, VFL_TYPE_GRABBER,
				       video_nr[dev->devno]);
	if (retval) {
		em28xx_errdev("unable to register video device (error=%i).\n",
			      retval);
		goto fail_unreg;
	}

	/* Allocate and fill vbi video_device struct */
	dev->vbi_dev = em28xx_vdev_init(dev, &em28xx_video_template, "vbi");
	/* register v4l2 vbi video_device */
	if (video_register_device(dev->vbi_dev, VFL_TYPE_VBI,
					vbi_nr[dev->devno]) < 0) {
		em28xx_errdev("unable to register vbi device\n");
		retval = -ENODEV;
		goto fail_unreg;
	}

	if (em28xx_boards[dev->model].radio.type == EM28XX_RADIO) {
		dev->radio_dev = em28xx_vdev_init(dev, &em28xx_radio_template, "radio");
		if (NULL == dev->radio_dev) {
			em28xx_errdev("cannot allocate video_device.\n");
			goto fail_unreg;
		}
		retval = video_register_device(dev->radio_dev, VFL_TYPE_RADIO,
					    radio_nr[dev->devno]);
		if (retval < 0) {
			em28xx_errdev("can't register radio device\n");
			goto fail_unreg;
		}
		em28xx_info("Registered radio device as /dev/radio%d\n",
			    dev->radio_dev->minor & 0x1f);
	}

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	INIT_LIST_HEAD(&dev->vidq.queued);


	if (dev->has_msp34xx) {
		/* Send a reset to other chips via gpio */
		em28xx_write_regs_req(dev, 0x00, 0x08, "\xf7", 1);
		msleep(3);
		em28xx_write_regs_req(dev, 0x00, 0x08, "\xff", 1);
		msleep(3);
	}

	video_mux(dev, 0);

	em28xx_info("V4L2 device registered as /dev/video%d and /dev/vbi%d\n",
				dev->vdev->minor-MINOR_VFL_TYPE_GRABBER_MIN,
				dev->vbi_dev->minor-MINOR_VFL_TYPE_VBI_MIN);

	mutex_lock(&em28xx_extension_devlist_lock);
	if (!list_empty(&em28xx_extension_devlist)) {
		list_for_each_entry(ops, &em28xx_extension_devlist, next) {
			if (ops->id)
				ops->init(dev);
		}
	}
	mutex_unlock(&em28xx_extension_devlist_lock);

	return 0;

fail_unreg:
	em28xx_release_resources(dev);
	mutex_unlock(&dev->lock);
	kfree(dev);
	return retval;
}

#if defined(CONFIG_MODULES) && defined(MODULE)
static void request_module_async(struct work_struct *work)
{
	struct em28xx *dev = container_of(work,
			     struct em28xx, request_module_wk);

	if (dev->has_audio_class)
		request_module("snd-usb-audio");
	else
		request_module("em28xx-alsa");

	if (dev->has_dvb)
		request_module("em28xx-dvb");
}

static void request_modules(struct em28xx *dev)
{
	INIT_WORK(&dev->request_module_wk, request_module_async);
	schedule_work(&dev->request_module_wk);
}
#else
#define request_modules(dev)
#endif /* CONFIG_MODULES */

/*
 * em28xx_usb_probe()
 * checks for supported devices
 */
static int em28xx_usb_probe(struct usb_interface *interface,
			    const struct usb_device_id *id)
{
	const struct usb_endpoint_descriptor *endpoint;
	struct usb_device *udev;
	struct usb_interface *uif;
	struct em28xx *dev = NULL;
	int retval = -ENODEV;
	int i, nr, ifnum;

	udev = usb_get_dev(interface_to_usbdev(interface));
	ifnum = interface->altsetting[0].desc.bInterfaceNumber;

	/* Check to see next free device and mark as used */
	nr = find_first_zero_bit(&em28xx_devused, EM28XX_MAXBOARDS);
	em28xx_devused |= 1<<nr;

	/* Don't register audio interfaces */
	if (interface->altsetting[0].desc.bInterfaceClass == USB_CLASS_AUDIO) {
		em28xx_err(DRIVER_NAME " audio device (%04x:%04x): interface %i, class %i\n",
				udev->descriptor.idVendor,
				udev->descriptor.idProduct,
				ifnum,
				interface->altsetting[0].desc.bInterfaceClass);

		em28xx_devused &= ~(1<<nr);
		return -ENODEV;
	}

	em28xx_err(DRIVER_NAME " new video device (%04x:%04x): interface %i, class %i\n",
			udev->descriptor.idVendor,
			udev->descriptor.idProduct,
			ifnum,
			interface->altsetting[0].desc.bInterfaceClass);

	endpoint = &interface->cur_altsetting->endpoint[1].desc;

	/* check if the device has the iso in endpoint at the correct place */
	if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) !=
	    USB_ENDPOINT_XFER_ISOC) {
		em28xx_err(DRIVER_NAME " probing error: endpoint is non-ISO endpoint!\n");
		em28xx_devused &= ~(1<<nr);
		return -ENODEV;
	}
	if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT) {
		em28xx_err(DRIVER_NAME " probing error: endpoint is ISO OUT endpoint!\n");
		em28xx_devused &= ~(1<<nr);
		return -ENODEV;
	}

	if (nr >= EM28XX_MAXBOARDS) {
		printk(DRIVER_NAME ": Supports only %i em28xx boards.\n",
				EM28XX_MAXBOARDS);
		em28xx_devused &= ~(1<<nr);
		return -ENOMEM;
	}

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		em28xx_err(DRIVER_NAME ": out of memory!\n");
		em28xx_devused &= ~(1<<nr);
		return -ENOMEM;
	}

	snprintf(dev->name, 29, "em28xx #%d", nr);
	dev->devno = nr;
	dev->model = id->driver_info;
	dev->alt   = -1;

	/* Checks if audio is provided by some interface */
	for (i = 0; i < udev->config->desc.bNumInterfaces; i++) {
		uif = udev->config->interface[i];
		if (uif->altsetting[0].desc.bInterfaceClass == USB_CLASS_AUDIO) {
			dev->has_audio_class = 1;
			break;
		}
	}

	printk(KERN_INFO DRIVER_NAME " %s usb audio class\n",
		   dev->has_audio_class ? "Has" : "Doesn't have");

	/* compute alternate max packet sizes */
	uif = udev->actconfig->interface[0];

	dev->num_alt = uif->num_altsetting;
	em28xx_info("Alternate settings: %i\n", dev->num_alt);
/*	dev->alt_max_pkt_size = kmalloc(sizeof(*dev->alt_max_pkt_size)* */
	dev->alt_max_pkt_size = kmalloc(32 * dev->num_alt, GFP_KERNEL);

	if (dev->alt_max_pkt_size == NULL) {
		em28xx_errdev("out of memory!\n");
		em28xx_devused &= ~(1<<nr);
		kfree(dev);
		return -ENOMEM;
	}

	for (i = 0; i < dev->num_alt ; i++) {
		u16 tmp = le16_to_cpu(uif->altsetting[i].endpoint[1].desc.
							wMaxPacketSize);
		dev->alt_max_pkt_size[i] =
		    (tmp & 0x07ff) * (((tmp & 0x1800) >> 11) + 1);
		em28xx_info("Alternate setting %i, max size= %i\n", i,
						dev->alt_max_pkt_size[i]);
	}

	if ((card[nr] >= 0) && (card[nr] < em28xx_bcount))
		dev->model = card[nr];

	/* allocate device struct */
	retval = em28xx_init_dev(&dev, udev, nr);
	if (retval)
		return retval;

	em28xx_info("Found %s\n", em28xx_boards[dev->model].name);

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	request_modules(dev);

	return 0;
}

/*
 * em28xx_usb_disconnect()
 * called when the device gets diconencted
 * video device will be unregistered on v4l2_close in case it is still open
 */
static void em28xx_usb_disconnect(struct usb_interface *interface)
{
	struct em28xx *dev;
	struct em28xx_ops *ops = NULL;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	if (!dev)
		return;

	em28xx_info("disconnecting %s\n", dev->vdev->name);

	/* wait until all current v4l2 io is finished then deallocate
	   resources */
	mutex_lock(&dev->lock);

	wake_up_interruptible_all(&dev->open);

	if (dev->users) {
		em28xx_warn
		    ("device /dev/video%d is open! Deregistration and memory "
		     "deallocation are deferred on close.\n",
				dev->vdev->minor-MINOR_VFL_TYPE_GRABBER_MIN);

		dev->state |= DEV_MISCONFIGURED;
		em28xx_uninit_isoc(dev);
		dev->state |= DEV_DISCONNECTED;
		wake_up_interruptible(&dev->wait_frame);
		wake_up_interruptible(&dev->wait_stream);
	} else {
		dev->state |= DEV_DISCONNECTED;
		em28xx_release_resources(dev);
	}
	mutex_unlock(&dev->lock);

	mutex_lock(&em28xx_extension_devlist_lock);
	if (!list_empty(&em28xx_extension_devlist)) {
		list_for_each_entry(ops, &em28xx_extension_devlist, next) {
			ops->fini(dev);
		}
	}
	mutex_unlock(&em28xx_extension_devlist_lock);

	if (!dev->users) {
		kfree(dev->alt_max_pkt_size);
		kfree(dev);
	}
}

static struct usb_driver em28xx_usb_driver = {
	.name = "em28xx",
	.probe = em28xx_usb_probe,
	.disconnect = em28xx_usb_disconnect,
	.id_table = em28xx_id_table,
};

static int __init em28xx_module_init(void)
{
	int result;

	printk(KERN_INFO DRIVER_NAME " v4l2 driver version %d.%d.%d loaded\n",
	       (EM28XX_VERSION_CODE >> 16) & 0xff,
	       (EM28XX_VERSION_CODE >> 8) & 0xff, EM28XX_VERSION_CODE & 0xff);
#ifdef SNAPSHOT
	printk(KERN_INFO DRIVER_NAME " snapshot date %04d-%02d-%02d\n",
	       SNAPSHOT / 10000, (SNAPSHOT / 100) % 100, SNAPSHOT % 100);
#endif

	/* register this driver with the USB subsystem */
	result = usb_register(&em28xx_usb_driver);
	if (result)
		em28xx_err(DRIVER_NAME
			   " usb_register failed. Error number %d.\n", result);

	return result;
}

static void __exit em28xx_module_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&em28xx_usb_driver);
}

module_init(em28xx_module_init);
module_exit(em28xx_module_exit);

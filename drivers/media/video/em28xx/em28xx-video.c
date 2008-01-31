/*
   em28xx-video.c - driver for Empia EM2800/EM2820/2840 USB video capture devices

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
			 dev->name, __FUNCTION__ , ##arg); } while (0)

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

static unsigned int video_debug = 0;
module_param(video_debug,int,0644);
MODULE_PARM_DESC(video_debug,"enable debug messages [video]");

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
	},{
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


/*********************  v4l2 interface  ******************************************/

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

/*	em28xx_write_regs_req(dev,0x00,0x0e,"\xC0",1); audio register */
/*	em28xx_write_regs_req(dev,0x00,0x0f,"\x80",1); clk register */
	em28xx_write_regs_req(dev,0x00,0x11,"\x51",1);

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

/*
 * em28xx_empty_framequeues()
 * prepare queues for incoming and outgoing frames
 */
static void em28xx_empty_framequeues(struct em28xx *dev)
{
	u32 i;

	INIT_LIST_HEAD(&dev->inqueue);
	INIT_LIST_HEAD(&dev->outqueue);

	for (i = 0; i < EM28XX_NUM_FRAMES; i++) {
		dev->frame[i].state = F_UNUSED;
		dev->frame[i].buf.bytesused = 0;
	}
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
		if (dev->i2s_speed)
			em28xx_i2c_call_clients(dev, VIDIOC_INT_I2S_CLOCK_FREQ, &dev->i2s_speed);
		route.input = dev->ctl_ainput;
		route.output = MSP_OUTPUT(MSP_SC_IN_DSP_SCART1);
		/* Note: this is msp3400 specific */
		em28xx_i2c_call_clients(dev, VIDIOC_INT_S_AUDIO_ROUTING, &route);
	}

	em28xx_set_audio_source(dev);
}

/* Usage lock check functions */
static int res_get(struct em28xx_fh *fh)
{
	struct em28xx    *dev = fh->dev;
	int		 rc   = 0;

	/* This instance already has stream_on */
	if (fh->stream_on)
		return rc;

	mutex_lock(&dev->lock);

	if (dev->stream_on)
		rc = -EINVAL;
	else {
		dev->stream_on = 1;
		fh->stream_on  = 1;
	}

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
 * em28xx_vm_open()
 */
static void em28xx_vm_open(struct vm_area_struct *vma)
{
	struct em28xx_frame_t *f = vma->vm_private_data;
	f->vma_use_count++;
}

/*
 * em28xx_vm_close()
 */
static void em28xx_vm_close(struct vm_area_struct *vma)
{
	/* NOTE: buffers are not freed here */
	struct em28xx_frame_t *f = vma->vm_private_data;

	if (f->vma_use_count)
		f->vma_use_count--;
}

static struct vm_operations_struct em28xx_vm_ops = {
	.open = em28xx_vm_open,
	.close = em28xx_vm_close,
};


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

/*
 * em28xx_stream_interrupt()
 * stops streaming
 */
static int em28xx_stream_interrupt(struct em28xx *dev)
{
	int rc = 0;

	/* stop reading from the device */

	dev->stream = STREAM_INTERRUPT;
	rc = wait_event_timeout(dev->wait_stream,
				(dev->stream == STREAM_OFF) ||
				(dev->state & DEV_DISCONNECTED),
				EM28XX_URB_TIMEOUT);

	if (rc) {
		dev->state |= DEV_MISCONFIGURED;
		em28xx_videodbg("device is misconfigured; close and "
			"open /dev/video%d again\n",
				dev->vdev->minor-MINOR_VFL_TYPE_GRABBER_MIN);
		return rc;
	}

	return 0;
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

static int vidioc_g_fmt_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;

	mutex_lock(&dev->lock);

	f->fmt.pix.width = dev->width;
	f->fmt.pix.height = dev->height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	f->fmt.pix.bytesperline = dev->bytesperline;
	f->fmt.pix.sizeimage = dev->frame_size;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	/* FIXME: TOP? NONE? BOTTOM? ALTENATE? */
	f->fmt.pix.field = dev->interlaced ?
			   V4L2_FIELD_INTERLACED : V4L2_FIELD_TOP;

	mutex_unlock(&dev->lock);
	return 0;
}

static int vidioc_try_fmt_cap(struct file *file, void *priv,
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

static int vidioc_s_fmt_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc, i;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	vidioc_try_fmt_cap(file, priv, f);

	mutex_lock(&dev->lock);

	for (i = 0; i < dev->num_frames; i++)
		if (dev->frame[i].vma_use_count) {
			em28xx_videodbg("VIDIOC_S_FMT failed. "
					"Unmap the buffers first.\n");
			rc = -EINVAL;
			goto err;
		}

	/* stop io in case it is already in progress */
	if (dev->stream == STREAM_ON) {
		em28xx_videodbg("VIDIOC_SET_FMT: interrupting stream\n");
		rc = em28xx_stream_interrupt(dev);
		if (rc < 0)
			goto err;
	}

	em28xx_release_buffers(dev);
	dev->io = IO_NONE;

	/* set new image size */
	dev->width = f->fmt.pix.width;
	dev->height = f->fmt.pix.height;
	dev->frame_size = dev->width * dev->height * 2;
	dev->field_size = dev->frame_size >> 1;
	dev->bytesperline = dev->width * 2;
	get_scale(dev, dev->width, dev->height, &dev->hscale, &dev->vscale);

	/* FIXME: This is really weird! Why capture is starting with
	   this ioctl ???
	 */
	em28xx_uninit_isoc(dev);
	em28xx_set_alternate(dev);
	em28xx_capture_start(dev, 1);
	em28xx_resolution_set(dev);
	em28xx_init_isoc(dev);
	rc = 0;

err:
	mutex_unlock(&dev->lock);
	return rc;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id *norm)
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
	vidioc_try_fmt_cap(file, priv, &f);

	mutex_lock(&dev->lock);

	/* set new image size */
	dev->width = f.fmt.pix.width;
	dev->height = f.fmt.pix.height;
	dev->frame_size = dev->width * dev->height * 2;
	dev->field_size = dev->frame_size >> 1;
	dev->bytesperline = dev->width * 2;
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

	if (index == 0) {
		strcpy(a->name, "Television");
	} else {
		strcpy(a->name, "Line In");
	}
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

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE || dev->io != IO_MMAP)
		return -EINVAL;

	if (list_empty(&dev->inqueue))
		return -EINVAL;

	mutex_lock(&dev->lock);

	if (unlikely(res_get(fh) < 0)) {
		mutex_unlock(&dev->lock);
		return -EBUSY;
	}

	dev->stream = STREAM_ON;	/* FIXME: Start video capture here? */

	mutex_unlock(&dev->lock);
	return 0;
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

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE || dev->io != IO_MMAP)
		return -EINVAL;

	mutex_lock(&dev->lock);

	if (dev->stream == STREAM_ON) {
		em28xx_videodbg("VIDIOC_STREAMOFF: interrupting stream\n");
		rc = em28xx_stream_interrupt(dev);
		if (rc < 0) {
			mutex_unlock(&dev->lock);
			return rc;
		}
	}

	em28xx_empty_framequeues(dev);

	mutex_unlock(&dev->lock);
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

static int vidioc_enum_fmt_cap(struct file *file, void  *priv,
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
static int vidioc_g_fmt_vbi_capture(struct file *file, void *priv,
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

static int vidioc_try_set_vbi_capture(struct file *file, void *priv,
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
	u32                   i;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (rb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
		rb->memory != V4L2_MEMORY_MMAP)
		return -EINVAL;

	if (dev->io == IO_READ) {
		em28xx_videodbg("method is set to read;"
				" close and open the device again to"
				" choose the mmap I/O method\n");
		return -EINVAL;
	}

	for (i = 0; i < dev->num_frames; i++)
		if (dev->frame[i].vma_use_count) {
			em28xx_videodbg("VIDIOC_REQBUFS failed; "
					"previous buffers are still mapped\n");
			return -EINVAL;
		}

	mutex_lock(&dev->lock);

	if (dev->stream == STREAM_ON) {
		em28xx_videodbg("VIDIOC_REQBUFS: interrupting stream\n");
		rc = em28xx_stream_interrupt(dev);
		if (rc < 0) {
			mutex_unlock(&dev->lock);
			return rc;
		}
	}

	em28xx_empty_framequeues(dev);

	em28xx_release_buffers(dev);
	if (rb->count)
		rb->count = em28xx_request_buffers(dev, rb->count);

	dev->frame_current = NULL;
	dev->io = rb->count ? IO_MMAP : IO_NONE;

	mutex_unlock(&dev->lock);
	return 0;
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

	if (b->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
		b->index >= dev->num_frames || dev->io != IO_MMAP)
		return -EINVAL;

	mutex_lock(&dev->lock);

	memcpy(b, &dev->frame[b->index].buf, sizeof(*b));

	if (dev->frame[b->index].vma_use_count)
		b->flags |= V4L2_BUF_FLAG_MAPPED;

	if (dev->frame[b->index].state == F_DONE)
		b->flags |= V4L2_BUF_FLAG_DONE;
	else if (dev->frame[b->index].state != F_UNUSED)
		b->flags |= V4L2_BUF_FLAG_QUEUED;

	mutex_unlock(&dev->lock);
	return 0;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	unsigned long         lock_flags;
	int                   rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (b->type != V4L2_BUF_TYPE_VIDEO_CAPTURE  || dev->io != IO_MMAP ||
						b->index >= dev->num_frames)
		return -EINVAL;

	if (dev->frame[b->index].state != F_UNUSED)
		return -EAGAIN;

	dev->frame[b->index].state = F_QUEUED;

	/* add frame to fifo */
	spin_lock_irqsave(&dev->queue_lock, lock_flags);
	list_add_tail(&dev->frame[b->index].frame, &dev->inqueue);
	spin_unlock_irqrestore(&dev->queue_lock, lock_flags);

	return 0;
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct em28xx_fh      *fh  = priv;
	struct em28xx         *dev = fh->dev;
	int                   rc;
	struct em28xx_frame_t *f;
	unsigned long         lock_flags;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (b->type != V4L2_BUF_TYPE_VIDEO_CAPTURE || dev->io != IO_MMAP)
		return -EINVAL;

	if (list_empty(&dev->outqueue)) {
		if (dev->stream == STREAM_OFF)
			return -EINVAL;

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		rc = wait_event_interruptible(dev->wait_frame,
					(!list_empty(&dev->outqueue)) ||
					(dev->state & DEV_DISCONNECTED));
		if (rc)
			return rc;

		if (dev->state & DEV_DISCONNECTED)
			return -ENODEV;
	}

	spin_lock_irqsave(&dev->queue_lock, lock_flags);
	f = list_entry(dev->outqueue.next, struct em28xx_frame_t, frame);
	list_del(dev->outqueue.next);
	spin_unlock_irqrestore(&dev->queue_lock, lock_flags);

	f->state = F_UNUSED;
	memcpy(b, &f->buf, sizeof(*b));

	if (f->vma_use_count)
		b->flags |= V4L2_BUF_FLAG_MAPPED;

	return 0;
}

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
	struct em28xx *h,*dev = NULL;
	struct em28xx_fh *fh;

	list_for_each_entry(h, &em28xx_devlist, devlist) {
		if (h->vdev->minor == minor) {
			dev  = h;
			dev->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		}
		if (h->vbi_dev->minor == minor) {
			dev  = h;
			dev->type = V4L2_BUF_TYPE_VBI_CAPTURE;
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
				minor,v4l2_type_names[dev->type],dev->users);

	fh = kzalloc(sizeof(struct em28xx_fh), GFP_KERNEL);

	if (!fh) {
		em28xx_errdev("em28xx-video.c: Out of memory?!\n");
		return -ENOMEM;
	}
	mutex_lock(&dev->lock);
	fh->dev = dev;
	fh->radio = radio;
	filp->private_data = fh;

	if (dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE && dev->users == 0) {
		em28xx_set_alternate(dev);

		dev->width = norm_maxw(dev);
		dev->height = norm_maxh(dev);
		dev->frame_size = dev->width * dev->height * 2;
		dev->field_size = dev->frame_size >> 1;	/*both_fileds ? dev->frame_size>>1 : dev->frame_size; */
		dev->bytesperline = dev->width * 2;
		dev->hscale = 0;
		dev->vscale = 0;

		em28xx_capture_start(dev, 1);
		em28xx_resolution_set(dev);


		/* start the transfer */
		errCode = em28xx_init_isoc(dev);
		if (errCode)
			goto err;

		em28xx_empty_framequeues(dev);
	}
	if (fh->radio) {
		em28xx_videodbg("video_open: setting radio device\n");
		em28xx_i2c_call_clients(dev, AUDC_SET_RADIO, NULL);
	}

	dev->users++;

err:
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
	em28xx_devused&=~(1<<dev->devno);
}

/*
 * em28xx_v4l2_close()
 * stops streaming and deallocates all resources allocated by the v4l2 calls and ioctls
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
		em28xx_uninit_isoc(dev);
		em28xx_release_buffers(dev);
		dev->io = IO_NONE;

		/* the device is already disconnect,
		   free the remaining resources */
		if (dev->state & DEV_DISCONNECTED) {
			em28xx_release_resources(dev);
			mutex_unlock(&dev->lock);
			kfree(dev);
			return 0;
		}

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
em28xx_v4l2_read(struct file *filp, char __user * buf, size_t count,
		 loff_t * f_pos)
{
	struct em28xx_frame_t *f, *i;
	unsigned long lock_flags;
	int ret = 0;
	struct em28xx_fh *fh = filp->private_data;
	struct em28xx *dev = fh->dev;

	/* FIXME: read() is not prepared to allow changing the video
	   resolution while streaming. Seems a bug at em28xx_set_fmt
	 */

	if (unlikely(res_get(fh) < 0))
		return -EBUSY;

	mutex_lock(&dev->lock);

	if (dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		em28xx_videodbg("V4l2_Buf_type_videocapture is set\n");

	if (dev->type == V4L2_BUF_TYPE_VBI_CAPTURE) {
		em28xx_videodbg("V4L2_BUF_TYPE_VBI_CAPTURE is set\n");
		em28xx_videodbg("not supported yet! ...\n");
		if (copy_to_user(buf, "", 1)) {
			mutex_unlock(&dev->lock);
			return -EFAULT;
		}
		mutex_unlock(&dev->lock);
		return (1);
	}
	if (dev->type == V4L2_BUF_TYPE_SLICED_VBI_CAPTURE) {
		em28xx_videodbg("V4L2_BUF_TYPE_SLICED_VBI_CAPTURE is set\n");
		em28xx_videodbg("not supported yet! ...\n");
		if (copy_to_user(buf, "", 1)) {
			mutex_unlock(&dev->lock);
			return -EFAULT;
		}
		mutex_unlock(&dev->lock);
		return (1);
	}

	if (dev->state & DEV_DISCONNECTED) {
		em28xx_videodbg("device not present\n");
		mutex_unlock(&dev->lock);
		return -ENODEV;
	}

	if (dev->state & DEV_MISCONFIGURED) {
		em28xx_videodbg("device misconfigured; close and open it again\n");
		mutex_unlock(&dev->lock);
		return -EIO;
	}

	if (dev->io == IO_MMAP) {
		em28xx_videodbg ("IO method is set to mmap; close and open"
				" the device again to choose the read method\n");
		mutex_unlock(&dev->lock);
		return -EINVAL;
	}

	if (dev->io == IO_NONE) {
		if (!em28xx_request_buffers(dev, EM28XX_NUM_READ_FRAMES)) {
			em28xx_errdev("read failed, not enough memory\n");
			mutex_unlock(&dev->lock);
			return -ENOMEM;
		}
		dev->io = IO_READ;
		dev->stream = STREAM_ON;
		em28xx_queue_unusedframes(dev);
	}

	if (!count) {
		mutex_unlock(&dev->lock);
		return 0;
	}

	if (list_empty(&dev->outqueue)) {
		if (filp->f_flags & O_NONBLOCK) {
			mutex_unlock(&dev->lock);
			return -EAGAIN;
		}
		ret = wait_event_interruptible
		    (dev->wait_frame,
		     (!list_empty(&dev->outqueue)) ||
		     (dev->state & DEV_DISCONNECTED));
		if (ret) {
			mutex_unlock(&dev->lock);
			return ret;
		}
		if (dev->state & DEV_DISCONNECTED) {
			mutex_unlock(&dev->lock);
			return -ENODEV;
		}
		dev->video_bytesread = 0;
	}

	f = list_entry(dev->outqueue.prev, struct em28xx_frame_t, frame);

	em28xx_queue_unusedframes(dev);

	if (count > f->buf.length)
		count = f->buf.length;

	if ((dev->video_bytesread + count) > dev->frame_size)
		count = dev->frame_size - dev->video_bytesread;

	if (copy_to_user(buf, f->bufmem+dev->video_bytesread, count)) {
		em28xx_err("Error while copying to user\n");
		return -EFAULT;
	}
	dev->video_bytesread += count;

	if (dev->video_bytesread == dev->frame_size) {
		spin_lock_irqsave(&dev->queue_lock, lock_flags);
		list_for_each_entry(i, &dev->outqueue, frame)
				    i->state = F_UNUSED;
		INIT_LIST_HEAD(&dev->outqueue);
		spin_unlock_irqrestore(&dev->queue_lock, lock_flags);

		em28xx_queue_unusedframes(dev);
		dev->video_bytesread = 0;
	}

	*f_pos += count;

	mutex_unlock(&dev->lock);

	return count;
}

/*
 * em28xx_v4l2_poll()
 * will allocate buffers when called for the first time
 */
static unsigned int em28xx_v4l2_poll(struct file *filp, poll_table * wait)
{
	unsigned int mask = 0;
	struct em28xx_fh *fh = filp->private_data;
	struct em28xx *dev = fh->dev;

	if (unlikely(res_get(fh) < 0))
		return POLLERR;

	mutex_lock(&dev->lock);

	if (dev->state & DEV_DISCONNECTED) {
		em28xx_videodbg("device not present\n");
	} else if (dev->state & DEV_MISCONFIGURED) {
		em28xx_videodbg("device is misconfigured; close and open it again\n");
	} else {
		if (dev->io == IO_NONE) {
			if (!em28xx_request_buffers
			    (dev, EM28XX_NUM_READ_FRAMES)) {
				em28xx_warn
				    ("poll() failed, not enough memory\n");
			} else {
				dev->io = IO_READ;
				dev->stream = STREAM_ON;
			}
		}

		if (dev->io == IO_READ) {
			em28xx_queue_unusedframes(dev);
			poll_wait(filp, &dev->wait_frame, wait);

			if (!list_empty(&dev->outqueue))
				mask |= POLLIN | POLLRDNORM;

			mutex_unlock(&dev->lock);

			return mask;
		}
	}

	mutex_unlock(&dev->lock);
	return POLLERR;
}

/*
 * em28xx_v4l2_mmap()
 */
static int em28xx_v4l2_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct em28xx_fh *fh    = filp->private_data;
	struct em28xx	 *dev   = fh->dev;
	unsigned long	 size   = vma->vm_end - vma->vm_start;
	unsigned long	 start  = vma->vm_start;
	void 		 *pos;
	u32		 i;

	if (unlikely(res_get(fh) < 0))
		return -EBUSY;

	mutex_lock(&dev->lock);

	if (dev->state & DEV_DISCONNECTED) {
		em28xx_videodbg("mmap: device not present\n");
		mutex_unlock(&dev->lock);
		return -ENODEV;
	}

	if (dev->state & DEV_MISCONFIGURED) {
		em28xx_videodbg ("mmap: Device is misconfigured; close and "
						"open it again\n");
		mutex_unlock(&dev->lock);
		return -EIO;
	}

	if (dev->io != IO_MMAP || !(vma->vm_flags & VM_WRITE)) {
		mutex_unlock(&dev->lock);
		return -EINVAL;
	}

	if (size > PAGE_ALIGN(dev->frame[0].buf.length))
		size = PAGE_ALIGN(dev->frame[0].buf.length);

	for (i = 0; i < dev->num_frames; i++) {
		if ((dev->frame[i].buf.m.offset >> PAGE_SHIFT) == vma->vm_pgoff)
			break;
	}
	if (i == dev->num_frames) {
		em28xx_videodbg("mmap: user supplied mapping address is out of range\n");
		mutex_unlock(&dev->lock);
		return -EINVAL;
	}

	/* VM_IO is eventually going to replace PageReserved altogether */
	vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_RESERVED;	/* avoid to swap out this VMA */

	pos = dev->frame[i].bufmem;
	while (size > 0) {	/* size is page-aligned */
		if (vm_insert_page(vma, start, vmalloc_to_page(pos))) {
			em28xx_videodbg("mmap: vm_insert_page failed\n");
			mutex_unlock(&dev->lock);
			return -EAGAIN;
		}
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	vma->vm_ops = &em28xx_vm_ops;
	vma->vm_private_data = &dev->frame[i];

	em28xx_vm_open(vma);
	mutex_unlock(&dev->lock);
	return 0;
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

static const struct file_operations radio_fops = {
	.owner         = THIS_MODULE,
	.open          = em28xx_v4l2_open,
	.release       = em28xx_v4l2_close,
	.ioctl	       = video_ioctl2,
	.compat_ioctl  = v4l_compat_ioctl32,
	.llseek        = no_llseek,
};

static const struct video_device em28xx_video_template = {
	.fops                       = &em28xx_v4l_fops,
	.release                    = video_device_release,

	.minor                      = -1,
	.vidioc_querycap            = vidioc_querycap,
	.vidioc_enum_fmt_cap        = vidioc_enum_fmt_cap,
	.vidioc_g_fmt_cap           = vidioc_g_fmt_cap,
	.vidioc_try_fmt_cap         = vidioc_try_fmt_cap,
	.vidioc_s_fmt_cap           = vidioc_s_fmt_cap,
	.vidioc_g_audio             = vidioc_g_audio,
	.vidioc_s_audio             = vidioc_s_audio,
	.vidioc_cropcap             = vidioc_cropcap,

	.vidioc_g_fmt_vbi_capture   = vidioc_g_fmt_vbi_capture,
	.vidioc_try_fmt_vbi_capture = vidioc_try_set_vbi_capture,
	.vidioc_s_fmt_vbi_capture   = vidioc_try_set_vbi_capture,

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

	.tvnorms                    = V4L2_STD_ALL,
	.current_norm               = V4L2_STD_PAL,
};

static struct video_device em28xx_radio_template = {
	.name                 = "em28xx-radio",
	.type                 = VID_TYPE_TUNER,
	.fops                 = &radio_fops,
	.minor                = -1,
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
};

/******************************** usb interface *****************************************/


static LIST_HEAD(em28xx_extension_devlist);
static DEFINE_MUTEX(em28xx_extension_devlist_lock);

int em28xx_register_extension(struct em28xx_ops *ops)
{
	struct em28xx *h, *dev = NULL;

	list_for_each_entry(h, &em28xx_devlist, devlist)
		dev = h;

	mutex_lock(&em28xx_extension_devlist_lock);
	list_add_tail(&ops->next, &em28xx_extension_devlist);
	if (dev)
		ops->init(dev);

	printk(KERN_INFO "Em28xx: Initialized (%s) extension\n", ops->name);
	mutex_unlock(&em28xx_extension_devlist_lock);

	return 0;
}
EXPORT_SYMBOL(em28xx_register_extension);

void em28xx_unregister_extension(struct em28xx_ops *ops)
{
	struct em28xx *h, *dev = NULL;

	list_for_each_entry(h, &em28xx_devlist, devlist)
		dev = h;

	if (dev)
		ops->fini(dev);

	mutex_lock(&em28xx_extension_devlist_lock);
	printk(KERN_INFO "Em28xx: Removed (%s) extension\n", ops->name);
	list_del(&ops->next);
	mutex_unlock(&em28xx_extension_devlist_lock);
}
EXPORT_SYMBOL(em28xx_unregister_extension);

struct video_device *em28xx_vdev_init(struct em28xx *dev,
				      const struct video_device *template,
				      const int type,
				      const char *type_name)
{
	struct video_device *vfd;

	vfd = video_device_alloc();
	if (NULL == vfd)
		return NULL;
	*vfd = *template;
	vfd->minor   = -1;
	vfd->dev = &dev->udev->dev;
	vfd->release = video_device_release;
	vfd->type = type;

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
	spin_lock_init(&dev->queue_lock);
	init_waitqueue_head(&dev->open);
	init_waitqueue_head(&dev->wait_frame);
	init_waitqueue_head(&dev->wait_stream);

	dev->em28xx_write_regs = em28xx_write_regs;
	dev->em28xx_read_reg = em28xx_read_reg;
	dev->em28xx_read_reg_req_len = em28xx_read_reg_req_len;
	dev->em28xx_write_regs_req = em28xx_write_regs_req;
	dev->em28xx_read_reg_req = em28xx_read_reg_req;
	dev->is_em2800 = em28xx_boards[dev->model].is_em2800;

	errCode = em28xx_read_reg(dev, CHIPID_REG);
	if (errCode >= 0)
		em28xx_info("em28xx chip ID = %d\n", errCode);

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
	dev->field_size = dev->width * dev->height;
	dev->frame_size =
	    dev->interlaced ? dev->field_size << 1 : dev->field_size;
	dev->bytesperline = dev->width * 2;
	dev->hscale = 0;
	dev->vscale = 0;
	dev->ctl_input = 2;

	errCode = em28xx_config(dev);

	list_add_tail(&dev->devlist, &em28xx_devlist);

	/* allocate and fill video video_device struct */
	dev->vdev = em28xx_vdev_init(dev, &em28xx_video_template,
					  VID_TYPE_CAPTURE, "video");
	if (NULL == dev->vdev) {
		em28xx_errdev("cannot allocate video_device.\n");
		goto fail_unreg;
	}
	if (dev->tuner_type != TUNER_ABSENT)
		dev->vdev->type |= VID_TYPE_TUNER;

	/* register v4l2 video video_device */
	retval = video_register_device(dev->vdev, VFL_TYPE_GRABBER,
				       video_nr[dev->devno]);
	if (retval) {
		em28xx_errdev("unable to register video device (error=%i).\n",
			      retval);
		goto fail_unreg;
	}

	/* Allocate and fill vbi video_device struct */
	dev->vbi_dev = em28xx_vdev_init(dev, &em28xx_video_template,
					  VFL_TYPE_VBI, "vbi");
	/* register v4l2 vbi video_device */
	if (video_register_device(dev->vbi_dev, VFL_TYPE_VBI,
					vbi_nr[dev->devno]) < 0) {
		em28xx_errdev("unable to register vbi device\n");
		retval = -ENODEV;
		goto fail_unreg;
	}

	if (em28xx_boards[dev->model].radio.type == EM28XX_RADIO) {
		dev->radio_dev = em28xx_vdev_init(dev, &em28xx_radio_template,
					VFL_TYPE_RADIO, "radio");
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
	nr=find_first_zero_bit(&em28xx_devused,EM28XX_MAXBOARDS);
	em28xx_devused|=1<<nr;

	/* Don't register audio interfaces */
	if (interface->altsetting[0].desc.bInterfaceClass == USB_CLASS_AUDIO) {
		em28xx_err(DRIVER_NAME " audio device (%04x:%04x): interface %i, class %i\n",
				udev->descriptor.idVendor,udev->descriptor.idProduct,
				ifnum,
				interface->altsetting[0].desc.bInterfaceClass);

		em28xx_devused&=~(1<<nr);
		return -ENODEV;
	}

	em28xx_err(DRIVER_NAME " new video device (%04x:%04x): interface %i, class %i\n",
			udev->descriptor.idVendor,udev->descriptor.idProduct,
			ifnum,
			interface->altsetting[0].desc.bInterfaceClass);

	endpoint = &interface->cur_altsetting->endpoint[1].desc;

	/* check if the device has the iso in endpoint at the correct place */
	if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) !=
	    USB_ENDPOINT_XFER_ISOC) {
		em28xx_err(DRIVER_NAME " probing error: endpoint is non-ISO endpoint!\n");
		em28xx_devused&=~(1<<nr);
		return -ENODEV;
	}
	if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT) {
		em28xx_err(DRIVER_NAME " probing error: endpoint is ISO OUT endpoint!\n");
		em28xx_devused&=~(1<<nr);
		return -ENODEV;
	}

	if (nr >= EM28XX_MAXBOARDS) {
		printk (DRIVER_NAME ": Supports only %i em28xx boards.\n",EM28XX_MAXBOARDS);
		em28xx_devused&=~(1<<nr);
		return -ENOMEM;
	}

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		em28xx_err(DRIVER_NAME ": out of memory!\n");
		em28xx_devused&=~(1<<nr);
		return -ENOMEM;
	}

	snprintf(dev->name, 29, "em28xx #%d", nr);
	dev->devno = nr;
	dev->model = id->driver_info;

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

	dev->num_alt=uif->num_altsetting;
	em28xx_info("Alternate settings: %i\n",dev->num_alt);
//	dev->alt_max_pkt_size = kmalloc(sizeof(*dev->alt_max_pkt_size)*
	dev->alt_max_pkt_size = kmalloc(32*
						dev->num_alt,GFP_KERNEL);
	if (dev->alt_max_pkt_size == NULL) {
		em28xx_errdev("out of memory!\n");
		em28xx_devused&=~(1<<nr);
		kfree(dev);
		return -ENOMEM;
	}

	for (i = 0; i < dev->num_alt ; i++) {
		u16 tmp = le16_to_cpu(uif->altsetting[i].endpoint[1].desc.
							wMaxPacketSize);
		dev->alt_max_pkt_size[i] =
		    (tmp & 0x07ff) * (((tmp & 0x1800) >> 11) + 1);
		em28xx_info("Alternate setting %i, max size= %i\n",i,
							dev->alt_max_pkt_size[i]);
	}

	if ((card[nr]>=0)&&(card[nr]<em28xx_bcount))
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

	/* wait until all current v4l2 io is finished then deallocate resources */
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

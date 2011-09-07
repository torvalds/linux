/****************************************************************************
 *
 *  Filename: cpia2_v4l.c
 *
 *  Copyright 2001, STMicrolectronics, Inc.
 *      Contact:  steve.miller@st.com
 *  Copyright 2001,2005, Scott J. Bertin <scottbertin@yahoo.com>
 *
 *  Description:
 *     This is a USB driver for CPia2 based video cameras.
 *     The infrastructure of this driver is based on the cpia usb driver by
 *     Jochen Scharrlach and Johannes Erdfeldt.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Stripped of 2.4 stuff ready for main kernel submit by
 *		Alan Cox <alan@lxorguk.ukuu.org.uk>
 ****************************************************************************/

#define CPIA_VERSION "3.0.1"

#include <linux/module.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/videodev2.h>
#include <linux/stringify.h>
#include <media/v4l2-ioctl.h>

#include "cpia2.h"
#include "cpia2dev.h"

static int video_nr = -1;
module_param(video_nr, int, 0);
MODULE_PARM_DESC(video_nr,"video device to register (0=/dev/video0, etc)");

static int buffer_size = 68*1024;
module_param(buffer_size, int, 0);
MODULE_PARM_DESC(buffer_size, "Size for each frame buffer in bytes (default 68k)");

static int num_buffers = 3;
module_param(num_buffers, int, 0);
MODULE_PARM_DESC(num_buffers, "Number of frame buffers (1-"
		 __stringify(VIDEO_MAX_FRAME) ", default 3)");

static int alternate = DEFAULT_ALT;
module_param(alternate, int, 0);
MODULE_PARM_DESC(alternate, "USB Alternate (" __stringify(USBIF_ISO_1) "-"
		 __stringify(USBIF_ISO_6) ", default "
		 __stringify(DEFAULT_ALT) ")");

static int flicker_freq = 60;
module_param(flicker_freq, int, 0);
MODULE_PARM_DESC(flicker_freq, "Flicker frequency (" __stringify(50) "or"
		 __stringify(60) ", default "
		 __stringify(60) ")");

static int flicker_mode = NEVER_FLICKER;
module_param(flicker_mode, int, 0);
MODULE_PARM_DESC(flicker_mode,
		 "Flicker supression (" __stringify(NEVER_FLICKER) "or"
		 __stringify(ANTI_FLICKER_ON) ", default "
		 __stringify(NEVER_FLICKER) ")");

MODULE_AUTHOR("Steve Miller (STMicroelectronics) <steve.miller@st.com>");
MODULE_DESCRIPTION("V4L-driver for STMicroelectronics CPiA2 based cameras");
MODULE_SUPPORTED_DEVICE("video");
MODULE_LICENSE("GPL");
MODULE_VERSION(CPIA_VERSION);

#define ABOUT "V4L-Driver for Vision CPiA2 based cameras"

struct control_menu_info {
	int value;
	char name[32];
};

static struct control_menu_info framerate_controls[] =
{
	{ CPIA2_VP_FRAMERATE_6_25, "6.25 fps" },
	{ CPIA2_VP_FRAMERATE_7_5,  "7.5 fps"  },
	{ CPIA2_VP_FRAMERATE_12_5, "12.5 fps" },
	{ CPIA2_VP_FRAMERATE_15,   "15 fps"   },
	{ CPIA2_VP_FRAMERATE_25,   "25 fps"   },
	{ CPIA2_VP_FRAMERATE_30,   "30 fps"   },
};
#define NUM_FRAMERATE_CONTROLS (ARRAY_SIZE(framerate_controls))

static struct control_menu_info flicker_controls[] =
{
	{ NEVER_FLICKER, "Off" },
	{ FLICKER_50,    "50 Hz" },
	{ FLICKER_60,    "60 Hz"  },
};
#define NUM_FLICKER_CONTROLS (ARRAY_SIZE(flicker_controls))

static struct control_menu_info lights_controls[] =
{
	{ 0,   "Off" },
	{ 64,  "Top" },
	{ 128, "Bottom"  },
	{ 192, "Both"  },
};
#define NUM_LIGHTS_CONTROLS (ARRAY_SIZE(lights_controls))
#define GPIO_LIGHTS_MASK 192

static struct v4l2_queryctrl controls[] = {
	{
		.id            = V4L2_CID_BRIGHTNESS,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Brightness",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = DEFAULT_BRIGHTNESS,
	},
	{
		.id            = V4L2_CID_CONTRAST,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Contrast",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = DEFAULT_CONTRAST,
	},
	{
		.id            = V4L2_CID_SATURATION,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Saturation",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = DEFAULT_SATURATION,
	},
	{
		.id            = V4L2_CID_HFLIP,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
		.name          = "Mirror Horizontally",
		.minimum       = 0,
		.maximum       = 1,
		.step          = 1,
		.default_value = 0,
	},
	{
		.id            = V4L2_CID_VFLIP,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
		.name          = "Flip Vertically",
		.minimum       = 0,
		.maximum       = 1,
		.step          = 1,
		.default_value = 0,
	},
	{
		.id            = CPIA2_CID_TARGET_KB,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Target KB",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = DEFAULT_TARGET_KB,
	},
	{
		.id            = CPIA2_CID_GPIO,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "GPIO",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 0,
	},
	{
		.id            = CPIA2_CID_FLICKER_MODE,
		.type          = V4L2_CTRL_TYPE_MENU,
		.name          = "Flicker Reduction",
		.minimum       = 0,
		.maximum       = NUM_FLICKER_CONTROLS-1,
		.step          = 1,
		.default_value = 0,
	},
	{
		.id            = CPIA2_CID_FRAMERATE,
		.type          = V4L2_CTRL_TYPE_MENU,
		.name          = "Framerate",
		.minimum       = 0,
		.maximum       = NUM_FRAMERATE_CONTROLS-1,
		.step          = 1,
		.default_value = NUM_FRAMERATE_CONTROLS-1,
	},
	{
		.id            = CPIA2_CID_USB_ALT,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "USB Alternate",
		.minimum       = USBIF_ISO_1,
		.maximum       = USBIF_ISO_6,
		.step          = 1,
		.default_value = DEFAULT_ALT,
	},
	{
		.id            = CPIA2_CID_LIGHTS,
		.type          = V4L2_CTRL_TYPE_MENU,
		.name          = "Lights",
		.minimum       = 0,
		.maximum       = NUM_LIGHTS_CONTROLS-1,
		.step          = 1,
		.default_value = 0,
	},
	{
		.id            = CPIA2_CID_RESET_CAMERA,
		.type          = V4L2_CTRL_TYPE_BUTTON,
		.name          = "Reset Camera",
		.minimum       = 0,
		.maximum       = 0,
		.step          = 0,
		.default_value = 0,
	},
};
#define NUM_CONTROLS (ARRAY_SIZE(controls))


/******************************************************************************
 *
 *  cpia2_open
 *
 *****************************************************************************/
static int cpia2_open(struct file *file)
{
	struct camera_data *cam = video_drvdata(file);
	struct cpia2_fh *fh;

	if (!cam) {
		ERR("Internal error, camera_data not found!\n");
		return -ENODEV;
	}

	if (!cam->present)
		return -ENODEV;

	if (cam->open_count == 0) {
		if (cpia2_allocate_buffers(cam))
			return -ENOMEM;

		/* reset the camera */
		if (cpia2_reset_camera(cam) < 0)
			return -EIO;

		cam->APP_len = 0;
		cam->COM_len = 0;
	}

	fh = kmalloc(sizeof(*fh), GFP_KERNEL);
	if (!fh)
		return -ENOMEM;
	file->private_data = fh;
	fh->prio = V4L2_PRIORITY_UNSET;
	v4l2_prio_open(&cam->prio, &fh->prio);
	fh->mmapped = 0;

	++cam->open_count;

	cpia2_dbg_dump_registers(cam);
	return 0;
}

/******************************************************************************
 *
 *  cpia2_close
 *
 *****************************************************************************/
static int cpia2_close(struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct camera_data *cam = video_get_drvdata(dev);
	struct cpia2_fh *fh = file->private_data;

	if (cam->present &&
	    (cam->open_count == 1 || fh->prio == V4L2_PRIORITY_RECORD)) {
		cpia2_usb_stream_stop(cam);

		if (cam->open_count == 1) {
			/* save camera state for later open */
			cpia2_save_camera_state(cam);

			cpia2_set_low_power(cam);
			cpia2_free_buffers(cam);
		}
	}

	if (fh->mmapped)
		cam->mmapped = 0;
	v4l2_prio_close(&cam->prio, fh->prio);
	file->private_data = NULL;
	kfree(fh);

	if (--cam->open_count == 0) {
		cpia2_free_buffers(cam);
		if (!cam->present) {
			video_unregister_device(dev);
			kfree(cam);
			return 0;
		}
	}

	return 0;
}

/******************************************************************************
 *
 *  cpia2_v4l_read
 *
 *****************************************************************************/
static ssize_t cpia2_v4l_read(struct file *file, char __user *buf, size_t count,
			      loff_t *off)
{
	struct camera_data *cam = video_drvdata(file);
	int noblock = file->f_flags&O_NONBLOCK;

	struct cpia2_fh *fh = file->private_data;

	if(!cam)
		return -EINVAL;

	/* Priority check */
	if(fh->prio != V4L2_PRIORITY_RECORD) {
		return -EBUSY;
	}

	return cpia2_read(cam, buf, count, noblock);
}


/******************************************************************************
 *
 *  cpia2_v4l_poll
 *
 *****************************************************************************/
static unsigned int cpia2_v4l_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct camera_data *cam = video_drvdata(filp);
	struct cpia2_fh *fh = filp->private_data;

	if(!cam)
		return POLLERR;

	/* Priority check */
	if(fh->prio != V4L2_PRIORITY_RECORD) {
		return POLLERR;
	}

	return cpia2_poll(cam, filp, wait);
}


static int sync(struct camera_data *cam, int frame_nr)
{
	struct framebuf *frame = &cam->buffers[frame_nr];

	while (1) {
		if (frame->status == FRAME_READY)
			return 0;

		if (!cam->streaming) {
			frame->status = FRAME_READY;
			frame->length = 0;
			return 0;
		}

		mutex_unlock(&cam->v4l2_lock);
		wait_event_interruptible(cam->wq_stream,
					 !cam->streaming ||
					 frame->status == FRAME_READY);
		mutex_lock(&cam->v4l2_lock);
		if (signal_pending(current))
			return -ERESTARTSYS;
		if(!cam->present)
			return -ENOTTY;
	}
}

/******************************************************************************
 *
 *  ioctl_set_gpio
 *
 *****************************************************************************/

static long cpia2_default(struct file *file, void *fh, bool valid_prio,
			  int cmd, void *arg)
{
	struct camera_data *cam = video_drvdata(file);
	__u32 gpio_val;

	if (cmd != CPIA2_CID_GPIO)
		return -EINVAL;

	gpio_val = *(__u32*) arg;

	if (gpio_val &~ 0xFFU)
		return -EINVAL;

	return cpia2_set_gpio(cam, (unsigned char)gpio_val);
}

/******************************************************************************
 *
 *  ioctl_querycap
 *
 *  V4L2 device capabilities
 *
 *****************************************************************************/

static int cpia2_querycap(struct file *file, void *fh, struct v4l2_capability *vc)
{
	struct camera_data *cam = video_drvdata(file);

	strcpy(vc->driver, "cpia2");

	if (cam->params.pnp_id.product == 0x151)
		strcpy(vc->card, "QX5 Microscope");
	else
		strcpy(vc->card, "CPiA2 Camera");
	switch (cam->params.pnp_id.device_type) {
	case DEVICE_STV_672:
		strcat(vc->card, " (672/");
		break;
	case DEVICE_STV_676:
		strcat(vc->card, " (676/");
		break;
	default:
		strcat(vc->card, " (XXX/");
		break;
	}
	switch (cam->params.version.sensor_flags) {
	case CPIA2_VP_SENSOR_FLAGS_404:
		strcat(vc->card, "404)");
		break;
	case CPIA2_VP_SENSOR_FLAGS_407:
		strcat(vc->card, "407)");
		break;
	case CPIA2_VP_SENSOR_FLAGS_409:
		strcat(vc->card, "409)");
		break;
	case CPIA2_VP_SENSOR_FLAGS_410:
		strcat(vc->card, "410)");
		break;
	case CPIA2_VP_SENSOR_FLAGS_500:
		strcat(vc->card, "500)");
		break;
	default:
		strcat(vc->card, "XXX)");
		break;
	}

	if (usb_make_path(cam->dev, vc->bus_info, sizeof(vc->bus_info)) <0)
		memset(vc->bus_info,0, sizeof(vc->bus_info));

	vc->capabilities = V4L2_CAP_VIDEO_CAPTURE |
			   V4L2_CAP_READWRITE |
			   V4L2_CAP_STREAMING;

	return 0;
}

/******************************************************************************
 *
 *  ioctl_input
 *
 *  V4L2 input get/set/enumerate
 *
 *****************************************************************************/

static int cpia2_enum_input(struct file *file, void *fh, struct v4l2_input *i)
{
	if (i->index)
		return -EINVAL;
	strcpy(i->name, "Camera");
	i->type = V4L2_INPUT_TYPE_CAMERA;
	return 0;
}

static int cpia2_g_input(struct file *file, void *fh, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int cpia2_s_input(struct file *file, void *fh, unsigned int i)
{
	return i ? -EINVAL : 0;
}

/******************************************************************************
 *
 *  ioctl_enum_fmt
 *
 *  V4L2 format enumerate
 *
 *****************************************************************************/

static int cpia2_enum_fmt_vid_cap(struct file *file, void *fh,
					    struct v4l2_fmtdesc *f)
{
	int index = f->index;

	if (index < 0 || index > 1)
	       return -EINVAL;

	memset(f, 0, sizeof(*f));
	f->index = index;
	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f->flags = V4L2_FMT_FLAG_COMPRESSED;
	switch(index) {
	case 0:
		strcpy(f->description, "MJPEG");
		f->pixelformat = V4L2_PIX_FMT_MJPEG;
		break;
	case 1:
		strcpy(f->description, "JPEG");
		f->pixelformat = V4L2_PIX_FMT_JPEG;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/******************************************************************************
 *
 *  ioctl_try_fmt
 *
 *  V4L2 format try
 *
 *****************************************************************************/

static int cpia2_try_fmt_vid_cap(struct file *file, void *fh,
					  struct v4l2_format *f)
{
	struct camera_data *cam = video_drvdata(file);

	if (f->fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG &&
	    f->fmt.pix.pixelformat != V4L2_PIX_FMT_JPEG)
	       return -EINVAL;

	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage = cam->frame_size;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	f->fmt.pix.priv = 0;

	switch (cpia2_match_video_size(f->fmt.pix.width, f->fmt.pix.height)) {
	case VIDEOSIZE_VGA:
		f->fmt.pix.width = 640;
		f->fmt.pix.height = 480;
		break;
	case VIDEOSIZE_CIF:
		f->fmt.pix.width = 352;
		f->fmt.pix.height = 288;
		break;
	case VIDEOSIZE_QVGA:
		f->fmt.pix.width = 320;
		f->fmt.pix.height = 240;
		break;
	case VIDEOSIZE_288_216:
		f->fmt.pix.width = 288;
		f->fmt.pix.height = 216;
		break;
	case VIDEOSIZE_256_192:
		f->fmt.pix.width = 256;
		f->fmt.pix.height = 192;
		break;
	case VIDEOSIZE_224_168:
		f->fmt.pix.width = 224;
		f->fmt.pix.height = 168;
		break;
	case VIDEOSIZE_192_144:
		f->fmt.pix.width = 192;
		f->fmt.pix.height = 144;
		break;
	case VIDEOSIZE_QCIF:
	default:
		f->fmt.pix.width = 176;
		f->fmt.pix.height = 144;
		break;
	}

	return 0;
}

/******************************************************************************
 *
 *  ioctl_set_fmt
 *
 *  V4L2 format set
 *
 *****************************************************************************/

static int cpia2_s_fmt_vid_cap(struct file *file, void *_fh,
					struct v4l2_format *f)
{
	struct camera_data *cam = video_drvdata(file);
	struct cpia2_fh *fh = _fh;
	int err, frame;

	err = v4l2_prio_check(&cam->prio, fh->prio);
	if (err)
		return err;
	err = cpia2_try_fmt_vid_cap(file, _fh, f);
	if(err != 0)
		return err;

	/* Ensure that only this process can change the format. */
	err = v4l2_prio_change(&cam->prio, &fh->prio, V4L2_PRIORITY_RECORD);
	if(err != 0) {
		return err;
	}

	cam->pixelformat = f->fmt.pix.pixelformat;

	/* NOTE: This should be set to 1 for MJPEG, but some apps don't handle
	 * the missing Huffman table properly. */
	cam->params.compression.inhibit_htables = 0;
		/*f->fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG;*/

	/* we set the video window to something smaller or equal to what
	 * is requested by the user???
	 */
	DBG("Requested width = %d, height = %d\n",
	    f->fmt.pix.width, f->fmt.pix.height);
	if (f->fmt.pix.width != cam->width ||
	    f->fmt.pix.height != cam->height) {
		cam->width = f->fmt.pix.width;
		cam->height = f->fmt.pix.height;
		cam->params.roi.width = f->fmt.pix.width;
		cam->params.roi.height = f->fmt.pix.height;
		cpia2_set_format(cam);
	}

	for (frame = 0; frame < cam->num_frames; ++frame) {
		if (cam->buffers[frame].status == FRAME_READING)
			if ((err = sync(cam, frame)) < 0)
				return err;

		cam->buffers[frame].status = FRAME_EMPTY;
	}

	return 0;
}

/******************************************************************************
 *
 *  ioctl_get_fmt
 *
 *  V4L2 format get
 *
 *****************************************************************************/

static int cpia2_g_fmt_vid_cap(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct camera_data *cam = video_drvdata(file);

	f->fmt.pix.width = cam->width;
	f->fmt.pix.height = cam->height;
	f->fmt.pix.pixelformat = cam->pixelformat;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage = cam->frame_size;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	f->fmt.pix.priv = 0;

	return 0;
}

/******************************************************************************
 *
 *  ioctl_cropcap
 *
 *  V4L2 query cropping capabilities
 *  NOTE: cropping is currently disabled
 *
 *****************************************************************************/

static int cpia2_cropcap(struct file *file, void *fh, struct v4l2_cropcap *c)
{
	struct camera_data *cam = video_drvdata(file);

	if (c->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
	       return -EINVAL;

	c->bounds.left = 0;
	c->bounds.top = 0;
	c->bounds.width = cam->width;
	c->bounds.height = cam->height;
	c->defrect.left = 0;
	c->defrect.top = 0;
	c->defrect.width = cam->width;
	c->defrect.height = cam->height;
	c->pixelaspect.numerator = 1;
	c->pixelaspect.denominator = 1;

	return 0;
}

/******************************************************************************
 *
 *  ioctl_queryctrl
 *
 *  V4L2 query possible control variables
 *
 *****************************************************************************/

static int cpia2_queryctrl(struct file *file, void *fh, struct v4l2_queryctrl *c)
{
	struct camera_data *cam = video_drvdata(file);
	int i;

	for(i=0; i<NUM_CONTROLS; ++i) {
		if(c->id == controls[i].id) {
			memcpy(c, controls+i, sizeof(*c));
			break;
		}
	}

	if(i == NUM_CONTROLS)
		return -EINVAL;

	/* Some devices have additional limitations */
	switch(c->id) {
	case V4L2_CID_BRIGHTNESS:
		/***
		 * Don't let the register be set to zero - bug in VP4
		 * flash of full brightness
		 ***/
		if (cam->params.pnp_id.device_type == DEVICE_STV_672)
			c->minimum = 1;
		break;
	case V4L2_CID_VFLIP:
		// VP5 Only
		if(cam->params.pnp_id.device_type == DEVICE_STV_672)
			c->flags |= V4L2_CTRL_FLAG_DISABLED;
		break;
	case CPIA2_CID_FRAMERATE:
		if(cam->params.pnp_id.device_type == DEVICE_STV_672 &&
		   cam->params.version.sensor_flags==CPIA2_VP_SENSOR_FLAGS_500){
			// Maximum 15fps
			for(i=0; i<c->maximum; ++i) {
				if(framerate_controls[i].value ==
				   CPIA2_VP_FRAMERATE_15) {
					c->maximum = i;
					c->default_value = i;
				}
			}
		}
		break;
	case CPIA2_CID_FLICKER_MODE:
		// Flicker control only valid for 672.
		if(cam->params.pnp_id.device_type != DEVICE_STV_672)
			c->flags |= V4L2_CTRL_FLAG_DISABLED;
		break;
	case CPIA2_CID_LIGHTS:
		// Light control only valid for the QX5 Microscope.
		if(cam->params.pnp_id.product != 0x151)
			c->flags |= V4L2_CTRL_FLAG_DISABLED;
		break;
	default:
		break;
	}

	return 0;
}

/******************************************************************************
 *
 *  ioctl_querymenu
 *
 *  V4L2 query possible control variables
 *
 *****************************************************************************/

static int cpia2_querymenu(struct file *file, void *fh, struct v4l2_querymenu *m)
{
	struct camera_data *cam = video_drvdata(file);

	switch(m->id) {
	case CPIA2_CID_FLICKER_MODE:
		if (m->index >= NUM_FLICKER_CONTROLS)
			return -EINVAL;

		strcpy(m->name, flicker_controls[m->index].name);
		break;
	case CPIA2_CID_FRAMERATE:
	    {
		int maximum = NUM_FRAMERATE_CONTROLS - 1;
		if(cam->params.pnp_id.device_type == DEVICE_STV_672 &&
		   cam->params.version.sensor_flags==CPIA2_VP_SENSOR_FLAGS_500){
			// Maximum 15fps
			int i;
			for(i=0; i<maximum; ++i) {
				if(framerate_controls[i].value ==
				   CPIA2_VP_FRAMERATE_15)
					maximum = i;
			}
		}
		if (m->index > maximum)
			return -EINVAL;

		strcpy(m->name, framerate_controls[m->index].name);
		break;
	    }
	case CPIA2_CID_LIGHTS:
		if (m->index >= NUM_LIGHTS_CONTROLS)
			return -EINVAL;

		strcpy(m->name, lights_controls[m->index].name);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/******************************************************************************
 *
 *  ioctl_g_ctrl
 *
 *  V4L2 get the value of a control variable
 *
 *****************************************************************************/

static int cpia2_g_ctrl(struct file *file, void *fh, struct v4l2_control *c)
{
	struct camera_data *cam = video_drvdata(file);

	switch(c->id) {
	case V4L2_CID_BRIGHTNESS:
		cpia2_do_command(cam, CPIA2_CMD_GET_VP_BRIGHTNESS,
				 TRANSFER_READ, 0);
		c->value = cam->params.color_params.brightness;
		break;
	case V4L2_CID_CONTRAST:
		cpia2_do_command(cam, CPIA2_CMD_GET_CONTRAST,
				 TRANSFER_READ, 0);
		c->value = cam->params.color_params.contrast;
		break;
	case V4L2_CID_SATURATION:
		cpia2_do_command(cam, CPIA2_CMD_GET_VP_SATURATION,
				 TRANSFER_READ, 0);
		c->value = cam->params.color_params.saturation;
		break;
	case V4L2_CID_HFLIP:
		cpia2_do_command(cam, CPIA2_CMD_GET_USER_EFFECTS,
				 TRANSFER_READ, 0);
		c->value = (cam->params.vp_params.user_effects &
			    CPIA2_VP_USER_EFFECTS_MIRROR) != 0;
		break;
	case V4L2_CID_VFLIP:
		cpia2_do_command(cam, CPIA2_CMD_GET_USER_EFFECTS,
				 TRANSFER_READ, 0);
		c->value = (cam->params.vp_params.user_effects &
			    CPIA2_VP_USER_EFFECTS_FLIP) != 0;
		break;
	case CPIA2_CID_TARGET_KB:
		c->value = cam->params.vc_params.target_kb;
		break;
	case CPIA2_CID_GPIO:
		cpia2_do_command(cam, CPIA2_CMD_GET_VP_GPIO_DATA,
				 TRANSFER_READ, 0);
		c->value = cam->params.vp_params.gpio_data;
		break;
	case CPIA2_CID_FLICKER_MODE:
	{
		int i, mode;
		cpia2_do_command(cam, CPIA2_CMD_GET_FLICKER_MODES,
				 TRANSFER_READ, 0);
		if(cam->params.flicker_control.cam_register &
		   CPIA2_VP_FLICKER_MODES_NEVER_FLICKER) {
			mode = NEVER_FLICKER;
		} else {
		    if(cam->params.flicker_control.cam_register &
		       CPIA2_VP_FLICKER_MODES_50HZ) {
			mode = FLICKER_50;
		    } else {
			mode = FLICKER_60;
		    }
		}
		for(i=0; i<NUM_FLICKER_CONTROLS; i++) {
			if(flicker_controls[i].value == mode) {
				c->value = i;
				break;
			}
		}
		if(i == NUM_FLICKER_CONTROLS)
			return -EINVAL;
		break;
	}
	case CPIA2_CID_FRAMERATE:
	{
		int maximum = NUM_FRAMERATE_CONTROLS - 1;
		int i;
		for(i=0; i<= maximum; i++) {
			if(cam->params.vp_params.frame_rate ==
			   framerate_controls[i].value)
				break;
		}
		if(i > maximum)
			return -EINVAL;
		c->value = i;
		break;
	}
	case CPIA2_CID_USB_ALT:
		c->value = cam->params.camera_state.stream_mode;
		break;
	case CPIA2_CID_LIGHTS:
	{
		int i;
		cpia2_do_command(cam, CPIA2_CMD_GET_VP_GPIO_DATA,
				 TRANSFER_READ, 0);
		for(i=0; i<NUM_LIGHTS_CONTROLS; i++) {
			if((cam->params.vp_params.gpio_data&GPIO_LIGHTS_MASK) ==
			   lights_controls[i].value) {
				break;
			}
		}
		if(i == NUM_LIGHTS_CONTROLS)
			return -EINVAL;
		c->value = i;
		break;
	}
	case CPIA2_CID_RESET_CAMERA:
		return -EINVAL;
	default:
		return -EINVAL;
	}

	DBG("Get control id:%d, value:%d\n", c->id, c->value);

	return 0;
}

/******************************************************************************
 *
 *  ioctl_s_ctrl
 *
 *  V4L2 set the value of a control variable
 *
 *****************************************************************************/

static int cpia2_s_ctrl(struct file *file, void *fh, struct v4l2_control *c)
{
	struct camera_data *cam = video_drvdata(file);
	int i;
	int retval = 0;

	DBG("Set control id:%d, value:%d\n", c->id, c->value);

	/* Check that the value is in range */
	for(i=0; i<NUM_CONTROLS; i++) {
		if(c->id == controls[i].id) {
			if(c->value < controls[i].minimum ||
			   c->value > controls[i].maximum) {
				return -EINVAL;
			}
			break;
		}
	}
	if(i == NUM_CONTROLS)
		return -EINVAL;

	switch(c->id) {
	case V4L2_CID_BRIGHTNESS:
		cpia2_set_brightness(cam, c->value);
		break;
	case V4L2_CID_CONTRAST:
		cpia2_set_contrast(cam, c->value);
		break;
	case V4L2_CID_SATURATION:
		cpia2_set_saturation(cam, c->value);
		break;
	case V4L2_CID_HFLIP:
		cpia2_set_property_mirror(cam, c->value);
		break;
	case V4L2_CID_VFLIP:
		cpia2_set_property_flip(cam, c->value);
		break;
	case CPIA2_CID_TARGET_KB:
		retval = cpia2_set_target_kb(cam, c->value);
		break;
	case CPIA2_CID_GPIO:
		retval = cpia2_set_gpio(cam, c->value);
		break;
	case CPIA2_CID_FLICKER_MODE:
		retval = cpia2_set_flicker_mode(cam,
					      flicker_controls[c->value].value);
		break;
	case CPIA2_CID_FRAMERATE:
		retval = cpia2_set_fps(cam, framerate_controls[c->value].value);
		break;
	case CPIA2_CID_USB_ALT:
		retval = cpia2_usb_change_streaming_alternate(cam, c->value);
		break;
	case CPIA2_CID_LIGHTS:
		retval = cpia2_set_gpio(cam, lights_controls[c->value].value);
		break;
	case CPIA2_CID_RESET_CAMERA:
		cpia2_usb_stream_pause(cam);
		cpia2_reset_camera(cam);
		cpia2_usb_stream_resume(cam);
		break;
	default:
		retval = -EINVAL;
	}

	return retval;
}

/******************************************************************************
 *
 *  ioctl_g_jpegcomp
 *
 *  V4L2 get the JPEG compression parameters
 *
 *****************************************************************************/

static int cpia2_g_jpegcomp(struct file *file, void *fh, struct v4l2_jpegcompression *parms)
{
	struct camera_data *cam = video_drvdata(file);

	memset(parms, 0, sizeof(*parms));

	parms->quality = 80; // TODO: Can this be made meaningful?

	parms->jpeg_markers = V4L2_JPEG_MARKER_DQT | V4L2_JPEG_MARKER_DRI;
	if(!cam->params.compression.inhibit_htables) {
		parms->jpeg_markers |= V4L2_JPEG_MARKER_DHT;
	}

	parms->APPn = cam->APPn;
	parms->APP_len = cam->APP_len;
	if(cam->APP_len > 0) {
		memcpy(parms->APP_data, cam->APP_data, cam->APP_len);
		parms->jpeg_markers |= V4L2_JPEG_MARKER_APP;
	}

	parms->COM_len = cam->COM_len;
	if(cam->COM_len > 0) {
		memcpy(parms->COM_data, cam->COM_data, cam->COM_len);
		parms->jpeg_markers |= JPEG_MARKER_COM;
	}

	DBG("G_JPEGCOMP APP_len:%d COM_len:%d\n",
	    parms->APP_len, parms->COM_len);

	return 0;
}

/******************************************************************************
 *
 *  ioctl_s_jpegcomp
 *
 *  V4L2 set the JPEG compression parameters
 *  NOTE: quality and some jpeg_markers are ignored.
 *
 *****************************************************************************/

static int cpia2_s_jpegcomp(struct file *file, void *fh, struct v4l2_jpegcompression *parms)
{
	struct camera_data *cam = video_drvdata(file);

	DBG("S_JPEGCOMP APP_len:%d COM_len:%d\n",
	    parms->APP_len, parms->COM_len);

	cam->params.compression.inhibit_htables =
		!(parms->jpeg_markers & V4L2_JPEG_MARKER_DHT);

	if(parms->APP_len != 0) {
		if(parms->APP_len > 0 &&
		   parms->APP_len <= sizeof(cam->APP_data) &&
		   parms->APPn >= 0 && parms->APPn <= 15) {
			cam->APPn = parms->APPn;
			cam->APP_len = parms->APP_len;
			memcpy(cam->APP_data, parms->APP_data, parms->APP_len);
		} else {
			LOG("Bad APPn Params n=%d len=%d\n",
			    parms->APPn, parms->APP_len);
			return -EINVAL;
		}
	} else {
		cam->APP_len = 0;
	}

	if(parms->COM_len != 0) {
		if(parms->COM_len > 0 &&
		   parms->COM_len <= sizeof(cam->COM_data)) {
			cam->COM_len = parms->COM_len;
			memcpy(cam->COM_data, parms->COM_data, parms->COM_len);
		} else {
			LOG("Bad COM_len=%d\n", parms->COM_len);
			return -EINVAL;
		}
	}

	return 0;
}

/******************************************************************************
 *
 *  ioctl_reqbufs
 *
 *  V4L2 Initiate memory mapping.
 *  NOTE: The user's request is ignored. For now the buffers are fixed.
 *
 *****************************************************************************/

static int cpia2_reqbufs(struct file *file, void *fh, struct v4l2_requestbuffers *req)
{
	struct camera_data *cam = video_drvdata(file);

	if(req->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	   req->memory != V4L2_MEMORY_MMAP)
		return -EINVAL;

	DBG("REQBUFS requested:%d returning:%d\n", req->count, cam->num_frames);
	req->count = cam->num_frames;
	memset(&req->reserved, 0, sizeof(req->reserved));

	return 0;
}

/******************************************************************************
 *
 *  ioctl_querybuf
 *
 *  V4L2 Query memory buffer status.
 *
 *****************************************************************************/

static int cpia2_querybuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	struct camera_data *cam = video_drvdata(file);

	if(buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	   buf->index > cam->num_frames)
		return -EINVAL;

	buf->m.offset = cam->buffers[buf->index].data - cam->frame_buffer;
	buf->length = cam->frame_size;

	buf->memory = V4L2_MEMORY_MMAP;

	if(cam->mmapped)
		buf->flags = V4L2_BUF_FLAG_MAPPED;
	else
		buf->flags = 0;

	switch (cam->buffers[buf->index].status) {
	case FRAME_EMPTY:
	case FRAME_ERROR:
	case FRAME_READING:
		buf->bytesused = 0;
		buf->flags = V4L2_BUF_FLAG_QUEUED;
		break;
	case FRAME_READY:
		buf->bytesused = cam->buffers[buf->index].length;
		buf->timestamp = cam->buffers[buf->index].timestamp;
		buf->sequence = cam->buffers[buf->index].seq;
		buf->flags = V4L2_BUF_FLAG_DONE;
		break;
	}

	DBG("QUERYBUF index:%d offset:%d flags:%d seq:%d bytesused:%d\n",
	     buf->index, buf->m.offset, buf->flags, buf->sequence,
	     buf->bytesused);

	return 0;
}

/******************************************************************************
 *
 *  ioctl_qbuf
 *
 *  V4L2 User is freeing buffer
 *
 *****************************************************************************/

static int cpia2_qbuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	struct camera_data *cam = video_drvdata(file);

	if(buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	   buf->memory != V4L2_MEMORY_MMAP ||
	   buf->index > cam->num_frames)
		return -EINVAL;

	DBG("QBUF #%d\n", buf->index);

	if(cam->buffers[buf->index].status == FRAME_READY)
		cam->buffers[buf->index].status = FRAME_EMPTY;

	return 0;
}

/******************************************************************************
 *
 *  find_earliest_filled_buffer
 *
 *  Helper for ioctl_dqbuf. Find the next ready buffer.
 *
 *****************************************************************************/

static int find_earliest_filled_buffer(struct camera_data *cam)
{
	int i;
	int found = -1;
	for (i=0; i<cam->num_frames; i++) {
		if(cam->buffers[i].status == FRAME_READY) {
			if(found < 0) {
				found = i;
			} else {
				/* find which buffer is earlier */
				struct timeval *tv1, *tv2;
				tv1 = &cam->buffers[i].timestamp;
				tv2 = &cam->buffers[found].timestamp;
				if(tv1->tv_sec < tv2->tv_sec ||
				   (tv1->tv_sec == tv2->tv_sec &&
				    tv1->tv_usec < tv2->tv_usec))
					found = i;
			}
		}
	}
	return found;
}

/******************************************************************************
 *
 *  ioctl_dqbuf
 *
 *  V4L2 User is asking for a filled buffer.
 *
 *****************************************************************************/

static int cpia2_dqbuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	struct camera_data *cam = video_drvdata(file);
	int frame;

	if(buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	   buf->memory != V4L2_MEMORY_MMAP)
		return -EINVAL;

	frame = find_earliest_filled_buffer(cam);

	if(frame < 0 && file->f_flags&O_NONBLOCK)
		return -EAGAIN;

	if(frame < 0) {
		/* Wait for a frame to become available */
		struct framebuf *cb=cam->curbuff;
		mutex_unlock(&cam->v4l2_lock);
		wait_event_interruptible(cam->wq_stream,
					 !cam->present ||
					 (cb=cam->curbuff)->status == FRAME_READY);
		mutex_lock(&cam->v4l2_lock);
		if (signal_pending(current))
			return -ERESTARTSYS;
		if(!cam->present)
			return -ENOTTY;
		frame = cb->num;
	}


	buf->index = frame;
	buf->bytesused = cam->buffers[buf->index].length;
	buf->flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_DONE;
	buf->field = V4L2_FIELD_NONE;
	buf->timestamp = cam->buffers[buf->index].timestamp;
	buf->sequence = cam->buffers[buf->index].seq;
	buf->m.offset = cam->buffers[buf->index].data - cam->frame_buffer;
	buf->length = cam->frame_size;
	buf->input = 0;
	buf->reserved = 0;
	memset(&buf->timecode, 0, sizeof(buf->timecode));

	DBG("DQBUF #%d status:%d seq:%d length:%d\n", buf->index,
	    cam->buffers[buf->index].status, buf->sequence, buf->bytesused);

	return 0;
}

static int cpia2_g_priority(struct file *file, void *_fh, enum v4l2_priority *p)
{
	struct cpia2_fh *fh = _fh;

	*p = fh->prio;
	return 0;
}

static int cpia2_s_priority(struct file *file, void *_fh, enum v4l2_priority prio)
{
	struct camera_data *cam = video_drvdata(file);
	struct cpia2_fh *fh = _fh;

	if (cam->streaming && prio != fh->prio &&
			fh->prio == V4L2_PRIORITY_RECORD)
		/* Can't drop record priority while streaming */
		return -EBUSY;

	if (prio == V4L2_PRIORITY_RECORD && prio != fh->prio &&
			v4l2_prio_max(&cam->prio) == V4L2_PRIORITY_RECORD)
		/* Only one program can record at a time */
		return -EBUSY;
	return v4l2_prio_change(&cam->prio, &fh->prio, prio);
}

static int cpia2_streamon(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct camera_data *cam = video_drvdata(file);

	DBG("VIDIOC_STREAMON, streaming=%d\n", cam->streaming);
	if (!cam->mmapped || type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (!cam->streaming)
		return cpia2_usb_stream_start(cam,
				cam->params.camera_state.stream_mode);
	return -EINVAL;
}

static int cpia2_streamoff(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct camera_data *cam = video_drvdata(file);

	DBG("VIDIOC_STREAMOFF, streaming=%d\n", cam->streaming);
	if (!cam->mmapped || type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (cam->streaming)
		return cpia2_usb_stream_stop(cam);
	return -EINVAL;
}

/******************************************************************************
 *
 *  cpia2_mmap
 *
 *****************************************************************************/
static int cpia2_mmap(struct file *file, struct vm_area_struct *area)
{
	struct camera_data *cam = video_drvdata(file);
	int retval;

	/* Priority check */
	struct cpia2_fh *fh = file->private_data;
	if(fh->prio != V4L2_PRIORITY_RECORD) {
		return -EBUSY;
	}

	retval = cpia2_remap_buffer(cam, area);

	if(!retval)
		fh->mmapped = 1;
	return retval;
}

/******************************************************************************
 *
 *  reset_camera_struct_v4l
 *
 *  Sets all values to the defaults
 *****************************************************************************/
static void reset_camera_struct_v4l(struct camera_data *cam)
{
	cam->width = cam->params.roi.width;
	cam->height = cam->params.roi.height;

	cam->frame_size = buffer_size;
	cam->num_frames = num_buffers;

	/* FlickerModes */
	cam->params.flicker_control.flicker_mode_req = flicker_mode;
	cam->params.flicker_control.mains_frequency = flicker_freq;

	/* streamMode */
	cam->params.camera_state.stream_mode = alternate;

	cam->pixelformat = V4L2_PIX_FMT_JPEG;
	v4l2_prio_init(&cam->prio);
}

static const struct v4l2_ioctl_ops cpia2_ioctl_ops = {
	.vidioc_querycap		    = cpia2_querycap,
	.vidioc_enum_input		    = cpia2_enum_input,
	.vidioc_g_input			    = cpia2_g_input,
	.vidioc_s_input			    = cpia2_s_input,
	.vidioc_enum_fmt_vid_cap	    = cpia2_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		    = cpia2_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		    = cpia2_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		    = cpia2_try_fmt_vid_cap,
	.vidioc_queryctrl		    = cpia2_queryctrl,
	.vidioc_querymenu		    = cpia2_querymenu,
	.vidioc_g_ctrl			    = cpia2_g_ctrl,
	.vidioc_s_ctrl			    = cpia2_s_ctrl,
	.vidioc_g_jpegcomp		    = cpia2_g_jpegcomp,
	.vidioc_s_jpegcomp		    = cpia2_s_jpegcomp,
	.vidioc_cropcap			    = cpia2_cropcap,
	.vidioc_reqbufs			    = cpia2_reqbufs,
	.vidioc_querybuf		    = cpia2_querybuf,
	.vidioc_qbuf			    = cpia2_qbuf,
	.vidioc_dqbuf			    = cpia2_dqbuf,
	.vidioc_streamon		    = cpia2_streamon,
	.vidioc_streamoff		    = cpia2_streamoff,
	.vidioc_g_priority		    = cpia2_g_priority,
	.vidioc_s_priority		    = cpia2_s_priority,
	.vidioc_default			    = cpia2_default,
};

/***
 * The v4l video device structure initialized for this device
 ***/
static const struct v4l2_file_operations cpia2_fops = {
	.owner		= THIS_MODULE,
	.open		= cpia2_open,
	.release	= cpia2_close,
	.read		= cpia2_v4l_read,
	.poll		= cpia2_v4l_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= cpia2_mmap,
};

static struct video_device cpia2_template = {
	/* I could not find any place for the old .initialize initializer?? */
	.name =		"CPiA2 Camera",
	.fops =		&cpia2_fops,
	.ioctl_ops =	&cpia2_ioctl_ops,
	.release =	video_device_release,
};

/******************************************************************************
 *
 *  cpia2_register_camera
 *
 *****************************************************************************/
int cpia2_register_camera(struct camera_data *cam)
{
	cam->vdev = video_device_alloc();
	if(!cam->vdev)
		return -ENOMEM;

	memcpy(cam->vdev, &cpia2_template, sizeof(cpia2_template));
	video_set_drvdata(cam->vdev, cam);
	cam->vdev->lock = &cam->v4l2_lock;

	reset_camera_struct_v4l(cam);

	/* register v4l device */
	if (video_register_device(cam->vdev, VFL_TYPE_GRABBER, video_nr) < 0) {
		ERR("video_register_device failed\n");
		video_device_release(cam->vdev);
		return -ENODEV;
	}

	return 0;
}

/******************************************************************************
 *
 *  cpia2_unregister_camera
 *
 *****************************************************************************/
void cpia2_unregister_camera(struct camera_data *cam)
{
	if (!cam->open_count) {
		video_unregister_device(cam->vdev);
	} else {
		LOG("%s removed while open, deferring "
		    "video_unregister_device\n",
		    video_device_node_name(cam->vdev));
	}
}

/******************************************************************************
 *
 *  check_parameters
 *
 *  Make sure that all user-supplied parameters are sensible
 *****************************************************************************/
static void __init check_parameters(void)
{
	if(buffer_size < PAGE_SIZE) {
		buffer_size = PAGE_SIZE;
		LOG("buffer_size too small, setting to %d\n", buffer_size);
	} else if(buffer_size > 1024*1024) {
		/* arbitrary upper limiit */
		buffer_size = 1024*1024;
		LOG("buffer_size ridiculously large, setting to %d\n",
		    buffer_size);
	} else {
		buffer_size += PAGE_SIZE-1;
		buffer_size &= ~(PAGE_SIZE-1);
	}

	if(num_buffers < 1) {
		num_buffers = 1;
		LOG("num_buffers too small, setting to %d\n", num_buffers);
	} else if(num_buffers > VIDEO_MAX_FRAME) {
		num_buffers = VIDEO_MAX_FRAME;
		LOG("num_buffers too large, setting to %d\n", num_buffers);
	}

	if(alternate < USBIF_ISO_1 || alternate > USBIF_ISO_6) {
		alternate = DEFAULT_ALT;
		LOG("alternate specified is invalid, using %d\n", alternate);
	}

	if (flicker_mode != NEVER_FLICKER && flicker_mode != ANTI_FLICKER_ON) {
		flicker_mode = NEVER_FLICKER;
		LOG("Flicker mode specified is invalid, using %d\n",
		    flicker_mode);
	}

	if (flicker_freq != FLICKER_50 && flicker_freq != FLICKER_60) {
		flicker_freq = FLICKER_60;
		LOG("Flicker mode specified is invalid, using %d\n",
		    flicker_freq);
	}

	if(video_nr < -1 || video_nr > 64) {
		video_nr = -1;
		LOG("invalid video_nr specified, must be -1 to 64\n");
	}

	DBG("Using %d buffers, each %d bytes, alternate=%d\n",
	    num_buffers, buffer_size, alternate);
}

/************   Module Stuff ***************/


/******************************************************************************
 *
 * cpia2_init/module_init
 *
 *****************************************************************************/
static int __init cpia2_init(void)
{
	LOG("%s v%s\n",
	    ABOUT, CPIA_VERSION);
	check_parameters();
	cpia2_usb_init();
	return 0;
}


/******************************************************************************
 *
 * cpia2_exit/module_exit
 *
 *****************************************************************************/
static void __exit cpia2_exit(void)
{
	cpia2_usb_cleanup();
	schedule_timeout(2 * HZ);
}

module_init(cpia2_init);
module_exit(cpia2_exit);

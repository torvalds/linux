/*
 * Zoran 364xx based USB webcam module version 0.72
 *
 * Allows you to use your USB webcam with V4L2 applications
 * This is still in heavy developpement !
 *
 * Copyright (C) 2004  Antoine Jacquet <royale@zerezo.com>
 * http://royale.zerezo.com/zr364xx/
 *
 * Heavily inspired by usb-skeleton.c, vicam.c, cpia.c and spca50x.c drivers
 * V4L2 version inspired by meye.c driver
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/highmem.h>
#include <media/v4l2-common.h>


/* Version Information */
#define DRIVER_VERSION "v0.72"
#define DRIVER_AUTHOR "Antoine Jacquet, http://royale.zerezo.com/"
#define DRIVER_DESC "Zoran 364xx"


/* Camera */
#define FRAMES 2
#define MAX_FRAME_SIZE 100000
#define BUFFER_SIZE 0x1000
#define CTRL_TIMEOUT 500


/* Debug macro */
#define DBG(x...) if (debug) info(x)


/* Init methods, need to find nicer names for these
 * the exact names of the chipsets would be the best if someone finds it */
#define METHOD0 0
#define METHOD1 1
#define METHOD2 2


/* Module parameters */
static int debug = 0;
static int mode = 0;


/* Module parameters interface */
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level");
module_param(mode, int, 0644);
MODULE_PARM_DESC(mode, "0 = 320x240, 1 = 160x120, 2 = 640x480");


/* Devices supported by this driver
 * .driver_info contains the init method used by the camera */
static struct usb_device_id device_table[] = {
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
	{}			/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, device_table);


/* Camera stuff */
struct zr364xx_camera {
	struct usb_device *udev;	/* save off the usb device pointer */
	struct usb_interface *interface;/* the interface for this device */
	struct video_device *vdev;	/* v4l video device */
	u8 *framebuf;
	int nb;
	unsigned char *buffer;
	int skip;
	int brightness;
	int width;
	int height;
	int method;
	struct mutex lock;
};


/* function used to send initialisation commands to the camera */
static int send_control_msg(struct usb_device *udev, u8 request, u16 value,
			    u16 index, unsigned char *cp, u16 size)
{
	int status;

	unsigned char *transfer_buffer = kmalloc(size, GFP_KERNEL);
	if (!transfer_buffer) {
		info("kmalloc(%d) failed", size);
		return -ENOMEM;
	}

	memcpy(transfer_buffer, cp, size);

	status = usb_control_msg(udev,
				 usb_sndctrlpipe(udev, 0),
				 request,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_DEVICE, value, index,
				 transfer_buffer, size, CTRL_TIMEOUT);

	kfree(transfer_buffer);

	if (status < 0)
		info("Failed sending control message, error %d.", status);

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
static message *init[3] = { m0, m1, m2 };


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



/********************/
/* V4L2 integration */
/********************/

/* this function reads a full JPEG picture synchronously
 * TODO: do it asynchronously... */
static int read_frame(struct zr364xx_camera *cam, int framenum)
{
	int i, n, temp, head, size, actual_length;
	unsigned char *ptr = NULL, *jpeg;

      redo:
	/* hardware brightness */
	n = send_control_msg(cam->udev, 1, 0x2001, 0, NULL, 0);
	temp = (0x60 << 8) + 127 - cam->brightness;
	n = send_control_msg(cam->udev, 1, temp, 0, NULL, 0);

	/* during the first loop we are going to insert JPEG header */
	head = 0;
	/* this is the place in memory where we are going to build
	 * the JPEG image */
	jpeg = cam->framebuf + framenum * MAX_FRAME_SIZE;
	/* read data... */
	do {
		n = usb_bulk_msg(cam->udev,
				 usb_rcvbulkpipe(cam->udev, 0x81),
				 cam->buffer, BUFFER_SIZE, &actual_length,
				 CTRL_TIMEOUT);
		DBG("buffer : %d %d", cam->buffer[0], cam->buffer[1]);
		DBG("bulk : n=%d size=%d", n, actual_length);
		if (n < 0) {
			info("error reading bulk msg");
			return 0;
		}
		if (actual_length < 0 || actual_length > BUFFER_SIZE) {
			info("wrong number of bytes");
			return 0;
		}

		/* swap bytes if camera needs it */
		if (cam->method == METHOD0) {
			u16 *buf = (u16*)cam->buffer;
			for (i = 0; i < BUFFER_SIZE/2; i++)
				swab16s(buf + i);
		}

		/* write the JPEG header */
		if (!head) {
			DBG("jpeg header");
			ptr = jpeg;
			memcpy(ptr, header1, sizeof(header1));
			ptr += sizeof(header1);
			header3 = 0;
			memcpy(ptr, &header3, 1);
			ptr++;
			memcpy(ptr, cam->buffer, 64);
			ptr += 64;
			header3 = 1;
			memcpy(ptr, &header3, 1);
			ptr++;
			memcpy(ptr, cam->buffer + 64, 64);
			ptr += 64;
			memcpy(ptr, header2, sizeof(header2));
			ptr += sizeof(header2);
			memcpy(ptr, cam->buffer + 128,
			       actual_length - 128);
			ptr += actual_length - 128;
			head = 1;
			DBG("header : %d %d %d %d %d %d %d %d %d",
			    cam->buffer[0], cam->buffer[1], cam->buffer[2],
			    cam->buffer[3], cam->buffer[4], cam->buffer[5],
			    cam->buffer[6], cam->buffer[7], cam->buffer[8]);
		} else {
			memcpy(ptr, cam->buffer, actual_length);
			ptr += actual_length;
		}
	}
	/* ... until there is no more */
	while (actual_length == BUFFER_SIZE);

	/* we skip the 2 first frames which are usually buggy */
	if (cam->skip) {
		cam->skip--;
		goto redo;
	}

	/* go back to find the JPEG EOI marker */
	size = ptr - jpeg;
	ptr -= 2;
	while (ptr > jpeg) {
		if (*ptr == 0xFF && *(ptr + 1) == 0xD9
		    && *(ptr + 2) == 0xFF)
			break;
		ptr--;
	}
	if (ptr == jpeg)
		DBG("No EOI marker");

	/* Sometimes there is junk data in the middle of the picture,
	 * we want to skip this bogus frames */
	while (ptr > jpeg) {
		if (*ptr == 0xFF && *(ptr + 1) == 0xFF
		    && *(ptr + 2) == 0xFF)
			break;
		ptr--;
	}
	if (ptr != jpeg) {
		DBG("Bogus frame ? %d", cam->nb);
		goto redo;
	}

	DBG("jpeg : %d %d %d %d %d %d %d %d",
	    jpeg[0], jpeg[1], jpeg[2], jpeg[3],
	    jpeg[4], jpeg[5], jpeg[6], jpeg[7]);

	return size;
}


static ssize_t zr364xx_read(struct file *file, char *buf, size_t cnt,
			    loff_t * ppos)
{
	unsigned long count = cnt;
	struct video_device *vdev = video_devdata(file);
	struct zr364xx_camera *cam;

	DBG("zr364xx_read: read %d bytes.", (int) count);

	if (vdev == NULL)
		return -ENODEV;
	cam = video_get_drvdata(vdev);

	if (!buf)
		return -EINVAL;

	if (!count)
		return -EINVAL;

	/* NoMan Sux ! */
	count = read_frame(cam, 0);

	if (copy_to_user(buf, cam->framebuf, count))
		return -EFAULT;

	return count;
}


static int zr364xx_vidioc_querycap(struct file *file, void *priv,
				   struct v4l2_capability *cap)
{
	memset(cap, 0, sizeof(*cap));
	strcpy(cap->driver, DRIVER_DESC);
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE;
	return 0;
}

static int zr364xx_vidioc_enum_input(struct file *file, void *priv,
				     struct v4l2_input *i)
{
	if (i->index != 0)
		return -EINVAL;
	memset(i, 0, sizeof(*i));
	i->index = 0;
	strcpy(i->name, DRIVER_DESC " Camera");
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

static int zr364xx_vidioc_queryctrl(struct file *file, void *priv,
				    struct v4l2_queryctrl *c)
{
	struct video_device *vdev = video_devdata(file);
	struct zr364xx_camera *cam;

	if (vdev == NULL)
		return -ENODEV;
	cam = video_get_drvdata(vdev);

	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		c->type = V4L2_CTRL_TYPE_INTEGER;
		strcpy(c->name, "Brightness");
		c->minimum = 0;
		c->maximum = 127;
		c->step = 1;
		c->default_value = cam->brightness;
		c->flags = 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int zr364xx_vidioc_s_ctrl(struct file *file, void *priv,
				 struct v4l2_control *c)
{
	struct video_device *vdev = video_devdata(file);
	struct zr364xx_camera *cam;

	if (vdev == NULL)
		return -ENODEV;
	cam = video_get_drvdata(vdev);

	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		cam->brightness = c->value;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int zr364xx_vidioc_g_ctrl(struct file *file, void *priv,
				 struct v4l2_control *c)
{
	struct video_device *vdev = video_devdata(file);
	struct zr364xx_camera *cam;

	if (vdev == NULL)
		return -ENODEV;
	cam = video_get_drvdata(vdev);

	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		c->value = cam->brightness;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int zr364xx_vidioc_enum_fmt_cap(struct file *file,
				       void *priv, struct v4l2_fmtdesc *f)
{
	if (f->index > 0)
		return -EINVAL;
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	memset(f, 0, sizeof(*f));
	f->index = 0;
	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f->flags = V4L2_FMT_FLAG_COMPRESSED;
	strcpy(f->description, "JPEG");
	f->pixelformat = V4L2_PIX_FMT_JPEG;
	return 0;
}

static int zr364xx_vidioc_try_fmt_cap(struct file *file, void *priv,
				      struct v4l2_format *f)
{
	struct video_device *vdev = video_devdata(file);
	struct zr364xx_camera *cam;

	if (vdev == NULL)
		return -ENODEV;
	cam = video_get_drvdata(vdev);

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (f->fmt.pix.pixelformat != V4L2_PIX_FMT_JPEG)
		return -EINVAL;
	if (f->fmt.pix.field != V4L2_FIELD_ANY &&
	    f->fmt.pix.field != V4L2_FIELD_NONE)
		return -EINVAL;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.width = cam->width;
	f->fmt.pix.height = cam->height;
	f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = 0;
	f->fmt.pix.priv = 0;
	return 0;
}

static int zr364xx_vidioc_g_fmt_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct video_device *vdev = video_devdata(file);
	struct zr364xx_camera *cam;

	if (vdev == NULL)
		return -ENODEV;
	cam = video_get_drvdata(vdev);

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	memset(&f->fmt.pix, 0, sizeof(struct v4l2_pix_format));
	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.width = cam->width;
	f->fmt.pix.height = cam->height;
	f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = 0;
	f->fmt.pix.priv = 0;
	return 0;
}

static int zr364xx_vidioc_s_fmt_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct video_device *vdev = video_devdata(file);
	struct zr364xx_camera *cam;

	if (vdev == NULL)
		return -ENODEV;
	cam = video_get_drvdata(vdev);

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (f->fmt.pix.pixelformat != V4L2_PIX_FMT_JPEG)
		return -EINVAL;
	if (f->fmt.pix.field != V4L2_FIELD_ANY &&
	    f->fmt.pix.field != V4L2_FIELD_NONE)
		return -EINVAL;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.width = cam->width;
	f->fmt.pix.height = cam->height;
	f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = 0;
	f->fmt.pix.priv = 0;
	DBG("ok!");
	return 0;
}

static int zr364xx_vidioc_streamon(struct file *file, void *priv,
				   enum v4l2_buf_type type)
{
	return 0;
}

static int zr364xx_vidioc_streamoff(struct file *file, void *priv,
				    enum v4l2_buf_type type)
{
	return 0;
}


/* open the camera */
static int zr364xx_open(struct inode *inode, struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct zr364xx_camera *cam = video_get_drvdata(vdev);
	struct usb_device *udev = cam->udev;
	int i, err;

	DBG("zr364xx_open");

	cam->skip = 2;

	err = video_exclusive_open(inode, file);
	if (err < 0)
		return err;

	if (!cam->framebuf) {
		cam->framebuf = vmalloc_32(MAX_FRAME_SIZE * FRAMES);
		if (!cam->framebuf) {
			info("vmalloc_32 failed!");
			return -ENOMEM;
		}
	}

	mutex_lock(&cam->lock);
	for (i = 0; init[cam->method][i].size != -1; i++) {
		err =
		    send_control_msg(udev, 1, init[cam->method][i].value,
				     0, init[cam->method][i].bytes,
				     init[cam->method][i].size);
		if (err < 0) {
			info("error during open sequence: %d", i);
			mutex_unlock(&cam->lock);
			return err;
		}
	}

	file->private_data = vdev;

	/* Added some delay here, since opening/closing the camera quickly,
	 * like Ekiga does during its startup, can crash the webcam
	 */
	mdelay(100);

	mutex_unlock(&cam->lock);
	return 0;
}


/* release the camera */
static int zr364xx_release(struct inode *inode, struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct zr364xx_camera *cam;
	struct usb_device *udev;
	int i, err;

	DBG("zr364xx_release");

	if (vdev == NULL)
		return -ENODEV;
	cam = video_get_drvdata(vdev);

	udev = cam->udev;

	mutex_lock(&cam->lock);
	for (i = 0; i < 2; i++) {
		err =
		    send_control_msg(udev, 1, init[cam->method][i].value,
				     0, init[i][cam->method].bytes,
				     init[cam->method][i].size);
		if (err < 0) {
			info("error during release sequence");
			mutex_unlock(&cam->lock);
			return err;
		}
	}

	file->private_data = NULL;
	video_exclusive_release(inode, file);

	/* Added some delay here, since opening/closing the camera quickly,
	 * like Ekiga does during its startup, can crash the webcam
	 */
	mdelay(100);

	mutex_unlock(&cam->lock);
	return 0;
}


static int zr364xx_mmap(struct file *file, struct vm_area_struct *vma)
{
	void *pos;
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	struct video_device *vdev = video_devdata(file);
	struct zr364xx_camera *cam;

	DBG("zr364xx_mmap: %ld\n", size);

	if (vdev == NULL)
		return -ENODEV;
	cam = video_get_drvdata(vdev);

	pos = cam->framebuf;
	while (size > 0) {
		if (vm_insert_page(vma, start, vmalloc_to_page(pos)))
			return -EAGAIN;
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	return 0;
}


static struct file_operations zr364xx_fops = {
	.owner = THIS_MODULE,
	.open = zr364xx_open,
	.release = zr364xx_release,
	.read = zr364xx_read,
	.mmap = zr364xx_mmap,
	.ioctl = video_ioctl2,
	.llseek = no_llseek,
};

static struct video_device zr364xx_template = {
	.owner = THIS_MODULE,
	.name = DRIVER_DESC,
	.type = VID_TYPE_CAPTURE,
	.fops = &zr364xx_fops,
	.release = video_device_release,
	.minor = -1,

	.vidioc_querycap	= zr364xx_vidioc_querycap,
	.vidioc_enum_fmt_cap	= zr364xx_vidioc_enum_fmt_cap,
	.vidioc_try_fmt_cap	= zr364xx_vidioc_try_fmt_cap,
	.vidioc_s_fmt_cap	= zr364xx_vidioc_s_fmt_cap,
	.vidioc_g_fmt_cap	= zr364xx_vidioc_g_fmt_cap,
	.vidioc_enum_input	= zr364xx_vidioc_enum_input,
	.vidioc_g_input		= zr364xx_vidioc_g_input,
	.vidioc_s_input		= zr364xx_vidioc_s_input,
	.vidioc_streamon	= zr364xx_vidioc_streamon,
	.vidioc_streamoff	= zr364xx_vidioc_streamoff,
	.vidioc_queryctrl	= zr364xx_vidioc_queryctrl,
	.vidioc_g_ctrl		= zr364xx_vidioc_g_ctrl,
	.vidioc_s_ctrl		= zr364xx_vidioc_s_ctrl,
};



/*******************/
/* USB integration */
/*******************/

static int zr364xx_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct zr364xx_camera *cam = NULL;

	DBG("probing...");

	info(DRIVER_DESC " compatible webcam plugged");
	info("model %04x:%04x detected", udev->descriptor.idVendor,
	     udev->descriptor.idProduct);

	if ((cam =
	     kmalloc(sizeof(struct zr364xx_camera), GFP_KERNEL)) == NULL) {
		info("cam: out of memory !");
		return -ENODEV;
	}
	memset(cam, 0x00, sizeof(struct zr364xx_camera));
	/* save the init method used by this camera */
	cam->method = id->driver_info;

	cam->vdev = video_device_alloc();
	if (cam->vdev == NULL) {
		info("cam->vdev: out of memory !");
		kfree(cam);
		return -ENODEV;
	}
	memcpy(cam->vdev, &zr364xx_template, sizeof(zr364xx_template));
	video_set_drvdata(cam->vdev, cam);
	if (debug)
		cam->vdev->debug = V4L2_DEBUG_IOCTL | V4L2_DEBUG_IOCTL_ARG;

	cam->udev = udev;

	if ((cam->buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL)) == NULL) {
		info("cam->buffer: out of memory !");
		video_device_release(cam->vdev);
		kfree(cam);
		return -ENODEV;
	}

	switch (mode) {
	case 1:
		info("160x120 mode selected");
		cam->width = 160;
		cam->height = 120;
		break;
	case 2:
		info("640x480 mode selected");
		cam->width = 640;
		cam->height = 480;
		break;
	default:
		info("320x240 mode selected");
		cam->width = 320;
		cam->height = 240;
		break;
	}

	m0d1[0] = mode;
	m1[2].value = 0xf000 + mode;
	m2[1].value = 0xf000 + mode;
	header2[437] = cam->height / 256;
	header2[438] = cam->height % 256;
	header2[439] = cam->width / 256;
	header2[440] = cam->width % 256;

	cam->nb = 0;
	cam->brightness = 64;
	mutex_init(&cam->lock);

	if (video_register_device(cam->vdev, VFL_TYPE_GRABBER, -1) == -1) {
		info("video_register_device failed");
		video_device_release(cam->vdev);
		kfree(cam->buffer);
		kfree(cam);
		return -ENODEV;
	}

	usb_set_intfdata(intf, cam);

	info(DRIVER_DESC " controlling video device %d", cam->vdev->minor);
	return 0;
}


static void zr364xx_disconnect(struct usb_interface *intf)
{
	struct zr364xx_camera *cam = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);
	dev_set_drvdata(&intf->dev, NULL);
	info(DRIVER_DESC " webcam unplugged");
	if (cam->vdev)
		video_unregister_device(cam->vdev);
	cam->vdev = NULL;
	kfree(cam->buffer);
	if (cam->framebuf)
		vfree(cam->framebuf);
	kfree(cam);
}



/**********************/
/* Module integration */
/**********************/

static struct usb_driver zr364xx_driver = {
	.name = "zr364xx",
	.probe = zr364xx_probe,
	.disconnect = zr364xx_disconnect,
	.id_table = device_table
};


static int __init zr364xx_init(void)
{
	int retval;
	retval = usb_register(&zr364xx_driver) < 0;
	if (retval)
		info("usb_register failed!");
	else
		info(DRIVER_DESC " module loaded");
	return retval;
}


static void __exit zr364xx_exit(void)
{
	info(DRIVER_DESC " module unloaded");
	usb_deregister(&zr364xx_driver);
}


module_init(zr364xx_init);
module_exit(zr364xx_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

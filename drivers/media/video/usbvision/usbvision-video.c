/*
 * USB USBVISION Video device driver 0.9.9
 *
 *
 *
 * Copyright (c) 1999-2005 Joerg Heckenbach <joerg@heckenbach-aw.de>
 *
 * This module is part of usbvision driver project.
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
 *
 * Let's call the version 0.... until compression decoding is completely
 * implemented.
 *
 * This driver is written by Jose Ignacio Gijon and Joerg Heckenbach.
 * It was based on USB CPiA driver written by Peter Pregler,
 * Scott J. Bertin and Johannes Erdfelt
 * Ideas are taken from bttv driver by Ralph Metzler, Marcus Metzler &
 * Gerd Knorr and zoran 36120/36125 driver by Pauline Middelink
 * Updates to driver completed by Dwaine P. Garden
 *
 *
 * TODO:
 *     - use submit_urb for all setup packets
 *     - Fix memory settings for nt1004. It is 4 times as big as the
 *       nt1003 memory.
 *     - Add audio on endpoint 3 for nt1004 chip.  Seems impossible, needs a codec interface.  Which one?
 *     - Clean up the driver.
 *     - optimization for performance.
 *     - Add Videotext capability (VBI).  Working on it.....
 *     - Check audio for other devices
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/highmem.h>
#include <linux/smp_lock.h>
#include <linux/videodev.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <linux/videodev2.h>
#include <linux/video_decoder.h>
#include <linux/i2c.h>

#include <media/saa7115.h>
#include <media/v4l2-common.h>
#include <media/tuner.h>
#include <media/audiochip.h>

#include <linux/moduleparam.h>
#include <linux/workqueue.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#include "usbvision.h"

#define DRIVER_AUTHOR "Joerg Heckenbach <joerg@heckenbach-aw.de>, Dwaine Garden <DwaineGarden@rogers.com>"
#define DRIVER_NAME "usbvision"
#define DRIVER_ALIAS "USBVision"
#define DRIVER_DESC "USBVision USB Video Device Driver for Linux"
#define DRIVER_LICENSE "GPL"
#define USBVISION_DRIVER_VERSION_MAJOR 0
#define USBVISION_DRIVER_VERSION_MINOR 9
#define USBVISION_DRIVER_VERSION_PATCHLEVEL 9
#define USBVISION_DRIVER_VERSION KERNEL_VERSION(USBVISION_DRIVER_VERSION_MAJOR,USBVISION_DRIVER_VERSION_MINOR,USBVISION_DRIVER_VERSION_PATCHLEVEL)
#define USBVISION_VERSION_STRING __stringify(USBVISION_DRIVER_VERSION_MAJOR) "." __stringify(USBVISION_DRIVER_VERSION_MINOR) "." __stringify(USBVISION_DRIVER_VERSION_PATCHLEVEL)

#define	ENABLE_HEXDUMP	0	/* Enable if you need it */


#define USBVISION_DEBUG		/* Turn on debug messages */

#ifdef USBVISION_DEBUG
	#define PDEBUG(level, fmt, args...) \
		if (video_debug & (level)) info("[%s:%d] " fmt, __PRETTY_FUNCTION__, __LINE__ , ## args)
#else
	#define PDEBUG(level, fmt, args...) do {} while(0)
#endif

#define DBG_IOCTL	1<<0
#define DBG_IO		1<<1
#define DBG_PROBE	1<<2
#define DBG_FUNC	1<<3

//String operations
#define rmspace(str)	while(*str==' ') str++;
#define goto2next(str)	while(*str!=' ') str++; while(*str==' ') str++;


static int usbvision_nr = 0;			// sequential number of usbvision device

static struct usbvision_v4l2_format_st usbvision_v4l2_format[] = {
	{ 1, 1,  8, V4L2_PIX_FMT_GREY    , "GREY" },
	{ 1, 2, 16, V4L2_PIX_FMT_RGB565  , "RGB565" },
	{ 1, 3, 24, V4L2_PIX_FMT_RGB24   , "RGB24" },
	{ 1, 4, 32, V4L2_PIX_FMT_RGB32   , "RGB32" },
	{ 1, 2, 16, V4L2_PIX_FMT_RGB555  , "RGB555" },
	{ 1, 2, 16, V4L2_PIX_FMT_YUYV    , "YUV422" },
	{ 1, 2, 12, V4L2_PIX_FMT_YVU420  , "YUV420P" }, // 1.5 !
	{ 1, 2, 16, V4L2_PIX_FMT_YUV422P , "YUV422P" }
};

/* supported tv norms */
static struct usbvision_tvnorm tvnorms[] = {
	{
		.name = "PAL",
		.id = V4L2_STD_PAL,
	}, {
		.name = "NTSC",
		.id = V4L2_STD_NTSC,
	}, {
		 .name = "SECAM",
		 .id = V4L2_STD_SECAM,
	}, {
		.name = "PAL-M",
		.id = V4L2_STD_PAL_M,
	}
};

#define TVNORMS ARRAY_SIZE(tvnorms)

// Function prototypes
static void usbvision_release(struct usb_usbvision *usbvision);

// Default initalization of device driver parameters
static int isocMode = ISOC_MODE_COMPRESS;		// Set the default format for ISOC endpoint
static int video_debug = 0;				// Set the default Debug Mode of the device driver
static int PowerOnAtOpen = 1;				// Set the default device to power on at startup
static int video_nr = -1;				// Sequential Number of Video Device
static int radio_nr = -1;				// Sequential Number of Radio Device
static int vbi_nr = -1;					// Sequential Number of VBI Device
static char *CustomDevice=NULL;				// Set as nothing....

// Grab parameters for the device driver

#if defined(module_param)                               // Showing parameters under SYSFS
module_param(isocMode, int, 0444);
module_param(video_debug, int, 0444);
module_param(PowerOnAtOpen, int, 0444);
module_param(video_nr, int, 0444);
module_param(radio_nr, int, 0444);
module_param(vbi_nr, int, 0444);
module_param(CustomDevice, charp, 0444);
#else							// Old Style
MODULE_PARAM(isocMode, "i");
MODULE_PARM(video_debug, "i");				// Grab the Debug Mode of the device driver
MODULE_PARM(adjustCompression, "i");			// Grab the compression to be adaptive
MODULE_PARM(PowerOnAtOpen, "i");			// Grab the device to power on at startup
MODULE_PARM(SwitchSVideoInput, "i");			// To help people with Black and White output with using s-video input.  Some cables and input device are wired differently.
MODULE_PARM(video_nr, "i");				// video_nr option allows to specify a certain /dev/videoX device (like /dev/video0 or /dev/video1 ...)
MODULE_PARM(radio_nr, "i");				// radio_nr option allows to specify a certain /dev/radioX device (like /dev/radio0 or /dev/radio1 ...)
MODULE_PARM(vbi_nr, "i");				// vbi_nr option allows to specify a certain /dev/vbiX device (like /dev/vbi0 or /dev/vbi1 ...)
MODULE_PARM(CustomDevice, "s");				// .... CustomDevice
#endif

MODULE_PARM_DESC(isocMode, " Set the default format for ISOC endpoint.  Default: 0x60 (Compression On)");
MODULE_PARM_DESC(video_debug, " Set the default Debug Mode of the device driver.  Default: 0 (Off)");
MODULE_PARM_DESC(PowerOnAtOpen, " Set the default device to power on when device is opened.  Default: 1 (On)");
MODULE_PARM_DESC(video_nr, "Set video device number (/dev/videoX).  Default: -1 (autodetect)");
MODULE_PARM_DESC(radio_nr, "Set radio device number (/dev/radioX).  Default: -1 (autodetect)");
MODULE_PARM_DESC(vbi_nr, "Set vbi device number (/dev/vbiX).  Default: -1 (autodetect)");
MODULE_PARM_DESC(CustomDevice, " Define the fine tuning parameters for the device.  Default: null");


// Misc stuff
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);
MODULE_VERSION(USBVISION_VERSION_STRING);
MODULE_ALIAS(DRIVER_ALIAS);


/****************************************************************************************/
/* SYSFS Code - Copied from the stv680.c usb module.					*/
/* Device information is located at /sys/class/video4linux/video0			*/
/* Device parameters information is located at /sys/module/usbvision                    */
/* Device USB Information is located at /sys/bus/usb/drivers/USBVision Video Grabber    */
/****************************************************************************************/


#define YES_NO(x) ((x) ? "Yes" : "No")

static inline struct usb_usbvision *cd_to_usbvision(struct class_device *cd)
{
	struct video_device *vdev = to_video_device(cd);
	return video_get_drvdata(vdev);
}

static ssize_t show_version(struct class_device *cd, char *buf)
{
	return sprintf(buf, "%s\n", USBVISION_VERSION_STRING);
}
static CLASS_DEVICE_ATTR(version, S_IRUGO, show_version, NULL);

static ssize_t show_model(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%s\n", usbvision_device_data[usbvision->DevModel].ModelString);
}
static CLASS_DEVICE_ATTR(model, S_IRUGO, show_model, NULL);

static ssize_t show_hue(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	struct v4l2_control ctrl;
	ctrl.id = V4L2_CID_HUE;
	ctrl.value = 0;
	call_i2c_clients(usbvision, VIDIOC_G_CTRL, &ctrl);
	return sprintf(buf, "%d\n", ctrl.value >> 8);
}
static CLASS_DEVICE_ATTR(hue, S_IRUGO, show_hue, NULL);

static ssize_t show_contrast(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	struct v4l2_control ctrl;
	ctrl.id = V4L2_CID_CONTRAST;
	ctrl.value = 0;
	call_i2c_clients(usbvision, VIDIOC_G_CTRL, &ctrl);
	return sprintf(buf, "%d\n", ctrl.value >> 8);
}
static CLASS_DEVICE_ATTR(contrast, S_IRUGO, show_contrast, NULL);

static ssize_t show_brightness(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	struct v4l2_control ctrl;
	ctrl.id = V4L2_CID_BRIGHTNESS;
	ctrl.value = 0;
	call_i2c_clients(usbvision, VIDIOC_G_CTRL, &ctrl);
	return sprintf(buf, "%d\n", ctrl.value >> 8);
}
static CLASS_DEVICE_ATTR(brightness, S_IRUGO, show_brightness, NULL);

static ssize_t show_saturation(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	struct v4l2_control ctrl;
	ctrl.id = V4L2_CID_SATURATION;
	ctrl.value = 0;
	call_i2c_clients(usbvision, VIDIOC_G_CTRL, &ctrl);
	return sprintf(buf, "%d\n", ctrl.value >> 8);
}
static CLASS_DEVICE_ATTR(saturation, S_IRUGO, show_saturation, NULL);

static ssize_t show_streaming(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%s\n", YES_NO(usbvision->streaming==Stream_On?1:0));
}
static CLASS_DEVICE_ATTR(streaming, S_IRUGO, show_streaming, NULL);

static ssize_t show_compression(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%s\n", YES_NO(usbvision->isocMode==ISOC_MODE_COMPRESS));
}
static CLASS_DEVICE_ATTR(compression, S_IRUGO, show_compression, NULL);

static ssize_t show_device_bridge(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%d\n", usbvision->bridgeType);
}
static CLASS_DEVICE_ATTR(bridge, S_IRUGO, show_device_bridge, NULL);

static void usbvision_create_sysfs(struct video_device *vdev)
{
	int res;
	if (vdev) {
		res=video_device_create_file(vdev, &class_device_attr_version);
		res=video_device_create_file(vdev, &class_device_attr_model);
		res=video_device_create_file(vdev, &class_device_attr_hue);
		res=video_device_create_file(vdev, &class_device_attr_contrast);
		res=video_device_create_file(vdev, &class_device_attr_brightness);
		res=video_device_create_file(vdev, &class_device_attr_saturation);
		res=video_device_create_file(vdev, &class_device_attr_streaming);
		res=video_device_create_file(vdev, &class_device_attr_compression);
		res=video_device_create_file(vdev, &class_device_attr_bridge);
	}
}

static void usbvision_remove_sysfs(struct video_device *vdev)
{
	if (vdev) {
		video_device_remove_file(vdev, &class_device_attr_version);
		video_device_remove_file(vdev, &class_device_attr_model);
		video_device_remove_file(vdev, &class_device_attr_hue);
		video_device_remove_file(vdev, &class_device_attr_contrast);
		video_device_remove_file(vdev, &class_device_attr_brightness);
		video_device_remove_file(vdev, &class_device_attr_saturation);
		video_device_remove_file(vdev, &class_device_attr_streaming);
		video_device_remove_file(vdev, &class_device_attr_compression);
		video_device_remove_file(vdev, &class_device_attr_bridge);
	}
}


/*
 * usbvision_open()
 *
 * This is part of Video 4 Linux API. The driver can be opened by one
 * client only (checks internal counter 'usbvision->user'). The procedure
 * then allocates buffers needed for video processing.
 *
 */
static int usbvision_v4l2_open(struct inode *inode, struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);
	int errCode = 0;

	PDEBUG(DBG_IO, "open");


	usbvision_reset_powerOffTimer(usbvision);

	if (usbvision->user)
		errCode = -EBUSY;
	else {
		/* Allocate memory for the frame buffers */
		errCode = usbvision_frames_alloc(usbvision);
		if(!errCode) {
			/* Allocate memory for the scratch ring buffer */
			errCode = usbvision_scratch_alloc(usbvision);
			if(!errCode) {
				/* Allocate memory for the USB S buffers */
				errCode = usbvision_sbuf_alloc(usbvision);
				if ((!errCode) && (usbvision->isocMode==ISOC_MODE_COMPRESS)) {
					/* Allocate intermediate decompression buffers only if needed */
					errCode = usbvision_decompress_alloc(usbvision);
				}
			}
		}
		if (errCode) {
			/* Deallocate all buffers if trouble */
			usbvision_frames_free(usbvision);
			usbvision_scratch_free(usbvision);
			usbvision_sbuf_free(usbvision);
			usbvision_decompress_free(usbvision);
		}
	}

	/* If so far no errors then we shall start the camera */
	if (!errCode) {
		down(&usbvision->lock);
		if (usbvision->power == 0) {
			usbvision_power_on(usbvision);
			usbvision_init_i2c(usbvision);
		}

		/* Send init sequence only once, it's large! */
		if (!usbvision->initialized) {
			int setup_ok = 0;
			setup_ok = usbvision_setup(usbvision,isocMode);
			if (setup_ok)
				usbvision->initialized = 1;
			else
				errCode = -EBUSY;
		}

		if (!errCode) {
			usbvision_begin_streaming(usbvision);
			errCode = usbvision_init_isoc(usbvision);
			/* device needs to be initialized before isoc transfer */
			usbvision_muxsel(usbvision,0);
			usbvision->user++;
		}
		else {
			if (PowerOnAtOpen) {
				usbvision_i2c_usb_del_bus(&usbvision->i2c_adap);
				usbvision_power_off(usbvision);
				usbvision->initialized = 0;
			}
		}
		up(&usbvision->lock);
	}

	if (errCode) {
	}

	/* prepare queues */
	usbvision_empty_framequeues(usbvision);

	PDEBUG(DBG_IO, "success");
	return errCode;
}

/*
 * usbvision_v4l2_close()
 *
 * This is part of Video 4 Linux API. The procedure
 * stops streaming and deallocates all buffers that were earlier
 * allocated in usbvision_v4l2_open().
 *
 */
static int usbvision_v4l2_close(struct inode *inode, struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);

	PDEBUG(DBG_IO, "close");
	down(&usbvision->lock);

	usbvision_audio_off(usbvision);
	usbvision_restart_isoc(usbvision);
	usbvision_stop_isoc(usbvision);

	usbvision_decompress_free(usbvision);
	usbvision_rvfree(usbvision->fbuf, usbvision->fbuf_size);
	usbvision_scratch_free(usbvision);
	usbvision_sbuf_free(usbvision);

	usbvision->user--;

	if (PowerOnAtOpen) {
		/* power off in a little while to avoid off/on every close/open short sequences */
		usbvision_set_powerOffTimer(usbvision);
		usbvision->initialized = 0;
	}

	up(&usbvision->lock);

	if (usbvision->remove_pending) {
		info("%s: Final disconnect", __FUNCTION__);
		usbvision_release(usbvision);
	}

	PDEBUG(DBG_IO, "success");


	return 0;
}


/*
 * usbvision_ioctl()
 *
 * This is part of Video 4 Linux API. The procedure handles ioctl() calls.
 *
 */
static int usbvision_v4l2_do_ioctl(struct inode *inode, struct file *file,
				 unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return -EFAULT;

	switch (cmd) {

#ifdef CONFIG_VIDEO_ADV_DEBUG
		/* ioctls to allow direct acces to the NT100x registers */
		case VIDIOC_INT_G_REGISTER:
		{
			struct v4l2_register *reg = arg;
			int errCode;

			if (reg->i2c_id != 0)
				return -EINVAL;
			/* NT100x has a 8-bit register space */
			errCode = usbvision_read_reg(usbvision, reg->reg&0xff);
			if (errCode < 0) {
				err("%s: VIDIOC_INT_G_REGISTER failed: error %d", __FUNCTION__, errCode);
			}
			else {
				reg->val=(unsigned char)errCode;
				PDEBUG(DBG_IOCTL, "VIDIOC_INT_G_REGISTER reg=0x%02X, value=0x%02X",
							(unsigned int)reg->reg, reg->val);
				errCode = 0; // No error
			}
			return errCode;
		}
		case VIDIOC_INT_S_REGISTER:
		{
			struct v4l2_register *reg = arg;
			int errCode;

			if (reg->i2c_id != 0)
				return -EINVAL;
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			errCode = usbvision_write_reg(usbvision, reg->reg&0xff, reg->val);
			if (errCode < 0) {
				err("%s: VIDIOC_INT_S_REGISTER failed: error %d", __FUNCTION__, errCode);
			}
			else {
				PDEBUG(DBG_IOCTL, "VIDIOC_INT_S_REGISTER reg=0x%02X, value=0x%02X",
							(unsigned int)reg->reg, reg->val);
				errCode = 0;
			}
			return 0;
		}
#endif
		case VIDIOC_QUERYCAP:
		{
			struct v4l2_capability *vc=arg;

			memset(vc, 0, sizeof(*vc));
			strlcpy(vc->driver, "USBVision", sizeof(vc->driver));
			strlcpy(vc->card, usbvision_device_data[usbvision->DevModel].ModelString,
				sizeof(vc->card));
			strlcpy(vc->bus_info, usbvision->dev->dev.bus_id,
				sizeof(vc->bus_info));
			vc->version = USBVISION_DRIVER_VERSION;
			vc->capabilities = V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_AUDIO |
				V4L2_CAP_READWRITE |
				V4L2_CAP_STREAMING |
				(usbvision->have_tuner ? V4L2_CAP_TUNER : 0);
			PDEBUG(DBG_IOCTL, "VIDIOC_QUERYCAP");
			return 0;
		}
		case VIDIOC_ENUMINPUT:
		{
			struct v4l2_input *vi = arg;
			int chan;

			if ((vi->index >= usbvision->video_inputs) || (vi->index < 0) )
				return -EINVAL;
			if (usbvision->have_tuner) {
				chan = vi->index;
			}
			else {
				chan = vi->index + 1; //skip Television string
			}
			switch(chan) {
				case 0:
					if (usbvision_device_data[usbvision->DevModel].VideoChannels == 4) {
						strcpy(vi->name, "White Video Input");
					}
					else {
						strcpy(vi->name, "Television");
						vi->type = V4L2_INPUT_TYPE_TUNER;
						vi->audioset = 1;
						vi->tuner = chan;
						vi->std = V4L2_STD_PAL | V4L2_STD_NTSC | V4L2_STD_SECAM;
					}
					break;
				case 1:
					vi->type = V4L2_INPUT_TYPE_CAMERA;
					if (usbvision_device_data[usbvision->DevModel].VideoChannels == 4) {
						strcpy(vi->name, "Green Video Input");
					}
					else {
						strcpy(vi->name, "Composite Video Input");
					}
					vi->std = V4L2_STD_PAL;
					break;
				case 2:
					vi->type = V4L2_INPUT_TYPE_CAMERA;
					if (usbvision_device_data[usbvision->DevModel].VideoChannels == 4) {
						strcpy(vi->name, "Yellow Video Input");
					}
					else {
					strcpy(vi->name, "S-Video Input");
					}
					vi->std = V4L2_STD_PAL;
					break;
				case 3:
					vi->type = V4L2_INPUT_TYPE_CAMERA;
					strcpy(vi->name, "Red Video Input");
					vi->std = V4L2_STD_PAL;
					break;
			}
			PDEBUG(DBG_IOCTL, "VIDIOC_ENUMINPUT name=%s:%d tuners=%d type=%d norm=%x",
			       vi->name, vi->index, vi->tuner,vi->type,(int)vi->std);
			return 0;
		}
		case VIDIOC_ENUMSTD:
		{
			struct v4l2_standard *e = arg;
			unsigned int i;
			int ret;

			i = e->index;
			if (i >= TVNORMS)
				return -EINVAL;
			ret = v4l2_video_std_construct(e, tvnorms[e->index].id,
						       tvnorms[e->index].name);
			e->index = i;
			if (ret < 0)
				return ret;
			return 0;
		}
		case VIDIOC_G_INPUT:
		{
			int *input = arg;
			*input = usbvision->ctl_input;
			return 0;
		}
		case VIDIOC_S_INPUT:
		{
			int *input = arg;
			if ((*input >= usbvision->video_inputs) || (*input < 0) )
				return -EINVAL;
			usbvision->ctl_input = *input;

			down(&usbvision->lock);
			usbvision_muxsel(usbvision, usbvision->ctl_input);
			usbvision_set_input(usbvision);
			usbvision_set_output(usbvision, usbvision->curwidth, usbvision->curheight);
			up(&usbvision->lock);
			return 0;
		}
		case VIDIOC_G_STD:
		{
			v4l2_std_id *id = arg;

			*id = usbvision->tvnorm->id;

			PDEBUG(DBG_IOCTL, "VIDIOC_G_STD std_id=%s", usbvision->tvnorm->name);
			return 0;
		}
		case VIDIOC_S_STD:
		{
			v4l2_std_id *id = arg;
			unsigned int i;

			for (i = 0; i < TVNORMS; i++)
				if (*id == tvnorms[i].id)
					break;
			if (i == TVNORMS)
				for (i = 0; i < TVNORMS; i++)
					if (*id & tvnorms[i].id)
						break;
			if (i == TVNORMS)
				return -EINVAL;

			down(&usbvision->lock);
			usbvision->tvnorm = &tvnorms[i];

			call_i2c_clients(usbvision, VIDIOC_S_STD,
					 &usbvision->tvnorm->id);

			up(&usbvision->lock);

			PDEBUG(DBG_IOCTL, "VIDIOC_S_STD std_id=%s", usbvision->tvnorm->name);
			return 0;
		}
		case VIDIOC_G_TUNER:
		{
			struct v4l2_tuner *vt = arg;

			if (!usbvision->have_tuner || vt->index)	// Only tuner 0
				return -EINVAL;
			strcpy(vt->name, "Television");
			/* Let clients fill in the remainder of this struct */
			call_i2c_clients(usbvision,VIDIOC_G_TUNER,vt);

			PDEBUG(DBG_IOCTL, "VIDIOC_G_TUNER signal=%x, afc=%x",vt->signal,vt->afc);
			return 0;
		}
		case VIDIOC_S_TUNER:
		{
			struct v4l2_tuner *vt = arg;

			// Only no or one tuner for now
			if (!usbvision->have_tuner || vt->index)
				return -EINVAL;
			/* let clients handle this */
			call_i2c_clients(usbvision,VIDIOC_S_TUNER,vt);

			PDEBUG(DBG_IOCTL, "VIDIOC_S_TUNER");
			return 0;
		}
		case VIDIOC_G_FREQUENCY:
		{
			struct v4l2_frequency *freq = arg;

			freq->tuner = 0; // Only one tuner
			freq->type = V4L2_TUNER_ANALOG_TV;
			freq->frequency = usbvision->freq;
			PDEBUG(DBG_IOCTL, "VIDIOC_G_FREQUENCY freq=0x%X", (unsigned)freq->frequency);
			return 0;
		}
		case VIDIOC_S_FREQUENCY:
		{
			struct v4l2_frequency *freq = arg;

			// Only no or one tuner for now
			if (!usbvision->have_tuner || freq->tuner)
				return -EINVAL;

			usbvision->freq = freq->frequency;
			call_i2c_clients(usbvision, cmd, freq);
			PDEBUG(DBG_IOCTL, "VIDIOC_S_FREQUENCY freq=0x%X", (unsigned)freq->frequency);
			return 0;
		}
		case VIDIOC_G_AUDIO:
		{
			struct v4l2_audio *v = arg;
			memset(v,0, sizeof(v));
			strcpy(v->name, "TV");
			PDEBUG(DBG_IOCTL, "VIDIOC_G_AUDIO");
			return 0;
		}
		case VIDIOC_S_AUDIO:
		{
			struct v4l2_audio *v = arg;
			if(v->index) {
				return -EINVAL;
			}
			PDEBUG(DBG_IOCTL, "VIDIOC_S_AUDIO");
			return 0;
		}
		case VIDIOC_QUERYCTRL:
		{
			struct v4l2_queryctrl *ctrl = arg;
			int id=ctrl->id;

			memset(ctrl,0,sizeof(*ctrl));
			ctrl->id=id;

			call_i2c_clients(usbvision, cmd, arg);

			if (ctrl->type)
				return 0;
			else
				return -EINVAL;

			PDEBUG(DBG_IOCTL,"VIDIOC_QUERYCTRL id=%x value=%x",ctrl->id,ctrl->type);
		}
		case VIDIOC_G_CTRL:
		{
			struct v4l2_control *ctrl = arg;
			PDEBUG(DBG_IOCTL,"VIDIOC_G_CTRL id=%x value=%x",ctrl->id,ctrl->value);
			call_i2c_clients(usbvision, VIDIOC_G_CTRL, ctrl);
			return 0;
		}
		case VIDIOC_S_CTRL:
		{
			struct v4l2_control *ctrl = arg;

			PDEBUG(DBG_IOCTL, "VIDIOC_S_CTRL id=%x value=%x",ctrl->id,ctrl->value);
			call_i2c_clients(usbvision, VIDIOC_S_CTRL, ctrl);
			return 0;
		}
		case VIDIOC_REQBUFS:
		{
			struct v4l2_requestbuffers *vr = arg;
			int ret;

			RESTRICT_TO_RANGE(vr->count,1,USBVISION_NUMFRAMES);

			// Check input validity : the user must do a VIDEO CAPTURE and MMAP method.
			if((vr->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) ||
			   (vr->memory != V4L2_MEMORY_MMAP))
				return -EINVAL;

			if(usbvision->streaming == Stream_On) {
				if ((ret = usbvision_stream_interrupt(usbvision)))
				    return ret;
			}

			usbvision_empty_framequeues(usbvision);

			usbvision->curFrame = NULL;

			PDEBUG(DBG_IOCTL, "VIDIOC_REQBUFS count=%d",vr->count);
			return 0;
		}
		case VIDIOC_QUERYBUF:
		{
			struct v4l2_buffer *vb = arg;
			struct usbvision_frame *frame;

			// FIXME : must control that buffers are mapped (VIDIOC_REQBUFS has been called)

			if(vb->type != V4L2_CAP_VIDEO_CAPTURE) {
				return -EINVAL;
			}
			if(vb->index>=USBVISION_NUMFRAMES)  {
				return -EINVAL;
			}
			// Updating the corresponding frame state
			vb->flags = 0;
			frame = &usbvision->frame[vb->index];
			if(frame->grabstate >= FrameState_Ready)
				vb->flags |= V4L2_BUF_FLAG_QUEUED;
			if(frame->grabstate >= FrameState_Done)
				vb->flags |= V4L2_BUF_FLAG_DONE;
			if(frame->grabstate == FrameState_Unused)
				vb->flags |= V4L2_BUF_FLAG_MAPPED;
			vb->memory = V4L2_MEMORY_MMAP;

			vb->m.offset = vb->index*usbvision->max_frame_size;

			vb->memory = V4L2_MEMORY_MMAP;
			vb->field = V4L2_FIELD_NONE;
			vb->length = usbvision->max_frame_size;
			vb->timestamp = usbvision->frame[vb->index].timestamp;
			vb->sequence = usbvision->frame[vb->index].sequence;
			return 0;
		}
		case VIDIOC_QBUF:
		{
			struct v4l2_buffer *vb = arg;
			struct usbvision_frame *frame;
			unsigned long lock_flags;

			// FIXME : works only on VIDEO_CAPTURE MODE, MMAP.
			if(vb->type != V4L2_CAP_VIDEO_CAPTURE) {
				return -EINVAL;
			}
			if(vb->index>=USBVISION_NUMFRAMES)  {
				return -EINVAL;
			}

			frame = &usbvision->frame[vb->index];

			if (frame->grabstate != FrameState_Unused) {
				return -EAGAIN;
			}

			/* Mark it as ready and enqueue frame */
			frame->grabstate = FrameState_Ready;
			frame->scanstate = ScanState_Scanning;
			frame->scanlength = 0;	/* Accumulated in usbvision_parse_data() */

			vb->flags &= ~V4L2_BUF_FLAG_DONE;

			/* set v4l2_format index */
			frame->v4l2_format = usbvision->palette;

			spin_lock_irqsave(&usbvision->queue_lock, lock_flags);
			list_add_tail(&usbvision->frame[vb->index].frame, &usbvision->inqueue);
			spin_unlock_irqrestore(&usbvision->queue_lock, lock_flags);

			PDEBUG(DBG_IOCTL, "VIDIOC_QBUF frame #%d",vb->index);
			return 0;
		}
		case VIDIOC_DQBUF:
		{
			struct v4l2_buffer *vb = arg;
			int ret;
			struct usbvision_frame *f;
			unsigned long lock_flags;

			if (vb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
				return -EINVAL;

			if (list_empty(&(usbvision->outqueue))) {
				if (usbvision->streaming == Stream_Idle)
					return -EINVAL;
				ret = wait_event_interruptible
					(usbvision->wait_frame,
					 !list_empty(&(usbvision->outqueue)));
				if (ret)
					return ret;
			}

			spin_lock_irqsave(&usbvision->queue_lock, lock_flags);
			f = list_entry(usbvision->outqueue.next,
				       struct usbvision_frame, frame);
			list_del(usbvision->outqueue.next);
			spin_unlock_irqrestore(&usbvision->queue_lock, lock_flags);

			f->grabstate = FrameState_Unused;

			vb->memory = V4L2_MEMORY_MMAP;
			vb->flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE;
			vb->index = f->index;
			vb->sequence = f->sequence;
			vb->timestamp = f->timestamp;
			vb->field = V4L2_FIELD_NONE;
			vb->bytesused = f->scanlength;

			return 0;
		}
		case VIDIOC_STREAMON:
		{
			int b=V4L2_BUF_TYPE_VIDEO_CAPTURE;

			usbvision->streaming = Stream_On;

			call_i2c_clients(usbvision,VIDIOC_STREAMON , &b);

			PDEBUG(DBG_IOCTL, "VIDIOC_STREAMON");

			return 0;
		}
		case VIDIOC_STREAMOFF:
		{
			int *type = arg;
			int b=V4L2_BUF_TYPE_VIDEO_CAPTURE;

			if (*type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
				return -EINVAL;

			if(usbvision->streaming == Stream_On) {
				usbvision_stream_interrupt(usbvision);
				// Stop all video streamings
				call_i2c_clients(usbvision,VIDIOC_STREAMOFF , &b);
			}
			usbvision_empty_framequeues(usbvision);

			PDEBUG(DBG_IOCTL, "VIDIOC_STREAMOFF");
			return 0;
		}
		case VIDIOC_ENUM_FMT:
		{
			struct v4l2_fmtdesc *vfd = arg;

			if(vfd->index>=USBVISION_SUPPORTED_PALETTES-1) {
				return -EINVAL;
			}
			vfd->flags = 0;
			vfd->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			strcpy(vfd->description,usbvision_v4l2_format[vfd->index].desc);
			vfd->pixelformat = usbvision_v4l2_format[vfd->index].format;
			memset(vfd->reserved, 0, sizeof(vfd->reserved));
			return 0;
		}
		case VIDIOC_G_FMT:
		{
			struct v4l2_format *vf = arg;

			switch (vf->type) {
				case V4L2_BUF_TYPE_VIDEO_CAPTURE:
				{
					vf->fmt.pix.width = usbvision->curwidth;
					vf->fmt.pix.height = usbvision->curheight;
					vf->fmt.pix.pixelformat = usbvision->palette.format;
					vf->fmt.pix.bytesperline =  usbvision->curwidth*usbvision->palette.bytes_per_pixel;
					vf->fmt.pix.sizeimage = vf->fmt.pix.bytesperline*usbvision->curheight;
					vf->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
					vf->fmt.pix.field = V4L2_FIELD_NONE; /* Always progressive image */
				}
				return 0;
				default:
					PDEBUG(DBG_IOCTL, "VIDIOC_G_FMT invalid type %d",vf->type);
					return -EINVAL;
			}
			PDEBUG(DBG_IOCTL, "VIDIOC_G_FMT w=%d, h=%d",vf->fmt.win.w.width, vf->fmt.win.w.height);
			return 0;
		}
		case VIDIOC_TRY_FMT:
		case VIDIOC_S_FMT:
		{
			struct v4l2_format *vf = arg;
			int formatIdx,ret;

			switch(vf->type) {
				case V4L2_BUF_TYPE_VIDEO_CAPTURE:
				{
					/* Find requested format in available ones */
					for(formatIdx=0;formatIdx<USBVISION_SUPPORTED_PALETTES;formatIdx++) {
						if(vf->fmt.pix.pixelformat == usbvision_v4l2_format[formatIdx].format) {
							usbvision->palette = usbvision_v4l2_format[formatIdx];
							break;
						}
					}
					/* robustness */
					if(formatIdx == USBVISION_SUPPORTED_PALETTES) {
						return -EINVAL;
					}
					RESTRICT_TO_RANGE(vf->fmt.pix.width, MIN_FRAME_WIDTH, MAX_FRAME_WIDTH);
					RESTRICT_TO_RANGE(vf->fmt.pix.height, MIN_FRAME_HEIGHT, MAX_FRAME_HEIGHT);

					/* stop io in case it is already in progress */
					if(usbvision->streaming == Stream_On) {
						if ((ret = usbvision_stream_interrupt(usbvision)))
							return ret;
					}
					usbvision_empty_framequeues(usbvision);

					usbvision->curFrame = NULL;

					// by now we are committed to the new data...
					down(&usbvision->lock);
					usbvision_set_output(usbvision, vf->fmt.pix.width, vf->fmt.pix.height);
					up(&usbvision->lock);

					PDEBUG(DBG_IOCTL, "VIDIOC_S_FMT grabdisplay w=%d, h=%d, format=%s",
					       vf->fmt.pix.width, vf->fmt.pix.height,usbvision->palette.desc);
					return 0;
				}
				default:
					return -EINVAL;
			}
		}
		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static int usbvision_v4l2_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, usbvision_v4l2_do_ioctl);
}


static ssize_t usbvision_v4l2_read(struct file *file, char *buf,
		      size_t count, loff_t *ppos)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);
	int noblock = file->f_flags & O_NONBLOCK;
	unsigned long lock_flags;

	int frmx = -1;
	int ret,i;
	struct usbvision_frame *frame;

	PDEBUG(DBG_IO, "%s: %ld bytes, noblock=%d", __FUNCTION__, (unsigned long)count, noblock);

	if (!USBVISION_IS_OPERATIONAL(usbvision) || (buf == NULL))
		return -EFAULT;

	/* no stream is running, make it running ! */
	usbvision->streaming = Stream_On;
	call_i2c_clients(usbvision,VIDIOC_STREAMON , NULL);

	/* First, enqueue as many frames as possible (like a user of VIDIOC_QBUF would do) */
	for(i=0;i<USBVISION_NUMFRAMES;i++) {
		frame = &usbvision->frame[i];
		if(frame->grabstate == FrameState_Unused) {
			/* Mark it as ready and enqueue frame */
			frame->grabstate = FrameState_Ready;
			frame->scanstate = ScanState_Scanning;
			frame->scanlength = 0;	/* Accumulated in usbvision_parse_data() */

			/* set v4l2_format index */
			frame->v4l2_format = usbvision->palette;

			spin_lock_irqsave(&usbvision->queue_lock, lock_flags);
			list_add_tail(&frame->frame, &usbvision->inqueue);
			spin_unlock_irqrestore(&usbvision->queue_lock, lock_flags);
		}
	}

	/* Then try to steal a frame (like a VIDIOC_DQBUF would do) */
	if (list_empty(&(usbvision->outqueue))) {
		if(noblock)
			return -EAGAIN;

		ret = wait_event_interruptible
			(usbvision->wait_frame,
			 !list_empty(&(usbvision->outqueue)));
		if (ret)
			return ret;
	}

	spin_lock_irqsave(&usbvision->queue_lock, lock_flags);
	frame = list_entry(usbvision->outqueue.next,
			   struct usbvision_frame, frame);
	list_del(usbvision->outqueue.next);
	spin_unlock_irqrestore(&usbvision->queue_lock, lock_flags);

	/* An error returns an empty frame */
	if (frame->grabstate == FrameState_Error) {
		frame->bytes_read = 0;
		return 0;
	}

	PDEBUG(DBG_IO, "%s: frmx=%d, bytes_read=%ld, scanlength=%ld", __FUNCTION__,
		       frame->index, frame->bytes_read, frame->scanlength);

	/* copy bytes to user space; we allow for partials reads */
	if ((count + frame->bytes_read) > (unsigned long)frame->scanlength)
		count = frame->scanlength - frame->bytes_read;

	if (copy_to_user(buf, frame->data + frame->bytes_read, count)) {
		return -EFAULT;
	}

	frame->bytes_read += count;
	PDEBUG(DBG_IO, "%s: {copy} count used=%ld, new bytes_read=%ld", __FUNCTION__,
		       (unsigned long)count, frame->bytes_read);

	// For now, forget the frame if it has not been read in one shot.
/* 	if (frame->bytes_read >= frame->scanlength) {// All data has been read */
		frame->bytes_read = 0;

		/* Mark it as available to be used again. */
		usbvision->frame[frmx].grabstate = FrameState_Unused;
/* 	} */

	return count;
}

static int usbvision_v4l2_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start,
		start = vma->vm_start;
	void *pos;
	u32 i;

	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);

	down(&usbvision->lock);

	if (!USBVISION_IS_OPERATIONAL(usbvision)) {
		up(&usbvision->lock);
		return -EFAULT;
	}

	if (!(vma->vm_flags & VM_WRITE) ||
	    size != PAGE_ALIGN(usbvision->max_frame_size)) {
		up(&usbvision->lock);
		return -EINVAL;
	}

	for (i = 0; i < USBVISION_NUMFRAMES; i++) {
		if (((usbvision->max_frame_size*i) >> PAGE_SHIFT) == vma->vm_pgoff)
			break;
	}
	if (i == USBVISION_NUMFRAMES) {
		PDEBUG(DBG_FUNC, "mmap: user supplied mapping address is out of range");
		up(&usbvision->lock);
		return -EINVAL;
	}

	/* VM_IO is eventually going to replace PageReserved altogether */
	vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_RESERVED;	/* avoid to swap out this VMA */

	pos = usbvision->frame[i].data;
	while (size > 0) {

		if (vm_insert_page(vma, start, vmalloc_to_page(pos))) {
			PDEBUG(DBG_FUNC, "mmap: vm_insert_page failed");
			up(&usbvision->lock);
			return -EAGAIN;
		}
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	up(&usbvision->lock);
	return 0;
}


/*
 * Here comes the stuff for radio on usbvision based devices
 *
 */
static int usbvision_radio_open(struct inode *inode, struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);
	struct v4l2_frequency freq;
	int errCode = 0;

	PDEBUG(DBG_IO, "%s:", __FUNCTION__);

	down(&usbvision->lock);

	if (usbvision->user) {
		err("%s: Someone tried to open an already opened USBVision Radio!", __FUNCTION__);
		errCode = -EBUSY;
	}
	else {
		if(PowerOnAtOpen) {
			usbvision_reset_powerOffTimer(usbvision);
			if (usbvision->power == 0) {
				usbvision_power_on(usbvision);
				usbvision_init_i2c(usbvision);
			}
		}

		// If so far no errors then we shall start the radio
		usbvision->radio = 1;
		call_i2c_clients(usbvision,AUDC_SET_RADIO,&usbvision->tuner_type);
		freq.frequency = 1517; //SWR3 @ 94.8MHz
		call_i2c_clients(usbvision, VIDIOC_S_FREQUENCY, &freq);
		usbvision_set_audio(usbvision, USBVISION_AUDIO_RADIO);
		usbvision->user++;
	}

	if (errCode) {
		if (PowerOnAtOpen) {
			usbvision_i2c_usb_del_bus(&usbvision->i2c_adap);
			usbvision_power_off(usbvision);
			usbvision->initialized = 0;
		}
	}
	up(&usbvision->lock);
	return errCode;
}


static int usbvision_radio_close(struct inode *inode, struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);
	int errCode = 0;

	PDEBUG(DBG_IO, "");

	down(&usbvision->lock);

	usbvision_audio_off(usbvision);
	usbvision->radio=0;
	usbvision->user--;

	if (PowerOnAtOpen) {
		usbvision_set_powerOffTimer(usbvision);
		usbvision->initialized = 0;
	}

	up(&usbvision->lock);

	if (usbvision->remove_pending) {
		info("%s: Final disconnect", __FUNCTION__);
		usbvision_release(usbvision);
	}


	PDEBUG(DBG_IO, "success");

	return errCode;
}

static int usbvision_do_radio_ioctl(struct inode *inode, struct file *file,
				 unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return -EIO;

	switch (cmd) {
		case VIDIOC_QUERYCAP:
		{
			struct v4l2_capability *vc=arg;

			memset(vc, 0, sizeof(*vc));
			strlcpy(vc->driver, "USBVision", sizeof(vc->driver));
			strlcpy(vc->card, usbvision_device_data[usbvision->DevModel].ModelString,
				sizeof(vc->card));
			strlcpy(vc->bus_info, usbvision->dev->dev.bus_id,
				sizeof(vc->bus_info));
			vc->version = USBVISION_DRIVER_VERSION;
			vc->capabilities = (usbvision->have_tuner ? V4L2_CAP_TUNER : 0);
			PDEBUG(DBG_IO, "VIDIOC_QUERYCAP");
			return 0;
		}
		case VIDIOC_QUERYCTRL:
		{
			struct v4l2_queryctrl *ctrl = arg;
			int id=ctrl->id;

			memset(ctrl,0,sizeof(*ctrl));
			ctrl->id=id;

			call_i2c_clients(usbvision, cmd, arg);
			PDEBUG(DBG_IO,"VIDIOC_QUERYCTRL id=%x value=%x",ctrl->id,ctrl->type);

			if (ctrl->type)
				return 0;
			else
				return -EINVAL;

		}
		case VIDIOC_G_CTRL:
		{
			struct v4l2_control *ctrl = arg;

			call_i2c_clients(usbvision, VIDIOC_G_CTRL, ctrl);
			PDEBUG(DBG_IO,"VIDIOC_G_CTRL id=%x value=%x",ctrl->id,ctrl->value);
			return 0;
		}
		case VIDIOC_S_CTRL:
		{
			struct v4l2_control *ctrl = arg;

			call_i2c_clients(usbvision, VIDIOC_S_CTRL, ctrl);
			PDEBUG(DBG_IO, "VIDIOC_S_CTRL id=%x value=%x",ctrl->id,ctrl->value);
			return 0;
		}
		case VIDIOC_G_TUNER:
		{
			struct v4l2_tuner *t = arg;

			if (t->index > 0)
				return -EINVAL;

			memset(t,0,sizeof(*t));
			strcpy(t->name, "Radio");
			t->type = V4L2_TUNER_RADIO;

			/* Let clients fill in the remainder of this struct */
			call_i2c_clients(usbvision,VIDIOC_G_TUNER,t);
			PDEBUG(DBG_IO, "VIDIOC_G_TUNER signal=%x, afc=%x",t->signal,t->afc);
			return 0;
		}
		case VIDIOC_S_TUNER:
		{
			struct v4l2_tuner *vt = arg;

			// Only no or one tuner for now
			if (!usbvision->have_tuner || vt->index)
				return -EINVAL;
			/* let clients handle this */
			call_i2c_clients(usbvision,VIDIOC_S_TUNER,vt);

			PDEBUG(DBG_IO, "VIDIOC_S_TUNER");
			return 0;
		}
		case VIDIOC_G_AUDIO:
		{
			struct v4l2_audio *a = arg;

			memset(a,0,sizeof(*a));
			strcpy(a->name,"Radio");
			PDEBUG(DBG_IO, "VIDIOC_G_AUDIO");
			return 0;
		}
		case VIDIOC_S_AUDIO:
		case VIDIOC_S_INPUT:
		case VIDIOC_S_STD:
		return 0;

		case VIDIOC_G_FREQUENCY:
		{
			struct v4l2_frequency *f = arg;

			memset(f,0,sizeof(*f));

			f->type = V4L2_TUNER_RADIO;
			f->frequency = usbvision->freq;
			call_i2c_clients(usbvision, cmd, f);
			PDEBUG(DBG_IO, "VIDIOC_G_FREQUENCY freq=0x%X", (unsigned)f->frequency);

			return 0;
		}
		case VIDIOC_S_FREQUENCY:
		{
			struct v4l2_frequency *f = arg;

			if (f->tuner != 0)
				return -EINVAL;
			usbvision->freq = f->frequency;
			call_i2c_clients(usbvision, cmd, f);
			PDEBUG(DBG_IO, "VIDIOC_S_FREQUENCY freq=0x%X", (unsigned)f->frequency);

			return 0;
		}
		default:
		{
			PDEBUG(DBG_IO, "%s: Unknown command %x", __FUNCTION__, cmd);
			return -ENOIOCTLCMD;
		}
	}
	return 0;
}


static int usbvision_radio_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, usbvision_do_radio_ioctl);
}


/*
 * Here comes the stuff for vbi on usbvision based devices
 *
 */
static int usbvision_vbi_open(struct inode *inode, struct file *file)
{
	/* TODO */
	return -EINVAL;

}

static int usbvision_vbi_close(struct inode *inode, struct file *file)
{
	/* TODO */
	return -EINVAL;
}

static int usbvision_do_vbi_ioctl(struct inode *inode, struct file *file,
				 unsigned int cmd, void *arg)
{
	/* TODO */
	return -EINVAL;
}

static int usbvision_vbi_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, usbvision_do_vbi_ioctl);
}


//
// Video registration stuff
//

// Video template
static struct file_operations usbvision_fops = {
	.owner             = THIS_MODULE,
	.open		= usbvision_v4l2_open,
	.release	= usbvision_v4l2_close,
	.read		= usbvision_v4l2_read,
	.mmap		= usbvision_v4l2_mmap,
	.ioctl		= usbvision_v4l2_ioctl,
	.llseek		= no_llseek,
};
static struct video_device usbvision_video_template = {
	.owner             = THIS_MODULE,
	.type		= VID_TYPE_TUNER | VID_TYPE_CAPTURE,
	.hardware	= VID_HARDWARE_USBVISION,
	.fops		= &usbvision_fops,
	.name           = "usbvision-video",
	.release	= video_device_release,
	.minor		= -1,
};


// Radio template
static struct file_operations usbvision_radio_fops = {
	.owner             = THIS_MODULE,
	.open		= usbvision_radio_open,
	.release	= usbvision_radio_close,
	.ioctl		= usbvision_radio_ioctl,
	.llseek		= no_llseek,
};

static struct video_device usbvision_radio_template=
{
	.owner             = THIS_MODULE,
	.type		= VID_TYPE_TUNER,
	.hardware	= VID_HARDWARE_USBVISION,
	.fops		= &usbvision_radio_fops,
	.release	= video_device_release,
	.name           = "usbvision-radio",
	.minor		= -1,
};


// vbi template
static struct file_operations usbvision_vbi_fops = {
	.owner             = THIS_MODULE,
	.open		= usbvision_vbi_open,
	.release	= usbvision_vbi_close,
	.ioctl		= usbvision_vbi_ioctl,
	.llseek		= no_llseek,
};

static struct video_device usbvision_vbi_template=
{
	.owner             = THIS_MODULE,
	.type		= VID_TYPE_TUNER,
	.hardware	= VID_HARDWARE_USBVISION,
	.fops		= &usbvision_vbi_fops,
	.release	= video_device_release,
	.name           = "usbvision-vbi",
	.minor		= -1,
};


static struct video_device *usbvision_vdev_init(struct usb_usbvision *usbvision,
					struct video_device *vdev_template,
					char *name)
{
	struct usb_device *usb_dev = usbvision->dev;
	struct video_device *vdev;

	if (usb_dev == NULL) {
		err("%s: usbvision->dev is not set", __FUNCTION__);
		return NULL;
	}

	vdev = video_device_alloc();
	if (NULL == vdev) {
		return NULL;
	}
	*vdev = *vdev_template;
//	vdev->minor   = -1;
	vdev->dev     = &usb_dev->dev;
	snprintf(vdev->name, sizeof(vdev->name), "%s", name);
	video_set_drvdata(vdev, usbvision);
	return vdev;
}

// unregister video4linux devices
static void usbvision_unregister_video(struct usb_usbvision *usbvision)
{
	// vbi Device:
	if (usbvision->vbi) {
		PDEBUG(DBG_PROBE, "unregister /dev/vbi%d [v4l2]", usbvision->vbi->minor & 0x1f);
		if (usbvision->vbi->minor != -1) {
			video_unregister_device(usbvision->vbi);
		}
		else {
			video_device_release(usbvision->vbi);
		}
		usbvision->vbi = NULL;
	}

	// Radio Device:
	if (usbvision->rdev) {
		PDEBUG(DBG_PROBE, "unregister /dev/radio%d [v4l2]", usbvision->rdev->minor & 0x1f);
		if (usbvision->rdev->minor != -1) {
			video_unregister_device(usbvision->rdev);
		}
		else {
			video_device_release(usbvision->rdev);
		}
		usbvision->rdev = NULL;
	}

	// Video Device:
	if (usbvision->vdev) {
		PDEBUG(DBG_PROBE, "unregister /dev/video%d [v4l2]", usbvision->vdev->minor & 0x1f);
		if (usbvision->vdev->minor != -1) {
			video_unregister_device(usbvision->vdev);
		}
		else {
			video_device_release(usbvision->vdev);
		}
		usbvision->vdev = NULL;
	}
}

// register video4linux devices
static int __devinit usbvision_register_video(struct usb_usbvision *usbvision)
{
	// Video Device:
	usbvision->vdev = usbvision_vdev_init(usbvision, &usbvision_video_template, "USBVision Video");
	if (usbvision->vdev == NULL) {
		goto err_exit;
	}
	if (video_register_device(usbvision->vdev, VFL_TYPE_GRABBER, video_nr)<0) {
		goto err_exit;
	}
	info("USBVision[%d]: registered USBVision Video device /dev/video%d [v4l2]", usbvision->nr,usbvision->vdev->minor & 0x1f);

	// Radio Device:
	if (usbvision_device_data[usbvision->DevModel].Radio) {
		// usbvision has radio
		usbvision->rdev = usbvision_vdev_init(usbvision, &usbvision_radio_template, "USBVision Radio");
		if (usbvision->rdev == NULL) {
			goto err_exit;
		}
		if (video_register_device(usbvision->rdev, VFL_TYPE_RADIO, radio_nr)<0) {
			goto err_exit;
		}
		info("USBVision[%d]: registered USBVision Radio device /dev/radio%d [v4l2]", usbvision->nr, usbvision->rdev->minor & 0x1f);
	}
	// vbi Device:
	if (usbvision_device_data[usbvision->DevModel].vbi) {
		usbvision->vbi = usbvision_vdev_init(usbvision, &usbvision_vbi_template, "USBVision VBI");
		if (usbvision->vdev == NULL) {
			goto err_exit;
		}
		if (video_register_device(usbvision->vbi, VFL_TYPE_VBI, vbi_nr)<0) {
			goto err_exit;
		}
		info("USBVision[%d]: registered USBVision VBI device /dev/vbi%d [v4l2] (Not Working Yet!)", usbvision->nr,usbvision->vbi->minor & 0x1f);
	}
	// all done
	return 0;

 err_exit:
	err("USBVision[%d]: video_register_device() failed", usbvision->nr);
	usbvision_unregister_video(usbvision);
	return -1;
}

/*
 * usbvision_alloc()
 *
 * This code allocates the struct usb_usbvision. It is filled with default values.
 *
 * Returns NULL on error, a pointer to usb_usbvision else.
 *
 */
static struct usb_usbvision *usbvision_alloc(struct usb_device *dev)
{
	struct usb_usbvision *usbvision;

	if ((usbvision = kzalloc(sizeof(struct usb_usbvision), GFP_KERNEL)) == NULL) {
		goto err_exit;
	}

	usbvision->dev = dev;

	init_MUTEX(&usbvision->lock);	/* to 1 == available */

	// prepare control urb for control messages during interrupts
	usbvision->ctrlUrb = usb_alloc_urb(USBVISION_URB_FRAMES, GFP_KERNEL);
	if (usbvision->ctrlUrb == NULL) {
		goto err_exit;
	}
	init_waitqueue_head(&usbvision->ctrlUrb_wq);
	init_MUTEX(&usbvision->ctrlUrbLock);	/* to 1 == available */

	usbvision_init_powerOffTimer(usbvision);

	return usbvision;

err_exit:
	if (usbvision && usbvision->ctrlUrb) {
		usb_free_urb(usbvision->ctrlUrb);
	}
	if (usbvision) {
		kfree(usbvision);
	}
	return NULL;
}

/*
 * usbvision_release()
 *
 * This code does final release of struct usb_usbvision. This happens
 * after the device is disconnected -and- all clients closed their files.
 *
 */
static void usbvision_release(struct usb_usbvision *usbvision)
{
	PDEBUG(DBG_PROBE, "");

	down(&usbvision->lock);

	usbvision_reset_powerOffTimer(usbvision);

	usbvision->initialized = 0;

	up(&usbvision->lock);

	usbvision_remove_sysfs(usbvision->vdev);
	usbvision_unregister_video(usbvision);

	if (usbvision->ctrlUrb) {
		usb_free_urb(usbvision->ctrlUrb);
	}

	kfree(usbvision);

	PDEBUG(DBG_PROBE, "success");
}


/******************************** usb interface *****************************************/

static void usbvision_configure_video(struct usb_usbvision *usbvision)
{
	int model,i;

	if (usbvision == NULL)
		return;

	model = usbvision->DevModel;
	usbvision->palette = usbvision_v4l2_format[2]; // V4L2_PIX_FMT_RGB24;

	if (usbvision_device_data[usbvision->DevModel].Vin_Reg2 >= 0) {
		usbvision->Vin_Reg2_Preset = usbvision_device_data[usbvision->DevModel].Vin_Reg2 & 0xff;
	} else {
		usbvision->Vin_Reg2_Preset = 0;
	}

	for (i = 0; i < TVNORMS; i++)
		if (usbvision_device_data[model].VideoNorm == tvnorms[i].mode)
			break;
	if (i == TVNORMS)
		i = 0;
	usbvision->tvnorm = &tvnorms[i];        /* set default norm */

	usbvision->video_inputs = usbvision_device_data[model].VideoChannels;
	usbvision->ctl_input = 0;

	/* This should be here to make i2c clients to be able to register */
	usbvision_audio_off(usbvision);	//first switch off audio
	if (!PowerOnAtOpen) {
		usbvision_power_on(usbvision);	//and then power up the noisy tuner
		usbvision_init_i2c(usbvision);
	}
}

/*
 * usbvision_probe()
 *
 * This procedure queries device descriptor and accepts the interface
 * if it looks like USBVISION video device
 *
 */
static int __devinit usbvision_probe(struct usb_interface *intf, const struct usb_device_id *devid)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	__u8 ifnum = intf->altsetting->desc.bInterfaceNumber;
	const struct usb_host_interface *interface;
	struct usb_usbvision *usbvision = NULL;
	const struct usb_endpoint_descriptor *endpoint;
	int model;

	PDEBUG(DBG_PROBE, "VID=%#04x, PID=%#04x, ifnum=%u",
					dev->descriptor.idVendor, dev->descriptor.idProduct, ifnum);
	/* Is it an USBVISION video dev? */
	model = 0;
	for(model = 0; usbvision_device_data[model].idVendor; model++) {
		if (le16_to_cpu(dev->descriptor.idVendor) != usbvision_device_data[model].idVendor) {
			continue;
		}
		if (le16_to_cpu(dev->descriptor.idProduct) != usbvision_device_data[model].idProduct) {
			continue;
		}

		info("%s: %s found", __FUNCTION__, usbvision_device_data[model].ModelString);
		break;
	}

	if (usbvision_device_data[model].idVendor == 0) {
		return -ENODEV; //no matching device
	}
	if (usbvision_device_data[model].Interface >= 0) {
		interface = &dev->actconfig->interface[usbvision_device_data[model].Interface]->altsetting[0];
	}
	else {
		interface = &dev->actconfig->interface[ifnum]->altsetting[0];
	}
	endpoint = &interface->endpoint[1].desc;
	if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC) {
		err("%s: interface %d. has non-ISO endpoint!", __FUNCTION__, ifnum);
		err("%s: Endpoint attribures %d", __FUNCTION__, endpoint->bmAttributes);
		return -ENODEV;
	}
	if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT) {
		err("%s: interface %d. has ISO OUT endpoint!", __FUNCTION__, ifnum);
		return -ENODEV;
	}

	usb_get_dev(dev);

	if ((usbvision = usbvision_alloc(dev)) == NULL) {
		err("%s: couldn't allocate USBVision struct", __FUNCTION__);
		return -ENOMEM;
	}
	if (dev->descriptor.bNumConfigurations > 1) {
		usbvision->bridgeType = BRIDGE_NT1004;
	}
	else if (usbvision_device_data[model].ModelString == "Dazzle Fusion Model DVC-90 Rev 1 (SECAM)") {
		usbvision->bridgeType = BRIDGE_NT1005;
	}
	else {
		usbvision->bridgeType = BRIDGE_NT1003;
	}
	PDEBUG(DBG_PROBE, "bridgeType %d", usbvision->bridgeType);

	down(&usbvision->lock);

	usbvision->nr = usbvision_nr++;

	usbvision->have_tuner = usbvision_device_data[model].Tuner;
	if (usbvision->have_tuner) {
		usbvision->tuner_type = usbvision_device_data[model].TunerType;
	}

	usbvision->tuner_addr = ADDR_UNSET;

	usbvision->DevModel = model;
	usbvision->remove_pending = 0;
	usbvision->iface = ifnum;
	usbvision->ifaceAltInactive = 0;
	usbvision->ifaceAltActive = 1;
	usbvision->video_endp = endpoint->bEndpointAddress;
	usbvision->isocPacketSize = 0;
	usbvision->usb_bandwidth = 0;
	usbvision->user = 0;
	usbvision->streaming = Stream_Off;
	usbvision_register_video(usbvision);
	usbvision_configure_video(usbvision);
	up(&usbvision->lock);


	usb_set_intfdata (intf, usbvision);
	usbvision_create_sysfs(usbvision->vdev);

	PDEBUG(DBG_PROBE, "success");
	return 0;
}


/*
 * usbvision_disconnect()
 *
 * This procedure stops all driver activity, deallocates interface-private
 * structure (pointed by 'ptr') and after that driver should be removable
 * with no ill consequences.
 *
 */
static void __devexit usbvision_disconnect(struct usb_interface *intf)
{
	struct usb_usbvision *usbvision = usb_get_intfdata(intf);

	PDEBUG(DBG_PROBE, "");

	if (usbvision == NULL) {
		err("%s: usb_get_intfdata() failed", __FUNCTION__);
		return;
	}
	usb_set_intfdata (intf, NULL);

	down(&usbvision->lock);

	// At this time we ask to cancel outstanding URBs
	usbvision_stop_isoc(usbvision);

	if (usbvision->power) {
		usbvision_i2c_usb_del_bus(&usbvision->i2c_adap);
		usbvision_power_off(usbvision);
	}
	usbvision->remove_pending = 1;	// Now all ISO data will be ignored

	usb_put_dev(usbvision->dev);
	usbvision->dev = NULL;	// USB device is no more

	up(&usbvision->lock);

	if (usbvision->user) {
		info("%s: In use, disconnect pending", __FUNCTION__);
		wake_up_interruptible(&usbvision->wait_frame);
		wake_up_interruptible(&usbvision->wait_stream);
	}
	else {
		usbvision_release(usbvision);
	}

	PDEBUG(DBG_PROBE, "success");

}

static struct usb_driver usbvision_driver = {
	.name		= "usbvision",
	.id_table	= usbvision_table,
	.probe		= usbvision_probe,
	.disconnect	= usbvision_disconnect
};

/*
 * customdevice_process()
 *
 * This procedure preprocesses CustomDevice parameter if any
 *
 */
void customdevice_process(void)
{
	usbvision_device_data[0]=usbvision_device_data[1];
	usbvision_table[0]=usbvision_table[1];

	if(CustomDevice)
	{
		char *parse=CustomDevice;

		PDEBUG(DBG_PROBE, "CustomDevide=%s", CustomDevice);

		/*format is CustomDevice="0x0573 0x4D31 0 7113 3 PAL 1 1 1 5 -1 -1 -1 -1 -1"
		usbvision_device_data[0].idVendor;
		usbvision_device_data[0].idProduct;
		usbvision_device_data[0].Interface;
		usbvision_device_data[0].Codec;
		usbvision_device_data[0].VideoChannels;
		usbvision_device_data[0].VideoNorm;
		usbvision_device_data[0].AudioChannels;
		usbvision_device_data[0].Radio;
		usbvision_device_data[0].Tuner;
		usbvision_device_data[0].TunerType;
		usbvision_device_data[0].Vin_Reg1;
		usbvision_device_data[0].Vin_Reg2;
		usbvision_device_data[0].X_Offset;
		usbvision_device_data[0].Y_Offset;
		usbvision_device_data[0].Dvi_yuv;
		usbvision_device_data[0].ModelString;
		*/

		rmspace(parse);
		usbvision_device_data[0].ModelString="USBVISION Custom Device";

		parse+=2;
		sscanf(parse,"%x",&usbvision_device_data[0].idVendor);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "idVendor=0x%.4X", usbvision_device_data[0].idVendor);
		parse+=2;
		sscanf(parse,"%x",&usbvision_device_data[0].idProduct);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "idProduct=0x%.4X", usbvision_device_data[0].idProduct);
		sscanf(parse,"%d",&usbvision_device_data[0].Interface);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "Interface=%d", usbvision_device_data[0].Interface);
		sscanf(parse,"%d",&usbvision_device_data[0].Codec);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "Codec=%d", usbvision_device_data[0].Codec);
		sscanf(parse,"%d",&usbvision_device_data[0].VideoChannels);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "VideoChannels=%d", usbvision_device_data[0].VideoChannels);

		switch(*parse)
		{
			case 'P':
				PDEBUG(DBG_PROBE, "VideoNorm=PAL");
				usbvision_device_data[0].VideoNorm=VIDEO_MODE_PAL;
				break;

			case 'S':
				PDEBUG(DBG_PROBE, "VideoNorm=SECAM");
				usbvision_device_data[0].VideoNorm=VIDEO_MODE_SECAM;
				break;

			case 'N':
				PDEBUG(DBG_PROBE, "VideoNorm=NTSC");
				usbvision_device_data[0].VideoNorm=VIDEO_MODE_NTSC;
				break;

			default:
				PDEBUG(DBG_PROBE, "VideoNorm=PAL (by default)");
				usbvision_device_data[0].VideoNorm=VIDEO_MODE_PAL;
				break;
		}
		goto2next(parse);

		sscanf(parse,"%d",&usbvision_device_data[0].AudioChannels);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "AudioChannels=%d", usbvision_device_data[0].AudioChannels);
		sscanf(parse,"%d",&usbvision_device_data[0].Radio);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "Radio=%d", usbvision_device_data[0].Radio);
		sscanf(parse,"%d",&usbvision_device_data[0].Tuner);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "Tuner=%d", usbvision_device_data[0].Tuner);
		sscanf(parse,"%d",&usbvision_device_data[0].TunerType);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "TunerType=%d", usbvision_device_data[0].TunerType);
		sscanf(parse,"%d",&usbvision_device_data[0].Vin_Reg1);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "Vin_Reg1=%d", usbvision_device_data[0].Vin_Reg1);
		sscanf(parse,"%d",&usbvision_device_data[0].Vin_Reg2);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "Vin_Reg2=%d", usbvision_device_data[0].Vin_Reg2);
		sscanf(parse,"%d",&usbvision_device_data[0].X_Offset);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "X_Offset=%d", usbvision_device_data[0].X_Offset);
		sscanf(parse,"%d",&usbvision_device_data[0].Y_Offset);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "Y_Offset=%d", usbvision_device_data[0].Y_Offset);
		sscanf(parse,"%d",&usbvision_device_data[0].Dvi_yuv);
		PDEBUG(DBG_PROBE, "Dvi_yuv=%d", usbvision_device_data[0].Dvi_yuv);

		//add to usbvision_table also
		usbvision_table[0].match_flags=USB_DEVICE_ID_MATCH_DEVICE;
		usbvision_table[0].idVendor=usbvision_device_data[0].idVendor;
		usbvision_table[0].idProduct=usbvision_device_data[0].idProduct;

	}
}



/*
 * usbvision_init()
 *
 * This code is run to initialize the driver.
 *
 */
static int __init usbvision_init(void)
{
	int errCode;

	PDEBUG(DBG_PROBE, "");

	PDEBUG(DBG_IOCTL, "IOCTL   debugging is enabled [video]");
	PDEBUG(DBG_IO,  "IO      debugging is enabled [video]");
	PDEBUG(DBG_PROBE, "PROBE   debugging is enabled [video]");
	PDEBUG(DBG_FUNC, "FUNC    debugging is enabled [video]");

	/* disable planar mode support unless compression enabled */
	if (isocMode != ISOC_MODE_COMPRESS ) {
		// FIXME : not the right way to set supported flag
		usbvision_v4l2_format[6].supported = 0; // V4L2_PIX_FMT_YVU420
		usbvision_v4l2_format[7].supported = 0; // V4L2_PIX_FMT_YUV422P
	}

	customdevice_process();

	errCode = usb_register(&usbvision_driver);

	if (errCode == 0) {
		info(DRIVER_DESC " : " USBVISION_VERSION_STRING);
		PDEBUG(DBG_PROBE, "success");
	}
	return errCode;
}

static void __exit usbvision_exit(void)
{
 PDEBUG(DBG_PROBE, "");

 usb_deregister(&usbvision_driver);
 PDEBUG(DBG_PROBE, "success");
}

module_init(usbvision_init);
module_exit(usbvision_exit);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

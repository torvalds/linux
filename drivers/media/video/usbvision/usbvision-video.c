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
 *     - Add audio on endpoint 3 for nt1004 chip.
 *         Seems impossible, needs a codec interface.  Which one?
 *     - Clean up the driver.
 *     - optimization for performance.
 *     - Add Videotext capability (VBI).  Working on it.....
 *     - Check audio for other devices
 *
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/highmem.h>
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
#include "usbvision-cards.h"

#define DRIVER_AUTHOR "Joerg Heckenbach <joerg@heckenbach-aw.de>,\
 Dwaine Garden <DwaineGarden@rogers.com>"
#define DRIVER_NAME "usbvision"
#define DRIVER_ALIAS "USBVision"
#define DRIVER_DESC "USBVision USB Video Device Driver for Linux"
#define DRIVER_LICENSE "GPL"
#define USBVISION_DRIVER_VERSION_MAJOR 0
#define USBVISION_DRIVER_VERSION_MINOR 9
#define USBVISION_DRIVER_VERSION_PATCHLEVEL 9
#define USBVISION_DRIVER_VERSION KERNEL_VERSION(USBVISION_DRIVER_VERSION_MAJOR,\
USBVISION_DRIVER_VERSION_MINOR,\
USBVISION_DRIVER_VERSION_PATCHLEVEL)
#define USBVISION_VERSION_STRING __stringify(USBVISION_DRIVER_VERSION_MAJOR)\
 "." __stringify(USBVISION_DRIVER_VERSION_MINOR)\
 "." __stringify(USBVISION_DRIVER_VERSION_PATCHLEVEL)

#define	ENABLE_HEXDUMP	0	/* Enable if you need it */


#ifdef USBVISION_DEBUG
	#define PDEBUG(level, fmt, args...) \
		if (video_debug & (level)) \
			info("[%s:%d] " fmt, __PRETTY_FUNCTION__, __LINE__ ,\
				## args)
#else
	#define PDEBUG(level, fmt, args...) do {} while(0)
#endif

#define DBG_IO		1<<1
#define DBG_PROBE	1<<2
#define DBG_MMAP	1<<3

//String operations
#define rmspace(str)	while(*str==' ') str++;
#define goto2next(str)	while(*str!=' ') str++; while(*str==' ') str++;


/* sequential number of usbvision device */
static int usbvision_nr = 0;

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

/* Function prototypes */
static void usbvision_release(struct usb_usbvision *usbvision);

/* Default initalization of device driver parameters */
/* Set the default format for ISOC endpoint */
static int isocMode = ISOC_MODE_COMPRESS;
/* Set the default Debug Mode of the device driver */
static int video_debug = 0;
/* Set the default device to power on at startup */
static int PowerOnAtOpen = 1;
/* Sequential Number of Video Device */
static int video_nr = -1;
/* Sequential Number of Radio Device */
static int radio_nr = -1;
/* Sequential Number of VBI Device */
static int vbi_nr = -1;

/* Grab parameters for the device driver */

/* Showing parameters under SYSFS */
module_param(isocMode, int, 0444);
module_param(video_debug, int, 0444);
module_param(PowerOnAtOpen, int, 0444);
module_param(video_nr, int, 0444);
module_param(radio_nr, int, 0444);
module_param(vbi_nr, int, 0444);

MODULE_PARM_DESC(isocMode, " Set the default format for ISOC endpoint.  Default: 0x60 (Compression On)");
MODULE_PARM_DESC(video_debug, " Set the default Debug Mode of the device driver.  Default: 0 (Off)");
MODULE_PARM_DESC(PowerOnAtOpen, " Set the default device to power on when device is opened.  Default: 1 (On)");
MODULE_PARM_DESC(video_nr, "Set video device number (/dev/videoX).  Default: -1 (autodetect)");
MODULE_PARM_DESC(radio_nr, "Set radio device number (/dev/radioX).  Default: -1 (autodetect)");
MODULE_PARM_DESC(vbi_nr, "Set vbi device number (/dev/vbiX).  Default: -1 (autodetect)");


// Misc stuff
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);
MODULE_VERSION(USBVISION_VERSION_STRING);
MODULE_ALIAS(DRIVER_ALIAS);


/*****************************************************************************/
/* SYSFS Code - Copied from the stv680.c usb module.			     */
/* Device information is located at /sys/class/video4linux/video0            */
/* Device parameters information is located at /sys/module/usbvision         */
/* Device USB Information is located at                                      */
/*   /sys/bus/usb/drivers/USBVision Video Grabber                            */
/*****************************************************************************/


#define YES_NO(x) ((x) ? "Yes" : "No")

static inline struct usb_usbvision *cd_to_usbvision(struct class_device *cd)
{
	struct video_device *vdev =
		container_of(cd, struct video_device, class_dev);
	return video_get_drvdata(vdev);
}

static ssize_t show_version(struct class_device *cd, char *buf)
{
	return sprintf(buf, "%s\n", USBVISION_VERSION_STRING);
}
static CLASS_DEVICE_ATTR(version, S_IRUGO, show_version, NULL);

static ssize_t show_model(struct class_device *cd, char *buf)
{
	struct video_device *vdev =
		container_of(cd, struct video_device, class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%s\n",
		       usbvision_device_data[usbvision->DevModel].ModelString);
}
static CLASS_DEVICE_ATTR(model, S_IRUGO, show_model, NULL);

static ssize_t show_hue(struct class_device *cd, char *buf)
{
	struct video_device *vdev =
		container_of(cd, struct video_device, class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	struct v4l2_control ctrl;
	ctrl.id = V4L2_CID_HUE;
	ctrl.value = 0;
	if(usbvision->user)
		call_i2c_clients(usbvision, VIDIOC_G_CTRL, &ctrl);
	return sprintf(buf, "%d\n", ctrl.value);
}
static CLASS_DEVICE_ATTR(hue, S_IRUGO, show_hue, NULL);

static ssize_t show_contrast(struct class_device *cd, char *buf)
{
	struct video_device *vdev =
		container_of(cd, struct video_device, class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	struct v4l2_control ctrl;
	ctrl.id = V4L2_CID_CONTRAST;
	ctrl.value = 0;
	if(usbvision->user)
		call_i2c_clients(usbvision, VIDIOC_G_CTRL, &ctrl);
	return sprintf(buf, "%d\n", ctrl.value);
}
static CLASS_DEVICE_ATTR(contrast, S_IRUGO, show_contrast, NULL);

static ssize_t show_brightness(struct class_device *cd, char *buf)
{
	struct video_device *vdev =
		container_of(cd, struct video_device, class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	struct v4l2_control ctrl;
	ctrl.id = V4L2_CID_BRIGHTNESS;
	ctrl.value = 0;
	if(usbvision->user)
		call_i2c_clients(usbvision, VIDIOC_G_CTRL, &ctrl);
	return sprintf(buf, "%d\n", ctrl.value);
}
static CLASS_DEVICE_ATTR(brightness, S_IRUGO, show_brightness, NULL);

static ssize_t show_saturation(struct class_device *cd, char *buf)
{
	struct video_device *vdev =
		container_of(cd, struct video_device, class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	struct v4l2_control ctrl;
	ctrl.id = V4L2_CID_SATURATION;
	ctrl.value = 0;
	if(usbvision->user)
		call_i2c_clients(usbvision, VIDIOC_G_CTRL, &ctrl);
	return sprintf(buf, "%d\n", ctrl.value);
}
static CLASS_DEVICE_ATTR(saturation, S_IRUGO, show_saturation, NULL);

static ssize_t show_streaming(struct class_device *cd, char *buf)
{
	struct video_device *vdev =
		container_of(cd, struct video_device, class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%s\n",
		       YES_NO(usbvision->streaming==Stream_On?1:0));
}
static CLASS_DEVICE_ATTR(streaming, S_IRUGO, show_streaming, NULL);

static ssize_t show_compression(struct class_device *cd, char *buf)
{
	struct video_device *vdev =
		container_of(cd, struct video_device, class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%s\n",
		       YES_NO(usbvision->isocMode==ISOC_MODE_COMPRESS));
}
static CLASS_DEVICE_ATTR(compression, S_IRUGO, show_compression, NULL);

static ssize_t show_device_bridge(struct class_device *cd, char *buf)
{
	struct video_device *vdev =
		container_of(cd, struct video_device, class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%d\n", usbvision->bridgeType);
}
static CLASS_DEVICE_ATTR(bridge, S_IRUGO, show_device_bridge, NULL);

static void usbvision_create_sysfs(struct video_device *vdev)
{
	int res;
	if (!vdev)
		return;
	do {
		res=class_device_create_file(&vdev->class_dev,
					     &class_device_attr_version);
		if (res<0)
			break;
		res=class_device_create_file(&vdev->class_dev,
					     &class_device_attr_model);
		if (res<0)
			break;
		res=class_device_create_file(&vdev->class_dev,
					     &class_device_attr_hue);
		if (res<0)
			break;
		res=class_device_create_file(&vdev->class_dev,
					     &class_device_attr_contrast);
		if (res<0)
			break;
		res=class_device_create_file(&vdev->class_dev,
					     &class_device_attr_brightness);
		if (res<0)
			break;
		res=class_device_create_file(&vdev->class_dev,
					     &class_device_attr_saturation);
		if (res<0)
			break;
		res=class_device_create_file(&vdev->class_dev,
					     &class_device_attr_streaming);
		if (res<0)
			break;
		res=class_device_create_file(&vdev->class_dev,
					     &class_device_attr_compression);
		if (res<0)
			break;
		res=class_device_create_file(&vdev->class_dev,
					     &class_device_attr_bridge);
		if (res>=0)
			return;
	} while (0);

	err("%s error: %d\n", __FUNCTION__, res);
}

static void usbvision_remove_sysfs(struct video_device *vdev)
{
	if (vdev) {
		class_device_remove_file(&vdev->class_dev,
					 &class_device_attr_version);
		class_device_remove_file(&vdev->class_dev,
					 &class_device_attr_model);
		class_device_remove_file(&vdev->class_dev,
					 &class_device_attr_hue);
		class_device_remove_file(&vdev->class_dev,
					 &class_device_attr_contrast);
		class_device_remove_file(&vdev->class_dev,
					 &class_device_attr_brightness);
		class_device_remove_file(&vdev->class_dev,
					 &class_device_attr_saturation);
		class_device_remove_file(&vdev->class_dev,
					 &class_device_attr_streaming);
		class_device_remove_file(&vdev->class_dev,
					 &class_device_attr_compression);
		class_device_remove_file(&vdev->class_dev,
					 &class_device_attr_bridge);
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
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	int errCode = 0;

	PDEBUG(DBG_IO, "open");


	usbvision_reset_powerOffTimer(usbvision);

	if (usbvision->user)
		errCode = -EBUSY;
	else {
		/* Allocate memory for the scratch ring buffer */
		errCode = usbvision_scratch_alloc(usbvision);
		if (isocMode==ISOC_MODE_COMPRESS) {
			/* Allocate intermediate decompression buffers
			   only if needed */
			errCode = usbvision_decompress_alloc(usbvision);
		}
		if (errCode) {
			/* Deallocate all buffers if trouble */
			usbvision_scratch_free(usbvision);
			usbvision_decompress_free(usbvision);
		}
	}

	/* If so far no errors then we shall start the camera */
	if (!errCode) {
		down(&usbvision->lock);
		if (usbvision->power == 0) {
			usbvision_power_on(usbvision);
			usbvision_i2c_register(usbvision);
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
			/* device must be initialized before isoc transfer */
			usbvision_muxsel(usbvision,0);
			usbvision->user++;
		} else {
			if (PowerOnAtOpen) {
				usbvision_i2c_unregister(usbvision);
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
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);

	PDEBUG(DBG_IO, "close");
	down(&usbvision->lock);

	usbvision_audio_off(usbvision);
	usbvision_restart_isoc(usbvision);
	usbvision_stop_isoc(usbvision);

	usbvision_decompress_free(usbvision);
	usbvision_frames_free(usbvision);
	usbvision_empty_framequeues(usbvision);
	usbvision_scratch_free(usbvision);

	usbvision->user--;

	if (PowerOnAtOpen) {
		/* power off in a little while
		   to avoid off/on every close/open short sequences */
		usbvision_set_powerOffTimer(usbvision);
		usbvision->initialized = 0;
	}

	up(&usbvision->lock);

	if (usbvision->remove_pending) {
		printk(KERN_INFO "%s: Final disconnect\n", __FUNCTION__);
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
#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vidioc_g_register (struct file *file, void *priv,
				struct v4l2_register *reg)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	int errCode;

	if (!v4l2_chip_match_host(reg->match_type, reg->match_chip))
		return -EINVAL;
	/* NT100x has a 8-bit register space */
	errCode = usbvision_read_reg(usbvision, reg->reg&0xff);
	if (errCode < 0) {
		err("%s: VIDIOC_DBG_G_REGISTER failed: error %d",
		    __FUNCTION__, errCode);
		return errCode;
	}
	reg->val = errCode;
	return 0;
}

static int vidioc_s_register (struct file *file, void *priv,
				struct v4l2_register *reg)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	int errCode;

	if (!v4l2_chip_match_host(reg->match_type, reg->match_chip))
		return -EINVAL;
	/* NT100x has a 8-bit register space */
	errCode = usbvision_write_reg(usbvision, reg->reg&0xff, reg->val);
	if (errCode < 0) {
		err("%s: VIDIOC_DBG_S_REGISTER failed: error %d",
		    __FUNCTION__, errCode);
		return errCode;
	}
	return 0;
}
#endif

static int vidioc_querycap (struct file *file, void  *priv,
					struct v4l2_capability *vc)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);

	strlcpy(vc->driver, "USBVision", sizeof(vc->driver));
	strlcpy(vc->card,
		usbvision_device_data[usbvision->DevModel].ModelString,
		sizeof(vc->card));
	strlcpy(vc->bus_info, usbvision->dev->dev.bus_id,
		sizeof(vc->bus_info));
	vc->version = USBVISION_DRIVER_VERSION;
	vc->capabilities = V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_AUDIO |
		V4L2_CAP_READWRITE |
		V4L2_CAP_STREAMING |
		(usbvision->have_tuner ? V4L2_CAP_TUNER : 0);
	return 0;
}

static int vidioc_enum_input (struct file *file, void *priv,
				struct v4l2_input *vi)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	int chan;

	if ((vi->index >= usbvision->video_inputs) || (vi->index < 0) )
		return -EINVAL;
	if (usbvision->have_tuner) {
		chan = vi->index;
	} else {
		chan = vi->index + 1; /*skip Television string*/
	}
	/* Determine the requested input characteristics
	   specific for each usbvision card model */
	switch(chan) {
	case 0:
		if (usbvision_device_data[usbvision->DevModel].VideoChannels == 4) {
			strcpy(vi->name, "White Video Input");
		} else {
			strcpy(vi->name, "Television");
			vi->type = V4L2_INPUT_TYPE_TUNER;
			vi->audioset = 1;
			vi->tuner = chan;
			vi->std = USBVISION_NORMS;
		}
		break;
	case 1:
		vi->type = V4L2_INPUT_TYPE_CAMERA;
		if (usbvision_device_data[usbvision->DevModel].VideoChannels == 4) {
			strcpy(vi->name, "Green Video Input");
		} else {
			strcpy(vi->name, "Composite Video Input");
		}
		vi->std = V4L2_STD_PAL;
		break;
	case 2:
		vi->type = V4L2_INPUT_TYPE_CAMERA;
		if (usbvision_device_data[usbvision->DevModel].VideoChannels == 4) {
			strcpy(vi->name, "Yellow Video Input");
		} else {
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
	return 0;
}

static int vidioc_g_input (struct file *file, void *priv, unsigned int *input)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);

	*input = usbvision->ctl_input;
	return 0;
}

static int vidioc_s_input (struct file *file, void *priv, unsigned int input)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);

	if ((input >= usbvision->video_inputs) || (input < 0) )
		return -EINVAL;

	down(&usbvision->lock);
	usbvision_muxsel(usbvision, input);
	usbvision_set_input(usbvision);
	usbvision_set_output(usbvision,
			     usbvision->curwidth,
			     usbvision->curheight);
	up(&usbvision->lock);
	return 0;
}

static int vidioc_s_std (struct file *file, void *priv, v4l2_std_id *id)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	usbvision->tvnormId=*id;

	down(&usbvision->lock);
	call_i2c_clients(usbvision, VIDIOC_S_STD,
			 &usbvision->tvnormId);
	up(&usbvision->lock);
	/* propagate the change to the decoder */
	usbvision_muxsel(usbvision, usbvision->ctl_input);

	return 0;
}

static int vidioc_g_tuner (struct file *file, void *priv,
				struct v4l2_tuner *vt)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);

	if (!usbvision->have_tuner || vt->index)	// Only tuner 0
		return -EINVAL;
	if(usbvision->radio) {
		strcpy(vt->name, "Radio");
		vt->type = V4L2_TUNER_RADIO;
	} else {
		strcpy(vt->name, "Television");
	}
	/* Let clients fill in the remainder of this struct */
	call_i2c_clients(usbvision,VIDIOC_G_TUNER,vt);

	return 0;
}

static int vidioc_s_tuner (struct file *file, void *priv,
				struct v4l2_tuner *vt)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);

	// Only no or one tuner for now
	if (!usbvision->have_tuner || vt->index)
		return -EINVAL;
	/* let clients handle this */
	call_i2c_clients(usbvision,VIDIOC_S_TUNER,vt);

	return 0;
}

static int vidioc_g_frequency (struct file *file, void *priv,
				struct v4l2_frequency *freq)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);

	freq->tuner = 0; // Only one tuner
	if(usbvision->radio) {
		freq->type = V4L2_TUNER_RADIO;
	} else {
		freq->type = V4L2_TUNER_ANALOG_TV;
	}
	freq->frequency = usbvision->freq;

	return 0;
}

static int vidioc_s_frequency (struct file *file, void *priv,
				struct v4l2_frequency *freq)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);

	// Only no or one tuner for now
	if (!usbvision->have_tuner || freq->tuner)
		return -EINVAL;

	usbvision->freq = freq->frequency;
	call_i2c_clients(usbvision, VIDIOC_S_FREQUENCY, freq);

	return 0;
}

static int vidioc_g_audio (struct file *file, void *priv, struct v4l2_audio *a)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);

	memset(a,0,sizeof(*a));
	if(usbvision->radio) {
		strcpy(a->name,"Radio");
	} else {
		strcpy(a->name, "TV");
	}

	return 0;
}

static int vidioc_s_audio (struct file *file, void *fh,
			  struct v4l2_audio *a)
{
	if(a->index) {
		return -EINVAL;
	}

	return 0;
}

static int vidioc_queryctrl (struct file *file, void *priv,
			    struct v4l2_queryctrl *ctrl)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	int id=ctrl->id;

	memset(ctrl,0,sizeof(*ctrl));
	ctrl->id=id;

	call_i2c_clients(usbvision, VIDIOC_QUERYCTRL, ctrl);

	if (!ctrl->type)
		return -EINVAL;

	return 0;
}

static int vidioc_g_ctrl (struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	call_i2c_clients(usbvision, VIDIOC_G_CTRL, ctrl);

	return 0;
}

static int vidioc_s_ctrl (struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	call_i2c_clients(usbvision, VIDIOC_S_CTRL, ctrl);

	return 0;
}

static int vidioc_reqbufs (struct file *file,
			   void *priv, struct v4l2_requestbuffers *vr)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	int ret;

	RESTRICT_TO_RANGE(vr->count,1,USBVISION_NUMFRAMES);

	/* Check input validity:
	   the user must do a VIDEO CAPTURE and MMAP method. */
	if((vr->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) ||
	   (vr->memory != V4L2_MEMORY_MMAP))
		return -EINVAL;

	if(usbvision->streaming == Stream_On) {
		if ((ret = usbvision_stream_interrupt(usbvision)))
			return ret;
	}

	usbvision_frames_free(usbvision);
	usbvision_empty_framequeues(usbvision);
	vr->count = usbvision_frames_alloc(usbvision,vr->count);

	usbvision->curFrame = NULL;

	return 0;
}

static int vidioc_querybuf (struct file *file,
			    void *priv, struct v4l2_buffer *vb)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	struct usbvision_frame *frame;

	/* FIXME : must control
	   that buffers are mapped (VIDIOC_REQBUFS has been called) */
	if(vb->type != V4L2_CAP_VIDEO_CAPTURE) {
		return -EINVAL;
	}
	if(vb->index>=usbvision->num_frames)  {
		return -EINVAL;
	}
	/* Updating the corresponding frame state */
	vb->flags = 0;
	frame = &usbvision->frame[vb->index];
	if(frame->grabstate >= FrameState_Ready)
		vb->flags |= V4L2_BUF_FLAG_QUEUED;
	if(frame->grabstate >= FrameState_Done)
		vb->flags |= V4L2_BUF_FLAG_DONE;
	if(frame->grabstate == FrameState_Unused)
		vb->flags |= V4L2_BUF_FLAG_MAPPED;
	vb->memory = V4L2_MEMORY_MMAP;

	vb->m.offset = vb->index*PAGE_ALIGN(usbvision->max_frame_size);

	vb->memory = V4L2_MEMORY_MMAP;
	vb->field = V4L2_FIELD_NONE;
	vb->length = usbvision->curwidth*
		usbvision->curheight*
		usbvision->palette.bytes_per_pixel;
	vb->timestamp = usbvision->frame[vb->index].timestamp;
	vb->sequence = usbvision->frame[vb->index].sequence;
	return 0;
}

static int vidioc_qbuf (struct file *file, void *priv, struct v4l2_buffer *vb)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	struct usbvision_frame *frame;
	unsigned long lock_flags;

	/* FIXME : works only on VIDEO_CAPTURE MODE, MMAP. */
	if(vb->type != V4L2_CAP_VIDEO_CAPTURE) {
		return -EINVAL;
	}
	if(vb->index>=usbvision->num_frames)  {
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

	return 0;
}

static int vidioc_dqbuf (struct file *file, void *priv, struct v4l2_buffer *vb)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
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
	vb->flags = V4L2_BUF_FLAG_MAPPED |
		V4L2_BUF_FLAG_QUEUED |
		V4L2_BUF_FLAG_DONE;
	vb->index = f->index;
	vb->sequence = f->sequence;
	vb->timestamp = f->timestamp;
	vb->field = V4L2_FIELD_NONE;
	vb->bytesused = f->scanlength;

	return 0;
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	int b=V4L2_BUF_TYPE_VIDEO_CAPTURE;

	usbvision->streaming = Stream_On;
	call_i2c_clients(usbvision,VIDIOC_STREAMON , &b);

	return 0;
}

static int vidioc_streamoff(struct file *file,
			    void *priv, enum v4l2_buf_type type)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	int b=V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if(usbvision->streaming == Stream_On) {
		usbvision_stream_interrupt(usbvision);
		/* Stop all video streamings */
		call_i2c_clients(usbvision,VIDIOC_STREAMOFF , &b);
	}
	usbvision_empty_framequeues(usbvision);

	return 0;
}

static int vidioc_enum_fmt_cap (struct file *file, void  *priv,
					struct v4l2_fmtdesc *vfd)
{
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

static int vidioc_g_fmt_cap (struct file *file, void *priv,
					struct v4l2_format *vf)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	vf->fmt.pix.width = usbvision->curwidth;
	vf->fmt.pix.height = usbvision->curheight;
	vf->fmt.pix.pixelformat = usbvision->palette.format;
	vf->fmt.pix.bytesperline =
		usbvision->curwidth*usbvision->palette.bytes_per_pixel;
	vf->fmt.pix.sizeimage = vf->fmt.pix.bytesperline*usbvision->curheight;
	vf->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	vf->fmt.pix.field = V4L2_FIELD_NONE; /* Always progressive image */

	return 0;
}

static int vidioc_try_fmt_cap (struct file *file, void *priv,
			       struct v4l2_format *vf)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	int formatIdx;

	/* Find requested format in available ones */
	for(formatIdx=0;formatIdx<USBVISION_SUPPORTED_PALETTES;formatIdx++) {
		if(vf->fmt.pix.pixelformat ==
		   usbvision_v4l2_format[formatIdx].format) {
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

	vf->fmt.pix.bytesperline = vf->fmt.pix.width*
		usbvision->palette.bytes_per_pixel;
	vf->fmt.pix.sizeimage = vf->fmt.pix.bytesperline*vf->fmt.pix.height;

	return 0;
}

static int vidioc_s_fmt_cap(struct file *file, void *priv,
			       struct v4l2_format *vf)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	int ret;

	if( 0 != (ret=vidioc_try_fmt_cap (file, priv, vf)) ) {
		return ret;
	}

	/* stop io in case it is already in progress */
	if(usbvision->streaming == Stream_On) {
		if ((ret = usbvision_stream_interrupt(usbvision)))
			return ret;
	}
	usbvision_frames_free(usbvision);
	usbvision_empty_framequeues(usbvision);

	usbvision->curFrame = NULL;

	/* by now we are committed to the new data... */
	down(&usbvision->lock);
	usbvision_set_output(usbvision, vf->fmt.pix.width, vf->fmt.pix.height);
	up(&usbvision->lock);

	return 0;
}

static ssize_t usbvision_v4l2_read(struct file *file, char __user *buf,
		      size_t count, loff_t *ppos)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	int noblock = file->f_flags & O_NONBLOCK;
	unsigned long lock_flags;

	int ret,i;
	struct usbvision_frame *frame;

	PDEBUG(DBG_IO, "%s: %ld bytes, noblock=%d", __FUNCTION__,
	       (unsigned long)count, noblock);

	if (!USBVISION_IS_OPERATIONAL(usbvision) || (buf == NULL))
		return -EFAULT;

	/* This entry point is compatible with the mmap routines
	   so that a user can do either VIDIOC_QBUF/VIDIOC_DQBUF
	   to get frames or call read on the device. */
	if(!usbvision->num_frames) {
		/* First, allocate some frames to work with
		   if this has not been done with VIDIOC_REQBUF */
		usbvision_frames_free(usbvision);
		usbvision_empty_framequeues(usbvision);
		usbvision_frames_alloc(usbvision,USBVISION_NUMFRAMES);
	}

	if(usbvision->streaming != Stream_On) {
		/* no stream is running, make it running ! */
		usbvision->streaming = Stream_On;
		call_i2c_clients(usbvision,VIDIOC_STREAMON , NULL);
	}

	/* Then, enqueue as many frames as possible
	   (like a user of VIDIOC_QBUF would do) */
	for(i=0;i<usbvision->num_frames;i++) {
		frame = &usbvision->frame[i];
		if(frame->grabstate == FrameState_Unused) {
			/* Mark it as ready and enqueue frame */
			frame->grabstate = FrameState_Ready;
			frame->scanstate = ScanState_Scanning;
			/* Accumulated in usbvision_parse_data() */
			frame->scanlength = 0;

			/* set v4l2_format index */
			frame->v4l2_format = usbvision->palette;

			spin_lock_irqsave(&usbvision->queue_lock, lock_flags);
			list_add_tail(&frame->frame, &usbvision->inqueue);
			spin_unlock_irqrestore(&usbvision->queue_lock,
					       lock_flags);
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

	PDEBUG(DBG_IO, "%s: frmx=%d, bytes_read=%ld, scanlength=%ld",
	       __FUNCTION__,
	       frame->index, frame->bytes_read, frame->scanlength);

	/* copy bytes to user space; we allow for partials reads */
	if ((count + frame->bytes_read) > (unsigned long)frame->scanlength)
		count = frame->scanlength - frame->bytes_read;

	if (copy_to_user(buf, frame->data + frame->bytes_read, count)) {
		return -EFAULT;
	}

	frame->bytes_read += count;
	PDEBUG(DBG_IO, "%s: {copy} count used=%ld, new bytes_read=%ld",
	       __FUNCTION__,
	       (unsigned long)count, frame->bytes_read);

	/* For now, forget the frame if it has not been read in one shot. */
/* 	if (frame->bytes_read >= frame->scanlength) {// All data has been read */
		frame->bytes_read = 0;

		/* Mark it as available to be used again. */
		frame->grabstate = FrameState_Unused;
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
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);

	PDEBUG(DBG_MMAP, "mmap");

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

	for (i = 0; i < usbvision->num_frames; i++) {
		if (((PAGE_ALIGN(usbvision->max_frame_size)*i) >> PAGE_SHIFT) ==
		    vma->vm_pgoff)
			break;
	}
	if (i == usbvision->num_frames) {
		PDEBUG(DBG_MMAP,
		       "mmap: user supplied mapping address is out of range");
		up(&usbvision->lock);
		return -EINVAL;
	}

	/* VM_IO is eventually going to replace PageReserved altogether */
	vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_RESERVED;	/* avoid to swap out this VMA */

	pos = usbvision->frame[i].data;
	while (size > 0) {

		if (vm_insert_page(vma, start, vmalloc_to_page(pos))) {
			PDEBUG(DBG_MMAP, "mmap: vm_insert_page failed");
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
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
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
				usbvision_i2c_register(usbvision);
			}
		}

		/* Alternate interface 1 is is the biggest frame size */
		errCode = usbvision_set_alternate(usbvision);
		if (errCode < 0) {
			usbvision->last_error = errCode;
			return -EBUSY;
		}

		// If so far no errors then we shall start the radio
		usbvision->radio = 1;
		call_i2c_clients(usbvision,AUDC_SET_RADIO,&usbvision->tuner_type);
		usbvision_set_audio(usbvision, USBVISION_AUDIO_RADIO);
		usbvision->user++;
	}

	if (errCode) {
		if (PowerOnAtOpen) {
			usbvision_i2c_unregister(usbvision);
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
	struct usb_usbvision *usbvision =
		(struct usb_usbvision *) video_get_drvdata(dev);
	int errCode = 0;

	PDEBUG(DBG_IO, "");

	down(&usbvision->lock);

	/* Set packet size to 0 */
	usbvision->ifaceAlt=0;
	errCode = usb_set_interface(usbvision->dev, usbvision->iface,
				    usbvision->ifaceAlt);

	usbvision_audio_off(usbvision);
	usbvision->radio=0;
	usbvision->user--;

	if (PowerOnAtOpen) {
		usbvision_set_powerOffTimer(usbvision);
		usbvision->initialized = 0;
	}

	up(&usbvision->lock);

	if (usbvision->remove_pending) {
		printk(KERN_INFO "%s: Final disconnect\n", __FUNCTION__);
		usbvision_release(usbvision);
	}


	PDEBUG(DBG_IO, "success");

	return errCode;
}

/*
 * Here comes the stuff for vbi on usbvision based devices
 *
 */
static int usbvision_vbi_open(struct inode *inode, struct file *file)
{
	/* TODO */
	return -ENODEV;

}

static int usbvision_vbi_close(struct inode *inode, struct file *file)
{
	/* TODO */
	return -ENODEV;
}

static int usbvision_do_vbi_ioctl(struct inode *inode, struct file *file,
				 unsigned int cmd, void *arg)
{
	/* TODO */
	return -ENOIOCTLCMD;
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
static const struct file_operations usbvision_fops = {
	.owner             = THIS_MODULE,
	.open		= usbvision_v4l2_open,
	.release	= usbvision_v4l2_close,
	.read		= usbvision_v4l2_read,
	.mmap		= usbvision_v4l2_mmap,
	.ioctl		= video_ioctl2,
	.llseek		= no_llseek,
/* 	.poll          = video_poll, */
	.compat_ioctl  = v4l_compat_ioctl32,
};
static struct video_device usbvision_video_template = {
	.owner             = THIS_MODULE,
	.type		= VID_TYPE_TUNER | VID_TYPE_CAPTURE,
	.hardware	= VID_HARDWARE_USBVISION,
	.fops		= &usbvision_fops,
	.name           = "usbvision-video",
	.release	= video_device_release,
	.minor		= -1,
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_cap  = vidioc_enum_fmt_cap,
	.vidioc_g_fmt_cap     = vidioc_g_fmt_cap,
	.vidioc_try_fmt_cap   = vidioc_try_fmt_cap,
	.vidioc_s_fmt_cap     = vidioc_s_fmt_cap,
	.vidioc_reqbufs       = vidioc_reqbufs,
	.vidioc_querybuf      = vidioc_querybuf,
	.vidioc_qbuf          = vidioc_qbuf,
	.vidioc_dqbuf         = vidioc_dqbuf,
	.vidioc_s_std         = vidioc_s_std,
	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
	.vidioc_queryctrl     = vidioc_queryctrl,
	.vidioc_g_audio       = vidioc_g_audio,
	.vidioc_s_audio       = vidioc_s_audio,
	.vidioc_g_ctrl        = vidioc_g_ctrl,
	.vidioc_s_ctrl        = vidioc_s_ctrl,
	.vidioc_streamon      = vidioc_streamon,
	.vidioc_streamoff     = vidioc_streamoff,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
/*  	.vidiocgmbuf          = vidiocgmbuf, */
#endif
	.vidioc_g_tuner       = vidioc_g_tuner,
	.vidioc_s_tuner       = vidioc_s_tuner,
	.vidioc_g_frequency   = vidioc_g_frequency,
	.vidioc_s_frequency   = vidioc_s_frequency,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register    = vidioc_g_register,
	.vidioc_s_register    = vidioc_s_register,
#endif
	.tvnorms              = USBVISION_NORMS,
	.current_norm         = V4L2_STD_PAL
};


// Radio template
static const struct file_operations usbvision_radio_fops = {
	.owner             = THIS_MODULE,
	.open		= usbvision_radio_open,
	.release	= usbvision_radio_close,
	.ioctl		= video_ioctl2,
	.llseek		= no_llseek,
	.compat_ioctl  = v4l_compat_ioctl32,
};

static struct video_device usbvision_radio_template=
{
	.owner             = THIS_MODULE,
	.type		= VID_TYPE_TUNER,
	.hardware	= VID_HARDWARE_USBVISION,
	.fops		= &usbvision_radio_fops,
	.name           = "usbvision-radio",
	.release	= video_device_release,
	.minor		= -1,
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
	.vidioc_queryctrl     = vidioc_queryctrl,
	.vidioc_g_audio       = vidioc_g_audio,
	.vidioc_s_audio       = vidioc_s_audio,
	.vidioc_g_ctrl        = vidioc_g_ctrl,
	.vidioc_s_ctrl        = vidioc_s_ctrl,
	.vidioc_g_tuner       = vidioc_g_tuner,
	.vidioc_s_tuner       = vidioc_s_tuner,
	.vidioc_g_frequency   = vidioc_g_frequency,
	.vidioc_s_frequency   = vidioc_s_frequency,

	.tvnorms              = USBVISION_NORMS,
	.current_norm         = V4L2_STD_PAL
};

// vbi template
static const struct file_operations usbvision_vbi_fops = {
	.owner             = THIS_MODULE,
	.open		= usbvision_vbi_open,
	.release	= usbvision_vbi_close,
	.ioctl		= usbvision_vbi_ioctl,
	.llseek		= no_llseek,
	.compat_ioctl  = v4l_compat_ioctl32,
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
		PDEBUG(DBG_PROBE, "unregister /dev/vbi%d [v4l2]",
		       usbvision->vbi->minor & 0x1f);
		if (usbvision->vbi->minor != -1) {
			video_unregister_device(usbvision->vbi);
		} else {
			video_device_release(usbvision->vbi);
		}
		usbvision->vbi = NULL;
	}

	// Radio Device:
	if (usbvision->rdev) {
		PDEBUG(DBG_PROBE, "unregister /dev/radio%d [v4l2]",
		       usbvision->rdev->minor & 0x1f);
		if (usbvision->rdev->minor != -1) {
			video_unregister_device(usbvision->rdev);
		} else {
			video_device_release(usbvision->rdev);
		}
		usbvision->rdev = NULL;
	}

	// Video Device:
	if (usbvision->vdev) {
		PDEBUG(DBG_PROBE, "unregister /dev/video%d [v4l2]",
		       usbvision->vdev->minor & 0x1f);
		if (usbvision->vdev->minor != -1) {
			video_unregister_device(usbvision->vdev);
		} else {
			video_device_release(usbvision->vdev);
		}
		usbvision->vdev = NULL;
	}
}

// register video4linux devices
static int __devinit usbvision_register_video(struct usb_usbvision *usbvision)
{
	// Video Device:
	usbvision->vdev = usbvision_vdev_init(usbvision,
					      &usbvision_video_template,
					      "USBVision Video");
	if (usbvision->vdev == NULL) {
		goto err_exit;
	}
	if (video_register_device(usbvision->vdev,
				  VFL_TYPE_GRABBER,
				  video_nr)<0) {
		goto err_exit;
	}
	printk(KERN_INFO "USBVision[%d]: registered USBVision Video device /dev/video%d [v4l2]\n",
	       usbvision->nr,usbvision->vdev->minor & 0x1f);

	// Radio Device:
	if (usbvision_device_data[usbvision->DevModel].Radio) {
		// usbvision has radio
		usbvision->rdev = usbvision_vdev_init(usbvision,
						      &usbvision_radio_template,
						      "USBVision Radio");
		if (usbvision->rdev == NULL) {
			goto err_exit;
		}
		if (video_register_device(usbvision->rdev,
					  VFL_TYPE_RADIO,
					  radio_nr)<0) {
			goto err_exit;
		}
		printk(KERN_INFO "USBVision[%d]: registered USBVision Radio device /dev/radio%d [v4l2]\n",
		       usbvision->nr, usbvision->rdev->minor & 0x1f);
	}
	// vbi Device:
	if (usbvision_device_data[usbvision->DevModel].vbi) {
		usbvision->vbi = usbvision_vdev_init(usbvision,
						     &usbvision_vbi_template,
						     "USBVision VBI");
		if (usbvision->vdev == NULL) {
			goto err_exit;
		}
		if (video_register_device(usbvision->vbi,
					  VFL_TYPE_VBI,
					  vbi_nr)<0) {
			goto err_exit;
		}
		printk(KERN_INFO "USBVision[%d]: registered USBVision VBI device /dev/vbi%d [v4l2] (Not Working Yet!)\n",
		       usbvision->nr,usbvision->vbi->minor & 0x1f);
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
 * This code allocates the struct usb_usbvision.
 * It is filled with default values.
 *
 * Returns NULL on error, a pointer to usb_usbvision else.
 *
 */
static struct usb_usbvision *usbvision_alloc(struct usb_device *dev)
{
	struct usb_usbvision *usbvision;

	if ((usbvision = kzalloc(sizeof(struct usb_usbvision), GFP_KERNEL)) ==
	    NULL) {
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


/*********************** usb interface **********************************/

static void usbvision_configure_video(struct usb_usbvision *usbvision)
{
	int model;

	if (usbvision == NULL)
		return;

	model = usbvision->DevModel;
	usbvision->palette = usbvision_v4l2_format[2]; // V4L2_PIX_FMT_RGB24;

	if (usbvision_device_data[usbvision->DevModel].Vin_Reg2_override) {
		usbvision->Vin_Reg2_Preset =
			usbvision_device_data[usbvision->DevModel].Vin_Reg2;
	} else {
		usbvision->Vin_Reg2_Preset = 0;
	}

	usbvision->tvnormId = usbvision_device_data[model].VideoNorm;

	usbvision->video_inputs = usbvision_device_data[model].VideoChannels;
	usbvision->ctl_input = 0;

	/* This should be here to make i2c clients to be able to register */
	/* first switch off audio */
	usbvision_audio_off(usbvision);
	if (!PowerOnAtOpen) {
		/* and then power up the noisy tuner */
		usbvision_power_on(usbvision);
		usbvision_i2c_register(usbvision);
	}
}

/*
 * usbvision_probe()
 *
 * This procedure queries device descriptor and accepts the interface
 * if it looks like USBVISION video device
 *
 */
static int __devinit usbvision_probe(struct usb_interface *intf,
				     const struct usb_device_id *devid)
{
	struct usb_device *dev = usb_get_dev(interface_to_usbdev(intf));
	struct usb_interface *uif;
	__u8 ifnum = intf->altsetting->desc.bInterfaceNumber;
	const struct usb_host_interface *interface;
	struct usb_usbvision *usbvision = NULL;
	const struct usb_endpoint_descriptor *endpoint;
	int model,i;

	PDEBUG(DBG_PROBE, "VID=%#04x, PID=%#04x, ifnum=%u",
				dev->descriptor.idVendor,
				dev->descriptor.idProduct, ifnum);

	model = devid->driver_info;
	if ( (model<0) || (model>=usbvision_device_data_size) ) {
		PDEBUG(DBG_PROBE, "model out of bounds %d",model);
		return -ENODEV;
	}
	printk(KERN_INFO "%s: %s found\n", __FUNCTION__,
				usbvision_device_data[model].ModelString);

	if (usbvision_device_data[model].Interface >= 0) {
		interface = &dev->actconfig->interface[usbvision_device_data[model].Interface]->altsetting[0];
	} else {
		interface = &dev->actconfig->interface[ifnum]->altsetting[0];
	}
	endpoint = &interface->endpoint[1].desc;
	if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) !=
	    USB_ENDPOINT_XFER_ISOC) {
		err("%s: interface %d. has non-ISO endpoint!",
		    __FUNCTION__, ifnum);
		err("%s: Endpoint attributes %d",
		    __FUNCTION__, endpoint->bmAttributes);
		return -ENODEV;
	}
	if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) ==
	    USB_DIR_OUT) {
		err("%s: interface %d. has ISO OUT endpoint!",
		    __FUNCTION__, ifnum);
		return -ENODEV;
	}

	if ((usbvision = usbvision_alloc(dev)) == NULL) {
		err("%s: couldn't allocate USBVision struct", __FUNCTION__);
		return -ENOMEM;
	}

	if (dev->descriptor.bNumConfigurations > 1) {
		usbvision->bridgeType = BRIDGE_NT1004;
	} else if (model == DAZZLE_DVC_90_REV_1_SECAM) {
		usbvision->bridgeType = BRIDGE_NT1005;
	} else {
		usbvision->bridgeType = BRIDGE_NT1003;
	}
	PDEBUG(DBG_PROBE, "bridgeType %d", usbvision->bridgeType);

	down(&usbvision->lock);

	/* compute alternate max packet sizes */
	uif = dev->actconfig->interface[0];

	usbvision->num_alt=uif->num_altsetting;
	PDEBUG(DBG_PROBE, "Alternate settings: %i",usbvision->num_alt);
	usbvision->alt_max_pkt_size = kmalloc(32*
					      usbvision->num_alt,GFP_KERNEL);
	if (usbvision->alt_max_pkt_size == NULL) {
		err("usbvision: out of memory!\n");
		return -ENOMEM;
	}

	for (i = 0; i < usbvision->num_alt ; i++) {
		u16 tmp = le16_to_cpu(uif->altsetting[i].endpoint[1].desc.
				      wMaxPacketSize);
		usbvision->alt_max_pkt_size[i] =
			(tmp & 0x07ff) * (((tmp & 0x1800) >> 11) + 1);
		PDEBUG(DBG_PROBE, "Alternate setting %i, max size= %i",i,
		       usbvision->alt_max_pkt_size[i]);
	}


	usbvision->nr = usbvision_nr++;

	usbvision->have_tuner = usbvision_device_data[model].Tuner;
	if (usbvision->have_tuner) {
		usbvision->tuner_type = usbvision_device_data[model].TunerType;
	}

	usbvision->tuner_addr = ADDR_UNSET;

	usbvision->DevModel = model;
	usbvision->remove_pending = 0;
	usbvision->iface = ifnum;
	usbvision->ifaceAlt = 0;
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
		usbvision_i2c_unregister(usbvision);
		usbvision_power_off(usbvision);
	}
	usbvision->remove_pending = 1;	// Now all ISO data will be ignored

	usb_put_dev(usbvision->dev);
	usbvision->dev = NULL;	// USB device is no more

	up(&usbvision->lock);

	if (usbvision->user) {
		printk(KERN_INFO "%s: In use, disconnect pending\n",
		       __FUNCTION__);
		wake_up_interruptible(&usbvision->wait_frame);
		wake_up_interruptible(&usbvision->wait_stream);
	} else {
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
 * usbvision_init()
 *
 * This code is run to initialize the driver.
 *
 */
static int __init usbvision_init(void)
{
	int errCode;

	PDEBUG(DBG_PROBE, "");

	PDEBUG(DBG_IO,  "IO      debugging is enabled [video]");
	PDEBUG(DBG_PROBE, "PROBE   debugging is enabled [video]");
	PDEBUG(DBG_MMAP, "MMAP    debugging is enabled [video]");

	/* disable planar mode support unless compression enabled */
	if (isocMode != ISOC_MODE_COMPRESS ) {
		// FIXME : not the right way to set supported flag
		usbvision_v4l2_format[6].supported = 0; // V4L2_PIX_FMT_YVU420
		usbvision_v4l2_format[7].supported = 0; // V4L2_PIX_FMT_YUV422P
	}

	errCode = usb_register(&usbvision_driver);

	if (errCode == 0) {
		printk(KERN_INFO DRIVER_DESC " : " USBVISION_VERSION_STRING "\n");
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

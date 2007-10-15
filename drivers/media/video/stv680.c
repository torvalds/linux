/*
 *  STV0680 USB Camera Driver, by Kevin Sisson (kjsisson@bellsouth.net)
 *
 * Thanks to STMicroelectronics for information on the usb commands, and
 * to Steve Miller at STM for his help and encouragement while I was
 * writing this driver.
 *
 * This driver is based heavily on the
 * Endpoints (formerly known as AOX) se401 USB Camera Driver
 * Copyright (c) 2000 Jeroen B. Vreeken (pe1rxq@amsat.org)
 *
 * Still somewhat based on the Linux ov511 driver.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * History:
 * ver 0.1 October, 2001. Initial attempt.
 *
 * ver 0.2 November, 2001. Fixed asbility to resize, added brightness
 *                         function, made more stable (?)
 *
 * ver 0.21 Nov, 2001.     Added gamma correction and white balance,
 *                         due to Alexander Schwartz. Still trying to
 *                         improve stablility. Moved stuff into stv680.h
 *
 * ver 0.22 Nov, 2001.	   Added sharpen function (by Michael Sweet,
 *                         mike@easysw.com) from GIMP, also used in pencam.
 *                         Simple, fast, good integer math routine.
 *
 * ver 0.23 Dec, 2001 (gkh)
 * 			   Took out sharpen function, ran code through
 * 			   Lindent, and did other minor tweaks to get
 * 			   things to work properly with 2.5.1
 *
 * ver 0.24 Jan, 2002 (kjs)
 *                         Fixed the problem with webcam crashing after
 *                         two pictures. Changed the way pic is halved to
 *                         improve quality. Got rid of green line around
 *                         frame. Fix brightness reset when changing size
 *                         bug. Adjusted gamma filters slightly.
 *
 * ver 0.25 Jan, 2002 (kjs)
 *			   Fixed a bug in which the driver sometimes attempted
 *			   to set to a non-supported size. This allowed
 *			   gnomemeeting to work.
 *			   Fixed proc entry removal bug.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/errno.h>
#include <linux/videodev.h>
#include <media/v4l2-common.h>
#include <linux/usb.h>
#include <linux/mutex.h>

#include "stv680.h"

static int video_nr = -1;
static int swapRGB = 0;   /* default for auto sleect */
static int swapRGB_on = 0; /* default to allow auto select; -1=swap never, +1= swap always */

static unsigned int debug = 0;

#define PDEBUG(level, fmt, args...) \
	do { \
	if (debug >= level)	\
		info("[%s:%d] " fmt, __FUNCTION__, __LINE__ , ## args);	\
	} while (0)


/*
 * Version Information
 */
#define DRIVER_VERSION "v0.25"
#define DRIVER_AUTHOR "Kevin Sisson <kjsisson@bellsouth.net>"
#define DRIVER_DESC "STV0680 USB Camera Driver"

MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_DESCRIPTION (DRIVER_DESC);
MODULE_LICENSE ("GPL");
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC (debug, "Debug enabled or not");
module_param(swapRGB_on, int, 0);
MODULE_PARM_DESC (swapRGB_on, "Red/blue swap: 1=always, 0=auto, -1=never");
module_param(video_nr, int, 0);

/********************************************************************
 *
 * Memory management
 *
 * This is a shameless copy from the USB-cpia driver (linux kernel
 * version 2.3.29 or so, I have no idea what this code actually does ;).
 * Actually it seems to be a copy of a shameless copy of the bttv-driver.
 * Or that is a copy of a shameless copy of ... (To the powers: is there
 * no generic kernel-function to do this sort of stuff?)
 *
 * Yes, it was a shameless copy from the bttv-driver. IIRC, Alan says
 * there will be one, but apparentely not yet -jerdfelt
 *
 * So I copied it again for the ov511 driver -claudio
 *
 * Same for the se401 driver -Jeroen
 *
 * And the STV0680 driver - Kevin
 ********************************************************************/
static void *rvmalloc (unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32 (size);
	if (!mem)
		return NULL;

	memset (mem, 0, size);	/* Clear the ram out, no junk to the user */
	adr = (unsigned long) mem;
	while (size > 0) {
		SetPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	return mem;
}

static void rvfree (void *mem, unsigned long size)
{
	unsigned long adr;

	if (!mem)
		return;

	adr = (unsigned long) mem;
	while ((long) size > 0) {
		ClearPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	vfree (mem);
}


/*********************************************************************
 * pencam read/write functions
 ********************************************************************/

static int stv_sndctrl (int set, struct usb_stv *stv680, unsigned short req, unsigned short value, unsigned char *buffer, int size)
{
	int ret = -1;

	switch (set) {
	case 0:		/*  0xc1  */
		ret = usb_control_msg (stv680->udev,
				       usb_rcvctrlpipe (stv680->udev, 0),
				       req,
				       (USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT),
				       value, 0, buffer, size, PENCAM_TIMEOUT);
		break;

	case 1:		/*  0x41  */
		ret = usb_control_msg (stv680->udev,
				       usb_sndctrlpipe (stv680->udev, 0),
				       req,
				       (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT),
				       value, 0, buffer, size, PENCAM_TIMEOUT);
		break;

	case 2:		/*  0x80  */
		ret = usb_control_msg (stv680->udev,
				       usb_rcvctrlpipe (stv680->udev, 0),
				       req,
				       (USB_DIR_IN | USB_RECIP_DEVICE),
				       value, 0, buffer, size, PENCAM_TIMEOUT);
		break;

	case 3:		/*  0x40  */
		ret = usb_control_msg (stv680->udev,
				       usb_sndctrlpipe (stv680->udev, 0),
				       req,
				       (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE),
				       value, 0, buffer, size, PENCAM_TIMEOUT);
		break;

	}
	if ((ret < 0) && (req != 0x0a)) {
		PDEBUG (1, "STV(e): usb_control_msg error %i, request = 0x%x, error = %i", set, req, ret);
	}
	return ret;
}

static int stv_set_config (struct usb_stv *dev, int configuration, int interface, int alternate)
{

	if (configuration != dev->udev->actconfig->desc.bConfigurationValue
			|| usb_reset_configuration (dev->udev) < 0) {
		PDEBUG (1, "STV(e): FAILED to reset configuration %i", configuration);
		return -1;
	}
	if (usb_set_interface (dev->udev, interface, alternate) < 0) {
		PDEBUG (1, "STV(e): FAILED to set alternate interface %i", alternate);
		return -1;
	}
	return 0;
}

static int stv_stop_video (struct usb_stv *dev)
{
	int i;
	unsigned char *buf;

	buf = kmalloc (40, GFP_KERNEL);
	if (buf == NULL) {
		PDEBUG (0, "STV(e): Out of (small buf) memory");
		return -1;
	}

	/* this is a high priority command; it stops all lower order commands */
	if ((i = stv_sndctrl (1, dev, 0x04, 0x0000, buf, 0x0)) < 0) {
		i = stv_sndctrl (0, dev, 0x80, 0, buf, 0x02);	/* Get Last Error; 2 = busy */
		PDEBUG (1, "STV(i): last error: %i,  command = 0x%x", buf[0], buf[1]);
	} else {
		PDEBUG (1, "STV(i): Camera reset to idle mode.");
	}

	if ((i = stv_set_config (dev, 1, 0, 0)) < 0)
		PDEBUG (1, "STV(e): Reset config during exit failed");

	/*  get current mode  */
	buf[0] = 0xf0;
	if ((i = stv_sndctrl (0, dev, 0x87, 0, buf, 0x08)) != 0x08)	/* get mode */
		PDEBUG (0, "STV(e): Stop_video: problem setting original mode");
	if (dev->origMode != buf[0]) {
		memset (buf, 0, 8);
		buf[0] = (unsigned char) dev->origMode;
		if ((i = stv_sndctrl (3, dev, 0x07, 0x0100, buf, 0x08)) != 0x08) {
			PDEBUG (0, "STV(e): Stop_video: Set_Camera_Mode failed");
			i = -1;
		}
		buf[0] = 0xf0;
		i = stv_sndctrl (0, dev, 0x87, 0, buf, 0x08);
		if ((i != 0x08) || (buf[0] != dev->origMode)) {
			PDEBUG (0, "STV(e): camera NOT set to original resolution.");
			i = -1;
		} else
			PDEBUG (0, "STV(i): Camera set to original resolution");
	}
	/* origMode */
	kfree(buf);
	return i;
}

static int stv_set_video_mode (struct usb_stv *dev)
{
	int i, stop_video = 1;
	unsigned char *buf;

	buf = kmalloc (40, GFP_KERNEL);
	if (buf == NULL) {
		PDEBUG (0, "STV(e): Out of (small buf) memory");
		return -1;
	}

	if ((i = stv_set_config (dev, 1, 0, 0)) < 0) {
		kfree(buf);
		return i;
	}

	i = stv_sndctrl (2, dev, 0x06, 0x0100, buf, 0x12);
	if (!(i > 0) && (buf[8] == 0x53) && (buf[9] == 0x05)) {
		PDEBUG (1, "STV(e): Could not get descriptor 0100.");
		goto error;
	}

	/*  set alternate interface 1 */
	if ((i = stv_set_config (dev, 1, 0, 1)) < 0)
		goto error;

	if ((i = stv_sndctrl (0, dev, 0x85, 0, buf, 0x10)) != 0x10)
		goto error;
	PDEBUG (1, "STV(i): Setting video mode.");
	/*  Switch to Video mode: 0x0100 = VGA (640x480), 0x0000 = CIF (352x288) 0x0300 = QVGA (320x240)  */
	if ((i = stv_sndctrl (1, dev, 0x09, dev->VideoMode, buf, 0x0)) < 0) {
		stop_video = 0;
		goto error;
	}
	goto exit;

error:
	kfree(buf);
	if (stop_video == 1)
		stv_stop_video (dev);
	return -1;

exit:
	kfree(buf);
	return 0;
}

static int stv_init (struct usb_stv *stv680)
{
	int i = 0;
	unsigned char *buffer;
	unsigned long int bufsize;

	buffer = kzalloc (40, GFP_KERNEL);
	if (buffer == NULL) {
		PDEBUG (0, "STV(e): Out of (small buf) memory");
		return -1;
	}
	udelay (100);

	/* set config 1, interface 0, alternate 0 */
	if ((i = stv_set_config (stv680, 1, 0, 0)) < 0) {
		kfree(buffer);
		PDEBUG (0, "STV(e): set config 1,0,0 failed");
		return -1;
	}
	/* ping camera to be sure STV0680 is present */
	if ((i = stv_sndctrl (0, stv680, 0x88, 0x5678, buffer, 0x02)) != 0x02)
		goto error;
	if ((buffer[0] != 0x56) || (buffer[1] != 0x78)) {
		PDEBUG (1, "STV(e): camera ping failed!!");
		goto error;
	}

	/* get camera descriptor */
	if ((i = stv_sndctrl (2, stv680, 0x06, 0x0200, buffer, 0x09)) != 0x09)
		goto error;
	i = stv_sndctrl (2, stv680, 0x06, 0x0200, buffer, 0x22);
	if (!(i >= 0) && (buffer[7] == 0xa0) && (buffer[8] == 0x23)) {
		PDEBUG (1, "STV(e): Could not get descriptor 0200.");
		goto error;
	}
	if ((i = stv_sndctrl (0, stv680, 0x8a, 0, buffer, 0x02)) != 0x02)
		goto error;
	if ((i = stv_sndctrl (0, stv680, 0x8b, 0, buffer, 0x24)) != 0x24)
		goto error;
	if ((i = stv_sndctrl (0, stv680, 0x85, 0, buffer, 0x10)) != 0x10)
		goto error;

	stv680->SupportedModes = buffer[7];
	i = stv680->SupportedModes;
	stv680->CIF = 0;
	stv680->VGA = 0;
	stv680->QVGA = 0;
	if (i & 1)
		stv680->CIF = 1;
	if (i & 2)
		stv680->VGA = 1;
	if (i & 8)
		stv680->QVGA = 1;
	if (stv680->SupportedModes == 0) {
		PDEBUG (0, "STV(e): There are NO supported STV680 modes!!");
		i = -1;
		goto error;
	} else {
		if (stv680->CIF)
			PDEBUG (0, "STV(i): CIF is supported");
		if (stv680->QVGA)
			PDEBUG (0, "STV(i): QVGA is supported");
	}
	/* FW rev, ASIC rev, sensor ID  */
	PDEBUG (1, "STV(i): Firmware rev is %i.%i", buffer[0], buffer[1]);
	PDEBUG (1, "STV(i): ASIC rev is %i.%i", buffer[2], buffer[3]);
	PDEBUG (1, "STV(i): Sensor ID is %i", (buffer[4]*16) + (buffer[5]>>4));

	/*  set alternate interface 1 */
	if ((i = stv_set_config (stv680, 1, 0, 1)) < 0)
		goto error;

	if ((i = stv_sndctrl (0, stv680, 0x85, 0, buffer, 0x10)) != 0x10)
		goto error;
	if ((i = stv_sndctrl (0, stv680, 0x8d, 0, buffer, 0x08)) != 0x08)
		goto error;
	i = buffer[3];
	PDEBUG (0, "STV(i): Camera has %i pictures.", i);

	/*  get current mode */
	if ((i = stv_sndctrl (0, stv680, 0x87, 0, buffer, 0x08)) != 0x08)
		goto error;
	stv680->origMode = buffer[0];	/* 01 = VGA, 03 = QVGA, 00 = CIF */

	/* This will attemp CIF mode, if supported. If not, set to QVGA  */
	memset (buffer, 0, 8);
	if (stv680->CIF)
		buffer[0] = 0x00;
	else if (stv680->QVGA)
		buffer[0] = 0x03;
	if ((i = stv_sndctrl (3, stv680, 0x07, 0x0100, buffer, 0x08)) != 0x08) {
		PDEBUG (0, "STV(i): Set_Camera_Mode failed");
		i = -1;
		goto error;
	}
	buffer[0] = 0xf0;
	stv_sndctrl (0, stv680, 0x87, 0, buffer, 0x08);
	if (((stv680->CIF == 1) && (buffer[0] != 0x00)) || ((stv680->QVGA == 1) && (buffer[0] != 0x03))) {
		PDEBUG (0, "STV(e): Error setting camera video mode!");
		i = -1;
		goto error;
	} else {
		if (buffer[0] == 0) {
			stv680->VideoMode = 0x0000;
			PDEBUG (0, "STV(i): Video Mode set to CIF");
		}
		if (buffer[0] == 0x03) {
			stv680->VideoMode = 0x0300;
			PDEBUG (0, "STV(i): Video Mode set to QVGA");
		}
	}
	if ((i = stv_sndctrl (0, stv680, 0x8f, 0, buffer, 0x10)) != 0x10)
		goto error;
	bufsize = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | (buffer[3]);
	stv680->cwidth = (buffer[4] << 8) | (buffer[5]);	/* ->camera = 322, 356, 644  */
	stv680->cheight = (buffer[6] << 8) | (buffer[7]);	/* ->camera = 242, 292, 484  */
	stv680->origGain = buffer[12];

	goto exit;

error:
	i = stv_sndctrl (0, stv680, 0x80, 0, buffer, 0x02);	/* Get Last Error */
	PDEBUG (1, "STV(i): last error: %i,  command = 0x%x", buffer[0], buffer[1]);
	kfree(buffer);
	return -1;

exit:
	kfree(buffer);

	/* video = 320x240, 352x288 */
	if (stv680->CIF == 1) {
		stv680->maxwidth = 352;
		stv680->maxheight = 288;
		stv680->vwidth = 352;
		stv680->vheight = 288;
	}
	if (stv680->QVGA == 1) {
		stv680->maxwidth = 320;
		stv680->maxheight = 240;
		stv680->vwidth = 320;
		stv680->vheight = 240;
	}

	stv680->rawbufsize = bufsize;	/* must be ./. by 8 */
	stv680->maxframesize = bufsize * 3;	/* RGB size */
	PDEBUG (2, "STV(i): cwidth = %i, cheight = %i", stv680->cwidth, stv680->cheight);
	PDEBUG (1, "STV(i): width = %i, height = %i, rawbufsize = %li", stv680->vwidth, stv680->vheight, stv680->rawbufsize);

	/* some default values */
	stv680->bulk_in_endpointAddr = 0x82;
	stv680->dropped = 0;
	stv680->error = 0;
	stv680->framecount = 0;
	stv680->readcount = 0;
	stv680->streaming = 0;
	/* bright, white, colour, hue, contrast are set by software, not in stv0680 */
	stv680->brightness = 32767;
	stv680->chgbright = 0;
	stv680->whiteness = 0;	/* only for greyscale */
	stv680->colour = 32767;
	stv680->contrast = 32767;
	stv680->hue = 32767;
	stv680->palette = STV_VIDEO_PALETTE;
	stv680->depth = 24;	/* rgb24 bits */
	if ((swapRGB_on == 0) && (swapRGB == 0))
		PDEBUG (1, "STV(i): swapRGB is (auto) OFF");
	else if ((swapRGB_on == 0) && (swapRGB == 1))
		PDEBUG (1, "STV(i): swapRGB is (auto) ON");
	else if (swapRGB_on == 1)
		PDEBUG (1, "STV(i): swapRGB is (forced) ON");
	else if (swapRGB_on == -1)
		PDEBUG (1, "STV(i): swapRGB is (forced) OFF");

	if (stv_set_video_mode (stv680) < 0) {
		PDEBUG (0, "STV(e): Could not set video mode in stv_init");
		return -1;
	}

	return 0;
}

/***************** last of pencam  routines  *******************/

/****************************************************************************
 *  sysfs
 ***************************************************************************/
#define stv680_file(name, variable, field)				\
static ssize_t show_##name(struct device *class_dev,			\
			   struct device_attribute *attr, char *buf)	\
{									\
	struct video_device *vdev = to_video_device(class_dev);		\
	struct usb_stv *stv = video_get_drvdata(vdev);			\
	return sprintf(buf, field, stv->variable);			\
}									\
static DEVICE_ATTR(name, S_IRUGO, show_##name, NULL);

stv680_file(model, camera_name, "%s\n");
stv680_file(in_use, user, "%d\n");
stv680_file(streaming, streaming, "%d\n");
stv680_file(palette, palette, "%i\n");
stv680_file(frames_total, readcount, "%d\n");
stv680_file(frames_read, framecount, "%d\n");
stv680_file(packets_dropped, dropped, "%d\n");
stv680_file(decoding_errors, error, "%d\n");

static int stv680_create_sysfs_files(struct video_device *vdev)
{
	int rc;

	rc = video_device_create_file(vdev, &dev_attr_model);
	if (rc) goto err;
	rc = video_device_create_file(vdev, &dev_attr_in_use);
	if (rc) goto err_model;
	rc = video_device_create_file(vdev, &dev_attr_streaming);
	if (rc) goto err_inuse;
	rc = video_device_create_file(vdev, &dev_attr_palette);
	if (rc) goto err_stream;
	rc = video_device_create_file(vdev, &dev_attr_frames_total);
	if (rc) goto err_pal;
	rc = video_device_create_file(vdev, &dev_attr_frames_read);
	if (rc) goto err_framtot;
	rc = video_device_create_file(vdev, &dev_attr_packets_dropped);
	if (rc) goto err_framread;
	rc = video_device_create_file(vdev, &dev_attr_decoding_errors);
	if (rc) goto err_dropped;

	return 0;

err_dropped:
	video_device_remove_file(vdev, &dev_attr_packets_dropped);
err_framread:
	video_device_remove_file(vdev, &dev_attr_frames_read);
err_framtot:
	video_device_remove_file(vdev, &dev_attr_frames_total);
err_pal:
	video_device_remove_file(vdev, &dev_attr_palette);
err_stream:
	video_device_remove_file(vdev, &dev_attr_streaming);
err_inuse:
	video_device_remove_file(vdev, &dev_attr_in_use);
err_model:
	video_device_remove_file(vdev, &dev_attr_model);
err:
	return rc;
}

static void stv680_remove_sysfs_files(struct video_device *vdev)
{
	video_device_remove_file(vdev, &dev_attr_model);
	video_device_remove_file(vdev, &dev_attr_in_use);
	video_device_remove_file(vdev, &dev_attr_streaming);
	video_device_remove_file(vdev, &dev_attr_palette);
	video_device_remove_file(vdev, &dev_attr_frames_total);
	video_device_remove_file(vdev, &dev_attr_frames_read);
	video_device_remove_file(vdev, &dev_attr_packets_dropped);
	video_device_remove_file(vdev, &dev_attr_decoding_errors);
}

/********************************************************************
 * Camera control
 *******************************************************************/

static int stv680_get_pict (struct usb_stv *stv680, struct video_picture *p)
{
	/* This sets values for v4l interface. max/min = 65535/0  */

	p->brightness = stv680->brightness;
	p->whiteness = stv680->whiteness;	/* greyscale */
	p->colour = stv680->colour;
	p->contrast = stv680->contrast;
	p->hue = stv680->hue;
	p->palette = stv680->palette;
	p->depth = stv680->depth;
	return 0;
}

static int stv680_set_pict (struct usb_stv *stv680, struct video_picture *p)
{
	/* See above stv680_get_pict  */

	if (p->palette != STV_VIDEO_PALETTE) {
		PDEBUG (2, "STV(e): Palette set error in _set_pic");
		return 1;
	}

	if (stv680->brightness != p->brightness) {
		stv680->chgbright = 1;
		stv680->brightness = p->brightness;
	}

	stv680->whiteness = p->whiteness;	/* greyscale */
	stv680->colour = p->colour;
	stv680->contrast = p->contrast;
	stv680->hue = p->hue;
	stv680->palette = p->palette;
	stv680->depth = p->depth;

	return 0;
}

static void stv680_video_irq (struct urb *urb)
{
	struct usb_stv *stv680 = urb->context;
	int length = urb->actual_length;

	if (length < stv680->rawbufsize)
		PDEBUG (2, "STV(i): Lost data in transfer: exp %li, got %i", stv680->rawbufsize, length);

	/* ohoh... */
	if (!stv680->streaming)
		return;

	if (!stv680->udev) {
		PDEBUG (0, "STV(e): device vapourished in video_irq");
		return;
	}

	/* 0 sized packets happen if we are to fast, but sometimes the camera
	   keeps sending them forever...
	 */
	if (length && !urb->status) {
		stv680->nullpackets = 0;
		switch (stv680->scratch[stv680->scratch_next].state) {
		case BUFFER_READY:
		case BUFFER_BUSY:
			stv680->dropped++;
			break;

		case BUFFER_UNUSED:
			memcpy (stv680->scratch[stv680->scratch_next].data,
				(unsigned char *) urb->transfer_buffer, length);
			stv680->scratch[stv680->scratch_next].state = BUFFER_READY;
			stv680->scratch[stv680->scratch_next].length = length;
			if (waitqueue_active (&stv680->wq)) {
				wake_up_interruptible (&stv680->wq);
			}
			stv680->scratch_overflow = 0;
			stv680->scratch_next++;
			if (stv680->scratch_next >= STV680_NUMSCRATCH)
				stv680->scratch_next = 0;
			break;
		}		/* switch  */
	} else {
		stv680->nullpackets++;
		if (stv680->nullpackets > STV680_MAX_NULLPACKETS) {
			if (waitqueue_active (&stv680->wq)) {
				wake_up_interruptible (&stv680->wq);
			}
		}
	}			/*  if - else */

	/* Resubmit urb for new data */
	urb->status = 0;
	urb->dev = stv680->udev;
	if (usb_submit_urb (urb, GFP_ATOMIC))
		PDEBUG (0, "STV(e): urb burned down in video irq");
	return;
}				/*  _video_irq  */

static int stv680_start_stream (struct usb_stv *stv680)
{
	struct urb *urb;
	int err = 0, i;

	stv680->streaming = 1;

	/* Do some memory allocation */
	for (i = 0; i < STV680_NUMFRAMES; i++) {
		stv680->frame[i].data = stv680->fbuf + i * stv680->maxframesize;
		stv680->frame[i].curpix = 0;
	}
	/* packet size = 4096  */
	for (i = 0; i < STV680_NUMSBUF; i++) {
		stv680->sbuf[i].data = kmalloc (stv680->rawbufsize, GFP_KERNEL);
		if (stv680->sbuf[i].data == NULL) {
			PDEBUG (0, "STV(e): Could not kmalloc raw data buffer %i", i);
			goto nomem_err;
		}
	}

	stv680->scratch_next = 0;
	stv680->scratch_use = 0;
	stv680->scratch_overflow = 0;
	for (i = 0; i < STV680_NUMSCRATCH; i++) {
		stv680->scratch[i].data = kmalloc (stv680->rawbufsize, GFP_KERNEL);
		if (stv680->scratch[i].data == NULL) {
			PDEBUG (0, "STV(e): Could not kmalloc raw scratch buffer %i", i);
			goto nomem_err;
		}
		stv680->scratch[i].state = BUFFER_UNUSED;
	}

	for (i = 0; i < STV680_NUMSBUF; i++) {
		urb = usb_alloc_urb (0, GFP_KERNEL);
		if (!urb)
			goto nomem_err;

		/* sbuf is urb->transfer_buffer, later gets memcpyed to scratch */
		usb_fill_bulk_urb (urb, stv680->udev,
				   usb_rcvbulkpipe (stv680->udev, stv680->bulk_in_endpointAddr),
				   stv680->sbuf[i].data, stv680->rawbufsize,
				   stv680_video_irq, stv680);
		stv680->urb[i] = urb;
		err = usb_submit_urb (stv680->urb[i], GFP_KERNEL);
		if (err) {
			PDEBUG (0, "STV(e): urb burned down with err "
				   "%d in start stream %d", err, i);
			goto nomem_err;
		}
	}			/* i STV680_NUMSBUF */

	stv680->framecount = 0;
	return 0;

 nomem_err:
	for (i = 0; i < STV680_NUMSCRATCH; i++) {
		kfree(stv680->scratch[i].data);
		stv680->scratch[i].data = NULL;
	}
	for (i = 0; i < STV680_NUMSBUF; i++) {
		usb_kill_urb(stv680->urb[i]);
		usb_free_urb(stv680->urb[i]);
		stv680->urb[i] = NULL;
		kfree(stv680->sbuf[i].data);
		stv680->sbuf[i].data = NULL;
	}
	return -ENOMEM;

}

static int stv680_stop_stream (struct usb_stv *stv680)
{
	int i;

	if (!stv680->streaming || !stv680->udev)
		return 1;

	stv680->streaming = 0;

	for (i = 0; i < STV680_NUMSBUF; i++)
		if (stv680->urb[i]) {
			usb_kill_urb (stv680->urb[i]);
			usb_free_urb (stv680->urb[i]);
			stv680->urb[i] = NULL;
			kfree(stv680->sbuf[i].data);
		}
	for (i = 0; i < STV680_NUMSCRATCH; i++) {
		kfree(stv680->scratch[i].data);
		stv680->scratch[i].data = NULL;
	}

	return 0;
}

static int stv680_set_size (struct usb_stv *stv680, int width, int height)
{
	int wasstreaming = stv680->streaming;

	/* Check to see if we need to change */
	if ((stv680->vwidth == width) && (stv680->vheight == height))
		return 0;

	PDEBUG (1, "STV(i): size request for %i x %i", width, height);
	/* Check for a valid mode */
	if ((!width || !height) || ((width & 1) || (height & 1))) {
		PDEBUG (1, "STV(e): set_size error: request: v.width = %i, v.height = %i  actual: stv.width = %i, stv.height = %i", width, height, stv680->vwidth, stv680->vheight);
		return 1;
	}

	if ((width < (stv680->maxwidth / 2)) || (height < (stv680->maxheight / 2))) {
		width = stv680->maxwidth / 2;
		height = stv680->maxheight / 2;
	} else if ((width >= 158) && (width <= 166) && (stv680->QVGA == 1)) {
		width = 160;
		height = 120;
	} else if ((width >= 172) && (width <= 180) && (stv680->CIF == 1)) {
		width = 176;
		height = 144;
	} else if ((width >= 318) && (width <= 350) && (stv680->QVGA == 1)) {
		width = 320;
		height = 240;
	} else if ((width >= 350) && (width <= 358) && (stv680->CIF == 1)) {
		width = 352;
		height = 288;
	} else {
		PDEBUG (1, "STV(e): request for non-supported size: request: v.width = %i, v.height = %i  actual: stv.width = %i, stv.height = %i", width, height, stv680->vwidth, stv680->vheight);
		return 1;
	}

	/* Stop a current stream and start it again at the new size */
	if (wasstreaming)
		stv680_stop_stream (stv680);
	stv680->vwidth = width;
	stv680->vheight = height;
	PDEBUG (1, "STV(i): size set to %i x %i", stv680->vwidth, stv680->vheight);
	if (wasstreaming)
		stv680_start_stream (stv680);

	return 0;
}

/**********************************************************************
 * Video Decoding
 **********************************************************************/

/*******  routines from the pencam program; hey, they work!  ********/

/*
 * STV0680 Vision Camera Chipset Driver
 * Copyright (C) 2000 Adam Harrison <adam@antispin.org>
*/

#define RED 0
#define GREEN 1
#define BLUE 2
#define AD(x, y, w) (((y)*(w)+(x))*3)

static void bayer_unshuffle (struct usb_stv *stv680, struct stv680_scratch *buffer)
{
	int x, y, i;
	int w = stv680->cwidth;
	int vw = stv680->cwidth, vh = stv680->cheight;
	unsigned int p = 0;
	int colour = 0, bayer = 0;
	unsigned char *raw = buffer->data;
	struct stv680_frame *frame = &stv680->frame[stv680->curframe];
	unsigned char *output = frame->data;
	unsigned char *temp = frame->data;
	int offset = buffer->offset;

	if (frame->curpix == 0) {
		if (frame->grabstate == FRAME_READY) {
			frame->grabstate = FRAME_GRABBING;
		}
	}
	if (offset != frame->curpix) {	/* Regard frame as lost :( */
		frame->curpix = 0;
		stv680->error++;
		return;
	}

	if ((stv680->vwidth == 320) || (stv680->vwidth == 160)) {
		vw = 320;
		vh = 240;
	}
	if ((stv680->vwidth == 352) || (stv680->vwidth == 176)) {
		vw = 352;
		vh = 288;
	}

	memset (output, 0, 3 * vw * vh);	/* clear output matrix. */

	for (y = 0; y < vh; y++) {
		for (x = 0; x < vw; x++) {
			if (x & 1)
				p = *(raw + y * w + (x >> 1));
			else
				p = *(raw + y * w + (x >> 1) + (w >> 1));

			if (y & 1)
				bayer = 2;
			else
				bayer = 0;
			if (x & 1)
				bayer++;

			switch (bayer) {
			case 0:
			case 3:
				colour = 1;
				break;
			case 1:
				colour = 0;
				break;
			case 2:
				colour = 2;
				break;
			}
			i = (y * vw + x) * 3;
			*(output + i + colour) = (unsigned char) p;
		}		/* for x */

	}			/* for y */

	/****** gamma correction plus hardcoded white balance */
	/* Thanks to Alexander Schwartx <alexander.schwartx@gmx.net> for this code.
	   Correction values red[], green[], blue[], are generated by
	   (pow(i/256.0, GAMMA)*255.0)*white balanceRGB where GAMMA=0.55, 1<i<255.
	   White balance (RGB)= 1.0, 1.17, 1.48. Values are calculated as double float and
	   converted to unsigned char. Values are in stv680.h  */

	for (y = 0; y < vh; y++) {
		for (x = 0; x < vw; x++) {
			i = (y * vw + x) * 3;
			*(output + i) = red[*(output + i)];
			*(output + i + 1) = green[*(output + i + 1)];
			*(output + i + 2) = blue[*(output + i + 2)];
		}
	}

	/******  bayer demosaic  ******/
	for (y = 1; y < (vh - 1); y++) {
		for (x = 1; x < (vw - 1); x++) {	/* work out pixel type */
			if (y & 1)
				bayer = 0;
			else
				bayer = 2;
			if (!(x & 1))
				bayer++;

			switch (bayer) {
			case 0:	/* green. blue lr, red tb */
				*(output + AD (x, y, vw) + BLUE) = ((int) *(output + AD (x - 1, y, vw) + BLUE) + (int) *(output + AD (x + 1, y, vw) + BLUE)) >> 1;
				*(output + AD (x, y, vw) + RED) = ((int) *(output + AD (x, y - 1, vw) + RED) + (int) *(output + AD (x, y + 1, vw) + RED)) >> 1;
				break;

			case 1:	/* blue. green lrtb, red diagonals */
				*(output + AD (x, y, vw) + GREEN) = ((int) *(output + AD (x - 1, y, vw) + GREEN) + (int) *(output + AD (x + 1, y, vw) + GREEN) + (int) *(output + AD (x, y - 1, vw) + GREEN) + (int) *(output + AD (x, y + 1, vw) + GREEN)) >> 2;
				*(output + AD (x, y, vw) + RED) = ((int) *(output + AD (x - 1, y - 1, vw) + RED) + (int) *(output + AD (x - 1, y + 1, vw) + RED) + (int) *(output + AD (x + 1, y - 1, vw) + RED) + (int) *(output + AD (x + 1, y + 1, vw) + RED)) >> 2;
				break;

			case 2:	/* red. green lrtb, blue diagonals */
				*(output + AD (x, y, vw) + GREEN) = ((int) *(output + AD (x - 1, y, vw) + GREEN) + (int) *(output + AD (x + 1, y, vw) + GREEN) + (int) *(output + AD (x, y - 1, vw) + GREEN) + (int) *(output + AD (x, y + 1, vw) + GREEN)) >> 2;
				*(output + AD (x, y, vw) + BLUE) = ((int) *(output + AD (x - 1, y - 1, vw) + BLUE) + (int) *(output + AD (x + 1, y - 1, vw) + BLUE) + (int) *(output + AD (x - 1, y + 1, vw) + BLUE) + (int) *(output + AD (x + 1, y + 1, vw) + BLUE)) >> 2;
				break;

			case 3:	/* green. red lr, blue tb */
				*(output + AD (x, y, vw) + RED) = ((int) *(output + AD (x - 1, y, vw) + RED) + (int) *(output + AD (x + 1, y, vw) + RED)) >> 1;
				*(output + AD (x, y, vw) + BLUE) = ((int) *(output + AD (x, y - 1, vw) + BLUE) + (int) *(output + AD (x, y + 1, vw) + BLUE)) >> 1;
				break;
			}	/* switch */
		}		/* for x */
	}			/* for y  - end demosaic  */

	/* fix top and bottom row, left and right side */
	i = vw * 3;
	memcpy (output, (output + i), i);
	memcpy ((output + (vh * i)), (output + ((vh - 1) * i)), i);
	for (y = 0; y < vh; y++) {
		i = y * vw * 3;
		memcpy ((output + i), (output + i + 3), 3);
		memcpy ((output + i + (vw * 3)), (output + i + (vw - 1) * 3), 3);
	}

	/*  process all raw data, then trim to size if necessary */
	if ((stv680->vwidth == 160) || (stv680->vwidth == 176))  {
		i = 0;
		for (y = 0; y < vh; y++) {
			if (!(y & 1)) {
				for (x = 0; x < vw; x++) {
					p = (y * vw + x) * 3;
					if (!(x & 1)) {
						*(output + i) = *(output + p);
						*(output + i + 1) = *(output + p + 1);
						*(output + i + 2) = *(output + p + 2);
						i += 3;
					}
				}  /* for x */
			}
		}  /* for y */
	}
	/* reset to proper width */
	if ((stv680->vwidth == 160)) {
		vw = 160;
		vh = 120;
	}
	if ((stv680->vwidth == 176)) {
		vw = 176;
		vh = 144;
	}

	/* output is RGB; some programs want BGR  */
	/* swapRGB_on=0 -> program decides;  swapRGB_on=1, always swap */
	/* swapRGB_on=-1, never swap */
	if (((swapRGB == 1) && (swapRGB_on != -1)) || (swapRGB_on == 1)) {
		for (y = 0; y < vh; y++) {
			for (x = 0; x < vw; x++) {
				i = (y * vw + x) * 3;
				*(temp) = *(output + i);
				*(output + i) = *(output + i + 2);
				*(output + i + 2) = *(temp);
			}
		}
	}
	/* brightness */
	if (stv680->chgbright == 1) {
		if (stv680->brightness >= 32767) {
			p = (stv680->brightness - 32767) / 256;
			for (x = 0; x < (vw * vh * 3); x++) {
				if ((*(output + x) + (unsigned char) p) > 255)
					*(output + x) = 255;
				else
					*(output + x) += (unsigned char) p;
			}	/* for */
		} else {
			p = (32767 - stv680->brightness) / 256;
			for (x = 0; x < (vw * vh * 3); x++) {
				if ((unsigned char) p > *(output + x))
					*(output + x) = 0;
				else
					*(output + x) -= (unsigned char) p;
			}	/* for */
		}		/* else */
	}
	/* if */
	frame->curpix = 0;
	frame->curlinepix = 0;
	frame->grabstate = FRAME_DONE;
	stv680->framecount++;
	stv680->readcount++;
	if (stv680->frame[(stv680->curframe + 1) & (STV680_NUMFRAMES - 1)].grabstate == FRAME_READY) {
		stv680->curframe = (stv680->curframe + 1) & (STV680_NUMFRAMES - 1);
	}

}				/* bayer_unshuffle */

/*******  end routines from the pencam program  *********/

static int stv680_newframe (struct usb_stv *stv680, int framenr)
{
	int errors = 0;

	while (stv680->streaming && (stv680->frame[framenr].grabstate == FRAME_READY || stv680->frame[framenr].grabstate == FRAME_GRABBING)) {
		if (!stv680->frame[framenr].curpix) {
			errors++;
		}
		wait_event_interruptible (stv680->wq, (stv680->scratch[stv680->scratch_use].state == BUFFER_READY));

		if (stv680->nullpackets > STV680_MAX_NULLPACKETS) {
			stv680->nullpackets = 0;
			PDEBUG (2, "STV(i): too many null length packets, restarting capture");
			stv680_stop_stream (stv680);
			stv680_start_stream (stv680);
		} else {
			if (stv680->scratch[stv680->scratch_use].state != BUFFER_READY) {
				stv680->frame[framenr].grabstate = FRAME_ERROR;
				PDEBUG (2, "STV(e): FRAME_ERROR in _newframe");
				return -EIO;
			}
			stv680->scratch[stv680->scratch_use].state = BUFFER_BUSY;

			bayer_unshuffle (stv680, &stv680->scratch[stv680->scratch_use]);

			stv680->scratch[stv680->scratch_use].state = BUFFER_UNUSED;
			stv680->scratch_use++;
			if (stv680->scratch_use >= STV680_NUMSCRATCH)
				stv680->scratch_use = 0;
			if (errors > STV680_MAX_ERRORS) {
				errors = 0;
				PDEBUG (2, "STV(i): too many errors, restarting capture");
				stv680_stop_stream (stv680);
				stv680_start_stream (stv680);
			}
		}		/* else */
	}			/* while */
	return 0;
}

/*********************************************************************
 * Video4Linux
 *********************************************************************/

static int stv_open (struct inode *inode, struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct usb_stv *stv680 = video_get_drvdata(dev);
	int err = 0;

	/* we are called with the BKL held */
	stv680->user = 1;
	err = stv_init (stv680);	/* main initialization routine for camera */

	if (err >= 0) {
		stv680->fbuf = rvmalloc (stv680->maxframesize * STV680_NUMFRAMES);
		if (!stv680->fbuf) {
			PDEBUG (0, "STV(e): Could not rvmalloc frame bufer");
			err = -ENOMEM;
		}
		file->private_data = dev;
	}
	if (err)
		stv680->user = 0;

	return err;
}

static int stv_close (struct inode *inode, struct file *file)
{
	struct video_device *dev = file->private_data;
	struct usb_stv *stv680 = video_get_drvdata(dev);
	int i;

	for (i = 0; i < STV680_NUMFRAMES; i++)
		stv680->frame[i].grabstate = FRAME_UNUSED;
	if (stv680->streaming)
		stv680_stop_stream (stv680);

	if ((i = stv_stop_video (stv680)) < 0)
		PDEBUG (1, "STV(e): stop_video failed in stv_close");

	rvfree (stv680->fbuf, stv680->maxframesize * STV680_NUMFRAMES);
	stv680->user = 0;

	if (stv680->removed) {
		kfree(stv680);
		stv680 = NULL;
		PDEBUG (0, "STV(i): device unregistered");
	}
	file->private_data = NULL;
	return 0;
}

static int stv680_do_ioctl (struct inode *inode, struct file *file,
			    unsigned int cmd, void *arg)
{
	struct video_device *vdev = file->private_data;
	struct usb_stv *stv680 = video_get_drvdata(vdev);

	if (!stv680->udev)
		return -EIO;

	switch (cmd) {
	case VIDIOCGCAP:{
			struct video_capability *b = arg;

			strcpy (b->name, stv680->camera_name);
			b->type = VID_TYPE_CAPTURE;
			b->channels = 1;
			b->audios = 0;
			b->maxwidth = stv680->maxwidth;
			b->maxheight = stv680->maxheight;
			b->minwidth = stv680->maxwidth / 2;
			b->minheight = stv680->maxheight / 2;
			return 0;
		}
	case VIDIOCGCHAN:{
			struct video_channel *v = arg;

			if (v->channel != 0)
				return -EINVAL;
			v->flags = 0;
			v->tuners = 0;
			v->type = VIDEO_TYPE_CAMERA;
			strcpy (v->name, "STV Camera");
			return 0;
		}
	case VIDIOCSCHAN:{
			struct video_channel *v = arg;
			if (v->channel != 0)
				return -EINVAL;
			return 0;
		}
	case VIDIOCGPICT:{
			struct video_picture *p = arg;

			stv680_get_pict (stv680, p);
			return 0;
		}
	case VIDIOCSPICT:{
			struct video_picture *p = arg;

			if (stv680_set_pict (stv680, p))
				return -EINVAL;
			return 0;
		}
	case VIDIOCSWIN:{
			struct video_window *vw = arg;

			if (vw->flags)
				return -EINVAL;
			if (vw->clipcount)
				return -EINVAL;
			if (vw->width != stv680->vwidth) {
				if (stv680_set_size (stv680, vw->width, vw->height)) {
					PDEBUG (2, "STV(e): failed (from user) set size in VIDIOCSWIN");
					return -EINVAL;
				}
			}
			return 0;
		}
	case VIDIOCGWIN:{
			struct video_window *vw = arg;

			vw->x = 0;	/* FIXME */
			vw->y = 0;
			vw->chromakey = 0;
			vw->flags = 0;
			vw->clipcount = 0;
			vw->width = stv680->vwidth;
			vw->height = stv680->vheight;
			return 0;
		}
	case VIDIOCGMBUF:{
			struct video_mbuf *vm = arg;
			int i;

			memset (vm, 0, sizeof (*vm));
			vm->size = STV680_NUMFRAMES * stv680->maxframesize;
			vm->frames = STV680_NUMFRAMES;
			for (i = 0; i < STV680_NUMFRAMES; i++)
				vm->offsets[i] = stv680->maxframesize * i;
			return 0;
		}
	case VIDIOCMCAPTURE:{
			struct video_mmap *vm = arg;

			if (vm->format != STV_VIDEO_PALETTE) {
				PDEBUG (2, "STV(i): VIDIOCMCAPTURE vm.format (%i) != VIDEO_PALETTE (%i)",
					vm->format, STV_VIDEO_PALETTE);
				if ((vm->format == 3) && (swapRGB_on == 0))  {
					PDEBUG (2, "STV(i): VIDIOCMCAPTURE swapRGB is (auto) ON");
					/* this may fix those apps (e.g., xawtv) that want BGR */
					swapRGB = 1;
				}
				return -EINVAL;
			}
			if (vm->frame >= STV680_NUMFRAMES) {
				PDEBUG (2, "STV(e): VIDIOCMCAPTURE vm.frame > NUMFRAMES");
				return -EINVAL;
			}
			if ((stv680->frame[vm->frame].grabstate == FRAME_ERROR)
			    || (stv680->frame[vm->frame].grabstate == FRAME_GRABBING)) {
				PDEBUG (2, "STV(e): VIDIOCMCAPTURE grabstate (%i) error",
					stv680->frame[vm->frame].grabstate);
				return -EBUSY;
			}
			/* Is this according to the v4l spec??? */
			if (stv680->vwidth != vm->width) {
				if (stv680_set_size (stv680, vm->width, vm->height)) {
					PDEBUG (2, "STV(e): VIDIOCMCAPTURE set_size failed");
					return -EINVAL;
				}
			}
			stv680->frame[vm->frame].grabstate = FRAME_READY;

			if (!stv680->streaming)
				stv680_start_stream (stv680);

			return 0;
		}
	case VIDIOCSYNC:{
			int *frame = arg;
			int ret = 0;

			if (*frame < 0 || *frame >= STV680_NUMFRAMES) {
				PDEBUG (2, "STV(e): Bad frame # in VIDIOCSYNC");
				return -EINVAL;
			}
			ret = stv680_newframe (stv680, *frame);
			stv680->frame[*frame].grabstate = FRAME_UNUSED;
			return ret;
		}
	case VIDIOCGFBUF:{
			struct video_buffer *vb = arg;

			memset (vb, 0, sizeof (*vb));
			return 0;
		}
	case VIDIOCKEY:
		return 0;
	case VIDIOCCAPTURE:
		{
			PDEBUG (2, "STV(e): VIDIOCCAPTURE failed");
			return -EINVAL;
		}
	case VIDIOCSFBUF:
	case VIDIOCGTUNER:
	case VIDIOCSTUNER:
	case VIDIOCGFREQ:
	case VIDIOCSFREQ:
	case VIDIOCGAUDIO:
	case VIDIOCSAUDIO:
		return -EINVAL;
	default:
		return -ENOIOCTLCMD;
	}			/* end switch */

	return 0;
}

static int stv680_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, stv680_do_ioctl);
}

static int stv680_mmap (struct file *file, struct vm_area_struct *vma)
{
	struct video_device *dev = file->private_data;
	struct usb_stv *stv680 = video_get_drvdata(dev);
	unsigned long start = vma->vm_start;
	unsigned long size  = vma->vm_end-vma->vm_start;
	unsigned long page, pos;

	mutex_lock(&stv680->lock);

	if (stv680->udev == NULL) {
		mutex_unlock(&stv680->lock);
		return -EIO;
	}
	if (size > (((STV680_NUMFRAMES * stv680->maxframesize) + PAGE_SIZE - 1)
		    & ~(PAGE_SIZE - 1))) {
		mutex_unlock(&stv680->lock);
		return -EINVAL;
	}
	pos = (unsigned long) stv680->fbuf;
	while (size > 0) {
		page = vmalloc_to_pfn((void *)pos);
		if (remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED)) {
			mutex_unlock(&stv680->lock);
			return -EAGAIN;
		}
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}
	mutex_unlock(&stv680->lock);

	return 0;
}

static ssize_t stv680_read (struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct video_device *dev = file->private_data;
	unsigned long int realcount = count;
	int ret = 0;
	struct usb_stv *stv680 = video_get_drvdata(dev);
	unsigned long int i;

	if (STV680_NUMFRAMES != 2) {
		PDEBUG (0, "STV(e): STV680_NUMFRAMES needs to be 2!");
		return -1;
	}
	if (stv680->udev == NULL)
		return -EIO;
	if (realcount > (stv680->vwidth * stv680->vheight * 3))
		realcount = stv680->vwidth * stv680->vheight * 3;

	/* Shouldn't happen: */
	if (stv680->frame[0].grabstate == FRAME_GRABBING) {
		PDEBUG (2, "STV(e): FRAME_GRABBING in stv680_read");
		return -EBUSY;
	}
	stv680->frame[0].grabstate = FRAME_READY;
	stv680->frame[1].grabstate = FRAME_UNUSED;
	stv680->curframe = 0;

	if (!stv680->streaming)
		stv680_start_stream (stv680);

	if (!stv680->streaming) {
		ret = stv680_newframe (stv680, 0);	/* ret should = 0 */
	}

	ret = stv680_newframe (stv680, 0);

	if (!ret) {
		if ((i = copy_to_user (buf, stv680->frame[0].data, realcount)) != 0) {
			PDEBUG (2, "STV(e): copy_to_user frame 0 failed, ret count = %li", i);
			return -EFAULT;
		}
	} else {
		realcount = ret;
	}
	stv680->frame[0].grabstate = FRAME_UNUSED;
	return realcount;
}				/* stv680_read */

static const struct file_operations stv680_fops = {
	.owner =	THIS_MODULE,
	.open =		stv_open,
	.release =     	stv_close,
	.read =		stv680_read,
	.mmap =		stv680_mmap,
	.ioctl =        stv680_ioctl,
	.compat_ioctl = v4l_compat_ioctl32,
	.llseek =       no_llseek,
};
static struct video_device stv680_template = {
	.owner =	THIS_MODULE,
	.name =		"STV0680 USB camera",
	.type =		VID_TYPE_CAPTURE,
	.fops =         &stv680_fops,
	.release =	video_device_release,
	.minor = 	-1,
};

static int stv680_probe (struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface;
	struct usb_stv *stv680 = NULL;
	char *camera_name = NULL;
	int retval = 0;

	/* We don't handle multi-config cameras */
	if (dev->descriptor.bNumConfigurations != 1) {
		PDEBUG (0, "STV(e): Number of Configurations != 1");
		return -ENODEV;
	}

	interface = &intf->altsetting[0];
	/* Is it a STV680? */
	if ((le16_to_cpu(dev->descriptor.idVendor) == USB_PENCAM_VENDOR_ID) &&
	    (le16_to_cpu(dev->descriptor.idProduct) == USB_PENCAM_PRODUCT_ID)) {
		camera_name = "STV0680";
		PDEBUG (0, "STV(i): STV0680 camera found.");
	} else if ((le16_to_cpu(dev->descriptor.idVendor) == USB_CREATIVEGOMINI_VENDOR_ID) &&
		   (le16_to_cpu(dev->descriptor.idProduct) == USB_CREATIVEGOMINI_PRODUCT_ID)) {
		camera_name = "Creative WebCam Go Mini";
		PDEBUG (0, "STV(i): Creative WebCam Go Mini found.");
	} else {
		PDEBUG (0, "STV(e): Vendor/Product ID do not match STV0680 or Creative WebCam Go Mini values.");
		PDEBUG (0, "STV(e): Check that the STV0680 or Creative WebCam Go Mini camera is connected to the computer.");
		retval = -ENODEV;
		goto error;
	}
	/* We found one */
	if ((stv680 = kzalloc (sizeof (*stv680), GFP_KERNEL)) == NULL) {
		PDEBUG (0, "STV(e): couldn't kmalloc stv680 struct.");
		retval = -ENOMEM;
		goto error;
	}

	stv680->udev = dev;
	stv680->camera_name = camera_name;

	stv680->vdev = video_device_alloc();
	if (!stv680->vdev) {
		retval = -ENOMEM;
		goto error;
	}
	memcpy(stv680->vdev, &stv680_template, sizeof(stv680_template));
	stv680->vdev->dev = &intf->dev;
	video_set_drvdata(stv680->vdev, stv680);

	memcpy (stv680->vdev->name, stv680->camera_name, strlen (stv680->camera_name));
	init_waitqueue_head (&stv680->wq);
	mutex_init (&stv680->lock);
	wmb ();

	if (video_register_device (stv680->vdev, VFL_TYPE_GRABBER, video_nr) == -1) {
		PDEBUG (0, "STV(e): video_register_device failed");
		retval = -EIO;
		goto error_vdev;
	}
	PDEBUG (0, "STV(i): registered new video device: video%d", stv680->vdev->minor);

	usb_set_intfdata (intf, stv680);
	retval = stv680_create_sysfs_files(stv680->vdev);
	if (retval)
		goto error_unreg;
	return 0;

error_unreg:
	video_unregister_device(stv680->vdev);
error_vdev:
	video_device_release(stv680->vdev);
error:
	kfree(stv680);
	return retval;
}

static inline void usb_stv680_remove_disconnected (struct usb_stv *stv680)
{
	int i;

	stv680->udev = NULL;
	stv680->frame[0].grabstate = FRAME_ERROR;
	stv680->frame[1].grabstate = FRAME_ERROR;
	stv680->streaming = 0;

	wake_up_interruptible (&stv680->wq);

	for (i = 0; i < STV680_NUMSBUF; i++)
		if (stv680->urb[i]) {
			usb_kill_urb (stv680->urb[i]);
			usb_free_urb (stv680->urb[i]);
			stv680->urb[i] = NULL;
			kfree(stv680->sbuf[i].data);
		}
	for (i = 0; i < STV680_NUMSCRATCH; i++)
		kfree(stv680->scratch[i].data);
	PDEBUG (0, "STV(i): %s disconnected", stv680->camera_name);

	/* Free the memory */
	kfree(stv680);
}

static void stv680_disconnect (struct usb_interface *intf)
{
	struct usb_stv *stv680 = usb_get_intfdata (intf);

	usb_set_intfdata (intf, NULL);

	if (stv680) {
		/* We don't want people trying to open up the device */
		if (stv680->vdev) {
			stv680_remove_sysfs_files(stv680->vdev);
			video_unregister_device(stv680->vdev);
			stv680->vdev = NULL;
		}
		if (!stv680->user) {
			usb_stv680_remove_disconnected (stv680);
		} else {
			stv680->removed = 1;
		}
	}
}

static struct usb_driver stv680_driver = {
	.name =		"stv680",
	.probe =	stv680_probe,
	.disconnect =	stv680_disconnect,
	.id_table =	device_table
};

/********************************************************************
 *  Module routines
 ********************************************************************/

static int __init usb_stv680_init (void)
{
	if (usb_register (&stv680_driver) < 0) {
		PDEBUG (0, "STV(e): Could not setup STV0680 driver");
		return -1;
	}
	PDEBUG (0, "STV(i): usb camera driver version %s registering", DRIVER_VERSION);

	info(DRIVER_DESC " " DRIVER_VERSION);
	return 0;
}

static void __exit usb_stv680_exit (void)
{
	usb_deregister (&stv680_driver);
	PDEBUG (0, "STV(i): driver deregistered");
}

module_init (usb_stv680_init);
module_exit (usb_stv680_exit);

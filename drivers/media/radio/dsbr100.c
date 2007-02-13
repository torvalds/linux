/* A driver for the D-Link DSB-R100 USB radio.  The R100 plugs
 into both the USB and an analog audio input, so this thing
 only deals with initialisation and frequency setting, the
 audio data has to be handled by a sound driver.

 Major issue: I can't find out where the device reports the signal
 strength, and indeed the windows software appearantly just looks
 at the stereo indicator as well.  So, scanning will only find
 stereo stations.  Sad, but I can't help it.

 Also, the windows program sends oodles of messages over to the
 device, and I couldn't figure out their meaning.  My suspicion
 is that they don't have any:-)

 You might find some interesting stuff about this module at
 http://unimut.fsk.uni-heidelberg.de/unimut/demi/dsbr

 Copyright (c) 2000 Markus Demleitner <msdemlei@cl.uni-heidelberg.de>

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
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

 History:

 Version 0.41-ac1:
	Alan Cox: Some cleanups and fixes

 Version 0.41:
	Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>

 Version 0.40:
	Markus: Updates for 2.6.x kernels, code layout changes, name sanitizing

 Version 0.30:
	Markus: Updates for 2.5.x kernel and more ISO compliant source

 Version 0.25:
	PSL and Markus: Cleanup, radio now doesn't stop on device close

 Version 0.24:
	Markus: Hope I got these silly VIDEO_TUNER_LOW issues finally
	right.  Some minor cleanup, improved standalone compilation

 Version 0.23:
	Markus: Sign extension bug fixed by declaring transfer_buffer unsigned

 Version 0.22:
	Markus: Some (brown bag) cleanup in what VIDIOCSTUNER returns,
	thanks to Mike Cox for pointing the problem out.

 Version 0.21:
	Markus: Minor cleanup, warnings if something goes wrong, lame attempt
	to adhere to Documentation/CodingStyle

 Version 0.2:
	Brad Hards <bradh@dynamite.com.au>: Fixes to make it work as non-module
	Markus: Copyright clarification

 Version 0.01: Markus: initial release

*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <linux/usb.h>
#include <linux/smp_lock.h>

/*
 * Version Information
 */
#include <linux/version.h>	/* for KERNEL_VERSION MACRO	*/

#define DRIVER_VERSION "v0.41"
#define RADIO_VERSION KERNEL_VERSION(0,4,1)

static struct v4l2_queryctrl radio_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.default_value = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	}
};

#define DRIVER_AUTHOR "Markus Demleitner <msdemlei@tucana.harvard.edu>"
#define DRIVER_DESC "D-Link DSB-R100 USB FM radio driver"

#define DSB100_VENDOR 0x04b4
#define DSB100_PRODUCT 0x1002

/* Commands the device appears to understand */
#define DSB100_TUNE 1
#define DSB100_ONOFF 2

#define TB_LEN 16

/* Frequency limits in MHz -- these are European values.  For Japanese
devices, that would be 76 and 91.  */
#define FREQ_MIN  87.5
#define FREQ_MAX 108.0
#define FREQ_MUL 16000


static int usb_dsbr100_probe(struct usb_interface *intf,
			     const struct usb_device_id *id);
static void usb_dsbr100_disconnect(struct usb_interface *intf);
static int usb_dsbr100_ioctl(struct inode *inode, struct file *file,
			     unsigned int cmd, unsigned long arg);
static int usb_dsbr100_open(struct inode *inode, struct file *file);
static int usb_dsbr100_close(struct inode *inode, struct file *file);

static int radio_nr = -1;
module_param(radio_nr, int, 0);

/* Data for one (physical) device */
struct dsbr100_device {
	struct usb_device *usbdev;
	struct video_device *videodev;
	unsigned char transfer_buffer[TB_LEN];
	int curfreq;
	int stereo;
	int users;
	int removed;
	int muted;
};


/* File system interface */
static const struct file_operations usb_dsbr100_fops = {
	.owner =	THIS_MODULE,
	.open =		usb_dsbr100_open,
	.release =     	usb_dsbr100_close,
	.ioctl =        usb_dsbr100_ioctl,
	.compat_ioctl = v4l_compat_ioctl32,
	.llseek =       no_llseek,
};

/* V4L interface */
static struct video_device dsbr100_videodev_template=
{
	.owner =	THIS_MODULE,
	.name =		"D-Link DSB-R 100",
	.type =		VID_TYPE_TUNER,
	.fops =         &usb_dsbr100_fops,
	.release = video_device_release,
};

static struct usb_device_id usb_dsbr100_device_table [] = {
	{ USB_DEVICE(DSB100_VENDOR, DSB100_PRODUCT) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usb_dsbr100_device_table);

/* USB subsystem interface */
static struct usb_driver usb_dsbr100_driver = {
	.name =		"dsbr100",
	.probe =	usb_dsbr100_probe,
	.disconnect =	usb_dsbr100_disconnect,
	.id_table =	usb_dsbr100_device_table,
};

/* Low-level device interface begins here */

/* switch on radio */
static int dsbr100_start(struct dsbr100_device *radio)
{
	if (usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			USB_REQ_GET_STATUS,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			0x00, 0xC7, radio->transfer_buffer, 8, 300)<0 ||
	usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			DSB100_ONOFF,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			0x01, 0x00, radio->transfer_buffer, 8, 300)<0)
		return -1;
	radio->muted=0;
	return (radio->transfer_buffer)[0];
}


/* switch off radio */
static int dsbr100_stop(struct dsbr100_device *radio)
{
	if (usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			USB_REQ_GET_STATUS,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			0x16, 0x1C, radio->transfer_buffer, 8, 300)<0 ||
	usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			DSB100_ONOFF,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			0x00, 0x00, radio->transfer_buffer, 8, 300)<0)
		return -1;
	radio->muted=1;
	return (radio->transfer_buffer)[0];
}

/* set a frequency, freq is defined by v4l's TUNER_LOW, i.e. 1/16th kHz */
static int dsbr100_setfreq(struct dsbr100_device *radio, int freq)
{
	freq = (freq/16*80)/1000+856;
	if (usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			DSB100_TUNE,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			(freq>>8)&0x00ff, freq&0xff,
			radio->transfer_buffer, 8, 300)<0 ||
	   usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			USB_REQ_GET_STATUS,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			0x96, 0xB7, radio->transfer_buffer, 8, 300)<0 ||
	usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			USB_REQ_GET_STATUS,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE |  USB_DIR_IN,
			0x00, 0x24, radio->transfer_buffer, 8, 300)<0) {
		radio->stereo = -1;
		return -1;
	}
	radio->stereo = ! ((radio->transfer_buffer)[0]&0x01);
	return (radio->transfer_buffer)[0];
}

/* return the device status.  This is, in effect, just whether it
sees a stereo signal or not.  Pity. */
static void dsbr100_getstat(struct dsbr100_device *radio)
{
	if (usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
		USB_REQ_GET_STATUS,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
		0x00 , 0x24, radio->transfer_buffer, 8, 300)<0)
		radio->stereo = -1;
	else
		radio->stereo = ! (radio->transfer_buffer[0]&0x01);
}


/* USB subsystem interface begins here */

/* check if the device is present and register with v4l and
usb if it is */
static int usb_dsbr100_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct dsbr100_device *radio;

	if (!(radio = kmalloc(sizeof(struct dsbr100_device), GFP_KERNEL)))
		return -ENOMEM;
	if (!(radio->videodev = video_device_alloc())) {
		kfree(radio);
		return -ENOMEM;
	}
	memcpy(radio->videodev, &dsbr100_videodev_template,
		sizeof(dsbr100_videodev_template));
	radio->removed = 0;
	radio->users = 0;
	radio->usbdev = interface_to_usbdev(intf);
	radio->curfreq = FREQ_MIN*FREQ_MUL;
	video_set_drvdata(radio->videodev, radio);
	if (video_register_device(radio->videodev, VFL_TYPE_RADIO,
		radio_nr)) {
		warn("Could not register video device");
		video_device_release(radio->videodev);
		kfree(radio);
		return -EIO;
	}
	usb_set_intfdata(intf, radio);
	return 0;
}

/* handle unplugging of the device, release data structures
if nothing keeps us from doing it.  If something is still
keeping us busy, the release callback of v4l will take care
of releasing it.  stv680.c does not relase its private
data, so I don't do this here either.  Checking out the
code I'd expect I better did that, but if there's a memory
leak here it's tiny (~50 bytes per disconnect) */
static void usb_dsbr100_disconnect(struct usb_interface *intf)
{
	struct dsbr100_device *radio = usb_get_intfdata(intf);

	usb_set_intfdata (intf, NULL);
	if (radio) {
		video_unregister_device(radio->videodev);
		radio->videodev = NULL;
		if (radio->users) {
			kfree(radio);
		} else {
			radio->removed = 1;
		}
	}
}


/* Video for Linux interface */

static int usb_dsbr100_do_ioctl(struct inode *inode, struct file *file,
				unsigned int cmd, void *arg)
{
	struct dsbr100_device *radio=video_get_drvdata(video_devdata(file));

	if (!radio)
		return -EIO;

	switch(cmd) {
		case VIDIOC_QUERYCAP:
		{
			struct v4l2_capability *v = arg;
			memset(v,0,sizeof(*v));
			strlcpy(v->driver, "dsbr100", sizeof (v->driver));
			strlcpy(v->card, "D-Link R-100 USB FM Radio", sizeof (v->card));
			sprintf(v->bus_info,"ISA");
			v->version = RADIO_VERSION;
			v->capabilities = V4L2_CAP_TUNER;

			return 0;
		}
		case VIDIOC_G_TUNER:
		{
			struct v4l2_tuner *v = arg;

			if (v->index > 0)
				return -EINVAL;

			dsbr100_getstat(radio);

			memset(v,0,sizeof(*v));
			strcpy(v->name, "FM");
			v->type = V4L2_TUNER_RADIO;

			v->rangelow = FREQ_MIN*FREQ_MUL;
			v->rangehigh = FREQ_MAX*FREQ_MUL;
			v->rxsubchans =V4L2_TUNER_SUB_MONO|V4L2_TUNER_SUB_STEREO;
			v->capability=V4L2_TUNER_CAP_LOW;
			if(radio->stereo)
				v->audmode = V4L2_TUNER_MODE_STEREO;
			else
				v->audmode = V4L2_TUNER_MODE_MONO;
			v->signal = 0xFFFF;     /* We can't get the signal strength */

			return 0;
		}
		case VIDIOC_S_TUNER:
		{
			struct v4l2_tuner *v = arg;

			if (v->index > 0)
				return -EINVAL;

			return 0;
		}
		case VIDIOC_S_FREQUENCY:
		{
			struct v4l2_frequency *f = arg;

			radio->curfreq = f->frequency;
			if (dsbr100_setfreq(radio, radio->curfreq)==-1)
				warn("Set frequency failed");
			return 0;
		}
		case VIDIOC_G_FREQUENCY:
		{
			struct v4l2_frequency *f = arg;

			f->type = V4L2_TUNER_RADIO;
			f->frequency = radio->curfreq;

			return 0;
		}
		case VIDIOC_QUERYCTRL:
		{
			struct v4l2_queryctrl *qc = arg;
			int i;

			for (i = 0; i < ARRAY_SIZE(radio_qctrl); i++) {
				if (qc->id && qc->id == radio_qctrl[i].id) {
					memcpy(qc, &(radio_qctrl[i]),
								sizeof(*qc));
					return 0;
				}
			}
			return -EINVAL;
		}
		case VIDIOC_G_CTRL:
		{
			struct v4l2_control *ctrl= arg;

			switch (ctrl->id) {
			case V4L2_CID_AUDIO_MUTE:
				ctrl->value=radio->muted;
				return 0;
			}
			return -EINVAL;
		}
		case VIDIOC_S_CTRL:
		{
			struct v4l2_control *ctrl= arg;

			switch (ctrl->id) {
			case V4L2_CID_AUDIO_MUTE:
				if (ctrl->value) {
					if (dsbr100_stop(radio)==-1)
						warn("Radio did not respond properly");
				} else {
					if (dsbr100_start(radio)==-1)
						warn("Radio did not respond properly");
				}
				return 0;
			}
			return -EINVAL;
		}
		default:
			return v4l_compat_translate_ioctl(inode,file,cmd,arg,
							  usb_dsbr100_do_ioctl);
	}
}

static int usb_dsbr100_ioctl(struct inode *inode, struct file *file,
			     unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, usb_dsbr100_do_ioctl);
}

static int usb_dsbr100_open(struct inode *inode, struct file *file)
{
	struct dsbr100_device *radio=video_get_drvdata(video_devdata(file));

	radio->users = 1;
	radio->muted = 1;

	if (dsbr100_start(radio)<0) {
		warn("Radio did not start up properly");
		radio->users = 0;
		return -EIO;
	}
	dsbr100_setfreq(radio, radio->curfreq);
	return 0;
}

static int usb_dsbr100_close(struct inode *inode, struct file *file)
{
	struct dsbr100_device *radio=video_get_drvdata(video_devdata(file));

	if (!radio)
		return -ENODEV;
	radio->users = 0;
	if (radio->removed) {
		kfree(radio);
	}
	return 0;
}

static int __init dsbr100_init(void)
{
	int retval = usb_register(&usb_dsbr100_driver);
	info(DRIVER_VERSION ":" DRIVER_DESC);
	return retval;
}

static void __exit dsbr100_exit(void)
{
	usb_deregister(&usb_dsbr100_driver);
}

module_init (dsbr100_init);
module_exit (dsbr100_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

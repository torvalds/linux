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
#include <linux/videodev.h>
#include <linux/usb.h>
#include <linux/smp_lock.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.40"
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
typedef struct {
	struct usb_device *usbdev;
	struct video_device *videodev;
	unsigned char transfer_buffer[TB_LEN];
	int curfreq;
	int stereo;
	int users;
	int removed;
} dsbr100_device;


/* File system interface */
static struct file_operations usb_dsbr100_fops = {
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
	.hardware =	VID_HARDWARE_AZTECH,
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
static int dsbr100_start(dsbr100_device *radio)
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
	return (radio->transfer_buffer)[0];
}


/* switch off radio */
static int dsbr100_stop(dsbr100_device *radio)
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
	return (radio->transfer_buffer)[0];
}

/* set a frequency, freq is defined by v4l's TUNER_LOW, i.e. 1/16th kHz */
static int dsbr100_setfreq(dsbr100_device *radio, int freq)
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
static void dsbr100_getstat(dsbr100_device *radio)
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
	dsbr100_device *radio;

	if (!(radio = kmalloc(sizeof(dsbr100_device), GFP_KERNEL)))
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
	dsbr100_device *radio = usb_get_intfdata(intf);

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
	dsbr100_device *radio=video_get_drvdata(video_devdata(file));

	if (!radio)
		return -EIO;

	switch(cmd) {
		case VIDIOCGCAP: {
			struct video_capability *v = arg;

			memset(v, 0, sizeof(*v));
			v->type = VID_TYPE_TUNER;
			v->channels = 1;
			v->audios = 1;
			strcpy(v->name, "D-Link R-100 USB FM Radio");
			return 0;
		}
		case VIDIOCGTUNER: {
			struct video_tuner *v = arg;

			dsbr100_getstat(radio);
			if(v->tuner)	/* Only 1 tuner */
				return -EINVAL;
			v->rangelow = FREQ_MIN*FREQ_MUL;
			v->rangehigh = FREQ_MAX*FREQ_MUL;
			v->flags = VIDEO_TUNER_LOW;
			v->mode = VIDEO_MODE_AUTO;
			v->signal = radio->stereo*0x7000;
				/* Don't know how to get signal strength */
			v->flags |= VIDEO_TUNER_STEREO_ON*radio->stereo;
			strcpy(v->name, "DSB R-100");
			return 0;
		}
		case VIDIOCSTUNER: {
			struct video_tuner *v = arg;

			if(v->tuner!=0)
				return -EINVAL;
			/* Only 1 tuner so no setting needed ! */
			return 0;
		}
		case VIDIOCGFREQ: {
			int *freq = arg;

			if (radio->curfreq==-1)
				return -EINVAL;
			*freq = radio->curfreq;
			return 0;
		}
		case VIDIOCSFREQ: {
			int *freq = arg;

			radio->curfreq = *freq;
			if (dsbr100_setfreq(radio, radio->curfreq)==-1)
				warn("Set frequency failed");
			return 0;
		}
		case VIDIOCGAUDIO: {
			struct video_audio *v = arg;

			memset(v, 0, sizeof(*v));
			v->flags |= VIDEO_AUDIO_MUTABLE;
			v->mode = VIDEO_SOUND_STEREO;
			v->volume = 1;
			v->step = 1;
			strcpy(v->name, "Radio");
			return 0;
		}
		case VIDIOCSAUDIO: {
			struct video_audio *v = arg;

			if (v->audio)
				return -EINVAL;
			if (v->flags&VIDEO_AUDIO_MUTE) {
				if (dsbr100_stop(radio)==-1)
					warn("Radio did not respond properly");
			}
			else
				if (dsbr100_start(radio)==-1)
					warn("Radio did not respond properly");
			return 0;
		}
		default:
			return -ENOIOCTLCMD;
	}
}

static int usb_dsbr100_ioctl(struct inode *inode, struct file *file,
			     unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, usb_dsbr100_do_ioctl);
}

static int usb_dsbr100_open(struct inode *inode, struct file *file)
{
	dsbr100_device *radio=video_get_drvdata(video_devdata(file));

	radio->users = 1;
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
	dsbr100_device *radio=video_get_drvdata(video_devdata(file));

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

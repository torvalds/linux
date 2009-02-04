/*
 * A driver for the AverMedia MR 800 USB FM radio. This device plugs
 * into both the USB and an analog audio input, so this thing
 * only deals with initialization and frequency setting, the
 * audio data has to be handled by a sound driver.
 *
 * Copyright (c) 2008 Alexey Klimov <klimov.linux@gmail.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/*
 * Big thanks to authors of dsbr100.c and radio-si470x.c
 *
 * When work was looked pretty good, i discover this:
 * http://av-usbradio.sourceforge.net/index.php
 * http://sourceforge.net/projects/av-usbradio/
 * Latest release of theirs project was in 2005.
 * Probably, this driver could be improved trough using their
 * achievements (specifications given).
 * So, we have smth to begin with.
 *
 * History:
 * Version 0.01:	First working version.
 * 			It's required to blacklist AverMedia USB Radio
 * 			in usbhid/hid-quirks.c
 *
 * Many things to do:
 * 	- Correct power managment of device (suspend & resume)
 * 	- Make x86 independance (little-endian and big-endian stuff)
 * 	- Add code for scanning and smooth tuning
 * 	- Checked and add stereo&mono stuff
 * 	- Add code for sensitivity value
 * 	- Correct mistakes
 * 	- In Japan another FREQ_MIN and FREQ_MAX
 */

/* kernel includes */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <linux/usb.h>
#include <linux/version.h>	/* for KERNEL_VERSION MACRO */

/* driver and module definitions */
#define DRIVER_AUTHOR "Alexey Klimov <klimov.linux@gmail.com>"
#define DRIVER_DESC "AverMedia MR 800 USB FM radio driver"
#define DRIVER_VERSION "0.01"
#define RADIO_VERSION KERNEL_VERSION(0, 0, 1)

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define USB_AMRADIO_VENDOR 0x07ca
#define USB_AMRADIO_PRODUCT 0xb800

/* dev_warn macro with driver name */
#define MR800_DRIVER_NAME "radio-mr800"
#define amradio_dev_warn(dev, fmt, arg...)				\
		dev_warn(dev, MR800_DRIVER_NAME " - " fmt, ##arg)

/* Probably USB_TIMEOUT should be modified in module parameter */
#define BUFFER_LENGTH 8
#define USB_TIMEOUT 500

/* Frequency limits in MHz -- these are European values.  For Japanese
devices, that would be 76 and 91.  */
#define FREQ_MIN  87.5
#define FREQ_MAX 108.0
#define FREQ_MUL 16000

/* module parameter */
static int radio_nr = -1;
module_param(radio_nr, int, 0);
MODULE_PARM_DESC(radio_nr, "Radio Nr");

static struct v4l2_queryctrl radio_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.step	       = 1,
		.default_value = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},
/* HINT: the disabled controls are only here to satify kradio and such apps */
	{	.id		= V4L2_CID_AUDIO_VOLUME,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
	{
		.id		= V4L2_CID_AUDIO_BALANCE,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
	{
		.id		= V4L2_CID_AUDIO_BASS,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
	{
		.id		= V4L2_CID_AUDIO_TREBLE,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
	{
		.id		= V4L2_CID_AUDIO_LOUDNESS,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
};

static int usb_amradio_probe(struct usb_interface *intf,
			     const struct usb_device_id *id);
static void usb_amradio_disconnect(struct usb_interface *intf);
static int usb_amradio_open(struct file *file);
static int usb_amradio_close(struct file *file);
static int usb_amradio_suspend(struct usb_interface *intf,
				pm_message_t message);
static int usb_amradio_resume(struct usb_interface *intf);

/* Data for one (physical) device */
struct amradio_device {
	/* reference to USB and video device */
	struct usb_device *usbdev;
	struct video_device *videodev;

	unsigned char *buffer;
	struct mutex lock;	/* buffer locking */
	int curfreq;
	int stereo;
	int users;
	int removed;
	int muted;
};

/* USB Device ID List */
static struct usb_device_id usb_amradio_device_table[] = {
	{USB_DEVICE_AND_INTERFACE_INFO(USB_AMRADIO_VENDOR, USB_AMRADIO_PRODUCT,
							USB_CLASS_HID, 0, 0) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, usb_amradio_device_table);

/* USB subsystem interface */
static struct usb_driver usb_amradio_driver = {
	.name			= MR800_DRIVER_NAME,
	.probe			= usb_amradio_probe,
	.disconnect		= usb_amradio_disconnect,
	.suspend		= usb_amradio_suspend,
	.resume			= usb_amradio_resume,
	.reset_resume		= usb_amradio_resume,
	.id_table		= usb_amradio_device_table,
	.supports_autosuspend	= 0,
};

/* switch on radio. Send 8 bytes to device. */
static int amradio_start(struct amradio_device *radio)
{
	int retval;
	int size;

	mutex_lock(&radio->lock);

	radio->buffer[0] = 0x00;
	radio->buffer[1] = 0x55;
	radio->buffer[2] = 0xaa;
	radio->buffer[3] = 0x00;
	radio->buffer[4] = 0xab;
	radio->buffer[5] = 0x00;
	radio->buffer[6] = 0x00;
	radio->buffer[7] = 0x00;

	retval = usb_bulk_msg(radio->usbdev, usb_sndintpipe(radio->usbdev, 2),
		(void *) (radio->buffer), BUFFER_LENGTH, &size, USB_TIMEOUT);

	if (retval) {
		mutex_unlock(&radio->lock);
		return retval;
	}

	radio->muted = 0;

	mutex_unlock(&radio->lock);

	return retval;
}

/* switch off radio */
static int amradio_stop(struct amradio_device *radio)
{
	int retval;
	int size;

	/* safety check */
	if (radio->removed)
		return -EIO;

	mutex_lock(&radio->lock);

	radio->buffer[0] = 0x00;
	radio->buffer[1] = 0x55;
	radio->buffer[2] = 0xaa;
	radio->buffer[3] = 0x00;
	radio->buffer[4] = 0xab;
	radio->buffer[5] = 0x01;
	radio->buffer[6] = 0x00;
	radio->buffer[7] = 0x00;

	retval = usb_bulk_msg(radio->usbdev, usb_sndintpipe(radio->usbdev, 2),
		(void *) (radio->buffer), BUFFER_LENGTH, &size, USB_TIMEOUT);

	if (retval) {
		mutex_unlock(&radio->lock);
		return retval;
	}

	radio->muted = 1;

	mutex_unlock(&radio->lock);

	return retval;
}

/* set a frequency, freq is defined by v4l's TUNER_LOW, i.e. 1/16th kHz */
static int amradio_setfreq(struct amradio_device *radio, int freq)
{
	int retval;
	int size;
	unsigned short freq_send = 0x13 + (freq >> 3) / 25;

	/* safety check */
	if (radio->removed)
		return -EIO;

	mutex_lock(&radio->lock);

	radio->buffer[0] = 0x00;
	radio->buffer[1] = 0x55;
	radio->buffer[2] = 0xaa;
	radio->buffer[3] = 0x03;
	radio->buffer[4] = 0xa4;
	radio->buffer[5] = 0x00;
	radio->buffer[6] = 0x00;
	radio->buffer[7] = 0x08;

	retval = usb_bulk_msg(radio->usbdev, usb_sndintpipe(radio->usbdev, 2),
		(void *) (radio->buffer), BUFFER_LENGTH, &size, USB_TIMEOUT);

	if (retval) {
		mutex_unlock(&radio->lock);
		return retval;
	}

	/* frequency is calculated from freq_send and placed in first 2 bytes */
	radio->buffer[0] = (freq_send >> 8) & 0xff;
	radio->buffer[1] = freq_send & 0xff;
	radio->buffer[2] = 0x01;
	radio->buffer[3] = 0x00;
	radio->buffer[4] = 0x00;
	/* 5 and 6 bytes of buffer already = 0x00 */
	radio->buffer[7] = 0x00;

	retval = usb_bulk_msg(radio->usbdev, usb_sndintpipe(radio->usbdev, 2),
		(void *) (radio->buffer), BUFFER_LENGTH, &size, USB_TIMEOUT);

	if (retval) {
		mutex_unlock(&radio->lock);
		return retval;
	}

	radio->stereo = 0;

	mutex_unlock(&radio->lock);

	return retval;
}

/* USB subsystem interface begins here */

/* handle unplugging of the device, release data structures
if nothing keeps us from doing it.  If something is still
keeping us busy, the release callback of v4l will take care
of releasing it. */
static void usb_amradio_disconnect(struct usb_interface *intf)
{
	struct amradio_device *radio = usb_get_intfdata(intf);

	mutex_lock(&radio->lock);
	radio->removed = 1;
	mutex_unlock(&radio->lock);

	usb_set_intfdata(intf, NULL);
	video_unregister_device(radio->videodev);
}

/* vidioc_querycap - query device capabilities */
static int vidioc_querycap(struct file *file, void *priv,
					struct v4l2_capability *v)
{
	strlcpy(v->driver, "radio-mr800", sizeof(v->driver));
	strlcpy(v->card, "AverMedia MR 800 USB FM Radio", sizeof(v->card));
	sprintf(v->bus_info, "USB");
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER;
	return 0;
}

/* vidioc_g_tuner - get tuner attributes */
static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	struct amradio_device *radio = video_get_drvdata(video_devdata(file));

	/* safety check */
	if (radio->removed)
		return -EIO;

	if (v->index > 0)
		return -EINVAL;

/* TODO: Add function which look is signal stereo or not
 * 	amradio_getstat(radio);
 */
	radio->stereo = -1;
	strcpy(v->name, "FM");
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = FREQ_MIN * FREQ_MUL;
	v->rangehigh = FREQ_MAX * FREQ_MUL;
	v->rxsubchans = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
	v->capability = V4L2_TUNER_CAP_LOW;
	if (radio->stereo)
		v->audmode = V4L2_TUNER_MODE_STEREO;
	else
		v->audmode = V4L2_TUNER_MODE_MONO;
	v->signal = 0xffff;     /* Can't get the signal strength, sad.. */
	v->afc = 0; /* Don't know what is this */
	return 0;
}

/* vidioc_s_tuner - set tuner attributes */
static int vidioc_s_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	struct amradio_device *radio = video_get_drvdata(video_devdata(file));

	/* safety check */
	if (radio->removed)
		return -EIO;

	if (v->index > 0)
		return -EINVAL;
	return 0;
}

/* vidioc_s_frequency - set tuner radio frequency */
static int vidioc_s_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct amradio_device *radio = video_get_drvdata(video_devdata(file));

	/* safety check */
	if (radio->removed)
		return -EIO;

	radio->curfreq = f->frequency;
	if (amradio_setfreq(radio, radio->curfreq) < 0)
		amradio_dev_warn(&radio->videodev->dev,
			"set frequency failed\n");
	return 0;
}

/* vidioc_g_frequency - get tuner radio frequency */
static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct amradio_device *radio = video_get_drvdata(video_devdata(file));

	/* safety check */
	if (radio->removed)
		return -EIO;

	f->type = V4L2_TUNER_RADIO;
	f->frequency = radio->curfreq;
	return 0;
}

/* vidioc_queryctrl - enumerate control items */
static int vidioc_queryctrl(struct file *file, void *priv,
				struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(radio_qctrl); i++) {
		if (qc->id && qc->id == radio_qctrl[i].id) {
			memcpy(qc, &(radio_qctrl[i]), sizeof(*qc));
			return 0;
		}
	}
	return -EINVAL;
}

/* vidioc_g_ctrl - get the value of a control */
static int vidioc_g_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct amradio_device *radio = video_get_drvdata(video_devdata(file));

	/* safety check */
	if (radio->removed)
		return -EIO;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = radio->muted;
		return 0;
	}
	return -EINVAL;
}

/* vidioc_s_ctrl - set the value of a control */
static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct amradio_device *radio = video_get_drvdata(video_devdata(file));

	/* safety check */
	if (radio->removed)
		return -EIO;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value) {
			if (amradio_stop(radio) < 0) {
				amradio_dev_warn(&radio->videodev->dev,
					"amradio_stop failed\n");
				return -1;
			}
		} else {
			if (amradio_start(radio) < 0) {
				amradio_dev_warn(&radio->videodev->dev,
					"amradio_start failed\n");
				return -1;
			}
		}
		return 0;
	}
	return -EINVAL;
}

/* vidioc_g_audio - get audio attributes */
static int vidioc_g_audio(struct file *file, void *priv,
				struct v4l2_audio *a)
{
	if (a->index > 1)
		return -EINVAL;

	strcpy(a->name, "Radio");
	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

/* vidioc_s_audio - set audio attributes  */
static int vidioc_s_audio(struct file *file, void *priv,
					struct v4l2_audio *a)
{
	if (a->index != 0)
		return -EINVAL;
	return 0;
}

/* vidioc_g_input - get input */
static int vidioc_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

/* vidioc_s_input - set input */
static int vidioc_s_input(struct file *filp, void *priv, unsigned int i)
{
	if (i != 0)
		return -EINVAL;
	return 0;
}

/* open device - amradio_start() and amradio_setfreq() */
static int usb_amradio_open(struct file *file)
{
	struct amradio_device *radio = video_get_drvdata(video_devdata(file));

	lock_kernel();

	radio->users = 1;
	radio->muted = 1;

	if (amradio_start(radio) < 0) {
		amradio_dev_warn(&radio->videodev->dev,
			"radio did not start up properly\n");
		radio->users = 0;
		unlock_kernel();
		return -EIO;
	}
	if (amradio_setfreq(radio, radio->curfreq) < 0)
		amradio_dev_warn(&radio->videodev->dev,
			"set frequency failed\n");

	unlock_kernel();
	return 0;
}

/*close device */
static int usb_amradio_close(struct file *file)
{
	struct amradio_device *radio = video_get_drvdata(video_devdata(file));
	int retval;

	if (!radio)
		return -ENODEV;

	radio->users = 0;

	if (!radio->removed) {
		retval = amradio_stop(radio);
		if (retval < 0)
			amradio_dev_warn(&radio->videodev->dev,
				"amradio_stop failed\n");
	}

	return 0;
}

/* Suspend device - stop device. Need to be checked and fixed */
static int usb_amradio_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct amradio_device *radio = usb_get_intfdata(intf);

	if (amradio_stop(radio) < 0)
		dev_warn(&intf->dev, "amradio_stop failed\n");

	dev_info(&intf->dev, "going into suspend..\n");

	return 0;
}

/* Resume device - start device. Need to be checked and fixed */
static int usb_amradio_resume(struct usb_interface *intf)
{
	struct amradio_device *radio = usb_get_intfdata(intf);

	if (amradio_start(radio) < 0)
		dev_warn(&intf->dev, "amradio_start failed\n");

	dev_info(&intf->dev, "coming out of suspend..\n");

	return 0;
}

/* File system interface */
static const struct v4l2_file_operations usb_amradio_fops = {
	.owner		= THIS_MODULE,
	.open		= usb_amradio_open,
	.release	= usb_amradio_close,
	.ioctl		= video_ioctl2,
};

static const struct v4l2_ioctl_ops usb_amradio_ioctl_ops = {
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_g_tuner     = vidioc_g_tuner,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
	.vidioc_queryctrl   = vidioc_queryctrl,
	.vidioc_g_ctrl      = vidioc_g_ctrl,
	.vidioc_s_ctrl      = vidioc_s_ctrl,
	.vidioc_g_audio     = vidioc_g_audio,
	.vidioc_s_audio     = vidioc_s_audio,
	.vidioc_g_input     = vidioc_g_input,
	.vidioc_s_input     = vidioc_s_input,
};

static void usb_amradio_device_release(struct video_device *videodev)
{
	struct amradio_device *radio = video_get_drvdata(videodev);

	/* we call v4l to free radio->videodev */
	video_device_release(videodev);

	/* free rest memory */
	kfree(radio->buffer);
	kfree(radio);
}

/* V4L2 interface */
static struct video_device amradio_videodev_template = {
	.name		= "AverMedia MR 800 USB FM Radio",
	.fops		= &usb_amradio_fops,
	.ioctl_ops 	= &usb_amradio_ioctl_ops,
	.release	= usb_amradio_device_release,
};

/* check if the device is present and register with v4l and
usb if it is */
static int usb_amradio_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct amradio_device *radio;

	radio = kmalloc(sizeof(struct amradio_device), GFP_KERNEL);

	if (!(radio))
		return -ENOMEM;

	radio->buffer = kmalloc(BUFFER_LENGTH, GFP_KERNEL);

	if (!(radio->buffer)) {
		kfree(radio);
		return -ENOMEM;
	}

	radio->videodev = video_device_alloc();

	if (!(radio->videodev)) {
		kfree(radio->buffer);
		kfree(radio);
		return -ENOMEM;
	}

	memcpy(radio->videodev, &amradio_videodev_template,
		sizeof(amradio_videodev_template));

	radio->removed = 0;
	radio->users = 0;
	radio->usbdev = interface_to_usbdev(intf);
	radio->curfreq = 95.16 * FREQ_MUL;

	mutex_init(&radio->lock);

	video_set_drvdata(radio->videodev, radio);
	if (video_register_device(radio->videodev, VFL_TYPE_RADIO, radio_nr)) {
		dev_warn(&intf->dev, "could not register video device\n");
		video_device_release(radio->videodev);
		kfree(radio->buffer);
		kfree(radio);
		return -EIO;
	}

	usb_set_intfdata(intf, radio);
	return 0;
}

static int __init amradio_init(void)
{
	int retval = usb_register(&usb_amradio_driver);

	pr_info(KBUILD_MODNAME
		": version " DRIVER_VERSION " " DRIVER_DESC "\n");

	if (retval)
		pr_err(KBUILD_MODNAME
			": usb_register failed. Error number %d\n", retval);

	return retval;
}

static void __exit amradio_exit(void)
{
	usb_deregister(&usb_amradio_driver);
}

module_init(amradio_init);
module_exit(amradio_exit);


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
 * Big thanks to authors and contributors of dsbr100.c and radio-si470x.c
 *
 * When work was looked pretty good, i discover this:
 * http://av-usbradio.sourceforge.net/index.php
 * http://sourceforge.net/projects/av-usbradio/
 * Latest release of theirs project was in 2005.
 * Probably, this driver could be improved through using their
 * achievements (specifications given).
 * Also, Faidon Liambotis <paravoid@debian.org> wrote nice driver for this radio
 * in 2007. He allowed to use his driver to improve current mr800 radio driver.
 * http://kerneltrap.org/mailarchive/linux-usb-devel/2007/10/11/342492
 *
 * Version 0.01:	First working version.
 * 			It's required to blacklist AverMedia USB Radio
 * 			in usbhid/hid-quirks.c
 * Version 0.10:	A lot of cleanups and fixes: unpluging the device,
 * 			few mutex locks were added, codinstyle issues, etc.
 * 			Added stereo support. Thanks to
 * 			Douglas Schilling Landgraf <dougsland@gmail.com> and
 * 			David Ellingsworth <david@identd.dyndns.org>
 * 			for discussion, help and support.
 * Version 0.11:	Converted to v4l2_device.
 *
 * Many things to do:
 * 	- Correct power management of device (suspend & resume)
 * 	- Add code for scanning and smooth tuning
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
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <linux/usb.h>
#include <linux/version.h>	/* for KERNEL_VERSION MACRO */
#include <linux/mutex.h>

/* driver and module definitions */
#define DRIVER_AUTHOR "Alexey Klimov <klimov.linux@gmail.com>"
#define DRIVER_DESC "AverMedia MR 800 USB FM radio driver"
#define DRIVER_VERSION "0.11"
#define RADIO_VERSION KERNEL_VERSION(0, 1, 1)

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define USB_AMRADIO_VENDOR 0x07ca
#define USB_AMRADIO_PRODUCT 0xb800

/* dev_warn macro with driver name */
#define MR800_DRIVER_NAME "radio-mr800"
#define amradio_dev_warn(dev, fmt, arg...)				\
		dev_warn(dev, MR800_DRIVER_NAME " - " fmt, ##arg)

#define amradio_dev_err(dev, fmt, arg...) \
		dev_err(dev, MR800_DRIVER_NAME " - " fmt, ##arg)

/* Probably USB_TIMEOUT should be modified in module parameter */
#define BUFFER_LENGTH 8
#define USB_TIMEOUT 500

/* Frequency limits in MHz -- these are European values.  For Japanese
devices, that would be 76 and 91.  */
#define FREQ_MIN  87.5
#define FREQ_MAX 108.0
#define FREQ_MUL 16000

/*
 * Commands that device should understand
 * List isn't full and will be updated with implementation of new functions
 */
#define AMRADIO_SET_FREQ	0xa4
#define AMRADIO_SET_MUTE	0xab
#define AMRADIO_SET_MONO	0xae

/* Comfortable defines for amradio_set_mute */
#define AMRADIO_START		0x00
#define AMRADIO_STOP		0x01

/* Comfortable defines for amradio_set_stereo */
#define WANT_STEREO		0x00
#define WANT_MONO		0x01

/* module parameter */
static int radio_nr = -1;
module_param(radio_nr, int, 0);
MODULE_PARM_DESC(radio_nr, "Radio Nr");

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
	struct usb_interface *intf;
	struct video_device videodev;
	struct v4l2_device v4l2_dev;

	unsigned char *buffer;
	struct mutex lock;	/* buffer locking */
	int curfreq;
	int stereo;
	int muted;
	int initialized;
};

static inline struct amradio_device *to_amradio_dev(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct amradio_device, v4l2_dev);
}

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
	.supports_autosuspend	= 1,
};

/* switch on/off the radio. Send 8 bytes to device */
static int amradio_set_mute(struct amradio_device *radio, char argument)
{
	int retval;
	int size;

	radio->buffer[0] = 0x00;
	radio->buffer[1] = 0x55;
	radio->buffer[2] = 0xaa;
	radio->buffer[3] = 0x00;
	radio->buffer[4] = AMRADIO_SET_MUTE;
	radio->buffer[5] = argument;
	radio->buffer[6] = 0x00;
	radio->buffer[7] = 0x00;

	retval = usb_bulk_msg(radio->usbdev, usb_sndintpipe(radio->usbdev, 2),
		(void *) (radio->buffer), BUFFER_LENGTH, &size, USB_TIMEOUT);

	if (retval < 0 || size != BUFFER_LENGTH) {
		amradio_dev_warn(&radio->videodev.dev, "set mute failed\n");
		return retval;
	}

	radio->muted = argument;

	return retval;
}

/* set a frequency, freq is defined by v4l's TUNER_LOW, i.e. 1/16th kHz */
static int amradio_setfreq(struct amradio_device *radio, int freq)
{
	int retval;
	int size;
	unsigned short freq_send = 0x10 + (freq >> 3) / 25;

	radio->buffer[0] = 0x00;
	radio->buffer[1] = 0x55;
	radio->buffer[2] = 0xaa;
	radio->buffer[3] = 0x03;
	radio->buffer[4] = AMRADIO_SET_FREQ;
	radio->buffer[5] = 0x00;
	radio->buffer[6] = 0x00;
	radio->buffer[7] = 0x08;

	retval = usb_bulk_msg(radio->usbdev, usb_sndintpipe(radio->usbdev, 2),
		(void *) (radio->buffer), BUFFER_LENGTH, &size, USB_TIMEOUT);

	if (retval < 0 || size != BUFFER_LENGTH)
		goto out_err;

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

	if (retval < 0 || size != BUFFER_LENGTH)
		goto out_err;

	radio->curfreq = freq;
	goto out;

out_err:
	amradio_dev_warn(&radio->videodev.dev, "set frequency failed\n");
out:
	return retval;
}

static int amradio_set_stereo(struct amradio_device *radio, char argument)
{
	int retval;
	int size;

	radio->buffer[0] = 0x00;
	radio->buffer[1] = 0x55;
	radio->buffer[2] = 0xaa;
	radio->buffer[3] = 0x00;
	radio->buffer[4] = AMRADIO_SET_MONO;
	radio->buffer[5] = argument;
	radio->buffer[6] = 0x00;
	radio->buffer[7] = 0x00;

	retval = usb_bulk_msg(radio->usbdev, usb_sndintpipe(radio->usbdev, 2),
		(void *) (radio->buffer), BUFFER_LENGTH, &size, USB_TIMEOUT);

	if (retval < 0 || size != BUFFER_LENGTH) {
		amradio_dev_warn(&radio->videodev.dev, "set stereo failed\n");
		return retval;
	}

	if (argument == WANT_STEREO)
		radio->stereo = 1;
	else
		radio->stereo = 0;

	return retval;
}

/* Handle unplugging the device.
 * We call video_unregister_device in any case.
 * The last function called in this procedure is
 * usb_amradio_device_release.
 */
static void usb_amradio_disconnect(struct usb_interface *intf)
{
	struct amradio_device *radio = to_amradio_dev(usb_get_intfdata(intf));

	mutex_lock(&radio->lock);
	/* increase the device node's refcount */
	get_device(&radio->videodev.dev);
	v4l2_device_disconnect(&radio->v4l2_dev);
	video_unregister_device(&radio->videodev);
	mutex_unlock(&radio->lock);
	/* decrease the device node's refcount, allowing it to be released */
	put_device(&radio->videodev.dev);
}

/* vidioc_querycap - query device capabilities */
static int vidioc_querycap(struct file *file, void *priv,
					struct v4l2_capability *v)
{
	struct amradio_device *radio = file->private_data;

	strlcpy(v->driver, "radio-mr800", sizeof(v->driver));
	strlcpy(v->card, "AverMedia MR 800 USB FM Radio", sizeof(v->card));
	usb_make_path(radio->usbdev, v->bus_info, sizeof(v->bus_info));
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER;
	return 0;
}

/* vidioc_g_tuner - get tuner attributes */
static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	struct amradio_device *radio = file->private_data;
	int retval;

	if (v->index > 0)
		return -EINVAL;

/* TODO: Add function which look is signal stereo or not
 * 	amradio_getstat(radio);
 */

/* we call amradio_set_stereo to set radio->stereo
 * Honestly, amradio_getstat should cover this in future and
 * amradio_set_stereo shouldn't be here
 */
	retval = amradio_set_stereo(radio, WANT_STEREO);

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

	return retval;
}

/* vidioc_s_tuner - set tuner attributes */
static int vidioc_s_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	struct amradio_device *radio = file->private_data;
	int retval = -EINVAL;

	if (v->index > 0)
		return -EINVAL;

	/* mono/stereo selector */
	switch (v->audmode) {
	case V4L2_TUNER_MODE_MONO:
		retval = amradio_set_stereo(radio, WANT_MONO);
		break;
	case V4L2_TUNER_MODE_STEREO:
		retval = amradio_set_stereo(radio, WANT_STEREO);
		break;
	}

	return retval;
}

/* vidioc_s_frequency - set tuner radio frequency */
static int vidioc_s_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct amradio_device *radio = file->private_data;

	if (f->tuner != 0 || f->type != V4L2_TUNER_RADIO)
		return -EINVAL;
	return amradio_setfreq(radio, f->frequency);
}

/* vidioc_g_frequency - get tuner radio frequency */
static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct amradio_device *radio = file->private_data;

	if (f->tuner != 0)
		return -EINVAL;
	f->type = V4L2_TUNER_RADIO;
	f->frequency = radio->curfreq;

	return 0;
}

/* vidioc_queryctrl - enumerate control items */
static int vidioc_queryctrl(struct file *file, void *priv,
				struct v4l2_queryctrl *qc)
{
	switch (qc->id) {
	case V4L2_CID_AUDIO_MUTE:
		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 1);
	}

	return -EINVAL;
}

/* vidioc_g_ctrl - get the value of a control */
static int vidioc_g_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct amradio_device *radio = file->private_data;

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
	struct amradio_device *radio = file->private_data;
	int retval = -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value)
			retval = amradio_set_mute(radio, AMRADIO_STOP);
		else
			retval = amradio_set_mute(radio, AMRADIO_START);

		break;
	}

	return retval;
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

static int usb_amradio_init(struct amradio_device *radio)
{
	int retval;

	retval = amradio_set_mute(radio, AMRADIO_STOP);
	if (retval)
		goto out_err;

	retval = amradio_set_stereo(radio, WANT_STEREO);
	if (retval)
		goto out_err;

	radio->initialized = 1;
	goto out;

out_err:
	amradio_dev_err(&radio->videodev.dev, "initialization failed\n");
out:
	return retval;
}

/* open device - amradio_start() and amradio_setfreq() */
static int usb_amradio_open(struct file *file)
{
	struct amradio_device *radio = video_drvdata(file);
	int retval;

	file->private_data = radio;
	retval = usb_autopm_get_interface(radio->intf);
	if (retval)
		return retval;

	if (unlikely(!radio->initialized)) {
		retval = usb_amradio_init(radio);
		if (retval)
			usb_autopm_put_interface(radio->intf);
	}
	return retval;
}

/*close device */
static int usb_amradio_close(struct file *file)
{
	struct amradio_device *radio = file->private_data;

	if (video_is_registered(&radio->videodev))
		usb_autopm_put_interface(radio->intf);
	return 0;
}

/* Suspend device - stop device. Need to be checked and fixed */
static int usb_amradio_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct amradio_device *radio = to_amradio_dev(usb_get_intfdata(intf));

	mutex_lock(&radio->lock);
	if (!radio->muted && radio->initialized) {
		amradio_set_mute(radio, AMRADIO_STOP);
		radio->muted = 0;
	}
	mutex_unlock(&radio->lock);

	dev_info(&intf->dev, "going into suspend..\n");
	return 0;
}

/* Resume device - start device. Need to be checked and fixed */
static int usb_amradio_resume(struct usb_interface *intf)
{
	struct amradio_device *radio = to_amradio_dev(usb_get_intfdata(intf));

	mutex_lock(&radio->lock);
	if (unlikely(!radio->initialized))
		goto unlock;

	if (radio->stereo)
		amradio_set_stereo(radio, WANT_STEREO);
	else
		amradio_set_stereo(radio, WANT_MONO);

	amradio_setfreq(radio, radio->curfreq);

	if (!radio->muted)
		amradio_set_mute(radio, AMRADIO_START);

unlock:
	mutex_unlock(&radio->lock);

	dev_info(&intf->dev, "coming out of suspend..\n");
	return 0;
}

/* File system interface */
static const struct v4l2_file_operations usb_amradio_fops = {
	.owner		= THIS_MODULE,
	.open		= usb_amradio_open,
	.release	= usb_amradio_close,
	.unlocked_ioctl	= video_ioctl2,
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

static void usb_amradio_video_device_release(struct video_device *videodev)
{
	struct amradio_device *radio = video_get_drvdata(videodev);

	/* free rest memory */
	kfree(radio->buffer);
	kfree(radio);
}

/* check if the device is present and register with v4l and usb if it is */
static int usb_amradio_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct amradio_device *radio;
	int retval = 0;

	radio = kzalloc(sizeof(struct amradio_device), GFP_KERNEL);

	if (!radio) {
		dev_err(&intf->dev, "kmalloc for amradio_device failed\n");
		retval = -ENOMEM;
		goto err;
	}

	radio->buffer = kmalloc(BUFFER_LENGTH, GFP_KERNEL);

	if (!radio->buffer) {
		dev_err(&intf->dev, "kmalloc for radio->buffer failed\n");
		retval = -ENOMEM;
		goto err_nobuf;
	}

	retval = v4l2_device_register(&intf->dev, &radio->v4l2_dev);
	if (retval < 0) {
		dev_err(&intf->dev, "couldn't register v4l2_device\n");
		goto err_v4l2;
	}

	mutex_init(&radio->lock);

	strlcpy(radio->videodev.name, radio->v4l2_dev.name,
		sizeof(radio->videodev.name));
	radio->videodev.v4l2_dev = &radio->v4l2_dev;
	radio->videodev.fops = &usb_amradio_fops;
	radio->videodev.ioctl_ops = &usb_amradio_ioctl_ops;
	radio->videodev.release = usb_amradio_video_device_release;
	radio->videodev.lock = &radio->lock;

	radio->usbdev = interface_to_usbdev(intf);
	radio->intf = intf;
	radio->curfreq = 95.16 * FREQ_MUL;

	video_set_drvdata(&radio->videodev, radio);

	retval = video_register_device(&radio->videodev, VFL_TYPE_RADIO,
					radio_nr);
	if (retval < 0) {
		dev_err(&intf->dev, "could not register video device\n");
		goto err_vdev;
	}

	return 0;

err_vdev:
	v4l2_device_unregister(&radio->v4l2_dev);
err_v4l2:
	kfree(radio->buffer);
err_nobuf:
	kfree(radio);
err:
	return retval;
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


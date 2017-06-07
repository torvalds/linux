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
 * http://www.spinics.net/lists/linux-usb-devel/msg10109.html
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
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <linux/usb.h>
#include <linux/mutex.h>

/* driver and module definitions */
#define DRIVER_AUTHOR "Alexey Klimov <klimov.linux@gmail.com>"
#define DRIVER_DESC "AverMedia MR 800 USB FM radio driver"
#define DRIVER_VERSION "0.1.2"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

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
#define AMRADIO_GET_READY_FLAG	0xa5
#define AMRADIO_GET_SIGNAL	0xa7
#define AMRADIO_GET_FREQ	0xa8
#define AMRADIO_SET_SEARCH_UP	0xa9
#define AMRADIO_SET_SEARCH_DOWN	0xaa
#define AMRADIO_SET_MUTE	0xab
#define AMRADIO_SET_RIGHT_MUTE	0xac
#define AMRADIO_SET_LEFT_MUTE	0xad
#define AMRADIO_SET_MONO	0xae
#define AMRADIO_SET_SEARCH_LVL	0xb0
#define AMRADIO_STOP_SEARCH	0xb1

/* Comfortable defines for amradio_set_stereo */
#define WANT_STEREO		0x00
#define WANT_MONO		0x01

/* module parameter */
static int radio_nr = -1;
module_param(radio_nr, int, 0);
MODULE_PARM_DESC(radio_nr, "Radio Nr");

/* Data for one (physical) device */
struct amradio_device {
	/* reference to USB and video device */
	struct usb_device *usbdev;
	struct usb_interface *intf;
	struct video_device vdev;
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler hdl;

	u8 *buffer;
	struct mutex lock;	/* buffer locking */
	int curfreq;
	int stereo;
	int muted;
};

static inline struct amradio_device *to_amradio_dev(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct amradio_device, v4l2_dev);
}

static int amradio_send_cmd(struct amradio_device *radio, u8 cmd, u8 arg,
		u8 *extra, u8 extralen, bool reply)
{
	int retval;
	int size;

	radio->buffer[0] = 0x00;
	radio->buffer[1] = 0x55;
	radio->buffer[2] = 0xaa;
	radio->buffer[3] = extralen;
	radio->buffer[4] = cmd;
	radio->buffer[5] = arg;
	radio->buffer[6] = 0x00;
	radio->buffer[7] = extra || reply ? 8 : 0;

	retval = usb_bulk_msg(radio->usbdev, usb_sndintpipe(radio->usbdev, 2),
		radio->buffer, BUFFER_LENGTH, &size, USB_TIMEOUT);

	if (retval < 0 || size != BUFFER_LENGTH) {
		if (video_is_registered(&radio->vdev))
			amradio_dev_warn(&radio->vdev.dev,
					"cmd %02x failed\n", cmd);
		return retval ? retval : -EIO;
	}
	if (!extra && !reply)
		return 0;

	if (extra) {
		memcpy(radio->buffer, extra, extralen);
		memset(radio->buffer + extralen, 0, 8 - extralen);
		retval = usb_bulk_msg(radio->usbdev, usb_sndintpipe(radio->usbdev, 2),
			radio->buffer, BUFFER_LENGTH, &size, USB_TIMEOUT);
	} else {
		memset(radio->buffer, 0, 8);
		retval = usb_bulk_msg(radio->usbdev, usb_rcvbulkpipe(radio->usbdev, 0x81),
			radio->buffer, BUFFER_LENGTH, &size, USB_TIMEOUT);
	}
	if (retval == 0 && size == BUFFER_LENGTH)
		return 0;
	if (video_is_registered(&radio->vdev) && cmd != AMRADIO_GET_READY_FLAG)
		amradio_dev_warn(&radio->vdev.dev, "follow-up to cmd %02x failed\n", cmd);
	return retval ? retval : -EIO;
}

/* switch on/off the radio. Send 8 bytes to device */
static int amradio_set_mute(struct amradio_device *radio, bool mute)
{
	int ret = amradio_send_cmd(radio,
			AMRADIO_SET_MUTE, mute, NULL, 0, false);

	if (!ret)
		radio->muted = mute;
	return ret;
}

/* set a frequency, freq is defined by v4l's TUNER_LOW, i.e. 1/16th kHz */
static int amradio_set_freq(struct amradio_device *radio, int freq)
{
	unsigned short freq_send;
	u8 buf[3];
	int retval;

	/* we need to be sure that frequency isn't out of range */
	freq = clamp_t(unsigned, freq, FREQ_MIN * FREQ_MUL, FREQ_MAX * FREQ_MUL);
	freq_send = 0x10 + (freq >> 3) / 25;

	/* frequency is calculated from freq_send and placed in first 2 bytes */
	buf[0] = (freq_send >> 8) & 0xff;
	buf[1] = freq_send & 0xff;
	buf[2] = 0x01;

	retval = amradio_send_cmd(radio, AMRADIO_SET_FREQ, 0, buf, 3, false);
	if (retval)
		return retval;
	radio->curfreq = freq;
	msleep(40);
	return 0;
}

static int amradio_set_stereo(struct amradio_device *radio, bool stereo)
{
	int ret = amradio_send_cmd(radio,
			AMRADIO_SET_MONO, !stereo, NULL, 0, false);

	if (!ret)
		radio->stereo = stereo;
	return ret;
}

static int amradio_get_stat(struct amradio_device *radio, bool *is_stereo, u32 *signal)
{
	int ret = amradio_send_cmd(radio,
			AMRADIO_GET_SIGNAL, 0, NULL, 0, true);

	if (ret)
		return ret;
	*is_stereo = radio->buffer[2] >> 7;
	*signal = (radio->buffer[3] & 0xf0) << 8;
	return 0;
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
	video_unregister_device(&radio->vdev);
	amradio_set_mute(radio, true);
	usb_set_intfdata(intf, NULL);
	v4l2_device_disconnect(&radio->v4l2_dev);
	mutex_unlock(&radio->lock);
	v4l2_device_put(&radio->v4l2_dev);
}

/* vidioc_querycap - query device capabilities */
static int vidioc_querycap(struct file *file, void *priv,
					struct v4l2_capability *v)
{
	struct amradio_device *radio = video_drvdata(file);

	strlcpy(v->driver, "radio-mr800", sizeof(v->driver));
	strlcpy(v->card, "AverMedia MR 800 USB FM Radio", sizeof(v->card));
	usb_make_path(radio->usbdev, v->bus_info, sizeof(v->bus_info));
	v->device_caps = V4L2_CAP_RADIO | V4L2_CAP_TUNER |
					V4L2_CAP_HW_FREQ_SEEK;
	v->capabilities = v->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

/* vidioc_g_tuner - get tuner attributes */
static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	struct amradio_device *radio = video_drvdata(file);
	bool is_stereo = false;
	int retval;

	if (v->index > 0)
		return -EINVAL;

	v->signal = 0;
	retval = amradio_get_stat(radio, &is_stereo, &v->signal);
	if (retval)
		return retval;

	strcpy(v->name, "FM");
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = FREQ_MIN * FREQ_MUL;
	v->rangehigh = FREQ_MAX * FREQ_MUL;
	v->capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
		V4L2_TUNER_CAP_HWSEEK_WRAP;
	v->rxsubchans = is_stereo ? V4L2_TUNER_SUB_STEREO : V4L2_TUNER_SUB_MONO;
	v->audmode = radio->stereo ?
		V4L2_TUNER_MODE_STEREO : V4L2_TUNER_MODE_MONO;
	return 0;
}

/* vidioc_s_tuner - set tuner attributes */
static int vidioc_s_tuner(struct file *file, void *priv,
				const struct v4l2_tuner *v)
{
	struct amradio_device *radio = video_drvdata(file);

	if (v->index > 0)
		return -EINVAL;

	/* mono/stereo selector */
	switch (v->audmode) {
	case V4L2_TUNER_MODE_MONO:
		return amradio_set_stereo(radio, WANT_MONO);
	default:
		return amradio_set_stereo(radio, WANT_STEREO);
	}
}

/* vidioc_s_frequency - set tuner radio frequency */
static int vidioc_s_frequency(struct file *file, void *priv,
				const struct v4l2_frequency *f)
{
	struct amradio_device *radio = video_drvdata(file);

	if (f->tuner != 0)
		return -EINVAL;
	return amradio_set_freq(radio, f->frequency);
}

/* vidioc_g_frequency - get tuner radio frequency */
static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct amradio_device *radio = video_drvdata(file);

	if (f->tuner != 0 || f->type != V4L2_TUNER_RADIO)
		return -EINVAL;
	f->type = V4L2_TUNER_RADIO;
	f->frequency = radio->curfreq;

	return 0;
}

static int vidioc_s_hw_freq_seek(struct file *file, void *priv,
		const struct v4l2_hw_freq_seek *seek)
{
	static u8 buf[8] = {
		0x3d, 0x32, 0x0f, 0x08, 0x3d, 0x32, 0x0f, 0x08
	};
	struct amradio_device *radio = video_drvdata(file);
	unsigned long timeout;
	int retval;

	if (seek->tuner != 0 || !seek->wrap_around)
		return -EINVAL;

	if (file->f_flags & O_NONBLOCK)
		return -EWOULDBLOCK;

	retval = amradio_send_cmd(radio,
			AMRADIO_SET_SEARCH_LVL, 0, buf, 8, false);
	if (retval)
		return retval;
	amradio_set_freq(radio, radio->curfreq);
	retval = amradio_send_cmd(radio,
		seek->seek_upward ? AMRADIO_SET_SEARCH_UP : AMRADIO_SET_SEARCH_DOWN,
		0, NULL, 0, false);
	if (retval)
		return retval;
	timeout = jiffies + msecs_to_jiffies(30000);
	for (;;) {
		if (time_after(jiffies, timeout)) {
			retval = -ENODATA;
			break;
		}
		if (schedule_timeout_interruptible(msecs_to_jiffies(10))) {
			retval = -ERESTARTSYS;
			break;
		}
		retval = amradio_send_cmd(radio, AMRADIO_GET_READY_FLAG,
				0, NULL, 0, true);
		if (retval)
			continue;
		amradio_send_cmd(radio, AMRADIO_GET_FREQ, 0, NULL, 0, true);
		if (radio->buffer[1] || radio->buffer[2]) {
			/* To check: sometimes radio->curfreq is set to out of range value */
			radio->curfreq = (radio->buffer[1] << 8) | radio->buffer[2];
			radio->curfreq = (radio->curfreq - 0x10) * 200;
			amradio_send_cmd(radio, AMRADIO_STOP_SEARCH,
					0, NULL, 0, false);
			amradio_set_freq(radio, radio->curfreq);
			retval = 0;
			break;
		}
	}
	amradio_send_cmd(radio, AMRADIO_STOP_SEARCH, 0, NULL, 0, false);
	amradio_set_freq(radio, radio->curfreq);
	return retval;
}

static int usb_amradio_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct amradio_device *radio =
		container_of(ctrl->handler, struct amradio_device, hdl);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		return amradio_set_mute(radio, ctrl->val);
	}

	return -EINVAL;
}

static int usb_amradio_init(struct amradio_device *radio)
{
	int retval;

	retval = amradio_set_mute(radio, true);
	if (retval)
		goto out_err;
	retval = amradio_set_stereo(radio, true);
	if (retval)
		goto out_err;
	retval = amradio_set_freq(radio, radio->curfreq);
	if (retval)
		goto out_err;
	return 0;

out_err:
	amradio_dev_err(&radio->vdev.dev, "initialization failed\n");
	return retval;
}

/* Suspend device - stop device. Need to be checked and fixed */
static int usb_amradio_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct amradio_device *radio = to_amradio_dev(usb_get_intfdata(intf));

	mutex_lock(&radio->lock);
	if (!radio->muted) {
		amradio_set_mute(radio, true);
		radio->muted = false;
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
	amradio_set_stereo(radio, radio->stereo);
	amradio_set_freq(radio, radio->curfreq);

	if (!radio->muted)
		amradio_set_mute(radio, false);

	mutex_unlock(&radio->lock);

	dev_info(&intf->dev, "coming out of suspend..\n");
	return 0;
}

static const struct v4l2_ctrl_ops usb_amradio_ctrl_ops = {
	.s_ctrl = usb_amradio_s_ctrl,
};

/* File system interface */
static const struct v4l2_file_operations usb_amradio_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= v4l2_fh_release,
	.poll		= v4l2_ctrl_poll,
	.unlocked_ioctl	= video_ioctl2,
};

static const struct v4l2_ioctl_ops usb_amradio_ioctl_ops = {
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_g_tuner     = vidioc_g_tuner,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
	.vidioc_s_hw_freq_seek = vidioc_s_hw_freq_seek,
	.vidioc_log_status  = v4l2_ctrl_log_status,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static void usb_amradio_release(struct v4l2_device *v4l2_dev)
{
	struct amradio_device *radio = to_amradio_dev(v4l2_dev);

	/* free rest memory */
	v4l2_ctrl_handler_free(&radio->hdl);
	v4l2_device_unregister(&radio->v4l2_dev);
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

	v4l2_ctrl_handler_init(&radio->hdl, 1);
	v4l2_ctrl_new_std(&radio->hdl, &usb_amradio_ctrl_ops,
			  V4L2_CID_AUDIO_MUTE, 0, 1, 1, 1);
	if (radio->hdl.error) {
		retval = radio->hdl.error;
		dev_err(&intf->dev, "couldn't register control\n");
		goto err_ctrl;
	}
	mutex_init(&radio->lock);

	radio->v4l2_dev.ctrl_handler = &radio->hdl;
	radio->v4l2_dev.release = usb_amradio_release;
	strlcpy(radio->vdev.name, radio->v4l2_dev.name,
		sizeof(radio->vdev.name));
	radio->vdev.v4l2_dev = &radio->v4l2_dev;
	radio->vdev.fops = &usb_amradio_fops;
	radio->vdev.ioctl_ops = &usb_amradio_ioctl_ops;
	radio->vdev.release = video_device_release_empty;
	radio->vdev.lock = &radio->lock;

	radio->usbdev = interface_to_usbdev(intf);
	radio->intf = intf;
	usb_set_intfdata(intf, &radio->v4l2_dev);
	radio->curfreq = 95.16 * FREQ_MUL;

	video_set_drvdata(&radio->vdev, radio);
	retval = usb_amradio_init(radio);
	if (retval)
		goto err_vdev;

	retval = video_register_device(&radio->vdev, VFL_TYPE_RADIO,
					radio_nr);
	if (retval < 0) {
		dev_err(&intf->dev, "could not register video device\n");
		goto err_vdev;
	}

	return 0;

err_vdev:
	v4l2_ctrl_handler_free(&radio->hdl);
err_ctrl:
	v4l2_device_unregister(&radio->v4l2_dev);
err_v4l2:
	kfree(radio->buffer);
err_nobuf:
	kfree(radio);
err:
	return retval;
}

/* USB Device ID List */
static struct usb_device_id usb_amradio_device_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(USB_AMRADIO_VENDOR, USB_AMRADIO_PRODUCT,
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
};

module_usb_driver(usb_amradio_driver);

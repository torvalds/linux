/*
 * Driver for the MasterKit MA901 USB FM radio. This device plugs
 * into the USB port and an analog audio input or headphones, so this thing
 * only deals with initialization, frequency setting, volume.
 *
 * Copyright (c) 2012 Alexey Klimov <klimov.linux@gmail.com>
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

#define DRIVER_AUTHOR "Alexey Klimov <klimov.linux@gmail.com>"
#define DRIVER_DESC "Masterkit MA901 USB FM radio driver"
#define DRIVER_VERSION "0.0.1"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

#define USB_MA901_VENDOR  0x16c0
#define USB_MA901_PRODUCT 0x05df

/* dev_warn macro with driver name */
#define MA901_DRIVER_NAME "radio-ma901"
#define ma901radio_dev_warn(dev, fmt, arg...)				\
		dev_warn(dev, MA901_DRIVER_NAME " - " fmt, ##arg)

#define ma901radio_dev_err(dev, fmt, arg...) \
		dev_err(dev, MA901_DRIVER_NAME " - " fmt, ##arg)

/* Probably USB_TIMEOUT should be modified in module parameter */
#define BUFFER_LENGTH 8
#define USB_TIMEOUT 500

#define FREQ_MIN  87.5
#define FREQ_MAX 108.0
#define FREQ_MUL 16000

#define MA901_VOLUME_MAX 16
#define MA901_VOLUME_MIN 0

/* Commands that device should understand
 * List isn't full and will be updated with implementation of new functions
 */
#define MA901_RADIO_SET_FREQ		0x03
#define MA901_RADIO_SET_VOLUME		0x04
#define MA901_RADIO_SET_MONO_STEREO	0x05

/* Comfortable defines for ma901radio_set_stereo */
#define MA901_WANT_STEREO		0x50
#define MA901_WANT_MONO			0xd0

/* module parameter */
static int radio_nr = -1;
module_param(radio_nr, int, 0);
MODULE_PARM_DESC(radio_nr, "Radio file number");

/* Data for one (physical) device */
struct ma901radio_device {
	/* reference to USB and video device */
	struct usb_device *usbdev;
	struct usb_interface *intf;
	struct video_device vdev;
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler hdl;

	u8 *buffer;
	struct mutex lock;	/* buffer locking */
	int curfreq;
	u16 volume;
	int stereo;
	bool muted;
};

static inline struct ma901radio_device *to_ma901radio_dev(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct ma901radio_device, v4l2_dev);
}

/* set a frequency, freq is defined by v4l's TUNER_LOW, i.e. 1/16th kHz */
static int ma901radio_set_freq(struct ma901radio_device *radio, int freq)
{
	unsigned int freq_send = 0x300 + (freq >> 5) / 25;
	int retval;

	radio->buffer[0] = 0x0a;
	radio->buffer[1] = MA901_RADIO_SET_FREQ;
	radio->buffer[2] = ((freq_send >> 8) & 0xff) + 0x80;
	radio->buffer[3] = freq_send & 0xff;
	radio->buffer[4] = 0x00;
	radio->buffer[5] = 0x00;
	radio->buffer[6] = 0x00;
	radio->buffer[7] = 0x00;

	retval = usb_control_msg(radio->usbdev, usb_sndctrlpipe(radio->usbdev, 0),
				9, 0x21, 0x0300, 0,
				radio->buffer, BUFFER_LENGTH, USB_TIMEOUT);
	if (retval < 0)
		return retval;

	radio->curfreq = freq;
	return 0;
}

static int ma901radio_set_volume(struct ma901radio_device *radio, u16 vol_to_set)
{
	int retval;

	radio->buffer[0] = 0x0a;
	radio->buffer[1] = MA901_RADIO_SET_VOLUME;
	radio->buffer[2] = 0xc2;
	radio->buffer[3] = vol_to_set + 0x20;
	radio->buffer[4] = 0x00;
	radio->buffer[5] = 0x00;
	radio->buffer[6] = 0x00;
	radio->buffer[7] = 0x00;

	retval = usb_control_msg(radio->usbdev, usb_sndctrlpipe(radio->usbdev, 0),
				9, 0x21, 0x0300, 0,
				radio->buffer, BUFFER_LENGTH, USB_TIMEOUT);
	if (retval < 0)
		return retval;

	radio->volume = vol_to_set;
	return retval;
}

static int ma901_set_stereo(struct ma901radio_device *radio, u8 stereo)
{
	int retval;

	radio->buffer[0] = 0x0a;
	radio->buffer[1] = MA901_RADIO_SET_MONO_STEREO;
	radio->buffer[2] = stereo;
	radio->buffer[3] = 0x00;
	radio->buffer[4] = 0x00;
	radio->buffer[5] = 0x00;
	radio->buffer[6] = 0x00;
	radio->buffer[7] = 0x00;

	retval = usb_control_msg(radio->usbdev, usb_sndctrlpipe(radio->usbdev, 0),
				9, 0x21, 0x0300, 0,
				radio->buffer, BUFFER_LENGTH, USB_TIMEOUT);

	if (retval < 0)
		return retval;

	if (stereo == MA901_WANT_STEREO)
		radio->stereo = V4L2_TUNER_MODE_STEREO;
	else
		radio->stereo = V4L2_TUNER_MODE_MONO;

	return retval;
}

/* Handle unplugging the device.
 * We call video_unregister_device in any case.
 * The last function called in this procedure is
 * usb_ma901radio_device_release.
 */
static void usb_ma901radio_disconnect(struct usb_interface *intf)
{
	struct ma901radio_device *radio = to_ma901radio_dev(usb_get_intfdata(intf));

	mutex_lock(&radio->lock);
	video_unregister_device(&radio->vdev);
	usb_set_intfdata(intf, NULL);
	v4l2_device_disconnect(&radio->v4l2_dev);
	mutex_unlock(&radio->lock);
	v4l2_device_put(&radio->v4l2_dev);
}

/* vidioc_querycap - query device capabilities */
static int vidioc_querycap(struct file *file, void *priv,
					struct v4l2_capability *v)
{
	struct ma901radio_device *radio = video_drvdata(file);

	strlcpy(v->driver, "radio-ma901", sizeof(v->driver));
	strlcpy(v->card, "Masterkit MA901 USB FM Radio", sizeof(v->card));
	usb_make_path(radio->usbdev, v->bus_info, sizeof(v->bus_info));
	v->device_caps = V4L2_CAP_RADIO | V4L2_CAP_TUNER;
	v->capabilities = v->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

/* vidioc_g_tuner - get tuner attributes */
static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	struct ma901radio_device *radio = video_drvdata(file);

	if (v->index > 0)
		return -EINVAL;

	v->signal = 0;

	/* TODO: the same words like in _probe() goes here.
	 * When receiving of stats will be implemented then we can call
	 * ma901radio_get_stat().
	 * retval = ma901radio_get_stat(radio, &is_stereo, &v->signal);
	 */

	strcpy(v->name, "FM");
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = FREQ_MIN * FREQ_MUL;
	v->rangehigh = FREQ_MAX * FREQ_MUL;
	v->capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO;
	/* v->rxsubchans = is_stereo ? V4L2_TUNER_SUB_STEREO : V4L2_TUNER_SUB_MONO; */
	v->audmode = radio->stereo ?
		V4L2_TUNER_MODE_STEREO : V4L2_TUNER_MODE_MONO;
	return 0;
}

/* vidioc_s_tuner - set tuner attributes */
static int vidioc_s_tuner(struct file *file, void *priv,
				const struct v4l2_tuner *v)
{
	struct ma901radio_device *radio = video_drvdata(file);

	if (v->index > 0)
		return -EINVAL;

	/* mono/stereo selector */
	switch (v->audmode) {
	case V4L2_TUNER_MODE_MONO:
		return ma901_set_stereo(radio, MA901_WANT_MONO);
	default:
		return ma901_set_stereo(radio, MA901_WANT_STEREO);
	}
}

/* vidioc_s_frequency - set tuner radio frequency */
static int vidioc_s_frequency(struct file *file, void *priv,
				const struct v4l2_frequency *f)
{
	struct ma901radio_device *radio = video_drvdata(file);

	if (f->tuner != 0)
		return -EINVAL;

	return ma901radio_set_freq(radio, clamp_t(unsigned, f->frequency,
				FREQ_MIN * FREQ_MUL, FREQ_MAX * FREQ_MUL));
}

/* vidioc_g_frequency - get tuner radio frequency */
static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct ma901radio_device *radio = video_drvdata(file);

	if (f->tuner != 0)
		return -EINVAL;
	f->frequency = radio->curfreq;

	return 0;
}

static int usb_ma901radio_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ma901radio_device *radio =
		container_of(ctrl->handler, struct ma901radio_device, hdl);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:     /* set volume */
		return ma901radio_set_volume(radio, (u16)ctrl->val);
	}

	return -EINVAL;
}

/* TODO: Should we really need to implement suspend and resume functions?
 * Radio has it's own memory and will continue playing if power is present
 * on usb port and on resume it will start to play again based on freq, volume
 * values in device memory.
 */
static int usb_ma901radio_suspend(struct usb_interface *intf, pm_message_t message)
{
	return 0;
}

static int usb_ma901radio_resume(struct usb_interface *intf)
{
	return 0;
}

static const struct v4l2_ctrl_ops usb_ma901radio_ctrl_ops = {
	.s_ctrl = usb_ma901radio_s_ctrl,
};

/* File system interface */
static const struct v4l2_file_operations usb_ma901radio_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= v4l2_fh_release,
	.poll		= v4l2_ctrl_poll,
	.unlocked_ioctl	= video_ioctl2,
};

static const struct v4l2_ioctl_ops usb_ma901radio_ioctl_ops = {
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_g_tuner     = vidioc_g_tuner,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
	.vidioc_log_status  = v4l2_ctrl_log_status,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static void usb_ma901radio_release(struct v4l2_device *v4l2_dev)
{
	struct ma901radio_device *radio = to_ma901radio_dev(v4l2_dev);

	v4l2_ctrl_handler_free(&radio->hdl);
	v4l2_device_unregister(&radio->v4l2_dev);
	kfree(radio->buffer);
	kfree(radio);
}

/* check if the device is present and register with v4l and usb if it is */
static int usb_ma901radio_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct ma901radio_device *radio;
	int retval = 0;

	/* Masterkit MA901 usb radio has the same USB ID as many others
	 * Atmel V-USB devices. Let's make additional checks to be sure
	 * that this is our device.
	 */

	if (dev->product && dev->manufacturer &&
		(strncmp(dev->product, "MA901", 5) != 0
		|| strncmp(dev->manufacturer, "www.masterkit.ru", 16) != 0))
		return -ENODEV;

	radio = kzalloc(sizeof(struct ma901radio_device), GFP_KERNEL);
	if (!radio) {
		dev_err(&intf->dev, "kzalloc for ma901radio_device failed\n");
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

	/* TODO:It looks like this radio doesn't have mute/unmute control
	 * and windows program just emulate it using volume control.
	 * Let's plan to do the same in this driver.
	 *
	 * v4l2_ctrl_new_std(&radio->hdl, &usb_ma901radio_ctrl_ops,
	 *		  V4L2_CID_AUDIO_MUTE, 0, 1, 1, 1);
	 */

	v4l2_ctrl_new_std(&radio->hdl, &usb_ma901radio_ctrl_ops,
			  V4L2_CID_AUDIO_VOLUME, MA901_VOLUME_MIN,
			  MA901_VOLUME_MAX, 1, MA901_VOLUME_MAX);

	if (radio->hdl.error) {
		retval = radio->hdl.error;
		dev_err(&intf->dev, "couldn't register control\n");
		goto err_ctrl;
	}
	mutex_init(&radio->lock);

	radio->v4l2_dev.ctrl_handler = &radio->hdl;
	radio->v4l2_dev.release = usb_ma901radio_release;
	strlcpy(radio->vdev.name, radio->v4l2_dev.name,
		sizeof(radio->vdev.name));
	radio->vdev.v4l2_dev = &radio->v4l2_dev;
	radio->vdev.fops = &usb_ma901radio_fops;
	radio->vdev.ioctl_ops = &usb_ma901radio_ioctl_ops;
	radio->vdev.release = video_device_release_empty;
	radio->vdev.lock = &radio->lock;
	set_bit(V4L2_FL_USE_FH_PRIO, &radio->vdev.flags);

	radio->usbdev = interface_to_usbdev(intf);
	radio->intf = intf;
	usb_set_intfdata(intf, &radio->v4l2_dev);
	radio->curfreq = 95.21 * FREQ_MUL;

	video_set_drvdata(&radio->vdev, radio);

	/* TODO: we can get some statistics (freq, volume) from device
	 * but it's not implemented yet. After insertion in usb-port radio
	 * setups frequency and starts playing without any initialization.
	 * So we don't call usb_ma901radio_init/get_stat() here.
	 * retval = usb_ma901radio_init(radio);
	 */

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
static struct usb_device_id usb_ma901radio_device_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(USB_MA901_VENDOR, USB_MA901_PRODUCT,
							USB_CLASS_HID, 0, 0) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, usb_ma901radio_device_table);

/* USB subsystem interface */
static struct usb_driver usb_ma901radio_driver = {
	.name			= MA901_DRIVER_NAME,
	.probe			= usb_ma901radio_probe,
	.disconnect		= usb_ma901radio_disconnect,
	.suspend		= usb_ma901radio_suspend,
	.resume			= usb_ma901radio_resume,
	.reset_resume		= usb_ma901radio_resume,
	.id_table		= usb_ma901radio_device_table,
};

module_usb_driver(usb_ma901radio_driver);

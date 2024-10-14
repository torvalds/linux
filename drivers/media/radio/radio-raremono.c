// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2013 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/unaligned.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>

/*
 * 'Thanko's Raremono' is a Japanese si4734-based AM/FM/SW USB receiver:
 *
 * http://www.raremono.jp/product/484.html/
 *
 * The USB protocol has been reversed engineered using wireshark, initially
 * by Dinesh Ram <dinesh.ram@cern.ch> and finished by Hans Verkuil
 * <hverkuil@xs4all.nl>.
 *
 * Sadly the firmware used in this product hides lots of goodies since the
 * si4734 has more features than are supported by the firmware. Oh well...
 */

/* driver and module definitions */
MODULE_AUTHOR("Hans Verkuil <hverkuil@xs4all.nl>");
MODULE_DESCRIPTION("Thanko's Raremono AM/FM/SW Receiver USB driver");
MODULE_LICENSE("GPL v2");

/*
 * The Device announces itself as Cygnal Integrated Products, Inc.
 *
 * The vendor and product IDs (and in fact all other lsusb information as
 * well) are identical to the si470x Silicon Labs USB FM Radio Reference
 * Design board, even though this card has a si4734 device. Clearly the
 * designer of this product never bothered to change the USB IDs.
 */

/* USB Device ID List */
static const struct usb_device_id usb_raremono_device_table[] = {
	{USB_DEVICE_AND_INTERFACE_INFO(0x10c4, 0x818a, USB_CLASS_HID, 0, 0) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, usb_raremono_device_table);

#define BUFFER_LENGTH 64

/* Timeout is set to a high value, could probably be reduced. Need more tests */
#define USB_TIMEOUT 10000

/* Frequency limits in KHz */
#define FM_FREQ_RANGE_LOW	64000
#define FM_FREQ_RANGE_HIGH	108000

#define AM_FREQ_RANGE_LOW	520
#define AM_FREQ_RANGE_HIGH	1710

#define SW_FREQ_RANGE_LOW	2300
#define SW_FREQ_RANGE_HIGH	26100

enum { BAND_FM, BAND_AM, BAND_SW };

static const struct v4l2_frequency_band bands[] = {
	/* Band FM */
	{
		.type = V4L2_TUNER_RADIO,
		.index = 0,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
			      V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   = FM_FREQ_RANGE_LOW * 16,
		.rangehigh  = FM_FREQ_RANGE_HIGH * 16,
		.modulation = V4L2_BAND_MODULATION_FM,
	},
	/* Band AM */
	{
		.type = V4L2_TUNER_RADIO,
		.index = 1,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   = AM_FREQ_RANGE_LOW * 16,
		.rangehigh  = AM_FREQ_RANGE_HIGH * 16,
		.modulation = V4L2_BAND_MODULATION_AM,
	},
	/* Band SW */
	{
		.type = V4L2_TUNER_RADIO,
		.index = 2,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   = SW_FREQ_RANGE_LOW * 16,
		.rangehigh  = SW_FREQ_RANGE_HIGH * 16,
		.modulation = V4L2_BAND_MODULATION_AM,
	},
};

struct raremono_device {
	struct usb_device *usbdev;
	struct usb_interface *intf;
	struct video_device vdev;
	struct v4l2_device v4l2_dev;
	struct mutex lock;

	u8 *buffer;
	u32 band;
	unsigned curfreq;
};

static inline struct raremono_device *to_raremono_dev(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct raremono_device, v4l2_dev);
}

/* Set frequency. */
static int raremono_cmd_main(struct raremono_device *radio, unsigned band, unsigned freq)
{
	unsigned band_offset;
	int ret;

	switch (band) {
	case BAND_FM:
		band_offset = 1;
		freq /= 10;
		break;
	case BAND_AM:
		band_offset = 0;
		break;
	default:
		band_offset = 2;
		break;
	}
	radio->buffer[0] = 0x04 + band_offset;
	radio->buffer[1] = freq >> 8;
	radio->buffer[2] = freq & 0xff;

	ret = usb_control_msg(radio->usbdev, usb_sndctrlpipe(radio->usbdev, 0),
			HID_REQ_SET_REPORT,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT,
			0x0300 + radio->buffer[0], 2,
			radio->buffer, 3, USB_TIMEOUT);

	if (ret < 0) {
		dev_warn(radio->v4l2_dev.dev, "%s failed (%d)\n", __func__, ret);
		return ret;
	}
	radio->curfreq = (band == BAND_FM) ? freq * 10 : freq;
	return 0;
}

/* Handle unplugging the device.
 * We call video_unregister_device in any case.
 * The last function called in this procedure is
 * usb_raremono_device_release.
 */
static void usb_raremono_disconnect(struct usb_interface *intf)
{
	struct raremono_device *radio = to_raremono_dev(usb_get_intfdata(intf));

	dev_info(&intf->dev, "Thanko's Raremono disconnected\n");

	mutex_lock(&radio->lock);
	usb_set_intfdata(intf, NULL);
	video_unregister_device(&radio->vdev);
	v4l2_device_disconnect(&radio->v4l2_dev);
	mutex_unlock(&radio->lock);
	v4l2_device_put(&radio->v4l2_dev);
}

/*
 * Linux Video interface
 */
static int vidioc_querycap(struct file *file, void *priv,
					struct v4l2_capability *v)
{
	struct raremono_device *radio = video_drvdata(file);

	strscpy(v->driver, "radio-raremono", sizeof(v->driver));
	strscpy(v->card, "Thanko's Raremono", sizeof(v->card));
	usb_make_path(radio->usbdev, v->bus_info, sizeof(v->bus_info));
	return 0;
}

static int vidioc_enum_freq_bands(struct file *file, void *priv,
		struct v4l2_frequency_band *band)
{
	if (band->tuner != 0)
		return -EINVAL;

	if (band->index >= ARRAY_SIZE(bands))
		return -EINVAL;

	*band = bands[band->index];

	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
		struct v4l2_tuner *v)
{
	struct raremono_device *radio = video_drvdata(file);
	int ret;

	if (v->index > 0)
		return -EINVAL;

	strscpy(v->name, "AM/FM/SW", sizeof(v->name));
	v->capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
		V4L2_TUNER_CAP_FREQ_BANDS;
	v->rangelow = AM_FREQ_RANGE_LOW * 16;
	v->rangehigh = FM_FREQ_RANGE_HIGH * 16;
	v->rxsubchans = V4L2_TUNER_SUB_STEREO | V4L2_TUNER_SUB_MONO;
	v->audmode = (radio->curfreq < FM_FREQ_RANGE_LOW) ?
		V4L2_TUNER_MODE_MONO : V4L2_TUNER_MODE_STEREO;
	memset(radio->buffer, 1, BUFFER_LENGTH);
	ret = usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			1, 0xa1, 0x030d, 2, radio->buffer, BUFFER_LENGTH, USB_TIMEOUT);

	if (ret < 0) {
		dev_warn(radio->v4l2_dev.dev, "%s failed (%d)\n", __func__, ret);
		return ret;
	}
	v->signal = ((radio->buffer[1] & 0xf) << 8 | radio->buffer[2]) << 4;
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
					const struct v4l2_tuner *v)
{
	return v->index ? -EINVAL : 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
				const struct v4l2_frequency *f)
{
	struct raremono_device *radio = video_drvdata(file);
	u32 freq;
	unsigned band;

	if (f->tuner != 0 || f->type != V4L2_TUNER_RADIO)
		return -EINVAL;

	if (f->frequency >= (FM_FREQ_RANGE_LOW + SW_FREQ_RANGE_HIGH) * 8)
		band = BAND_FM;
	else if (f->frequency <= (AM_FREQ_RANGE_HIGH + SW_FREQ_RANGE_LOW) * 8)
		band = BAND_AM;
	else
		band = BAND_SW;

	freq = clamp_t(u32, f->frequency, bands[band].rangelow, bands[band].rangehigh);
	return raremono_cmd_main(radio, band, freq / 16);
}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct raremono_device *radio = video_drvdata(file);

	if (f->tuner != 0)
		return -EINVAL;
	f->type = V4L2_TUNER_RADIO;
	f->frequency = radio->curfreq * 16;
	return 0;
}

static void raremono_device_release(struct v4l2_device *v4l2_dev)
{
	struct raremono_device *radio = to_raremono_dev(v4l2_dev);

	kfree(radio->buffer);
	kfree(radio);
}

/* File system interface */
static const struct v4l2_file_operations usb_raremono_fops = {
	.owner		= THIS_MODULE,
	.open           = v4l2_fh_open,
	.release        = v4l2_fh_release,
	.unlocked_ioctl	= video_ioctl2,
};

static const struct v4l2_ioctl_ops usb_raremono_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	.vidioc_g_tuner = vidioc_g_tuner,
	.vidioc_s_tuner = vidioc_s_tuner,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
	.vidioc_enum_freq_bands = vidioc_enum_freq_bands,
};

/* check if the device is present and register with v4l and usb if it is */
static int usb_raremono_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct raremono_device *radio;
	int retval = 0;

	radio = kzalloc(sizeof(*radio), GFP_KERNEL);
	if (!radio)
		return -ENOMEM;
	radio->buffer = kmalloc(BUFFER_LENGTH, GFP_KERNEL);
	if (!radio->buffer) {
		kfree(radio);
		return -ENOMEM;
	}

	radio->usbdev = interface_to_usbdev(intf);
	radio->intf = intf;

	/*
	 * This device uses the same USB IDs as the si470x SiLabs reference
	 * design. So do an additional check: attempt to read the device ID
	 * from the si470x: the lower 12 bits are 0x0242 for the si470x. The
	 * Raremono always returns 0x0800 (the meaning of that is unknown, but
	 * at least it works).
	 *
	 * We use this check to determine which device we are dealing with.
	 */
	msleep(20);
	retval = usb_control_msg(radio->usbdev,
		usb_rcvctrlpipe(radio->usbdev, 0),
		HID_REQ_GET_REPORT,
		USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
		1, 2,
		radio->buffer, 3, 500);
	if (retval != 3 ||
	    (get_unaligned_be16(&radio->buffer[1]) & 0xfff) == 0x0242) {
		dev_info(&intf->dev, "this is not Thanko's Raremono.\n");
		retval = -ENODEV;
		goto free_mem;
	}

	dev_info(&intf->dev, "Thanko's Raremono connected: (%04X:%04X)\n",
			id->idVendor, id->idProduct);

	retval = v4l2_device_register(&intf->dev, &radio->v4l2_dev);
	if (retval < 0) {
		dev_err(&intf->dev, "couldn't register v4l2_device\n");
		goto free_mem;
	}

	mutex_init(&radio->lock);

	strscpy(radio->vdev.name, radio->v4l2_dev.name,
		sizeof(radio->vdev.name));
	radio->vdev.v4l2_dev = &radio->v4l2_dev;
	radio->vdev.fops = &usb_raremono_fops;
	radio->vdev.ioctl_ops = &usb_raremono_ioctl_ops;
	radio->vdev.lock = &radio->lock;
	radio->vdev.release = video_device_release_empty;
	radio->vdev.device_caps = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	radio->v4l2_dev.release = raremono_device_release;

	usb_set_intfdata(intf, &radio->v4l2_dev);

	video_set_drvdata(&radio->vdev, radio);

	raremono_cmd_main(radio, BAND_FM, 95160);

	retval = video_register_device(&radio->vdev, VFL_TYPE_RADIO, -1);
	if (retval == 0) {
		dev_info(&intf->dev, "V4L2 device registered as %s\n",
				video_device_node_name(&radio->vdev));
		return 0;
	}
	dev_err(&intf->dev, "could not register video device\n");
	v4l2_device_unregister(&radio->v4l2_dev);

free_mem:
	kfree(radio->buffer);
	kfree(radio);
	return retval;
}

/* USB subsystem interface */
static struct usb_driver usb_raremono_driver = {
	.name			= "radio-raremono",
	.probe			= usb_raremono_probe,
	.disconnect		= usb_raremono_disconnect,
	.id_table		= usb_raremono_device_table,
};

module_usb_driver(usb_raremono_driver);

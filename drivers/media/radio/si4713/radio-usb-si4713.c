// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2013 Cisco Systems, Inc. and/or its affiliates.
 * All rights reserved.
 */

/* kernel includes */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
/* V4l includes */
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <linux/platform_data/media/si4713.h>

#include "si4713.h"

/* driver and module definitions */
MODULE_AUTHOR("Dinesh Ram <dinesh.ram@cern.ch>");
MODULE_DESCRIPTION("Si4713 FM Transmitter USB driver");
MODULE_LICENSE("GPL v2");

/* The Device announces itself as Cygnal Integrated Products, Inc. */
#define USB_SI4713_VENDOR		0x10c4
#define USB_SI4713_PRODUCT		0x8244

#define BUFFER_LENGTH			64
#define USB_TIMEOUT			1000
#define USB_RESP_TIMEOUT		50000

/* USB Device ID List */
static const struct usb_device_id usb_si4713_usb_device_table[] = {
	{USB_DEVICE_AND_INTERFACE_INFO(USB_SI4713_VENDOR, USB_SI4713_PRODUCT,
							USB_CLASS_HID, 0, 0) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, usb_si4713_usb_device_table);

struct si4713_usb_device {
	struct usb_device	*usbdev;
	struct usb_interface	*intf;
	struct video_device	vdev;
	struct v4l2_device	v4l2_dev;
	struct v4l2_subdev	*v4l2_subdev;
	struct mutex		lock;
	struct i2c_adapter	i2c_adapter;

	u8			*buffer;
};

static inline struct si4713_usb_device *to_si4713_dev(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct si4713_usb_device, v4l2_dev);
}

static int vidioc_querycap(struct file *file, void *priv,
					struct v4l2_capability *v)
{
	struct si4713_usb_device *radio = video_drvdata(file);

	strscpy(v->driver, "radio-usb-si4713", sizeof(v->driver));
	strscpy(v->card, "Si4713 FM Transmitter", sizeof(v->card));
	usb_make_path(radio->usbdev, v->bus_info, sizeof(v->bus_info));
	return 0;
}

static int vidioc_g_modulator(struct file *file, void *priv,
				struct v4l2_modulator *vm)
{
	struct si4713_usb_device *radio = video_drvdata(file);

	return v4l2_subdev_call(radio->v4l2_subdev, tuner, g_modulator, vm);
}

static int vidioc_s_modulator(struct file *file, void *priv,
				const struct v4l2_modulator *vm)
{
	struct si4713_usb_device *radio = video_drvdata(file);

	return v4l2_subdev_call(radio->v4l2_subdev, tuner, s_modulator, vm);
}

static int vidioc_s_frequency(struct file *file, void *priv,
				const struct v4l2_frequency *vf)
{
	struct si4713_usb_device *radio = video_drvdata(file);

	return v4l2_subdev_call(radio->v4l2_subdev, tuner, s_frequency, vf);
}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *vf)
{
	struct si4713_usb_device *radio = video_drvdata(file);

	return v4l2_subdev_call(radio->v4l2_subdev, tuner, g_frequency, vf);
}

static const struct v4l2_ioctl_ops usb_si4713_ioctl_ops = {
	.vidioc_querycap	  = vidioc_querycap,
	.vidioc_g_modulator	  = vidioc_g_modulator,
	.vidioc_s_modulator	  = vidioc_s_modulator,
	.vidioc_g_frequency	  = vidioc_g_frequency,
	.vidioc_s_frequency	  = vidioc_s_frequency,
	.vidioc_log_status	  = v4l2_ctrl_log_status,
	.vidioc_subscribe_event   = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

/* File system interface */
static const struct v4l2_file_operations usb_si4713_fops = {
	.owner		= THIS_MODULE,
	.open           = v4l2_fh_open,
	.release        = v4l2_fh_release,
	.poll           = v4l2_ctrl_poll,
	.unlocked_ioctl	= video_ioctl2,
};

static void usb_si4713_video_device_release(struct v4l2_device *v4l2_dev)
{
	struct si4713_usb_device *radio = to_si4713_dev(v4l2_dev);
	struct i2c_adapter *adapter = &radio->i2c_adapter;

	i2c_del_adapter(adapter);
	v4l2_device_unregister(&radio->v4l2_dev);
	kfree(radio->buffer);
	kfree(radio);
}

/*
 * This command sequence emulates the behaviour of the Windows driver.
 * The structure of these commands was determined by sniffing the
 * usb traffic of the device during startup.
 * Most likely, these commands make some queries to the device.
 * Commands are sent to enquire parameters like the bus mode,
 * component revision, boot mode, the device serial number etc.
 *
 * These commands are necessary to be sent in this order during startup.
 * The device fails to powerup if these commands are not sent.
 *
 * The complete list of startup commands is given in the start_seq table below.
 */
static int si4713_send_startup_command(struct si4713_usb_device *radio)
{
	unsigned long until_jiffies = jiffies + usecs_to_jiffies(USB_RESP_TIMEOUT) + 1;
	u8 *buffer = radio->buffer;
	int retval;

	/* send the command */
	retval = usb_control_msg(radio->usbdev, usb_sndctrlpipe(radio->usbdev, 0),
					0x09, 0x21, 0x033f, 0, radio->buffer,
					BUFFER_LENGTH, USB_TIMEOUT);
	if (retval < 0)
		return retval;

	for (;;) {
		/* receive the response */
		retval = usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
				0x01, 0xa1, 0x033f, 0, radio->buffer,
				BUFFER_LENGTH, USB_TIMEOUT);
		if (retval < 0)
			return retval;
		if (!radio->buffer[1]) {
			/* USB traffic sniffing showed that some commands require
			 * additional checks. */
			switch (buffer[1]) {
			case 0x32:
				if (radio->buffer[2] == 0)
					return 0;
				break;
			case 0x14:
			case 0x12:
				if (radio->buffer[2] & SI4713_CTS)
					return 0;
				break;
			case 0x06:
				if ((radio->buffer[2] & SI4713_CTS) && radio->buffer[9] == 0x08)
					return 0;
				break;
			default:
				return 0;
			}
		}
		if (time_is_before_jiffies(until_jiffies))
			return -EIO;
		msleep(3);
	}

	return retval;
}

struct si4713_start_seq_table {
	int len;
	u8 payload[8];
};

/*
 * Some of the startup commands that could be recognized are :
 * (0x03): Get serial number of the board (Response : CB000-00-00)
 * (0x06, 0x03, 0x03, 0x08, 0x01, 0x0f) : Get Component revision
 */
static const struct si4713_start_seq_table start_seq[] = {

	{ 1, { 0x03 } },
	{ 2, { 0x32, 0x7f } },
	{ 6, { 0x06, 0x03, 0x03, 0x08, 0x01, 0x0f } },
	{ 2, { 0x14, 0x02 } },
	{ 2, { 0x09, 0x90 } },
	{ 3, { 0x08, 0x90, 0xfa } },
	{ 2, { 0x36, 0x01 } },
	{ 2, { 0x05, 0x03 } },
	{ 7, { 0x06, 0x00, 0x06, 0x0e, 0x01, 0x0f, 0x05 } },
	{ 1, { 0x12 } },
	/* Commands that are sent after pressing the 'Initialize'
		button in the windows application */
	{ 1, { 0x03 } },
	{ 1, { 0x01 } },
	{ 2, { 0x09, 0x90 } },
	{ 3, { 0x08, 0x90, 0xfa } },
	{ 1, { 0x34 } },
	{ 2, { 0x35, 0x01 } },
	{ 2, { 0x36, 0x01 } },
	{ 2, { 0x30, 0x09 } },
	{ 4, { 0x30, 0x06, 0x00, 0xe2 } },
	{ 3, { 0x31, 0x01, 0x30 } },
	{ 3, { 0x31, 0x04, 0x09 } },
	{ 2, { 0x05, 0x02 } },
	{ 6, { 0x06, 0x03, 0x03, 0x08, 0x01, 0x0f } },
};

static int si4713_start_seq(struct si4713_usb_device *radio)
{
	int retval = 0;
	int i;

	radio->buffer[0] = 0x3f;

	for (i = 0; i < ARRAY_SIZE(start_seq); i++) {
		int len = start_seq[i].len;
		const u8 *payload = start_seq[i].payload;

		memcpy(radio->buffer + 1, payload, len);
		memset(radio->buffer + len + 1, 0, BUFFER_LENGTH - 1 - len);
		retval = si4713_send_startup_command(radio);
	}

	return retval;
}

static struct i2c_board_info si4713_board_info = {
	I2C_BOARD_INFO("si4713", SI4713_I2C_ADDR_BUSEN_HIGH),
};

struct si4713_command_table {
	int command_id;
	u8 payload[8];
};

/*
 * Structure of a command :
 *	Byte 1 : 0x3f (always)
 *	Byte 2 : 0x06 (send a command)
 *	Byte 3 : Unknown
 *	Byte 4 : Number of arguments + 1 (for the command byte)
 *	Byte 5 : Number of response bytes
 */
static struct si4713_command_table command_table[] = {

	{ SI4713_CMD_POWER_UP,		{ 0x00, SI4713_PWUP_NARGS + 1, SI4713_PWUP_NRESP} },
	{ SI4713_CMD_GET_REV,		{ 0x03, 0x01, SI4713_GETREV_NRESP } },
	{ SI4713_CMD_POWER_DOWN,	{ 0x00, 0x01, SI4713_PWDN_NRESP} },
	{ SI4713_CMD_SET_PROPERTY,	{ 0x00, SI4713_SET_PROP_NARGS + 1, SI4713_SET_PROP_NRESP } },
	{ SI4713_CMD_GET_PROPERTY,	{ 0x00, SI4713_GET_PROP_NARGS + 1, SI4713_GET_PROP_NRESP } },
	{ SI4713_CMD_TX_TUNE_FREQ,	{ 0x03, SI4713_TXFREQ_NARGS + 1, SI4713_TXFREQ_NRESP } },
	{ SI4713_CMD_TX_TUNE_POWER,	{ 0x03, SI4713_TXPWR_NARGS + 1, SI4713_TXPWR_NRESP } },
	{ SI4713_CMD_TX_TUNE_MEASURE,	{ 0x03, SI4713_TXMEA_NARGS + 1, SI4713_TXMEA_NRESP } },
	{ SI4713_CMD_TX_TUNE_STATUS,	{ 0x00, SI4713_TXSTATUS_NARGS + 1, SI4713_TXSTATUS_NRESP } },
	{ SI4713_CMD_TX_ASQ_STATUS,	{ 0x03, SI4713_ASQSTATUS_NARGS + 1, SI4713_ASQSTATUS_NRESP } },
	{ SI4713_CMD_GET_INT_STATUS,	{ 0x03, 0x01, SI4713_GET_STATUS_NRESP } },
	{ SI4713_CMD_TX_RDS_BUFF,	{ 0x03, SI4713_RDSBUFF_NARGS + 1, SI4713_RDSBUFF_NRESP } },
	{ SI4713_CMD_TX_RDS_PS,		{ 0x00, SI4713_RDSPS_NARGS + 1, SI4713_RDSPS_NRESP } },
};

static int send_command(struct si4713_usb_device *radio, u8 *payload, char *data, int len)
{
	int retval;

	radio->buffer[0] = 0x3f;
	radio->buffer[1] = 0x06;

	memcpy(radio->buffer + 2, payload, 3);
	memcpy(radio->buffer + 5, data, len);
	memset(radio->buffer + 5 + len, 0, BUFFER_LENGTH - 5 - len);

	/* send the command */
	retval = usb_control_msg(radio->usbdev, usb_sndctrlpipe(radio->usbdev, 0),
					0x09, 0x21, 0x033f, 0, radio->buffer,
					BUFFER_LENGTH, USB_TIMEOUT);

	return retval < 0 ? retval : 0;
}

static int si4713_i2c_read(struct si4713_usb_device *radio, char *data, int len)
{
	unsigned long until_jiffies = jiffies + usecs_to_jiffies(USB_RESP_TIMEOUT) + 1;
	int retval;

	/* receive the response */
	for (;;) {
		retval = usb_control_msg(radio->usbdev,
					usb_rcvctrlpipe(radio->usbdev, 0),
					0x01, 0xa1, 0x033f, 0, radio->buffer,
					BUFFER_LENGTH, USB_TIMEOUT);
		if (retval < 0)
			return retval;

		/*
		 * Check that we get a valid reply back (buffer[1] == 0) and
		 * that CTS is set before returning, otherwise we wait and try
		 * again. The i2c driver also does the CTS check, but the timeouts
		 * used there are much too small for this USB driver, so we wait
		 * for it here.
		 */
		if (radio->buffer[1] == 0 && (radio->buffer[2] & SI4713_CTS)) {
			memcpy(data, radio->buffer + 2, len);
			return 0;
		}
		if (time_is_before_jiffies(until_jiffies)) {
			/* Zero the status value, ensuring CTS isn't set */
			data[0] = 0;
			return 0;
		}
		msleep(3);
	}
}

static int si4713_i2c_write(struct si4713_usb_device *radio, char *data, int len)
{
	int retval = -EINVAL;
	int i;

	if (len > BUFFER_LENGTH - 5)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(command_table); i++) {
		if (data[0] == command_table[i].command_id)
			retval = send_command(radio, command_table[i].payload,
						data, len);
	}

	return retval < 0 ? retval : 0;
}

static int si4713_transfer(struct i2c_adapter *i2c_adapter,
				struct i2c_msg *msgs, int num)
{
	struct si4713_usb_device *radio = i2c_get_adapdata(i2c_adapter);
	int retval = -EINVAL;
	int i;

	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_RD)
			retval = si4713_i2c_read(radio, msgs[i].buf, msgs[i].len);
		else
			retval = si4713_i2c_write(radio, msgs[i].buf, msgs[i].len);
		if (retval)
			break;
	}

	return retval ? retval : num;
}

static u32 si4713_functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm si4713_algo = {
	.master_xfer   = si4713_transfer,
	.functionality = si4713_functionality,
};

/* This name value shows up in the sysfs filename associated
		with this I2C adapter */
static const struct i2c_adapter si4713_i2c_adapter_template = {
	.name   = "si4713-i2c",
	.owner  = THIS_MODULE,
	.algo   = &si4713_algo,
};

static int si4713_register_i2c_adapter(struct si4713_usb_device *radio)
{
	radio->i2c_adapter = si4713_i2c_adapter_template;
	/* set up sysfs linkage to our parent device */
	radio->i2c_adapter.dev.parent = &radio->usbdev->dev;
	i2c_set_adapdata(&radio->i2c_adapter, radio);

	return i2c_add_adapter(&radio->i2c_adapter);
}

/* check if the device is present and register with v4l and usb if it is */
static int usb_si4713_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct si4713_usb_device *radio;
	struct i2c_adapter *adapter;
	struct v4l2_subdev *sd;
	int retval;

	dev_info(&intf->dev, "Si4713 development board discovered: (%04X:%04X)\n",
			id->idVendor, id->idProduct);

	/* Initialize local device structure */
	radio = kzalloc(sizeof(struct si4713_usb_device), GFP_KERNEL);
	if (radio)
		radio->buffer = kmalloc(BUFFER_LENGTH, GFP_KERNEL);

	if (!radio || !radio->buffer) {
		dev_err(&intf->dev, "kmalloc for si4713_usb_device failed\n");
		kfree(radio);
		return -ENOMEM;
	}

	mutex_init(&radio->lock);

	radio->usbdev = interface_to_usbdev(intf);
	radio->intf = intf;
	usb_set_intfdata(intf, &radio->v4l2_dev);

	retval = si4713_start_seq(radio);
	if (retval < 0)
		goto err_v4l2;

	retval = v4l2_device_register(&intf->dev, &radio->v4l2_dev);
	if (retval < 0) {
		dev_err(&intf->dev, "couldn't register v4l2_device\n");
		goto err_v4l2;
	}

	retval = si4713_register_i2c_adapter(radio);
	if (retval < 0) {
		dev_err(&intf->dev, "could not register i2c device\n");
		goto err_i2cdev;
	}

	adapter = &radio->i2c_adapter;
	sd = v4l2_i2c_new_subdev_board(&radio->v4l2_dev, adapter,
					  &si4713_board_info, NULL);
	radio->v4l2_subdev = sd;
	if (!sd) {
		dev_err(&intf->dev, "cannot get v4l2 subdevice\n");
		retval = -ENODEV;
		goto del_adapter;
	}

	radio->vdev.ctrl_handler = sd->ctrl_handler;
	radio->v4l2_dev.release = usb_si4713_video_device_release;
	strscpy(radio->vdev.name, radio->v4l2_dev.name,
		sizeof(radio->vdev.name));
	radio->vdev.v4l2_dev = &radio->v4l2_dev;
	radio->vdev.fops = &usb_si4713_fops;
	radio->vdev.ioctl_ops = &usb_si4713_ioctl_ops;
	radio->vdev.lock = &radio->lock;
	radio->vdev.release = video_device_release_empty;
	radio->vdev.vfl_dir = VFL_DIR_TX;
	radio->vdev.device_caps = V4L2_CAP_MODULATOR | V4L2_CAP_RDS_OUTPUT;

	video_set_drvdata(&radio->vdev, radio);

	retval = video_register_device(&radio->vdev, VFL_TYPE_RADIO, -1);
	if (retval < 0) {
		dev_err(&intf->dev, "could not register video device\n");
		goto del_adapter;
	}

	dev_info(&intf->dev, "V4L2 device registered as %s\n",
			video_device_node_name(&radio->vdev));

	return 0;

del_adapter:
	i2c_del_adapter(adapter);
err_i2cdev:
	v4l2_device_unregister(&radio->v4l2_dev);
err_v4l2:
	kfree(radio->buffer);
	kfree(radio);
	return retval;
}

static void usb_si4713_disconnect(struct usb_interface *intf)
{
	struct si4713_usb_device *radio = to_si4713_dev(usb_get_intfdata(intf));

	dev_info(&intf->dev, "Si4713 development board now disconnected\n");

	mutex_lock(&radio->lock);
	usb_set_intfdata(intf, NULL);
	video_unregister_device(&radio->vdev);
	v4l2_device_disconnect(&radio->v4l2_dev);
	mutex_unlock(&radio->lock);
	v4l2_device_put(&radio->v4l2_dev);
}

/* USB subsystem interface */
static struct usb_driver usb_si4713_driver = {
	.name			= "radio-usb-si4713",
	.probe			= usb_si4713_probe,
	.disconnect		= usb_si4713_disconnect,
	.id_table		= usb_si4713_usb_device_table,
};

module_usb_driver(usb_si4713_driver);

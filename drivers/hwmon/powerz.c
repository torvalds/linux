// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2023 Thomas Weißschuh <linux@weissschuh.net>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/usb.h>

#define DRIVER_NAME	"powerz"
#define POWERZ_EP_CMD_OUT	0x01
#define POWERZ_EP_DATA_IN	0x81

struct powerz_sensor_data {
	u8 _unknown_1[8];
	__le32 V_bus;
	__le32 I_bus;
	__le32 V_bus_avg;
	__le32 I_bus_avg;
	u8 _unknown_2[8];
	u8 temp[2];
	__le16 V_cc1;
	__le16 V_cc2;
	__le16 V_dp;
	__le16 V_dm;
	__le16 V_dd;
	u8 _unknown_3[4];
} __packed;

struct powerz_priv {
	char transfer_buffer[64];	/* first member to satisfy DMA alignment */
	struct mutex mutex;
	struct completion completion;
	struct urb *urb;
	int status;
};

static const struct hwmon_channel_info *const powerz_info[] = {
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL | HWMON_I_AVERAGE,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL | HWMON_C_AVERAGE),
	    HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_LABEL),
	NULL
};

static umode_t powerz_is_visible(const void *data, enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	return 0444;
}

static int powerz_read_string(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, const char **str)
{
	if (type == hwmon_curr && attr == hwmon_curr_label) {
		*str = "IBUS";
	} else if (type == hwmon_in && attr == hwmon_in_label) {
		if (channel == 0)
			*str = "VBUS";
		else if (channel == 1)
			*str = "VCC1";
		else if (channel == 2)
			*str = "VCC2";
		else if (channel == 3)
			*str = "VDP";
		else if (channel == 4)
			*str = "VDM";
		else if (channel == 5)
			*str = "VDD";
		else
			return -EOPNOTSUPP;
	} else if (type == hwmon_temp && attr == hwmon_temp_label) {
		*str = "TEMP";
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

static void powerz_usb_data_complete(struct urb *urb)
{
	struct powerz_priv *priv = urb->context;

	complete(&priv->completion);
}

static void powerz_usb_cmd_complete(struct urb *urb)
{
	struct powerz_priv *priv = urb->context;

	usb_fill_bulk_urb(urb, urb->dev,
			  usb_rcvbulkpipe(urb->dev, POWERZ_EP_DATA_IN),
			  priv->transfer_buffer, sizeof(priv->transfer_buffer),
			  powerz_usb_data_complete, priv);

	priv->status = usb_submit_urb(urb, GFP_ATOMIC);
	if (priv->status)
		complete(&priv->completion);
}

static int powerz_read_data(struct usb_device *udev, struct powerz_priv *priv)
{
	int ret;

	priv->status = -ETIMEDOUT;
	reinit_completion(&priv->completion);

	priv->transfer_buffer[0] = 0x0c;
	priv->transfer_buffer[1] = 0x00;
	priv->transfer_buffer[2] = 0x02;
	priv->transfer_buffer[3] = 0x00;

	usb_fill_bulk_urb(priv->urb, udev,
			  usb_sndbulkpipe(udev, POWERZ_EP_CMD_OUT),
			  priv->transfer_buffer, 4, powerz_usb_cmd_complete,
			  priv);
	ret = usb_submit_urb(priv->urb, GFP_KERNEL);
	if (ret)
		return ret;

	if (!wait_for_completion_interruptible_timeout
	    (&priv->completion, msecs_to_jiffies(5))) {
		usb_kill_urb(priv->urb);
		return -EIO;
	}

	if (priv->urb->actual_length < sizeof(struct powerz_sensor_data))
		return -EIO;

	return priv->status;
}

static int powerz_read(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long *val)
{
	struct usb_interface *intf = to_usb_interface(dev->parent);
	struct usb_device *udev = interface_to_usbdev(intf);
	struct powerz_priv *priv = usb_get_intfdata(intf);
	struct powerz_sensor_data *data;
	int ret;

	if (!priv)
		return -EIO;	/* disconnected */

	mutex_lock(&priv->mutex);
	ret = powerz_read_data(udev, priv);
	if (ret)
		goto out;

	data = (struct powerz_sensor_data *)priv->transfer_buffer;

	if (type == hwmon_curr) {
		if (attr == hwmon_curr_input)
			*val = ((s32)le32_to_cpu(data->I_bus)) / 1000;
		else if (attr == hwmon_curr_average)
			*val = ((s32)le32_to_cpu(data->I_bus_avg)) / 1000;
		else
			ret = -EOPNOTSUPP;
	} else if (type == hwmon_in) {
		if (attr == hwmon_in_input) {
			if (channel == 0)
				*val = le32_to_cpu(data->V_bus) / 1000;
			else if (channel == 1)
				*val = le16_to_cpu(data->V_cc1) / 10;
			else if (channel == 2)
				*val = le16_to_cpu(data->V_cc2) / 10;
			else if (channel == 3)
				*val = le16_to_cpu(data->V_dp) / 10;
			else if (channel == 4)
				*val = le16_to_cpu(data->V_dm) / 10;
			else if (channel == 5)
				*val = le16_to_cpu(data->V_dd) / 10;
			else
				ret = -EOPNOTSUPP;
		} else if (attr == hwmon_in_average && channel == 0) {
			*val = le32_to_cpu(data->V_bus_avg) / 1000;
		} else {
			ret = -EOPNOTSUPP;
		}
	} else if (type == hwmon_temp && attr == hwmon_temp_input) {
		*val = data->temp[1] * 2000 + data->temp[0] * 1000 / 128;
	} else {
		ret = -EOPNOTSUPP;
	}

out:
	mutex_unlock(&priv->mutex);
	return ret;
}

static const struct hwmon_ops powerz_hwmon_ops = {
	.is_visible = powerz_is_visible,
	.read = powerz_read,
	.read_string = powerz_read_string,
};

static const struct hwmon_chip_info powerz_chip_info = {
	.ops = &powerz_hwmon_ops,
	.info = powerz_info,
};

static int powerz_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct powerz_priv *priv;
	struct device *hwmon_dev;
	struct device *parent;

	parent = &intf->dev;

	priv = devm_kzalloc(parent, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!priv->urb)
		return -ENOMEM;
	mutex_init(&priv->mutex);
	init_completion(&priv->completion);

	hwmon_dev =
	    devm_hwmon_device_register_with_info(parent, DRIVER_NAME, priv,
						 &powerz_chip_info, NULL);
	if (IS_ERR(hwmon_dev)) {
		usb_free_urb(priv->urb);
		return PTR_ERR(hwmon_dev);
	}

	usb_set_intfdata(intf, priv);

	return 0;
}

static void powerz_disconnect(struct usb_interface *intf)
{
	struct powerz_priv *priv = usb_get_intfdata(intf);

	mutex_lock(&priv->mutex);
	usb_kill_urb(priv->urb);
	usb_free_urb(priv->urb);
	mutex_unlock(&priv->mutex);
}

static const struct usb_device_id powerz_id_table[] = {
	{ USB_DEVICE_INTERFACE_NUMBER(0x5FC9, 0x0061, 0x00) },	/* ChargerLAB POWER-Z KM002C */
	{ USB_DEVICE_INTERFACE_NUMBER(0x5FC9, 0x0063, 0x00) },	/* ChargerLAB POWER-Z KM003C */
	{ }
};

MODULE_DEVICE_TABLE(usb, powerz_id_table);

static struct usb_driver powerz_driver = {
	.name = DRIVER_NAME,
	.id_table = powerz_id_table,
	.probe = powerz_probe,
	.disconnect = powerz_disconnect,
};

module_usb_driver(powerz_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Weißschuh <linux@weissschuh.net>");
MODULE_DESCRIPTION("ChargerLAB POWER-Z USB-C tester");

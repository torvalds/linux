// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sbrmi.c - hwmon driver for a SB-RMI mailbox
 *           compliant AMD SoC device.
 *
 * Copyright (C) 2020-2021 Advanced Micro Devices, Inc.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>

/* Do not allow setting negative power limit */
#define SBRMI_PWR_MIN	0
/* Mask for Status Register bit[1] */
#define SW_ALERT_MASK	0x2

/* Software Interrupt for triggering */
#define START_CMD	0x80
#define TRIGGER_MAILBOX	0x01

/*
 * SB-RMI supports soft mailbox service request to MP1 (power management
 * firmware) through SBRMI inbound/outbound message registers.
 * SB-RMI message IDs
 */
enum sbrmi_msg_id {
	SBRMI_READ_PKG_PWR_CONSUMPTION = 0x1,
	SBRMI_WRITE_PKG_PWR_LIMIT,
	SBRMI_READ_PKG_PWR_LIMIT,
	SBRMI_READ_PKG_MAX_PWR_LIMIT,
};

/* SB-RMI registers */
enum sbrmi_reg {
	SBRMI_CTRL		= 0x01,
	SBRMI_STATUS,
	SBRMI_OUTBNDMSG0	= 0x30,
	SBRMI_OUTBNDMSG1,
	SBRMI_OUTBNDMSG2,
	SBRMI_OUTBNDMSG3,
	SBRMI_OUTBNDMSG4,
	SBRMI_OUTBNDMSG5,
	SBRMI_OUTBNDMSG6,
	SBRMI_OUTBNDMSG7,
	SBRMI_INBNDMSG0,
	SBRMI_INBNDMSG1,
	SBRMI_INBNDMSG2,
	SBRMI_INBNDMSG3,
	SBRMI_INBNDMSG4,
	SBRMI_INBNDMSG5,
	SBRMI_INBNDMSG6,
	SBRMI_INBNDMSG7,
	SBRMI_SW_INTERRUPT,
};

/* Each client has this additional data */
struct sbrmi_data {
	struct i2c_client *client;
	struct mutex lock;
	u32 pwr_limit_max;
};

struct sbrmi_mailbox_msg {
	u8 cmd;
	bool read;
	u32 data_in;
	u32 data_out;
};

static int sbrmi_enable_alert(struct i2c_client *client)
{
	int ctrl;

	/*
	 * Enable the SB-RMI Software alert status
	 * by writing 0 to bit 4 of Control register(0x1)
	 */
	ctrl = i2c_smbus_read_byte_data(client, SBRMI_CTRL);
	if (ctrl < 0)
		return ctrl;

	if (ctrl & 0x10) {
		ctrl &= ~0x10;
		return i2c_smbus_write_byte_data(client,
						 SBRMI_CTRL, ctrl);
	}

	return 0;
}

static int rmi_mailbox_xfer(struct sbrmi_data *data,
			    struct sbrmi_mailbox_msg *msg)
{
	int i, ret, retry = 10;
	int sw_status;
	u8 byte;

	mutex_lock(&data->lock);

	/* Indicate firmware a command is to be serviced */
	ret = i2c_smbus_write_byte_data(data->client,
					SBRMI_INBNDMSG7, START_CMD);
	if (ret < 0)
		goto exit_unlock;

	/* Write the command to SBRMI::InBndMsg_inst0 */
	ret = i2c_smbus_write_byte_data(data->client,
					SBRMI_INBNDMSG0, msg->cmd);
	if (ret < 0)
		goto exit_unlock;

	/*
	 * For both read and write the initiator (BMC) writes
	 * Command Data In[31:0] to SBRMI::InBndMsg_inst[4:1]
	 * SBRMI_x3C(MSB):SBRMI_x39(LSB)
	 */
	for (i = 0; i < 4; i++) {
		byte = (msg->data_in >> i * 8) & 0xff;
		ret = i2c_smbus_write_byte_data(data->client,
						SBRMI_INBNDMSG1 + i, byte);
		if (ret < 0)
			goto exit_unlock;
	}

	/*
	 * Write 0x01 to SBRMI::SoftwareInterrupt to notify firmware to
	 * perform the requested read or write command
	 */
	ret = i2c_smbus_write_byte_data(data->client,
					SBRMI_SW_INTERRUPT, TRIGGER_MAILBOX);
	if (ret < 0)
		goto exit_unlock;

	/*
	 * Firmware will write SBRMI::Status[SwAlertSts]=1 to generate
	 * an ALERT (if enabled) to initiator (BMC) to indicate completion
	 * of the requested command
	 */
	do {
		sw_status = i2c_smbus_read_byte_data(data->client,
						     SBRMI_STATUS);
		if (sw_status < 0) {
			ret = sw_status;
			goto exit_unlock;
		}
		if (sw_status & SW_ALERT_MASK)
			break;
		usleep_range(50, 100);
	} while (retry--);

	if (retry < 0) {
		dev_err(&data->client->dev,
			"Firmware fail to indicate command completion\n");
		ret = -EIO;
		goto exit_unlock;
	}

	/*
	 * For a read operation, the initiator (BMC) reads the firmware
	 * response Command Data Out[31:0] from SBRMI::OutBndMsg_inst[4:1]
	 * {SBRMI_x34(MSB):SBRMI_x31(LSB)}.
	 */
	if (msg->read) {
		for (i = 0; i < 4; i++) {
			ret = i2c_smbus_read_byte_data(data->client,
						       SBRMI_OUTBNDMSG1 + i);
			if (ret < 0)
				goto exit_unlock;
			msg->data_out |= ret << i * 8;
		}
	}

	/*
	 * BMC must write 1'b1 to SBRMI::Status[SwAlertSts] to clear the
	 * ALERT to initiator
	 */
	ret = i2c_smbus_write_byte_data(data->client, SBRMI_STATUS,
					sw_status | SW_ALERT_MASK);

exit_unlock:
	mutex_unlock(&data->lock);
	return ret;
}

static int sbrmi_read(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long *val)
{
	struct sbrmi_data *data = dev_get_drvdata(dev);
	struct sbrmi_mailbox_msg msg = { 0 };
	int ret;

	if (type != hwmon_power)
		return -EINVAL;

	msg.read = true;
	switch (attr) {
	case hwmon_power_input:
		msg.cmd = SBRMI_READ_PKG_PWR_CONSUMPTION;
		ret = rmi_mailbox_xfer(data, &msg);
		break;
	case hwmon_power_cap:
		msg.cmd = SBRMI_READ_PKG_PWR_LIMIT;
		ret = rmi_mailbox_xfer(data, &msg);
		break;
	case hwmon_power_cap_max:
		msg.data_out = data->pwr_limit_max;
		ret = 0;
		break;
	default:
		return -EINVAL;
	}
	if (ret < 0)
		return ret;
	/* hwmon power attributes are in microWatt */
	*val = (long)msg.data_out * 1000;
	return ret;
}

static int sbrmi_write(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long val)
{
	struct sbrmi_data *data = dev_get_drvdata(dev);
	struct sbrmi_mailbox_msg msg = { 0 };

	if (type != hwmon_power && attr != hwmon_power_cap)
		return -EINVAL;
	/*
	 * hwmon power attributes are in microWatt
	 * mailbox read/write is in mWatt
	 */
	val /= 1000;

	val = clamp_val(val, SBRMI_PWR_MIN, data->pwr_limit_max);

	msg.cmd = SBRMI_WRITE_PKG_PWR_LIMIT;
	msg.data_in = val;
	msg.read = false;

	return rmi_mailbox_xfer(data, &msg);
}

static umode_t sbrmi_is_visible(const void *data,
				enum hwmon_sensor_types type,
				u32 attr, int channel)
{
	switch (type) {
	case hwmon_power:
		switch (attr) {
		case hwmon_power_input:
		case hwmon_power_cap_max:
			return 0444;
		case hwmon_power_cap:
			return 0644;
		}
		break;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_channel_info * const sbrmi_info[] = {
	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_CAP | HWMON_P_CAP_MAX),
	NULL
};

static const struct hwmon_ops sbrmi_hwmon_ops = {
	.is_visible = sbrmi_is_visible,
	.read = sbrmi_read,
	.write = sbrmi_write,
};

static const struct hwmon_chip_info sbrmi_chip_info = {
	.ops = &sbrmi_hwmon_ops,
	.info = sbrmi_info,
};

static int sbrmi_get_max_pwr_limit(struct sbrmi_data *data)
{
	struct sbrmi_mailbox_msg msg = { 0 };
	int ret;

	msg.cmd = SBRMI_READ_PKG_MAX_PWR_LIMIT;
	msg.read = true;
	ret = rmi_mailbox_xfer(data, &msg);
	if (ret < 0)
		return ret;
	data->pwr_limit_max = msg.data_out;

	return ret;
}

static int sbrmi_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct sbrmi_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(struct sbrmi_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->lock);

	/* Enable alert for SB-RMI sequence */
	ret = sbrmi_enable_alert(client);
	if (ret < 0)
		return ret;

	/* Cache maximum power limit */
	ret = sbrmi_get_max_pwr_limit(data);
	if (ret < 0)
		return ret;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, data,
							 &sbrmi_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id sbrmi_id[] = {
	{"sbrmi", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, sbrmi_id);

static const struct of_device_id __maybe_unused sbrmi_of_match[] = {
	{
		.compatible = "amd,sbrmi",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, sbrmi_of_match);

static struct i2c_driver sbrmi_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "sbrmi",
		.of_match_table = of_match_ptr(sbrmi_of_match),
	},
	.probe = sbrmi_probe,
	.id_table = sbrmi_id,
};

module_i2c_driver(sbrmi_driver);

MODULE_AUTHOR("Akshay Gupta <akshay.gupta@amd.com>");
MODULE_DESCRIPTION("Hwmon driver for AMD SB-RMI emulated sensor");
MODULE_LICENSE("GPL");

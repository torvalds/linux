// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MELFAS MIP4 Touchscreen
 *
 * Copyright (C) 2016 MELFAS Inc.
 *
 * Author : Sangwon Jee <jeesw@melfas.com>
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/unaligned.h>

#define MIP4_DEVICE_NAME	"mip4_ts"

/*****************************************************************
 * Protocol
 * Version : MIP 4.0 Rev 5.4
 *****************************************************************/

/* Address */
#define MIP4_R0_BOOT				0x00
#define MIP4_R1_BOOT_MODE			0x01
#define MIP4_R1_BOOT_BUF_ADDR			0x10
#define MIP4_R1_BOOT_STATUS			0x20
#define MIP4_R1_BOOT_CMD			0x30
#define MIP4_R1_BOOT_TARGET_ADDR		0x40
#define MIP4_R1_BOOT_SIZE			0x44

#define MIP4_R0_INFO				0x01
#define MIP4_R1_INFO_PRODUCT_NAME		0x00
#define MIP4_R1_INFO_RESOLUTION_X		0x10
#define MIP4_R1_INFO_RESOLUTION_Y		0x12
#define MIP4_R1_INFO_NODE_NUM_X			0x14
#define MIP4_R1_INFO_NODE_NUM_Y			0x15
#define MIP4_R1_INFO_KEY_NUM			0x16
#define MIP4_R1_INFO_PRESSURE_NUM		0x17
#define MIP4_R1_INFO_LENGTH_X			0x18
#define MIP4_R1_INFO_LENGTH_Y			0x1A
#define MIP4_R1_INFO_PPM_X			0x1C
#define MIP4_R1_INFO_PPM_Y			0x1D
#define MIP4_R1_INFO_VERSION_BOOT		0x20
#define MIP4_R1_INFO_VERSION_CORE		0x22
#define MIP4_R1_INFO_VERSION_APP		0x24
#define MIP4_R1_INFO_VERSION_PARAM		0x26
#define MIP4_R1_INFO_SECT_BOOT_START		0x30
#define MIP4_R1_INFO_SECT_BOOT_END		0x31
#define MIP4_R1_INFO_SECT_CORE_START		0x32
#define MIP4_R1_INFO_SECT_CORE_END		0x33
#define MIP4_R1_INFO_SECT_APP_START		0x34
#define MIP4_R1_INFO_SECT_APP_END		0x35
#define MIP4_R1_INFO_SECT_PARAM_START		0x36
#define MIP4_R1_INFO_SECT_PARAM_END		0x37
#define MIP4_R1_INFO_BUILD_DATE			0x40
#define MIP4_R1_INFO_BUILD_TIME			0x44
#define MIP4_R1_INFO_CHECKSUM_PRECALC		0x48
#define MIP4_R1_INFO_CHECKSUM_REALTIME		0x4A
#define MIP4_R1_INFO_PROTOCOL_NAME		0x50
#define MIP4_R1_INFO_PROTOCOL_VERSION		0x58
#define MIP4_R1_INFO_IC_ID			0x70
#define MIP4_R1_INFO_IC_NAME			0x71
#define MIP4_R1_INFO_IC_VENDOR_ID		0x75
#define MIP4_R1_INFO_IC_HW_CATEGORY		0x77
#define MIP4_R1_INFO_CONTACT_THD_SCR		0x78
#define MIP4_R1_INFO_CONTACT_THD_KEY		0x7A
#define MIP4_R1_INFO_PID				0x7C
#define MIP4_R1_INFO_VID				0x7E
#define MIP4_R1_INFO_SLAVE_ADDR			0x80

#define MIP4_R0_EVENT				0x02
#define MIP4_R1_EVENT_SUPPORTED_FUNC		0x00
#define MIP4_R1_EVENT_FORMAT			0x04
#define MIP4_R1_EVENT_SIZE			0x06
#define MIP4_R1_EVENT_PACKET_INFO		0x10
#define MIP4_R1_EVENT_PACKET_DATA		0x11

#define MIP4_R0_CTRL				0x06
#define MIP4_R1_CTRL_READY_STATUS		0x00
#define MIP4_R1_CTRL_EVENT_READY		0x01
#define MIP4_R1_CTRL_MODE			0x10
#define MIP4_R1_CTRL_EVENT_TRIGGER_TYPE		0x11
#define MIP4_R1_CTRL_RECALIBRATE		0x12
#define MIP4_R1_CTRL_POWER_STATE		0x13
#define MIP4_R1_CTRL_GESTURE_TYPE		0x14
#define MIP4_R1_CTRL_DISABLE_ESD_ALERT		0x18
#define MIP4_R1_CTRL_CHARGER_MODE		0x19
#define MIP4_R1_CTRL_HIGH_SENS_MODE		0x1A
#define MIP4_R1_CTRL_WINDOW_MODE		0x1B
#define MIP4_R1_CTRL_PALM_REJECTION		0x1C
#define MIP4_R1_CTRL_EDGE_CORRECTION		0x1D
#define MIP4_R1_CTRL_ENTER_GLOVE_MODE		0x1E
#define MIP4_R1_CTRL_I2C_ON_LPM			0x1F
#define MIP4_R1_CTRL_GESTURE_DEBUG		0x20
#define MIP4_R1_CTRL_PALM_EVENT			0x22
#define MIP4_R1_CTRL_PROXIMITY_SENSING		0x23

/* Value */
#define MIP4_BOOT_MODE_BOOT			0x01
#define MIP4_BOOT_MODE_APP			0x02

#define MIP4_BOOT_STATUS_BUSY			0x05
#define MIP4_BOOT_STATUS_ERROR			0x0E
#define MIP4_BOOT_STATUS_DONE			0xA0

#define MIP4_BOOT_CMD_MASS_ERASE		0x15
#define MIP4_BOOT_CMD_PROGRAM			0x54
#define MIP4_BOOT_CMD_ERASE			0x8F
#define MIP4_BOOT_CMD_WRITE			0xA5
#define MIP4_BOOT_CMD_READ			0xC2

#define MIP4_EVENT_INPUT_TYPE_KEY		0
#define MIP4_EVENT_INPUT_TYPE_SCREEN		1
#define MIP4_EVENT_INPUT_TYPE_PROXIMITY		2

#define I2C_RETRY_COUNT				3	/* 2~ */

#define MIP4_BUF_SIZE				128
#define MIP4_MAX_FINGERS			10
#define MIP4_MAX_KEYS				4

#define MIP4_TOUCH_MAJOR_MIN			0
#define MIP4_TOUCH_MAJOR_MAX			255
#define MIP4_TOUCH_MINOR_MIN			0
#define MIP4_TOUCH_MINOR_MAX			255
#define MIP4_PRESSURE_MIN			0
#define MIP4_PRESSURE_MAX			255

#define MIP4_FW_NAME			"melfas_mip4.fw"
#define MIP4_FW_UPDATE_DEBUG		0	/* 0 (default) or 1 */

struct mip4_fw_version {
	u16 boot;
	u16 core;
	u16 app;
	u16 param;
};

struct mip4_ts {
	struct i2c_client *client;
	struct input_dev *input;
	struct gpio_desc *gpio_ce;

	char phys[32];
	char product_name[16];
	u16 product_id;
	char ic_name[4];
	char fw_name[32];

	unsigned int max_x;
	unsigned int max_y;
	u8 node_x;
	u8 node_y;
	u8 node_key;
	unsigned int ppm_x;
	unsigned int ppm_y;

	struct mip4_fw_version fw_version;

	unsigned int event_size;
	unsigned int event_format;

	unsigned int key_num;
	unsigned short key_code[MIP4_MAX_KEYS];

	bool wake_irq_enabled;

	u8 buf[MIP4_BUF_SIZE];
};

static int mip4_i2c_xfer(struct mip4_ts *ts,
			 char *write_buf, unsigned int write_len,
			 char *read_buf, unsigned int read_len)
{
	struct i2c_msg msg[] = {
		{
			.addr = ts->client->addr,
			.flags = 0,
			.buf = write_buf,
			.len = write_len,
		}, {
			.addr = ts->client->addr,
			.flags = I2C_M_RD,
			.buf = read_buf,
			.len = read_len,
		},
	};
	int retry = I2C_RETRY_COUNT;
	int res;
	int error;

	do {
		res = i2c_transfer(ts->client->adapter, msg, ARRAY_SIZE(msg));
		if (res == ARRAY_SIZE(msg))
			return 0;

		error = res < 0 ? res : -EIO;
		dev_err(&ts->client->dev,
			"%s - i2c_transfer failed: %d (%d)\n",
			__func__, error, res);
	} while (--retry);

	return error;
}

static void mip4_parse_fw_version(const u8 *buf, struct mip4_fw_version *v)
{
	v->boot  = get_unaligned_le16(buf + 0);
	v->core  = get_unaligned_le16(buf + 2);
	v->app   = get_unaligned_le16(buf + 4);
	v->param = get_unaligned_le16(buf + 6);
}

/*
 * Read chip firmware version
 */
static int mip4_get_fw_version(struct mip4_ts *ts)
{
	u8 cmd[] = { MIP4_R0_INFO, MIP4_R1_INFO_VERSION_BOOT };
	u8 buf[sizeof(ts->fw_version)];
	int error;

	error = mip4_i2c_xfer(ts, cmd, sizeof(cmd), buf, sizeof(buf));
	if (error) {
		memset(&ts->fw_version, 0xff, sizeof(ts->fw_version));
		return error;
	}

	mip4_parse_fw_version(buf, &ts->fw_version);

	return 0;
}

/*
 * Fetch device characteristics
 */
static int mip4_query_device(struct mip4_ts *ts)
{
	union i2c_smbus_data dummy;
	int error;
	u8 cmd[2];
	u8 buf[14];

	/*
	 * Make sure there is something at this address as we do not
	 * consider subsequent failures as fatal.
	 */
	if (i2c_smbus_xfer(ts->client->adapter, ts->client->addr,
			   0, I2C_SMBUS_READ, 0, I2C_SMBUS_BYTE, &dummy) < 0) {
		dev_err(&ts->client->dev, "nothing at this address\n");
		return -ENXIO;
	}

	/* Product name */
	cmd[0] = MIP4_R0_INFO;
	cmd[1] = MIP4_R1_INFO_PRODUCT_NAME;
	error = mip4_i2c_xfer(ts, cmd, sizeof(cmd),
			      ts->product_name, sizeof(ts->product_name));
	if (error)
		dev_warn(&ts->client->dev,
			 "Failed to retrieve product name: %d\n", error);
	else
		dev_dbg(&ts->client->dev, "product name: %.*s\n",
			(int)sizeof(ts->product_name), ts->product_name);

	/* Product ID */
	cmd[0] = MIP4_R0_INFO;
	cmd[1] = MIP4_R1_INFO_PID;
	error = mip4_i2c_xfer(ts, cmd, sizeof(cmd), buf, 2);
	if (error) {
		dev_warn(&ts->client->dev,
			 "Failed to retrieve product id: %d\n", error);
	} else {
		ts->product_id = get_unaligned_le16(&buf[0]);
		dev_dbg(&ts->client->dev, "product id: %04X\n", ts->product_id);
	}

	/* Firmware name */
	snprintf(ts->fw_name, sizeof(ts->fw_name),
		"melfas_mip4_%04X.fw", ts->product_id);
	dev_dbg(&ts->client->dev, "firmware name: %s\n", ts->fw_name);

	/* IC name */
	cmd[0] = MIP4_R0_INFO;
	cmd[1] = MIP4_R1_INFO_IC_NAME;
	error = mip4_i2c_xfer(ts, cmd, sizeof(cmd),
			      ts->ic_name, sizeof(ts->ic_name));
	if (error)
		dev_warn(&ts->client->dev,
			 "Failed to retrieve IC name: %d\n", error);
	else
		dev_dbg(&ts->client->dev, "IC name: %.*s\n",
			(int)sizeof(ts->ic_name), ts->ic_name);

	/* Firmware version */
	error = mip4_get_fw_version(ts);
	if (error)
		dev_warn(&ts->client->dev,
			"Failed to retrieve FW version: %d\n", error);
	else
		dev_dbg(&ts->client->dev, "F/W Version: %04X %04X %04X %04X\n",
			 ts->fw_version.boot, ts->fw_version.core,
			 ts->fw_version.app, ts->fw_version.param);

	/* Resolution */
	cmd[0] = MIP4_R0_INFO;
	cmd[1] = MIP4_R1_INFO_RESOLUTION_X;
	error = mip4_i2c_xfer(ts, cmd, sizeof(cmd), buf, 14);
	if (error) {
		dev_warn(&ts->client->dev,
			 "Failed to retrieve touchscreen parameters: %d\n",
			 error);
	} else {
		ts->max_x = get_unaligned_le16(&buf[0]);
		ts->max_y = get_unaligned_le16(&buf[2]);
		dev_dbg(&ts->client->dev, "max_x: %d, max_y: %d\n",
			ts->max_x, ts->max_y);

		ts->node_x = buf[4];
		ts->node_y = buf[5];
		ts->node_key = buf[6];
		dev_dbg(&ts->client->dev,
			"node_x: %d, node_y: %d, node_key: %d\n",
			ts->node_x, ts->node_y, ts->node_key);

		ts->ppm_x = buf[12];
		ts->ppm_y = buf[13];
		dev_dbg(&ts->client->dev, "ppm_x: %d, ppm_y: %d\n",
			ts->ppm_x, ts->ppm_y);

		/* Key ts */
		if (ts->node_key > 0)
			ts->key_num = ts->node_key;
	}

	/* Protocol */
	cmd[0] = MIP4_R0_EVENT;
	cmd[1] = MIP4_R1_EVENT_SUPPORTED_FUNC;
	error = mip4_i2c_xfer(ts, cmd, sizeof(cmd), buf, 7);
	if (error) {
		dev_warn(&ts->client->dev,
			"Failed to retrieve device type: %d\n", error);
		ts->event_format = 0xff;
	} else {
		ts->event_format = get_unaligned_le16(&buf[4]);
		ts->event_size = buf[6];
		dev_dbg(&ts->client->dev, "event_format: %d, event_size: %d\n",
			ts->event_format, ts->event_size);

		if (ts->event_format == 2 || ts->event_format > 3)
			dev_warn(&ts->client->dev,
				 "Unknown event format %d\n", ts->event_format);
	}

	return 0;
}

static int mip4_power_on(struct mip4_ts *ts)
{
	if (ts->gpio_ce) {
		gpiod_set_value_cansleep(ts->gpio_ce, 1);

		/* Booting delay : 200~300ms */
		usleep_range(200 * 1000, 300 * 1000);
	}

	return 0;
}

static void mip4_power_off(struct mip4_ts *ts)
{
	if (ts->gpio_ce)
		gpiod_set_value_cansleep(ts->gpio_ce, 0);
}

/*
 * Clear touch input event status
 */
static void mip4_clear_input(struct mip4_ts *ts)
{
	int i;

	/* Screen */
	for (i = 0; i < MIP4_MAX_FINGERS; i++) {
		input_mt_slot(ts->input, i);
		input_mt_report_slot_inactive(ts->input);
	}

	/* Keys */
	for (i = 0; i < ts->key_num; i++)
		input_report_key(ts->input, ts->key_code[i], 0);

	input_sync(ts->input);
}

static int mip4_enable(struct mip4_ts *ts)
{
	int error;

	error = mip4_power_on(ts);
	if (error)
		return error;

	enable_irq(ts->client->irq);

	return 0;
}

static void mip4_disable(struct mip4_ts *ts)
{
	disable_irq(ts->client->irq);

	mip4_power_off(ts);

	mip4_clear_input(ts);
}

/*****************************************************************
 * Input handling
 *****************************************************************/

static void mip4_report_keys(struct mip4_ts *ts, u8 *packet)
{
	u8 key;
	bool down;

	switch (ts->event_format) {
	case 0:
	case 1:
		key = packet[0] & 0x0F;
		down = packet[0] & 0x80;
		break;

	case 3:
	default:
		key = packet[0] & 0x0F;
		down = packet[1] & 0x01;
		break;
	}

	/* Report key event */
	if (key >= 1 && key <= ts->key_num) {
		unsigned short keycode = ts->key_code[key - 1];

		dev_dbg(&ts->client->dev,
			"Key - ID: %d, keycode: %d, state: %d\n",
			key, keycode, down);

		input_event(ts->input, EV_MSC, MSC_SCAN, keycode);
		input_report_key(ts->input, keycode, down);

	} else {
		dev_err(&ts->client->dev, "Unknown key: %d\n", key);
	}
}

static void mip4_report_touch(struct mip4_ts *ts, u8 *packet)
{
	int id;
	bool __always_unused hover;
	bool palm;
	bool state;
	u16 x, y;
	u8 __always_unused pressure_stage = 0;
	u8 pressure;
	u8 __always_unused size;
	u8 touch_major;
	u8 touch_minor;

	switch (ts->event_format) {
	case 0:
	case 1:
		/* Touch only */
		state = packet[0] & BIT(7);
		hover = packet[0] & BIT(5);
		palm = packet[0] & BIT(4);
		id = (packet[0] & 0x0F) - 1;
		x = ((packet[1] & 0x0F) << 8) | packet[2];
		y = (((packet[1] >> 4) & 0x0F) << 8) |
			packet[3];
		pressure = packet[4];
		size = packet[5];
		if (ts->event_format == 0) {
			touch_major = packet[5];
			touch_minor = packet[5];
		} else {
			touch_major = packet[6];
			touch_minor = packet[7];
		}
		break;

	case 3:
	default:
		/* Touch + Force(Pressure) */
		id = (packet[0] & 0x0F) - 1;
		hover = packet[1] & BIT(2);
		palm = packet[1] & BIT(1);
		state = packet[1] & BIT(0);
		x = ((packet[2] & 0x0F) << 8) | packet[3];
		y = (((packet[2] >> 4) & 0x0F) << 8) |
			packet[4];
		size = packet[6];
		pressure_stage = (packet[7] & 0xF0) >> 4;
		pressure = ((packet[7] & 0x0F) << 8) |
			packet[8];
		touch_major = packet[9];
		touch_minor = packet[10];
		break;
	}

	dev_dbg(&ts->client->dev,
		"Screen - Slot: %d State: %d X: %04d Y: %04d Z: %d\n",
		id, state, x, y, pressure);

	if (unlikely(id < 0 || id >= MIP4_MAX_FINGERS)) {
		dev_err(&ts->client->dev, "Screen - invalid slot ID: %d\n", id);
		goto out;
	}

	input_mt_slot(ts->input, id);
	if (input_mt_report_slot_state(ts->input,
				       palm ? MT_TOOL_PALM : MT_TOOL_FINGER,
				       state)) {
		input_report_abs(ts->input, ABS_MT_POSITION_X, x);
		input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
		input_report_abs(ts->input, ABS_MT_PRESSURE, pressure);
		input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, touch_major);
		input_report_abs(ts->input, ABS_MT_TOUCH_MINOR, touch_minor);
	}

out:
	input_mt_sync_frame(ts->input);
}

static int mip4_handle_packet(struct mip4_ts *ts, u8 *packet)
{
	u8 type;

	switch (ts->event_format) {
	case 0:
	case 1:
		type = (packet[0] & 0x40) >> 6;
		break;

	case 3:
		type = (packet[0] & 0xF0) >> 4;
		break;

	default:
		/* Should not happen unless we have corrupted firmware */
		return -EINVAL;
	}

	dev_dbg(&ts->client->dev, "Type: %d\n", type);

	/* Report input event */
	switch (type) {
	case MIP4_EVENT_INPUT_TYPE_KEY:
		mip4_report_keys(ts, packet);
		break;

	case MIP4_EVENT_INPUT_TYPE_SCREEN:
		mip4_report_touch(ts, packet);
		break;

	default:
		dev_err(&ts->client->dev, "Unknown event type: %d\n", type);
		break;
	}

	return 0;
}

static irqreturn_t mip4_interrupt(int irq, void *dev_id)
{
	struct mip4_ts *ts = dev_id;
	struct i2c_client *client = ts->client;
	unsigned int i;
	int error;
	u8 cmd[2];
	u8 size;
	bool alert;

	/* Read packet info */
	cmd[0] = MIP4_R0_EVENT;
	cmd[1] = MIP4_R1_EVENT_PACKET_INFO;
	error = mip4_i2c_xfer(ts, cmd, sizeof(cmd), ts->buf, 1);
	if (error) {
		dev_err(&client->dev,
			"Failed to read packet info: %d\n", error);
		goto out;
	}

	size = ts->buf[0] & 0x7F;
	alert = ts->buf[0] & BIT(7);
	dev_dbg(&client->dev, "packet size: %d, alert: %d\n", size, alert);

	/* Check size */
	if (!size) {
		dev_err(&client->dev, "Empty packet\n");
		goto out;
	}

	/* Read packet data */
	cmd[0] = MIP4_R0_EVENT;
	cmd[1] = MIP4_R1_EVENT_PACKET_DATA;
	error = mip4_i2c_xfer(ts, cmd, sizeof(cmd), ts->buf, size);
	if (error) {
		dev_err(&client->dev,
			"Failed to read packet data: %d\n", error);
		goto out;
	}

	if (alert) {
		dev_dbg(&client->dev, "Alert: %d\n", ts->buf[0]);
	} else {
		for (i = 0; i < size; i += ts->event_size) {
			error = mip4_handle_packet(ts, &ts->buf[i]);
			if (error)
				break;
		}

		input_sync(ts->input);
	}

out:
	return IRQ_HANDLED;
}

static int mip4_input_open(struct input_dev *dev)
{
	struct mip4_ts *ts = input_get_drvdata(dev);

	return mip4_enable(ts);
}

static void mip4_input_close(struct input_dev *dev)
{
	struct mip4_ts *ts = input_get_drvdata(dev);

	mip4_disable(ts);
}

/*****************************************************************
 * Firmware update
 *****************************************************************/

/* Firmware Info */
#define MIP4_BL_PAGE_SIZE		512	/* 512 */
#define MIP4_BL_PACKET_SIZE		512	/* 512, 256, 128, 64, ... */

/*
 * Firmware binary tail info
 */

struct mip4_bin_tail {
	u8 tail_mark[4];
	u8 chip_name[4];

	__le32 bin_start_addr;
	__le32 bin_length;

	__le16 ver_boot;
	__le16 ver_core;
	__le16 ver_app;
	__le16 ver_param;

	u8 boot_start;
	u8 boot_end;
	u8 core_start;
	u8 core_end;
	u8 app_start;
	u8 app_end;
	u8 param_start;
	u8 param_end;

	u8 checksum_type;
	u8 hw_category;

	__le16 param_id;
	__le32 param_length;
	__le32 build_date;
	__le32 build_time;

	__le32 reserved1;
	__le32 reserved2;
	__le16 reserved3;
	__le16 tail_size;
	__le32 crc;
} __packed;

#define MIP4_BIN_TAIL_MARK	"MBT\001"
#define MIP4_BIN_TAIL_SIZE	(sizeof(struct mip4_bin_tail))

/*
* Bootloader - Read status
*/
static int mip4_bl_read_status(struct mip4_ts *ts)
{
	u8 cmd[] = { MIP4_R0_BOOT, MIP4_R1_BOOT_STATUS };
	u8 result;
	struct i2c_msg msg[] = {
		{
			.addr = ts->client->addr,
			.flags = 0,
			.buf = cmd,
			.len = sizeof(cmd),
		}, {
			.addr = ts->client->addr,
			.flags = I2C_M_RD,
			.buf = &result,
			.len = sizeof(result),
		},
	};
	int ret;
	int error;
	int retry = 1000;

	do {
		ret = i2c_transfer(ts->client->adapter, msg, ARRAY_SIZE(msg));
		if (ret != ARRAY_SIZE(msg)) {
			error = ret < 0 ? ret : -EIO;
			dev_err(&ts->client->dev,
				"Failed to read bootloader status: %d\n",
				error);
			return error;
		}

		switch (result) {
		case MIP4_BOOT_STATUS_DONE:
			dev_dbg(&ts->client->dev, "%s - done\n", __func__);
			return 0;

		case MIP4_BOOT_STATUS_ERROR:
			dev_err(&ts->client->dev, "Bootloader failure\n");
			return -EIO;

		case MIP4_BOOT_STATUS_BUSY:
			dev_dbg(&ts->client->dev, "%s - Busy\n", __func__);
			error = -EBUSY;
			break;

		default:
			dev_err(&ts->client->dev,
				"Unexpected bootloader status: %#02x\n",
				result);
			error = -EINVAL;
			break;
		}

		usleep_range(1000, 2000);
	} while (--retry);

	return error;
}

/*
* Bootloader - Change mode
*/
static int mip4_bl_change_mode(struct mip4_ts *ts, u8 mode)
{
	u8 mode_chg_cmd[] = { MIP4_R0_BOOT, MIP4_R1_BOOT_MODE, mode };
	u8 mode_read_cmd[] = { MIP4_R0_BOOT, MIP4_R1_BOOT_MODE };
	u8 result;
	struct i2c_msg msg[] = {
		{
			.addr = ts->client->addr,
			.flags = 0,
			.buf = mode_read_cmd,
			.len = sizeof(mode_read_cmd),
		}, {
			.addr = ts->client->addr,
			.flags = I2C_M_RD,
			.buf = &result,
			.len = sizeof(result),
		},
	};
	int retry = 10;
	int ret;
	int error;

	do {
		/* Send mode change command */
		ret = i2c_master_send(ts->client,
				      mode_chg_cmd, sizeof(mode_chg_cmd));
		if (ret != sizeof(mode_chg_cmd)) {
			error = ret < 0 ? ret : -EIO;
			dev_err(&ts->client->dev,
				"Failed to send %d mode change: %d (%d)\n",
				mode, error, ret);
			return error;
		}

		dev_dbg(&ts->client->dev,
			"Sent mode change request (mode: %d)\n", mode);

		/* Wait */
		msleep(1000);

		/* Verify target mode */
		ret = i2c_transfer(ts->client->adapter, msg, ARRAY_SIZE(msg));
		if (ret != ARRAY_SIZE(msg)) {
			error = ret < 0 ? ret : -EIO;
			dev_err(&ts->client->dev,
				"Failed to read device mode: %d\n", error);
			return error;
		}

		dev_dbg(&ts->client->dev,
			"Current device mode: %d, want: %d\n", result, mode);

		if (result == mode)
			return 0;

	} while (--retry);

	return -EIO;
}

/*
 * Bootloader - Start bootloader mode
 */
static int mip4_bl_enter(struct mip4_ts *ts)
{
	return mip4_bl_change_mode(ts, MIP4_BOOT_MODE_BOOT);
}

/*
 * Bootloader - Exit bootloader mode
 */
static int mip4_bl_exit(struct mip4_ts *ts)
{
	return mip4_bl_change_mode(ts, MIP4_BOOT_MODE_APP);
}

static int mip4_bl_get_address(struct mip4_ts *ts, u16 *buf_addr)
{
	u8 cmd[] = { MIP4_R0_BOOT, MIP4_R1_BOOT_BUF_ADDR };
	u8 result[sizeof(u16)];
	struct i2c_msg msg[] = {
		{
			.addr = ts->client->addr,
			.flags = 0,
			.buf = cmd,
			.len = sizeof(cmd),
		}, {
			.addr = ts->client->addr,
			.flags = I2C_M_RD,
			.buf = result,
			.len = sizeof(result),
		},
	};
	int ret;
	int error;

	ret = i2c_transfer(ts->client->adapter, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&ts->client->dev,
			"Failed to retrieve bootloader buffer address: %d\n",
			error);
		return error;
	}

	*buf_addr = get_unaligned_le16(result);
	dev_dbg(&ts->client->dev,
		"Bootloader buffer address %#04x\n", *buf_addr);

	return 0;
}

static int mip4_bl_program_page(struct mip4_ts *ts, int offset,
				const u8 *data, int length, u16 buf_addr)
{
	u8 cmd[6];
	u8 *data_buf;
	u16 buf_offset;
	int ret;
	int error;

	dev_dbg(&ts->client->dev, "Writing page @%#06x (%d)\n",
		offset, length);

	if (length > MIP4_BL_PAGE_SIZE || length % MIP4_BL_PACKET_SIZE) {
		dev_err(&ts->client->dev,
			"Invalid page length: %d\n", length);
		return -EINVAL;
	}

	data_buf = kmalloc(2 + MIP4_BL_PACKET_SIZE, GFP_KERNEL);
	if (!data_buf)
		return -ENOMEM;

	/* Addr */
	cmd[0] = MIP4_R0_BOOT;
	cmd[1] = MIP4_R1_BOOT_TARGET_ADDR;
	put_unaligned_le32(offset, &cmd[2]);
	ret = i2c_master_send(ts->client, cmd, 6);
	if (ret != 6) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&ts->client->dev,
			"Failed to send write page address: %d\n", error);
		goto out;
	}

	/* Size */
	cmd[0] = MIP4_R0_BOOT;
	cmd[1] = MIP4_R1_BOOT_SIZE;
	put_unaligned_le32(length, &cmd[2]);
	ret = i2c_master_send(ts->client, cmd, 6);
	if (ret != 6) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&ts->client->dev,
			"Failed to send write page size: %d\n", error);
		goto out;
	}

	/* Data */
	for (buf_offset = 0;
	     buf_offset < length;
	     buf_offset += MIP4_BL_PACKET_SIZE) {
		dev_dbg(&ts->client->dev,
			"writing chunk at %#04x (size %d)\n",
			buf_offset, MIP4_BL_PACKET_SIZE);
		put_unaligned_be16(buf_addr + buf_offset, data_buf);
		memcpy(&data_buf[2], &data[buf_offset], MIP4_BL_PACKET_SIZE);
		ret = i2c_master_send(ts->client,
				      data_buf, 2 + MIP4_BL_PACKET_SIZE);
		if (ret != 2 + MIP4_BL_PACKET_SIZE) {
			error = ret < 0 ? ret : -EIO;
			dev_err(&ts->client->dev,
				"Failed to read chunk at %#04x (size %d): %d\n",
				buf_offset, MIP4_BL_PACKET_SIZE, error);
			goto out;
		}
	}

	/* Command */
	cmd[0] = MIP4_R0_BOOT;
	cmd[1] = MIP4_R1_BOOT_CMD;
	cmd[2] = MIP4_BOOT_CMD_PROGRAM;
	ret = i2c_master_send(ts->client, cmd, 3);
	if (ret != 3) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&ts->client->dev,
			"Failed to send 'write' command: %d\n", error);
		goto out;
	}

	/* Status */
	error = mip4_bl_read_status(ts);

out:
	kfree(data_buf);
	return error ? error : 0;
}

static int mip4_bl_verify_page(struct mip4_ts *ts, int offset,
			       const u8 *data, int length, int buf_addr)
{
	u8 cmd[8];
	u8 *read_buf;
	int buf_offset;
	struct i2c_msg msg[] = {
		{
			.addr = ts->client->addr,
			.flags = 0,
			.buf = cmd,
			.len = 2,
		}, {
			.addr = ts->client->addr,
			.flags = I2C_M_RD,
			.len = MIP4_BL_PACKET_SIZE,
		},
	};
	int ret;
	int error;

	dev_dbg(&ts->client->dev, "Validating page @%#06x (%d)\n",
		offset, length);

	/* Addr */
	cmd[0] = MIP4_R0_BOOT;
	cmd[1] = MIP4_R1_BOOT_TARGET_ADDR;
	put_unaligned_le32(offset, &cmd[2]);
	ret = i2c_master_send(ts->client, cmd, 6);
	if (ret != 6) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&ts->client->dev,
			"Failed to send read page address: %d\n", error);
		return error;
	}

	/* Size */
	cmd[0] = MIP4_R0_BOOT;
	cmd[1] = MIP4_R1_BOOT_SIZE;
	put_unaligned_le32(length, &cmd[2]);
	ret = i2c_master_send(ts->client, cmd, 6);
	if (ret != 6) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&ts->client->dev,
			"Failed to send read page size: %d\n", error);
		return error;
	}

	/* Command */
	cmd[0] = MIP4_R0_BOOT;
	cmd[1] = MIP4_R1_BOOT_CMD;
	cmd[2] = MIP4_BOOT_CMD_READ;
	ret = i2c_master_send(ts->client, cmd, 3);
	if (ret != 3) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&ts->client->dev,
			"Failed to send 'read' command: %d\n", error);
		return error;
	}

	/* Status */
	error = mip4_bl_read_status(ts);
	if (error)
		return error;

	/* Read */
	msg[1].buf = read_buf = kmalloc(MIP4_BL_PACKET_SIZE, GFP_KERNEL);
	if (!read_buf)
		return -ENOMEM;

	for (buf_offset = 0;
	     buf_offset < length;
	     buf_offset += MIP4_BL_PACKET_SIZE) {
		dev_dbg(&ts->client->dev,
			"reading chunk at %#04x (size %d)\n",
			buf_offset, MIP4_BL_PACKET_SIZE);
		put_unaligned_be16(buf_addr + buf_offset, cmd);
		ret = i2c_transfer(ts->client->adapter, msg, ARRAY_SIZE(msg));
		if (ret != ARRAY_SIZE(msg)) {
			error = ret < 0 ? ret : -EIO;
			dev_err(&ts->client->dev,
				"Failed to read chunk at %#04x (size %d): %d\n",
				buf_offset, MIP4_BL_PACKET_SIZE, error);
			break;
		}

		if (memcmp(&data[buf_offset], read_buf, MIP4_BL_PACKET_SIZE)) {
			dev_err(&ts->client->dev,
				"Failed to validate chunk at %#04x (size %d)\n",
				buf_offset, MIP4_BL_PACKET_SIZE);
#if MIP4_FW_UPDATE_DEBUG
			print_hex_dump(KERN_DEBUG,
				       MIP4_DEVICE_NAME " F/W File: ",
				       DUMP_PREFIX_OFFSET, 16, 1,
				       data + offset, MIP4_BL_PACKET_SIZE,
				       false);
			print_hex_dump(KERN_DEBUG,
				       MIP4_DEVICE_NAME " F/W Chip: ",
				       DUMP_PREFIX_OFFSET, 16, 1,
				       read_buf, MIP4_BL_PAGE_SIZE, false);
#endif
			error = -EINVAL;
			break;
		}
	}

	kfree(read_buf);
	return error ? error : 0;
}

/*
 * Flash chip firmware
 */
static int mip4_flash_fw(struct mip4_ts *ts,
			 const u8 *fw_data, u32 fw_size, u32 fw_offset)
{
	struct i2c_client *client = ts->client;
	int offset;
	u16 buf_addr;
	int error, error2;

	/* Enter bootloader mode */
	dev_dbg(&client->dev, "Entering bootloader mode\n");

	error = mip4_bl_enter(ts);
	if (error) {
		dev_err(&client->dev,
			"Failed to enter bootloader mode: %d\n",
			error);
		return error;
	}

	/* Read info */
	error = mip4_bl_get_address(ts, &buf_addr);
	if (error)
		goto exit_bl;

	/* Program & Verify */
	dev_dbg(&client->dev,
		"Program & Verify, page size: %d, packet size: %d\n",
		MIP4_BL_PAGE_SIZE, MIP4_BL_PACKET_SIZE);

	for (offset = fw_offset;
	     offset < fw_offset + fw_size;
	     offset += MIP4_BL_PAGE_SIZE) {
		/* Program */
		error = mip4_bl_program_page(ts, offset, fw_data + offset,
					     MIP4_BL_PAGE_SIZE, buf_addr);
		if (error)
			break;

		/* Verify */
		error = mip4_bl_verify_page(ts, offset, fw_data + offset,
					    MIP4_BL_PAGE_SIZE, buf_addr);
		if (error)
			break;
	}

exit_bl:
	/* Exit bootloader mode */
	dev_dbg(&client->dev, "Exiting bootloader mode\n");

	error2 = mip4_bl_exit(ts);
	if (error2) {
		dev_err(&client->dev,
			"Failed to exit bootloader mode: %d\n", error2);
		if (!error)
			error = error2;
	}

	/* Reset chip */
	mip4_power_off(ts);
	mip4_power_on(ts);

	mip4_query_device(ts);

	/* Refresh device parameters */
	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 0, ts->max_x, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 0, ts->max_y, 0, 0);
	input_set_abs_params(ts->input, ABS_X, 0, ts->max_x, 0, 0);
	input_set_abs_params(ts->input, ABS_Y, 0, ts->max_y, 0, 0);
	input_abs_set_res(ts->input, ABS_MT_POSITION_X, ts->ppm_x);
	input_abs_set_res(ts->input, ABS_MT_POSITION_Y, ts->ppm_y);
	input_abs_set_res(ts->input, ABS_X, ts->ppm_x);
	input_abs_set_res(ts->input, ABS_Y, ts->ppm_y);

	return error ? error : 0;
}

static int mip4_parse_firmware(struct mip4_ts *ts, const struct firmware *fw,
			       u32 *fw_offset_start, u32 *fw_size,
			       const struct mip4_bin_tail **pfw_info)
{
	const struct mip4_bin_tail *fw_info;
	struct mip4_fw_version fw_version;
	u16 tail_size;

	if (fw->size < MIP4_BIN_TAIL_SIZE) {
		dev_err(&ts->client->dev,
			"Invalid firmware, size mismatch (tail %zd vs %zd)\n",
			MIP4_BIN_TAIL_SIZE, fw->size);
		return -EINVAL;
	}

	fw_info = (const void *)&fw->data[fw->size - MIP4_BIN_TAIL_SIZE];

#if MIP4_FW_UPDATE_DEBUG
	print_hex_dump(KERN_ERR, MIP4_DEVICE_NAME " Bin Info: ",
		       DUMP_PREFIX_OFFSET, 16, 1, *fw_info, tail_size, false);
#endif

	tail_size = get_unaligned_le16(&fw_info->tail_size);
	if (tail_size != MIP4_BIN_TAIL_SIZE) {
		dev_err(&ts->client->dev,
			"wrong tail size: %d (expected %zd)\n",
			tail_size, MIP4_BIN_TAIL_SIZE);
		return -EINVAL;
	}

	/* Check bin format */
	if (memcmp(fw_info->tail_mark, MIP4_BIN_TAIL_MARK,
		   sizeof(fw_info->tail_mark))) {
		dev_err(&ts->client->dev,
			"unable to locate tail marker (%*ph vs %*ph)\n",
			(int)sizeof(fw_info->tail_mark), fw_info->tail_mark,
			(int)sizeof(fw_info->tail_mark), MIP4_BIN_TAIL_MARK);
		return -EINVAL;
	}

	*fw_offset_start = get_unaligned_le32(&fw_info->bin_start_addr);
	*fw_size = get_unaligned_le32(&fw_info->bin_length);

	dev_dbg(&ts->client->dev,
		"F/W Data offset: %#08x, size: %d\n",
		*fw_offset_start, *fw_size);

	if (*fw_size % MIP4_BL_PAGE_SIZE) {
		dev_err(&ts->client->dev,
			"encoded fw length %d is not multiple of pages (%d)\n",
			*fw_size, MIP4_BL_PAGE_SIZE);
		return -EINVAL;
	}

	if (fw->size != *fw_offset_start + *fw_size) {
		dev_err(&ts->client->dev,
			"Wrong firmware size, expected %d bytes, got %zd\n",
			*fw_offset_start + *fw_size, fw->size);
		return -EINVAL;
	}

	mip4_parse_fw_version((const u8 *)&fw_info->ver_boot, &fw_version);

	dev_dbg(&ts->client->dev,
		"F/W file version %04X %04X %04X %04X\n",
		fw_version.boot, fw_version.core,
		fw_version.app, fw_version.param);

	dev_dbg(&ts->client->dev, "F/W chip version: %04X %04X %04X %04X\n",
		 ts->fw_version.boot, ts->fw_version.core,
		 ts->fw_version.app, ts->fw_version.param);

	/* Check F/W type */
	if (fw_version.boot != 0xEEEE && fw_version.boot != 0xFFFF &&
	    fw_version.core == 0xEEEE &&
	    fw_version.app == 0xEEEE &&
	    fw_version.param == 0xEEEE) {
		dev_dbg(&ts->client->dev, "F/W type: Bootloader\n");
	} else if (fw_version.boot == 0xEEEE &&
		   fw_version.core != 0xEEEE && fw_version.core != 0xFFFF &&
		   fw_version.app != 0xEEEE && fw_version.app != 0xFFFF &&
		   fw_version.param != 0xEEEE && fw_version.param != 0xFFFF) {
		dev_dbg(&ts->client->dev, "F/W type: Main\n");
	} else {
		dev_err(&ts->client->dev, "Wrong firmware type\n");
		return -EINVAL;
	}

	return 0;
}

static int mip4_execute_fw_update(struct mip4_ts *ts, const struct firmware *fw)
{
	const struct mip4_bin_tail *fw_info;
	u32 fw_start_offset;
	u32 fw_size;
	int retires = 3;
	int error;

	error = mip4_parse_firmware(ts, fw,
				    &fw_start_offset, &fw_size, &fw_info);
	if (error)
		return error;

	if (input_device_enabled(ts->input)) {
		disable_irq(ts->client->irq);
	} else {
		error = mip4_power_on(ts);
		if (error)
			return error;
	}

	/* Update firmware */
	do {
		error = mip4_flash_fw(ts, fw->data, fw_size, fw_start_offset);
		if (!error)
			break;
	} while (--retires);

	if (error)
		dev_err(&ts->client->dev,
			"Failed to flash firmware: %d\n", error);

	/* Enable IRQ */
	if (input_device_enabled(ts->input))
		enable_irq(ts->client->irq);
	else
		mip4_power_off(ts);

	return error ? error : 0;
}

static ssize_t mip4_sysfs_fw_update(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mip4_ts *ts = i2c_get_clientdata(client);
	const struct firmware *fw;
	int error;

	error = request_firmware(&fw, ts->fw_name, dev);
	if (error) {
		dev_err(&ts->client->dev,
			"Failed to retrieve firmware %s: %d\n",
			ts->fw_name, error);
		return error;
	}

	/*
	 * Take input mutex to prevent racing with itself and also with
	 * userspace opening and closing the device and also suspend/resume
	 * transitions.
	 */
	mutex_lock(&ts->input->mutex);

	error = mip4_execute_fw_update(ts, fw);

	mutex_unlock(&ts->input->mutex);

	release_firmware(fw);

	if (error) {
		dev_err(&ts->client->dev,
			"Firmware update failed: %d\n", error);
		return error;
	}

	return count;
}

static DEVICE_ATTR(update_fw, S_IWUSR, NULL, mip4_sysfs_fw_update);

static ssize_t mip4_sysfs_read_fw_version(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mip4_ts *ts = i2c_get_clientdata(client);
	size_t count;

	/* Take lock to prevent racing with firmware update */
	mutex_lock(&ts->input->mutex);

	count = sysfs_emit(buf, "%04X %04X %04X %04X\n",
			   ts->fw_version.boot, ts->fw_version.core,
			   ts->fw_version.app, ts->fw_version.param);

	mutex_unlock(&ts->input->mutex);

	return count;
}

static DEVICE_ATTR(fw_version, S_IRUGO, mip4_sysfs_read_fw_version, NULL);

static ssize_t mip4_sysfs_read_hw_version(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mip4_ts *ts = i2c_get_clientdata(client);
	size_t count;

	/* Take lock to prevent racing with firmware update */
	mutex_lock(&ts->input->mutex);

	/*
	 * product_name shows the name or version of the hardware
	 * paired with current firmware in the chip.
	 */
	count = sysfs_emit(buf, "%.*s\n",
			   (int)sizeof(ts->product_name), ts->product_name);

	mutex_unlock(&ts->input->mutex);

	return count;
}

static DEVICE_ATTR(hw_version, S_IRUGO, mip4_sysfs_read_hw_version, NULL);

static ssize_t mip4_sysfs_read_product_id(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mip4_ts *ts = i2c_get_clientdata(client);
	size_t count;

	mutex_lock(&ts->input->mutex);

	count = sysfs_emit(buf, "%04X\n", ts->product_id);

	mutex_unlock(&ts->input->mutex);

	return count;
}

static DEVICE_ATTR(product_id, S_IRUGO, mip4_sysfs_read_product_id, NULL);

static ssize_t mip4_sysfs_read_ic_name(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mip4_ts *ts = i2c_get_clientdata(client);
	size_t count;

	mutex_lock(&ts->input->mutex);

	count = sysfs_emit(buf, "%.*s\n",
			   (int)sizeof(ts->ic_name), ts->ic_name);

	mutex_unlock(&ts->input->mutex);

	return count;
}

static DEVICE_ATTR(ic_name, S_IRUGO, mip4_sysfs_read_ic_name, NULL);

static struct attribute *mip4_attrs[] = {
	&dev_attr_fw_version.attr,
	&dev_attr_hw_version.attr,
	&dev_attr_product_id.attr,
	&dev_attr_ic_name.attr,
	&dev_attr_update_fw.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mip4);

static int mip4_probe(struct i2c_client *client)
{
	struct mip4_ts *ts;
	struct input_dev *input;
	int error;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "Not supported I2C adapter\n");
		return -ENXIO;
	}

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;

	ts->client = client;
	ts->input = input;

	snprintf(ts->phys, sizeof(ts->phys),
		 "%s/input0", dev_name(&client->dev));

	ts->gpio_ce = devm_gpiod_get_optional(&client->dev,
					      "ce", GPIOD_OUT_LOW);
	if (IS_ERR(ts->gpio_ce))
		return dev_err_probe(&client->dev, PTR_ERR(ts->gpio_ce), "Failed to get gpio\n");

	error = mip4_power_on(ts);
	if (error)
		return error;
	error = mip4_query_device(ts);
	mip4_power_off(ts);
	if (error)
		return error;

	input->name = "MELFAS MIP4 Touchscreen";
	input->phys = ts->phys;

	input->id.bustype = BUS_I2C;
	input->id.vendor = 0x13c5;
	input->id.product = ts->product_id;

	input->open = mip4_input_open;
	input->close = mip4_input_close;

	input_set_drvdata(input, ts);

	input->keycode = ts->key_code;
	input->keycodesize = sizeof(*ts->key_code);
	input->keycodemax = ts->key_num;

	input_set_abs_params(input, ABS_MT_TOOL_TYPE, 0, MT_TOOL_PALM, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, ts->max_x, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, ts->max_y, 0, 0);
	input_set_abs_params(input, ABS_MT_PRESSURE,
			     MIP4_PRESSURE_MIN, MIP4_PRESSURE_MAX, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR,
			     MIP4_TOUCH_MAJOR_MIN, MIP4_TOUCH_MAJOR_MAX, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MINOR,
			     MIP4_TOUCH_MINOR_MIN, MIP4_TOUCH_MINOR_MAX, 0, 0);
	input_abs_set_res(ts->input, ABS_MT_POSITION_X, ts->ppm_x);
	input_abs_set_res(ts->input, ABS_MT_POSITION_Y, ts->ppm_y);

	error = input_mt_init_slots(input, MIP4_MAX_FINGERS, INPUT_MT_DIRECT);
	if (error)
		return error;

	i2c_set_clientdata(client, ts);

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, mip4_interrupt,
					  IRQF_ONESHOT | IRQF_NO_AUTOEN,
					  MIP4_DEVICE_NAME, ts);
	if (error) {
		dev_err(&client->dev,
			"Failed to request interrupt %d: %d\n",
			client->irq, error);
		return error;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(&client->dev,
			"Failed to register input device: %d\n", error);
		return error;
	}

	return 0;
}

static int mip4_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mip4_ts *ts = i2c_get_clientdata(client);
	struct input_dev *input = ts->input;

	mutex_lock(&input->mutex);

	if (device_may_wakeup(dev))
		ts->wake_irq_enabled = enable_irq_wake(client->irq) == 0;
	else if (input_device_enabled(input))
		mip4_disable(ts);

	mutex_unlock(&input->mutex);

	return 0;
}

static int mip4_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mip4_ts *ts = i2c_get_clientdata(client);
	struct input_dev *input = ts->input;

	mutex_lock(&input->mutex);

	if (ts->wake_irq_enabled)
		disable_irq_wake(client->irq);
	else if (input_device_enabled(input))
		mip4_enable(ts);

	mutex_unlock(&input->mutex);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(mip4_pm_ops, mip4_suspend, mip4_resume);

#ifdef CONFIG_OF
static const struct of_device_id mip4_of_match[] = {
	{ .compatible = "melfas,mip4_ts", },
	{ },
};
MODULE_DEVICE_TABLE(of, mip4_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id mip4_acpi_match[] = {
	{ "MLFS0000", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, mip4_acpi_match);
#endif

static const struct i2c_device_id mip4_i2c_ids[] = {
	{ MIP4_DEVICE_NAME },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mip4_i2c_ids);

static struct i2c_driver mip4_driver = {
	.id_table = mip4_i2c_ids,
	.probe = mip4_probe,
	.driver = {
		.name = MIP4_DEVICE_NAME,
		.dev_groups = mip4_groups,
		.of_match_table = of_match_ptr(mip4_of_match),
		.acpi_match_table = ACPI_PTR(mip4_acpi_match),
		.pm = pm_sleep_ptr(&mip4_pm_ops),
	},
};
module_i2c_driver(mip4_driver);

MODULE_DESCRIPTION("MELFAS MIP4 Touchscreen");
MODULE_AUTHOR("Sangwon Jee <jeesw@melfas.com>");
MODULE_LICENSE("GPL");

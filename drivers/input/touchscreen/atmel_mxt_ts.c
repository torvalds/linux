/*
 * Atmel maXTouch Touchscreen driver
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Copyright (C) 2012 Google, Inc.
 *
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/i2c/atmel_mxt_ts.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

/* Version */
#define MXT_VER_20		20
#define MXT_VER_21		21
#define MXT_VER_22		22

/* Firmware files */
#define MXT_FW_NAME		"maxtouch.fw"
#define MXT_CFG_NAME		"maxtouch.cfg"
#define MXT_CFG_MAGIC		"OBP_RAW V1"

/* Registers */
#define MXT_INFO		0x00
#define MXT_FAMILY_ID		0x00
#define MXT_VARIANT_ID		0x01
#define MXT_VERSION		0x02
#define MXT_BUILD		0x03
#define MXT_MATRIX_X_SIZE	0x04
#define MXT_MATRIX_Y_SIZE	0x05
#define MXT_OBJECT_NUM		0x06
#define MXT_OBJECT_START	0x07

#define MXT_OBJECT_SIZE		6
#define MXT_INFO_CHECKSUM_SIZE	3
#define MXT_MAX_BLOCK_WRITE	256

/* Object types */
#define MXT_DEBUG_DIAGNOSTIC_T37	37
#define MXT_GEN_MESSAGE_T5		5
#define MXT_GEN_COMMAND_T6		6
#define MXT_GEN_POWER_T7		7
#define MXT_GEN_ACQUIRE_T8		8
#define MXT_GEN_DATASOURCE_T53		53
#define MXT_TOUCH_MULTI_T9		9
#define MXT_TOUCH_KEYARRAY_T15		15
#define MXT_TOUCH_PROXIMITY_T23		23
#define MXT_TOUCH_PROXKEY_T52		52
#define MXT_PROCI_GRIPFACE_T20		20
#define MXT_PROCG_NOISE_T22		22
#define MXT_PROCI_ONETOUCH_T24		24
#define MXT_PROCI_TWOTOUCH_T27		27
#define MXT_PROCI_GRIP_T40		40
#define MXT_PROCI_PALM_T41		41
#define MXT_PROCI_TOUCHSUPPRESSION_T42	42
#define MXT_PROCI_STYLUS_T47		47
#define MXT_PROCG_NOISESUPPRESSION_T48	48
#define MXT_SPT_COMMSCONFIG_T18		18
#define MXT_SPT_GPIOPWM_T19		19
#define MXT_SPT_SELFTEST_T25		25
#define MXT_SPT_CTECONFIG_T28		28
#define MXT_SPT_USERDATA_T38		38
#define MXT_SPT_DIGITIZER_T43		43
#define MXT_SPT_MESSAGECOUNT_T44	44
#define MXT_SPT_CTECONFIG_T46		46

/* MXT_GEN_COMMAND_T6 field */
#define MXT_COMMAND_RESET	0
#define MXT_COMMAND_BACKUPNV	1
#define MXT_COMMAND_CALIBRATE	2
#define MXT_COMMAND_REPORTALL	3
#define MXT_COMMAND_DIAGNOSTIC	5

/* Define for T6 status byte */
#define MXT_T6_STATUS_RESET	(1 << 7)

/* MXT_GEN_POWER_T7 field */
#define MXT_POWER_IDLEACQINT	0
#define MXT_POWER_ACTVACQINT	1
#define MXT_POWER_ACTV2IDLETO	2

/* MXT_GEN_ACQUIRE_T8 field */
#define MXT_ACQUIRE_CHRGTIME	0
#define MXT_ACQUIRE_TCHDRIFT	2
#define MXT_ACQUIRE_DRIFTST	3
#define MXT_ACQUIRE_TCHAUTOCAL	4
#define MXT_ACQUIRE_SYNC	5
#define MXT_ACQUIRE_ATCHCALST	6
#define MXT_ACQUIRE_ATCHCALSTHR	7

/* MXT_TOUCH_MULTI_T9 field */
#define MXT_TOUCH_CTRL		0
#define MXT_T9_ORIENT		9
#define MXT_T9_RANGE		18

struct t9_range {
	u16 x;
	u16 y;
} __packed;

/* Touch orient bits */
#define MXT_XY_SWITCH		(1 << 0)

/* MXT_PROCI_GRIPFACE_T20 field */
#define MXT_GRIPFACE_CTRL	0
#define MXT_GRIPFACE_XLOGRIP	1
#define MXT_GRIPFACE_XHIGRIP	2
#define MXT_GRIPFACE_YLOGRIP	3
#define MXT_GRIPFACE_YHIGRIP	4
#define MXT_GRIPFACE_MAXTCHS	5
#define MXT_GRIPFACE_SZTHR1	7
#define MXT_GRIPFACE_SZTHR2	8
#define MXT_GRIPFACE_SHPTHR1	9
#define MXT_GRIPFACE_SHPTHR2	10
#define MXT_GRIPFACE_SUPEXTTO	11

/* MXT_PROCI_NOISE field */
#define MXT_NOISE_CTRL		0
#define MXT_NOISE_OUTFLEN	1
#define MXT_NOISE_GCAFUL_LSB	3
#define MXT_NOISE_GCAFUL_MSB	4
#define MXT_NOISE_GCAFLL_LSB	5
#define MXT_NOISE_GCAFLL_MSB	6
#define MXT_NOISE_ACTVGCAFVALID	7
#define MXT_NOISE_NOISETHR	8
#define MXT_NOISE_FREQHOPSCALE	10
#define MXT_NOISE_FREQ0		11
#define MXT_NOISE_FREQ1		12
#define MXT_NOISE_FREQ2		13
#define MXT_NOISE_FREQ3		14
#define MXT_NOISE_FREQ4		15
#define MXT_NOISE_IDLEGCAFVALID	16

/* MXT_SPT_COMMSCONFIG_T18 */
#define MXT_COMMS_CTRL		0
#define MXT_COMMS_CMD		1

/* MXT_SPT_CTECONFIG_T28 field */
#define MXT_CTE_CTRL		0
#define MXT_CTE_CMD		1
#define MXT_CTE_MODE		2
#define MXT_CTE_IDLEGCAFDEPTH	3
#define MXT_CTE_ACTVGCAFDEPTH	4
#define MXT_CTE_VOLTAGE		5

#define MXT_VOLTAGE_DEFAULT	2700000
#define MXT_VOLTAGE_STEP	10000

/* Define for MXT_GEN_COMMAND_T6 */
#define MXT_BOOT_VALUE		0xa5
#define MXT_RESET_VALUE		0x01
#define MXT_BACKUP_VALUE	0x55

/* Delay times */
#define MXT_BACKUP_TIME		50	/* msec */
#define MXT_RESET_TIME		200	/* msec */
#define MXT_RESET_TIMEOUT	3000	/* msec */
#define MXT_CRC_TIMEOUT		1000	/* msec */
#define MXT_FW_RESET_TIME	3000	/* msec */
#define MXT_FW_CHG_TIMEOUT	300	/* msec */

/* Command to unlock bootloader */
#define MXT_UNLOCK_CMD_MSB	0xaa
#define MXT_UNLOCK_CMD_LSB	0xdc

/* Bootloader mode status */
#define MXT_WAITING_BOOTLOAD_CMD	0xc0	/* valid 7 6 bit only */
#define MXT_WAITING_FRAME_DATA	0x80	/* valid 7 6 bit only */
#define MXT_FRAME_CRC_CHECK	0x02
#define MXT_FRAME_CRC_FAIL	0x03
#define MXT_FRAME_CRC_PASS	0x04
#define MXT_APP_CRC_FAIL	0x40	/* valid 7 8 bit only */
#define MXT_BOOT_STATUS_MASK	0x3f
#define MXT_BOOT_EXTENDED_ID	(1 << 5)
#define MXT_BOOT_ID_MASK	0x1f

/* Touch status */
#define MXT_UNGRIP		(1 << 0)
#define MXT_SUPPRESS		(1 << 1)
#define MXT_AMP			(1 << 2)
#define MXT_VECTOR		(1 << 3)
#define MXT_MOVE		(1 << 4)
#define MXT_RELEASE		(1 << 5)
#define MXT_PRESS		(1 << 6)
#define MXT_DETECT		(1 << 7)

/* Touchscreen absolute values */
#define MXT_MAX_AREA		0xff

#define MXT_PIXELS_PER_MM	20

struct mxt_info {
	u8 family_id;
	u8 variant_id;
	u8 version;
	u8 build;
	u8 matrix_xsize;
	u8 matrix_ysize;
	u8 object_num;
};

struct mxt_object {
	u8 type;
	u16 start_address;
	u8 size_minus_one;
	u8 instances_minus_one;
	u8 num_report_ids;
} __packed;

struct mxt_message {
	u8 reportid;
	u8 message[7];
};

/* Each client has this additional data */
struct mxt_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	char phys[64];		/* device physical location */
	const struct mxt_platform_data *pdata;
	struct mxt_object *object_table;
	struct mxt_info info;
	unsigned int irq;
	unsigned int max_x;
	unsigned int max_y;
	bool in_bootloader;
	u16 mem_size;
	u32 config_crc;
	u32 info_crc;
	u8 bootloader_addr;

	/* Cached parameters from object table */
	u8 T6_reportid;
	u16 T6_address;
	u16 T7_address;
	u8 T9_reportid_min;
	u8 T9_reportid_max;
	u8 T19_reportid;

	/* for fw update in bootloader */
	struct completion bl_completion;

	/* for reset handling */
	struct completion reset_completion;

	/* for config update handling */
	struct completion crc_completion;
};

static size_t mxt_obj_size(const struct mxt_object *obj)
{
	return obj->size_minus_one + 1;
}

static size_t mxt_obj_instances(const struct mxt_object *obj)
{
	return obj->instances_minus_one + 1;
}

static bool mxt_object_readable(unsigned int type)
{
	switch (type) {
	case MXT_GEN_COMMAND_T6:
	case MXT_GEN_POWER_T7:
	case MXT_GEN_ACQUIRE_T8:
	case MXT_GEN_DATASOURCE_T53:
	case MXT_TOUCH_MULTI_T9:
	case MXT_TOUCH_KEYARRAY_T15:
	case MXT_TOUCH_PROXIMITY_T23:
	case MXT_TOUCH_PROXKEY_T52:
	case MXT_PROCI_GRIPFACE_T20:
	case MXT_PROCG_NOISE_T22:
	case MXT_PROCI_ONETOUCH_T24:
	case MXT_PROCI_TWOTOUCH_T27:
	case MXT_PROCI_GRIP_T40:
	case MXT_PROCI_PALM_T41:
	case MXT_PROCI_TOUCHSUPPRESSION_T42:
	case MXT_PROCI_STYLUS_T47:
	case MXT_PROCG_NOISESUPPRESSION_T48:
	case MXT_SPT_COMMSCONFIG_T18:
	case MXT_SPT_GPIOPWM_T19:
	case MXT_SPT_SELFTEST_T25:
	case MXT_SPT_CTECONFIG_T28:
	case MXT_SPT_USERDATA_T38:
	case MXT_SPT_DIGITIZER_T43:
	case MXT_SPT_CTECONFIG_T46:
		return true;
	default:
		return false;
	}
}

static void mxt_dump_message(struct device *dev,
			     struct mxt_message *message)
{
	dev_dbg(dev, "reportid: %u\tmessage: %*ph\n",
		message->reportid, 7, message->message);
}

static int mxt_wait_for_completion(struct mxt_data *data,
				   struct completion *comp,
				   unsigned int timeout_ms)
{
	struct device *dev = &data->client->dev;
	unsigned long timeout = msecs_to_jiffies(timeout_ms);
	long ret;

	ret = wait_for_completion_interruptible_timeout(comp, timeout);
	if (ret < 0) {
		return ret;
	} else if (ret == 0) {
		dev_err(dev, "Wait for completion timed out.\n");
		return -ETIMEDOUT;
	}
	return 0;
}

static int mxt_bootloader_read(struct mxt_data *data,
			       u8 *val, unsigned int count)
{
	int ret;
	struct i2c_msg msg;

	msg.addr = data->bootloader_addr;
	msg.flags = data->client->flags & I2C_M_TEN;
	msg.flags |= I2C_M_RD;
	msg.len = count;
	msg.buf = val;

	ret = i2c_transfer(data->client->adapter, &msg, 1);

	if (ret == 1) {
		ret = 0;
	} else {
		ret = ret < 0 ? ret : -EIO;
		dev_err(&data->client->dev, "%s: i2c recv failed (%d)\n",
			__func__, ret);
	}

	return ret;
}

static int mxt_bootloader_write(struct mxt_data *data,
				const u8 * const val, unsigned int count)
{
	int ret;
	struct i2c_msg msg;

	msg.addr = data->bootloader_addr;
	msg.flags = data->client->flags & I2C_M_TEN;
	msg.len = count;
	msg.buf = (u8 *)val;

	ret = i2c_transfer(data->client->adapter, &msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		ret = ret < 0 ? ret : -EIO;
		dev_err(&data->client->dev, "%s: i2c send failed (%d)\n",
			__func__, ret);
	}

	return ret;
}

static int mxt_lookup_bootloader_address(struct mxt_data *data)
{
	u8 appmode = data->client->addr;
	u8 bootloader;

	switch (appmode) {
	case 0x4a:
	case 0x4b:
	case 0x4c:
	case 0x4d:
	case 0x5a:
	case 0x5b:
		bootloader = appmode - 0x26;
		break;
	default:
		dev_err(&data->client->dev,
			"Appmode i2c address 0x%02x not found\n",
			appmode);
		return -EINVAL;
	}

	data->bootloader_addr = bootloader;
	return 0;
}

static u8 mxt_get_bootloader_version(struct mxt_data *data, u8 val)
{
	struct device *dev = &data->client->dev;
	u8 buf[3];

	if (val & MXT_BOOT_EXTENDED_ID) {
		if (mxt_bootloader_read(data, &buf[0], 3) != 0) {
			dev_err(dev, "%s: i2c failure\n", __func__);
			return -EIO;
		}

		dev_dbg(dev, "Bootloader ID:%d Version:%d\n", buf[1], buf[2]);

		return buf[0];
	} else {
		dev_dbg(dev, "Bootloader ID:%d\n", val & MXT_BOOT_ID_MASK);

		return val;
	}
}

static int mxt_check_bootloader(struct mxt_data *data, unsigned int state)
{
	struct device *dev = &data->client->dev;
	u8 val;
	int ret;

recheck:
	if (state != MXT_WAITING_BOOTLOAD_CMD) {
		/*
		 * In application update mode, the interrupt
		 * line signals state transitions. We must wait for the
		 * CHG assertion before reading the status byte.
		 * Once the status byte has been read, the line is deasserted.
		 */
		ret = mxt_wait_for_completion(data, &data->bl_completion,
					      MXT_FW_CHG_TIMEOUT);
		if (ret) {
			/*
			 * TODO: handle -ERESTARTSYS better by terminating
			 * fw update process before returning to userspace
			 * by writing length 0x000 to device (iff we are in
			 * WAITING_FRAME_DATA state).
			 */
			dev_err(dev, "Update wait error %d\n", ret);
			return ret;
		}
	}

	ret = mxt_bootloader_read(data, &val, 1);
	if (ret)
		return ret;

	if (state == MXT_WAITING_BOOTLOAD_CMD)
		val = mxt_get_bootloader_version(data, val);

	switch (state) {
	case MXT_WAITING_BOOTLOAD_CMD:
	case MXT_WAITING_FRAME_DATA:
		val &= ~MXT_BOOT_STATUS_MASK;
		break;
	case MXT_FRAME_CRC_PASS:
		if (val == MXT_FRAME_CRC_CHECK) {
			goto recheck;
		} else if (val == MXT_FRAME_CRC_FAIL) {
			dev_err(dev, "Bootloader CRC fail\n");
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	if (val != state) {
		dev_err(dev, "Invalid bootloader state %02X != %02X\n",
			val, state);
		return -EINVAL;
	}

	return 0;
}

static int mxt_unlock_bootloader(struct mxt_data *data)
{
	int ret;
	u8 buf[2];

	buf[0] = MXT_UNLOCK_CMD_LSB;
	buf[1] = MXT_UNLOCK_CMD_MSB;

	ret = mxt_bootloader_write(data, buf, 2);
	if (ret)
		return ret;

	return 0;
}

static int __mxt_read_reg(struct i2c_client *client,
			       u16 reg, u16 len, void *val)
{
	struct i2c_msg xfer[2];
	u8 buf[2];
	int ret;

	buf[0] = reg & 0xff;
	buf[1] = (reg >> 8) & 0xff;

	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 2;
	xfer[0].buf = buf;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = len;
	xfer[1].buf = val;

	ret = i2c_transfer(client->adapter, xfer, 2);
	if (ret == 2) {
		ret = 0;
	} else {
		if (ret >= 0)
			ret = -EIO;
		dev_err(&client->dev, "%s: i2c transfer failed (%d)\n",
			__func__, ret);
	}

	return ret;
}

static int __mxt_write_reg(struct i2c_client *client, u16 reg, u16 len,
			   const void *val)
{
	u8 *buf;
	size_t count;
	int ret;

	count = len + 2;
	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = reg & 0xff;
	buf[1] = (reg >> 8) & 0xff;
	memcpy(&buf[2], val, len);

	ret = i2c_master_send(client, buf, count);
	if (ret == count) {
		ret = 0;
	} else {
		if (ret >= 0)
			ret = -EIO;
		dev_err(&client->dev, "%s: i2c send failed (%d)\n",
			__func__, ret);
	}

	kfree(buf);
	return ret;
}

static int mxt_write_reg(struct i2c_client *client, u16 reg, u8 val)
{
	return __mxt_write_reg(client, reg, 1, &val);
}

static struct mxt_object *
mxt_get_object(struct mxt_data *data, u8 type)
{
	struct mxt_object *object;
	int i;

	for (i = 0; i < data->info.object_num; i++) {
		object = data->object_table + i;
		if (object->type == type)
			return object;
	}

	dev_warn(&data->client->dev, "Invalid object type T%u\n", type);
	return NULL;
}

static int mxt_read_message(struct mxt_data *data,
				 struct mxt_message *message)
{
	struct mxt_object *object;
	u16 reg;

	object = mxt_get_object(data, MXT_GEN_MESSAGE_T5);
	if (!object)
		return -EINVAL;

	reg = object->start_address;
	return __mxt_read_reg(data->client, reg,
			sizeof(struct mxt_message), message);
}

static int mxt_write_object(struct mxt_data *data,
				 u8 type, u8 offset, u8 val)
{
	struct mxt_object *object;
	u16 reg;

	object = mxt_get_object(data, type);
	if (!object || offset >= mxt_obj_size(object))
		return -EINVAL;

	reg = object->start_address;
	return mxt_write_reg(data->client, reg + offset, val);
}

static void mxt_input_button(struct mxt_data *data, struct mxt_message *message)
{
	struct input_dev *input = data->input_dev;
	const struct mxt_platform_data *pdata = data->pdata;
	bool button;
	int i;

	/* Active-low switch */
	for (i = 0; i < pdata->t19_num_keys; i++) {
		if (pdata->t19_keymap[i] == KEY_RESERVED)
			continue;
		button = !(message->message[0] & (1 << i));
		input_report_key(input, pdata->t19_keymap[i], button);
	}
}

static void mxt_input_touchevent(struct mxt_data *data,
				      struct mxt_message *message, int id)
{
	struct device *dev = &data->client->dev;
	u8 status = message->message[0];
	struct input_dev *input_dev = data->input_dev;
	int x;
	int y;
	int area;
	int pressure;

	x = (message->message[1] << 4) | ((message->message[3] >> 4) & 0xf);
	y = (message->message[2] << 4) | ((message->message[3] & 0xf));
	if (data->max_x < 1024)
		x = x >> 2;
	if (data->max_y < 1024)
		y = y >> 2;

	area = message->message[4];
	pressure = message->message[5];

	dev_dbg(dev,
		"[%u] %c%c%c%c%c%c%c%c x: %5u y: %5u area: %3u amp: %3u\n",
		id,
		(status & MXT_DETECT) ? 'D' : '.',
		(status & MXT_PRESS) ? 'P' : '.',
		(status & MXT_RELEASE) ? 'R' : '.',
		(status & MXT_MOVE) ? 'M' : '.',
		(status & MXT_VECTOR) ? 'V' : '.',
		(status & MXT_AMP) ? 'A' : '.',
		(status & MXT_SUPPRESS) ? 'S' : '.',
		(status & MXT_UNGRIP) ? 'U' : '.',
		x, y, area, pressure);

	input_mt_slot(input_dev, id);
	input_mt_report_slot_state(input_dev, MT_TOOL_FINGER,
				   status & MXT_DETECT);

	if (status & MXT_DETECT) {
		input_report_abs(input_dev, ABS_MT_POSITION_X, x);
		input_report_abs(input_dev, ABS_MT_POSITION_Y, y);
		input_report_abs(input_dev, ABS_MT_PRESSURE, pressure);
		input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, area);
	}
}

static u16 mxt_extract_T6_csum(const u8 *csum)
{
	return csum[0] | (csum[1] << 8) | (csum[2] << 16);
}

static bool mxt_is_T9_message(struct mxt_data *data, struct mxt_message *msg)
{
	u8 id = msg->reportid;
	return (id >= data->T9_reportid_min && id <= data->T9_reportid_max);
}

static irqreturn_t mxt_process_messages_until_invalid(struct mxt_data *data)
{
	struct mxt_message message;
	const u8 *payload = &message.message[0];
	struct device *dev = &data->client->dev;
	u8 reportid;
	bool update_input = false;
	u32 crc;

	do {
		if (mxt_read_message(data, &message)) {
			dev_err(dev, "Failed to read message\n");
			return IRQ_NONE;
		}

		reportid = message.reportid;

		if (reportid == data->T6_reportid) {
			u8 status = payload[0];

			crc = mxt_extract_T6_csum(&payload[1]);
			if (crc != data->config_crc) {
				data->config_crc = crc;
				complete(&data->crc_completion);
			}

			dev_dbg(dev, "Status: %02x Config Checksum: %06x\n",
				status, data->config_crc);

			if (status & MXT_T6_STATUS_RESET)
				complete(&data->reset_completion);
		} else if (mxt_is_T9_message(data, &message)) {
			int id = reportid - data->T9_reportid_min;
			mxt_input_touchevent(data, &message, id);
			update_input = true;
		} else if (message.reportid == data->T19_reportid) {
			mxt_input_button(data, &message);
			update_input = true;
		} else {
			mxt_dump_message(dev, &message);
		}
	} while (reportid != 0xff);

	if (update_input) {
		input_mt_report_pointer_emulation(data->input_dev, false);
		input_sync(data->input_dev);
	}

	return IRQ_HANDLED;
}

static irqreturn_t mxt_interrupt(int irq, void *dev_id)
{
	struct mxt_data *data = dev_id;

	if (data->in_bootloader) {
		/* bootloader state transition completion */
		complete(&data->bl_completion);
		return IRQ_HANDLED;
	}

	return mxt_process_messages_until_invalid(data);
}

static int mxt_t6_command(struct mxt_data *data, u16 cmd_offset,
			  u8 value, bool wait)
{
	u16 reg;
	u8 command_register;
	int timeout_counter = 0;
	int ret;

	reg = data->T6_address + cmd_offset;

	ret = mxt_write_reg(data->client, reg, value);
	if (ret)
		return ret;

	if (!wait)
		return 0;

	do {
		msleep(20);
		ret = __mxt_read_reg(data->client, reg, 1, &command_register);
		if (ret)
			return ret;
	} while (command_register != 0 && timeout_counter++ <= 100);

	if (timeout_counter > 100) {
		dev_err(&data->client->dev, "Command failed!\n");
		return -EIO;
	}

	return 0;
}

static int mxt_soft_reset(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int ret = 0;

	dev_info(dev, "Resetting chip\n");

	INIT_COMPLETION(data->reset_completion);

	ret = mxt_t6_command(data, MXT_COMMAND_RESET, MXT_RESET_VALUE, false);
	if (ret)
		return ret;

	ret = mxt_wait_for_completion(data, &data->reset_completion,
				      MXT_RESET_TIMEOUT);
	if (ret)
		return ret;

	return 0;
}

static void mxt_update_crc(struct mxt_data *data, u8 cmd, u8 value)
{
	/*
	 * On failure, CRC is set to 0 and config will always be
	 * downloaded.
	 */
	data->config_crc = 0;
	INIT_COMPLETION(data->crc_completion);

	mxt_t6_command(data, cmd, value, true);

	/*
	 * Wait for crc message. On failure, CRC is set to 0 and config will
	 * always be downloaded.
	 */
	mxt_wait_for_completion(data, &data->crc_completion, MXT_CRC_TIMEOUT);
}

static void mxt_calc_crc24(u32 *crc, u8 firstbyte, u8 secondbyte)
{
	static const unsigned int crcpoly = 0x80001B;
	u32 result;
	u32 data_word;

	data_word = (secondbyte << 8) | firstbyte;
	result = ((*crc << 1) ^ data_word);

	if (result & 0x1000000)
		result ^= crcpoly;

	*crc = result;
}

static u32 mxt_calculate_crc(u8 *base, off_t start_off, off_t end_off)
{
	u32 crc = 0;
	u8 *ptr = base + start_off;
	u8 *last_val = base + end_off - 1;

	if (end_off < start_off)
		return -EINVAL;

	while (ptr < last_val) {
		mxt_calc_crc24(&crc, *ptr, *(ptr + 1));
		ptr += 2;
	}

	/* if len is odd, fill the last byte with 0 */
	if (ptr == last_val)
		mxt_calc_crc24(&crc, *ptr, 0);

	/* Mask to 24-bit */
	crc &= 0x00FFFFFF;

	return crc;
}

/*
 * mxt_check_reg_init - download configuration to chip
 *
 * Atmel Raw Config File Format
 *
 * The first four lines of the raw config file contain:
 *  1) Version
 *  2) Chip ID Information (first 7 bytes of device memory)
 *  3) Chip Information Block 24-bit CRC Checksum
 *  4) Chip Configuration 24-bit CRC Checksum
 *
 * The rest of the file consists of one line per object instance:
 *   <TYPE> <INSTANCE> <SIZE> <CONTENTS>
 *
 *   <TYPE> - 2-byte object type as hex
 *   <INSTANCE> - 2-byte object instance number as hex
 *   <SIZE> - 2-byte object size as hex
 *   <CONTENTS> - array of <SIZE> 1-byte hex values
 */
static int mxt_check_reg_init(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	struct mxt_info cfg_info;
	struct mxt_object *object;
	const struct firmware *cfg = NULL;
	int ret;
	int offset;
	int data_pos;
	int byte_offset;
	int i;
	int cfg_start_ofs;
	u32 info_crc, config_crc, calculated_crc;
	u8 *config_mem;
	size_t config_mem_size;
	unsigned int type, instance, size;
	u8 val;
	u16 reg;

	ret = request_firmware(&cfg, MXT_CFG_NAME, dev);
	if (ret < 0) {
		dev_err(dev, "Failure to request config file %s\n",
			MXT_CFG_NAME);
		return 0;
	}

	mxt_update_crc(data, MXT_COMMAND_REPORTALL, 1);

	if (strncmp(cfg->data, MXT_CFG_MAGIC, strlen(MXT_CFG_MAGIC))) {
		dev_err(dev, "Unrecognised config file\n");
		ret = -EINVAL;
		goto release;
	}

	data_pos = strlen(MXT_CFG_MAGIC);

	/* Load information block and check */
	for (i = 0; i < sizeof(struct mxt_info); i++) {
		ret = sscanf(cfg->data + data_pos, "%hhx%n",
			     (unsigned char *)&cfg_info + i,
			     &offset);
		if (ret != 1) {
			dev_err(dev, "Bad format\n");
			ret = -EINVAL;
			goto release;
		}

		data_pos += offset;
	}

	if (cfg_info.family_id != data->info.family_id) {
		dev_err(dev, "Family ID mismatch!\n");
		ret = -EINVAL;
		goto release;
	}

	if (cfg_info.variant_id != data->info.variant_id) {
		dev_err(dev, "Variant ID mismatch!\n");
		ret = -EINVAL;
		goto release;
	}

	/* Read CRCs */
	ret = sscanf(cfg->data + data_pos, "%x%n", &info_crc, &offset);
	if (ret != 1) {
		dev_err(dev, "Bad format: failed to parse Info CRC\n");
		ret = -EINVAL;
		goto release;
	}
	data_pos += offset;

	ret = sscanf(cfg->data + data_pos, "%x%n", &config_crc, &offset);
	if (ret != 1) {
		dev_err(dev, "Bad format: failed to parse Config CRC\n");
		ret = -EINVAL;
		goto release;
	}
	data_pos += offset;

	/*
	 * The Info Block CRC is calculated over mxt_info and the object
	 * table. If it does not match then we are trying to load the
	 * configuration from a different chip or firmware version, so
	 * the configuration CRC is invalid anyway.
	 */
	if (info_crc == data->info_crc) {
		if (config_crc == 0 || data->config_crc == 0) {
			dev_info(dev, "CRC zero, attempting to apply config\n");
		} else if (config_crc == data->config_crc) {
			dev_dbg(dev, "Config CRC 0x%06X: OK\n",
				 data->config_crc);
			ret = 0;
			goto release;
		} else {
			dev_info(dev, "Config CRC 0x%06X: does not match file 0x%06X\n",
				 data->config_crc, config_crc);
		}
	} else {
		dev_warn(dev,
			 "Warning: Info CRC error - device=0x%06X file=0x%06X\n",
			 data->info_crc, info_crc);
	}

	/* Malloc memory to store configuration */
	cfg_start_ofs = MXT_OBJECT_START +
			data->info.object_num * sizeof(struct mxt_object) +
			MXT_INFO_CHECKSUM_SIZE;
	config_mem_size = data->mem_size - cfg_start_ofs;
	config_mem = kzalloc(config_mem_size, GFP_KERNEL);
	if (!config_mem) {
		dev_err(dev, "Failed to allocate memory\n");
		ret = -ENOMEM;
		goto release;
	}

	while (data_pos < cfg->size) {
		/* Read type, instance, length */
		ret = sscanf(cfg->data + data_pos, "%x %x %x%n",
			     &type, &instance, &size, &offset);
		if (ret == 0) {
			/* EOF */
			break;
		} else if (ret != 3) {
			dev_err(dev, "Bad format: failed to parse object\n");
			ret = -EINVAL;
			goto release_mem;
		}
		data_pos += offset;

		object = mxt_get_object(data, type);
		if (!object) {
			/* Skip object */
			for (i = 0; i < size; i++) {
				ret = sscanf(cfg->data + data_pos, "%hhx%n",
					     &val,
					     &offset);
				data_pos += offset;
			}
			continue;
		}

		if (size > mxt_obj_size(object)) {
			/*
			 * Either we are in fallback mode due to wrong
			 * config or config from a later fw version,
			 * or the file is corrupt or hand-edited.
			 */
			dev_warn(dev, "Discarding %zu byte(s) in T%u\n",
				 size - mxt_obj_size(object), type);
		} else if (mxt_obj_size(object) > size) {
			/*
			 * If firmware is upgraded, new bytes may be added to
			 * end of objects. It is generally forward compatible
			 * to zero these bytes - previous behaviour will be
			 * retained. However this does invalidate the CRC and
			 * will force fallback mode until the configuration is
			 * updated. We warn here but do nothing else - the
			 * malloc has zeroed the entire configuration.
			 */
			dev_warn(dev, "Zeroing %zu byte(s) in T%d\n",
				 mxt_obj_size(object) - size, type);
		}

		if (instance >= mxt_obj_instances(object)) {
			dev_err(dev, "Object instances exceeded!\n");
			ret = -EINVAL;
			goto release_mem;
		}

		reg = object->start_address + mxt_obj_size(object) * instance;

		for (i = 0; i < size; i++) {
			ret = sscanf(cfg->data + data_pos, "%hhx%n",
				     &val,
				     &offset);
			if (ret != 1) {
				dev_err(dev, "Bad format in T%d\n", type);
				ret = -EINVAL;
				goto release_mem;
			}
			data_pos += offset;

			if (i > mxt_obj_size(object))
				continue;

			byte_offset = reg + i - cfg_start_ofs;

			if ((byte_offset >= 0)
			    && (byte_offset <= config_mem_size)) {
				*(config_mem + byte_offset) = val;
			} else {
				dev_err(dev, "Bad object: reg:%d, T%d, ofs=%d\n",
					reg, object->type, byte_offset);
				ret = -EINVAL;
				goto release_mem;
			}
		}
	}

	/* Calculate crc of the received configs (not the raw config file) */
	if (data->T7_address < cfg_start_ofs) {
		dev_err(dev, "Bad T7 address, T7addr = %x, config offset %x\n",
			data->T7_address, cfg_start_ofs);
		ret = 0;
		goto release_mem;
	}

	calculated_crc = mxt_calculate_crc(config_mem,
					   data->T7_address - cfg_start_ofs,
					   config_mem_size);

	if (config_crc > 0 && (config_crc != calculated_crc))
		dev_warn(dev, "Config CRC error, calculated=%06X, file=%06X\n",
			 calculated_crc, config_crc);

	/* Write configuration as blocks */
	byte_offset = 0;
	while (byte_offset < config_mem_size) {
		size = config_mem_size - byte_offset;

		if (size > MXT_MAX_BLOCK_WRITE)
			size = MXT_MAX_BLOCK_WRITE;

		ret = __mxt_write_reg(data->client,
				      cfg_start_ofs + byte_offset,
				      size, config_mem + byte_offset);
		if (ret != 0) {
			dev_err(dev, "Config write error, ret=%d\n", ret);
			goto release_mem;
		}

		byte_offset += size;
	}

	mxt_update_crc(data, MXT_COMMAND_BACKUPNV, MXT_BACKUP_VALUE);

	ret = mxt_soft_reset(data);
	if (ret)
		goto release_mem;

	dev_info(dev, "Config successfully updated\n");

release_mem:
	kfree(config_mem);
release:
	release_firmware(cfg);
	return ret;
}

static int mxt_make_highchg(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	struct mxt_message message;
	int count = 10;
	int error;

	/* Read dummy message to make high CHG pin */
	do {
		error = mxt_read_message(data, &message);
		if (error)
			return error;
	} while (message.reportid != 0xff && --count);

	if (!count) {
		dev_err(dev, "CHG pin isn't cleared\n");
		return -EBUSY;
	}

	return 0;
}

static int mxt_get_info(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	struct mxt_info *info = &data->info;
	int error;

	/* Read 7-byte info block starting at address 0 */
	error = __mxt_read_reg(client, MXT_INFO, sizeof(*info), info);
	if (error)
		return error;

	return 0;
}

static int mxt_get_object_table(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	size_t table_size;
	int error;
	int i;
	u8 reportid;
	u16 end_address;

	table_size = data->info.object_num * sizeof(struct mxt_object);
	error = __mxt_read_reg(client, MXT_OBJECT_START, table_size,
			data->object_table);
	if (error)
		return error;

	/* Valid Report IDs start counting from 1 */
	reportid = 1;
	data->mem_size = 0;
	for (i = 0; i < data->info.object_num; i++) {
		struct mxt_object *object = data->object_table + i;
		u8 min_id, max_id;

		le16_to_cpus(&object->start_address);

		if (object->num_report_ids) {
			min_id = reportid;
			reportid += object->num_report_ids *
					mxt_obj_instances(object);
			max_id = reportid - 1;
		} else {
			min_id = 0;
			max_id = 0;
		}

		dev_dbg(&data->client->dev,
			"T%u Start:%u Size:%zu Instances:%zu Report IDs:%u-%u\n",
			object->type, object->start_address,
			mxt_obj_size(object), mxt_obj_instances(object),
			min_id, max_id);

		switch (object->type) {
		case MXT_GEN_COMMAND_T6:
			data->T6_reportid = min_id;
			data->T6_address = object->start_address;
			break;
		case MXT_GEN_POWER_T7:
			data->T7_address = object->start_address;
			break;
		case MXT_TOUCH_MULTI_T9:
			data->T9_reportid_min = min_id;
			data->T9_reportid_max = max_id;
			break;
		case MXT_SPT_GPIOPWM_T19:
			data->T19_reportid = min_id;
			break;
		}

		end_address = object->start_address
			+ mxt_obj_size(object) * mxt_obj_instances(object) - 1;

		if (end_address >= data->mem_size)
			data->mem_size = end_address + 1;
	}

	return 0;
}

static void mxt_free_object_table(struct mxt_data *data)
{
	kfree(data->object_table);
	data->object_table = NULL;
	data->T6_reportid = 0;
	data->T9_reportid_min = 0;
	data->T9_reportid_max = 0;
	data->T19_reportid = 0;
}

static int mxt_read_t9_resolution(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int error;
	struct t9_range range;
	unsigned char orient;
	struct mxt_object *object;

	object = mxt_get_object(data, MXT_TOUCH_MULTI_T9);
	if (!object)
		return -EINVAL;

	error = __mxt_read_reg(client,
			       object->start_address + MXT_T9_RANGE,
			       sizeof(range), &range);
	if (error)
		return error;

	le16_to_cpus(range.x);
	le16_to_cpus(range.y);

	error =  __mxt_read_reg(client,
				object->start_address + MXT_T9_ORIENT,
				1, &orient);
	if (error)
		return error;

	/* Handle default values */
	if (range.x == 0)
		range.x = 1023;

	if (range.y == 0)
		range.y = 1023;

	if (orient & MXT_XY_SWITCH) {
		data->max_x = range.y;
		data->max_y = range.x;
	} else {
		data->max_x = range.x;
		data->max_y = range.y;
	}

	dev_dbg(&client->dev,
		"Touchscreen size X%uY%u\n", data->max_x, data->max_y);

	return 0;
}

static int mxt_initialize(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	struct mxt_info *info = &data->info;
	int error;

	error = mxt_get_info(data);
	if (error)
		return error;

	data->object_table = kcalloc(info->object_num,
				     sizeof(struct mxt_object),
				     GFP_KERNEL);
	if (!data->object_table) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	/* Get object table information */
	error = mxt_get_object_table(data);
	if (error) {
		dev_err(&client->dev, "Error %d reading object table\n", error);
		goto err_free_object_table;
	}

	/* Check register init values */
	error = mxt_check_reg_init(data);
	if (error) {
		dev_err(&client->dev, "Error %d initializing configuration\n",
			error);
		goto err_free_object_table;
	}

	error = mxt_read_t9_resolution(data);
	if (error) {
		dev_err(&client->dev, "Failed to initialize T9 resolution\n");
		goto err_free_object_table;
	}

	dev_info(&client->dev,
		 "Family: %u Variant: %u Firmware V%u.%u.%02X Objects: %u\n",
		 info->family_id, info->variant_id, info->version >> 4,
		 info->version & 0xf, info->build, info->object_num);

	return 0;

err_free_object_table:
	mxt_free_object_table(data);
	return error;
}

/* Firmware Version is returned as Major.Minor.Build */
static ssize_t mxt_fw_version_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct mxt_info *info = &data->info;
	return scnprintf(buf, PAGE_SIZE, "%u.%u.%02X\n",
			 info->version >> 4, info->version & 0xf, info->build);
}

/* Hardware Version is returned as FamilyID.VariantID */
static ssize_t mxt_hw_version_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct mxt_info *info = &data->info;
	return scnprintf(buf, PAGE_SIZE, "%u.%u\n",
			 info->family_id, info->variant_id);
}

static ssize_t mxt_show_instance(char *buf, int count,
				 struct mxt_object *object, int instance,
				 const u8 *val)
{
	int i;

	if (mxt_obj_instances(object) > 1)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "Instance %u\n", instance);

	for (i = 0; i < mxt_obj_size(object); i++)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				"\t[%2u]: %02x (%d)\n", i, val[i], val[i]);
	count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

	return count;
}

static ssize_t mxt_object_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct mxt_object *object;
	int count = 0;
	int i, j;
	int error;
	u8 *obuf;

	/* Pre-allocate buffer large enough to hold max sized object. */
	obuf = kmalloc(256, GFP_KERNEL);
	if (!obuf)
		return -ENOMEM;

	error = 0;
	for (i = 0; i < data->info.object_num; i++) {
		object = data->object_table + i;

		if (!mxt_object_readable(object->type))
			continue;

		count += scnprintf(buf + count, PAGE_SIZE - count,
				"T%u:\n", object->type);

		for (j = 0; j < mxt_obj_instances(object); j++) {
			u16 size = mxt_obj_size(object);
			u16 addr = object->start_address + j * size;

			error = __mxt_read_reg(data->client, addr, size, obuf);
			if (error)
				goto done;

			count = mxt_show_instance(buf, count, object, j, obuf);
		}
	}

done:
	kfree(obuf);
	return error ?: count;
}

static int mxt_check_firmware_format(struct device *dev,
				     const struct firmware *fw)
{
	unsigned int pos = 0;
	char c;

	while (pos < fw->size) {
		c = *(fw->data + pos);

		if (c < '0' || (c > '9' && c < 'A') || c > 'F')
			return 0;

		pos++;
	}

	/*
	 * To convert file try:
	 * xxd -r -p mXTXXX__APP_VX-X-XX.enc > maxtouch.fw
	 */
	dev_err(dev, "Aborting: firmware file must be in binary format\n");

	return -EINVAL;
}

static int mxt_load_fw(struct device *dev, const char *fn)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	const struct firmware *fw = NULL;
	unsigned int frame_size;
	unsigned int pos = 0;
	unsigned int retry = 0;
	unsigned int frame = 0;
	int ret;

	ret = request_firmware(&fw, fn, dev);
	if (ret) {
		dev_err(dev, "Unable to open firmware %s\n", fn);
		return ret;
	}

	/* Check for incorrect enc file */
	ret = mxt_check_firmware_format(dev, fw);
	if (ret)
		goto release_firmware;

	ret = mxt_lookup_bootloader_address(data);
	if (ret)
		goto release_firmware;

	/* Change to the bootloader mode */
	data->in_bootloader = true;

	ret = mxt_t6_command(data, MXT_COMMAND_RESET, MXT_BOOT_VALUE, false);
	if (ret)
		goto release_firmware;

	msleep(MXT_RESET_TIME);

	INIT_COMPLETION(data->bl_completion);

	ret = mxt_check_bootloader(data, MXT_WAITING_BOOTLOAD_CMD);
	if (ret)
		goto disable_irq;

	/* Unlock bootloader */
	mxt_unlock_bootloader(data);

	while (pos < fw->size) {
		ret = mxt_check_bootloader(data, MXT_WAITING_FRAME_DATA);
		if (ret)
			goto disable_irq;

		frame_size = ((*(fw->data + pos) << 8) | *(fw->data + pos + 1));

		/* Take account of CRC bytes */
		frame_size += 2;

		/* Write one frame to device */
		ret = mxt_bootloader_write(data, fw->data + pos, frame_size);
		if (ret)
			goto disable_irq;

		ret = mxt_check_bootloader(data, MXT_FRAME_CRC_PASS);
		if (ret) {
			retry++;

			/* Back off by 20ms per retry */
			msleep(retry * 20);

			if (retry > 20) {
				dev_err(dev, "Retry count exceeded\n");
				goto disable_irq;
			}
		} else {
			retry = 0;
			pos += frame_size;
			frame++;
		}

		if (frame % 50 == 0)
			dev_dbg(dev, "Sent %d frames, %d/%zd bytes\n",
				frame, pos, fw->size);
	}

	/* Wait for flash. */
	ret = mxt_wait_for_completion(data, &data->bl_completion,
				      MXT_FW_RESET_TIME);
	if (ret)
		goto disable_irq;

	dev_dbg(dev, "Sent %d frames, %d bytes\n", frame, pos);

	/*
	 * Wait for device to reset. Some bootloader versions do not assert
	 * the CHG line after bootloading has finished, so ignore potential
	 * errors.
	 */
	mxt_wait_for_completion(data, &data->bl_completion, MXT_FW_RESET_TIME);

	data->in_bootloader = false;

disable_irq:
	disable_irq(data->irq);
release_firmware:
	release_firmware(fw);
	return ret;
}

static ssize_t mxt_update_fw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int error;

	error = mxt_load_fw(dev, MXT_FW_NAME);
	if (error) {
		dev_err(dev, "The firmware update failed(%d)\n", error);
		count = error;
	} else {
		dev_info(dev, "The firmware update succeeded\n");

		mxt_free_object_table(data);

		mxt_initialize(data);

		enable_irq(data->irq);

		error = mxt_make_highchg(data);
		if (error)
			return error;
	}

	return count;
}

static DEVICE_ATTR(fw_version, S_IRUGO, mxt_fw_version_show, NULL);
static DEVICE_ATTR(hw_version, S_IRUGO, mxt_hw_version_show, NULL);
static DEVICE_ATTR(object, S_IRUGO, mxt_object_show, NULL);
static DEVICE_ATTR(update_fw, S_IWUSR, NULL, mxt_update_fw_store);

static struct attribute *mxt_attrs[] = {
	&dev_attr_fw_version.attr,
	&dev_attr_hw_version.attr,
	&dev_attr_object.attr,
	&dev_attr_update_fw.attr,
	NULL
};

static const struct attribute_group mxt_attr_group = {
	.attrs = mxt_attrs,
};

static void mxt_start(struct mxt_data *data)
{
	/* Touch enable */
	mxt_write_object(data,
			MXT_TOUCH_MULTI_T9, MXT_TOUCH_CTRL, 0x83);
}

static void mxt_stop(struct mxt_data *data)
{
	/* Touch disable */
	mxt_write_object(data,
			MXT_TOUCH_MULTI_T9, MXT_TOUCH_CTRL, 0);
}

static int mxt_input_open(struct input_dev *dev)
{
	struct mxt_data *data = input_get_drvdata(dev);

	mxt_start(data);

	return 0;
}

static void mxt_input_close(struct input_dev *dev)
{
	struct mxt_data *data = input_get_drvdata(dev);

	mxt_stop(data);
}

static int mxt_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	const struct mxt_platform_data *pdata = dev_get_platdata(&client->dev);
	struct mxt_data *data;
	struct input_dev *input_dev;
	int error;
	unsigned int num_mt_slots;
	unsigned int mt_flags = 0;
	int i;

	if (!pdata)
		return -EINVAL;

	data = kzalloc(sizeof(struct mxt_data), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!data || !input_dev) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	input_dev->name = "Atmel maXTouch Touchscreen";
	snprintf(data->phys, sizeof(data->phys), "i2c-%u-%04x/input0",
		 client->adapter->nr, client->addr);

	input_dev->phys = data->phys;

	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	input_dev->open = mxt_input_open;
	input_dev->close = mxt_input_close;

	data->client = client;
	data->input_dev = input_dev;
	data->pdata = pdata;
	data->irq = client->irq;

	init_completion(&data->bl_completion);
	init_completion(&data->reset_completion);
	init_completion(&data->crc_completion);

	error = mxt_initialize(data);
	if (error)
		goto err_free_mem;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);

	if (pdata->t19_num_keys) {
		__set_bit(INPUT_PROP_BUTTONPAD, input_dev->propbit);

		for (i = 0; i < pdata->t19_num_keys; i++)
			if (pdata->t19_keymap[i] != KEY_RESERVED)
				input_set_capability(input_dev, EV_KEY,
						     pdata->t19_keymap[i]);

		mt_flags |= INPUT_MT_POINTER;

		input_abs_set_res(input_dev, ABS_X, MXT_PIXELS_PER_MM);
		input_abs_set_res(input_dev, ABS_Y, MXT_PIXELS_PER_MM);
		input_abs_set_res(input_dev, ABS_MT_POSITION_X,
				  MXT_PIXELS_PER_MM);
		input_abs_set_res(input_dev, ABS_MT_POSITION_Y,
				  MXT_PIXELS_PER_MM);

		input_dev->name = "Atmel maXTouch Touchpad";
	}

	/* For single touch */
	input_set_abs_params(input_dev, ABS_X,
			     0, data->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_Y,
			     0, data->max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE,
			     0, 255, 0, 0);

	/* For multi touch */
	num_mt_slots = data->T9_reportid_max - data->T9_reportid_min + 1;
	error = input_mt_init_slots(input_dev, num_mt_slots, mt_flags);
	if (error)
		goto err_free_object;
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
			     0, MXT_MAX_AREA, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
			     0, data->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
			     0, data->max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_PRESSURE,
			     0, 255, 0, 0);

	input_set_drvdata(input_dev, data);
	i2c_set_clientdata(client, data);

	error = request_threaded_irq(client->irq, NULL, mxt_interrupt,
				     pdata->irqflags | IRQF_ONESHOT,
				     client->name, data);
	if (error) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		goto err_free_object;
	}

	error = mxt_make_highchg(data);
	if (error)
		goto err_free_irq;

	error = input_register_device(input_dev);
	if (error) {
		dev_err(&client->dev, "Error %d registering input device\n",
			error);
		goto err_free_irq;
	}

	error = sysfs_create_group(&client->dev.kobj, &mxt_attr_group);
	if (error) {
		dev_err(&client->dev, "Failure %d creating sysfs group\n",
			error);
		goto err_unregister_device;
	}

	return 0;

err_unregister_device:
	input_unregister_device(input_dev);
	input_dev = NULL;
err_free_irq:
	free_irq(client->irq, data);
err_free_object:
	kfree(data->object_table);
err_free_mem:
	input_free_device(input_dev);
	kfree(data);
	return error;
}

static int mxt_remove(struct i2c_client *client)
{
	struct mxt_data *data = i2c_get_clientdata(client);

	sysfs_remove_group(&client->dev.kobj, &mxt_attr_group);
	free_irq(data->irq, data);
	input_unregister_device(data->input_dev);
	kfree(data->object_table);
	kfree(data);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mxt_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mxt_data *data = i2c_get_clientdata(client);
	struct input_dev *input_dev = data->input_dev;

	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		mxt_stop(data);

	mutex_unlock(&input_dev->mutex);

	return 0;
}

static int mxt_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mxt_data *data = i2c_get_clientdata(client);
	struct input_dev *input_dev = data->input_dev;

	mxt_soft_reset(data);

	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		mxt_start(data);

	mutex_unlock(&input_dev->mutex);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(mxt_pm_ops, mxt_suspend, mxt_resume);

static const struct i2c_device_id mxt_id[] = {
	{ "qt602240_ts", 0 },
	{ "atmel_mxt_ts", 0 },
	{ "atmel_mxt_tp", 0 },
	{ "mXT224", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mxt_id);

static struct i2c_driver mxt_driver = {
	.driver = {
		.name	= "atmel_mxt_ts",
		.owner	= THIS_MODULE,
		.pm	= &mxt_pm_ops,
	},
	.probe		= mxt_probe,
	.remove		= mxt_remove,
	.id_table	= mxt_id,
};

module_i2c_driver(mxt_driver);

/* Module information */
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_DESCRIPTION("Atmel maXTouch Touchscreen driver");
MODULE_LICENSE("GPL");

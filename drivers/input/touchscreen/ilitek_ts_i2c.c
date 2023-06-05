// SPDX-License-Identifier: GPL-2.0
/*
 * ILITEK Touch IC driver for 23XX, 25XX and Lego series
 *
 * Copyright (C) 2011 ILI Technology Corporation.
 * Copyright (C) 2020 Luca Hsu <luca_hsu@ilitek.com>
 * Copyright (C) 2021 Joe Hung <joe_hung@ilitek.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/input/touchscreen.h>
#include <asm/unaligned.h>


#define ILITEK_TS_NAME					"ilitek_ts"
#define BL_V1_8						0x108
#define BL_V1_7						0x107
#define BL_V1_6						0x106

#define ILITEK_TP_CMD_GET_TP_RES			0x20
#define ILITEK_TP_CMD_GET_SCRN_RES			0x21
#define ILITEK_TP_CMD_SET_IC_SLEEP			0x30
#define ILITEK_TP_CMD_SET_IC_WAKE			0x31
#define ILITEK_TP_CMD_GET_FW_VER			0x40
#define ILITEK_TP_CMD_GET_PRL_VER			0x42
#define ILITEK_TP_CMD_GET_MCU_VER			0x61
#define ILITEK_TP_CMD_GET_IC_MODE			0xC0

#define REPORT_COUNT_ADDRESS				61
#define ILITEK_SUPPORT_MAX_POINT			40

struct ilitek_protocol_info {
	u16 ver;
	u8 ver_major;
};

struct ilitek_ts_data {
	struct i2c_client		*client;
	struct gpio_desc		*reset_gpio;
	struct input_dev		*input_dev;
	struct touchscreen_properties	prop;

	const struct ilitek_protocol_map *ptl_cb_func;
	struct ilitek_protocol_info	ptl;

	char				product_id[30];
	u16				mcu_ver;
	u8				ic_mode;
	u8				firmware_ver[8];

	s32				reset_time;
	s32				screen_max_x;
	s32				screen_max_y;
	s32				screen_min_x;
	s32				screen_min_y;
	s32				max_tp;
};

struct ilitek_protocol_map {
	u16 cmd;
	const char *name;
	int (*func)(struct ilitek_ts_data *ts, u16 cmd, u8 *inbuf, u8 *outbuf);
};

enum ilitek_cmds {
	/* common cmds */
	GET_PTL_VER = 0,
	GET_FW_VER,
	GET_SCRN_RES,
	GET_TP_RES,
	GET_IC_MODE,
	GET_MCU_VER,
	SET_IC_SLEEP,
	SET_IC_WAKE,

	/* ALWAYS keep at the end */
	MAX_CMD_CNT
};

/* ILITEK I2C R/W APIs */
static int ilitek_i2c_write_and_read(struct ilitek_ts_data *ts,
				     u8 *cmd, int write_len, int delay,
				     u8 *data, int read_len)
{
	int error;
	struct i2c_client *client = ts->client;
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = write_len,
			.buf = cmd,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = read_len,
			.buf = data,
		},
	};

	if (delay == 0 && write_len > 0 && read_len > 0) {
		error = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (error < 0)
			return error;
	} else {
		if (write_len > 0) {
			error = i2c_transfer(client->adapter, msgs, 1);
			if (error < 0)
				return error;
		}
		if (delay > 0)
			mdelay(delay);

		if (read_len > 0) {
			error = i2c_transfer(client->adapter, msgs + 1, 1);
			if (error < 0)
				return error;
		}
	}

	return 0;
}

/* ILITEK ISR APIs */
static void ilitek_touch_down(struct ilitek_ts_data *ts, unsigned int id,
			      unsigned int x, unsigned int y)
{
	struct input_dev *input = ts->input_dev;

	input_mt_slot(input, id);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, true);

	touchscreen_report_pos(input, &ts->prop, x, y, true);
}

static int ilitek_process_and_report_v6(struct ilitek_ts_data *ts)
{
	int error = 0;
	u8 buf[512];
	int packet_len = 5;
	int packet_max_point = 10;
	int report_max_point;
	int i, count;
	struct input_dev *input = ts->input_dev;
	struct device *dev = &ts->client->dev;
	unsigned int x, y, status, id;

	error = ilitek_i2c_write_and_read(ts, NULL, 0, 0, buf, 64);
	if (error) {
		dev_err(dev, "get touch info failed, err:%d\n", error);
		goto err_sync_frame;
	}

	report_max_point = buf[REPORT_COUNT_ADDRESS];
	if (report_max_point > ts->max_tp) {
		dev_err(dev, "FW report max point:%d > panel info. max:%d\n",
			report_max_point, ts->max_tp);
		error = -EINVAL;
		goto err_sync_frame;
	}

	count = DIV_ROUND_UP(report_max_point, packet_max_point);
	for (i = 1; i < count; i++) {
		error = ilitek_i2c_write_and_read(ts, NULL, 0, 0,
						  buf + i * 64, 64);
		if (error) {
			dev_err(dev, "get touch info. failed, cnt:%d, err:%d\n",
				count, error);
			goto err_sync_frame;
		}
	}

	for (i = 0; i < report_max_point; i++) {
		status = buf[i * packet_len + 1] & 0x40;
		if (!status)
			continue;

		id = buf[i * packet_len + 1] & 0x3F;

		x = get_unaligned_le16(buf + i * packet_len + 2);
		y = get_unaligned_le16(buf + i * packet_len + 4);

		if (x > ts->screen_max_x || x < ts->screen_min_x ||
		    y > ts->screen_max_y || y < ts->screen_min_y) {
			dev_warn(dev, "invalid position, X[%d,%u,%d], Y[%d,%u,%d]\n",
				 ts->screen_min_x, x, ts->screen_max_x,
				 ts->screen_min_y, y, ts->screen_max_y);
			continue;
		}

		ilitek_touch_down(ts, id, x, y);
	}

err_sync_frame:
	input_mt_sync_frame(input);
	input_sync(input);
	return error;
}

/* APIs of cmds for ILITEK Touch IC */
static int api_protocol_set_cmd(struct ilitek_ts_data *ts,
				u16 idx, u8 *inbuf, u8 *outbuf)
{
	u16 cmd;
	int error;

	if (idx >= MAX_CMD_CNT)
		return -EINVAL;

	cmd = ts->ptl_cb_func[idx].cmd;
	error = ts->ptl_cb_func[idx].func(ts, cmd, inbuf, outbuf);
	if (error)
		return error;

	return 0;
}

static int api_protocol_get_ptl_ver(struct ilitek_ts_data *ts,
				    u16 cmd, u8 *inbuf, u8 *outbuf)
{
	int error;
	u8 buf[64];

	buf[0] = cmd;
	error = ilitek_i2c_write_and_read(ts, buf, 1, 5, outbuf, 3);
	if (error)
		return error;

	ts->ptl.ver = get_unaligned_be16(outbuf);
	ts->ptl.ver_major = outbuf[0];

	return 0;
}

static int api_protocol_get_mcu_ver(struct ilitek_ts_data *ts,
				    u16 cmd, u8 *inbuf, u8 *outbuf)
{
	int error;
	u8 buf[64];

	buf[0] = cmd;
	error = ilitek_i2c_write_and_read(ts, buf, 1, 5, outbuf, 32);
	if (error)
		return error;

	ts->mcu_ver = get_unaligned_le16(outbuf);
	memset(ts->product_id, 0, sizeof(ts->product_id));
	memcpy(ts->product_id, outbuf + 6, 26);

	return 0;
}

static int api_protocol_get_fw_ver(struct ilitek_ts_data *ts,
				   u16 cmd, u8 *inbuf, u8 *outbuf)
{
	int error;
	u8 buf[64];

	buf[0] = cmd;
	error = ilitek_i2c_write_and_read(ts, buf, 1, 5, outbuf, 8);
	if (error)
		return error;

	memcpy(ts->firmware_ver, outbuf, 8);

	return 0;
}

static int api_protocol_get_scrn_res(struct ilitek_ts_data *ts,
				     u16 cmd, u8 *inbuf, u8 *outbuf)
{
	int error;
	u8 buf[64];

	buf[0] = cmd;
	error = ilitek_i2c_write_and_read(ts, buf, 1, 5, outbuf, 8);
	if (error)
		return error;

	ts->screen_min_x = get_unaligned_le16(outbuf);
	ts->screen_min_y = get_unaligned_le16(outbuf + 2);
	ts->screen_max_x = get_unaligned_le16(outbuf + 4);
	ts->screen_max_y = get_unaligned_le16(outbuf + 6);

	return 0;
}

static int api_protocol_get_tp_res(struct ilitek_ts_data *ts,
				   u16 cmd, u8 *inbuf, u8 *outbuf)
{
	int error;
	u8 buf[64];

	buf[0] = cmd;
	error = ilitek_i2c_write_and_read(ts, buf, 1, 5, outbuf, 15);
	if (error)
		return error;

	ts->max_tp = outbuf[8];
	if (ts->max_tp > ILITEK_SUPPORT_MAX_POINT) {
		dev_err(&ts->client->dev, "Invalid MAX_TP:%d from FW\n",
			ts->max_tp);
		return -EINVAL;
	}

	return 0;
}

static int api_protocol_get_ic_mode(struct ilitek_ts_data *ts,
				    u16 cmd, u8 *inbuf, u8 *outbuf)
{
	int error;
	u8 buf[64];

	buf[0] = cmd;
	error = ilitek_i2c_write_and_read(ts, buf, 1, 5, outbuf, 2);
	if (error)
		return error;

	ts->ic_mode = outbuf[0];
	return 0;
}

static int api_protocol_set_ic_sleep(struct ilitek_ts_data *ts,
				     u16 cmd, u8 *inbuf, u8 *outbuf)
{
	u8 buf[64];

	buf[0] = cmd;
	return ilitek_i2c_write_and_read(ts, buf, 1, 0, NULL, 0);
}

static int api_protocol_set_ic_wake(struct ilitek_ts_data *ts,
				    u16 cmd, u8 *inbuf, u8 *outbuf)
{
	u8 buf[64];

	buf[0] = cmd;
	return ilitek_i2c_write_and_read(ts, buf, 1, 0, NULL, 0);
}

static const struct ilitek_protocol_map ptl_func_map[] = {
	/* common cmds */
	[GET_PTL_VER] = {
		ILITEK_TP_CMD_GET_PRL_VER, "GET_PTL_VER",
		api_protocol_get_ptl_ver
	},
	[GET_FW_VER] = {
		ILITEK_TP_CMD_GET_FW_VER, "GET_FW_VER",
		api_protocol_get_fw_ver
	},
	[GET_SCRN_RES] = {
		ILITEK_TP_CMD_GET_SCRN_RES, "GET_SCRN_RES",
		api_protocol_get_scrn_res
	},
	[GET_TP_RES] = {
		ILITEK_TP_CMD_GET_TP_RES, "GET_TP_RES",
		api_protocol_get_tp_res
	},
	[GET_IC_MODE] = {
		ILITEK_TP_CMD_GET_IC_MODE, "GET_IC_MODE",
			   api_protocol_get_ic_mode
	},
	[GET_MCU_VER] = {
		ILITEK_TP_CMD_GET_MCU_VER, "GET_MOD_VER",
			   api_protocol_get_mcu_ver
	},
	[SET_IC_SLEEP] = {
		ILITEK_TP_CMD_SET_IC_SLEEP, "SET_IC_SLEEP",
		api_protocol_set_ic_sleep
	},
	[SET_IC_WAKE] = {
		ILITEK_TP_CMD_SET_IC_WAKE, "SET_IC_WAKE",
		api_protocol_set_ic_wake
	},
};

/* Probe APIs */
static void ilitek_reset(struct ilitek_ts_data *ts, int delay)
{
	if (ts->reset_gpio) {
		gpiod_set_value(ts->reset_gpio, 1);
		mdelay(10);
		gpiod_set_value(ts->reset_gpio, 0);
		mdelay(delay);
	}
}

static int ilitek_protocol_init(struct ilitek_ts_data *ts)
{
	int error;
	u8 outbuf[64];

	ts->ptl_cb_func = ptl_func_map;
	ts->reset_time = 600;

	error = api_protocol_set_cmd(ts, GET_PTL_VER, NULL, outbuf);
	if (error)
		return error;

	/* Protocol v3 is not support currently */
	if (ts->ptl.ver_major == 0x3 ||
	    ts->ptl.ver == BL_V1_6 ||
	    ts->ptl.ver == BL_V1_7)
		return -EINVAL;

	return 0;
}

static int ilitek_read_tp_info(struct ilitek_ts_data *ts, bool boot)
{
	u8 outbuf[256];
	int error;

	error = api_protocol_set_cmd(ts, GET_PTL_VER, NULL, outbuf);
	if (error)
		return error;

	error = api_protocol_set_cmd(ts, GET_MCU_VER, NULL, outbuf);
	if (error)
		return error;

	error = api_protocol_set_cmd(ts, GET_FW_VER, NULL, outbuf);
	if (error)
		return error;

	if (boot) {
		error = api_protocol_set_cmd(ts, GET_SCRN_RES, NULL,
					     outbuf);
		if (error)
			return error;
	}

	error = api_protocol_set_cmd(ts, GET_TP_RES, NULL, outbuf);
	if (error)
		return error;

	error = api_protocol_set_cmd(ts, GET_IC_MODE, NULL, outbuf);
	if (error)
		return error;

	return 0;
}

static int ilitek_input_dev_init(struct device *dev, struct ilitek_ts_data *ts)
{
	int error;
	struct input_dev *input;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	ts->input_dev = input;
	input->name = ILITEK_TS_NAME;
	input->id.bustype = BUS_I2C;

	__set_bit(INPUT_PROP_DIRECT, input->propbit);

	input_set_abs_params(input, ABS_MT_POSITION_X,
			     ts->screen_min_x, ts->screen_max_x, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y,
			     ts->screen_min_y, ts->screen_max_y, 0, 0);

	touchscreen_parse_properties(input, true, &ts->prop);

	error = input_mt_init_slots(input, ts->max_tp,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error) {
		dev_err(dev, "initialize MT slots failed, err:%d\n", error);
		return error;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(dev, "register input device failed, err:%d\n", error);
		return error;
	}

	return 0;
}

static irqreturn_t ilitek_i2c_isr(int irq, void *dev_id)
{
	struct ilitek_ts_data *ts = dev_id;
	int error;

	error = ilitek_process_and_report_v6(ts);
	if (error < 0) {
		dev_err(&ts->client->dev, "[%s] err:%d\n", __func__, error);
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static ssize_t firmware_version_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ilitek_ts_data *ts = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE,
			 "fw version: [%02X%02X.%02X%02X.%02X%02X.%02X%02X]\n",
			 ts->firmware_ver[0], ts->firmware_ver[1],
			 ts->firmware_ver[2], ts->firmware_ver[3],
			 ts->firmware_ver[4], ts->firmware_ver[5],
			 ts->firmware_ver[6], ts->firmware_ver[7]);
}
static DEVICE_ATTR_RO(firmware_version);

static ssize_t product_id_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ilitek_ts_data *ts = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "product id: [%04X], module: [%s]\n",
			 ts->mcu_ver, ts->product_id);
}
static DEVICE_ATTR_RO(product_id);

static struct attribute *ilitek_sysfs_attrs[] = {
	&dev_attr_firmware_version.attr,
	&dev_attr_product_id.attr,
	NULL
};

static struct attribute_group ilitek_attrs_group = {
	.attrs = ilitek_sysfs_attrs,
};

static int ilitek_ts_i2c_probe(struct i2c_client *client)
{
	struct ilitek_ts_data *ts;
	struct device *dev = &client->dev;
	int error;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "i2c check functionality failed\n");
		return -ENXIO;
	}

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = client;
	i2c_set_clientdata(client, ts);

	ts->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ts->reset_gpio)) {
		error = PTR_ERR(ts->reset_gpio);
		dev_err(dev, "request gpiod failed: %d", error);
		return error;
	}

	ilitek_reset(ts, 1000);

	error = ilitek_protocol_init(ts);
	if (error) {
		dev_err(dev, "protocol init failed: %d", error);
		return error;
	}

	error = ilitek_read_tp_info(ts, true);
	if (error) {
		dev_err(dev, "read tp info failed: %d", error);
		return error;
	}

	error = ilitek_input_dev_init(dev, ts);
	if (error) {
		dev_err(dev, "input dev init failed: %d", error);
		return error;
	}

	error = devm_request_threaded_irq(dev, ts->client->irq,
					  NULL, ilitek_i2c_isr, IRQF_ONESHOT,
					  "ilitek_touch_irq", ts);
	if (error) {
		dev_err(dev, "request threaded irq failed: %d\n", error);
		return error;
	}

	error = devm_device_add_group(dev, &ilitek_attrs_group);
	if (error) {
		dev_err(dev, "sysfs create group failed: %d\n", error);
		return error;
	}

	return 0;
}

static int ilitek_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ilitek_ts_data *ts = i2c_get_clientdata(client);
	int error;

	disable_irq(client->irq);

	if (!device_may_wakeup(dev)) {
		error = api_protocol_set_cmd(ts, SET_IC_SLEEP, NULL, NULL);
		if (error)
			return error;
	}

	return 0;
}

static int ilitek_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ilitek_ts_data *ts = i2c_get_clientdata(client);
	int error;

	if (!device_may_wakeup(dev)) {
		error = api_protocol_set_cmd(ts, SET_IC_WAKE, NULL, NULL);
		if (error)
			return error;

		ilitek_reset(ts, ts->reset_time);
	}

	enable_irq(client->irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(ilitek_pm_ops, ilitek_suspend, ilitek_resume);

static const struct i2c_device_id ilitek_ts_i2c_id[] = {
	{ ILITEK_TS_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, ilitek_ts_i2c_id);

#ifdef CONFIG_ACPI
static const struct acpi_device_id ilitekts_acpi_id[] = {
	{ "ILTK0001", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, ilitekts_acpi_id);
#endif

#ifdef CONFIG_OF
static const struct of_device_id ilitek_ts_i2c_match[] = {
	{.compatible = "ilitek,ili2130",},
	{.compatible = "ilitek,ili2131",},
	{.compatible = "ilitek,ili2132",},
	{.compatible = "ilitek,ili2316",},
	{.compatible = "ilitek,ili2322",},
	{.compatible = "ilitek,ili2323",},
	{.compatible = "ilitek,ili2326",},
	{.compatible = "ilitek,ili2520",},
	{.compatible = "ilitek,ili2521",},
	{ },
};
MODULE_DEVICE_TABLE(of, ilitek_ts_i2c_match);
#endif

static struct i2c_driver ilitek_ts_i2c_driver = {
	.driver = {
		.name = ILITEK_TS_NAME,
		.pm = pm_sleep_ptr(&ilitek_pm_ops),
		.of_match_table = of_match_ptr(ilitek_ts_i2c_match),
		.acpi_match_table = ACPI_PTR(ilitekts_acpi_id),
	},
	.probe_new = ilitek_ts_i2c_probe,
	.id_table = ilitek_ts_i2c_id,
};
module_i2c_driver(ilitek_ts_i2c_driver);

MODULE_AUTHOR("ILITEK");
MODULE_DESCRIPTION("ILITEK I2C Touchscreen Driver");
MODULE_LICENSE("GPL");

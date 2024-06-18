// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Driver for Hynitron cstxxx Touchscreen
 *
 *  Copyright (c) 2022 Chris Morgan <macromorgan@hotmail.com>
 *
 *  This code is based on hynitron_core.c authored by Hynitron.
 *  Note that no datasheet was available, so much of these registers
 *  are undocumented. This is essentially a cleaned-up version of the
 *  vendor driver with support removed for hardware I cannot test and
 *  device-specific functions replated with generic functions wherever
 *  possible.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <asm/unaligned.h>

/* Per chip data */
struct hynitron_ts_chip_data {
	unsigned int max_touch_num;
	u32 ic_chkcode;
	int (*firmware_info)(struct i2c_client *client);
	int (*bootloader_enter)(struct i2c_client *client);
	int (*init_input)(struct i2c_client *client);
	void (*report_touch)(struct i2c_client *client);
};

/* Data generic to all (supported and non-supported) controllers. */
struct hynitron_ts_data {
	const struct hynitron_ts_chip_data *chip;
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct touchscreen_properties prop;
	struct gpio_desc *reset_gpio;
};

/*
 * Since I have no datasheet, these values are guessed and/or assumed
 * based on observation and testing.
 */
#define CST3XX_FIRMWARE_INFO_START_CMD		0x01d1
#define CST3XX_FIRMWARE_INFO_END_CMD		0x09d1
#define CST3XX_FIRMWARE_CHK_CODE_REG		0xfcd1
#define CST3XX_FIRMWARE_VERSION_REG		0x08d2
#define CST3XX_FIRMWARE_VER_INVALID_VAL		0xa5a5a5a5

#define CST3XX_BOOTLDR_PROG_CMD			0xaa01a0
#define CST3XX_BOOTLDR_PROG_CHK_REG		0x02a0
#define CST3XX_BOOTLDR_CHK_VAL			0xac

#define CST3XX_TOUCH_DATA_PART_REG		0x00d0
#define CST3XX_TOUCH_DATA_FULL_REG		0x07d0
#define CST3XX_TOUCH_DATA_CHK_VAL		0xab
#define CST3XX_TOUCH_DATA_TOUCH_VAL		0x03
#define CST3XX_TOUCH_DATA_STOP_CMD		0xab00d0
#define CST3XX_TOUCH_COUNT_MASK			GENMASK(6, 0)


/*
 * Hard coded reset delay value of 20ms not IC dependent in
 * vendor driver.
 */
static void hyn_reset_proc(struct i2c_client *client, int delay)
{
	struct hynitron_ts_data *ts_data = i2c_get_clientdata(client);

	gpiod_set_value_cansleep(ts_data->reset_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(ts_data->reset_gpio, 0);
	if (delay)
		fsleep(delay * 1000);
}

static irqreturn_t hyn_interrupt_handler(int irq, void *dev_id)
{
	struct i2c_client *client = dev_id;
	struct hynitron_ts_data *ts_data = i2c_get_clientdata(client);

	ts_data->chip->report_touch(client);

	return IRQ_HANDLED;
}

/*
 * The vendor driver would retry twice before failing to read or write
 * to the i2c device.
 */

static int cst3xx_i2c_write(struct i2c_client *client,
			    unsigned char *buf, int len)
{
	int ret;
	int retries = 0;

	while (retries < 2) {
		ret = i2c_master_send(client, buf, len);
		if (ret == len)
			return 0;
		if (ret <= 0)
			retries++;
		else
			break;
	}

	return ret < 0 ? ret : -EIO;
}

static int cst3xx_i2c_read_register(struct i2c_client *client, u16 reg,
				    u8 *val, u16 len)
{
	__le16 buf = cpu_to_le16(reg);
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 2,
			.buf = (u8 *)&buf,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = val,
		}
	};
	int err;
	int ret;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret == ARRAY_SIZE(msgs))
		return 0;

	err = ret < 0 ? ret : -EIO;
	dev_err(&client->dev, "Error reading %d bytes from 0x%04x: %d (%d)\n",
		len, reg, err, ret);

	return err;
}

static int cst3xx_firmware_info(struct i2c_client *client)
{
	struct hynitron_ts_data *ts_data = i2c_get_clientdata(client);
	int err;
	u32 tmp;
	unsigned char buf[4];

	/*
	 * Tests suggest this command needed to read firmware regs.
	 */
	put_unaligned_le16(CST3XX_FIRMWARE_INFO_START_CMD, buf);
	err = cst3xx_i2c_write(client, buf, 2);
	if (err)
		return err;

	usleep_range(10000, 11000);

	/*
	 * Read register for check-code to determine if device detected
	 * correctly.
	 */
	err = cst3xx_i2c_read_register(client, CST3XX_FIRMWARE_CHK_CODE_REG,
				       buf, 4);
	if (err)
		return err;

	tmp = get_unaligned_le32(buf);
	if ((tmp & 0xffff0000) != ts_data->chip->ic_chkcode) {
		dev_err(&client->dev, "%s ic mismatch, chkcode is %u\n",
			__func__, tmp);
		return -ENODEV;
	}

	usleep_range(10000, 11000);

	/* Read firmware version and test if firmware missing. */
	err = cst3xx_i2c_read_register(client, CST3XX_FIRMWARE_VERSION_REG,
				       buf, 4);
	if (err)
		return err;

	tmp = get_unaligned_le32(buf);
	if (tmp == CST3XX_FIRMWARE_VER_INVALID_VAL) {
		dev_err(&client->dev, "Device firmware missing\n");
		return -ENODEV;
	}

	/*
	 * Tests suggest cmd required to exit reading firmware regs.
	 */
	put_unaligned_le16(CST3XX_FIRMWARE_INFO_END_CMD, buf);
	err = cst3xx_i2c_write(client, buf, 2);
	if (err)
		return err;

	usleep_range(5000, 6000);

	return 0;
}

static int cst3xx_bootloader_enter(struct i2c_client *client)
{
	int err;
	u8 retry;
	u32 tmp = 0;
	unsigned char buf[3];

	for (retry = 0; retry < 5; retry++) {
		hyn_reset_proc(client, (7 + retry));
		/* set cmd to enter program mode */
		put_unaligned_le24(CST3XX_BOOTLDR_PROG_CMD, buf);
		err = cst3xx_i2c_write(client, buf, 3);
		if (err)
			continue;

		usleep_range(2000, 2500);

		/* check whether in program mode */
		err = cst3xx_i2c_read_register(client,
					       CST3XX_BOOTLDR_PROG_CHK_REG,
					       buf, 1);
		if (err)
			continue;

		tmp = get_unaligned(buf);
		if (tmp == CST3XX_BOOTLDR_CHK_VAL)
			break;
	}

	if (tmp != CST3XX_BOOTLDR_CHK_VAL) {
		dev_err(&client->dev, "%s unable to enter bootloader mode\n",
			__func__);
		return -ENODEV;
	}

	hyn_reset_proc(client, 40);

	return 0;
}

static void cst3xx_report_contact(struct hynitron_ts_data *ts_data,
				  u8 id, unsigned int x, unsigned int y, u8 w)
{
	input_mt_slot(ts_data->input_dev, id);
	input_mt_report_slot_state(ts_data->input_dev, MT_TOOL_FINGER, 1);
	touchscreen_report_pos(ts_data->input_dev, &ts_data->prop, x, y, true);
	input_report_abs(ts_data->input_dev, ABS_MT_TOUCH_MAJOR, w);
}

static int cst3xx_finish_touch_read(struct i2c_client *client)
{
	unsigned char buf[3];
	int err;

	put_unaligned_le24(CST3XX_TOUCH_DATA_STOP_CMD, buf);
	err = cst3xx_i2c_write(client, buf, 3);
	if (err) {
		dev_err(&client->dev,
			"send read touch info ending failed: %d\n", err);
		return err;
	}

	return 0;
}

/*
 * Handle events from IRQ. Note that for cst3xx it appears that IRQ
 * fires continuously while touched, otherwise once every 1500ms
 * when not touched (assume touchscreen waking up periodically).
 * Note buffer is sized for 5 fingers, if more needed buffer must
 * be increased. The buffer contains 5 bytes for each touch point,
 * a touch count byte, a check byte, and then a second check byte after
 * all other touch points.
 *
 * For example 1 touch would look like this:
 * touch1[5]:touch_count[1]:chk_byte[1]
 *
 * 3 touches would look like this:
 * touch1[5]:touch_count[1]:chk_byte[1]:touch2[5]:touch3[5]:chk_byte[1]
 */
static void cst3xx_touch_report(struct i2c_client *client)
{
	struct hynitron_ts_data *ts_data = i2c_get_clientdata(client);
	u8 buf[28];
	u8 finger_id, sw, w;
	unsigned int x, y;
	unsigned int touch_cnt, end_byte;
	unsigned int idx = 0;
	unsigned int i;
	int err;

	/* Read and validate the first bits of input data. */
	err = cst3xx_i2c_read_register(client, CST3XX_TOUCH_DATA_PART_REG,
				       buf, 28);
	if (err ||
	    buf[6] != CST3XX_TOUCH_DATA_CHK_VAL ||
	    buf[0] == CST3XX_TOUCH_DATA_CHK_VAL) {
		dev_err(&client->dev, "cst3xx touch read failure\n");
		return;
	}

	/* Report to the device we're done reading the touch data. */
	err = cst3xx_finish_touch_read(client);
	if (err)
		return;

	touch_cnt = buf[5] & CST3XX_TOUCH_COUNT_MASK;
	/*
	 * Check the check bit of the last touch slot. The check bit is
	 * always present after touch point 1 for valid data, and then
	 * appears as the last byte after all other touch data.
	 */
	if (touch_cnt > 1) {
		end_byte = touch_cnt * 5 + 2;
		if (buf[end_byte] != CST3XX_TOUCH_DATA_CHK_VAL) {
			dev_err(&client->dev, "cst3xx touch read failure\n");
			return;
		}
	}

	/* Parse through the buffer to capture touch data. */
	for (i = 0; i < touch_cnt; i++) {
		x = ((buf[idx + 1] << 4) | ((buf[idx + 3] >> 4) & 0x0f));
		y = ((buf[idx + 2] << 4) | (buf[idx + 3] & 0x0f));
		w = (buf[idx + 4] >> 3);
		sw = (buf[idx] & 0x0f) >> 1;
		finger_id = (buf[idx] >> 4) & 0x0f;

		/* Sanity check we don't have more fingers than we expect */
		if (ts_data->chip->max_touch_num < finger_id) {
			dev_err(&client->dev, "cst3xx touch read failure\n");
			break;
		}

		/* sw value of 0 means no touch, 0x03 means touch */
		if (sw == CST3XX_TOUCH_DATA_TOUCH_VAL)
			cst3xx_report_contact(ts_data, finger_id, x, y, w);

		idx += 5;

		/* Skip the 2 bytes between point 1 and point 2 */
		if (i == 0)
			idx += 2;
	}

	input_mt_sync_frame(ts_data->input_dev);
	input_sync(ts_data->input_dev);
}

static int cst3xx_input_dev_int(struct i2c_client *client)
{
	struct hynitron_ts_data *ts_data = i2c_get_clientdata(client);
	int err;

	ts_data->input_dev = devm_input_allocate_device(&client->dev);
	if (!ts_data->input_dev) {
		dev_err(&client->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	ts_data->input_dev->name = "Hynitron cst3xx Touchscreen";
	ts_data->input_dev->phys = "input/ts";
	ts_data->input_dev->id.bustype = BUS_I2C;

	input_set_drvdata(ts_data->input_dev, ts_data);

	input_set_capability(ts_data->input_dev, EV_ABS, ABS_MT_POSITION_X);
	input_set_capability(ts_data->input_dev, EV_ABS, ABS_MT_POSITION_Y);
	input_set_abs_params(ts_data->input_dev, ABS_MT_TOUCH_MAJOR,
			     0, 255, 0, 0);

	touchscreen_parse_properties(ts_data->input_dev, true, &ts_data->prop);

	if (!ts_data->prop.max_x || !ts_data->prop.max_y) {
		dev_err(&client->dev,
			"Invalid x/y (%d, %d), using defaults\n",
			ts_data->prop.max_x, ts_data->prop.max_y);
		ts_data->prop.max_x = 1152;
		ts_data->prop.max_y = 1920;
		input_abs_set_max(ts_data->input_dev,
				  ABS_MT_POSITION_X, ts_data->prop.max_x);
		input_abs_set_max(ts_data->input_dev,
				  ABS_MT_POSITION_Y, ts_data->prop.max_y);
	}

	err = input_mt_init_slots(ts_data->input_dev,
				  ts_data->chip->max_touch_num,
				  INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (err) {
		dev_err(&client->dev,
			"Failed to initialize input slots: %d\n", err);
		return err;
	}

	err = input_register_device(ts_data->input_dev);
	if (err) {
		dev_err(&client->dev,
			"Input device registration failed: %d\n", err);
		return err;
	}

	return 0;
}

static int hyn_probe(struct i2c_client *client)
{
	struct hynitron_ts_data *ts_data;
	int err;

	ts_data = devm_kzalloc(&client->dev, sizeof(*ts_data), GFP_KERNEL);
	if (!ts_data)
		return -ENOMEM;

	ts_data->client = client;
	i2c_set_clientdata(client, ts_data);

	ts_data->chip = device_get_match_data(&client->dev);
	if (!ts_data->chip)
		return -EINVAL;

	ts_data->reset_gpio = devm_gpiod_get(&client->dev,
					     "reset", GPIOD_OUT_LOW);
	err = PTR_ERR_OR_ZERO(ts_data->reset_gpio);
	if (err) {
		dev_err(&client->dev, "request reset gpio failed: %d\n", err);
		return err;
	}

	hyn_reset_proc(client, 60);

	err = ts_data->chip->bootloader_enter(client);
	if (err < 0)
		return err;

	err = ts_data->chip->init_input(client);
	if (err < 0)
		return err;

	err = ts_data->chip->firmware_info(client);
	if (err < 0)
		return err;

	err = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, hyn_interrupt_handler,
					IRQF_ONESHOT,
					"Hynitron Touch Int", client);
	if (err) {
		dev_err(&client->dev, "failed to request IRQ: %d\n", err);
		return err;
	}

	return 0;
}

static const struct hynitron_ts_chip_data cst3xx_data = {
	.max_touch_num		= 5,
	.ic_chkcode		= 0xcaca0000,
	.firmware_info		= &cst3xx_firmware_info,
	.bootloader_enter	= &cst3xx_bootloader_enter,
	.init_input		= &cst3xx_input_dev_int,
	.report_touch		= &cst3xx_touch_report,
};

static const struct i2c_device_id hyn_tpd_id[] = {
	{ .name = "hynitron_ts", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i2c, hyn_tpd_id);

static const struct of_device_id hyn_dt_match[] = {
	{ .compatible = "hynitron,cst340", .data = &cst3xx_data },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, hyn_dt_match);

static struct i2c_driver hynitron_i2c_driver = {
	.driver = {
		.name = "Hynitron-TS",
		.of_match_table = hyn_dt_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = hyn_tpd_id,
	.probe = hyn_probe,
};

module_i2c_driver(hynitron_i2c_driver);

MODULE_AUTHOR("Chris Morgan");
MODULE_DESCRIPTION("Hynitron Touchscreen Driver");
MODULE_LICENSE("GPL");

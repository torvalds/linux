// SPDX-License-Identifier: GPL-2.0-only
/*
 * Touch Screen driver for SiS 9200 family I2C Touch panels
 *
 * Copyright (C) 2015 SiS, Inc.
 * Copyright (C) 2016 Nextfour Group
 */

#include <linux/crc-itu-t.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#define SIS_I2C_NAME		"sis_i2c_ts"

/*
 * The I2C packet format:
 * le16		byte count
 * u8		Report ID
 * <contact data - variable length>
 * u8		Number of contacts
 * le16		Scan Time (optional)
 * le16		CRC
 *
 * One touch point information consists of 6+ bytes, the order is:
 * u8		contact state
 * u8		finger id
 * le16		x axis
 * le16		y axis
 * u8		contact width (optional)
 * u8		contact height (optional)
 * u8		pressure (optional)
 *
 * Maximum amount of data transmitted in one shot is 64 bytes, if controller
 * needs to report more contacts than fit in one packet it will send true
 * number of contacts in first packet and 0 as number of contacts in second
 * packet.
 */

#define SIS_MAX_PACKET_SIZE		64

#define SIS_PKT_LEN_OFFSET		0
#define SIS_PKT_REPORT_OFFSET		2 /* Report ID/type */
#define SIS_PKT_CONTACT_OFFSET		3 /* First contact */

#define SIS_SCAN_TIME_LEN		2

/* Supported report types */
#define SIS_ALL_IN_ONE_PACKAGE		0x10
#define SIS_PKT_IS_TOUCH(x)		(((x) & 0x0f) == 0x01)
#define SIS_PKT_IS_HIDI2C(x)		(((x) & 0x0f) == 0x06)

/* Contact properties within report */
#define SIS_PKT_HAS_AREA(x)		((x) & BIT(4))
#define SIS_PKT_HAS_PRESSURE(x)		((x) & BIT(5))
#define SIS_PKT_HAS_SCANTIME(x)		((x) & BIT(6))

/* Contact size */
#define SIS_BASE_LEN_PER_CONTACT	6
#define SIS_AREA_LEN_PER_CONTACT	2
#define SIS_PRESSURE_LEN_PER_CONTACT	1

/* Offsets within contact data */
#define SIS_CONTACT_STATUS_OFFSET	0
#define SIS_CONTACT_ID_OFFSET		1 /* Contact ID */
#define SIS_CONTACT_X_OFFSET		2
#define SIS_CONTACT_Y_OFFSET		4
#define SIS_CONTACT_WIDTH_OFFSET	6
#define SIS_CONTACT_HEIGHT_OFFSET	7
#define SIS_CONTACT_PRESSURE_OFFSET(id)	(SIS_PKT_HAS_AREA(id) ? 8 : 6)

/* Individual contact state */
#define SIS_STATUS_UP			0x0
#define SIS_STATUS_DOWN			0x3

/* Touchscreen parameters */
#define SIS_MAX_FINGERS			10
#define SIS_MAX_X			4095
#define SIS_MAX_Y			4095
#define SIS_MAX_PRESSURE		255

/* Resolution diagonal */
#define SIS_AREA_LENGTH_LONGER		5792
/*((SIS_MAX_X^2) + (SIS_MAX_Y^2))^0.5*/
#define SIS_AREA_LENGTH_SHORT		5792
#define SIS_AREA_UNIT			(5792 / 32)

struct sis_ts_data {
	struct i2c_client *client;
	struct input_dev *input;

	struct gpio_desc *attn_gpio;
	struct gpio_desc *reset_gpio;

	u8 packet[SIS_MAX_PACKET_SIZE];
};

static int sis_read_packet(struct i2c_client *client, u8 *buf,
			   unsigned int *num_contacts,
			   unsigned int *contact_size)
{
	int count_idx;
	int ret;
	u16 len;
	u16 crc, pkg_crc;
	u8 report_id;

	ret = i2c_master_recv(client, buf, SIS_MAX_PACKET_SIZE);
	if (ret <= 0)
		return -EIO;

	len = get_unaligned_le16(&buf[SIS_PKT_LEN_OFFSET]);
	if (len > SIS_MAX_PACKET_SIZE) {
		dev_err(&client->dev,
			"%s: invalid packet length (%d vs %d)\n",
			__func__, len, SIS_MAX_PACKET_SIZE);
		return -E2BIG;
	}

	if (len < 10)
		return -EINVAL;

	report_id = buf[SIS_PKT_REPORT_OFFSET];
	count_idx  = len - 1;
	*contact_size = SIS_BASE_LEN_PER_CONTACT;

	if (report_id != SIS_ALL_IN_ONE_PACKAGE) {
		if (SIS_PKT_IS_TOUCH(report_id)) {
			/*
			 * Calculate CRC ignoring packet length
			 * in the beginning and CRC transmitted
			 * at the end of the packet.
			 */
			crc = crc_itu_t(0, buf + 2, len - 2 - 2);
			pkg_crc = get_unaligned_le16(&buf[len - 2]);

			if (crc != pkg_crc) {
				dev_err(&client->dev,
					"%s: CRC Error (%d vs %d)\n",
					__func__, crc, pkg_crc);
				return -EINVAL;
			}

			count_idx -= 2;

		} else if (!SIS_PKT_IS_HIDI2C(report_id)) {
			dev_err(&client->dev,
				"%s: invalid packet ID %#02x\n",
				__func__, report_id);
			return -EINVAL;
		}

		if (SIS_PKT_HAS_SCANTIME(report_id))
			count_idx -= SIS_SCAN_TIME_LEN;

		if (SIS_PKT_HAS_AREA(report_id))
			*contact_size += SIS_AREA_LEN_PER_CONTACT;
		if (SIS_PKT_HAS_PRESSURE(report_id))
			*contact_size += SIS_PRESSURE_LEN_PER_CONTACT;
	}

	*num_contacts = buf[count_idx];
	return 0;
}

static int sis_ts_report_contact(struct sis_ts_data *ts, const u8 *data, u8 id)
{
	struct input_dev *input = ts->input;
	int slot;
	u8 status = data[SIS_CONTACT_STATUS_OFFSET];
	u8 pressure;
	u8 height, width;
	u16 x, y;

	if (status != SIS_STATUS_DOWN && status != SIS_STATUS_UP) {
		dev_err(&ts->client->dev, "Unexpected touch status: %#02x\n",
			data[SIS_CONTACT_STATUS_OFFSET]);
		return -EINVAL;
	}

	slot = input_mt_get_slot_by_key(input, data[SIS_CONTACT_ID_OFFSET]);
	if (slot < 0)
		return -ENOENT;

	input_mt_slot(input, slot);
	input_mt_report_slot_state(input, MT_TOOL_FINGER,
				   status == SIS_STATUS_DOWN);

	if (status == SIS_STATUS_DOWN) {
		pressure = height = width = 1;
		if (id != SIS_ALL_IN_ONE_PACKAGE) {
			if (SIS_PKT_HAS_AREA(id)) {
				width = data[SIS_CONTACT_WIDTH_OFFSET];
				height = data[SIS_CONTACT_HEIGHT_OFFSET];
			}

			if (SIS_PKT_HAS_PRESSURE(id))
				pressure =
					data[SIS_CONTACT_PRESSURE_OFFSET(id)];
		}

		x = get_unaligned_le16(&data[SIS_CONTACT_X_OFFSET]);
		y = get_unaligned_le16(&data[SIS_CONTACT_Y_OFFSET]);

		input_report_abs(input, ABS_MT_TOUCH_MAJOR,
				 width * SIS_AREA_UNIT);
		input_report_abs(input, ABS_MT_TOUCH_MINOR,
				 height * SIS_AREA_UNIT);
		input_report_abs(input, ABS_MT_PRESSURE, pressure);
		input_report_abs(input, ABS_MT_POSITION_X, x);
		input_report_abs(input, ABS_MT_POSITION_Y, y);
	}

	return 0;
}

static void sis_ts_handle_packet(struct sis_ts_data *ts)
{
	const u8 *contact;
	unsigned int num_to_report = 0;
	unsigned int num_contacts;
	unsigned int num_reported;
	unsigned int contact_size;
	int error;
	u8 report_id;

	do {
		error = sis_read_packet(ts->client, ts->packet,
					&num_contacts, &contact_size);
		if (error)
			break;

		if (num_to_report == 0) {
			num_to_report = num_contacts;
		} else if (num_contacts != 0) {
			dev_err(&ts->client->dev,
				"%s: nonzero (%d) point count in tail packet\n",
				__func__, num_contacts);
			break;
		}

		report_id = ts->packet[SIS_PKT_REPORT_OFFSET];
		contact = &ts->packet[SIS_PKT_CONTACT_OFFSET];
		num_reported = 0;

		while (num_to_report > 0) {
			error = sis_ts_report_contact(ts, contact, report_id);
			if (error)
				break;

			contact += contact_size;
			num_to_report--;
			num_reported++;

			if (report_id != SIS_ALL_IN_ONE_PACKAGE &&
			    num_reported >= 5) {
				/*
				 * The remainder of contacts is sent
				 * in the 2nd packet.
				 */
				break;
			}
		}
	} while (num_to_report > 0);

	input_mt_sync_frame(ts->input);
	input_sync(ts->input);
}

static irqreturn_t sis_ts_irq_handler(int irq, void *dev_id)
{
	struct sis_ts_data *ts = dev_id;

	do {
		sis_ts_handle_packet(ts);
	} while (ts->attn_gpio && gpiod_get_value_cansleep(ts->attn_gpio));

	return IRQ_HANDLED;
}

static void sis_ts_reset(struct sis_ts_data *ts)
{
	if (ts->reset_gpio) {
		/* Get out of reset */
		usleep_range(1000, 2000);
		gpiod_set_value(ts->reset_gpio, 1);
		usleep_range(1000, 2000);
		gpiod_set_value(ts->reset_gpio, 0);
		msleep(100);
	}
}

static int sis_ts_probe(struct i2c_client *client)
{
	struct sis_ts_data *ts;
	struct input_dev *input;
	int error;

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = client;

	ts->attn_gpio = devm_gpiod_get_optional(&client->dev,
						"attn", GPIOD_IN);
	if (IS_ERR(ts->attn_gpio))
		return dev_err_probe(&client->dev, PTR_ERR(ts->attn_gpio),
				     "Failed to get attention GPIO\n");

	ts->reset_gpio = devm_gpiod_get_optional(&client->dev,
						 "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ts->reset_gpio))
		return dev_err_probe(&client->dev, PTR_ERR(ts->reset_gpio),
				     "Failed to get reset GPIO\n");

	sis_ts_reset(ts);

	ts->input = input = devm_input_allocate_device(&client->dev);
	if (!input) {
		dev_err(&client->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	input->name = "SiS Touchscreen";
	input->id.bustype = BUS_I2C;

	input_set_abs_params(input, ABS_MT_POSITION_X, 0, SIS_MAX_X, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, SIS_MAX_Y, 0, 0);
	input_set_abs_params(input, ABS_MT_PRESSURE, 0, SIS_MAX_PRESSURE, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR,
			     0, SIS_AREA_LENGTH_LONGER, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MINOR,
			     0, SIS_AREA_LENGTH_SHORT, 0, 0);

	error = input_mt_init_slots(input, SIS_MAX_FINGERS, INPUT_MT_DIRECT);
	if (error) {
		dev_err(&client->dev,
			"Failed to initialize MT slots: %d\n", error);
		return error;
	}

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, sis_ts_irq_handler,
					  IRQF_ONESHOT,
					  client->name, ts);
	if (error) {
		dev_err(&client->dev, "Failed to request IRQ: %d\n", error);
		return error;
	}

	error = input_register_device(ts->input);
	if (error) {
		dev_err(&client->dev,
			"Failed to register input device: %d\n", error);
		return error;
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id sis_ts_dt_ids[] = {
	{ .compatible = "sis,9200-ts" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sis_ts_dt_ids);
#endif

static const struct i2c_device_id sis_ts_id[] = {
	{ SIS_I2C_NAME,	0 },
	{ "9200-ts",	0 },
	{ /* sentinel */  }
};
MODULE_DEVICE_TABLE(i2c, sis_ts_id);

static struct i2c_driver sis_ts_driver = {
	.driver = {
		.name	= SIS_I2C_NAME,
		.of_match_table = of_match_ptr(sis_ts_dt_ids),
	},
	.probe		= sis_ts_probe,
	.id_table	= sis_ts_id,
};
module_i2c_driver(sis_ts_driver);

MODULE_DESCRIPTION("SiS 9200 Family Touchscreen Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Mika Penttilä <mika.penttila@nextfour.com>");

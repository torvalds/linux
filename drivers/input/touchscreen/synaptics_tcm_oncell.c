// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Driver for Synaptics TCM Oncell Touchscreens
 *
 *  Copyright (c) 2024 Frieder Hannenheim <frieder.hannenheim@proton.me>
 *  Copyright (c) 2024 Caleb Connolly <caleb@postmarketos.org>
 */

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/touchscreen.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/unaligned.h>
#include <linux/delay.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

/*
 * The TCM oncell interface uses a command byte, which may be followed by additional
 * data. The packet format is defined in the tcm_cmd struct.
 *
 * The following list only defines commands that are used in this driver (and their
 * counterparts for context). Vendor reference implementations can be found at
 * https://github.com/LineageOS/android_kernel_oneplus_sm8250/tree/ee0a7ee1939ffd53000e42051caf8f0800defb27/drivers/input/touchscreen/synaptics_tcm
 */

/*
 * Request information about the chip. We don't send this command explicitly as
 * the controller automatically sends this information when starting up.
 */
#define TCM_IDENTIFY				0x02

/* Enable/disable reporting touch inputs */
#define TCM_ENABLE_REPORT			0x05
#define TCM_DISABLE_REPORT			0x06

/*
 * After powering on, we send this to exit the bootloader mode and run the main
 * firmware.
 */
#define TCM_RUN_APPLICATION_FIRMWARE		0x14

/*
 * Reports information about the vendor provided application firmware. This is
 * also used to determine when the firmware has finished booting.
 */
#define TCM_GET_APPLICATION_INFO		0x20

#define MODE_APPLICATION			0x01

#define APP_STATUS_OK				0x00
#define APP_STATUS_BOOTING			0x01
#define APP_STATUS_UPDATING			0x02

/* status codes */
#define REPORT_IDLE				0x00
#define REPORT_OK				0x01
#define REPORT_BUSY				0x02
#define REPORT_CONTINUED_READ			0x03
#define REPORT_RECEIVE_BUFFER_OVERFLOW		0x0c
#define REPORT_PREVIOUS_COMMAND_PENDING		0x0d
#define REPORT_NOT_IMPLEMENTED			0x0e
#define REPORT_ERROR				0x0f

/* report types */
#define REPORT_IDENTIFY				0x10
#define REPORT_TOUCH				0x11
#define REPORT_DELTA				0x12
#define REPORT_RAW				0x13
#define REPORT_DEBUG				0x14
#define REPORT_LOG				0x1d
#define REPORT_TOUCH_HOLD			0x20
#define REPORT_INVALID				0xff

struct tcm_message_header {
	u8 marker;
	u8 code;
	__le16 length;
} __packed;

struct tcm_cmd {
	u8 cmd;
	__le16 length;
	u8 data[];
};

struct tcm_identification {
	struct tcm_message_header header;
	u8 version;
	u8 mode;
	char part_number[16];
	u8 build_id[4];
	u8 max_write_size[2];
} __packed;

struct tcm_app_info {
	struct tcm_message_header header;
	u8 version[2];
	__le16 status;
	u8 static_config_size[2];
	u8 dynamic_config_size[2];
	u8 app_config_start_write_block[2];
	u8 app_config_size[2];
	u8 max_touch_report_config_size[2];
	u8 max_touch_report_payload_size[2];
	char customer_config_id[16];
	__le16 max_x;
	__le16 max_y;
	u8 max_objects[2];
	u8 num_of_buttons[2];
	u8 num_of_image_rows[2];
	u8 num_of_image_cols[2];
	u8 has_hybrid_data[2];
} __packed;

struct tcm_data {
	struct i2c_client *client;
	struct regmap *regmap;
	struct input_dev *input;
	struct gpio_desc *reset_gpio;
	struct completion response;
	struct touchscreen_properties props;
	struct regulator_bulk_data supplies[2];

	/* annoying state */
	u16 buf_size;
	char buf[256];
};

static int tcm_send_cmd(struct tcm_data *tcm, struct tcm_cmd *cmd)
{
	struct i2c_client *client = tcm->client;
	struct i2c_msg msg;
	int ret;

	dev_dbg(&client->dev, "sending command %#x\n", cmd->cmd);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 1 + cmd->length;
	msg.buf = (u8 *)cmd;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret == 1)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int tcm_send_cmd_noargs(struct tcm_data *tcm, u8 cmd)
{
	struct tcm_cmd c = {
		.cmd = cmd,
		.length = 0,
	};

	return tcm_send_cmd(tcm, &c);
}

static int tcm_recv_report(struct tcm_data *tcm,
			   void *buf, size_t length)
{
	struct i2c_client *client = tcm->client;
	struct i2c_msg msg;
	int ret;

	msg.addr = client->addr;
	msg.flags = I2C_M_RD;
	msg.len = length;
	msg.buf = buf;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret == 1)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int tcm_read_message(struct tcm_data *tcm, u8 cmd, void *buf, size_t length)
{
	int ret;

	reinit_completion(&tcm->response);
	ret = tcm_send_cmd_noargs(tcm, cmd);
	if (ret)
		return ret;

	ret = wait_for_completion_timeout(&tcm->response, msecs_to_jiffies(1000));
	if (ret == 0)
		return -ETIMEDOUT;

	if (buf) {
		if (length > tcm->buf_size) {
			dev_warn(&tcm->client->dev, "expected %zu bytes, got %u\n",
				 length, tcm->buf_size);
		}
		length = min(tcm->buf_size, length);
		memcpy(buf, tcm->buf, length);
	}

	return 0;
}

static void tcm_power_off(void *data)
{
	struct tcm_data *tcm = data;

	disable_irq(tcm->client->irq);
	regulator_bulk_disable(ARRAY_SIZE(tcm->supplies), tcm->supplies);
}

static int tcm_input_open(struct input_dev *dev)
{
	struct tcm_data *tcm = input_get_drvdata(dev);

	return i2c_smbus_write_byte(tcm->client, TCM_ENABLE_REPORT);
}

static void tcm_input_close(struct input_dev *dev)
{
	struct tcm_data *tcm = input_get_drvdata(dev);
	int ret;

	ret = i2c_smbus_write_byte(tcm->client, TCM_DISABLE_REPORT);
	if (ret)
		dev_err(&tcm->client->dev, "failed to turn off sensing\n");
}

/*
 * The default report config looks like this:
 *
 * a5 01 80 00 11 08 1e 08 0f 01 04 01 06 04 07 04
 * 08 0c 09 0c 0a 08 0b 08 0c 08 0d 10 0e 10 03 00
 * 00 00
 *
 * a5 01 80 00 - HEADER + length
 *
 * 11 08 - TOUCH_FRAME_RATE (8 bits)
 * 30 08 - UNKNOWN (8 bits)
 * 0f 01 - TOUCH_0D_BUTTONS_STATE (1 bit)
 * 04 01 - TOUCH_PAD_TO_NEXT_BYTE (7 bits - padding)
 * 06 04 - TOUCH_OBJECT_N_INDEX (4 bits)
 * 07 04 - TOUCH_OBJECT_N_CLASSIFICATION (4 bits)
 * 08 0c - TOUCH_OBJECT_N_X_POSITION (12 bits)
 * 09 0c - TOUCH_OBJECT_N_Y_POSITION (12 bits)
 * 0a 08 - TOUCH_OBJECT_N_Z (8 bits)
 * 0b 08 - TOUCH_OBJECT_N_X_WIDTH (8 bits)
 * 0c 08 - TOUCH_OBJECT_N_Y_WIDTH (8 bits)
 * 0d 10 - TOUCH_OBJECT_N_TX_POSITION_TIXELS (16 bits) ??
 * 0e 10 - TOUCH_OBJECT_N_RX_POSITION_TIXELS (16 bits) ??
 * 03 00 - TOUCH_FOREACH_END (0 bits)
 * 00 00 - TOUCH_END (0 bits)
 *
 * Since we only support this report config, we just hardcode the format below.
 * To support additional report configs, we would need to parse the config and
 * use it to parse the reports dynamically.
 */

struct tcm_report_point {
	u8 unknown;
	u8 buttons;
	__le32 point; /* idx : 4, class : 4, x : 12, y : 12 */
	// u8 idx : 4;
	// u8 classification : 4;
	// u16 x : 12;
	// u16 y : 12;
	u8 z;
	u8 width_x;
	u8 width_y;
	u8 tx;
	u8 rx;
} __packed;

static int tcm_handle_touch_report(struct tcm_data *tcm, const char *buf, size_t len)
{
	const struct tcm_report_point *point;
	/* If the input device hasn't registered yet then we can't do anything */
	if (!tcm->input)
		return 0;

	buf += sizeof(struct tcm_message_header);
	len -= sizeof(struct tcm_message_header);

	dev_dbg(&tcm->client->dev, "touch report len %zu\n", len);
	if ((len - 3) % sizeof(*point))
		dev_err(&tcm->client->dev, "invalid touch report length\n");

	buf++; /* Skip the FPS report */

	/* We don't need to report releases because we have INPUT_MT_DROP_UNUSED */
	for (int i = 0; i < (len - 1) / sizeof(*point); i++) {
		u8 major_width, minor_width;
		u16 idx, x, y;
		u32 _point;

		point = (struct tcm_report_point *)buf;
		_point = le32_to_cpu(point->point);

		minor_width = point->width_x;
		major_width = point->width_y;

		if (minor_width > major_width)
			swap(major_width, minor_width);

		idx = _point & 0xf;
		x = (_point >> 8) & 0xfff;
		y = (_point >> 20) & 0xfff;

		dev_dbg(&tcm->client->dev, "touch report: idx %u x %u y %u\n",
			idx, x, y);

		input_mt_slot(tcm->input, idx);
		input_mt_report_slot_state(tcm->input, MT_TOOL_FINGER, true);

		touchscreen_report_pos(tcm->input, &tcm->props, x, y, true);

		input_report_abs(tcm->input, ABS_MT_TOUCH_MAJOR, major_width);
		input_report_abs(tcm->input, ABS_MT_TOUCH_MINOR, minor_width);
		input_report_abs(tcm->input, ABS_MT_PRESSURE, point->z);

		buf += sizeof(*point);
	}

	input_mt_sync_frame(tcm->input);
	input_sync(tcm->input);

	return 0;
}

static irqreturn_t tcm_report_irq(int irq, void *data)
{
	struct tcm_data *tcm = data;
	struct tcm_message_header *header;
	char buf[256];
	u16 len;
	int ret;

	header = (struct tcm_message_header *)buf;
	ret = tcm_recv_report(tcm, buf, sizeof(buf));
	if (ret) {
		dev_err(&tcm->client->dev, "failed to read report: %d\n", ret);
		return IRQ_HANDLED;
	}

	switch (header->code) {
	case REPORT_OK:
	case REPORT_IDENTIFY:
	case REPORT_TOUCH:
	case REPORT_DELTA:
	case REPORT_RAW:
	case REPORT_DEBUG:
	case REPORT_TOUCH_HOLD:
		break;
	default:
		dev_dbg(&tcm->client->dev, "Ignoring report %#x\n", header->code);
		return IRQ_HANDLED;
	}

	len = le32_to_cpu(header->length);

	dev_dbg(&tcm->client->dev, "report %#x len %u\n", header->code, len);
	print_hex_dump_bytes("report: ", DUMP_PREFIX_OFFSET, buf,
			     min(sizeof(buf), len + sizeof(*header)));

	if (len > sizeof(buf) - sizeof(*header)) {
		dev_err(&tcm->client->dev, "report too long\n");
		return IRQ_HANDLED;
	}

	/* Check if this is a read response or an indication. For indications
	 * (user touched the screen) we just parse the report directly.
	 */
	if (completion_done(&tcm->response) && header->code == REPORT_TOUCH) {
		tcm_handle_touch_report(tcm, buf, len + sizeof(*header));
		return IRQ_HANDLED;
	}

	tcm->buf_size = len + sizeof(*header);
	memcpy(tcm->buf, buf, len + sizeof(*header));
	complete(&tcm->response);

	return IRQ_HANDLED;
}

static int tcm_hw_init(struct tcm_data *tcm, u16 *max_x, u16 *max_y)
{
	int ret;
	struct tcm_identification id = { 0 };
	struct tcm_app_info app_info = { 0 };
	u16 status;

	/*
	 * Tell the firmware to start up. After starting it sends an IDENTIFY report, which
	 * we treat like a response to this message even though it's technically a new report.
	 */
	ret = tcm_read_message(tcm, TCM_RUN_APPLICATION_FIRMWARE, &id, sizeof(id));
	if (ret) {
		dev_err(&tcm->client->dev, "failed to identify device: %d\n", ret);
		return ret;
	}

	dev_dbg(&tcm->client->dev, "Synaptics TCM %s v%d mode %d\n",
		id.part_number, id.version, id.mode);
	if (id.mode != MODE_APPLICATION) {
		/* We don't support firmware updates or anything else */
		dev_err(&tcm->client->dev, "Device is not in application mode\n");
		return -ENODEV;
	}

	do {
		msleep(20);
		ret = tcm_read_message(tcm, TCM_GET_APPLICATION_INFO, &app_info, sizeof(app_info));
		if (ret) {
			dev_err(&tcm->client->dev, "failed to get application info: %d\n", ret);
			return ret;
		}
		status = le16_to_cpu(app_info.status);
	} while (status == APP_STATUS_BOOTING || status == APP_STATUS_UPDATING);

	dev_dbg(&tcm->client->dev, "Application firmware v%d.%d (customer '%s') status %d\n",
		 app_info.version[0], app_info.version[1], app_info.customer_config_id,
		 status);

	*max_x = le16_to_cpu(app_info.max_x);
	*max_y = le16_to_cpu(app_info.max_y);

	return 0;
}

static int tcm_power_on(struct tcm_data *tcm)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(tcm->supplies),
				    tcm->supplies);
	if (ret)
		return ret;

	gpiod_set_value_cansleep(tcm->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(tcm->reset_gpio, 0);
	usleep_range(80000, 81000);

	return 0;
}

static int tcm_probe(struct i2c_client *client)
{
	struct tcm_data *tcm;
	u16 max_x, max_y;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
						I2C_FUNC_SMBUS_BYTE_DATA |
						I2C_FUNC_SMBUS_I2C_BLOCK))
		return -ENODEV;

	tcm = devm_kzalloc(&client->dev, sizeof(struct tcm_data), GFP_KERNEL);
	if (!tcm)
		return -ENOMEM;

	i2c_set_clientdata(client, tcm);
	tcm->client = client;

	init_completion(&tcm->response);

	tcm->supplies[0].supply = "vdd";
	tcm->supplies[1].supply = "vcc";
	ret = devm_regulator_bulk_get(&client->dev, ARRAY_SIZE(tcm->supplies),
				      tcm->supplies);
	if (ret)
		return ret;

	tcm->reset_gpio = devm_gpiod_get(&client->dev, "reset", GPIOD_OUT_LOW);

	ret = devm_add_action_or_reset(&client->dev, tcm_power_off,
				       tcm);
	if (ret)
		return ret;

	ret = tcm_power_on(tcm);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					tcm_report_irq,
					IRQF_ONESHOT,
					"synaptics_tcm_report", tcm);
	if (ret < 0)
		return ret;

	ret = tcm_hw_init(tcm, &max_x, &max_y);
	if (ret) {
		dev_err(&client->dev, "failed to initialize hardware\n");
		return ret;
	}

	tcm->input = devm_input_allocate_device(&client->dev);
	if (!tcm->input)
		return -ENOMEM;

	tcm->input->name = "Synaptics TCM Oncell Touchscreen";
	tcm->input->id.bustype = BUS_I2C;
	tcm->input->open = tcm_input_open;
	tcm->input->close = tcm_input_close;

	input_set_abs_params(tcm->input, ABS_MT_POSITION_X, 0, max_x, 0, 0);
	input_set_abs_params(tcm->input, ABS_MT_POSITION_Y, 0, max_y, 0, 0);
	input_set_abs_params(tcm->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(tcm->input, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);
	input_set_abs_params(tcm->input, ABS_MT_PRESSURE, 0, 255, 0, 0);

	touchscreen_parse_properties(tcm->input, true, &tcm->props);

	ret = input_mt_init_slots(tcm->input, 10, INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (ret)
		return ret;

	input_set_drvdata(tcm->input, tcm);

	ret = input_register_device(tcm->input);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id syna_driver_ids[] = {
	{
		.compatible = "syna,s3908",
	},
	{}
};
MODULE_DEVICE_TABLE(of, syna_driver_ids);

static const struct i2c_device_id syna_i2c_ids[] = {
	{ "synaptics-tcm", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, syna_i2c_ids);

static struct i2c_driver syna_i2c_driver = {
	.probe		= tcm_probe,
	.id_table	= syna_i2c_ids,
	.driver		= {
	.name		= "synaptics-tcm",
	.of_match_table	= syna_driver_ids,
	},
};

module_i2c_driver(syna_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Frieder Hannenheim <frieder.hannenheim@proton.me>");
MODULE_AUTHOR("Caleb Connolly <caleb@postmarketos.org>");
MODULE_DESCRIPTION("A driver for Synaptics TCM Oncell Touchpanels");


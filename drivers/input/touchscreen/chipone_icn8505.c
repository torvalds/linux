// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for ChipOne icn8505 i2c touchscreen controller
 *
 * Copyright (c) 2015-2018 Red Hat Inc.
 *
 * Red Hat authors:
 * Hans de Goede <hdegoede@redhat.com>
 */

#include <asm/unaligned.h>
#include <linux/acpi.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/module.h>

/* Normal operation mode defines */
#define ICN8505_REG_ADDR_WIDTH		16

#define ICN8505_REG_POWER		0x0004
#define ICN8505_REG_TOUCHDATA		0x1000
#define ICN8505_REG_CONFIGDATA		0x8000

/* ICN8505_REG_POWER commands */
#define ICN8505_POWER_ACTIVE		0x00
#define ICN8505_POWER_MONITOR		0x01
#define ICN8505_POWER_HIBERNATE		0x02
/*
 * The Android driver uses these to turn on/off the charger filter, but the
 * filter is way too aggressive making e.g. onscreen keyboards unusable.
 */
#define ICN8505_POWER_ENA_CHARGER_MODE	0x55
#define ICN8505_POWER_DIS_CHARGER_MODE	0x66

#define ICN8505_MAX_TOUCHES		10

/* Programming mode defines */
#define ICN8505_PROG_I2C_ADDR		0x30
#define ICN8505_PROG_REG_ADDR_WIDTH	24

#define MAX_FW_UPLOAD_TRIES		3

struct icn8505_touch {
	u8 slot;
	u8 x[2];
	u8 y[2];
	u8 pressure;	/* Seems more like finger width then pressure really */
	u8 event;
/* The difference between 2 and 3 is unclear */
#define ICN8505_EVENT_NO_DATA	1 /* No finger seen yet since wakeup */
#define ICN8505_EVENT_UPDATE1	2 /* New or updated coordinates */
#define ICN8505_EVENT_UPDATE2	3 /* New or updated coordinates */
#define ICN8505_EVENT_END	4 /* Finger lifted */
} __packed;

struct icn8505_touch_data {
	u8 softbutton;
	u8 touch_count;
	struct icn8505_touch touches[ICN8505_MAX_TOUCHES];
} __packed;

struct icn8505_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct gpio_desc *wake_gpio;
	struct touchscreen_properties prop;
	char firmware_name[32];
};

static int icn8505_read_xfer(struct i2c_client *client, u16 i2c_addr,
			     int reg_addr, int reg_addr_width,
			     void *data, int len, bool silent)
{
	u8 buf[3];
	int i, ret;
	struct i2c_msg msg[2] = {
		{
			.addr = i2c_addr,
			.buf = buf,
			.len = reg_addr_width / 8,
		},
		{
			.addr = i2c_addr,
			.flags = I2C_M_RD,
			.buf = data,
			.len = len,
		}
	};

	for (i = 0; i < (reg_addr_width / 8); i++)
		buf[i] = (reg_addr >> (reg_addr_width - (i + 1) * 8)) & 0xff;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret != ARRAY_SIZE(msg)) {
		if (ret >= 0)
			ret = -EIO;
		if (!silent)
			dev_err(&client->dev,
				"Error reading addr %#x reg %#x: %d\n",
				i2c_addr, reg_addr, ret);
		return ret;
	}

	return 0;
}

static int icn8505_write_xfer(struct i2c_client *client, u16 i2c_addr,
			      int reg_addr, int reg_addr_width,
			      const void *data, int len, bool silent)
{
	u8 buf[3 + 32]; /* 3 bytes for 24 bit reg-addr + 32 bytes max len */
	int i, ret;
	struct i2c_msg msg = {
		.addr = i2c_addr,
		.buf = buf,
		.len = reg_addr_width / 8 + len,
	};

	if (WARN_ON(len > 32))
		return -EINVAL;

	for (i = 0; i < (reg_addr_width / 8); i++)
		buf[i] = (reg_addr >> (reg_addr_width - (i + 1) * 8)) & 0xff;

	memcpy(buf + reg_addr_width / 8, data, len);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret != 1) {
		if (ret >= 0)
			ret = -EIO;
		if (!silent)
			dev_err(&client->dev,
				"Error writing addr %#x reg %#x: %d\n",
				i2c_addr, reg_addr, ret);
		return ret;
	}

	return 0;
}

static int icn8505_read_data(struct icn8505_data *icn8505, int reg,
			     void *buf, int len)
{
	return icn8505_read_xfer(icn8505->client, icn8505->client->addr, reg,
				 ICN8505_REG_ADDR_WIDTH, buf, len, false);
}

static int icn8505_read_reg_silent(struct icn8505_data *icn8505, int reg)
{
	u8 buf;
	int error;

	error = icn8505_read_xfer(icn8505->client, icn8505->client->addr, reg,
				  ICN8505_REG_ADDR_WIDTH, &buf, 1, true);
	if (error)
		return error;

	return buf;
}

static int icn8505_write_reg(struct icn8505_data *icn8505, int reg, u8 val)
{
	return icn8505_write_xfer(icn8505->client, icn8505->client->addr, reg,
				  ICN8505_REG_ADDR_WIDTH, &val, 1, false);
}

static int icn8505_read_prog_data(struct icn8505_data *icn8505, int reg,
				  void *buf, int len)
{
	return icn8505_read_xfer(icn8505->client, ICN8505_PROG_I2C_ADDR, reg,
				 ICN8505_PROG_REG_ADDR_WIDTH, buf, len, false);
}

static int icn8505_write_prog_data(struct icn8505_data *icn8505, int reg,
				   const void *buf, int len)
{
	return icn8505_write_xfer(icn8505->client, ICN8505_PROG_I2C_ADDR, reg,
				  ICN8505_PROG_REG_ADDR_WIDTH, buf, len, false);
}

static int icn8505_write_prog_reg(struct icn8505_data *icn8505, int reg, u8 val)
{
	return icn8505_write_xfer(icn8505->client, ICN8505_PROG_I2C_ADDR, reg,
				  ICN8505_PROG_REG_ADDR_WIDTH, &val, 1, false);
}

/*
 * Note this function uses a number of magic register addresses and values,
 * there are deliberately no defines for these because the algorithm is taken
 * from the icn85xx Android driver and I do not want to make up possibly wrong
 * names for the addresses and/or values.
 */
static int icn8505_try_fw_upload(struct icn8505_data *icn8505,
				 const struct firmware *fw)
{
	struct device *dev = &icn8505->client->dev;
	size_t offset, count;
	int error;
	u8 buf[4];
	u32 crc;

	/* Put the controller in programming mode */
	error = icn8505_write_prog_reg(icn8505, 0xcc3355, 0x5a);
	if (error)
		return error;

	usleep_range(2000, 5000);

	error = icn8505_write_prog_reg(icn8505, 0x040400, 0x01);
	if (error)
		return error;

	usleep_range(2000, 5000);

	error = icn8505_read_prog_data(icn8505, 0x040002, buf, 1);
	if (error)
		return error;

	if (buf[0] != 0x85) {
		dev_err(dev, "Failed to enter programming mode\n");
		return -ENODEV;
	}

	usleep_range(1000, 5000);

	/* Enable CRC mode */
	error = icn8505_write_prog_reg(icn8505, 0x40028, 1);
	if (error)
		return error;

	/* Send the firmware to SRAM */
	for (offset = 0; offset < fw->size; offset += count) {
		count = min_t(size_t, fw->size - offset, 32);
		error = icn8505_write_prog_data(icn8505, offset,
					      fw->data + offset, count);
		if (error)
			return error;
	}

	/* Disable CRC mode */
	error = icn8505_write_prog_reg(icn8505, 0x40028, 0);
	if (error)
		return error;

	/* Get and check length and CRC */
	error = icn8505_read_prog_data(icn8505, 0x40034, buf, 2);
	if (error)
		return error;

	if (get_unaligned_le16(buf) != fw->size) {
		dev_warn(dev, "Length mismatch after uploading fw\n");
		return -EIO;
	}

	error = icn8505_read_prog_data(icn8505, 0x4002c, buf, 4);
	if (error)
		return error;

	crc = crc32_be(0, fw->data, fw->size);
	if (get_unaligned_le32(buf) != crc) {
		dev_warn(dev, "CRC mismatch after uploading fw\n");
		return -EIO;
	}

	/* Boot controller from SRAM */
	error = icn8505_write_prog_reg(icn8505, 0x40400, 0x03);
	if (error)
		return error;

	usleep_range(2000, 5000);
	return 0;
}

static int icn8505_upload_fw(struct icn8505_data *icn8505)
{
	struct device *dev = &icn8505->client->dev;
	const struct firmware *fw;
	int i, error;

	/*
	 * Always load the firmware, even if we don't need it at boot, we
	 * we may need it at resume. Having loaded it once will make the
	 * firmware class code cache it at suspend/resume.
	 */
	error = firmware_request_platform(&fw, icn8505->firmware_name, dev);
	if (error) {
		dev_err(dev, "Firmware request error %d\n", error);
		return error;
	}

	/* Check if the controller is not already up and running */
	if (icn8505_read_reg_silent(icn8505, 0x000a) == 0x85)
		goto success;

	for (i = 1; i <= MAX_FW_UPLOAD_TRIES; i++) {
		error = icn8505_try_fw_upload(icn8505, fw);
		if (!error)
			goto success;

		dev_err(dev, "Failed to upload firmware: %d (attempt %d/%d)\n",
			error, i, MAX_FW_UPLOAD_TRIES);
		usleep_range(2000, 5000);
	}

success:
	release_firmware(fw);
	return error;
}

static bool icn8505_touch_active(u8 event)
{
	return event == ICN8505_EVENT_UPDATE1 ||
	       event == ICN8505_EVENT_UPDATE2;
}

static irqreturn_t icn8505_irq(int irq, void *dev_id)
{
	struct icn8505_data *icn8505 = dev_id;
	struct device *dev = &icn8505->client->dev;
	struct icn8505_touch_data touch_data;
	int i, error;

	error = icn8505_read_data(icn8505, ICN8505_REG_TOUCHDATA,
				  &touch_data, sizeof(touch_data));
	if (error) {
		dev_err(dev, "Error reading touch data: %d\n", error);
		return IRQ_HANDLED;
	}

	if (touch_data.touch_count > ICN8505_MAX_TOUCHES) {
		dev_warn(dev, "Too many touches %d > %d\n",
			 touch_data.touch_count, ICN8505_MAX_TOUCHES);
		touch_data.touch_count = ICN8505_MAX_TOUCHES;
	}

	for (i = 0; i < touch_data.touch_count; i++) {
		struct icn8505_touch *touch = &touch_data.touches[i];
		bool act = icn8505_touch_active(touch->event);

		input_mt_slot(icn8505->input, touch->slot);
		input_mt_report_slot_state(icn8505->input, MT_TOOL_FINGER, act);
		if (!act)
			continue;

		touchscreen_report_pos(icn8505->input, &icn8505->prop,
				       get_unaligned_le16(touch->x),
				       get_unaligned_le16(touch->y),
				       true);
	}

	input_mt_sync_frame(icn8505->input);
	input_report_key(icn8505->input, KEY_LEFTMETA,
			 touch_data.softbutton == 1);
	input_sync(icn8505->input);

	return IRQ_HANDLED;
}

static int icn8505_probe_acpi(struct icn8505_data *icn8505, struct device *dev)
{
	const char *subsys;
	int error;

	subsys = acpi_get_subsystem_id(ACPI_HANDLE(dev));
	error = PTR_ERR_OR_ZERO(subsys);
	if (error == -ENODATA)
		subsys = "unknown";
	else if (error)
		return error;

	snprintf(icn8505->firmware_name, sizeof(icn8505->firmware_name),
		 "chipone/icn8505-%s.fw", subsys);

	kfree_const(subsys);
	return 0;
}

static int icn8505_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct icn8505_data *icn8505;
	struct input_dev *input;
	__le16 resolution[2];
	int error;

	if (!client->irq) {
		dev_err(dev, "No irq specified\n");
		return -EINVAL;
	}

	icn8505 = devm_kzalloc(dev, sizeof(*icn8505), GFP_KERNEL);
	if (!icn8505)
		return -ENOMEM;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	input->name = client->name;
	input->id.bustype = BUS_I2C;

	input_set_capability(input, EV_ABS, ABS_MT_POSITION_X);
	input_set_capability(input, EV_ABS, ABS_MT_POSITION_Y);
	input_set_capability(input, EV_KEY, KEY_LEFTMETA);

	icn8505->client = client;
	icn8505->input = input;
	input_set_drvdata(input, icn8505);

	error = icn8505_probe_acpi(icn8505, dev);
	if (error)
		return error;

	error = icn8505_upload_fw(icn8505);
	if (error)
		return error;

	error = icn8505_read_data(icn8505, ICN8505_REG_CONFIGDATA,
				resolution, sizeof(resolution));
	if (error) {
		dev_err(dev, "Error reading resolution: %d\n", error);
		return error;
	}

	input_set_abs_params(input, ABS_MT_POSITION_X, 0,
			     le16_to_cpu(resolution[0]) - 1, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0,
			     le16_to_cpu(resolution[1]) - 1, 0, 0);

	touchscreen_parse_properties(input, true, &icn8505->prop);
	if (!input_abs_get_max(input, ABS_MT_POSITION_X) ||
	    !input_abs_get_max(input, ABS_MT_POSITION_Y)) {
		dev_err(dev, "Error touchscreen-size-x and/or -y missing\n");
		return -EINVAL;
	}

	error = input_mt_init_slots(input, ICN8505_MAX_TOUCHES,
				  INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error)
		return error;

	error = devm_request_threaded_irq(dev, client->irq, NULL, icn8505_irq,
					IRQF_ONESHOT, client->name, icn8505);
	if (error) {
		dev_err(dev, "Error requesting irq: %d\n", error);
		return error;
	}

	error = input_register_device(input);
	if (error)
		return error;

	i2c_set_clientdata(client, icn8505);
	return 0;
}

static int icn8505_suspend(struct device *dev)
{
	struct icn8505_data *icn8505 = i2c_get_clientdata(to_i2c_client(dev));

	disable_irq(icn8505->client->irq);

	icn8505_write_reg(icn8505, ICN8505_REG_POWER, ICN8505_POWER_HIBERNATE);

	return 0;
}

static int icn8505_resume(struct device *dev)
{
	struct icn8505_data *icn8505 = i2c_get_clientdata(to_i2c_client(dev));
	int error;

	error = icn8505_upload_fw(icn8505);
	if (error)
		return error;

	enable_irq(icn8505->client->irq);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(icn8505_pm_ops, icn8505_suspend, icn8505_resume);

static const struct acpi_device_id icn8505_acpi_match[] = {
	{ "CHPN0001" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, icn8505_acpi_match);

static struct i2c_driver icn8505_driver = {
	.driver = {
		.name	= "chipone_icn8505",
		.pm	= pm_sleep_ptr(&icn8505_pm_ops),
		.acpi_match_table = icn8505_acpi_match,
	},
	.probe_new = icn8505_probe,
};

module_i2c_driver(icn8505_driver);

MODULE_DESCRIPTION("ChipOne icn8505 I2C Touchscreen Driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");

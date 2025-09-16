// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for Infineon TDA38640
 *
 * Copyright (c) 2023 9elements GmbH
 *
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regulator/driver.h>
#include "pmbus.h"

static const struct regulator_desc __maybe_unused tda38640_reg_desc[] = {
	PMBUS_REGULATOR_ONE_NODE("vout"),
};

struct tda38640_data {
	struct pmbus_driver_info info;
	u32 en_pin_lvl;
};

#define to_tda38640_data(x)  container_of(x, struct tda38640_data, info)

/*
 * Map PB_ON_OFF_CONFIG_POLARITY_HIGH to PB_OPERATION_CONTROL_ON.
 */
static int tda38640_read_byte_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct tda38640_data *data = to_tda38640_data(info);
	int ret, on_off_config, enabled;

	if (reg != PMBUS_OPERATION)
		return -ENODATA;

	ret = pmbus_read_byte_data(client, page, reg);
	if (ret < 0)
		return ret;

	on_off_config = pmbus_read_byte_data(client, page,
					     PMBUS_ON_OFF_CONFIG);
	if (on_off_config < 0)
		return on_off_config;

	enabled = !!(on_off_config & PB_ON_OFF_CONFIG_POLARITY_HIGH);

	enabled ^= data->en_pin_lvl;
	if (enabled)
		ret &= ~PB_OPERATION_CONTROL_ON;
	else
		ret |= PB_OPERATION_CONTROL_ON;

	return ret;
}

/*
 * Map PB_OPERATION_CONTROL_ON to PB_ON_OFF_CONFIG_POLARITY_HIGH.
 */
static int tda38640_write_byte_data(struct i2c_client *client, int page,
				    int reg, u8 byte)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct tda38640_data *data = to_tda38640_data(info);
	int enable, ret;

	if (reg != PMBUS_OPERATION)
		return -ENODATA;

	enable = !!(byte & PB_OPERATION_CONTROL_ON);

	byte &= ~PB_OPERATION_CONTROL_ON;
	ret = pmbus_write_byte_data(client, page, reg, byte);
	if (ret < 0)
		return ret;

	enable ^= data->en_pin_lvl;

	return pmbus_update_byte_data(client, page, PMBUS_ON_OFF_CONFIG,
				      PB_ON_OFF_CONFIG_POLARITY_HIGH,
				      enable ? 0 : PB_ON_OFF_CONFIG_POLARITY_HIGH);
}

static int svid_mode(struct i2c_client *client, struct tda38640_data *data)
{
	/* PMBUS_MFR_READ(0xD0) + MTP Address offset */
	u8 write_buf[] = {0xd0, 0x44, 0x00};
	u8 read_buf[2];
	int ret, svid;
	bool off, reg_en_pin_pol;

	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = write_buf,
			.len = sizeof(write_buf),
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.buf = read_buf,
			.len = sizeof(read_buf),
		}
	};

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret < 0) {
		dev_err(&client->dev, "i2c_transfer failed. %d", ret);
		return ret;
	}

	/*
	 * 0x44[15] determines PMBus Operating Mode
	 * If bit is set then it is SVID mode.
	 */
	svid = !!(read_buf[1] & BIT(7));

	/*
	 * Determine EN pin level for use in SVID mode.
	 * This is done with help of STATUS_BYTE bit 6(OFF) & ON_OFF_CONFIG bit 2(EN pin polarity).
	 */
	if (svid) {
		ret = i2c_smbus_read_byte_data(client, PMBUS_STATUS_BYTE);
		if (ret < 0)
			return ret;
		off = !!(ret & PB_STATUS_OFF);

		ret = i2c_smbus_read_byte_data(client, PMBUS_ON_OFF_CONFIG);
		if (ret < 0)
			return ret;
		reg_en_pin_pol = !!(ret & PB_ON_OFF_CONFIG_POLARITY_HIGH);
		data->en_pin_lvl = off ^ reg_en_pin_pol;
	}

	return svid;
}

static struct pmbus_driver_info tda38640_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_POWER] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT
	    | PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP
	    | PMBUS_HAVE_IIN
	    | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
	    | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT
	    | PMBUS_HAVE_POUT | PMBUS_HAVE_PIN,
#if IS_ENABLED(CONFIG_SENSORS_TDA38640_REGULATOR)
	.num_regulators = 1,
	.reg_desc = tda38640_reg_desc,
#endif
};

static int tda38640_probe(struct i2c_client *client)
{
	struct tda38640_data *data;
	int svid;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	memcpy(&data->info, &tda38640_info, sizeof(tda38640_info));

	if (IS_ENABLED(CONFIG_SENSORS_TDA38640_REGULATOR) &&
	    of_property_read_bool(client->dev.of_node, "infineon,en-pin-fixed-level")) {
		svid = svid_mode(client, data);
		if (svid < 0) {
			dev_err_probe(&client->dev, svid, "Could not determine operating mode.");
			return svid;
		}

		/*
		 * Apply ON_OFF_CONFIG workaround as enabling the regulator using the
		 * OPERATION register doesn't work in SVID mode.
		 *
		 * One should configure PMBUS_ON_OFF_CONFIG here, but
		 * PB_ON_OFF_CONFIG_POWERUP_CONTROL and PB_ON_OFF_CONFIG_EN_PIN_REQ
		 * are ignored by the device.
		 * Only PB_ON_OFF_CONFIG_POLARITY_HIGH has an effect.
		 */
		if (svid) {
			data->info.read_byte_data = tda38640_read_byte_data;
			data->info.write_byte_data = tda38640_write_byte_data;
		}
	}
	return pmbus_do_probe(client, &data->info);
}

static const struct i2c_device_id tda38640_id[] = {
	{"tda38640"},
	{}
};
MODULE_DEVICE_TABLE(i2c, tda38640_id);

static const struct of_device_id __maybe_unused tda38640_of_match[] = {
	{ .compatible = "infineon,tda38640"},
	{ },
};
MODULE_DEVICE_TABLE(of, tda38640_of_match);

/* This is the driver that will be inserted */
static struct i2c_driver tda38640_driver = {
	.driver = {
		.name = "tda38640",
		.of_match_table = of_match_ptr(tda38640_of_match),
	},
	.probe = tda38640_probe,
	.id_table = tda38640_id,
};

module_i2c_driver(tda38640_driver);

MODULE_AUTHOR("Patrick Rudolph <patrick.rudolph@9elements.com>");
MODULE_DESCRIPTION("PMBus driver for Infineon TDA38640");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");

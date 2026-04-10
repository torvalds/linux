// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for Sony APS-379 Power Supplies
 *
 * Copyright 2026 Allied Telesis Labs
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pmbus.h>
#include "pmbus.h"

/*
 * The VOUT format used by the chip is linear11, not linear16. Provide a hard
 * coded VOUT_MODE that says VOUT is in linear mode with a fixed exponent of
 * 2^-4.
 */
#define APS_379_VOUT_MODE ((u8)(-4 & 0x1f))

static int aps_379_read_byte_data(struct i2c_client *client, int page, int reg)
{
	switch (reg) {
	case PMBUS_VOUT_MODE:
		return APS_379_VOUT_MODE;
	default:
		return -ENODATA;
	}
}

/*
 * The APS-379 uses linear11 format instead of linear16. We've reported the exponent
 * via the PMBUS_VOUT_MODE so we just return the mantissa here.
 */
static int aps_379_read_vout(struct i2c_client *client)
{
	int ret;

	ret = pmbus_read_word_data(client, 0, 0xff, PMBUS_READ_VOUT);
	if (ret < 0)
		return ret;

	return clamp_val(sign_extend32(ret & 0x7ff, 10), 0, 0x3ff);
}

static int aps_379_read_word_data(struct i2c_client *client, int page, int phase, int reg)
{
	switch (reg) {
	case PMBUS_VOUT_UV_WARN_LIMIT:
	case PMBUS_VOUT_OV_WARN_LIMIT:
	case PMBUS_VOUT_UV_FAULT_LIMIT:
	case PMBUS_VOUT_OV_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
	case PMBUS_IOUT_UC_FAULT_LIMIT:
	case PMBUS_UT_WARN_LIMIT:
	case PMBUS_UT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_PIN_OP_WARN_LIMIT:
	case PMBUS_POUT_OP_WARN_LIMIT:
	case PMBUS_MFR_IIN_MAX:
	case PMBUS_MFR_PIN_MAX:
	case PMBUS_MFR_VOUT_MIN:
	case PMBUS_MFR_VOUT_MAX:
	case PMBUS_MFR_IOUT_MAX:
	case PMBUS_MFR_POUT_MAX:
	case PMBUS_MFR_MAX_TEMP_1:
		/* These commands return data but it is invalid/un-documented */
		return -ENXIO;
	case PMBUS_IOUT_OC_FAULT_LIMIT:
		/*
		 * The standard requires this to be a value in Amps but it's
		 * actually a percentage of the rated output (123A for
		 * 110-240Vac, 110A for 90-100Vac) which we don't know. Ignore
		 * it rather than guessing.
		 */
		return -ENXIO;
	case PMBUS_READ_VOUT:
		return aps_379_read_vout(client);
	default:
		return -ENODATA;
	}
}

static struct pmbus_driver_info aps_379_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_OUT] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_FAN] = linear,
	.func[0] = PMBUS_HAVE_VOUT |
		PMBUS_HAVE_IOUT |
		PMBUS_HAVE_PIN | PMBUS_HAVE_POUT |
		PMBUS_HAVE_TEMP |
		PMBUS_HAVE_FAN12,
	.read_byte_data = aps_379_read_byte_data,
	.read_word_data = aps_379_read_word_data,
};

static const struct i2c_device_id aps_379_id[] = {
	{ "aps-379", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, aps_379_id);

static int aps_379_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	u8 buf[I2C_SMBUS_BLOCK_MAX + 1] = { 0 };
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_BYTE_DATA
				     | I2C_FUNC_SMBUS_READ_WORD_DATA
				     | I2C_FUNC_SMBUS_READ_BLOCK_DATA))
		return -ENODEV;

	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_MODEL, buf);
	if (ret < 0) {
		dev_err(dev, "Failed to read Manufacturer Model\n");
		return ret;
	}

	if (strncasecmp(buf, aps_379_id[0].name, strlen(aps_379_id[0].name)) != 0) {
		buf[ret] = '\0';
		dev_err(dev, "Unsupported Manufacturer Model '%s'\n", buf);
		return -ENODEV;
	}

	return pmbus_do_probe(client, &aps_379_info);
}

static const struct of_device_id __maybe_unused aps_379_of_match[] = {
	{ .compatible = "sony,aps-379" },
	{},
};
MODULE_DEVICE_TABLE(of, aps_379_of_match);

static struct i2c_driver aps_379_driver = {
	.driver = {
		.name = "aps-379",
		.of_match_table = of_match_ptr(aps_379_of_match),
	},
	.probe = aps_379_probe,
	.id_table = aps_379_id,
};

module_i2c_driver(aps_379_driver);

MODULE_AUTHOR("Chris Packham");
MODULE_DESCRIPTION("PMBus driver for Sony APS-379");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");

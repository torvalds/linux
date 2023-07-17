// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for Texas Instruments TPS53679
 *
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Vadim Pasternak <vadimp@mellanox.com>
 */

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

enum chips {
	tps53647, tps53667, tps53676, tps53679, tps53681, tps53688
};

#define TPS53647_PAGE_NUM		1

#define TPS53676_USER_DATA_03		0xb3
#define TPS53676_MAX_PHASES		7

#define TPS53679_PROT_VR12_5MV		0x01 /* VR12.0 mode, 5-mV DAC */
#define TPS53679_PROT_VR12_5_10MV	0x02 /* VR12.5 mode, 10-mV DAC */
#define TPS53679_PROT_VR13_10MV		0x04 /* VR13.0 mode, 10-mV DAC */
#define TPS53679_PROT_IMVP8_5MV		0x05 /* IMVP8 mode, 5-mV DAC */
#define TPS53679_PROT_VR13_5MV		0x07 /* VR13.0 mode, 5-mV DAC */
#define TPS53679_PAGE_NUM		2

#define TPS53681_DEVICE_ID		0x81

#define TPS53681_PMBUS_REVISION		0x33

#define TPS53681_MFR_SPECIFIC_20	0xe4	/* Number of phases, per page */

static const struct i2c_device_id tps53679_id[];

static int tps53679_identify_mode(struct i2c_client *client,
				  struct pmbus_driver_info *info)
{
	u8 vout_params;
	int i, ret;

	for (i = 0; i < info->pages; i++) {
		/* Read the register with VOUT scaling value.*/
		ret = pmbus_read_byte_data(client, i, PMBUS_VOUT_MODE);
		if (ret < 0)
			return ret;

		vout_params = ret & GENMASK(4, 0);

		switch (vout_params) {
		case TPS53679_PROT_VR13_10MV:
		case TPS53679_PROT_VR12_5_10MV:
			info->vrm_version[i] = vr13;
			break;
		case TPS53679_PROT_VR13_5MV:
		case TPS53679_PROT_VR12_5MV:
		case TPS53679_PROT_IMVP8_5MV:
			info->vrm_version[i] = vr12;
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int tps53679_identify_phases(struct i2c_client *client,
				    struct pmbus_driver_info *info)
{
	int ret;

	/* On TPS53681, only channel A provides per-phase output current */
	ret = pmbus_read_byte_data(client, 0, TPS53681_MFR_SPECIFIC_20);
	if (ret < 0)
		return ret;
	info->phases[0] = (ret & 0x07) + 1;

	return 0;
}

static int tps53679_identify_chip(struct i2c_client *client,
				  u8 revision, u16 id)
{
	u8 buf[I2C_SMBUS_BLOCK_MAX];
	int ret;

	ret = pmbus_read_byte_data(client, 0, PMBUS_REVISION);
	if (ret < 0)
		return ret;
	if (ret != revision) {
		dev_err(&client->dev, "Unexpected PMBus revision 0x%x\n", ret);
		return -ENODEV;
	}

	ret = i2c_smbus_read_block_data(client, PMBUS_IC_DEVICE_ID, buf);
	if (ret < 0)
		return ret;
	if (ret != 1 || buf[0] != id) {
		dev_err(&client->dev, "Unexpected device ID 0x%x\n", buf[0]);
		return -ENODEV;
	}
	return 0;
}

/*
 * Common identification function for chips with multi-phase support.
 * Since those chips have special configuration registers, we want to have
 * some level of reassurance that we are really talking with the chip
 * being probed. Check PMBus revision and chip ID.
 */
static int tps53679_identify_multiphase(struct i2c_client *client,
					struct pmbus_driver_info *info,
					int pmbus_rev, int device_id)
{
	int ret;

	ret = tps53679_identify_chip(client, pmbus_rev, device_id);
	if (ret < 0)
		return ret;

	ret = tps53679_identify_mode(client, info);
	if (ret < 0)
		return ret;

	return tps53679_identify_phases(client, info);
}

static int tps53679_identify(struct i2c_client *client,
			     struct pmbus_driver_info *info)
{
	return tps53679_identify_mode(client, info);
}

static int tps53681_identify(struct i2c_client *client,
			     struct pmbus_driver_info *info)
{
	return tps53679_identify_multiphase(client, info,
					    TPS53681_PMBUS_REVISION,
					    TPS53681_DEVICE_ID);
}

static int tps53676_identify(struct i2c_client *client,
			     struct pmbus_driver_info *info)
{
	u8 buf[I2C_SMBUS_BLOCK_MAX];
	int phases_a = 0, phases_b = 0;
	int i, ret;

	ret = i2c_smbus_read_block_data(client, PMBUS_IC_DEVICE_ID, buf);
	if (ret < 0)
		return ret;
	if (strncmp("TI\x53\x67\x60", buf, 5)) {
		dev_err(&client->dev, "Unexpected device ID: %s\n", buf);
		return -ENODEV;
	}

	ret = i2c_smbus_read_block_data(client, TPS53676_USER_DATA_03, buf);
	if (ret < 0)
		return ret;
	if (ret != 24)
		return -EIO;
	for (i = 0; i < 2 * TPS53676_MAX_PHASES; i += 2) {
		if (buf[i + 1] & 0x80) {
			if (buf[i] & 0x08)
				phases_b++;
			else
				phases_a++;
		}
	}

	info->format[PSC_VOLTAGE_OUT] = linear;
	info->pages = 1;
	info->phases[0] = phases_a;
	if (phases_b > 0) {
		info->pages = 2;
		info->phases[1] = phases_b;
	}
	return 0;
}

static int tps53681_read_word_data(struct i2c_client *client, int page,
				   int phase, int reg)
{
	/*
	 * For reading the total output current (READ_IOUT) for all phases,
	 * the chip datasheet is a bit vague. It says "PHASE must be set to
	 * FFh to access all phases simultaneously. PHASE may also be set to
	 * 80h readack (!) the total phase current".
	 * Experiments show that the command does _not_ report the total
	 * current for all phases if the phase is set to 0xff. Instead, it
	 * appears to report the current of one of the phases. Override phase
	 * parameter with 0x80 when reading the total output current on page 0.
	 */
	if (reg == PMBUS_READ_IOUT && page == 0 && phase == 0xff)
		return pmbus_read_word_data(client, page, 0x80, reg);
	return -ENODATA;
}

static struct pmbus_driver_info tps53679_info = {
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = vid,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN | PMBUS_HAVE_PIN |
		PMBUS_HAVE_STATUS_INPUT |
		PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP |
		PMBUS_HAVE_POUT,
	.func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP |
		PMBUS_HAVE_POUT,
	.pfunc[0] = PMBUS_HAVE_IOUT,
	.pfunc[1] = PMBUS_HAVE_IOUT,
	.pfunc[2] = PMBUS_HAVE_IOUT,
	.pfunc[3] = PMBUS_HAVE_IOUT,
	.pfunc[4] = PMBUS_HAVE_IOUT,
	.pfunc[5] = PMBUS_HAVE_IOUT,
	.pfunc[6] = PMBUS_HAVE_IOUT,
};

static int tps53679_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct pmbus_driver_info *info;
	enum chips chip_id;

	if (dev->of_node)
		chip_id = (enum chips)of_device_get_match_data(dev);
	else
		chip_id = i2c_match_id(tps53679_id, client)->driver_data;

	info = devm_kmemdup(dev, &tps53679_info, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	switch (chip_id) {
	case tps53647:
	case tps53667:
		info->pages = TPS53647_PAGE_NUM;
		info->identify = tps53679_identify;
		break;
	case tps53676:
		info->identify = tps53676_identify;
		break;
	case tps53679:
	case tps53688:
		info->pages = TPS53679_PAGE_NUM;
		info->identify = tps53679_identify;
		break;
	case tps53681:
		info->pages = TPS53679_PAGE_NUM;
		info->phases[0] = 6;
		info->identify = tps53681_identify;
		info->read_word_data = tps53681_read_word_data;
		break;
	default:
		return -ENODEV;
	}

	return pmbus_do_probe(client, info);
}

static const struct i2c_device_id tps53679_id[] = {
	{"bmr474", tps53676},
	{"tps53647", tps53647},
	{"tps53667", tps53667},
	{"tps53676", tps53676},
	{"tps53679", tps53679},
	{"tps53681", tps53681},
	{"tps53688", tps53688},
	{}
};

MODULE_DEVICE_TABLE(i2c, tps53679_id);

static const struct of_device_id __maybe_unused tps53679_of_match[] = {
	{.compatible = "ti,tps53647", .data = (void *)tps53647},
	{.compatible = "ti,tps53667", .data = (void *)tps53667},
	{.compatible = "ti,tps53676", .data = (void *)tps53676},
	{.compatible = "ti,tps53679", .data = (void *)tps53679},
	{.compatible = "ti,tps53681", .data = (void *)tps53681},
	{.compatible = "ti,tps53688", .data = (void *)tps53688},
	{}
};
MODULE_DEVICE_TABLE(of, tps53679_of_match);

static struct i2c_driver tps53679_driver = {
	.driver = {
		.name = "tps53679",
		.of_match_table = of_match_ptr(tps53679_of_match),
	},
	.probe = tps53679_probe,
	.id_table = tps53679_id,
};

module_i2c_driver(tps53679_driver);

MODULE_AUTHOR("Vadim Pasternak <vadimp@mellanox.com>");
MODULE_DESCRIPTION("PMBus driver for Texas Instruments TPS53679");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);

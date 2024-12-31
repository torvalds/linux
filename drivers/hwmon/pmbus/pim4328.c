// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for PIM4006, PIM4328 and PIM4820
 *
 * Copyright (c) 2021 Flextronics International Sweden AB
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pmbus.h>
#include <linux/slab.h>
#include "pmbus.h"

enum chips { pim4006, pim4328, pim4820 };

struct pim4328_data {
	enum chips id;
	struct pmbus_driver_info info;
};

#define to_pim4328_data(x)  container_of(x, struct pim4328_data, info)

/* PIM4006 and PIM4328 */
#define PIM4328_MFR_READ_VINA		0xd3
#define PIM4328_MFR_READ_VINB		0xd4

/* PIM4006 */
#define PIM4328_MFR_READ_IINA		0xd6
#define PIM4328_MFR_READ_IINB		0xd7
#define PIM4328_MFR_FET_CHECKSTATUS	0xd9

/* PIM4328 */
#define PIM4328_MFR_STATUS_BITS		0xd5

/* PIM4820 */
#define PIM4328_MFR_READ_STATUS		0xd0

static const struct i2c_device_id pim4328_id[] = {
	{"bmr455", pim4328},
	{"pim4006", pim4006},
	{"pim4106", pim4006},
	{"pim4206", pim4006},
	{"pim4306", pim4006},
	{"pim4328", pim4328},
	{"pim4406", pim4006},
	{"pim4820", pim4820},
	{}
};
MODULE_DEVICE_TABLE(i2c, pim4328_id);

static int pim4328_read_word_data(struct i2c_client *client, int page,
				  int phase, int reg)
{
	int ret;

	if (page > 0)
		return -ENXIO;

	if (phase == 0xff)
		return -ENODATA;

	switch (reg) {
	case PMBUS_READ_VIN:
		ret = pmbus_read_word_data(client, page, phase,
					   phase == 0 ? PIM4328_MFR_READ_VINA
						      : PIM4328_MFR_READ_VINB);
		break;
	case PMBUS_READ_IIN:
		ret = pmbus_read_word_data(client, page, phase,
					   phase == 0 ? PIM4328_MFR_READ_IINA
						      : PIM4328_MFR_READ_IINB);
		break;
	default:
		ret = -ENODATA;
	}

	return ret;
}

static int pim4328_read_byte_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct pim4328_data *data = to_pim4328_data(info);
	int ret, status;

	if (page > 0)
		return -ENXIO;

	switch (reg) {
	case PMBUS_STATUS_BYTE:
		ret = pmbus_read_byte_data(client, page, PMBUS_STATUS_BYTE);
		if (ret < 0)
			return ret;
		if (data->id == pim4006) {
			status = pmbus_read_word_data(client, page, 0xff,
						      PIM4328_MFR_FET_CHECKSTATUS);
			if (status < 0)
				return status;
			if (status & 0x0630) /* Input UV */
				ret |= PB_STATUS_VIN_UV;
		} else if (data->id == pim4328) {
			status = pmbus_read_byte_data(client, page,
						      PIM4328_MFR_STATUS_BITS);
			if (status < 0)
				return status;
			if (status & 0x04) /* Input UV */
				ret |= PB_STATUS_VIN_UV;
			if (status & 0x40) /* Output UV */
				ret |= PB_STATUS_NONE_ABOVE;
		} else if (data->id == pim4820) {
			status = pmbus_read_byte_data(client, page,
						      PIM4328_MFR_READ_STATUS);
			if (status < 0)
				return status;
			if (status & 0x05) /* Input OV or OC */
				ret |= PB_STATUS_NONE_ABOVE;
			if (status & 0x1a) /* Input UV */
				ret |= PB_STATUS_VIN_UV;
			if (status & 0x40) /* OT */
				ret |= PB_STATUS_TEMPERATURE;
		}
		break;
	default:
		ret = -ENODATA;
	}

	return ret;
}

static int pim4328_probe(struct i2c_client *client)
{
	int status;
	u8 device_id[I2C_SMBUS_BLOCK_MAX + 1];
	const struct i2c_device_id *mid;
	struct pim4328_data *data;
	struct pmbus_driver_info *info;
	struct pmbus_platform_data *pdata;
	struct device *dev = &client->dev;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_BYTE_DATA
				     | I2C_FUNC_SMBUS_BLOCK_DATA))
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(struct pim4328_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	status = i2c_smbus_read_block_data(client, PMBUS_MFR_MODEL, device_id);
	if (status < 0) {
		dev_err(&client->dev, "Failed to read Manufacturer Model\n");
		return status;
	}
	for (mid = pim4328_id; mid->name[0]; mid++) {
		if (!strncasecmp(mid->name, device_id, strlen(mid->name)))
			break;
	}
	if (!mid->name[0]) {
		dev_err(&client->dev, "Unsupported device\n");
		return -ENODEV;
	}

	if (strcmp(client->name, mid->name))
		dev_notice(&client->dev,
			   "Device mismatch: Configured %s, detected %s\n",
			   client->name, mid->name);

	data->id = mid->driver_data;
	info = &data->info;
	info->pages = 1;
	info->read_byte_data = pim4328_read_byte_data;
	info->read_word_data = pim4328_read_word_data;

	pdata = devm_kzalloc(dev, sizeof(struct pmbus_platform_data),
			     GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	dev->platform_data = pdata;
	pdata->flags = PMBUS_NO_CAPABILITY | PMBUS_NO_WRITE_PROTECT;

	switch (data->id) {
	case pim4006:
		info->phases[0] = 2;
		info->func[0] = PMBUS_PHASE_VIRTUAL | PMBUS_HAVE_VIN
			| PMBUS_HAVE_TEMP | PMBUS_HAVE_IOUT;
		info->pfunc[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN;
		info->pfunc[1] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN;
		break;
	case pim4328:
		info->phases[0] = 2;
		info->func[0] = PMBUS_PHASE_VIRTUAL
			| PMBUS_HAVE_VCAP | PMBUS_HAVE_VIN
			| PMBUS_HAVE_TEMP | PMBUS_HAVE_IOUT;
		info->pfunc[0] = PMBUS_HAVE_VIN;
		info->pfunc[1] = PMBUS_HAVE_VIN;
		info->format[PSC_VOLTAGE_IN] = direct;
		info->format[PSC_TEMPERATURE] = direct;
		info->format[PSC_CURRENT_OUT] = direct;
		pdata->flags |= PMBUS_USE_COEFFICIENTS_CMD;
		break;
	case pim4820:
		info->func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_TEMP
			| PMBUS_HAVE_IIN;
		info->format[PSC_VOLTAGE_IN] = direct;
		info->format[PSC_TEMPERATURE] = direct;
		info->format[PSC_CURRENT_IN] = direct;
		pdata->flags |= PMBUS_USE_COEFFICIENTS_CMD;
		break;
	default:
		return -ENODEV;
	}

	return pmbus_do_probe(client, info);
}

static struct i2c_driver pim4328_driver = {
	.driver = {
		   .name = "pim4328",
		   },
	.probe = pim4328_probe,
	.id_table = pim4328_id,
};

module_i2c_driver(pim4328_driver);

MODULE_AUTHOR("Erik Rosen <erik.rosen@metormote.com>");
MODULE_DESCRIPTION("PMBus driver for PIM4006, PIM4328, PIM4820 power interface modules");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");

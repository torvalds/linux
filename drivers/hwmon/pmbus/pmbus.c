/*
 * Hardware monitoring driver for PMBus devices
 *
 * Copyright (c) 2010, 2011 Ericsson AB.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/pmbus.h>
#include "pmbus.h"

/*
 * Find sensor groups and status registers on each page.
 */
static void pmbus_find_sensor_groups(struct i2c_client *client,
				     struct pmbus_driver_info *info)
{
	int page;

	/* Sensors detected on page 0 only */
	if (pmbus_check_word_register(client, 0, PMBUS_READ_VIN))
		info->func[0] |= PMBUS_HAVE_VIN;
	if (pmbus_check_word_register(client, 0, PMBUS_READ_VCAP))
		info->func[0] |= PMBUS_HAVE_VCAP;
	if (pmbus_check_word_register(client, 0, PMBUS_READ_IIN))
		info->func[0] |= PMBUS_HAVE_IIN;
	if (pmbus_check_word_register(client, 0, PMBUS_READ_PIN))
		info->func[0] |= PMBUS_HAVE_PIN;
	if (info->func[0]
	    && pmbus_check_byte_register(client, 0, PMBUS_STATUS_INPUT))
		info->func[0] |= PMBUS_HAVE_STATUS_INPUT;
	if (pmbus_check_byte_register(client, 0, PMBUS_FAN_CONFIG_12) &&
	    pmbus_check_word_register(client, 0, PMBUS_READ_FAN_SPEED_1)) {
		info->func[0] |= PMBUS_HAVE_FAN12;
		if (pmbus_check_byte_register(client, 0, PMBUS_STATUS_FAN_12))
			info->func[0] |= PMBUS_HAVE_STATUS_FAN12;
	}
	if (pmbus_check_byte_register(client, 0, PMBUS_FAN_CONFIG_34) &&
	    pmbus_check_word_register(client, 0, PMBUS_READ_FAN_SPEED_3)) {
		info->func[0] |= PMBUS_HAVE_FAN34;
		if (pmbus_check_byte_register(client, 0, PMBUS_STATUS_FAN_34))
			info->func[0] |= PMBUS_HAVE_STATUS_FAN34;
	}
	if (pmbus_check_word_register(client, 0, PMBUS_READ_TEMPERATURE_1))
		info->func[0] |= PMBUS_HAVE_TEMP;
	if (pmbus_check_word_register(client, 0, PMBUS_READ_TEMPERATURE_2))
		info->func[0] |= PMBUS_HAVE_TEMP2;
	if (pmbus_check_word_register(client, 0, PMBUS_READ_TEMPERATURE_3))
		info->func[0] |= PMBUS_HAVE_TEMP3;
	if (info->func[0] & (PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2
			     | PMBUS_HAVE_TEMP3)
	    && pmbus_check_byte_register(client, 0,
					 PMBUS_STATUS_TEMPERATURE))
			info->func[0] |= PMBUS_HAVE_STATUS_TEMP;

	/* Sensors detected on all pages */
	for (page = 0; page < info->pages; page++) {
		if (pmbus_check_word_register(client, page, PMBUS_READ_VOUT)) {
			info->func[page] |= PMBUS_HAVE_VOUT;
			if (pmbus_check_byte_register(client, page,
						      PMBUS_STATUS_VOUT))
				info->func[page] |= PMBUS_HAVE_STATUS_VOUT;
		}
		if (pmbus_check_word_register(client, page, PMBUS_READ_IOUT)) {
			info->func[page] |= PMBUS_HAVE_IOUT;
			if (pmbus_check_byte_register(client, 0,
						      PMBUS_STATUS_IOUT))
				info->func[page] |= PMBUS_HAVE_STATUS_IOUT;
		}
		if (pmbus_check_word_register(client, page, PMBUS_READ_POUT))
			info->func[page] |= PMBUS_HAVE_POUT;
	}
}

/*
 * Identify chip parameters.
 */
static int pmbus_identify(struct i2c_client *client,
			  struct pmbus_driver_info *info)
{
	int ret = 0;

	if (!info->pages) {
		/*
		 * Check if the PAGE command is supported. If it is,
		 * keep setting the page number until it fails or until the
		 * maximum number of pages has been reached. Assume that
		 * this is the number of pages supported by the chip.
		 */
		if (pmbus_check_byte_register(client, 0, PMBUS_PAGE)) {
			int page;

			for (page = 1; page < PMBUS_PAGES; page++) {
				if (pmbus_set_page(client, page) < 0)
					break;
			}
			pmbus_set_page(client, 0);
			info->pages = page;
		} else {
			info->pages = 1;
		}

		pmbus_clear_faults(client);
	}

	if (pmbus_check_byte_register(client, 0, PMBUS_VOUT_MODE)) {
		int vout_mode;

		vout_mode = pmbus_read_byte_data(client, 0, PMBUS_VOUT_MODE);
		if (vout_mode >= 0 && vout_mode != 0xff) {
			switch (vout_mode >> 5) {
			case 0:
				break;
			case 1:
				info->format[PSC_VOLTAGE_OUT] = vid;
				info->vrm_version = vr11;
				break;
			case 2:
				info->format[PSC_VOLTAGE_OUT] = direct;
				break;
			default:
				ret = -ENODEV;
				goto abort;
			}
		}
	}

	/*
	 * We should check if the COEFFICIENTS register is supported.
	 * If it is, and the chip is configured for direct mode, we can read
	 * the coefficients from the chip, one set per group of sensor
	 * registers.
	 *
	 * To do this, we will need access to a chip which actually supports the
	 * COEFFICIENTS command, since the command is too complex to implement
	 * without testing it. Until then, abort if a chip configured for direct
	 * mode was detected.
	 */
	if (info->format[PSC_VOLTAGE_OUT] == direct) {
		ret = -ENODEV;
		goto abort;
	}

	/* Try to find sensor groups  */
	pmbus_find_sensor_groups(client, info);
abort:
	return ret;
}

static int pmbus_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct pmbus_driver_info *info;
	struct pmbus_platform_data *pdata = NULL;
	struct device *dev = &client->dev;

	info = devm_kzalloc(dev, sizeof(struct pmbus_driver_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (!strcmp(id->name, "dps460") || !strcmp(id->name, "dps800") ||
	    !strcmp(id->name, "sgd009")) {
		pdata = devm_kzalloc(dev, sizeof(struct pmbus_platform_data),
				     GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		pdata->flags = PMBUS_SKIP_STATUS_CHECK;
	}

	info->pages = id->driver_data;
	info->identify = pmbus_identify;
	dev->platform_data = pdata;

	return pmbus_do_probe(client, id, info);
}

/*
 * Use driver_data to set the number of pages supported by the chip.
 */
static const struct i2c_device_id pmbus_id[] = {
	{"adp4000", 1},
	{"bmr453", 1},
	{"bmr454", 1},
	{"dps460", 1},
	{"dps800", 1},
	{"mdt040", 1},
	{"ncp4200", 1},
	{"ncp4208", 1},
	{"pdt003", 1},
	{"pdt006", 1},
	{"pdt012", 1},
	{"pmbus", 0},
	{"sgd009", 1},
	{"tps40400", 1},
	{"tps544b20", 1},
	{"tps544b25", 1},
	{"tps544c20", 1},
	{"tps544c25", 1},
	{"udt020", 1},
	{}
};

MODULE_DEVICE_TABLE(i2c, pmbus_id);

/* This is the driver that will be inserted */
static struct i2c_driver pmbus_driver = {
	.driver = {
		   .name = "pmbus",
		   },
	.probe = pmbus_probe,
	.remove = pmbus_do_remove,
	.id_table = pmbus_id,
};

module_i2c_driver(pmbus_driver);

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("Generic PMBus driver");
MODULE_LICENSE("GPL");

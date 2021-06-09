// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for PMBus devices
 *
 * Copyright (c) 2010, 2011 Ericsson AB.
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

struct pmbus_device_info {
	int pages;
	u32 flags;
};

static const struct i2c_device_id pmbus_id[];

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
				if (pmbus_set_page(client, page, 0xff) < 0)
					break;
			}
			pmbus_set_page(client, 0, 0xff);
			info->pages = page;
		} else {
			info->pages = 1;
		}

		pmbus_clear_faults(client);
	}

	if (pmbus_check_byte_register(client, 0, PMBUS_VOUT_MODE)) {
		int vout_mode, i;

		vout_mode = pmbus_read_byte_data(client, 0, PMBUS_VOUT_MODE);
		if (vout_mode >= 0 && vout_mode != 0xff) {
			switch (vout_mode >> 5) {
			case 0:
				break;
			case 1:
				info->format[PSC_VOLTAGE_OUT] = vid;
				for (i = 0; i < info->pages; i++)
					info->vrm_version[i] = vr11;
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

static int pmbus_probe(struct i2c_client *client)
{
	struct pmbus_driver_info *info;
	struct pmbus_platform_data *pdata = NULL;
	struct device *dev = &client->dev;
	struct pmbus_device_info *device_info;

	info = devm_kzalloc(dev, sizeof(struct pmbus_driver_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	device_info = (struct pmbus_device_info *)i2c_match_id(pmbus_id, client)->driver_data;
	if (device_info->flags & PMBUS_SKIP_STATUS_CHECK) {
		pdata = devm_kzalloc(dev, sizeof(struct pmbus_platform_data),
				     GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		pdata->flags = PMBUS_SKIP_STATUS_CHECK;
	}

	info->pages = device_info->pages;
	info->identify = pmbus_identify;
	dev->platform_data = pdata;

	return pmbus_do_probe(client, info);
}

static const struct pmbus_device_info pmbus_info_one = {
	.pages = 1,
	.flags = 0
};
static const struct pmbus_device_info pmbus_info_zero = {
	.pages = 0,
	.flags = 0
};
static const struct pmbus_device_info pmbus_info_one_skip = {
	.pages = 1,
	.flags = PMBUS_SKIP_STATUS_CHECK
};

/*
 * Use driver_data to set the number of pages supported by the chip.
 */
static const struct i2c_device_id pmbus_id[] = {
	{"adp4000", (kernel_ulong_t)&pmbus_info_one},
	{"bmr453", (kernel_ulong_t)&pmbus_info_one},
	{"bmr454", (kernel_ulong_t)&pmbus_info_one},
	{"dps460", (kernel_ulong_t)&pmbus_info_one_skip},
	{"dps650ab", (kernel_ulong_t)&pmbus_info_one_skip},
	{"dps800", (kernel_ulong_t)&pmbus_info_one_skip},
	{"max20796", (kernel_ulong_t)&pmbus_info_one},
	{"mdt040", (kernel_ulong_t)&pmbus_info_one},
	{"ncp4200", (kernel_ulong_t)&pmbus_info_one},
	{"ncp4208", (kernel_ulong_t)&pmbus_info_one},
	{"pdt003", (kernel_ulong_t)&pmbus_info_one},
	{"pdt006", (kernel_ulong_t)&pmbus_info_one},
	{"pdt012", (kernel_ulong_t)&pmbus_info_one},
	{"pmbus", (kernel_ulong_t)&pmbus_info_zero},
	{"sgd009", (kernel_ulong_t)&pmbus_info_one_skip},
	{"tps40400", (kernel_ulong_t)&pmbus_info_one},
	{"tps544b20", (kernel_ulong_t)&pmbus_info_one},
	{"tps544b25", (kernel_ulong_t)&pmbus_info_one},
	{"tps544c20", (kernel_ulong_t)&pmbus_info_one},
	{"tps544c25", (kernel_ulong_t)&pmbus_info_one},
	{"udt020", (kernel_ulong_t)&pmbus_info_one},
	{}
};

MODULE_DEVICE_TABLE(i2c, pmbus_id);

/* This is the driver that will be inserted */
static struct i2c_driver pmbus_driver = {
	.driver = {
		   .name = "pmbus",
		   },
	.probe_new = pmbus_probe,
	.id_table = pmbus_id,
};

module_i2c_driver(pmbus_driver);

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("Generic PMBus driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);

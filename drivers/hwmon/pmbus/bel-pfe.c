// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for BEL PFE family power supplies.
 *
 * Copyright (c) 2019 Facebook Inc.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pmbus.h>

#include "pmbus.h"

enum chips {pfe1100, pfe3000};

/*
 * Disable status check for pfe3000 devices, because some devices report
 * communication error (invalid command) for VOUT_MODE command (0x20)
 * although correct VOUT_MODE (0x16) is returned: it leads to incorrect
 * exponent in linear mode.
 */
static struct pmbus_platform_data pfe3000_plat_data = {
	.flags = PMBUS_SKIP_STATUS_CHECK,
};

static struct pmbus_driver_info pfe_driver_info[] = {
	[pfe1100] = {
		.pages = 1,
		.format[PSC_VOLTAGE_IN] = linear,
		.format[PSC_VOLTAGE_OUT] = linear,
		.format[PSC_CURRENT_IN] = linear,
		.format[PSC_CURRENT_OUT] = linear,
		.format[PSC_POWER] = linear,
		.format[PSC_TEMPERATURE] = linear,
		.format[PSC_FAN] = linear,

		.func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
			   PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
			   PMBUS_HAVE_POUT |
			   PMBUS_HAVE_VIN | PMBUS_HAVE_IIN |
			   PMBUS_HAVE_PIN | PMBUS_HAVE_STATUS_INPUT |
			   PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 |
			   PMBUS_HAVE_STATUS_TEMP |
			   PMBUS_HAVE_FAN12,
	},

	[pfe3000] = {
		.pages = 7,
		.format[PSC_VOLTAGE_IN] = linear,
		.format[PSC_VOLTAGE_OUT] = linear,
		.format[PSC_CURRENT_IN] = linear,
		.format[PSC_CURRENT_OUT] = linear,
		.format[PSC_POWER] = linear,
		.format[PSC_TEMPERATURE] = linear,
		.format[PSC_FAN] = linear,

		/* Page 0: V1. */
		.func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
			   PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
			   PMBUS_HAVE_POUT | PMBUS_HAVE_FAN12 |
			   PMBUS_HAVE_VIN | PMBUS_HAVE_IIN |
			   PMBUS_HAVE_PIN | PMBUS_HAVE_STATUS_INPUT |
			   PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 |
			   PMBUS_HAVE_TEMP3 | PMBUS_HAVE_STATUS_TEMP |
			   PMBUS_HAVE_VCAP,

		/* Page 1: Vsb. */
		.func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
			   PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
			   PMBUS_HAVE_PIN | PMBUS_HAVE_STATUS_INPUT |
			   PMBUS_HAVE_POUT,

		/*
		 * Page 2: V1 Ishare.
		 * Page 3: Reserved.
		 * Page 4: V1 Cathode.
		 * Page 5: Vsb Cathode.
		 * Page 6: V1 Sense.
		 */
		.func[2] = PMBUS_HAVE_VOUT,
		.func[4] = PMBUS_HAVE_VOUT,
		.func[5] = PMBUS_HAVE_VOUT,
		.func[6] = PMBUS_HAVE_VOUT,
	},
};

static int pfe_pmbus_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	int model;

	model = (int)id->driver_data;

	/*
	 * PFE3000-12-069RA devices may not stay in page 0 during device
	 * probe which leads to probe failure (read status word failed).
	 * So let's set the device to page 0 at the beginning.
	 */
	if (model == pfe3000) {
		client->dev.platform_data = &pfe3000_plat_data;
		i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);
	}

	return pmbus_do_probe(client, id, &pfe_driver_info[model]);
}

static const struct i2c_device_id pfe_device_id[] = {
	{"pfe1100", pfe1100},
	{"pfe3000", pfe3000},
	{}
};

MODULE_DEVICE_TABLE(i2c, pfe_device_id);

static struct i2c_driver pfe_pmbus_driver = {
	.driver = {
		   .name = "bel-pfe",
	},
	.probe = pfe_pmbus_probe,
	.remove = pmbus_do_remove,
	.id_table = pfe_device_id,
};

module_i2c_driver(pfe_pmbus_driver);

MODULE_AUTHOR("Tao Ren <rentao.bupt@gmail.com>");
MODULE_DESCRIPTION("PMBus driver for BEL PFE Family Power Supplies");
MODULE_LICENSE("GPL");

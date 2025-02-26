// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for Maxim MAX15301
 *
 * Copyright (c) 2021 Flextronics International Sweden AB
 *
 * Even though the specification does not specifically mention it,
 * extensive empirical testing has revealed that auto-detection of
 * limit-registers will fail in a random fashion unless the delay
 * parameter is set to above about 80us. The default delay is set
 * to 100us to include some safety margin.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/pmbus.h>
#include "pmbus.h"

static const struct i2c_device_id max15301_id[] = {
	{ "bmr461" },
	{ "max15301" },
	{ "max15303" },
	{}
};
MODULE_DEVICE_TABLE(i2c, max15301_id);

struct max15301_data {
	int id;
	struct pmbus_driver_info info;
};

#define to_max15301_data(x)  container_of(x, struct max15301_data, info)

#define MAX15301_WAIT_TIME		100	/* us	*/

static ushort delay = MAX15301_WAIT_TIME;
module_param(delay, ushort, 0644);
MODULE_PARM_DESC(delay, "Delay between chip accesses in us");

static struct max15301_data max15301_data = {
	.info = {
		.pages = 1,
		.func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
			| PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT
			| PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2
			| PMBUS_HAVE_STATUS_TEMP
			| PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
	}
};

static int max15301_probe(struct i2c_client *client)
{
	int status;
	u8 device_id[I2C_SMBUS_BLOCK_MAX + 1];
	const struct i2c_device_id *mid;
	struct pmbus_driver_info *info = &max15301_data.info;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_BYTE_DATA
				     | I2C_FUNC_SMBUS_BLOCK_DATA))
		return -ENODEV;

	status = i2c_smbus_read_block_data(client, PMBUS_IC_DEVICE_ID, device_id);
	if (status < 0) {
		dev_err(&client->dev, "Failed to read Device Id\n");
		return status;
	}
	for (mid = max15301_id; mid->name[0]; mid++) {
		if (!strncasecmp(mid->name, device_id, strlen(mid->name)))
			break;
	}
	if (!mid->name[0]) {
		dev_err(&client->dev, "Unsupported device\n");
		return -ENODEV;
	}

	info->access_delay = delay;

	return pmbus_do_probe(client, info);
}

static struct i2c_driver max15301_driver = {
	.driver = {
		   .name = "max15301",
		   },
	.probe = max15301_probe,
	.id_table = max15301_id,
};

module_i2c_driver(max15301_driver);

MODULE_AUTHOR("Erik Rosen <erik.rosen@metormote.com>");
MODULE_DESCRIPTION("PMBus driver for Maxim MAX15301");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");

/*
 * Hardware monitoring driver for ZL6100 and compatibles
 *
 * Copyright (c) 2011 Ericsson AB.
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
#include <linux/i2c.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include "pmbus.h"

enum chips { zl2004, zl2005, zl2006, zl2008, zl2105, zl2106, zl6100, zl6105,
	     zl9101, zl9117 };

struct zl6100_data {
	int id;
	ktime_t access;		/* chip access time */
	int delay;		/* Delay between chip accesses in uS */
	struct pmbus_driver_info info;
};

#define to_zl6100_data(x)  container_of(x, struct zl6100_data, info)

#define ZL6100_MFR_CONFIG		0xd0
#define ZL6100_DEVICE_ID		0xe4

#define ZL6100_MFR_XTEMP_ENABLE		(1 << 7)

#define ZL6100_WAIT_TIME		1000	/* uS	*/

static ushort delay = ZL6100_WAIT_TIME;
module_param(delay, ushort, 0644);
MODULE_PARM_DESC(delay, "Delay between chip accesses in uS");

/* Some chips need a delay between accesses */
static inline void zl6100_wait(const struct zl6100_data *data)
{
	if (data->delay) {
		s64 delta = ktime_us_delta(ktime_get(), data->access);
		if (delta < data->delay)
			udelay(data->delay - delta);
	}
}

static int zl6100_read_word_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct zl6100_data *data = to_zl6100_data(info);
	int ret;

	if (page || reg >= PMBUS_VIRT_BASE)
		return -ENXIO;

	if (data->id == zl2005) {
		/*
		 * Limit register detection is not reliable on ZL2005.
		 * Make sure registers are not erroneously detected.
		 */
		switch (reg) {
		case PMBUS_VOUT_OV_WARN_LIMIT:
		case PMBUS_VOUT_UV_WARN_LIMIT:
		case PMBUS_IOUT_OC_WARN_LIMIT:
			return -ENXIO;
		}
	}

	zl6100_wait(data);
	ret = pmbus_read_word_data(client, page, reg);
	data->access = ktime_get();

	return ret;
}

static int zl6100_read_byte_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct zl6100_data *data = to_zl6100_data(info);
	int ret;

	if (page > 0)
		return -ENXIO;

	zl6100_wait(data);
	ret = pmbus_read_byte_data(client, page, reg);
	data->access = ktime_get();

	return ret;
}

static int zl6100_write_word_data(struct i2c_client *client, int page, int reg,
				  u16 word)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct zl6100_data *data = to_zl6100_data(info);
	int ret;

	if (page || reg >= PMBUS_VIRT_BASE)
		return -ENXIO;

	zl6100_wait(data);
	ret = pmbus_write_word_data(client, page, reg, word);
	data->access = ktime_get();

	return ret;
}

static int zl6100_write_byte(struct i2c_client *client, int page, u8 value)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct zl6100_data *data = to_zl6100_data(info);
	int ret;

	if (page > 0)
		return -ENXIO;

	zl6100_wait(data);
	ret = pmbus_write_byte(client, page, value);
	data->access = ktime_get();

	return ret;
}

static const struct i2c_device_id zl6100_id[] = {
	{"bmr450", zl2005},
	{"bmr451", zl2005},
	{"bmr462", zl2008},
	{"bmr463", zl2008},
	{"bmr464", zl2008},
	{"zl2004", zl2004},
	{"zl2005", zl2005},
	{"zl2006", zl2006},
	{"zl2008", zl2008},
	{"zl2105", zl2105},
	{"zl2106", zl2106},
	{"zl6100", zl6100},
	{"zl6105", zl6105},
	{"zl9101", zl9101},
	{"zl9117", zl9117},
	{ }
};
MODULE_DEVICE_TABLE(i2c, zl6100_id);

static int zl6100_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct zl6100_data *data;
	struct pmbus_driver_info *info;
	u8 device_id[I2C_SMBUS_BLOCK_MAX + 1];
	const struct i2c_device_id *mid;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA
				     | I2C_FUNC_SMBUS_READ_BLOCK_DATA))
		return -ENODEV;

	ret = i2c_smbus_read_block_data(client, ZL6100_DEVICE_ID,
					device_id);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to read device ID\n");
		return ret;
	}
	device_id[ret] = '\0';
	dev_info(&client->dev, "Device ID %s\n", device_id);

	mid = NULL;
	for (mid = zl6100_id; mid->name[0]; mid++) {
		if (!strncasecmp(mid->name, device_id, strlen(mid->name)))
			break;
	}
	if (!mid->name[0]) {
		dev_err(&client->dev, "Unsupported device\n");
		return -ENODEV;
	}
	if (id->driver_data != mid->driver_data)
		dev_notice(&client->dev,
			   "Device mismatch: Configured %s, detected %s\n",
			   id->name, mid->name);

	data = devm_kzalloc(&client->dev, sizeof(struct zl6100_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->id = mid->driver_data;

	/*
	 * According to information from the chip vendor, all currently
	 * supported chips are known to require a wait time between I2C
	 * accesses.
	 */
	data->delay = delay;

	/*
	 * Since there was a direct I2C device access above, wait before
	 * accessing the chip again.
	 */
	data->access = ktime_get();
	zl6100_wait(data);

	info = &data->info;

	info->pages = 1;
	info->func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT
	  | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
	  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT
	  | PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP;

	ret = i2c_smbus_read_word_data(client, ZL6100_MFR_CONFIG);
	if (ret < 0)
		return ret;

	if (ret & ZL6100_MFR_XTEMP_ENABLE)
		info->func[0] |= PMBUS_HAVE_TEMP2;

	data->access = ktime_get();
	zl6100_wait(data);

	info->read_word_data = zl6100_read_word_data;
	info->read_byte_data = zl6100_read_byte_data;
	info->write_word_data = zl6100_write_word_data;
	info->write_byte = zl6100_write_byte;

	return pmbus_do_probe(client, mid, info);
}

static struct i2c_driver zl6100_driver = {
	.driver = {
		   .name = "zl6100",
		   },
	.probe = zl6100_probe,
	.remove = pmbus_do_remove,
	.id_table = zl6100_id,
};

module_i2c_driver(zl6100_driver);

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("PMBus driver for ZL6100 and compatibles");
MODULE_LICENSE("GPL");

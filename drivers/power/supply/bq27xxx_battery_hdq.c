/*
 * BQ27xxx battery monitor HDQ/1-wire driver
 *
 * Copyright (C) 2007-2017 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/power/bq27xxx_battery.h>

#include <linux/w1.h>

#define W1_FAMILY_BQ27000	0x01

#define HDQ_CMD_READ	(0 << 7)
#define HDQ_CMD_WRITE	(1 << 7)

static int F_ID;
module_param(F_ID, int, S_IRUSR);
MODULE_PARM_DESC(F_ID, "1-wire slave FID for BQ27xxx device");

static int w1_bq27000_read(struct w1_slave *sl, unsigned int reg)
{
	u8 val;

	mutex_lock(&sl->master->bus_mutex);
	w1_write_8(sl->master, HDQ_CMD_READ | reg);
	val = w1_read_8(sl->master);
	mutex_unlock(&sl->master->bus_mutex);

	return val;
}

static int bq27xxx_battery_hdq_read(struct bq27xxx_device_info *di, u8 reg,
				    bool single)
{
	struct w1_slave *sl = dev_to_w1_slave(di->dev);
	unsigned int timeout = 3;
	int upper, lower;
	int temp;

	if (!single) {
		/*
		 * Make sure the value has not changed in between reading the
		 * lower and the upper part
		 */
		upper = w1_bq27000_read(sl, reg + 1);
		do {
			temp = upper;
			if (upper < 0)
				return upper;

			lower = w1_bq27000_read(sl, reg);
			if (lower < 0)
				return lower;

			upper = w1_bq27000_read(sl, reg + 1);
		} while (temp != upper && --timeout);

		if (timeout == 0)
			return -EIO;

		return (upper << 8) | lower;
	}

	return w1_bq27000_read(sl, reg);
}

static int bq27xxx_battery_hdq_add_slave(struct w1_slave *sl)
{
	struct bq27xxx_device_info *di;

	di = devm_kzalloc(&sl->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	dev_set_drvdata(&sl->dev, di);

	di->dev = &sl->dev;
	di->chip = BQ27000;
	di->name = "bq27000-battery";
	di->bus.read = bq27xxx_battery_hdq_read;

	return bq27xxx_battery_setup(di);
}

static void bq27xxx_battery_hdq_remove_slave(struct w1_slave *sl)
{
	struct bq27xxx_device_info *di = dev_get_drvdata(&sl->dev);

	bq27xxx_battery_teardown(di);
}

static struct w1_family_ops bq27xxx_battery_hdq_fops = {
	.add_slave	= bq27xxx_battery_hdq_add_slave,
	.remove_slave	= bq27xxx_battery_hdq_remove_slave,
};

static struct w1_family bq27xxx_battery_hdq_family = {
	.fid = W1_FAMILY_BQ27000,
	.fops = &bq27xxx_battery_hdq_fops,
};

static int __init bq27xxx_battery_hdq_init(void)
{
	if (F_ID)
		bq27xxx_battery_hdq_family.fid = F_ID;

	return w1_register_family(&bq27xxx_battery_hdq_family);
}
module_init(bq27xxx_battery_hdq_init);

static void __exit bq27xxx_battery_hdq_exit(void)
{
	w1_unregister_family(&bq27xxx_battery_hdq_family);
}
module_exit(bq27xxx_battery_hdq_exit);

MODULE_AUTHOR("Texas Instruments Ltd");
MODULE_DESCRIPTION("BQ27xxx battery monitor HDQ/1-wire driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_BQ27000));

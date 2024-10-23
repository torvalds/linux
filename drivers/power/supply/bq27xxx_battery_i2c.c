// SPDX-License-Identifier: GPL-2.0
/*
 * BQ27xxx battery monitor I2C driver
 *
 * Copyright (C) 2015 Texas Instruments Incorporated - https://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/unaligned.h>

#include <linux/power/bq27xxx_battery.h>

static DEFINE_IDR(battery_id);
static DEFINE_MUTEX(battery_mutex);

static irqreturn_t bq27xxx_battery_irq_handler_thread(int irq, void *data)
{
	struct bq27xxx_device_info *di = data;

	bq27xxx_battery_update(di);

	return IRQ_HANDLED;
}

static int bq27xxx_battery_i2c_read(struct bq27xxx_device_info *di, u8 reg,
				    bool single)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	struct i2c_msg msg[2];
	u8 data[2];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = &reg;
	msg[0].len = sizeof(reg);
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = data;
	if (single)
		msg[1].len = 1;
	else
		msg[1].len = 2;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0)
		return ret;

	if (!single)
		ret = get_unaligned_le16(data);
	else
		ret = data[0];

	return ret;
}

static int bq27xxx_battery_i2c_write(struct bq27xxx_device_info *di, u8 reg,
				     int value, bool single)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	struct i2c_msg msg;
	u8 data[4];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	data[0] = reg;
	if (single) {
		data[1] = (u8) value;
		msg.len = 2;
	} else {
		put_unaligned_le16(value, &data[1]);
		msg.len = 3;
	}

	msg.buf = data;
	msg.addr = client->addr;
	msg.flags = 0;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EINVAL;
	return 0;
}

static int bq27xxx_battery_i2c_bulk_read(struct bq27xxx_device_info *di, u8 reg,
					 u8 *data, int len)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	int ret;

	if (!client->adapter)
		return -ENODEV;

	ret = i2c_smbus_read_i2c_block_data(client, reg, len, data);
	if (ret < 0)
		return ret;
	if (ret != len)
		return -EINVAL;
	return 0;
}

static int bq27xxx_battery_i2c_bulk_write(struct bq27xxx_device_info *di,
					  u8 reg, u8 *data, int len)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	struct i2c_msg msg;
	u8 buf[33];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	buf[0] = reg;
	memcpy(&buf[1], data, len);

	msg.buf = buf;
	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = len + 1;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EINVAL;
	return 0;
}

#ifdef CONFIG_BATTERY_BQ27XXX_RESIST_TABLE_UPDATES_NVM
static int bq27xx_parse_dt(struct bq27xxx_device_info *di,
					struct device *dev,
					struct device_node *battery_np)
{
	int ret;
	int rc;

	ret = of_property_read_u32(battery_np, "qmax-cell0", &di->qmax_cell0);
	if (ret) {
		dev_err(dev, "Undefined Qmax-Cell0\n");
		return ret;
	}

	rc = of_property_count_elems_of_size(battery_np, "resist-table",
					     sizeof(u32));

	if (rc != BQ27XXX_RESISTANCE_TABLE_LENGTH) {
		dev_err(dev, "Invalid number of elements in resist-table\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_array(battery_np, "resist-table",
				   di->resist_table, BQ27XXX_RESISTANCE_TABLE_LENGTH);
	if (ret)
		dev_err(dev, "Undefined resistance table\n");

	return ret;
}
#endif

static int bq27xxx_restore(struct device *dev)
{
	int ret = 0;
	struct i2c_client *client = to_i2c_client(dev);
	struct bq27xxx_device_info *di = i2c_get_clientdata(client);

	if (client->irq > 0) {
		disable_irq_nosync(client->irq);
		devm_free_irq(dev, client->irq, di);
		ret = request_threaded_irq(client->irq,
					   NULL, bq27xxx_battery_irq_handler_thread,
					   IRQF_ONESHOT,
					   di->name, di);
	}

	return ret;
}

static const struct dev_pm_ops bq27xxx_pm_ops = {
	.restore = bq27xxx_restore,
};

static int bq27xxx_battery_i2c_probe(struct i2c_client *client,
				     const struct i2c_device_id *id)
{
	struct bq27xxx_device_info *di;
	int ret;
	char *name;
	int num;
#ifdef CONFIG_BATTERY_BQ27XXX_RESIST_TABLE_UPDATES_NVM
	struct device_node *battery_np_rt;
#endif

	/* Get new ID for the new battery device */
	mutex_lock(&battery_mutex);
	num = idr_alloc(&battery_id, client, 0, 0, GFP_KERNEL);
	mutex_unlock(&battery_mutex);
	if (num < 0)
		return num;

	name = devm_kasprintf(&client->dev, GFP_KERNEL, "%s-%d", id->name, num);
	if (!name)
		goto err_mem;

	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		goto err_mem;

	di->id = num;
	di->dev = &client->dev;
	di->chip = id->driver_data;
	di->name = name;

	di->bus.read = bq27xxx_battery_i2c_read;
	di->bus.write = bq27xxx_battery_i2c_write;
	di->bus.read_bulk = bq27xxx_battery_i2c_bulk_read;
	di->bus.write_bulk = bq27xxx_battery_i2c_bulk_write;

#ifdef CONFIG_BATTERY_BQ27XXX_RESIST_TABLE_UPDATES_NVM
	battery_np_rt = of_parse_phandle(client->dev.of_node,
					 "bat-resist-table", 0);
	if (!battery_np_rt)
		return -ENODEV;
	ret = bq27xx_parse_dt(di, di->dev, battery_np_rt);
	of_node_put(battery_np_rt);
	if (ret)
		return -EINVAL;
#endif

	ret = bq27xxx_battery_setup(di);
	if (ret)
		goto err_failed;

	/* Schedule a polling after about 1 min */
	schedule_delayed_work(&di->work, 60 * HZ);

	i2c_set_clientdata(client, di);

	if (client->irq) {
		ret = request_threaded_irq(client->irq,
				NULL, bq27xxx_battery_irq_handler_thread,
				IRQF_ONESHOT,
				di->name, di);
		if (ret) {
			dev_err(&client->dev,
				"Unable to register IRQ %d error %d\n",
				client->irq, ret);
			bq27xxx_battery_teardown(di);
			goto err_failed;
		}
	}

	return 0;

err_mem:
	ret = -ENOMEM;

err_failed:
	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, num);
	mutex_unlock(&battery_mutex);

	return ret;
}

static void bq27xxx_battery_i2c_remove(struct i2c_client *client)
{
	struct bq27xxx_device_info *di = i2c_get_clientdata(client);

	if (client->irq)
		free_irq(client->irq, di);

	bq27xxx_battery_teardown(di);

	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, di->id);
	mutex_unlock(&battery_mutex);
}

static const struct i2c_device_id bq27xxx_i2c_id_table[] = {
	{ "bq27200", BQ27000 },
	{ "bq27210", BQ27010 },
	{ "bq27500", BQ2750X },
	{ "bq27510", BQ2751X },
	{ "bq27520", BQ2752X },
	{ "bq27500-1", BQ27500 },
	{ "bq27510g1", BQ27510G1 },
	{ "bq27510g2", BQ27510G2 },
	{ "bq27510g3", BQ27510G3 },
	{ "bq27520g1", BQ27520G1 },
	{ "bq27520g2", BQ27520G2 },
	{ "bq27520g3", BQ27520G3 },
	{ "bq27520g4", BQ27520G4 },
	{ "bq27521", BQ27521 },
	{ "bq27530", BQ27530 },
	{ "bq27531", BQ27531 },
	{ "bq27541", BQ27541 },
	{ "bq27542", BQ27542 },
	{ "bq27546", BQ27546 },
	{ "bq27742", BQ27742 },
	{ "bq27545", BQ27545 },
	{ "bq27411", BQ27411 },
	{ "bq27421", BQ27421 },
	{ "bq27425", BQ27425 },
	{ "bq27426", BQ27426 },
	{ "bq27441", BQ27441 },
	{ "bq27621", BQ27621 },
	{ "bq27z561", BQ27Z561 },
	{ "bq28z610", BQ28Z610 },
	{ "bq34z100", BQ34Z100 },
	{ "bq78z100", BQ78Z100 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq27xxx_i2c_id_table);

#ifdef CONFIG_OF
static const struct of_device_id bq27xxx_battery_i2c_of_match_table[] = {
	{ .compatible = "ti,bq27200" },
	{ .compatible = "ti,bq27210" },
	{ .compatible = "ti,bq27500" },
	{ .compatible = "ti,bq27510" },
	{ .compatible = "ti,bq27520" },
	{ .compatible = "ti,bq27500-1" },
	{ .compatible = "ti,bq27510g1" },
	{ .compatible = "ti,bq27510g2" },
	{ .compatible = "ti,bq27510g3" },
	{ .compatible = "ti,bq27520g1" },
	{ .compatible = "ti,bq27520g2" },
	{ .compatible = "ti,bq27520g3" },
	{ .compatible = "ti,bq27520g4" },
	{ .compatible = "ti,bq27521" },
	{ .compatible = "ti,bq27530" },
	{ .compatible = "ti,bq27531" },
	{ .compatible = "ti,bq27541" },
	{ .compatible = "ti,bq27542" },
	{ .compatible = "ti,bq27546" },
	{ .compatible = "ti,bq27742" },
	{ .compatible = "ti,bq27545" },
	{ .compatible = "ti,bq27411" },
	{ .compatible = "ti,bq27421" },
	{ .compatible = "ti,bq27425" },
	{ .compatible = "ti,bq27426" },
	{ .compatible = "ti,bq27441" },
	{ .compatible = "ti,bq27621" },
	{ .compatible = "ti,bq27z561" },
	{ .compatible = "ti,bq28z610" },
	{ .compatible = "ti,bq34z100" },
	{ .compatible = "ti,bq78z100" },
	{},
};
MODULE_DEVICE_TABLE(of, bq27xxx_battery_i2c_of_match_table);
#endif

static struct i2c_driver bq27xxx_battery_i2c_driver = {
	.driver = {
		.name = "bq27xxx-battery",
		.of_match_table = of_match_ptr(bq27xxx_battery_i2c_of_match_table),
		.pm = &bq27xxx_pm_ops,
	},
	.probe = bq27xxx_battery_i2c_probe,
	.remove = bq27xxx_battery_i2c_remove,
	.id_table = bq27xxx_i2c_id_table,
};
module_i2c_driver(bq27xxx_battery_i2c_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("BQ27xxx battery monitor i2c driver");
MODULE_LICENSE("GPL");

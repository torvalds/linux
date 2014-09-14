/*
 * Core driver for TPS61050/61052 boost converters, used for while LED
 * driving, audio power amplification, white LED flash, and generic
 * boost conversion. Additionally it provides a 1-bit GPIO pin (out or in)
 * and a flash synchronization pin to synchronize flash events when used as
 * flashgun.
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tps6105x.h>

int tps6105x_set(struct tps6105x *tps6105x, u8 reg, u8 value)
{
	int ret;

	ret = mutex_lock_interruptible(&tps6105x->lock);
	if (ret)
		return ret;
	ret = i2c_smbus_write_byte_data(tps6105x->client, reg, value);
	mutex_unlock(&tps6105x->lock);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(tps6105x_set);

int tps6105x_get(struct tps6105x *tps6105x, u8 reg, u8 *buf)
{
	int ret;

	ret = mutex_lock_interruptible(&tps6105x->lock);
	if (ret)
		return ret;
	ret = i2c_smbus_read_byte_data(tps6105x->client, reg);
	mutex_unlock(&tps6105x->lock);
	if (ret < 0)
		return ret;

	*buf = ret;
	return 0;
}
EXPORT_SYMBOL(tps6105x_get);

/*
 * Masks off the bits in the mask and sets the bits in the bitvalues
 * parameter in one atomic operation
 */
int tps6105x_mask_and_set(struct tps6105x *tps6105x, u8 reg,
			  u8 bitmask, u8 bitvalues)
{
	int ret;
	u8 regval;

	ret = mutex_lock_interruptible(&tps6105x->lock);
	if (ret)
		return ret;
	ret = i2c_smbus_read_byte_data(tps6105x->client, reg);
	if (ret < 0)
		goto fail;
	regval = ret;
	regval = (~bitmask & regval) | (bitmask & bitvalues);
	ret = i2c_smbus_write_byte_data(tps6105x->client, reg, regval);
fail:
	mutex_unlock(&tps6105x->lock);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(tps6105x_mask_and_set);

static int tps6105x_startup(struct tps6105x *tps6105x)
{
	int ret;
	u8 regval;

	ret = tps6105x_get(tps6105x, TPS6105X_REG_0, &regval);
	if (ret)
		return ret;
	switch (regval >> TPS6105X_REG0_MODE_SHIFT) {
	case TPS6105X_REG0_MODE_SHUTDOWN:
		dev_info(&tps6105x->client->dev,
			 "TPS6105x found in SHUTDOWN mode\n");
		break;
	case TPS6105X_REG0_MODE_TORCH:
		dev_info(&tps6105x->client->dev,
			 "TPS6105x found in TORCH mode\n");
		break;
	case TPS6105X_REG0_MODE_TORCH_FLASH:
		dev_info(&tps6105x->client->dev,
			 "TPS6105x found in FLASH mode\n");
		break;
	case TPS6105X_REG0_MODE_VOLTAGE:
		dev_info(&tps6105x->client->dev,
			 "TPS6105x found in VOLTAGE mode\n");
		break;
	default:
		break;
	}

	return ret;
}

/*
 * MFD cells - we have one cell which is selected operation
 * mode, and we always have a GPIO cell.
 */
static struct mfd_cell tps6105x_cells[] = {
	{
		/* name will be runtime assigned */
		.id = -1,
	},
	{
		.name = "tps6105x-gpio",
		.id = -1,
	},
};

static int tps6105x_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct tps6105x			*tps6105x;
	struct tps6105x_platform_data	*pdata;
	int ret;
	int i;

	tps6105x = devm_kmalloc(&client->dev, sizeof(*tps6105x), GFP_KERNEL);
	if (!tps6105x)
		return -ENOMEM;

	i2c_set_clientdata(client, tps6105x);
	tps6105x->client = client;
	pdata = dev_get_platdata(&client->dev);
	tps6105x->pdata = pdata;
	mutex_init(&tps6105x->lock);

	ret = tps6105x_startup(tps6105x);
	if (ret) {
		dev_err(&client->dev, "chip initialization failed\n");
		return ret;
	}

	/* Remove warning texts when you implement new cell drivers */
	switch (pdata->mode) {
	case TPS6105X_MODE_SHUTDOWN:
		dev_info(&client->dev,
			 "present, not used for anything, only GPIO\n");
		break;
	case TPS6105X_MODE_TORCH:
		tps6105x_cells[0].name = "tps6105x-leds";
		dev_warn(&client->dev,
			 "torch mode is unsupported\n");
		break;
	case TPS6105X_MODE_TORCH_FLASH:
		tps6105x_cells[0].name = "tps6105x-flash";
		dev_warn(&client->dev,
			 "flash mode is unsupported\n");
		break;
	case TPS6105X_MODE_VOLTAGE:
		tps6105x_cells[0].name ="tps6105x-regulator";
		break;
	default:
		break;
	}

	/* Set up and register the platform devices. */
	for (i = 0; i < ARRAY_SIZE(tps6105x_cells); i++) {
		/* One state holder for all drivers, this is simple */
		tps6105x_cells[i].platform_data = tps6105x;
		tps6105x_cells[i].pdata_size = sizeof(*tps6105x);
	}

	return mfd_add_devices(&client->dev, 0, tps6105x_cells,
			       ARRAY_SIZE(tps6105x_cells), NULL, 0, NULL);
}

static int tps6105x_remove(struct i2c_client *client)
{
	struct tps6105x *tps6105x = i2c_get_clientdata(client);

	mfd_remove_devices(&client->dev);

	/* Put chip in shutdown mode */
	tps6105x_mask_and_set(tps6105x, TPS6105X_REG_0,
		TPS6105X_REG0_MODE_MASK,
		TPS6105X_MODE_SHUTDOWN << TPS6105X_REG0_MODE_SHIFT);

	return 0;
}

static const struct i2c_device_id tps6105x_id[] = {
	{ "tps61050", 0 },
	{ "tps61052", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tps6105x_id);

static struct i2c_driver tps6105x_driver = {
	.driver = {
		.name	= "tps6105x",
	},
	.probe		= tps6105x_probe,
	.remove		= tps6105x_remove,
	.id_table	= tps6105x_id,
};

static int __init tps6105x_init(void)
{
	return i2c_add_driver(&tps6105x_driver);
}
subsys_initcall(tps6105x_init);

static void __exit tps6105x_exit(void)
{
	i2c_del_driver(&tps6105x_driver);
}
module_exit(tps6105x_exit);

MODULE_AUTHOR("Linus Walleij");
MODULE_DESCRIPTION("TPS6105x White LED Boost Converter Driver");
MODULE_LICENSE("GPL v2");

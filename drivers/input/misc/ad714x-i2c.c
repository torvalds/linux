/*
 * AD714X CapTouch Programmable Controller driver (I2C bus)
 *
 * Copyright 2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/input.h>	/* BUS_I2C */
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/types.h>
#include "ad714x.h"

#ifdef CONFIG_PM
static int ad714x_i2c_suspend(struct i2c_client *client, pm_message_t message)
{
	return ad714x_disable(i2c_get_clientdata(client));
}

static int ad714x_i2c_resume(struct i2c_client *client)
{
	return ad714x_enable(i2c_get_clientdata(client));
}
#else
# define ad714x_i2c_suspend NULL
# define ad714x_i2c_resume  NULL
#endif

static int ad714x_i2c_write(struct device *dev, unsigned short reg,
				unsigned short data)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret = 0;
	u8 *_reg = (u8 *)&reg;
	u8 *_data = (u8 *)&data;

	u8 tx[4] = {
		_reg[1],
		_reg[0],
		_data[1],
		_data[0]
	};

	ret = i2c_master_send(client, tx, 4);
	if (ret < 0)
		dev_err(&client->dev, "I2C write error\n");

	return ret;
}

static int ad714x_i2c_read(struct device *dev, unsigned short reg,
				unsigned short *data)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret = 0;
	u8 *_reg = (u8 *)&reg;
	u8 *_data = (u8 *)data;

	u8 tx[2] = {
		_reg[1],
		_reg[0]
	};
	u8 rx[2];

	ret = i2c_master_send(client, tx, 2);
	if (ret >= 0)
		ret = i2c_master_recv(client, rx, 2);

	if (unlikely(ret < 0)) {
		dev_err(&client->dev, "I2C read error\n");
	} else {
		_data[0] = rx[1];
		_data[1] = rx[0];
	}

	return ret;
}

static int __devinit ad714x_i2c_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct ad714x_chip *chip;

	chip = ad714x_probe(&client->dev, BUS_I2C, client->irq,
			    ad714x_i2c_read, ad714x_i2c_write);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	i2c_set_clientdata(client, chip);

	return 0;
}

static int __devexit ad714x_i2c_remove(struct i2c_client *client)
{
	struct ad714x_chip *chip = i2c_get_clientdata(client);

	ad714x_remove(chip);

	return 0;
}

static const struct i2c_device_id ad714x_id[] = {
	{ "ad7142_captouch", 0 },
	{ "ad7143_captouch", 0 },
	{ "ad7147_captouch", 0 },
	{ "ad7147a_captouch", 0 },
	{ "ad7148_captouch", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad714x_id);

static struct i2c_driver ad714x_i2c_driver = {
	.driver = {
		.name = "ad714x_captouch",
	},
	.probe    = ad714x_i2c_probe,
	.remove   = __devexit_p(ad714x_i2c_remove),
	.suspend  = ad714x_i2c_suspend,
	.resume	  = ad714x_i2c_resume,
	.id_table = ad714x_id,
};

static __init int ad714x_i2c_init(void)
{
	return i2c_add_driver(&ad714x_i2c_driver);
}
module_init(ad714x_i2c_init);

static __exit void ad714x_i2c_exit(void)
{
	i2c_del_driver(&ad714x_i2c_driver);
}
module_exit(ad714x_i2c_exit);

MODULE_DESCRIPTION("Analog Devices AD714X Capacitance Touch Sensor I2C Bus Driver");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL");

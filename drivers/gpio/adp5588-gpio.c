/*
 * GPIO Chip driver for Analog Devices
 * ADP5588 I/O Expander and QWERTY Keypad Controller
 *
 * Copyright 2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/gpio.h>

#include <linux/i2c/adp5588.h>

#define DRV_NAME		"adp5588-gpio"
#define MAXGPIO			18
#define ADP_BANK(offs)		((offs) >> 3)
#define ADP_BIT(offs)		(1u << ((offs) & 0x7))

struct adp5588_gpio {
	struct i2c_client *client;
	struct gpio_chip gpio_chip;
	struct mutex lock;	/* protect cached dir, dat_out */
	unsigned gpio_start;
	uint8_t dat_out[3];
	uint8_t dir[3];
};

static int adp5588_gpio_read(struct i2c_client *client, u8 reg)
{
	int ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0)
		dev_err(&client->dev, "Read Error\n");

	return ret;
}

static int adp5588_gpio_write(struct i2c_client *client, u8 reg, u8 val)
{
	int ret = i2c_smbus_write_byte_data(client, reg, val);

	if (ret < 0)
		dev_err(&client->dev, "Write Error\n");

	return ret;
}

static int adp5588_gpio_get_value(struct gpio_chip *chip, unsigned off)
{
	struct adp5588_gpio *dev =
	    container_of(chip, struct adp5588_gpio, gpio_chip);

	return !!(adp5588_gpio_read(dev->client, GPIO_DAT_STAT1 + ADP_BANK(off))
		  & ADP_BIT(off));
}

static void adp5588_gpio_set_value(struct gpio_chip *chip,
				   unsigned off, int val)
{
	unsigned bank, bit;
	struct adp5588_gpio *dev =
	    container_of(chip, struct adp5588_gpio, gpio_chip);

	bank = ADP_BANK(off);
	bit = ADP_BIT(off);

	mutex_lock(&dev->lock);
	if (val)
		dev->dat_out[bank] |= bit;
	else
		dev->dat_out[bank] &= ~bit;

	adp5588_gpio_write(dev->client, GPIO_DAT_OUT1 + bank,
			   dev->dat_out[bank]);
	mutex_unlock(&dev->lock);
}

static int adp5588_gpio_direction_input(struct gpio_chip *chip, unsigned off)
{
	int ret;
	unsigned bank;
	struct adp5588_gpio *dev =
	    container_of(chip, struct adp5588_gpio, gpio_chip);

	bank = ADP_BANK(off);

	mutex_lock(&dev->lock);
	dev->dir[bank] &= ~ADP_BIT(off);
	ret = adp5588_gpio_write(dev->client, GPIO_DIR1 + bank, dev->dir[bank]);
	mutex_unlock(&dev->lock);

	return ret;
}

static int adp5588_gpio_direction_output(struct gpio_chip *chip,
					 unsigned off, int val)
{
	int ret;
	unsigned bank, bit;
	struct adp5588_gpio *dev =
	    container_of(chip, struct adp5588_gpio, gpio_chip);

	bank = ADP_BANK(off);
	bit = ADP_BIT(off);

	mutex_lock(&dev->lock);
	dev->dir[bank] |= bit;

	if (val)
		dev->dat_out[bank] |= bit;
	else
		dev->dat_out[bank] &= ~bit;

	ret = adp5588_gpio_write(dev->client, GPIO_DAT_OUT1 + bank,
				 dev->dat_out[bank]);
	ret |= adp5588_gpio_write(dev->client, GPIO_DIR1 + bank,
				 dev->dir[bank]);
	mutex_unlock(&dev->lock);

	return ret;
}

static int __devinit adp5588_gpio_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct adp5588_gpio_platform_data *pdata = client->dev.platform_data;
	struct adp5588_gpio *dev;
	struct gpio_chip *gc;
	int ret, i, revid;

	if (pdata == NULL) {
		dev_err(&client->dev, "missing platform data\n");
		return -ENODEV;
	}

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "SMBUS Byte Data not Supported\n");
		return -EIO;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(&client->dev, "failed to alloc memory\n");
		return -ENOMEM;
	}

	dev->client = client;

	gc = &dev->gpio_chip;
	gc->direction_input = adp5588_gpio_direction_input;
	gc->direction_output = adp5588_gpio_direction_output;
	gc->get = adp5588_gpio_get_value;
	gc->set = adp5588_gpio_set_value;
	gc->can_sleep = 1;

	gc->base = pdata->gpio_start;
	gc->ngpio = MAXGPIO;
	gc->label = client->name;
	gc->owner = THIS_MODULE;

	mutex_init(&dev->lock);


	ret = adp5588_gpio_read(dev->client, DEV_ID);
	if (ret < 0)
		goto err;

	revid = ret & ADP5588_DEVICE_ID_MASK;

	for (i = 0, ret = 0; i <= ADP_BANK(MAXGPIO); i++) {
		dev->dat_out[i] = adp5588_gpio_read(client, GPIO_DAT_OUT1 + i);
		dev->dir[i] = adp5588_gpio_read(client, GPIO_DIR1 + i);
		ret |= adp5588_gpio_write(client, KP_GPIO1 + i, 0);
		ret |= adp5588_gpio_write(client, GPIO_PULL1 + i,
				(pdata->pullup_dis_mask >> (8 * i)) & 0xFF);

		if (ret)
			goto err;
	}

	ret = gpiochip_add(&dev->gpio_chip);
	if (ret)
		goto err;

	dev_info(&client->dev, "gpios %d..%d on a %s Rev. %d\n",
			gc->base, gc->base + gc->ngpio - 1,
			client->name, revid);

	if (pdata->setup) {
		ret = pdata->setup(client, gc->base, gc->ngpio, pdata->context);
		if (ret < 0)
			dev_warn(&client->dev, "setup failed, %d\n", ret);
	}

	i2c_set_clientdata(client, dev);
	return 0;

err:
	kfree(dev);
	return ret;
}

static int __devexit adp5588_gpio_remove(struct i2c_client *client)
{
	struct adp5588_gpio_platform_data *pdata = client->dev.platform_data;
	struct adp5588_gpio *dev = i2c_get_clientdata(client);
	int ret;

	if (pdata->teardown) {
		ret = pdata->teardown(client,
				      dev->gpio_chip.base, dev->gpio_chip.ngpio,
				      pdata->context);
		if (ret < 0) {
			dev_err(&client->dev, "teardown failed %d\n", ret);
			return ret;
		}
	}

	ret = gpiochip_remove(&dev->gpio_chip);
	if (ret) {
		dev_err(&client->dev, "gpiochip_remove failed %d\n", ret);
		return ret;
	}

	kfree(dev);
	return 0;
}

static const struct i2c_device_id adp5588_gpio_id[] = {
	{DRV_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, adp5588_gpio_id);

static struct i2c_driver adp5588_gpio_driver = {
	.driver = {
		   .name = DRV_NAME,
		   },
	.probe = adp5588_gpio_probe,
	.remove = __devexit_p(adp5588_gpio_remove),
	.id_table = adp5588_gpio_id,
};

static int __init adp5588_gpio_init(void)
{
	return i2c_add_driver(&adp5588_gpio_driver);
}

module_init(adp5588_gpio_init);

static void __exit adp5588_gpio_exit(void)
{
	i2c_del_driver(&adp5588_gpio_driver);
}

module_exit(adp5588_gpio_exit);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("GPIO ADP5588 Driver");
MODULE_LICENSE("GPL");

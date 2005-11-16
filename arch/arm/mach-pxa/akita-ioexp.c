/*
 * Support for the Extra GPIOs on the Sharp SL-C1000 (Akita)
 * (uses a Maxim MAX7310 8 Port IO Expander)
 *
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Richard Purdie <richard@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <asm/arch/akita.h>

/* MAX7310 Regiser Map */
#define MAX7310_INPUT    0x00
#define MAX7310_OUTPUT   0x01
#define MAX7310_POLINV   0x02
#define MAX7310_IODIR    0x03 /* 1 = Input, 0 = Output */
#define MAX7310_TIMEOUT  0x04

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x18, I2C_CLIENT_END };

/* I2C Magic */
I2C_CLIENT_INSMOD;

static int max7310_write(struct i2c_client *client, int address, int data);
static struct i2c_client max7310_template;
static void akita_ioexp_work(void *private_);

static struct device *akita_ioexp_device;
static unsigned char ioexp_output_value = AKITA_IOEXP_IO_OUT;
DECLARE_WORK(akita_ioexp, akita_ioexp_work, NULL);


/*
 * MAX7310 Access
 */
static int max7310_config(struct device *dev, int iomode, int polarity)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);

	ret = max7310_write(client, MAX7310_POLINV, polarity);
	if (ret < 0)
		return ret;
	ret = max7310_write(client, MAX7310_IODIR, iomode);
	return ret;
}

static int max7310_set_ouputs(struct device *dev, int outputs)
{
	struct i2c_client *client = to_i2c_client(dev);

	return max7310_write(client, MAX7310_OUTPUT, outputs);
}

/*
 * I2C Functions
 */
static int max7310_write(struct i2c_client *client, int address, int value)
{
	u8 data[2];

	data[0] = address & 0xff;
	data[1] = value & 0xff;

	if (i2c_master_send(client, data, 2) == 2)
		return 0;
	return -1;
}

static int max7310_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *new_client;
	int err;

	if (!(new_client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL)))
		return -ENOMEM;

	max7310_template.adapter = adapter;
	max7310_template.addr = address;

	memcpy(new_client, &max7310_template, sizeof(struct i2c_client));

	if ((err = i2c_attach_client(new_client))) {
		kfree(new_client);
		return err;
	}

	max7310_config(&new_client->dev, AKITA_IOEXP_IO_DIR, 0);
	akita_ioexp_device = &new_client->dev;
	schedule_work(&akita_ioexp);

	return 0;
}

static int max7310_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_probe(adapter, &addr_data, max7310_detect);
}

static int max7310_detach_client(struct i2c_client *client)
{
	int err;

	akita_ioexp_device = NULL;

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(client);
	return 0;
}

static struct i2c_driver max7310_i2c_driver = {
	.owner		= THIS_MODULE,
	.name		= "akita-max7310",
	.id		= I2C_DRIVERID_AKITAIOEXP,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= max7310_attach_adapter,
	.detach_client	= max7310_detach_client,
};

static struct i2c_client max7310_template = {
	name:   "akita-max7310",
	flags:  I2C_CLIENT_ALLOW_USE,
	driver: &max7310_i2c_driver,
};

void akita_set_ioexp(struct device *dev, unsigned char bit)
{
	ioexp_output_value |= bit;

	if (akita_ioexp_device)
		schedule_work(&akita_ioexp);
	return;
}

void akita_reset_ioexp(struct device *dev, unsigned char bit)
{
	ioexp_output_value &= ~bit;

	if (akita_ioexp_device)
		schedule_work(&akita_ioexp);
	return;
}

EXPORT_SYMBOL(akita_set_ioexp);
EXPORT_SYMBOL(akita_reset_ioexp);

static void akita_ioexp_work(void *private_)
{
	if (akita_ioexp_device)
		max7310_set_ouputs(akita_ioexp_device, ioexp_output_value);
}


#ifdef CONFIG_PM
static int akita_ioexp_suspend(struct platform_device *pdev, pm_message_t state)
{
	flush_scheduled_work();
	return 0;
}

static int akita_ioexp_resume(struct platform_device *pdev)
{
	schedule_work(&akita_ioexp);
	return 0;
}
#else
#define akita_ioexp_suspend NULL
#define akita_ioexp_resume NULL
#endif

static int __init akita_ioexp_probe(struct platform_device *pdev)
{
	return i2c_add_driver(&max7310_i2c_driver);
}

static int akita_ioexp_remove(struct platform_device *pdev)
{
	i2c_del_driver(&max7310_i2c_driver);
	return 0;
}

static struct platform_driver akita_ioexp_driver = {
	.probe		= akita_ioexp_probe,
	.remove		= akita_ioexp_remove,
	.suspend	= akita_ioexp_suspend,
	.resume		= akita_ioexp_resume,
	.driver		= {
		.name	= "akita-ioexp",
	},
};

static int __init akita_ioexp_init(void)
{
	return platform_driver_register(&akita_ioexp_driver);
}

static void __exit akita_ioexp_exit(void)
{
	platform_driver_unregister(&akita_ioexp_driver);
}

MODULE_AUTHOR("Richard Purdie <rpurdie@openedhand.com>");
MODULE_DESCRIPTION("Akita IO-Expander driver");
MODULE_LICENSE("GPL");

fs_initcall(akita_ioexp_init);
module_exit(akita_ioexp_exit);


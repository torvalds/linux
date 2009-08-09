/*
 * drivers/media/radio/si470x/radio-si470x-i2c.c
 *
 * I2C driver for radios with Silicon Labs Si470x FM Radio Receivers
 *
 * Copyright (C) 2009 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *
 * TODO:
 * - RDS support
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#include "radio-si470x.h"

#define DRIVER_KERNEL_VERSION	KERNEL_VERSION(1, 0, 0)
#define DRIVER_CARD		"Silicon Labs Si470x FM Radio Receiver"
#define DRIVER_VERSION		"1.0.0"

/* starting with the upper byte of register 0x0a */
#define READ_REG_NUM		RADIO_REGISTER_NUM
#define READ_INDEX(i)		((i + RADIO_REGISTER_NUM - 0x0a) % READ_REG_NUM)

static int si470x_get_all_registers(struct si470x_device *radio)
{
	int i;
	u16 buf[READ_REG_NUM];
	struct i2c_msg msgs[1] = {
		{ radio->client->addr, I2C_M_RD, sizeof(u16) * READ_REG_NUM,
			(void *)buf },
	};

	if (i2c_transfer(radio->client->adapter, msgs, 1) != 1)
		return -EIO;

	for (i = 0; i < READ_REG_NUM; i++)
		radio->registers[i] = __be16_to_cpu(buf[READ_INDEX(i)]);

	return 0;
}

int si470x_get_register(struct si470x_device *radio, int regnr)
{
	u16 buf[READ_REG_NUM];
	struct i2c_msg msgs[1] = {
		{ radio->client->addr, I2C_M_RD, sizeof(u16) * READ_REG_NUM,
			(void *)buf },
	};

	if (i2c_transfer(radio->client->adapter, msgs, 1) != 1)
		return -EIO;

	radio->registers[regnr] = __be16_to_cpu(buf[READ_INDEX(regnr)]);

	return 0;
}

/* starting with the upper byte of register 0x02h */
#define WRITE_REG_NUM		8
#define WRITE_INDEX(i)		(i + 0x02)

int si470x_set_register(struct si470x_device *radio, int regnr)
{
	int i;
	u16 buf[WRITE_REG_NUM];
	struct i2c_msg msgs[1] = {
		{ radio->client->addr, 0, sizeof(u16) * WRITE_REG_NUM,
			(void *)buf },
	};

	for (i = 0; i < WRITE_REG_NUM; i++)
		buf[i] = __cpu_to_be16(radio->registers[WRITE_INDEX(i)]);

	if (i2c_transfer(radio->client->adapter, msgs, 1) != 1)
		return -EIO;

	return 0;
}

int si470x_disconnect_check(struct si470x_device *radio)
{
	return 0;
}

static int si470x_fops_open(struct file *file)
{
	struct si470x_device *radio = video_drvdata(file);
	int retval = 0;

	mutex_lock(&radio->lock);
	radio->users++;

	if (radio->users == 1)
		/* start radio */
		retval = si470x_start(radio);
	mutex_unlock(&radio->lock);

	return retval;
}

static int si470x_fops_release(struct file *file)
{
	struct si470x_device *radio = video_drvdata(file);
	int retval = 0;

	/* safety check */
	if (!radio)
		return -ENODEV;

	mutex_lock(&radio->lock);
	radio->users--;
	if (radio->users == 0)
		/* stop radio */
		retval = si470x_stop(radio);
	mutex_unlock(&radio->lock);

	return retval;
}

const struct v4l2_file_operations si470x_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= video_ioctl2,
	.open		= si470x_fops_open,
	.release	= si470x_fops_release,
};

int si470x_vidioc_querycap(struct file *file, void *priv,
		struct v4l2_capability *capability)
{
	strlcpy(capability->driver, DRIVER_NAME, sizeof(capability->driver));
	strlcpy(capability->card, DRIVER_CARD, sizeof(capability->card));
	capability->version = DRIVER_KERNEL_VERSION;
	capability->capabilities = V4L2_CAP_HW_FREQ_SEEK |
		V4L2_CAP_TUNER | V4L2_CAP_RADIO;

	return 0;
}

static int __devinit si470x_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct si470x_device *radio;
	int retval = 0;

	/* private data allocation and initialization */
	radio = kzalloc(sizeof(struct si470x_device), GFP_KERNEL);
	if (!radio) {
		retval = -ENOMEM;
		goto err_initial;
	}
	radio->client = client;
	radio->users = 0;
	mutex_init(&radio->lock);

	/* video device allocation and initialization */
	radio->videodev = video_device_alloc();
	if (!radio->videodev) {
		retval = -ENOMEM;
		goto err_radio;
	}
	memcpy(radio->videodev, &si470x_viddev_template,
			sizeof(si470x_viddev_template));
	video_set_drvdata(radio->videodev, radio);

	/* power up : need 110ms */
	radio->registers[POWERCFG] = POWERCFG_ENABLE;
	if (si470x_set_register(radio, POWERCFG) < 0) {
		retval = -EIO;
		goto err_all;
	}
	msleep(110);

	/* show some infos about the specific si470x device */
	if (si470x_get_all_registers(radio) < 0) {
		retval = -EIO;
		goto err_radio;
	}
	dev_info(&client->dev, "DeviceID=0x%4.4hx ChipID=0x%4.4hx\n",
			radio->registers[DEVICEID], radio->registers[CHIPID]);

	/* set initial frequency */
	si470x_set_freq(radio, 87.5 * FREQ_MUL); /* available in all regions */

	/* register video device */
	retval = video_register_device(radio->videodev, VFL_TYPE_RADIO, -1);
	if (retval) {
		dev_warn(&client->dev, "Could not register video device\n");
		goto err_all;
	}

	i2c_set_clientdata(client, radio);

	return 0;
err_all:
	video_device_release(radio->videodev);
err_radio:
	kfree(radio);
err_initial:
	return retval;
}

static __devexit int si470x_i2c_remove(struct i2c_client *client)
{
	struct si470x_device *radio = i2c_get_clientdata(client);

	video_unregister_device(radio->videodev);
	kfree(radio);
	i2c_set_clientdata(client, NULL);

	return 0;
}

static const struct i2c_device_id si470x_i2c_id[] = {
	{ "si470x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si470x_i2c_id);

static struct i2c_driver si470x_i2c_driver = {
	.driver = {
		.name = "si470x",
		.owner = THIS_MODULE,
	},
	.probe = si470x_i2c_probe,
	.remove = __devexit_p(si470x_i2c_remove),
	.id_table = si470x_i2c_id,
};

static int __init si470x_i2c_init(void)
{
	return i2c_add_driver(&si470x_i2c_driver);
}
module_init(si470x_i2c_init);

static void __exit si470x_i2c_exit(void)
{
	i2c_del_driver(&si470x_i2c_driver);
}
module_exit(si470x_i2c_exit);

MODULE_DESCRIPTION("i2c radio driver for si470x fm radio receivers");
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_LICENSE("GPL");

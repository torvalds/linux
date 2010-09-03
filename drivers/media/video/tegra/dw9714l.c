/*
 * DW9714L focuser driver.
 *
 * Copyright (C) 2010 Motorola Inc.
 *
 * Contributors:
 *      Andrei Warkentin <andreiw@motorola.com>
 *
 * Based on ov5650.c.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <media/dw9714l.h>

#define DW9714L_MAX_RETRIES (3)

#define POS_PER_STEP (3)

struct dw9714l_info {
	struct i2c_client *i2c_client;
	struct regulator *regulator;
};

static int dw9714l_write(struct i2c_client *client, u16 value)
{
	int count;
	struct i2c_msg msg[1];
	unsigned char data[2];
	int retry = 0;

	if (!client->adapter)
		return -ENODEV;

	data[0] = (u8) (value >> 8);
	data[1] = (u8) (value & 0xFF);

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = ARRAY_SIZE(data);
	msg[0].buf = data;

	do {
		count = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
		if (count == ARRAY_SIZE(msg))
			return 0;
		retry++;
		pr_err("dw9714l: i2c transfer failed, retrying %x\n",
		       value);
		msleep(3);
	} while (retry <= DW9714L_MAX_RETRIES);
	return -EIO;
}

static int dw9714l_set_position(struct dw9714l_info *info, u32 position)
{
	int ret;

	/* Protection off. */
	ret = dw9714l_write(info->i2c_client, 0xECA3);
	if (ret)
		return ret;

	ret = dw9714l_write(info->i2c_client, 0xF200 | (0x0F << 3));
	if (ret)
		return ret;

	/* Protection on. */
	ret = dw9714l_write(info->i2c_client, 0xDC51);
	if (ret)
		return ret;

	ret = dw9714l_write(info->i2c_client,
			    (position << 4) |
			    (POS_PER_STEP << 2));
	if (ret)
		return ret;

	return 0;
}

static int dw9714l_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct dw9714l_info *info = file->private_data;

	switch (cmd) {
	case DW9714L_IOCTL_SET_POSITION:
		return dw9714l_set_position(info, (u32) arg);
	default:
		return -EINVAL;
	}
	return 0;
}

struct dw9714l_info *info = NULL;

static int dw9714l_open(struct inode *inode, struct file *file)
{
	u8 status;

	pr_info("%s\n", __func__);
	file->private_data = info;
	if (info->regulator)
		regulator_enable(info->regulator);
	return 0;
}

int dw9714l_release(struct inode *inode, struct file *file)
{
	pr_info("%s\n", __func__);
	if (info->regulator)
		regulator_disable(info->regulator);
	file->private_data = NULL;
	return 0;
}


static const struct file_operations dw9714l_fileops = {
	.owner = THIS_MODULE,
	.open = dw9714l_open,
	.ioctl = dw9714l_ioctl,
	.release = dw9714l_release,
};

static struct miscdevice dw9714l_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "dw9714l",
	.fops = &dw9714l_fileops,
};

static int dw9714l_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;

	pr_info("dw9714l: probing sensor.\n");

	info = kzalloc(sizeof(struct dw9714l_info), GFP_KERNEL);
	if (!info) {
		pr_err("dw9714l: Unable to allocate memory!\n");
		return -ENOMEM;
	}

	err = misc_register(&dw9714l_device);
	if (err) {
		pr_err("dw9714l: Unable to register misc device!\n");
		kfree(info);
		return err;
	}

	info->regulator = regulator_get(&client->dev, "vcc");
	if (IS_ERR_OR_NULL(info->regulator)) {
		dev_err(&client->dev, "unable to get regulator %s\n",
			dev_name(&client->dev));
		info->regulator = NULL;
	} else {
		regulator_enable(info->regulator);
	}

	info->i2c_client = client;
	i2c_set_clientdata(client, info);
	return 0;
}

static int dw9714l_remove(struct i2c_client *client)
{
	struct dw9714l_info *info;
	info = i2c_get_clientdata(client);
	misc_deregister(&dw9714l_device);
	kfree(info);
	return 0;
}

static const struct i2c_device_id dw9714l_id[] = {
	{ "dw9714l", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, dw9714l_id);

static struct i2c_driver dw9714l_i2c_driver = {
	.driver = {
		.name = "dw9714l",
		.owner = THIS_MODULE,
	},
	.probe = dw9714l_probe,
	.remove = dw9714l_remove,
	.id_table = dw9714l_id,
};

static int __init dw9714l_init(void)
{
	pr_info("dw9714l sensor driver loading\n");
	return i2c_add_driver(&dw9714l_i2c_driver);
}

static void __exit dw9714l_exit(void)
{
	i2c_del_driver(&dw9714l_i2c_driver);
}

module_init(dw9714l_init);
module_exit(dw9714l_exit);


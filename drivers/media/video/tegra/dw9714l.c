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
#include <linux/uaccess.h>
#include <media/dw9714l.h>

#define POS_LOW (144)
#define POS_HIGH (520)
#define SETTLETIME_MS (50)
#define FOCAL_LENGTH (4.42f)
#define FNUMBER (2.8f)
#define DEFAULT_MODE (MODE_LSC);

#define PROT_OFF (0xECA3)
#define PROT_ON (0xDC51)
#define DLC_MCLK (0x2)
#define DLC_TSRC (0x17)
#define LSC_MCLK (0x1)
#define LSC_S10 (0x3)
#define LSC_S32 (0x3)
#define LSC_TSRC (0x3)
#define LSC_GOAL(pos) ((pos << 4) | (LSC_S32 << 2) | LSC_S10)
#define GOAL(pos) (pos << 4)

#define DW9714L_MAX_RETRIES (3)

struct dw9714l_info {
	struct i2c_client *i2c_client;
	struct regulator *regulator;
	struct dw9714l_config config;
};

static u16 dlc_pre_set_pos[] =
{
	PROT_OFF,
	0xA10C | DLC_MCLK,
	0xF200 | (DLC_TSRC << 3),
	PROT_ON
};

static u16 lsc_pre_set_pos[] =
{
	PROT_OFF,
	0xA104 | LSC_MCLK,
	0xF200 | (LSC_TSRC << 3),
	PROT_ON
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

static int dw9714l_write_many(struct i2c_client *client,
			      u16 *values,
			      size_t count)
{
	int ix = 0;
	int ret = 0;
	while (ix < count && ret == 0)
		ret = dw9714l_write(client, values[ix++]);

	return ret;
}

static int dw9714l_set_position(struct dw9714l_info *info, u32 position)
{
	int ret;

	if (position < info->config.pos_low ||
	    position > info->config.pos_high)
		return -EINVAL;

	/*
	  As we calibrate the focuser, we might go back and forth on
	  the actual mode of setting the position. To
	  make this least painful, we'll make the mode a settable
	  parameter exposed to the focuser HAL.
	 */

	switch(info->config.mode)
	{
	case MODE_LSC:
		ret = dw9714l_write_many(info->i2c_client,
					 lsc_pre_set_pos,
					 ARRAY_SIZE(lsc_pre_set_pos));

		if (ret)
			return ret;

		ret = dw9714l_write(info->i2c_client,
				    LSC_GOAL(position));

		break;
	case MODE_DLC:
		ret = dw9714l_write_many(info->i2c_client,
					 dlc_pre_set_pos,
					 ARRAY_SIZE(dlc_pre_set_pos));

		if (ret)
			return ret;

		/* Fall through */
	case MODE_DIRECT:
		ret = dw9714l_write(info->i2c_client,
				    GOAL(position));
		break;
	case MODE_INVALID:
	default:
		WARN_ON(info->config.mode);
	}

	return ret;
}

static long dw9714l_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct dw9714l_info *info = file->private_data;

	switch (cmd) {
	case DW9714L_IOCTL_GET_CONFIG:
	{
		if (copy_to_user((void __user *) arg,
				 &info->config,
				 sizeof(info->config))) {
			pr_err("%s: 0x%x\n", __func__, __LINE__);
			return -EFAULT;
		}

		break;
	}
	case DW9714L_IOCTL_SET_CAL:
	{
		struct dw9714l_cal cal;
		if (copy_from_user(&cal,
				   (const void __user *) arg,
				   sizeof(cal))) {
			pr_err("%s: 0x%x\n", __func__, __LINE__);
			return -EFAULT;
		}

		if (cal.mode >= MODE_INVALID)
			return -EINVAL;

		info->config.mode = cal.mode;
		break;
	}
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

	file->private_data = info;
	if (info->regulator)
		regulator_enable(info->regulator);
	return 0;
}

int dw9714l_release(struct inode *inode, struct file *file)
{
	if (info->regulator)
		regulator_disable(info->regulator);
	file->private_data = NULL;
	return 0;
}


static const struct file_operations dw9714l_fileops = {
	.owner = THIS_MODULE,
	.open = dw9714l_open,
	.unlocked_ioctl = dw9714l_ioctl,
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
	info->config.settle_time = SETTLETIME_MS;
	info->config.focal_length = FOCAL_LENGTH;
	info->config.fnumber = FNUMBER;
	info->config.pos_low = POS_LOW;
	info->config.pos_high = POS_HIGH;
	info->config.mode = DEFAULT_MODE;
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


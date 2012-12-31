/*
* ar0832_focuser.c - focuser driver
*
* Copyright (c) 2011, NVIDIA, All Rights Reserved.
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
#include <media/ar0832_focuser.h>


#define POS_LOW 50
#define POS_HIGH 1000
#define SETTLETIME_MS 100
#define FOCAL_LENGTH (3.5f)
#define FNUMBER (2.8f)
#define FPOS_COUNT 1024
DEFINE_MUTEX(star_focuser_lock);
#define DW9716_MAX_RETRIES (3)

static int ar0832_focuser_write(struct i2c_client *client, u16 value)
{
	int count;
	struct i2c_msg msg[1];
	unsigned char data[2];
	int retry = 0;

	if (!client->adapter)
		return -ENODEV;

	data[0] = (u8) ((value >> 4) & 0x3F);
	data[1] = (u8) ((value & 0xF) << 4);
	/* Slew rate control (8 steps, 50us) */
	data[1] = (data[1] & 0xF0) | 0x05;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = ARRAY_SIZE(data);
	msg[0].buf = data;

	do {
		count = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
		if (count == ARRAY_SIZE(msg))
			return 0;
		retry++;
		pr_err("ar0832_focuser: i2c transfer failed, retrying %x\n",
				value);
		usleep_range(3000, 3500);
	} while (retry <= DW9716_MAX_RETRIES);
	return -EIO;
}

static int ar0832_focuser_write_helper(
			struct ar0832_focuser_info *info, u16 value)
{
	int ret;
	switch (info->camera_mode) {
	case MAIN:
	case LEFT_ONLY:
		ret = ar0832_focuser_write(info->i2c_client,  value);
		break;
	case STEREO:
		ret = ar0832_focuser_write(info->i2c_client,  value);
		ret = ar0832_focuser_write(info->i2c_client_right,  value);
		break;
	case RIGHT_ONLY:
		ret = ar0832_focuser_write(info->i2c_client_right,  value);
		break;
	default:
		return -1;
	}
	return ret;
}
static int ar0832_focuser_set_position(
		struct ar0832_focuser_info *info, u32 position)
{
	if (position < info->config.pos_low ||
		position > info->config.pos_high)
		return -EINVAL;

	return ar0832_focuser_write(info->i2c_client, position);
}

static long ar0832_focuser_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct ar0832_focuser_info *info = file->private_data;
	int ret;
	switch (cmd) {
	case AR0832_FOCUSER_IOCTL_GET_CONFIG:
	{
		if (copy_to_user((void __user *) arg,
				 &info->config,
				 sizeof(info->config))) {
			pr_err("%s: 0x%x\n", __func__, __LINE__);
			return -EFAULT;
		}

		break;
	}
	case AR0832_FOCUSER_IOCTL_SET_POSITION:
		mutex_lock(&star_focuser_lock);
		ret = ar0832_focuser_set_position(info, (u32) arg);
		mutex_unlock(&star_focuser_lock);
		return ret;
	case AR0832_FOCUSER_IOCTL_SET_MODE:
		info->camera_mode = (enum StereoCameraMode)arg;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct ar0832_focuser_info *info;

static int ar0832_focuser_open(struct inode *inode, struct file *file)
{
	pr_info("ar0832_focuser: open!\n");
	file->private_data = info;
	return 0;
}

int ar0832_focuser_release(struct inode *inode, struct file *file)
{
	pr_info("ar0832_focuser: release!\n");
	file->private_data = NULL;
	return 0;
}


static const struct file_operations ar0832_focuser_fileops = {
	.owner = THIS_MODULE,
	.open = ar0832_focuser_open,
	.unlocked_ioctl = ar0832_focuser_ioctl,
	.release = ar0832_focuser_release,
};

static struct miscdevice ar0832_focuser_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ar0832_focuser",
	.fops = &ar0832_focuser_fileops,
};

static int ar0832_focuser_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;

	pr_info("ar0832_focuser: probing sensor.\n");

	info = kzalloc(sizeof(struct ar0832_focuser_info), GFP_KERNEL);
	if (!info) {
		pr_err("ar0832_focuser: Unable to allocate memory!\n");
		return -ENOMEM;
	}

	err = misc_register(&ar0832_focuser_device);
	if (err) {
		pr_err("ar0832_focuser: Unable to register misc device!\n");
		kfree(info);
		return err;
	}

	info->regulator = 0;
	info->i2c_client = client;
	info->config.settle_time = SETTLETIME_MS;
	/* FIX-ME */
	/*
	focuser_info->config.focal_length = FOCAL_LENGTH;
	focuser_info->config.fnumber = FNUMBER;
	*/
	info->config.pos_low = POS_LOW;
	info->config.pos_high = POS_HIGH;
	i2c_set_clientdata(client, info);

	return 0;
}

static int ar0832_focuser_remove(struct i2c_client *client)
{
	struct ar0832_focuser_info *info;
	info = i2c_get_clientdata(client);
	misc_deregister(&ar0832_focuser_device);
	kfree(info);
	return 0;
}

static const struct i2c_device_id ar0832_focuser_id[] = {
	{ "ar0832_focuser", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, ar0832_focuser_id);

static struct i2c_driver ar0832_focuser_i2c_driver = {
	.driver = {
		.name = "ar0832_focuser",
		.owner = THIS_MODULE,
	},
	.probe = ar0832_focuser_probe,
	.remove = ar0832_focuser_remove,
	.id_table = ar0832_focuser_id,
};

static int __init ar0832_focuser_init(void)
{
	pr_info("ar0832_focuser sensor driver loading\n");
	i2c_add_driver(&ar0832_focuser_i2c_driver);

	return 0;
}

static void __exit ar0832_focuser_exit(void)
{
	i2c_del_driver(&ar0832_focuser_i2c_driver);
}

module_init(ar0832_focuser_init);
module_exit(ar0832_focuser_exit);

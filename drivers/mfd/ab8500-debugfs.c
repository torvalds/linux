/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Mattias Wallin <mattias.wallin@stericsson.com> for ST-Ericsson.
 * License Terms: GNU General Public License v2
 */

#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>

#include <linux/mfd/abx500.h>
#include <linux/mfd/ab8500.h>

static u32 debug_bank;
static u32 debug_address;

/**
 * struct ab8500_reg_range
 * @first: the first address of the range
 * @last: the last address of the range
 * @perm: access permissions for the range
 */
struct ab8500_reg_range {
	u8 first;
	u8 last;
	u8 perm;
};

/**
 * struct ab8500_i2c_ranges
 * @num_ranges: the number of ranges in the list
 * @bankid: bank identifier
 * @range: the list of register ranges
 */
struct ab8500_i2c_ranges {
	u8 num_ranges;
	u8 bankid;
	const struct ab8500_reg_range *range;
};

#define AB8500_NAME_STRING "ab8500"
#define AB8500_NUM_BANKS 22

#define AB8500_REV_REG 0x80

static struct ab8500_i2c_ranges debug_ranges[AB8500_NUM_BANKS] = {
	[0x0] = {
		.num_ranges = 0,
		.range = 0,
	},
	[AB8500_SYS_CTRL1_BLOCK] = {
		.num_ranges = 3,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x02,
			},
			{
				.first = 0x42,
				.last = 0x42,
			},
			{
				.first = 0x80,
				.last = 0x81,
			},
		},
	},
	[AB8500_SYS_CTRL2_BLOCK] = {
		.num_ranges = 4,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x0D,
			},
			{
				.first = 0x0F,
				.last = 0x17,
			},
			{
				.first = 0x30,
				.last = 0x30,
			},
			{
				.first = 0x32,
				.last = 0x33,
			},
		},
	},
	[AB8500_REGU_CTRL1] = {
		.num_ranges = 3,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x00,
			},
			{
				.first = 0x03,
				.last = 0x10,
			},
			{
				.first = 0x80,
				.last = 0x84,
			},
		},
	},
	[AB8500_REGU_CTRL2] = {
		.num_ranges = 5,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x15,
			},
			{
				.first = 0x17,
				.last = 0x19,
			},
			{
				.first = 0x1B,
				.last = 0x1D,
			},
			{
				.first = 0x1F,
				.last = 0x22,
			},
			{
				.first = 0x40,
				.last = 0x44,
			},
			/* 0x80-0x8B is SIM registers and should
			 * not be accessed from here */
		},
	},
	[AB8500_USB] = {
		.num_ranges = 2,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x80,
				.last = 0x83,
			},
			{
				.first = 0x87,
				.last = 0x8A,
			},
		},
	},
	[AB8500_TVOUT] = {
		.num_ranges = 9,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x12,
			},
			{
				.first = 0x15,
				.last = 0x17,
			},
			{
				.first = 0x19,
				.last = 0x21,
			},
			{
				.first = 0x27,
				.last = 0x2C,
			},
			{
				.first = 0x41,
				.last = 0x41,
			},
			{
				.first = 0x45,
				.last = 0x5B,
			},
			{
				.first = 0x5D,
				.last = 0x5D,
			},
			{
				.first = 0x69,
				.last = 0x69,
			},
			{
				.first = 0x80,
				.last = 0x81,
			},
		},
	},
	[AB8500_DBI] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_ECI_AV_ACC] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x80,
				.last = 0x82,
			},
		},
	},
	[0x9] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_GPADC] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x08,
			},
		},
	},
	[AB8500_CHARGER] = {
		.num_ranges = 8,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x03,
			},
			{
				.first = 0x05,
				.last = 0x05,
			},
			{
				.first = 0x40,
				.last = 0x40,
			},
			{
				.first = 0x42,
				.last = 0x42,
			},
			{
				.first = 0x44,
				.last = 0x44,
			},
			{
				.first = 0x50,
				.last = 0x55,
			},
			{
				.first = 0x80,
				.last = 0x82,
			},
			{
				.first = 0xC0,
				.last = 0xC2,
			},
		},
	},
	[AB8500_GAS_GAUGE] = {
		.num_ranges = 3,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x00,
			},
			{
				.first = 0x07,
				.last = 0x0A,
			},
			{
				.first = 0x10,
				.last = 0x14,
			},
		},
	},
	[AB8500_AUDIO] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x6F,
			},
		},
	},
	[AB8500_INTERRUPT] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_RTC] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x0F,
			},
		},
	},
	[AB8500_MISC] = {
		.num_ranges = 8,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x05,
			},
			{
				.first = 0x10,
				.last = 0x15,
			},
			{
				.first = 0x20,
				.last = 0x25,
			},
			{
				.first = 0x30,
				.last = 0x35,
			},
			{
				.first = 0x40,
				.last = 0x45,
			},
			{
				.first = 0x50,
				.last = 0x50,
			},
			{
				.first = 0x60,
				.last = 0x67,
			},
			{
				.first = 0x80,
				.last = 0x80,
			},
		},
	},
	[0x11] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[0x12] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[0x13] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[0x14] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_OTP_EMUL] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x01,
				.last = 0x0F,
			},
		},
	},
};

static int ab8500_registers_print(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	unsigned int i;
	u32 bank = debug_bank;

	seq_printf(s, AB8500_NAME_STRING " register values:\n");

	seq_printf(s, " bank %u:\n", bank);
	for (i = 0; i < debug_ranges[bank].num_ranges; i++) {
		u32 reg;

		for (reg = debug_ranges[bank].range[i].first;
			reg <= debug_ranges[bank].range[i].last;
			reg++) {
			u8 value;
			int err;

			err = abx500_get_register_interruptible(dev,
				(u8)bank, (u8)reg, &value);
			if (err < 0) {
				dev_err(dev, "ab->read fail %d\n", err);
				return err;
			}

			err = seq_printf(s, "  [%u/0x%02X]: 0x%02X\n", bank,
				reg, value);
			if (err < 0) {
				dev_err(dev, "seq_printf overflow\n");
				/* Error is not returned here since
				 * the output is wanted in any case */
				return 0;
			}
		}
	}
	return 0;
}

static int ab8500_registers_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_registers_print, inode->i_private);
}

static const struct file_operations ab8500_registers_fops = {
	.open = ab8500_registers_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_bank_print(struct seq_file *s, void *p)
{
	return seq_printf(s, "%d\n", debug_bank);
}

static int ab8500_bank_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_bank_print, inode->i_private);
}

static ssize_t ab8500_bank_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct device *dev = ((struct seq_file *)(file->private_data))->private;
	unsigned long user_bank;
	int err;

	/* Get userspace string and assure termination */
	err = kstrtoul_from_user(user_buf, count, 0, &user_bank);
	if (err)
		return err;

	if (user_bank >= AB8500_NUM_BANKS) {
		dev_err(dev, "debugfs error input > number of banks\n");
		return -EINVAL;
	}

	debug_bank = user_bank;

	return count;
}

static int ab8500_address_print(struct seq_file *s, void *p)
{
	return seq_printf(s, "0x%02X\n", debug_address);
}

static int ab8500_address_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_address_print, inode->i_private);
}

static ssize_t ab8500_address_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct device *dev = ((struct seq_file *)(file->private_data))->private;
	unsigned long user_address;
	int err;

	/* Get userspace string and assure termination */
	err = kstrtoul_from_user(user_buf, count, 0, &user_address);
	if (err)
		return err;

	if (user_address > 0xff) {
		dev_err(dev, "debugfs error input > 0xff\n");
		return -EINVAL;
	}
	debug_address = user_address;
	return count;
}

static int ab8500_val_print(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	int ret;
	u8 regvalue;

	ret = abx500_get_register_interruptible(dev,
		(u8)debug_bank, (u8)debug_address, &regvalue);
	if (ret < 0) {
		dev_err(dev, "abx500_get_reg fail %d, %d\n",
			ret, __LINE__);
		return -EINVAL;
	}
	seq_printf(s, "0x%02X\n", regvalue);

	return 0;
}

static int ab8500_val_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_val_print, inode->i_private);
}

static ssize_t ab8500_val_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct device *dev = ((struct seq_file *)(file->private_data))->private;
	unsigned long user_val;
	int err;

	/* Get userspace string and assure termination */
	err = kstrtoul_from_user(user_buf, count, 0, &user_val);
	if (err)
		return err;

	if (user_val > 0xff) {
		dev_err(dev, "debugfs error input > 0xff\n");
		return -EINVAL;
	}
	err = abx500_set_register_interruptible(dev,
		(u8)debug_bank, debug_address, (u8)user_val);
	if (err < 0) {
		printk(KERN_ERR "abx500_set_reg failed %d, %d", err, __LINE__);
		return -EINVAL;
	}

	return count;
}

static const struct file_operations ab8500_bank_fops = {
	.open = ab8500_bank_open,
	.write = ab8500_bank_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ab8500_address_fops = {
	.open = ab8500_address_open,
	.write = ab8500_address_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ab8500_val_fops = {
	.open = ab8500_val_open,
	.write = ab8500_val_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct dentry *ab8500_dir;
static struct dentry *ab8500_reg_file;
static struct dentry *ab8500_bank_file;
static struct dentry *ab8500_address_file;
static struct dentry *ab8500_val_file;

static int __devinit ab8500_debug_probe(struct platform_device *plf)
{
	debug_bank = AB8500_MISC;
	debug_address = AB8500_REV_REG & 0x00FF;

	ab8500_dir = debugfs_create_dir(AB8500_NAME_STRING, NULL);
	if (!ab8500_dir)
		goto exit_no_debugfs;

	ab8500_reg_file = debugfs_create_file("all-bank-registers",
		S_IRUGO, ab8500_dir, &plf->dev, &ab8500_registers_fops);
	if (!ab8500_reg_file)
		goto exit_destroy_dir;

	ab8500_bank_file = debugfs_create_file("register-bank",
		(S_IRUGO | S_IWUSR), ab8500_dir, &plf->dev, &ab8500_bank_fops);
	if (!ab8500_bank_file)
		goto exit_destroy_reg;

	ab8500_address_file = debugfs_create_file("register-address",
		(S_IRUGO | S_IWUSR), ab8500_dir, &plf->dev,
		&ab8500_address_fops);
	if (!ab8500_address_file)
		goto exit_destroy_bank;

	ab8500_val_file = debugfs_create_file("register-value",
		(S_IRUGO | S_IWUSR), ab8500_dir, &plf->dev, &ab8500_val_fops);
	if (!ab8500_val_file)
		goto exit_destroy_address;

	return 0;

exit_destroy_address:
	debugfs_remove(ab8500_address_file);
exit_destroy_bank:
	debugfs_remove(ab8500_bank_file);
exit_destroy_reg:
	debugfs_remove(ab8500_reg_file);
exit_destroy_dir:
	debugfs_remove(ab8500_dir);
exit_no_debugfs:
	dev_err(&plf->dev, "failed to create debugfs entries.\n");
	return -ENOMEM;
}

static int __devexit ab8500_debug_remove(struct platform_device *plf)
{
	debugfs_remove(ab8500_val_file);
	debugfs_remove(ab8500_address_file);
	debugfs_remove(ab8500_bank_file);
	debugfs_remove(ab8500_reg_file);
	debugfs_remove(ab8500_dir);

	return 0;
}

static struct platform_driver ab8500_debug_driver = {
	.driver = {
		.name = "ab8500-debug",
		.owner = THIS_MODULE,
	},
	.probe  = ab8500_debug_probe,
	.remove = __devexit_p(ab8500_debug_remove)
};

static int __init ab8500_debug_init(void)
{
	return platform_driver_register(&ab8500_debug_driver);
}

static void __exit ab8500_debug_exit(void)
{
	platform_driver_unregister(&ab8500_debug_driver);
}
subsys_initcall(ab8500_debug_init);
module_exit(ab8500_debug_exit);

MODULE_AUTHOR("Mattias WALLIN <mattias.wallin@stericsson.com");
MODULE_DESCRIPTION("AB8500 DEBUG");
MODULE_LICENSE("GPL v2");

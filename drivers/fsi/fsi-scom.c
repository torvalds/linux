/*
 * SCOM FSI Client device driver
 *
 * Copyright (C) IBM Corporation 2016
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERGCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/fsi.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/idr.h>

#define FSI_ENGID_SCOM		0x5

#define SCOM_FSI2PIB_DELAY	50

/* SCOM engine register set */
#define SCOM_DATA0_REG		0x00
#define SCOM_DATA1_REG		0x04
#define SCOM_CMD_REG		0x08
#define SCOM_RESET_REG		0x1C

#define SCOM_RESET_CMD		0x80000000
#define SCOM_WRITE_CMD		0x80000000

struct scom_device {
	struct list_head link;
	struct fsi_device *fsi_dev;
	struct miscdevice mdev;
	char	name[32];
	int idx;
};

#define to_scom_dev(x)		container_of((x), struct scom_device, mdev)

static struct list_head scom_devices;

static DEFINE_IDA(scom_ida);

static int put_scom(struct scom_device *scom_dev, uint64_t value,
			uint32_t addr)
{
	int rc;
	uint32_t data;

	data = cpu_to_be32((value >> 32) & 0xffffffff);
	rc = fsi_device_write(scom_dev->fsi_dev, SCOM_DATA0_REG, &data,
				sizeof(uint32_t));
	if (rc)
		return rc;

	data = cpu_to_be32(value & 0xffffffff);
	rc = fsi_device_write(scom_dev->fsi_dev, SCOM_DATA1_REG, &data,
				sizeof(uint32_t));
	if (rc)
		return rc;

	data = cpu_to_be32(SCOM_WRITE_CMD | addr);
	return fsi_device_write(scom_dev->fsi_dev, SCOM_CMD_REG, &data,
				sizeof(uint32_t));
}

static int get_scom(struct scom_device *scom_dev, uint64_t *value,
			uint32_t addr)
{
	uint32_t result, data;
	int rc;

	*value = 0ULL;
	data = cpu_to_be32(addr);
	rc = fsi_device_write(scom_dev->fsi_dev, SCOM_CMD_REG, &data,
				sizeof(uint32_t));
	if (rc)
		return rc;

	rc = fsi_device_read(scom_dev->fsi_dev, SCOM_DATA0_REG, &result,
				sizeof(uint32_t));
	if (rc)
		return rc;

	*value |= (uint64_t)cpu_to_be32(result) << 32;
	rc = fsi_device_read(scom_dev->fsi_dev, SCOM_DATA1_REG, &result,
				sizeof(uint32_t));
	if (rc)
		return rc;

	*value |= cpu_to_be32(result);

	return 0;
}

static ssize_t scom_read(struct file *filep, char __user *buf, size_t len,
			loff_t *offset)
{
	int rc;
	struct miscdevice *mdev =
				(struct miscdevice *)filep->private_data;
	struct scom_device *scom = to_scom_dev(mdev);
	struct device *dev = &scom->fsi_dev->dev;
	uint64_t val;

	if (len != sizeof(uint64_t))
		return -EINVAL;

	rc = get_scom(scom, &val, *offset);
	if (rc) {
		dev_dbg(dev, "get_scom fail:%d\n", rc);
		return rc;
	}

	rc = copy_to_user(buf, &val, len);
	if (rc)
		dev_dbg(dev, "copy to user failed:%d\n", rc);

	return rc ? rc : len;
}

static ssize_t scom_write(struct file *filep, const char __user *buf,
			size_t len, loff_t *offset)
{
	int rc;
	struct miscdevice *mdev = filep->private_data;
	struct scom_device *scom = to_scom_dev(mdev);
	struct device *dev = &scom->fsi_dev->dev;
	uint64_t val;

	if (len != sizeof(uint64_t))
		return -EINVAL;

	rc = copy_from_user(&val, buf, len);
	if (rc) {
		dev_dbg(dev, "copy from user failed:%d\n", rc);
		return -EINVAL;
	}

	rc = put_scom(scom, val, *offset);
	if (rc) {
		dev_dbg(dev, "put_scom failed with:%d\n", rc);
		return rc;
	}

	return len;
}

static loff_t scom_llseek(struct file *file, loff_t offset, int whence)
{
	switch (whence) {
	case SEEK_CUR:
		break;
	case SEEK_SET:
		file->f_pos = offset;
		break;
	default:
		return -EINVAL;
	}

	return offset;
}

static const struct file_operations scom_fops = {
	.owner	= THIS_MODULE,
	.llseek	= scom_llseek,
	.read	= scom_read,
	.write	= scom_write,
};

static int scom_probe(struct device *dev)
{
	uint32_t data;
	struct fsi_device *fsi_dev = to_fsi_dev(dev);
	struct scom_device *scom;

	scom = devm_kzalloc(dev, sizeof(*scom), GFP_KERNEL);
	if (!scom)
		return -ENOMEM;

	scom->idx = ida_simple_get(&scom_ida, 1, INT_MAX, GFP_KERNEL);
	snprintf(scom->name, sizeof(scom->name), "scom%d", scom->idx);
	scom->fsi_dev = fsi_dev;
	scom->mdev.minor = MISC_DYNAMIC_MINOR;
	scom->mdev.fops = &scom_fops;
	scom->mdev.name = scom->name;
	scom->mdev.parent = dev;
	list_add(&scom->link, &scom_devices);

	data = cpu_to_be32(SCOM_RESET_CMD);
	fsi_device_write(fsi_dev, SCOM_RESET_REG, &data, sizeof(uint32_t));

	return misc_register(&scom->mdev);
}

static int scom_remove(struct device *dev)
{
	struct scom_device *scom, *scom_tmp;
	struct fsi_device *fsi_dev = to_fsi_dev(dev);

	list_for_each_entry_safe(scom, scom_tmp, &scom_devices, link) {
		if (scom->fsi_dev == fsi_dev) {
			list_del(&scom->link);
			ida_simple_remove(&scom_ida, scom->idx);
			misc_deregister(&scom->mdev);
		}
	}

	return 0;
}

static struct fsi_device_id scom_ids[] = {
	{
		.engine_type = FSI_ENGID_SCOM,
		.version = FSI_VERSION_ANY,
	},
	{ 0 }
};

static struct fsi_driver scom_drv = {
	.id_table = scom_ids,
	.drv = {
		.name = "scom",
		.bus = &fsi_bus_type,
		.probe = scom_probe,
		.remove = scom_remove,
	}
};

static int scom_init(void)
{
	INIT_LIST_HEAD(&scom_devices);
	return fsi_driver_register(&scom_drv);
}

static void scom_exit(void)
{
	struct list_head *pos;
	struct scom_device *scom;

	list_for_each(pos, &scom_devices) {
		scom = list_entry(pos, struct scom_device, link);
		misc_deregister(&scom->mdev);
		devm_kfree(&scom->fsi_dev->dev, scom);
	}
	fsi_driver_unregister(&scom_drv);
}

module_init(scom_init);
module_exit(scom_exit);
MODULE_LICENSE("GPL");

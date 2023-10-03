// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) IBM Corporation 2023 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/fsi.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>

#include "fsi-master-i2cr.h"
#include "fsi-slave.h"

struct i2cr_scom {
	struct device dev;
	struct cdev cdev;
	struct fsi_master_i2cr *i2cr;
};

static loff_t i2cr_scom_llseek(struct file *file, loff_t offset, int whence)
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

static ssize_t i2cr_scom_read(struct file *filep, char __user *buf, size_t len, loff_t *offset)
{
	struct i2cr_scom *scom = filep->private_data;
	u64 data;
	int ret;

	if (len != sizeof(data))
		return -EINVAL;

	ret = fsi_master_i2cr_read(scom->i2cr, (u32)*offset, &data);
	if (ret)
		return ret;

	ret = copy_to_user(buf, &data, len);
	if (ret)
		return ret;

	return len;
}

static ssize_t i2cr_scom_write(struct file *filep, const char __user *buf, size_t len,
			       loff_t *offset)
{
	struct i2cr_scom *scom = filep->private_data;
	u64 data;
	int ret;

	if (len != sizeof(data))
		return -EINVAL;

	ret = copy_from_user(&data, buf, len);
	if (ret)
		return ret;

	ret = fsi_master_i2cr_write(scom->i2cr, (u32)*offset, data);
	if (ret)
		return ret;

	return len;
}

static const struct file_operations i2cr_scom_fops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.llseek		= i2cr_scom_llseek,
	.read		= i2cr_scom_read,
	.write		= i2cr_scom_write,
};

static int i2cr_scom_probe(struct device *dev)
{
	struct fsi_device *fsi_dev = to_fsi_dev(dev);
	struct i2cr_scom *scom;
	int didx;
	int ret;

	if (!is_fsi_master_i2cr(fsi_dev->slave->master))
		return -ENODEV;

	scom = devm_kzalloc(dev, sizeof(*scom), GFP_KERNEL);
	if (!scom)
		return -ENOMEM;

	scom->i2cr = to_fsi_master_i2cr(fsi_dev->slave->master);
	dev_set_drvdata(dev, scom);

	scom->dev.type = &fsi_cdev_type;
	scom->dev.parent = dev;
	device_initialize(&scom->dev);

	ret = fsi_get_new_minor(fsi_dev, fsi_dev_scom, &scom->dev.devt, &didx);
	if (ret)
		return ret;

	dev_set_name(&scom->dev, "scom%d", didx);
	cdev_init(&scom->cdev, &i2cr_scom_fops);
	ret = cdev_device_add(&scom->cdev, &scom->dev);
	if (ret)
		fsi_free_minor(scom->dev.devt);

	return ret;
}

static int i2cr_scom_remove(struct device *dev)
{
	struct i2cr_scom *scom = dev_get_drvdata(dev);

	cdev_device_del(&scom->cdev, &scom->dev);
	fsi_free_minor(scom->dev.devt);

	return 0;
}

static const struct of_device_id i2cr_scom_of_ids[] = {
	{ .compatible = "ibm,i2cr-scom" },
	{ }
};
MODULE_DEVICE_TABLE(of, i2cr_scom_of_ids);

static const struct fsi_device_id i2cr_scom_ids[] = {
	{ 0x5, FSI_VERSION_ANY },
	{ }
};

static struct fsi_driver i2cr_scom_driver = {
	.id_table = i2cr_scom_ids,
	.drv = {
		.name = "i2cr_scom",
		.bus = &fsi_bus_type,
		.of_match_table = i2cr_scom_of_ids,
		.probe = i2cr_scom_probe,
		.remove = i2cr_scom_remove,
	}
};

module_fsi_driver(i2cr_scom_driver);

MODULE_AUTHOR("Eddie James <eajames@linux.ibm.com>");
MODULE_DESCRIPTION("IBM I2C Responder SCOM driver");
MODULE_LICENSE("GPL");

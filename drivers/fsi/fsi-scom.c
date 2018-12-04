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
#include <linux/list.h>

#include <uapi/linux/fsi.h>

#define FSI_ENGID_SCOM		0x5

/* SCOM engine register set */
#define SCOM_DATA0_REG		0x00
#define SCOM_DATA1_REG		0x04
#define SCOM_CMD_REG		0x08
#define SCOM_FSI2PIB_RESET_REG	0x18
#define SCOM_STATUS_REG		0x1C /* Read */
#define SCOM_PIB_RESET_REG	0x1C /* Write */

/* Command register */
#define SCOM_WRITE_CMD		0x80000000
#define SCOM_READ_CMD		0x00000000

/* Status register bits */
#define SCOM_STATUS_ERR_SUMMARY		0x80000000
#define SCOM_STATUS_PROTECTION		0x01000000
#define SCOM_STATUS_PARITY		0x04000000
#define SCOM_STATUS_PIB_ABORT		0x00100000
#define SCOM_STATUS_PIB_RESP_MASK	0x00007000
#define SCOM_STATUS_PIB_RESP_SHIFT	12

#define SCOM_STATUS_ANY_ERR		(SCOM_STATUS_ERR_SUMMARY | \
					 SCOM_STATUS_PROTECTION | \
					 SCOM_STATUS_PARITY |	  \
					 SCOM_STATUS_PIB_ABORT | \
					 SCOM_STATUS_PIB_RESP_MASK)
/* SCOM address encodings */
#define XSCOM_ADDR_IND_FLAG		BIT_ULL(63)
#define XSCOM_ADDR_INF_FORM1		BIT_ULL(60)

/* SCOM indirect stuff */
#define XSCOM_ADDR_DIRECT_PART		0x7fffffffull
#define XSCOM_ADDR_INDIRECT_PART	0x000fffff00000000ull
#define XSCOM_DATA_IND_READ		BIT_ULL(63)
#define XSCOM_DATA_IND_COMPLETE		BIT_ULL(31)
#define XSCOM_DATA_IND_ERR_MASK		0x70000000ull
#define XSCOM_DATA_IND_ERR_SHIFT	28
#define XSCOM_DATA_IND_DATA		0x0000ffffull
#define XSCOM_DATA_IND_FORM1_DATA	0x000fffffffffffffull
#define XSCOM_ADDR_FORM1_LOW		0x000ffffffffull
#define XSCOM_ADDR_FORM1_HI		0xfff00000000ull
#define XSCOM_ADDR_FORM1_HI_SHIFT	20

/* Retries */
#define SCOM_MAX_RETRIES		100	/* Retries on busy */
#define SCOM_MAX_IND_RETRIES		10	/* Retries indirect not ready */

struct scom_device {
	struct list_head link;
	struct fsi_device *fsi_dev;
	struct device dev;
	struct cdev cdev;
	struct mutex lock;
	bool dead;
};

static int __put_scom(struct scom_device *scom_dev, uint64_t value,
		      uint32_t addr, uint32_t *status)
{
	__be32 data, raw_status;
	int rc;

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
	rc = fsi_device_write(scom_dev->fsi_dev, SCOM_CMD_REG, &data,
				sizeof(uint32_t));
	if (rc)
		return rc;
	rc = fsi_device_read(scom_dev->fsi_dev, SCOM_STATUS_REG, &raw_status,
			     sizeof(uint32_t));
	if (rc)
		return rc;
	*status = be32_to_cpu(raw_status);

	return 0;
}

static int __get_scom(struct scom_device *scom_dev, uint64_t *value,
		      uint32_t addr, uint32_t *status)
{
	__be32 data, raw_status;
	int rc;


	*value = 0ULL;
	data = cpu_to_be32(SCOM_READ_CMD | addr);
	rc = fsi_device_write(scom_dev->fsi_dev, SCOM_CMD_REG, &data,
				sizeof(uint32_t));
	if (rc)
		return rc;
	rc = fsi_device_read(scom_dev->fsi_dev, SCOM_STATUS_REG, &raw_status,
			     sizeof(uint32_t));
	if (rc)
		return rc;

	/*
	 * Read the data registers even on error, so we don't have
	 * to interpret the status register here.
	 */
	rc = fsi_device_read(scom_dev->fsi_dev, SCOM_DATA0_REG, &data,
				sizeof(uint32_t));
	if (rc)
		return rc;
	*value |= (uint64_t)be32_to_cpu(data) << 32;
	rc = fsi_device_read(scom_dev->fsi_dev, SCOM_DATA1_REG, &data,
				sizeof(uint32_t));
	if (rc)
		return rc;
	*value |= be32_to_cpu(data);
	*status = be32_to_cpu(raw_status);

	return rc;
}

static int put_indirect_scom_form0(struct scom_device *scom, uint64_t value,
				   uint64_t addr, uint32_t *status)
{
	uint64_t ind_data, ind_addr;
	int rc, retries, err = 0;

	if (value & ~XSCOM_DATA_IND_DATA)
		return -EINVAL;

	ind_addr = addr & XSCOM_ADDR_DIRECT_PART;
	ind_data = (addr & XSCOM_ADDR_INDIRECT_PART) | value;
	rc = __put_scom(scom, ind_data, ind_addr, status);
	if (rc || (*status & SCOM_STATUS_ANY_ERR))
		return rc;

	for (retries = 0; retries < SCOM_MAX_IND_RETRIES; retries++) {
		rc = __get_scom(scom, &ind_data, addr, status);
		if (rc || (*status & SCOM_STATUS_ANY_ERR))
			return rc;

		err = (ind_data & XSCOM_DATA_IND_ERR_MASK) >> XSCOM_DATA_IND_ERR_SHIFT;
		*status = err << SCOM_STATUS_PIB_RESP_SHIFT;
		if ((ind_data & XSCOM_DATA_IND_COMPLETE) || (err != SCOM_PIB_BLOCKED))
			return 0;

		msleep(1);
	}
	return rc;
}

static int put_indirect_scom_form1(struct scom_device *scom, uint64_t value,
				   uint64_t addr, uint32_t *status)
{
	uint64_t ind_data, ind_addr;

	if (value & ~XSCOM_DATA_IND_FORM1_DATA)
		return -EINVAL;

	ind_addr = addr & XSCOM_ADDR_FORM1_LOW;
	ind_data = value | (addr & XSCOM_ADDR_FORM1_HI) << XSCOM_ADDR_FORM1_HI_SHIFT;
	return __put_scom(scom, ind_data, ind_addr, status);
}

static int get_indirect_scom_form0(struct scom_device *scom, uint64_t *value,
				   uint64_t addr, uint32_t *status)
{
	uint64_t ind_data, ind_addr;
	int rc, retries, err = 0;

	ind_addr = addr & XSCOM_ADDR_DIRECT_PART;
	ind_data = (addr & XSCOM_ADDR_INDIRECT_PART) | XSCOM_DATA_IND_READ;
	rc = __put_scom(scom, ind_data, ind_addr, status);
	if (rc || (*status & SCOM_STATUS_ANY_ERR))
		return rc;

	for (retries = 0; retries < SCOM_MAX_IND_RETRIES; retries++) {
		rc = __get_scom(scom, &ind_data, addr, status);
		if (rc || (*status & SCOM_STATUS_ANY_ERR))
			return rc;

		err = (ind_data & XSCOM_DATA_IND_ERR_MASK) >> XSCOM_DATA_IND_ERR_SHIFT;
		*status = err << SCOM_STATUS_PIB_RESP_SHIFT;
		*value = ind_data & XSCOM_DATA_IND_DATA;

		if ((ind_data & XSCOM_DATA_IND_COMPLETE) || (err != SCOM_PIB_BLOCKED))
			return 0;

		msleep(1);
	}
	return rc;
}

static int raw_put_scom(struct scom_device *scom, uint64_t value,
			uint64_t addr, uint32_t *status)
{
	if (addr & XSCOM_ADDR_IND_FLAG) {
		if (addr & XSCOM_ADDR_INF_FORM1)
			return put_indirect_scom_form1(scom, value, addr, status);
		else
			return put_indirect_scom_form0(scom, value, addr, status);
	} else
		return __put_scom(scom, value, addr, status);
}

static int raw_get_scom(struct scom_device *scom, uint64_t *value,
			uint64_t addr, uint32_t *status)
{
	if (addr & XSCOM_ADDR_IND_FLAG) {
		if (addr & XSCOM_ADDR_INF_FORM1)
			return -ENXIO;
		return get_indirect_scom_form0(scom, value, addr, status);
	} else
		return __get_scom(scom, value, addr, status);
}

static int handle_fsi2pib_status(struct scom_device *scom, uint32_t status)
{
	uint32_t dummy = -1;

	if (status & SCOM_STATUS_PROTECTION)
		return -EPERM;
	if (status & SCOM_STATUS_PARITY) {
		fsi_device_write(scom->fsi_dev, SCOM_FSI2PIB_RESET_REG, &dummy,
				 sizeof(uint32_t));
		return -EIO;
	}
	/* Return -EBUSY on PIB abort to force a retry */
	if (status & SCOM_STATUS_PIB_ABORT)
		return -EBUSY;
	if (status & SCOM_STATUS_ERR_SUMMARY) {
		fsi_device_write(scom->fsi_dev, SCOM_FSI2PIB_RESET_REG, &dummy,
				 sizeof(uint32_t));
		return -EIO;
	}
	return 0;
}

static int handle_pib_status(struct scom_device *scom, uint8_t status)
{
	uint32_t dummy = -1;

	if (status == SCOM_PIB_SUCCESS)
		return 0;
	if (status == SCOM_PIB_BLOCKED)
		return -EBUSY;

	/* Reset the bridge */
	fsi_device_write(scom->fsi_dev, SCOM_FSI2PIB_RESET_REG, &dummy,
			 sizeof(uint32_t));

	switch(status) {
	case SCOM_PIB_OFFLINE:
		return -ENODEV;
	case SCOM_PIB_BAD_ADDR:
		return -ENXIO;
	case SCOM_PIB_TIMEOUT:
		return -ETIMEDOUT;
	case SCOM_PIB_PARTIAL:
	case SCOM_PIB_CLK_ERR:
	case SCOM_PIB_PARITY_ERR:
	default:
		return -EIO;
	}
}

static int put_scom(struct scom_device *scom, uint64_t value,
		    uint64_t addr)
{
	uint32_t status, dummy = -1;
	int rc, retries;

	for (retries = 0; retries < SCOM_MAX_RETRIES; retries++) {
		rc = raw_put_scom(scom, value, addr, &status);
		if (rc) {
			/* Try resetting the bridge if FSI fails */
			if (rc != -ENODEV && retries == 0) {
				fsi_device_write(scom->fsi_dev, SCOM_FSI2PIB_RESET_REG,
						 &dummy, sizeof(uint32_t));
				rc = -EBUSY;
			} else
				return rc;
		} else
			rc = handle_fsi2pib_status(scom, status);
		if (rc && rc != -EBUSY)
			break;
		if (rc == 0) {
			rc = handle_pib_status(scom,
					       (status & SCOM_STATUS_PIB_RESP_MASK)
					       >> SCOM_STATUS_PIB_RESP_SHIFT);
			if (rc && rc != -EBUSY)
				break;
		}
		if (rc == 0)
			break;
		msleep(1);
	}
	return rc;
}

static int get_scom(struct scom_device *scom, uint64_t *value,
		    uint64_t addr)
{
	uint32_t status, dummy = -1;
	int rc, retries;

	for (retries = 0; retries < SCOM_MAX_RETRIES; retries++) {
		rc = raw_get_scom(scom, value, addr, &status);
		if (rc) {
			/* Try resetting the bridge if FSI fails */
			if (rc != -ENODEV && retries == 0) {
				fsi_device_write(scom->fsi_dev, SCOM_FSI2PIB_RESET_REG,
						 &dummy, sizeof(uint32_t));
				rc = -EBUSY;
			} else
				return rc;
		} else
			rc = handle_fsi2pib_status(scom, status);
		if (rc && rc != -EBUSY)
			break;
		if (rc == 0) {
			rc = handle_pib_status(scom,
					       (status & SCOM_STATUS_PIB_RESP_MASK)
					       >> SCOM_STATUS_PIB_RESP_SHIFT);
			if (rc && rc != -EBUSY)
				break;
		}
		if (rc == 0)
			break;
		msleep(1);
	}
	return rc;
}

static ssize_t scom_read(struct file *filep, char __user *buf, size_t len,
			 loff_t *offset)
{
	struct scom_device *scom = filep->private_data;
	struct device *dev = &scom->fsi_dev->dev;
	uint64_t val;
	int rc;

	if (len != sizeof(uint64_t))
		return -EINVAL;

	mutex_lock(&scom->lock);
	if (scom->dead)
		rc = -ENODEV;
	else
		rc = get_scom(scom, &val, *offset);
	mutex_unlock(&scom->lock);
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
	struct scom_device *scom = filep->private_data;
	struct device *dev = &scom->fsi_dev->dev;
	uint64_t val;

	if (len != sizeof(uint64_t))
		return -EINVAL;

	rc = copy_from_user(&val, buf, len);
	if (rc) {
		dev_dbg(dev, "copy from user failed:%d\n", rc);
		return -EINVAL;
	}

	mutex_lock(&scom->lock);
	if (scom->dead)
		rc = -ENODEV;
	else
		rc = put_scom(scom, val, *offset);
	mutex_unlock(&scom->lock);
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

static void raw_convert_status(struct scom_access *acc, uint32_t status)
{
	acc->pib_status = (status & SCOM_STATUS_PIB_RESP_MASK) >>
		SCOM_STATUS_PIB_RESP_SHIFT;
	acc->intf_errors = 0;

	if (status & SCOM_STATUS_PROTECTION)
		acc->intf_errors |= SCOM_INTF_ERR_PROTECTION;
	else if (status & SCOM_STATUS_PARITY)
		acc->intf_errors |= SCOM_INTF_ERR_PARITY;
	else if (status & SCOM_STATUS_PIB_ABORT)
		acc->intf_errors |= SCOM_INTF_ERR_ABORT;
	else if (status & SCOM_STATUS_ERR_SUMMARY)
		acc->intf_errors |= SCOM_INTF_ERR_UNKNOWN;
}

static int scom_raw_read(struct scom_device *scom, void __user *argp)
{
	struct scom_access acc;
	uint32_t status;
	int rc;

	if (copy_from_user(&acc, argp, sizeof(struct scom_access)))
		return -EFAULT;

	rc = raw_get_scom(scom, &acc.data, acc.addr, &status);
	if (rc)
		return rc;
	raw_convert_status(&acc, status);
	if (copy_to_user(argp, &acc, sizeof(struct scom_access)))
		return -EFAULT;
	return 0;
}

static int scom_raw_write(struct scom_device *scom, void __user *argp)
{
	u64 prev_data, mask, data;
	struct scom_access acc;
	uint32_t status;
	int rc;

	if (copy_from_user(&acc, argp, sizeof(struct scom_access)))
		return -EFAULT;

	if (acc.mask) {
		rc = raw_get_scom(scom, &prev_data, acc.addr, &status);
		if (rc)
			return rc;
		if (status & SCOM_STATUS_ANY_ERR)
			goto fail;
		mask = acc.mask;
	} else {
		prev_data = mask = -1ull;
	}
	data = (prev_data & ~mask) | (acc.data & mask);
	rc = raw_put_scom(scom, data, acc.addr, &status);
	if (rc)
		return rc;
 fail:
	raw_convert_status(&acc, status);
	if (copy_to_user(argp, &acc, sizeof(struct scom_access)))
		return -EFAULT;
	return 0;
}

static int scom_reset(struct scom_device *scom, void __user *argp)
{
	uint32_t flags, dummy = -1;
	int rc = 0;

	if (get_user(flags, (__u32 __user *)argp))
		return -EFAULT;
	if (flags & SCOM_RESET_PIB)
		rc = fsi_device_write(scom->fsi_dev, SCOM_PIB_RESET_REG, &dummy,
				      sizeof(uint32_t));
	if (!rc && (flags & (SCOM_RESET_PIB | SCOM_RESET_INTF)))
		rc = fsi_device_write(scom->fsi_dev, SCOM_FSI2PIB_RESET_REG, &dummy,
				      sizeof(uint32_t));
	return rc;
}

static int scom_check(struct scom_device *scom, void __user *argp)
{
	/* Still need to find out how to get "protected" */
	return put_user(SCOM_CHECK_SUPPORTED, (__u32 __user *)argp);
}

static long scom_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct scom_device *scom = file->private_data;
	void __user *argp = (void __user *)arg;
	int rc = -ENOTTY;

	mutex_lock(&scom->lock);
	if (scom->dead) {
		mutex_unlock(&scom->lock);
		return -ENODEV;
	}
	switch(cmd) {
	case FSI_SCOM_CHECK:
		rc = scom_check(scom, argp);
		break;
	case FSI_SCOM_READ:
		rc = scom_raw_read(scom, argp);
		break;
	case FSI_SCOM_WRITE:
		rc = scom_raw_write(scom, argp);
		break;
	case FSI_SCOM_RESET:
		rc = scom_reset(scom, argp);
		break;
	}
	mutex_unlock(&scom->lock);
	return rc;
}

static int scom_open(struct inode *inode, struct file *file)
{
	struct scom_device *scom = container_of(inode->i_cdev, struct scom_device, cdev);

	file->private_data = scom;

	return 0;
}

static const struct file_operations scom_fops = {
	.owner		= THIS_MODULE,
	.open		= scom_open,
	.llseek		= scom_llseek,
	.read		= scom_read,
	.write		= scom_write,
	.unlocked_ioctl	= scom_ioctl,
};

static void scom_free(struct device *dev)
{
	struct scom_device *scom = container_of(dev, struct scom_device, dev);

	put_device(&scom->fsi_dev->dev);
	kfree(scom);
}

static int scom_probe(struct device *dev)
{
	struct fsi_device *fsi_dev = to_fsi_dev(dev);
	struct scom_device *scom;
	int rc, didx;

	scom = kzalloc(sizeof(*scom), GFP_KERNEL);
	if (!scom)
		return -ENOMEM;
	dev_set_drvdata(dev, scom);
	mutex_init(&scom->lock);

	/* Grab a reference to the device (parent of our cdev), we'll drop it later */
	if (!get_device(dev)) {
		kfree(scom);
		return -ENODEV;
	}
	scom->fsi_dev = fsi_dev;

	/* Create chardev for userspace access */
	scom->dev.type = &fsi_cdev_type;
	scom->dev.parent = dev;
	scom->dev.release = scom_free;
	device_initialize(&scom->dev);

	/* Allocate a minor in the FSI space */
	rc = fsi_get_new_minor(fsi_dev, fsi_dev_scom, &scom->dev.devt, &didx);
	if (rc)
		goto err;

	dev_set_name(&scom->dev, "scom%d", didx);
	cdev_init(&scom->cdev, &scom_fops);
	rc = cdev_device_add(&scom->cdev, &scom->dev);
	if (rc) {
		dev_err(dev, "Error %d creating char device %s\n",
			rc, dev_name(&scom->dev));
		goto err_free_minor;
	}

	return 0;
 err_free_minor:
	fsi_free_minor(scom->dev.devt);
 err:
	put_device(&scom->dev);
	return rc;
}

static int scom_remove(struct device *dev)
{
	struct scom_device *scom = dev_get_drvdata(dev);

	mutex_lock(&scom->lock);
	scom->dead = true;
	mutex_unlock(&scom->lock);
	cdev_device_del(&scom->cdev, &scom->dev);
	fsi_free_minor(scom->dev.devt);
	put_device(&scom->dev);

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
	return fsi_driver_register(&scom_drv);
}

static void scom_exit(void)
{
	fsi_driver_unregister(&scom_drv);
}

module_init(scom_init);
module_exit(scom_exit);
MODULE_LICENSE("GPL");

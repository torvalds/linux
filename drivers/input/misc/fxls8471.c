/*
 *  fxls8471.c - Linux kernel modules for 3-Axis Accel sensor
 *  Copyright (C) 2014-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/input-polldev.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include "fxls8471.h"

#define	SENSOR_IOCTL_BASE		'S'
#define	SENSOR_GET_MODEL_NAME		_IOR(SENSOR_IOCTL_BASE, 0, char *)
#define	SENSOR_GET_POWER_STATUS		_IOR(SENSOR_IOCTL_BASE, 2, int)
#define	SENSOR_SET_POWER_STATUS		_IOR(SENSOR_IOCTL_BASE, 3, int)
#define	SENSOR_GET_DELAY_TIME		_IOR(SENSOR_IOCTL_BASE, 4, int)
#define	SENSOR_SET_DELAY_TIME		_IOR(SENSOR_IOCTL_BASE, 5, int)
#define	SENSOR_GET_RAW_DATA		_IOR(SENSOR_IOCTL_BASE, 6, short[3])

#define FXLS8471_POSITION_DEFAULT	2
#define FXLS8471_DELAY_DEFAULT		200

#define FXLS8471_STATUS_ZYXDR		0x08
#define FXLS8471_BUF_SIZE		6

struct fxls8471_data fxls8471_dev;

static int fxls8471_position_setting[8][3][3] = {
	{{0, -1, 0}, {1, 0, 0}, {0, 0, 1} },
	{{-1, 0, 0}, {0, -1, 0}, {0, 0, 1} },
	{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1} },
	{{1, 0, 0}, {0, 1, 0}, {0, 0, 1} },

	{{0, -1, 0}, {-1, 0, 0}, {0, 0, -1} },
	{{-1, 0, 0}, {0, 1, 0}, {0, 0, -1} },
	{{0, 1, 0}, {1, 0, 0}, {0, 0, -1} },
	{{1, 0, 0}, {0, -1, 0}, {0, 0, -1} },
};

static int fxls8471_bus_write(struct fxls8471_data *pdata, u8 reg, u8 val)
{
	if (pdata && pdata->write)
		return pdata->write(pdata, reg, val);
	return -EIO;
}

static int fxls8471_bus_read(struct fxls8471_data *pdata, u8 reg)
{
	if (pdata && pdata->read)
		return pdata->read(pdata, reg);
	return -EIO;
}

static int fxls8471_bus_read_block(struct fxls8471_data *pdata, u8 reg, u8 len,
				   u8 *val)
{
	if (pdata && pdata->read_block)
		return pdata->read_block(pdata, reg, len, val);
	return -EIO;
}

static int fxls8471_data_convert(struct fxls8471_data *pdata,
				 struct fxls8471_data_axis *axis_data)
{
	short rawdata[3], data[3];
	int i, j;
	int position = atomic_read(&pdata->position);

	if (position < 0 || position > 7)
		position = 0;
	rawdata[0] = axis_data->x;
	rawdata[1] = axis_data->y;
	rawdata[2] = axis_data->z;
	for (i = 0; i < 3; i++) {
		data[i] = 0;
		for (j = 0; j < 3; j++)
			data[i] +=
			    rawdata[j] *
			    fxls8471_position_setting[position][i][j];
	}
	axis_data->x = data[0];
	axis_data->y = data[1];
	axis_data->z = data[2];
	return 0;
}

static int fxls8471_device_init(struct fxls8471_data *pdata)
{
	int result;
	result = fxls8471_bus_write(pdata, FXLS8471_CTRL_REG1, 0);
	if (result < 0)
		goto out;

	result = fxls8471_bus_write(pdata, FXLS8471_XYZ_DATA_CFG, MODE_2G);
	if (result < 0)
		goto out;

	if (pdata->irq) {
		result = fxls8471_bus_write(pdata, FXLS8471_CTRL_REG5, 0x01);
		if (result < 0)
			goto out;
		result = fxls8471_bus_write(pdata, FXLS8471_CTRL_REG4, 0x01);
		if (result < 0)
			goto out;
	}
	atomic_set(&pdata->active, STANDBY);
	return 0;
out:
	printk("FXLS8471 device init error\n");
	return result;

}

static int fxls8471_change_mode(struct fxls8471_data *pdata, int mode)
{
	u8 val;
	int ret;
	val = fxls8471_bus_read(pdata, FXLS8471_CTRL_REG1);
	if (mode == ACTIVED)
		val |= 0x01;
	else
		val &= (~0x01);
	ret = fxls8471_bus_write(pdata, FXLS8471_CTRL_REG1, val);
	return ret;
}

static int fxls8471_set_delay(struct fxls8471_data *pdata, int delay)
{
	u8 val;
	val = fxls8471_bus_read(pdata, FXLS8471_CTRL_REG1);
	/* set sensor standby */
	fxls8471_bus_write(pdata, FXLS8471_CTRL_REG1, (val & ~0x01));
	val &= ~(0x7 << 3);
	if (delay <= 10)
		val |= 0x02 << 3;
	else if (delay <= 20)
		val |= 0x03 << 3;
	else if (delay <= 67)
		val |= 0x04 << 3;
	else
		val |= 0x05 << 3;
	/* set sensor standby */
	fxls8471_bus_write(pdata, FXLS8471_CTRL_REG1, val);
	return 0;
}

static int fxls8471_change_range(struct fxls8471_data *pdata, int range)
{
	int ret;

	ret = fxls8471_bus_write(pdata, FXLS8471_XYZ_DATA_CFG, range);

	return ret;
}

static int fxls8471_read_data(struct fxls8471_data *pdata,
			      struct fxls8471_data_axis *data)
{
	u8 tmp_data[FXLS8471_BUF_SIZE];
	int ret;
	ret = fxls8471_bus_read_block(pdata, FXLS8471_OUT_X_MSB,
				      FXLS8471_BUF_SIZE, tmp_data);
	if (ret < FXLS8471_BUF_SIZE) {
		printk(KERN_ERR "FXLS8471 read sensor block data error\n");
		return -EIO;
	}
	data->x = ((tmp_data[0] << 8) & 0xff00) | tmp_data[1];
	data->y = ((tmp_data[2] << 8) & 0xff00) | tmp_data[3];
	data->z = ((tmp_data[4] << 8) & 0xff00) | tmp_data[5];
	return 0;
}

/* fxls8471 miscdevice */
static long fxls8471_ioctl(struct file *file, unsigned int reg,
			   unsigned long arg)
{
	struct fxls8471_data *pdata = file->private_data;
	void __user *argp = (void __user *)arg;
	long ret = 0;
	short sdata[3];
	int enable;
	int delay;
	struct fxls8471_data_axis data;
	if (!pdata) {
		printk(KERN_ERR "FXLS8471 struct datt point is NULL.");
		return -EFAULT;
	}
	switch (reg) {
	case SENSOR_GET_MODEL_NAME:
		if (copy_to_user(argp, "fxls8471", strlen("fxls8471") + 1)) {
			printk(KERN_ERR
			       "SENSOR_GET_MODEL_NAME copy_to_user failed.");
			ret = -EFAULT;
		}
		break;
	case SENSOR_GET_POWER_STATUS:
		enable = atomic_read(&pdata->active);
		if (copy_to_user(argp, &enable, sizeof(int))) {
			printk(KERN_ERR
			       "SENSOR_SET_POWER_STATUS copy_to_user failed.");
			ret = -EFAULT;
		}
		break;
	case SENSOR_SET_POWER_STATUS:
		if (copy_from_user(&enable, argp, sizeof(int))) {
			printk(KERN_ERR
			       "SENSOR_SET_POWER_STATUS copy_to_user failed.");
			ret = -EFAULT;
		}
		if (pdata) {
			ret =
			    fxls8471_change_mode(pdata,
						 enable ? ACTIVED : STANDBY);
			if (!ret)
				atomic_set(&pdata->active, enable);
		}
		break;
	case SENSOR_GET_DELAY_TIME:
		delay = atomic_read(&pdata->delay);
		if (copy_to_user(argp, &delay, sizeof(delay))) {
			printk(KERN_ERR
			       "SENSOR_GET_DELAY_TIME copy_to_user failed.");
			return -EFAULT;
		}
		break;
	case SENSOR_SET_DELAY_TIME:
		if (copy_from_user(&delay, argp, sizeof(int))) {
			printk(KERN_ERR
			       "SENSOR_GET_DELAY_TIME copy_to_user failed.");
			ret = -EFAULT;
		}
		if (pdata && delay > 0 && delay <= 500) {
			ret = fxls8471_set_delay(pdata, delay);
			if (!ret)
				atomic_set(&pdata->delay, delay);
		}
		break;
	case SENSOR_GET_RAW_DATA:
		ret = fxls8471_read_data(pdata, &data);
		if (!ret) {
			fxls8471_data_convert(pdata, &data);
			sdata[0] = data.x;
			sdata[1] = data.y;
			sdata[2] = data.z;
			if (copy_to_user(argp, sdata, sizeof(sdata))) {
				printk(KERN_ERR
				       "SENSOR_GET_RAW_DATA copy_to_user failed.");
				ret = -EFAULT;
			}
		}
		break;
	default:
		ret = -1;
	}
	return ret;
}

static int fxls8471_open(struct inode *inode, struct file *file)
{
	file->private_data = &fxls8471_dev;
	return nonseekable_open(inode, file);
}

static int fxls8471_release(struct inode *inode, struct file *file)
{
	/* note: releasing the wdt in NOWAYOUT-mode does not stop it */
	return 0;
}

static const struct file_operations fxls8471_fops = {
	.owner = THIS_MODULE,
	.open = fxls8471_open,
	.release = fxls8471_release,
	.unlocked_ioctl = fxls8471_ioctl,
};

static struct miscdevice fxls8471_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "FreescaleAccelerometer",
	.fops = &fxls8471_fops,
};

static ssize_t fxls8471_enable_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct fxls8471_data *pdata = &fxls8471_dev;
	int enable = 0;
	enable = atomic_read(&pdata->active);
	return sprintf(buf, "%d\n", enable);
}

static ssize_t fxls8471_enable_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct fxls8471_data *pdata = &fxls8471_dev;
	int ret;
	unsigned long enable;

	if (kstrtoul(buf, 10, &enable) < 0)
		return -EINVAL;
	enable = (enable > 0) ? 1 : 0;
	ret = fxls8471_change_mode(pdata, (enable > 0 ? ACTIVED : STANDBY));
	if (!ret) {
		atomic_set(&pdata->active, enable);
		if (enable)
			printk(KERN_INFO "mma enable setting actived\n");
		else
			printk(KERN_INFO "mma enable setting standby\n");
	}
	return count;
}

static ssize_t fxls8471_poll_delay_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct fxls8471_data *pdata = &fxls8471_dev;
	int delay = 0;
	delay = atomic_read(&pdata->delay);
	return sprintf(buf, "%d\n", delay);
}

static ssize_t fxls8471_poll_delay_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct fxls8471_data *pdata = &fxls8471_dev;
	int ret;
	unsigned long delay;

	if (kstrtoul(buf, 10, &delay) < 0)
		return -EINVAL;
	ret = fxls8471_set_delay(pdata, delay);
	if (!ret)
		atomic_set(&pdata->delay, delay);

	return count;
}

static ssize_t fxls8471_position_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct fxls8471_data *pdata = &fxls8471_dev;
	int position = 0;
	position = atomic_read(&pdata->position);
	return sprintf(buf, "%d\n", position);
}

static ssize_t fxls8471_position_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct fxls8471_data *pdata = &fxls8471_dev;
	unsigned long position;

	if (kstrtoul(buf, 10, &position) < 0)
		return -EINVAL;
	atomic_set(&pdata->position, position);

	return count;
}

static ssize_t fxls8471_data_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct fxls8471_data *pdata = &fxls8471_dev;
	int ret = 0;
	struct fxls8471_data_axis data;
	ret = fxls8471_read_data(pdata, &data);
	if (!ret)
		fxls8471_data_convert(pdata, &data);
	return sprintf(buf, "%d,%d,%d\n", data.x, data.y, data.z);
}

static ssize_t fxls8471_range_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct fxls8471_data *pdata = &fxls8471_dev;
	int range = 0;

	range = atomic_read(&pdata->range);
	return sprintf(buf, "%d\n", range);
}

static ssize_t fxls8471_range_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct fxls8471_data *pdata = &fxls8471_dev;
	int ret;
	unsigned long range;

	if (kstrtoul(buf, 10, &range) < 0)
		return -EINVAL;

	if (range == atomic_read(&pdata->range))
		return count;

	if (atomic_read(&pdata->active))
		printk(KERN_INFO "Pls set the sensor standby and then actived\n");
	ret = fxls8471_change_range(pdata, range);
	if (!ret)
		atomic_set(&pdata->range, range);

	return count;
}

static DEVICE_ATTR(enable, S_IWUSR | S_IRUGO, fxls8471_enable_show, fxls8471_enable_store);
static DEVICE_ATTR(poll_delay, S_IWUSR | S_IRUGO, fxls8471_poll_delay_show,
		   fxls8471_poll_delay_store);

static DEVICE_ATTR(position, S_IWUSR | S_IRUGO, fxls8471_position_show,
		   fxls8471_position_store);

static DEVICE_ATTR(data, S_IWUSR | S_IRUGO, fxls8471_data_show, NULL);

static DEVICE_ATTR(range, S_IWUSR | S_IRUGO, fxls8471_range_show, fxls8471_range_store);

static struct attribute *fxls8471_attributes[] = {
	&dev_attr_enable.attr,
	&dev_attr_poll_delay.attr,
	&dev_attr_position.attr,
	&dev_attr_data.attr,
	&dev_attr_range.attr,
	NULL
};

static const struct attribute_group fxls8471_attr_group = {
	.attrs = fxls8471_attributes,
};

static irqreturn_t fxls8471_irq_handler(int irq, void *dev)
{
	int ret;
	u8 int_src;
	struct fxls8471_data *pdata = (struct fxls8471_data *)dev;
	struct fxls8471_data_axis data;
	int_src = fxls8471_bus_read(pdata, FXLS8471_INT_SOURCE);
	/* data ready interrupt */
	if (int_src & 0x01) {
		ret = fxls8471_read_data(pdata, &data);
		if (!ret) {
			fxls8471_data_convert(pdata, &data);
			input_report_abs(pdata->idev, ABS_X, data.x);
			input_report_abs(pdata->idev, ABS_Y, data.y);
			input_report_abs(pdata->idev, ABS_Z, data.z);
			input_sync(pdata->idev);
		}

	}
	return IRQ_HANDLED;
}

int fxls8471_driver_init(struct fxls8471_data *pdata)
{
	int result, chip_id;

	chip_id = fxls8471_bus_read(pdata, FXLS8471_WHO_AM_I);

	if (chip_id != FXSL8471_ID) {
		printk(KERN_ERR "read sensor who am i (0x%x)error !\n",
		       chip_id);
		result = -EINVAL;
		goto err_out;
	}
	/* Initialize the FXLS8471 chip */
	pdata->chip_id = chip_id;
	atomic_set(&pdata->delay, FXLS8471_DELAY_DEFAULT);
	atomic_set(&pdata->position, FXLS8471_POSITION_DEFAULT);
	result = misc_register(&fxls8471_device);
	if (result != 0) {
		printk(KERN_ERR "register acc miscdevice error");
		goto err_regsiter_misc;
	}

	result =
	    sysfs_create_group(&fxls8471_device.this_device->kobj,
			       &fxls8471_attr_group);
	if (result) {
		printk(KERN_ERR "create device file failed!\n");
		result = -EINVAL;
		goto err_create_sysfs;
	}
	/*create data  input device */
	pdata->idev = input_allocate_device();
	if (!pdata->idev) {
		result = -ENOMEM;
		printk(KERN_ERR "alloc fxls8471 input device failed!\n");
		goto err_alloc_input_device;
	}
	pdata->idev->name = "FreescaleAccelerometer";
	pdata->idev->id.bustype = BUS_I2C;
	pdata->idev->evbit[0] = BIT_MASK(EV_ABS);
	input_set_abs_params(pdata->idev, ABS_X, -0x7fff, 0x7fff, 0, 0);
	input_set_abs_params(pdata->idev, ABS_Y, -0x7fff, 0x7fff, 0, 0);
	input_set_abs_params(pdata->idev, ABS_Z, -0x7fff, 0x7fff, 0, 0);
	result = input_register_device(pdata->idev);
	if (result) {
		printk(KERN_ERR "register fxls8471 input device failed!\n");
		goto err_register_input_device;
	}
	if (pdata->irq) {
		result =
		    request_threaded_irq(pdata->irq, NULL, fxls8471_irq_handler,
					 IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					 pdata->idev->name, pdata);
		if (result < 0) {
			printk(KERN_ERR "failed to register MMA8x5x irq %d!\n",
			       pdata->irq);
			goto err_register_irq;
		}
	}
	fxls8471_device_init(pdata);
	printk("fxls8471 device driver probe successfully\n");
	return 0;
err_register_irq:
	input_unregister_device(pdata->idev);
err_register_input_device:
	input_free_device(pdata->idev);
err_alloc_input_device:
	sysfs_remove_group(&fxls8471_device.this_device->kobj,
			   &fxls8471_attr_group);
err_create_sysfs:
	misc_deregister(&fxls8471_device);
err_regsiter_misc:
	kfree(pdata);
err_out:
	return result;
}
EXPORT_SYMBOL_GPL(fxls8471_driver_init);

int fxls8471_driver_remove(struct fxls8471_data *pdata)
{
	fxls8471_change_mode(pdata, STANDBY);
	misc_deregister(&fxls8471_device);
	if (pdata != NULL)
		kfree(pdata);
	return 0;
}
EXPORT_SYMBOL_GPL(fxls8471_driver_remove);

#ifdef CONFIG_PM_SLEEP
int fxls8471_driver_suspend(struct fxls8471_data *pdata)
{
	if (atomic_read(&pdata->active))
		fxls8471_change_mode(pdata, STANDBY);
	return 0;
}
EXPORT_SYMBOL_GPL(fxls8471_driver_suspend);

int fxls8471_driver_resume(struct fxls8471_data *pdata)
{
	if (atomic_read(&pdata->active))
		fxls8471_change_mode(pdata, ACTIVED);
	return 0;
}
EXPORT_SYMBOL_GPL(fxls8471_driver_resume);

#endif

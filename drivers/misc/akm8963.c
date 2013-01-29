/* drivers/misc/akm8963.c - akm8963 compass driver
 *
 * Copyright (C) 2007-2008 HTC Corporation.
 * Author: Hou-Kun Chen <houkun.chen@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*#define DEBUG*/
/*#define VERBOSE_DEBUG*/

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/akm8963.h>

#define AKM8963_DEBUG_IF	0
#define AKM8963_DEBUG_DATA	0

#define AKM_ACCEL_ITEMS 3
/* Wait timeout in millisecond */
#define AKM8963_DRDY_TIMEOUT	100

struct akm8963_data {
	struct i2c_client	*i2c;
	struct input_dev	*input;
	struct device		*class_dev;
	struct class		*compass;
	struct delayed_work	work;

	wait_queue_head_t	drdy_wq;
	wait_queue_head_t	open_wq;

	struct mutex sensor_mutex;
	int8_t	sense_data[SENSOR_DATA_SIZE];
	struct mutex accel_mutex;
	int16_t	accel_data[AKM_ACCEL_ITEMS];

	struct mutex	val_mutex;
	uint32_t	enable_flag;
	int64_t		delay[AKM_NUM_SENSORS];

	atomic_t	active;
	atomic_t	is_busy;
	atomic_t	drdy;
	atomic_t	suspend;

	char layout;
	char outbit;
	int	irq;
	int	rstn;
};

static struct akm8963_data *s_akm;



/***** I2C I/O function ***********************************************/
static int akm8963_i2c_rxdata(
	struct i2c_client *i2c,
	unsigned char *rxData,
	int length)
{
	struct i2c_msg msgs[] = {
	{
		.addr = i2c->addr,
		.flags = 0,
		.len = 1,
		.buf = rxData,
		.scl_rate = 200*1000,
	},
	{
		.addr = i2c->addr,
		.flags = I2C_M_RD,
		.len = length,
		.buf = rxData,
		.scl_rate = 200*1000,
	}, };
	unsigned char addr = rxData[0];

	if (i2c_transfer(i2c->adapter, msgs, 2) < 0) {
		dev_err(&i2c->dev, "%s: transfer failed.", __func__);
		return -EIO;
	}

	dev_vdbg(&i2c->dev, "RxData: len=%02x, addr=%02x, data=%02x",
		length, addr, rxData[0]);
	return 0;
}

static int akm8963_i2c_txdata(
	struct i2c_client *i2c,
	unsigned char *txData,
	int length)
{
	struct i2c_msg msg[] = {
	{
		.addr = i2c->addr,
		.flags = 0,
		.len = length,
		.buf = txData,
		.scl_rate = 200*1000,
	}, };

	if (i2c_transfer(i2c->adapter, msg, 1) < 0) {
		dev_err(&i2c->dev, "%s: transfer failed.", __func__);
		return -EIO;
	}

	dev_vdbg(&i2c->dev, "TxData: len=%02x, addr=%02x data=%02x",
		length, txData[0], txData[1]);
	return 0;
}

static int akm8963_i2c_check_device(
	struct i2c_client *client)
{
	unsigned char buffer[2];
	int err;

	/* Set measure mode */
	buffer[0] = AK8963_REG_WIA;
	err = akm8963_i2c_rxdata(client, buffer, 1);
	if (err < 0) {
		dev_err(&client->dev,
			"%s: Can not read WIA.", __func__);
		return err;
	}
	/* Check read data */
	if (buffer[0] != 0x48) {
		dev_err(&client->dev,
			"%s: The device is not AK8963.", __func__);
		return -ENXIO;
	}

	//printk("========[%x] ,[%x]" , buffer[0],buffer[1]);
	return err;
}

/***** akm miscdevice functions *************************************/
static int AKECS_Open(struct inode *inode, struct file *file);
static int AKECS_Release(struct inode *inode, struct file *file);
static long AKECS_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg);

static struct file_operations AKECS_fops = {
	.owner = THIS_MODULE,
	.open = AKECS_Open,
	.release = AKECS_Release,
	.unlocked_ioctl = AKECS_ioctl,
};

static struct miscdevice akm8963_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "akm8963_dev",
	.fops = &AKECS_fops,
};

static int AKECS_Set_CNTL1(
	struct akm8963_data *akm,
	unsigned char mode)
{
	unsigned char buffer[2];
	int err;

	/* Busy check */
	if (atomic_cmpxchg(&akm->is_busy, 0, 1) != 0) {
		dev_err(&akm->i2c->dev, "%s: device is busy.", __func__);
		return -EBUSY;
	}

	/* Set flag */
	atomic_set(&akm->drdy, 0);

	/* Set measure mode */
	buffer[0] = AK8963_REG_CNTL1;
	buffer[1] = mode;
	err = akm8963_i2c_txdata(akm->i2c, buffer, 2);
	if (err < 0) {
		dev_err(&akm->i2c->dev, "%s: Can not set CNTL.", __func__);
		atomic_set(&akm->is_busy, 0);
	} else {
		dev_dbg(&akm->i2c->dev, "Mode is set to (%d).", mode);
	}

	return err;
}

static int AKECS_Set_PowerDown(
	struct akm8963_data *akm)
{
	unsigned char buffer[2];
	int err;

	/* Set measure mode */
	buffer[0] = AK8963_REG_CNTL1;
	buffer[1] = AK8963_MODE_POWERDOWN;
	err = akm8963_i2c_txdata(akm->i2c, buffer, 2);
	if (err < 0) {
		dev_err(&akm->i2c->dev,
			"%s: Can not set to measurement mode.", __func__);
		atomic_set(&akm->is_busy, 0);
	} else {
		dev_dbg(&akm->i2c->dev, "Powerdown mode is set.");
	}

	/* Set to initial status. */
	atomic_set(&akm->is_busy, 0);
	atomic_set(&akm->drdy, 0);

	return err;
}

static int AKECS_Reset(
	struct akm8963_data *akm,
	int hard)
{
	unsigned char buffer[2];
	int err = 0;

	if (hard != 0) {
		gpio_set_value(akm->rstn, 0);
		udelay(5);
		gpio_set_value(akm->rstn, 1);
	} else {
		/* Set measure mode */
		buffer[0] = AK8963_REG_CNTL2;
		buffer[1] = 0x01;
		err = akm8963_i2c_txdata(akm->i2c, buffer, 2);
		if (err < 0) {
			dev_err(&akm->i2c->dev,
				"%s: Can not set SRST bit.", __func__);
		} else {
			dev_dbg(&akm->i2c->dev, "Soft reset is done.");
		}
	}

	/* Device will be accessible 100 us after */
	udelay(100);

	return err;
}

static int AKECS_SetMode(
	struct akm8963_data *akm,
	unsigned char mode)
{
	int err;

	switch (mode & 0x0F) {
	case AK8963_MODE_SNG_MEASURE:
	case AK8963_MODE_SELF_TEST:
	case AK8963_MODE_FUSE_ACCESS:
		err = AKECS_Set_CNTL1(akm, mode);
		if ((err >= 0) && (akm->irq == 0)) {
			schedule_delayed_work(
				&akm->work,
				usecs_to_jiffies(AK8963_MEASUREMENT_TIME_US));
		}
		break;
	case AK8963_MODE_POWERDOWN:
		err = AKECS_Set_PowerDown(akm);
		break;
	default:
		dev_err(&akm->i2c->dev,
			"%s: Unknown mode(%d).", __func__, mode);
		return -EINVAL;
	}

	/* wait at least 100us after changing mode */
	udelay(100);

	return err;
}

/* This function will block a process until the latest measurement
 * data is available.
 */
static int AKECS_GetData(
	struct akm8963_data *akm,
	char *rbuf,
	int size)
{
	int err;
	err = wait_event_interruptible_timeout(
			akm->drdy_wq,
			atomic_read(&akm->drdy),
			AKM8963_DRDY_TIMEOUT);

	if (err < 0) {
		dev_err(&akm->i2c->dev,
			"%s: wait_event failed (%d).", __func__, err);
		return -1;
	}
	if (!atomic_read(&akm->drdy)) {
		dev_err(&akm->i2c->dev,
			"%s: DRDY is not set.", __func__);
		return -1;
	}

	mutex_lock(&akm->sensor_mutex);
	memcpy(rbuf, akm->sense_data, size);
	atomic_set(&akm->drdy, 0);
	mutex_unlock(&akm->sensor_mutex);

	return 0;
}

static void AKECS_SetYPR(
	struct akm8963_data *akm,
	int *rbuf)
{
	uint32_t ready;
	dev_vdbg(&akm->i2c->dev, "AKM8963 %s: flag =0x%X", __func__,
		rbuf[0]);
	dev_vdbg(&akm->input->dev, "  Acceleration[LSB]: %6d,%6d,%6d stat=%d",
		rbuf[1], rbuf[2], rbuf[3], rbuf[4]);
	dev_vdbg(&akm->input->dev, "  Geomagnetism[LSB]: %6d,%6d,%6d stat=%d",
		rbuf[5], rbuf[6], rbuf[7], rbuf[8]);
	dev_vdbg(&akm->input->dev, "  Orientation[YPR] : %6d,%6d,%6d",
		rbuf[9], rbuf[10], rbuf[11]);

	/* No events are reported */
	if (!rbuf[0]) {
		dev_dbg(&akm->i2c->dev, "Don't waste a time.");
		return;
	}

	mutex_lock(&akm->val_mutex);
	ready = (akm->enable_flag & (uint32_t)rbuf[0]);
	mutex_unlock(&akm->val_mutex);

	/* Report acceleration sensor information */
	if (ready & ACC_DATA_READY) {
		input_report_abs(akm->input, ABS_X, rbuf[1]);
		input_report_abs(akm->input, ABS_Y, rbuf[2]);
		input_report_abs(akm->input, ABS_Z, rbuf[3]);
		input_report_abs(akm->input, ABS_THROTTLE, rbuf[4]);
	}
	/* Report magnetic vector information */
	if (ready & MAG_DATA_READY) {
		input_report_abs(akm->input, ABS_RX, rbuf[5]);
		input_report_abs(akm->input, ABS_RY, rbuf[6]);
		input_report_abs(akm->input, ABS_RZ, rbuf[7]);
		input_report_abs(akm->input, ABS_RUDDER, rbuf[8]);
	}
	/* Report orientation sensor information */
	if (ready & ORI_DATA_READY) {
		input_report_abs(akm->input, ABS_HAT0X, rbuf[9]);
		input_report_abs(akm->input, ABS_HAT0Y, rbuf[10]);
		input_report_abs(akm->input, ABS_HAT1X, rbuf[11]);
		input_report_abs(akm->input, ABS_HAT1Y, rbuf[4]);
	}

	input_sync(akm->input);
}

static int AKECS_GetOpenStatus(
	struct akm8963_data *akm)
{
	return wait_event_interruptible(
			akm->open_wq, (atomic_read(&akm->active) != 0));
}

static int AKECS_GetCloseStatus(
	struct akm8963_data *akm)
{
	return wait_event_interruptible(
			akm->open_wq, (atomic_read(&akm->active) <= 0));
}

static int AKECS_Open(struct inode *inode, struct file *file)
{
	file->private_data = s_akm;
	return nonseekable_open(inode, file);
}

static int AKECS_Release(struct inode *inode, struct file *file)
{
	return 0;
}

static long
AKECS_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct akm8963_data *akm = file->private_data;

	/* NOTE: In this function the size of "char" should be 1-byte. */
	char i2c_buf[RWBUF_SIZE];		/* for READ/WRITE */
	int8_t sensor_buf[SENSOR_DATA_SIZE];/* for GETDATA */
	int32_t ypr_buf[YPR_DATA_SIZE];	/* for SET_YPR */
	int16_t acc_buf[3];				/* for GET_ACCEL */
	int64_t delay[AKM_NUM_SENSORS];	/* for GET_DELAY */
	char mode;			/* for SET_MODE*/
	char layout;		/* for GET_LAYOUT */
	char outbit;		/* for GET_OUTBIT */
	int status;			/* for OPEN/CLOSE_STATUS */
	int ret = -1;		/* Return value. */

	switch (cmd) {
	case ECS_IOCTL_READ:
	case ECS_IOCTL_WRITE:
		if (argp == NULL) {
			dev_err(&akm->i2c->dev, "invalid argument.");
			return -EINVAL;
		}
		if (copy_from_user(&i2c_buf, argp, sizeof(i2c_buf))) {
			dev_err(&akm->i2c->dev, "copy_from_user failed.");
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_SET_MODE:
		if (argp == NULL) {
			dev_err(&akm->i2c->dev, "invalid argument.");
			return -EINVAL;
		}
		if (copy_from_user(&mode, argp, sizeof(mode))) {
			dev_err(&akm->i2c->dev, "copy_from_user failed.");
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_SET_YPR:
		if (argp == NULL) {
			dev_err(&akm->i2c->dev, "invalid argument.");
			return -EINVAL;
		}
		if (copy_from_user(&ypr_buf, argp, sizeof(ypr_buf))) {
			dev_err(&akm->i2c->dev, "copy_from_user failed.");
			return -EFAULT;
		}
	case ECS_IOCTL_GETDATA:
	case ECS_IOCTL_GET_OPEN_STATUS:
	case ECS_IOCTL_GET_CLOSE_STATUS:
	case ECS_IOCTL_GET_DELAY:
	case ECS_IOCTL_GET_LAYOUT:
	case ECS_IOCTL_GET_OUTBIT:
	case ECS_IOCTL_GET_ACCEL:
		/* Just check buffer pointer */
		if (argp == NULL) {
			dev_err(&akm->i2c->dev, "invalid argument.");
			return -EINVAL;
		}
		break;
		break;
	default:
		break;
	}

	switch (cmd) {
	case ECS_IOCTL_READ:
		dev_vdbg(&akm->i2c->dev, "IOCTL_READ called.");
		if ((i2c_buf[0] < 1) || (i2c_buf[0] > (RWBUF_SIZE-1))) {
			dev_err(&akm->i2c->dev, "invalid argument.");
			return -EINVAL;
		}
		ret = akm8963_i2c_rxdata(akm->i2c, &i2c_buf[1], i2c_buf[0]);
		if (ret < 0)
			return ret;
		break;
	case ECS_IOCTL_WRITE:
		dev_vdbg(&akm->i2c->dev, "IOCTL_WRITE called.");
		if ((i2c_buf[0] < 2) || (i2c_buf[0] > (RWBUF_SIZE-1))) {
			dev_err(&akm->i2c->dev, "invalid argument.");
			return -EINVAL;
		}
		ret = akm8963_i2c_txdata(akm->i2c, &i2c_buf[1], i2c_buf[0]);
		if (ret < 0)
			return ret;
		break;
	case ECS_IOCTL_SET_MODE:
		dev_vdbg(&akm->i2c->dev, "IOCTL_SET_MODE called.");
		ret = AKECS_SetMode(akm, mode);
		if (ret < 0)
			return ret;
		break;
	case ECS_IOCTL_GETDATA:
		dev_vdbg(&akm->i2c->dev, "IOCTL_GETDATA called.");
		ret = AKECS_GetData(akm, sensor_buf, SENSOR_DATA_SIZE);
		if (ret < 0)
			return ret;
		break;
	case ECS_IOCTL_SET_YPR:
		dev_vdbg(&akm->i2c->dev, "IOCTL_SET_YPR called.");
		AKECS_SetYPR(akm, ypr_buf);
		break;
	case ECS_IOCTL_GET_OPEN_STATUS:
		dev_vdbg(&akm->i2c->dev, "IOCTL_GET_OPEN_STATUS called.");
		ret = AKECS_GetOpenStatus(akm);
		if (ret < 0) {
			dev_err(&akm->i2c->dev,
				"Get Open returns error (%d).", ret);
		}
		break;
	case ECS_IOCTL_GET_CLOSE_STATUS:
		dev_vdbg(&akm->i2c->dev, "IOCTL_GET_CLOSE_STATUS called.");
		ret = AKECS_GetCloseStatus(akm);
		if (ret < 0) {
			dev_err(&akm->i2c->dev,
				"Get Close returns error (%d).", ret);
		}
		break;
	case ECS_IOCTL_GET_DELAY:
		dev_vdbg(&akm->i2c->dev, "IOCTL_GET_DELAY called.");
		mutex_lock(&akm->val_mutex);
		delay[0] = akm->delay[0];
		delay[1] = akm->delay[1];
		delay[2] = akm->delay[2];
		mutex_unlock(&akm->val_mutex);
		break;
	case ECS_IOCTL_GET_LAYOUT:
		dev_vdbg(&akm->i2c->dev, "IOCTL_GET_LAYOUT called.");
		layout = akm->layout;
		break;
	case ECS_IOCTL_GET_OUTBIT:
		dev_vdbg(&akm->i2c->dev, "IOCTL_GET_OUTBIT called.");
		outbit = akm->outbit;
		break;
	case ECS_IOCTL_RESET:
		ret = AKECS_Reset(akm, akm->rstn);
		if (ret < 0)
			return ret;
		break;
	case ECS_IOCTL_GET_ACCEL:
		dev_vdbg(&akm->i2c->dev, "IOCTL_GET_ACCEL called.");
		mutex_lock(&akm->accel_mutex);
		acc_buf[0] = akm->accel_data[0];
		acc_buf[1] = akm->accel_data[1];
		acc_buf[2] = akm->accel_data[2];
		mutex_unlock(&akm->accel_mutex);
		break;
	default:
		return -ENOTTY;
	}

	switch (cmd) {
	case ECS_IOCTL_READ:
		if (copy_to_user(argp, &i2c_buf, i2c_buf[0]+1)) {
			dev_err(&akm->i2c->dev, "copy_to_user failed.");
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_GETDATA:
		if (copy_to_user(argp, &sensor_buf, sizeof(sensor_buf))) {
			dev_err(&akm->i2c->dev, "copy_to_user failed.");
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_GET_OPEN_STATUS:
	case ECS_IOCTL_GET_CLOSE_STATUS:
		status = atomic_read(&akm->active);
		if (copy_to_user(argp, &status, sizeof(status))) {
			dev_err(&akm->i2c->dev, "copy_to_user failed.");
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_GET_DELAY:
		if (copy_to_user(argp, &delay, sizeof(delay))) {
			dev_err(&akm->i2c->dev, "copy_to_user failed.");
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_GET_LAYOUT:
		if (copy_to_user(argp, &layout, sizeof(layout))) {
			dev_err(&akm->i2c->dev, "copy_to_user failed.");
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_GET_OUTBIT:
		if (copy_to_user(argp, &outbit, sizeof(outbit))) {
			dev_err(&akm->i2c->dev, "copy_to_user failed.");
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_GET_ACCEL:
		if (copy_to_user(argp, &acc_buf, sizeof(acc_buf))) {
			dev_err(&akm->i2c->dev, "copy_to_user failed.");
			return -EFAULT;
		}
		break;
	default:
		break;
	}

	return 0;
}

/***** akm sysfs functions ******************************************/
static int create_device_attributes(
	struct device *dev,
	struct device_attribute *attrs)
{
	int i;
	int err = 0;

	for (i = 0 ; NULL != attrs[i].attr.name ; ++i) {
		err = device_create_file(dev, &attrs[i]);
		if (0 != err)
			break;
	}

	if (0 != err) {
		for (; i >= 0 ; --i)
			device_remove_file(dev, &attrs[i]);
	}

	return err;
}

static void remove_device_attributes(
	struct device *dev,
	struct device_attribute *attrs)
{
	int i;

	for (i = 0 ; NULL != attrs[i].attr.name ; ++i)
		device_remove_file(dev, &attrs[i]);
}

static int create_device_binary_attributes(
	struct kobject *kobj,
	struct bin_attribute *attrs)
{
	int i;
	int err = 0;

	err = 0;

	for (i = 0 ; NULL != attrs[i].attr.name ; ++i) {
		err = sysfs_create_bin_file(kobj, &attrs[i]);
		if (0 != err)
			break;
	}

	if (0 != err) {
		for (; i >= 0 ; --i)
			sysfs_remove_bin_file(kobj, &attrs[i]);
	}

	return err;
}

static void remove_device_binary_attributes(
	struct kobject *kobj,
	struct bin_attribute *attrs)
{
	int i;

	for (i = 0 ; NULL != attrs[i].attr.name ; ++i)
		sysfs_remove_bin_file(kobj, &attrs[i]);
}

static bool get_value_as_int(char const *buf, size_t size, int *value)
{
	long tmp;

	if (size == 0)
		return false;

	/* maybe text format value */
	if ((buf[0] == '0') && (size > 1)) {
		if ((buf[1] == 'x') || (buf[1] == 'X')) {
			/* hexadecimal format */
			if (0 != strict_strtol(buf, 16, &tmp))
				return false;
		} else {
			/* octal format */
			if (0 != strict_strtol(buf, 8, &tmp))
				return false;
		}
	} else {
		/* decimal format */
		if (0 != strict_strtol(buf, 10, &tmp))
			return false;
	}

	if (tmp > INT_MAX)
		return false;

	*value = tmp;

	return true;
}

static bool get_value_as_int64(char const *buf, size_t size, long long *value)
{
	long long tmp;

	if (size == 0)
		return false;

	/* maybe text format value */
	if ((buf[0] == '0') && (size > 1)) {
		if ((buf[1] == 'x') || (buf[1] == 'X')) {
			/* hexadecimal format */
			if (0 != strict_strtoll(buf, 16, &tmp))
				return false;
		} else {
			/* octal format */
			if (0 != strict_strtoll(buf, 8, &tmp))
				return false;
		}
	} else {
		/* decimal format */
		if (0 != strict_strtoll(buf, 10, &tmp))
			return false;
	}

	if (tmp > LLONG_MAX)
		return false;

	*value = tmp;

	return true;
}

/*********************************************************************
 *
 * SysFS attribute functions
 *
 * directory : /sys/class/compass/akm8963/
 * files :
 *  - enable_acc [rw] [t] : enable flag for accelerometer
 *  - enable_mag [rw] [t] : enable flag for magnetometer
 *  - enable_ori [rw] [t] : enable flag for orientation
 *  - delay_acc  [rw] [t] : delay in nanosecond for accelerometer
 *  - delay_mag  [rw] [t] : delay in nanosecond for magnetometer
 *  - delay_ori  [rw] [t] : delay in nanosecond for orientation
 *  - accel	     [w]  [b] : accelerometer data
 *
 * debug :
 *  - mode       [w]  [t] : AK8963's mode
 *  - bdata      [r]  [t] : raw data
 *  - asa        [r]  [t] : FUSEROM data
 *
 * [b] = binary format
 * [t] = text format
 */

/***** sysfs enable *************************************************/
static void akm8963_sysfs_update_active_status(
	struct akm8963_data *akm)
{
	uint32_t en;
	mutex_lock(&akm->val_mutex);
	en = akm->enable_flag;
	mutex_unlock(&akm->val_mutex);

	if (en == 0) {
		if (atomic_cmpxchg(&akm->active, 1, 0) == 1) {
			wake_up(&akm->open_wq);
			dev_dbg(akm->class_dev, "Deactivated");
		}
	} else {
		if (atomic_cmpxchg(&akm->active, 0, 1) == 0) {
			wake_up(&akm->open_wq);
			dev_dbg(akm->class_dev, "Activated");
		}
	}
	dev_dbg(&akm->i2c->dev,
		"Status updated: enable=0x%X, active=%d",
		en, atomic_read(&akm->active));
}

static ssize_t akm8963_sysfs_enable_show(
	struct akm8963_data *akm, char *buf, int pos)
{
	int flag;

	mutex_lock(&akm->val_mutex);
	flag = ((akm->enable_flag >> pos) & 1);
	mutex_unlock(&akm->val_mutex);

	return sprintf(buf, "%d\n", flag);
}

static ssize_t akm8963_sysfs_enable_store(
	struct akm8963_data *akm, char const *buf, size_t count, int pos)
{
	int en = 0;

	if (NULL == buf)
		return -EINVAL;

	if (0 == count)
		return 0;

	if (false == get_value_as_int(buf, count, &en))
		return -EINVAL;

	en = en ? 1 : 0;

	mutex_lock(&akm->val_mutex);
	akm->enable_flag &= ~(1<<pos);
	akm->enable_flag |= ((uint32_t)(en))<<pos;
	mutex_unlock(&akm->val_mutex);

	akm8963_sysfs_update_active_status(akm);

	return count;
}

/***** Acceleration ***/
static ssize_t akm8963_enable_acc_show(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	return akm8963_sysfs_enable_show(
		dev_get_drvdata(dev), buf, ACC_DATA_FLAG);
}
static ssize_t akm8963_enable_acc_store(
	struct device *dev, struct device_attribute *attr,
	char const *buf, size_t count)
{
	return akm8963_sysfs_enable_store(
		dev_get_drvdata(dev), buf, count, ACC_DATA_FLAG);
}

/***** Magnetic field ***/
static ssize_t akm8963_enable_mag_show(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	return akm8963_sysfs_enable_show(
		dev_get_drvdata(dev), buf, MAG_DATA_FLAG);
}
static ssize_t akm8963_enable_mag_store(
	struct device *dev, struct device_attribute *attr,
	char const *buf, size_t count)
{
	return akm8963_sysfs_enable_store(
		dev_get_drvdata(dev), buf, count, MAG_DATA_FLAG);
}

/***** Orientation ***/
static ssize_t akm8963_enable_ori_show(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	return akm8963_sysfs_enable_show(
		dev_get_drvdata(dev), buf, ORI_DATA_FLAG);
}
static ssize_t akm8963_enable_ori_store(
	struct device *dev, struct device_attribute *attr,
	char const *buf, size_t count)
{
	return akm8963_sysfs_enable_store(
		dev_get_drvdata(dev), buf, count, ORI_DATA_FLAG);
}

/***** sysfs delay **************************************************/
static ssize_t akm8963_sysfs_delay_show(
	struct akm8963_data *akm, char *buf, int pos)
{
	int64_t val;

	mutex_lock(&akm->val_mutex);
	val = akm->delay[pos];
	mutex_unlock(&akm->val_mutex);

	return sprintf(buf, "%lld\n", val);
}

static ssize_t akm8963_sysfs_delay_store(
	struct akm8963_data *akm, char const *buf, size_t count, int pos)
{
	long long val = 0;

	if (NULL == buf)
		return -EINVAL;

	if (0 == count)
		return 0;

	if (false == get_value_as_int64(buf, count, &val))
		return -EINVAL;

	mutex_lock(&akm->val_mutex);
	akm->delay[pos] = val;
	mutex_unlock(&akm->val_mutex);

	return count;
}

/***** Accelerometer ***/
static ssize_t akm8963_delay_acc_show(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	return akm8963_sysfs_delay_show(
		dev_get_drvdata(dev), buf, ACC_DATA_FLAG);
}
static ssize_t akm8963_delay_acc_store(
	struct device *dev, struct device_attribute *attr,
	char const *buf, size_t count)
{
	return akm8963_sysfs_delay_store(
		dev_get_drvdata(dev), buf, count, ACC_DATA_FLAG);
}

/***** Magnetic field ***/
static ssize_t akm8963_delay_mag_show(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	return akm8963_sysfs_delay_show(
		dev_get_drvdata(dev), buf, MAG_DATA_FLAG);
}
static ssize_t akm8963_delay_mag_store(
	struct device *dev, struct device_attribute *attr,
	char const *buf, size_t count)
{
	return akm8963_sysfs_delay_store(
		dev_get_drvdata(dev), buf, count, MAG_DATA_FLAG);
}

/***** Orientation ***/
static ssize_t akm8963_delay_ori_show(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	return akm8963_sysfs_delay_show(
		dev_get_drvdata(dev), buf, ORI_DATA_FLAG);
}
static ssize_t akm8963_delay_ori_store(
	struct device *dev, struct device_attribute *attr,
	char const *buf, size_t count)
{
	return akm8963_sysfs_delay_store(
		dev_get_drvdata(dev), buf, count, ORI_DATA_FLAG);
}


/***** accel (binary) ***/
static ssize_t akm8963_bin_accel_write(
	struct file *file,
	struct kobject *kobj,
	struct bin_attribute *attr,
			char *buf,
			loff_t pos,
			size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct akm8963_data *akm = dev_get_drvdata(dev);
	int16_t *accel_data;

	if (size == 0)
		return 0;

	accel_data = (int16_t*)buf;

	mutex_lock(&akm->accel_mutex);
	akm->accel_data[0] = accel_data[0];
	akm->accel_data[1] = accel_data[1];
	akm->accel_data[2] = accel_data[2];
	mutex_unlock(&akm->accel_mutex);

	dev_vdbg(&akm->i2c->dev, "accel:%d,%d,%d\n",
		accel_data[0], accel_data[1], accel_data[2]);

	return size;
}


#ifdef AKM8963_DEBUG_IF
static ssize_t akm8963_mode_store(
	struct device *dev, struct device_attribute *attr,
	char const *buf, size_t count)
{
	struct akm8963_data *akm = dev_get_drvdata(dev);
	int mode = 0;

	if (NULL == buf)
		return -EINVAL;

	if (0 == count)
		return 0;

	if (false == get_value_as_int(buf, count, &mode))
		return -EINVAL;

	if (AKECS_SetMode(akm, mode) < 0)
		return -EINVAL;

	return 1;
}

static ssize_t akm8963_bdata_show(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	struct akm8963_data *akm = dev_get_drvdata(dev);
	char rbuf[SENSOR_DATA_SIZE];

	mutex_lock(&akm->sensor_mutex);
	memcpy(&rbuf, akm->sense_data, sizeof(rbuf));
	mutex_unlock(&akm->sensor_mutex);

	return sprintf(buf,
		"0x%02X,0x%02X,0x%02X,0x%02X,"
		"0x%02X,0x%02X,0x%02X,0x%02X\n",
		rbuf[0],rbuf[1],rbuf[2],rbuf[3],
		rbuf[4],rbuf[5],rbuf[6],rbuf[7]);
}

static ssize_t akm8963_asa_show(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	struct akm8963_data *akm = dev_get_drvdata(dev);
	int err;
	unsigned char asa[3];

	err = AKECS_SetMode(akm, AK8963_MODE_FUSE_ACCESS);
	if (err < 0)
		return err;

	asa[0] = AK8963_FUSE_ASAX;
	err = akm8963_i2c_rxdata(akm->i2c, asa, 3);
	if (err < 0)
		return err;

	err = AKECS_SetMode(akm, AK8963_MODE_POWERDOWN);
	if (err < 0)
		return err;

	return sprintf(buf, "0x%02X,0x%02X,0x%02X\n",
		asa[0], asa[1], asa[2]);
}
#endif

static struct device_attribute akm8963_attributes[] = {
	__ATTR(enable_acc, 0660, akm8963_enable_acc_show, akm8963_enable_acc_store),
	__ATTR(enable_mag, 0660, akm8963_enable_mag_show, akm8963_enable_mag_store),
	__ATTR(enable_ori, 0660, akm8963_enable_ori_show, akm8963_enable_ori_store),
	__ATTR(delay_acc,  0660, akm8963_delay_acc_show,  akm8963_delay_acc_store),
	__ATTR(delay_mag,  0660, akm8963_delay_mag_show,  akm8963_delay_mag_store),
	__ATTR(delay_ori,  0660, akm8963_delay_ori_show,  akm8963_delay_ori_store),
#ifdef AKM8963_DEBUG_IF
	__ATTR(mode,  0220, NULL, akm8963_mode_store),
	__ATTR(bdata, 0440, akm8963_bdata_show, NULL),
	__ATTR(asa,   0440, akm8963_asa_show, NULL),
#endif
	__ATTR_NULL,
};

#define __BIN_ATTR(name_, mode_, size_, private_, read_, write_) \
	{ \
		.attr    = { .name = __stringify(name_), .mode = mode_ }, \
		.size    = size_, \
		.private = private_, \
		.read    = read_, \
		.write   = write_, \
	}

#define __BIN_ATTR_NULL \
	{ \
		.attr   = { .name = NULL }, \
	}

static struct bin_attribute akm8963_bin_attributes[] = {
	__BIN_ATTR(accel, 0220, 6, NULL,
				NULL, akm8963_bin_accel_write),
	__BIN_ATTR_NULL
};

static char const *const compass_class_name = "compass";
static char const *const akm8963_device_name = "akm8963";
static char const *const device_link_name = "i2c";
static dev_t const akm8963_device_dev_t = MKDEV(MISC_MAJOR, 240);

static int create_sysfs_interfaces(struct akm8963_data *akm)
{
	int err;

	if (NULL == akm)
		return -EINVAL;

	err = 0;

	akm->compass = class_create(THIS_MODULE, compass_class_name);
	if (IS_ERR(akm->compass)) {
		err = PTR_ERR(akm->compass);
		goto exit_class_create_failed;
	}

	akm->class_dev = device_create(
						akm->compass,
						NULL,
						akm8963_device_dev_t,
						akm,
						akm8963_device_name);
	if (IS_ERR(akm->class_dev)) {
		err = PTR_ERR(akm->class_dev);
		goto exit_class_device_create_failed;
	}

	err = sysfs_create_link(
			&akm->class_dev->kobj,
			&akm->i2c->dev.kobj,
			device_link_name);
	if (0 > err)
		goto exit_sysfs_create_link_failed;

	err = create_device_attributes(
			akm->class_dev,
			akm8963_attributes);
	if (0 > err)
		goto exit_device_attributes_create_failed;

	err = create_device_binary_attributes(
			&akm->class_dev->kobj,
			akm8963_bin_attributes);
	if (0 > err)
		goto exit_device_binary_attributes_create_failed;

	return err;

exit_device_binary_attributes_create_failed:
	remove_device_attributes(akm->class_dev, akm8963_attributes);
exit_device_attributes_create_failed:
	sysfs_remove_link(&akm->class_dev->kobj, device_link_name);
exit_sysfs_create_link_failed:
	device_destroy(akm->compass, akm8963_device_dev_t);
exit_class_device_create_failed:
	akm->class_dev = NULL;
	class_destroy(akm->compass);
exit_class_create_failed:
	akm->compass = NULL;
	return err;
}

static void remove_sysfs_interfaces(struct akm8963_data *akm)
{
	if (NULL == akm)
		return;

	if (NULL != akm->class_dev) {
		remove_device_binary_attributes(
			&akm->class_dev->kobj,
			akm8963_bin_attributes);
		remove_device_attributes(
			akm->class_dev,
			akm8963_attributes);
		sysfs_remove_link(
			&akm->class_dev->kobj,
			device_link_name);
		akm->class_dev = NULL;
	}
	if (NULL != akm->compass) {
		device_destroy(
			akm->compass,
			akm8963_device_dev_t);
		class_destroy(akm->compass);
		akm->compass = NULL;
	}
}


/***** akm input device functions ***********************************/
static int akm8963_input_init(
	struct input_dev **input)
{
	int err = 0;

	/* Declare input device */
	*input = input_allocate_device();
	if (!*input)
		return -ENOMEM;

	/* Setup input device */
	set_bit(EV_ABS, (*input)->evbit);
	/* Accelerometer (720 x 16G)*/
	input_set_abs_params(*input, ABS_X,
			-11520, 11520, 0, 0);
	input_set_abs_params(*input, ABS_Y,
			-11520, 11520, 0, 0);
	input_set_abs_params(*input, ABS_Z,
			-11520, 11520, 0, 0);
	input_set_abs_params(*input, ABS_THROTTLE,
			0, 3, 0, 0);
	/* Magnetic field (-81900, 81900) -> limited to 16bit*/
	input_set_abs_params(*input, ABS_RX,
			-32768, 32767, 0, 0);
	input_set_abs_params(*input, ABS_RY,
			-32768, 32767, 0, 0);
	input_set_abs_params(*input, ABS_RZ,
			-32768, 32767, 0, 0);
	input_set_abs_params(*input, ABS_RUDDER,
			0, 3, 0, 0);
	/* Orientation (yaw:0,360 pitch:-180,180 roll:-90,90) */
	input_set_abs_params(*input, ABS_HAT0X,
			0, 23040, 0, 0);
	input_set_abs_params(*input, ABS_HAT0Y,
			-11520, 11520, 0, 0);
	input_set_abs_params(*input, ABS_HAT1X,
			-5760, 5760, 0, 0);
	input_set_abs_params(*input, ABS_HAT1Y,
			0, 3, 0, 0);

	/* Set name */
	(*input)->name = "compass";

	/* Register */
	err = input_register_device(*input);
	if (err) {
		input_free_device(*input);
		return err;
	}

	return err;
}

/***** akm functions ************************************************/

static irqreturn_t akm8963_irq(int irq, void *handle)
{
	struct akm8963_data *akm = handle;
	char buffer[SENSOR_DATA_SIZE];
	int err;
	
	memset(buffer, 0, sizeof(buffer));
	buffer[0] = AK8963_REG_ST1;
	err = akm8963_i2c_rxdata(akm->i2c, buffer, SENSOR_DATA_SIZE);
	if (err < 0) {
		dev_err(&akm->i2c->dev, "%s failed.", __func__);
		goto work_func_end;
	}
	/* Check ST bit */
	if ((buffer[0] & 0x01) != 0x01) {
		dev_err(&akm->i2c->dev, "%s ST is not set.", __func__);
		goto work_func_end;
	}

	mutex_lock(&akm->sensor_mutex);
	memcpy(akm->sense_data, buffer, SENSOR_DATA_SIZE);
	mutex_unlock(&akm->sensor_mutex);

	atomic_set(&akm->drdy, 1);
	atomic_set(&akm->is_busy, 0);
	wake_up(&akm->drdy_wq);

work_func_end:
	return IRQ_HANDLED;
}

static void akm8963_delayed_work(struct work_struct *work)
{
	struct akm8963_data *akm = container_of(
		work, struct akm8963_data, work.work);

	akm8963_irq(akm->irq, akm);
}

static int akm8963_suspend(struct device *dev)
{
	struct akm8963_data *akm = dev_get_drvdata(dev);
	dev_dbg(&akm->i2c->dev, "suspended\n");

	return 0;
}

static int akm8963_resume(struct device *dev)
{
	struct akm8963_data *akm = dev_get_drvdata(dev);
	dev_dbg(&akm->i2c->dev, "resumed\n");

	return 0;
}

int akm8963_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct akm8963_platform_data *pdata;
	int err = 0;
	int i;

	dev_dbg(&client->dev, "start probing.");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "%s: check_functionality failed.", __func__);
		err = -ENODEV;
		goto exit0;
	}

	/* Allocate memory for driver data */
	s_akm = kzalloc(sizeof(struct akm8963_data), GFP_KERNEL);
	if (!s_akm) {
		dev_err(&client->dev, "%s: memory allocation failed.", __func__);
		err = -ENOMEM;
		goto exit1;
	}

	/***** Set layout information *****/
	pdata = client->dev.platform_data;
	if (pdata) {
		/* Platform data is available. copy its value to local. */
		s_akm->layout = pdata->layout;
		s_akm->outbit = pdata->outbit;
		s_akm->rstn = pdata->gpio_RST;
	} else {
		/* Platform data is not available.
		   Layout and Outbit information should be set by each application. */
		dev_dbg(&client->dev, "%s: No platform data.", __func__);
		s_akm->layout = 0;
		s_akm->outbit = 0;
		s_akm->rstn = 0;
	}

	/***** I2C initialization *****/
	s_akm->i2c = client;
	/* check connection */
	err = akm8963_i2c_check_device(client);
	if (err < 0)
		goto exit2;
	/* set client data */
	i2c_set_clientdata(client, s_akm);


	/***** input *****/
	err = akm8963_input_init(&s_akm->input);
	if (err) {
		dev_err(&client->dev,
			"%s: input_dev register failed", __func__);
		goto exit3;
	}
	input_set_drvdata(s_akm->input, s_akm);


	/**** initialize variables in akm8963_data *****/
	init_waitqueue_head(&s_akm->drdy_wq);
	init_waitqueue_head(&s_akm->open_wq);

	mutex_init(&s_akm->sensor_mutex);
	mutex_init(&s_akm->accel_mutex);
	mutex_init(&s_akm->val_mutex);

	atomic_set(&s_akm->active, 0);
	atomic_set(&s_akm->is_busy, 0);
	atomic_set(&s_akm->drdy, 0);
	atomic_set(&s_akm->suspend, 0);

	s_akm->enable_flag = 0;
	for (i=0; i<AKM_NUM_SENSORS; i++)
		s_akm->delay[i] = -1;


	/***** IRQ setup *****/
	s_akm->irq = client->irq;


	if (s_akm->irq == 0) {
		dev_dbg(&client->dev, "%s: IRQ is not set.", __func__);
		/* Use timer to notify measurement end */
		INIT_DELAYED_WORK(&s_akm->work, akm8963_delayed_work);
	} else {
		err = request_threaded_irq(
				s_akm->irq,
				NULL,
				akm8963_irq,
				IRQF_TRIGGER_HIGH|IRQF_ONESHOT,
				dev_name(&client->dev),
				s_akm);
		if (err < 0) {
			dev_err(&client->dev,
				"%s: request irq failed.", __func__);
			goto exit4;
		}
	}

	/***** misc *****/
	err = misc_register(&akm8963_dev);
	if (err) {
		dev_err(&client->dev,
			"%s: akm8963_dev register failed", __func__);
		goto exit5;
	}

	/***** sysfs *****/
	err = create_sysfs_interfaces(s_akm);
	if (0 > err) {
		dev_err(&client->dev,
			"%s: create sysfs failed.", __func__);
		goto exit6;
	}

	dev_info(&client->dev, "successfully probed.");
	return 0;

exit6:
	misc_deregister(&akm8963_dev);
exit5:
	if (s_akm->irq)
		free_irq(s_akm->irq, s_akm);
exit4:
	input_unregister_device(s_akm->input);
exit3:
exit2:
	kfree(s_akm);
exit1:
exit0:
	return err;
}

static int akm8963_remove(struct i2c_client *client)
{
	struct akm8963_data *akm = i2c_get_clientdata(client);

	remove_sysfs_interfaces(akm);
	if (misc_deregister(&akm8963_dev) < 0)
		dev_err(&client->dev, "misc deregister failed.");
	if (akm->irq)
		free_irq(akm->irq, akm);
	input_unregister_device(akm->input);
	kfree(akm);
	dev_info(&client->dev, "successfully removed.");
	return 0;
}

static const struct i2c_device_id akm8963_id[] = {
	{AKM8963_I2C_NAME, 0 },
	{ }
};

static const struct dev_pm_ops akm8963_pm_ops = {
	.suspend    = akm8963_suspend,
	.resume     = akm8963_resume,
};

static struct i2c_driver akm8963_driver = {
	.probe		= akm8963_probe,
	.remove 	= akm8963_remove,
	.id_table	= akm8963_id,
	.driver = {
		.name	= AKM8963_I2C_NAME,
		.pm		= &akm8963_pm_ops,
	},
};

static int __init akm8963_init(void)
{
	printk(KERN_INFO "AKM8963 compass driver: initialize.");
	return i2c_add_driver(&akm8963_driver);
}

static void __exit akm8963_exit(void)
{
	printk(KERN_INFO "AKM8963 compass driver: release.");
	i2c_del_driver(&akm8963_driver);
}

module_init(akm8963_init);
module_exit(akm8963_exit);

MODULE_AUTHOR("viral wang <viral_wang@htc.com>");
MODULE_DESCRIPTION("AKM8963 compass driver");
MODULE_LICENSE("GPL");


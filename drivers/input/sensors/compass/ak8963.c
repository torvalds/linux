/* drivers/input/sensors/access/akm8963.c
 *
 * Copyright (C) 2012-2015 ROCKCHIP.
 * Author: luowei <lw@rock-chips.com>
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
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/sensor-dev.h>

#define AKM_SENSOR_INFO_SIZE	2
#define AKM_SENSOR_CONF_SIZE	3
#define SENSOR_DATA_SIZE		8
#define YPR_DATA_SIZE			12
#define RWBUF_SIZE				16

#define ACC_DATA_FLAG			0
#define MAG_DATA_FLAG			1
#define ORI_DATA_FLAG			2
#define AKM_NUM_SENSORS		3

#define ACC_DATA_READY			(1 << (ACC_DATA_FLAG))
#define MAG_DATA_READY		(1 << (MAG_DATA_FLAG))
#define ORI_DATA_READY			(1 << (ORI_DATA_FLAG))

#define AK8963_MEASUREMENT_TIME_US	10000

#define AK8963_MODE_SNG_MEASURE	0x01
#define	AK8963_MODE_SELF_TEST	0x08
#define	AK8963_MODE_FUSE_ACCESS	0x0F
#define	AK8963_MODE_POWERDOWN	0x00

#define AK8963_REG_WIA		0x00
#define AK8963_REG_INFO	0x01
#define AK8963_REG_ST1		0x02
#define AK8963_REG_HXL		0x03
#define AK8963_REG_HXH		0x04
#define AK8963_REG_HYL		0x05
#define AK8963_REG_HYH		0x06
#define AK8963_REG_HZL		0x07
#define AK8963_REG_HZH		0x08
#define AK8963_REG_ST2		0x09
#define AK8963_REG_CNTL1	0x0A
#define AK8963_REG_CNTL2	0x0B
#define AK8963_REG_ASTC	0x0C
#define AK8963_REG_TS1		0x0D
#define AK8963_REG_TS2		0x0E
#define AK8963_REG_I2CDIS	0x0F

#define AK8963_WIA_VALUE	0x48

#define AK8963_FUSE_ASAX	0x10
#define AK8963_FUSE_ASAY	0x11
#define AK8963_FUSE_ASAZ	0x12

#define AK8963_INFO_DATA	(0x03 << 3)

#define COMPASS_IOCTL_MAGIC                   'c'

/* IOCTLs for AKM library */
#define ECS_IOCTL_WRITE				_IOW(COMPASS_IOCTL_MAGIC, 0x01, char*)
#define ECS_IOCTL_READ					_IOWR(COMPASS_IOCTL_MAGIC, 0x02, char*)
#define ECS_IOCTL_RESET				_IO(COMPASS_IOCTL_MAGIC, 0x03)
#define ECS_IOCTL_SET_MODE			_IOW(COMPASS_IOCTL_MAGIC, 0x04, short)
#define ECS_IOCTL_GETDATA				_IOR(COMPASS_IOCTL_MAGIC, 0x05, char[SENSOR_DATA_SIZE])
#define ECS_IOCTL_SET_YPR				_IOW(COMPASS_IOCTL_MAGIC, 0x06, short[12])
#define ECS_IOCTL_GET_OPEN_STATUS	_IOR(COMPASS_IOCTL_MAGIC, 0x07, int)
#define ECS_IOCTL_GET_CLOSE_STATUS	_IOR(COMPASS_IOCTL_MAGIC, 0x08, int)
#define ECS_IOCTL_GET_LAYOUT			_IOR(COMPASS_IOCTL_MAGIC, 0x09, char)
#define ECS_IOCTL_GET_ACCEL			_IOR(COMPASS_IOCTL_MAGIC, 0x0A, short[3])
#define ECS_IOCTL_GET_OUTBIT			_IOR(COMPASS_IOCTL_MAGIC, 0x0B, char)
#define ECS_IOCTL_GET_DELAY			_IOR(COMPASS_IOCTL_MAGIC, 0x30, short)
#define ECS_IOCTL_GET_PROJECT_NAME	_IOR(COMPASS_IOCTL_MAGIC, 0x0D, char[64])
#define ECS_IOCTL_GET_MATRIX			_IOR(COMPASS_IOCTL_MAGIC, 0x0E, short [4][3][3])
#define ECS_IOCTL_GET_PLATFORM_DATA	_IOR(COMPASS_IOCTL_MAGIC, 0x0E, struct akm_platform_data)
#define ECS_IOCTL_GET_INFO				_IOR(COMPASS_IOCTL_MAGIC, 0x27, unsigned char[AKM_SENSOR_INFO_SIZE])
#define ECS_IOCTL_GET_CONF				_IOR(COMPASS_IOCTL_MAGIC, 0x28, unsigned char[AKM_SENSOR_CONF_SIZE])

#define AK8963_DEVICE_ID				0x48
static struct i2c_client *this_client;
static struct miscdevice compass_dev_device;

static int g_akm_rbuf_ready;
static int g_akm_rbuf[12];

/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;

	if (enable)
		sensor->ops->ctrl_data = AK8963_MODE_SNG_MEASURE;
	else
		sensor->ops->ctrl_data = AK8963_MODE_POWERDOWN;

	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if (result)
		pr_err("%s:fail to active sensor\n", __func__);

	return result;
}

static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;
	char info = 0;

	this_client = client;

	result = sensor->ops->active(client, 0, 0);
	if (result) {
		pr_err("%s:line=%d,error\n", __func__, __LINE__);
		return result;
	}

	sensor->status_cur = SENSOR_OFF;

	info = sensor_read_reg(client, AK8963_REG_INFO);
	if ((info & (0x0f << 3)) != AK8963_INFO_DATA) {
		pr_err("%s:info=0x%x,it is not %s\n", __func__, info, sensor->ops->name);
		return -1;
	}

	result = misc_register(&compass_dev_device);
	if (result < 0) {
		pr_err("%s:fail to register misc device %s\n", __func__, compass_dev_device.name);
		result = -1;
	}

	return result;
}

static void compass_report_value(void)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *)i2c_get_clientdata(this_client);
	static int flag;

	if (!g_akm_rbuf_ready) {
		pr_info("g_akm_rbuf not ready..............\n");
		return;
	}

	/* Report magnetic vector information */
	if (atomic_read(&sensor->flags.mv_flag) && (g_akm_rbuf[0] & MAG_DATA_READY)) {
		/*
		 *input dev will ignore report data if data value is the same with last_value,
		 *sample rate will not enough by this way, so just avoid this case
		 */
		if ((sensor->axis.x == g_akm_rbuf[5]) &&
			(sensor->axis.y == g_akm_rbuf[6]) && (sensor->axis.z == g_akm_rbuf[7])) {
			if (flag) {
				flag = 0;
				sensor->axis.x += 1;
				sensor->axis.y += 1;
				sensor->axis.z += 1;
			} else {
				flag = 1;
				sensor->axis.x -= 1;
				sensor->axis.y -= 1;
				sensor->axis.z -= 1;
			}
		} else {
			sensor->axis.x = g_akm_rbuf[5];
			sensor->axis.y = g_akm_rbuf[6];
			sensor->axis.z = g_akm_rbuf[7];
		}
		input_report_abs(sensor->input_dev, ABS_HAT0X, sensor->axis.x);
		input_report_abs(sensor->input_dev, ABS_HAT0Y, sensor->axis.y);
		input_report_abs(sensor->input_dev, ABS_BRAKE, sensor->axis.z);
		input_report_abs(sensor->input_dev, ABS_HAT1X, g_akm_rbuf[8]);
	}
	input_sync(sensor->input_dev);
}

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor = (struct sensor_private_data *)i2c_get_clientdata(client);
	char buffer[8] = {0};
	unsigned char *stat;
	unsigned char *stat2;
	int ret = 0;
	char value = 0;

	mutex_lock(&sensor->data_mutex);
	compass_report_value();
	mutex_unlock(&sensor->data_mutex);

	if (sensor->ops->read_len < 8) {
		pr_err("%s:length is error,len=%d\n", __func__, sensor->ops->read_len);
		return -1;
	}

	memset(buffer, 0, 8);

	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	do {
		*buffer = sensor->ops->read_reg;
		ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
		if (ret < 0)
			return ret;
	} while (0);

	stat = &buffer[0];
	stat2 = &buffer[7];

	/*
	 * ST : data ready -
	 * Measurement has been completed and data is ready to be read.
	 */
	if ((*stat & 0x01) != 0x01) {
		pr_err("%s:ST is not set\n", __func__);
		return -1;
	}

	/*
	 * ST2 : data error -
	 * occurs when data read is started outside of a readable period;
	 * data read would not be correct.
	 * Valid in continuous measurement mode only.
	 * In single measurement mode this error should not occour but we
	 * stil account for it and return an error, since the data would be
	 * corrupted.
	 * DERR bit is self-clearing when ST2 register is read.
	 */
	if (*stat2 & 0x04) {
		pr_err("%s:compass data error\n", __func__);
		return -2;
	}

	/*
	 * ST2 : overflow -
	 * the sum of the absolute values of all axis |X|+|Y|+|Z| < 2400uT.
	 * This is likely to happen in presence of an external magnetic
	 * disturbance; it indicates, the sensor data is incorrect and should
	 * be ignored.
	 * An error is returned.
	 * HOFL bit clears when a new measurement starts.
	 */
	if (*stat2 & 0x08) {
		pr_err("%s:compass data overflow\n", __func__);
		return -3;
	}

	mutex_lock(&sensor->data_mutex);
	memcpy(sensor->sensor_data, buffer, sensor->ops->read_len);
	mutex_unlock(&sensor->data_mutex);

	if ((sensor->pdata->irq_enable) && (sensor->ops->int_status_reg >= 0))
		value = sensor_read_reg(client, sensor->ops->int_status_reg);

	ret = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if (ret) {
		pr_err("%s:fail to set ctrl_data:0x%x\n", __func__, sensor->ops->ctrl_data);
		return ret;
	}

	return ret;
}

static void compass_set_YPR(int *rbuf)
{
	/* No events are reported */
	if (!rbuf[0]) {
		pr_err("%s:Don't waste a time.", __func__);
		return;
	}

	g_akm_rbuf_ready = 1;
	memcpy(g_akm_rbuf, rbuf, 12 * sizeof(int));
}

static int compass_dev_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int compass_dev_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int compass_akm_set_mode(struct i2c_client *client, char mode)
{
	struct sensor_private_data *sensor = (struct sensor_private_data *)i2c_get_clientdata(this_client);
	int result = 0;

	switch (mode & 0x0f) {
	case AK8963_MODE_SNG_MEASURE:
	case AK8963_MODE_SELF_TEST:
	case AK8963_MODE_FUSE_ACCESS:
		if (sensor->status_cur == SENSOR_OFF) {
			sensor->stop_work = 0;
			sensor->status_cur = SENSOR_ON;
			pr_info("compass ak09911 start measure");
			schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
		}
		break;

	case AK8963_MODE_POWERDOWN:
		if (sensor->status_cur == SENSOR_ON) {
			sensor->stop_work = 1;
			cancel_delayed_work_sync(&sensor->delaywork);
			pr_info("compass ak09911 stop measure");
			g_akm_rbuf_ready = 0;
			sensor->status_cur = SENSOR_OFF;
		}
		break;
	}

	switch (mode & 0x0f) {
	case AK8963_MODE_SNG_MEASURE:
		result = sensor_write_reg(client, sensor->ops->ctrl_reg, mode);
		if (result)
			pr_err("%s:i2c error,mode=%d\n", __func__, mode);
		break;
	case AK8963_MODE_SELF_TEST:
		result = sensor_write_reg(client, sensor->ops->ctrl_reg, mode);
		if (result)
			pr_err("%s:i2c error,mode=%d\n", __func__, mode);
		break;
	case AK8963_MODE_FUSE_ACCESS:
		result = sensor_write_reg(client, sensor->ops->ctrl_reg, mode);
		if (result)
			pr_err("%s:i2c error,mode=%d\n", __func__, mode);
		break;
	case AK8963_MODE_POWERDOWN:
		/* Set powerdown mode */
		result = sensor_write_reg(client, sensor->ops->ctrl_reg, AK8963_MODE_POWERDOWN);
		if (result)
			pr_err("%s:i2c error,mode=%d\n", __func__, mode);
		udelay(100);
		break;
	default:
		pr_err("%s: Unknown mode(%d)", __func__, mode);
		result = -EINVAL;
		break;
	}

	return result;
}

static int compass_akm_reset(struct i2c_client *client)
{
	struct sensor_private_data *sensor = (struct sensor_private_data *)i2c_get_clientdata(this_client);
	int result = 0;

	if (sensor->pdata->reset_pin > 0) {
		gpio_direction_output(sensor->pdata->reset_pin, GPIO_LOW);
		udelay(10);
		gpio_direction_output(sensor->pdata->reset_pin, GPIO_HIGH);
	} else {
		/* Set measure mode */
		result = sensor_write_reg(client, AK8963_REG_CNTL2, AK8963_MODE_SNG_MEASURE);
		if (result)
			pr_err("%s:fail to Set measure mode\n", __func__);
	}

	udelay(100);

	return result;
}

static int compass_akm_get_openstatus(void)
{
	struct sensor_private_data *sensor = (struct sensor_private_data *)i2c_get_clientdata(this_client);

	wait_event_interruptible(sensor->flags.open_wq, (atomic_read(&sensor->flags.open_flag) != 0));

	return atomic_read(&sensor->flags.open_flag);
}

static int compass_akm_get_closestatus(void)
{
	struct sensor_private_data *sensor = (struct sensor_private_data *)i2c_get_clientdata(this_client);

	wait_event_interruptible(sensor->flags.open_wq, (atomic_read(&sensor->flags.open_flag) <= 0));

	return atomic_read(&sensor->flags.open_flag);
}

/* ioctl - I/O control */
static long compass_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = (struct sensor_private_data *)i2c_get_clientdata(this_client);
	struct i2c_client *client = this_client;
	void __user *argp = (void __user *)arg;
	int result = 0;
	struct akm_platform_data compass;
	unsigned char sense_info[AKM_SENSOR_INFO_SIZE];
	unsigned char sense_conf[AKM_SENSOR_CONF_SIZE];

	/* NOTE: In this function the size of "char" should be 1-byte. */
	char compass_data[SENSOR_DATA_SIZE];	/* for GETDATA */
	char rwbuf[RWBUF_SIZE];		/* for READ/WRITE */
	char mode;		/* for SET_MODE*/
	int value[12];		/* for SET_YPR */
	int status;		/* for OPEN/CLOSE_STATUS */
	int ret = -1;		/* Return value. */

	int16_t acc_buf[3];		/* for GET_ACCEL */
	int64_t delay[AKM_NUM_SENSORS];	/* for GET_DELAY */

	char layout;		/* for GET_LAYOUT */
	char outbit;		/* for GET_OUTBIT */

	switch (cmd) {
	case ECS_IOCTL_WRITE:
	case ECS_IOCTL_READ:
		if (!argp)
			return -EINVAL;
		if (copy_from_user(&rwbuf, argp, sizeof(rwbuf)))
			return -EFAULT;
		break;
	case ECS_IOCTL_SET_MODE:
		if (!argp)
			return -EINVAL;
		if (copy_from_user(&mode, argp, sizeof(mode)))
			return -EFAULT;
		break;
	case ECS_IOCTL_SET_YPR:
		if (!argp)
			return -EINVAL;
		if (copy_from_user(&value, argp, sizeof(value)))
			return -EFAULT;
		break;
	case ECS_IOCTL_GET_INFO:
	case ECS_IOCTL_GET_CONF:
	case ECS_IOCTL_GETDATA:
	case ECS_IOCTL_GET_OPEN_STATUS:
	case ECS_IOCTL_GET_CLOSE_STATUS:
	case ECS_IOCTL_GET_DELAY:
	case ECS_IOCTL_GET_LAYOUT:
	case ECS_IOCTL_GET_OUTBIT:
	case ECS_IOCTL_GET_ACCEL:
		/* Just check buffer pointer */
		if (!argp) {
			pr_err("%s:invalid argument\n", __func__);
			return -EINVAL;
		}
		break;
	default:
		break;
	}

	switch (cmd) {
	case ECS_IOCTL_GET_INFO:
		sense_info[0] = AK8963_REG_WIA;
		mutex_lock(&sensor->operation_mutex);
		ret = sensor_rx_data(client, &sense_info[0], AKM_SENSOR_INFO_SIZE);
		mutex_unlock(&sensor->operation_mutex);
		if (ret < 0) {
			pr_err("%s:fait to get sense_info\n", __func__);
			return ret;
		}
		/* Check read data */
		if (sense_info[0] != AK8963_WIA_VALUE) {
			dev_err(&client->dev,
				"%s: The device is not AKM Compass.", __func__);
			return -ENXIO;
		}
		break;
	case ECS_IOCTL_GET_CONF:
		sense_conf[0] = AK8963_FUSE_ASAX;
		mutex_lock(&sensor->operation_mutex);
		ret = sensor_rx_data(client, &sense_conf[0], AKM_SENSOR_CONF_SIZE);
		mutex_unlock(&sensor->operation_mutex);
		if (ret < 0) {
			pr_err("%s:fait to get sense_conf\n", __func__);
			return ret;
		}
		break;
	case ECS_IOCTL_WRITE:
		mutex_lock(&sensor->operation_mutex);
		if ((rwbuf[0] < 2) || (rwbuf[0] > (RWBUF_SIZE - 1))) {
			mutex_unlock(&sensor->operation_mutex);
			return -EINVAL;
		}
		ret = sensor_tx_data(client, &rwbuf[1], rwbuf[0]);
		if (ret < 0) {
			mutex_unlock(&sensor->operation_mutex);
			pr_err("%s:fait to tx data\n", __func__);
			return ret;
		}
		mutex_unlock(&sensor->operation_mutex);
		break;
	case ECS_IOCTL_READ:
		mutex_lock(&sensor->operation_mutex);
		if ((rwbuf[0] < 1) || (rwbuf[0] > (RWBUF_SIZE - 1))) {
			mutex_unlock(&sensor->operation_mutex);
			pr_err("%s:data is error\n", __func__);
			return -EINVAL;
		}
		ret = sensor_rx_data(client, &rwbuf[1], rwbuf[0]);
		if (ret < 0) {
			mutex_unlock(&sensor->operation_mutex);
			pr_err("%s:fait to rx data\n", __func__);
			return ret;
		}
		mutex_unlock(&sensor->operation_mutex);
		break;
	case ECS_IOCTL_SET_MODE:
		mutex_lock(&sensor->operation_mutex);
		if (sensor->ops->ctrl_data != mode) {
			ret = compass_akm_set_mode(client, mode);
			if (ret < 0) {
				pr_err("%s:fait to set mode\n", __func__);
				mutex_unlock(&sensor->operation_mutex);
				return ret;
			}

			sensor->ops->ctrl_data = mode;
		}
		mutex_unlock(&sensor->operation_mutex);
		break;
	case ECS_IOCTL_GETDATA:
			mutex_lock(&sensor->data_mutex);
			memcpy(compass_data, sensor->sensor_data, SENSOR_DATA_SIZE);
			mutex_unlock(&sensor->data_mutex);
			break;
	case ECS_IOCTL_SET_YPR:
			mutex_lock(&sensor->data_mutex);
			compass_set_YPR(value);
			mutex_unlock(&sensor->data_mutex);
		break;
	case ECS_IOCTL_GET_OPEN_STATUS:
		status = compass_akm_get_openstatus();
		break;
	case ECS_IOCTL_GET_CLOSE_STATUS:
		status = compass_akm_get_closestatus();
		break;
	case ECS_IOCTL_GET_DELAY:
		mutex_lock(&sensor->operation_mutex);
		delay[0] = sensor->flags.delay;
		delay[1] = sensor->flags.delay;
		delay[2] = sensor->flags.delay;
		mutex_unlock(&sensor->operation_mutex);
		break;

	case ECS_IOCTL_GET_PLATFORM_DATA:
		ret = copy_to_user(argp, &compass, sizeof(compass));
		if (ret < 0) {
			pr_err("%s:error,ret=%d\n", __func__, ret);
			return ret;
		}
		break;
	case ECS_IOCTL_GET_LAYOUT:
		layout = sensor->pdata->layout;
		break;
	case ECS_IOCTL_GET_OUTBIT:
		outbit = 1;
		break;
	case ECS_IOCTL_RESET:
		ret = compass_akm_reset(client);
		if (ret < 0)
			return ret;
		break;
	case ECS_IOCTL_GET_ACCEL:
		break;

	default:
		return -ENOTTY;
	}

	switch (cmd) {
	case ECS_IOCTL_READ:
		if (copy_to_user(argp, &rwbuf, rwbuf[0] + 1))
			return -EFAULT;
		break;
	case ECS_IOCTL_GETDATA:
		if (copy_to_user(argp, &compass_data, sizeof(compass_data)))
			return -EFAULT;
		break;
	case ECS_IOCTL_GET_OPEN_STATUS:
	case ECS_IOCTL_GET_CLOSE_STATUS:
		if (copy_to_user(argp, &status, sizeof(status)))
			return -EFAULT;
		break;
	case ECS_IOCTL_GET_DELAY:
		if (copy_to_user(argp, &delay, sizeof(delay)))
			return -EFAULT;
		break;
	case ECS_IOCTL_GET_LAYOUT:
		if (copy_to_user(argp, &layout, sizeof(layout))) {
			pr_err("%s:error:%d\n", __func__, __LINE__);
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_GET_OUTBIT:
		if (copy_to_user(argp, &outbit, sizeof(outbit))) {
			pr_err("%s:error:%d\n", __func__, __LINE__);
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_GET_ACCEL:
		if (copy_to_user(argp, &acc_buf, sizeof(acc_buf))) {
			pr_err("%s:error:%d\n", __func__, __LINE__);
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_GET_INFO:
		if (copy_to_user(argp, &sense_info,	sizeof(sense_info))) {
			pr_err("%s:error:%d\n", __func__, __LINE__);
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_GET_CONF:
		if (copy_to_user(argp, &sense_conf,	sizeof(sense_conf))) {
			pr_err("%s:error:%d\n", __func__, __LINE__);
			return -EFAULT;
		}
		break;
	default:
		break;
	}

	return result;
}

static const struct file_operations compass_dev_fops = {
	.owner = THIS_MODULE,
	.open = compass_dev_open,
	.release = compass_dev_release,
	.unlocked_ioctl = compass_dev_ioctl,
};

static struct miscdevice compass_dev_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "akm8963_dev",
	.fops = &compass_dev_fops,
};

struct sensor_operate compass_akm8963_ops = {
	.name				= "akm8963",
	.type				= SENSOR_TYPE_COMPASS,
	.id_i2c				= COMPASS_ID_AK8963,
	.read_reg				= AK8963_REG_ST1,
	.read_len				= SENSOR_DATA_SIZE,
	.id_reg				= AK8963_REG_WIA,
	.id_data				= AK8963_DEVICE_ID,
	.precision				= 8,
	.ctrl_reg				= AK8963_REG_CNTL1,
	.int_status_reg		= SENSOR_UNKNOW_DATA,
	.range				= {-0xffff, 0xffff},
	.trig					= IRQF_TRIGGER_RISING,
	.active				= sensor_active,
	.init					= sensor_init,
	.report				= sensor_report_value,
	.misc_dev			= NULL,
};

/****************operate according to sensor chip:end************/

static struct sensor_operate *compass_get_ops(void)
{
	return &compass_akm8963_ops;
}

static int __init compass_akm8963_init(void)
{
	struct sensor_operate *ops = compass_get_ops();
	int result = 0;
	int type = ops->type;

	result = sensor_register_slave(type, NULL, NULL, compass_get_ops);

	return result;
}

static void __exit compass_akm8963_exit(void)
{
	struct sensor_operate *ops = compass_get_ops();
	int type = ops->type;

	sensor_unregister_slave(type, NULL, NULL, compass_get_ops);
}

module_init(compass_akm8963_init);
module_exit(compass_akm8963_exit);

// SPDX-License-Identifier: GPL-2.0
/*
 * kernel/drivers/input/sensors/compass/akm09918.c
 *
 * Copyright (C) 2020 Rockchip Co.,Ltd.
 * Author: Wang Jie <dave.wang@rock-chips.com>
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

#define SENSOR_DATA_SIZE	9
#define YPR_DATA_SIZE		16
#define RWBUF_SIZE		16

#define ACC_DATA_FLAG		0
#define MAG_DATA_FLAG		1
#define ORI_DATA_FLAG		2
#define AKM_NUM_SENSORS		3

#define ACC_DATA_READY		(1 << (ACC_DATA_FLAG))
#define MAG_DATA_READY		(1 << (MAG_DATA_FLAG))
#define ORI_DATA_READY		(1 << (ORI_DATA_FLAG))

/*Constant definitions of the AK09918.*/
#define AK09918_MEASUREMENT_TIME_US	10000
#define AK09918_MODE_SNG_MEASURE	0x01
#define AK09918_MODE_SELF_TEST		0x10
#define AK09918_MODE_FUSE_ACCESS	0x1F
#define AK09918_MODE_POWERDOWN		0x00
#define AK09918_RESET_DATA		0x01

/* Device specific constant values */
#define AK09918_REG_WIA1		0x00
#define AK09918_REG_WIA2		0x01
#define AK09918_REG_INFO1		0x02
#define AK09918_REG_INFO2		0x03
#define AK09918_REG_ST1			0x10
#define AK09918_REG_HXL			0x11
#define AK09918_REG_HXH			0x12
#define AK09918_REG_HYL			0x13
#define AK09918_REG_HYH			0x14
#define AK09918_REG_HZL			0x15
#define AK09918_REG_HZH			0x16
#define AK09918_REG_TMPS		0x17
#define AK09918_REG_ST2			0x18
#define AK09918_REG_CNTL1		0x30
#define AK09918_REG_CNTL2		0x31
#define AK09918_REG_CNTL3		0x32

#define AK09918_FUSE_ASAX		0x60
#define AK09918_FUSE_ASAY		0x61
#define AK09918_FUSE_ASAZ		0x62

#define AK09918_INFO_SIZE		2
#define AK09918_CONF_SIZE		3

#define COMPASS_IOCTL_MAGIC		'c'

/* IOCTLs for AKM library */
#define ECS_IOCTL_WRITE		_IOW(COMPASS_IOCTL_MAGIC, 0x01, char*)
#define ECS_IOCTL_READ		_IOWR(COMPASS_IOCTL_MAGIC, 0x02, char*)
#define ECS_IOCTL_RESET		_IO(COMPASS_IOCTL_MAGIC, 0x03)
#define ECS_IOCTL_SET_MODE	_IOW(COMPASS_IOCTL_MAGIC, 0x04, short)
#define ECS_IOCTL_GETDATA	_IOR(COMPASS_IOCTL_MAGIC, 0x05, char[8])
#define ECS_IOCTL_SET_YPR	_IOW(COMPASS_IOCTL_MAGIC, 0x06, short[12])
#define ECS_IOCTL_GET_OPEN_STATUS	_IOR(COMPASS_IOCTL_MAGIC, 0x07, int)
#define ECS_IOCTL_GET_CLOSE_STATUS	_IOR(COMPASS_IOCTL_MAGIC, 0x08, int)
#define ECS_IOCTL_GET_LAYOUT		_IOR(COMPASS_IOCTL_MAGIC, 0x09, char)
#define ECS_IOCTL_GET_ACCEL		_IOR(COMPASS_IOCTL_MAGIC, 0x0A, short[3])
#define ECS_IOCTL_GET_OUTBIT		_IOR(COMPASS_IOCTL_MAGIC, 0x0B, char)
#define ECS_IOCTL_GET_INFO		_IOR(COMPASS_IOCTL_MAGIC, 0x27, unsigned char[AK09918_INFO_SIZE])
#define ECS_IOCTL_GET_CONF		_IOR(COMPASS_IOCTL_MAGIC, 0x28, unsigned char[AK09918_CONF_SIZE])
#define ECS_IOCTL_GET_PLATFORM_DATA	_IOR(COMPASS_IOCTL_MAGIC, 0x0E, struct akm_platform_data)
#define ECS_IOCTL_GET_DELAY		_IOR(COMPASS_IOCTL_MAGIC, 0x30, short)
#define AK09918_DEVICE_ID	0x0c

static struct i2c_client *this_client;
static struct miscdevice compass_dev_device;

static int g_akm_rbuf_ready;
static int g_akm_rbuf[12];
static char g_sensor_info[AK09918_INFO_SIZE];
static char g_sensor_conf[AK09918_CONF_SIZE];

/****************operate according to sensor chip:start************/
static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;

	if (enable)
		sensor->ops->ctrl_data = AK09918_MODE_SNG_MEASURE;
	else
		sensor->ops->ctrl_data = AK09918_MODE_POWERDOWN;

	result = sensor_write_reg(client,
			sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if (result)
		dev_err(&client->dev,
			"%s: fail to active sensor(%d)\n", __func__, result);

	return result;
}

static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;

	this_client = client;

	result = sensor->ops->active(client, 0, 0);
	if (result) {
		dev_err(&client->dev, "%s: line= %d, result = %d\n",
			__func__, __LINE__, result);
		return result;
	}

	sensor->status_cur = SENSOR_OFF;

	result = misc_register(&compass_dev_device);
	if (result < 0) {
		dev_err(&client->dev,
			"%s:fail to register misc device %s(%d)\n",
			__func__, compass_dev_device.name, result);
		result = -1;
	}

	g_sensor_info[0] = AK09918_REG_WIA1;
	result = sensor_rx_data(client, g_sensor_info, AK09918_INFO_SIZE);
	if (result) {
		dev_err(&client->dev, "%s: line= %d, error(%d)\n",
			__func__, __LINE__, result);
		return result;
	}

	g_sensor_conf[0] = AK09918_FUSE_ASAX;
	result = sensor_rx_data(client, g_sensor_conf, AK09918_CONF_SIZE);
	if (result) {
		dev_err(&client->dev, "%s: line= %d, error(%d)\n",
			__func__, __LINE__, result);
		return result;
	}

	return result;
}

static void compass_report_value(void)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(this_client);
	static int flag;

	if (!g_akm_rbuf_ready) {
		dev_err(&this_client->dev, "g_akm_rbuf not ready\n");
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
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(client);
	char buffer[SENSOR_DATA_SIZE] = {0};
	unsigned char *stat;
	unsigned char *stat2;
	int ret = 0;
	char value = 0;

	mutex_lock(&sensor->data_mutex);
	compass_report_value();
	mutex_unlock(&sensor->data_mutex);

	if (sensor->ops->read_len < SENSOR_DATA_SIZE) {
		dev_err(&client->dev, "%s: length is error,len = %d\n",
			__func__, sensor->ops->read_len);
		return -EINVAL;
	}

	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	*buffer = sensor->ops->read_reg;
	ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
	if (ret < 0) {
		dev_err(&client->dev,
			"%s: read data error(%d)\n", __func__, ret);
		return ret;
	}

	stat = &buffer[0];
	stat2 = &buffer[7];

	/*
	 * ST : data ready -
	 * Measurement has been completed and data is ready to be read.
	 */
	if ((*stat & 0x01) != 0x01) {
		dev_err(&client->dev,
			"%s: ST is not set, stat = 0x%x\n", __func__, *stat);
		return -EINVAL;
	}

	mutex_lock(&sensor->data_mutex);
	memcpy(sensor->sensor_data, buffer, sensor->ops->read_len);
	mutex_unlock(&sensor->data_mutex);

	if ((sensor->pdata->irq_enable) && (sensor->ops->int_status_reg >= 0))
		value = sensor_read_reg(client, sensor->ops->int_status_reg);

	/* trigger next measurement */
	ret = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if (ret) {
		dev_err(&client->dev,
			"%s: fail to set ctrl_data(%d)\n", __func__, ret);
		return ret;
	}

	return ret;
}

static void compass_set_YPR(int *rbuf)
{
	/* No events are reported */
	if (!rbuf[0]) {
		pr_err("%s: Don't waste a time.", __func__);
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
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(this_client);
	int result = 0;

	switch (mode & 0x1f) {
	case AK09918_MODE_SNG_MEASURE:
	case AK09918_MODE_SELF_TEST:
	case AK09918_MODE_FUSE_ACCESS:
		if (sensor->status_cur == SENSOR_OFF) {
			sensor->stop_work = 0;
			sensor->status_cur = SENSOR_ON;
			dev_info(&client->dev, "compass AK09918 start measure\n");
			schedule_delayed_work(&sensor->delaywork, 0);
		}
		break;

	case AK09918_MODE_POWERDOWN:
		if (sensor->status_cur == SENSOR_ON) {
			sensor->stop_work = 1;
			cancel_delayed_work_sync(&sensor->delaywork);
			dev_err(&client->dev, "compass AK09918 stop measure\n");
			g_akm_rbuf_ready = 0;
			sensor->status_cur = SENSOR_OFF;
		}
		break;
	}

	switch (mode & 0x1f) {
	case AK09918_MODE_SNG_MEASURE:
		result = sensor_write_reg(client, sensor->ops->ctrl_reg, AK09918_MODE_SNG_MEASURE);
		if (result)
			dev_err(&client->dev,
				"%s: i2c error,mode = %d \n", __func__, mode);
		break;
	case AK09918_MODE_SELF_TEST:
		result = sensor_write_reg(client, sensor->ops->ctrl_reg, AK09918_MODE_SELF_TEST);
		if (result)
			dev_err(&client->dev,
				"%s: i2c error,mode = %d\n", __func__, mode);
		break;
	case AK09918_MODE_FUSE_ACCESS:
		result = sensor_write_reg(client, sensor->ops->ctrl_reg, AK09918_MODE_FUSE_ACCESS);
		if (result)
			dev_err(&client->dev,
				"%s: i2c error,mode = %d\n", __func__, mode);
		break;
	case AK09918_MODE_POWERDOWN:
		/* Set powerdown mode */
		result = sensor_write_reg(client, sensor->ops->ctrl_reg, AK09918_MODE_POWERDOWN);
		if (result)
			dev_err(&client->dev,
				"%s: i2c error, mode= %d \n", __func__, mode);
		udelay(100);
		break;
	default:
		dev_err(&client->dev, "%s: Unknown mode(%d)", __func__, mode);
		result = -EINVAL;
		break;
	}
	return result;
}

static int compass_akm_reset(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(this_client);
	int result = 0;

	if (sensor->pdata->reset_pin > 0) {
		gpio_direction_output(sensor->pdata->reset_pin, GPIO_LOW);
		udelay(10);
		gpio_direction_output(sensor->pdata->reset_pin, GPIO_HIGH);
	} else {
		/* Set measure mode */
		result = sensor_write_reg(client, sensor->ops->ctrl_reg, AK09918_MODE_SNG_MEASURE);
		if (result)
			dev_err(&client->dev,
				"%s: fail to Set measure mode(%d)\n", __func__, result);
	}

	udelay(100);

	return result;
}

static int compass_akm_get_openstatus(void)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(this_client);

	wait_event_interruptible(sensor->flags.open_wq, (atomic_read(&sensor->flags.open_flag) != 0));

	return atomic_read(&sensor->flags.open_flag);
}

static int compass_akm_get_closestatus(void)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(this_client);

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

	/* NOTE: In this function the size of "char" should be 1-byte. */
	char compass_data[SENSOR_DATA_SIZE];	/* for GETDATA */
	char rwbuf[RWBUF_SIZE];	/* for READ/WRITE */
	char mode;				/* for SET_MODE*/
	int value[YPR_DATA_SIZE];		/* for SET_YPR */
	int status;				/* for OPEN/CLOSE_STATUS */
	int ret = -1;				/* Return value. */

	int16_t acc_buf[3];			/* for GET_ACCEL */
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
	case ECS_IOCTL_GETDATA:
	case ECS_IOCTL_GET_OPEN_STATUS:
	case ECS_IOCTL_GET_CLOSE_STATUS:
	case ECS_IOCTL_GET_DELAY:
	case ECS_IOCTL_GET_LAYOUT:
	case ECS_IOCTL_GET_OUTBIT:
	case ECS_IOCTL_GET_ACCEL:
	case ECS_IOCTL_GET_INFO:
	case ECS_IOCTL_GET_CONF:
		/* Just check buffer pointer */
		if (!argp) {
			dev_err(&client->dev,
				"%s: invalid argument\n", __func__);
			return -EINVAL;
		}
		break;
	default:
		break;
	}

	switch (cmd) {
	case ECS_IOCTL_WRITE:
		mutex_lock(&sensor->operation_mutex);
		if ((rwbuf[0] < 2) || (rwbuf[0] > (RWBUF_SIZE - 1))) {
			mutex_unlock(&sensor->operation_mutex);
			return -EINVAL;
		}
		ret = sensor_tx_data(client, &rwbuf[1], rwbuf[0]);
		if (ret < 0) {
			mutex_unlock(&sensor->operation_mutex);
			dev_err(&client->dev,
				"%s: line = %d, fail to tx data(%d)\n",
				__func__, __LINE__, ret);
			return ret;
		}
		mutex_unlock(&sensor->operation_mutex);
		break;
	case ECS_IOCTL_READ:
		mutex_lock(&sensor->operation_mutex);
		if ((rwbuf[0] < 1) || (rwbuf[0] > (RWBUF_SIZE - 1))) {
			mutex_unlock(&sensor->operation_mutex);
			dev_err(&client->dev,
				"%s: data is error\n", __func__);
			return -EINVAL;
		}
		ret = sensor_rx_data(client, &rwbuf[1], rwbuf[0]);
		if (ret < 0) {
			mutex_unlock(&sensor->operation_mutex);
			dev_err(&client->dev,
				"%s: line = %d, fail to rx data(%d)\n",
				__func__, __LINE__, ret);
			return ret;
		}
		mutex_unlock(&sensor->operation_mutex);
		break;
	case ECS_IOCTL_SET_MODE:
		mutex_lock(&sensor->operation_mutex);
		if (sensor->ops->ctrl_data != mode) {
			ret = compass_akm_set_mode(client, mode);
			if (ret < 0) {
				dev_err(&client->dev,
					"%s: fail to set mode(%d)\n",
					__func__, ret);
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
			dev_err(&client->dev, "%s: line = %d, ret = %d\n",
				__func__, __LINE__, ret);
			return ret;
		}
		break;
	case ECS_IOCTL_GET_LAYOUT:
		if ((sensor->pdata->layout >= 1) && (sensor->pdata->layout <= 8))
			layout = sensor->pdata->layout;
		else
			layout = 1;
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
	case ECS_IOCTL_GET_INFO:
		ret = copy_to_user(argp, g_sensor_info, sizeof(g_sensor_info));
		if (ret < 0) {
			dev_err(&client->dev, "%s: line = %d, ret = %d\n",
				__func__, __LINE__, ret);
			return ret;
		}
		break;
	case ECS_IOCTL_GET_CONF:
		ret = copy_to_user(argp, g_sensor_conf, sizeof(g_sensor_conf));
		if (ret < 0) {
			dev_err(&client->dev, "%s: line = %d, ret = %d\n",
				__func__, __LINE__, ret);
			return ret;
		}
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
			dev_err(&client->dev, "%s: line = %d, ret = %d\n",
				__func__, __LINE__, ret);
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_GET_OUTBIT:
		if (copy_to_user(argp, &outbit, sizeof(outbit))) {
			dev_err(&client->dev, "%s: line = %d, ret = %d\n",
				 __func__, __LINE__, ret);
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_GET_ACCEL:
		if (copy_to_user(argp, &acc_buf, sizeof(acc_buf))) {
			dev_err(&client->dev, "%s: line = %d, ret = %d\n",
				__func__, __LINE__, ret);
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

static struct sensor_operate compass_akm09918_ops = {
	.name		= "akm09918",
	.type		= SENSOR_TYPE_COMPASS,
	.id_i2c		= COMPASS_ID_AK09918,
	.read_reg	= AK09918_REG_ST1,
	.read_len	= SENSOR_DATA_SIZE,
	.id_reg		= AK09918_REG_WIA2,
	.id_data	= AK09918_DEVICE_ID,
	.precision	= 8,
	.ctrl_reg	= AK09918_REG_CNTL2,
	.int_status_reg	= SENSOR_UNKNOW_DATA,
	.range		= {-0xffff, 0xffff},
	.trig		= IRQF_TRIGGER_RISING,
	.active		= sensor_active,
	.init		= sensor_init,
	.report		= sensor_report_value,
	.misc_dev	= NULL,
};

/****************operate according to sensor chip:end************/
static int compass_akm09918_probe(struct i2c_client *client,
				  const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &compass_akm09918_ops);
}

static int compass_akm09918_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &compass_akm09918_ops);
}

static const struct i2c_device_id compass_akm09918_id[] = {
	{"ak09918", COMPASS_ID_AK09918},
	{}
};

static struct i2c_driver compass_akm09918_driver = {
	.probe = compass_akm09918_probe,
	.remove = compass_akm09918_remove,
	.shutdown = sensor_shutdown,
	.id_table = compass_akm09918_id,
	.driver = {
		.name = "compass_akm09918",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(compass_akm09918_driver);

MODULE_AUTHOR("Wang Jie <dave.wang@rock-chips.com>");
MODULE_DESCRIPTION("akm09918 3-Axis compasss driver");
MODULE_LICENSE("GPL");

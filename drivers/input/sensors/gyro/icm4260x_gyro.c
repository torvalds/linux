// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Rockchip Co.,Ltd.
 * Author: Wangqiang Guo <kay.guo@rock-chips.com>
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
#include <linux/icm4260x.h>

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int status = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);

	if (!enable) {
		status = (0xff & ~BIT_GYRO_MODE_MASK);
		sensor->ops->ctrl_data &= status;
	} else {
		status = BIT_GYRO_MODE_LNM;
		sensor->ops->ctrl_data |= status;
		sensor->ops->ctrl_data &= ~BIT_IDLE;
	}

	result = sensor_write_reg(client, sensor->ops->ctrl_reg,
						sensor->ops->ctrl_data);
	if (result) {
		dev_err(&client->dev,
			"%s: fail to set pwr_mgmt0(%d)\n", __func__, result);
		return result;
	}
	/* Gyroscope needs to be kept ON for a minimum of 45ms */
	usleep_range(45*1000, 45*1010);

	return result;
}

static int sensor_init(struct i2c_client *client)
{
	int ret = 0;
	u8 value;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	/*
	 * init on icm42607_acc.c
	 */

	/* set Full scale select for accelerometer UI interface output*/
	value = sensor_read_reg(client, ICM4260X_GYRO_CONFIG0);
	value &= ~BIT_GYRO_FSR;
	value |= GYRO_FS_SEL << SHIFT_GYRO_FS_SEL;
	ret = sensor_write_reg(client, ICM4260X_GYRO_CONFIG0, value);
	if (ret)
		return ret;

	/* turn on accelerometer*/
	ret = sensor->ops->active(client, 0, sensor->pdata->poll_delay_ms);
	if (ret) {
		dev_err(&client->dev,
			"%s: fail to active sensor(%d)\n", __func__, ret);
		return ret;
	}

	return ret;
}

static int gyro_report_value(struct i2c_client *client,
				struct sensor_axis *axis)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	if (sensor->status_cur == SENSOR_ON) {
		/* Report acceleration sensor information */
		input_report_abs(sensor->input_dev, ABS_RX, axis->x);
		input_report_abs(sensor->input_dev, ABS_RY, axis->y);
		input_report_abs(sensor->input_dev, ABS_RZ, axis->z);
		input_sync(sensor->input_dev);
	}

	return 0;
}

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(client);
	struct sensor_platform_data *pdata = sensor->pdata;
	int ret = 0;
	short x, y, z;
	struct sensor_axis axis;
	u8 buffer[6] = {0};

	if (sensor->ops->read_len < 6) {
		dev_err(&client->dev, "%s: length is error, len = %d\n",
			__func__, sensor->ops->read_len);
		return -EINVAL;
	}

	*buffer = sensor->ops->read_reg;
	ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
	if (ret < 0) {
		dev_err(&client->dev,
			"%s: read data failed, ret = %d\n", __func__, ret);
		return ret;
	}

	x = ((buffer[0] << 8) & 0xff00) + (buffer[1] & 0xFF);
	y = ((buffer[2] << 8) & 0xff00) + (buffer[3] & 0xFF);
	z = ((buffer[4] << 8) & 0xff00) + (buffer[5] & 0xFF);

	axis.x = (pdata->orientation[0]) * x + (pdata->orientation[1]) * y +
		 (pdata->orientation[2]) * z;
	axis.y = (pdata->orientation[3]) * x + (pdata->orientation[4]) * y +
		 (pdata->orientation[5]) * z;
	axis.z = (pdata->orientation[6]) * x + (pdata->orientation[7]) * y +
		 (pdata->orientation[8]) * z;

	gyro_report_value(client, &axis);

	mutex_lock(&(sensor->data_mutex));
	sensor->axis = axis;
	mutex_unlock(&(sensor->data_mutex));

	return ret;
}

static struct sensor_operate gyro_icm4260x_ops = {
	.name		= "icm4260x_gyro",
	.type		= SENSOR_TYPE_GYROSCOPE,
	.id_i2c		= GYRO_ID_ICM4260X,
	.read_reg	= ICM4260X_GYRO_DATA_X0,
	.read_len	= 6,
	.id_reg		= SENSOR_UNKNOW_DATA,
	.id_data	= SENSOR_UNKNOW_DATA,
	.precision	= ICM4260X_PRECISION,
	.ctrl_reg	= ICM4260X_PWR_MGMT_0,
	.int_status_reg = ICM4260X_INT_STATUS,
	.range		= {-32768, 32768},
	.trig		= IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
	.active		= sensor_active,
	.init		= sensor_init,
	.report		= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int gyro_icm4260x_probe(struct i2c_client *client,
				 const struct i2c_device_id *devid)
{
	client->addr = ICM42607_ADDR;
	return sensor_register_device(client, NULL, devid, &gyro_icm4260x_ops);
}

static int gyro_icm4260x_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gyro_icm4260x_ops);
}

static const struct i2c_device_id gyro_icm4260x_id[] = {
	{"icm42607_gyro", GYRO_ID_ICM4260X},
	{}
};

static struct i2c_driver gyro_icm4260x_driver = {
	.probe = gyro_icm4260x_probe,
	.remove = gyro_icm4260x_remove,
	.shutdown = sensor_shutdown,
	.id_table = gyro_icm4260x_id,
	.driver = {
		.name = "gyro_icm4260x",
#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
#endif
	},
};

static int __init gyro_icm4260x_init(void)
{
	return i2c_add_driver(&gyro_icm4260x_driver);
}

static void __exit gyro_icm4260x_exit(void)
{
	i2c_del_driver(&gyro_icm4260x_driver);
}
/* must register after icm4260x_acc */
device_initcall_sync(gyro_icm4260x_init);
module_exit(gyro_icm4260x_exit);

MODULE_AUTHOR("Wangqiang Guo <dave.wang@rock-chips.com>");
MODULE_DESCRIPTION("icm4260x_gyro 3-Axis accelerometer driver");
MODULE_LICENSE("GPL");

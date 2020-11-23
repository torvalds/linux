// SPDX-License-Identifier: GPL-2.0
/*
 * kernel/drivers/input/sensors/accel/icm2060x_acc.c
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
#include <linux/icm2060x.h>

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int status = 0;
	u8 pwrm1 = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	pwrm1 = sensor_read_reg(client, ICM2060X_PWR_MGMT_1);

	if (!enable) {
		status = BIT_ACCEL_STBY;
		sensor->ops->ctrl_data |= status;
		if ((sensor->ops->ctrl_data & BIT_GYRO_STBY) == BIT_GYRO_STBY)
			pwrm1 |= ICM2060X_PWRM1_SLEEP;
	} else {
		status = ~BIT_ACCEL_STBY;
		sensor->ops->ctrl_data &= status;
		pwrm1 &= ~ICM2060X_PWRM1_SLEEP;
	}

	result = sensor_write_reg(client, sensor->ops->ctrl_reg,
						sensor->ops->ctrl_data);
	if (result) {
		dev_err(&client->dev,
			"%s: fail to set pwrm2(%d)\n", __func__, result);
		return result;
	}

	msleep(20);

	result = sensor_write_reg(client, ICM2060X_PWR_MGMT_1, pwrm1);
	if (result) {
		dev_err(&client->dev,
			"%s:fail to set pwrm1(%d)\n", __func__, result);
		return result;
	}
	msleep(50);

	return result;
}

static int sensor_init(struct i2c_client *client)
{
	int res = 0;
	u8 device_id = 0;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	device_id = sensor_read_reg(client, ICM2060X_WHOAMI);
	if (device_id != ICM20600_DEVICE_ID &&
	    device_id != ICM20607_DEVICE_ID) {
		dev_err(&client->dev, "%s: check id err, read_id: %d\n",
			__func__, device_id);
		return -1;
	}

	res = sensor_write_reg(client, ICM2060X_PWR_MGMT_1, 0x80);
	if (res) {
		dev_err(&client->dev,
			"set ICM2060X_PWR_MGMT_1 error,res: %d!\n", res);
		return res;
	}
	usleep_range(1000, 2000);

	res = sensor_write_reg(client, ICM2060X_GYRO_CONFIG, 0x18);
	if (res) {
		dev_err(&client->dev,
			"set ICM2060X_GYRO_CONFIG error,res: %d!\n", res);
		return res;
	}
	usleep_range(1000, 2000);

	res = sensor_write_reg(client, ICM2060X_ACCEL_CONFIG, 0x00);
	if (res) {
		dev_err(&client->dev,
			"set ICM2060X_ACCEL_CONFIG error,res: %d!\n", res);
		return res;
	}
	usleep_range(1000, 2000);

	res = sensor_write_reg(client, ICM2060X_ACCEL_CONFIG2, 0x00);
	if (res) {
		dev_err(&client->dev,
			"set ICM2060X_ACCEL_CONFIG2 error,res: %d!\n", res);
		return res;
	}

	res = sensor_write_reg(client, ICM2060X_PWR_MGMT_2, 0x3F);
	if (res) {
		dev_err(&client->dev,
			"set ICM2060X_PWR_MGMT_2 error,res: %d!\n", res);
		return res;
	}
	usleep_range(1000, 2000);

	res = sensor_write_reg(client, ICM2060X_PWR_MGMT_1, 0x41);
	if (res) {
		dev_err(&client->dev,
			"set ICM2060X_PWR_MGMT_1 error,res: %d!\n", res);
		return res;
	}
	usleep_range(1000, 2000);

	res = sensor->ops->active(client, 0, sensor->pdata->poll_delay_ms);
	if (res) {
		dev_err(&client->dev,
			"%s: fail to active sensor(%d)\n", __func__, res);
		return res;
	}

	return res;
}

static int gsensor_report_value(struct i2c_client *client,
				struct sensor_axis *axis)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	if (sensor->status_cur == SENSOR_ON) {
		/* Report acceleration sensor information */
		input_report_abs(sensor->input_dev, ABS_X, axis->x);
		input_report_abs(sensor->input_dev, ABS_Y, axis->y);
		input_report_abs(sensor->input_dev, ABS_Z, axis->z);
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
	char value = 0;

	if (sensor->ops->read_len < 6) {
		dev_err(&client->dev, "%s: length is error, len = %d\n",
			__func__, sensor->ops->read_len);
		return -EINVAL;
	}

	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
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

	gsensor_report_value(client, &axis);

	mutex_lock(&(sensor->data_mutex));
	sensor->axis = axis;
	mutex_unlock(&(sensor->data_mutex));

	if ((sensor->pdata->irq_enable) && (sensor->ops->int_status_reg >= 0))
		value = sensor_read_reg(client, sensor->ops->int_status_reg);

	return ret;
}

struct sensor_operate gsensor_icm2060x_ops = {
	.name		= "icm2060x_acc",
	.type		= SENSOR_TYPE_ACCEL,
	.id_i2c		= ACCEL_ID_ICM2060X,
	.read_reg	= ICM2060X_ACCEL_XOUT_H,
	.read_len	= 6,
	.id_reg		= SENSOR_UNKNOW_DATA,
	.id_data	= SENSOR_UNKNOW_DATA,
	.precision	= ICM2060X_PRECISION,
	.ctrl_reg	= ICM2060X_PWR_MGMT_2,
	.int_status_reg = ICM2060X_INT_STATUS,
	.range		= {-32768, 32768},
	.trig		= IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
	.active		= sensor_active,
	.init		= sensor_init,
	.report		= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int gsensor_icm2060x_probe(struct i2c_client *client,
				 const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &gsensor_icm2060x_ops);
}

static int gsensor_icm2060x_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gsensor_icm2060x_ops);
}

static const struct i2c_device_id gsensor_icm2060x_id[] = {
	{"icm2060x_acc", ACCEL_ID_ICM2060X},
	{}
};

static struct i2c_driver gsensor_icm2060x_driver = {
	.probe = gsensor_icm2060x_probe,
	.remove = gsensor_icm2060x_remove,
	.shutdown = sensor_shutdown,
	.id_table = gsensor_icm2060x_id,
	.driver = {
		.name = "gsensor_icm2060x",
#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
#endif
	},
};

module_i2c_driver(gsensor_icm2060x_driver);

MODULE_AUTHOR("Wang Jie <dave.wang@rock-chips.com>");
MODULE_DESCRIPTION("icm2060x_acc 3-Axis accelerometer driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/*
 * kernel/drivers/input/sensors/gyro/icm2060x_gyro.c
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
		status = BIT_GYRO_STBY;
		sensor->ops->ctrl_data |= status;
		if ((sensor->ops->ctrl_data & BIT_ACCEL_STBY) == BIT_ACCEL_STBY)
			pwrm1 |= ICM2060X_PWRM1_SLEEP;
	} else {
		status = ~BIT_GYRO_STBY;
		sensor->ops->ctrl_data &= status;
		pwrm1 &= ~ICM2060X_PWRM1_SLEEP;
	}

	result = sensor_write_reg(client, sensor->ops->ctrl_reg,
					sensor->ops->ctrl_data);
	if (result) {
		dev_err(&client->dev,
			"%s:fail to set ctrl_reg(%d)\n", __func__, result);
		return result;
	}
	msleep(20);

	result = sensor_write_reg(client, ICM2060X_PWR_MGMT_1, pwrm1);
	if (result) {
		dev_err(&client->dev,
			"%s: fail to set pwrm1(%d)\n", __func__, result);
		return result;
	}
	msleep(50);

	return result;
}

static int sensor_init(struct i2c_client *client)
{
	int result;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	/* init on icm2060x_acc.c */
	result = sensor->ops->active(client, 0, sensor->pdata->poll_delay_ms);
	if (result) {
		dev_err(&client->dev,
			"%s: sensor init err(%d)\n", __func__, result);
		return result;
	}

	return result;
}

static int gyro_report_value(struct i2c_client *client,
			     struct sensor_axis *axis)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	if (sensor->status_cur == SENSOR_ON) {
		/* Report gyro sensor information */
		input_report_rel(sensor->input_dev, ABS_RX, axis->x);
		input_report_rel(sensor->input_dev, ABS_RY, axis->y);
		input_report_rel(sensor->input_dev, ABS_RZ, axis->z);
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
		dev_err(&client->dev, "%s: length is error,len = %d\n",
			__func__, sensor->ops->read_len);
		return -1;
	}

	*buffer = sensor->ops->read_reg;
	ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
	if (ret < 0) {
		dev_err(&client->dev,
			"%s: read data failed, ret = %d\n", __func__, ret);
		return ret;
	}

	x = ((buffer[0] << 8) & 0xFF00) + (buffer[1] & 0xFF);
	y = ((buffer[2] << 8) & 0xFF00) + (buffer[3] & 0xFF);
	z = ((buffer[4] << 8) & 0xFF00) + (buffer[5] & 0xFF);

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

	if ((sensor->pdata->irq_enable) && (sensor->ops->int_status_reg >= 0))
		value = sensor_read_reg(client, sensor->ops->int_status_reg);

	return ret;
}

static struct sensor_operate gyro_icm2060x_ops = {
	.name		= "icm2060x_gyro",
	.type		= SENSOR_TYPE_GYROSCOPE,
	.id_i2c		= GYRO_ID_ICM2060X,
	.read_reg	= ICM2060X_GYRO_XOUT_H,
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
static int gyro_icm2060x_probe(struct i2c_client *client,
				const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &gyro_icm2060x_ops);
}

static int gyro_icm2060x_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gyro_icm2060x_ops);
}

static const struct i2c_device_id gyro_icm2060x_id[] = {
	{"icm2060x_gyro", GYRO_ID_ICM2060X},
	{}
};

static struct i2c_driver gyro_icm2060x_driver = {
	.probe = gyro_icm2060x_probe,
	.remove = gyro_icm2060x_remove,
	.shutdown = sensor_shutdown,
	.id_table = gyro_icm2060x_id,
	.driver = {
		.name = "gyro_icm2060x",
#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
#endif
	},
};

static int __init gyro_icm2060x_init(void)
{
	return i2c_add_driver(&gyro_icm2060x_driver);
}

static void __exit gyro_icm2060x_exit(void)
{
	i2c_del_driver(&gyro_icm2060x_driver);
}

/* must register after icm2060x_acc */
device_initcall_sync(gyro_icm2060x_init);
module_exit(gyro_icm2060x_exit);

MODULE_AUTHOR("Wang Jie <dave.wang@rock-chips.com>");
MODULE_DESCRIPTION("icm2060x_gyro 3-Axis Gyroscope driver");
MODULE_LICENSE("GPL");

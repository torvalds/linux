/* drivers/input/sensors/access/mpu6880_acc.c
 *
 * Copyright (C) 2012-2015 ROCKCHIP.
 * Author: oeh<oeh@rock-chips.com>
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
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/sensor-dev.h>
#include <linux/mpu6880.h>

static int mpu6880_set_lpf(struct i2c_client *client, int rate)
{
	const short hz[] = {184, 98, 41, 20, 10, 5};
	const int   d[] = {DLPF_CFG_184HZ, DLPF_CFG_98HZ,
			DLPF_CFG_41HZ, DLPF_CFG_20HZ,
			DLPF_CFG_10HZ, DLPF_CFG_5HZ};
	int i, h, data, result;

	h = (rate >> 1);
	i = 0;
	while ((h < hz[i]) && (i < ARRAY_SIZE(d) - 1))
		i++;
	data = d[i];

	result = sensor_write_reg(client, MPU6880_CONFIG, data);
	if (result)
		return -1;

	return 0;
}

static int mpu6880_set_rate(struct i2c_client *client, int rate)
{
	u8 data;
	int result;
	u16 fifo_rate;

	/* always use poll mode, no need to set rate */
	return 0;

	if ((rate < 1) || (rate > 250))
		return -1;

	data = rate - 1;
	result = sensor_write_reg(client, MPU6880_SMPLRT_DIV, data);
	if (result)
		return result;

	fifo_rate = 1000 / rate;

	result = mpu6880_set_lpf(client, fifo_rate);
	if (result)
		return -1;

	return 0;
}

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int status = 0;
	u8 pwrm1 = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	pwrm1 = sensor_read_reg(client, MPU6880_PWR_MGMT_1);

	if (!enable) {
		status = BIT_ACCEL_STBY;
		sensor->ops->ctrl_data |= status;
		if ((sensor->ops->ctrl_data &  BIT_GYRO_STBY) == BIT_GYRO_STBY) {
			pwrm1 |= MPU6880_PWRM1_SLEEP;
		}
	} else {
		status = ~BIT_ACCEL_STBY;
		sensor->ops->ctrl_data &= status;
		pwrm1 &= ~MPU6880_PWRM1_SLEEP;

		mpu6880_set_rate(client, rate);
	}
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if (result) {
		dev_err(&client->dev, "%s:fail to set pwrm2\n", __func__);
		return -1;
	}
	msleep(20);

	result = sensor_write_reg(client, MPU6880_PWR_MGMT_1, pwrm1);
	if (result) {
		dev_err(&client->dev, "%s:fail to set pwrm1\n", __func__);
		return -1;
	}
	msleep(50);

	return result;
}

static int sensor_init(struct i2c_client *client)
{
	int res = 0;
	u8 read_data = 0;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	read_data = sensor_read_reg(client, sensor->ops->id_reg);
	if (read_data != sensor->ops->id_data) {
		dev_err(&client->dev, "%s:check id err,read_data:%d,ops->id_data:%d\n", __func__, read_data, sensor->ops->id_data);
		return -1;
	}

	res = sensor_write_reg(client, MPU6880_PWR_MGMT_1, 0x80);
	if (res) {
		dev_err(&client->dev, "set MPU6880_PWR_MGMT_1 error,res: %d!\n", res);
		return res;
	}
	msleep(40);

	res = sensor_write_reg(client, MPU6880_GYRO_CONFIG, 0x18);
	if (res) {
		dev_err(&client->dev, "set MPU6880_GYRO_CONFIG error,res: %d!\n", res);
		return res;
	}
	msleep(10);

	res = sensor_write_reg(client, MPU6880_ACCEL_CONFIG, 0x00);
	if (res) {
		dev_err(&client->dev, "set MPU6880_ACCEL_CONFIG error,res: %d!\n", res);
		return res;
	}
	msleep(10);

	res = sensor_write_reg(client, MPU6880_ACCEL_CONFIG2, 0x00);
	if (res) {
		dev_err(&client->dev, "set MPU6880_ACCEL_CONFIG2 error,res: %d!\n", res);
		return res;
	}
	res = sensor_write_reg(client, MPU6880_PWR_MGMT_2, 0x3F);
	if (res) {
		dev_err(&client->dev, "set MPU6880_PWR_MGMT_2 error,res: %d!\n", res);
		return res;
	}
	msleep(10);
	res = sensor_write_reg(client, MPU6880_PWR_MGMT_1, 0x41);
	if (res) {
		dev_err(&client->dev, "set MPU6880_PWR_MGMT_1 error,res: %d!\n", res);
		return res;
	}
	msleep(10);

	res = sensor->ops->active(client, 0, sensor->pdata->poll_delay_ms);
	if (res) {
		dev_err(&client->dev, "%s:line=%d,error\n", __func__, __LINE__);
		return res;
	}
	return res;
}

static int gsensor_report_value(struct i2c_client *client, struct sensor_axis *axis)
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
		dev_err(&client->dev, "%s:lenth is error,len=%d\n", __func__, sensor->ops->read_len);
		return -1;
	}

	memset(buffer, 0, 6);

	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	do {
		*buffer = sensor->ops->read_reg;
		ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
		if (ret < 0)
			return ret;
	} while (0);

	x = ((buffer[0] << 8) & 0xff00) + (buffer[1] & 0xFF);
	y = ((buffer[2] << 8) & 0xff00) + (buffer[3] & 0xFF);
	z = ((buffer[4] << 8) & 0xff00) + (buffer[5] & 0xFF);
	axis.x = (pdata->orientation[0]) * x + (pdata->orientation[1]) * y + (pdata->orientation[2]) * z;
	axis.y = (pdata->orientation[3]) * x + (pdata->orientation[4]) * y + (pdata->orientation[5]) * z;
	axis.z = (pdata->orientation[6]) * x + (pdata->orientation[7]) * y + (pdata->orientation[8]) * z;

	gsensor_report_value(client, &axis);

	mutex_lock(&(sensor->data_mutex));
	sensor->axis = axis;
	mutex_unlock(&(sensor->data_mutex));

	if ((sensor->pdata->irq_enable) && (sensor->ops->int_status_reg >= 0))
		value = sensor_read_reg(client, sensor->ops->int_status_reg);

	return ret;
}

struct sensor_operate gsensor_mpu6880_ops = {
	.name				= "mpu6880_acc",
	.type				= SENSOR_TYPE_ACCEL,
	.id_i2c				= ACCEL_ID_MPU6880,
	.read_reg				= MPU6880_ACCEL_XOUT_H,
	.read_len				= 6,
	.id_reg				= MPU6880_WHOAMI,
	.id_data 				= MPU6880_DEVICE_ID,
	.precision				= MPU6880_PRECISION,
	.ctrl_reg 				= MPU6880_PWR_MGMT_2,
	.int_status_reg 		= MPU6880_INT_STATUS,
	.range				= {-32768, 32768},
	.trig					= IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
	.active				= sensor_active,
	.init					= sensor_init,
	.report 				= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int gsensor_mpu6880_probe(struct i2c_client *client,
				const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &gsensor_mpu6880_ops);
}

static int gsensor_mpu6880_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gsensor_mpu6880_ops);
}

static const struct i2c_device_id gsensor_mpu6880_id[] = {
	{"mpu6880_acc", ACCEL_ID_MPU6880},
	{}
};

static struct i2c_driver gsensor_mpu6880_driver = {
	.probe = gsensor_mpu6880_probe,
	.remove = gsensor_mpu6880_remove,
	.shutdown = sensor_shutdown,
	.id_table = gsensor_mpu6880_id,
	.driver = {
		.name = "gsensor_mpu6880",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(gsensor_mpu6880_driver);

MODULE_AUTHOR("oeh <oeh@rock-chips.com>");
MODULE_DESCRIPTION("mpu6880 3-Axis accelerometer driver");
MODULE_LICENSE("GPL");

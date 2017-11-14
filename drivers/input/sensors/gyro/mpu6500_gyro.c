/* drivers/input/sensors/access/mpu6880_gyro.c
 *
 * Copyright (C) 2012-2015 ROCKCHIP.
 * Author: ouenhui <oeh@rock-chips.com>
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
#include <asm/uaccess.h>
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
#include <linux/mpu6500.h>

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int status = 0;
	u8 pwrm1 = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	pwrm1 = sensor_read_reg(client, MPU6500_PWR_MGMT_1);

	if (!enable) {
		status = BIT_GYRO_STBY;
		sensor->ops->ctrl_data |= status;
		if ((sensor->ops->ctrl_data & BIT_ACCEL_STBY) == BIT_ACCEL_STBY) {
			pwrm1 |= MPU6500_PWRM1_SLEEP;
		}
	} else {
		status = ~BIT_GYRO_STBY;
		sensor->ops->ctrl_data &= status;
		pwrm1 &= ~MPU6500_PWRM1_SLEEP;
	}

	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if (result) {
		dev_err(&client->dev, "%s:fail to active sensor\n", __func__);
		return -1;
	}
	msleep(20);

	result = sensor_write_reg(client, MPU6500_PWR_MGMT_1, pwrm1);
	if (result) {
		dev_err(&client->dev, "%s:fail to set pwrm1\n", __func__);
		return -1;
	}
	msleep(50);

	return result;
}

static int sensor_init(struct i2c_client *client)
{
	int ret;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	/* init on mpu6500_acc.c */
	ret = sensor->ops->active(client, 0, sensor->pdata->poll_delay_ms);
	if (ret) {
		dev_err(&client->dev, "%s:line=%d,error\n", __func__, __LINE__);
		return ret;
	}

	return ret;
}

static int gyro_report_value(struct i2c_client *client, struct sensor_axis *axis)
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
		dev_err(&client->dev, "%s:lenth is error,len=%d\n", __func__, sensor->ops->read_len);
		return -1;
	}

	memset(buffer, 0, 6);

	do {
		*buffer = sensor->ops->read_reg;
		ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
		if (ret < 0)
			return ret;
	} while (0);

	x = ((buffer[0] << 8) & 0xFF00) + (buffer[1] & 0xFF);
	y = ((buffer[2] << 8) & 0xFF00) + (buffer[3] & 0xFF);
	z = ((buffer[4] << 8) & 0xFF00) + (buffer[5] & 0xFF);

	axis.x = (pdata->orientation[0]) * x + (pdata->orientation[1]) * y + (pdata->orientation[2]) * z;
	axis.y = (pdata->orientation[3]) * x + (pdata->orientation[4]) * y + (pdata->orientation[5]) * z;
	axis.z = (pdata->orientation[6]) * x + (pdata->orientation[7]) * y + (pdata->orientation[8]) * z;

	gyro_report_value(client, &axis);

	mutex_lock(&(sensor->data_mutex));
	sensor->axis = axis;
	mutex_unlock(&(sensor->data_mutex));

	if ((sensor->pdata->irq_enable) && (sensor->ops->int_status_reg >= 0))
		value = sensor_read_reg(client, sensor->ops->int_status_reg);

	return ret;
}

struct sensor_operate gyro_mpu6500_ops = {
	.name				= "mpu6500_gyro",
	.type				= SENSOR_TYPE_GYROSCOPE,
	.id_i2c				= GYRO_ID_MPU6500,
	.read_reg				= MPU6500_GYRO_XOUT_H,
	.read_len				= 6,
	.id_reg				= SENSOR_UNKNOW_DATA,
	.id_data 				= SENSOR_UNKNOW_DATA,
	.precision				= MPU6500_PRECISION,
	.ctrl_reg 				= MPU6500_PWR_MGMT_2,
	.int_status_reg 		= MPU6500_INT_STATUS,
	.range				= {-32768, 32768},
	.trig					= IRQF_TRIGGER_HIGH |IRQF_ONESHOT,
	.active				= sensor_active,
	.init					= sensor_init,
	.report 				= sensor_report_value,
};

/****************operate according to sensor chip:end************/

static struct sensor_operate *gyro_get_ops(void)
{
	return &gyro_mpu6500_ops;
}

static int __init gyro_mpu6500_init(void)
{
	struct sensor_operate *ops = gyro_get_ops();
	int type = ops->type;

	return sensor_register_slave(type, NULL, NULL, gyro_get_ops);
}

static void __exit gyro_mpu6500_exit(void)
{
	struct sensor_operate *ops = gyro_get_ops();
	int type = ops->type;

	sensor_unregister_slave(type, NULL, NULL, gyro_get_ops);
}

/* must register after mpu6500_acc */
device_initcall_sync(gyro_mpu6500_init);
module_exit(gyro_mpu6500_exit);

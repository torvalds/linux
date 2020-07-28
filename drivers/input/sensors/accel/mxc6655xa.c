// SPDX-License-Identifier: GPL-2.0
/*
 * kernel/drivers/input/sensors/accel/mxc6655xa.c
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

/* Linear acceleration  register */
#define MXC6655_INT_SRC0	0x00
#define MXC6655_INT_CLR0	0x00
#define MXC6655_INT_SRC1	0x01
#define MXC6655_INT_CLR1	0x01
#define MXC6655_STATUS		0x02
#define	MXC6655_OUT_X_H		0x03
#define	MXC6655_OUT_X_L		0x04
#define	MXC6655_OUT_Y_H		0x05
#define	MXC6655_OUT_Y_L		0x06
#define	MXC6655_OUT_Z_H		0x07
#define	MXC6655_OUT_Z_L		0x08
#define MXC6655_OUT_TEMP	0x09
#define MXC6655_INT_MASK0	0x0A
#define MXC6655_INT_MASK1	0x0B
#define MXC6655_DETECTION	0x0C
#define MXC6655_CONTROL		0x0D
#define	MXC6655_WHO_AM_I_A	0x0F
#define	MXC6655_DEVICE_ID_A	0x05
#define MXC6655_POWER_DOWN	BIT(0)
#define MXC6655_INT_ENABLE	BIT(0)
#define MXC6655_RANGE		(16384 * 2)
#define MXC6655_PRECISION	12
#define MXC6655_BOUNDARY	(0x1 << (MXC6655_PRECISION - 1))
#define MXC6655_GRAVITY_STEP	(MXC6655_RANGE / MXC6655_BOUNDARY)

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	if (enable)
		sensor->ops->ctrl_data &= ~MXC6655_POWER_DOWN;
	else
		sensor->ops->ctrl_data |= MXC6655_POWER_DOWN;

	result = sensor_write_reg(client, sensor->ops->ctrl_reg,
				  sensor->ops->ctrl_data);
	if (result)
		dev_err(&client->dev, "%s:fail to active sensor\n", __func__);

	pr_debug("%s:reg = 0x%x, reg_ctrl = 0x%x, enable= %d\n",
		 __func__,
		 sensor->ops->ctrl_reg, sensor->ops->ctrl_data, enable);

	return result;
}

static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	int status = 0;
	int result = 0;

	result = sensor->ops->active(client, 0, 0);
	if (result) {
		dev_err(&client->dev,
			"%s:line=%d,error\n", __func__, __LINE__);
		return result;
	}
	sensor->status_cur = SENSOR_OFF;

	/*  Operating mode control and full-scale range(2g) */
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, 0x01);
	if (result) {
		dev_err(&client->dev,
			"%s:fail to set MXC6655_CONTROL.\n", __func__);
		return result;
	}

	/* Enable or Disable for DRDY Interrupt */
	status = sensor_read_reg(client, MXC6655_INT_MASK1);
	if (sensor->pdata->irq_enable)
		status |= MXC6655_INT_ENABLE;
	else
		status &= ~MXC6655_INT_ENABLE;
	result = sensor_write_reg(client, MXC6655_INT_MASK1, status);
	if (result) {
		dev_err(&client->dev,
			"%s:fail to set MXC6655_INT_MASK1.\n", __func__);
		return result;
	}

	return result;
}

static int sensor_convert_data(struct i2c_client *client,
			       char high_byte, char low_byte)
{
	s64 result;

	result = ((int)high_byte << (MXC6655_PRECISION - 8)) |
		 ((int)low_byte >> (16 - MXC6655_PRECISION));

	if (result < MXC6655_BOUNDARY)
		result = result * MXC6655_GRAVITY_STEP;
	else
		result = ~(((~result & (0x7fff >> (16 - MXC6655_PRECISION))) +
			  1) * MXC6655_GRAVITY_STEP) + 1;

	return (int)result;
}

static int gsensor_report_value(struct i2c_client *client,
				struct sensor_axis *axis)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);

	if (sensor->status_cur == SENSOR_ON) {
		input_report_abs(sensor->input_dev, ABS_X, axis->x);
		input_report_abs(sensor->input_dev, ABS_Y, axis->y);
		input_report_abs(sensor->input_dev, ABS_Z, axis->z);
	}

	return 0;
}

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_axis axis;
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	struct sensor_platform_data *pdata = sensor->pdata;
	char buffer[6] = {0};
	char value = 0;
	int x, y, z;
	int ret = 0;

	if (sensor->ops->read_len < 6) {
		dev_err(&client->dev, "%s:Read len is error,len= %d\n",
			__func__, sensor->ops->read_len);
		return -1;
	}

	*buffer = sensor->ops->read_reg;
	ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
	if (ret < 0) {
		dev_err(&client->dev,
			"mxc6655 read data failed, ret = %d\n", ret);
		return ret;
	}

	/* x,y,z axis is the 12-bit acceleration output */
	x = sensor_convert_data(sensor->client, buffer[0], buffer[1]);
	y = sensor_convert_data(sensor->client, buffer[2], buffer[3]);
	z = sensor_convert_data(sensor->client, buffer[4], buffer[5]);

	pr_debug("%s: x = %d, y = %d, z = %d\n", __func__, x, y, z);

	axis.x = (pdata->orientation[0]) * x + (pdata->orientation[1]) * y +
		 (pdata->orientation[2]) * z;
	axis.y = (pdata->orientation[3]) * x + (pdata->orientation[4]) * y +
		 (pdata->orientation[5]) * z;
	axis.z = (pdata->orientation[6]) * x + (pdata->orientation[7]) * y +
		 (pdata->orientation[8]) * z;

	gsensor_report_value(client, &axis);

	mutex_lock(&sensor->data_mutex);
	sensor->axis = axis;
	mutex_unlock(&sensor->data_mutex);

	if (sensor->pdata->irq_enable) {
		value = sensor_read_reg(client, sensor->ops->int_status_reg);
		if (value & 0x01) {
			pr_debug("%s:gsensor int status :0x%x\n",
				 __func__, value);
			ret = sensor_write_reg(client, MXC6655_INT_CLR1, 0x01);
			if (ret) {
				dev_err(&client->dev,
					"%s:fail to clear MXC6655_INT_CLR1.\n",
					__func__);
				return ret;
			}
		}
	}

	return ret;
}

static struct sensor_operate gsensor_mxc6655_ops = {
	.name		= "gs_mxc6655xa",
	.type		= SENSOR_TYPE_ACCEL,
	.id_i2c		= ACCEL_ID_MXC6655XA,
	.read_reg	= MXC6655_OUT_X_H,
	.read_len	= 6,
	.id_reg		= MXC6655_WHO_AM_I_A,
	.id_data	= MXC6655_DEVICE_ID_A,
	.precision	= MXC6655_PRECISION,
	.ctrl_reg	= MXC6655_CONTROL,
	.int_status_reg	= MXC6655_INT_SRC1,
	.range		= {-MXC6655_RANGE, MXC6655_RANGE},
	.trig		= IRQF_TRIGGER_LOW | IRQF_ONESHOT,
	.active		= sensor_active,
	.init		= sensor_init,
	.report		= sensor_report_value,
};

static int gsensor_mxc6655_probe(struct i2c_client *client,
				const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &gsensor_mxc6655_ops);
}

static int gsensor_mxc6655_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gsensor_mxc6655_ops);
}

static const struct i2c_device_id gsensor_mxc6655_id[] = {
	{"gs_mxc6655xa", ACCEL_ID_MXC6655XA},
	{}
};

static struct i2c_driver gsensor_mxc6655_driver = {
	.probe = gsensor_mxc6655_probe,
	.remove = gsensor_mxc6655_remove,
	.shutdown = sensor_shutdown,
	.id_table = gsensor_mxc6655_id,
	.driver = {
		.name = "gsensor_mxc6655",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(gsensor_mxc6655_driver);

MODULE_AUTHOR("Wang Jie <dave.wang@rock-chips.com>");
MODULE_DESCRIPTION("mxc6655 3-Axis accelerometer driver");
MODULE_LICENSE("GPL");

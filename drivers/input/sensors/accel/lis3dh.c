/* drivers/input/sensors/access/kxtik.c
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

#define LIS3DH_INT_COUNT		(0x0E)
#define LIS3DH_WHO_AM_I			(0x0F)

/* full scale setting - register & mask */
#define LIS3DH_TEMP_CFG_REG		(0x1F)
#define LIS3DH_CTRL_REG1		(0x20)
#define LIS3DH_CTRL_REG2		(0x21)
#define LIS3DH_CTRL_REG3		(0x22)
#define LIS3DH_CTRL_REG4		(0x23)
#define LIS3DH_CTRL_REG5		(0x24)
#define LIS3DH_CTRL_REG6		(0x25)
#define LIS3DH_REFERENCE		(0x26)
#define LIS3DH_STATUS_REG		(0x27)
#define LIS3DH_OUT_X_L			(0x28)
#define LIS3DH_OUT_X_H			(0x29)
#define LIS3DH_OUT_Y_L			(0x2a)
#define LIS3DH_OUT_Y_H			(0x2b)
#define LIS3DH_OUT_Z_L			(0x2c)
#define LIS3DH_OUT_Z_H			(0x2d)
#define LIS3DH_FIFO_CTRL_REG		(0x2E)

#define LIS3DH_INT1_CFG			(0x30)
#define LIS3DH_INT1_SRC			(0x31)
#define LIS3DH_INT1_THS			(0x32)
#define LIS3DH_INT1_DURATION		(0x33)
#define LIS3DH_TT_CFG			(0x38)
#define LIS3DH_TT_THS			(0x3a)
#define LIS3DH_TT_LIM			(0x3b)
#define LIS3DH_TT_TLAT			(0x3c)
#define LIS3DH_TT_TW			(0x3d)


#define LIS3DH_DEVID			(0x33)
#define LIS3DH_ACC_DISABLE		(0x08)

#define LIS3DH_RANGE			2000000

/* LIS3DH */
#define LIS3DH_PRECISION		16

#define LIS3DH_ACC_ODR1			0x10  /* 1Hz output data rate */
#define LIS3DH_ACC_ODR10		0x20  /* 10Hz output data rate */
#define LIS3DH_ACC_ODR25		0x30  /* 25Hz output data rate */
#define LIS3DH_ACC_ODR50		0x40  /* 50Hz output data rate */
#define LIS3DH_ACC_ODR100		0x50  /* 100Hz output data rate */
#define LIS3DH_ACC_ODR200		0x60  /* 200Hz output data rate */
#define LIS3DH_ACC_ODR400		0x70  /* 400Hz output data rate */
#define LIS3DH_ACC_ODR1250		0x90  /* 1250Hz output data rate */

struct sensor_reg_data {
	char reg;
	char data;
};

/****************operate according to sensor chip:start************/
/* odr table, hz */
struct odr_table {
	unsigned int cutoff_ms;
	unsigned int mask;
};

static struct odr_table lis3dh_acc_odr_table[] = {
		{1,	LIS3DH_ACC_ODR1250},
		{3,	LIS3DH_ACC_ODR400},
		{5,	LIS3DH_ACC_ODR200},
		{10,	LIS3DH_ACC_ODR100},
		{20,	LIS3DH_ACC_ODR50},
		{40,	LIS3DH_ACC_ODR25},
		{100,	LIS3DH_ACC_ODR10},
		{1000,	LIS3DH_ACC_ODR1},
};

static int lis3dh_select_odr(int want)
{
	int i;
	int max_index = ARRAY_SIZE(lis3dh_acc_odr_table);

	for (i = max_index - 1; i >= 0; i--) {
		if ((lis3dh_acc_odr_table[i].cutoff_ms <= want) ||
		    (i == 0))
			break;
	}

	return lis3dh_acc_odr_table[i].mask;
}

static int sensor_active(struct i2c_client *client, int enable, int rate/*ms*/)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int status = 0;
	int odr_rate = 0;

	if (rate == 0) {
		dev_err(&client->dev, "%s: rate == 0!!!\n", __func__);
		return -1;
	}

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	result = lis3dh_select_odr(odr_rate);
	sensor->ops->ctrl_data |= result;

	if (!enable) {
		status = LIS3DH_ACC_DISABLE;
		sensor->ops->ctrl_data |= status;
	} else {
		sensor->ops->init(client);
		status = ~LIS3DH_ACC_DISABLE;
		sensor->ops->ctrl_data &= status;
	}

	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if (result)
		dev_err(&client->dev, "%s:fail to active sensor\n", __func__);

	return result;
}

static int sensor_init(struct i2c_client *client)
{
	int result = 0;
	int i;

	struct sensor_reg_data reg_data[] = {
		{LIS3DH_CTRL_REG1, 0x07},
		{LIS3DH_TEMP_CFG_REG, 0x00},
		{LIS3DH_FIFO_CTRL_REG, 0x00},
		{LIS3DH_TT_THS, 0x00},
		{LIS3DH_TT_LIM, 0x00},
		{LIS3DH_TT_TLAT, 0x00},
		{LIS3DH_TT_TW, 0x00},
		{LIS3DH_TT_CFG, 0x00},
		{LIS3DH_INT1_THS, 0x7f},
		{LIS3DH_INT1_DURATION, 0x7f},
		{LIS3DH_INT1_CFG, 0xff},
		{LIS3DH_CTRL_REG2, 0x00},
		{LIS3DH_CTRL_REG3, 0x40},
		{LIS3DH_CTRL_REG4, 0x08},
		{LIS3DH_CTRL_REG5, 0x08},
		{LIS3DH_CTRL_REG6, 0x40},
	};

	for (i = 0; i < (sizeof(reg_data) / sizeof(struct sensor_reg_data)); i++) {
		result = sensor_write_reg(client, reg_data[i].reg, reg_data[i].data);
		if (result) {
			dev_err(&client->dev, "%s:line=%d,i=%d,error\n", __func__, __LINE__, i);
			return result;
		}
	}

	return result;
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
	char buffer[6] = {0};

	if (sensor->ops->read_len < 6) {
		dev_err(&client->dev, "%s:lenth is error,len=%d\n", __func__, sensor->ops->read_len);
		return -1;
	}

	memset(buffer, 0, 6);

	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	do {
		*buffer = sensor->ops->read_reg | 0x80;
		ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
		if (ret < 0) {
			dev_err(&client->dev, "lis3dh read data failed, ret = %d\n", ret);
			return ret;
		}
	} while (0);

	x = ((buffer[1] << 8) & 0xff00) + (buffer[0] & 0xFF);
	y = ((buffer[3] << 8) & 0xff00) + (buffer[2] & 0xFF);
	z = ((buffer[5] << 8) & 0xff00) + (buffer[4] & 0xFF);

	axis.x = (pdata->orientation[0]) * x + (pdata->orientation[1]) * y + (pdata->orientation[2]) * z;
	axis.y = (pdata->orientation[3]) * x + (pdata->orientation[4]) * y + (pdata->orientation[5]) * z;
	axis.z = (pdata->orientation[6]) * x + (pdata->orientation[7]) * y + (pdata->orientation[8]) * z;

	gsensor_report_value(client, &axis);

	mutex_lock(&(sensor->data_mutex));
	sensor->axis = axis;
	mutex_unlock(&(sensor->data_mutex));

	if ((sensor->pdata->irq_enable) && (sensor->ops->int_status_reg >= 0))
		sensor_read_reg(client, sensor->ops->int_status_reg);

	return ret;
}

struct sensor_operate gsensor_lis3dh_ops = {
	.name				= "lis3dh",
	.type				= SENSOR_TYPE_ACCEL,
	.id_i2c				= ACCEL_ID_LIS3DH,
	.read_reg				= LIS3DH_OUT_X_L,
	.read_len				= 6,
	.id_reg				= LIS3DH_WHO_AM_I,
	.id_data 				= LIS3DH_DEVID,
	.precision				= LIS3DH_PRECISION,
	.ctrl_reg 				= LIS3DH_CTRL_REG1,
	.int_status_reg 		= LIS3DH_INT1_SRC,
	.range				= {-32768, +32768},
	.trig					= (IRQF_TRIGGER_LOW | IRQF_ONESHOT),
	.active				= sensor_active,
	.init					= sensor_init,
	.report				= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int gsensor_lis3dh_probe(struct i2c_client *client,
				const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &gsensor_lis3dh_ops);
}

static int gsensor_lis3dh_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gsensor_lis3dh_ops);
}

static const struct i2c_device_id gsensor_lis3dh_id[] = {
	{"gs_lis3dh", ACCEL_ID_LIS3DH},
	{}
};

static struct i2c_driver gsensor_lis3dh_driver = {
	.probe = gsensor_lis3dh_probe,
	.remove = gsensor_lis3dh_remove,
	.shutdown = sensor_shutdown,
	.id_table = gsensor_lis3dh_id,
	.driver = {
		.name = "gsensor_lis3dh",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(gsensor_lis3dh_driver);

MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_DESCRIPTION("lis3dh 3-Axis accelerometer driver");
MODULE_LICENSE("GPL");

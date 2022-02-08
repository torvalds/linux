/*
 * kernel/drivers/input/sensors/accel/lsm330_acc.c
 *
 * Copyright (C) 2012-2016 Rockchip Co.,Ltd.
 * Author: Bin Yang <yangbin@rock-chips.com>
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

/* Linear acceleration  register */
#define	WHO_AM_I_A	0x0F
#define	OFF_X	0x10
#define	OFF_Y	0x11
#define	OFF_Z	0x12
#define	CS_X	0x13
#define	CS_Y	0x14
#define	CS_Z	0x15
#define	LC_L	0x16
#define	LC_H	0x17
#define	STAT	0x18
#define	VFC_1	0x1B
#define	VFC_2	0x1C
#define	VFC_3	0x1D
#define	VFC_4	0x1E
#define	THRS3	0x1F
#define	CTRL_REG2_A	0x21
#define	CTRL_REG3_A	0x22
#define	CTRL_REG4_A	0x23
#define	CTRL_REG5_A	0x20
#define	CTRL_REG6_A	0x24
#define	CTRL_REG7_A	0x25
#define	STATUS_REG_A	0x27
#define	OUT_X_L_A	0x28
#define	OUT_X_H_A	0x29
#define	OUT_Y_L_A	0x2A
#define	OUT_Y_H_A	0x2B
#define	OUT_Z_L_A	0x2C
#define	OUT_Z_H_A	0x2D
#define	FIFO_CTRL_REG	0x2E
#define	FIFO_SRC_REG	0x2F

#define	INT1_CFG_A	0x30
#define	INT1_SOURCE_A	0x31
#define	INT1_THS_A	0x32
#define	INT1_DURATION_A	0x33
#define	INT2_CFG_A	0x34
#define	INT2_SOURCE_A	0x35
#define	INT2_THS_A	0x36
#define	INT2_DURATION_A	0x37
#define	CLICK_CFG_A	0x38
#define	CLICK_SRC_A	0x39
#define	CLICK_THS_A	0x3A
#define	TIME_LIMIT_A	0x3B
#define	TIME_LATENCY_A	0x3C
#define	TIME_WINDOW_A	0x3D
#define	ACT_THS	0x3E
#define	ACT_DUR	0x3F

#define	LSM330_DEVICE_ID_A	0x40

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	if (enable)
		result = sensor_write_reg(client,
			sensor->ops->ctrl_reg,
			sensor->ops->ctrl_data | 0x07);
	else
		result = sensor_write_reg(client, sensor->ops->ctrl_reg, 0x00);

	if (result)
		dev_err(&client->dev, "%s:fail to active sensor\n", __func__);

	DBG("%s:reg=0x%x,reg_ctrl=0x%x,enable=%d\n",
		__func__,
		sensor->ops->ctrl_reg,
		sensor->ops->ctrl_data, enable);

	return result;
}

static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;

	result = sensor->ops->active(client, 0, 0);
	if (result) {
		dev_err(&client->dev, "%s:line=%d,error\n",
			__func__, __LINE__);
		return result;
	}
	sensor->status_cur = SENSOR_OFF;

	result = sensor_write_reg(client, CTRL_REG4_A, 0xE8);
	if (result) {
		dev_err(&client->dev, "%s:fail to set CTRL_REG4_A.\n",
			__func__);
		return result;
	}
	/* Normal / Low power mode (1600 Hz) */
	result = sensor_write_reg(client, CTRL_REG5_A, 0x90);
	if (result) {
		dev_err(&client->dev, "%s:fail to set CTRL_REG5_A.\n",
			__func__);
		return result;
	}
	result = sensor_write_reg(client, CTRL_REG6_A, 0x00);
	if (result) {
		dev_err(&client->dev, "%s:fail to set CTRL_REG6_A.\n",
			__func__);
		return result;
	}
	result = sensor_write_reg(client, CTRL_REG7_A, 0x00);
	if (result) {
		dev_err(&client->dev, "%s:fail to set CTRL_REG7_A.\n",
			__func__);
		return result;
	}

	return result;
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
		input_sync(sensor->input_dev);
	}

	return 0;
}

#define GSENSOR_MIN 10

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	struct sensor_platform_data *pdata = sensor->pdata;
	char buffer[6];
	int ret = 0;
	char value = 0;
	struct sensor_axis axis;
	short x, y, z;
	unsigned char reg_buf = 0;
	unsigned char i = 0;

	if (sensor->ops->read_len < 6) {
		dev_err(&client->dev, "%s:Read len is error,len=%d\n",
			__func__,
			sensor->ops->read_len);
		return -1;
	}
	memset(buffer, 0, 6);

	reg_buf = sensor->ops->read_reg;
	for (i = 0; i < sensor->ops->read_len; i++) {
		buffer[i] = sensor_read_reg(client, reg_buf);
		reg_buf++;
	}

	x = ((buffer[1] << 8) & 0xFF00) + (buffer[0] & 0xFF);
	y = ((buffer[3] << 8) & 0xFF00) + (buffer[2] & 0xFF);
	z = ((buffer[5] << 8) & 0xFF00) + (buffer[4] & 0xFF);

	axis.x = (pdata->orientation[0]) * x +
		(pdata->orientation[1]) * y +
		(pdata->orientation[2]) * z;
	axis.y = (pdata->orientation[3]) * x +
		(pdata->orientation[4]) * y +
		(pdata->orientation[5]) * z;
	axis.z = (pdata->orientation[6]) * x +
		(pdata->orientation[7]) * y +
		(pdata->orientation[8]) * z;

	gsensor_report_value(client, &axis);
	mutex_lock(&sensor->data_mutex);
	sensor->axis = axis;
	mutex_unlock(&sensor->data_mutex);

	if (sensor->pdata->irq_enable) {
		value = sensor_read_reg(client, sensor->ops->int_status_reg);
		DBG("%s:gsensor int status :0x%x\n", __func__, value);
	}

	return ret;
}

static struct sensor_operate gsensor_lsm330_ops = {
	.name			= "lsm330_acc",
	.type			= SENSOR_TYPE_ACCEL,
	.id_i2c			= ACCEL_ID_LSM330,
	.read_reg			= OUT_X_L_A,
	.read_len			= 6,
	.id_reg			= WHO_AM_I_A,
	.id_data			= LSM330_DEVICE_ID_A,
	.precision			= 16,
	.ctrl_reg			= CTRL_REG5_A,
	.int_status_reg	= STATUS_REG_A,
	.range			= {-32768, 32768},
	.trig				= IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
	.active			= sensor_active,
	.init				= sensor_init,
	.report			= sensor_report_value,
};

static int gsensor_lsm330_probe(struct i2c_client *client,
				const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &gsensor_lsm330_ops);
}

static int gsensor_lsm330_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gsensor_lsm330_ops);
}

static const struct i2c_device_id gsensor_lsm330_id[] = {
	{"lsm330_acc", ACCEL_ID_LSM330},
	{}
};

static struct i2c_driver gsensor_lsm330_driver = {
	.probe = gsensor_lsm330_probe,
	.remove = gsensor_lsm330_remove,
	.shutdown = sensor_shutdown,
	.id_table = gsensor_lsm330_id,
	.driver = {
		.name = "gsensor_lsm330",
#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
#endif
	},
};

module_i2c_driver(gsensor_lsm330_driver);

MODULE_AUTHOR("Bin Yang <yangbin@rock-chips.com>");
MODULE_DESCRIPTION("lsm330_acc 3-Axis accelerometer driver");
MODULE_LICENSE("GPL");

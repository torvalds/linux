/*
 * kernel/drivers/input/sensors/accel/lsm330_gyro.c
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

/* Angular rate sensor register */
#define	WHO_AM_I_G	0x0F
#define	CTRL_REG1_G	0x20
#define	CTRL_REG2_G	0x21
#define	CTRL_REG3_G	0x22
#define	CTRL_REG4_G	0x23
#define	CTRL_REG5_G	0x24
#define	REFERENCE_G	0x25
#define	OUT_TEMP_G	0x26
#define	STATUS_REG_G	0x27
#define	OUT_X_L_G	0x28
#define	OUT_X_H_G	0x29
#define	OUT_Y_L_G	0x2A
#define	OUT_Y_H_G	0x2B
#define	OUT_Z_L_G	0x2C
#define	OUT_Z_H_G	0x2D
#define	FIFO_CTRL_REG_G	0x2E
#define	FIFO_SRC_REG_G	0x2F
#define	INT1_CFG_G	0x30
#define	INT1_SRC_G	0x31
#define	INT1_TSH_XH_G	0x32
#define	INT1_TSH_XL_G	0x33
#define	INT1_TSH_YH_G	0x34
#define	INT1_TSH_YL_G	0x35
#define	INT1_TSH_ZH_G	0x36
#define	INT1_TSH_ZL_G	0x37
#define	INT1_DURATION_G	0x38

#define	LSM330_DEVICE_ID_G	0xD4

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	if (enable)
		result = sensor_write_reg(client,
		sensor->ops->ctrl_reg,
		sensor->ops->ctrl_data | 0x0F);
	else
		result = sensor_write_reg(client,
		sensor->ops->ctrl_reg,
		0x00);

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
		dev_err(&client->dev,
			"%s:line=%d,error\n",
			__func__,
			__LINE__);
		return result;
	}
	sensor->status_cur = SENSOR_OFF;

	/*ODR: 760Hz, Cut-off: 100Hz*/
	result = sensor_write_reg(client, CTRL_REG1_G, 0xF0);
	if (result) {
		dev_err(&client->dev,
			"%s:fail to set CTRL_REG1_A.\n",
			__func__);
		return result;
	}
	result = sensor_write_reg(client, CTRL_REG3_G, 0x80);
	if (result) {
		dev_err(&client->dev,
			"%s:fail to set CTRL_REG3_G.\n",
			__func__);
		return result;
	}
	/*Full-scale selection: 2000dps*/
	result = sensor_write_reg(client, CTRL_REG4_G, 0x30);
	if (result) {
		dev_err(&client->dev,
			"%s:fail to set CTRL_REG4_G.\n",
			__func__);
		return result;
	}
	result = sensor_write_reg(client, INT1_CFG_G, 0x7F);
	if (result) {
		dev_err(&client->dev, "%s:fail to set INT1_CFG_G.\n", __func__);
		return result;
	}

	return result;
}

static int gyro_report_value(struct i2c_client *client,
				struct sensor_axis *axis)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);

	if (sensor->status_cur == SENSOR_ON) {
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

	gyro_report_value(client, &axis);
	mutex_lock(&sensor->data_mutex);
	sensor->axis = axis;
	mutex_unlock(&sensor->data_mutex);

	if (sensor->pdata->irq_enable) {
		value = sensor_read_reg(client, sensor->ops->int_status_reg);
		DBG("%s:gyro int status :0x%x\n", __func__, value);
	}

	return ret;
}

struct sensor_operate gyro_lsm330_ops = {
	.name			= "lsm330_gyro",
	.type			= SENSOR_TYPE_GYROSCOPE,
	.id_i2c			= GYRO_ID_LSM330,
	.read_reg		= OUT_X_L_G,
	.read_len		= 6,
	.id_reg			= WHO_AM_I_G,
	.id_data		= LSM330_DEVICE_ID_G,
	.precision		= 16,
	.ctrl_reg		= CTRL_REG1_G,
	.int_status_reg	= INT1_SRC_G,
	.range			= {-32768, 32768},
	.trig			= IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
	.active			= sensor_active,
	.init			= sensor_init,
	.report			= sensor_report_value,
};

static struct sensor_operate *gyro_get_ops(void)
{
	return &gyro_lsm330_ops;
}

static int __init gyro_lsm330_init(void)
{
	struct sensor_operate *ops = gyro_get_ops();
	int result = 0;
	int type = ops->type;

	result = sensor_register_slave(type, NULL, NULL, gyro_get_ops);

	return result;
}

static void __exit gyro_lsm330_exit(void)
{
	struct sensor_operate *ops = gyro_get_ops();
	int type = ops->type;

	sensor_unregister_slave(type, NULL, NULL, gyro_get_ops);
}

module_init(gyro_lsm330_init);
module_exit(gyro_lsm330_exit);

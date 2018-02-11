/* drivers/input/sensors/access/mma7660.c
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

#define MMA7660_ENABLE			1
#define MMA7660_REG_X_OUT		0x0
#define MMA7660_REG_Y_OUT		0x1
#define MMA7660_REG_Z_OUT		0x2
#define MMA7660_REG_TILT		0x3
#define MMA7660_REG_SRST		0x4
#define MMA7660_REG_SPCNT		0x5
#define MMA7660_REG_INTSU		0x6
#define MMA7660_REG_MODE		0x7
#define MMA7660_REG_SR			0x8
#define MMA7660_REG_PDET		0x9
#define MMA7660_REG_PD		0xa
#define MMA7660_PRECISION		6

/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;
	int status = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);

	if (enable) {
		status = MMA7660_ENABLE;
		sensor->ops->ctrl_data |= status;	
	} else {
		status = ~MMA7660_ENABLE;
		sensor->ops->ctrl_data &= status;
	}

	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if (result)
		dev_err(&client->dev, "%s:fail to active sensor\n", __func__);

	return result;
}

static int sensor_init(struct i2c_client *client)
{	
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;

	result = sensor->ops->active(client, 0, 0);
	if (result) {
		dev_err(&client->dev, "%s:line=%d,error\n", __func__, __LINE__);
		return result;
	}

	sensor->status_cur = SENSOR_OFF;

	/*120 Samples/Second Active and Auto-Sleep Mode */
	result = sensor_write_reg(client, MMA7660_REG_SR, 0x01 << 5);
	if (result) {
		dev_err(&client->dev, "%s:line=%d,error\n", __func__, __LINE__);
		return result;
	}

	if (sensor->pdata->irq_enable) {
		result = sensor_write_reg(client, MMA7660_REG_INTSU, 1 << 4);
		if (result) {
			dev_err(&client->dev, "%s:line=%d,error\n", __func__, __LINE__);
			return result;
		}
	}

	sensor->ops->ctrl_data = 1 << 6;
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if (result) {
		dev_err(&client->dev, "%s:line=%d,error\n", __func__, __LINE__);
		return result;
	}

	return result;
}

static int sensor_convert_data(struct i2c_client *client, char high_byte, char low_byte)
{
	int result = (int)low_byte;

	if (low_byte & 0x20)
		result = ((~result & 0x1f) + 1) * (-768);
	else
		result = (result & 0x1f) * 768;

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
		(struct sensor_private_data *)i2c_get_clientdata(client);
	struct sensor_platform_data *pdata = sensor->pdata;
	int ret = 0;
	int x, y, z;
	struct sensor_axis axis;
	char buffer[3] = {0};
	char value = 0;
	static int flag;

	if (sensor->ops->read_len < 3) {
		dev_err(&client->dev, "%s:lenth is error,len=%d\n", __func__, sensor->ops->read_len);
		return -1;
	}

	memset(buffer, 0, 3);

	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	do {
		*buffer = sensor->ops->read_reg;
		ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
		if (ret < 0)
			return ret;
	} while (0);

	x = sensor_convert_data(sensor->client, 0, buffer[0]);
	y = sensor_convert_data(sensor->client, 0, buffer[1]);
	z = sensor_convert_data(sensor->client, 0, buffer[2]);

	axis.x = (pdata->orientation[0]) * x + (pdata->orientation[1]) * y + (pdata->orientation[2]) * z;
	axis.y = (pdata->orientation[3]) * x + (pdata->orientation[4]) * y + (pdata->orientation[5]) * z;
	axis.z = (pdata->orientation[6]) * x + (pdata->orientation[7]) * y + (pdata->orientation[8]) * z;

	/*
	 *input dev will ignore report data if data value is the same with last_value,
	 *sample rate will not enough by this way, so just avoid this case
	 */
	if ((sensor->axis.x == axis.x) && (sensor->axis.y == axis.y) && (sensor->axis.z == axis.z)) {
		if (flag) {
			flag = 0;
			axis.x += 1;
			axis.y += 1;
			axis.z += 1;
		} else {
			flag = 1;
			axis.x -= 1;
			axis.y -= 1;
			axis.z -= 1;
		}
	}

	gsensor_report_value(client, &axis);

	mutex_lock(&sensor->data_mutex);
	sensor->axis = axis;
	mutex_unlock(&sensor->data_mutex);

	if (sensor->pdata->irq_enable && sensor->ops->int_status_reg >= 0)
		value = sensor_read_reg(client, sensor->ops->int_status_reg);

	return ret;
}

struct sensor_operate gsensor_mma7660_ops = {
	.name			= "mma7660",
	.type			= SENSOR_TYPE_ACCEL,
	.id_i2c			= ACCEL_ID_MMA7660,
	.read_reg			= MMA7660_REG_X_OUT,
	.read_len			= 3,
	.id_reg			= SENSOR_UNKNOW_DATA,
	.id_data			= SENSOR_UNKNOW_DATA,
	.precision			= MMA7660_PRECISION,
	.ctrl_reg			= MMA7660_REG_MODE,
	.int_status_reg	= SENSOR_UNKNOW_DATA,
	.range			= {-24576, 24576},
	.trig				= IRQF_TRIGGER_LOW | IRQF_ONESHOT,
	.active			= sensor_active,
	.init				= sensor_init,
	.report			= sensor_report_value,
};

/****************operate according to sensor chip:end************/

//function name should not be changed
static struct sensor_operate *gsensor_get_ops(void)
{
	return &gsensor_mma7660_ops;
}

static int __init gsensor_mma7660_init(void)
{
	struct sensor_operate *ops = gsensor_get_ops();
	int type = ops->type;

	return sensor_register_slave(type, NULL, NULL, gsensor_get_ops);
}

static void __exit gsensor_mma7660_exit(void)
{
	struct sensor_operate *ops = gsensor_get_ops();
	int type = ops->type;

	sensor_unregister_slave(type, NULL, NULL, gsensor_get_ops);
}

module_init(gsensor_mma7660_init);
module_exit(gsensor_mma7660_exit);

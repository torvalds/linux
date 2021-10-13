// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Kay Guo <kay.guo@rock-chips.com>
 */
#include <linux/atomic.h>
#include <linux/delay.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/freezer.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/of_gpio.h>
#include <linux/sensor-dev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#include "da215s_core.h"

/* Linear acceleration  register */
#define DA215S_CONFIG	0X00
#define DA215S_CHIP_ID	0x01
#define ACC_X_LSB	0x02
#define ACC_X_MSB	0x03
#define ACC_Y_LSB	0x04
#define ACC_Y_MSB	0x05
#define ACC_Z_LSB       0x06
#define ACC_Z_MSB       0x07
#define MOTION_FLAG	0x09
#define NEWDATA_FLAG	0x0A
#define ACTIVE_STATUS	0x0B
#define DA215S_RANGE	0x0F
#define ODR_AXIS	0x10
#define DA215S_MODE_BW	0x11
#define SWAP_POLARITY	0x12
#define INT_ACTIVE_SET1	0x16
#define INT_DATA_SET2	0x17
#define INT_MAP1	0x19
#define INT_MAP2	0x1A
#define INT_CONFIG	0x20
#define INT_LATCH	0x21
#define ACTIVE_DUR	0x27
#define ACTIVE_THS	0x28

#define DA215S_CHIPID_DATA	0x13
#define DA215S_CTRL_NORMAL	0x34
#define DA215S_CTRL_SUSPEND	0x80
#define INT_ACTIVE_ENABLE	0x87
#define INT_NEW_DATA_ENABLE	0x10

#define DA215S_OFFSET_MAX	200
#define DA215S_OFFSET_CUS	130
#define DA215S_OFFSET_SEN	1024

#define GSENSOR_MIN		2
#define DA215S_PRECISION	14
#define DA215S_DATA_RANGE	(16384*4)
#define DA215S_BOUNDARY		(0x1 << (DA215S_PRECISION - 1))
#define DA215S_GRAVITY_STEP	(DA215S_DATA_RANGE/DA215S_BOUNDARY)


static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	if (enable)
		sensor->ops->ctrl_data &= DA215S_CTRL_NORMAL;
	else
		sensor->ops->ctrl_data |= DA215S_CTRL_SUSPEND;

	result = sensor_write_reg(client, sensor->ops->ctrl_reg,
				  sensor->ops->ctrl_data);
	if (result)
		dev_err(&client->dev, "%s:fail to active sensor\n", __func__);

	dev_dbg(&client->dev, "reg = 0x%x, reg_ctrl = 0x%x, enable= %d\n",
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

	result = sensor_write_reg(client, 0x00, 0x24);
	mdelay(25);
	/*+/-4G,14bit  normal mode  ODR = 62.5hz*/
	result |= sensor_write_reg(client, DA215S_RANGE, 0x61);
	result |= sensor_write_reg(client, DA215S_MODE_BW, 0x34);
	result |= sensor_write_reg(client, ODR_AXIS, 0x06);
	if (result) {
		dev_err(&client->dev, "%s:fail to config DA215S_accel.\n",
		__func__);
		return result;
	}


	/* Enable or Disable for active Interrupt */
	status = sensor_read_reg(client, INT_ACTIVE_SET1);
	if (sensor->pdata->irq_enable)
		status |= INT_ACTIVE_ENABLE;
	else
		status &= ~INT_ACTIVE_ENABLE;
	result = sensor_write_reg(client, INT_ACTIVE_SET1, status);
	if (result) {
		dev_err(&client->dev,
			"%s:fail to set DA215S_INT_ACTIVE.\n", __func__);
		return result;
	}

	/* Enable or Disable for new data Interrupt */
	status = sensor_read_reg(client, INT_DATA_SET2);
	if (sensor->pdata->irq_enable)
		status |= INT_NEW_DATA_ENABLE;
	else
		status &= ~INT_NEW_DATA_ENABLE;
	result = sensor_write_reg(client, INT_DATA_SET2, status);
	if (result) {
		dev_err(&client->dev,
			"%s:fail to set DA215S_INT_NEW_DATA.\n", __func__);
		return result;
	}

	return result;
}

static int sensor_convert_data(struct i2c_client *client,
			      unsigned char low_byte4, unsigned char high_byte8)
{
	s64 result;

	result = ((short)((high_byte8 << 8)|low_byte4)) >> 2;

	return (int)result;
}


static int gsensor_report_value(struct i2c_client *client,
				struct sensor_axis *axis)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	if ((abs(sensor->axis.x - axis->x) > GSENSOR_MIN) ||
	    (abs(sensor->axis.y - axis->y) > GSENSOR_MIN) ||
	    (abs(sensor->axis.z - axis->z) > GSENSOR_MIN)) {
		input_report_abs(sensor->input_dev, ABS_X, axis->x);
		input_report_abs(sensor->input_dev, ABS_Y, axis->y);
		input_report_abs(sensor->input_dev, ABS_Z, axis->z);
		input_sync(sensor->input_dev);
	}

	return 0;
}

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_axis axis;
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	struct sensor_platform_data *pdata = sensor->pdata;
	unsigned char buffer[6] = {0};
	int x = 0, y = 0, z = 0;
	int ret = 0;
	int tmp_x = 0, tmp_y = 0, tmp_z = 0;

	if (sensor->ops->read_len < 6) {
		dev_err(&client->dev, "%s:Read len is error,len= %d\n",
			__func__, sensor->ops->read_len);
		return -EINVAL;
	}

	*buffer = sensor->ops->read_reg;
	sensor_rx_data(client, buffer, sensor->ops->read_len);
	if (ret < 0) {
		dev_err(&client->dev,
			"da215s read data failed, ret = %d\n", ret);
		return ret;
	}

	/* x,y,z axis is the 12-bit acceleration output */
	x = sensor_convert_data(sensor->client, buffer[0], buffer[1]);
	y = sensor_convert_data(sensor->client, buffer[2], buffer[3]);
	z = sensor_convert_data(sensor->client, buffer[4], buffer[5]);

	dev_dbg(&client->dev, "%s:x=%d, y=%d, z=%d\n",  __func__, x, y, z);
	da215s_temp_calibrate(&x, &y, &z);

	dev_dbg(&client->dev, "%s:x=%d, y=%d, z=%d\n",  __func__, x, y, z);

	tmp_x = x * DA215S_GRAVITY_STEP;
	tmp_y = y * DA215S_GRAVITY_STEP;
	tmp_z = z * DA215S_GRAVITY_STEP;
	dev_dbg(&client->dev, "%s:temp_x=%d, temp_y=%d, temp_z=%d\n",
		__func__, tmp_x, tmp_y, tmp_z);

	axis.x = (pdata->orientation[0]) * tmp_x + (pdata->orientation[1]) * tmp_y +
		 (pdata->orientation[2]) * tmp_z;
	axis.y = (pdata->orientation[3]) * tmp_x + (pdata->orientation[4]) * tmp_y +
		 (pdata->orientation[5]) * tmp_z;
	axis.z = (pdata->orientation[6]) * tmp_x + (pdata->orientation[7]) * tmp_y +
		 (pdata->orientation[8]) * tmp_z;
	dev_dbg(&client->dev, "<map:>axis = %d, %d, %d\n", axis.x, axis.y, axis.z);

	gsensor_report_value(client, &axis);

	mutex_lock(&(sensor->data_mutex));
	sensor->axis = axis;
	mutex_unlock(&(sensor->data_mutex));

	if (sensor->pdata->irq_enable) {
		ret = sensor_write_reg(client, INT_MAP1, 0);
		if (ret) {
			dev_err(&client->dev,
				"%s:fail to clear DA215S_INT_register.\n",
				__func__);
			return ret;
		}
		ret = sensor_write_reg(client, INT_MAP2, 0);
		if (ret) {
			dev_err(&client->dev,
				"%s:fail to clear DA215S_INT_register.\n",
				__func__);
			return ret;
		}
	}

	return ret;
}

/******************************************************************************/
static int sensor_suspend(struct i2c_client *client)
{
	int result = 0;

//	MI_FUN;
//	result = mir3da_set_enable(client, false);
//	if (result) {
//		MI_ERR("sensor_suspend disable  fail!!\n");
//		return result;
//	}

	return result;
}

/******************************************************************************/
static int sensor_resume(struct i2c_client *client)
{
	int result = 0;

//	MI_FUN;

	/*
	 * result = mir3da_chip_resume(client);
	 * if(result) {
	 * MI_ERR("sensor_resume chip resume fail!!\n");
	 * return result;
	 * }
	 */
//	result = mir3da_set_enable(client, true);
//	if (result) {
//		MI_ERR("sensor_resume enable  fail!!\n");
//		return result;
//	}

	return result;
}

static struct sensor_operate gsensor_da215s_ops = {
	.name		= "gs_da215s",
	.type		= SENSOR_TYPE_ACCEL,
	.id_i2c		= ACCEL_ID_DA215S,
	.read_reg	= ACC_X_LSB,
	.read_len	= 6,
	.id_reg		= DA215S_CHIP_ID,
	.id_data	= DA215S_CHIPID_DATA,
	.precision	= DA215S_PRECISION,
	.ctrl_reg	= DA215S_MODE_BW,
	.int_status_reg	= INT_MAP1,
	.range          = {-DA215S_DATA_RANGE, DA215S_DATA_RANGE},
	.trig		= IRQF_TRIGGER_LOW | IRQF_ONESHOT,
	.active		= sensor_active,
	.init		= sensor_init,
	.report		= sensor_report_value,
	.suspend        = sensor_suspend,
	.resume         = sensor_resume,
};

static int gsensor_da215s_probe(struct i2c_client *client,
				const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &gsensor_da215s_ops);
}

static int gsensor_da215s_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gsensor_da215s_ops);
}

static const struct i2c_device_id gsensor_da215s_id[] = {
	{"gs_da215s", ACCEL_ID_DA215S},
	{}
};

static struct i2c_driver gsensor_da215s_driver = {
	.probe = gsensor_da215s_probe,
	.remove = gsensor_da215s_remove,
	.shutdown = sensor_shutdown,
	.id_table = gsensor_da215s_id,
	.driver = {
		.name = "gsensor_da215s",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(gsensor_da215s_driver);

MODULE_AUTHOR("Guo Wangqiang <kay.guo@rock-chips.com>");
MODULE_DESCRIPTION("da215s 3-Axis accelerometer driver");
MODULE_LICENSE("GPL");

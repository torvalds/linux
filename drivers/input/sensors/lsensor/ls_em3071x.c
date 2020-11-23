// SPDX-License-Identifier: GPL-2.0
/*
 * kernel/drivers/input/sensors/lsensor/ls_em3071x.c
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

#define ALS_CMD		0x01
#define STA_TUS		0X02
#define ALS_DT1		0x09
#define ALS_DT2		0X0a
#define ALS_THDL1	0X05
#define ALS_THDL2	0X06
#define ALS_THDH1	0X07

#define SW_RESET	0X0E

/* ALS_CMD */
#define ALS_SD_ENABLE	0x06
#define ALS_SD_DISABLE	0xF8
#define ALS_INT_DISABLE	(0 << 1)
#define ALS_INT_ENABLE	(1 << 1)
#define ALS_1T_100MS	(0 << 2)
#define ALS_2T_200MS	(1 << 2)
#define ALS_4T_400MS	(2 << 2)
#define ALS_8T_800MS	(3 << 2)
#define ALS_RANGE_57671	(0 << 6)
#define ALS_RANGE_28836	(1 << 6)

/* PS_CMD */
#define PS_SD_ENABLE	(0 << 0)
#define PS_SD_DISABLE	(1 << 0)
#define PS_INT_DISABLE	(0 << 1)
#define PS_INT_ENABLE	(1 << 1)
#define PS_10T_2MS	(0 << 2)
#define PS_15T_3MS	(1 << 2)
#define PS_20T_4MS	(2 << 2)
#define PS_25T_5MS	(3 << 2)
#define PS_CUR_100MA	(0 << 4)
#define PS_CUR_200MA	(1 << 4)
#define PS_SLP_10MS	(0 << 5)
#define PS_SLP_30MS	(1 << 5)
#define PS_SLP_90MS	(2 << 5)
#define PS_SLP_270MS	(3 << 5)
#define TRIG_PS_OR_LS	(0 << 7)
#define TRIG_PS_AND_LS	(1 << 7)

/* STA_TUS */
#define STA_PS_INT	(1 << 5)
#define	STA_ALS_INT	(1 << 4)

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int status = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	if (enable) {
		status = ALS_SD_ENABLE;
		sensor->ops->ctrl_data |= status;
	} else {
		status = ~ALS_SD_ENABLE;
		sensor->ops->ctrl_data &= status;
	}

	result = sensor_write_reg(client, sensor->ops->ctrl_reg,
					sensor->ops->ctrl_data);
	if (result) {
		dev_err(&client->dev, "%s:fail to active sensor\n", __func__);
		return result;
	}

	dev_dbg(&client->dev, "%s:reg = 0x%x, reg_ctrl = 0x%x, enable= %d\n",
		  __func__,
		  sensor->ops->ctrl_reg, sensor->ops->ctrl_data, enable);

	return result;
}


static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;

	result = sensor->ops->active(client, 0, 0);
	if (result) {
		dev_err(&client->dev, "%s: line = %d, result = %d\n",
			__func__, __LINE__, result);
		return result;
	}

	sensor->status_cur = SENSOR_OFF;
	result = sensor_write_reg(client, SW_RESET, 0);
	if (result) {
		dev_err(&client->dev,
			"%s: fail to set SW_RESET(%d)\n", __func__, result);
		return result;
	}

	/* it is important,if not then als can not trig intterupt */
	result = sensor_write_reg(client, ALS_THDL1, 0);
	if (result) {
		dev_err(&client->dev,
			"%s: fail to set ALS_THDL1(%d)\n", __func__, result);
		return result;
	}

	result = sensor_write_reg(client, ALS_THDL2, 0XF0);
	if (result) {
		dev_err(&client->dev,
			"%s: fail to set ALS_THDL2(%d)\n", __func__, result);
		return result;
	}

	result = sensor_write_reg(client, ALS_THDH1, 0XFF);
	if (result) {
		dev_err(&client->dev,
			"%s: fail to set ALS_THDH1(%d)\n", __func__, result);
		return result;
	}

	result = sensor_write_reg(client, STA_TUS, 0X00);
	if (result) {
		dev_err(&client->dev,
			"%s: fail to set STA_TUS(%d)\n", __func__, result);
		return result;
	}

	return result;
}


static int light_report_value(struct input_dev *input, int data)
{
	unsigned char index = 0;

	if (data <= 10) {
		index = 0;
		goto report;
	} else if (data <= 60) {
		index = 1;
		goto report;
	} else if (data <= 122) {
		index = 2;
		goto report;
	} else if (data <= 200) {
		index = 3;
		goto report;
	} else if (data <= 400) {
		index = 4;
		goto report;
	} else if (data <= 800) {
		index = 5;
		goto report;
	} else if (data <= 1260) {
		index = 6;
		goto report;
	} else {
		index = 7;
		goto report;
	}

report:
	input_report_abs(input, ABS_MISC, index);
	input_sync(input);

	return index;
}


static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int value = 0;
	char buffer[2] = {0};
	char index = 0;

	buffer[0] = sensor_read_reg(client, 0X09);
	buffer[1] = sensor_read_reg(client, 0X0A);
	value = ((buffer[1] & 0X0F) << 8) | buffer[0];

	index = light_report_value(sensor->input_dev, value);

	dev_dbg(&client->dev,
		 "%s: value = %d, index = %d\n", __func__, value, index);

	if (sensor->pdata->irq_enable) {
		value = sensor_read_reg(client, sensor->ops->int_status_reg);
		if (value & STA_ALS_INT) {
			value &= ~STA_ALS_INT;
			result = sensor_write_reg(client,
					sensor->ops->int_status_reg, value);
			if (result) {
				dev_err(&client->dev,
					"%s:write status reg error(%d)\n",
					__func__, result);
				return result;
			}
		}
	}

	return result;
}

struct sensor_operate light_em3071x_ops = {
	.name		= "ls_em3071x",
	.type		= SENSOR_TYPE_LIGHT,
	.id_i2c		= LIGHT_ID_EM3071X,
	.read_reg	= ALS_DT1,
	.read_len	= 2,
	.id_reg		= SENSOR_UNKNOW_DATA,
	.id_data	= SENSOR_UNKNOW_DATA,
	.precision	= 16,
	.ctrl_reg	= ALS_CMD,
	.int_status_reg = STA_TUS,
	.range		= {100, 65535},
	.brightness	= {10, 255},
	.trig		= IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED,
	.active		= sensor_active,
	.init		= sensor_init,
	.report		= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int light_em3071x_probe(struct i2c_client *client,
				   const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &light_em3071x_ops);
}

static int light_em3071x_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &light_em3071x_ops);
}

static const struct i2c_device_id light_em3071x_id[] = {
	{"ls_em3071x", LIGHT_ID_EM3071X},
	{}
};

static struct i2c_driver light_em3071x_driver = {
	.probe = light_em3071x_probe,
	.remove = light_em3071x_remove,
	.shutdown = sensor_shutdown,
	.id_table = light_em3071x_id,
	.driver = {
		.name = "light_em3071x",
#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
#endif
	},
};

module_i2c_driver(light_em3071x_driver);

MODULE_AUTHOR("Wang Jie <dave.wang@rock-chips.com>");
MODULE_DESCRIPTION("em3071x light driver");
MODULE_LICENSE("GPL");

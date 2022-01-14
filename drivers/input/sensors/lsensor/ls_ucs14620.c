// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
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

#define	SYSM_CTRL	0x00
#define	INT_CTRL	0x01
#define	INT_FLAG	0x02
#define	WAIT_TIME	0x03
#define	ALS_GAIN	0x04
#define	ALS_TIME	0x05
#define	LED_CTRL	0x06
#define	PS_GAIN		0x07
#define	PS_PULSE	0x08
#define	PS_TIME		0x09

#define PERSISTENCE	0x0B
#define	ALS_THR_LL	0x0C
#define	ALS_THR_LH	0x0D
#define	ALS_THR_HL	0x0E
#define	ALS_THR_HH	0x0F
#define	PS_THR_LL	0x10
#define	PS_THR_LH	0x11
#define	PS_THR_HL	0x12
#define	PS_THR_HH	0x13
#define	PS_OFFSET_L	0x14
#define PS_OFFSET_H	0x15
#define INT_SOURCE	0x16
#define ERROR_FLAG	0x17
#define PS_DATA_L	0x18
#define PS_DATA_H	0x19
#define IR_DATA_L	0x1A
#define IR_DATA_H	0x1B
#define CH0_DATA_L	0x1C
#define CH0_DATA_H	0x1D
#define CH1_DATA_L	0x1E
#define CH1_DATA_H	0x1F

/* SYSM_CTRL	0x00 */
#define	ALS_DISABLE	(0 << 0)
#define	ALS_ENABLE	(1 << 0)
#define	PS_DISABLE	(0 << 1)
#define	PS_ENABLE	(1 << 1)
#define	FRST_DISABLE	(0 << 5)
#define	FRST_ENABLE	(1 << 5)
#define WAIT_DISABLE	(0 << 6)
#define WAIT_ENABLE	(1 << 6)
#define	SWRST_START	(1 << 7)

/* INT_CTRL 0x01 */
#define AINT_DISABLE	(0 << 0)
#define AINT_ENABLE	(1 << 0)
#define PINT_DISABLE	(0 << 1)
#define PINT_ENABLE     (1 << 1)
#define ALS_PEND_EN	(1 << 4)
#define ALS_PEND_DIS	(0 << 4)
#define PS_PEND_EN	(1 << 5)
#define PS_PEND_DIS	(0 << 5)
#define SPEED_UP_EN	(1 << 6)
#define SPEED_UP_DIS	(0 << 6)
#define PS_INT_HYS	(0 << 7)
#define PS_INT_ZONE	(1 << 7)

/* INT_FLAG 0x02 */
#define	ALS_INT_FLAG	(1 << 0)
#define	PS_INT_FLAG	(1 << 1)
#define	OBJ_DET_FLAG	(1 << 5)
#define	DATA_INVALID	(1 << 6)
#define	POWER_ON_FLAG	(1 << 7)

/* WAIT_TIME 0x03 */
#define	WAIT_TIME_5MS(X)	(X)
/* ALS_GAIN 0x04*/
#define ALS_GAIN_1		0x00
#define ALS_GAIN_4		0x01
#define ALS_GAIN_8		0x02
#define ALS_GAIN_32		0x03
#define ALS_GAIN_96		0x04
#define ALS_GAIN_192	0x05
#define ALS_GAIN_368	0x06

/* ALS_TIME 0x05 */
#define ALS_GET_TIME	0x30
/* LED_CTRL */
#define IR_12_5MA	(0 << 6)
#define IR_100MA	(1 << 6)
#define IR_150MA	(2 << 6)
#define IR_200MA	(3 << 6)

/* PS_GAIN 0x07 */
#define PS_GAIN_1	(1 << 0)
#define	PS_GAIN_2	(1 << 1)
#define	PS_GAIN_4	(1 << 2)
#define	PS_GAIN_8	(1 << 4)


static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;
	int status = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	if (!enable) {
		status = ~ALS_ENABLE;
		sensor->ops->ctrl_data &= status;
	} else {
		status |= ALS_ENABLE;
		sensor->ops->ctrl_data |= status;
	}

	dev_dbg(&client->dev, "reg=0x%x, reg_ctrl=0x%x, enable=%d\n",
		sensor->ops->ctrl_reg, sensor->ops->ctrl_data, enable);

	result = sensor_write_reg(client, sensor->ops->ctrl_reg,
				  sensor->ops->ctrl_data);
	if (result)
		dev_err(&client->dev, "%s:fail to active sensor\n", __func__);

	return result;
}

static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	struct device_node *np = client->dev.of_node;
	int als_val = 0;
	int val = 0;
	int ret = 0;

	ret = sensor->ops->active(client, 0, 0);
	if (ret) {
		dev_err(&client->dev, "%s:sensor active fail\n", __func__);
		return ret;
	}
	sensor->status_cur = SENSOR_OFF;

	ret = of_property_read_u32(np, "als_threshold_low", &als_val);
	if (ret)
		dev_err(&client->dev, "%s:Unable to read als_threshold_low\n",
			__func__);
	ret = sensor_write_reg(client, ALS_THR_LH,
			       (unsigned char)(als_val >> 8));
	if (ret) {
		dev_err(&client->dev, "%s:write ALS_THR_LH fail\n", __func__);
		return ret;
	}

	ret = sensor_write_reg(client, ALS_THR_LL, (unsigned char)als_val);
	if (ret) {
		dev_err(&client->dev, "%s:write ALS_THR_LL fail\n", __func__);
		return ret;
	}

	ret = of_property_read_u32(np, "als_threshold_high", &als_val);
	if (ret)
		dev_err(&client->dev, "%s:Unable to read als_threshold_high\n",
			__func__);

	ret = sensor_write_reg(client, ALS_THR_HH,
			       (unsigned char)(als_val >> 8));
	if (ret) {
		dev_err(&client->dev, "%s:write ALS_THR_HH fail\n", __func__);
		return ret;
	}

	ret = sensor_write_reg(client, ALS_THR_HL, (unsigned char)als_val);
	if (ret) {
		dev_err(&client->dev, "%s:write ALS_THR_HL fail\n", __func__);
		return ret;
	}

	ret = of_property_read_u32(np, "als_ctrl_gain", &als_val);
	if (ret)
		dev_err(&client->dev, "%s:Unable to read als_ctrl_gain\n",
			__func__);

	ret = sensor_write_reg(client, ALS_GAIN, (unsigned char)als_val);
	if (ret) {
		dev_err(&client->dev, "%s:write ALS_GAIN fail\n", __func__);
		return ret;
	}


	ret = of_property_read_u32(np, "als_ctrl_time", &als_val);
	if (ret)
		dev_err(&client->dev, "%s:Unable to read als_ctrl_time\n",
			__func__);

	ret = sensor_write_reg(client, ALS_TIME, (unsigned char)als_val);
	if (ret) {
		dev_err(&client->dev, "%s:write ALS_TIME fail\n", __func__);
		return ret;
	}

	val = sensor_read_reg(client, INT_CTRL);
	if (sensor->pdata->irq_enable)
		val |= AINT_ENABLE;
	else
		val &= ~AINT_ENABLE;
	ret = sensor_write_reg(client, INT_CTRL, val);
	if (ret) {
		dev_err(&client->dev, "%s:write INT_CTRL fail\n", __func__);
		return ret;
	}

	return ret;
}

static int light_report_value(struct input_dev *input, int data)
{
	unsigned char index = 0;

	if (data <= 50) {
		index = 0;
		goto report;
	} else if (data <= 160) {
		index = 1;
		goto report;
	} else if (data <= 640) {
		index = 2;
		goto report;
	} else if (data <= 1280) {
		index = 3;
		goto report;
	} else if (data <= 2600) {
		index = 4;
		goto report;
	} else if (data <= 10240) {
		index = 5;
		goto report;
	} else if (data <= 20000) {
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
		(struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;
	int value, ch0_value = 0;
	int index = 0;
	char buffer[4] = { 0 };

	if (sensor->ops->read_len < 4) {
		dev_err(&client->dev, "%s:length is error, len=%d\n", __func__,
			sensor->ops->read_len);
		return -EINVAL;
	}

	buffer[0] = sensor->ops->read_reg;
	result = sensor_rx_data(client, buffer, sensor->ops->read_len);
	if (result) {
		dev_err(&client->dev, "%s:sensor read data fail\n", __func__);
		return result;
	}
	ch0_value = (buffer[1] << 8) | buffer[0];
	index = light_report_value(sensor->input_dev, ch0_value);
	dev_dbg(&client->dev, "%s result=0x%x, ch0_index=%d\n",
		sensor->ops->name, ch0_value, index);

	if (sensor->pdata->irq_enable && sensor->ops->int_status_reg) {
		value = sensor_read_reg(client, sensor->ops->int_status_reg);
		if (value & ALS_INT_FLAG) {
			value &= ~ALS_INT_FLAG;
			result = sensor_write_reg(client,
						  sensor->ops->int_status_reg,
						  value);
			if (result) {
				dev_err(&client->dev, "write status reg error\n");
				return result;
			}
		}
	}

	return result;
}

static struct sensor_operate light_ucs14620_ops = {
	.name			= "ls_ucs14620",
	.type			= SENSOR_TYPE_LIGHT,
	.id_i2c			= LIGHT_ID_UCS14620,
	.read_reg		= CH0_DATA_L,
	.read_len		= 4,
	.id_reg			= SENSOR_UNKNOW_DATA,
	.id_data		= SENSOR_UNKNOW_DATA,
	.precision		= 16,
	.ctrl_reg		= SYSM_CTRL,
	.int_status_reg		= INT_FLAG,
	.range			= { 100, 65535 },
	.brightness		= { 10, 255 },
	.trig			= IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED,
	.active			= sensor_active,
	.init			= sensor_init,
	.report			= sensor_report_value,
};

static int light_ucs14620_probe(struct i2c_client *client,
			       const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &light_ucs14620_ops);
}

static int light_ucs14620_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &light_ucs14620_ops);
}

static const struct i2c_device_id light_ucs14620_id[] = {
	{ "ls_ucs14620", LIGHT_ID_UCS14620 },
	{}
};

static struct i2c_driver light_ucs14620_driver = {
	.probe = light_ucs14620_probe,
	.remove = light_ucs14620_remove,
	.shutdown = sensor_shutdown,
	.id_table = light_ucs14620_id,
	.driver = {
		.name = "light_ucs14620",
#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
#endif
	},
};

module_i2c_driver(light_ucs14620_driver);

MODULE_AUTHOR("Kay Guo <kay.guo@rock-chips.com>");
MODULE_DESCRIPTION("ucs14620 light driver");
MODULE_LICENSE("GPL");

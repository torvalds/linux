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
#define ALS_GAIN_1	0x00
#define ALS_GAIN_4	0x01
#define ALS_GAIN_8	0x02
#define ALS_GAIN_32	0x03
#define ALS_GAIN_96	0x04
#define ALS_GAIN_192	0x05
#define ALS_GAIN_368	0x06

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

#define PS_PULSE_NUM(X)		(X)
#define LED_PULSE_WIDTH		(0x0f)


static int ps_threshold_low;
static int ps_threshold_high;

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;
	int status = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	if (!enable) {
		status = ~PS_ENABLE;
		sensor->ops->ctrl_data &= status;
	} else {
		status = PS_ENABLE;
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
	int ps_val = 0;
	int result = 0;
	int val = 0;

	result = sensor->ops->active(client, 0, 0);
	if (result) {
		dev_err(&client->dev, "%s:sensor active fail\n", __func__);
		return result;
	}
	sensor->status_cur = SENSOR_OFF;

	result = of_property_read_u32(np, "ps_threshold_low", &ps_val);
	if (result)
		dev_err(&client->dev, "%s:Unable to read ps_threshold_low\n",
			__func__);

	ps_threshold_low = ps_val;
	result = sensor_write_reg(client, PS_THR_LH,
				  (unsigned char)(ps_val >> 8));
	if (result) {
		dev_err(&client->dev, "%s:write PS_THR_LH fail\n", __func__);
		return result;
	}
	result = sensor_write_reg(client, PS_THR_LL, (unsigned char)ps_val);
	if (result) {
		dev_err(&client->dev, "%s:write PS_THR_LL fail\n", __func__);
		return result;
	}

	result = of_property_read_u32(np, "ps_threshold_high", &ps_val);
	if (result)
		dev_err(&client->dev, "%s:Unable to read ps_threshold_high\n",
			__func__);

	ps_threshold_high = ps_val;
	result = sensor_write_reg(client, PS_THR_HH,
				  (unsigned char)(ps_val >> 8));
	if (result) {
		dev_err(&client->dev, "%s:write PS_THR_HH fail\n", __func__);
		return result;
	}

	result = sensor_write_reg(client, PS_THR_HL, (unsigned char)ps_val);
	if (result) {
		dev_err(&client->dev, "%s:write PS_THR_HL fail\n", __func__);
		return result;
	}

	result = of_property_read_u32(np, "ps_ctrl_gain", &ps_val);
	if (result)
		dev_err(&client->dev, "%s:Unable to read ps_ctrl_gain\n",
			__func__);

	result = sensor_write_reg(client, PS_GAIN, (unsigned char)ps_val);
	if (result) {
		dev_err(&client->dev, "%s:write PS_GAIN fail\n", __func__);
		return result;
	}

	result = of_property_read_u32(np, "ps_led_current", &ps_val);
	if (result)
		dev_err(&client->dev, "%s:Unable to read ps_led_current\n",
			__func__);

	result |= sensor_write_reg(client, LED_CTRL,
				  (unsigned char)((ps_val << 6) | LED_PULSE_WIDTH));
	if (result) {
		dev_err(&client->dev, "%s:write LED_CTRL fail\n", __func__);
		return result;
	}

	val = sensor_read_reg(client, INT_CTRL);
	if (sensor->pdata->irq_enable) {
		val |= PINT_ENABLE;
		val |= PS_PEND_EN;
	} else {
		val &= PINT_DISABLE;
	}
	result = sensor_write_reg(client, INT_CTRL, val);
	if (result) {
		dev_err(&client->dev, "%s:write INT_CTRL fail\n", __func__);
		return result;
	}

	return result;
}

static int ucs14620_get_ps_value(int ps)
{
	int index = 0;
	static int value = 1;

	if (ps > ps_threshold_high) {
		index = 0;
		value = 0;
	} else if (ps < ps_threshold_low) {
		index = 1;
		value = 1;
	} else {
		index = value;
	}

	return index;
}

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;
	int value = 0;
	char buffer[2] = { 0 };
	int index = 1;

	if (sensor->ops->read_len < 2) {
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
	value = (buffer[1] << 8) | buffer[0];
	if (sensor->pdata->irq_enable && sensor->ops->int_status_reg) {
		value = sensor_read_reg(client, sensor->ops->int_status_reg);
		if (value & PS_INT_FLAG)
			index = 0;
		else
			index = 1;
		input_report_abs(sensor->input_dev, ABS_DISTANCE, index);
		input_sync(sensor->input_dev);
		value &= ~PS_INT_FLAG;
		result = sensor_write_reg(client,
					  sensor->ops->int_status_reg,
					  value);

		dev_dbg(&client->dev, "%s object near = %d", sensor->ops->name, index);

		if (result) {
			dev_err(&client->dev, "write status reg error\n");
			return result;
		}
	} else if (!sensor->pdata->irq_enable) {
		index = ucs14620_get_ps_value(value);
		input_report_abs(sensor->input_dev, ABS_DISTANCE, index);
		input_sync(sensor->input_dev);
		dev_dbg(&client->dev, "%s sensor closed=%d\n",
			sensor->ops->name, index);
	}

	return result;
}

static struct sensor_operate psensor_ucs14620_ops = {
	.name			= "ps_ucs14620",
	.type			= SENSOR_TYPE_PROXIMITY,
	.id_i2c			= PROXIMITY_ID_UCS14620,
	.read_reg		= PS_DATA_L,
	.read_len		= 2,
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

static int proximity_ucs14620_probe(struct i2c_client *client,
				   const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &psensor_ucs14620_ops);
}

static int proximity_ucs14620_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &psensor_ucs14620_ops);
}

static const struct i2c_device_id proximity_ucs14620_id[] = {
	{ "ps_ucs14620", PROXIMITY_ID_UCS14620 },
	{}
};

static struct i2c_driver proximity_ucs14620_driver = {
	.probe = proximity_ucs14620_probe,
	.remove = proximity_ucs14620_remove,
	.shutdown = sensor_shutdown,
	.id_table = proximity_ucs14620_id,
	.driver = {
		.name = "ps_ucs14620",
#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
#endif
	},
};

module_i2c_driver(proximity_ucs14620_driver);

MODULE_AUTHOR("Kay Guo<yangbin@rock-chips.com>");
MODULE_DESCRIPTION("ucs14620 proximity driver");
MODULE_LICENSE("GPL");

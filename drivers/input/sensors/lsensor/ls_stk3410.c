/*
 * drivers/input/sensors/lsensor/ls_stk3410.c
 *
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 *
 * Authors: Bin Yang <yangbin@rock-chips.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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

#define	STK_STATE	0x00
#define	PS_CTRL		0x01
#define	ALS_CTRL	0x02
#define	LED_CTRL	0x03
#define	INT_CTRL	0x04
#define	STK_WAIT	0x05
#define	THDH1_PS	0x06
#define	THDH2_PS	0x07
#define	THDL1_PS	0x08
#define	THDL2_PS	0x09
#define	THDH1_ALS	0x0A
#define	THDH2_ALS	0x0B
#define	THDL1_ALS	0x0C
#define	THDL2_ALS	0x0D
#define	STK_FLAG	0x10
#define	DATA1_PS	0x11
#define	DATA2_PS	0x12
#define	DATA1_ALS	0x13
#define	DATA2_ALS	0x14
#define	DATA1_OFFSET	0x15
#define	DATA2_OFFSET	0x16
#define	DATA1_IR	0x17
#define	DATA2_IR	0x18
#define	DATA1_GS0	0x24
#define	DATA2_GS0	0x25
#define	DATA1_GS1	0x26
#define	DATA2_GS1	0x27
#define	STKPDT_ID	0x3E
#define	SOFT_RESET	0x80

/* STK_STATE	0x00 */
#define	PS_DISABLE	(0 << 0)
#define	PS_ENABLE	(1 << 0)
#define	ALS_DISABLE	(0 << 1)
#define	ALS_ENABLE	(1 << 1)
#define	WAIT_DISABLE	(0 << 2)
#define	WAIT_ENABLE	(1 << 2)
#define	IRO_DISABLE	(0 << 4)
#define	IRO_ENABLE	(1 << 4)
#define	ASO_DISABLE	(0 << 5)
#define	ASO_ENABLE	(1 << 5)
#define	AK_DISABLE	(0 << 6)
#define	AK_ENABLE	(1 << 6)
#define	IRS_DISABLE	(0 << 7)
#define	IRS_ENABLE	(1 << 7)

/* PS/GS_CTRL 0x01 */
#define	PS_REFT_MS	(1 << 0)	/* [3:0] 0.185 ms ,  default value is 0.37ms */
#define	PS_GAIN_1G	(0 << 4)
#define	PS_GAIN_4G	(1 << 4)
#define	PS_GAIN_16G	(2 << 4)
#define	PS_GAIN_64G	(3 << 4)
#define	PS_PRS_1T	(0 << 6)
#define	PS_PRS_4T	(1 << 6)

/* ALS_CTRL 0x02 */
#define	ALS_REFT_MS	(9 << 0)/* [3:0] 0.185 ms,  default value is 94.85ms */
#define	ALS_GAIN_1G	(0 << 4)
#define	ALS_GAIN_4G	(1 << 4)
#define	ALS_GAIN_16G	(2 << 4)
#define	ALS_GAIN_64G	(3 << 4)
#define	ALS_PRS_1T	(0 << 6)
#define	ALS_PRS_4T	(1 << 6)

/* LED_CTRL 0x03 */
#define	LED_REFT_US	0x3F	/* [5:0] 2.89us ,  default value is 0.185ms */
#define	LED_CUR_12MA	(0 << 6)
#define	LED_CUR_25MA	(1 << 6)
#define	LED_CUR_50MA	(2 << 6)
#define	LED_CUR_100MA	(3 << 6)

/* INT 0x04 */
#define	PS_INT_DISABLE		(0 << 0)
#define	PS_INT_ENABLE		(1 << 0)
#define	PS_INT_ENABLE_FLGNFH	(2 << 0)
#define	PS_INT_ENABLE_FLGNFL	(3 << 0)
#define	PS_INT_MODE_ENABLE	(4 << 0)
#define	PS_INT_ENABLE_THL	(5 << 0)
#define	PS_INT_ENABLE_THH	(6 << 0)
#define	PS_INT_ENABLE_THHL	(7 << 0)
#define	ALS_INT_DISABLE		(0 << 3)
#define	ALS_INT_ENABLE		(1 << 3)
#define	INT_CTRL_PS_OR_LS	(0 << 7)
#define	INT_CTRL_PS_AND_LS	(1 << 7)

/* FLAG 0x10 */
#define	STK_FLAG_NF	(1 << 0)
#define	STK_FLAG_IR_RDY	(1 << 1)
#define	STK_FLAG_OUI	(1 << 2)
#define	STK_FLAG_PSINT	(1 << 4)
#define	STK_FLAG_ALSINT	(1 << 5)
#define	STK_FLAG_PSDR	(1 << 6)
#define	STK_FLAG_ALSDR	(1 << 7)

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
		status = ALS_ENABLE;
		sensor->ops->ctrl_data |= status;
	}

	DBG
		("%s:reg=0x%x, reg_ctrl=0x%x, enable=%d\n",
		__func__,
		sensor->ops->ctrl_reg,
		sensor->ops->ctrl_data,
		enable);

	result = sensor_write_reg
		(client, sensor->ops->ctrl_reg,
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
	int result = 0;
	int val = 0;
	int ret = 0;

	result = sensor->ops->active(client, 0, 0);
	if (result) {
		dev_err(&client->dev, "%s:sensor active fail\n", __func__);
		return result;
	}
	sensor->status_cur = SENSOR_OFF;

	ret = of_property_read_u32(np, "als_threshold_low", &als_val);
	if (ret)
		dev_err(&client->dev, "%s:Unable to read als_threshold_low\n",
			__func__);
	result = sensor_write_reg(client, THDL1_ALS, (unsigned char)als_val);
	if (result) {
		dev_err(&client->dev, "%s:write THDL1_ALS fail\n", __func__);
		return result;
	}
	result = sensor_write_reg
			(client, THDL2_ALS, (unsigned char)(als_val >> 8));
	if (result) {
		dev_err(&client->dev, "%s:write THDL1_ALS fail\n", __func__);
		return result;
	}

	ret = of_property_read_u32(np, "als_threshold_high", &als_val);
	if (ret)
		dev_err(&client->dev, "%s:Unable to read als_threshold_high\n",
			__func__);

	result = sensor_write_reg(client, THDH1_ALS, (unsigned char)als_val);
	if (result) {
		dev_err(&client->dev, "%s:write THDH1_ALS fail\n", __func__);
		return result;
	}
	result = sensor_write_reg
		(client, THDH2_ALS,
		(unsigned char)(als_val >> 8));
	if (result) {
		dev_err(&client->dev, "%s:write THDH1_ALS fail\n", __func__);
		return result;
	}

	ret = of_property_read_u32(np, "als_ctrl_gain", &als_val);
	if (ret)
		dev_err(&client->dev, "%s:Unable to read als_ctrl_gain\n",
			__func__);

	result = sensor_write_reg
		(client, ALS_CTRL,
		(unsigned char)((als_val << 4) | ALS_REFT_MS));
	if (result) {
		dev_err(&client->dev, "%s:write ALS_CTRL fail\n", __func__);
		return result;
	}

	val = sensor_read_reg(client, INT_CTRL);
	val &= ~INT_CTRL_PS_AND_LS;
	if (sensor->pdata->irq_enable)
		val |= ALS_INT_ENABLE;
	else
		val &= ~ALS_INT_ENABLE;
	result = sensor_write_reg(client, INT_CTRL, val);
	if (result) {
		dev_err(&client->dev, "%s:write INT_CTRL fail\n", __func__);
		return result;
	}

	return result;
}

static int light_report_value(struct input_dev *input, int data)
{
	unsigned char index = 0;

	if (data <= 100) {
		index = 0;
		goto report;
	} else if (data <= 1600) {
		index = 1;
		goto report;
	} else if (data <= 2250) {
		index = 2;
		goto report;
	} else if (data <= 3200) {
		index = 3;
		goto report;
	} else if (data <= 6400) {
		index = 4;
		goto report;
	} else if (data <= 12800) {
		index = 5;
		goto report;
	} else if (data <= 26000) {
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
	int value = 0;
	int index = 0;
	char buffer[2] = {0};

	if (sensor->ops->read_len < 2) {
		dev_err(&client->dev, "%s:length is error, len=%d\n", __func__,
			sensor->ops->read_len);
		return -1;
	}

	buffer[0] = sensor->ops->read_reg;
	result = sensor_rx_data(client, buffer, sensor->ops->read_len);
	if (result) {
		dev_err(&client->dev, "%s:sensor read data fail\n", __func__);
		return result;
	}
	value = (buffer[0] << 8) | buffer[1];
	index = light_report_value(sensor->input_dev, value);
	DBG
	("%s:%s result=0x%x, index=%d\n", __func__,
	sensor->ops->name, value, index);

	if (sensor->pdata->irq_enable && sensor->ops->int_status_reg) {
		value = sensor_read_reg(client, sensor->ops->int_status_reg);
		if (value & STK_FLAG_ALSINT) {
			value &= ~STK_FLAG_ALSINT;
			result = sensor_write_reg
				(client, sensor->ops->int_status_reg, value);
			if (result) {
				dev_err(&client->dev, "%s:write status reg error\n",
					__func__);
				return result;
			}
		}
	}

	return result;
}

struct sensor_operate light_stk3410_ops = {
	.name			= "ls_stk3410",
	.type			= SENSOR_TYPE_LIGHT,
	.id_i2c			= LIGHT_ID_STK3410,
	.read_reg		= DATA1_ALS,
	.read_len		= 2,
	.id_reg			= SENSOR_UNKNOW_DATA,
	.id_data		= SENSOR_UNKNOW_DATA,
	.precision		= 16,
	.ctrl_reg		= STK_STATE,
	.int_status_reg	= STK_FLAG,
	.range			= {100, 65535},
	.brightness		= {10, 255},
	.trig			= IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED,
	.active			= sensor_active,
	.init			= sensor_init,
	.report			= sensor_report_value,
};

static struct sensor_operate *light_get_ops(void)
{
	return &light_stk3410_ops;
}

static int __init light_stk3410_init(void)
{
	struct sensor_operate *ops = light_get_ops();
	int result = 0;
	int type = ops->type;

	result = sensor_register_slave(type, NULL, NULL, light_get_ops);

	return result;
}

static void __exit light_stk3410_exit(void)
{
	struct sensor_operate *ops = light_get_ops();
	int type = ops->type;

	sensor_unregister_slave(type, NULL, NULL, light_get_ops);
}

module_init(light_stk3410_init);
module_exit(light_stk3410_exit);

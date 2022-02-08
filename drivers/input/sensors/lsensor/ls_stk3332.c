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

#define	STK_STATE	0x00
#define	PS_CTRL		0x01
#define	ALS_CTRL1	0x02
#define	LED_CTRL	0x03
#define	INT_CTRL1	0x04
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
#define DATA1_C         0x1B
#define DATA2_C         0x1C
#define DATA1_PS_OFFSET 0x1D
#define DATA2_PS_OFFSET 0x1E
#define DATA_CTRL1      0x20
#define DATA_CTRL2      0x21
#define DATA_CTRL3      0x22
#define DATA_CTRL4      0x23
#define	STKPDT_ID	0x3E
#define STK_RESERVED    0x3F
#define ALS_CTRL2       0x4E
#define INTELLI_WAIT    0x4F
#define	SOFT_RESET	0x80
#define PSPD_CTRL       0xA1
#define INT_CTRL2       0xA5

/* STK_STATE	0x00 */
#define	PS_DISABLE	(0 << 0)
#define	PS_ENABLE	(1 << 0)
#define	ALS_DISABLE	(0 << 1)
#define	ALS_ENABLE	(1 << 1)
#define	WAIT_DISABLE	(0 << 2)
#define	WAIT_ENABLE	(1 << 2)
#define INTELLI_DISABLE (0 << 3)
#define INTELLI_ENABLE  (1 << 3)
#define	CTAUTOK_DISABLE	(0 << 4)
#define	CTAUTOK_ENABLE	(1 << 4)

/* PS/GS_CTRL 0x01 */
#define PS_IT_96US      (0 << 0)
#define PS_IT_192US     (1 << 0)
#define PS_IT_384US     (2 << 0)
#define PS_IT_768US     (3 << 0)
#define PS_IT_1MS54     (4 << 0)
#define PS_IT_3MS07     (5 << 0)
#define PS_IT_6MS14     (6 << 0)

#define	PS_GAIN_1G	(0 << 4)
#define	PS_GAIN_2G	(1 << 4)
#define	PS_GAIN_4G	(2 << 4)
#define	PS_GAIN_8G	(3 << 4)
#define	PS_PRST_1T	(0 << 6)
#define	PS_PRST_2T	(1 << 6)
#define	PS_PRST_4T	(2 << 6)
#define	PS_PRST_16T	(3 << 6)

/* ALS_CTRL1 0x02 */
#define	ALS_REFT_MS	(1 << 0)/* [3:0] 25 ms,  default value is 50ms */
#define	ALS_GAIN_1G	(0 << 4)
#define	ALS_GAIN_4G	(1 << 4)
#define	ALS_GAIN_16G	(2 << 4)
#define	ALS_GAIN_64G	(3 << 4)
#define	ALS_PRST_1T	(0 << 6)
#define	ALS_PRST_2T	(1 << 6)
#define	ALS_PRST_4T	(2 << 6)
#define	ALS_PRST_8T	(3 << 6)

/* LED_CTRL 0x03 */
#define	LED_REFT_US	0x03	/* [5:0] 2.89us ,  default value is 0.185ms */
#define CTIR_DISABLE    (0 << 0)
#define CTIR_ENABLE     (1 << 0)
#define CTIRFC_DISABLE  (0 << 1)
#define CTIRFC_ENABLE   (1 << 1)
#define	LED_CUR_12MA	(2 << 5)
#define	LED_CUR_25MA	(3 << 5)
#define	LED_CUR_50MA	(4 << 5)
#define	LED_CUR_100MA	(5 << 5)
#define	LED_CUR_150MA	(6 << 5)

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
#define	STK_FLAG_NF	        (1 << 0)
#define	STK_FLAG_INPS_INT	(1 << 1)
#define	STK_FLAG_ALS_STATE	(1 << 2)
#define	STK_FLAG_PS_INT		(1 << 4)
#define	STK_FLAG_ALS_INT	(1 << 5)
#define	STK_FLAG_PSDR           (1 << 6)
#define	STK_FLAG_ALSDR          (1 << 7)

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
	ret = sensor_write_reg(client, THDL1_ALS,
			       (unsigned char)(als_val >> 8));
	if (ret) {
		dev_err(&client->dev, "%s:write THDL1_ALS fail\n", __func__);
		return ret;
	}

	ret = sensor_write_reg(client, THDL2_ALS, (unsigned char)als_val);
	if (ret) {
		dev_err(&client->dev, "%s:write THDL1_ALS fail\n", __func__);
		return ret;
	}

	ret = of_property_read_u32(np, "als_threshold_high", &als_val);
	if (ret)
		dev_err(&client->dev, "%s:Unable to read als_threshold_high\n",
			__func__);

	ret = sensor_write_reg(client, THDH1_ALS,
			       (unsigned char)(als_val >> 8));
	if (ret) {
		dev_err(&client->dev, "%s:write THDH1_ALS fail\n", __func__);
		return ret;
	}

	ret = sensor_write_reg(client, THDH2_ALS, (unsigned char)als_val);
	if (ret) {
		dev_err(&client->dev, "%s:write THDH1_ALS fail\n", __func__);
		return ret;
	}

	ret = of_property_read_u32(np, "als_ctrl_gain", &als_val);
	if (ret)
		dev_err(&client->dev, "%s:Unable to read als_ctrl_gain\n",
			__func__);

	ret = sensor_write_reg(client, ALS_CTRL1,
			       (unsigned char)((als_val << 4) | ALS_REFT_MS));
	if (ret) {
		dev_err(&client->dev, "%s:write ALS_CTRL fail\n", __func__);
		return ret;
	}

	val = sensor_read_reg(client, INT_CTRL1);
	val &= ~INT_CTRL_PS_AND_LS;
	if (sensor->pdata->irq_enable)
		val |= ALS_INT_ENABLE;
	else
		val &= ~ALS_INT_ENABLE;
	ret = sensor_write_reg(client, INT_CTRL1, val);
	if (ret) {
		dev_err(&client->dev, "%s:write INT_CTRL1 fail\n", __func__);
		return ret;
	}

	return ret;
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
	char buffer[2] = { 0 };

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
	value = (buffer[0] << 8) | buffer[1];
	index = light_report_value(sensor->input_dev, value);
	dev_dbg(&client->dev, "%s result=0x%x, index=%d\n",
		sensor->ops->name, value, index);

	if (sensor->pdata->irq_enable && sensor->ops->int_status_reg) {
		value = sensor_read_reg(client, sensor->ops->int_status_reg);
		if (value & STK_FLAG_ALS_INT) {
			value &= ~STK_FLAG_ALS_INT;
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

static struct sensor_operate light_stk3332_ops = {
	.name			= "ls_stk3332",
	.type			= SENSOR_TYPE_LIGHT,
	.id_i2c			= LIGHT_ID_STK3332,
	.read_reg		= DATA1_ALS,
	.read_len		= 2,
	.id_reg			= SENSOR_UNKNOW_DATA,
	.id_data		= SENSOR_UNKNOW_DATA,
	.precision		= 16,
	.ctrl_reg		= STK_STATE,
	.int_status_reg         = STK_FLAG,
	.range			= { 100, 65535 },
	.brightness		= { 10, 255 },
	.trig			= IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED,
	.active			= sensor_active,
	.init			= sensor_init,
	.report			= sensor_report_value,
};

static int light_stk3332_probe(struct i2c_client *client,
			       const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &light_stk3332_ops);
}

static int light_stk3332_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &light_stk3332_ops);
}

static const struct i2c_device_id light_stk3332_id[] = {
	{ "ls_stk3332", LIGHT_ID_STK3332 },
	{}
};

static struct i2c_driver light_stk3332_driver = {
	.probe = light_stk3332_probe,
	.remove = light_stk3332_remove,
	.shutdown = sensor_shutdown,
	.id_table = light_stk3332_id,
	.driver = {
		.name = "light_stk3332",
#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
#endif
	},
};

module_i2c_driver(light_stk3332_driver);

MODULE_AUTHOR("Kay Guo <kay.guo@rock-chips.com>");
MODULE_DESCRIPTION("stk3332 light driver");
MODULE_LICENSE("GPL");

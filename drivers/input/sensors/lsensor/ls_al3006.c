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


#define CONFIG_REG        (0x00)
#define TIM_CTL_REG       (0x01)
#define ALS_CTL_REG       (0x02)
#define INT_STATUS_REG    (0x03)
#define PS_CTL_REG        (0x04)
#define PS_ALS_DATA_REG   (0x05)
#define ALS_WINDOWS_REG   (0x08)

//enable bit[ 0-1], in register CONFIG_REG
#define ONLY_ALS_EN       (0x00)
#define ONLY_PROX_EN      (0x01)
#define ALL_PROX_ALS_EN   (0x02)
#define ALL_IDLE          (0x03)

#define POWER_MODE_MASK   (0x0C)
#define POWER_UP_MODE     (0x00)
#define POWER_DOWN_MODE   (0x08)
#define POWER_RESET_MODE  (0x0C)

static int sensor_power_updown(struct i2c_client *client, int on)
{
	int result = 0;
	char value = 0;
	int i = 0;
	for(i=0; i<3; i++)
	{
		if(!on)
		{
			value = sensor_read_reg(client, CONFIG_REG);
			value &= ~POWER_MODE_MASK;
			value |= POWER_DOWN_MODE;
			result = sensor_write_reg(client, CONFIG_REG, value);
			if(result)
				return result;
		}
		else
		{
			value = sensor_read_reg(client, CONFIG_REG);
			value &= ~POWER_MODE_MASK;
			value |= POWER_UP_MODE;
			result = sensor_write_reg(client, CONFIG_REG, value);
			if(result)
				return result;
		}

		if(!result)
		break;
	}

	if(i>1)
	printk("%s:set %d times",__func__,i);

	return result;
}

/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	char value = 0;

	if(enable)
	sensor_power_updown(client, 1);

	value = sensor_read_reg(client, sensor->ops->ctrl_reg);

	//register setting according to chip datasheet
	if(enable)
	{
		if( (value & 0x03) == ONLY_PROX_EN )
		{
			value &= ~0x03;
			value |= ALL_PROX_ALS_EN;
		}
		else if((value & 0x03) == ALL_IDLE )
		{
			value &= ~0x03;
			value |= ONLY_ALS_EN;
		}

	}
	else
	{
		if( (value & 0x03) == ONLY_ALS_EN )
		{
			value &= ~0x03;
			value |= ALL_IDLE;
		}
		else if((value & 0x03) == ALL_PROX_ALS_EN )
		{
			value &= ~0x03;
			value |= ONLY_PROX_EN;
		}

	}

	sensor->ops->ctrl_data = value;

	DBG("%s:reg=0x%x,reg_ctrl=0x%x,enable=%d\n",__func__,sensor->ops->ctrl_reg, sensor->ops->ctrl_data, enable);
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if(result)
		printk("%s:fail to active sensor\n",__func__);

	return result;

}


static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	char value = 0;

	sensor_power_updown(client, 0);

	result = sensor->ops->active(client,0,0);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	sensor->status_cur = SENSOR_OFF;

	value = 0x41;//The ADC effective resolution = 9;  Low lux threshold level = 1;
	//value = 0x69; //The ADC effective resolution = 17;  Low lux threshold level = 9;
	result = sensor_write_reg(client, ALS_CTL_REG, value);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	//value = 0x04;//0x01-0x0f; 17%->93.5% if value = 0x04,then Compensate Loss 52%
	value = 0x02;//0x01-0x0f; 17%->93.5% if value = 0x02,then Compensate Loss 31%
	result = sensor_write_reg(client, ALS_WINDOWS_REG, value);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	return result;
}


static int light_report_value(struct input_dev *input, int data)
{
	unsigned char index = 0;
	if(data <= 0){
		index = 0;goto report;
	}
	else if(data <= 2){
		index = 1;goto report;
	}
	else if(data <= 4){
		index = 2;goto report;
	}
	else if(data <= 8){
		index = 3;goto report;
	}
	else if(data <= 14){
		index = 4;goto report;
	}
	else if(data <= 20){
		index = 5;goto report;
	}
	else if(data <= 26){
		index = 6;goto report;
	}
	else{
		index = 7;goto report;
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
	char value = 0;
	char index = 0;

	if(sensor->pdata->irq_enable)
	{
		if(sensor->ops->int_status_reg)
		{
			value = sensor_read_reg(client, sensor->ops->int_status_reg);
		}

	}

	value = sensor_read_reg(client, sensor->ops->read_reg);
	index = light_report_value(sensor->input_dev, value&0x3f); // bit0-5  is ls data;

	DBG("%s:%s result=0x%x,index=%d\n",__func__,sensor->ops->name, value,index);

	return result;
}

static struct sensor_operate light_al3006_ops = {
	.name				= "ls_al3006",
	.type				= SENSOR_TYPE_LIGHT,	//sensor type and it should be correct
	.id_i2c				= LIGHT_ID_AL3006,	//i2c id number
	.read_reg			= PS_ALS_DATA_REG,	//read data
	.read_len			= 1,			//data length
	.id_reg				= SENSOR_UNKNOW_DATA,	//read device id from this register
	.id_data 			= SENSOR_UNKNOW_DATA,	//device id
	.precision			= 8,			//8 bits
	.ctrl_reg 			= CONFIG_REG,		//enable or disable
	.int_status_reg 		= INT_STATUS_REG,	//intterupt status register
	.range				= {100,65535},		//range
	.brightness                                        ={10,255},                          // brightness
	.trig				= IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED,
	.active				= sensor_active,
	.init				= sensor_init,
	.report				= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int light_al3006_probe(struct i2c_client *client,
			      const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &light_al3006_ops);
}

static int light_al3006_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &light_al3006_ops);
}

static const struct i2c_device_id light_al3006_id[] = {
	{"light_al3006", LIGHT_ID_AL3006},
	{}
};

static struct i2c_driver light_al3006_driver = {
	.probe = light_al3006_probe,
	.remove = light_al3006_remove,
	.shutdown = sensor_shutdown,
	.id_table = light_al3006_id,
	.driver = {
		.name = "light_al3006",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(light_al3006_driver);

MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_DESCRIPTION("al3006 light driver");
MODULE_LICENSE("GPL");


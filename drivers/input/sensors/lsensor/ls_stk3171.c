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


#define ALS_CMD 	0x01
#define ALS_DT1		0x02
#define ALS_DT2		0X03
#define ALS_THDH1	0X04
#define ALS_THDH2	0X05
#define ALS_THDL1	0X06
#define ALS_THDL2	0X07
#define STA_TUS		0X08
#define PS_CMD		0X09
#define PS_DT		0X0A
#define PS_THDH		0X0B
#define PS_THDL		0X0C
#define SW_RESET	0X80

//ALS_CMD
#define ALS_SD_ENABLE	(0<<0)
#define ALS_SD_DISABLE	(1<<0)
#define ALS_INT_DISABLE	(0<<1)
#define ALS_INT_ENABLE	(1<<1)
#define ALS_1T_100MS	(0<<2)
#define ALS_2T_200MS	(1<<2)
#define ALS_4T_400MS	(2<<2)
#define ALS_8T_800MS	(3<<2)
#define ALS_RANGE_57671	(0<<6)
#define ALS_RANGE_28836	(1<<6)

//PS_CMD
#define PS_SD_ENABLE	(0<<0)
#define PS_SD_DISABLE	(1<<0)
#define PS_INT_DISABLE	(0<<1)
#define PS_INT_ENABLE	(1<<1)
#define PS_10T_2MS	(0<<2)
#define PS_15T_3MS	(1<<2)
#define PS_20T_4MS	(2<<2)
#define PS_25T_5MS	(3<<2)
#define PS_CUR_100MA	(0<<4)
#define PS_CUR_200MA	(1<<4)
#define PS_SLP_10MS	(0<<5)
#define PS_SLP_30MS	(1<<5)
#define PS_SLP_90MS	(2<<5)
#define PS_SLP_270MS	(3<<5)
#define TRIG_PS_OR_LS	(0<<7)
#define TRIG_PS_AND_LS	(1<<7)

//STA_TUS
#define STA_PS_INT	(1<<5)
#define	STA_ALS_INT	(1<<4)



/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int status = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);

	//register setting according to chip datasheet
	if(!enable)
	{
		status = ALS_SD_DISABLE;
		sensor->ops->ctrl_data |= status;
	}
	else
	{
		status = ~ALS_SD_DISABLE;
		sensor->ops->ctrl_data &= status;
	}

	DBG("%s:reg=0x%x,reg_ctrl=0x%x,enable=%d\n",__func__,sensor->ops->ctrl_reg, sensor->ops->ctrl_data, enable);
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if(result)
		printk("%s:fail to active sensor\n",__func__);

	if(enable)
	sensor->ops->report(sensor->client);

	return result;

}


static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;

	result = sensor->ops->active(client,0,0);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	sensor->status_cur = SENSOR_OFF;

	result = sensor_write_reg(client, SW_RESET, 0);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	result = sensor_write_reg(client, ALS_THDH1, 0);//it is important,if not then als can not trig intterupt
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	result = sensor_write_reg(client, ALS_THDH2, 0);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	sensor->ops->ctrl_data |= ALS_1T_100MS;

	if(sensor->pdata->irq_enable)
		sensor->ops->ctrl_data |= ALS_INT_ENABLE;
	else
		sensor->ops->ctrl_data &= ~ALS_INT_ENABLE;

	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
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
	if(data <= 100){
		index = 0;goto report;
	}
	else if(data <= 1600){
		index = 1;goto report;
	}
	else if(data <= 2250){
		index = 2;goto report;
	}
	else if(data <= 3200){
		index = 3;goto report;
	}
	else if(data <= 6400){
		index = 4;goto report;
	}
	else if(data <= 12800){
		index = 5;goto report;
	}
	else if(data <= 26000){
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
	int value = 0;
	char buffer[2] = {0};
	char index = 0;

	if(sensor->ops->read_len < 2)	//sensor->ops->read_len = 2
	{
		printk("%s:lenth is error,len=%d\n",__func__,sensor->ops->read_len);
		return -1;
	}

	memset(buffer, 0, 2);

	buffer[0] = sensor->ops->read_reg;
	result = sensor_rx_data(client, buffer, sensor->ops->read_len);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	value = (buffer[0] << 8) | buffer[1];


	index = light_report_value(sensor->input_dev, value);

	DBG("%s:%s result=0x%x,index=%d\n",__func__,sensor->ops->name, value,index);

	if(sensor->pdata->irq_enable)
	{
		if(sensor->ops->int_status_reg)
		{
			value = sensor_read_reg(client, sensor->ops->int_status_reg);
		}

		if(value & STA_ALS_INT)
		{
			value &= ~STA_ALS_INT;
			result = sensor_write_reg(client, sensor->ops->int_status_reg,value);	//clear int
			if(result)
			{
				printk("%s:line=%d,error\n",__func__,__LINE__);
				return result;
			}
		}
	}


	return result;
}

static struct sensor_operate light_stk3171_ops = {
	.name				= "ls_stk3171",
	.type				= SENSOR_TYPE_LIGHT,	//sensor type and it should be correct
	.id_i2c				= LIGHT_ID_STK3171,	//i2c id number
	.read_reg			= ALS_DT1,		//read data
	.read_len			= 2,			//data length
	.id_reg				= SENSOR_UNKNOW_DATA,	//read device id from this register
	.id_data 			= SENSOR_UNKNOW_DATA,	//device id
	.precision			= 16,			//8 bits
	.ctrl_reg 			= ALS_CMD,		//enable or disable
	.int_status_reg 		= STA_TUS,		//intterupt status register
	.range				= {100,65535},		//range
	.brightness                                        ={10,255},     //brightness
	.trig				= IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED,
	.active				= sensor_active,
	.init				= sensor_init,
	.report				= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int light_stk3171_probe(struct i2c_client *client,
			       const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &light_stk3171_ops);
}

static int light_stk3171_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &light_stk3171_ops);
}

static const struct i2c_device_id light_stk3171_id[] = {
	{"ls_stk3171", LIGHT_ID_STK3171},
	{}
};

static struct i2c_driver light_stk3171_driver = {
	.probe = light_stk3171_probe,
	.remove = light_stk3171_remove,
	.shutdown = sensor_shutdown,
	.id_table = light_stk3171_id,
	.driver = {
		.name = "light_stk3171",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(light_stk3171_driver);

MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_DESCRIPTION("stk3171 light driver");
MODULE_LICENSE("GPL");



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
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <mach/gpio.h>
#include <mach/board.h> 
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/sensor-dev.h>


#define CM3217_ADDR_COM1	0x10
#define CM3217_ADDR_COM2	0x11
#define CM3217_ADDR_DATA_MSB	0x10
#define CM3217_ADDR_DATA_LSB	0x11

#define CM3217_COM1_VALUE	0xA7	// (GAIN1:GAIN0)=10, (IT_T1:IT_TO)=01,WMD=1,SD=1,
#define CM3217_COM2_VALUE	0xA0	//100ms

#define CM3217_CLOSE	0x01



/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	int result = 0;
	int status = 0;
	
	sensor->client->addr = sensor->ops->ctrl_reg;	
	sensor->ops->ctrl_data = sensor_read_reg_normal(client);
	
	//register setting according to chip datasheet		
	if(!enable)
	{	
		status = CM3217_CLOSE;	//cm3217	
		sensor->ops->ctrl_data |= status;	
	}
	else
	{
		status = ~CM3217_CLOSE;	//cm3217
		sensor->ops->ctrl_data &= status;
	}

	DBG("%s:reg=0x%x,reg_ctrl=0x%x,enable=%d\n",__func__,sensor->ops->ctrl_reg, sensor->ops->ctrl_data, enable);
	result = sensor_write_reg_normal(client, sensor->ops->ctrl_data);
	if(result)
		printk("%s:fail to active sensor\n",__func__);
	
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
	
	sensor->client->addr = sensor->ops->ctrl_reg;		
	sensor->ops->ctrl_data = CM3217_COM1_VALUE;	
	result = sensor_write_reg_normal(client, sensor->ops->ctrl_data);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}
	
	sensor->client->addr = CM3217_ADDR_COM2;	
	result = sensor_write_reg_normal(client, CM3217_COM2_VALUE);
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
	
	if(data <= 10){
		index = 0;goto report;
	}
	else if(data <= 160){
		index = 1;goto report;
	}
	else if(data <= 225){
		index = 2;goto report;
	}
	else if(data <= 320){
		index = 3;goto report;
	}
	else if(data <= 640){
		index = 4;goto report;
	}
	else if(data <= 1280){
		index = 5;goto report;
	}
	else if(data <= 2600){
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
	char msb = 0, lsb = 0;
	int index = 0;
	
	sensor->client->addr = CM3217_ADDR_DATA_LSB;
	sensor_rx_data_normal(sensor->client, &lsb, 1);
	sensor->client->addr = CM3217_ADDR_DATA_MSB;
	sensor_rx_data_normal(sensor->client, &msb, 1);
	result = ((msb << 8) | lsb) & 0xffff;
	
	index = light_report_value(sensor->input_dev, result);
	DBG("%s:%s result=0x%x,index=%d\n",__func__,sensor->ops->name, result,index);
	
	if((sensor->pdata->irq_enable)&& (sensor->ops->int_status_reg >= 0))	//read sensor intterupt status register
	{
		
		result= sensor_read_reg(client, sensor->ops->int_status_reg);
		if(result)
		{
			printk("%s:fail to clear sensor int status,ret=0x%x\n",__func__,result);
		}
	}
	
	return result;
}


struct sensor_operate light_cm3217_ops = {
	.name				= "cm3217",
	.type				= SENSOR_TYPE_LIGHT,	//sensor type and it should be correct
	.id_i2c				= LIGHT_ID_CM3217,	//i2c id number
	.read_reg			= CM3217_ADDR_DATA_LSB,	//read data
	.read_len			= 2,			//data length
	.id_reg				= SENSOR_UNKNOW_DATA,	//read device id from this register
	.id_data 			= SENSOR_UNKNOW_DATA,	//device id
	.precision			= 8,			//8 bits
	.ctrl_reg 			= CM3217_ADDR_COM1,	//enable or disable 
	.int_status_reg 		= SENSOR_UNKNOW_DATA,	//intterupt status register
	.range				= {100,65535},		//range
	.brightness                                        ={10,255},                          // brightness
	.trig				= SENSOR_UNKNOW_DATA,		
	.active				= sensor_active,	
	.init				= sensor_init,
	.report				= sensor_report_value,
};

/****************operate according to sensor chip:end************/

//function name should not be changed
static struct sensor_operate *light_get_ops(void)
{
	return &light_cm3217_ops;
}


static int __init light_cm3217_init(void)
{
	struct sensor_operate *ops = light_get_ops();
	int result = 0;
	int type = ops->type;
	result = sensor_register_slave(type, NULL, NULL, light_get_ops);
	return result;
}

static void __exit light_cm3217_exit(void)
{
	struct sensor_operate *ops = light_get_ops();
	int type = ops->type;
	sensor_unregister_slave(type, NULL, NULL, light_get_ops);
}


module_init(light_cm3217_init);
module_exit(light_cm3217_exit);



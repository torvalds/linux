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


#define CM3232_CLOSE	0x01


#define CM3232_ADDR_COM 0
#define CM3232_ADDR_DATA 50

#define CM3232_DRV_NAME "cm3232"
//command code
#define COMMAND_CTRL 		0
#define COMMAND_ALS_DATA 	50 		//ALS: 15:8 MSB 8bits data
						//7:0 LSB 8bits data

//ctrl bit
#define ALS_RESET(x)			(((x)&1)<<6) //0 = Reset disable; 1 = Reset enable
#define ALS_IT(x)   			(((x)&7)<<2) //ALS integration time setting
#define HIGH_SENSITIVITY(x)		(((x)&1)<<1) //0 = Normal mode; 1 = High sensitivity mode
#define SHUT_DOWN(x) 			(((x)&1)<<0) //ALS shut down setting: 0 = ALS Power on ; 1 = ALS Shut down

#define ALS_IT100MS 	0  		//100ms
#define ALS_IT200MS 	1  		//200ms
#define ALS_IT400MS 	2  		//400ms
#define ALS_IT800MS 	3  		//800ms
#define ALS_IT1600MS 	4  		//1600ms
#define ALS_IT3200MS 	5  		//3200ms


/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	//int status = 0;

	//sensor->client->addr = sensor->ops->ctrl_reg;
	//sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	//printk("%s:  client addr = %#x\n\n", __func__, client->addr);
	//register setting according to chip datasheet
	if (enable) {
		sensor->ops->ctrl_data = ALS_RESET(1);
		result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
		if(result) {
			printk("%s:fail to active sensor\n",__func__);
			return -1;
		}
	}

	if(enable)
	{
		sensor->ops->ctrl_data = ALS_IT(ALS_IT200MS) | HIGH_SENSITIVITY(1);
	}
	else
	{
		sensor->ops->ctrl_data = SHUT_DOWN(1);
	}

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

	result = sensor->ops->active(client,0,0);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	sensor->status_cur = SENSOR_OFF;
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
	//char msb = 0, lsb = 0;
	char data[2] = {0};
	unsigned short value = 0;
	int index = 0;

	//sensor->client->addr = CM3232_ADDR_DATA;
	data[0] = CM3232_ADDR_DATA;
	sensor_rx_data(sensor->client, data, 2);
	value = (data[1] << 8) | data[0] ;

	DBG("%s:result=%d\n",__func__,value);
	//printk("%s:result=%d\n",__func__,value);
	index = light_report_value(sensor->input_dev, value);
	DBG("%s:%s result=0x%x,index=%d\n",__func__,sensor->ops->name, value,index);

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


struct sensor_operate light_cm3232_ops = {
	.name				= "cm3232",
	.type				= SENSOR_TYPE_LIGHT,	//sensor type and it should be correct
	.id_i2c				= LIGHT_ID_CM3232,	//i2c id number
	.read_reg			= CM3232_ADDR_DATA,	//read data
	.read_len			= 2,			//data length
	.id_reg				= SENSOR_UNKNOW_DATA,	//read device id from this register
	.id_data 			= SENSOR_UNKNOW_DATA,	//device id
	.precision			= 8,			//8 bits
	.ctrl_reg 			= CM3232_ADDR_COM,	//enable or disable
	.int_status_reg 		= SENSOR_UNKNOW_DATA,	//intterupt status register
	.range				= {100,65535},		//range
	.brightness         		= {10,255},             // brightness
	.trig				= SENSOR_UNKNOW_DATA,
	.active				= sensor_active,
	.init				= sensor_init,
	.report				= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int light_cm3232_probe(struct i2c_client *client,
			      const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &light_cm3232_ops);
}

static int light_cm3232_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &light_cm3232_ops);
}

static const struct i2c_device_id light_cm3232_id[] = {
	{"light_cm3232", LIGHT_ID_CM3232},
	{}
};

static struct i2c_driver light_cm3232_driver = {
	.probe = light_cm3232_probe,
	.remove = light_cm3232_remove,
	.shutdown = sensor_shutdown,
	.id_table = light_cm3232_id,
	.driver = {
		.name = "light_cm3232",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(light_cm3232_driver);

MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_DESCRIPTION("cm3232 light driver");
MODULE_LICENSE("GPL");

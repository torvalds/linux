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
#include <linux/rk_keys.h>
#include <linux/sensor-dev.h>


#define CM3232_CLOSE	0x01


#define CM3232_ADDR_COM 0
#define CM3232_ADDR_DATA 50	

#define CM3232_DRV_NAME "cm3232"
//command code
#define COMMAND_CTRL 		0
#define COMMAND_ALS_DATA 	50 		//ALS: 15:8 MSB 8bits data
						//7:0 LSB 8bits data

/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	if(enable)
	{
		sensor->status_cur = SENSOR_ON;
	}
	else
	{
		sensor->status_cur = SENSOR_OFF;
	}
	return 0;
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


static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	struct sensor_platform_data *pdata = sensor->pdata;
	int gpio_value = 0;
	gpio_value = gpio_get_value(pdata->irq_pin);
	if(gpio_value == 0)
	{		
		//send power key to sleep
		rk_send_power_key(1);
		rk_send_power_key(0);
	}
	else
	{
		//rk_send_power_key(1);
		//rk_send_power_key(0);
		rk_send_wakeup_key(); // wake up the system
	}
	return 0;
}


static struct sensor_operate hall_och165t_ops = {
	.name				= "och165t",
	.type				= SENSOR_TYPE_HALL,	//sensor type and it should be correct
	.id_i2c				= HALL_ID_OCH165T,	//i2c id number
	.read_reg			= SENSOR_UNKNOW_DATA,	//read data
	.read_len			= 2,			//data length
	.id_reg				= SENSOR_UNKNOW_DATA,	//read device id from this register
	.id_data 			= SENSOR_UNKNOW_DATA,	//device id
	.precision			= 8,			//8 bits
	.ctrl_reg 			= SENSOR_UNKNOW_DATA,	//enable or disable 
	.int_status_reg 		= SENSOR_UNKNOW_DATA,	//intterupt status register
	.range				= {100,65535},		//range
	.brightness         		= {10,255},             // brightness
	.trig				= SENSOR_UNKNOW_DATA,		
	.active				= sensor_active,	
	.init				= sensor_init,
	.report				= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int hall_och165t_probe(struct i2c_client *client,
			      const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &hall_och165t_ops);
}

static int hall_och165t_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &hall_och165t_ops);
}

static const struct i2c_device_id hall_och165t_id[] = {
	{"hall_och165t", HALL_ID_OCH165T},
	{}
};

static struct i2c_driver hall_och165t_driver = {
	.probe = hall_och165t_probe,
	.remove = hall_och165t_remove,
	.shutdown = sensor_shutdown,
	.id_table = hall_och165t_id,
	.driver = {
		.name = "hall_och165t",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(hall_och165t_driver);

MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_DESCRIPTION("och165t hall driver");
MODULE_LICENSE("GPL");

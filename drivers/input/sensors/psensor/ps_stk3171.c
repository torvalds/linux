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

#if 0
#define SENSOR_DEBUG_TYPE SENSOR_TYPE_PROXIMITY
#define DBG(x...) if(sensor->pdata->type == SENSOR_DEBUG_TYPE) printk(x)
#else
#define DBG(x...)
#endif

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
#define ALS_RANGE_57671	(0<<5)
#define ALS_RANGE_28836	(1<<5)

//PS_CMD
#define PS_SD_ENABLE	(0<<0)
#define PS_SD_DISABLE	(1<<0)
#define PS_INT_DISABLE	(0<<1)
#define PS_INT_ENABLE	(1<<1)
#define PS_10T_2MS	(0<<2)
#define PS_15T_3MS	(1<<2)
#define PS_20T_4MS	(2<<2)
#define PS_25T_5MS	(3<<2)
#define PS_CUR_100MA	(0<<3)
#define PS_CUR_200MA	(1<<3)
#define PS_SLP_10MS	(0<<4)
#define PS_SLP_30MS	(1<<4)
#define PS_SLP_90MS	(2<<4)
#define PS_SLP_270MS	(3<<4)
#define TRIG_PS_OR_LS	(0<<5)
#define TRIG_PS_AND_LS	(1<<5)

//STA_TUS
#define STA_PS_INT	(1<<4)
#define	STA_ALS_INT	(1<<3)


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
		status = PS_SD_DISABLE;	
		sensor->ops->ctrl_data |= status;	
	}
	else
	{
		status = ~PS_SD_DISABLE;
		sensor->ops->ctrl_data &= status;
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

	result = sensor_write_reg(client, SW_RESET, 0);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}


	sensor->ops->ctrl_data |= (PS_15T_3MS| PS_SLP_90MS | TRIG_PS_OR_LS);

	if(sensor->pdata->irq_enable)
		sensor->ops->ctrl_data |= PS_INT_ENABLE;
	else
		sensor->ops->ctrl_data &= ~PS_INT_ENABLE;
	
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}
		
	return result;
}



static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	int result = 0;
	int value = 0;
	char buffer[1] = {0};	
	
	if(sensor->ops->read_len < 1)	//sensor->ops->read_len = 1
	{
		printk("%s:lenth is error,len=%d\n",__func__,sensor->ops->read_len);
		return -1;
	}
	
	memset(buffer, 0, 1);

	result = sensor_rx_data(client, buffer, sensor->ops->read_len);
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	value = buffer[0];
	
	input_report_abs(sensor->input_dev, ABS_DISTANCE, value?0:1);
	input_sync(sensor->input_dev);
	DBG("%s:%s result=0x%x,index=%d\n",__func__,sensor->ops->name, value,value?0:1);		
	
	if(sensor->pdata->irq_enable)
	{
		if(sensor->ops->int_status_reg)
		{	
			value = sensor_read_reg(client, sensor->ops->int_status_reg);
		}
		
		if(value & STA_PS_INT)
		{
			value &= ~STA_PS_INT;
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

struct sensor_operate proximity_ops = {
	.name				= "ps_stk3171",
	.type				= SENSOR_TYPE_PROXIMITY,	//sensor type and it should be correct
	.id_i2c				= PROXIMITY_ID_STK3171,		//i2c id number
	.read_reg			= PS_DT,			//read data
	.read_len			= 1,				//data length
	.id_reg				= SENSOR_UNKNOW_DATA,		//read device id from this register
	.id_data 			= SENSOR_UNKNOW_DATA,		//device id
	.precision			= 8,				//8 bits
	.ctrl_reg 			= PS_CMD,			//enable or disable 
	.int_status_reg 		= STA_TUS,			//intterupt status register
	.range				= {0,1},			//range
	.trig				= IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED,		
	.active				= sensor_active,	
	.init				= sensor_init,
	.report				= sensor_report_value,
};

/****************operate according to sensor chip:end************/

//function name should not be changed
struct sensor_operate *proximity_get_ops(void)
{
	return &proximity_ops;
}

EXPORT_SYMBOL(proximity_get_ops);

static int __init proximity_init(void)
{
	struct sensor_operate *ops = proximity_get_ops();
	int result = 0;
	int type = ops->type;
	result = sensor_register_slave(type, NULL, NULL, proximity_get_ops);
	printk("%s\n",__func__);
	return result;
}

static void __exit proximity_exit(void)
{
	struct sensor_operate *ops = proximity_get_ops();
	int type = ops->type;
	sensor_unregister_slave(type, NULL, NULL, proximity_get_ops);
}


module_init(proximity_init);
module_exit(proximity_exit);



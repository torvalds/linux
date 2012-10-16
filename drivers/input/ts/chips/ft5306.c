/* drivers/input/ts/chips/ts_ft5306.c
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
#include <linux/input/mt.h>
#include <mach/gpio.h>
#include <mach/board.h> 
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif	 
#include <linux/ts-auto.h>
	 
	 
#if 0
#define DBG(x...)  printk(x)
#else
#define DBG(x...)
#endif


#define FT5306_ID_REG		0x00
#define FT5306_DEVID		0x00
#define FT5306_DATA_REG		0x00


/****************operate according to ts chip:start************/

static int ts_active(struct ts_private_data *ts, int enable)
{	
	int result = 0;

	if(enable)
	{
		gpio_direction_output(ts->pdata->reset_pin, GPIO_LOW);
		msleep(10);
		gpio_direction_output(ts->pdata->reset_pin, GPIO_HIGH);
		msleep(100);
	}
	else
	{
		gpio_direction_output(ts->pdata->reset_pin, GPIO_LOW);	
	}
		
	
	return result;
}

static int ts_init(struct ts_private_data *ts)
{
	int irq_pin = irq_to_gpio(ts->pdata->irq);
	int result = 0;
	
	gpio_direction_output(ts->pdata->reset_pin, GPIO_LOW);
	mdelay(10);
	gpio_direction_output(ts->pdata->reset_pin, GPIO_HIGH);
	msleep(100);

	//init some register
	//to do
	
	return result;
}


static int ts_report_value(struct ts_private_data *ts)
{
	struct ts_platform_data *pdata = ts->pdata;
	struct ts_event *event = &ts->event;
	unsigned char buf[32] = {0};
	int result = 0 , i = 0, off = 0, id = 0;

	result = ts_bulk_read(ts, (unsigned short)ts->ops->read_reg, ts->ops->read_len, (unsigned short *)buf);
	if(result < 0)
	{
		printk("%s:fail to init ts\n",__func__);
		return result;
	}

	//for(i=0; i<ts->ops->read_len; i++)
	//DBG("buf[%d]=0x%x\n",i,buf[i]);
	
	event->touch_point = buf[2] & 0x07;// 0000 1111

	for(i=0; i<ts->ops->max_point; i++)
	{
		event->point[i].status = 0;
		event->point[i].x = 0;
		event->point[i].y = 0;
	}

	if(event->touch_point == 0)
	{	
		for(i=0; i<ts->ops->max_point; i++)
		{
			if(event->point[i].last_status != 0)
			{
				event->point[i].last_status = 0;				
				input_mt_slot(ts->input_dev, event->point[i].id);				
				input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, -1);
				input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
				DBG("%s:%s press up,id=%d\n",__func__,ts->ops->name, event->point[i].id);
			}
		}
		
		input_sync(ts->input_dev);
		memset(event, 0x00, sizeof(struct ts_event));
				
		return 0;
	}

	for(i = 0; i<event->touch_point; i++)
	{
		off = i*6+3;
		id = (buf[off+2] & 0xf0) >> 4;				
		event->point[id].id = id;
		event->point[id].status = (buf[off+0] & 0xc0) >> 6;
		event->point[id].x = ((buf[off+0] & 0x0f)<<8) | buf[off+1];
		event->point[id].y = ((buf[off+2] & 0x0f)<<8) | buf[off+3];
		
		if(ts->ops->xy_swap)
		{
			swap(event->point[id].x, event->point[id].y);
		}

		if(ts->ops->x_revert)
		{
			event->point[id].x = ts->ops->range[0] - event->point[id].x;	
		}

		if(ts->ops->y_revert)
		{
			event->point[id].y = ts->ops->range[1] - event->point[id].y;
		}	
		
	}

	for(i=0; i<ts->ops->max_point; i++)
	{
		if(event->point[i].status != 0)
		{		
			input_mt_slot(ts->input_dev, event->point[i].id);
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, event->point[i].id);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 1);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, event->point[i].x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, event->point[i].y);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 1);
	   		DBG("%s:%s press down,id=%d,x=%d,y=%d\n",__func__,ts->ops->name, event->point[id].id, event->point[id].x,event->point[id].y);
		}
		else if ((event->point[i].status == 0) && (event->point[i].last_status != 0))
		{				
			input_mt_slot(ts->input_dev, event->point[i].id);				
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, -1);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
			DBG("%s:%s press up1,id=%d\n",__func__,ts->ops->name, event->point[i].id);
		}

		event->point[i].last_status = event->point[i].status;
	}
	input_sync(ts->input_dev);

	return 0;
}

static int ts_suspend(struct ts_private_data *ts)
{
	struct ts_platform_data *pdata = ts->pdata;

	if(ts->ops->active)
		ts->ops->active(ts, 0);
	
	return 0;
}




static int ts_resume(struct ts_private_data *ts)
{
	struct ts_platform_data *pdata = ts->pdata;
	
	if(ts->ops->active)
		ts->ops->active(ts, 1);
	return 0;
}





struct ts_operate ts_ft5306_ops = {
	.name				= "ft5306",
	.slave_addr			= 0x3e,
	.ts_id				= TS_ID_FT5306,			//i2c id number
	.bus_type			= TS_BUS_TYPE_I2C,
	.reg_size			= 1,
	.id_reg				= FT5306_ID_REG,
	.id_data			= TS_UNKNOW_DATA,
	.version_reg			= TS_UNKNOW_DATA,
	.version_len			= 0,
	.version_data			= NULL,
	.read_reg			= FT5306_DATA_REG,		//read data
	.read_len			= 32,				//data length
	.trig				= IRQF_TRIGGER_FALLING,		
	.max_point			= 5,
	.xy_swap 			= 1,
	.x_revert 			= 1,
	.y_revert			= 0,
	.range				= {1024,768},
	.irq_enable			= 1,
	.poll_delay_ms			= 0,
	.active				= ts_active,	
	.init				= ts_init,
	.check_irq			= NULL,
	.report 			= ts_report_value,
	.firmware			= NULL,
	.suspend			= ts_suspend,
	.resume				= ts_resume,
};

/****************operate according to ts chip:end************/

//function name should not be changed
static struct ts_operate *ts_get_ops(void)
{
	return &ts_ft5306_ops;
}


static int __init ts_ft5306_init(void)
{
	struct ts_operate *ops = ts_get_ops();
	int result = 0;
	result = ts_register_slave(NULL, NULL, ts_get_ops);	
	DBG("%s\n",__func__);
	return result;
}

static void __exit ts_ft5306_exit(void)
{
	struct ts_operate *ops = ts_get_ops();
	ts_unregister_slave(NULL, NULL, ts_get_ops);
}


subsys_initcall(ts_ft5306_init);
module_exit(ts_ft5306_exit);


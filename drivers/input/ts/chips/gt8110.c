/* drivers/input/ts/chips/gt8110.c
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


#define GT8110_ID_REG		0x00
#define GT8110_DATA_REG		0x00


/****************operate according to ts chip:start************/

static int ts_active(struct i2c_client *client, int enable)
{
	struct ts_private_data *ts =
	    (struct ts_private_data *) i2c_get_clientdata(client);
	unsigned char buf_suspend[2] = {0x38, 0x56};		//suspend cmd
	int result = 0;

	if(enable)
	{
		gpio_direction_output(ts->pdata->reset_pin, GPIO_LOW);
		mdelay(200);
		gpio_direction_output(ts->pdata->reset_pin, GPIO_HIGH);
		msleep(200);
	}
	else
	{
		result = ts_tx_data(client, buf_suspend, 2);
		if(result < 0)
		{
			printk("%s:fail to init ts\n",__func__);
			return result;
		}
		
		gpio_direction_output(ts->pdata->reset_pin, GPIO_LOW);	
	}
		
	
	return result;
}

static int ts_init(struct i2c_client *client)
{
	struct ts_private_data *ts =
	    (struct ts_private_data *) i2c_get_clientdata(client);
	int irq_pin = irq_to_gpio(ts->pdata->irq);
	char version_data[18] = {240};
	char init_data[95] = {
	0x65,0x02,0x00,0x10,0x00,0x10,0x0A,0x6E,0x0A,0x00,
	0x0F,0x1E,0x02,0x08,0x10,0x00,0x00,0x27,0x00,0x00,
	0x50,0x10,0x10,0x11,0x37,0x00,0x00,0x00,0x01,0x02,
	0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0xFF,
	0xFF,0xFF,0xFF,0x00,0x01,0x02,0x03,0x04,0x05,0x06,
	0x07,0x08,0x09,0x0A,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
	0x00,0x50,0x64,0x50,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x20
        };
	int result = 0, i = 0;

	//read version
	result = ts_rx_data(client, version_data, 17);
	if(result < 0)
	{
		printk("%s:fail to init ts\n",__func__);
		return result;
	}
	version_data[17]='\0';

	printk("%s:%s version is %s\n",__func__,ts->ops->name, version_data);
#if 1
	//init some register
	result = ts_tx_data(client, init_data, 95);
	if(result < 0)
	{
		printk("%s:fail to init ts\n",__func__);
		return result;
	}
#endif
	result = ts_rx_data(client, init_data, 95);
	if(result < 0)
	{
		printk("%s:fail to init ts\n",__func__);
		return result;
	}

	
	printk("%s:rx:",__func__);
	for(i=0; i<95; i++)
	printk("0x%x,",init_data[i]);

	printk("\n");
	
	return result;
}


static int ts_report_value(struct i2c_client *client)
{
	struct ts_private_data *ts =
		(struct ts_private_data *) i2c_get_clientdata(client);	
	struct ts_platform_data *pdata = ts->pdata;
	struct ts_event *event = &ts->event;
	unsigned char buf[54] = {0};
	int result = 0 , i = 0, j = 0, off = 0, id = 0;
	int temp = 0, num = 0;

	buf[0] = ts->ops->read_reg;
	result = ts_rx_data(client, buf, ts->ops->read_len);
	if(result < 0)
	{
		printk("%s:fail to init ts\n",__func__);
		return result;
	}

	//for(i=0; i<ts->ops->read_len; i++)
	//DBG("buf[%d]=0x%x\n",i,buf[i]);

	//temp =  (buf[2]<<8) + buf[1];

	temp = ((buf[2]&0x03) << 8) | buf[1];
	for(i=0; i<ts->ops->max_point; i++)
	{
		if(temp & (1 << i)) 
			num++;
	}

	event->touch_point = num;
#if 0
	if(event->touch_point == 0)
	{	
		for(i=0; i<ts->ops->max_point; i++)
		{
			if(event->point[i].status != 0)
			{
				event->point[i].status = 0;				
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
#endif	
	for(i = 0; i<ts->ops->max_point; i++)
	{
		off = 3 + i*4;
		
		id = i;				
		event->point[id].id = id;
		event->point[id].status = temp & (1 << (ts->ops->max_point - i -1));
		event->point[id].x = (unsigned int)(buf[off+0]<<8) + (unsigned int)buf[off+1];
		event->point[id].y = (unsigned int)(buf[off+2]<<8) + (unsigned int)buf[off+3];
		//event->point[id].press = buf[off+4];

		//for(j=0; j<(3 + (i+1)*4); j++)
		//DBG("buf[%d]=0x%x\n",j,buf[j]);
		
		if(ts->ops->xy_swap)
		{
			swap(event->point[id].x, event->point[id].y);
		}

		if(ts->ops->x_revert)
		{
			event->point[id].x = ts->ops->pixel.max_x - event->point[id].x;	
		}

		if(ts->ops->y_revert)
		{
			event->point[id].y = ts->ops->pixel.max_y - event->point[id].y;
		}

		DBG("%s:point[%d].status=%d,point[%d].last_status=%d\n",__func__,i,event->point[i].status,i,event->point[i].last_status);

		if(event->point[id].status != 0)
		{		
			input_mt_slot(ts->input_dev, event->point[id].id);
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, event->point[id].id);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 1);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, event->point[id].x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, event->point[id].y);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 1);	
	   		DBG("%s:%s press down,id=%d,x=%d,y=%d\n\n",__func__,ts->ops->name, event->point[id].id, event->point[id].x,event->point[id].y);
		}
		else if((event->point[id].status == 0) && (event->point[id].last_status != 0))
		{
			event->point[i].status = 0;				
			input_mt_slot(ts->input_dev, event->point[i].id);				
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, -1);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
			DBG("%s:%s press up,id=%d\n\n",__func__,ts->ops->name, event->point[i].id);

		}
		
		event->point[id].last_status = event->point[id].status;
	}
	
	input_sync(ts->input_dev);

	return 0;
}

static int ts_suspend(struct i2c_client *client)
{
	struct ts_private_data *ts =
		(struct ts_private_data *) i2c_get_clientdata(client);	
	struct ts_platform_data *pdata = ts->pdata;
	
	if(ts->pdata->irq_enable)	
		disable_irq_nosync(client->irq);

	if(ts->ops->active)
		ts->ops->active(client, 0);
	
	return 0;
}


static int ts_resume(struct i2c_client *client)
{
	struct ts_private_data *ts =
		(struct ts_private_data *) i2c_get_clientdata(client);	
	struct ts_platform_data *pdata = ts->pdata;
	
	if(ts->pdata->irq_enable)	
		enable_irq(client->irq);

	if(ts->ops->active)
		ts->ops->active(client, 1);
	return 0;
}



struct ts_operate ts_gt8110_ops = {
	.name				= "gt8110",
	.slave_addr			= 0x5c,
	.id_i2c				= TS_ID_GT8110,			//i2c id number
	.pixel				= {1280,800},
	.id_reg				= GT8110_ID_REG,
	.id_data			= TS_UNKNOW_DATA,	
	.read_reg			= GT8110_DATA_REG,		//read data
	.read_len			= 5*10+3+1,			//data length
	.trig				= IRQ_TYPE_LEVEL_LOW | IRQF_ONESHOT,		
	.max_point			= 10,
	.xy_swap 			= 0,
	.x_revert 			= 0,
	.y_revert			= 0,
	.range				= {4096,4096},
	.active				= ts_active,	
	.init				= ts_init,
	.report 			= ts_report_value,
	.firmware			= NULL,
	.suspend			= ts_suspend,
	.resume				= ts_resume,
};

/****************operate according to ts chip:end************/

//function name should not be changed
static struct ts_operate *ts_get_ops(void)
{
	return &ts_gt8110_ops;
}


static int __init ts_gt8110_init(void)
{
	struct ts_operate *ops = ts_get_ops();
	int result = 0;
	result = ts_register_slave(NULL, NULL, ts_get_ops);	
	DBG("%s\n",__func__);
	return result;
}

static void __exit ts_gt8110_exit(void)
{
	struct ts_operate *ops = ts_get_ops();
	ts_unregister_slave(NULL, NULL, ts_get_ops);
}


subsys_initcall(ts_gt8110_init);
module_exit(ts_gt8110_exit);


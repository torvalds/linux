/* drivers/input/ts/chips/gt828.c
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


//Register define
#define GTP_READ_COOR_ADDR    0x0F40
#define GTP_REG_SLEEP         0x0FF2
#define GTP_REG_SENSOR_ID     0x0FF5
#define GTP_REG_CONFIG_DATA   0x0F80
#define GTP_REG_VERSION       0x0F7D

#define GTP_CONFIG_LENGTH	112
#define TRIGGER_LOC		64

//STEP_1(REQUIRED):Change config table.
/*TODO: puts the config info corresponded to your TP here, the following is just
a sample config, send this config should cause the chip cannot work normally*/
//default or float

u8 cfg_info_group[][GTP_CONFIG_LENGTH] = 
{
	{
		0x00,0x0F,0x01,0x10,0x02,0x11,0x03,0x12,0x04,0x13,
		0x05,0x14,0x06,0x15,0x07,0x16,0x08,0x17,0x09,0x18,
		0x0A,0x19,0x0B,0x1A,0x0C,0x1B,0x0D,0x1C,0xFF,0xFF,
		0x02,0x0C,0x03,0x0D,0x04,0x0E,0x05,0x0F,0x06,0x10,
		0x07,0x11,0x08,0x12,0x09,0x13,0xFF,0x11,0x12,0x13,
		0x0F,0x03,0x88,0x10,0x10,0x2A,0x00,0x00,0x00,0x00,
		0x00,0x0E,0x45,0x30,0x58,0x03,0x00,0x05,0x00,0x02,
		0x58,0x03,0x20,0x55,0x5E,0x50,0x58,0x27,0x00,0x05,
		0x19,0x05,0x14,0x10,0x00,0x05,0x00,0x00,0x00,0x00,
		0x00,0x00,0x40,0x30,0x30,0x3C,0x00,0x00,0x00,0x00,
		0x0F,0x88,0x28,0x05,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x01
	},
	{
		0x00,0x0F,0x01,0x10,0x02,0x11,0x03,0x12,0x04,0x13,
		0x05,0x14,0x06,0x15,0x07,0x16,0x08,0x17,0x09,0x18,
		0x0A,0x19,0x0B,0x1A,0x0C,0x1B,0x0D,0x1C,0xFF,0xFF,
		0x02,0x0C,0x03,0x0D,0x04,0x0E,0x05,0x0F,0x06,0x10,
		0x07,0x11,0x08,0x12,0x09,0x13,0xFF,0x11,0x12,0x13,
		0x0F,0x03,0x88,0x10,0x10,0x2A,0x00,0x00,0x00,0x00,
		0x00,0x0E,0x45,0x30,0x58,0x03,0x00,0x05,0x00,0x02,
		0x58,0x03,0x20,0x55,0x5E,0x50,0x58,0x27,0x00,0x05,
		0x19,0x05,0x14,0x10,0x00,0x05,0x00,0x00,0x00,0x00,
		0x00,0x00,0x40,0x30,0x30,0x3C,0x00,0x00,0x00,0x00,
		0x0F,0x88,0x28,0x05,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x01
	},
		
	{
		0x00,0x0F,0x01,0x10,0x02,0x11,0x03,0x12,0x04,0x13,
		0x05,0x14,0x06,0x15,0x07,0x16,0x08,0x17,0x09,0x18,
		0x0A,0x19,0x0B,0x1A,0x0C,0x1B,0x0D,0x1C,0xFF,0xFF,
		0x02,0x0C,0x03,0x0D,0x04,0x0E,0x05,0x0F,0x06,0x10,
		0x07,0x11,0x08,0x12,0x09,0x13,0xFF,0x11,0x12,0x13,
		0x0F,0x03,0x88,0x10,0x10,0x2A,0x00,0x00,0x00,0x00,
		0x00,0x0E,0x45,0x30,0x58,0x03,0x00,0x05,0x00,0x02,
		0x58,0x03,0x20,0x55,0x5E,0x50,0x58,0x27,0x00,0x05,
		0x19,0x05,0x14,0x10,0x00,0x05,0x00,0x00,0x00,0x00,
		0x00,0x00,0x40,0x30,0x30,0x3C,0x00,0x00,0x00,0x00,
		0x0F,0x88,0x28,0x05,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x01
	}

};

static u8 config[GTP_CONFIG_LENGTH+2] = {GTP_REG_CONFIG_DATA >> 8, GTP_REG_CONFIG_DATA & 0xff};


/****************operate according to ts chip:start************/

int ts_i2c_end_cmd(struct ts_private_data *ts)
{
	int result  = -1;
	char end_cmd_data[2]={0x80, 0x00};

	result = ts_reg_write(ts, end_cmd_data[0], end_cmd_data[1]);
	if(result < 0)
	{
		printk("%s:fail to init ts\n",__func__);
		return result;
	}

	return result;
}


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
	int result = 0;
	char version_data[5] = {ts->ops->version_reg >> 8, ts->ops->version_reg & 0xff};
	u8 rd_cfg_buf[2];
	
	//init some register	
	result = ts_bulk_read(ts, ts->ops->version_reg, 1, (unsigned short *)rd_cfg_buf);
	if(result < 0)
	{
		printk("%s:fail to read rd_cfg_buf\n",__func__);
		return result;
	}

	result = ts_i2c_end_cmd(ts);
	if(result < 0)
	{
		printk("%s:fail to end cmd\n",__func__);	
		rd_cfg_buf[0] = 0;
		//return result;
	}

	rd_cfg_buf[0] &= 0x03;
		
	printk("%s:%s id is %d\n",__func__,ts->ops->name, rd_cfg_buf[0]);
	
	memcpy(&config[2], cfg_info_group[rd_cfg_buf[0]], GTP_CONFIG_LENGTH);

	if((ts->ops->trig & IRQF_TRIGGER_FALLING) || (ts->ops->trig & IRQF_TRIGGER_LOW))  //FALLING
	{
		config[TRIGGER_LOC+2] &= 0xf7;
	}
	else if((ts->ops->trig & IRQF_TRIGGER_RISING) || (ts->ops->trig & IRQF_TRIGGER_HIGH))  //RISING
	{
		config[TRIGGER_LOC+2] |= 0x08;
	}
	
	result = ts_bulk_write(ts, GTP_REG_CONFIG_DATA, GTP_CONFIG_LENGTH, (unsigned short *)config);
	if(result < 0)
	{
		printk("%s:fail to send config data\n",__func__);
		return result;
	}

	result = ts_i2c_end_cmd(ts);
	if(result < 0)
	{
		printk("%s:fail to end cmd\n",__func__);	
		//return result;
	}

	//read version
	result = ts_bulk_read(ts, ts->ops->version_reg, 4, (unsigned short *)version_data);
	if(result < 0)
	{
		printk("%s:fail to init ts\n",__func__);
		return result;
	}
	version_data[4]='\0';
	
	result = ts_i2c_end_cmd(ts);
	if(result < 0)
	{
		printk("%s:fail to end cmd\n",__func__);
		//return result;
	}
	
	printk("%s:%s version is %s\n",__func__,ts->ops->name, version_data);
	
	return result;
}


static int ts_report_value(struct ts_private_data *ts)
{
	struct ts_platform_data *pdata = ts->pdata;
	struct ts_event *event = &ts->event;
	unsigned char buf[2 + 2 + 5 * 5 + 1] = {0};
	int result = 0 , i = 0, off = 0, id = 0;
	int finger = 0;
	int checksum = 0;

	result = ts_bulk_read(ts, ts->ops->read_reg, ts->ops->read_len, (unsigned short *)buf);
	if(result < 0)
	{
		printk("%s:fail to init ts\n",__func__);
		return result;
	}

	result = ts_i2c_end_cmd(ts);
	if(result < 0)
	{
		printk("%s:fail to end ts\n",__func__);
		return result;
	}
	
	//for(i=0; i<ts->ops->read_len; i++)
	//DBG("buf[%d]=0x%x\n",i,buf[i]);
	finger = buf[0];
	if((finger & 0x80) != 0x80)
	{
		DBG("%s:data not ready!,finger=0x%x\n",__func__,finger);
		//return -1;
	}
	
	for(i = 0; i<ts->ops->max_point; i++)
	{
		off = i*5+2;

		id = i;				
		event->point[id].id = id;
		event->point[id].status = !!(finger & (1 << i));
		event->point[id].x = (buf[off+0]<<8) | buf[off+1];
		event->point[id].y = (buf[off+2]<<8) | buf[off+3];
		event->point[id].press = buf[off+4];	
		
		DBG("data:0x%x,0x%x,0x%x,0x%x\n",buf[off+0],buf[off+1],buf[off+2],buf[off+3]);
		
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

		if(event->point[id].status != 0)
		{		
			input_mt_slot(ts->input_dev, event->point[id].id);
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, event->point[id].id);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, event->point[id].press);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, event->point[id].x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, event->point[id].y);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 1);
	   		DBG("%s:%s press down,id=%d,x=%d,y=%d\n",__func__,ts->ops->name, event->point[id].id, event->point[id].x,event->point[id].y);
		}
		else if ((event->point[id].status == 0) && (event->point[id].last_status != 0))
		{			
			input_mt_slot(ts->input_dev, event->point[i].id);				
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, -1);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
			DBG("%s:%s press up,id=%d\n",__func__,ts->ops->name, event->point[i].id);
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





struct ts_operate ts_gt828_ops = {
	.name				= "gt828",
	.slave_addr			= 0x5d,
	.ts_id				= TS_ID_GT828,			//i2c id number
	.bus_type			= TS_BUS_TYPE_I2C,
	.reg_size			= 2,
	.id_reg				= GTP_REG_SENSOR_ID,
	.id_data			= TS_UNKNOW_DATA,
	.version_reg			= 0x0F7D,
	.version_len			= 0,
	.version_data			= NULL,
	.read_reg			= GTP_READ_COOR_ADDR,		//read data
	.read_len			= 2 + 2 + 5 * 5 + 1,		//data length
	.trig				= IRQF_TRIGGER_FALLING,		
	.max_point			= 5,
	.xy_swap 			= 0,
	.x_revert 			= 0,
	.y_revert			= 0,
	.range				= {800,1280},
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
	return &ts_gt828_ops;
}


static int __init ts_gt828_init(void)
{
	struct ts_operate *ops = ts_get_ops();
	int result = 0;
	result = ts_register_slave(NULL, NULL, ts_get_ops);	
	DBG("%s\n",__func__);
	return result;
}

static void __exit ts_gt828_exit(void)
{
	struct ts_operate *ops = ts_get_ops();
	ts_unregister_slave(NULL, NULL, ts_get_ops);
}


subsys_initcall(ts_gt828_init);
module_exit(ts_gt828_exit);


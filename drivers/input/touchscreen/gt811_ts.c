/* drivers/input/touchscreen/gt811.c
 *
 * Copyright (C) 2010 - 2011 Goodix, Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 *Any problem,please contact andrew@goodix.com,+86 755-33338828
 *
 */
 
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <mach/gpio.h>
//#include <plat/gpio-cfg.h>
//#include <plat/gpio-bank-l.h>
//#include <plat/gpio-bank-f.h>
#include <linux/irq.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/proc_fs.h>
#include "gt811_ts.h"
#include "gt811_firmware.h"

#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/completion.h>
#include <asm/uaccess.h>
#include <linux/input/mt.h>
#include <plat/board.h>
#include <mach/iomux.h>
#ifdef CONFIG_RK_CONFIG
#include <mach/config.h>
#endif
#define TP_MODULE_NAME  GOODIX_I2C_NAME
#ifdef CONFIG_RK_CONFIG

enum {
#if defined(RK2928_SDK_DEFAULT_CONFIG)
        DEF_EN = 1,
        DEF_IRQ = 0x008003c7,
        DEF_RST = 0X000003c3,
#elif defined(RK2926_V86_DEFAULT_CONFIG)
        DEF_EN = 1,
        DEF_IRQ = 0x008001b0,
        DEF_RST = 0X000000d3,
#else
        DEF_EN = 0,
        DEF_IRQ = -1,
        DEF_RST = -1,
#endif
        DEF_I2C = 2, 
        DEF_ADDR = 0x5d,
        DEF_X_MAX = 800,
        DEF_Y_MAX = 480,
};
static int en = DEF_EN;
module_param(en, int, 0644);

static int irq = DEF_IRQ;
module_param(irq, int, 0644);
static int rst =DEF_RST;
module_param(rst, int, 0644);

static int i2c = DEF_I2C;            // i2c channel
module_param(i2c, int, 0644);
static int addr = DEF_ADDR;           // i2c addr
module_param(addr, int, 0644);
static int x_max = DEF_X_MAX;
module_param(x_max, int, 0644);
static int y_max = DEF_Y_MAX;
module_param(y_max, int, 0644);

static int tp_hw_init(void)
{
        int ret = 0;

        ret = gpio_request(get_port_config(irq).gpio, "tp_irq");
        if(ret < 0){
                printk("%s: gpio_request(irq gpio) failed\n", __func__);
                return ret;
        }

        ret = port_output_init(rst, 1, "tp_rst");
        if(ret < 0){
                printk("%s: port(rst) output init faild\n", __func__);
                return ret;
        }
        mdelay(10);
        port_output_off(rst);
        mdelay(10);
        port_output_on(rst);
        msleep(300);

         return 0;
}
#include "rk_tp.c"
#endif



static struct workqueue_struct *goodix_wq;
static const char *s3c_ts_name = "gt811_ts";
//static struct point_queue finger_list;
struct i2c_client * i2c_connect_client = NULL;
int reset_pin = 0;
//EXPORT_SYMBOL(i2c_connect_client);
static struct proc_dir_entry *goodix_proc_entry;
static short  goodix_read_version(struct gt811_ts_data *ts);	
//static int tpd_button(struct gt811_ts_data *ts, unsigned int x, unsigned int y, unsigned int down);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h);
static void goodix_ts_late_resume(struct early_suspend *h);
int  gt811_downloader( struct gt811_ts_data *ts, unsigned char * data);
#endif
//used by firmware update CRC
unsigned int oldcrc32 = 0xFFFFFFFF;
unsigned int crc32_table[256];
unsigned int ulPolynomial = 0x04c11db7;

unsigned int raw_data_ready = RAW_DATA_NON_ACTIVE;

//#define DEBUG
#ifdef DEBUG
int sum = 0;
int access_count = 0;
int int_count = 0;
#endif
#define HAVE_TOUCH_KEY
 //#define READ_KEY_VALUE
 //#define READ_KEY_COOR

#ifdef HAVE_TOUCH_KEY
    const uint16_t touch_key_array[]={
                                       KEY_MENU,             //MENU
                                       KEY_HOMEPAGE,                                    
                                       KEY_BACK,
                                       KEY_SEARCH
                                      };
#define MAX_KEY_NUM  (sizeof(touch_key_array)/sizeof(touch_key_array[0]))
#endif

/*******************************************************	
Function:
	Read data from the slave
	Each read operation with two i2c_msg composition, for the first message sent from the machine address,
	Article 2 reads the address used to send and retrieve data; each message sent before the start signal
Parameters:
	client: i2c devices, including device address
	buf [0]: The first byte to read Address
	buf [1] ~ buf [len]: data buffer
	len: the length of read data
return:
	Execution messages
*********************************************************/
/*Function as i2c_master_send */
static int i2c_read_bytes(struct i2c_client *client, uint8_t *buf, int len)
{
	struct i2c_msg msgs[2];
	int ret=-1;
	
	msgs[0].flags=!I2C_M_RD;
	msgs[0].addr=client->addr;
	msgs[0].len=2;
	msgs[0].buf=&buf[0];
	msgs[0].scl_rate=200000;

	msgs[1].flags=I2C_M_RD;
	msgs[1].addr=client->addr;
	msgs[1].len=len-2;
	msgs[1].buf=&buf[2];
	msgs[1].scl_rate=200000;
	
	ret=i2c_transfer(client->adapter,msgs, 2);
	return ret;
}

/*******************************************************	
Function:
	Write data to a slave
Parameters:
	client: i2c devices, including device address
	buf [0]: The first byte of the write address
	buf [1] ~ buf [len]: data buffer
	len: data length
return:
	Execution messages
*******************************************************/
/*Function as i2c_master_send */
static int i2c_write_bytes(struct i2c_client *client,uint8_t *data,int len)
{
	struct i2c_msg msg;
	int ret=-1;
	//发送设备地址
	msg.flags=!I2C_M_RD;//写消�?	msg.addr=client->addr;
	msg.len=len;
	msg.buf=data;	
	msg.scl_rate=200000;
    msg.addr=client->addr;
	
	ret=i2c_transfer(client->adapter,&msg, 1);
	return ret;
}

/*******************************************************
Function:
	Send a prefix command
	
Parameters:
	ts: client private data structure
	
return:
	Results of the implementation code, 0 for normal execution
*******************************************************/
static int i2c_pre_cmd(struct gt811_ts_data *ts)
{
	int ret;
	uint8_t pre_cmd_data[2]={0};	
	pre_cmd_data[0]=0x0f;
	pre_cmd_data[1]=0xff;
	ret=i2c_write_bytes(ts->client,pre_cmd_data,2);
	//msleep(2);
	return ret;
}

/*******************************************************
Function:
	Send a suffix command
	
Parameters:
	ts: client private data structure
	
return:
	Results of the implementation code, 0 for normal execution
*******************************************************/
static int i2c_end_cmd(struct gt811_ts_data *ts)
{
	int ret;
	uint8_t end_cmd_data[2]={0};	
	end_cmd_data[0]=0x80;
	end_cmd_data[1]=0x00;
	ret=i2c_write_bytes(ts->client,end_cmd_data,2);
	//msleep(2);
	return ret;
}

/********************************************************************

*********************************************************************/
#ifdef COOR_TO_KEY
static int list_key(s32 x_value, s32 y_value, u8* key)
{
	s32 i;

#ifdef AREA_Y
	if (y_value <= AREA_Y)
#else
	if (x_value <= AREA_X)
#endif
	{
		return 0;
	}

	for (i = 0; i < MAX_KEY_NUM; i++)
	{
		if (abs(key_center[i][x] - x_value) < KEY_X 
		&& abs(key_center[i][y] - y_value) < KEY_Y)
		{
			(*key) |= (0x01<<i);
        	}
   	 }

    return 1;
}
#endif 

/*******************************************************
Function:
	Guitar initialization function, used to send configuration information, access to version information
Parameters:
	ts: client private data structure
return:
	Results of the implementation code, 0 for normal execution
*******************************************************/
static int goodix_init_panel(struct gt811_ts_data *ts)
{
	short ret=-1;
	uint8_t config_info[] = {
	0x06,0xA2,
/*
	0x00,0x02,0x04,0x06,0x08,0x0A,0x0C,0x0E,0x10,0x12,0x60,0x00,0x50,0x00,0x40,0x00,
	0x30,0x00,0x20,0x00,0x10,0x00,0x00,0x00,0x70,0x00,0x80,0x00,0x90,0x00,0xA0,0x00,
	0xB0,0x00,0xC0,0x00,0xD0,0x00,0xE0,0x00,0xF0,0x00,0x05,0x03,0x90,0x90,0x90,0x30,
	0x30,0x30,0x0F,0x0F,0x0A,0x50,0x3C,0x08,0x03,0x3C,0x05,0x00,0x14,0x00,0x20,0x04,
	0x04,0x64,0x5A,0x40,0x40,0x00,0x00,0x03,0x19,0x00,0x05,0x00,0x00,0x00,0x00,0x00,
	0x20,0x10,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0x50,
	0x3C,0x3C,0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x01
*/
/*	0x00,0x02,0x04,0x06,0x08,0x0A,0x0C,0x0E,0x10,0x12,0x13,0x33,0x23,0x33,0x33,0x33,
	0x43,0x33,0x53,0x33,0x63,0x33,0x73,0x33,0x83,0x33,0x93,0x33,0xA3,0x33,0xB3,0x33,
	0xC3,0x33,0xD3,0x33,0xE3,0x33,0xF3,0x33,0x03,0x33,0x3B,0x03,0x88,0x88,0x88,0x1B,
	0x1B,0x1B,0x0F,0x0F,0x0A,0x40,0x30,0x0F,0x03,0x00,0x05,0x00,0x14,0x00,0x1E,0x04,
	0x04,0x64,0x5A,0x40,0x40,0x00,0x00,0x05,0x19,0x05,0x05,0x00,0x00,0x00,0x00,0x00,
	0x20,0x10,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0x50,
	0x3C,0x3C,0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x01
*/	

   /* 0x12,0x10,0x0E,0x0C,0x0A,0x08,0x06,0x04,0x02,0x00,
    0x02,0x22,0x12,0x22,0x22,0x22,0x32,0x22,0x42,0x22,
    0x52,0x22,0x62,0x22,0x72,0x22,0x82,0x22,0x92,0x22,
    0xA2,0x22,0xB2,0x22,0xC2,0x22,0xD2,0x22,0xE2,0x22,
    0xF2,0x22,0x1B,0x03,0x88,0x88,0x88,0x1D,0x1D,0x1D,
    0x10,0x0F,0x0A,0x47,0x2A,0x0D,0x03,0x00,0x05,0x00,
    0x03,0x00,0x04,0x00,0x00,0x38,0x33,0x35,0x30,0x00,
    0x00,0x07,0x20,0x05,0x05,0x00,0x00,0x00,0x00,0x00,
    0x32,0x16,0x80,0x03,0x00,0x40,0x17,0x88,0x1B,0x00,
    0x71,0x80,0x90,0xA0,0x0A,0x45,0x30,0x20,0x14,0x00,
    0x00,0x00,0x00,0x00,0x00,0x01*/
     
	/* 0x12,0x10,0x0E,0x0C,0x0A,0x08,0x06,0x04,0x02,0x00,
	 0x52,0x22,0x62,0x22,0x72,0x22,0x82,0x22,0x92,0x22,
	 0xA2,0x22,0xB2,0x22,0xC2,0x22,0xD2,0x22,0xE2,0x22,
	 0xF2,0x22,0x02,0x22,0x12,0x22,0x22,0x22,0x32,0x22,
	 0x42,0x22,0x07,0x03,0x88,0x88,0x88,0x27,0x27,0x27,
	 0x0B,0x0A,0x09,0x40,0x30,0x01,0x03,0x00,0x05,0xE0,
	 0x01,0x20,0x03,0x00,0x00,0xFF,0xFF,0x42,0x2B,0x00,
	 0x00,0x23,0x14,0x05,0x06,0x00,0x00,0x00,0x00,0x00,
	 0x14,0x10,0xC6,0x02,0x00,0x40,0x17,0x88,0x20,0x00,
	 0x60,0x70,0x80,0x90,0x0D,0x40,0x30,0x25,0x20,0x00,
	 0x00,0x00,0x00,0x00,0x00,0x01*/
	   0x12,0x10,0x0E,0x0C,0x0A,0x08,0x06,0x04,0x02,0x00,
	   0x02,0x22,0x12,0x22,0x22,0x22,0x32,0x22,0x42,0x22,
	   0x52,0x22,0x62,0x22,0x72,0x22,0x83,0x22,0x92,0x22,
	   0xA2,0x22,0xB2,0x22,0xC2,0x22,0xD2,0x22,0xE2,0x22,
	   0xF2,0x22,0x1B,0x03,0x28,0x28,0x28,0x20,0x20,0x20,
	   0x0F,0x0F,0x0A,0x45,0x30,0x04,0x03,0x00,0x05,0xE0,
	   0x01,0x20,0x03,0x00,0x00,0x38,0x33,0x35,0x30,0x00,
	   0x00,0x26,0x14,0x02,0x0A,0x00,0x00,0x00,0x00,0x00,
	   0x14,0x10,0x30,0x02,0x00,0x00,0x00,0x00,0x00,0x00,
	   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	   0x00,0x00,0x00,0x00,0x00,0x01
    
    };

	config_info[62] = TOUCH_MAX_WIDTH >> 8;
    	config_info[61] = TOUCH_MAX_WIDTH & 0xff;
    	config_info[64] = TOUCH_MAX_HEIGHT >> 8;
    	config_info[63] = TOUCH_MAX_HEIGHT & 0xff;
	
	ret = i2c_write_bytes(ts->client, config_info, sizeof(config_info)/sizeof(config_info[0]));
	if(ret < 0)
	{
		dev_info(&ts->client->dev, "GT811 Send config failed!\n");
		return ret;
	}
	ts->abs_y_max = (config_info[62]<<8) + config_info[61];
	ts->abs_x_max = (config_info[64]<<8) + config_info[63];
	ts->max_touch_num = config_info[60];
	ts->int_trigger_type = ((config_info[57]>>3)&0x01);
	dev_info(&ts->client->dev, "GT811 init info:X_MAX=%d,Y_MAX=%d,TRIG_MODE=%s\n",
	ts->abs_x_max, ts->abs_y_max, ts->int_trigger_type?"RISING EDGE":"FALLING EDGE");

	return 0;
}

/*******************************************************
FUNCTION:
	Read gt811 IC Version
Argument:
	ts:	client
return:
	0:success
       -1:error
*******************************************************/
static short  goodix_read_version(struct gt811_ts_data *ts)
{
	short ret;
	uint8_t version_data[5]={0x07,0x17,0,0};	//store touchscreen version infomation
	uint8_t version_data2[5]={0x07,0x17,0,0};	//store touchscreen version infomation

	char i = 0;
	char cpf = 0;
	memset(version_data, 0, 5);
	version_data[0]=0x07;
	version_data[1]=0x17;	

      	ret=i2c_read_bytes(ts->client, version_data, 4);
	if (ret < 0) 
		return ret;
	
	for(i = 0;i < 10;i++)
	{
		i2c_read_bytes(ts->client, version_data2, 4);
		if((version_data[2] !=version_data2[2])||(version_data[3] != version_data2[3]))
		{
			version_data[2] = version_data2[2];
			version_data[3] = version_data2[3];
			msleep(5);
			break;
		}
		msleep(5);
		cpf++;
	}

	if(cpf == 10)
	{
		ts->version = (version_data[2]<<8)+version_data[3];
		dev_info(&ts->client->dev, "GT811 Verion:0x%04x\n", ts->version);
		ret = 0;
	}
	else
	{
		dev_info(&ts->client->dev," Guitar Version Read Error: %d.%d\n",version_data[3],version_data[2]);
		ts->version = 0xffff;
		ret = -1;
	}
	
	return ret;
	
}
/******************start add by kuuga*******************/
static void gt811_irq_enable(struct gt811_ts_data *ts)
{	
	unsigned long irqflags;	
	spin_lock_irqsave(&ts->irq_lock, irqflags);
	if (ts->irq_is_disable) 
	{		
		enable_irq(ts->irq);		
		ts->irq_is_disable = 0;	
	}	
	spin_unlock_irqrestore(&ts->irq_lock, irqflags);
}

static void gt811_irq_disable(struct gt811_ts_data *ts)
{	
	unsigned long irqflags;
	spin_lock_irqsave(&ts->irq_lock, irqflags);
	if (!ts->irq_is_disable) 
	{		
		disable_irq_nosync(ts->irq);		
		ts->irq_is_disable = 1;	
	}	
	spin_unlock_irqrestore(&ts->irq_lock, irqflags);
}

/*****************end add by kuuga****************/

/*******************************************************	
Function:
	Touch-screen work function
	Triggered by the interruption, to accept a set of coordinate data,
	and then analyze the output parity
Parameters:
	ts: client private data structure
return:
	Results of the implementation code, 0 for normal execution
********************************************************/
static void goodix_ts_work_func(struct work_struct *work)
{	
	uint8_t  point_data[READ_BYTES_NUM] = {READ_TOUCH_ADDR_H,READ_TOUCH_ADDR_L,0};//point_data[8*MAX_FINGER_NUM+2]={ 0 };  
	uint8_t  check_sum = 0;
	uint8_t  read_position = 0;
	uint8_t  track_id[MAX_FINGER_NUM];
	uint8_t  point_index = 0;
	uint8_t  point_tmp = 0;
	uint8_t  point_count = 0;
	uint16_t input_x = 0;
	uint16_t input_y = 0;
	uint8_t  input_w = 0;
	static uint8_t  last_key = 0;
	uint8_t  finger = 0;
	uint8_t  key = 0;
	unsigned int  count = 0;
	unsigned int position = 0;	
	int ret=-1;
	int tmp = 0;
	
	struct gt811_ts_data *ts = container_of(work, struct gt811_ts_data, work);
#ifdef DEBUG
    	printk("int count :%d\n", ++int_count);
   	printk("ready?:%d\n", raw_data_ready);
#endif     
    	if (RAW_DATA_ACTIVE == raw_data_ready)
	{
	    raw_data_ready = RAW_DATA_READY;
#ifdef DEBUG	    
	    printk("ready!\n");
#endif
	}
	
#ifndef INT_PORT
COORDINATE_POLL:
#endif
	if( tmp > 9) 
	{
		dev_info(&(ts->client->dev), "Because of transfer error,touchscreen stop working.\n");
		goto XFER_ERROR ;
	}
	
	ret=i2c_read_bytes(ts->client, point_data, sizeof(point_data)/sizeof(point_data[0]));
	if(ret <= 0) 
	{
       dev_err(&(ts->client->dev),"I2C transfer error. Number:%d\n ", ret);
       ts->bad_data = 1;
       tmp ++;
       ts->retry++;
#ifndef INT_PORT
       goto COORDINATE_POLL;
#else   
       goto XFER_ERROR;
#endif  
   }
#if 0
	for(count=0;count<(sizeof(point_data)/sizeof(point_data[0])); count++)
	{
		printk("[%2d]:0x%2x", count, point_data[count]);
		if((count+1)%10==0)printk("\n");
	}
	printk("\n");
#endif	
	if(point_data[2]&0x20)
	{
		if(point_data[3]==0xF0)
		{
			gpio_direction_output(reset_pin, 0);
			msleep(1);
		//	gpio_direction_input(reset_pin);
            gpio_set_value(reset_pin,0);
            msleep(100);
            gpio_set_value(reset_pin,1);
            msleep(100);

			goodix_init_panel(ts);
			goto WORK_FUNC_END;
		}
	}
	switch(point_data[2]& 0x1f)
	{
		case 0:
		case 1:
			for(count=2; count<9; count++)
				check_sum += (int)point_data[count];
			read_position = 9;
			break;
		case 2:
		case 3:
			for(count=2; count<14;count++)
				check_sum += (int)point_data[count];
			read_position = 14;
			break;	
		default:		//touch finger larger than 3
			for(count=2; count<35;count++)
				check_sum += (int)point_data[count];
			read_position = 35;
	}
	if(check_sum != point_data[read_position])
	{
		dev_info(&ts->client->dev, "coor chksum error!\n");
		goto XFER_ERROR;
	}
    
	point_index = point_data[2]&0x1f;
	point_tmp = point_index;
	for(position=0; (position<MAX_FINGER_NUM)&&point_tmp; position++)
	{
		if(point_tmp&0x01)
		{
			track_id[point_count++] = position;
		}	
		point_tmp >>= 1;
	}	
	finger = point_count;
	if(finger)
	{
		for(count=0; count<finger; count++)
		{
			if(track_id[count]!=3)
			{
				if(track_id[count]<3)
					position = 4+track_id[count]*5;
				else
					position = 30;
				input_x = (uint16_t)(point_data[position]<<8)+(uint16_t)point_data[position+1];
				input_y = (uint16_t)(point_data[position+2]<<8)+(uint16_t)point_data[position+3];
				input_w = point_data[position+4];
			}
			else
			{
				input_x = (uint16_t)(point_data[19]<<8)+(uint16_t)point_data[26];
        input_y = (uint16_t)(point_data[27]<<8)+(uint16_t)point_data[28];
        input_w = point_data[29];	
			}
			
			//printk("real_input_y = %d,TOUCH_MAX_HEIGHT=%d,TOUCH_MAX_WIDTH=%d,input_x=%d,input_y=%d,input_w=%d\n",input_y, TOUCH_MAX_HEIGHT,TOUCH_MAX_WIDTH,TOUCH_MAX_HEIGHT - input_y,TOUCH_MAX_WIDTH-input_x,  input_w);
			//if((input_y > ts->abs_x_max)||(TOUCH_MAX_WIDTH-input_x > ts->abs_y_max))continue;
			input_mt_slot(ts->input_dev, track_id[count]);	
			//input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
			//printk("DOWN\n")
; ;
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, input_y);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, input_x);			
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, input_w);
			//input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, input_w);
			//input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, track_id[count]);
			//input_mt_sync(ts->input_dev);	
		}
	}
	else
	{
		for(tmp=0; tmp< MAX_FINGER_NUM; tmp++)
		{
		//printk("tmp=%d\n", tmp);
		input_mt_slot(ts->input_dev, tmp);//按序号上�?		//input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
		}
		//input_mt_sync(ts->input_dev);
	}
	
	//input_report_key(ts->input_dev, BTN_TOUCH, finger > 0);
	input_sync(ts->input_dev);

#ifdef HAVE_TOUCH_KEY
	key = point_data[3]&0x0F;
	if((last_key != 0)||(key != 0))
	{
		for(count = 0; count < MAX_KEY_NUM; count++)
		{
      //printk("####################input_report_key++++++++++++%d\n",key);
			input_report_key(ts->input_dev, touch_key_array[count], !!(key&(0x01<<count)));	
		}
	}		
	last_key = key;	
#endif

XFER_ERROR:
WORK_FUNC_END:
#ifndef STOP_IRQ_TYPE
	if(ts->use_irq)
		gt811_irq_enable(ts);     //KT ADD 1202
#endif
}

/*******************************************************	
Function:
	Response function timer
	Triggered by a timer, scheduling the work function of the touch screen operation; after re-timing
Parameters:
	timer: the timer function is associated
return:
	Timer mode, HRTIMER_NORESTART that do not automatically restart
********************************************************/
static enum hrtimer_restart goodix_ts_timer_func(struct hrtimer *timer)
{
	struct gt811_ts_data *ts = container_of(timer, struct gt811_ts_data, timer);
	queue_work(goodix_wq, &ts->work);
	hrtimer_start(&ts->timer, ktime_set(0, (POLL_TIME+6)*1000000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

/*******************************************************	
Function:
	Interrupt response function
	Triggered by an interrupt, the scheduler runs the touch screen handler
********************************************************/
static irqreturn_t goodix_ts_irq_handler(int irq, void *dev_id)
{
	struct gt811_ts_data *ts = dev_id;

#ifndef STOP_IRQ_TYPE
	gt811_irq_disable(ts);     //KT ADD 1202
#endif
	//disable_irq_nosync(ts->client->irq);
	queue_work(goodix_wq, &ts->work);
	
	return IRQ_HANDLED;
}

/*******************************************************	
Function:
	Power management gt811, gt811 allowed to sleep or to wake up
Parameters:
	on: 0 that enable sleep, wake up 1
return:
	Is set successfully, 0 for success
	Error code: -1 for the i2c error, -2 for the GPIO error;-EINVAL on error as a parameter
********************************************************/
static int goodix_ts_power(struct gt811_ts_data * ts, int on)
{
	int ret = -1;

	unsigned char i2c_control_buf[3] = {0x06,0x92,0x01};		//suspend cmd
	
#ifdef INT_PORT	
	if(ts != NULL && !ts->use_irq)
		return -2;
#endif		
	switch(on)
	{
		case 0:
			ret = i2c_write_bytes(ts->client, i2c_control_buf, 3);
            printk("ret++++++++++++++++= %d=\n",ret);
			dev_info(&ts->client->dev, "Send suspend cmd\n");
			if(ret < 0)						//failed
				ret = 0;
			return ret;
			
		case 1:
			gpio_direction_output(reset_pin,0);
			msleep(1);
	    gpio_set_value(reset_pin,0);
	    msleep(100);
	    gpio_set_value(reset_pin,1);
	    msleep(100);
			ret = 0;
			return ret;
				
		default:
			dev_info(&ts->client->dev, "%s: Cant't support this command.", s3c_ts_name);
			return -EINVAL;
	}

}
/*******************************************************	
Function:
	Touch-screen detection function
	Called when the registration drive (required for a corresponding client);
	For IO, interrupts and other resources to apply; equipment registration; touch screen initialization, etc.
Parameters:
	client: the device structure to be driven
	id: device ID
return:
	Results of the implementation code, 0 for normal execution
********************************************************/
static int goodix_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	int val_ret = 1;
	int retry=0;
        char test_data = 1;
		char buf[2]={0};   //w++
	unsigned char read_data[2] = {0,0 };        
	const char irq_table[2] = {IRQF_TRIGGER_FALLING,IRQF_TRIGGER_RISING};
	struct gt811_ts_data *ts;
 //   struct gt811_platform_data *811data = client->dev.platform_data;
	struct goodix_platform_data *pdata;
	dev_info(&client->dev,"Install gt811 driver.\n");
	dev_info(&client->dev,"Driver Release Date:2012-02-08\n");	

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}
#if 0	
	ret=i2c_read_bytes(client, read_data, sizeof(read_data)/sizeof(read_data[0]));
	if(ret <= 0)  
		{
	dev_err(&client->dev, "Must have GT811.\n");
	goto err_check_functionality_failed;	
		}
#endif	
	
	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}

	i2c_connect_client = client;
	ts->client = client;
	i2c_set_clientdata(client, ts);
	pdata = client->dev.platform_data;
#ifdef CONFIG_RK_CONFIG
        tp_hw_init();
        reset_pin = get_port_config(rst).gpio;
        client->irq = get_port_config(irq).gpio;
#else
	if (pdata != NULL)
	{
		reset_pin = pdata->rest_pin;
		
		if (pdata->init_platform_hw)
		{
			pdata->init_platform_hw();
		}

	}
#endif
/*	
	gpio_free(SHUTDOWN_PORT);
	ret = gpio_request(SHUTDOWN_PORT, "RESET_INT");
	if (ret < 0)
        {
		dev_err(&client->dev, "Failed to request RESET GPIO:%d, ERRNO:%d\n",(int)SHUTDOWN_PORT,ret);
		goto err_gpio_request;
	}
	
	rk29_mux_api_set(GPIO0D3_PWM_1_NAME,GPIO0D_GPIO0D3);
	 gpio_pull_updown(SHUTDOWN_PORT, 1);		//set GPIO pull-up
*/	
	for(retry=0;retry <= 10; retry++)
	{
	gpio_direction_output(reset_pin,0);
	msleep(1);
	//		gpio_direction_input(reset_pin);//setinput means not ack so set the reset high
	//		msleep(100);
	gpio_set_value(reset_pin,1);
	msleep(100);
	gpio_set_value(reset_pin,0);
	msleep(100);
	gpio_set_value(reset_pin,1);
	msleep(100);
	val_ret = gpio_get_value(reset_pin);
    ret = i2c_write_bytes(client, &test_data, 1);
	//ret =i2c_master_reg8_recv(client, 0x00, buf, 2, 200*1000);//i2c_write_bytes(client, &test_data, 1);	//Test I2C connection.
	if (ret == 1)
	break;
	dev_info(&client->dev, "GT811 I2C TEST FAILED!Please check the HARDWARE connect\n");
	}

	if(ret <= 0)
	{
		dev_err(&client->dev, "Warnning: I2C communication might be ERROR!\n");
		goto err_i2c_failed;
	}	

	INIT_WORK(&ts->work, goodix_ts_work_func);		//init work_struct

/////////////////////////////// UPDATE STEP 1 START/////////////////////////////////////////////////////////////////
#ifdef AUTO_UPDATE_GT811		//modify by andrew
	msleep(20);
  goodix_read_version(ts);
      
  ret = gt811_downloader( ts, goodix_gt811_firmware);
  if(ret < 0)
  {
          dev_err(&client->dev, "Warnning: gt811 update might be ERROR!\n");
          //goto err_input_dev_alloc_failed;
  }
#endif
///////////////////////////////UPDATE STEP 1 END////////////////////////////////////////////////////////////////      
/*
#ifdef INT_PORT	
	client->irq=TS_INT;		//If not defined in client
	if (client->irq)
	{
		gpio_free(INT_PORT);
		ret = gpio_request(INT_PORT, "TS_INT");	//Request IO
        if (ret < 0) 
		{
			dev_err(&client->dev, "Failed to request GPIO:%d, ERRNO:%d\n",(int)INT_PORT,ret);
			goto err_gpio_request_failed;
		}
		
		 gpio_pull_updown(INT_PORT, NULL);	//ret > 0 ?
		
	
	#ifndef STOP_IRQ_TYPE
		ts->irq = TS_INT;     //KT ADD 1202
		ts->irq_is_disable = 0;           // enable irq
	#endif	
	}
#endif	
*/
err_gpio_request_failed:
	for(retry=0; retry<3; retry++)
	{
		ret=goodix_init_panel(ts);
		msleep(2);
		if(ret != 0)	//Initiall failed
			continue;
		else
			break;
	}
	if(ret != 0) 
	{
		ts->bad_data=1;
		goto err_init_godix_ts;
	}

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) 
	{
		ret = -ENOMEM;
		dev_dbg(&client->dev,"goodix_ts_probe: Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}
	
	//ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) ;
	//ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	//////ts->input_dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE);
#ifdef HAVE_TOUCH_KEY
	for(retry = 0; retry < MAX_KEY_NUM; retry++)
	{
		input_set_capability(ts->input_dev,EV_KEY,touch_key_array[retry]);	
	}
#endif
/*
	input_set_abs_params(ts->input_dev, ABS_X, 0,  ts->abs_x_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, 0, ts->abs_y_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);
*/	
#ifdef GOODIX_MULTI_TOUCH

	__set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
	__set_bit(EV_ABS, ts->input_dev->evbit);
	
	input_mt_init_slots(ts->input_dev, ts->max_touch_num);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
#ifdef CONFIG_RK_CONFIG
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, x_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, y_max, 0, 0);	
#else
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->abs_x_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->abs_y_max, 0, 0);	
#endif
	
	
#endif	

	sprintf(ts->phys, "input/ts");
	ts->input_dev->name = s3c_ts_name;
	ts->input_dev->phys = ts->phys;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0xDEAD;
	ts->input_dev->id.product = 0xBEEF;
	ts->input_dev->id.version = 10427;	//screen firmware version
	
	ret = input_register_device(ts->input_dev);
	if (ret) {
		dev_err(&client->dev,"Probe: Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}
	ts->bad_data = 0;

#ifdef INT_PORT	
	ret  = request_irq(gpio_to_irq(client->irq), goodix_ts_irq_handler ,irq_table[ts->int_trigger_type],
			client->name, ts);
	if (ret != 0)
	{
		dev_err(&client->dev,"Cannot allocate ts INT!ERRNO:%d\n", ret);
		gpio_direction_input(client->irq);
		gpio_free(client->irq);
		goto err_init_godix_ts;
	}
	else 
	{	
	#ifndef STOP_IRQ_TYPE
		gt811_irq_disable(ts);     //KT ADD 1202
	//#define
	//	disable_irq(client->irq);
	#endif
		ts->use_irq = 1;
		dev_dbg(&client->dev,"Reques EIRQ %d succesd on GPIO:%d\n",client->irq,client->irq);
	}	
#endif	

	
	if (!ts->use_irq) 
	{
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = goodix_ts_timer_func;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}
	
	if(ts->use_irq)
	#ifndef STOP_IRQ_TYPE
		gt811_irq_enable(ts);     //KT ADD 1202
	//#elif
	//	enable_irq(client->irq);
	#endif
		
	ts->power = goodix_ts_power;

	goodix_read_version(ts);
	
#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;//EARLY_SUSPEND_LEVEL_BLANK_SCREEN +1;
	ts->early_suspend.suspend = goodix_ts_early_suspend;
	ts->early_suspend.resume = goodix_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

/////////////////////////////// UPDATE STEP 2 START /////////////////////////////////////////////////////////////////
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
	goodix_proc_entry = create_proc_entry("goodix-update", 0666, NULL);
	if(goodix_proc_entry == NULL)
	{
		dev_info(&client->dev, "Couldn't create proc entry!\n");
		ret = -ENOMEM;
		goto err_create_proc_entry;
	}
	else
	{
		dev_info(&client->dev, "Create proc entry success!\n");
		goodix_proc_entry->write_proc = goodix_update_write;
		goodix_proc_entry->read_proc = goodix_update_read;
	}
#endif
///////////////////////////////UPDATE STEP 2 END /////////////////////////////////////////////////////////////////
	dev_info(&client->dev,"Start %s in %s mode,Driver Modify Date:2012-01-05\n", 
		ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");
	return 0;

err_init_godix_ts:
	i2c_end_cmd(ts);
	if(ts->use_irq)
	{
		ts->use_irq = 0;
		free_irq(gpio_to_irq(client->irq),ts);
	#ifdef INT_PORT	
		gpio_direction_input(client->irq);
		gpio_free(client->irq);
	#endif	
	}
	else 
		hrtimer_cancel(&ts->timer);

err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
	i2c_set_clientdata(client, NULL);
err_gpio_request:
	gpio_free(reset_pin);
err_i2c_failed:	
	kfree(ts);	
err_alloc_data_failed:
err_check_functionality_failed:
err_create_proc_entry:
	return ret;
}


/*******************************************************	
Function:
	Drive the release of resources
Parameters:
	client: the device structure
return:
	Results of the implementation code, 0 for normal execution
********************************************************/
static int goodix_ts_remove(struct i2c_client *client)
{
	struct gt811_ts_data *ts = i2c_get_clientdata(client);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
/////////////////////////////// UPDATE STEP 3 START/////////////////////////////////////////////////////////////////
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
	remove_proc_entry("goodix-update", NULL);
#endif
/////////////////////////////////UPDATE STEP 3 END///////////////////////////////////////////////////////////////

	if (ts && ts->use_irq) 
	{
	#ifdef INT_PORT
		gpio_direction_input(client->irq);
		gpio_free(client->irq);
	#endif	
		free_irq(gpio_to_irq(client->irq), ts);
	}	
	else if(ts)
		hrtimer_cancel(&ts->timer);
	
	dev_notice(&client->dev,"The driver is removing...\n");
	i2c_set_clientdata(client, NULL);
	input_unregister_device(ts->input_dev);
	kfree(ts);
	return 0;
}

//停用设备
static int goodix_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	struct gt811_ts_data *ts = i2c_get_clientdata(client);
	disable_irq(gpio_to_irq(client->irq));
	if (ts->power) 
	{	
	}
        return 0;
}
static int goodix_ts_resume(struct i2c_client *client)
{
	int ret;
	struct gt811_ts_data *ts = i2c_get_clientdata(client);

	if (ts->power) {
		ret = ts->power(ts, 1);
		printk("resume >>>>>>>>>ret=%d",ret);
		if (ret < 0)
		printk(KERN_ERR "goodix_ts_resume power on failed\n");
	}
 	enable_irq(gpio_to_irq(client->irq));
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h)
{
	struct gt811_ts_data *ts;
	ts = container_of(h, struct gt811_ts_data, early_suspend);
	goodix_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void goodix_ts_late_resume(struct early_suspend *h)
{
	struct gt811_ts_data *ts;
	ts = container_of(h, struct gt811_ts_data, early_suspend);
	goodix_ts_resume(ts->client);
}
#endif
/////////////////////////////// UPDATE STEP 4 START/////////////////////////////////////////////////////////////////
//******************************Begin of firmware update surpport*******************************
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
static struct file * update_file_open(char * path, mm_segment_t * old_fs_p)
{
	struct file * filp = NULL;
	int errno = -1;
		
	filp = filp_open(path, O_RDONLY, 0644);
	
	if(!filp || IS_ERR(filp))
	{
		if(!filp)
			errno = -ENOENT;
		else 
			errno = PTR_ERR(filp);					
		printk(KERN_ERR "The update file for Guitar open error.\n");
		return NULL;
	}
	*old_fs_p = get_fs();
	set_fs(get_ds());

	filp->f_op->llseek(filp,0,0);
	return filp ;
}

static void update_file_close(struct file * filp, mm_segment_t old_fs)
{
	set_fs(old_fs);
	if(filp)
		filp_close(filp, NULL);
}
static int update_get_flen(char * path)
{
	struct file * file_ck = NULL;
	mm_segment_t old_fs;
	int length ;
	
	file_ck = update_file_open(path, &old_fs);
	if(file_ck == NULL)
		return 0;

	length = file_ck->f_op->llseek(file_ck, 0, SEEK_END);
	//printk("File length: %d\n", length);
	if(length < 0)
		length = 0;
	update_file_close(file_ck, old_fs);
	return length;	
}

static int goodix_update_write(struct file *filp, const char __user *buff, unsigned long len, void *data)
{
	unsigned char cmd[120];
	int ret = -1;
        int retry = 0;
	static unsigned char update_path[60];
	struct gt811_ts_data *ts;
	struct file * file_data = NULL;
    	mm_segment_t old_fs;
	unsigned char *file_ptr = NULL;
	unsigned int file_len;
	
	ts = i2c_get_clientdata(i2c_connect_client);
	if(ts==NULL)
	{
            printk(KERN_INFO"goodix write to kernel via proc file!@@@@@@\n");
		return 0;
	}
	
	//printk(KERN_INFO"goodix write to kernel via proc file!@@@@@@\n");
	if(copy_from_user(&cmd, buff, len))
	{
            printk(KERN_INFO"goodix write to kernel via proc file!@@@@@@\n");
		return -EFAULT;
	}
	//printk(KERN_INFO"Write cmd is:%d,write len is:%ld\n",cmd[0], len);
	switch(cmd[0])
	{
            case APK_UPDATE_TP:
            printk(KERN_INFO"Write cmd is:%d,cmd arg is:%s,write len is:%ld\n",cmd[0], &cmd[1], len);
            memset(update_path, 0, 60);
            strncpy(update_path, cmd+1, 60);
			
#ifndef STOP_IRQ_TYPE
		gt811_irq_disable(ts);     //KT ADD 1202
//#elif
//		disable_irq(ts->client->irq);
#endif
	file_data = update_file_open(update_path, &old_fs);
        if(file_data == NULL)   //file_data has been opened at the last time
        {
		dev_info(&ts->client->dev, "cannot open update file\n");
		return 0;
        }

        file_len = update_get_flen(update_path);
	dev_info(&ts->client->dev, "Update file length:%d\n", file_len);
	file_ptr = (unsigned char*)vmalloc(file_len);
	if(file_ptr==NULL)
	{
		dev_info(&ts->client->dev, "cannot malloc memory!\n");
		return 0;
	}	

        ret = file_data->f_op->read(file_data, file_ptr, file_len, &file_data->f_pos);
        if(ret <= 0)
        {
		dev_info(&ts->client->dev, "read file data failed\n");
		return 0;
        }
        update_file_close(file_data, old_fs);	

        ret = gt811_downloader(ts, file_ptr);
        vfree(file_ptr);
	if(ret < 0)
        {
                printk(KERN_INFO"Warnning: GT811 update might be ERROR!\n");
                return 0;
        }
             
 //       i2c_pre_cmd(ts);
	
	gpio_direction_output(reset_pin, 0);
        msleep(5);
        gpio_direction_input(reset_pin);
	msleep(20);
	for(retry=0; retry<3; retry++)
	{
		ret=goodix_init_panel(ts);
		msleep(2);
		if(ret != 0)	//Initiall failed
		{
			dev_info(&ts->client->dev, "Init panel failed!\n");
			continue;
		}
		else
			break;
		
	}

   //     s3c_gpio_cfgpin(INT_PORT, INT_CFG);     //Set IO port function 
	//gpio_direction_input(INT_PORT);
//	s3c_gpio_setpull(INT_PORT, S3C_GPIO_PULL_UP); 
//        s3c_gpio_cfgpin(INT_PORT, INT_CFG);	//Set IO port as interrupt port	
	//s3c_gpio_setpull(INT_PORT, S3C_GPIO_PULL_NONE);
//	while(1);		
#ifndef STOP_IRQ_TYPE
	gt811_irq_enable(ts);     //KT ADD 1202
//#elif
//	enable_irq(ts->client->irq);
#endif   
//        i2c_end_cmd(ts);
        return 1;
    
    case APK_READ_FUN:							//functional command
		if(cmd[1] == CMD_READ_VER)
		{
			printk(KERN_INFO"Read version!\n");
			ts->read_mode = MODE_RD_VER;
		}
        else if(cmd[1] == CMD_READ_CFG)
		{
			printk(KERN_INFO"Read config info!\n");

			ts->read_mode = MODE_RD_CFG;
		}
		else if (cmd[1] == CMD_READ_RAW)
		{
		    printk(KERN_INFO"Read raw data!\n");

			ts->read_mode = MODE_RD_RAW;
		}
        else if (cmd[1] == CMD_READ_CHIP_TYPE)
		{
		    printk(KERN_INFO"Read chip type!\n");

			ts->read_mode = MODE_RD_CHIP_TYPE;
		}
        return 1;
        
    case APK_WRITE_CFG:			
		printk(KERN_INFO"Begin write config info!Config length:%d\n",cmd[1]);
		i2c_pre_cmd(ts);
        ret = i2c_write_bytes(ts->client, cmd+2, cmd[1]+2); 
        i2c_end_cmd(ts);
        if(ret != 1)
        {
            printk("Write Config failed!return:%d\n",ret);
            return -1;
        }
        return 1;
            
    default:
	    return 0;
	}
	return 0;
}

static int goodix_update_read( char *page, char **start, off_t off, int count, int *eof, void *data )
{
	int ret = -1;
    	int len = 0;
    	int read_times = 0;
	struct gt811_ts_data *ts;

	unsigned char read_data[360] = {80, };

	ts = i2c_get_clientdata(i2c_connect_client);
	if(ts==NULL)
		return 0;
    
       	printk("___READ__\n");
	if(ts->read_mode == MODE_RD_VER)		//read version data
	{
		i2c_pre_cmd(ts);
		ret = goodix_read_version(ts);
             	i2c_end_cmd(ts);
		if(ret < 0)
		{
			printk(KERN_INFO"Read version data failed!\n");
			return 0;
		}
        
             	read_data[1] = (char)(ts->version&0xff);
             	read_data[0] = (char)((ts->version>>8)&0xff);

		memcpy(page, read_data, 2);
		//*eof = 1;
		return 2;
	}
    else if (ts->read_mode == MODE_RD_CHIP_TYPE)
    {
        page[0] = GT811;
        return 1;
    }
    else if(ts->read_mode == MODE_RD_CFG)
	{

            read_data[0] = 0x06;
            read_data[1] = 0xa2;       // cfg start address
            printk("read config addr is:%x,%x\n", read_data[0],read_data[1]);

	     len = 106;
           i2c_pre_cmd(ts);
	     ret = i2c_read_bytes(ts->client, read_data, len+2);
            i2c_end_cmd(ts);
            if(ret <= 0)
		{
			printk(KERN_INFO"Read config info failed!\n");
			return 0;
		}
              
		memcpy(page, read_data+2, len);
		return len;
	}
	else if (ts->read_mode == MODE_RD_RAW)
	{
#define TIMEOUT (-100)
	    int retry = 0;
        if (raw_data_ready != RAW_DATA_READY)
        {
            raw_data_ready = RAW_DATA_ACTIVE;
        }

RETRY:
        read_data[0] = 0x07;
        read_data[1] = 0x11;
        read_data[2] = 0x01;
        
        ret = i2c_write_bytes(ts->client, read_data, 3);
        
#ifdef DEBUG
        sum += read_times;
        printk("count :%d\n", ++access_count);
        printk("A total of try times:%d\n", sum);
#endif
               
        read_times = 0;
	    while (RAW_DATA_READY != raw_data_ready)
	    {
	        msleep(4);

	        if (read_times++ > 10)
	        {
    	        if (retry++ > 5)
    	        {
    	            return TIMEOUT;
    	        }
                goto RETRY;
	        }
	    }
#ifdef DEBUG	    
        printk("read times:%d\n", read_times);
#endif	    
        read_data[0] = 0x08;
        read_data[1] = 0x80;       // raw data address
        
	    len = 160;

	   // msleep(4);

        i2c_pre_cmd(ts);
	    ret = i2c_read_bytes(ts->client, read_data, len+2);    	    
  //      i2c_end_cmd(ts);
        
        if(ret <= 0)
		{
			printk(KERN_INFO"Read raw data failed!\n");
			return 0;
		}
		memcpy(page, read_data+2, len);

		read_data[0] = 0x09;
        read_data[1] = 0xC0;
	//	i2c_pre_cmd(ts);
	    ret = i2c_read_bytes(ts->client, read_data, len+2);    	    
        i2c_end_cmd(ts);
        
        if(ret <= 0)
		{
			printk(KERN_INFO"Read raw data failed!\n");
			return 0;
		}
		memcpy(&page[160], read_data+2, len);

#ifdef DEBUG
//**************
        for (i = 0; i < 300; i++)
        {
            printk("%6x", page[i]);

            if ((i+1) % 10 == 0)
            {
                printk("\n");
            }
        }
//********************/  
#endif
        raw_data_ready = RAW_DATA_NON_ACTIVE;
    
		return (2*len);   
		
    }
	return 0;
#endif
}             
//********************************************************************************************
static u8  is_equal( u8 *src , u8 *dst , int len )
{
    int i;

#if 0    
    for( i = 0 ; i < len ; i++ )
    {
        printk(KERN_INFO"[%02X:%02X]", src[i], dst[i]);
       if((i+1)%10==0)printk("\n");
    }
#endif

    for( i = 0 ; i < len ; i++ )
    {
        if ( src[i] != dst[i] )
        {
            return 0;
        }
    }
    
    return 1;
}

static  u8 gt811_nvram_store( struct gt811_ts_data *ts )
{
    int ret;
    int i;
    u8 inbuf[3] = {REG_NVRCS_H,REG_NVRCS_L,0};
    //u8 outbuf[3] = {};
    ret = i2c_read_bytes( ts->client, inbuf, 3 );
    
    if ( ret < 0 )
    {
        return 0;
    }
    
    if ( ( inbuf[2] & BIT_NVRAM_LOCK ) == BIT_NVRAM_LOCK )
    {
        return 0;
    }
    
    inbuf[2] = (1<<BIT_NVRAM_STROE);		//store command
	    
    for ( i = 0 ; i < 300 ; i++ )
    {
        ret = i2c_write_bytes( ts->client, inbuf, 3 );
        
        if ( ret < 0 )
            break;
    }
    
    return ret;
}

static u8  gt811_nvram_recall( struct gt811_ts_data *ts )
{
    int ret;
    u8 inbuf[3] = {REG_NVRCS_H,REG_NVRCS_L,0};
    
    ret = i2c_read_bytes( ts->client, inbuf, 3 );
    
    if ( ret < 0 )
    {
        return 0;
    }
    
    if ( ( inbuf[2]&BIT_NVRAM_LOCK) == BIT_NVRAM_LOCK )
    {
        return 0;
    }
    
    inbuf[2] = ( 1 << BIT_NVRAM_RECALL );		//recall command
    ret = i2c_write_bytes( ts->client , inbuf, 3);
    return ret;
}

static  int gt811_reset( struct gt811_ts_data *ts )
{
    int ret = 1;
    u8 retry;
    
    unsigned char outbuf[3] = {0,0xff,0};
    unsigned char inbuf[3] = {0,0xff,0};
    //outbuf[1] = 1;

    gpio_direction_output(reset_pin,0);
    msleep(20);
    gpio_direction_input(reset_pin);
    msleep(100);
    for(retry=0;retry < 80; retry++)
    {
        ret =i2c_write_bytes(ts->client, inbuf, 0);	//Test I2C connection.
        if (ret > 0)
        {
            msleep(10);
            ret =i2c_read_bytes(ts->client, inbuf, 3);	//Test I2C connection.
            if (ret > 0)
            {
                if(inbuf[2] == 0x55)
                {
			ret =i2c_write_bytes(ts->client, outbuf, 3);
			msleep(10);
			break;						
		}
	    }			
	}
	else
	{
		gpio_direction_output(reset_pin,0);
		msleep(20);
		gpio_direction_input(reset_pin);
		msleep(20);
		dev_info(&ts->client->dev, "i2c address failed\n");
	}	
		
    }
    dev_info(&ts->client->dev, "Detect address %0X\n", ts->client->addr);
    //msleep(500);
    return ret;	
}

static  int gt811_reset2( struct gt811_ts_data *ts )
{
    int ret = 1;
    u8 retry;
    
    //unsigned char outbuf[3] = {0,0xff,0};
    unsigned char inbuf[3] = {0,0xff,0};
    //outbuf[1] = 1;

    gpio_direction_output(reset_pin,0);
    msleep(20);
    gpio_direction_input(reset_pin);
    msleep(100);
    for(retry=0;retry < 80; retry++)
    {
        ret =i2c_write_bytes(ts->client, inbuf, 0);	//Test I2C connection.
        if (ret > 0)
        {
            msleep(10);
            ret =i2c_read_bytes(ts->client, inbuf, 3);	//Test I2C connection.
            if (ret > 0)
            {
             //   if(inbuf[2] == 0x55)
             //       {
		//	    ret =i2c_write_bytes(ts->client, outbuf, 3);
		//	    msleep(10);
			    break;						
	//		}
				}			
			}	
		
		}
    dev_info(&ts->client->dev, "Detect address %0X\n", ts->client->addr);
    //msleep(500);
    return ret;	
}
static  int gt811_set_address_2( struct gt811_ts_data *ts )
{
    unsigned char inbuf[3] = {0,0,0};
    int i;

    for ( i = 0 ; i < 12 ; i++ )
    {
        if ( i2c_read_bytes( ts->client, inbuf, 3) )
        {
            dev_info(&ts->client->dev, "Got response\n");
            return 1;
        }
        dev_info(&ts->client->dev, "wait for retry\n");
        msleep(50);
    } 
    return 0;
}
static u8  gt811_update_firmware( u8 *nvram, u16 start_addr, u16 length, struct gt811_ts_data *ts)
{
    u8 ret,err,retry_time,i;
    u16 cur_code_addr;
    u16 cur_frame_num, total_frame_num, cur_frame_len;
    u32 gt80x_update_rate;

    unsigned char i2c_data_buf[PACK_SIZE+2] = {0,};
    unsigned char i2c_chk_data_buf[PACK_SIZE+2] = {0,};
    
    if( length > NVRAM_LEN - NVRAM_BOOT_SECTOR_LEN )
    {
        dev_info(&ts->client->dev, "Fw length %d is bigger than limited length %d\n", length, NVRAM_LEN - NVRAM_BOOT_SECTOR_LEN );
        return 0;
    }
    	
    total_frame_num = ( length + PACK_SIZE - 1) / PACK_SIZE;  

    //gt80x_update_sta = _UPDATING;
    gt80x_update_rate = 0;

    for( cur_frame_num = 0 ; cur_frame_num < total_frame_num ; cur_frame_num++ )	  
    {
        retry_time = 5;
       
	dev_info(&ts->client->dev, "PACK[%d]\n",cur_frame_num); 
        cur_code_addr = /*NVRAM_UPDATE_START_ADDR*/start_addr + cur_frame_num * PACK_SIZE; 	
        i2c_data_buf[0] = (cur_code_addr>>8)&0xff;
        i2c_data_buf[1] = cur_code_addr&0xff;
        
        i2c_chk_data_buf[0] = i2c_data_buf[0];
        i2c_chk_data_buf[1] = i2c_data_buf[1];
        
        if( cur_frame_num == total_frame_num - 1 )
        {
            cur_frame_len = length - cur_frame_num * PACK_SIZE;
        }
        else
        {
            cur_frame_len = PACK_SIZE;
        }
        
        //strncpy(&i2c_data_buf[2], &nvram[cur_frame_num*PACK_SIZE], cur_frame_len);
        for(i=0;i<cur_frame_len;i++)
        {
            i2c_data_buf[2+i] = nvram[cur_frame_num*PACK_SIZE+i];
        }
        do
        {
            err = 0;

            //ret = gt811_i2c_write( guitar_i2c_address, cur_code_addr, &nvram[cur_frame_num*I2C_FRAME_MAX_LENGTH], cur_frame_len );		
	    ret = i2c_write_bytes(ts->client, i2c_data_buf, (cur_frame_len+2));
            if ( ret <= 0 )
            {
                dev_info(&ts->client->dev, "write fail\n");
                err = 1;
            }
            
            ret = i2c_read_bytes(ts->client, i2c_chk_data_buf, (cur_frame_len+2));
            // ret = gt811_i2c_read( guitar_i2c_address, cur_code_addr, inbuf, cur_frame_len);
            if ( ret <= 0 )
            {
                dev_info(&ts->client->dev, "read fail\n");
                err = 1;
            }
	    
            if( is_equal( &i2c_data_buf[2], &i2c_chk_data_buf[2], cur_frame_len ) == 0 )
            {
                dev_info(&ts->client->dev, "not equal\n");
                err = 1;
            }
			
        } while ( err == 1 && (--retry_time) > 0 );
        
        if( err == 1 )
        {
            break;
        }
		
        gt80x_update_rate = ( cur_frame_num + 1 )*128/total_frame_num;
    
    }

    if( err == 1 )
    {
        dev_info(&ts->client->dev, "write nvram fail\n");
        return 0;
    }
    
    ret = gt811_nvram_store(ts);
    
    msleep( 20 );

    if( ret == 0 )
    {
        dev_info(&ts->client->dev, "nvram store fail\n");
        return 0;
    }
    
    ret = gt811_nvram_recall(ts);

    msleep( 20 );
    
    if( ret == 0 )
    {
        dev_info(&ts->client->dev, "nvram recall fail\n");
        return 0;
    }

    for ( cur_frame_num = 0 ; cur_frame_num < total_frame_num ; cur_frame_num++ )		 //	read out all the code
    {

        cur_code_addr = NVRAM_UPDATE_START_ADDR + cur_frame_num*PACK_SIZE;
        retry_time=5;
        i2c_chk_data_buf[0] = (cur_code_addr>>8)&0xff;
        i2c_chk_data_buf[1] = cur_code_addr&0xff;
        
        
        if ( cur_frame_num == total_frame_num-1 )
        {
            cur_frame_len = length - cur_frame_num*PACK_SIZE;
        }
        else
        {
            cur_frame_len = PACK_SIZE;
        }
        
        do
        {
            err = 0;
            //ret = gt811_i2c_read( guitar_i2c_address, cur_code_addr, inbuf, cur_frame_len);
            ret = i2c_read_bytes(ts->client, i2c_chk_data_buf, (cur_frame_len+2));

            if ( ret == 0 )
            {
                err = 1;
            }
            
            if( is_equal( &nvram[cur_frame_num*PACK_SIZE], &i2c_chk_data_buf[2], cur_frame_len ) == 0 )
            {
                err = 1;
            }
        } while ( err == 1 && (--retry_time) > 0 );
        
        if( err == 1 )
        {
            break;
        }
        
        gt80x_update_rate = 127 + ( cur_frame_num + 1 )*128/total_frame_num;
    }
    
    gt80x_update_rate = 255;
    //gt80x_update_sta = _UPDATECHKCODE;

    if( err == 1 )
    {
        dev_info(&ts->client->dev, "nvram validate fail\n");
        return 0;
    }
    
    return 1;
}

static u8  gt811_update_proc( u8 *nvram, u16 start_addr , u16 length, struct gt811_ts_data *ts )
{
    u8 ret;
    u8 error = 0;
    //struct tpd_info_t tpd_info;
    GT811_SET_INT_PIN( ts->client->irq, 0 );
    msleep( 20 );
    ret = gt811_reset(ts);
    if ( ret < 0 )
    {
        error = 1;
        dev_info(&ts->client->dev, "reset fail\n");
        goto end;
    }

    ret = gt811_set_address_2( ts );
    if ( ret == 0 )
    {
        error = 1;
        dev_info(&ts->client->dev, "set address fail\n");
        goto end;
    }

    ret = gt811_update_firmware( nvram, start_addr, length, ts);
    if ( ret == 0 )
    {
        error=1;
       	dev_info(&ts->client->dev, "firmware update fail\n");
        goto end;
    }

end:
    GT811_SET_INT_PIN( ts->client->irq, 1 );
//    gpio_free(INT_PORT);
    gpio_pull_updown(ts->client->irq, 0);
    
    msleep( 500 );
    ret = gt811_reset2(ts);
    if ( ret < 0 )
    {
        error=1;
        dev_info(&ts->client->dev, "final reset fail\n");
        goto end;
    }
    if ( error == 1 )
    {
        return 0; 
    }
	
//    i2c_pre_cmd(ts);
    while(goodix_read_version(ts)<0);
    
//    i2c_end_cmd(ts);
    return 1;
}

u16 Little2BigEndian(u16 little_endian)
{
	u16 temp = 0;
	temp = little_endian&0xff;
	return (temp<<8)+((little_endian>>8)&0xff);
}

int  gt811_downloader( struct gt811_ts_data *ts,  unsigned char * data)
{
    struct tpd_firmware_info_t *fw_info = (struct tpd_firmware_info_t *)data;
    //int i;
    //unsigned short checksum = 0;
    //unsigned int  checksum = 0;
    unsigned int  fw_checksum = 0;
    //unsigned char fw_chip_type;
    unsigned short fw_version;
    unsigned short fw_start_addr;
    unsigned short fw_length;
    unsigned char *data_ptr;
    //unsigned char *file_ptr = &(fw_info->chip_type);
    int retry = 0,ret;
    int err = 0;
    unsigned char rd_buf[4] = {0};
    unsigned char *mandatory_base = "GOODIX";
    unsigned char rd_rom_version;
    unsigned char rd_chip_type;
    unsigned char rd_nvram_flag;

    //struct file * file_data = NULL;
    //mm_segment_t old_fs;
    //unsigned int rd_len;
    //unsigned int file_len = 0;
    //unsigned char i2c_data_buf[PACK_SIZE] = {0,};
    
    rd_buf[0]=0x14;
    rd_buf[1]=0x00;
    rd_buf[2]=0x80;
    ret = i2c_write_bytes(ts->client, rd_buf, 3);
    if(ret<0)
    {
            dev_info(&ts->client->dev, "i2c write failed\n");
            goto exit_downloader;
    }
    rd_buf[0]=0x40;
    rd_buf[1]=0x11;
    ret = i2c_read_bytes(ts->client, rd_buf, 3);
    if(ret<=0)
    {
            dev_info(&ts->client->dev, "i2c request failed!\n");
            goto exit_downloader;
    }
    rd_chip_type = rd_buf[2];
    rd_buf[0]=0xFB;
    rd_buf[1]=0xED;
    ret = i2c_read_bytes(ts->client, rd_buf, 3);
    if(ret<=0)
    {
            dev_info(&ts->client->dev, "i2c read failed!\n");
            goto exit_downloader;
    }
    rd_rom_version = rd_buf[2];
    rd_buf[0]=0x06;
    rd_buf[1]=0x94;
    ret = i2c_read_bytes(ts->client, rd_buf, 3);
    if(ret<=0)
    {
            dev_info(&ts->client->dev, "i2c read failed!\n");
            goto exit_downloader;
    }
    rd_nvram_flag = rd_buf[2];

    fw_version = Little2BigEndian(fw_info->version);
    fw_start_addr = Little2BigEndian(fw_info->start_addr);
    fw_length = Little2BigEndian(fw_info->length);	
    data_ptr = &(fw_info->data);	

    dev_info(&ts->client->dev,"chip_type=0x%02x\n", fw_info->chip_type);
    dev_info(&ts->client->dev,"version=0x%04x\n", fw_version);
    dev_info(&ts->client->dev,"rom_version=0x%02x\n",fw_info->rom_version);
    dev_info(&ts->client->dev,"start_addr=0x%04x\n",fw_start_addr);
    dev_info(&ts->client->dev,"file_size=0x%04x\n",fw_length);
    fw_checksum = ((u32)fw_info->checksum[0]<<16) + ((u32)fw_info->checksum[1]<<8) + ((u32)fw_info->checksum[2]);
    dev_info(&ts->client->dev,"fw_checksum=0x%06x\n",fw_checksum);
    dev_info(&ts->client->dev,"%s\n", __func__ );
    dev_info(&ts->client->dev,"current version 0x%04X, target verion 0x%04X\n", ts->version, fw_version );

//chk_chip_type:
    if(rd_chip_type!=fw_info->chip_type)
    {
	dev_info(&ts->client->dev, "Chip type not match,exit downloader\n");
	goto exit_downloader;
    }
	
//chk_mask_version:	
    if(!rd_rom_version)
    {
 	if(fw_info->rom_version!=0x45)
	{
		dev_info(&ts->client->dev, "Rom version not match,exit downloader\n");
		goto exit_downloader;
	}
	dev_info(&ts->client->dev, "Rom version E.\n");
	goto chk_fw_version;
    }
    else if(rd_rom_version!=fw_info->rom_version);
    {
	dev_info(&ts->client->dev, "Rom version not match,exidownloader\n");
	goto exit_downloader;
    }
    dev_info(&ts->client->dev, "Rom version %c\n",rd_rom_version);

//chk_nvram:	
    if(rd_nvram_flag==0x55)
    {
	dev_info(&ts->client->dev, "NVRAM correct!\n");
	goto chk_fw_version;
    }
    else if(rd_nvram_flag==0xAA)
    {
	dev_info(&ts->client->dev, "NVRAM incorrect!Need update.\n");
	goto begin_upgrade;
    }
    else
    {
	dev_info(&ts->client->dev, "NVRAM other error![0x694]=0x%02x\n", rd_nvram_flag);
	goto begin_upgrade;
    }
chk_fw_version:
//	ts->version -= 1;               //test by andrew        
    if( ts->version >= fw_version )   // current low byte higher than back-up low byte
    {
            dev_info(&ts->client->dev, "Fw verison not match.\n");
            goto chk_mandatory_upgrade;
    }
    dev_info(&ts->client->dev,"Need to upgrade\n");
    goto begin_upgrade;
chk_mandatory_upgrade:
//	dev_info(&ts->client->dev, "%s\n", mandatory_base);
//	dev_info(&ts->client->dev, "%s\n", fw_info->mandatory_flag);
    ret = memcmp(mandatory_base, fw_info->mandatory_flag, 6);
    if(ret)
    {
 	dev_info(&ts->client->dev,"Not meet mandatory upgrade,exit downloader!ret:%d\n", ret);
	goto exit_downloader;
    }
    dev_info(&ts->client->dev, "Mandatory upgrade!\n");
begin_upgrade:
    dev_info(&ts->client->dev, "Begin upgrade!\n");
 //   goto exit_downloader;
    dev_info(&ts->client->dev,"STEP_0:\n");

   
    dev_info(&ts->client->dev, "STEP_1:\n");
    err = -1;
    while( retry < 3 ) 
    {
//        ret = gt811_update_proc( data_ptr,fw_start_addr, fw_length, ts);
        if(ret == 1)
        {
            err = 1;
            break;
        }
        retry++;
    }
    
exit_downloader:
    //mt_set_gpio_mode(GPIO_CTP_EINT_PIN, GPIO_CTP_EINT_PIN_M_EINT);
   // mt_set_gpio_out(GPIO_CTP_EN_PIN, GPIO_OUT_ONE);
       // gpio_direction_output(INT_PORT,1);
       // msleep(1);
    gpio_free(ts->client->irq);
    gpio_pull_updown(ts->client->irq, 0);
    return err;

}
//******************************End of firmware update surpport*******************************
/////////////////////////////// UPDATE STEP 4 END /////////////////////////////////////////////////////////////////

//可用于该驱动�?设备名—设备ID 列表
//only one client
static const struct i2c_device_id goodix_ts_id[] = {
	{ GOODIX_I2C_NAME, 0 },
	{ }
};

//设备驱动结构�?
static struct i2c_driver goodix_ts_driver = {
	.probe		= goodix_ts_probe,
	.remove		= goodix_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= goodix_ts_suspend,
	.resume		= goodix_ts_resume,
#endif
	.id_table	= goodix_ts_id,
	.driver = {
		.name	= GOODIX_I2C_NAME,
		.owner = THIS_MODULE,
	},
};

/*******************************************************	
功能�?	驱动加载函数
return�?	执行结果码，0表示正常执行
********************************************************/
static int __devinit goodix_ts_init(void)
{
	int ret;
	
#ifdef CONFIG_RK_CONFIG
        ret = tp_board_init();

        if(ret < 0)
                return ret;
#endif
	goodix_wq = create_workqueue("goodix_wq");		//create a work queue and worker thread
	if (!goodix_wq) {
		printk(KERN_ALERT "creat workqueue faiked\n");
		return -ENOMEM;
		
	}
	ret=i2c_add_driver(&goodix_ts_driver);
	return ret; 
}

/*******************************************************	
功能�?	驱动卸载函数
参数�?	client：设备结构体
********************************************************/
static void __exit goodix_ts_exit(void)
{
	printk(KERN_ALERT "Touchscreen driver of guitar exited.\n");
	i2c_del_driver(&goodix_ts_driver);
	if (goodix_wq)
		destroy_workqueue(goodix_wq);		//release our work queue
}

late_initcall_sync(goodix_ts_init); 				//最后初始化驱动felix
module_exit(goodix_ts_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Driver");
MODULE_LICENSE("GPL");
               

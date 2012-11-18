/* drivers/input/ts/chips/ct360.c
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
#include "ct360_firmware.h"
	 
static struct i2c_client *this_client;	 
	 
#if 0
#define DBG(x...)  printk(x)
#else
#define DBG(x...)
#endif


#define CT360_ID_REG		0x00
#define CT360_DEVID		0x00
#define CT360_DATA_REG		0x00


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


static int ts_firmware(struct ts_private_data *ts)
{
	int i = 0, j = 0;
	unsigned int ver_chk_cnt = 0;
	unsigned int flash_addr = 0;
	unsigned char CheckSum[16];
	unsigned char buf[32];
	char slave_addr = ts->ops->slave_addr;
	int ret = 0;

	ret = ts_bulk_read_normal(ts, 1, buf, 200*1000);
	if (Binary_Data_Ct360[16372] <= buf[0] )
	{
		printk("%s:%s,firmware is new\n",__func__,ts->ops->name);
		return 0;
	}

	ts->ops->slave_addr = 0x7F;
	
	//------------------------------
	// Step1 --> initial BootLoader
	// Note. 0x7F -> 0x00 -> 0xA5 ;
	// MCU goto idle
	//------------------------------
	printk("%s() Set mcu to idle \n", __FUNCTION__);
	buf[0] = 0x00;
	buf[1] = 0xA5;
	ts_bulk_write_normal(ts, 2, buf, 200*1000);
	mdelay(10);
	
	//------------------------------
	// Reset I2C Offset address
	// Note. 0x7F -> 0x00	
	//------------------------------
	printk("%s() Reset i2c offset address \n", __FUNCTION__);
	buf[0] = 0x00;
	ts_bulk_write_normal(ts, 1, buf, 200*1000);
	mdelay(10);
	
	//------------------------------
	// Read I2C Bus status
	//------------------------------
	printk("%s() Read i2c bus status \n", __FUNCTION__);
	ts_bulk_read_normal(ts, 1, buf, 200*1000);
	mdelay(10);									// Delay 1 ms

	// if return "AAH" then going next step
	if (buf[0] != 0xAA)
	{
		printk("%s() i2c bus status: 0x%x \n", __FUNCTION__, buf[0]);
		goto exit;
	}

	//------------------------------
	// Check incomplete flash erase
	//------------------------------
	printk("%s() Flash erase verify \n", __FUNCTION__);
	buf[0] = 0x00;
	buf[1] = 0x99;		// Generate check sum command  -->read flash, set addr
	buf[2] = 0x00;		// define a flash address for CT36x to generate check sum
	buf[3] = 0x00;		//
	buf[4] = 0x08;		// Define a data length for CT36x to generate check sum

	// Write Genertate check sum command to CT36x
	ts_bulk_write_normal(ts, 5, buf, 200*1000);
	mdelay(10);								// Delay 10 ms

	ts_bulk_read_normal(ts, 13, buf, 200*1000);
	mdelay(10);								// Delay 10 ms 

	CheckSum[0] = buf[5];
	CheckSum[1] = buf[6];

	buf[0] = 0x00;
	buf[1] = 0x99;		// Generate check sum command  -->read flash, set addr
	buf[2] = 0x3F;		// define a flash address for CT36x to generate check sum
	buf[3] = 0xE0;		//
	buf[4] = 0x08;		// Define a data length for CT36x to generate check sum
	// Write Genertate check sum command to CT36x
	ts_bulk_write_normal(ts, 5, buf, 200*1000);
	mdelay(10);								// Delay 10 ms

	ts_bulk_read_normal(ts, 13, buf, 200*1000);
	mdelay(10);

	CheckSum[2] = buf[5];
	CheckSum[3] = buf[6];

	if ( (CheckSum[0] ^ CheckSum[2]) == 0xFF && (CheckSum[1] ^ CheckSum[3]) == 0xFF )
		goto FLASH_ERASE;
	
	//------------------------------
	// check valid Vendor ID
	//------------------------------
	printk("%s() Vendor ID Check \n", __FUNCTION__);
	buf[0] = 0x00;
	buf[1] = 0x99;		// Generate check sum command  -->read flash, set addr
	buf[2] = 0x00;		// define a flash address for CT365 to generate check sum
	buf[3] = 0x44;		//
	buf[4] = 0x08;		// Define a data length for CT365 to generate check sum

	// Write Genertate check sum command to CT36x
	ts_bulk_write_normal(ts, 5, buf, 200*1000);
	mdelay(10);								// Delay 10 ms

	ts_bulk_read_normal(ts, 13, buf, 200*1000);
	mdelay(10);								// Delay 10 ms 
	
	// Read check sum and flash data from CT36x
	if ( (buf[5] != 'V') || (buf[9] != 'T') )
		ver_chk_cnt++;

	buf[0] = 0x00;
	buf[1] = 0x99;		// Generate check sum command  -->read flash,set addr
	buf[2] = 0x00;		// define a flash address for CT365 to generate check sum	
	buf[3] = 0xA4;		//
	buf[4] = 0x08;		// Define a data length for CT365 to generate check sum 

	// Write Genertate check sum command to CT365
	ts_bulk_write_normal(ts, 5, buf, 200*1000);
	mdelay(10);								// Delay 10 ms

	ts_bulk_read_normal(ts, 13, buf, 200*1000);
	mdelay(10);								// Delay 10 ms 
	
	if ((buf[5] != 'V') || (buf[9] != 'T'))
		ver_chk_cnt++;

	if ( ver_chk_cnt >= 2 ) {
		printk("%s() Invalid FW Version \n", __FUNCTION__);
		goto exit;
	}

FLASH_ERASE:
	//-----------------------------------------------------
	// Step 2 : Erase 32K flash memory via Mass Erase (33H)  
	// 0x7F --> 0x00 --> 0x33 --> 0x00;
	//-----------------------------------------------------
	printk("%s() Erase flash \n", __FUNCTION__);
	for(i = 0; i < 8; i++ ) {
		buf[0] = 0x00;		// Offset address
		buf[1] = 0x33;		// Mass Erase command
		buf[2] = 0x00 + (i * 8);  
		ts_bulk_write_normal(ts, 3, buf, 200*1000);
		mdelay(120);				// Delay 10 mS

		//------------------------------
		// Reset I2C Offset address
		// Note. 0x7F -> 0x00	
		//------------------------------
		buf[0] = 0x00;
		ts_bulk_write_normal(ts, 1, buf, 200*1000);
		mdelay(120);				// Delay 10 mS

		//------------------------------
		// Read I2C Bus status
		//------------------------------
		ts_bulk_read_normal(ts, 1, buf, 200*1000);
		mdelay(10);							// Delay 1 ms 

		// if return "AAH" then going next step
		if( buf[0] != 0xAA )
		{
			goto exit;
		}
	}

	//----------------------------------------
	// Step3. Host write 128 bytes to CT36x
	// Step4. Host read checksum to verify ;
	// Write/Read for 256 times ( 32k Bytes )
	//----------------------------------------
	printk("%s() flash FW start \n", __FUNCTION__);
	for ( flash_addr = 0; flash_addr < 0x3FFF; flash_addr+=8 ) {
		// Step 3 : write binary data to CT36x
		buf[0] = 0x00;							// Offset address 
		buf[1] = 0x55;							// Flash write command
		buf[2] = (char)(flash_addr  >> 8);			// Flash address [15:8]
		buf[3] = (char)(flash_addr & 0xFF);			// Flash address [7:0]
		buf[4] = 0x08;							// Data Length 

		if( flash_addr == 160 || flash_addr == 168 ) {
			buf[6] = ~Binary_Data_Ct360[flash_addr + 0]; // Binary data 1
			buf[7] = ~Binary_Data_Ct360[flash_addr + 1]; // Binary data 2
			buf[8] = ~Binary_Data_Ct360[flash_addr + 2]; // Binary data 3
			buf[9] = ~Binary_Data_Ct360[flash_addr + 3]; // Binary data 4
			buf[10] = ~Binary_Data_Ct360[flash_addr + 4];	// Binary data 5
			buf[11] = ~Binary_Data_Ct360[flash_addr + 5];	// Binary data 6
			buf[12] = ~Binary_Data_Ct360[flash_addr + 6];	// Binary data 7
			buf[13] = ~Binary_Data_Ct360[flash_addr + 7];	// Binary data 8
		} else {
			buf[6] = Binary_Data_Ct360[flash_addr + 0];			// Binary data 1
			buf[7] = Binary_Data_Ct360[flash_addr + 1];			// Binary data 2
			buf[8] = Binary_Data_Ct360[flash_addr + 2];			// Binary data 3
			buf[9] = Binary_Data_Ct360[flash_addr + 3];			// Binary data 4
			buf[10] = Binary_Data_Ct360[flash_addr + 4];		// Binary data 5
			buf[11] = Binary_Data_Ct360[flash_addr + 5];		// Binary data 6
			buf[12] = Binary_Data_Ct360[flash_addr + 6];		// Binary data 7
			buf[13] = Binary_Data_Ct360[flash_addr + 7];		// Binary data 8
		}
		// Calculate a check sum by Host controller. 
		// Checksum = / (FLASH_ADRH+FLASH_ADRL+LENGTH+
		// Binary_Data_Ct3601+Binary_Data_Ct3602+Binary_Data_Ct3603+Binary_Data_Ct3604+
		// Binary_Data_Ct3605+Binary_Data_Ct3606+Binary_Data_Ct3607+Binary_Data_Ct3608) + 1 
		CheckSum[0] = ~(buf[2] + buf[3] + buf[4] + buf[6] + buf[7] + 
			buf[8] + buf[9] + buf[10] + buf[11] + buf[12] +
			buf[13]) + 1; 

		buf[5] = CheckSum[0];						// Load check sum to I2C Buffer 

		ts_bulk_write_normal(ts, 14, buf, 200*1000);									// Host write I2C_Buf[0?K12] to CT365. 
		mdelay(1);													// 8 Bytes program --> Need 1 ms delay time 

		// Step4. Verify process 
		//printk("%s(flash_addr:0x%04x) Verify FW \n", __FUNCTION__, flash_addr);
		//Step 4 : Force CT365 generate check sum for host to compare data. 
		//Prepare get check sum from CT36x
		buf[0] = 0x00;
		buf[1] = 0x99;							// Generate check sum command
		buf[2] = (char)(flash_addr >> 8);			// define a flash address for NT1100x to generate check sum 
		buf[3] = (char)(flash_addr & 0xFF);		//
		buf[4] = 0x08;							// Define a data length for CT36x to generate check sum 

		ts_bulk_write_normal(ts, 5, buf, 200*1000);									// Write Genertate check sum command to CT365
		mdelay(1);													// Delay 1 ms

		ts_bulk_read_normal(ts, 13, buf, 200*1000);	// Read check sum and flash data from CT365

		// Compare host check sum with CT365 check sum(I2C_Buf[4])
		if ( buf[4] != CheckSum[0] ) {		
			goto exit;
		}
	}

	
	printk("%s() flash FW complete \n", __FUNCTION__);

exit:	
	ts->ops->slave_addr = slave_addr;
	if(ts->ops->active)
		ts->ops->active(ts, 0);
	if(ts->ops->active)
		ts->ops->active(ts, 1);
	return	0;

}


static int ts_init(struct ts_private_data *ts)
{
	int irq_pin = irq_to_gpio(ts->pdata->irq);
	int result = 0;
	int uc_reg_value ;

	
	char loader_buf[3] = {0xfF,0x0f,0x2A};
	
	gpio_direction_output(ts->pdata->reset_pin, GPIO_LOW);
	mdelay(10);
	gpio_direction_output(ts->pdata->reset_pin, GPIO_HIGH);
	msleep(300);

	//init some register
	//to do	
	ts_bulk_write_normal(ts, 3, loader_buf, 200*1000);
	
	return result;
}


static int ts_report_value(struct ts_private_data *ts)
{
	struct ts_platform_data *pdata = ts->pdata;
	struct ts_event *event = &ts->event;
	unsigned char buf[20] = {0};
	int result = 0 , i = 0, off = 0, id = 0;
	int syn_flag = 0;
	
	result = ts_bulk_read(ts, (unsigned short)ts->ops->read_reg, ts->ops->read_len, buf);
	if(result < 0)
	{
		printk("%s:fail to init ts\n",__func__);
		return result;
	}

	//for(i=0; i<ts->ops->read_len; i++)
	//DBG("buf[%d]=0x%x\n",i,buf[i]);

	for(i = 0; i<ts->ops->max_point; i++)
	{
		off = i*4;	
		id = buf[off] >> 4;
		event->point[id].id = id;
		event->point[id].status = buf[off+0] & 0x0f;
		event->point[id].x = (((s16)buf[i+1] << 4)|((s16)buf[i+3] >> 4));
		event->point[id].y = (((s16)buf[i+2] << 4)|((s16)buf[i+3] & 0x0f));
		
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

		if((event->point[id].status == 1) || (event->point[id].status == 2))
		{		
			input_mt_slot(ts->input_dev, event->point[id].id);
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, event->point[id].id);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 1);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, event->point[id].x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, event->point[id].y);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			syn_flag = 1;
			DBG("%s:%s press down,id=%d,x=%d,y=%d\n",__func__,ts->ops->name, event->point[id].id, event->point[id].x,event->point[id].y);
		}
		else if ((event->point[id].status == 3) || (event->point[id].status == 0))
		{				
			input_mt_slot(ts->input_dev, event->point[id].id);				
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, -1);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
			syn_flag = 1;
			DBG("%s:%s press up,id=%d\n",__func__,ts->ops->name, event->point[id].id);
		}

		event->point[id].last_status = event->point[id].status;
		
	}
	
	if(syn_flag)
	{
		syn_flag = 0;
		input_sync(ts->input_dev);
	}

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


struct ts_operate ts_ct360_ops = {
	.name				= "ct360",
	.slave_addr			= 0x01,
	.ts_id				= TS_ID_CT360,			//i2c id number
	.bus_type			= TS_BUS_TYPE_I2C,
	.reg_size			= 1,
	.id_reg				= CT360_ID_REG,
	.id_data			= TS_UNKNOW_DATA,
	.version_reg			= TS_UNKNOW_DATA,
	.version_len			= 0,
	.version_data			= NULL,
	.read_reg			= CT360_DATA_REG,		//read data
	.read_len			= 4*5,				//data length
	.trig				= IRQF_TRIGGER_FALLING,		
	.max_point			= 5,
	.xy_swap 			= 0,
	.x_revert 			= 0,
	.y_revert			= 0,
	.range				= {800,480},
	.irq_enable			= 1,
	.poll_delay_ms			= 0,
	.active				= ts_active,	
	.init				= ts_init,
	.check_irq			= NULL,
	.report 			= ts_report_value,
	.firmware			= ts_firmware,
	.suspend			= ts_suspend,
	.resume				= ts_resume,
};

/****************operate according to ts chip:end************/

//function name should not be changed
static struct ts_operate *ts_get_ops(void)
{
	return &ts_ct360_ops;
}


static int __init ts_ct360_init(void)
{
	struct ts_operate *ops = ts_get_ops();
	int result = 0;
	result = ts_register_slave(NULL, NULL, ts_get_ops);	
	DBG("%s\n",__func__);
	return result;
}

static void __exit ts_ct360_exit(void)
{
	struct ts_operate *ops = ts_get_ops();
	ts_unregister_slave(NULL, NULL, ts_get_ops);
}


subsys_initcall(ts_ct360_init);
module_exit(ts_ct360_exit);


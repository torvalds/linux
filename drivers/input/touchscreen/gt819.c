/* drivers/input/touchscreen/goodix_touch.c
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
#include <linux/irq.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/completion.h>
#include <asm/uaccess.h>
#include <mach/board.h>
#include <linux/reboot.h>

#define GOODIX_I2C_NAME "Goodix-TS"
//define default resolution of the touchscreen
#define GOODIX_MULTI_TOUCH
#define GT819_IIC_SPEED              400*1000    //400*1000
#define TOUCH_MAX_WIDTH              800
#define TOUCH_MAX_HEIGHT             480
#define TOUCH_MAJOR_MAX              200
#define WIDTH_MAJOR_MAX              200
#define MAX_POINT                    5
#define INT_TRIGGER_EDGE_RISING      0
#define INT_TRIGGER_EDGE_FALLING     1
#define INT_TRIGGER_EDGE_LOW         2
#define INT_TRIGGER_EDGE_HIGH        3
#define INT_TRIGGER                  INT_TRIGGER_EDGE_FALLING
#define I2C_DELAY                    0x0f

#define PACK_SIZE                    64					//update file package size
#define MAX_TIMEOUT                  60000				//update time out conut
#define MAX_I2C_RETRIES	             20					//i2c retry times

//I2C buf address
#define ADDR_CMD                     80
#define ADDR_STA                     81
#define ADDR_DAT                     0
//moudle state
#define NEW_UPDATE_START			0x01
#define UPDATE_START				0x02
#define SLAVE_READY					0x08
#define UNKNOWN_ERROR				0x00
#define FRAME_ERROR					0x10
#define CHECKSUM_ERROR				0x20
#define TRANSLATE_ERROR				0x40
#define FLASH_ERROR					0X80
//error no
#define ERROR_NO_FILE				2	//ENOENT
#define ERROR_FILE_READ				23	//ENFILE
#define ERROR_FILE_TYPE				21	//EISDIR
#define ERROR_GPIO_REQUEST			4	//EINTR
#define ERROR_I2C_TRANSFER			5	//EIO
#define ERROR_NO_RESPONSE			16	//EBUSY
#define ERROR_TIMEOUT				110	//ETIMEDOUT

struct goodix_ts_data {
	struct workqueue_struct *goodix_wq;
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct work_struct  work;
	int irq;
	int irq_gpio;
	uint16_t abs_x_max;
	uint16_t abs_y_max;
	uint8_t max_touch_num;
	uint8_t int_trigger_type;
};

static const char *goodix_ts_name = "Goodix Capacitive TouchScreen";
unsigned int crc32_table[256];
unsigned int oldcrc32 = 0xFFFFFFFF;
unsigned int ulPolynomial = 0x04c11db7;
struct i2c_client * i2c_connect_client = NULL;
static struct early_suspend gt819_power;
static u8 gt819_fw[]=
{
#include "gt819_fw.i"
};
#if 0
uint8_t config_info[] = {
0x02,(TOUCH_MAX_WIDTH>>8),(TOUCH_MAX_WIDTH&0xff),
(TOUCH_MAX_HEIGHT>>8),(TOUCH_MAX_HEIGHT&0xff),MAX_POINT,(0xa0 | INT_TRIGGER),
0x20,0x00,0x00,0x0f,0x20,0x08,0x14,0x00,
0x00,0x20,0x00,0x00,0x88,0x88,0x88,0x00,0x37,0x00,0x00,0x00,0x01,0x02,0x03,0x04,
0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0xff,0xff,0x00,0x01,0x02,0x03,0x04,
0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0xff,0xff,0xff,0x00,0x00,0x3c,0x64,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40
};
#else
static u8 config_info[]=
{
#include "gt819.cfg"
};
#endif
static int gt819_read_regs(struct i2c_client *client, u8 reg, u8 buf[], unsigned len)
{
	int ret;
	ret = i2c_master_reg8_recv(client, reg, buf, len, GT819_IIC_SPEED);
	if(ret>0)
		return ret; 
	ret = i2c_master_reg8_recv(client, reg, buf, len, GT819_IIC_SPEED);
	if(ret>0)
		return ret; 
	ret = i2c_master_reg8_recv(client, reg, buf, len, GT819_IIC_SPEED);
	if(ret>0)
		return ret; 
	ret = i2c_master_reg8_recv(client, reg, buf, len, GT819_IIC_SPEED);
	if(ret>0)
		return ret; 
	ret = i2c_master_reg8_recv(client, reg, buf, len, GT819_IIC_SPEED);
	return ret;
}


static int gt819_set_regs(struct i2c_client *client, u8 reg, u8 const buf[], unsigned short len)
{
	int ret; 
	ret = i2c_master_reg8_send(client, reg, buf, (int)len, GT819_IIC_SPEED);
	if(ret>0)
		return ret; 
	ret = i2c_master_reg8_send(client, reg, buf, (int)len, GT819_IIC_SPEED);
	if(ret>0)
		return ret; 
	ret = i2c_master_reg8_send(client, reg, buf, (int)len, GT819_IIC_SPEED);
	if(ret>0)
		return ret; 
	ret = i2c_master_reg8_send(client, reg, buf, (int)len, GT819_IIC_SPEED);
	if(ret>0)
		return ret; 
	ret = i2c_master_reg8_send(client, reg, buf, (int)len, GT819_IIC_SPEED);
	return ret;
}

int gt819_printf(char *buf, int len)
{
	int x, y, row = len/8,mod = len%8;
	for (y=0; y<row; y++) {
		for (x=0; x<8; x++) {
			printk("0x%02x, ",buf[y*8+x]);
		}
		printk("\n");
	}
	for (x=0; x<mod; x++) {
		printk("0x%02x, ",buf[row*8+x]);
	}
	printk("\n");
	return 0;
}

int gt189_wait_for_slave(struct i2c_client *client, u8 status)
{
	unsigned char i2c_state_buf[2];
	int ret,i = 0;
	while(i < MAX_I2C_RETRIES)
	{
		ret = gt819_read_regs(client,ADDR_STA, i2c_state_buf, 1);
		printk("i2c read state byte:0x%x\n",i2c_state_buf[0]);
		if(ret < 0)
			return ERROR_I2C_TRANSFER;
		if(i2c_state_buf[0]==0xff)continue;
		if(i2c_state_buf[0] & status)
			return i2c_state_buf[0];
		msleep(10);
		i++;
	}
	return -ERROR_TIMEOUT;
}

int gt819_update_write_config(struct i2c_client *client)
	{
		int ret,len = sizeof(config_info)-1;			//byte[0] is the reg addr in the gt819.cfg
		u8 cfg_rd_buf[len];
		u8 cfg_cmd_buf = 0x03;
		u8 retries = 0;
		
	reconfig:	
		ret = gt819_set_regs(client, 101, &config_info[1], len);
		if(ret < 0)
			return ret;
		gt819_printf(config_info, len);
		ret = gt819_read_regs(client, 101, cfg_rd_buf, len);
		if(ret < 0)
			return ret;
		if(memcmp(cfg_rd_buf, &config_info[1], len))
		{	
			dev_info(&client->dev, "config info check error!\n");
			if(retries < 5)
			{
				retries++;
				ret = gt819_set_regs(client, ADDR_CMD, &cfg_cmd_buf, 1);
				if(ret < 0)
					return ret;
				goto reconfig;
			}
			return -1;
		}
		cfg_cmd_buf = 0x04;
		ret = gt819_set_regs(client, ADDR_CMD, &cfg_cmd_buf, 1);
		if(ret < 0)
			return ret;
		return 0;
	}


static int  gt819_read_version(struct i2c_client *client,char *version)
{
	int ret, count = 0;
	char *p;
	
	ret = gt819_read_regs(client,240, version, 16);
	if (ret < 0) 
		return ret;
	version[16]='\0';
	p = version;
	do 					
	{
		if((*p > 122) || (*p < 48 && *p != 32) || (*p >57 && *p  < 65) 
			||(*p > 90 && *p < 97 && *p  != '_'))		//check illeqal character
			count++;
	}while(*++p != '\0' );
	if(count > 2)
		return -1;
	dev_info(&client->dev, "fw version is %s\n",version);
	return ret;
}

int gt819_update_write_fw(struct i2c_client *client, char *fw_buf, int len)
{
	int ret,data_len,i,check_len,frame_checksum,frame_number = 0;
	unsigned char *p,i2c_data_buf[PACK_SIZE+8];
	u8 i2c_rd_buf[PACK_SIZE+8];
	
	u8 retries = 0;
	u8 check_state = 0;
	
	if(!client || !fw_buf)
		return -1;

	while(len){
		frame_checksum = 0;
		retries = 0;
		check_len = (len >= PACK_SIZE) ? PACK_SIZE : len;
		data_len = check_len+8;
		dev_info(&client->dev, "PACK[%d]:prepare data,remained len = %d\n",frame_number,len);
		p = &fw_buf[frame_number*PACK_SIZE];
		for(i=0; i<check_len; i++)
			frame_checksum += *p++;
		frame_checksum = 0 - frame_checksum;
		p = i2c_data_buf;
		*p++ = (frame_number>>24)&0xff;
		*p++ = (frame_number>>16)&0xff;
		*p++ = (frame_number>>8)&0xff;
		*p++ = frame_number&0xff;
		memcpy(p, &fw_buf[frame_number*PACK_SIZE],check_len);
		p += check_len;
		*p++ = frame_checksum&0xff;
		*p++ = (frame_checksum>>8)&0xff;
		*p++ = (frame_checksum>>16)&0xff;
		*p++ = (frame_checksum>>24)&0xff;
		//gt819_printf(i2c_data_buf, data_len);
		dev_info(&client->dev, "PACK[%d]:write to slave\n",frame_number);
resend:
		ret = gt819_set_regs(client,ADDR_DAT, i2c_data_buf, data_len);
		if(ret < 0)
			return ret;
		//gt819_printf(i2c_data_buf, data_len);
		msleep(10);
		dev_info(&client->dev, "PACK[%d]:read data\n",frame_number);
		memset(i2c_rd_buf, 0, sizeof(i2c_rd_buf));
		ret = gt819_read_regs(client,ADDR_DAT, i2c_rd_buf, data_len);
		if(ret < 0)
			return ret;
		//gt819_printf(i2c_data_buf, data_len);
		msleep(10);
		dev_info(&client->dev, "PACK[%d]:check data\n",frame_number);
		if(memcmp(&i2c_rd_buf[4],&fw_buf[frame_number*PACK_SIZE],check_len))
		{
            dev_info(&client->dev, "PACK[%d]:File Data Frame readback check Error!\n",frame_number);
		    i2c_rd_buf[0] = 0x03;
			ret = gt819_set_regs(client, ADDR_CMD, i2c_rd_buf, 1);
			if(ret < 0)
			    return ret;
			check_state = 0x01;
		}
		else
		{
    		dev_info(&client->dev, "PACK[%d]:tell slave check data pass\n",frame_number);
    		i2c_rd_buf[0] = 0x04;
    		ret = gt819_set_regs(client,ADDR_CMD, i2c_rd_buf, 1);
    		if(ret < 0)
    			return ret;
    		dev_info(&client->dev, "PACK[%d]:wait for slave to start next frame\n",frame_number);
		}
		
		ret = gt189_wait_for_slave(client, SLAVE_READY);
		if((ret & CHECKSUM_ERROR) || (ret & FRAME_ERROR) || (ret == ERROR_I2C_TRANSFER) || (ret < 0) || (check_state == 0x01))
		{
			
			if(((ret & CHECKSUM_ERROR) || (ret & FRAME_ERROR) || (check_state == 0x01))&&(retries < 5))
			{
				if(check_state != 0x01)
				{
				    printk("checksum error or miss frame error!\n");
				}
				check_state = 0x00;
				retries++;
				msleep(20);
				goto resend;
			}
			printk("wait slave return state:%d\n", ret);
			return ret;
		}
		dev_info(&client->dev, "PACK[%d]:frame transfer finished\n",frame_number);
		if(len < PACK_SIZE)
			return 0;
		frame_number++;
		len -= check_len;
	}
	return 0;
}

int gt819_update_fw(struct i2c_client *client)
{
	int ret,file_len,update_need_config;
	unsigned char i2c_control_buf[10];
	char version[17];
	const char version_base[17]={"GT81XNI"};
	
	dev_info(&client->dev, "gt819 firmware update start...\n");
	dev_info(&client->dev, "step 1:read version...\n");
	ret = gt819_read_version(client,version);
	if (ret < 0) 
		return ret;
	dev_info(&client->dev, "done!\n");
	dev_info(&client->dev, "step 2:disable irq...\n");
	disable_irq(client->irq);
	dev_info(&client->dev, "done!\n");
	dev_info(&client->dev, "step 3:set update start...\n");
	i2c_control_buf[0] = UPDATE_START;
	ret = gt819_set_regs(client,ADDR_CMD, i2c_control_buf, 1);
	if(ret < 0)
		return ret;
	//the time include time(APROM -> LDROM) and time(LDROM init)
	msleep(1000);
	dev_info(&client->dev, "done!\n");
	dev_info(&client->dev, "step 4:wait for slave start...\n");
	ret = gt189_wait_for_slave(client, UPDATE_START);
	if(ret < 0)
		return ret;
	if(!(ret & UPDATE_START))
		return -1;
	if(!(ret & NEW_UPDATE_START))
		update_need_config = 1;
	dev_info(&client->dev, "done!\n");
	dev_info(&client->dev, "step 5:write the fw length...\n");
	file_len = sizeof(gt819_fw) + 4;
	dev_info(&client->dev, "file length is:%d\n", file_len);
	i2c_control_buf[0] = (file_len>>24) & 0xff;
	i2c_control_buf[1] = (file_len>>16) & 0xff;
	i2c_control_buf[2] = (file_len>>8) & 0xff;
	i2c_control_buf[3] = file_len & 0xff;
	ret = gt819_set_regs(client,ADDR_DAT, i2c_control_buf, 4);
	if(ret < 0)
		return ret;
	dev_info(&client->dev, "done!\n");
	dev_info(&client->dev, "step 6:wait for slave ready\n");
	ret = gt189_wait_for_slave(client, SLAVE_READY);
	if(ret < 0)
		return ret;
	dev_info(&client->dev, "done!\n");
	dev_info(&client->dev, "step 7:write data\n");
	ret = gt819_update_write_fw(client, gt819_fw, sizeof(gt819_fw));
	if(ret < 0)
		return ret;
	dev_info(&client->dev, "done!\n");
	dev_info(&client->dev, "step 8:write config\n");
	ret = gt819_update_write_config(client);
	if(ret < 0)
		return ret;
	dev_info(&client->dev, "done!\n");
	dev_info(&client->dev, "step 9:wait for slave ready\n");
	ret = gt189_wait_for_slave(client,SLAVE_READY);
	if(ret < 0)
		return ret;
	if(ret & SLAVE_READY)
		dev_info(&client->dev, "The firmware updating succeed!update state:0x%x\n",ret);
	dev_info(&client->dev, "step 10:enable irq...\n");
	enable_irq(client->irq);
	dev_info(&client->dev, "done!\n");
	msleep(1000);						//wait slave reset
	dev_info(&client->dev, "step 11:read version...\n");
	ret = gt819_read_version(client,version);
	if (ret < 0) 
		return ret;
	dev_info(&client->dev, "done!\n");
	version[7] = '\0';
	if(strcmp(version ,version_base)==0)
	{
		sys_sync();
		msleep(200);
		kernel_restart(NULL);
	}
	return 0;
}


static void gt819_queue_work(struct work_struct *work)
{
	struct goodix_ts_data *ts = container_of(work, struct goodix_ts_data, work);
	uint8_t  point_data[53]={ 0 };
	int ret,i,offset,points;
	int points_chect;
	int x,y,w;
	unsigned int  count = 0;
	uint8_t  check_sum = 0;
	
	ret = gt819_read_regs(ts->client,1, point_data, 2);
	if (ret < 0) {
		dev_err(&ts->client->dev, "i2c_read_bytes fail:%d!\n",ret);
		enable_irq(ts->irq);
		return;
	}
	check_sum =point_data[0]+point_data[1];
	
	points = point_data[0] & 0x1f;
	//dev_info(&ts->client->dev, "points = %d\n",points);
	if (points == 0) {
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
		//input_mt_sync(data->input_dev);
		input_sync(ts->input_dev);
		enable_irq(ts->irq);
		dev_info(&ts->client->dev, "touch release\n");
		return; 
	}	
	for(i=0;0!=points;)
	{
	if(points&0x01)
		i++;
	points>>=1;
	}
	
	points = i;
	points_chect = points;
	ret = gt819_read_regs(ts->client,3, point_data, points*5+1);
	if (ret < 0) {
		dev_err(&ts->client->dev, "i2c_read_bytes fail:%d!\n",ret);
		enable_irq(ts->irq);
		return;
	}
	//add by Nitiion
	for(points_chect *= 5; points_chect > 0; points_chect--)
		{
		check_sum += point_data[count++];
		}
		check_sum += point_data[count];
	if(check_sum  != 0)			//checksum verify error
		{
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
			//input_mt_sync(data->input_dev);
			input_sync(ts->input_dev);
			enable_irq(ts->irq);
			dev_info(&ts->client->dev, "coor checksum error!touch release\n");
			return;
		}
		
	for(i=0;i<points;i++){
		offset = i*5;
		x = (((s16)(point_data[offset+0]))<<8) | ((s16)point_data[offset+1]);
		y = (((s16)(point_data[offset+2]))<<8) | ((s16)point_data[offset+3]);
		w = point_data[offset+4];
		//dev_info(&ts->client->dev, "goodix multiple report event[%d]:x = %d,y = %d,w = %d\n",i,x,y,w);
		if(x<=TOUCH_MAX_WIDTH && y<=TOUCH_MAX_HEIGHT){
			//dev_info(&ts->client->dev, "goodix multiple report event[%d]:x = %d,y = %d,w = %d\n",i,x,y,w);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, w);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X,  x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y,  y);
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, i);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, w);
			input_mt_sync(ts->input_dev);
		}
	}
	input_sync(ts->input_dev);
	enable_irq(ts->irq);
	return;
}

/*******************************************************
Description:
	External interrupt service routine.

Parameter:
	irq:	interrupt number.
	dev_id: private data pointer.
	
return:
	irq execute status.
*******************************************************/
static irqreturn_t gt819_irq_handler(int irq, void *dev_id)
{
	struct goodix_ts_data *ts = dev_id;

	disable_irq_nosync(ts->client->irq);
	queue_work(ts->goodix_wq, &ts->work);
	return IRQ_HANDLED;
}

static int gt819_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct goodix_platform_data *pdata = client->dev.platform_data;
	dev_info(&client->dev,"gt819_suspend\n");

	if (pdata->platform_sleep)                              
		pdata->platform_sleep();
	disable_irq(client->irq);
	return 0;
}

static int gt819_resume(struct i2c_client *client)
{
	struct goodix_platform_data *pdata = client->dev.platform_data;
	dev_info(&client->dev,"gt819_resume\n");

	enable_irq(client->irq);
	if (pdata->platform_wakeup)                              
		pdata->platform_wakeup();
	return 0;
}

static void gt819_early_suspend(struct early_suspend *h)
{
	dev_info(&i2c_connect_client->dev, "gt819_early_suspend!\n");
	gt819_suspend(i2c_connect_client,PMSG_SUSPEND);
}

static void gt819_early_resume(struct early_suspend *h)
{
	dev_info(&i2c_connect_client->dev, "gt819_resume_early!\n");
	gt819_resume(i2c_connect_client);
}

/*******************************************************
Description:
	Goodix touchscreen driver release function.

Parameter:
	client:	i2c device struct.
	
return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int gt819_remove(struct i2c_client *client)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(client);
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
	remove_proc_entry("goodix-update", NULL);
#endif
	//goodix_debug_sysfs_deinit();
	gpio_direction_input(ts->irq_gpio);
	gpio_free(ts->irq_gpio);
	free_irq(client->irq, ts);
	if(ts->goodix_wq)
		destroy_workqueue(ts->goodix_wq); 
	dev_notice(&client->dev,"The driver is removing...\n");
	i2c_set_clientdata(client, NULL);
	input_unregister_device(ts->input_dev);
    unregister_early_suspend(&gt819_power);
	i2c_connect_client = 0;
	kfree(ts);
	return 0;
}

static int gt819_init_panel(struct goodix_ts_data *ts)
{
	int ret,I2cDelay;
	int len = sizeof(config_info)-1;
	uint8_t rd_cfg_buf[10];
	struct goodix_platform_data *pdata = ts->client->dev.platform_data;

	ret = gt819_set_regs(ts->client, 101, &config_info[1], len);
	if(ret < 0)
	{
		pdata->platform_sleep();
		msleep(10);
		pdata->platform_wakeup();
		msleep(100);
		printk("First IIC request failed,retry!\n");
		ret = gt819_set_regs(ts->client, 101, &config_info[1], len);
		if(ret<0)
		return ret;
	}

	ret = gt819_read_regs(ts->client, 101, rd_cfg_buf, 10);
	if (ret < 0)
		return ret;
	ts->abs_x_max = ((((uint16_t)rd_cfg_buf[1])<<8)|rd_cfg_buf[2]);
	ts->abs_y_max = ((((uint16_t)rd_cfg_buf[3])<<8)|rd_cfg_buf[4]);
	ts->max_touch_num = rd_cfg_buf[5];
	ts->int_trigger_type = rd_cfg_buf[6]&0x03;
	I2cDelay = rd_cfg_buf[9]&0x0f;
	dev_info(&ts->client->dev,"X_MAX = %d,Y_MAX = %d,MAX_TOUCH_NUM = %d,INT_TRIGGER = %d,I2cDelay = %x\n",
		ts->abs_x_max,ts->abs_y_max,ts->max_touch_num,ts->int_trigger_type,I2cDelay);
	if((ts->abs_x_max!=TOUCH_MAX_WIDTH)||(ts->abs_y_max!=TOUCH_MAX_HEIGHT)||
		(MAX_POINT!=ts->max_touch_num)||INT_TRIGGER!=ts->int_trigger_type || I2C_DELAY!=I2cDelay){
		ts->abs_x_max = TOUCH_MAX_WIDTH;
		ts->abs_y_max = TOUCH_MAX_HEIGHT;
		ts->max_touch_num = MAX_POINT;
		ts->int_trigger_type = INT_TRIGGER;
		rd_cfg_buf[1] = ts->abs_x_max>>8;
		rd_cfg_buf[2] = ts->abs_x_max&0xff;
		rd_cfg_buf[3] = ts->abs_y_max>>8;
		rd_cfg_buf[4] = ts->abs_y_max&0xff;
		rd_cfg_buf[5] = ts->max_touch_num;
		rd_cfg_buf[6] = ((rd_cfg_buf[6]&0xfc) | INT_TRIGGER);
		rd_cfg_buf[9] = ((rd_cfg_buf[9]&0xf0) | I2C_DELAY);
		ret = gt819_set_regs(ts->client, 101, rd_cfg_buf, 10);
		if (ret < 0)
			return ret;
		dev_info(&ts->client->dev,"set config\n");
	}
	return 0;
}

/*******************************************************
Description:
	Goodix touchscreen probe function.

Parameter:
	client:	i2c device struct.
	id:device id.
	
return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int gt819_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	char version[17];
	char version_base[17]={"GT81XNI_1R05_18Q"};
	struct goodix_ts_data *ts;
	struct goodix_platform_data *pdata = client->dev.platform_data;
	const char irq_table[4] = {IRQ_TYPE_EDGE_RISING,
							   IRQ_TYPE_EDGE_FALLING,
							   IRQ_TYPE_LEVEL_LOW,
							   IRQ_TYPE_LEVEL_HIGH};

	dev_info(&client->dev,"Install goodix touch driver\n");

	if (!pdata) {
		dev_err(&client->dev, "platform data is required!\n");
		return -EINVAL;
	}
	
	if (pdata->init_platform_hw)
		pdata->init_platform_hw();

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		return -ENODEV;
	}
	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		return -ENOMEM;
	}
	
	ts->client = i2c_connect_client = client;
	
	ret = gt819_init_panel(ts);
	if(ret != 0){
	  dev_err(&client->dev,"init panel fail,ret = %d\n",ret);
	  goto err_init_panel_fail;
	}

	ret = gt819_read_version(client,version);	
	if((ret>=0) && (strcmp(version ,version_base)!=0)){
		gt819_update_fw(client);
	}

	if (!client->irq){
		dev_err(&client->dev,"no irq fail\n");
		ret = -ENODEV;
		goto err_no_irq_fail;
	}
	ts->irq_gpio = client->irq;
	ts->irq = client->irq = gpio_to_irq(client->irq);
	ret  = request_irq(client->irq, gt819_irq_handler, irq_table[ts->int_trigger_type],client->name, ts);
	if (ret != 0) {
		dev_err(&client->dev,"request_irq fail:%d\n", ret);
		goto err_irq_request_fail;
	}
	
	ts->goodix_wq = create_workqueue("goodix_wq");
	if (!ts->goodix_wq) {
		printk(KERN_ALERT "creat workqueue failed\n");
		ret = -ENOMEM;
		goto err_create_work_queue_fail;
	}

	INIT_WORK(&ts->work, gt819_queue_work);

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		dev_err(&client->dev,"Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}
	
	set_bit(ABS_MT_POSITION_X, ts->input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, ts->input_dev->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, ts->input_dev->absbit);
	set_bit(ABS_MT_TRACKING_ID, ts->input_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, ts->input_dev->absbit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
	input_set_abs_params(ts->input_dev,ABS_MT_POSITION_X, 0, TOUCH_MAX_WIDTH, 0, 0);
	input_set_abs_params(ts->input_dev,ABS_MT_POSITION_Y, 0, TOUCH_MAX_HEIGHT, 0, 0);
	input_set_abs_params(ts->input_dev,ABS_MT_TOUCH_MAJOR, 0, TOUCH_MAJOR_MAX, 0, 0);
	input_set_abs_params(ts->input_dev,ABS_MT_TRACKING_ID, 0, MAX_POINT, 0, 0);
	input_set_abs_params(ts->input_dev,ABS_MT_WIDTH_MAJOR, 0, WIDTH_MAJOR_MAX, 0, 0);

	ts->input_dev->name = goodix_ts_name;
	ret = input_register_device(ts->input_dev);
	if (ret) {
		dev_err(&client->dev,"Probe: Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}
	i2c_set_clientdata(client, ts);
	
	gt819_power.suspend = gt819_early_suspend;
	gt819_power.resume = gt819_early_resume;
	gt819_power.level = 0x2;
	register_early_suspend(&gt819_power);
	return 0;
	i2c_set_clientdata(client, NULL);
	input_unregister_device(ts->input_dev);
err_input_register_device_failed:
	input_free_device(ts->input_dev);
err_input_dev_alloc_failed:
	destroy_workqueue(ts->goodix_wq); 
err_create_work_queue_fail:
	free_irq(client->irq,ts);
err_irq_request_fail:
err_no_irq_fail:
err_init_panel_fail:
	if (pdata->exit_platform_hw)
		pdata->exit_platform_hw();
	kfree(ts);
	return ret;
}



static const struct i2c_device_id gt819_id[] = {
	{ GOODIX_I2C_NAME, 0 },
	{ }
};

static struct i2c_driver gt819_driver = {
	.probe		= gt819_probe,
	.remove		= gt819_remove,
	.id_table	= gt819_id,
	.driver = {
		.name	= GOODIX_I2C_NAME,
		.owner = THIS_MODULE,
	},
};

/*******************************************************	
Description:
	Driver Install function.
return:
	Executive Outcomes. 0---succeed.
********************************************************/
static int __devinit gt819_init(void)
{
	int ret;
	
	ret=i2c_add_driver(&gt819_driver);
	return ret; 
}

/*******************************************************	
Description:
	Driver uninstall function.
return:
	Executive Outcomes. 0---succeed.
********************************************************/
static void __exit gt819_exit(void)
{
	printk(KERN_ALERT "Touchscreen driver of guitar exited.\n");
	i2c_del_driver(&gt819_driver);
}

late_initcall(gt819_init);
module_exit(gt819_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Driver");
MODULE_LICENSE("GPL");

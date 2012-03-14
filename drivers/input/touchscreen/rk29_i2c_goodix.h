/*
 * include/linux/goodix_touch.h
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

#ifndef 	_LINUX_GOODIX_TOUCH_H
#define		_LINUX_GOODIX_TOUCH_H

#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>

//*************************TouchScreen Work Part*****************************

#define GOODIX_I2C_NAME "Goodix-TS"
#define TS_MAX_X 	CONFIG_TOUCH_MAX_X
#define TS_MAX_Y	CONFIG_TOUCH_MAX_Y

#if 1
#define INT_PORT		RK29_PIN0_PA2	    						
#ifdef INT_PORT
	#define TS_INT 		gpio_to_irq(INT_PORT)			
//	#define INT_CFG    	S3C_GPIO_SFN(2)					//IO configer as EINT
#else
	#define TS_INT	0
#endif	

//whether need send cfg?
//#define DRIVER_SEND_CFG

//set trigger mode
#define INT_TRIGGER			0

#endif

#define POLL_TIME		10	//actual query spacing interval:POLL_TIME+6

#define GOODIX_MULTI_TOUCH
#ifdef GOODIX_MULTI_TOUCH
	#define MAX_FINGER_NUM	 10
#else
	#define MAX_FINGER_NUM	 1
#endif

//#define swap(x, y) do { typeof(x) z = x; x = y; y = z; } while (0)

struct rk_touch_info
{
	u32 press;  
	u32 x ;
	u32 y ;
	int status ; // 0 1
} ;
struct rk_ts_data{
	struct i2c_client *client;
	struct input_dev *input_dev;
	int irq;
	int irq_pin;
	int pwr_pin;
	int rst_pin;
	int read_mode;					//read moudle mode,20110221 by andrew
	struct hrtimer timer;
	struct workqueue_struct *ts_wq;
	struct delayed_work  ts_work;
	char phys[32];
	struct early_suspend early_suspend;
	int (*power)(struct rk_ts_data * ts, int on);
	int (*ts_init)(struct rk_ts_data*ts);
	int (*input_parms_init)(struct rk_ts_data *ts);
	void (*get_touch_info)(struct rk_ts_data *ts,char *point_num,struct rk_touch_info *info_buf);  //get touch data info
	uint16_t abs_x_max;
	uint16_t abs_y_max;
	uint8_t max_touch_num;
	bool		pendown;
};



struct goodix_ts_data {
	uint16_t addr;
	uint8_t bad_data;
	struct i2c_client *client;
	struct input_dev *input_dev;
	int use_reset;		//use RESET flag
	int use_irq;		//use EINT flag
	int read_mode;		//read moudle mode,20110221 by andrew
	struct hrtimer timer;
	struct delayed_work  work;
	char phys[32];
	int retry;
	struct early_suspend early_suspend;
	int (*power)(struct goodix_ts_data * ts, int on);
	uint16_t abs_x_max;
	uint16_t abs_y_max;
	uint8_t max_touch_num;
	uint8_t int_trigger_type;
	bool		pendown;
};

static const char *rk_ts_name = "Goodix Capacitive TouchScreen";
static struct workqueue_struct *goodix_wq;
struct i2c_client * i2c_connect_client = NULL; 
static struct proc_dir_entry *goodix_proc_entry;
//static struct kobject *goodix_debug_kobj;
	

#define READ_COOR_ADDR 0x01

//*****************************End of Part I *********************************

//*************************Touchkey Surpport Part*****************************
//#define HAVE_TOUCH_KEY
#ifdef HAVE_TOUCH_KEY
	#define READ_COOR_ADDR 0x00
	const uint16_t touch_key_array[]={
									  KEY_MENU,				//MENU
									  KEY_HOME,				//HOME
									  KEY_SEND				//CALL
									 }; 
	#define MAX_KEY_NUM	 (sizeof(touch_key_array)/sizeof(touch_key_array[0]))
#else
	#define READ_COOR_ADDR 0x01
#endif
//*****************************End of Part II*********************************
#if 1
//*************************Firmware Update part*******************************
#define CONFIG_TOUCHSCREEN_GOODIX_IAP
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
#define UPDATE_NEW_PROTOCOL

unsigned int oldcrc32 = 0xFFFFFFFF;
unsigned int crc32_table[256];
unsigned int ulPolynomial = 0x04c11db7;
unsigned char rd_cfg_addr;
unsigned char rd_cfg_len;
unsigned char g_enter_isp = 0;

static int goodix_update_write(struct file *filp, const char __user *buff, unsigned long len, void *data);
static int goodix_update_read( char *page, char **start, off_t off, int count, int *eof, void *data );

#define PACK_SIZE 					64					//update file package size
#define MAX_TIMEOUT					60000				//update time out conut
#define MAX_I2C_RETRIES				20					//i2c retry times

//I2C buf address
#define ADDR_CMD					80
#define ADDR_STA					81
#ifdef UPDATE_NEW_PROTOCOL
	#define ADDR_DAT				0
#else
	#define ADDR_DAT				82
#endif

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

//update steps
#define STEP_SET_PATH              1
#define STEP_CHECK_FILE            2
#define STEP_WRITE_SYN             3
#define STEP_WAIT_SYN              4
#define STEP_WRITE_LENGTH          5
#define STEP_WAIT_READY            6
#define STEP_WRITE_DATA            7
#define STEP_READ_STATUS           8
#define FUN_CLR_VAL                9
#define FUN_CMD                    10
#define FUN_WRITE_CONFIG           11

//fun cmd
#define CMD_DISABLE_TP             0
#define CMD_ENABLE_TP              1
#define CMD_READ_VER               2
#define CMD_READ_RAW               3
#define CMD_READ_DIF               4
#define CMD_READ_CFG               5
#define CMD_SYS_REBOOT             101

//read mode
#define MODE_RD_VER                1
#define MODE_RD_RAW                2
#define MODE_RD_DIF                3
#define MODE_RD_CFG                4


#endif
//*****************************End of Part III********************************
#endif
struct goodix_i2c_platform_data {
	uint32_t version;	/* Use this entry for panels with */
	//reservation
};

#endif /* _LINUX_GOODIX_TOUCH_H */

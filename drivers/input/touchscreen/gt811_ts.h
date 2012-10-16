/* drivers/input/touchscreen/gt811.h
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

#ifndef 	_LINUX_GT811_H
#define		_LINUX_GT811_H

#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>

//*************************TouchScreen Work Part*****************************
#define GOODIX_I2C_NAME "gt811_ts"
#define GT801_PLUS
#define GT801_NUVOTON
#define GUITAR_UPDATE_STATE 0x02

//define resolution of the touchscreen
#define TOUCH_MAX_HEIGHT 	800			
#define TOUCH_MAX_WIDTH		480
//#define STOP_IRQ_TYPE                     // if define then   no stop irq in irq_handle   kuuga add 1202S
#define REFRESH 0     //0~0x64   Scan rate = 10000/(100+REFRESH)//define resolution of the LCD

#define SHUTDOWN_PORT 	     RK2928_PIN3_PC3 
#define INT_PORT 	         RK2928_PIN3_PC7  


#ifdef INT_PORT
	#define TS_INT 		        gpio_to_irq(INT_PORT)			//Interrupt Number,EINT18(119)
//	#define INT_CFG    	      S3C_GPIO_SFN(3) 					//IO configer as EINT
#else
	#define TS_INT	0
#endif	

/////////////////////////////// UPDATE STEP 5 START /////////////////////////////////////////////////////////////////
#define TPD_CHIP_VERSION_C_FIRMWARE_BASE 0x5A
#define TPD_CHIP_VERSION_D1_FIRMWARE_BASE 0x7A
#define TPD_CHIP_VERSION_E_FIRMWARE_BASE 0x9A
#define TPD_CHIP_VERSION_D2_FIRMWARE_BASE 0xBA


/////////////////////////////// UPDATE STEP 5 END /////////////////////////////////////////////////////////////////

#define FLAG_UP		0
#define FLAG_DOWN		1
//set GT801 PLUS trigger mode,只能设置0或1 
//#define INT_TRIGGER		1	   // 1=rising 0=falling
#define POLL_TIME		10	//actual query spacing interval:POLL_TIME+6

#define GOODIX_MULTI_TOUCH
#ifdef GOODIX_MULTI_TOUCH
	#define MAX_FINGER_NUM	5	
#else
	#define MAX_FINGER_NUM	1	
#endif

#if defined(INT_PORT)
	#if MAX_FINGER_NUM <= 3
	#define READ_BYTES_NUM 2+2+MAX_FINGER_NUM*5
	#elif MAX_FINGER_NUM == 4
	#define READ_BYTES_NUM 2+28
	#elif MAX_FINGER_NUM == 5
	#define READ_BYTES_NUM 2+34
	#endif
#else	
	#define READ_BYTES_NUM 2+34
#endif

#define swap(x, y) do { typeof(x) z = x; x = y; y = z; } while (0)

#define READ_TOUCH_ADDR_H 0x07
#define READ_TOUCH_ADDR_L 0x21				//GT811 0x721
#define READ_KEY_ADDR_H 0x07
#define READ_KEY_ADDR_L 0x21
#define READ_COOR_ADDR_H 0x07
#define READ_COOR_ADDR_L 0x22
#define READ_ID_ADDR_H 0x00
#define READ_ID_ADDR_L 0xff
//****************************升级模块参数******************************************

//******************************************************************************
struct gt811_ts_data {
	uint16_t addr;
	uint8_t bad_data;
	struct i2c_client *client;
	struct input_dev *input_dev;
	int use_reset;		//use RESET flag
	int use_irq;		//use EINT flag
	int read_mode;		//read moudle mode,20110221 by andrew
	struct hrtimer timer;
	struct work_struct  work;
	char phys[32];
	int retry;
	int irq;
	spinlock_t				irq_lock;      //add by kuuga
	int 				 irq_is_disable; /* 0: irq enable */ //add by kuuga
	uint16_t abs_x_max;
	uint16_t abs_y_max;
	uint8_t max_touch_num;
	uint8_t int_trigger_type;
	uint8_t btn_state;                    // key states
/////////////////////////////// UPDATE STEP 6 START /////////////////////////////////////////////////////////////////
       unsigned int version;
/////////////////////////////// UPDATE STEP 6 END /////////////////////////////////////////////////////////////////

	struct early_suspend early_suspend;
	int (*power)(struct gt811_ts_data * ts, int on);
};

//*****************************End of Part I *********************************

//*************************Touchkey Surpport Part*****************************
/*#define HAVE_TOUCH_KEY
//#define READ_KEY_VALUE
//#define READ_KEY_COOR

#ifdef HAVE_TOUCH_KEY
	const uint16_t toucher_key_array[]={
									  KEY_MENU,				//MENU
									  KEY_HOME,
									  KEY_BACK,				
									  KEY_SEARCH		
									 }; 
	#define MAX_KEY_NUM	 (sizeof(toucher_key_array)/sizeof(toucher_key_array[0]))
#endif
*/
//#define COOR_TO_KEY
    #ifdef COOR_TO_KEY

    #define KEY_X       40
    #define KEY_Y       20
    #if 0
    #define AREA_X      0
    #else
    #define AREA_Y      800
    #endif

    enum {x, y};
    s32 key_center[MAX_KEY_NUM][2] = {
		
	{48,840},{124,840},{208,840},{282,840}
	
                           };

    #endif 

//*****************************End of Part II*********************************

/////////////////////////////// UPDATE STEP 7 START /////////////////////////////////////////////////////////////////
//*************************Firmware Update part*******************************
//#define AUTO_UPDATE_GT811

#define CONFIG_TOUCHSCREEN_GOODIX_IAP        
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
static int goodix_update_write(struct file *filp, const char __user *buff, unsigned long len, void *data);
static int goodix_update_read( char *page, char **start, off_t off, int count, int *eof, void *data );

#define PACK_SIZE 					64					//update file package size
//#define MAX_TIMEOUT					30000				//update time out conut
//#define MAX_I2C_RETRIES				10					//i2c retry times

//write cmd
#define APK_UPDATE_TP               1
#define APK_READ_FUN                 10
#define APK_WRITE_CFG               11

//fun cmd
//#define CMD_DISABLE_TP             0
//#define CMD_ENABLE_TP              1
#define CMD_READ_VER               2
#define CMD_READ_RAW               3
#define CMD_READ_DIF               4
#define CMD_READ_CFG               5
#define CMD_READ_CHIP_TYPE         6
//#define CMD_SYS_REBOOT             101

//read mode
#define MODE_RD_VER                1
#define MODE_RD_RAW                2
#define MODE_RD_DIF                3
#define MODE_RD_CFG                4
#define MODE_RD_CHIP_TYPE          5

#if 0
struct tpd_firmware_info_t
{
    int magic_number_1;
    int magic_number_2;
    unsigned short version;
    unsigned short length;    
    unsigned short checksum;
    unsigned char data;
};
#else
#pragma pack(1)
struct tpd_firmware_info_t
{
	unsigned char  chip_type;
	unsigned short version;
	unsigned char  rom_version;
	unsigned char  reserved[3];
	unsigned short start_addr;
	unsigned short length;
	unsigned char  checksum[3];
	unsigned char  mandatory_flag[6];
	unsigned char  data;	
};
#pragma pack()
#endif

#define  NVRAM_LEN               0x0FF0   //	nvram total space
#define  NVRAM_BOOT_SECTOR_LEN	 0x0100	// boot sector 
#define  NVRAM_UPDATE_START_ADDR 0x4100

#define  BIT_NVRAM_STROE	    0
#define  BIT_NVRAM_RECALL	    1
#define BIT_NVRAM_LOCK 2
#define  REG_NVRCS_H 0X12
#define  REG_NVRCS_L 0X01
#define GT811_SET_INT_PIN( level ) gpio_direction_output(INT_PORT, level) //null macro now
#endif
//*****************************End of Part III********************************
/////////////////////////////// UPDATE STEP 7 END /////////////////////////////////////////////////////////////////

struct gt811_platform_data {
	uint32_t version;	/* Use this entry for panels with */
    u16     model;          /* 801. */
    bool    swap_xy;        /* swap x and y axes */
    u16     x_min, x_max;
    u16     y_min, y_max;
    bool    x_reverse, y_reverse;
    int     (*get_pendown_state)(void);
    int     (*init_platform_hw)(void);
    int     (*platform_sleep)(void);
    int     (*platform_wakeup)(void);
    void    (*exit_platform_hw)(void);
    int     gpio_reset;
    bool    gpio_reset_active_low;
    

    //reservation
};

#define RAW_DATA_READY          1
#define RAW_DATA_NON_ACTIVE     0xffffffff
#define RAW_DATA_ACTIVE         0


enum CHIP_TYPE
{
    GT800 = 1,
    GT800PLUS,
    GT800PLUS3,
    GT816,
    GT811,
    GT8105,
    GT8110,
    GT818PLUS
};


#endif /* _LINUX_GOODIX_TOUCH_H */

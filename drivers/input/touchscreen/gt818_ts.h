/* drivers/input/touchscreen/gt818_ts.h
 *
 * Copyright (C) 2011 Rockcip, Inc.
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
 * Author: hhb@rock-chips.com
 * Date: 2011.06.20
 */

#ifndef 	_LINUX_GOODIX_TOUCH_H
#define		_LINUX_GOODIX_TOUCH_H

#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>


//*************************TouchScreen Work Part*****************************
#define GOODIX_I2C_NAME "gt818_ts"
#define GT801_PLUS
#define GT801_NUVOTON
#define GUITAR_UPDATE_STATE 0x02
#define GT818_I2C_SCL 400*1000

//define resolution of the touchscreen
#define TOUCH_MAX_HEIGHT 	7168
#define TOUCH_MAX_WIDTH		5120

//define resolution of the LCD
#define SCREEN_MAX_HEIGHT	800				
#define SCREEN_MAX_WIDTH	480



#define SHUTDOWN_PORT 	pdata->gpio_reset			//SHUTDOWN�ܽź�
#define INT_PORT  		pdata->gpio_pendown

#ifdef INT_PORT
	#define TS_INT 		gpio_to_irq(INT_PORT)			//Interrupt Number
#else
	#define TS_INT	0
#endif	

#define HAVE_TOUCH_KEY


#define FLAG_UP		0
#define FLAG_DOWN	1
//set GT801 PLUS trigger mode,ֻ������0��1 
#define INT_TRIGGER		1
#define POLL_TIME		10	//actual query spacing interval:POLL_TIME+6

#define GOODIX_MULTI_TOUCH
#ifdef GOODIX_MULTI_TOUCH
	#define MAX_FINGER_NUM	2
#else
	#define MAX_FINGER_NUM	1	
#endif

#define READ_TOUCH_ADDR_H 	0x07
#define READ_TOUCH_ADDR_L 	0x12
#define READ_KEY_ADDR_H 	0x07
#define READ_KEY_ADDR_L 	0x21
#define READ_COOR_ADDR_H 	0x07
#define READ_COOR_ADDR_L 	0x22
#define READ_ID_ADDR_H 		0x00
#define READ_ID_ADDR_L 		0xff


#define IOMUX_NAME_SIZE 48
struct gt818_platform_data {

	u16		model;			/* 818. */
	bool	swap_xy;		/* swap x and y axes */
	u16		x_min, x_max;
	u16		y_min, y_max;
    int 	gpio_reset;
    int     gpio_reset_active_low;
	int		gpio_pendown;		/* the GPIO used to decide the pendown */

	char	pendown_iomux_name[IOMUX_NAME_SIZE];
	char	resetpin_iomux_name[IOMUX_NAME_SIZE];
	int		pendown_iomux_mode;
	int		resetpin_iomux_mode;

	int	    (*get_pendown_state)(void);
};


struct gt818_ts_data {


	u16 addr;
	u8 bad_data;
	struct i2c_client *client;
	struct input_dev *input_dev;
	int use_reset;		//use RESET flag
	int use_irq;		//use EINT flag
	int read_mode;		//read moudle mode,20110221 by andrew
	struct hrtimer timer;
	struct work_struct  work;
	char phys[32];
	char name[32];
	int retry;
	struct early_suspend early_suspend;
	int (*power)(struct gt818_ts_data * ts, int on);
};



#endif /* _LINUX_GOODIX_TOUCH_H */

/*
 * include/linux/goodix_touch.h
 *
 * Copyright (C) 2008 Goodix, Inc.
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

#ifndef 	_LINUX_GOODIX_TOUCH_H
#define		_LINUX_GOODIX_TOUCH_H


#include <linux/i2c.h>
#include <linux/input.h>

#define GOODIX_I2C_NAME "Goodix-TS"
#define GUITAR_GT80X
//触摸屏的分辨率
#define TOUCH_MAX_HEIGHT 	7680	
#define TOUCH_MAX_WIDTH	 	5120
//显示屏的分辨率，根据具体平台更改，与触摸屏映射坐标相关



#define SHUTDOWN_PORT                ()
#define INT_PORT                     (SW_INT_IRQNO_PIO)

#define GOODIX_MULTI_TOUCH
#ifndef GOODIX_MULTI_TOUCH
	#define MAX_FINGER_NUM 5
#else
	#define MAX_FINGER_NUM 5				//最大支持手指数(<=5)
#endif
#if defined(INT_PORT)
	#if MAX_FINGER_NUM <= 3
	#define READ_BYTES_NUM 1+2+MAX_FINGER_NUM*5
	#elif MAX_FINGER_NUM == 4
	#define READ_BYTES_NUM 1+28
	#elif MAX_FINGER_NUM == 5
	#define READ_BYTES_NUM 1+34
	#endif
#else	
	#define READ_BYTES_NUM 1+34
#endif

//#define swap(x, y) do { typeof(x) z = x; x = y; y = z; } while (0)

enum finger_state {
#define FLAG_MASK 0x01
	FLAG_UP = 0,
	FLAG_DOWN = 1,
	FLAG_INVALID = 2,
};


struct point_node
{
	uint8_t id;
	//uint8_t retry;
	enum finger_state state;
	uint8_t pressure;
	unsigned int x;
	unsigned int y;
};
struct ts_event {
	u16	x1;
	u16	y1;
	u16	x2;
	u16	y2;
	u16	x3;
	u16	y3;
	u16	x4;
	u16	y4;
	u16	x5;
	u16	y5;
	u16	pressure;
    u8  touch_point;
};

/* Notice: This definition used by platform_data.
 * It should be move this struct info to platform head file such as plat/ts.h.
 * If not used in client, it will be NULL in function of goodix_ts_probe. 
 */ 
struct goodix_i2c_platform_data {
	uint32_t gpio_irq;			//IRQ port, use macro such as "gpio_to_irq" to get Interrupt Number.
	uint32_t irq_cfg;			//IRQ port config, must refer to master's Datasheet.
	uint32_t gpio_shutdown;		        //Shutdown port number
	uint32_t shutdown_cfg;		        //Shutdown port config
	uint32_t screen_width;		        //screen width
	uint32_t screen_height;		        //screen height
}; 

#endif /* _LINUX_GOODIX_TOUCH_H */

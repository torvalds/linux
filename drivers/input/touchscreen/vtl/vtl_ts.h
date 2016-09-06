/*
 * VTL CTP driver
 *
 * Copyright (C) 2016 Freescale Semiconductor, Inc
 *
 * Using code from:
 *  - github.com/qdk0901/q98_source:drivers/input/touchscreen/vtl/vtl_ts.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#ifndef _TS_CORE_H_
#define _TS_CORE_H_

#include <linux/gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif


#ifdef TS_DEBUG
#define DEBUG() pr_debug("___%s___\n", __func__)
#else
#define DEBUG()
#endif


/* platform define */
#define COMMON    0x01 /* Samsung,Freescale,Amlogic,actions */
#define ROCKCHIP  0X02
#define ALLWINER  0X03
#define MTK       0X04

/* vtl touch IC define */
#define CT36X     0x01
#define CT360     0x02

/* xy data protocol */
#define OLD_PROTOCOL    0x01
#define NEW_PROTOCOL    0x02


/* vtl ts driver config */

/*platform config*/
#define PLATFORM COMMON

/*vtl ts driver name*/
#define DRIVER_NAME "vtl_ts"

/*vtl chip ID*/
#define CHIP_ID CT36X

#define XY_DATA_PROTOCOL NEW_PROTOCOL


/* maybe not use,please refer to the function
 * vtl_ts_config() in the file "vtl_ts.c"
 */
#define SCREEN_MAX_X    1024
#define SCREEN_MAX_y    600

#define TS_IRQ_GPIO_NUM           /* RK30_PIN4_PC2 */
#define TS_RST_GPIO_NUM           /* RK30_PIN4_PD0 */
#define TS_I2C_SPEED    400000    /* for rockchip  */


/* priate define and declare */
#if (CHIP_ID == CT360)
#define TOUCH_POINT_NUM    1
#elif (CHIP_ID == CT36X)
#define TOUCH_POINT_NUM    1
#endif


#if (CHIP_ID == CT360)
struct xy_data {
#if (XY_DATA_PROTOCOL == OLD_PROTOCOL)
	unsigned char status:4;	/* Action information, 1:Down;
				   2: Move; 3: Up */
	unsigned char id:4;	/* ID information, from 1 to
				   CFG_MAX_POINT_NUM */
#endif
	unsigned char xhi;	/* X coordinate Hi */
	unsigned char yhi;	/* Y coordinate Hi */
	unsigned char ylo:4;	/* Y coordinate Lo */
	unsigned char xlo:4;	/* X coordinate Lo */
#if (XY_DATA_PROTOCOL == NEW_PROTOCOL)
	unsigned char status:4;	/* Action information, 1: Down;
				   2: Move; 3: Up */
	unsigned char id:4;	/* ID information, from 1 to
				   CFG_MAX_POINT_NUM */
#endif
};
#else
struct xy_data {
#if (XY_DATA_PROTOCOL == OLD_PROTOCOL)
	unsigned char status:3;	/* Action information, 1: Down;
				   2: Move; 3: Up */
	unsigned char id:5;	/* ID information, from 1 to
				   CFG_MAX_POINT_NUM */
#endif
	unsigned char xhi;	/* X coordinate Hi */
	unsigned char yhi;	/* Y coordinate Hi */
	unsigned char ylo:4;	/* Y coordinate Lo */
	unsigned char xlo:4;	/* X coordinate Lo */
#if (XY_DATA_PROTOCOL == NEW_PROTOCOL)
	unsigned char status:3;	/* Action information, 1: Down;
				   2: Move; 3: Up */
	unsigned char id:5;	/* ID information, from 1 to
				   CFG_MAX_POINT_NUM */
#endif
	unsigned char area;	/* Touch area */
	unsigned char pressure;	/* Touch Pressure */
};
#endif


union ts_xy_data {
	struct xy_data point[TOUCH_POINT_NUM];
	unsigned char buf[TOUCH_POINT_NUM * sizeof(struct xy_data)];
};


struct ts_driver {

	struct i2c_client *client;

	/* input devices */
	struct input_dev *input_dev;

	struct proc_dir_entry *proc_entry;

	/* Work thread settings */
	struct work_struct event_work;
	struct workqueue_struct *workqueue;

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};

struct ts_config_info {

	unsigned int screen_max_x;
	unsigned int screen_max_y;
	unsigned int irq_gpio_number;
	unsigned int irq_number;
	unsigned int rst_gpio_number;
	unsigned char touch_point_number;
	unsigned char ctp_used;
	unsigned char i2c_bus_number;
	unsigned char revert_x_flag;
	unsigned char revert_y_flag;
	unsigned char exchange_x_y_flag;
	int (*tp_enter_init)(void);
	void (*tp_exit_init)(int state);
};


struct ts_chip_info {
	unsigned char chip_id;
};

struct ts_info {

	struct ts_driver *driver;
	struct ts_config_info config_info;
	struct	ts_chip_info chip_info;
	union ts_xy_data xy_data;
};

#endif

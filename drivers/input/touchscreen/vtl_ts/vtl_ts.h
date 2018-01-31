/* SPDX-License-Identifier: GPL-2.0 */
#ifndef	_TS_CORE_H_ 
#define	_TS_CORE_H_ 

#include <linux/gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include "../tp_suspend.h"


/*vtl touch IC define*/
#define CT36X			0x01//(CT36X:ct362,ct363,ct365)
#define	CT360			0x02//(CT360:ct360)

/*xy data protocol*/
#define OLD_PROTOCOL		0x01
#define	NEW_PROTOCOL		0x02


/***********************vtl ts driver config ******************************************/

/*vtl chip ID*/
#define	CHIP_ID			CT36X//CT360//

#define	XY_DATA_PROTOCOL	NEW_PROTOCOL//OLD_PROTOCOL//

#define TS_I2C_SPEED		400000	    //for rockchip
/*
#if(TB1_USE_F402)
#define		XY_SWAP_ENABLE		1
#else
#define		XY_SWAP_ENABLE		0
#endif

#define		X_REVERSE_ENABLE	0

#if(TB1_USE_F402)
#define		Y_REVERSE_ENABLE	0
#else
#define		Y_REVERSE_ENABLE	1
#endif
*/

#define		CHIP_UPDATE_ENABLE	1

#define		DEBUG_ENABLE		0


/***********************vtl ts driver config  end******************************************/






























/*vtl ts driver name*/
#define DRIVER_NAME		"vtl_ts"
//#define DEBUG_ENABLE  1
#if(DEBUG_ENABLE)
#define 	DEBUG()				printk("___%s___\n",__func__);
//#define 	XY_DEBUG(id,status,x,y)		printk("id = %d,status = %d,X = %d,Y = %d\n",id,status,x,y);
#else   
#define		DEBUG()
//#define 	XY_DEBUG(id,status,x,y)
#endif

/*TOUCH_POINT_NUM define*/
#if(CHIP_ID == CT360)
#define	TOUCH_POINT_NUM		5
#elif(CHIP_ID == CT36X)
#define	TOUCH_POINT_NUM		10
#endif

/*priate define and declare*/
#if(CHIP_ID == CT360)
struct xy_data {
	#if(XY_DATA_PROTOCOL == OLD_PROTOCOL)
	unsigned char	status : 4; 		// Action information, 1: Down; 2: Move; 3: Up
	unsigned char	id : 4; 		// ID information, from 1 to CFG_MAX_POINT_NUM
	#endif
	unsigned char	xhi;			// X coordinate Hi
	unsigned char	yhi;			// Y coordinate Hi
	unsigned char	ylo : 4;		// Y coordinate Lo
	unsigned char	xlo : 4;		// X coordinate Lo
	#if(XY_DATA_PROTOCOL == NEW_PROTOCOL)
	unsigned char	status : 4;		// Action information, 1: Down; 2: Move; 3: Up
	unsigned char	id : 4;			// ID information, from 1 to CFG_MAX_POINT_NUM
	#endif
};
#else
struct xy_data {
	#if(XY_DATA_PROTOCOL == OLD_PROTOCOL)
	unsigned char	status : 3;		// Action information, 1: Down; 2: Move; 3: Up
	unsigned char	id : 5;			// ID information, from 1 to CFG_MAX_POINT_NUM
	#endif
	unsigned char	xhi;			// X coordinate Hi
	unsigned char	yhi;			// Y coordinate Hi
	unsigned char	ylo : 4;		// Y coordinate Lo
	unsigned char	xlo : 4;		// X coordinate Lo
	#if(XY_DATA_PROTOCOL == NEW_PROTOCOL)
	unsigned char	status : 3;		// Action information, 1: Down; 2: Move; 3: Up
	unsigned char	id : 5;			// ID information, from 1 to CFG_MAX_POINT_NUM
	#endif
	unsigned char	area;			// Touch area
	unsigned char	pressure;		// Touch Pressure
};
#endif


union ts_xy_data {
	struct xy_data	point[TOUCH_POINT_NUM];
	unsigned char	buf[TOUCH_POINT_NUM * sizeof(struct xy_data)];
};


struct ts_driver{

	struct i2c_client		*client;

	/* input devices */
	struct input_dev		*input_dev;			

	struct proc_dir_entry		*proc_entry;	

	struct task_struct 		*ts_thread;

	//#ifdef CONFIG_HAS_EARLYSUSPEND
	//struct early_suspend		early_suspend;
	//#endif
};

struct ts_config_info{      
        
        unsigned int	screen_max_x;
        unsigned int	screen_max_y;
        unsigned int	xy_swap;
        unsigned int	x_reverse;
        unsigned int	y_reverse;
        unsigned int	x_mul;
        unsigned int	y_mul;
		unsigned int	bin_ver;
	unsigned int	irq_gpio_number;
	unsigned int	irq_number;
        unsigned int	rst_gpio_number;
	unsigned char	touch_point_number;
	unsigned char	ctp_used;
	//unsigned char	i2c_bus_number;
        //unsigned char	revert_x_flag;
        //unsigned char	revert_y_flag;
        //unsigned char	exchange_x_y_flag;                  
};

struct	ts_info{
	
	struct ts_driver	*driver;
	struct ts_config_info	config_info;
	union ts_xy_data	xy_data;
	unsigned char 		debug;
	struct  tp_device  tp;
};


//extern struct ts_info	*pg_ts;
extern struct ts_info * vtl_ts_get_object(void);
extern void vtl_ts_hw_reset(void);

#endif


/*
 * ISP driver
 *
 * Author: Kele Bai <kele.bai@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __TVIN_ISP_DRV_H
#define __TVIN_ISP_DRV_H

/* Standard Linux Headers */
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/irqreturn.h>
#include <linux/timer.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/device.h>

#include <linux/amlogic/tvin/tvin_v4l2.h>
#include "isp_hw.h"
#include "../tvin_frontend.h"

#define ISP_VER					"2014.01.16a"
#define ISP_NUM					1
#define DEVICE_NAME 			        "isp"

#define USE_WORK_QUEUE

#define ISP_FLAG_START				0x00000001
#define ISP_FLAG_AE				0x00000002
#define ISP_FLAG_AWB				0x00000004
#define ISP_FLAG_AF				0x00000008
#define ISP_FLAG_CAPTURE			0x00000010
#define ISP_FLAG_RECORD				0x00000020
#define ISP_WORK_MODE_MASK			0x00000030
#define ISP_FLAG_SET_EFFECT			0x00000040
#define ISP_FLAG_SET_SCENES			0x00000080
#define ISP_FLAG_AF_DBG				0x00000100
#define ISP_FLAG_MWB			        0x00000200
#define ISP_FLAG_BLNR				0x00000400
#define ISP_FLAG_SET_COMB4			0x00000800
#define ISP_TEST_FOR_AF_WIN			0x00001000
#define ISP_FLAG_TOUCH_AF			0x00002000
#define ISP_FLAG_SKIP_BUF			0x00004000
#define ISP_FLAG_TEST_WB			0x00008000
#define ISP_FLAG_RECONFIG                       0x00010000

#define ISP_AF_SM_MASK				(ISP_FLAG_AF|ISP_FLAG_TOUCH_AF)

typedef enum bayer_fmt_e {
	RAW_BGGR = 0,
	RAW_RGGB,
	RAW_GBRG,
	RAW_GRBG,//3
} bayer_fmt_t;
typedef struct isp_info_s {
	tvin_port_t fe_port;
	tvin_color_fmt_t bayer_fmt;
	tvin_color_fmt_t cfmt;
	tvin_color_fmt_t dfmt;
	unsigned int h_active;
	unsigned int v_active;
	unsigned short dest_hactive;
	unsigned short dest_vactive;
	unsigned int frame_rate;
	unsigned int skip_cnt;
} isp_info_t;
/*config in bsp*/
typedef struct flash_property_s {
	bool 	 valid;		 //true:have flash,false:havn't flash
	bool     torch_pol_inv;  // false: negative correlation
                                 // true: positive correlation
        bool 	 pin_mux_inv;	 // false: led1=>pin1 & led2=>pin2, true: led1=>pin2 & led2=>pin1

        bool 	 led1_pol_inv;	 // false: active high, true: active low
        bool     mode_pol_inv;   //        TORCH  FLASH
                                 //false: low      high
                                 //true:  high     low
} flash_property_t;
/*parameters used for ae in sm or driver*/
typedef struct isp_ae_info_s {
	int manul_level;//each step 2db
	atomic_t writeable;
} isp_ae_info_t;

/*for af debug*/
typedef struct af_debug_s {
	bool            dir;
	//unsigned int    control;
	unsigned int    state;
	unsigned int    step;
	unsigned int 	min_step;
		 int 	max_step;
	unsigned int    delay;
		 int 	cur_step;
	unsigned int    pre_step;
	unsigned int	mid_step;
	unsigned int	post_step;
	unsigned int	pre_threshold;
	unsigned int	post_threshold;
	isp_blnr_stat_t data[1024];
} af_debug_t;
/*for af test debug*/
typedef struct af_debug_test_s {
	unsigned int cnt;
	unsigned int max;
	struct isp_af_stat_s   *af_win;
	struct isp_blnr_stat_s *af_bl;
	struct isp_ae_stat_s   *ae_win;
	struct isp_awb_stat_s  *awb_stat;
} af_debug_test_t;
/*for af fine tune*/
typedef struct isp_af_fine_tune_s {
	unsigned int cur_step;
	isp_blnr_stat_t af_data;
} isp_af_fine_tune_t;

typedef struct isp_af_info_s {
	unsigned int cur_index;
	/*for lose focus*/
	unsigned int *v_dc;
        bool	     last_move;
	isp_blnr_stat_t last_blnr;
	/*for climbing algorithm*/
	unsigned int great_step;
	unsigned int cur_step;
	unsigned int capture_step;
	isp_blnr_stat_t *af_detect;
	isp_blnr_stat_t af_data[FOCUS_GRIDS];
	//unsigned char af_delay;
	atomic_t writeable;
	/*window for full scan&detect*/
	unsigned int x0;
	unsigned int y0;
	unsigned int x1;
	unsigned int y1;
	/*touch window radius*/
	unsigned int radius;
	/* blnr tmp for isr*/
	isp_blnr_stat_t isr_af_data;
	unsigned int valid_step_cnt;
	isp_af_fine_tune_t af_fine_data[FOCUS_GRIDS];
}isp_af_info_t;

/*for debug cmd*/
typedef struct debug_s {
	unsigned int comb4_mode;
} debug_t;

typedef struct isp_dev_s{
	int             index;
	dev_t		devt;
	unsigned int    offset;
	struct cdev	cdev;
	struct device	*dev;
	unsigned int    flag;
	unsigned int 	vs_cnt;
        /*add for tvin frontend*/
        tvin_frontend_t frontend;
	tvin_frontend_t *isp_fe;

	struct isp_info_s info;
#ifndef USE_WORK_QUEUE
    struct tasklet_struct isp_task;
    struct task_struct     *kthread;
#else
    struct work_struct isp_wq;
#endif
	struct isp_ae_stat_s isp_ae;
	struct isp_ae_info_s ae_info;
	struct isp_awb_stat_s isp_awb;
	struct isp_af_stat_s isp_af;
	struct isp_af_info_s af_info;
	struct isp_blnr_stat_s blnr_stat;
	cam_parameter_t *cam_param;
	xml_algorithm_ae_t *isp_ae_parm;
	xml_algorithm_awb_t *isp_awb_parm;
	xml_algorithm_af_t *isp_af_parm;
	xml_capture_t *capture_parm;
	wave_t        *wave;
	flash_property_t flash;
	af_debug_t      *af_dbg;
	debug_t         debug;
	/*test for af test win*/
	af_debug_test_t af_test;
	/*cmd state for camera*/
	cam_cmd_state_t cmd_state;
}isp_dev_t;

typedef enum data_type_e{
	ISP_U8=0,
	ISP_U16,
	ISP_U32,
	//ISP_FLOAT, //not use
}data_type_t;

typedef struct isp_param_s{
    const char *name;
    unsigned int *param;
    unsigned char length;
    data_type_t type;
}isp_param_t;

extern void set_ae_parm(xml_algorithm_ae_t * ae_sw,char * * parm);
extern void set_awb_parm(xml_algorithm_awb_t * awb_sw,char * * parm);
extern void set_af_parm(xml_algorithm_af_t * af_sw,char * * parm);
extern void set_cap_parm(struct xml_capture_s * cap_sw,char * * parm);
extern void set_wave_parm(struct wave_s * wave,char * * parm);
extern bool set_gamma_table_with_curve_ratio(unsigned int r,unsigned int g,unsigned int b);
#endif


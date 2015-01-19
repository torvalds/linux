/*
  * ISP 3A State Machine
  *
  * Author: Kele Bai <kele.bai@amlogic.com>
  *
  * Copyright (C) 2010 Amlogic Inc.
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License version 2 as
  * published by the Free Software Foundation.
  */

#ifndef __ISP_STATE_MACHINE_H
#define __ISP_STATE_MACHINE_H
#include "isp_drv.h"

typedef enum isp_auto_exposure_state_e {
	AE_INIT,
	AE_SHUTTER_ADJUST,
	AE_GAIN_ADJUST,
	AE_REST,
} isp_auto_exposure_state_t;

typedef enum isp_auto_white_balance_state_e {
	AWB_IDLE,
	AWB_INIT,
	AWB_CHECK,
} isp_auto_white_balance_state_t;

typedef enum af_state_e {
	AF_NULL,
	AF_DETECT_INIT,
	AF_GET_STEPS_INFO,
	AF_GET_STATUS,
	AF_SCAN_INIT,
	AF_GET_COARSE_INFO_H,
	AF_GET_COARSE_INFO_L,
	AF_CALC_GREAT,
	AF_GET_FINE_INFO,
	AF_CLIMBING,
	AF_FINE,
	AF_SUCCESS,
} af_state_t;
typedef enum isp_capture_state_e {
	CAPTURE_NULL,
	CAPTURE_INIT,
	CAPTURE_PRE_WAIT,//for time lapse
	CAPTURE_FLASH_ON,//turn on flash for red eye
	CAPTURE_TR_WAIT,
	CAPTURE_TUNE_3A,
	CAPTURE_LOW_GAIN,
	CAPTURE_EYE_WAIT,
	CAPTURE_POS_WAIT,
	CAPTURE_SINGLE,
	CAPTURE_FLASHW,
	CAPTURE_MULTI,
	CAPTURE_END,
}isp_capture_state_t;

typedef enum isp_ae_status_s {
	ISP_AE_STATUS_NULL = 0,
	ISP_AE_STATUS_UNSTABLE,
	ISP_AE_STATUS_STABLE,
	ISP_AE_STATUS_UNTUNEABLE,
}isp_ae_status_t;

typedef struct isp_ae_sm_s {
	unsigned int pixel_sum;
	unsigned int sub_pixel_sum;
	unsigned int win_l;
	unsigned int win_r;
	unsigned int win_t;
	unsigned int win_b;
	unsigned int alert_r;
	unsigned int alert_g;
	unsigned int alert_b;
	unsigned int cur_gain;
	unsigned int pre_gain;
	unsigned int max_gain;
	unsigned int min_gain;
	unsigned int max_step;
	unsigned int cur_step;
	unsigned int countlimit_r;
	unsigned int countlimit_g;
	unsigned int countlimit_b;
	unsigned int tf_ratio;
    unsigned int change_step;
	unsigned int max_lumasum1;  //low
	unsigned int max_lumasum2;
	unsigned int max_lumasum3;
	unsigned int max_lumasum4;	//high
	int targ;

	isp_auto_exposure_state_t isp_ae_state;
}isp_ae_sm_t;

typedef enum isp_awb_status_s {
	ISP_AWB_STATUS_NULL = 0,
	ISP_AWB_STATUS_UNSTABLE,
	ISP_AWB_STATUS_STABLE,
}isp_awb_status_t;

typedef struct isp_awb_sm_s {
	enum isp_awb_status_s status;
	unsigned int pixel_sum;
	unsigned int win_l;
	unsigned int win_r;
	unsigned int win_t;
	unsigned int win_b;
	unsigned int countlimitrgb;
	unsigned int countlimityh;
	unsigned int countlimitym;
	unsigned int countlimityl;

	unsigned int countlimityuv;
	unsigned char y;
	unsigned char w;
	unsigned char coun;

	isp_auto_white_balance_state_t isp_awb_state;

}isp_awb_sm_t;

typedef enum isp_flash_status_s {
	ISP_FLASH_STATUS_NULL = 0,
	ISP_FLASH_STATUS_ON,
	ISP_FLASH_STATUS_OFF,
}isp_flash_status_t;

typedef enum isp_env_status_s {
	ENV_NULL = 0,
	ENV_HIGH,
	ENV_MID,
	ENV_LOW,
}isp_env_status_t;
typedef struct isp_af_sm_s {
	af_state_t state;
} isp_af_sm_t;

typedef struct isp_capture_sm_s {
	unsigned int adj_cnt;
	unsigned int max_ac_sum;
	unsigned int tr_time;
	unsigned int fr_time;
	unsigned char flash_on;
	flash_mode_t  flash_mode;
	isp_capture_state_t capture_state;
} isp_capture_sm_t;

typedef struct isp_sm_s {
	enum isp_ae_status_s status;
	enum isp_flash_status_s flash;
	enum isp_env_status_s env;
	bool ae_down;
	af_state_t af_state;
	isp_ae_sm_t isp_ae_parm;
	isp_awb_sm_t isp_awb_parm;
	isp_af_sm_t af_sm;
	isp_capture_sm_t cap_sm;
} isp_sm_t;

typedef struct isp_ae_to_sensor_s {
	volatile unsigned int send;
	volatile unsigned int new_step;
	volatile unsigned int shutter;
	volatile unsigned int gain;
} isp_ae_to_sensor_t;

extern void isp_sm_init(isp_dev_t *devp);
extern void isp_sm_uninit(isp_dev_t *devp);
extern void af_sm_init(isp_dev_t *devp);
extern void capture_sm_init(isp_dev_t *devp);
extern void isp_set_flash_mode(isp_dev_t *devp);
extern void isp_ae_sm(isp_dev_t *devp);
extern void isp_awb_sm(isp_dev_t *devp);
extern void isp_af_fine_tune(isp_dev_t *devp);
extern void isp_af_detect(isp_dev_t *devp);
extern int isp_capture_sm(isp_dev_t *devp);
extern unsigned long long div64(unsigned long long n, unsigned long long d);
extern void isp_af_save_current_para(isp_dev_t *devp);
extern void isp_set_manual_exposure(isp_dev_t *devp);
extern unsigned int isp_tune_exposure(isp_dev_t *devp);
#endif



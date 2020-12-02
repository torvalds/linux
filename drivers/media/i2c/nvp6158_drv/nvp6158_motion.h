// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
*
*  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
*  Module		: motion.h
*  Description	:
*  Author		:
*  Date         :
*  Version		: Version 1.0
*
********************************************************************************
*  History      :
*
*
********************************************************************************/
#ifndef _MOTION_H_
#define _MOTION_H_

#include "nvp6158_common.h"

#define FUNC_ON		0x01
#define FUNC_OFF		0x00

typedef struct _motion_mode{
	unsigned char ch;
	unsigned char devnum;
	unsigned char set_val;

	unsigned char fmtdef;
}motion_mode;

void nvp6158_motion_onoff_set(motion_mode *motion_set);
void nvp6158_motion_display_onoff_set(motion_mode *motion_set);
void nvp6158_motion_pixel_all_onoff_set(motion_mode *motion_set);
void nvp6158_motion_pixel_onoff_set(motion_mode *motion_set);
void nvp6158_motion_pixel_onoff_get(motion_mode *motion_set);
void nvp6158_motion_tsen_set(motion_mode *motion_set);
void nvp6158_motion_psen_set(motion_mode *motion_set);
void nvp6158_motion_detection_get(motion_mode *motion_set);

#endif /* _MOTION_H_ */

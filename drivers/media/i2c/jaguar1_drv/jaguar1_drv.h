/* SPDX-License-Identifier: GPL-2.0 */
/********************************************************************************
 *
 *  Copyright (C) 2017 NEXTCHIP Inc. All rights reserved.
 *  Module		: Jaguar1 Device Driver
 *  Description	: MIPI
 *  Author		:
 *  Date         :
 *  Version		: Version 1.0
 *
 ********************************************************************************
 *  History      :
 *
 *
 ********************************************************************************/
#ifndef _JAGUAR1_DRV_
#define _JAGUAR1_DRV_

#include "jaguar1_video.h"

#define JAGUAR1_MCLK_594MHZ  0x01
#define JAGUAR1_MCLK_378MHZ  0x02
#define JAGUAR1_MCLK_1242MHZ 0x03

void jaguar1_set_mclk(unsigned int mclk);
void jaguar1_start(video_init_all *video_init);
void jaguar1_stop(void);
int jaguar1_init(int i2c_bus);
void jaguar1_exit(void);

#endif

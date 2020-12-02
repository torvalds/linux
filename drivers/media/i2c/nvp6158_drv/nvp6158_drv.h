// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
 *
 *  Copyright (C) 2017 NEXTCHIP Inc. All rights reserved.
 *  Module		: Nvp6158 Device Driver
 *  Description	: BT1120
 *  Author		:
 *  Date         :
 *  Version		: Version 1.0
 *
 ********************************************************************************
 *  History      :
 *
 *
 ********************************************************************************/
#ifndef _NVP6158_DRV_
#define _NVP6158_DRV_

#include "nvp6158_video.h"

extern void nvp6158_datareverse(unsigned char chip, unsigned char port);

int nvp6158_open(struct inode * inode, struct file * file);
int nvp6158_close(struct inode * inode, struct file * file);
long nvp6158_native_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
void nvp6158_i2c_client_exit(void);

void nvp6158_set_mclk(unsigned int mclk);
void nvp6158_start(video_init_all *video_init, bool dual_edge);
void nvp6158_stop(void);
int nvp6158_init(int i2c_bus);
void nvp6158_exit(void);

#endif

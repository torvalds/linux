/*
	$License:
	Copyright (C) 2011 InvenSense Corporation, All Rights Reserved.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	$
 */

#ifndef __SLAVEIRQ__
#define __SLAVEIRQ__

#include <linux/i2c-dev.h>

#include <linux/mpu.h>
#include "mpuirq.h"

#define SLAVEIRQ_SET_TIMEOUT           _IOW(MPU_IOCTL, 0x50, unsigned long)
#define SLAVEIRQ_GET_INTERRUPT_CNT     _IOR(MPU_IOCTL, 0x51, unsigned long)
#define SLAVEIRQ_GET_IRQ_TIME          _IOR(MPU_IOCTL, 0x52, unsigned long)

void slaveirq_exit(struct ext_slave_platform_data *pdata);
int slaveirq_init(struct i2c_adapter *slave_adapter,
		  struct ext_slave_platform_data *pdata, char *name);

#endif

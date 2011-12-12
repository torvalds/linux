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

#ifndef __TIMERIRQ__
#define __TIMERIRQ__

#include <linux/mpu.h>

#define TIMERIRQ_SET_TIMEOUT           _IOW(MPU_IOCTL, 0x60, unsigned long)
#define TIMERIRQ_GET_INTERRUPT_CNT     _IOW(MPU_IOCTL, 0x61, unsigned long)
#define TIMERIRQ_START                 _IOW(MPU_IOCTL, 0x62, unsigned long)
#define TIMERIRQ_STOP                  _IO(MPU_IOCTL, 0x63)

#endif

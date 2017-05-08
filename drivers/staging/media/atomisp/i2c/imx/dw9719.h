/*
 * Support for dw9719 vcm driver.
 *
 * Copyright (c) 2012 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef __DW9719_H__
#define __DW9719_H__

#include "../../include/linux/atomisp_platform.h"
#include <linux/types.h>

#define DW9719_VCM_ADDR	 (0x18 >> 1)

/* dw9719 device structure */
struct dw9719_device {
	struct timespec timestamp_t_focus_abs;
	s16 number_of_steps;
	bool initialized;		/* true if dw9719 is detected */
	s32 focus;			/* Current focus value */
	struct timespec focus_time;	/* Time when focus was last time set */
	__u8 buffer[4];			/* Used for i2c transactions */
	const struct camera_af_platform_data *platform_data;
};

#define DW9719_INVALID_CONFIG	0xffffffff
#define DW9719_MAX_FOCUS_POS	1023
#define DELAY_PER_STEP_NS	1000000
#define DELAY_MAX_PER_STEP_NS	(1000000 * 1023)

#define DW9719_INFO			0
#define DW9719_ID			0xF1
#define DW9719_CONTROL			2
#define DW9719_VCM_CURRENT		3

#define DW9719_MODE			6
#define DW9719_VCM_FREQ			7

#define DW9719_MODE_SAC3		0x40
#define DW9719_DEFAULT_VCM_FREQ		0x04
#define DW9719_ENABLE_RINGING		0x02

#endif

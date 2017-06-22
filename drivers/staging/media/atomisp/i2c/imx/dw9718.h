/*
 * Support for dw9719 vcm driver.
 *
 * Copyright (c) 2014 Intel Corporation. All Rights Reserved.
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

#ifndef __DW9718_H__
#define __DW9718_H__

#include "../../include/linux/atomisp_platform.h"
#include <linux/types.h>

#define DW9718_VCM_ADDR	 (0x18 >> 1)

/* dw9718 device structure */
struct dw9718_device {
	struct timespec timestamp_t_focus_abs;
	s16 number_of_steps;
	bool initialized;		/* true if dw9718 is detected */
	s32 focus;			/* Current focus value */
	struct timespec focus_time;	/* Time when focus was last time set */
	__u8 buffer[4];			/* Used for i2c transactions */
	const struct camera_af_platform_data *platform_data;
	__u8 power_on;
};

#define DW9718_MAX_FOCUS_POS	1023

/* Register addresses */
#define DW9718_PD			0x00
#define DW9718_CONTROL			0x01
#define DW9718_DATA_M			0x02
#define DW9718_DATA_L			0x03
#define DW9718_SW			0x04
#define DW9718_SACT			0x05
#define DW9718_FLAG			0x10

#define DW9718_CONTROL_SW_LINEAR	BIT(0)
#define DW9718_CONTROL_S_SAC4		(BIT(1) | BIT(3))
#define DW9718_CONTROL_OCP_DISABLE	BIT(4)
#define DW9718_CONTROL_UVLO_DISABLE	BIT(5)

#define DW9718_SACT_MULT_TWO		0x00
#define DW9718_SACT_PERIOD_8_8MS	0x19
#define DW9718_SACT_DEFAULT_VAL		0x60

#define DW9718_DEFAULT_FOCUS_POSITION	300

#endif /* __DW9718_H__ */

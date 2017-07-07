/*
 * include/media/lm3642.h
 *
 * Copyright (c) 2010-2012 Intel Corporation. All Rights Reserved.
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
 * along with this program.
 */

#ifndef _LM3642_H_
#define _LM3642_H_

#include <linux/videodev2.h>
#include <media/v4l2-subdev.h>

#define LM3642_NAME    "lm3642"
#define LM3642_ID      3642

#define	v4l2_queryctrl_entry_integer(_id, _name,\
		_minimum, _maximum, _step, \
		_default_value, _flags)	\
	{\
		.id = (_id), \
		.type = V4L2_CTRL_TYPE_INTEGER, \
		.name = _name, \
		.minimum = (_minimum), \
		.maximum = (_maximum), \
		.step = (_step), \
		.default_value = (_default_value),\
		.flags = (_flags),\
	}
#define	v4l2_queryctrl_entry_boolean(_id, _name,\
		_default_value, _flags)	\
	{\
		.id = (_id), \
		.type = V4L2_CTRL_TYPE_BOOLEAN, \
		.name = _name, \
		.minimum = 0, \
		.maximum = 1, \
		.step = 1, \
		.default_value = (_default_value),\
		.flags = (_flags),\
	}

#define	s_ctrl_id_entry_integer(_id, _name, \
		_minimum, _maximum, _step, \
		_default_value, _flags, \
		_s_ctrl, _g_ctrl)	\
	{\
		.qc = v4l2_queryctrl_entry_integer(_id, _name,\
				_minimum, _maximum, _step,\
				_default_value, _flags), \
		.s_ctrl = _s_ctrl, \
		.g_ctrl = _g_ctrl, \
	}

#define	s_ctrl_id_entry_boolean(_id, _name, \
		_default_value, _flags, \
		_s_ctrl, _g_ctrl)	\
	{\
		.qc = v4l2_queryctrl_entry_boolean(_id, _name,\
				_default_value, _flags), \
		.s_ctrl = _s_ctrl, \
		.g_ctrl = _g_ctrl, \
	}


/* Default Values */
#define LM3642_DEFAULT_TIMEOUT           300U
#define LM3642_DEFAULT_RAMP_TIME	 0x10 /* 1.024ms */
#define LM3642_DEFAULT_INDICATOR_CURRENT 0x01 /* 1.88A */
#define LM3642_DEFAULT_FLASH_CURRENT	 0x0f /* 1500mA */

/* Value settings for Flash Time-out Duration*/
#define LM3642_MIN_TIMEOUT              100U
#define LM3642_MAX_TIMEOUT              800U
#define LM3642_TIMEOUT_STEPSIZE         100U

/* Flash modes */
#define LM3642_MODE_SHUTDOWN            0
#define LM3642_MODE_INDICATOR           1
#define LM3642_MODE_TORCH               2
#define LM3642_MODE_FLASH               3

/* timer delay time */
#define LM3642_TIMER_DELAY		5

/* Percentage <-> value macros */
#define LM3642_MIN_PERCENT                   0U
#define LM3642_MAX_PERCENT                   100U
#define LM3642_CLAMP_PERCENTAGE(val) \
	clamp(val, LM3642_MIN_PERCENT, LM3642_MAX_PERCENT)

#define LM3642_VALUE_TO_PERCENT(v, step) \
	(((((unsigned long)((v)+1))*(step))+50)/100)
#define LM3642_PERCENT_TO_VALUE(p, step) \
	(((((unsigned long)(p))*100)+((step)>>1))/(step)-1)

/* Product specific limits
 * TODO: get these from platform data */
#define LM3642_FLASH_MAX_LVL   0x0F /* 1500mA */
#define LM3642_TORCH_MAX_LVL   0x07 /* 187mA */
#define LM3642_INDICATOR_MAX_LVL   0x01 /* 1.88A */

/* Flash brightness, input is percentage, output is [0..15] */
#define LM3642_FLASH_STEP	\
	((100ul*(LM3642_MAX_PERCENT) \
	+((LM3642_FLASH_MAX_LVL+1)>>1)) \
	/((LM3642_FLASH_MAX_LVL+1)))
#define LM3642_FLASH_DEFAULT_BRIGHTNESS \
	LM3642_VALUE_TO_PERCENT(15, LM3642_FLASH_STEP)

/* Torch brightness, input is percentage, output is [0..7] */
#define LM3642_TORCH_STEP	\
	((100ul*(LM3642_MAX_PERCENT) \
	+((LM3642_TORCH_MAX_LVL+1)>>1)) \
	/((LM3642_TORCH_MAX_LVL+1)))
#define LM3642_TORCH_DEFAULT_BRIGHTNESS \
	LM3642_VALUE_TO_PERCENT(0, LM3642_TORCH_STEP)

/* Indicator brightness, input is percentage, output is [0..1] */
#define LM3642_INDICATOR_STEP	\
	((100ul*(LM3642_MAX_PERCENT) \
	+((LM3642_INDICATOR_MAX_LVL+1)>>1)) \
	/((LM3642_INDICATOR_MAX_LVL+1)))
#define LM3642_INDICATOR_DEFAULT_BRIGHTNESS \
	LM3642_VALUE_TO_PERCENT(1, LM3642_INDICATOR_STEP)

/*
 * lm3642_platform_data - Flash controller platform data
 */
struct lm3642_platform_data {
	int gpio_torch;
	int gpio_strobe;
	int (*power_ctrl)(struct v4l2_subdev *subdev, int on);

	unsigned int torch_en;
	unsigned int flash_en;
	unsigned int tx_en;
	unsigned int ivfm_en;
};

#endif /* _LM3642_H_ */


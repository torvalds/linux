/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/media/lm3554.h
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
 *
 */
#ifndef _LM3554_H_
#define _LM3554_H_

#include <linux/videodev2.h>
#include <media/v4l2-subdev.h>

#define LM3554_ID      3554

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

/* Value settings for Flash Time-out Duration*/
#define LM3554_DEFAULT_TIMEOUT          512U
#define LM3554_MIN_TIMEOUT              32U
#define LM3554_MAX_TIMEOUT              1024U
#define LM3554_TIMEOUT_STEPSIZE         32U

/* Flash modes */
#define LM3554_MODE_SHUTDOWN            0
#define LM3554_MODE_INDICATOR           1
#define LM3554_MODE_TORCH               2
#define LM3554_MODE_FLASH               3

/* timer delay time */
#define LM3554_TIMER_DELAY		5

/* Percentage <-> value macros */
#define LM3554_MIN_PERCENT                   0U
#define LM3554_MAX_PERCENT                   100U
#define LM3554_CLAMP_PERCENTAGE(val) \
	clamp(val, LM3554_MIN_PERCENT, LM3554_MAX_PERCENT)

#define LM3554_VALUE_TO_PERCENT(v, step)     (((((unsigned long)(v)) * (step)) + 50) / 100)
#define LM3554_PERCENT_TO_VALUE(p, step)     (((((unsigned long)(p)) * 100) + (step >> 1)) / (step))

/* Product specific limits
 * TODO: get these from platform data */
#define LM3554_FLASH_MAX_LVL   0x0F /* 1191mA */

/* Flash brightness, input is percentage, output is [0..15] */
#define LM3554_FLASH_STEP	\
	((100ul * (LM3554_MAX_PERCENT) + ((LM3554_FLASH_MAX_LVL) >> 1)) / ((LM3554_FLASH_MAX_LVL)))
#define LM3554_FLASH_DEFAULT_BRIGHTNESS \
	LM3554_VALUE_TO_PERCENT(13, LM3554_FLASH_STEP)

/* Torch brightness, input is percentage, output is [0..7] */
#define LM3554_TORCH_STEP                    1250
#define LM3554_TORCH_DEFAULT_BRIGHTNESS \
	LM3554_VALUE_TO_PERCENT(2, LM3554_TORCH_STEP)

/* Indicator brightness, input is percentage, output is [0..3] */
#define LM3554_INDICATOR_STEP                2500
#define LM3554_INDICATOR_DEFAULT_BRIGHTNESS \
	LM3554_VALUE_TO_PERCENT(1, LM3554_INDICATOR_STEP)

/*
 * lm3554_platform_data - Flash controller platform data
 */
struct lm3554_platform_data {
	int gpio_torch;
	int gpio_strobe;
	int gpio_reset;

	unsigned int current_limit;
	unsigned int envm_tx2;
	unsigned int tx2_polarity;
};

#endif /* _LM3554_H_ */

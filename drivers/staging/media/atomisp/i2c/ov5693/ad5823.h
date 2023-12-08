/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for AD5823 VCM.
 *
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
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

#ifndef __AD5823_H__
#define __AD5823_H__

#include <linux/types.h>

#define AD5823_VCM_ADDR	0x0c

#define AD5823_REG_RESET		0x01
#define AD5823_REG_MODE			0x02
#define AD5823_REG_VCM_MOVE_TIME	0x03
#define AD5823_REG_VCM_CODE_MSB		0x04
#define AD5823_REG_VCM_CODE_LSB		0x05
#define AD5823_REG_VCM_THRESHOLD_MSB	0x06
#define AD5823_REG_VCM_THRESHOLD_LSB	0x07

#define AD5823_REG_LENGTH		0x1

#define AD5823_RING_CTRL_ENABLE		0x04
#define AD5823_RING_CTRL_DISABLE	0x00

#define AD5823_RESONANCE_PERIOD		100000
#define AD5823_RESONANCE_COEF		512
#define AD5823_HIGH_FREQ_RANGE		0x80

#define VCM_CODE_MSB_MASK		0xfc
#define AD5823_INIT_FOCUS_POS           350

enum ad5823_tok_type {
	AD5823_8BIT  = 0x1,
	AD5823_16BIT = 0x2,
};

enum ad5823_vcm_mode {
	AD5823_ARC_RES0 = 0x0,	/* Actuator response control RES1 */
	AD5823_ARC_RES1 = 0x1,	/* Actuator response control RES0.5 */
	AD5823_ARC_RES2 = 0x2,	/* Actuator response control RES2 */
	AD5823_ESRC = 0x3,	/* Enhanced slew rate control */
	AD5823_DIRECT = 0x4,	/* Direct control */
};

#define AD5823_INVALID_CONFIG	0xffffffff
#define AD5823_MAX_FOCUS_POS	1023
#define DELAY_PER_STEP_NS	1000000
#define DELAY_MAX_PER_STEP_NS	(1000000 * 1023)
#endif

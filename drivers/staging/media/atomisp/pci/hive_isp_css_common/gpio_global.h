/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __GPIO_GLOBAL_H_INCLUDED__
#define __GPIO_GLOBAL_H_INCLUDED__

#define IS_GPIO_VERSION_1

#include <gpio_block_defs.h>

/* pqiao: following part only defines in hive_isp_css_defs.h in fpga system.
	port it here
*/

/* GPIO pin defines */
/*#define HIVE_GPIO_CAMERA_BOARD_RESET_PIN_NR                   0
#define HIVE_GPIO_LCD_CLOCK_SELECT_PIN_NR                     7
#define HIVE_GPIO_HDMI_CLOCK_SELECT_PIN_NR                    8
#define HIVE_GPIO_LCD_VERT_FLIP_PIN_NR                        8
#define HIVE_GPIO_LCD_HOR_FLIP_PIN_NR                         9
#define HIVE_GPIO_AS3683_GPIO_P0_PIN_NR                       1
#define HIVE_GPIO_AS3683_DATA_P1_PIN_NR                       2
#define HIVE_GPIO_AS3683_CLK_P2_PIN_NR                        3
#define HIVE_GPIO_AS3683_T1_F0_PIN_NR                         4
#define HIVE_GPIO_AS3683_SFL_F1_PIN_NR                        5
#define HIVE_GPIO_AS3683_STROBE_F2_PIN_NR                     6
#define HIVE_GPIO_MAX1577_EN1_PIN_NR                          1
#define HIVE_GPIO_MAX1577_EN2_PIN_NR                          2
#define HIVE_GPIO_MAX8685A_EN_PIN_NR                          3
#define HIVE_GPIO_MAX8685A_TRIG_PIN_NR                        4*/

#define HIVE_GPIO_STROBE_TRIGGER_PIN		2

#endif /* __GPIO_GLOBAL_H_INCLUDED__ */

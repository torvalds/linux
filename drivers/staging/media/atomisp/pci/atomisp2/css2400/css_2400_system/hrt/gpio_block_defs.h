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

#ifndef _gpio_block_defs_h_
#define _gpio_block_defs_h_

#define _HRT_GPIO_BLOCK_REG_ALIGN 4

/* R/W registers */
#define _gpio_block_reg_do_e			         0
#define _gpio_block_reg_do_select		       1
#define _gpio_block_reg_do_0			         2
#define _gpio_block_reg_do_1			         3
#define _gpio_block_reg_do_pwm_cnt_0	     4
#define _gpio_block_reg_do_pwm_cnt_1	     5
#define _gpio_block_reg_do_pwm_cnt_2	     6
#define _gpio_block_reg_do_pwm_cnt_3	     7
#define _gpio_block_reg_do_pwm_main_cnt    8
#define _gpio_block_reg_do_pwm_enable      9
#define _gpio_block_reg_di_debounce_sel	  10
#define _gpio_block_reg_di_debounce_cnt_0	11
#define _gpio_block_reg_di_debounce_cnt_1	12
#define _gpio_block_reg_di_debounce_cnt_2	13
#define _gpio_block_reg_di_debounce_cnt_3	14
#define _gpio_block_reg_di_active_level	  15


/* read-only registers */
#define _gpio_block_reg_di			          16

#endif /* _gpio_block_defs_h_ */

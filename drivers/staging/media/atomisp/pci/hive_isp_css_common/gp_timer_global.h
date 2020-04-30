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

#ifndef __GP_TIMER_GLOBAL_H_INCLUDED__
#define __GP_TIMER_GLOBAL_H_INCLUDED__

#include "hive_isp_css_defs.h" /*HIVE_GP_TIMER_SP_DMEM_ERROR_IRQ */

/* from gp_timer_defs.h*/
#define GP_TIMER_COUNT_TYPE_HIGH             0
#define GP_TIMER_COUNT_TYPE_LOW              1
#define GP_TIMER_COUNT_TYPE_POSEDGE          2
#define GP_TIMER_COUNT_TYPE_NEGEDGE          3
#define GP_TIMER_COUNT_TYPE_TYPES            4

/* timer - 3 is selected */
#define GP_TIMER_SEL                         3

/*HIVE_GP_TIMER_SP_DMEM_ERROR_IRQ is selected*/
#define GP_TIMER_SIGNAL_SELECT  HIVE_GP_TIMER_SP_DMEM_ERROR_IRQ

#endif /* __GP_TIMER_GLOBAL_H_INCLUDED__ */

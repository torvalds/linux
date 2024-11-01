/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __TIMED_CTRL_GLOBAL_H_INCLUDED__
#define __TIMED_CTRL_GLOBAL_H_INCLUDED__

#define IS_TIMED_CTRL_VERSION_1

#include "timed_controller_defs.h"

/**
 * Order of the input bits for the timed controller taken from
 * ISP_CSS_2401 System Architecture Description valid for
 * 2400, 2401.
 *
 * Check for other systems.
 */
#define HIVE_TIMED_CTRL_GPIO_PIN_0_BIT_ID                       0
#define HIVE_TIMED_CTRL_GPIO_PIN_1_BIT_ID                       1
#define HIVE_TIMED_CTRL_GPIO_PIN_2_BIT_ID                       2
#define HIVE_TIMED_CTRL_GPIO_PIN_3_BIT_ID                       3
#define HIVE_TIMED_CTRL_GPIO_PIN_4_BIT_ID                       4
#define HIVE_TIMED_CTRL_GPIO_PIN_5_BIT_ID                       5
#define HIVE_TIMED_CTRL_GPIO_PIN_6_BIT_ID                       6
#define HIVE_TIMED_CTRL_GPIO_PIN_7_BIT_ID                       7
#define HIVE_TIMED_CTRL_GPIO_PIN_8_BIT_ID                       8
#define HIVE_TIMED_CTRL_GPIO_PIN_9_BIT_ID                       9
#define HIVE_TIMED_CTRL_GPIO_PIN_10_BIT_ID                      10
#define HIVE_TIMED_CTRL_GPIO_PIN_11_BIT_ID                      11
#define HIVE_TIMED_CTRL_IRQ_SP_BIT_ID                           12
#define HIVE_TIMED_CTRL_IRQ_ISP_BIT_ID                          13
#define HIVE_TIMED_CTRL_IRQ_INPUT_SYSTEM_BIT_ID                 14
#define HIVE_TIMED_CTRL_IRQ_INPUT_SELECTOR_BIT_ID               15
#define HIVE_TIMED_CTRL_IRQ_IF_BLOCK_BIT_ID                     16
#define HIVE_TIMED_CTRL_IRQ_GP_TIMER_0_BIT_ID                   17
#define HIVE_TIMED_CTRL_IRQ_GP_TIMER_1_BIT_ID                   18
#define HIVE_TIMED_CTRL_CSI_SOL_BIT_ID                          19
#define HIVE_TIMED_CTRL_CSI_EOL_BIT_ID                          20
#define HIVE_TIMED_CTRL_CSI_SOF_BIT_ID                          21
#define HIVE_TIMED_CTRL_CSI_EOF_BIT_ID                          22
#define HIVE_TIMED_CTRL_IRQ_IS_STREAMING_MONITOR_BIT_ID         23

#endif /* __TIMED_CTRL_GLOBAL_H_INCLUDED__ */

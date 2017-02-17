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

#ifndef _gp_timer_defs_h
#define _gp_timer_defs_h

#define _HRT_GP_TIMER_REG_ALIGN 4

#define HIVE_GP_TIMER_RESET_REG_IDX                              0
#define HIVE_GP_TIMER_OVERALL_ENABLE_REG_IDX                     1
#define HIVE_GP_TIMER_ENABLE_REG_IDX(timer)                     (HIVE_GP_TIMER_OVERALL_ENABLE_REG_IDX + 1 + timer)
#define HIVE_GP_TIMER_VALUE_REG_IDX(timer,timers)               (HIVE_GP_TIMER_ENABLE_REG_IDX(timers) + timer)
#define HIVE_GP_TIMER_COUNT_TYPE_REG_IDX(timer,timers)          (HIVE_GP_TIMER_VALUE_REG_IDX(timers, timers) + timer)
#define HIVE_GP_TIMER_SIGNAL_SELECT_REG_IDX(timer,timers)       (HIVE_GP_TIMER_COUNT_TYPE_REG_IDX(timers, timers) + timer)
#define HIVE_GP_TIMER_IRQ_TRIGGER_VALUE_REG_IDX(irq,timers)     (HIVE_GP_TIMER_SIGNAL_SELECT_REG_IDX(timers, timers) + irq)
#define HIVE_GP_TIMER_IRQ_TIMER_SELECT_REG_IDX(irq,timers,irqs) (HIVE_GP_TIMER_IRQ_TRIGGER_VALUE_REG_IDX(irqs, timers) + irq)
#define HIVE_GP_TIMER_IRQ_ENABLE_REG_IDX(irq,timers,irqs)       (HIVE_GP_TIMER_IRQ_TIMER_SELECT_REG_IDX(irqs, timers, irqs) + irq)

#define HIVE_GP_TIMER_COUNT_TYPE_HIGH                            0
#define HIVE_GP_TIMER_COUNT_TYPE_LOW                             1
#define HIVE_GP_TIMER_COUNT_TYPE_POSEDGE                         2
#define HIVE_GP_TIMER_COUNT_TYPE_NEGEDGE                         3
#define HIVE_GP_TIMER_COUNT_TYPES                                4

#endif /* _gp_timer_defs_h */   

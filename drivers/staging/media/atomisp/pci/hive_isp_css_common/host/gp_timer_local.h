/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 */

#ifndef __GP_TIMER_LOCAL_H_INCLUDED__
#define  __GP_TIMER_LOCAL_H_INCLUDED__

#include "gp_timer_global.h" /*GP_TIMER_SEL
				GP_TIMER_SIGNAL_SELECT*/

#include "gp_timer_defs.h"    /*HIVE_GP_TIMER_xxx registers*/
#include "hive_isp_css_defs.h" /*HIVE_GP_TIMER_NUM_COUNTERS
				 HIVE_GP_TIMER_NUM_IRQS*/

#define _REG_GP_TIMER_RESET_REG HIVE_GP_TIMER_RESET_REG_IDX
#define _REG_GP_TIMER_OVERALL_ENABLE HIVE_GP_TIMER_OVERALL_ENABLE_REG_IDX

/*Register offsets for timers [1,7] can be obtained
 * by adding (GP_TIMERx_ID * sizeof(uint32_t))*/
#define _REG_GP_TIMER_ENABLE_ID(timer_id)        HIVE_GP_TIMER_ENABLE_REG_IDX(timer_id)
#define _REG_GP_TIMER_VALUE_ID(timer_id)	 HIVE_GP_TIMER_VALUE_REG_IDX(timer_id, HIVE_GP_TIMER_NUM_COUNTERS)
#define _REG_GP_TIMER_COUNT_TYPE_ID(timer_id)    HIVE_GP_TIMER_COUNT_TYPE_REG_IDX(timer_id, HIVE_GP_TIMER_NUM_COUNTERS)
#define _REG_GP_TIMER_SIGNAL_SELECT_ID(timer_id) HIVE_GP_TIMER_SIGNAL_SELECT_REG_IDX(timer_id, HIVE_GP_TIMER_NUM_COUNTERS)

#define _REG_GP_TIMER_IRQ_TRIGGER_VALUE_ID(irq_id) HIVE_GP_TIMER_IRQ_TRIGGER_VALUE_REG_IDX(irq_id, HIVE_GP_TIMER_NUM_COUNTERS)

#define _REG_GP_TIMER_IRQ_TIMER_SELECT_ID(irq_id)   \
	HIVE_GP_TIMER_IRQ_TIMER_SELECT_REG_IDX(irq_id, HIVE_GP_TIMER_NUM_COUNTERS, HIVE_GP_TIMER_NUM_IRQS)

#define _REG_GP_TIMER_IRQ_ENABLE_ID(irq_id) \
	HIVE_GP_TIMER_IRQ_ENABLE_REG_IDX(irq_id, HIVE_GP_TIMER_NUM_COUNTERS, HIVE_GP_TIMER_NUM_IRQS)

#endif  /*__GP_TIMER_LOCAL_H_INCLUDED__*/

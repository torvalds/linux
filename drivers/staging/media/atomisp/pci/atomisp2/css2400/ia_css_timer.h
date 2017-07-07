#ifndef ISP2401
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
#else
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#endif

#ifndef __IA_CSS_TIMER_H
#define __IA_CSS_TIMER_H

/** @file
 * Timer interface definitions
 */
#include <type_support.h>		/* for uint32_t */
#include "ia_css_err.h"

/** @brief timer reading definition */
typedef uint32_t clock_value_t;

/** @brief 32 bit clock tick,(timestamp based on timer-value of CSS-internal timer)*/
struct ia_css_clock_tick {
	clock_value_t ticks; /**< measured time in ticks.*/
};

/** @brief TIMER event codes */
enum ia_css_tm_event {
	IA_CSS_TM_EVENT_AFTER_INIT,
	/**< Timer Event after Initialization */
	IA_CSS_TM_EVENT_MAIN_END,
	/**< Timer Event after end of Main */
	IA_CSS_TM_EVENT_THREAD_START,
	/**< Timer Event after thread start */
	IA_CSS_TM_EVENT_FRAME_PROC_START,
	/**< Timer Event after Frame Process Start */
	IA_CSS_TM_EVENT_FRAME_PROC_END
	/**< Timer Event after Frame Process End */
};

/** @brief code measurement common struct */
struct ia_css_time_meas {
	clock_value_t	start_timer_value;	/**< measured time in ticks */
	clock_value_t	end_timer_value;	/**< measured time in ticks */
};

/**@brief SIZE_OF_IA_CSS_CLOCK_TICK_STRUCT checks to ensure correct alignment for struct ia_css_clock_tick. */
#define SIZE_OF_IA_CSS_CLOCK_TICK_STRUCT sizeof(clock_value_t)
/** @brief checks to ensure correct alignment for ia_css_time_meas. */
#define SIZE_OF_IA_CSS_TIME_MEAS_STRUCT (sizeof(clock_value_t) \
					+ sizeof(clock_value_t))

/** @brief API to fetch timer count directly
*
* @param curr_ts [out] measured count value
* @return IA_CSS_SUCCESS if success
*
*/
enum ia_css_err
ia_css_timer_get_current_tick(
	struct ia_css_clock_tick *curr_ts);

#endif  /* __IA_CSS_TIMER_H */

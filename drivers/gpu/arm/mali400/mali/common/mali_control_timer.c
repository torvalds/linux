/*
 * Copyright (C) 2010-2012, 2014-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_utilization.h"
#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_kernel_common.h"
#include "mali_session.h"
#include "mali_dvfs_policy.h"
#include "mali_control_timer.h"

static u64 period_start_time = 0;

/** .KP : mali_control_timer */
static _mali_osk_timer_t *mali_control_timer = NULL;
static mali_bool timer_running = MALI_FALSE;

/**
 * period_of_notifying_mali_utilization_to_platform_dependent_part,
 * ms 为单位.
 */
static u32 mali_control_timeout = 20;

void mali_control_timer_add(u32 timeout)/* 'timeout' : 以 ms 为单位. */
{
	_mali_osk_timer_add(mali_control_timer, _mali_osk_time_mstoticks(timeout));
}

void mali_control_timer_mod(u32 timeout_in_ms)
{
	_mali_osk_timer_mod(mali_control_timer, _mali_osk_time_mstoticks(timeout_in_ms));
}

static void mali_control_timer_callback(void *arg)
{
	if (mali_utilization_enabled()) {
		struct mali_gpu_utilization_data *util_data = NULL;
		u64 time_period = 0;
		mali_bool need_add_timer = MALI_TRUE;

		/* Calculate gpu utilization */
		util_data = mali_utilization_calculate(&period_start_time, &time_period, &need_add_timer);

		if (util_data) {
#if defined(CONFIG_MALI_DVFS)
			mali_dvfs_policy_realize(util_data, time_period);
#else
			mali_utilization_platform_realize(util_data);
#endif

		if (MALI_TRUE == timer_running)
			if (MALI_TRUE == need_add_timer) {
				mali_control_timer_mod(mali_control_timeout);
			}
		}
	}
}

/* Init a timer (for now it is used for GPU utilization and dvfs) */
_mali_osk_errcode_t mali_control_timer_init(void)
{
	_mali_osk_device_data data;

	if (_MALI_OSK_ERR_OK == _mali_osk_device_data_get(&data)) {
		/* Use device specific settings (if defined) */
		if (0 != data.control_interval) {
			mali_control_timeout = data.control_interval;
			MALI_DEBUG_PRINT(2, ("Mali GPU Timer: %u\n", mali_control_timeout));
		}
	}

	mali_control_timer = _mali_osk_timer_init(mali_control_timer_callback);
	if (NULL == mali_control_timer) {
		return _MALI_OSK_ERR_FAULT;
	}
	_mali_osk_timer_setcallback(mali_control_timer, mali_control_timer_callback, NULL);

	return _MALI_OSK_ERR_OK;
}

void mali_control_timer_term(void)
{
	if (NULL != mali_control_timer) {
		_mali_osk_timer_del(mali_control_timer);
		timer_running = MALI_FALSE;
		_mali_osk_timer_term(mali_control_timer);
		mali_control_timer = NULL;
	}
}

mali_bool mali_control_timer_resume(u64 time_now)
{
	mali_utilization_data_assert_locked();

	if (timer_running != MALI_TRUE) {
		timer_running = MALI_TRUE;

		period_start_time = time_now;

		mali_utilization_reset();

		return MALI_TRUE;
	}

	return MALI_FALSE;
}

void mali_control_timer_pause(void)
{
	mali_utilization_data_assert_locked();
	if (timer_running == MALI_TRUE) {
		timer_running = MALI_FALSE;
	}
}

void mali_control_timer_suspend(mali_bool suspend)
{
	mali_utilization_data_lock();

	if (timer_running == MALI_TRUE) {
		timer_running = MALI_FALSE;

		mali_utilization_data_unlock();

		if (suspend == MALI_TRUE) {
			_mali_osk_timer_del(mali_control_timer);
			mali_utilization_reset();
		}
	} else {
		mali_utilization_data_unlock();
	}
}

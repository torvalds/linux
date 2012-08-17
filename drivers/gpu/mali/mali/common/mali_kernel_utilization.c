/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_utilization.h"
#include "mali_osk.h"
#include "mali_platform.h"

/* Define how often to calculate and report GPU utilization, in milliseconds */
#define MALI_GPU_UTILIZATION_TIMEOUT 1000

static _mali_osk_lock_t *time_data_lock;

static _mali_osk_atomic_t num_running_cores;

static u64 period_start_time = 0;
static u64 work_start_time = 0;
static u64 accumulated_work_time = 0;

static _mali_osk_timer_t *utilization_timer = NULL;
static mali_bool timer_running = MALI_FALSE;


static void calculate_gpu_utilization(void* arg)
{
	u64 time_now;
	u64 time_period;
	u32 leading_zeroes;
	u32 shift_val;
	u32 work_normalized;
	u32 period_normalized;
	u32 utilization;

	_mali_osk_lock_wait(time_data_lock, _MALI_OSK_LOCKMODE_RW);

	if (accumulated_work_time == 0 && work_start_time == 0)
	{
		/* Don't reschedule timer, this will be started if new work arrives */
		timer_running = MALI_FALSE;

		_mali_osk_lock_signal(time_data_lock, _MALI_OSK_LOCKMODE_RW);

		/* No work done for this period, report zero usage */
		mali_gpu_utilization_handler(0);

		return;
	}

	time_now = _mali_osk_time_get_ns();
	time_period = time_now - period_start_time;

	/* If we are currently busy, update working period up to now */
	if (work_start_time != 0)
	{
		accumulated_work_time += (time_now - work_start_time);
		work_start_time = time_now;
	}

	/*
	 * We have two 64-bit values, a dividend and a divisor.
	 * To avoid dependencies to a 64-bit divider, we shift down the two values
	 * equally first.
	 * We shift the dividend up and possibly the divisor down, making the result X in 256.
	 */

	/* Shift the 64-bit values down so they fit inside a 32-bit integer */
	leading_zeroes = _mali_osk_clz((u32)(time_period >> 32));
	shift_val = 32 - leading_zeroes;
	work_normalized = (u32)(accumulated_work_time >> shift_val);
	period_normalized = (u32)(time_period >> shift_val);

	/*
	 * Now, we should report the usage in parts of 256
	 * this means we must shift up the dividend or down the divisor by 8
	 * (we could do a combination, but we just use one for simplicity,
	 * but the end result should be good enough anyway)
	 */
	if (period_normalized > 0x00FFFFFF)
	{
		/* The divisor is so big that it is safe to shift it down */
		period_normalized >>= 8;
	}
	else
	{
		/*
		 * The divisor is so small that we can shift up the dividend, without loosing any data.
		 * (dividend is always smaller than the divisor)
		 */
		work_normalized <<= 8;
	}

	utilization = work_normalized / period_normalized;

	accumulated_work_time = 0;
	period_start_time = time_now; /* starting a new period */

	_mali_osk_lock_signal(time_data_lock, _MALI_OSK_LOCKMODE_RW);

	_mali_osk_timer_add(utilization_timer, _mali_osk_time_mstoticks(MALI_GPU_UTILIZATION_TIMEOUT));


	mali_gpu_utilization_handler(utilization);
}

_mali_osk_errcode_t mali_utilization_init(void)
{
	time_data_lock = _mali_osk_lock_init(_MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_SPINLOCK_IRQ |
	                     _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0, _MALI_OSK_LOCK_ORDER_UTILIZATION);

	if (NULL == time_data_lock)
	{
		return _MALI_OSK_ERR_FAULT;
	}

	_mali_osk_atomic_init(&num_running_cores, 0);

	utilization_timer = _mali_osk_timer_init();
	if (NULL == utilization_timer)
	{
		_mali_osk_lock_term(time_data_lock);
		return _MALI_OSK_ERR_FAULT;
	}
	_mali_osk_timer_setcallback(utilization_timer, calculate_gpu_utilization, NULL);

	return _MALI_OSK_ERR_OK;
}

void mali_utilization_suspend(void)
{
	if (NULL != utilization_timer)
	{
		_mali_osk_timer_del(utilization_timer);
		timer_running = MALI_FALSE;
	}
}

void mali_utilization_term(void)
{
	if (NULL != utilization_timer)
	{
		_mali_osk_timer_del(utilization_timer);
		timer_running = MALI_FALSE;
		_mali_osk_timer_term(utilization_timer);
		utilization_timer = NULL;
	}

	_mali_osk_atomic_term(&num_running_cores);

	_mali_osk_lock_term(time_data_lock);
}

void mali_utilization_core_start(u64 time_now)
{
	if (_mali_osk_atomic_inc_return(&num_running_cores) == 1)
	{
		/*
		 * We went from zero cores working, to one core working,
		 * we now consider the entire GPU for being busy
		 */

		_mali_osk_lock_wait(time_data_lock, _MALI_OSK_LOCKMODE_RW);

		if (time_now < period_start_time)
		{
			/*
			 * This might happen if the calculate_gpu_utilization() was able
			 * to run between the sampling of time_now and us grabbing the lock above
			 */
			time_now = period_start_time;
		}

		work_start_time = time_now;
		if (timer_running != MALI_TRUE)
		{
			timer_running = MALI_TRUE;
			period_start_time = work_start_time; /* starting a new period */

			_mali_osk_lock_signal(time_data_lock, _MALI_OSK_LOCKMODE_RW);

			_mali_osk_timer_add(utilization_timer, _mali_osk_time_mstoticks(MALI_GPU_UTILIZATION_TIMEOUT));
		}
		else
		{
			_mali_osk_lock_signal(time_data_lock, _MALI_OSK_LOCKMODE_RW);
		}
	}
}

void mali_utilization_core_end(u64 time_now)
{
	if (_mali_osk_atomic_dec_return(&num_running_cores) == 0)
	{
		/*
		 * No more cores are working, so accumulate the time we was busy.
		 */
		_mali_osk_lock_wait(time_data_lock, _MALI_OSK_LOCKMODE_RW);

		if (time_now < work_start_time)
		{
			/*
			 * This might happen if the calculate_gpu_utilization() was able
			 * to run between the sampling of time_now and us grabbing the lock above
			 */
			time_now = work_start_time;
		}

		accumulated_work_time += (time_now - work_start_time);
		work_start_time = 0;

		_mali_osk_lock_signal(time_data_lock, _MALI_OSK_LOCKMODE_RW);
	}
}

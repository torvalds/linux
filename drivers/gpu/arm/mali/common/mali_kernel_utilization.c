/*
 * Copyright (C) 2010-2014 ARM Limited. All rights reserved.
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
#include "mali_scheduler.h"

#include "mali_executor.h"
#include "mali_dvfs_policy.h"
#include "mali_control_timer.h"

/* Thresholds for GP bound detection. */
#define MALI_GP_BOUND_GP_UTILIZATION_THRESHOLD 240
#define MALI_GP_BOUND_PP_UTILIZATION_THRESHOLD 250

static _mali_osk_spinlock_irq_t *utilization_data_lock;

static u32 num_running_gp_cores = 0;
static u32 num_running_pp_cores = 0;

static u64 work_start_time_gpu = 0;
static u64 work_start_time_gp = 0;
static u64 work_start_time_pp = 0;
static u64 accumulated_work_time_gpu = 0;
static u64 accumulated_work_time_gp = 0;
static u64 accumulated_work_time_pp = 0;

static u32 last_utilization_gpu = 0 ;
static u32 last_utilization_gp = 0 ;
static u32 last_utilization_pp = 0 ;

void (*mali_utilization_callback)(struct mali_gpu_utilization_data *data) = NULL;

/* Define the first timer control timer timeout in milliseconds */
static u32 mali_control_first_timeout = 100;
static struct mali_gpu_utilization_data mali_util_data = {0, };

struct mali_gpu_utilization_data *mali_utilization_calculate(u64 *start_time, u64 *time_period)
{
	u64 time_now;
	u32 leading_zeroes;
	u32 shift_val;
	u32 work_normalized_gpu;
	u32 work_normalized_gp;
	u32 work_normalized_pp;
	u32 period_normalized;
	u32 utilization_gpu;
	u32 utilization_gp;
	u32 utilization_pp;

	mali_utilization_data_lock();

	time_now = _mali_osk_time_get_ns();

	*time_period = time_now - *start_time;

	if (accumulated_work_time_gpu == 0 && work_start_time_gpu == 0) {
		/*
		 * No work done for this period
		 * - No need to reschedule timer
		 * - Report zero usage
		 */
		last_utilization_gpu = 0;
		last_utilization_gp = 0;
		last_utilization_pp = 0;

		mali_util_data.utilization_gpu = last_utilization_gpu;
		mali_util_data.utilization_gp = last_utilization_gp;
		mali_util_data.utilization_pp = last_utilization_pp;

		mali_utilization_data_unlock();

		/* Stop add timer until the next job submited */
		mali_control_timer_suspend(MALI_FALSE);

		mali_executor_hint_disable(MALI_EXECUTOR_HINT_GP_BOUND);

		MALI_DEBUG_PRINT(4, ("last_utilization_gpu = %d \n", last_utilization_gpu));
		MALI_DEBUG_PRINT(4, ("last_utilization_gp = %d \n", last_utilization_gp));
		MALI_DEBUG_PRINT(4, ("last_utilization_pp = %d \n", last_utilization_pp));

		return &mali_util_data;
	}

	/* If we are currently busy, update working period up to now */
	if (work_start_time_gpu != 0) {
		accumulated_work_time_gpu += (time_now - work_start_time_gpu);
		work_start_time_gpu = time_now;

		/* GP and/or PP will also be busy if the GPU is busy at this point */

		if (work_start_time_gp != 0) {
			accumulated_work_time_gp += (time_now - work_start_time_gp);
			work_start_time_gp = time_now;
		}

		if (work_start_time_pp != 0) {
			accumulated_work_time_pp += (time_now - work_start_time_pp);
			work_start_time_pp = time_now;
		}
	}

	/*
	 * We have two 64-bit values, a dividend and a divisor.
	 * To avoid dependencies to a 64-bit divider, we shift down the two values
	 * equally first.
	 * We shift the dividend up and possibly the divisor down, making the result X in 256.
	 */

	/* Shift the 64-bit values down so they fit inside a 32-bit integer */
	leading_zeroes = _mali_osk_clz((u32)(*time_period >> 32));
	shift_val = 32 - leading_zeroes;
	work_normalized_gpu = (u32)(accumulated_work_time_gpu >> shift_val);
	work_normalized_gp = (u32)(accumulated_work_time_gp >> shift_val);
	work_normalized_pp = (u32)(accumulated_work_time_pp >> shift_val);
	period_normalized = (u32)(*time_period >> shift_val);

	/*
	 * Now, we should report the usage in parts of 256
	 * this means we must shift up the dividend or down the divisor by 8
	 * (we could do a combination, but we just use one for simplicity,
	 * but the end result should be good enough anyway)
	 */
	if (period_normalized > 0x00FFFFFF) {
		/* The divisor is so big that it is safe to shift it down */
		period_normalized >>= 8;
	} else {
		/*
		 * The divisor is so small that we can shift up the dividend, without loosing any data.
		 * (dividend is always smaller than the divisor)
		 */
		work_normalized_gpu <<= 8;
		work_normalized_gp <<= 8;
		work_normalized_pp <<= 8;
	}

	utilization_gpu = work_normalized_gpu / period_normalized;
	utilization_gp = work_normalized_gp / period_normalized;
	utilization_pp = work_normalized_pp / period_normalized;

	last_utilization_gpu = utilization_gpu;
	last_utilization_gp = utilization_gp;
	last_utilization_pp = utilization_pp;

	if ((MALI_GP_BOUND_GP_UTILIZATION_THRESHOLD < last_utilization_gp) &&
	    (MALI_GP_BOUND_PP_UTILIZATION_THRESHOLD > last_utilization_pp)) {
		mali_executor_hint_enable(MALI_EXECUTOR_HINT_GP_BOUND);
	} else {
		mali_executor_hint_disable(MALI_EXECUTOR_HINT_GP_BOUND);
	}

	/* starting a new period */
	accumulated_work_time_gpu = 0;
	accumulated_work_time_gp = 0;
	accumulated_work_time_pp = 0;

	*start_time = time_now;

	mali_util_data.utilization_gp = last_utilization_gp;
	mali_util_data.utilization_gpu = last_utilization_gpu;
	mali_util_data.utilization_pp = last_utilization_pp;

	mali_utilization_data_unlock();

	MALI_DEBUG_PRINT(4, ("last_utilization_gpu = %d \n", last_utilization_gpu));
	MALI_DEBUG_PRINT(4, ("last_utilization_gp = %d \n", last_utilization_gp));
	MALI_DEBUG_PRINT(4, ("last_utilization_pp = %d \n", last_utilization_pp));

	return &mali_util_data;
}

_mali_osk_errcode_t mali_utilization_init(void)
{
#if USING_GPU_UTILIZATION
	_mali_osk_device_data data;

	if (_MALI_OSK_ERR_OK == _mali_osk_device_data_get(&data)) {
		if (NULL != data.utilization_callback) {
			mali_utilization_callback = data.utilization_callback;
			MALI_DEBUG_PRINT(2, ("Mali GPU Utilization: Utilization handler installed \n"));
		}
	}
#endif /* defined(USING_GPU_UTILIZATION) */

	if (NULL == mali_utilization_callback) {
		MALI_DEBUG_PRINT(2, ("Mali GPU Utilization: No platform utilization handler installed\n"));
	}

	utilization_data_lock = _mali_osk_spinlock_irq_init(_MALI_OSK_LOCKFLAG_ORDERED, _MALI_OSK_LOCK_ORDER_UTILIZATION);
	if (NULL == utilization_data_lock) {
		return _MALI_OSK_ERR_FAULT;
	}

	num_running_gp_cores = 0;
	num_running_pp_cores = 0;

	return _MALI_OSK_ERR_OK;
}

void mali_utilization_term(void)
{
	if (NULL != utilization_data_lock) {
		_mali_osk_spinlock_irq_term(utilization_data_lock);
	}
}

void mali_utilization_gp_start(void)
{
	mali_utilization_data_lock();

	++num_running_gp_cores;
	if (1 == num_running_gp_cores) {
		u64 time_now = _mali_osk_time_get_ns();

		/* First GP core started, consider GP busy from now and onwards */
		work_start_time_gp = time_now;

		if (0 == num_running_pp_cores) {
			mali_bool is_resume = MALI_FALSE;
			/*
			 * There are no PP cores running, so this is also the point
			 * at which we consider the GPU to be busy as well.
			 */
			work_start_time_gpu = time_now;

			is_resume  = mali_control_timer_resume(time_now);

			mali_utilization_data_unlock();

			if (is_resume) {
				/* Do some policy in new period for performance consideration */
#if defined(CONFIG_MALI_DVFS)
				/* Clear session->number_of_window_jobs, prepare parameter for dvfs */
				mali_session_max_window_num();
				if (0 == last_utilization_gpu) {
					/*
					 * for mali_dev_pause is called in set clock,
					 * so each time we change clock, we will set clock to
					 * highest step even if under down clock case,
					 * it is not nessesary, so we only set the clock under
					 * last time utilization equal 0, we stop the timer then
					 * start the GPU again case
					 */
					mali_dvfs_policy_new_period();
				}
#endif
				/*
				 * First timeout using short interval for power consideration
				 * because we give full power in the new period, but if the
				 * job loading is light, finish in 10ms, the other time all keep
				 * in high freq it will wast time.
				 */
				mali_control_timer_add(mali_control_first_timeout);
			}
		} else {
			mali_utilization_data_unlock();
		}

	} else {
		/* Nothing to do */
		mali_utilization_data_unlock();
	}
}

void mali_utilization_pp_start(void)
{
	mali_utilization_data_lock();

	++num_running_pp_cores;
	if (1 == num_running_pp_cores) {
		u64 time_now = _mali_osk_time_get_ns();

		/* First PP core started, consider PP busy from now and onwards */
		work_start_time_pp = time_now;

		if (0 == num_running_gp_cores) {
			mali_bool is_resume = MALI_FALSE;
			/*
			 * There are no GP cores running, so this is also the point
			 * at which we consider the GPU to be busy as well.
			 */
			work_start_time_gpu = time_now;

			/* Start a new period if stoped */
			is_resume = mali_control_timer_resume(time_now);

			mali_utilization_data_unlock();

			if (is_resume) {
#if defined(CONFIG_MALI_DVFS)
				/* Clear session->number_of_window_jobs, prepare parameter for dvfs */
				mali_session_max_window_num();
				if (0 == last_utilization_gpu) {
					/*
					 * for mali_dev_pause is called in set clock,
					 * so each time we change clock, we will set clock to
					 * highest step even if under down clock case,
					 * it is not nessesary, so we only set the clock under
					 * last time utilization equal 0, we stop the timer then
					 * start the GPU again case
					 */
					mali_dvfs_policy_new_period();
				}
#endif

				/*
				 * First timeout using short interval for power consideration
				 * because we give full power in the new period, but if the
				 * job loading is light, finish in 10ms, the other time all keep
				 * in high freq it will wast time.
				 */
				mali_control_timer_add(mali_control_first_timeout);
			}
		} else {
			mali_utilization_data_unlock();
		}
	} else {
		/* Nothing to do */
		mali_utilization_data_unlock();
	}
}

void mali_utilization_gp_end(void)
{
	mali_utilization_data_lock();

	--num_running_gp_cores;
	if (0 == num_running_gp_cores) {
		u64 time_now = _mali_osk_time_get_ns();

		/* Last GP core ended, consider GP idle from now and onwards */
		accumulated_work_time_gp += (time_now - work_start_time_gp);
		work_start_time_gp = 0;

		if (0 == num_running_pp_cores) {
			/*
			 * There are no PP cores running, so this is also the point
			 * at which we consider the GPU to be idle as well.
			 */
			accumulated_work_time_gpu += (time_now - work_start_time_gpu);
			work_start_time_gpu = 0;
		}
	}

	mali_utilization_data_unlock();
}

void mali_utilization_pp_end(void)
{
	mali_utilization_data_lock();

	--num_running_pp_cores;
	if (0 == num_running_pp_cores) {
		u64 time_now = _mali_osk_time_get_ns();

		/* Last PP core ended, consider PP idle from now and onwards */
		accumulated_work_time_pp += (time_now - work_start_time_pp);
		work_start_time_pp = 0;

		if (0 == num_running_gp_cores) {
			/*
			 * There are no GP cores running, so this is also the point
			 * at which we consider the GPU to be idle as well.
			 */
			accumulated_work_time_gpu += (time_now - work_start_time_gpu);
			work_start_time_gpu = 0;
		}
	}

	mali_utilization_data_unlock();
}

mali_bool mali_utilization_enabled(void)
{
#if defined(CONFIG_MALI_DVFS)
	return mali_dvfs_policy_enabled();
#else
	return (NULL != mali_utilization_callback);
#endif /* defined(CONFIG_MALI_DVFS) */
}

void mali_utilization_platform_realize(struct mali_gpu_utilization_data *util_data)
{
	MALI_DEBUG_ASSERT_POINTER(mali_utilization_callback);

	mali_utilization_callback(util_data);
}

void mali_utilization_reset(void)
{
	accumulated_work_time_gpu = 0;
	accumulated_work_time_gp = 0;
	accumulated_work_time_pp = 0;

	last_utilization_gpu = 0;
	last_utilization_gp = 0;
	last_utilization_pp = 0;
}

void mali_utilization_data_lock(void)
{
	_mali_osk_spinlock_irq_lock(utilization_data_lock);
}

void mali_utilization_data_unlock(void)
{
	_mali_osk_spinlock_irq_unlock(utilization_data_lock);
}

u32 _mali_ukk_utilization_gp_pp(void)
{
	return last_utilization_gpu;
}

u32 _mali_ukk_utilization_gp(void)
{
	return last_utilization_gp;
}

u32 _mali_ukk_utilization_pp(void)
{
	return last_utilization_pp;
}

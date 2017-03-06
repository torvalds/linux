/*
 * Copyright (C) 2010-2012, 2014, 2016-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/mali/mali_utgard.h>
#include "mali_kernel_common.h"
#include "mali_scheduler.h"
#include "mali_dvfs_policy.h"
#include "mali_osk_mali.h"
#include "mali_osk_profiling.h"

#define CLOCK_TUNING_TIME_DEBUG 0

#define MAX_PERFORMANCE_VALUE 256
#define MALI_PERCENTAGE_TO_UTILIZATION_FRACTION(percent) ((int) ((percent)*(MAX_PERFORMANCE_VALUE)/100.0 + 0.5))

/** The max fps the same as display vsync default 60, can set by module insert parameter */
int mali_max_system_fps = 60;
/** A lower limit on their desired FPS default 58, can set by module insert parameter */
int mali_desired_fps = 58;

static int mali_fps_step1 = 0;
static int mali_fps_step2 = 0;

static int clock_step = -1;
static int cur_clk_step = -1;
static struct mali_gpu_clock *gpu_clk = NULL;

/*Function prototype */
static int (*mali_gpu_set_freq)(int) = NULL;
static int (*mali_gpu_get_freq)(void) = NULL;

static mali_bool mali_dvfs_enabled = MALI_FALSE;

#define NUMBER_OF_NANOSECONDS_PER_SECOND  1000000000ULL
static u32 calculate_window_render_fps(u64 time_period)
{
	u32 max_window_number;
	u64 tmp;
	u64 max = time_period;
	u32 leading_zeroes;
	u32 shift_val;
	u32 time_period_shift;
	u32 max_window_number_shift;
	u32 ret_val;

	max_window_number = mali_session_max_window_num();

	/* To avoid float division, extend the dividend to ns unit */
	tmp = (u64)max_window_number * NUMBER_OF_NANOSECONDS_PER_SECOND;
	if (tmp > time_period) {
		max = tmp;
	}

	/*
	 * We may have 64-bit values, a dividend or a divisor or both
	 * To avoid dependencies to a 64-bit divider, we shift down the two values
	 * equally first.
	 */
	leading_zeroes = _mali_osk_clz((u32)(max >> 32));
	shift_val = 32 - leading_zeroes;

	time_period_shift = (u32)(time_period >> shift_val);
	max_window_number_shift = (u32)(tmp >> shift_val);

	ret_val = max_window_number_shift / time_period_shift;

	return ret_val;
}

static bool mali_pickup_closest_avail_clock(int target_clock_mhz, mali_bool pick_clock_up)
{
	int i = 0;
	bool clock_changed = false;

	/* Round up the closest available frequency step for target_clock_hz */
	for (i = 0; i < gpu_clk->num_of_steps; i++) {
		/* Find the first item > target_clock_hz */
		if (((int)(gpu_clk->item[i].clock) - target_clock_mhz) > 0) {
			break;
		}
	}

	/* If the target clock greater than the maximum clock just pick the maximum one*/
	if (i == gpu_clk->num_of_steps) {
		i = gpu_clk->num_of_steps - 1;
	} else {
		if ((!pick_clock_up) && (i > 0)) {
			i = i - 1;
		}
	}

	clock_step = i;
	if (cur_clk_step != clock_step) {
		clock_changed = true;
	}

	return clock_changed;
}

void mali_dvfs_policy_realize(struct mali_gpu_utilization_data *data, u64 time_period)
{
	int under_perform_boundary_value = 0;
	int over_perform_boundary_value = 0;
	int current_fps = 0;
	int current_gpu_util = 0;
	bool clock_changed = false;
#if CLOCK_TUNING_TIME_DEBUG
	struct timeval start;
	struct timeval stop;
	unsigned int elapse_time;
	do_gettimeofday(&start);
#endif
	u32 window_render_fps;

	if (NULL == gpu_clk) {
		MALI_DEBUG_PRINT(2, ("Enable DVFS but patform doesn't Support freq change. \n"));
		return;
	}

	window_render_fps = calculate_window_render_fps(time_period);

	current_fps = window_render_fps;
	current_gpu_util = data->utilization_gpu;

	/* Get the specific under_perform_boundary_value and over_perform_boundary_value */
	if ((mali_desired_fps <= current_fps) && (current_fps < mali_max_system_fps)) {
		under_perform_boundary_value = MALI_PERCENTAGE_TO_UTILIZATION_FRACTION(90);
		over_perform_boundary_value = MALI_PERCENTAGE_TO_UTILIZATION_FRACTION(70);
	} else if ((mali_fps_step1 <= current_fps) && (current_fps < mali_desired_fps)) {
		under_perform_boundary_value = MALI_PERCENTAGE_TO_UTILIZATION_FRACTION(55);
		over_perform_boundary_value = MALI_PERCENTAGE_TO_UTILIZATION_FRACTION(35);
	} else if ((mali_fps_step2 <= current_fps) && (current_fps < mali_fps_step1)) {
		under_perform_boundary_value = MALI_PERCENTAGE_TO_UTILIZATION_FRACTION(70);
		over_perform_boundary_value = MALI_PERCENTAGE_TO_UTILIZATION_FRACTION(50);
	} else {
		under_perform_boundary_value = MALI_PERCENTAGE_TO_UTILIZATION_FRACTION(55);
		over_perform_boundary_value = MALI_PERCENTAGE_TO_UTILIZATION_FRACTION(35);
	}

	MALI_DEBUG_PRINT(5, ("Using ARM power policy: gpu util = %d \n", current_gpu_util));
	MALI_DEBUG_PRINT(5, ("Using ARM power policy: under_perform = %d,  over_perform = %d \n", under_perform_boundary_value, over_perform_boundary_value));
	MALI_DEBUG_PRINT(5, ("Using ARM power policy: render fps = %d,  pressure render fps = %d \n", current_fps, window_render_fps));

	/* Get current clock value */
	cur_clk_step = mali_gpu_get_freq();

	/* Consider offscreen */
	if (0 == current_fps) {
		/* GP or PP under perform, need to give full power */
		if (current_gpu_util > over_perform_boundary_value) {
			if (cur_clk_step != gpu_clk->num_of_steps - 1) {
				clock_changed = true;
				clock_step = gpu_clk->num_of_steps - 1;
			}
		}

		/* If GPU is idle, use lowest power */
		if (0 == current_gpu_util) {
			if (cur_clk_step != 0) {
				clock_changed = true;
				clock_step = 0;
			}
		}

		goto real_setting;
	}

	/* 2. Calculate target clock if the GPU clock can be tuned */
	if (-1 != cur_clk_step) {
		int target_clk_mhz = -1;
		mali_bool pick_clock_up = MALI_TRUE;

		if (current_gpu_util > under_perform_boundary_value) {
			/* when under perform, need to consider the fps part */
			target_clk_mhz = gpu_clk->item[cur_clk_step].clock * current_gpu_util * mali_desired_fps / under_perform_boundary_value / current_fps;
			pick_clock_up = MALI_TRUE;
		} else if (current_gpu_util < over_perform_boundary_value) {
			/* when over perform, did't need to consider fps, system didn't want to reach desired fps */
			target_clk_mhz = gpu_clk->item[cur_clk_step].clock * current_gpu_util / under_perform_boundary_value;
			pick_clock_up = MALI_FALSE;
		}

		if (-1 != target_clk_mhz) {
			clock_changed = mali_pickup_closest_avail_clock(target_clk_mhz, pick_clock_up);
		}
	}

real_setting:
	if (clock_changed) {
		mali_gpu_set_freq(clock_step);

		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
					      MALI_PROFILING_EVENT_CHANNEL_GPU |
					      MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
					      gpu_clk->item[clock_step].clock,
					      gpu_clk->item[clock_step].vol / 1000,
					      0, 0, 0);
	}

#if CLOCK_TUNING_TIME_DEBUG
	do_gettimeofday(&stop);

	elapse_time = timeval_to_ns(&stop) - timeval_to_ns(&start);
	MALI_DEBUG_PRINT(2, ("Using ARM power policy:  eclapse time = %d\n", elapse_time));
#endif
}

_mali_osk_errcode_t mali_dvfs_policy_init(void)
{
	_mali_osk_device_data data;
	_mali_osk_errcode_t err = _MALI_OSK_ERR_OK;

	if (_MALI_OSK_ERR_OK == _mali_osk_device_data_get(&data)) {
		if ((NULL != data.get_clock_info) && (NULL != data.set_freq) && (NULL != data.get_freq)) {
			MALI_DEBUG_PRINT(2, ("Mali DVFS init: using arm dvfs policy \n"));


			mali_fps_step1 = mali_max_system_fps / 3;
			mali_fps_step2 = mali_max_system_fps / 5;

			data.get_clock_info(&gpu_clk);

			if (gpu_clk != NULL) {
#ifdef DEBUG
				int i;
				for (i = 0; i < gpu_clk->num_of_steps; i++) {
					MALI_DEBUG_PRINT(5, ("mali gpu clock info: step%d clock(%d)Hz,vol(%d) \n",
							     i, gpu_clk->item[i].clock, gpu_clk->item[i].vol));
				}
#endif
			} else {
				MALI_DEBUG_PRINT(2, ("Mali DVFS init: platform didn't define enough info for ddk to do DVFS \n"));
			}

			mali_gpu_get_freq = data.get_freq;
			mali_gpu_set_freq = data.set_freq;

			if ((NULL != gpu_clk) && (gpu_clk->num_of_steps > 0)
			    && (NULL != mali_gpu_get_freq) && (NULL != mali_gpu_set_freq)) {
				mali_dvfs_enabled = MALI_TRUE;
			}
		} else {
			MALI_DEBUG_PRINT(2, ("Mali DVFS init: platform function callback incomplete, need check mali_gpu_device_data in platform .\n"));
		}
	} else {
		err = _MALI_OSK_ERR_FAULT;
		MALI_DEBUG_PRINT(2, ("Mali DVFS init: get platform data error .\n"));
	}

	return err;
}

/*
 * Always give full power when start a new period,
 * if mali dvfs enabled, for performance consideration
 */
void mali_dvfs_policy_new_period(void)
{
	/* Always give full power when start a new period */
	unsigned int cur_clk_step = 0;

	cur_clk_step = mali_gpu_get_freq();

	if (cur_clk_step != (gpu_clk->num_of_steps - 1)) {
		mali_gpu_set_freq(gpu_clk->num_of_steps - 1);

		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
					      MALI_PROFILING_EVENT_CHANNEL_GPU |
					      MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE, gpu_clk->item[gpu_clk->num_of_steps - 1].clock,
					      gpu_clk->item[gpu_clk->num_of_steps - 1].vol / 1000, 0, 0, 0);
	}
}

mali_bool mali_dvfs_policy_enabled(void)
{
	return mali_dvfs_enabled;
}

#if defined(CONFIG_MALI400_PROFILING)
void mali_get_current_gpu_clk_item(struct mali_gpu_clk_item *clk_item)
{
	if (mali_platform_device != NULL) {

		struct mali_gpu_device_data *device_data = NULL;
		device_data = (struct mali_gpu_device_data *)mali_platform_device->dev.platform_data;

		if ((NULL != device_data->get_clock_info) && (NULL != device_data->get_freq)) {

			int cur_clk_step = device_data->get_freq();
			struct mali_gpu_clock *mali_gpu_clk = NULL;

			device_data->get_clock_info(&mali_gpu_clk);
			clk_item->clock = mali_gpu_clk->item[cur_clk_step].clock;
			clk_item->vol = mali_gpu_clk->item[cur_clk_step].vol;
		} else {
			MALI_DEBUG_PRINT(2, ("Mali GPU Utilization: platform function callback incomplete, need check mali_gpu_device_data in platform .\n"));
		}
	}
}
#endif


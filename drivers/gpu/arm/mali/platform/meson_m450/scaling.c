/*
 * Copyright (C) 2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file arm_core_scaling.c
 * Example core scaling policy.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/mali/mali_utgard.h>
#include "mali_kernel_common.h"
#include "common/mali_osk_profiling.h"
#include "common/mali_kernel_utilization.h"
#include "common/mali_pp_scheduler.h"

#include <meson_main.h>

#define LOG_MALI_SCALING 0

static int num_cores_enabled;
static int currentStep;
static int lastStep;
static struct work_struct wq_work;
static mali_plat_info_t* pmali_plat = NULL;
static int  scaling_mode = MALI_PP_FS_SCALING;

static void do_scaling(struct work_struct *work)
{
	mali_dvfs_threshold_table * pdvfs = pmali_plat->dvfs_table;
	int err = mali_perf_set_num_pp_cores(num_cores_enabled);
	MALI_DEBUG_ASSERT(0 == err);
	MALI_IGNORE(err);
	if (pdvfs[currentStep].freq_index != pdvfs[lastStep].freq_index) {
		mali_dev_pause();
		mali_clock_set(pdvfs[currentStep].freq_index);
		mali_dev_resume();
		lastStep = currentStep;
	}
#ifdef CONFIG_MALI400_PROFILING
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
					MALI_PROFILING_EVENT_CHANNEL_GPU |
					MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
					get_current_frequency(),
					0,	0,	0,	0);
#endif
}

u32 revise_set_clk(u32 val, u32 flush)
{
	mali_scale_info_t* pinfo;
	u32 ret = 0;

	pinfo = &pmali_plat->scale_info;

	if (val < pinfo->minclk)
		val = pinfo->minclk;
	else if (val >  pinfo->maxclk)
		val =  pinfo->maxclk;

	if (val != currentStep) {
		currentStep = val;
		if (flush)
			schedule_work(&wq_work);
		else
			ret = 1;
	}

	return ret;
}

void get_mali_rt_clkpp(u32* clk, u32* pp)
{
	*clk = currentStep;
	*pp = num_cores_enabled;
}

u32 set_mali_rt_clkpp(u32 clk, u32 pp, u32 flush)
{
	mali_scale_info_t* pinfo;
	u32 ret = 0;
	u32 flush_work = 0;

	pinfo = &pmali_plat->scale_info;
	if (clk < pinfo->minclk)
		clk = pinfo->minclk;
	else if (clk >  pinfo->maxclk)
		clk =  pinfo->maxclk;

	if (clk != currentStep) {
		currentStep = clk;
		if (flush)
			flush_work++;
		else
			ret = 1;
	}
	if (pp < pinfo->minpp)
		pp = pinfo->minpp;
	else if (pp > pinfo->maxpp)
		pp = pinfo->maxpp;

	if (pp != num_cores_enabled) {
		num_cores_enabled = pp;
		if (flush)
			flush_work++;
		else
			ret = 1;
	}

	if (flush_work)
		schedule_work(&wq_work);
	return ret;
}

void revise_mali_rt(void)
{
	set_mali_rt_clkpp(currentStep, num_cores_enabled, 1);
}

void flush_scaling_job(void)
{
	cancel_work_sync(&wq_work);
}

static u32 enable_one_core(void)
{
	return set_mali_rt_clkpp(currentStep, num_cores_enabled + 1, 0);
}

static u32 disable_one_core(void)
{
	return set_mali_rt_clkpp(currentStep, num_cores_enabled - 1, 0);
}

static u32 enable_max_num_cores(void)
{
	return set_mali_rt_clkpp(currentStep, pmali_plat->scale_info.maxpp, 0);
}

static u32 enable_pp_cores(u32 val)
{
	return set_mali_rt_clkpp(currentStep, val, 0);
}

int mali_core_scaling_init(mali_plat_info_t *mali_plat)
{
	if (mali_plat == NULL) {
		printk(" Mali platform data is NULL!!!\n");
		return -1;
	}

	pmali_plat = mali_plat;
	num_cores_enabled = pmali_plat->sc_mpp;

	currentStep = pmali_plat->def_clock;
	lastStep = currentStep;
	INIT_WORK(&wq_work, do_scaling);

	return 0;
	/* NOTE: Mali is not fully initialized at this point. */
}

void mali_core_scaling_term(void)
{
	flush_scheduled_work();
}

static u32 mali_threshold [] = {
	102, /* 40% */
	128, /* 50% */
	230, /* 90% */
};

void mali_pp_scaling_update(struct mali_gpu_utilization_data *data)
{
	int ret = 0;

	if (mali_threshold[2] < data->utilization_pp)
		ret = enable_max_num_cores();
	else if (mali_threshold[1]< data->utilization_pp)
		ret = enable_one_core();
	else if (0 < data->utilization_pp)
		ret = disable_one_core();
	if (ret == 1)
		schedule_work(&wq_work);
}

#if LOG_MALI_SCALING
void trace_utilization(struct mali_gpu_utilization_data *data, u32 current_idx, u32 next,
	u32 current_pp, u32 next_pp)
{
	char direction;
	if (next > current_idx)
		direction = '>';
	else if ((current_idx > pmali_plat->scale_info.minpp) && (next < current_idx))
		direction = '<';
	else
		direction = '~';

	printk("[SCALING]%c (%3d-->%3d)@%3d{%3d - %3d}. pp:(%d-->%d)\n",
				direction,
				get_mali_freq(current_idx),
				get_mali_freq(next),
				data->utilization_gpu,
				pmali_plat->dvfs_table[current_idx].downthreshold,
				pmali_plat->dvfs_table[current_idx].upthreshold,
				current_pp, next_pp);
}
#endif

static int mali_stay_count = 0;
static void mali_decide_next_status(struct mali_gpu_utilization_data *data, int* next_fs_idx,
								int* pp_change_flag)
{
	u32 utilization, mali_up_limit, decided_fs_idx;
	u32 ld_left, ld_right;
	u32 ld_up, ld_down;
	char change_mode;

	*pp_change_flag = 0;
	change_mode = 0;
	utilization = data->utilization_gpu;

	mali_up_limit = (scaling_mode ==  MALI_TURBO_MODE) ?
				pmali_plat->turbo_clock : pmali_plat->scale_info.maxclk;
	decided_fs_idx = currentStep;

	ld_up = pmali_plat->dvfs_table[currentStep].upthreshold;
	ld_down = pmali_plat->dvfs_table[currentStep].downthreshold;
	if (utilization >= ld_up) { /* go up */
		if (currentStep < mali_up_limit) {
			change_mode = 1;
			if ((currentStep < pmali_plat->def_clock) && (utilization > pmali_plat->bst_gpu))
				decided_fs_idx = pmali_plat->def_clock;
			else
				decided_fs_idx++;
		}
		if ((data->utilization_pp > ld_up) &&
				(num_cores_enabled < pmali_plat->scale_info.maxpp)) {
			if ((num_cores_enabled < pmali_plat->sc_mpp) && (data->utilization_pp >= pmali_plat->bst_pp)) {
				*pp_change_flag = 1;
				change_mode = 1;
			} else if (change_mode == 0) {
				*pp_change_flag = 2;
				change_mode = 1;
			}
		}
	} else if (utilization <= ld_down) { /* go down */
		if (mali_stay_count > 0) {
			*next_fs_idx = decided_fs_idx;
			mali_stay_count--;
			return;
		}

		if (num_cores_enabled > pmali_plat->sc_mpp) {
			change_mode = 1;
			if (data->utilization_pp <= ld_down) {
				ld_left = data->utilization_pp * num_cores_enabled;
				ld_right = (pmali_plat->dvfs_table[currentStep].upthreshold) *
								(num_cores_enabled - 1);
				if (ld_left < ld_right) {
					change_mode = 2;
				}
			}
		} else if (currentStep > pmali_plat->scale_info.minpp) {
			change_mode = 1;
		} else if (num_cores_enabled > 1) { /* decrease PPS */
			if (data->utilization_pp <= ld_down) {
				ld_left = data->utilization_pp * num_cores_enabled;
				ld_right = (pmali_plat->dvfs_table[currentStep].upthreshold) *
								(num_cores_enabled - 1);
				if (ld_left < ld_right) {
					change_mode = 2;
				}
			}
		}

		if (change_mode == 1) {
			decided_fs_idx--;
		} else if (change_mode == 2) { /* decrease PPS */
			*pp_change_flag = -1;
		}
	}
	if (change_mode)
		mali_stay_count = pmali_plat->dvfs_table[decided_fs_idx].keep_count;
	*next_fs_idx = decided_fs_idx;
}

void mali_pp_fs_scaling_update(struct mali_gpu_utilization_data *data)
{
	int ret = 0;
	int pp_change_flag = 0;
	u32 next_idx = 0;
	
#if LOG_MALI_SCALING
	u32 last_pp = num_cores_enabled;
#endif
	mali_decide_next_status(data, &next_idx, &pp_change_flag);

	if (pp_change_flag == 1)
		ret = enable_pp_cores(pmali_plat->sc_mpp);
	else if (pp_change_flag == 2)
		ret = enable_one_core();
	else if (pp_change_flag == -1) {
		ret = disable_one_core();
	}

#if LOG_MALI_SCALING
	if (pp_change_flag || (next_idx != currentStep))
		trace_utilization(data, currentStep, next_idx, last_pp, num_cores_enabled);
#endif

	if (next_idx != currentStep) {
		ret = 1;
		currentStep = next_idx;
	}

	if (ret == 1)
		schedule_work(&wq_work);
#ifdef CONFIG_MALI400_PROFILING
	else
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
						MALI_PROFILING_EVENT_CHANNEL_GPU |
						MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
						get_current_frequency(),
						0,	0,	0,	0);
#endif
}

u32 get_mali_schel_mode(void)
{
	return scaling_mode;
}

void set_mali_schel_mode(u32 mode)
{
	MALI_DEBUG_ASSERT(mode < MALI_SCALING_MODE_MAX);
	if (mode >= MALI_SCALING_MODE_MAX)
		return;
	scaling_mode = mode;

	/* set default performance range. */
	pmali_plat->scale_info.minclk = pmali_plat->cfg_min_clock;
	pmali_plat->scale_info.maxclk = pmali_plat->cfg_clock;
	pmali_plat->scale_info.minpp = pmali_plat->cfg_min_pp;
	pmali_plat->scale_info.maxpp = pmali_plat->cfg_pp;

	/* set current status and tune max freq */
	if (scaling_mode == MALI_PP_FS_SCALING) {
		pmali_plat->scale_info.maxclk = pmali_plat->cfg_clock;
		enable_pp_cores(pmali_plat->sc_mpp);
	} else if (scaling_mode == MALI_SCALING_DISABLE) {
		pmali_plat->scale_info.maxclk = pmali_plat->cfg_clock;
		enable_max_num_cores();
	} else if (scaling_mode == MALI_TURBO_MODE) {
		pmali_plat->scale_info.maxclk = pmali_plat->turbo_clock;
		enable_max_num_cores();
	}
	currentStep = pmali_plat->scale_info.maxclk;
	schedule_work(&wq_work);
}

u32 get_current_frequency(void)
{
	return get_mali_freq(currentStep);
}

void mali_gpu_utilization_callback(struct mali_gpu_utilization_data *data)
{
	switch (scaling_mode) {
	case MALI_PP_FS_SCALING:
		mali_pp_fs_scaling_update(data);
		break;
	case MALI_PP_SCALING:
		mali_pp_scaling_update(data);
		break;
	default:
		break;
	}
}

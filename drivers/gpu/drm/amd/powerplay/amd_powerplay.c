/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include "amd_shared.h"
#include "amd_powerplay.h"

static int pp_early_init(void *handle)
{
	return 0;
}

static int pp_sw_init(void *handle)
{
	return 0;
}

static int pp_sw_fini(void *handle)
{
	return 0;
}

static int pp_hw_init(void *handle)
{
	return 0;
}

static int pp_hw_fini(void *handle)
{
	return 0;
}

static bool pp_is_idle(void *handle)
{
	return 0;
}

static int pp_wait_for_idle(void *handle)
{
	return 0;
}

static int pp_sw_reset(void *handle)
{
	return 0;
}

static void pp_print_status(void *handle)
{

}

static int pp_set_clockgating_state(void *handle,
				    enum amd_clockgating_state state)
{
	return 0;
}

static int pp_set_powergating_state(void *handle,
				    enum amd_powergating_state state)
{
	return 0;
}

static int pp_suspend(void *handle)
{
	return 0;
}

static int pp_resume(void *handle)
{
	return 0;
}

const struct amd_ip_funcs pp_ip_funcs = {
	.early_init = pp_early_init,
	.late_init = NULL,
	.sw_init = pp_sw_init,
	.sw_fini = pp_sw_fini,
	.hw_init = pp_hw_init,
	.hw_fini = pp_hw_fini,
	.suspend = pp_suspend,
	.resume = pp_resume,
	.is_idle = pp_is_idle,
	.wait_for_idle = pp_wait_for_idle,
	.soft_reset = pp_sw_reset,
	.print_status = pp_print_status,
	.set_clockgating_state = pp_set_clockgating_state,
	.set_powergating_state = pp_set_powergating_state,
};

static int pp_dpm_load_fw(void *handle)
{
	return 0;
}

static int pp_dpm_fw_loading_complete(void *handle)
{
	return 0;
}

static int pp_dpm_force_performance_level(void *handle,
					enum amd_dpm_forced_level level)
{
	return 0;
}
static enum amd_dpm_forced_level pp_dpm_get_performance_level(
								void *handle)
{
	return 0;
}
static int pp_dpm_get_sclk(void *handle, bool low)
{
	return 0;
}
static int pp_dpm_get_mclk(void *handle, bool low)
{
	return 0;
}
static int pp_dpm_powergate_vce(void *handle, bool gate)
{
	return 0;
}
static int pp_dpm_powergate_uvd(void *handle, bool gate)
{
	return 0;
}

int pp_dpm_dispatch_tasks(void *handle, enum amd_pp_event event_id, void *input, void *output)
{
	return 0;
}
enum amd_pm_state_type pp_dpm_get_current_power_state(void *handle)
{
	return 0;
}
static void
pp_debugfs_print_current_performance_level(void *handle,
					       struct seq_file *m)
{
	return;
}
const struct amd_powerplay_funcs pp_dpm_funcs = {
	.get_temperature = NULL,
	.load_firmware = pp_dpm_load_fw,
	.wait_for_fw_loading_complete = pp_dpm_fw_loading_complete,
	.force_performance_level = pp_dpm_force_performance_level,
	.get_performance_level = pp_dpm_get_performance_level,
	.get_current_power_state = pp_dpm_get_current_power_state,
	.get_sclk = pp_dpm_get_sclk,
	.get_mclk = pp_dpm_get_mclk,
	.powergate_vce = pp_dpm_powergate_vce,
	.powergate_uvd = pp_dpm_powergate_uvd,
	.dispatch_tasks = pp_dpm_dispatch_tasks,
	.print_current_performance_level = pp_debugfs_print_current_performance_level,
};

int amd_powerplay_init(struct amd_pp_init *pp_init,
		       struct amd_powerplay *amd_pp)
{
	if (pp_init == NULL || amd_pp == NULL)
		return -EINVAL;

	amd_pp->ip_funcs = &pp_ip_funcs;
	amd_pp->pp_funcs = &pp_dpm_funcs;

	return 0;
}

int amd_powerplay_fini(void *handle)
{
	return 0;
}

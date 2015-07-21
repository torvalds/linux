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
#include <linux/slab.h>
#include "amd_shared.h"
#include "amd_powerplay.h"
#include "pp_instance.h"

static int pp_early_init(void *handle)
{
	return 0;
}

static int pp_sw_init(void *handle)
{
	struct pp_instance *pp_handle;
	struct pp_hwmgr  *hwmgr;
	int ret = 0;

	if (handle == NULL)
		return -EINVAL;

	pp_handle = (struct pp_instance *)handle;
	hwmgr = pp_handle->hwmgr;

	if (hwmgr == NULL || hwmgr->pptable_func == NULL ||
	    hwmgr->hwmgr_func == NULL ||
	    hwmgr->pptable_func->pptable_init == NULL ||
	    hwmgr->hwmgr_func->backend_init == NULL)
		return -EINVAL;

	ret = hwmgr->pptable_func->pptable_init(hwmgr);
	if (ret == 0)
		ret = hwmgr->hwmgr_func->backend_init(hwmgr);

	return ret;
}

static int pp_sw_fini(void *handle)
{
	struct pp_instance *pp_handle;
	struct pp_hwmgr  *hwmgr;
	int ret = 0;

	if (handle == NULL)
		return -EINVAL;

	pp_handle = (struct pp_instance *)handle;
	hwmgr = pp_handle->hwmgr;

	if (hwmgr != NULL || hwmgr->hwmgr_func != NULL ||
	    hwmgr->hwmgr_func->backend_fini != NULL)
		ret = hwmgr->hwmgr_func->backend_fini(hwmgr);

	return ret;
}

static int pp_hw_init(void *handle)
{
	struct pp_instance *pp_handle;
	struct pp_smumgr *smumgr;
	int ret = 0;

	if (handle == NULL)
		return -EINVAL;

	pp_handle = (struct pp_instance *)handle;
	smumgr = pp_handle->smu_mgr;

	if (smumgr == NULL || smumgr->smumgr_funcs == NULL ||
		smumgr->smumgr_funcs->smu_init == NULL ||
		smumgr->smumgr_funcs->start_smu == NULL)
		return -EINVAL;

	ret = smumgr->smumgr_funcs->smu_init(smumgr);
	if (ret) {
		printk(KERN_ERR "[ powerplay ] smc initialization failed\n");
		return ret;
	}

	ret = smumgr->smumgr_funcs->start_smu(smumgr);
	if (ret) {
		printk(KERN_ERR "[ powerplay ] smc start failed\n");
		smumgr->smumgr_funcs->smu_fini(smumgr);
		return ret;
	}
	hw_init_power_state_table(pp_handle->hwmgr);

	return 0;
}

static int pp_hw_fini(void *handle)
{
	struct pp_instance *pp_handle;
	struct pp_smumgr *smumgr;

	if (handle == NULL)
		return -EINVAL;

	pp_handle = (struct pp_instance *)handle;
	smumgr = pp_handle->smu_mgr;

	if (smumgr != NULL || smumgr->smumgr_funcs != NULL ||
		smumgr->smumgr_funcs->smu_fini != NULL)
		smumgr->smumgr_funcs->smu_fini(smumgr);

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

static int amd_pp_instance_init(struct amd_pp_init *pp_init,
				struct amd_powerplay *amd_pp)
{
	int ret;
	struct pp_instance *handle;

	handle = kzalloc(sizeof(struct pp_instance), GFP_KERNEL);
	if (handle == NULL)
		return -ENOMEM;

	ret = smum_init(pp_init, handle);
	if (ret)
		goto fail_smum;

	ret = hwmgr_init(pp_init, handle);
	if (ret)
		goto fail_hwmgr;

	amd_pp->pp_handle = handle;
	return 0;

fail_hwmgr:
	smum_fini(handle->smu_mgr);
fail_smum:
	kfree(handle);
	return ret;
}

static int amd_pp_instance_fini(void *handle)
{
	struct pp_instance *instance = (struct pp_instance *)handle;
	if (instance == NULL)
		return -EINVAL;

	hwmgr_fini(instance->hwmgr);

	smum_fini(instance->smu_mgr);

	kfree(handle);
	return 0;
}

int amd_powerplay_init(struct amd_pp_init *pp_init,
		       struct amd_powerplay *amd_pp)
{
	int ret;

	if (pp_init == NULL || amd_pp == NULL)
		return -EINVAL;

	ret = amd_pp_instance_init(pp_init, amd_pp);

	if (ret)
		return ret;

	amd_pp->ip_funcs = &pp_ip_funcs;
	amd_pp->pp_funcs = &pp_dpm_funcs;

	return 0;
}

int amd_powerplay_fini(void *handle)
{
	amd_pp_instance_fini(handle);

	return 0;
}

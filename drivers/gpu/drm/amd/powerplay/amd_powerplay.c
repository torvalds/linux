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
#include "pp_debug.h"
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include "amd_shared.h"
#include "amd_powerplay.h"
#include "power_state.h"
#include "amdgpu.h"
#include "hwmgr.h"


static const struct amd_pm_funcs pp_dpm_funcs;

static int amd_powerplay_create(struct amdgpu_device *adev)
{
	struct pp_hwmgr *hwmgr;

	if (adev == NULL)
		return -EINVAL;

	hwmgr = kzalloc(sizeof(struct pp_hwmgr), GFP_KERNEL);
	if (hwmgr == NULL)
		return -ENOMEM;

	hwmgr->adev = adev;
	hwmgr->not_vf = !amdgpu_sriov_vf(adev);
	hwmgr->pm_en = (amdgpu_dpm && hwmgr->not_vf) ? true : false;
	hwmgr->device = amdgpu_cgs_create_device(adev);
	mutex_init(&hwmgr->smu_lock);
	hwmgr->chip_family = adev->family;
	hwmgr->chip_id = adev->asic_type;
	hwmgr->feature_mask = amdgpu_pp_feature_mask;
	hwmgr->display_config = &adev->pm.pm_display_cfg;
	adev->powerplay.pp_handle = hwmgr;
	adev->powerplay.pp_funcs = &pp_dpm_funcs;
	return 0;
}


static void amd_powerplay_destroy(struct amdgpu_device *adev)
{
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;

	kfree(hwmgr->hardcode_pp_table);
	hwmgr->hardcode_pp_table = NULL;

	kfree(hwmgr);
	hwmgr = NULL;
}

static int pp_early_init(void *handle)
{
	int ret;
	struct amdgpu_device *adev = handle;

	ret = amd_powerplay_create(adev);

	if (ret != 0)
		return ret;

	ret = hwmgr_early_init(adev->powerplay.pp_handle);
	if (ret)
		return -EINVAL;

	return 0;
}

static int pp_sw_init(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;
	int ret = 0;

	ret = hwmgr_sw_init(hwmgr);

	pr_debug("powerplay sw init %s\n", ret ? "failed" : "successfully");

	return ret;
}

static int pp_sw_fini(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;

	hwmgr_sw_fini(hwmgr);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_SMU) {
		release_firmware(adev->pm.fw);
		adev->pm.fw = NULL;
		amdgpu_ucode_fini_bo(adev);
	}

	return 0;
}

static int pp_hw_init(void *handle)
{
	int ret = 0;
	struct amdgpu_device *adev = handle;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_SMU)
		amdgpu_ucode_init_bo(adev);

	ret = hwmgr_hw_init(hwmgr);

	if (ret)
		pr_err("powerplay hw init failed\n");

	return ret;
}

static int pp_hw_fini(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;

	hwmgr_hw_fini(hwmgr);

	return 0;
}

static int pp_late_init(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;

	if (hwmgr && hwmgr->pm_en) {
		mutex_lock(&hwmgr->smu_lock);
		hwmgr_handle_task(hwmgr,
					AMD_PP_TASK_COMPLETE_INIT, NULL);
		mutex_unlock(&hwmgr->smu_lock);
	}
	return 0;
}

static void pp_late_fini(void *handle)
{
	struct amdgpu_device *adev = handle;

	amd_powerplay_destroy(adev);
}


static bool pp_is_idle(void *handle)
{
	return false;
}

static int pp_wait_for_idle(void *handle)
{
	return 0;
}

static int pp_sw_reset(void *handle)
{
	return 0;
}

static int pp_set_powergating_state(void *handle,
				    enum amd_powergating_state state)
{
	struct amdgpu_device *adev = handle;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;

	if (!hwmgr || !hwmgr->pm_en)
		return 0;

	if (hwmgr->hwmgr_func->enable_per_cu_power_gating == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return 0;
	}

	/* Enable/disable GFX per cu powergating through SMU */
	return hwmgr->hwmgr_func->enable_per_cu_power_gating(hwmgr,
			state == AMD_PG_STATE_GATE);
}

static int pp_suspend(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;

	return hwmgr_suspend(hwmgr);
}

static int pp_resume(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;

	return hwmgr_resume(hwmgr);
}

static int pp_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	return 0;
}

static const struct amd_ip_funcs pp_ip_funcs = {
	.name = "powerplay",
	.early_init = pp_early_init,
	.late_init = pp_late_init,
	.sw_init = pp_sw_init,
	.sw_fini = pp_sw_fini,
	.hw_init = pp_hw_init,
	.hw_fini = pp_hw_fini,
	.late_fini = pp_late_fini,
	.suspend = pp_suspend,
	.resume = pp_resume,
	.is_idle = pp_is_idle,
	.wait_for_idle = pp_wait_for_idle,
	.soft_reset = pp_sw_reset,
	.set_clockgating_state = pp_set_clockgating_state,
	.set_powergating_state = pp_set_powergating_state,
};

const struct amdgpu_ip_block_version pp_smu_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_SMC,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &pp_ip_funcs,
};

static int pp_dpm_load_fw(void *handle)
{
	return 0;
}

static int pp_dpm_fw_loading_complete(void *handle)
{
	return 0;
}

static int pp_set_clockgating_by_smu(void *handle, uint32_t msg_id)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->update_clock_gatings == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->update_clock_gatings(hwmgr, &msg_id);
}

static void pp_dpm_en_umd_pstate(struct pp_hwmgr  *hwmgr,
						enum amd_dpm_forced_level *level)
{
	uint32_t profile_mode_mask = AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD |
					AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK |
					AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK |
					AMD_DPM_FORCED_LEVEL_PROFILE_PEAK;

	if (!(hwmgr->dpm_level & profile_mode_mask)) {
		/* enter umd pstate, save current level, disable gfx cg*/
		if (*level & profile_mode_mask) {
			hwmgr->saved_dpm_level = hwmgr->dpm_level;
			hwmgr->en_umd_pstate = true;
			cgs_set_clockgating_state(hwmgr->device,
						AMD_IP_BLOCK_TYPE_GFX,
						AMD_CG_STATE_UNGATE);
			cgs_set_powergating_state(hwmgr->device,
					AMD_IP_BLOCK_TYPE_GFX,
					AMD_PG_STATE_UNGATE);
		}
	} else {
		/* exit umd pstate, restore level, enable gfx cg*/
		if (!(*level & profile_mode_mask)) {
			if (*level == AMD_DPM_FORCED_LEVEL_PROFILE_EXIT)
				*level = hwmgr->saved_dpm_level;
			hwmgr->en_umd_pstate = false;
			cgs_set_clockgating_state(hwmgr->device,
					AMD_IP_BLOCK_TYPE_GFX,
					AMD_CG_STATE_GATE);
			cgs_set_powergating_state(hwmgr->device,
					AMD_IP_BLOCK_TYPE_GFX,
					AMD_PG_STATE_GATE);
		}
	}
}

static int pp_dpm_force_performance_level(void *handle,
					enum amd_dpm_forced_level level)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (level == hwmgr->dpm_level)
		return 0;

	mutex_lock(&hwmgr->smu_lock);
	pp_dpm_en_umd_pstate(hwmgr, &level);
	hwmgr->request_dpm_level = level;
	hwmgr_handle_task(hwmgr, AMD_PP_TASK_READJUST_POWER_STATE, NULL);
	mutex_unlock(&hwmgr->smu_lock);

	return 0;
}

static enum amd_dpm_forced_level pp_dpm_get_performance_level(
								void *handle)
{
	struct pp_hwmgr *hwmgr = handle;
	enum amd_dpm_forced_level level;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);
	level = hwmgr->dpm_level;
	mutex_unlock(&hwmgr->smu_lock);
	return level;
}

static uint32_t pp_dpm_get_sclk(void *handle, bool low)
{
	struct pp_hwmgr *hwmgr = handle;
	uint32_t clk = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return 0;

	if (hwmgr->hwmgr_func->get_sclk == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return 0;
	}
	mutex_lock(&hwmgr->smu_lock);
	clk = hwmgr->hwmgr_func->get_sclk(hwmgr, low);
	mutex_unlock(&hwmgr->smu_lock);
	return clk;
}

static uint32_t pp_dpm_get_mclk(void *handle, bool low)
{
	struct pp_hwmgr *hwmgr = handle;
	uint32_t clk = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return 0;

	if (hwmgr->hwmgr_func->get_mclk == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return 0;
	}
	mutex_lock(&hwmgr->smu_lock);
	clk = hwmgr->hwmgr_func->get_mclk(hwmgr, low);
	mutex_unlock(&hwmgr->smu_lock);
	return clk;
}

static void pp_dpm_powergate_vce(void *handle, bool gate)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return;

	if (hwmgr->hwmgr_func->powergate_vce == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return;
	}
	mutex_lock(&hwmgr->smu_lock);
	hwmgr->hwmgr_func->powergate_vce(hwmgr, gate);
	mutex_unlock(&hwmgr->smu_lock);
}

static void pp_dpm_powergate_uvd(void *handle, bool gate)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return;

	if (hwmgr->hwmgr_func->powergate_uvd == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return;
	}
	mutex_lock(&hwmgr->smu_lock);
	hwmgr->hwmgr_func->powergate_uvd(hwmgr, gate);
	mutex_unlock(&hwmgr->smu_lock);
}

static int pp_dpm_dispatch_tasks(void *handle, enum amd_pp_task task_id,
		enum amd_pm_state_type *user_state)
{
	int ret = 0;
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);
	ret = hwmgr_handle_task(hwmgr, task_id, user_state);
	mutex_unlock(&hwmgr->smu_lock);

	return ret;
}

static enum amd_pm_state_type pp_dpm_get_current_power_state(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;
	struct pp_power_state *state;
	enum amd_pm_state_type pm_type;

	if (!hwmgr || !hwmgr->pm_en || !hwmgr->current_ps)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);

	state = hwmgr->current_ps;

	switch (state->classification.ui_label) {
	case PP_StateUILabel_Battery:
		pm_type = POWER_STATE_TYPE_BATTERY;
		break;
	case PP_StateUILabel_Balanced:
		pm_type = POWER_STATE_TYPE_BALANCED;
		break;
	case PP_StateUILabel_Performance:
		pm_type = POWER_STATE_TYPE_PERFORMANCE;
		break;
	default:
		if (state->classification.flags & PP_StateClassificationFlag_Boot)
			pm_type = POWER_STATE_TYPE_INTERNAL_BOOT;
		else
			pm_type = POWER_STATE_TYPE_DEFAULT;
		break;
	}
	mutex_unlock(&hwmgr->smu_lock);

	return pm_type;
}

static void pp_dpm_set_fan_control_mode(void *handle, uint32_t mode)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return;

	if (hwmgr->hwmgr_func->set_fan_control_mode == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return;
	}
	mutex_lock(&hwmgr->smu_lock);
	hwmgr->hwmgr_func->set_fan_control_mode(hwmgr, mode);
	mutex_unlock(&hwmgr->smu_lock);
}

static uint32_t pp_dpm_get_fan_control_mode(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;
	uint32_t mode = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return 0;

	if (hwmgr->hwmgr_func->get_fan_control_mode == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return 0;
	}
	mutex_lock(&hwmgr->smu_lock);
	mode = hwmgr->hwmgr_func->get_fan_control_mode(hwmgr);
	mutex_unlock(&hwmgr->smu_lock);
	return mode;
}

static int pp_dpm_set_fan_speed_percent(void *handle, uint32_t percent)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_fan_speed_percent == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return 0;
	}
	mutex_lock(&hwmgr->smu_lock);
	ret = hwmgr->hwmgr_func->set_fan_speed_percent(hwmgr, percent);
	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_dpm_get_fan_speed_percent(void *handle, uint32_t *speed)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->get_fan_speed_percent == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return 0;
	}

	mutex_lock(&hwmgr->smu_lock);
	ret = hwmgr->hwmgr_func->get_fan_speed_percent(hwmgr, speed);
	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_dpm_get_fan_speed_rpm(void *handle, uint32_t *rpm)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->get_fan_speed_rpm == NULL)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);
	ret = hwmgr->hwmgr_func->get_fan_speed_rpm(hwmgr, rpm);
	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_dpm_get_pp_num_states(void *handle,
		struct pp_states_info *data)
{
	struct pp_hwmgr *hwmgr = handle;
	int i;

	memset(data, 0, sizeof(*data));

	if (!hwmgr || !hwmgr->pm_en ||!hwmgr->ps)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);

	data->nums = hwmgr->num_ps;

	for (i = 0; i < hwmgr->num_ps; i++) {
		struct pp_power_state *state = (struct pp_power_state *)
				((unsigned long)hwmgr->ps + i * hwmgr->ps_size);
		switch (state->classification.ui_label) {
		case PP_StateUILabel_Battery:
			data->states[i] = POWER_STATE_TYPE_BATTERY;
			break;
		case PP_StateUILabel_Balanced:
			data->states[i] = POWER_STATE_TYPE_BALANCED;
			break;
		case PP_StateUILabel_Performance:
			data->states[i] = POWER_STATE_TYPE_PERFORMANCE;
			break;
		default:
			if (state->classification.flags & PP_StateClassificationFlag_Boot)
				data->states[i] = POWER_STATE_TYPE_INTERNAL_BOOT;
			else
				data->states[i] = POWER_STATE_TYPE_DEFAULT;
		}
	}
	mutex_unlock(&hwmgr->smu_lock);
	return 0;
}

static int pp_dpm_get_pp_table(void *handle, char **table)
{
	struct pp_hwmgr *hwmgr = handle;
	int size = 0;

	if (!hwmgr || !hwmgr->pm_en ||!hwmgr->soft_pp_table)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);
	*table = (char *)hwmgr->soft_pp_table;
	size = hwmgr->soft_pp_table_size;
	mutex_unlock(&hwmgr->smu_lock);
	return size;
}

static int amd_powerplay_reset(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret;

	ret = hwmgr_hw_fini(hwmgr);
	if (ret)
		return ret;

	ret = hwmgr_hw_init(hwmgr);
	if (ret)
		return ret;

	return hwmgr_handle_task(hwmgr, AMD_PP_TASK_COMPLETE_INIT, NULL);
}

static int pp_dpm_set_pp_table(void *handle, const char *buf, size_t size)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = -ENOMEM;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);
	if (!hwmgr->hardcode_pp_table) {
		hwmgr->hardcode_pp_table = kmemdup(hwmgr->soft_pp_table,
						   hwmgr->soft_pp_table_size,
						   GFP_KERNEL);
		if (!hwmgr->hardcode_pp_table)
			goto err;
	}

	memcpy(hwmgr->hardcode_pp_table, buf, size);

	hwmgr->soft_pp_table = hwmgr->hardcode_pp_table;

	ret = amd_powerplay_reset(handle);
	if (ret)
		goto err;

	if (hwmgr->hwmgr_func->avfs_control) {
		ret = hwmgr->hwmgr_func->avfs_control(hwmgr, false);
		if (ret)
			goto err;
	}
	mutex_unlock(&hwmgr->smu_lock);
	return 0;
err:
	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_dpm_force_clock_level(void *handle,
		enum pp_clock_type type, uint32_t mask)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->force_clock_level == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return 0;
	}
	mutex_lock(&hwmgr->smu_lock);
	if (hwmgr->dpm_level == AMD_DPM_FORCED_LEVEL_MANUAL)
		ret = hwmgr->hwmgr_func->force_clock_level(hwmgr, type, mask);
	else
		ret = -EINVAL;
	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_dpm_print_clock_levels(void *handle,
		enum pp_clock_type type, char *buf)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->print_clock_levels == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return 0;
	}
	mutex_lock(&hwmgr->smu_lock);
	ret = hwmgr->hwmgr_func->print_clock_levels(hwmgr, type, buf);
	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_dpm_get_sclk_od(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->get_sclk_od == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return 0;
	}
	mutex_lock(&hwmgr->smu_lock);
	ret = hwmgr->hwmgr_func->get_sclk_od(hwmgr);
	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_dpm_set_sclk_od(void *handle, uint32_t value)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_sclk_od == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return 0;
	}

	mutex_lock(&hwmgr->smu_lock);
	ret = hwmgr->hwmgr_func->set_sclk_od(hwmgr, value);
	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_dpm_get_mclk_od(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->get_mclk_od == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return 0;
	}
	mutex_lock(&hwmgr->smu_lock);
	ret = hwmgr->hwmgr_func->get_mclk_od(hwmgr);
	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_dpm_set_mclk_od(void *handle, uint32_t value)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_mclk_od == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return 0;
	}
	mutex_lock(&hwmgr->smu_lock);
	ret = hwmgr->hwmgr_func->set_mclk_od(hwmgr, value);
	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_dpm_read_sensor(void *handle, int idx,
			      void *value, int *size)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en || !value)
		return -EINVAL;

	switch (idx) {
	case AMDGPU_PP_SENSOR_STABLE_PSTATE_SCLK:
		*((uint32_t *)value) = hwmgr->pstate_sclk;
		return 0;
	case AMDGPU_PP_SENSOR_STABLE_PSTATE_MCLK:
		*((uint32_t *)value) = hwmgr->pstate_mclk;
		return 0;
	default:
		mutex_lock(&hwmgr->smu_lock);
		ret = hwmgr->hwmgr_func->read_sensor(hwmgr, idx, value, size);
		mutex_unlock(&hwmgr->smu_lock);
		return ret;
	}
}

static struct amd_vce_state*
pp_dpm_get_vce_clock_state(void *handle, unsigned idx)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return NULL;

	if (idx < hwmgr->num_vce_state_tables)
		return &hwmgr->vce_states[idx];
	return NULL;
}

static int pp_get_power_profile_mode(void *handle, char *buf)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en || !buf)
		return -EINVAL;

	if (hwmgr->hwmgr_func->get_power_profile_mode == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return snprintf(buf, PAGE_SIZE, "\n");
	}

	return hwmgr->hwmgr_func->get_power_profile_mode(hwmgr, buf);
}

static int pp_set_power_profile_mode(void *handle, long *input, uint32_t size)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = -EINVAL;

	if (!hwmgr || !hwmgr->pm_en)
		return ret;

	if (hwmgr->hwmgr_func->set_power_profile_mode == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return ret;
	}
	mutex_lock(&hwmgr->smu_lock);
	if (hwmgr->dpm_level == AMD_DPM_FORCED_LEVEL_MANUAL)
		ret = hwmgr->hwmgr_func->set_power_profile_mode(hwmgr, input, size);
	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_odn_edit_dpm_table(void *handle, uint32_t type, long *input, uint32_t size)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->odn_edit_dpm_table == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	return hwmgr->hwmgr_func->odn_edit_dpm_table(hwmgr, type, input, size);
}

static int pp_dpm_switch_power_profile(void *handle,
		enum PP_SMC_POWER_PROFILE type, bool en)
{
	struct pp_hwmgr *hwmgr = handle;
	long workload;
	uint32_t index;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_power_profile_mode == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	if (!(type < PP_SMC_POWER_PROFILE_CUSTOM))
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);

	if (!en) {
		hwmgr->workload_mask &= ~(1 << hwmgr->workload_prority[type]);
		index = fls(hwmgr->workload_mask);
		index = index > 0 && index <= Workload_Policy_Max ? index - 1 : 0;
		workload = hwmgr->workload_setting[index];
	} else {
		hwmgr->workload_mask |= (1 << hwmgr->workload_prority[type]);
		index = fls(hwmgr->workload_mask);
		index = index <= Workload_Policy_Max ? index - 1 : 0;
		workload = hwmgr->workload_setting[index];
	}

	if (hwmgr->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL)
		hwmgr->hwmgr_func->set_power_profile_mode(hwmgr, &workload, 0);
	mutex_unlock(&hwmgr->smu_lock);

	return 0;
}

static int pp_dpm_notify_smu_memory_info(void *handle,
					uint32_t virtual_addr_low,
					uint32_t virtual_addr_hi,
					uint32_t mc_addr_low,
					uint32_t mc_addr_hi,
					uint32_t size)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->notify_cac_buffer_info == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&hwmgr->smu_lock);

	ret = hwmgr->hwmgr_func->notify_cac_buffer_info(hwmgr, virtual_addr_low,
					virtual_addr_hi, mc_addr_low, mc_addr_hi,
					size);

	mutex_unlock(&hwmgr->smu_lock);

	return ret;
}

static int pp_set_power_limit(void *handle, uint32_t limit)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_power_limit == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	if (limit == 0)
		limit = hwmgr->default_power_limit;

	if (limit > hwmgr->default_power_limit)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);
	hwmgr->hwmgr_func->set_power_limit(hwmgr, limit);
	hwmgr->power_limit = limit;
	mutex_unlock(&hwmgr->smu_lock);
	return 0;
}

static int pp_get_power_limit(void *handle, uint32_t *limit, bool default_limit)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en ||!limit)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);

	if (default_limit)
		*limit = hwmgr->default_power_limit;
	else
		*limit = hwmgr->power_limit;

	mutex_unlock(&hwmgr->smu_lock);

	return 0;
}

static int pp_display_configuration_change(void *handle,
	const struct amd_pp_display_configuration *display_config)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);
	phm_store_dal_configuration_data(hwmgr, display_config);
	mutex_unlock(&hwmgr->smu_lock);
	return 0;
}

static int pp_get_display_power_level(void *handle,
		struct amd_pp_simple_clock_info *output)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en ||!output)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);
	ret = phm_get_dal_power_level(hwmgr, output);
	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_get_current_clocks(void *handle,
		struct amd_pp_clock_info *clocks)
{
	struct amd_pp_simple_clock_info simple_clocks;
	struct pp_clock_info hw_clocks;
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);

	phm_get_dal_power_level(hwmgr, &simple_clocks);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_PowerContainment))
		ret = phm_get_clock_info(hwmgr, &hwmgr->current_ps->hardware,
					&hw_clocks, PHM_PerformanceLevelDesignation_PowerContainment);
	else
		ret = phm_get_clock_info(hwmgr, &hwmgr->current_ps->hardware,
					&hw_clocks, PHM_PerformanceLevelDesignation_Activity);

	if (ret) {
		pr_info("Error in phm_get_clock_info \n");
		mutex_unlock(&hwmgr->smu_lock);
		return -EINVAL;
	}

	clocks->min_engine_clock = hw_clocks.min_eng_clk;
	clocks->max_engine_clock = hw_clocks.max_eng_clk;
	clocks->min_memory_clock = hw_clocks.min_mem_clk;
	clocks->max_memory_clock = hw_clocks.max_mem_clk;
	clocks->min_bus_bandwidth = hw_clocks.min_bus_bandwidth;
	clocks->max_bus_bandwidth = hw_clocks.max_bus_bandwidth;

	clocks->max_engine_clock_in_sr = hw_clocks.max_eng_clk;
	clocks->min_engine_clock_in_sr = hw_clocks.min_eng_clk;

	clocks->max_clocks_state = simple_clocks.level;

	if (0 == phm_get_current_shallow_sleep_clocks(hwmgr, &hwmgr->current_ps->hardware, &hw_clocks)) {
		clocks->max_engine_clock_in_sr = hw_clocks.max_eng_clk;
		clocks->min_engine_clock_in_sr = hw_clocks.min_eng_clk;
	}
	mutex_unlock(&hwmgr->smu_lock);
	return 0;
}

static int pp_get_clock_by_type(void *handle, enum amd_pp_clock_type type, struct amd_pp_clocks *clocks)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (clocks == NULL)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);
	ret = phm_get_clock_by_type(hwmgr, type, clocks);
	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_get_clock_by_type_with_latency(void *handle,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_latency *clocks)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en ||!clocks)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);
	ret = phm_get_clock_by_type_with_latency(hwmgr, type, clocks);
	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_get_clock_by_type_with_voltage(void *handle,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_voltage *clocks)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en ||!clocks)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);

	ret = phm_get_clock_by_type_with_voltage(hwmgr, type, clocks);

	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_set_watermarks_for_clocks_ranges(void *handle,
		struct pp_wm_sets_with_clock_ranges_soc15 *wm_with_clock_ranges)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en ||!wm_with_clock_ranges)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);
	ret = phm_set_watermarks_for_clocks_ranges(hwmgr,
			wm_with_clock_ranges);
	mutex_unlock(&hwmgr->smu_lock);

	return ret;
}

static int pp_display_clock_voltage_request(void *handle,
		struct pp_display_clock_request *clock)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en ||!clock)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);
	ret = phm_display_clock_voltage_request(hwmgr, clock);
	mutex_unlock(&hwmgr->smu_lock);

	return ret;
}

static int pp_get_display_mode_validation_clocks(void *handle,
		struct amd_pp_simple_clock_info *clocks)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en ||!clocks)
		return -EINVAL;

	mutex_lock(&hwmgr->smu_lock);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DynamicPatchPowerState))
		ret = phm_get_max_high_clocks(hwmgr, clocks);

	mutex_unlock(&hwmgr->smu_lock);
	return ret;
}

static int pp_set_mmhub_powergating_by_smu(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_mmhub_powergating_by_smu == NULL) {
		pr_info("%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->set_mmhub_powergating_by_smu(hwmgr);
}

static const struct amd_pm_funcs pp_dpm_funcs = {
	.load_firmware = pp_dpm_load_fw,
	.wait_for_fw_loading_complete = pp_dpm_fw_loading_complete,
	.force_performance_level = pp_dpm_force_performance_level,
	.get_performance_level = pp_dpm_get_performance_level,
	.get_current_power_state = pp_dpm_get_current_power_state,
	.powergate_vce = pp_dpm_powergate_vce,
	.powergate_uvd = pp_dpm_powergate_uvd,
	.dispatch_tasks = pp_dpm_dispatch_tasks,
	.set_fan_control_mode = pp_dpm_set_fan_control_mode,
	.get_fan_control_mode = pp_dpm_get_fan_control_mode,
	.set_fan_speed_percent = pp_dpm_set_fan_speed_percent,
	.get_fan_speed_percent = pp_dpm_get_fan_speed_percent,
	.get_fan_speed_rpm = pp_dpm_get_fan_speed_rpm,
	.get_pp_num_states = pp_dpm_get_pp_num_states,
	.get_pp_table = pp_dpm_get_pp_table,
	.set_pp_table = pp_dpm_set_pp_table,
	.force_clock_level = pp_dpm_force_clock_level,
	.print_clock_levels = pp_dpm_print_clock_levels,
	.get_sclk_od = pp_dpm_get_sclk_od,
	.set_sclk_od = pp_dpm_set_sclk_od,
	.get_mclk_od = pp_dpm_get_mclk_od,
	.set_mclk_od = pp_dpm_set_mclk_od,
	.read_sensor = pp_dpm_read_sensor,
	.get_vce_clock_state = pp_dpm_get_vce_clock_state,
	.switch_power_profile = pp_dpm_switch_power_profile,
	.set_clockgating_by_smu = pp_set_clockgating_by_smu,
	.notify_smu_memory_info = pp_dpm_notify_smu_memory_info,
	.get_power_profile_mode = pp_get_power_profile_mode,
	.set_power_profile_mode = pp_set_power_profile_mode,
	.odn_edit_dpm_table = pp_odn_edit_dpm_table,
	.set_power_limit = pp_set_power_limit,
	.get_power_limit = pp_get_power_limit,
/* export to DC */
	.get_sclk = pp_dpm_get_sclk,
	.get_mclk = pp_dpm_get_mclk,
	.display_configuration_change = pp_display_configuration_change,
	.get_display_power_level = pp_get_display_power_level,
	.get_current_clocks = pp_get_current_clocks,
	.get_clock_by_type = pp_get_clock_by_type,
	.get_clock_by_type_with_latency = pp_get_clock_by_type_with_latency,
	.get_clock_by_type_with_voltage = pp_get_clock_by_type_with_voltage,
	.set_watermarks_for_clocks_ranges = pp_set_watermarks_for_clocks_ranges,
	.display_clock_voltage_request = pp_display_clock_voltage_request,
	.get_display_mode_validation_clocks = pp_get_display_mode_validation_clocks,
	.set_mmhub_powergating_by_smu = pp_set_mmhub_powergating_by_smu,
};

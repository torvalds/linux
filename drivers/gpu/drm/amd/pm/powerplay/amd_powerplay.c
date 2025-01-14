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
#include <linux/reboot.h>
#include "amd_shared.h"
#include "amd_powerplay.h"
#include "power_state.h"
#include "amdgpu.h"
#include "hwmgr.h"
#include "amdgpu_dpm_internal.h"
#include "amdgpu_display.h"

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
	hwmgr->device = amdgpu_cgs_create_device(adev);
	mutex_init(&hwmgr->msg_lock);
	hwmgr->chip_family = adev->family;
	hwmgr->chip_id = adev->asic_type;
	hwmgr->feature_mask = adev->pm.pp_feature;
	hwmgr->display_config = &adev->pm.pm_display_cfg;
	adev->powerplay.pp_handle = hwmgr;
	adev->powerplay.pp_funcs = &pp_dpm_funcs;
	return 0;
}


static void amd_powerplay_destroy(struct amdgpu_device *adev)
{
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;

	mutex_destroy(&hwmgr->msg_lock);

	kfree(hwmgr->hardcode_pp_table);
	hwmgr->hardcode_pp_table = NULL;

	kfree(hwmgr);
	hwmgr = NULL;
}

static int pp_early_init(struct amdgpu_ip_block *ip_block)
{
	int ret;
	struct amdgpu_device *adev = ip_block->adev;
	ret = amd_powerplay_create(adev);

	if (ret != 0)
		return ret;

	ret = hwmgr_early_init(adev->powerplay.pp_handle);
	if (ret)
		return -EINVAL;

	return 0;
}

static void pp_swctf_delayed_work_handler(struct work_struct *work)
{
	struct pp_hwmgr *hwmgr =
		container_of(work, struct pp_hwmgr, swctf_delayed_work.work);
	struct amdgpu_device *adev = hwmgr->adev;
	struct amdgpu_dpm_thermal *range =
				&adev->pm.dpm.thermal;
	uint32_t gpu_temperature, size = sizeof(gpu_temperature);
	int ret;

	/*
	 * If the hotspot/edge temperature is confirmed as below SW CTF setting point
	 * after the delay enforced, nothing will be done.
	 * Otherwise, a graceful shutdown will be performed to prevent further damage.
	 */
	if (range->sw_ctf_threshold &&
	    hwmgr->hwmgr_func->read_sensor) {
		ret = hwmgr->hwmgr_func->read_sensor(hwmgr,
						     AMDGPU_PP_SENSOR_HOTSPOT_TEMP,
						     &gpu_temperature,
						     &size);
		/*
		 * For some legacy ASICs, hotspot temperature retrieving might be not
		 * supported. Check the edge temperature instead then.
		 */
		if (ret == -EOPNOTSUPP)
			ret = hwmgr->hwmgr_func->read_sensor(hwmgr,
							     AMDGPU_PP_SENSOR_EDGE_TEMP,
							     &gpu_temperature,
							     &size);
		if (!ret && gpu_temperature / 1000 < range->sw_ctf_threshold)
			return;
	}

	dev_emerg(adev->dev, "ERROR: GPU over temperature range(SW CTF) detected!\n");
	dev_emerg(adev->dev, "ERROR: System is going to shutdown due to GPU SW CTF!\n");
	orderly_poweroff(true);
}

static int pp_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;
	int ret = 0;

	ret = hwmgr_sw_init(hwmgr);

	pr_debug("powerplay sw init %s\n", ret ? "failed" : "successfully");

	if (!ret)
		INIT_DELAYED_WORK(&hwmgr->swctf_delayed_work,
				  pp_swctf_delayed_work_handler);

	return ret;
}

static int pp_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;

	hwmgr_sw_fini(hwmgr);

	amdgpu_ucode_release(&adev->pm.fw);

	return 0;
}

static int pp_hw_init(struct amdgpu_ip_block *ip_block)
{
	int ret = 0;
	struct amdgpu_device *adev = ip_block->adev;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;

	ret = hwmgr_hw_init(hwmgr);

	if (ret)
		pr_err("powerplay hw init failed\n");

	return ret;
}

static int pp_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct pp_hwmgr *hwmgr = ip_block->adev->powerplay.pp_handle;

	cancel_delayed_work_sync(&hwmgr->swctf_delayed_work);

	hwmgr_hw_fini(hwmgr);

	return 0;
}

static void pp_reserve_vram_for_smu(struct amdgpu_device *adev)
{
	int r = -EINVAL;
	void *cpu_ptr = NULL;
	uint64_t gpu_addr;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;

	if (amdgpu_bo_create_kernel(adev, adev->pm.smu_prv_buffer_size,
						PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
						&adev->pm.smu_prv_buffer,
						&gpu_addr,
						&cpu_ptr)) {
		DRM_ERROR("amdgpu: failed to create smu prv buffer\n");
		return;
	}

	if (hwmgr->hwmgr_func->notify_cac_buffer_info)
		r = hwmgr->hwmgr_func->notify_cac_buffer_info(hwmgr,
					lower_32_bits((unsigned long)cpu_ptr),
					upper_32_bits((unsigned long)cpu_ptr),
					lower_32_bits(gpu_addr),
					upper_32_bits(gpu_addr),
					adev->pm.smu_prv_buffer_size);

	if (r) {
		amdgpu_bo_free_kernel(&adev->pm.smu_prv_buffer, NULL, NULL);
		adev->pm.smu_prv_buffer = NULL;
		DRM_ERROR("amdgpu: failed to notify SMU buffer address\n");
	}
}

static int pp_late_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;

	if (hwmgr && hwmgr->pm_en)
		hwmgr_handle_task(hwmgr,
					AMD_PP_TASK_COMPLETE_INIT, NULL);
	if (adev->pm.smu_prv_buffer_size != 0)
		pp_reserve_vram_for_smu(adev);

	return 0;
}

static void pp_late_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	if (adev->pm.smu_prv_buffer)
		amdgpu_bo_free_kernel(&adev->pm.smu_prv_buffer, NULL, NULL);
	amd_powerplay_destroy(adev);
}


static bool pp_is_idle(void *handle)
{
	return false;
}

static int pp_set_powergating_state(struct amdgpu_ip_block *ip_block,
				    enum amd_powergating_state state)
{
	return 0;
}

static int pp_suspend(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;

	cancel_delayed_work_sync(&hwmgr->swctf_delayed_work);

	return hwmgr_suspend(hwmgr);
}

static int pp_resume(struct amdgpu_ip_block *ip_block)
{
	struct pp_hwmgr *hwmgr = ip_block->adev->powerplay.pp_handle;

	return hwmgr_resume(hwmgr);
}

static int pp_set_clockgating_state(struct amdgpu_ip_block *ip_block,
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

/* This interface only be supported On Vi,
 * because only smu7/8 can help to load gfx/sdma fw,
 * smu need to be enabled before load other ip's fw.
 * so call start smu to load smu7 fw and other ip's fw
 */
static int pp_dpm_load_fw(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->smumgr_funcs || !hwmgr->smumgr_funcs->start_smu)
		return -EINVAL;

	if (hwmgr->smumgr_funcs->start_smu(hwmgr)) {
		pr_err("fw load failed\n");
		return -EINVAL;
	}

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
		pr_info_ratelimited("%s was not implemented.\n", __func__);
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
		}
	} else {
		/* exit umd pstate, restore level, enable gfx cg*/
		if (!(*level & profile_mode_mask)) {
			if (*level == AMD_DPM_FORCED_LEVEL_PROFILE_EXIT)
				*level = hwmgr->saved_dpm_level;
			hwmgr->en_umd_pstate = false;
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

	pp_dpm_en_umd_pstate(hwmgr, &level);
	hwmgr->request_dpm_level = level;
	hwmgr_handle_task(hwmgr, AMD_PP_TASK_READJUST_POWER_STATE, NULL);

	return 0;
}

static enum amd_dpm_forced_level pp_dpm_get_performance_level(
								void *handle)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	return hwmgr->dpm_level;
}

static uint32_t pp_dpm_get_sclk(void *handle, bool low)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return 0;

	if (hwmgr->hwmgr_func->get_sclk == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return 0;
	}
	return hwmgr->hwmgr_func->get_sclk(hwmgr, low);
}

static uint32_t pp_dpm_get_mclk(void *handle, bool low)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return 0;

	if (hwmgr->hwmgr_func->get_mclk == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return 0;
	}
	return hwmgr->hwmgr_func->get_mclk(hwmgr, low);
}

static void pp_dpm_powergate_vce(void *handle, bool gate)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return;

	if (hwmgr->hwmgr_func->powergate_vce == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return;
	}
	hwmgr->hwmgr_func->powergate_vce(hwmgr, gate);
}

static void pp_dpm_powergate_uvd(void *handle, bool gate)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return;

	if (hwmgr->hwmgr_func->powergate_uvd == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return;
	}
	hwmgr->hwmgr_func->powergate_uvd(hwmgr, gate);
}

static int pp_dpm_dispatch_tasks(void *handle, enum amd_pp_task task_id,
		enum amd_pm_state_type *user_state)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	return hwmgr_handle_task(hwmgr, task_id, user_state);
}

static enum amd_pm_state_type pp_dpm_get_current_power_state(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;
	struct pp_power_state *state;
	enum amd_pm_state_type pm_type;

	if (!hwmgr || !hwmgr->pm_en || !hwmgr->current_ps)
		return -EINVAL;

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

	return pm_type;
}

static int pp_dpm_set_fan_control_mode(void *handle, uint32_t mode)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EOPNOTSUPP;

	if (hwmgr->hwmgr_func->set_fan_control_mode == NULL)
		return -EOPNOTSUPP;

	if (mode == U32_MAX)
		return -EINVAL;

	hwmgr->hwmgr_func->set_fan_control_mode(hwmgr, mode);

	return 0;
}

static int pp_dpm_get_fan_control_mode(void *handle, uint32_t *fan_mode)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EOPNOTSUPP;

	if (hwmgr->hwmgr_func->get_fan_control_mode == NULL)
		return -EOPNOTSUPP;

	if (!fan_mode)
		return -EINVAL;

	*fan_mode = hwmgr->hwmgr_func->get_fan_control_mode(hwmgr);
	return 0;
}

static int pp_dpm_set_fan_speed_pwm(void *handle, uint32_t speed)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EOPNOTSUPP;

	if (hwmgr->hwmgr_func->set_fan_speed_pwm == NULL)
		return -EOPNOTSUPP;

	if (speed == U32_MAX)
		return -EINVAL;

	return hwmgr->hwmgr_func->set_fan_speed_pwm(hwmgr, speed);
}

static int pp_dpm_get_fan_speed_pwm(void *handle, uint32_t *speed)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EOPNOTSUPP;

	if (hwmgr->hwmgr_func->get_fan_speed_pwm == NULL)
		return -EOPNOTSUPP;

	if (!speed)
		return -EINVAL;

	return hwmgr->hwmgr_func->get_fan_speed_pwm(hwmgr, speed);
}

static int pp_dpm_get_fan_speed_rpm(void *handle, uint32_t *rpm)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EOPNOTSUPP;

	if (hwmgr->hwmgr_func->get_fan_speed_rpm == NULL)
		return -EOPNOTSUPP;

	if (!rpm)
		return -EINVAL;

	return hwmgr->hwmgr_func->get_fan_speed_rpm(hwmgr, rpm);
}

static int pp_dpm_set_fan_speed_rpm(void *handle, uint32_t rpm)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EOPNOTSUPP;

	if (hwmgr->hwmgr_func->set_fan_speed_rpm == NULL)
		return -EOPNOTSUPP;

	if (rpm == U32_MAX)
		return -EINVAL;

	return hwmgr->hwmgr_func->set_fan_speed_rpm(hwmgr, rpm);
}

static int pp_dpm_get_pp_num_states(void *handle,
		struct pp_states_info *data)
{
	struct pp_hwmgr *hwmgr = handle;
	int i;

	memset(data, 0, sizeof(*data));

	if (!hwmgr || !hwmgr->pm_en || !hwmgr->ps)
		return -EINVAL;

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
	return 0;
}

static int pp_dpm_get_pp_table(void *handle, char **table)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en || !hwmgr->soft_pp_table)
		return -EINVAL;

	*table = (char *)hwmgr->soft_pp_table;
	return hwmgr->soft_pp_table_size;
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

	if (!hwmgr->hardcode_pp_table) {
		hwmgr->hardcode_pp_table = kmemdup(hwmgr->soft_pp_table,
						   hwmgr->soft_pp_table_size,
						   GFP_KERNEL);
		if (!hwmgr->hardcode_pp_table)
			return ret;
	}

	memcpy(hwmgr->hardcode_pp_table, buf, size);

	hwmgr->soft_pp_table = hwmgr->hardcode_pp_table;

	ret = amd_powerplay_reset(handle);
	if (ret)
		return ret;

	if (hwmgr->hwmgr_func->avfs_control)
		ret = hwmgr->hwmgr_func->avfs_control(hwmgr, false);

	return ret;
}

static int pp_dpm_force_clock_level(void *handle,
		enum pp_clock_type type, uint32_t mask)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->force_clock_level == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return 0;
	}

	if (hwmgr->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL) {
		pr_debug("force clock level is for dpm manual mode only.\n");
		return -EINVAL;
	}

	return hwmgr->hwmgr_func->force_clock_level(hwmgr, type, mask);
}

static int pp_dpm_emit_clock_levels(void *handle,
				    enum pp_clock_type type,
				    char *buf,
				    int *offset)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EOPNOTSUPP;

	if (!hwmgr->hwmgr_func->emit_clock_levels)
		return -ENOENT;

	return hwmgr->hwmgr_func->emit_clock_levels(hwmgr, type, buf, offset);
}

static int pp_dpm_print_clock_levels(void *handle,
		enum pp_clock_type type, char *buf)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->print_clock_levels == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return 0;
	}
	return hwmgr->hwmgr_func->print_clock_levels(hwmgr, type, buf);
}

static int pp_dpm_get_sclk_od(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->get_sclk_od == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return 0;
	}
	return hwmgr->hwmgr_func->get_sclk_od(hwmgr);
}

static int pp_dpm_set_sclk_od(void *handle, uint32_t value)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_sclk_od == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->set_sclk_od(hwmgr, value);
}

static int pp_dpm_get_mclk_od(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->get_mclk_od == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return 0;
	}
	return hwmgr->hwmgr_func->get_mclk_od(hwmgr);
}

static int pp_dpm_set_mclk_od(void *handle, uint32_t value)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_mclk_od == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return 0;
	}
	return hwmgr->hwmgr_func->set_mclk_od(hwmgr, value);
}

static int pp_dpm_read_sensor(void *handle, int idx,
			      void *value, int *size)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en || !value)
		return -EINVAL;

	switch (idx) {
	case AMDGPU_PP_SENSOR_STABLE_PSTATE_SCLK:
		*((uint32_t *)value) = hwmgr->pstate_sclk * 100;
		return 0;
	case AMDGPU_PP_SENSOR_STABLE_PSTATE_MCLK:
		*((uint32_t *)value) = hwmgr->pstate_mclk * 100;
		return 0;
	case AMDGPU_PP_SENSOR_PEAK_PSTATE_SCLK:
		*((uint32_t *)value) = hwmgr->pstate_sclk_peak * 100;
		return 0;
	case AMDGPU_PP_SENSOR_PEAK_PSTATE_MCLK:
		*((uint32_t *)value) = hwmgr->pstate_mclk_peak * 100;
		return 0;
	case AMDGPU_PP_SENSOR_MIN_FAN_RPM:
		*((uint32_t *)value) = hwmgr->thermal_controller.fanInfo.ulMinRPM;
		return 0;
	case AMDGPU_PP_SENSOR_MAX_FAN_RPM:
		*((uint32_t *)value) = hwmgr->thermal_controller.fanInfo.ulMaxRPM;
		return 0;
	default:
		return hwmgr->hwmgr_func->read_sensor(hwmgr, idx, value, size);
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

	if (!hwmgr || !hwmgr->pm_en || !hwmgr->hwmgr_func->get_power_profile_mode)
		return -EOPNOTSUPP;
	if (!buf)
		return -EINVAL;

	return hwmgr->hwmgr_func->get_power_profile_mode(hwmgr, buf);
}

static int pp_set_power_profile_mode(void *handle, long *input, uint32_t size)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en || !hwmgr->hwmgr_func->set_power_profile_mode)
		return -EOPNOTSUPP;

	if (hwmgr->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL) {
		pr_debug("power profile setting is for manual dpm mode only.\n");
		return -EINVAL;
	}

	return hwmgr->hwmgr_func->set_power_profile_mode(hwmgr, input, size);
}

static int pp_set_fine_grain_clk_vol(void *handle, uint32_t type, long *input, uint32_t size)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_fine_grain_clk_vol == NULL)
		return 0;

	return hwmgr->hwmgr_func->set_fine_grain_clk_vol(hwmgr, type, input, size);
}

static int pp_odn_edit_dpm_table(void *handle, enum PP_OD_DPM_TABLE_COMMAND type,
				 long *input, uint32_t size)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->odn_edit_dpm_table == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->odn_edit_dpm_table(hwmgr, type, input, size);
}

static int pp_dpm_set_mp1_state(void *handle, enum pp_mp1_state mp1_state)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr)
		return -EINVAL;

	if (!hwmgr->pm_en)
		return 0;

	if (hwmgr->hwmgr_func->set_mp1_state)
		return hwmgr->hwmgr_func->set_mp1_state(hwmgr, mp1_state);

	return 0;
}

static int pp_dpm_switch_power_profile(void *handle,
		enum PP_SMC_POWER_PROFILE type, bool en)
{
	struct pp_hwmgr *hwmgr = handle;
	long workload[1];
	uint32_t index;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_power_profile_mode == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	if (!(type < PP_SMC_POWER_PROFILE_CUSTOM))
		return -EINVAL;

	if (!en) {
		hwmgr->workload_mask &= ~(1 << hwmgr->workload_prority[type]);
		index = fls(hwmgr->workload_mask);
		index = index > 0 && index <= Workload_Policy_Max ? index - 1 : 0;
		workload[0] = hwmgr->workload_setting[index];
	} else {
		hwmgr->workload_mask |= (1 << hwmgr->workload_prority[type]);
		index = fls(hwmgr->workload_mask);
		index = index <= Workload_Policy_Max ? index - 1 : 0;
		workload[0] = hwmgr->workload_setting[index];
	}

	if (type == PP_SMC_POWER_PROFILE_COMPUTE &&
		hwmgr->hwmgr_func->disable_power_features_for_compute_performance) {
			if (hwmgr->hwmgr_func->disable_power_features_for_compute_performance(hwmgr, en))
				return -EINVAL;
	}

	if (hwmgr->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL)
		hwmgr->hwmgr_func->set_power_profile_mode(hwmgr, workload, 0);

	return 0;
}

static int pp_set_power_limit(void *handle, uint32_t limit)
{
	struct pp_hwmgr *hwmgr = handle;
	uint32_t max_power_limit;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_power_limit == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	if (limit == 0)
		limit = hwmgr->default_power_limit;

	max_power_limit = hwmgr->default_power_limit;
	if (hwmgr->od_enabled) {
		max_power_limit *= (100 + hwmgr->platform_descriptor.TDPODLimit);
		max_power_limit /= 100;
	}

	if (limit > max_power_limit)
		return -EINVAL;

	hwmgr->hwmgr_func->set_power_limit(hwmgr, limit);
	hwmgr->power_limit = limit;
	return 0;
}

static int pp_get_power_limit(void *handle, uint32_t *limit,
			      enum pp_power_limit_level pp_limit_level,
			      enum pp_power_type power_type)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en || !limit)
		return -EINVAL;

	if (power_type != PP_PWR_TYPE_SUSTAINED)
		return -EOPNOTSUPP;

	switch (pp_limit_level) {
		case PP_PWR_LIMIT_CURRENT:
			*limit = hwmgr->power_limit;
			break;
		case PP_PWR_LIMIT_DEFAULT:
			*limit = hwmgr->default_power_limit;
			break;
		case PP_PWR_LIMIT_MAX:
			*limit = hwmgr->default_power_limit;
			if (hwmgr->od_enabled) {
				*limit *= (100 + hwmgr->platform_descriptor.TDPODLimit);
				*limit /= 100;
			}
			break;
		case PP_PWR_LIMIT_MIN:
			*limit = 0;
			break;
		default:
			ret = -EOPNOTSUPP;
			break;
	}

	return ret;
}

static int pp_display_configuration_change(void *handle,
	const struct amd_pp_display_configuration *display_config)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	phm_store_dal_configuration_data(hwmgr, display_config);
	return 0;
}

static int pp_get_display_power_level(void *handle,
		struct amd_pp_simple_clock_info *output)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en || !output)
		return -EINVAL;

	return phm_get_dal_power_level(hwmgr, output);
}

static int pp_get_current_clocks(void *handle,
		struct amd_pp_clock_info *clocks)
{
	struct amd_pp_simple_clock_info simple_clocks = { 0 };
	struct pp_clock_info hw_clocks;
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	phm_get_dal_power_level(hwmgr, &simple_clocks);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_PowerContainment))
		ret = phm_get_clock_info(hwmgr, &hwmgr->current_ps->hardware,
					&hw_clocks, PHM_PerformanceLevelDesignation_PowerContainment);
	else
		ret = phm_get_clock_info(hwmgr, &hwmgr->current_ps->hardware,
					&hw_clocks, PHM_PerformanceLevelDesignation_Activity);

	if (ret) {
		pr_debug("Error in phm_get_clock_info \n");
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

	if (simple_clocks.level == 0)
		clocks->max_clocks_state = PP_DAL_POWERLEVEL_7;
	else
		clocks->max_clocks_state = simple_clocks.level;

	if (0 == phm_get_current_shallow_sleep_clocks(hwmgr, &hwmgr->current_ps->hardware, &hw_clocks)) {
		clocks->max_engine_clock_in_sr = hw_clocks.max_eng_clk;
		clocks->min_engine_clock_in_sr = hw_clocks.min_eng_clk;
	}
	return 0;
}

static int pp_get_clock_by_type(void *handle, enum amd_pp_clock_type type, struct amd_pp_clocks *clocks)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (clocks == NULL)
		return -EINVAL;

	return phm_get_clock_by_type(hwmgr, type, clocks);
}

static int pp_get_clock_by_type_with_latency(void *handle,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_latency *clocks)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en || !clocks)
		return -EINVAL;

	return phm_get_clock_by_type_with_latency(hwmgr, type, clocks);
}

static int pp_get_clock_by_type_with_voltage(void *handle,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_voltage *clocks)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en || !clocks)
		return -EINVAL;

	return phm_get_clock_by_type_with_voltage(hwmgr, type, clocks);
}

static int pp_set_watermarks_for_clocks_ranges(void *handle,
		void *clock_ranges)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en || !clock_ranges)
		return -EINVAL;

	return phm_set_watermarks_for_clocks_ranges(hwmgr,
						    clock_ranges);
}

static int pp_display_clock_voltage_request(void *handle,
		struct pp_display_clock_request *clock)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en || !clock)
		return -EINVAL;

	return phm_display_clock_voltage_request(hwmgr, clock);
}

static int pp_get_display_mode_validation_clocks(void *handle,
		struct amd_pp_simple_clock_info *clocks)
{
	struct pp_hwmgr *hwmgr = handle;
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en || !clocks)
		return -EINVAL;

	clocks->level = PP_DAL_POWERLEVEL_7;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DynamicPatchPowerState))
		ret = phm_get_max_high_clocks(hwmgr, clocks);

	return ret;
}

static int pp_dpm_powergate_mmhub(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->powergate_mmhub == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->powergate_mmhub(hwmgr);
}

static int pp_dpm_powergate_gfx(void *handle, bool gate)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return 0;

	if (hwmgr->hwmgr_func->powergate_gfx == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->powergate_gfx(hwmgr, gate);
}

static void pp_dpm_powergate_acp(void *handle, bool gate)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return;

	if (hwmgr->hwmgr_func->powergate_acp == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return;
	}

	hwmgr->hwmgr_func->powergate_acp(hwmgr, gate);
}

static void pp_dpm_powergate_sdma(void *handle, bool gate)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr)
		return;

	if (hwmgr->hwmgr_func->powergate_sdma == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return;
	}

	hwmgr->hwmgr_func->powergate_sdma(hwmgr, gate);
}

static int pp_set_powergating_by_smu(void *handle,
				uint32_t block_type,
				bool gate,
				int inst)
{
	int ret = 0;

	switch (block_type) {
	case AMD_IP_BLOCK_TYPE_UVD:
	case AMD_IP_BLOCK_TYPE_VCN:
		pp_dpm_powergate_uvd(handle, gate);
		break;
	case AMD_IP_BLOCK_TYPE_VCE:
		pp_dpm_powergate_vce(handle, gate);
		break;
	case AMD_IP_BLOCK_TYPE_GMC:
		/*
		 * For now, this is only used on PICASSO.
		 * And only "gate" operation is supported.
		 */
		if (gate)
			pp_dpm_powergate_mmhub(handle);
		break;
	case AMD_IP_BLOCK_TYPE_GFX:
		ret = pp_dpm_powergate_gfx(handle, gate);
		break;
	case AMD_IP_BLOCK_TYPE_ACP:
		pp_dpm_powergate_acp(handle, gate);
		break;
	case AMD_IP_BLOCK_TYPE_SDMA:
		pp_dpm_powergate_sdma(handle, gate);
		break;
	default:
		break;
	}
	return ret;
}

static int pp_notify_smu_enable_pwe(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->smus_notify_pwe == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	hwmgr->hwmgr_func->smus_notify_pwe(hwmgr);

	return 0;
}

static int pp_enable_mgpu_fan_boost(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr)
		return -EINVAL;

	if (!hwmgr->pm_en ||
	     hwmgr->hwmgr_func->enable_mgpu_fan_boost == NULL)
		return 0;

	hwmgr->hwmgr_func->enable_mgpu_fan_boost(hwmgr);

	return 0;
}

static int pp_set_min_deep_sleep_dcefclk(void *handle, uint32_t clock)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_min_deep_sleep_dcefclk == NULL) {
		pr_debug("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	hwmgr->hwmgr_func->set_min_deep_sleep_dcefclk(hwmgr, clock);

	return 0;
}

static int pp_set_hard_min_dcefclk_by_freq(void *handle, uint32_t clock)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_hard_min_dcefclk_by_freq == NULL) {
		pr_debug("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	hwmgr->hwmgr_func->set_hard_min_dcefclk_by_freq(hwmgr, clock);

	return 0;
}

static int pp_set_hard_min_fclk_by_freq(void *handle, uint32_t clock)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_hard_min_fclk_by_freq == NULL) {
		pr_debug("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	hwmgr->hwmgr_func->set_hard_min_fclk_by_freq(hwmgr, clock);

	return 0;
}

static int pp_set_active_display_count(void *handle, uint32_t count)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	return phm_set_active_display_count(hwmgr, count);
}

static int pp_get_asic_baco_capability(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr)
		return false;

	if (!(hwmgr->not_vf && amdgpu_dpm) ||
		!hwmgr->hwmgr_func->get_bamaco_support)
		return false;

	return hwmgr->hwmgr_func->get_bamaco_support(hwmgr);
}

static int pp_get_asic_baco_state(void *handle, int *state)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr)
		return -EINVAL;

	if (!hwmgr->pm_en || !hwmgr->hwmgr_func->get_asic_baco_state)
		return 0;

	hwmgr->hwmgr_func->get_asic_baco_state(hwmgr, (enum BACO_STATE *)state);

	return 0;
}

static int pp_set_asic_baco_state(void *handle, int state)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr)
		return -EINVAL;

	if (!(hwmgr->not_vf && amdgpu_dpm) ||
		!hwmgr->hwmgr_func->set_asic_baco_state)
		return 0;

	hwmgr->hwmgr_func->set_asic_baco_state(hwmgr, (enum BACO_STATE)state);

	return 0;
}

static int pp_get_ppfeature_status(void *handle, char *buf)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en || !buf)
		return -EINVAL;

	if (hwmgr->hwmgr_func->get_ppfeature_status == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	return hwmgr->hwmgr_func->get_ppfeature_status(hwmgr, buf);
}

static int pp_set_ppfeature_status(void *handle, uint64_t ppfeature_masks)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->set_ppfeature_status == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	return hwmgr->hwmgr_func->set_ppfeature_status(hwmgr, ppfeature_masks);
}

static int pp_asic_reset_mode_2(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->asic_reset == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	return hwmgr->hwmgr_func->asic_reset(hwmgr, SMU_ASIC_RESET_MODE_2);
}

static int pp_smu_i2c_bus_access(void *handle, bool acquire)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->smu_i2c_bus_access == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	return hwmgr->hwmgr_func->smu_i2c_bus_access(hwmgr, acquire);
}

static int pp_set_df_cstate(void *handle, enum pp_df_cstate state)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr)
		return -EINVAL;

	if (!hwmgr->pm_en || !hwmgr->hwmgr_func->set_df_cstate)
		return 0;

	hwmgr->hwmgr_func->set_df_cstate(hwmgr, state);

	return 0;
}

static int pp_set_xgmi_pstate(void *handle, uint32_t pstate)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr)
		return -EINVAL;

	if (!hwmgr->pm_en || !hwmgr->hwmgr_func->set_xgmi_pstate)
		return 0;

	hwmgr->hwmgr_func->set_xgmi_pstate(hwmgr, pstate);

	return 0;
}

static ssize_t pp_get_gpu_metrics(void *handle, void **table)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr)
		return -EINVAL;

	if (!hwmgr->pm_en || !hwmgr->hwmgr_func->get_gpu_metrics)
		return -EOPNOTSUPP;

	return hwmgr->hwmgr_func->get_gpu_metrics(hwmgr, table);
}

static int pp_gfx_state_change_set(void *handle, uint32_t state)
{
	struct pp_hwmgr *hwmgr = handle;

	if (!hwmgr || !hwmgr->pm_en)
		return -EINVAL;

	if (hwmgr->hwmgr_func->gfx_state_change == NULL) {
		pr_info_ratelimited("%s was not implemented.\n", __func__);
		return -EINVAL;
	}

	hwmgr->hwmgr_func->gfx_state_change(hwmgr, state);
	return 0;
}

static int pp_get_prv_buffer_details(void *handle, void **addr, size_t *size)
{
	struct pp_hwmgr *hwmgr = handle;
	struct amdgpu_device *adev = hwmgr->adev;
	int err;

	if (!addr || !size)
		return -EINVAL;

	*addr = NULL;
	*size = 0;
	if (adev->pm.smu_prv_buffer) {
		err = amdgpu_bo_kmap(adev->pm.smu_prv_buffer, addr);
		if (err)
			return err;
		*size = adev->pm.smu_prv_buffer_size;
	}

	return 0;
}

static void pp_pm_compute_clocks(void *handle)
{
	struct pp_hwmgr *hwmgr = handle;
	struct amdgpu_device *adev = hwmgr->adev;

	if (!adev->dc_enabled) {
		amdgpu_dpm_get_active_displays(adev);
		adev->pm.pm_display_cfg.num_display = adev->pm.dpm.new_active_crtc_count;
		adev->pm.pm_display_cfg.vrefresh = amdgpu_dpm_get_vrefresh(adev);
		adev->pm.pm_display_cfg.min_vblank_time = amdgpu_dpm_get_vblank_time(adev);
		/* we have issues with mclk switching with
		 * refresh rates over 120 hz on the non-DC code.
		 */
		if (adev->pm.pm_display_cfg.vrefresh > 120)
			adev->pm.pm_display_cfg.min_vblank_time = 0;

		pp_display_configuration_change(handle,
						&adev->pm.pm_display_cfg);
	}

	pp_dpm_dispatch_tasks(handle,
			      AMD_PP_TASK_DISPLAY_CONFIG_CHANGE,
			      NULL);
}

static const struct amd_pm_funcs pp_dpm_funcs = {
	.load_firmware = pp_dpm_load_fw,
	.wait_for_fw_loading_complete = pp_dpm_fw_loading_complete,
	.force_performance_level = pp_dpm_force_performance_level,
	.get_performance_level = pp_dpm_get_performance_level,
	.get_current_power_state = pp_dpm_get_current_power_state,
	.dispatch_tasks = pp_dpm_dispatch_tasks,
	.set_fan_control_mode = pp_dpm_set_fan_control_mode,
	.get_fan_control_mode = pp_dpm_get_fan_control_mode,
	.set_fan_speed_pwm = pp_dpm_set_fan_speed_pwm,
	.get_fan_speed_pwm = pp_dpm_get_fan_speed_pwm,
	.get_fan_speed_rpm = pp_dpm_get_fan_speed_rpm,
	.set_fan_speed_rpm = pp_dpm_set_fan_speed_rpm,
	.get_pp_num_states = pp_dpm_get_pp_num_states,
	.get_pp_table = pp_dpm_get_pp_table,
	.set_pp_table = pp_dpm_set_pp_table,
	.force_clock_level = pp_dpm_force_clock_level,
	.emit_clock_levels = pp_dpm_emit_clock_levels,
	.print_clock_levels = pp_dpm_print_clock_levels,
	.get_sclk_od = pp_dpm_get_sclk_od,
	.set_sclk_od = pp_dpm_set_sclk_od,
	.get_mclk_od = pp_dpm_get_mclk_od,
	.set_mclk_od = pp_dpm_set_mclk_od,
	.read_sensor = pp_dpm_read_sensor,
	.get_vce_clock_state = pp_dpm_get_vce_clock_state,
	.switch_power_profile = pp_dpm_switch_power_profile,
	.set_clockgating_by_smu = pp_set_clockgating_by_smu,
	.set_powergating_by_smu = pp_set_powergating_by_smu,
	.get_power_profile_mode = pp_get_power_profile_mode,
	.set_power_profile_mode = pp_set_power_profile_mode,
	.set_fine_grain_clk_vol = pp_set_fine_grain_clk_vol,
	.odn_edit_dpm_table = pp_odn_edit_dpm_table,
	.set_mp1_state = pp_dpm_set_mp1_state,
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
	.notify_smu_enable_pwe = pp_notify_smu_enable_pwe,
	.enable_mgpu_fan_boost = pp_enable_mgpu_fan_boost,
	.set_active_display_count = pp_set_active_display_count,
	.set_min_deep_sleep_dcefclk = pp_set_min_deep_sleep_dcefclk,
	.set_hard_min_dcefclk_by_freq = pp_set_hard_min_dcefclk_by_freq,
	.set_hard_min_fclk_by_freq = pp_set_hard_min_fclk_by_freq,
	.get_asic_baco_capability = pp_get_asic_baco_capability,
	.get_asic_baco_state = pp_get_asic_baco_state,
	.set_asic_baco_state = pp_set_asic_baco_state,
	.get_ppfeature_status = pp_get_ppfeature_status,
	.set_ppfeature_status = pp_set_ppfeature_status,
	.asic_reset_mode_2 = pp_asic_reset_mode_2,
	.smu_i2c_bus_access = pp_smu_i2c_bus_access,
	.set_df_cstate = pp_set_df_cstate,
	.set_xgmi_pstate = pp_set_xgmi_pstate,
	.get_gpu_metrics = pp_get_gpu_metrics,
	.gfx_state_change_set = pp_gfx_state_change_set,
	.get_smu_prv_buf_details = pp_get_prv_buffer_details,
	.pm_compute_clocks = pp_pm_compute_clocks,
};

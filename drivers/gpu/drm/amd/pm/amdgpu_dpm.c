/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */

#include "amdgpu.h"
#include "amdgpu_atombios.h"
#include "amdgpu_i2c.h"
#include "amdgpu_dpm.h"
#include "atom.h"
#include "amd_pcie.h"
#include "amdgpu_display.h"
#include "hwmgr.h"
#include <linux/power_supply.h>
#include "amdgpu_smu.h"

#define amdgpu_dpm_enable_bapm(adev, e) \
		((adev)->powerplay.pp_funcs->enable_bapm((adev)->powerplay.pp_handle, (e)))

int amdgpu_dpm_get_sclk(struct amdgpu_device *adev, bool low)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	return pp_funcs->get_sclk((adev)->powerplay.pp_handle, (low));
}

int amdgpu_dpm_get_mclk(struct amdgpu_device *adev, bool low)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	return pp_funcs->get_mclk((adev)->powerplay.pp_handle, (low));
}

int amdgpu_dpm_set_powergating_by_smu(struct amdgpu_device *adev, uint32_t block_type, bool gate)
{
	int ret = 0;
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	enum ip_power_state pwr_state = gate ? POWER_STATE_OFF : POWER_STATE_ON;

	if (atomic_read(&adev->pm.pwr_state[block_type]) == pwr_state) {
		dev_dbg(adev->dev, "IP block%d already in the target %s state!",
				block_type, gate ? "gate" : "ungate");
		return 0;
	}

	switch (block_type) {
	case AMD_IP_BLOCK_TYPE_UVD:
	case AMD_IP_BLOCK_TYPE_VCE:
		if (pp_funcs && pp_funcs->set_powergating_by_smu) {
			/*
			 * TODO: need a better lock mechanism
			 *
			 * Here adev->pm.mutex lock protection is enforced on
			 * UVD and VCE cases only. Since for other cases, there
			 * may be already lock protection in amdgpu_pm.c.
			 * This is a quick fix for the deadlock issue below.
			 *     NFO: task ocltst:2028 blocked for more than 120 seconds.
			 *     Tainted: G           OE     5.0.0-37-generic #40~18.04.1-Ubuntu
			 *     echo 0 > /proc/sys/kernel/hung_task_timeout_secs" disables this message.
			 *     cltst          D    0  2028   2026 0x00000000
			 *     all Trace:
			 *     __schedule+0x2c0/0x870
			 *     schedule+0x2c/0x70
			 *     schedule_preempt_disabled+0xe/0x10
			 *     __mutex_lock.isra.9+0x26d/0x4e0
			 *     __mutex_lock_slowpath+0x13/0x20
			 *     ? __mutex_lock_slowpath+0x13/0x20
			 *     mutex_lock+0x2f/0x40
			 *     amdgpu_dpm_set_powergating_by_smu+0x64/0xe0 [amdgpu]
			 *     gfx_v8_0_enable_gfx_static_mg_power_gating+0x3c/0x70 [amdgpu]
			 *     gfx_v8_0_set_powergating_state+0x66/0x260 [amdgpu]
			 *     amdgpu_device_ip_set_powergating_state+0x62/0xb0 [amdgpu]
			 *     pp_dpm_force_performance_level+0xe7/0x100 [amdgpu]
			 *     amdgpu_set_dpm_forced_performance_level+0x129/0x330 [amdgpu]
			 */
			mutex_lock(&adev->pm.mutex);
			ret = (pp_funcs->set_powergating_by_smu(
				(adev)->powerplay.pp_handle, block_type, gate));
			mutex_unlock(&adev->pm.mutex);
		}
		break;
	case AMD_IP_BLOCK_TYPE_GFX:
	case AMD_IP_BLOCK_TYPE_VCN:
	case AMD_IP_BLOCK_TYPE_SDMA:
	case AMD_IP_BLOCK_TYPE_JPEG:
	case AMD_IP_BLOCK_TYPE_GMC:
	case AMD_IP_BLOCK_TYPE_ACP:
		if (pp_funcs && pp_funcs->set_powergating_by_smu) {
			ret = (pp_funcs->set_powergating_by_smu(
				(adev)->powerplay.pp_handle, block_type, gate));
		}
		break;
	default:
		break;
	}

	if (!ret)
		atomic_set(&adev->pm.pwr_state[block_type], pwr_state);

	return ret;
}

int amdgpu_dpm_baco_enter(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	void *pp_handle = adev->powerplay.pp_handle;
	int ret = 0;

	if (!pp_funcs || !pp_funcs->set_asic_baco_state)
		return -ENOENT;

	/* enter BACO state */
	ret = pp_funcs->set_asic_baco_state(pp_handle, 1);

	return ret;
}

int amdgpu_dpm_baco_exit(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	void *pp_handle = adev->powerplay.pp_handle;
	int ret = 0;

	if (!pp_funcs || !pp_funcs->set_asic_baco_state)
		return -ENOENT;

	/* exit BACO state */
	ret = pp_funcs->set_asic_baco_state(pp_handle, 0);

	return ret;
}

int amdgpu_dpm_set_mp1_state(struct amdgpu_device *adev,
			     enum pp_mp1_state mp1_state)
{
	int ret = 0;
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (pp_funcs && pp_funcs->set_mp1_state) {
		ret = pp_funcs->set_mp1_state(
				adev->powerplay.pp_handle,
				mp1_state);
	}

	return ret;
}

bool amdgpu_dpm_is_baco_supported(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	void *pp_handle = adev->powerplay.pp_handle;
	bool baco_cap;

	if (!pp_funcs || !pp_funcs->get_asic_baco_capability)
		return false;

	if (pp_funcs->get_asic_baco_capability(pp_handle, &baco_cap))
		return false;

	return baco_cap;
}

int amdgpu_dpm_mode2_reset(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	void *pp_handle = adev->powerplay.pp_handle;

	if (!pp_funcs || !pp_funcs->asic_reset_mode_2)
		return -ENOENT;

	return pp_funcs->asic_reset_mode_2(pp_handle);
}

int amdgpu_dpm_baco_reset(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	void *pp_handle = adev->powerplay.pp_handle;
	int ret = 0;

	if (!pp_funcs || !pp_funcs->set_asic_baco_state)
		return -ENOENT;

	/* enter BACO state */
	ret = pp_funcs->set_asic_baco_state(pp_handle, 1);
	if (ret)
		return ret;

	/* exit BACO state */
	ret = pp_funcs->set_asic_baco_state(pp_handle, 0);
	if (ret)
		return ret;

	return 0;
}

bool amdgpu_dpm_is_mode1_reset_supported(struct amdgpu_device *adev)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	if (is_support_sw_smu(adev))
		return smu_mode1_reset_is_support(smu);

	return false;
}

int amdgpu_dpm_mode1_reset(struct amdgpu_device *adev)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	if (is_support_sw_smu(adev))
		return smu_mode1_reset(smu);

	return -EOPNOTSUPP;
}

int amdgpu_dpm_switch_power_profile(struct amdgpu_device *adev,
				    enum PP_SMC_POWER_PROFILE type,
				    bool en)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (amdgpu_sriov_vf(adev))
		return 0;

	if (pp_funcs && pp_funcs->switch_power_profile)
		ret = pp_funcs->switch_power_profile(
			adev->powerplay.pp_handle, type, en);

	return ret;
}

int amdgpu_dpm_set_xgmi_pstate(struct amdgpu_device *adev,
			       uint32_t pstate)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (pp_funcs && pp_funcs->set_xgmi_pstate)
		ret = pp_funcs->set_xgmi_pstate(adev->powerplay.pp_handle,
								pstate);

	return ret;
}

int amdgpu_dpm_set_df_cstate(struct amdgpu_device *adev,
			     uint32_t cstate)
{
	int ret = 0;
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	void *pp_handle = adev->powerplay.pp_handle;

	if (pp_funcs && pp_funcs->set_df_cstate)
		ret = pp_funcs->set_df_cstate(pp_handle, cstate);

	return ret;
}

int amdgpu_dpm_allow_xgmi_power_down(struct amdgpu_device *adev, bool en)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	if (is_support_sw_smu(adev))
		return smu_allow_xgmi_power_down(smu, en);

	return 0;
}

int amdgpu_dpm_enable_mgpu_fan_boost(struct amdgpu_device *adev)
{
	void *pp_handle = adev->powerplay.pp_handle;
	const struct amd_pm_funcs *pp_funcs =
			adev->powerplay.pp_funcs;
	int ret = 0;

	if (pp_funcs && pp_funcs->enable_mgpu_fan_boost)
		ret = pp_funcs->enable_mgpu_fan_boost(pp_handle);

	return ret;
}

int amdgpu_dpm_set_clockgating_by_smu(struct amdgpu_device *adev,
				      uint32_t msg_id)
{
	void *pp_handle = adev->powerplay.pp_handle;
	const struct amd_pm_funcs *pp_funcs =
			adev->powerplay.pp_funcs;
	int ret = 0;

	if (pp_funcs && pp_funcs->set_clockgating_by_smu)
		ret = pp_funcs->set_clockgating_by_smu(pp_handle,
						       msg_id);

	return ret;
}

int amdgpu_dpm_smu_i2c_bus_access(struct amdgpu_device *adev,
				  bool acquire)
{
	void *pp_handle = adev->powerplay.pp_handle;
	const struct amd_pm_funcs *pp_funcs =
			adev->powerplay.pp_funcs;
	int ret = -EOPNOTSUPP;

	if (pp_funcs && pp_funcs->smu_i2c_bus_access)
		ret = pp_funcs->smu_i2c_bus_access(pp_handle,
						   acquire);

	return ret;
}

void amdgpu_pm_acpi_event_handler(struct amdgpu_device *adev)
{
	if (adev->pm.dpm_enabled) {
		mutex_lock(&adev->pm.mutex);
		if (power_supply_is_system_supplied() > 0)
			adev->pm.ac_power = true;
		else
			adev->pm.ac_power = false;
		if (adev->powerplay.pp_funcs &&
		    adev->powerplay.pp_funcs->enable_bapm)
			amdgpu_dpm_enable_bapm(adev, adev->pm.ac_power);
		mutex_unlock(&adev->pm.mutex);

		if (is_support_sw_smu(adev))
			smu_set_ac_dc(adev->powerplay.pp_handle);
	}
}

int amdgpu_dpm_read_sensor(struct amdgpu_device *adev, enum amd_pp_sensors sensor,
			   void *data, uint32_t *size)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!data || !size)
		return -EINVAL;

	if (pp_funcs && pp_funcs->read_sensor)
		ret = pp_funcs->read_sensor((adev)->powerplay.pp_handle,
								    sensor, data, size);
	else
		ret = -EINVAL;

	return ret;
}

void amdgpu_dpm_compute_clocks(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!adev->pm.dpm_enabled)
		return;

	if (!pp_funcs->pm_compute_clocks)
		return;

	pp_funcs->pm_compute_clocks(adev->powerplay.pp_handle);
}

void amdgpu_dpm_enable_uvd(struct amdgpu_device *adev, bool enable)
{
	int ret = 0;

	ret = amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_UVD, !enable);
	if (ret)
		DRM_ERROR("Dpm %s uvd failed, ret = %d. \n",
			  enable ? "enable" : "disable", ret);
}

void amdgpu_dpm_enable_vce(struct amdgpu_device *adev, bool enable)
{
	int ret = 0;

	ret = amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_VCE, !enable);
	if (ret)
		DRM_ERROR("Dpm %s vce failed, ret = %d. \n",
			  enable ? "enable" : "disable", ret);
}

void amdgpu_dpm_enable_jpeg(struct amdgpu_device *adev, bool enable)
{
	int ret = 0;

	ret = amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_JPEG, !enable);
	if (ret)
		DRM_ERROR("Dpm %s jpeg failed, ret = %d. \n",
			  enable ? "enable" : "disable", ret);
}

int amdgpu_pm_load_smu_firmware(struct amdgpu_device *adev, uint32_t *smu_version)
{
	int r;

	if (adev->powerplay.pp_funcs && adev->powerplay.pp_funcs->load_firmware) {
		r = adev->powerplay.pp_funcs->load_firmware(adev->powerplay.pp_handle);
		if (r) {
			pr_err("smu firmware loading failed\n");
			return r;
		}

		if (smu_version)
			*smu_version = adev->pm.fw_version;
	}

	return 0;
}

int amdgpu_dpm_handle_passthrough_sbr(struct amdgpu_device *adev, bool enable)
{
	return smu_handle_passthrough_sbr(adev->powerplay.pp_handle, enable);
}

int amdgpu_dpm_send_hbm_bad_pages_num(struct amdgpu_device *adev, uint32_t size)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	return smu_send_hbm_bad_pages_num(smu, size);
}

int amdgpu_dpm_get_dpm_freq_range(struct amdgpu_device *adev,
				  enum pp_clock_type type,
				  uint32_t *min,
				  uint32_t *max)
{
	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	switch (type) {
	case PP_SCLK:
		return smu_get_dpm_freq_range(adev->powerplay.pp_handle, SMU_SCLK, min, max);
	default:
		return -EINVAL;
	}
}

int amdgpu_dpm_set_soft_freq_range(struct amdgpu_device *adev,
				   enum pp_clock_type type,
				   uint32_t min,
				   uint32_t max)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	switch (type) {
	case PP_SCLK:
		return smu_set_soft_freq_range(smu, SMU_SCLK, min, max);
	default:
		return -EINVAL;
	}
}

int amdgpu_dpm_write_watermarks_table(struct amdgpu_device *adev)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	if (!is_support_sw_smu(adev))
		return 0;

	return smu_write_watermarks_table(smu);
}

int amdgpu_dpm_wait_for_event(struct amdgpu_device *adev,
			      enum smu_event_type event,
			      uint64_t event_arg)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	return smu_wait_for_event(smu, event, event_arg);
}

int amdgpu_dpm_get_status_gfxoff(struct amdgpu_device *adev, uint32_t *value)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	return smu_get_status_gfxoff(smu, value);
}

uint64_t amdgpu_dpm_get_thermal_throttling_counter(struct amdgpu_device *adev)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	return atomic64_read(&smu->throttle_int_counter);
}

/* amdgpu_dpm_gfx_state_change - Handle gfx power state change set
 * @adev: amdgpu_device pointer
 * @state: gfx power state(1 -sGpuChangeState_D0Entry and 2 -sGpuChangeState_D3Entry)
 *
 */
void amdgpu_dpm_gfx_state_change(struct amdgpu_device *adev,
				 enum gfx_change_state state)
{
	mutex_lock(&adev->pm.mutex);
	if (adev->powerplay.pp_funcs &&
	    adev->powerplay.pp_funcs->gfx_state_change_set)
		((adev)->powerplay.pp_funcs->gfx_state_change_set(
			(adev)->powerplay.pp_handle, state));
	mutex_unlock(&adev->pm.mutex);
}

int amdgpu_dpm_get_ecc_info(struct amdgpu_device *adev,
			    void *umc_ecc)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	return smu_get_ecc_info(smu, umc_ecc);
}

struct amd_vce_state *amdgpu_dpm_get_vce_clock_state(struct amdgpu_device *adev,
						     uint32_t idx)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_vce_clock_state)
		return NULL;

	return pp_funcs->get_vce_clock_state(adev->powerplay.pp_handle,
					     idx);
}

void amdgpu_dpm_get_current_power_state(struct amdgpu_device *adev,
					enum amd_pm_state_type *state)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_current_power_state) {
		*state = adev->pm.dpm.user_state;
		return;
	}

	*state = pp_funcs->get_current_power_state(adev->powerplay.pp_handle);
	if (*state < POWER_STATE_TYPE_DEFAULT ||
	    *state > POWER_STATE_TYPE_INTERNAL_3DPERF)
		*state = adev->pm.dpm.user_state;
}

void amdgpu_dpm_set_power_state(struct amdgpu_device *adev,
				enum amd_pm_state_type state)
{
	adev->pm.dpm.user_state = state;

	if (is_support_sw_smu(adev))
		return;

	if (amdgpu_dpm_dispatch_task(adev,
				     AMD_PP_TASK_ENABLE_USER_STATE,
				     &state) == -EOPNOTSUPP)
		amdgpu_dpm_compute_clocks(adev);
}

enum amd_dpm_forced_level amdgpu_dpm_get_performance_level(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	enum amd_dpm_forced_level level;

	if (pp_funcs->get_performance_level)
		level = pp_funcs->get_performance_level(adev->powerplay.pp_handle);
	else
		level = adev->pm.dpm.forced_level;

	return level;
}

int amdgpu_dpm_force_performance_level(struct amdgpu_device *adev,
				       enum amd_dpm_forced_level level)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (pp_funcs->force_performance_level) {
		if (adev->pm.dpm.thermal_active)
			return -EINVAL;

		if (pp_funcs->force_performance_level(adev->powerplay.pp_handle,
						      level))
			return -EINVAL;

		adev->pm.dpm.forced_level = level;
	}

	return 0;
}

int amdgpu_dpm_get_pp_num_states(struct amdgpu_device *adev,
				 struct pp_states_info *states)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_pp_num_states)
		return -EOPNOTSUPP;

	return pp_funcs->get_pp_num_states(adev->powerplay.pp_handle, states);
}

int amdgpu_dpm_dispatch_task(struct amdgpu_device *adev,
			      enum amd_pp_task task_id,
			      enum amd_pm_state_type *user_state)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->dispatch_tasks)
		return -EOPNOTSUPP;

	return pp_funcs->dispatch_tasks(adev->powerplay.pp_handle, task_id, user_state);
}

int amdgpu_dpm_get_pp_table(struct amdgpu_device *adev, char **table)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_pp_table)
		return 0;

	return pp_funcs->get_pp_table(adev->powerplay.pp_handle, table);
}

int amdgpu_dpm_set_fine_grain_clk_vol(struct amdgpu_device *adev,
				      uint32_t type,
				      long *input,
				      uint32_t size)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_fine_grain_clk_vol)
		return 0;

	return pp_funcs->set_fine_grain_clk_vol(adev->powerplay.pp_handle,
						type,
						input,
						size);
}

int amdgpu_dpm_odn_edit_dpm_table(struct amdgpu_device *adev,
				  uint32_t type,
				  long *input,
				  uint32_t size)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->odn_edit_dpm_table)
		return 0;

	return pp_funcs->odn_edit_dpm_table(adev->powerplay.pp_handle,
					    type,
					    input,
					    size);
}

int amdgpu_dpm_print_clock_levels(struct amdgpu_device *adev,
				  enum pp_clock_type type,
				  char *buf)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->print_clock_levels)
		return 0;

	return pp_funcs->print_clock_levels(adev->powerplay.pp_handle,
					    type,
					    buf);
}

int amdgpu_dpm_set_ppfeature_status(struct amdgpu_device *adev,
				    uint64_t ppfeature_masks)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_ppfeature_status)
		return 0;

	return pp_funcs->set_ppfeature_status(adev->powerplay.pp_handle,
					      ppfeature_masks);
}

int amdgpu_dpm_get_ppfeature_status(struct amdgpu_device *adev, char *buf)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_ppfeature_status)
		return 0;

	return pp_funcs->get_ppfeature_status(adev->powerplay.pp_handle,
					      buf);
}

int amdgpu_dpm_force_clock_level(struct amdgpu_device *adev,
				 enum pp_clock_type type,
				 uint32_t mask)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->force_clock_level)
		return 0;

	return pp_funcs->force_clock_level(adev->powerplay.pp_handle,
					   type,
					   mask);
}

int amdgpu_dpm_get_sclk_od(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_sclk_od)
		return 0;

	return pp_funcs->get_sclk_od(adev->powerplay.pp_handle);
}

int amdgpu_dpm_set_sclk_od(struct amdgpu_device *adev, uint32_t value)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (is_support_sw_smu(adev))
		return 0;

	if (pp_funcs->set_sclk_od)
		pp_funcs->set_sclk_od(adev->powerplay.pp_handle, value);

	if (amdgpu_dpm_dispatch_task(adev,
				     AMD_PP_TASK_READJUST_POWER_STATE,
				     NULL) == -EOPNOTSUPP) {
		adev->pm.dpm.current_ps = adev->pm.dpm.boot_ps;
		amdgpu_dpm_compute_clocks(adev);
	}

	return 0;
}

int amdgpu_dpm_get_mclk_od(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_mclk_od)
		return 0;

	return pp_funcs->get_mclk_od(adev->powerplay.pp_handle);
}

int amdgpu_dpm_set_mclk_od(struct amdgpu_device *adev, uint32_t value)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (is_support_sw_smu(adev))
		return 0;

	if (pp_funcs->set_mclk_od)
		pp_funcs->set_mclk_od(adev->powerplay.pp_handle, value);

	if (amdgpu_dpm_dispatch_task(adev,
				     AMD_PP_TASK_READJUST_POWER_STATE,
				     NULL) == -EOPNOTSUPP) {
		adev->pm.dpm.current_ps = adev->pm.dpm.boot_ps;
		amdgpu_dpm_compute_clocks(adev);
	}

	return 0;
}

int amdgpu_dpm_get_power_profile_mode(struct amdgpu_device *adev,
				      char *buf)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_power_profile_mode)
		return -EOPNOTSUPP;

	return pp_funcs->get_power_profile_mode(adev->powerplay.pp_handle,
						buf);
}

int amdgpu_dpm_set_power_profile_mode(struct amdgpu_device *adev,
				      long *input, uint32_t size)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_power_profile_mode)
		return 0;

	return pp_funcs->set_power_profile_mode(adev->powerplay.pp_handle,
						input,
						size);
}

int amdgpu_dpm_get_gpu_metrics(struct amdgpu_device *adev, void **table)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_gpu_metrics)
		return 0;

	return pp_funcs->get_gpu_metrics(adev->powerplay.pp_handle, table);
}

int amdgpu_dpm_get_fan_control_mode(struct amdgpu_device *adev,
				    uint32_t *fan_mode)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_fan_control_mode)
		return -EOPNOTSUPP;

	*fan_mode = pp_funcs->get_fan_control_mode(adev->powerplay.pp_handle);

	return 0;
}

int amdgpu_dpm_set_fan_speed_pwm(struct amdgpu_device *adev,
				 uint32_t speed)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_fan_speed_pwm)
		return -EINVAL;

	return pp_funcs->set_fan_speed_pwm(adev->powerplay.pp_handle, speed);
}

int amdgpu_dpm_get_fan_speed_pwm(struct amdgpu_device *adev,
				 uint32_t *speed)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_fan_speed_pwm)
		return -EINVAL;

	return pp_funcs->get_fan_speed_pwm(adev->powerplay.pp_handle, speed);
}

int amdgpu_dpm_get_fan_speed_rpm(struct amdgpu_device *adev,
				 uint32_t *speed)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_fan_speed_rpm)
		return -EINVAL;

	return pp_funcs->get_fan_speed_rpm(adev->powerplay.pp_handle, speed);
}

int amdgpu_dpm_set_fan_speed_rpm(struct amdgpu_device *adev,
				 uint32_t speed)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_fan_speed_rpm)
		return -EINVAL;

	return pp_funcs->set_fan_speed_rpm(adev->powerplay.pp_handle, speed);
}

int amdgpu_dpm_set_fan_control_mode(struct amdgpu_device *adev,
				    uint32_t mode)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_fan_control_mode)
		return -EOPNOTSUPP;

	pp_funcs->set_fan_control_mode(adev->powerplay.pp_handle, mode);

	return 0;
}

int amdgpu_dpm_get_power_limit(struct amdgpu_device *adev,
			       uint32_t *limit,
			       enum pp_power_limit_level pp_limit_level,
			       enum pp_power_type power_type)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_power_limit)
		return -ENODATA;

	return pp_funcs->get_power_limit(adev->powerplay.pp_handle,
					 limit,
					 pp_limit_level,
					 power_type);
}

int amdgpu_dpm_set_power_limit(struct amdgpu_device *adev,
			       uint32_t limit)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_power_limit)
		return -EINVAL;

	return pp_funcs->set_power_limit(adev->powerplay.pp_handle, limit);
}

int amdgpu_dpm_is_cclk_dpm_supported(struct amdgpu_device *adev)
{
	if (!is_support_sw_smu(adev))
		return false;

	return is_support_cclk_dpm(adev);
}

int amdgpu_dpm_debugfs_print_current_performance_level(struct amdgpu_device *adev,
						       struct seq_file *m)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->debugfs_print_current_performance_level)
		return -EOPNOTSUPP;

	pp_funcs->debugfs_print_current_performance_level(adev->powerplay.pp_handle,
							  m);

	return 0;
}

int amdgpu_dpm_get_smu_prv_buf_details(struct amdgpu_device *adev,
				       void **addr,
				       size_t *size)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_smu_prv_buf_details)
		return -ENOSYS;

	return pp_funcs->get_smu_prv_buf_details(adev->powerplay.pp_handle,
						 addr,
						 size);
}

int amdgpu_dpm_is_overdrive_supported(struct amdgpu_device *adev)
{
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;
	struct smu_context *smu = adev->powerplay.pp_handle;

	if ((is_support_sw_smu(adev) && smu->od_enabled) ||
	    (is_support_sw_smu(adev) && smu->is_apu) ||
		(!is_support_sw_smu(adev) && hwmgr->od_enabled))
		return true;

	return false;
}

int amdgpu_dpm_set_pp_table(struct amdgpu_device *adev,
			    const char *buf,
			    size_t size)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_pp_table)
		return -EOPNOTSUPP;

	return pp_funcs->set_pp_table(adev->powerplay.pp_handle,
				      buf,
				      size);
}

int amdgpu_dpm_get_num_cpu_cores(struct amdgpu_device *adev)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	return smu->cpu_core_num;
}

void amdgpu_dpm_stb_debug_fs_init(struct amdgpu_device *adev)
{
	if (!is_support_sw_smu(adev))
		return;

	amdgpu_smu_stb_debug_fs_init(adev);
}

int amdgpu_dpm_display_configuration_change(struct amdgpu_device *adev,
					    const struct amd_pp_display_configuration *input)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->display_configuration_change)
		return 0;

	return pp_funcs->display_configuration_change(adev->powerplay.pp_handle,
						      input);
}

int amdgpu_dpm_get_clock_by_type(struct amdgpu_device *adev,
				 enum amd_pp_clock_type type,
				 struct amd_pp_clocks *clocks)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_clock_by_type)
		return 0;

	return pp_funcs->get_clock_by_type(adev->powerplay.pp_handle,
					   type,
					   clocks);
}

int amdgpu_dpm_get_display_mode_validation_clks(struct amdgpu_device *adev,
						struct amd_pp_simple_clock_info *clocks)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_display_mode_validation_clocks)
		return 0;

	return pp_funcs->get_display_mode_validation_clocks(adev->powerplay.pp_handle,
							    clocks);
}

int amdgpu_dpm_get_clock_by_type_with_latency(struct amdgpu_device *adev,
					      enum amd_pp_clock_type type,
					      struct pp_clock_levels_with_latency *clocks)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_clock_by_type_with_latency)
		return 0;

	return pp_funcs->get_clock_by_type_with_latency(adev->powerplay.pp_handle,
							type,
							clocks);
}

int amdgpu_dpm_get_clock_by_type_with_voltage(struct amdgpu_device *adev,
					      enum amd_pp_clock_type type,
					      struct pp_clock_levels_with_voltage *clocks)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_clock_by_type_with_voltage)
		return 0;

	return pp_funcs->get_clock_by_type_with_voltage(adev->powerplay.pp_handle,
							type,
							clocks);
}

int amdgpu_dpm_set_watermarks_for_clocks_ranges(struct amdgpu_device *adev,
					       void *clock_ranges)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_watermarks_for_clocks_ranges)
		return -EOPNOTSUPP;

	return pp_funcs->set_watermarks_for_clocks_ranges(adev->powerplay.pp_handle,
							  clock_ranges);
}

int amdgpu_dpm_display_clock_voltage_request(struct amdgpu_device *adev,
					     struct pp_display_clock_request *clock)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->display_clock_voltage_request)
		return -EOPNOTSUPP;

	return pp_funcs->display_clock_voltage_request(adev->powerplay.pp_handle,
						       clock);
}

int amdgpu_dpm_get_current_clocks(struct amdgpu_device *adev,
				  struct amd_pp_clock_info *clocks)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_current_clocks)
		return -EOPNOTSUPP;

	return pp_funcs->get_current_clocks(adev->powerplay.pp_handle,
					    clocks);
}

void amdgpu_dpm_notify_smu_enable_pwe(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->notify_smu_enable_pwe)
		return;

	pp_funcs->notify_smu_enable_pwe(adev->powerplay.pp_handle);
}

int amdgpu_dpm_set_active_display_count(struct amdgpu_device *adev,
					uint32_t count)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_active_display_count)
		return -EOPNOTSUPP;

	return pp_funcs->set_active_display_count(adev->powerplay.pp_handle,
						  count);
}

int amdgpu_dpm_set_min_deep_sleep_dcefclk(struct amdgpu_device *adev,
					  uint32_t clock)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_min_deep_sleep_dcefclk)
		return -EOPNOTSUPP;

	return pp_funcs->set_min_deep_sleep_dcefclk(adev->powerplay.pp_handle,
						    clock);
}

void amdgpu_dpm_set_hard_min_dcefclk_by_freq(struct amdgpu_device *adev,
					     uint32_t clock)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_hard_min_dcefclk_by_freq)
		return;

	pp_funcs->set_hard_min_dcefclk_by_freq(adev->powerplay.pp_handle,
					       clock);
}

void amdgpu_dpm_set_hard_min_fclk_by_freq(struct amdgpu_device *adev,
					  uint32_t clock)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_hard_min_fclk_by_freq)
		return;

	pp_funcs->set_hard_min_fclk_by_freq(adev->powerplay.pp_handle,
					    clock);
}

int amdgpu_dpm_display_disable_memory_clock_switch(struct amdgpu_device *adev,
						   bool disable_memory_clock_switch)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->display_disable_memory_clock_switch)
		return 0;

	return pp_funcs->display_disable_memory_clock_switch(adev->powerplay.pp_handle,
							     disable_memory_clock_switch);
}

int amdgpu_dpm_get_max_sustainable_clocks_by_dc(struct amdgpu_device *adev,
						struct pp_smu_nv_clock_table *max_clocks)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_max_sustainable_clocks_by_dc)
		return -EOPNOTSUPP;

	return pp_funcs->get_max_sustainable_clocks_by_dc(adev->powerplay.pp_handle,
							  max_clocks);
}

enum pp_smu_status amdgpu_dpm_get_uclk_dpm_states(struct amdgpu_device *adev,
						  unsigned int *clock_values_in_khz,
						  unsigned int *num_states)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_uclk_dpm_states)
		return -EOPNOTSUPP;

	return pp_funcs->get_uclk_dpm_states(adev->powerplay.pp_handle,
					     clock_values_in_khz,
					     num_states);
}

int amdgpu_dpm_get_dpm_clock_table(struct amdgpu_device *adev,
				   struct dpm_clocks *clock_table)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->get_dpm_clock_table)
		return -EOPNOTSUPP;

	return pp_funcs->get_dpm_clock_table(adev->powerplay.pp_handle,
					     clock_table);
}

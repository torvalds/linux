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

#define amdgpu_dpm_is_legacy_dpm(adev) ((adev)->powerplay.pp_handle == (adev))

int amdgpu_dpm_get_sclk(struct amdgpu_device *adev, bool low)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_sclk)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_sclk((adev)->powerplay.pp_handle,
				 low);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_mclk(struct amdgpu_device *adev, bool low)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_mclk)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_mclk((adev)->powerplay.pp_handle,
				 low);
	mutex_unlock(&adev->pm.mutex);

	return ret;
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

	mutex_lock(&adev->pm.mutex);

	switch (block_type) {
	case AMD_IP_BLOCK_TYPE_UVD:
	case AMD_IP_BLOCK_TYPE_VCE:
	case AMD_IP_BLOCK_TYPE_GFX:
	case AMD_IP_BLOCK_TYPE_VCN:
	case AMD_IP_BLOCK_TYPE_SDMA:
	case AMD_IP_BLOCK_TYPE_JPEG:
	case AMD_IP_BLOCK_TYPE_GMC:
	case AMD_IP_BLOCK_TYPE_ACP:
	case AMD_IP_BLOCK_TYPE_VPE:
		if (pp_funcs && pp_funcs->set_powergating_by_smu)
			ret = (pp_funcs->set_powergating_by_smu(
				(adev)->powerplay.pp_handle, block_type, gate));
		break;
	default:
		break;
	}

	if (!ret)
		atomic_set(&adev->pm.pwr_state[block_type], pwr_state);

	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_gfx_power_up_by_imu(struct amdgpu_device *adev)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret = -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = smu_set_gfx_power_up_by_imu(smu);
	mutex_unlock(&adev->pm.mutex);

	msleep(10);

	return ret;
}

int amdgpu_dpm_baco_enter(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	void *pp_handle = adev->powerplay.pp_handle;
	int ret = 0;

	if (!pp_funcs || !pp_funcs->set_asic_baco_state)
		return -ENOENT;

	mutex_lock(&adev->pm.mutex);

	/* enter BACO state */
	ret = pp_funcs->set_asic_baco_state(pp_handle, 1);

	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_baco_exit(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	void *pp_handle = adev->powerplay.pp_handle;
	int ret = 0;

	if (!pp_funcs || !pp_funcs->set_asic_baco_state)
		return -ENOENT;

	mutex_lock(&adev->pm.mutex);

	/* exit BACO state */
	ret = pp_funcs->set_asic_baco_state(pp_handle, 0);

	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_mp1_state(struct amdgpu_device *adev,
			     enum pp_mp1_state mp1_state)
{
	int ret = 0;
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (pp_funcs && pp_funcs->set_mp1_state) {
		mutex_lock(&adev->pm.mutex);

		ret = pp_funcs->set_mp1_state(
				adev->powerplay.pp_handle,
				mp1_state);

		mutex_unlock(&adev->pm.mutex);
	}

	return ret;
}

int amdgpu_dpm_notify_rlc_state(struct amdgpu_device *adev, bool en)
{
	int ret = 0;
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (pp_funcs && pp_funcs->notify_rlc_state) {
		mutex_lock(&adev->pm.mutex);

		ret = pp_funcs->notify_rlc_state(
				adev->powerplay.pp_handle,
				en);

		mutex_unlock(&adev->pm.mutex);
	}

	return ret;
}

bool amdgpu_dpm_is_baco_supported(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	void *pp_handle = adev->powerplay.pp_handle;
	bool ret;

	if (!pp_funcs || !pp_funcs->get_asic_baco_capability)
		return false;
	/* Don't use baco for reset in S3.
	 * This is a workaround for some platforms
	 * where entering BACO during suspend
	 * seems to cause reboots or hangs.
	 * This might be related to the fact that BACO controls
	 * power to the whole GPU including devices like audio and USB.
	 * Powering down/up everything may adversely affect these other
	 * devices.  Needs more investigation.
	 */
	if (adev->in_s3)
		return false;

	mutex_lock(&adev->pm.mutex);

	ret = pp_funcs->get_asic_baco_capability(pp_handle);

	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_mode2_reset(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	void *pp_handle = adev->powerplay.pp_handle;
	int ret = 0;

	if (!pp_funcs || !pp_funcs->asic_reset_mode_2)
		return -ENOENT;

	mutex_lock(&adev->pm.mutex);

	ret = pp_funcs->asic_reset_mode_2(pp_handle);

	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_enable_gfx_features(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	void *pp_handle = adev->powerplay.pp_handle;
	int ret = 0;

	if (!pp_funcs || !pp_funcs->asic_reset_enable_gfx_features)
		return -ENOENT;

	mutex_lock(&adev->pm.mutex);

	ret = pp_funcs->asic_reset_enable_gfx_features(pp_handle);

	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_baco_reset(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	void *pp_handle = adev->powerplay.pp_handle;
	int ret = 0;

	if (!pp_funcs || !pp_funcs->set_asic_baco_state)
		return -ENOENT;

	mutex_lock(&adev->pm.mutex);

	/* enter BACO state */
	ret = pp_funcs->set_asic_baco_state(pp_handle, 1);
	if (ret)
		goto out;

	/* exit BACO state */
	ret = pp_funcs->set_asic_baco_state(pp_handle, 0);

out:
	mutex_unlock(&adev->pm.mutex);
	return ret;
}

bool amdgpu_dpm_is_mode1_reset_supported(struct amdgpu_device *adev)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	bool support_mode1_reset = false;

	if (is_support_sw_smu(adev)) {
		mutex_lock(&adev->pm.mutex);
		support_mode1_reset = smu_mode1_reset_is_support(smu);
		mutex_unlock(&adev->pm.mutex);
	}

	return support_mode1_reset;
}

int amdgpu_dpm_mode1_reset(struct amdgpu_device *adev)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret = -EOPNOTSUPP;

	if (is_support_sw_smu(adev)) {
		mutex_lock(&adev->pm.mutex);
		ret = smu_mode1_reset(smu);
		mutex_unlock(&adev->pm.mutex);
	}

	return ret;
}

int amdgpu_dpm_switch_power_profile(struct amdgpu_device *adev,
				    enum PP_SMC_POWER_PROFILE type,
				    bool en)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (amdgpu_sriov_vf(adev))
		return 0;

	if (pp_funcs && pp_funcs->switch_power_profile) {
		mutex_lock(&adev->pm.mutex);
		ret = pp_funcs->switch_power_profile(
			adev->powerplay.pp_handle, type, en);
		mutex_unlock(&adev->pm.mutex);
	}

	return ret;
}

int amdgpu_dpm_set_xgmi_pstate(struct amdgpu_device *adev,
			       uint32_t pstate)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (pp_funcs && pp_funcs->set_xgmi_pstate) {
		mutex_lock(&adev->pm.mutex);
		ret = pp_funcs->set_xgmi_pstate(adev->powerplay.pp_handle,
								pstate);
		mutex_unlock(&adev->pm.mutex);
	}

	return ret;
}

int amdgpu_dpm_set_df_cstate(struct amdgpu_device *adev,
			     uint32_t cstate)
{
	int ret = 0;
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	void *pp_handle = adev->powerplay.pp_handle;

	if (pp_funcs && pp_funcs->set_df_cstate) {
		mutex_lock(&adev->pm.mutex);
		ret = pp_funcs->set_df_cstate(pp_handle, cstate);
		mutex_unlock(&adev->pm.mutex);
	}

	return ret;
}

int amdgpu_dpm_get_xgmi_plpd_mode(struct amdgpu_device *adev, char **mode_desc)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int mode = XGMI_PLPD_NONE;

	if (is_support_sw_smu(adev)) {
		mode = smu->plpd_mode;
		if (mode_desc == NULL)
			return mode;
		switch (smu->plpd_mode) {
		case XGMI_PLPD_DISALLOW:
			*mode_desc = "disallow";
			break;
		case XGMI_PLPD_DEFAULT:
			*mode_desc = "default";
			break;
		case XGMI_PLPD_OPTIMIZED:
			*mode_desc = "optimized";
			break;
		case XGMI_PLPD_NONE:
		default:
			*mode_desc = "none";
			break;
		}
	}

	return mode;
}

int amdgpu_dpm_set_xgmi_plpd_mode(struct amdgpu_device *adev, int mode)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret = -EOPNOTSUPP;

	if (is_support_sw_smu(adev)) {
		mutex_lock(&adev->pm.mutex);
		ret = smu_set_xgmi_plpd_mode(smu, mode);
		mutex_unlock(&adev->pm.mutex);
	}

	return ret;
}

int amdgpu_dpm_enable_mgpu_fan_boost(struct amdgpu_device *adev)
{
	void *pp_handle = adev->powerplay.pp_handle;
	const struct amd_pm_funcs *pp_funcs =
			adev->powerplay.pp_funcs;
	int ret = 0;

	if (pp_funcs && pp_funcs->enable_mgpu_fan_boost) {
		mutex_lock(&adev->pm.mutex);
		ret = pp_funcs->enable_mgpu_fan_boost(pp_handle);
		mutex_unlock(&adev->pm.mutex);
	}

	return ret;
}

int amdgpu_dpm_set_clockgating_by_smu(struct amdgpu_device *adev,
				      uint32_t msg_id)
{
	void *pp_handle = adev->powerplay.pp_handle;
	const struct amd_pm_funcs *pp_funcs =
			adev->powerplay.pp_funcs;
	int ret = 0;

	if (pp_funcs && pp_funcs->set_clockgating_by_smu) {
		mutex_lock(&adev->pm.mutex);
		ret = pp_funcs->set_clockgating_by_smu(pp_handle,
						       msg_id);
		mutex_unlock(&adev->pm.mutex);
	}

	return ret;
}

int amdgpu_dpm_smu_i2c_bus_access(struct amdgpu_device *adev,
				  bool acquire)
{
	void *pp_handle = adev->powerplay.pp_handle;
	const struct amd_pm_funcs *pp_funcs =
			adev->powerplay.pp_funcs;
	int ret = -EOPNOTSUPP;

	if (pp_funcs && pp_funcs->smu_i2c_bus_access) {
		mutex_lock(&adev->pm.mutex);
		ret = pp_funcs->smu_i2c_bus_access(pp_handle,
						   acquire);
		mutex_unlock(&adev->pm.mutex);
	}

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

		if (is_support_sw_smu(adev))
			smu_set_ac_dc(adev->powerplay.pp_handle);

		mutex_unlock(&adev->pm.mutex);
	}
}

int amdgpu_dpm_read_sensor(struct amdgpu_device *adev, enum amd_pp_sensors sensor,
			   void *data, uint32_t *size)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = -EINVAL;

	if (!data || !size)
		return -EINVAL;

	if (pp_funcs && pp_funcs->read_sensor) {
		mutex_lock(&adev->pm.mutex);
		ret = pp_funcs->read_sensor(adev->powerplay.pp_handle,
					    sensor,
					    data,
					    size);
		mutex_unlock(&adev->pm.mutex);
	}

	return ret;
}

int amdgpu_dpm_get_apu_thermal_limit(struct amdgpu_device *adev, uint32_t *limit)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = -EOPNOTSUPP;

	if (pp_funcs && pp_funcs->get_apu_thermal_limit) {
		mutex_lock(&adev->pm.mutex);
		ret = pp_funcs->get_apu_thermal_limit(adev->powerplay.pp_handle, limit);
		mutex_unlock(&adev->pm.mutex);
	}

	return ret;
}

int amdgpu_dpm_set_apu_thermal_limit(struct amdgpu_device *adev, uint32_t limit)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = -EOPNOTSUPP;

	if (pp_funcs && pp_funcs->set_apu_thermal_limit) {
		mutex_lock(&adev->pm.mutex);
		ret = pp_funcs->set_apu_thermal_limit(adev->powerplay.pp_handle, limit);
		mutex_unlock(&adev->pm.mutex);
	}

	return ret;
}

void amdgpu_dpm_compute_clocks(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int i;

	if (!adev->pm.dpm_enabled)
		return;

	if (!pp_funcs->pm_compute_clocks)
		return;

	if (adev->mode_info.num_crtc)
		amdgpu_display_bandwidth_update(adev);

	for (i = 0; i < AMDGPU_MAX_RINGS; i++) {
		struct amdgpu_ring *ring = adev->rings[i];
		if (ring && ring->sched.ready)
			amdgpu_fence_wait_empty(ring);
	}

	mutex_lock(&adev->pm.mutex);
	pp_funcs->pm_compute_clocks(adev->powerplay.pp_handle);
	mutex_unlock(&adev->pm.mutex);
}

void amdgpu_dpm_enable_uvd(struct amdgpu_device *adev, bool enable)
{
	int ret = 0;

	if (adev->family == AMDGPU_FAMILY_SI) {
		mutex_lock(&adev->pm.mutex);
		if (enable) {
			adev->pm.dpm.uvd_active = true;
			adev->pm.dpm.state = POWER_STATE_TYPE_INTERNAL_UVD;
		} else {
			adev->pm.dpm.uvd_active = false;
		}
		mutex_unlock(&adev->pm.mutex);

		amdgpu_dpm_compute_clocks(adev);
		return;
	}

	ret = amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_UVD, !enable);
	if (ret)
		DRM_ERROR("Dpm %s uvd failed, ret = %d. \n",
			  enable ? "enable" : "disable", ret);
}

void amdgpu_dpm_enable_vce(struct amdgpu_device *adev, bool enable)
{
	int ret = 0;

	if (adev->family == AMDGPU_FAMILY_SI) {
		mutex_lock(&adev->pm.mutex);
		if (enable) {
			adev->pm.dpm.vce_active = true;
			/* XXX select vce level based on ring/task */
			adev->pm.dpm.vce_level = AMD_VCE_LEVEL_AC_ALL;
		} else {
			adev->pm.dpm.vce_active = false;
		}
		mutex_unlock(&adev->pm.mutex);

		amdgpu_dpm_compute_clocks(adev);
		return;
	}

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

void amdgpu_dpm_enable_vpe(struct amdgpu_device *adev, bool enable)
{
	int ret = 0;

	ret = amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_VPE, !enable);
	if (ret)
		DRM_ERROR("Dpm %s vpe failed, ret = %d.\n",
			  enable ? "enable" : "disable", ret);
}

int amdgpu_pm_load_smu_firmware(struct amdgpu_device *adev, uint32_t *smu_version)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int r = 0;

	if (!pp_funcs || !pp_funcs->load_firmware)
		return 0;

	mutex_lock(&adev->pm.mutex);
	r = pp_funcs->load_firmware(adev->powerplay.pp_handle);
	if (r) {
		pr_err("smu firmware loading failed\n");
		goto out;
	}

	if (smu_version)
		*smu_version = adev->pm.fw_version;

out:
	mutex_unlock(&adev->pm.mutex);
	return r;
}

int amdgpu_dpm_handle_passthrough_sbr(struct amdgpu_device *adev, bool enable)
{
	int ret = 0;

	if (is_support_sw_smu(adev)) {
		mutex_lock(&adev->pm.mutex);
		ret = smu_handle_passthrough_sbr(adev->powerplay.pp_handle,
						 enable);
		mutex_unlock(&adev->pm.mutex);
	}

	return ret;
}

int amdgpu_dpm_send_hbm_bad_pages_num(struct amdgpu_device *adev, uint32_t size)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret = 0;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = smu_send_hbm_bad_pages_num(smu, size);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_send_hbm_bad_channel_flag(struct amdgpu_device *adev, uint32_t size)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret = 0;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = smu_send_hbm_bad_channel_flag(smu, size);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_send_rma_reason(struct amdgpu_device *adev)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = smu_send_rma_reason(smu);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_dpm_freq_range(struct amdgpu_device *adev,
				  enum pp_clock_type type,
				  uint32_t *min,
				  uint32_t *max)
{
	int ret = 0;

	if (type != PP_SCLK)
		return -EINVAL;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = smu_get_dpm_freq_range(adev->powerplay.pp_handle,
				     SMU_SCLK,
				     min,
				     max);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_soft_freq_range(struct amdgpu_device *adev,
				   enum pp_clock_type type,
				   uint32_t min,
				   uint32_t max)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret = 0;

	if (type != PP_SCLK)
		return -EINVAL;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = smu_set_soft_freq_range(smu,
				      SMU_SCLK,
				      min,
				      max);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_write_watermarks_table(struct amdgpu_device *adev)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret = 0;

	if (!is_support_sw_smu(adev))
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = smu_write_watermarks_table(smu);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_wait_for_event(struct amdgpu_device *adev,
			      enum smu_event_type event,
			      uint64_t event_arg)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret = 0;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = smu_wait_for_event(smu, event, event_arg);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_residency_gfxoff(struct amdgpu_device *adev, bool value)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret = 0;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = smu_set_residency_gfxoff(smu, value);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_residency_gfxoff(struct amdgpu_device *adev, u32 *value)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret = 0;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = smu_get_residency_gfxoff(smu, value);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_entrycount_gfxoff(struct amdgpu_device *adev, u64 *value)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret = 0;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = smu_get_entrycount_gfxoff(smu, value);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_status_gfxoff(struct amdgpu_device *adev, uint32_t *value)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret = 0;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = smu_get_status_gfxoff(smu, value);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

uint64_t amdgpu_dpm_get_thermal_throttling_counter(struct amdgpu_device *adev)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	if (!is_support_sw_smu(adev))
		return 0;

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
	int ret = 0;

	if (!is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = smu_get_ecc_info(smu, umc_ecc);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

struct amd_vce_state *amdgpu_dpm_get_vce_clock_state(struct amdgpu_device *adev,
						     uint32_t idx)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	struct amd_vce_state *vstate = NULL;

	if (!pp_funcs->get_vce_clock_state)
		return NULL;

	mutex_lock(&adev->pm.mutex);
	vstate = pp_funcs->get_vce_clock_state(adev->powerplay.pp_handle,
					       idx);
	mutex_unlock(&adev->pm.mutex);

	return vstate;
}

void amdgpu_dpm_get_current_power_state(struct amdgpu_device *adev,
					enum amd_pm_state_type *state)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	mutex_lock(&adev->pm.mutex);

	if (!pp_funcs->get_current_power_state) {
		*state = adev->pm.dpm.user_state;
		goto out;
	}

	*state = pp_funcs->get_current_power_state(adev->powerplay.pp_handle);
	if (*state < POWER_STATE_TYPE_DEFAULT ||
	    *state > POWER_STATE_TYPE_INTERNAL_3DPERF)
		*state = adev->pm.dpm.user_state;

out:
	mutex_unlock(&adev->pm.mutex);
}

void amdgpu_dpm_set_power_state(struct amdgpu_device *adev,
				enum amd_pm_state_type state)
{
	mutex_lock(&adev->pm.mutex);
	adev->pm.dpm.user_state = state;
	mutex_unlock(&adev->pm.mutex);

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

	if (!pp_funcs)
		return AMD_DPM_FORCED_LEVEL_AUTO;

	mutex_lock(&adev->pm.mutex);
	if (pp_funcs->get_performance_level)
		level = pp_funcs->get_performance_level(adev->powerplay.pp_handle);
	else
		level = adev->pm.dpm.forced_level;
	mutex_unlock(&adev->pm.mutex);

	return level;
}

int amdgpu_dpm_force_performance_level(struct amdgpu_device *adev,
				       enum amd_dpm_forced_level level)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	enum amd_dpm_forced_level current_level;
	uint32_t profile_mode_mask = AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD |
					AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK |
					AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK |
					AMD_DPM_FORCED_LEVEL_PROFILE_PEAK;

	if (!pp_funcs || !pp_funcs->force_performance_level)
		return 0;

	if (adev->pm.dpm.thermal_active)
		return -EINVAL;

	current_level = amdgpu_dpm_get_performance_level(adev);
	if (current_level == level)
		return 0;

	if (adev->asic_type == CHIP_RAVEN) {
		if (!(adev->apu_flags & AMD_APU_IS_RAVEN2)) {
			if (current_level != AMD_DPM_FORCED_LEVEL_MANUAL &&
			    level == AMD_DPM_FORCED_LEVEL_MANUAL)
				amdgpu_gfx_off_ctrl(adev, false);
			else if (current_level == AMD_DPM_FORCED_LEVEL_MANUAL &&
				 level != AMD_DPM_FORCED_LEVEL_MANUAL)
				amdgpu_gfx_off_ctrl(adev, true);
		}
	}

	if (!(current_level & profile_mode_mask) &&
	    (level == AMD_DPM_FORCED_LEVEL_PROFILE_EXIT))
		return -EINVAL;

	if (!(current_level & profile_mode_mask) &&
	      (level & profile_mode_mask)) {
		/* enter UMD Pstate */
		amdgpu_device_ip_set_powergating_state(adev,
						       AMD_IP_BLOCK_TYPE_GFX,
						       AMD_PG_STATE_UNGATE);
		amdgpu_device_ip_set_clockgating_state(adev,
						       AMD_IP_BLOCK_TYPE_GFX,
						       AMD_CG_STATE_UNGATE);
	} else if ((current_level & profile_mode_mask) &&
		    !(level & profile_mode_mask)) {
		/* exit UMD Pstate */
		amdgpu_device_ip_set_clockgating_state(adev,
						       AMD_IP_BLOCK_TYPE_GFX,
						       AMD_CG_STATE_GATE);
		amdgpu_device_ip_set_powergating_state(adev,
						       AMD_IP_BLOCK_TYPE_GFX,
						       AMD_PG_STATE_GATE);
	}

	mutex_lock(&adev->pm.mutex);

	if (pp_funcs->force_performance_level(adev->powerplay.pp_handle,
					      level)) {
		mutex_unlock(&adev->pm.mutex);
		return -EINVAL;
	}

	adev->pm.dpm.forced_level = level;

	mutex_unlock(&adev->pm.mutex);

	return 0;
}

int amdgpu_dpm_get_pp_num_states(struct amdgpu_device *adev,
				 struct pp_states_info *states)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_pp_num_states)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_pp_num_states(adev->powerplay.pp_handle,
					  states);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_dispatch_task(struct amdgpu_device *adev,
			      enum amd_pp_task task_id,
			      enum amd_pm_state_type *user_state)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->dispatch_tasks)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->dispatch_tasks(adev->powerplay.pp_handle,
				       task_id,
				       user_state);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_pp_table(struct amdgpu_device *adev, char **table)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_pp_table)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_pp_table(adev->powerplay.pp_handle,
				     table);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_fine_grain_clk_vol(struct amdgpu_device *adev,
				      uint32_t type,
				      long *input,
				      uint32_t size)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->set_fine_grain_clk_vol)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->set_fine_grain_clk_vol(adev->powerplay.pp_handle,
					       type,
					       input,
					       size);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_odn_edit_dpm_table(struct amdgpu_device *adev,
				  uint32_t type,
				  long *input,
				  uint32_t size)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->odn_edit_dpm_table)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->odn_edit_dpm_table(adev->powerplay.pp_handle,
					   type,
					   input,
					   size);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_print_clock_levels(struct amdgpu_device *adev,
				  enum pp_clock_type type,
				  char *buf)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->print_clock_levels)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->print_clock_levels(adev->powerplay.pp_handle,
					   type,
					   buf);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_emit_clock_levels(struct amdgpu_device *adev,
				  enum pp_clock_type type,
				  char *buf,
				  int *offset)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->emit_clock_levels)
		return -ENOENT;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->emit_clock_levels(adev->powerplay.pp_handle,
					   type,
					   buf,
					   offset);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_ppfeature_status(struct amdgpu_device *adev,
				    uint64_t ppfeature_masks)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->set_ppfeature_status)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->set_ppfeature_status(adev->powerplay.pp_handle,
					     ppfeature_masks);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_ppfeature_status(struct amdgpu_device *adev, char *buf)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_ppfeature_status)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_ppfeature_status(adev->powerplay.pp_handle,
					     buf);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_force_clock_level(struct amdgpu_device *adev,
				 enum pp_clock_type type,
				 uint32_t mask)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->force_clock_level)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->force_clock_level(adev->powerplay.pp_handle,
					  type,
					  mask);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_sclk_od(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_sclk_od)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_sclk_od(adev->powerplay.pp_handle);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_sclk_od(struct amdgpu_device *adev, uint32_t value)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	if (pp_funcs->set_sclk_od)
		pp_funcs->set_sclk_od(adev->powerplay.pp_handle, value);
	mutex_unlock(&adev->pm.mutex);

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
	int ret = 0;

	if (!pp_funcs->get_mclk_od)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_mclk_od(adev->powerplay.pp_handle);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_mclk_od(struct amdgpu_device *adev, uint32_t value)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (is_support_sw_smu(adev))
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	if (pp_funcs->set_mclk_od)
		pp_funcs->set_mclk_od(adev->powerplay.pp_handle, value);
	mutex_unlock(&adev->pm.mutex);

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
	int ret = 0;

	if (!pp_funcs->get_power_profile_mode)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_power_profile_mode(adev->powerplay.pp_handle,
					       buf);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_power_profile_mode(struct amdgpu_device *adev,
				      long *input, uint32_t size)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->set_power_profile_mode)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->set_power_profile_mode(adev->powerplay.pp_handle,
					       input,
					       size);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_gpu_metrics(struct amdgpu_device *adev, void **table)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_gpu_metrics)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_gpu_metrics(adev->powerplay.pp_handle,
					table);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

ssize_t amdgpu_dpm_get_pm_metrics(struct amdgpu_device *adev, void *pm_metrics,
				  size_t size)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_pm_metrics)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_pm_metrics(adev->powerplay.pp_handle, pm_metrics,
				       size);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_fan_control_mode(struct amdgpu_device *adev,
				    uint32_t *fan_mode)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_fan_control_mode)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_fan_control_mode(adev->powerplay.pp_handle,
					     fan_mode);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_fan_speed_pwm(struct amdgpu_device *adev,
				 uint32_t speed)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->set_fan_speed_pwm)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->set_fan_speed_pwm(adev->powerplay.pp_handle,
					  speed);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_fan_speed_pwm(struct amdgpu_device *adev,
				 uint32_t *speed)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_fan_speed_pwm)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_fan_speed_pwm(adev->powerplay.pp_handle,
					  speed);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_fan_speed_rpm(struct amdgpu_device *adev,
				 uint32_t *speed)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_fan_speed_rpm)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_fan_speed_rpm(adev->powerplay.pp_handle,
					  speed);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_fan_speed_rpm(struct amdgpu_device *adev,
				 uint32_t speed)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->set_fan_speed_rpm)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->set_fan_speed_rpm(adev->powerplay.pp_handle,
					  speed);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_fan_control_mode(struct amdgpu_device *adev,
				    uint32_t mode)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->set_fan_control_mode)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->set_fan_control_mode(adev->powerplay.pp_handle,
					     mode);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_power_limit(struct amdgpu_device *adev,
			       uint32_t *limit,
			       enum pp_power_limit_level pp_limit_level,
			       enum pp_power_type power_type)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_power_limit)
		return -ENODATA;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_power_limit(adev->powerplay.pp_handle,
					limit,
					pp_limit_level,
					power_type);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_power_limit(struct amdgpu_device *adev,
			       uint32_t limit)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->set_power_limit)
		return -EINVAL;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->set_power_limit(adev->powerplay.pp_handle,
					limit);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_is_cclk_dpm_supported(struct amdgpu_device *adev)
{
	bool cclk_dpm_supported = false;

	if (!is_support_sw_smu(adev))
		return false;

	mutex_lock(&adev->pm.mutex);
	cclk_dpm_supported = is_support_cclk_dpm(adev);
	mutex_unlock(&adev->pm.mutex);

	return (int)cclk_dpm_supported;
}

int amdgpu_dpm_debugfs_print_current_performance_level(struct amdgpu_device *adev,
						       struct seq_file *m)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->debugfs_print_current_performance_level)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	pp_funcs->debugfs_print_current_performance_level(adev->powerplay.pp_handle,
							  m);
	mutex_unlock(&adev->pm.mutex);

	return 0;
}

int amdgpu_dpm_get_smu_prv_buf_details(struct amdgpu_device *adev,
				       void **addr,
				       size_t *size)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_smu_prv_buf_details)
		return -ENOSYS;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_smu_prv_buf_details(adev->powerplay.pp_handle,
						addr,
						size);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_is_overdrive_supported(struct amdgpu_device *adev)
{
	if (is_support_sw_smu(adev)) {
		struct smu_context *smu = adev->powerplay.pp_handle;

		return (smu->od_enabled || smu->is_apu);
	} else {
		struct pp_hwmgr *hwmgr;

		/*
		 * dpm on some legacy asics don't carry od_enabled member
		 * as its pp_handle is casted directly from adev.
		 */
		if (amdgpu_dpm_is_legacy_dpm(adev))
			return false;

		hwmgr = (struct pp_hwmgr *)adev->powerplay.pp_handle;

		return hwmgr->od_enabled;
	}
}

int amdgpu_dpm_set_pp_table(struct amdgpu_device *adev,
			    const char *buf,
			    size_t size)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->set_pp_table)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->set_pp_table(adev->powerplay.pp_handle,
				     buf,
				     size);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_num_cpu_cores(struct amdgpu_device *adev)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	if (!is_support_sw_smu(adev))
		return INT_MAX;

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
	int ret = 0;

	if (!pp_funcs->display_configuration_change)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->display_configuration_change(adev->powerplay.pp_handle,
						     input);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_clock_by_type(struct amdgpu_device *adev,
				 enum amd_pp_clock_type type,
				 struct amd_pp_clocks *clocks)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_clock_by_type)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_clock_by_type(adev->powerplay.pp_handle,
					  type,
					  clocks);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_display_mode_validation_clks(struct amdgpu_device *adev,
						struct amd_pp_simple_clock_info *clocks)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_display_mode_validation_clocks)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_display_mode_validation_clocks(adev->powerplay.pp_handle,
							   clocks);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_clock_by_type_with_latency(struct amdgpu_device *adev,
					      enum amd_pp_clock_type type,
					      struct pp_clock_levels_with_latency *clocks)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_clock_by_type_with_latency)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_clock_by_type_with_latency(adev->powerplay.pp_handle,
						       type,
						       clocks);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_clock_by_type_with_voltage(struct amdgpu_device *adev,
					      enum amd_pp_clock_type type,
					      struct pp_clock_levels_with_voltage *clocks)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_clock_by_type_with_voltage)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_clock_by_type_with_voltage(adev->powerplay.pp_handle,
						       type,
						       clocks);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_watermarks_for_clocks_ranges(struct amdgpu_device *adev,
					       void *clock_ranges)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->set_watermarks_for_clocks_ranges)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->set_watermarks_for_clocks_ranges(adev->powerplay.pp_handle,
							 clock_ranges);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_display_clock_voltage_request(struct amdgpu_device *adev,
					     struct pp_display_clock_request *clock)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->display_clock_voltage_request)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->display_clock_voltage_request(adev->powerplay.pp_handle,
						      clock);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_current_clocks(struct amdgpu_device *adev,
				  struct amd_pp_clock_info *clocks)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_current_clocks)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_current_clocks(adev->powerplay.pp_handle,
					   clocks);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

void amdgpu_dpm_notify_smu_enable_pwe(struct amdgpu_device *adev)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->notify_smu_enable_pwe)
		return;

	mutex_lock(&adev->pm.mutex);
	pp_funcs->notify_smu_enable_pwe(adev->powerplay.pp_handle);
	mutex_unlock(&adev->pm.mutex);
}

int amdgpu_dpm_set_active_display_count(struct amdgpu_device *adev,
					uint32_t count)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->set_active_display_count)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->set_active_display_count(adev->powerplay.pp_handle,
						 count);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_set_min_deep_sleep_dcefclk(struct amdgpu_device *adev,
					  uint32_t clock)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->set_min_deep_sleep_dcefclk)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->set_min_deep_sleep_dcefclk(adev->powerplay.pp_handle,
						   clock);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

void amdgpu_dpm_set_hard_min_dcefclk_by_freq(struct amdgpu_device *adev,
					     uint32_t clock)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_hard_min_dcefclk_by_freq)
		return;

	mutex_lock(&adev->pm.mutex);
	pp_funcs->set_hard_min_dcefclk_by_freq(adev->powerplay.pp_handle,
					       clock);
	mutex_unlock(&adev->pm.mutex);
}

void amdgpu_dpm_set_hard_min_fclk_by_freq(struct amdgpu_device *adev,
					  uint32_t clock)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs->set_hard_min_fclk_by_freq)
		return;

	mutex_lock(&adev->pm.mutex);
	pp_funcs->set_hard_min_fclk_by_freq(adev->powerplay.pp_handle,
					    clock);
	mutex_unlock(&adev->pm.mutex);
}

int amdgpu_dpm_display_disable_memory_clock_switch(struct amdgpu_device *adev,
						   bool disable_memory_clock_switch)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->display_disable_memory_clock_switch)
		return 0;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->display_disable_memory_clock_switch(adev->powerplay.pp_handle,
							    disable_memory_clock_switch);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_max_sustainable_clocks_by_dc(struct amdgpu_device *adev,
						struct pp_smu_nv_clock_table *max_clocks)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_max_sustainable_clocks_by_dc)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_max_sustainable_clocks_by_dc(adev->powerplay.pp_handle,
							 max_clocks);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

enum pp_smu_status amdgpu_dpm_get_uclk_dpm_states(struct amdgpu_device *adev,
						  unsigned int *clock_values_in_khz,
						  unsigned int *num_states)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_uclk_dpm_states)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_uclk_dpm_states(adev->powerplay.pp_handle,
					    clock_values_in_khz,
					    num_states);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

int amdgpu_dpm_get_dpm_clock_table(struct amdgpu_device *adev,
				   struct dpm_clocks *clock_table)
{
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret = 0;

	if (!pp_funcs->get_dpm_clock_table)
		return -EOPNOTSUPP;

	mutex_lock(&adev->pm.mutex);
	ret = pp_funcs->get_dpm_clock_table(adev->powerplay.pp_handle,
					    clock_table);
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

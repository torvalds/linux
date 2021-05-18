/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
 * Authors: Rafał Miłecki <zajec5@gmail.com>
 *          Alex Deucher <alexdeucher@gmail.com>
 */

#include "amdgpu.h"
#include "amdgpu_drv.h"
#include "amdgpu_pm.h"
#include "amdgpu_dpm.h"
#include "atom.h"
#include <linux/pci.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/nospec.h>
#include <linux/pm_runtime.h>
#include <asm/processor.h>
#include "hwmgr.h"

static const struct cg_flag_name clocks[] = {
	{AMD_CG_SUPPORT_GFX_FGCG, "Graphics Fine Grain Clock Gating"},
	{AMD_CG_SUPPORT_GFX_MGCG, "Graphics Medium Grain Clock Gating"},
	{AMD_CG_SUPPORT_GFX_MGLS, "Graphics Medium Grain memory Light Sleep"},
	{AMD_CG_SUPPORT_GFX_CGCG, "Graphics Coarse Grain Clock Gating"},
	{AMD_CG_SUPPORT_GFX_CGLS, "Graphics Coarse Grain memory Light Sleep"},
	{AMD_CG_SUPPORT_GFX_CGTS, "Graphics Coarse Grain Tree Shader Clock Gating"},
	{AMD_CG_SUPPORT_GFX_CGTS_LS, "Graphics Coarse Grain Tree Shader Light Sleep"},
	{AMD_CG_SUPPORT_GFX_CP_LS, "Graphics Command Processor Light Sleep"},
	{AMD_CG_SUPPORT_GFX_RLC_LS, "Graphics Run List Controller Light Sleep"},
	{AMD_CG_SUPPORT_GFX_3D_CGCG, "Graphics 3D Coarse Grain Clock Gating"},
	{AMD_CG_SUPPORT_GFX_3D_CGLS, "Graphics 3D Coarse Grain memory Light Sleep"},
	{AMD_CG_SUPPORT_MC_LS, "Memory Controller Light Sleep"},
	{AMD_CG_SUPPORT_MC_MGCG, "Memory Controller Medium Grain Clock Gating"},
	{AMD_CG_SUPPORT_SDMA_LS, "System Direct Memory Access Light Sleep"},
	{AMD_CG_SUPPORT_SDMA_MGCG, "System Direct Memory Access Medium Grain Clock Gating"},
	{AMD_CG_SUPPORT_BIF_MGCG, "Bus Interface Medium Grain Clock Gating"},
	{AMD_CG_SUPPORT_BIF_LS, "Bus Interface Light Sleep"},
	{AMD_CG_SUPPORT_UVD_MGCG, "Unified Video Decoder Medium Grain Clock Gating"},
	{AMD_CG_SUPPORT_VCE_MGCG, "Video Compression Engine Medium Grain Clock Gating"},
	{AMD_CG_SUPPORT_HDP_LS, "Host Data Path Light Sleep"},
	{AMD_CG_SUPPORT_HDP_MGCG, "Host Data Path Medium Grain Clock Gating"},
	{AMD_CG_SUPPORT_DRM_MGCG, "Digital Right Management Medium Grain Clock Gating"},
	{AMD_CG_SUPPORT_DRM_LS, "Digital Right Management Light Sleep"},
	{AMD_CG_SUPPORT_ROM_MGCG, "Rom Medium Grain Clock Gating"},
	{AMD_CG_SUPPORT_DF_MGCG, "Data Fabric Medium Grain Clock Gating"},
	{AMD_CG_SUPPORT_VCN_MGCG, "VCN Medium Grain Clock Gating"},
	{AMD_CG_SUPPORT_HDP_DS, "Host Data Path Deep Sleep"},
	{AMD_CG_SUPPORT_HDP_SD, "Host Data Path Shutdown"},
	{AMD_CG_SUPPORT_IH_CG, "Interrupt Handler Clock Gating"},
	{AMD_CG_SUPPORT_JPEG_MGCG, "JPEG Medium Grain Clock Gating"},

	{AMD_CG_SUPPORT_ATHUB_MGCG, "Address Translation Hub Medium Grain Clock Gating"},
	{AMD_CG_SUPPORT_ATHUB_LS, "Address Translation Hub Light Sleep"},
	{0, NULL},
};

static const struct hwmon_temp_label {
	enum PP_HWMON_TEMP channel;
	const char *label;
} temp_label[] = {
	{PP_TEMP_EDGE, "edge"},
	{PP_TEMP_JUNCTION, "junction"},
	{PP_TEMP_MEM, "mem"},
};

/**
 * DOC: power_dpm_state
 *
 * The power_dpm_state file is a legacy interface and is only provided for
 * backwards compatibility. The amdgpu driver provides a sysfs API for adjusting
 * certain power related parameters.  The file power_dpm_state is used for this.
 * It accepts the following arguments:
 *
 * - battery
 *
 * - balanced
 *
 * - performance
 *
 * battery
 *
 * On older GPUs, the vbios provided a special power state for battery
 * operation.  Selecting battery switched to this state.  This is no
 * longer provided on newer GPUs so the option does nothing in that case.
 *
 * balanced
 *
 * On older GPUs, the vbios provided a special power state for balanced
 * operation.  Selecting balanced switched to this state.  This is no
 * longer provided on newer GPUs so the option does nothing in that case.
 *
 * performance
 *
 * On older GPUs, the vbios provided a special power state for performance
 * operation.  Selecting performance switched to this state.  This is no
 * longer provided on newer GPUs so the option does nothing in that case.
 *
 */

static ssize_t amdgpu_get_power_dpm_state(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	enum amd_pm_state_type pm;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (pp_funcs->get_current_power_state) {
		pm = amdgpu_dpm_get_current_power_state(adev);
	} else {
		pm = adev->pm.dpm.user_state;
	}

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return sysfs_emit(buf, "%s\n",
			  (pm == POWER_STATE_TYPE_BATTERY) ? "battery" :
			  (pm == POWER_STATE_TYPE_BALANCED) ? "balanced" : "performance");
}

static ssize_t amdgpu_set_power_dpm_state(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	enum amd_pm_state_type  state;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	if (strncmp("battery", buf, strlen("battery")) == 0)
		state = POWER_STATE_TYPE_BATTERY;
	else if (strncmp("balanced", buf, strlen("balanced")) == 0)
		state = POWER_STATE_TYPE_BALANCED;
	else if (strncmp("performance", buf, strlen("performance")) == 0)
		state = POWER_STATE_TYPE_PERFORMANCE;
	else
		return -EINVAL;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (is_support_sw_smu(adev)) {
		mutex_lock(&adev->pm.mutex);
		adev->pm.dpm.user_state = state;
		mutex_unlock(&adev->pm.mutex);
	} else if (adev->powerplay.pp_funcs->dispatch_tasks) {
		amdgpu_dpm_dispatch_task(adev, AMD_PP_TASK_ENABLE_USER_STATE, &state);
	} else {
		mutex_lock(&adev->pm.mutex);
		adev->pm.dpm.user_state = state;
		mutex_unlock(&adev->pm.mutex);

		amdgpu_pm_compute_clocks(adev);
	}
	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return count;
}


/**
 * DOC: power_dpm_force_performance_level
 *
 * The amdgpu driver provides a sysfs API for adjusting certain power
 * related parameters.  The file power_dpm_force_performance_level is
 * used for this.  It accepts the following arguments:
 *
 * - auto
 *
 * - low
 *
 * - high
 *
 * - manual
 *
 * - profile_standard
 *
 * - profile_min_sclk
 *
 * - profile_min_mclk
 *
 * - profile_peak
 *
 * auto
 *
 * When auto is selected, the driver will attempt to dynamically select
 * the optimal power profile for current conditions in the driver.
 *
 * low
 *
 * When low is selected, the clocks are forced to the lowest power state.
 *
 * high
 *
 * When high is selected, the clocks are forced to the highest power state.
 *
 * manual
 *
 * When manual is selected, the user can manually adjust which power states
 * are enabled for each clock domain via the sysfs pp_dpm_mclk, pp_dpm_sclk,
 * and pp_dpm_pcie files and adjust the power state transition heuristics
 * via the pp_power_profile_mode sysfs file.
 *
 * profile_standard
 * profile_min_sclk
 * profile_min_mclk
 * profile_peak
 *
 * When the profiling modes are selected, clock and power gating are
 * disabled and the clocks are set for different profiling cases. This
 * mode is recommended for profiling specific work loads where you do
 * not want clock or power gating for clock fluctuation to interfere
 * with your results. profile_standard sets the clocks to a fixed clock
 * level which varies from asic to asic.  profile_min_sclk forces the sclk
 * to the lowest level.  profile_min_mclk forces the mclk to the lowest level.
 * profile_peak sets all clocks (mclk, sclk, pcie) to the highest levels.
 *
 */

static ssize_t amdgpu_get_power_dpm_force_performance_level(struct device *dev,
							    struct device_attribute *attr,
							    char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	enum amd_dpm_forced_level level = 0xff;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (adev->powerplay.pp_funcs->get_performance_level)
		level = amdgpu_dpm_get_performance_level(adev);
	else
		level = adev->pm.dpm.forced_level;

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return sysfs_emit(buf, "%s\n",
			  (level == AMD_DPM_FORCED_LEVEL_AUTO) ? "auto" :
			  (level == AMD_DPM_FORCED_LEVEL_LOW) ? "low" :
			  (level == AMD_DPM_FORCED_LEVEL_HIGH) ? "high" :
			  (level == AMD_DPM_FORCED_LEVEL_MANUAL) ? "manual" :
			  (level == AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD) ? "profile_standard" :
			  (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK) ? "profile_min_sclk" :
			  (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK) ? "profile_min_mclk" :
			  (level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) ? "profile_peak" :
			  (level == AMD_DPM_FORCED_LEVEL_PERF_DETERMINISM) ? "perf_determinism" :
			  "unknown");
}

static ssize_t amdgpu_set_power_dpm_force_performance_level(struct device *dev,
							    struct device_attribute *attr,
							    const char *buf,
							    size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	enum amd_dpm_forced_level level;
	enum amd_dpm_forced_level current_level = 0xff;
	int ret = 0;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	if (strncmp("low", buf, strlen("low")) == 0) {
		level = AMD_DPM_FORCED_LEVEL_LOW;
	} else if (strncmp("high", buf, strlen("high")) == 0) {
		level = AMD_DPM_FORCED_LEVEL_HIGH;
	} else if (strncmp("auto", buf, strlen("auto")) == 0) {
		level = AMD_DPM_FORCED_LEVEL_AUTO;
	} else if (strncmp("manual", buf, strlen("manual")) == 0) {
		level = AMD_DPM_FORCED_LEVEL_MANUAL;
	} else if (strncmp("profile_exit", buf, strlen("profile_exit")) == 0) {
		level = AMD_DPM_FORCED_LEVEL_PROFILE_EXIT;
	} else if (strncmp("profile_standard", buf, strlen("profile_standard")) == 0) {
		level = AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD;
	} else if (strncmp("profile_min_sclk", buf, strlen("profile_min_sclk")) == 0) {
		level = AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK;
	} else if (strncmp("profile_min_mclk", buf, strlen("profile_min_mclk")) == 0) {
		level = AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK;
	} else if (strncmp("profile_peak", buf, strlen("profile_peak")) == 0) {
		level = AMD_DPM_FORCED_LEVEL_PROFILE_PEAK;
	} else if (strncmp("perf_determinism", buf, strlen("perf_determinism")) == 0) {
		level = AMD_DPM_FORCED_LEVEL_PERF_DETERMINISM;
	}  else {
		return -EINVAL;
	}

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (pp_funcs->get_performance_level)
		current_level = amdgpu_dpm_get_performance_level(adev);

	if (current_level == level) {
		pm_runtime_mark_last_busy(ddev->dev);
		pm_runtime_put_autosuspend(ddev->dev);
		return count;
	}

	if (adev->asic_type == CHIP_RAVEN) {
		if (!(adev->apu_flags & AMD_APU_IS_RAVEN2)) {
			if (current_level != AMD_DPM_FORCED_LEVEL_MANUAL && level == AMD_DPM_FORCED_LEVEL_MANUAL)
				amdgpu_gfx_off_ctrl(adev, false);
			else if (current_level == AMD_DPM_FORCED_LEVEL_MANUAL && level != AMD_DPM_FORCED_LEVEL_MANUAL)
				amdgpu_gfx_off_ctrl(adev, true);
		}
	}

	/* profile_exit setting is valid only when current mode is in profile mode */
	if (!(current_level & (AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD |
	    AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK |
	    AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK |
	    AMD_DPM_FORCED_LEVEL_PROFILE_PEAK)) &&
	    (level == AMD_DPM_FORCED_LEVEL_PROFILE_EXIT)) {
		pr_err("Currently not in any profile mode!\n");
		pm_runtime_mark_last_busy(ddev->dev);
		pm_runtime_put_autosuspend(ddev->dev);
		return -EINVAL;
	}

	if (pp_funcs->force_performance_level) {
		mutex_lock(&adev->pm.mutex);
		if (adev->pm.dpm.thermal_active) {
			mutex_unlock(&adev->pm.mutex);
			pm_runtime_mark_last_busy(ddev->dev);
			pm_runtime_put_autosuspend(ddev->dev);
			return -EINVAL;
		}
		ret = amdgpu_dpm_force_performance_level(adev, level);
		if (ret) {
			mutex_unlock(&adev->pm.mutex);
			pm_runtime_mark_last_busy(ddev->dev);
			pm_runtime_put_autosuspend(ddev->dev);
			return -EINVAL;
		} else {
			adev->pm.dpm.forced_level = level;
		}
		mutex_unlock(&adev->pm.mutex);
	}
	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return count;
}

static ssize_t amdgpu_get_pp_num_states(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	struct pp_states_info data;
	int i, buf_len, ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (pp_funcs->get_pp_num_states) {
		amdgpu_dpm_get_pp_num_states(adev, &data);
	} else {
		memset(&data, 0, sizeof(data));
	}

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	buf_len = snprintf(buf, PAGE_SIZE, "states: %d\n", data.nums);
	for (i = 0; i < data.nums; i++)
		buf_len += snprintf(buf + buf_len, PAGE_SIZE, "%d %s\n", i,
				(data.states[i] == POWER_STATE_TYPE_INTERNAL_BOOT) ? "boot" :
				(data.states[i] == POWER_STATE_TYPE_BATTERY) ? "battery" :
				(data.states[i] == POWER_STATE_TYPE_BALANCED) ? "balanced" :
				(data.states[i] == POWER_STATE_TYPE_PERFORMANCE) ? "performance" : "default");

	return buf_len;
}

static ssize_t amdgpu_get_pp_cur_state(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	struct pp_states_info data = {0};
	enum amd_pm_state_type pm = 0;
	int i = 0, ret = 0;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (pp_funcs->get_current_power_state
		 && pp_funcs->get_pp_num_states) {
		pm = amdgpu_dpm_get_current_power_state(adev);
		amdgpu_dpm_get_pp_num_states(adev, &data);
	}

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	for (i = 0; i < data.nums; i++) {
		if (pm == data.states[i])
			break;
	}

	if (i == data.nums)
		i = -EINVAL;

	return sysfs_emit(buf, "%d\n", i);
}

static ssize_t amdgpu_get_pp_force_state(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	if (adev->pp_force_state_enabled)
		return amdgpu_get_pp_cur_state(dev, attr, buf);
	else
		return sysfs_emit(buf, "\n");
}

static ssize_t amdgpu_set_pp_force_state(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	enum amd_pm_state_type state = 0;
	unsigned long idx;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	if (strlen(buf) == 1)
		adev->pp_force_state_enabled = false;
	else if (is_support_sw_smu(adev))
		adev->pp_force_state_enabled = false;
	else if (adev->powerplay.pp_funcs->dispatch_tasks &&
			adev->powerplay.pp_funcs->get_pp_num_states) {
		struct pp_states_info data;

		ret = kstrtoul(buf, 0, &idx);
		if (ret || idx >= ARRAY_SIZE(data.states))
			return -EINVAL;

		idx = array_index_nospec(idx, ARRAY_SIZE(data.states));

		amdgpu_dpm_get_pp_num_states(adev, &data);
		state = data.states[idx];

		ret = pm_runtime_get_sync(ddev->dev);
		if (ret < 0) {
			pm_runtime_put_autosuspend(ddev->dev);
			return ret;
		}

		/* only set user selected power states */
		if (state != POWER_STATE_TYPE_INTERNAL_BOOT &&
		    state != POWER_STATE_TYPE_DEFAULT) {
			amdgpu_dpm_dispatch_task(adev,
					AMD_PP_TASK_ENABLE_USER_STATE, &state);
			adev->pp_force_state_enabled = true;
		}
		pm_runtime_mark_last_busy(ddev->dev);
		pm_runtime_put_autosuspend(ddev->dev);
	}

	return count;
}

/**
 * DOC: pp_table
 *
 * The amdgpu driver provides a sysfs API for uploading new powerplay
 * tables.  The file pp_table is used for this.  Reading the file
 * will dump the current power play table.  Writing to the file
 * will attempt to upload a new powerplay table and re-initialize
 * powerplay using that new table.
 *
 */

static ssize_t amdgpu_get_pp_table(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	char *table = NULL;
	int size, ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (adev->powerplay.pp_funcs->get_pp_table) {
		size = amdgpu_dpm_get_pp_table(adev, &table);
		pm_runtime_mark_last_busy(ddev->dev);
		pm_runtime_put_autosuspend(ddev->dev);
		if (size < 0)
			return size;
	} else {
		pm_runtime_mark_last_busy(ddev->dev);
		pm_runtime_put_autosuspend(ddev->dev);
		return 0;
	}

	if (size >= PAGE_SIZE)
		size = PAGE_SIZE - 1;

	memcpy(buf, table, size);

	return size;
}

static ssize_t amdgpu_set_pp_table(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int ret = 0;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	ret = amdgpu_dpm_set_pp_table(adev, buf, count);
	if (ret) {
		pm_runtime_mark_last_busy(ddev->dev);
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return count;
}

/**
 * DOC: pp_od_clk_voltage
 *
 * The amdgpu driver provides a sysfs API for adjusting the clocks and voltages
 * in each power level within a power state.  The pp_od_clk_voltage is used for
 * this.
 *
 * Note that the actual memory controller clock rate are exposed, not
 * the effective memory clock of the DRAMs. To translate it, use the
 * following formula:
 *
 * Clock conversion (Mhz):
 *
 * HBM: effective_memory_clock = memory_controller_clock * 1
 *
 * G5: effective_memory_clock = memory_controller_clock * 1
 *
 * G6: effective_memory_clock = memory_controller_clock * 2
 *
 * DRAM data rate (MT/s):
 *
 * HBM: effective_memory_clock * 2 = data_rate
 *
 * G5: effective_memory_clock * 4 = data_rate
 *
 * G6: effective_memory_clock * 8 = data_rate
 *
 * Bandwidth (MB/s):
 *
 * data_rate * vram_bit_width / 8 = memory_bandwidth
 *
 * Some examples:
 *
 * G5 on RX460:
 *
 * memory_controller_clock = 1750 Mhz
 *
 * effective_memory_clock = 1750 Mhz * 1 = 1750 Mhz
 *
 * data rate = 1750 * 4 = 7000 MT/s
 *
 * memory_bandwidth = 7000 * 128 bits / 8 = 112000 MB/s
 *
 * G6 on RX5700:
 *
 * memory_controller_clock = 875 Mhz
 *
 * effective_memory_clock = 875 Mhz * 2 = 1750 Mhz
 *
 * data rate = 1750 * 8 = 14000 MT/s
 *
 * memory_bandwidth = 14000 * 256 bits / 8 = 448000 MB/s
 *
 * < For Vega10 and previous ASICs >
 *
 * Reading the file will display:
 *
 * - a list of engine clock levels and voltages labeled OD_SCLK
 *
 * - a list of memory clock levels and voltages labeled OD_MCLK
 *
 * - a list of valid ranges for sclk, mclk, and voltage labeled OD_RANGE
 *
 * To manually adjust these settings, first select manual using
 * power_dpm_force_performance_level. Enter a new value for each
 * level by writing a string that contains "s/m level clock voltage" to
 * the file.  E.g., "s 1 500 820" will update sclk level 1 to be 500 MHz
 * at 820 mV; "m 0 350 810" will update mclk level 0 to be 350 MHz at
 * 810 mV.  When you have edited all of the states as needed, write
 * "c" (commit) to the file to commit your changes.  If you want to reset to the
 * default power levels, write "r" (reset) to the file to reset them.
 *
 *
 * < For Vega20 and newer ASICs >
 *
 * Reading the file will display:
 *
 * - minimum and maximum engine clock labeled OD_SCLK
 *
 * - minimum(not available for Vega20 and Navi1x) and maximum memory
 *   clock labeled OD_MCLK
 *
 * - three <frequency, voltage> points labeled OD_VDDC_CURVE.
 *   They can be used to calibrate the sclk voltage curve.
 *
 * - voltage offset(in mV) applied on target voltage calculation.
 *   This is available for Sienna Cichlid, Navy Flounder and Dimgrey
 *   Cavefish. For these ASICs, the target voltage calculation can be
 *   illustrated by "voltage = voltage calculated from v/f curve +
 *   overdrive vddgfx offset"
 *
 * - a list of valid ranges for sclk, mclk, and voltage curve points
 *   labeled OD_RANGE
 *
 * To manually adjust these settings:
 *
 * - First select manual using power_dpm_force_performance_level
 *
 * - For clock frequency setting, enter a new value by writing a
 *   string that contains "s/m index clock" to the file. The index
 *   should be 0 if to set minimum clock. And 1 if to set maximum
 *   clock. E.g., "s 0 500" will update minimum sclk to be 500 MHz.
 *   "m 1 800" will update maximum mclk to be 800Mhz.
 *
 *   For sclk voltage curve, enter the new values by writing a
 *   string that contains "vc point clock voltage" to the file. The
 *   points are indexed by 0, 1 and 2. E.g., "vc 0 300 600" will
 *   update point1 with clock set as 300Mhz and voltage as
 *   600mV. "vc 2 1000 1000" will update point3 with clock set
 *   as 1000Mhz and voltage 1000mV.
 *
 *   To update the voltage offset applied for gfxclk/voltage calculation,
 *   enter the new value by writing a string that contains "vo offset".
 *   This is supported by Sienna Cichlid, Navy Flounder and Dimgrey Cavefish.
 *   And the offset can be a positive or negative value.
 *
 * - When you have edited all of the states as needed, write "c" (commit)
 *   to the file to commit your changes
 *
 * - If you want to reset to the default power levels, write "r" (reset)
 *   to the file to reset them
 *
 */

static ssize_t amdgpu_set_pp_od_clk_voltage(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int ret;
	uint32_t parameter_size = 0;
	long parameter[64];
	char buf_cpy[128];
	char *tmp_str;
	char *sub_str;
	const char delimiter[3] = {' ', '\n', '\0'};
	uint32_t type;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	if (count > 127)
		return -EINVAL;

	if (*buf == 's')
		type = PP_OD_EDIT_SCLK_VDDC_TABLE;
	else if (*buf == 'p')
		type = PP_OD_EDIT_CCLK_VDDC_TABLE;
	else if (*buf == 'm')
		type = PP_OD_EDIT_MCLK_VDDC_TABLE;
	else if(*buf == 'r')
		type = PP_OD_RESTORE_DEFAULT_TABLE;
	else if (*buf == 'c')
		type = PP_OD_COMMIT_DPM_TABLE;
	else if (!strncmp(buf, "vc", 2))
		type = PP_OD_EDIT_VDDC_CURVE;
	else if (!strncmp(buf, "vo", 2))
		type = PP_OD_EDIT_VDDGFX_OFFSET;
	else
		return -EINVAL;

	memcpy(buf_cpy, buf, count+1);

	tmp_str = buf_cpy;

	if ((type == PP_OD_EDIT_VDDC_CURVE) ||
	     (type == PP_OD_EDIT_VDDGFX_OFFSET))
		tmp_str++;
	while (isspace(*++tmp_str));

	while ((sub_str = strsep(&tmp_str, delimiter)) != NULL) {
		if (strlen(sub_str) == 0)
			continue;
		ret = kstrtol(sub_str, 0, &parameter[parameter_size]);
		if (ret)
			return -EINVAL;
		parameter_size++;

		while (isspace(*tmp_str))
			tmp_str++;
	}

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (adev->powerplay.pp_funcs->set_fine_grain_clk_vol) {
		ret = amdgpu_dpm_set_fine_grain_clk_vol(adev, type,
							parameter,
							parameter_size);
		if (ret) {
			pm_runtime_mark_last_busy(ddev->dev);
			pm_runtime_put_autosuspend(ddev->dev);
			return -EINVAL;
		}
	}

	if (adev->powerplay.pp_funcs->odn_edit_dpm_table) {
		ret = amdgpu_dpm_odn_edit_dpm_table(adev, type,
						    parameter, parameter_size);
		if (ret) {
			pm_runtime_mark_last_busy(ddev->dev);
			pm_runtime_put_autosuspend(ddev->dev);
			return -EINVAL;
		}
	}

	if (type == PP_OD_COMMIT_DPM_TABLE) {
		if (adev->powerplay.pp_funcs->dispatch_tasks) {
			amdgpu_dpm_dispatch_task(adev,
						 AMD_PP_TASK_READJUST_POWER_STATE,
						 NULL);
			pm_runtime_mark_last_busy(ddev->dev);
			pm_runtime_put_autosuspend(ddev->dev);
			return count;
		} else {
			pm_runtime_mark_last_busy(ddev->dev);
			pm_runtime_put_autosuspend(ddev->dev);
			return -EINVAL;
		}
	}

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return count;
}

static ssize_t amdgpu_get_pp_od_clk_voltage(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	ssize_t size;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (adev->powerplay.pp_funcs->print_clock_levels) {
		size = amdgpu_dpm_print_clock_levels(adev, OD_SCLK, buf);
		size += amdgpu_dpm_print_clock_levels(adev, OD_MCLK, buf+size);
		size += amdgpu_dpm_print_clock_levels(adev, OD_VDDC_CURVE, buf+size);
		size += amdgpu_dpm_print_clock_levels(adev, OD_VDDGFX_OFFSET, buf+size);
		size += amdgpu_dpm_print_clock_levels(adev, OD_RANGE, buf+size);
		size += amdgpu_dpm_print_clock_levels(adev, OD_CCLK, buf+size);
	} else {
		size = snprintf(buf, PAGE_SIZE, "\n");
	}
	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return size;
}

/**
 * DOC: pp_features
 *
 * The amdgpu driver provides a sysfs API for adjusting what powerplay
 * features to be enabled. The file pp_features is used for this. And
 * this is only available for Vega10 and later dGPUs.
 *
 * Reading back the file will show you the followings:
 * - Current ppfeature masks
 * - List of the all supported powerplay features with their naming,
 *   bitmasks and enablement status('Y'/'N' means "enabled"/"disabled").
 *
 * To manually enable or disable a specific feature, just set or clear
 * the corresponding bit from original ppfeature masks and input the
 * new ppfeature masks.
 */
static ssize_t amdgpu_set_pp_features(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	uint64_t featuremask;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = kstrtou64(buf, 0, &featuremask);
	if (ret)
		return -EINVAL;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (adev->powerplay.pp_funcs->set_ppfeature_status) {
		ret = amdgpu_dpm_set_ppfeature_status(adev, featuremask);
		if (ret) {
			pm_runtime_mark_last_busy(ddev->dev);
			pm_runtime_put_autosuspend(ddev->dev);
			return -EINVAL;
		}
	}
	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return count;
}

static ssize_t amdgpu_get_pp_features(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	ssize_t size;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (adev->powerplay.pp_funcs->get_ppfeature_status)
		size = amdgpu_dpm_get_ppfeature_status(adev, buf);
	else
		size = snprintf(buf, PAGE_SIZE, "\n");

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return size;
}

/**
 * DOC: pp_dpm_sclk pp_dpm_mclk pp_dpm_socclk pp_dpm_fclk pp_dpm_dcefclk pp_dpm_pcie
 *
 * The amdgpu driver provides a sysfs API for adjusting what power levels
 * are enabled for a given power state.  The files pp_dpm_sclk, pp_dpm_mclk,
 * pp_dpm_socclk, pp_dpm_fclk, pp_dpm_dcefclk and pp_dpm_pcie are used for
 * this.
 *
 * pp_dpm_socclk and pp_dpm_dcefclk interfaces are only available for
 * Vega10 and later ASICs.
 * pp_dpm_fclk interface is only available for Vega20 and later ASICs.
 *
 * Reading back the files will show you the available power levels within
 * the power state and the clock information for those levels.
 *
 * To manually adjust these states, first select manual using
 * power_dpm_force_performance_level.
 * Secondly, enter a new value for each level by inputing a string that
 * contains " echo xx xx xx > pp_dpm_sclk/mclk/pcie"
 * E.g.,
 *
 * .. code-block:: bash
 *
 *	echo "4 5 6" > pp_dpm_sclk
 *
 * will enable sclk levels 4, 5, and 6.
 *
 * NOTE: change to the dcefclk max dpm level is not supported now
 */

static ssize_t amdgpu_get_pp_dpm_clock(struct device *dev,
		enum pp_clock_type type,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	ssize_t size;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (adev->powerplay.pp_funcs->print_clock_levels)
		size = amdgpu_dpm_print_clock_levels(adev, type, buf);
	else
		size = snprintf(buf, PAGE_SIZE, "\n");

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return size;
}

/*
 * Worst case: 32 bits individually specified, in octal at 12 characters
 * per line (+1 for \n).
 */
#define AMDGPU_MASK_BUF_MAX	(32 * 13)

static ssize_t amdgpu_read_mask(const char *buf, size_t count, uint32_t *mask)
{
	int ret;
	unsigned long level;
	char *sub_str = NULL;
	char *tmp;
	char buf_cpy[AMDGPU_MASK_BUF_MAX + 1];
	const char delimiter[3] = {' ', '\n', '\0'};
	size_t bytes;

	*mask = 0;

	bytes = min(count, sizeof(buf_cpy) - 1);
	memcpy(buf_cpy, buf, bytes);
	buf_cpy[bytes] = '\0';
	tmp = buf_cpy;
	while ((sub_str = strsep(&tmp, delimiter)) != NULL) {
		if (strlen(sub_str)) {
			ret = kstrtoul(sub_str, 0, &level);
			if (ret || level > 31)
				return -EINVAL;
			*mask |= 1 << level;
		} else
			break;
	}

	return 0;
}

static ssize_t amdgpu_set_pp_dpm_clock(struct device *dev,
		enum pp_clock_type type,
		const char *buf,
		size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int ret;
	uint32_t mask = 0;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = amdgpu_read_mask(buf, count, &mask);
	if (ret)
		return ret;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (adev->powerplay.pp_funcs->force_clock_level)
		ret = amdgpu_dpm_force_clock_level(adev, type, mask);
	else
		ret = 0;

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	if (ret)
		return -EINVAL;

	return count;
}

static ssize_t amdgpu_get_pp_dpm_sclk(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return amdgpu_get_pp_dpm_clock(dev, PP_SCLK, buf);
}

static ssize_t amdgpu_set_pp_dpm_sclk(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	return amdgpu_set_pp_dpm_clock(dev, PP_SCLK, buf, count);
}

static ssize_t amdgpu_get_pp_dpm_mclk(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return amdgpu_get_pp_dpm_clock(dev, PP_MCLK, buf);
}

static ssize_t amdgpu_set_pp_dpm_mclk(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	return amdgpu_set_pp_dpm_clock(dev, PP_MCLK, buf, count);
}

static ssize_t amdgpu_get_pp_dpm_socclk(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return amdgpu_get_pp_dpm_clock(dev, PP_SOCCLK, buf);
}

static ssize_t amdgpu_set_pp_dpm_socclk(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	return amdgpu_set_pp_dpm_clock(dev, PP_SOCCLK, buf, count);
}

static ssize_t amdgpu_get_pp_dpm_fclk(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return amdgpu_get_pp_dpm_clock(dev, PP_FCLK, buf);
}

static ssize_t amdgpu_set_pp_dpm_fclk(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	return amdgpu_set_pp_dpm_clock(dev, PP_FCLK, buf, count);
}

static ssize_t amdgpu_get_pp_dpm_vclk(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return amdgpu_get_pp_dpm_clock(dev, PP_VCLK, buf);
}

static ssize_t amdgpu_set_pp_dpm_vclk(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	return amdgpu_set_pp_dpm_clock(dev, PP_VCLK, buf, count);
}

static ssize_t amdgpu_get_pp_dpm_dclk(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return amdgpu_get_pp_dpm_clock(dev, PP_DCLK, buf);
}

static ssize_t amdgpu_set_pp_dpm_dclk(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	return amdgpu_set_pp_dpm_clock(dev, PP_DCLK, buf, count);
}

static ssize_t amdgpu_get_pp_dpm_dcefclk(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return amdgpu_get_pp_dpm_clock(dev, PP_DCEFCLK, buf);
}

static ssize_t amdgpu_set_pp_dpm_dcefclk(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	return amdgpu_set_pp_dpm_clock(dev, PP_DCEFCLK, buf, count);
}

static ssize_t amdgpu_get_pp_dpm_pcie(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return amdgpu_get_pp_dpm_clock(dev, PP_PCIE, buf);
}

static ssize_t amdgpu_set_pp_dpm_pcie(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	return amdgpu_set_pp_dpm_clock(dev, PP_PCIE, buf, count);
}

static ssize_t amdgpu_get_pp_sclk_od(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	uint32_t value = 0;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (is_support_sw_smu(adev))
		value = 0;
	else if (adev->powerplay.pp_funcs->get_sclk_od)
		value = amdgpu_dpm_get_sclk_od(adev);

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t amdgpu_set_pp_sclk_od(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int ret;
	long int value;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = kstrtol(buf, 0, &value);

	if (ret)
		return -EINVAL;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (is_support_sw_smu(adev)) {
		value = 0;
	} else {
		if (adev->powerplay.pp_funcs->set_sclk_od)
			amdgpu_dpm_set_sclk_od(adev, (uint32_t)value);

		if (adev->powerplay.pp_funcs->dispatch_tasks) {
			amdgpu_dpm_dispatch_task(adev, AMD_PP_TASK_READJUST_POWER_STATE, NULL);
		} else {
			adev->pm.dpm.current_ps = adev->pm.dpm.boot_ps;
			amdgpu_pm_compute_clocks(adev);
		}
	}

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return count;
}

static ssize_t amdgpu_get_pp_mclk_od(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	uint32_t value = 0;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (is_support_sw_smu(adev))
		value = 0;
	else if (adev->powerplay.pp_funcs->get_mclk_od)
		value = amdgpu_dpm_get_mclk_od(adev);

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t amdgpu_set_pp_mclk_od(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int ret;
	long int value;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = kstrtol(buf, 0, &value);

	if (ret)
		return -EINVAL;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (is_support_sw_smu(adev)) {
		value = 0;
	} else {
		if (adev->powerplay.pp_funcs->set_mclk_od)
			amdgpu_dpm_set_mclk_od(adev, (uint32_t)value);

		if (adev->powerplay.pp_funcs->dispatch_tasks) {
			amdgpu_dpm_dispatch_task(adev, AMD_PP_TASK_READJUST_POWER_STATE, NULL);
		} else {
			adev->pm.dpm.current_ps = adev->pm.dpm.boot_ps;
			amdgpu_pm_compute_clocks(adev);
		}
	}

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return count;
}

/**
 * DOC: pp_power_profile_mode
 *
 * The amdgpu driver provides a sysfs API for adjusting the heuristics
 * related to switching between power levels in a power state.  The file
 * pp_power_profile_mode is used for this.
 *
 * Reading this file outputs a list of all of the predefined power profiles
 * and the relevant heuristics settings for that profile.
 *
 * To select a profile or create a custom profile, first select manual using
 * power_dpm_force_performance_level.  Writing the number of a predefined
 * profile to pp_power_profile_mode will enable those heuristics.  To
 * create a custom set of heuristics, write a string of numbers to the file
 * starting with the number of the custom profile along with a setting
 * for each heuristic parameter.  Due to differences across asic families
 * the heuristic parameters vary from family to family.
 *
 */

static ssize_t amdgpu_get_pp_power_profile_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	ssize_t size;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (adev->powerplay.pp_funcs->get_power_profile_mode)
		size = amdgpu_dpm_get_power_profile_mode(adev, buf);
	else
		size = snprintf(buf, PAGE_SIZE, "\n");

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return size;
}


static ssize_t amdgpu_set_pp_power_profile_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	int ret;
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	uint32_t parameter_size = 0;
	long parameter[64];
	char *sub_str, buf_cpy[128];
	char *tmp_str;
	uint32_t i = 0;
	char tmp[2];
	long int profile_mode = 0;
	const char delimiter[3] = {' ', '\n', '\0'};

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	tmp[0] = *(buf);
	tmp[1] = '\0';
	ret = kstrtol(tmp, 0, &profile_mode);
	if (ret)
		return -EINVAL;

	if (profile_mode == PP_SMC_POWER_PROFILE_CUSTOM) {
		if (count < 2 || count > 127)
			return -EINVAL;
		while (isspace(*++buf))
			i++;
		memcpy(buf_cpy, buf, count-i);
		tmp_str = buf_cpy;
		while ((sub_str = strsep(&tmp_str, delimiter)) != NULL) {
			if (strlen(sub_str) == 0)
				continue;
			ret = kstrtol(sub_str, 0, &parameter[parameter_size]);
			if (ret)
				return -EINVAL;
			parameter_size++;
			while (isspace(*tmp_str))
				tmp_str++;
		}
	}
	parameter[parameter_size] = profile_mode;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (adev->powerplay.pp_funcs->set_power_profile_mode)
		ret = amdgpu_dpm_set_power_profile_mode(adev, parameter, parameter_size);

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	if (!ret)
		return count;

	return -EINVAL;
}

/**
 * DOC: gpu_busy_percent
 *
 * The amdgpu driver provides a sysfs API for reading how busy the GPU
 * is as a percentage.  The file gpu_busy_percent is used for this.
 * The SMU firmware computes a percentage of load based on the
 * aggregate activity level in the IP cores.
 */
static ssize_t amdgpu_get_gpu_busy_percent(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int r, value, size = sizeof(value);

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	r = pm_runtime_get_sync(ddev->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return r;
	}

	/* read the IP busy sensor */
	r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GPU_LOAD,
				   (void *)&value, &size);

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	if (r)
		return r;

	return sysfs_emit(buf, "%d\n", value);
}

/**
 * DOC: mem_busy_percent
 *
 * The amdgpu driver provides a sysfs API for reading how busy the VRAM
 * is as a percentage.  The file mem_busy_percent is used for this.
 * The SMU firmware computes a percentage of load based on the
 * aggregate activity level in the IP cores.
 */
static ssize_t amdgpu_get_mem_busy_percent(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int r, value, size = sizeof(value);

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	r = pm_runtime_get_sync(ddev->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return r;
	}

	/* read the IP busy sensor */
	r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_MEM_LOAD,
				   (void *)&value, &size);

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	if (r)
		return r;

	return sysfs_emit(buf, "%d\n", value);
}

/**
 * DOC: pcie_bw
 *
 * The amdgpu driver provides a sysfs API for estimating how much data
 * has been received and sent by the GPU in the last second through PCIe.
 * The file pcie_bw is used for this.
 * The Perf counters count the number of received and sent messages and return
 * those values, as well as the maximum payload size of a PCIe packet (mps).
 * Note that it is not possible to easily and quickly obtain the size of each
 * packet transmitted, so we output the max payload size (mps) to allow for
 * quick estimation of the PCIe bandwidth usage
 */
static ssize_t amdgpu_get_pcie_bw(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	uint64_t count0 = 0, count1 = 0;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	if (adev->flags & AMD_IS_APU)
		return -ENODATA;

	if (!adev->asic_funcs->get_pcie_usage)
		return -ENODATA;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	amdgpu_asic_get_pcie_usage(adev, &count0, &count1);

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return sysfs_emit(buf, "%llu %llu %i\n",
			  count0, count1, pcie_get_mps(adev->pdev));
}

/**
 * DOC: unique_id
 *
 * The amdgpu driver provides a sysfs API for providing a unique ID for the GPU
 * The file unique_id is used for this.
 * This will provide a Unique ID that will persist from machine to machine
 *
 * NOTE: This will only work for GFX9 and newer. This file will be absent
 * on unsupported ASICs (GFX8 and older)
 */
static ssize_t amdgpu_get_unique_id(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	if (adev->unique_id)
		return sysfs_emit(buf, "%016llx\n", adev->unique_id);

	return 0;
}

/**
 * DOC: thermal_throttling_logging
 *
 * Thermal throttling pulls down the clock frequency and thus the performance.
 * It's an useful mechanism to protect the chip from overheating. Since it
 * impacts performance, the user controls whether it is enabled and if so,
 * the log frequency.
 *
 * Reading back the file shows you the status(enabled or disabled) and
 * the interval(in seconds) between each thermal logging.
 *
 * Writing an integer to the file, sets a new logging interval, in seconds.
 * The value should be between 1 and 3600. If the value is less than 1,
 * thermal logging is disabled. Values greater than 3600 are ignored.
 */
static ssize_t amdgpu_get_thermal_throttling_logging(struct device *dev,
						     struct device_attribute *attr,
						     char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	return sysfs_emit(buf, "%s: thermal throttling logging %s, with interval %d seconds\n",
			  adev_to_drm(adev)->unique,
			  atomic_read(&adev->throttling_logging_enabled) ? "enabled" : "disabled",
			  adev->throttling_logging_rs.interval / HZ + 1);
}

static ssize_t amdgpu_set_thermal_throttling_logging(struct device *dev,
						     struct device_attribute *attr,
						     const char *buf,
						     size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	long throttling_logging_interval;
	unsigned long flags;
	int ret = 0;

	ret = kstrtol(buf, 0, &throttling_logging_interval);
	if (ret)
		return ret;

	if (throttling_logging_interval > 3600)
		return -EINVAL;

	if (throttling_logging_interval > 0) {
		raw_spin_lock_irqsave(&adev->throttling_logging_rs.lock, flags);
		/*
		 * Reset the ratelimit timer internals.
		 * This can effectively restart the timer.
		 */
		adev->throttling_logging_rs.interval =
			(throttling_logging_interval - 1) * HZ;
		adev->throttling_logging_rs.begin = 0;
		adev->throttling_logging_rs.printed = 0;
		adev->throttling_logging_rs.missed = 0;
		raw_spin_unlock_irqrestore(&adev->throttling_logging_rs.lock, flags);

		atomic_set(&adev->throttling_logging_enabled, 1);
	} else {
		atomic_set(&adev->throttling_logging_enabled, 0);
	}

	return count;
}

/**
 * DOC: gpu_metrics
 *
 * The amdgpu driver provides a sysfs API for retrieving current gpu
 * metrics data. The file gpu_metrics is used for this. Reading the
 * file will dump all the current gpu metrics data.
 *
 * These data include temperature, frequency, engines utilization,
 * power consume, throttler status, fan speed and cpu core statistics(
 * available for APU only). That's it will give a snapshot of all sensors
 * at the same time.
 */
static ssize_t amdgpu_get_gpu_metrics(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	void *gpu_metrics;
	ssize_t size = 0;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (adev->powerplay.pp_funcs->get_gpu_metrics)
		size = amdgpu_dpm_get_gpu_metrics(adev, &gpu_metrics);

	if (size <= 0)
		goto out;

	if (size >= PAGE_SIZE)
		size = PAGE_SIZE - 1;

	memcpy(buf, gpu_metrics, size);

out:
	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return size;
}

static struct amdgpu_device_attr amdgpu_device_attrs[] = {
	AMDGPU_DEVICE_ATTR_RW(power_dpm_state,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RW(power_dpm_force_performance_level,	ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RO(pp_num_states,				ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RO(pp_cur_state,				ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RW(pp_force_state,				ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RW(pp_table,					ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_sclk,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_mclk,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_socclk,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_fclk,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_vclk,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_dclk,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_dcefclk,				ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_pcie,				ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RW(pp_sclk_od,				ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RW(pp_mclk_od,				ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RW(pp_power_profile_mode,			ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RW(pp_od_clk_voltage,			ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RO(gpu_busy_percent,				ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RO(mem_busy_percent,				ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RO(pcie_bw,					ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RW(pp_features,				ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RO(unique_id,				ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RW(thermal_throttling_logging,		ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RO(gpu_metrics,				ATTR_FLAG_BASIC),
};

static int default_attr_update(struct amdgpu_device *adev, struct amdgpu_device_attr *attr,
			       uint32_t mask, enum amdgpu_device_attr_states *states)
{
	struct device_attribute *dev_attr = &attr->dev_attr;
	const char *attr_name = dev_attr->attr.name;
	struct pp_hwmgr *hwmgr = adev->powerplay.pp_handle;
	enum amd_asic_type asic_type = adev->asic_type;

	if (!(attr->flags & mask)) {
		*states = ATTR_STATE_UNSUPPORTED;
		return 0;
	}

#define DEVICE_ATTR_IS(_name)	(!strcmp(attr_name, #_name))

	if (DEVICE_ATTR_IS(pp_dpm_socclk)) {
		if (asic_type < CHIP_VEGA10)
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_dpm_dcefclk)) {
		if (asic_type < CHIP_VEGA10 ||
		    asic_type == CHIP_ARCTURUS ||
		    asic_type == CHIP_ALDEBARAN)
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_dpm_fclk)) {
		if (asic_type < CHIP_VEGA20)
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_od_clk_voltage)) {
		*states = ATTR_STATE_UNSUPPORTED;
		if ((is_support_sw_smu(adev) && adev->smu.od_enabled) ||
		    (is_support_sw_smu(adev) && adev->smu.is_apu) ||
			(!is_support_sw_smu(adev) && hwmgr->od_enabled))
			*states = ATTR_STATE_SUPPORTED;
	} else if (DEVICE_ATTR_IS(mem_busy_percent)) {
		if (adev->flags & AMD_IS_APU || asic_type == CHIP_VEGA10)
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pcie_bw)) {
		/* PCIe Perf counters won't work on APU nodes */
		if (adev->flags & AMD_IS_APU)
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(unique_id)) {
		if (asic_type != CHIP_VEGA10 &&
		    asic_type != CHIP_VEGA20 &&
		    asic_type != CHIP_ARCTURUS)
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_features)) {
		if (adev->flags & AMD_IS_APU || asic_type < CHIP_VEGA10)
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(gpu_metrics)) {
		if (asic_type < CHIP_VEGA12)
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_dpm_vclk)) {
		if (!(asic_type == CHIP_VANGOGH))
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_dpm_dclk)) {
		if (!(asic_type == CHIP_VANGOGH))
			*states = ATTR_STATE_UNSUPPORTED;
	}

	if (asic_type == CHIP_ARCTURUS) {
		/* Arcturus does not support standalone mclk/socclk/fclk level setting */
		if (DEVICE_ATTR_IS(pp_dpm_mclk) ||
		    DEVICE_ATTR_IS(pp_dpm_socclk) ||
		    DEVICE_ATTR_IS(pp_dpm_fclk)) {
			dev_attr->attr.mode &= ~S_IWUGO;
			dev_attr->store = NULL;
		}
	}

	if (DEVICE_ATTR_IS(pp_dpm_dcefclk)) {
		/* SMU MP1 does not support dcefclk level setting */
		if (asic_type >= CHIP_NAVI10) {
			dev_attr->attr.mode &= ~S_IWUGO;
			dev_attr->store = NULL;
		}
	}

#undef DEVICE_ATTR_IS

	return 0;
}


static int amdgpu_device_attr_create(struct amdgpu_device *adev,
				     struct amdgpu_device_attr *attr,
				     uint32_t mask, struct list_head *attr_list)
{
	int ret = 0;
	struct device_attribute *dev_attr = &attr->dev_attr;
	const char *name = dev_attr->attr.name;
	enum amdgpu_device_attr_states attr_states = ATTR_STATE_SUPPORTED;
	struct amdgpu_device_attr_entry *attr_entry;

	int (*attr_update)(struct amdgpu_device *adev, struct amdgpu_device_attr *attr,
			   uint32_t mask, enum amdgpu_device_attr_states *states) = default_attr_update;

	BUG_ON(!attr);

	attr_update = attr->attr_update ? attr_update : default_attr_update;

	ret = attr_update(adev, attr, mask, &attr_states);
	if (ret) {
		dev_err(adev->dev, "failed to update device file %s, ret = %d\n",
			name, ret);
		return ret;
	}

	if (attr_states == ATTR_STATE_UNSUPPORTED)
		return 0;

	ret = device_create_file(adev->dev, dev_attr);
	if (ret) {
		dev_err(adev->dev, "failed to create device file %s, ret = %d\n",
			name, ret);
	}

	attr_entry = kmalloc(sizeof(*attr_entry), GFP_KERNEL);
	if (!attr_entry)
		return -ENOMEM;

	attr_entry->attr = attr;
	INIT_LIST_HEAD(&attr_entry->entry);

	list_add_tail(&attr_entry->entry, attr_list);

	return ret;
}

static void amdgpu_device_attr_remove(struct amdgpu_device *adev, struct amdgpu_device_attr *attr)
{
	struct device_attribute *dev_attr = &attr->dev_attr;

	device_remove_file(adev->dev, dev_attr);
}

static void amdgpu_device_attr_remove_groups(struct amdgpu_device *adev,
					     struct list_head *attr_list);

static int amdgpu_device_attr_create_groups(struct amdgpu_device *adev,
					    struct amdgpu_device_attr *attrs,
					    uint32_t counts,
					    uint32_t mask,
					    struct list_head *attr_list)
{
	int ret = 0;
	uint32_t i = 0;

	for (i = 0; i < counts; i++) {
		ret = amdgpu_device_attr_create(adev, &attrs[i], mask, attr_list);
		if (ret)
			goto failed;
	}

	return 0;

failed:
	amdgpu_device_attr_remove_groups(adev, attr_list);

	return ret;
}

static void amdgpu_device_attr_remove_groups(struct amdgpu_device *adev,
					     struct list_head *attr_list)
{
	struct amdgpu_device_attr_entry *entry, *entry_tmp;

	if (list_empty(attr_list))
		return ;

	list_for_each_entry_safe(entry, entry_tmp, attr_list, entry) {
		amdgpu_device_attr_remove(adev, entry->attr);
		list_del(&entry->entry);
		kfree(entry);
	}
}

static ssize_t amdgpu_hwmon_show_temp(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int channel = to_sensor_dev_attr(attr)->index;
	int r, temp = 0, size = sizeof(temp);

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	if (channel >= PP_TEMP_MAX)
		return -EINVAL;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	switch (channel) {
	case PP_TEMP_JUNCTION:
		/* get current junction temperature */
		r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_HOTSPOT_TEMP,
					   (void *)&temp, &size);
		break;
	case PP_TEMP_EDGE:
		/* get current edge temperature */
		r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_EDGE_TEMP,
					   (void *)&temp, &size);
		break;
	case PP_TEMP_MEM:
		/* get current memory temperature */
		r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_MEM_TEMP,
					   (void *)&temp, &size);
		break;
	default:
		r = -EINVAL;
		break;
	}

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (r)
		return r;

	return sysfs_emit(buf, "%d\n", temp);
}

static ssize_t amdgpu_hwmon_show_temp_thresh(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int hyst = to_sensor_dev_attr(attr)->index;
	int temp;

	if (hyst)
		temp = adev->pm.dpm.thermal.min_temp;
	else
		temp = adev->pm.dpm.thermal.max_temp;

	return sysfs_emit(buf, "%d\n", temp);
}

static ssize_t amdgpu_hwmon_show_hotspot_temp_thresh(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int hyst = to_sensor_dev_attr(attr)->index;
	int temp;

	if (hyst)
		temp = adev->pm.dpm.thermal.min_hotspot_temp;
	else
		temp = adev->pm.dpm.thermal.max_hotspot_crit_temp;

	return sysfs_emit(buf, "%d\n", temp);
}

static ssize_t amdgpu_hwmon_show_mem_temp_thresh(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int hyst = to_sensor_dev_attr(attr)->index;
	int temp;

	if (hyst)
		temp = adev->pm.dpm.thermal.min_mem_temp;
	else
		temp = adev->pm.dpm.thermal.max_mem_crit_temp;

	return sysfs_emit(buf, "%d\n", temp);
}

static ssize_t amdgpu_hwmon_show_temp_label(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	int channel = to_sensor_dev_attr(attr)->index;

	if (channel >= PP_TEMP_MAX)
		return -EINVAL;

	return sysfs_emit(buf, "%s\n", temp_label[channel].label);
}

static ssize_t amdgpu_hwmon_show_temp_emergency(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int channel = to_sensor_dev_attr(attr)->index;
	int temp = 0;

	if (channel >= PP_TEMP_MAX)
		return -EINVAL;

	switch (channel) {
	case PP_TEMP_JUNCTION:
		temp = adev->pm.dpm.thermal.max_hotspot_emergency_temp;
		break;
	case PP_TEMP_EDGE:
		temp = adev->pm.dpm.thermal.max_edge_emergency_temp;
		break;
	case PP_TEMP_MEM:
		temp = adev->pm.dpm.thermal.max_mem_emergency_temp;
		break;
	}

	return sysfs_emit(buf, "%d\n", temp);
}

static ssize_t amdgpu_hwmon_get_pwm1_enable(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	u32 pwm_mode = 0;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return ret;
	}

	if (!adev->powerplay.pp_funcs->get_fan_control_mode) {
		pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return -EINVAL;
	}

	pwm_mode = amdgpu_dpm_get_fan_control_mode(adev);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	return sprintf(buf, "%u\n", pwm_mode);
}

static ssize_t amdgpu_hwmon_set_pwm1_enable(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t count)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int err, ret;
	int value;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	err = kstrtoint(buf, 10, &value);
	if (err)
		return err;

	ret = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return ret;
	}

	if (!adev->powerplay.pp_funcs->set_fan_control_mode) {
		pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return -EINVAL;
	}

	amdgpu_dpm_set_fan_control_mode(adev, value);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	return count;
}

static ssize_t amdgpu_hwmon_get_pwm1_min(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return sprintf(buf, "%i\n", 0);
}

static ssize_t amdgpu_hwmon_get_pwm1_max(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return sprintf(buf, "%i\n", 255);
}

static ssize_t amdgpu_hwmon_set_pwm1(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int err;
	u32 value;
	u32 pwm_mode;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	err = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (err < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return err;
	}

	pwm_mode = amdgpu_dpm_get_fan_control_mode(adev);
	if (pwm_mode != AMD_FAN_CTRL_MANUAL) {
		pr_info("manual fan speed control should be enabled first\n");
		pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return -EINVAL;
	}

	err = kstrtou32(buf, 10, &value);
	if (err) {
		pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return err;
	}

	value = (value * 100) / 255;

	if (adev->powerplay.pp_funcs->set_fan_speed_percent)
		err = amdgpu_dpm_set_fan_speed_percent(adev, value);
	else
		err = -EINVAL;

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (err)
		return err;

	return count;
}

static ssize_t amdgpu_hwmon_get_pwm1(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int err;
	u32 speed = 0;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	err = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (err < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return err;
	}

	if (adev->powerplay.pp_funcs->get_fan_speed_percent)
		err = amdgpu_dpm_get_fan_speed_percent(adev, &speed);
	else
		err = -EINVAL;

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (err)
		return err;

	speed = (speed * 255) / 100;

	return sprintf(buf, "%i\n", speed);
}

static ssize_t amdgpu_hwmon_get_fan1_input(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int err;
	u32 speed = 0;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	err = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (err < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return err;
	}

	if (adev->powerplay.pp_funcs->get_fan_speed_rpm)
		err = amdgpu_dpm_get_fan_speed_rpm(adev, &speed);
	else
		err = -EINVAL;

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (err)
		return err;

	return sprintf(buf, "%i\n", speed);
}

static ssize_t amdgpu_hwmon_get_fan1_min(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	u32 min_rpm = 0;
	u32 size = sizeof(min_rpm);
	int r;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_MIN_FAN_RPM,
				   (void *)&min_rpm, &size);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (r)
		return r;

	return sysfs_emit(buf, "%d\n", min_rpm);
}

static ssize_t amdgpu_hwmon_get_fan1_max(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	u32 max_rpm = 0;
	u32 size = sizeof(max_rpm);
	int r;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_MAX_FAN_RPM,
				   (void *)&max_rpm, &size);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (r)
		return r;

	return sysfs_emit(buf, "%d\n", max_rpm);
}

static ssize_t amdgpu_hwmon_get_fan1_target(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int err;
	u32 rpm = 0;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	err = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (err < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return err;
	}

	if (adev->powerplay.pp_funcs->get_fan_speed_rpm)
		err = amdgpu_dpm_get_fan_speed_rpm(adev, &rpm);
	else
		err = -EINVAL;

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (err)
		return err;

	return sprintf(buf, "%i\n", rpm);
}

static ssize_t amdgpu_hwmon_set_fan1_target(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int err;
	u32 value;
	u32 pwm_mode;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	err = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (err < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return err;
	}

	pwm_mode = amdgpu_dpm_get_fan_control_mode(adev);

	if (pwm_mode != AMD_FAN_CTRL_MANUAL) {
		pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return -ENODATA;
	}

	err = kstrtou32(buf, 10, &value);
	if (err) {
		pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return err;
	}

	if (adev->powerplay.pp_funcs->set_fan_speed_rpm)
		err = amdgpu_dpm_set_fan_speed_rpm(adev, value);
	else
		err = -EINVAL;

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (err)
		return err;

	return count;
}

static ssize_t amdgpu_hwmon_get_fan1_enable(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	u32 pwm_mode = 0;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return ret;
	}

	if (!adev->powerplay.pp_funcs->get_fan_control_mode) {
		pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return -EINVAL;
	}

	pwm_mode = amdgpu_dpm_get_fan_control_mode(adev);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	return sprintf(buf, "%i\n", pwm_mode == AMD_FAN_CTRL_AUTO ? 0 : 1);
}

static ssize_t amdgpu_hwmon_set_fan1_enable(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t count)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int err;
	int value;
	u32 pwm_mode;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	err = kstrtoint(buf, 10, &value);
	if (err)
		return err;

	if (value == 0)
		pwm_mode = AMD_FAN_CTRL_AUTO;
	else if (value == 1)
		pwm_mode = AMD_FAN_CTRL_MANUAL;
	else
		return -EINVAL;

	err = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (err < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return err;
	}

	if (!adev->powerplay.pp_funcs->set_fan_control_mode) {
		pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return -EINVAL;
	}
	amdgpu_dpm_set_fan_control_mode(adev, pwm_mode);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	return count;
}

static ssize_t amdgpu_hwmon_show_vddgfx(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	u32 vddgfx;
	int r, size = sizeof(vddgfx);

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	/* get the voltage */
	r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_VDDGFX,
				   (void *)&vddgfx, &size);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (r)
		return r;

	return sysfs_emit(buf, "%d\n", vddgfx);
}

static ssize_t amdgpu_hwmon_show_vddgfx_label(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	return sysfs_emit(buf, "vddgfx\n");
}

static ssize_t amdgpu_hwmon_show_vddnb(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	u32 vddnb;
	int r, size = sizeof(vddnb);

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	/* only APUs have vddnb */
	if  (!(adev->flags & AMD_IS_APU))
		return -EINVAL;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	/* get the voltage */
	r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_VDDNB,
				   (void *)&vddnb, &size);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (r)
		return r;

	return sysfs_emit(buf, "%d\n", vddnb);
}

static ssize_t amdgpu_hwmon_show_vddnb_label(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	return sysfs_emit(buf, "vddnb\n");
}

static ssize_t amdgpu_hwmon_show_power_avg(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	u32 query = 0;
	int r, size = sizeof(u32);
	unsigned uw;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	/* get the voltage */
	r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GPU_POWER,
				   (void *)&query, &size);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (r)
		return r;

	/* convert to microwatts */
	uw = (query >> 8) * 1000000 + (query & 0xff) * 1000;

	return sysfs_emit(buf, "%u\n", uw);
}

static ssize_t amdgpu_hwmon_show_power_cap_min(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return sprintf(buf, "%i\n", 0);
}

static ssize_t amdgpu_hwmon_show_power_cap_max(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int limit_type = to_sensor_dev_attr(attr)->index;
	uint32_t limit = limit_type << 24;
	uint32_t max_limit = 0;
	ssize_t size;
	int r;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	if (is_support_sw_smu(adev)) {
		smu_get_power_limit(&adev->smu, &limit, SMU_PPT_LIMIT_MAX);
		size = snprintf(buf, PAGE_SIZE, "%u\n", limit * 1000000);
	} else if (pp_funcs && pp_funcs->get_power_limit) {
		pp_funcs->get_power_limit(adev->powerplay.pp_handle,
				&limit, &max_limit, true);
		size = snprintf(buf, PAGE_SIZE, "%u\n", max_limit * 1000000);
	} else {
		size = snprintf(buf, PAGE_SIZE, "\n");
	}

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	return size;
}

static ssize_t amdgpu_hwmon_show_power_cap(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int limit_type = to_sensor_dev_attr(attr)->index;
	uint32_t limit = limit_type << 24;
	ssize_t size;
	int r;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	if (is_support_sw_smu(adev)) {
		smu_get_power_limit(&adev->smu, &limit, SMU_PPT_LIMIT_CURRENT);
		size = snprintf(buf, PAGE_SIZE, "%u\n", limit * 1000000);
	} else if (pp_funcs && pp_funcs->get_power_limit) {
		pp_funcs->get_power_limit(adev->powerplay.pp_handle,
				&limit, NULL, false);
		size = snprintf(buf, PAGE_SIZE, "%u\n", limit * 1000000);
	} else {
		size = snprintf(buf, PAGE_SIZE, "\n");
	}

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	return size;
}

static ssize_t amdgpu_hwmon_show_power_cap_default(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int limit_type = to_sensor_dev_attr(attr)->index;
	uint32_t limit = limit_type << 24;
	ssize_t size;
	int r;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	if (is_support_sw_smu(adev)) {
		smu_get_power_limit(&adev->smu, &limit, SMU_PPT_LIMIT_DEFAULT);
		size = snprintf(buf, PAGE_SIZE, "%u\n", limit * 1000000);
	} else if (pp_funcs && pp_funcs->get_power_limit) {
		pp_funcs->get_power_limit(adev->powerplay.pp_handle,
				&limit, NULL, true);
		size = snprintf(buf, PAGE_SIZE, "%u\n", limit * 1000000);
	} else {
		size = snprintf(buf, PAGE_SIZE, "\n");
	}

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	return size;
}
static ssize_t amdgpu_hwmon_show_power_label(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int limit_type = to_sensor_dev_attr(attr)->index;

	return sysfs_emit(buf, "%s\n",
		limit_type == SMU_FAST_PPT_LIMIT ? "fastPPT" : "slowPPT");
}

static ssize_t amdgpu_hwmon_set_power_cap(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int limit_type = to_sensor_dev_attr(attr)->index;
	int err;
	u32 value;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	if (amdgpu_sriov_vf(adev))
		return -EINVAL;

	err = kstrtou32(buf, 10, &value);
	if (err)
		return err;

	value = value / 1000000; /* convert to Watt */
	value |= limit_type << 24;

	err = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (err < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return err;
	}

	if (pp_funcs && pp_funcs->set_power_limit)
		err = pp_funcs->set_power_limit(adev->powerplay.pp_handle, value);
	else
		err = -EINVAL;

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (err)
		return err;

	return count;
}

static ssize_t amdgpu_hwmon_show_sclk(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	uint32_t sclk;
	int r, size = sizeof(sclk);

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	/* get the sclk */
	r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GFX_SCLK,
				   (void *)&sclk, &size);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (r)
		return r;

	return sysfs_emit(buf, "%u\n", sclk * 10 * 1000);
}

static ssize_t amdgpu_hwmon_show_sclk_label(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	return sysfs_emit(buf, "sclk\n");
}

static ssize_t amdgpu_hwmon_show_mclk(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	uint32_t mclk;
	int r, size = sizeof(mclk);

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	/* get the sclk */
	r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GFX_MCLK,
				   (void *)&mclk, &size);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (r)
		return r;

	return sysfs_emit(buf, "%u\n", mclk * 10 * 1000);
}

static ssize_t amdgpu_hwmon_show_mclk_label(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	return sysfs_emit(buf, "mclk\n");
}

/**
 * DOC: hwmon
 *
 * The amdgpu driver exposes the following sensor interfaces:
 *
 * - GPU temperature (via the on-die sensor)
 *
 * - GPU voltage
 *
 * - Northbridge voltage (APUs only)
 *
 * - GPU power
 *
 * - GPU fan
 *
 * - GPU gfx/compute engine clock
 *
 * - GPU memory clock (dGPU only)
 *
 * hwmon interfaces for GPU temperature:
 *
 * - temp[1-3]_input: the on die GPU temperature in millidegrees Celsius
 *   - temp2_input and temp3_input are supported on SOC15 dGPUs only
 *
 * - temp[1-3]_label: temperature channel label
 *   - temp2_label and temp3_label are supported on SOC15 dGPUs only
 *
 * - temp[1-3]_crit: temperature critical max value in millidegrees Celsius
 *   - temp2_crit and temp3_crit are supported on SOC15 dGPUs only
 *
 * - temp[1-3]_crit_hyst: temperature hysteresis for critical limit in millidegrees Celsius
 *   - temp2_crit_hyst and temp3_crit_hyst are supported on SOC15 dGPUs only
 *
 * - temp[1-3]_emergency: temperature emergency max value(asic shutdown) in millidegrees Celsius
 *   - these are supported on SOC15 dGPUs only
 *
 * hwmon interfaces for GPU voltage:
 *
 * - in0_input: the voltage on the GPU in millivolts
 *
 * - in1_input: the voltage on the Northbridge in millivolts
 *
 * hwmon interfaces for GPU power:
 *
 * - power1_average: average power used by the GPU in microWatts
 *
 * - power1_cap_min: minimum cap supported in microWatts
 *
 * - power1_cap_max: maximum cap supported in microWatts
 *
 * - power1_cap: selected power cap in microWatts
 *
 * hwmon interfaces for GPU fan:
 *
 * - pwm1: pulse width modulation fan level (0-255)
 *
 * - pwm1_enable: pulse width modulation fan control method (0: no fan speed control, 1: manual fan speed control using pwm interface, 2: automatic fan speed control)
 *
 * - pwm1_min: pulse width modulation fan control minimum level (0)
 *
 * - pwm1_max: pulse width modulation fan control maximum level (255)
 *
 * - fan1_min: a minimum value Unit: revolution/min (RPM)
 *
 * - fan1_max: a maximum value Unit: revolution/max (RPM)
 *
 * - fan1_input: fan speed in RPM
 *
 * - fan[1-\*]_target: Desired fan speed Unit: revolution/min (RPM)
 *
 * - fan[1-\*]_enable: Enable or disable the sensors.1: Enable 0: Disable
 *
 * hwmon interfaces for GPU clocks:
 *
 * - freq1_input: the gfx/compute clock in hertz
 *
 * - freq2_input: the memory clock in hertz
 *
 * You can use hwmon tools like sensors to view this information on your system.
 *
 */

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, amdgpu_hwmon_show_temp, NULL, PP_TEMP_EDGE);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, amdgpu_hwmon_show_temp_thresh, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IRUGO, amdgpu_hwmon_show_temp_thresh, NULL, 1);
static SENSOR_DEVICE_ATTR(temp1_emergency, S_IRUGO, amdgpu_hwmon_show_temp_emergency, NULL, PP_TEMP_EDGE);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, amdgpu_hwmon_show_temp, NULL, PP_TEMP_JUNCTION);
static SENSOR_DEVICE_ATTR(temp2_crit, S_IRUGO, amdgpu_hwmon_show_hotspot_temp_thresh, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_crit_hyst, S_IRUGO, amdgpu_hwmon_show_hotspot_temp_thresh, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_emergency, S_IRUGO, amdgpu_hwmon_show_temp_emergency, NULL, PP_TEMP_JUNCTION);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, amdgpu_hwmon_show_temp, NULL, PP_TEMP_MEM);
static SENSOR_DEVICE_ATTR(temp3_crit, S_IRUGO, amdgpu_hwmon_show_mem_temp_thresh, NULL, 0);
static SENSOR_DEVICE_ATTR(temp3_crit_hyst, S_IRUGO, amdgpu_hwmon_show_mem_temp_thresh, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_emergency, S_IRUGO, amdgpu_hwmon_show_temp_emergency, NULL, PP_TEMP_MEM);
static SENSOR_DEVICE_ATTR(temp1_label, S_IRUGO, amdgpu_hwmon_show_temp_label, NULL, PP_TEMP_EDGE);
static SENSOR_DEVICE_ATTR(temp2_label, S_IRUGO, amdgpu_hwmon_show_temp_label, NULL, PP_TEMP_JUNCTION);
static SENSOR_DEVICE_ATTR(temp3_label, S_IRUGO, amdgpu_hwmon_show_temp_label, NULL, PP_TEMP_MEM);
static SENSOR_DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR, amdgpu_hwmon_get_pwm1, amdgpu_hwmon_set_pwm1, 0);
static SENSOR_DEVICE_ATTR(pwm1_enable, S_IRUGO | S_IWUSR, amdgpu_hwmon_get_pwm1_enable, amdgpu_hwmon_set_pwm1_enable, 0);
static SENSOR_DEVICE_ATTR(pwm1_min, S_IRUGO, amdgpu_hwmon_get_pwm1_min, NULL, 0);
static SENSOR_DEVICE_ATTR(pwm1_max, S_IRUGO, amdgpu_hwmon_get_pwm1_max, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, amdgpu_hwmon_get_fan1_input, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_min, S_IRUGO, amdgpu_hwmon_get_fan1_min, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_max, S_IRUGO, amdgpu_hwmon_get_fan1_max, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_target, S_IRUGO | S_IWUSR, amdgpu_hwmon_get_fan1_target, amdgpu_hwmon_set_fan1_target, 0);
static SENSOR_DEVICE_ATTR(fan1_enable, S_IRUGO | S_IWUSR, amdgpu_hwmon_get_fan1_enable, amdgpu_hwmon_set_fan1_enable, 0);
static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, amdgpu_hwmon_show_vddgfx, NULL, 0);
static SENSOR_DEVICE_ATTR(in0_label, S_IRUGO, amdgpu_hwmon_show_vddgfx_label, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, amdgpu_hwmon_show_vddnb, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_label, S_IRUGO, amdgpu_hwmon_show_vddnb_label, NULL, 0);
static SENSOR_DEVICE_ATTR(power1_average, S_IRUGO, amdgpu_hwmon_show_power_avg, NULL, 0);
static SENSOR_DEVICE_ATTR(power1_cap_max, S_IRUGO, amdgpu_hwmon_show_power_cap_max, NULL, 0);
static SENSOR_DEVICE_ATTR(power1_cap_min, S_IRUGO, amdgpu_hwmon_show_power_cap_min, NULL, 0);
static SENSOR_DEVICE_ATTR(power1_cap, S_IRUGO | S_IWUSR, amdgpu_hwmon_show_power_cap, amdgpu_hwmon_set_power_cap, 0);
static SENSOR_DEVICE_ATTR(power1_cap_default, S_IRUGO, amdgpu_hwmon_show_power_cap_default, NULL, 0);
static SENSOR_DEVICE_ATTR(power1_label, S_IRUGO, amdgpu_hwmon_show_power_label, NULL, 0);
static SENSOR_DEVICE_ATTR(power2_average, S_IRUGO, amdgpu_hwmon_show_power_avg, NULL, 1);
static SENSOR_DEVICE_ATTR(power2_cap_max, S_IRUGO, amdgpu_hwmon_show_power_cap_max, NULL, 1);
static SENSOR_DEVICE_ATTR(power2_cap_min, S_IRUGO, amdgpu_hwmon_show_power_cap_min, NULL, 1);
static SENSOR_DEVICE_ATTR(power2_cap, S_IRUGO | S_IWUSR, amdgpu_hwmon_show_power_cap, amdgpu_hwmon_set_power_cap, 1);
static SENSOR_DEVICE_ATTR(power2_cap_default, S_IRUGO, amdgpu_hwmon_show_power_cap_default, NULL, 1);
static SENSOR_DEVICE_ATTR(power2_label, S_IRUGO, amdgpu_hwmon_show_power_label, NULL, 1);
static SENSOR_DEVICE_ATTR(freq1_input, S_IRUGO, amdgpu_hwmon_show_sclk, NULL, 0);
static SENSOR_DEVICE_ATTR(freq1_label, S_IRUGO, amdgpu_hwmon_show_sclk_label, NULL, 0);
static SENSOR_DEVICE_ATTR(freq2_input, S_IRUGO, amdgpu_hwmon_show_mclk, NULL, 0);
static SENSOR_DEVICE_ATTR(freq2_label, S_IRUGO, amdgpu_hwmon_show_mclk_label, NULL, 0);

static struct attribute *hwmon_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_crit.dev_attr.attr,
	&sensor_dev_attr_temp3_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_emergency.dev_attr.attr,
	&sensor_dev_attr_temp2_emergency.dev_attr.attr,
	&sensor_dev_attr_temp3_emergency.dev_attr.attr,
	&sensor_dev_attr_temp1_label.dev_attr.attr,
	&sensor_dev_attr_temp2_label.dev_attr.attr,
	&sensor_dev_attr_temp3_label.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1_min.dev_attr.attr,
	&sensor_dev_attr_pwm1_max.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_max.dev_attr.attr,
	&sensor_dev_attr_fan1_target.dev_attr.attr,
	&sensor_dev_attr_fan1_enable.dev_attr.attr,
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_label.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_label.dev_attr.attr,
	&sensor_dev_attr_power1_average.dev_attr.attr,
	&sensor_dev_attr_power1_cap_max.dev_attr.attr,
	&sensor_dev_attr_power1_cap_min.dev_attr.attr,
	&sensor_dev_attr_power1_cap.dev_attr.attr,
	&sensor_dev_attr_power1_cap_default.dev_attr.attr,
	&sensor_dev_attr_power1_label.dev_attr.attr,
	&sensor_dev_attr_power2_average.dev_attr.attr,
	&sensor_dev_attr_power2_cap_max.dev_attr.attr,
	&sensor_dev_attr_power2_cap_min.dev_attr.attr,
	&sensor_dev_attr_power2_cap.dev_attr.attr,
	&sensor_dev_attr_power2_cap_default.dev_attr.attr,
	&sensor_dev_attr_power2_label.dev_attr.attr,
	&sensor_dev_attr_freq1_input.dev_attr.attr,
	&sensor_dev_attr_freq1_label.dev_attr.attr,
	&sensor_dev_attr_freq2_input.dev_attr.attr,
	&sensor_dev_attr_freq2_label.dev_attr.attr,
	NULL
};

static umode_t hwmon_attributes_visible(struct kobject *kobj,
					struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	umode_t effective_mode = attr->mode;

	/* under multi-vf mode, the hwmon attributes are all not supported */
	if (amdgpu_sriov_vf(adev) && !amdgpu_sriov_is_pp_one_vf(adev))
		return 0;

	/* there is no fan under pp one vf mode */
	if (amdgpu_sriov_is_pp_one_vf(adev) &&
	    (attr == &sensor_dev_attr_pwm1.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_enable.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_min.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_input.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_min.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_target.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_enable.dev_attr.attr))
		return 0;

	/* Skip fan attributes if fan is not present */
	if (adev->pm.no_fan && (attr == &sensor_dev_attr_pwm1.dev_attr.attr ||
	    attr == &sensor_dev_attr_pwm1_enable.dev_attr.attr ||
	    attr == &sensor_dev_attr_pwm1_max.dev_attr.attr ||
	    attr == &sensor_dev_attr_pwm1_min.dev_attr.attr ||
	    attr == &sensor_dev_attr_fan1_input.dev_attr.attr ||
	    attr == &sensor_dev_attr_fan1_min.dev_attr.attr ||
	    attr == &sensor_dev_attr_fan1_max.dev_attr.attr ||
	    attr == &sensor_dev_attr_fan1_target.dev_attr.attr ||
	    attr == &sensor_dev_attr_fan1_enable.dev_attr.attr))
		return 0;

	/* Skip fan attributes on APU */
	if ((adev->flags & AMD_IS_APU) &&
	    (attr == &sensor_dev_attr_pwm1.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_enable.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_min.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_input.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_min.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_target.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_enable.dev_attr.attr))
		return 0;

	/* Skip crit temp on APU */
	if ((adev->flags & AMD_IS_APU) && (adev->family >= AMDGPU_FAMILY_CZ) &&
	    (attr == &sensor_dev_attr_temp1_crit.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp1_crit_hyst.dev_attr.attr))
		return 0;

	/* Skip limit attributes if DPM is not enabled */
	if (!adev->pm.dpm_enabled &&
	    (attr == &sensor_dev_attr_temp1_crit.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp1_crit_hyst.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_enable.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_min.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_input.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_min.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_target.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_enable.dev_attr.attr))
		return 0;

	if (!is_support_sw_smu(adev)) {
		/* mask fan attributes if we have no bindings for this asic to expose */
		if ((!adev->powerplay.pp_funcs->get_fan_speed_percent &&
		     attr == &sensor_dev_attr_pwm1.dev_attr.attr) || /* can't query fan */
		    (!adev->powerplay.pp_funcs->get_fan_control_mode &&
		     attr == &sensor_dev_attr_pwm1_enable.dev_attr.attr)) /* can't query state */
			effective_mode &= ~S_IRUGO;

		if ((!adev->powerplay.pp_funcs->set_fan_speed_percent &&
		     attr == &sensor_dev_attr_pwm1.dev_attr.attr) || /* can't manage fan */
		    (!adev->powerplay.pp_funcs->set_fan_control_mode &&
		     attr == &sensor_dev_attr_pwm1_enable.dev_attr.attr)) /* can't manage state */
			effective_mode &= ~S_IWUSR;
	}

	if (((adev->family == AMDGPU_FAMILY_SI) ||
		 ((adev->flags & AMD_IS_APU) &&
	      (adev->asic_type != CHIP_VANGOGH))) &&	/* not implemented yet */
	    (attr == &sensor_dev_attr_power1_cap_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_power1_cap_min.dev_attr.attr||
	     attr == &sensor_dev_attr_power1_cap.dev_attr.attr ||
	     attr == &sensor_dev_attr_power1_cap_default.dev_attr.attr))
		return 0;

	if (((adev->family == AMDGPU_FAMILY_SI) ||
	     ((adev->flags & AMD_IS_APU) &&
	      (adev->asic_type < CHIP_RENOIR))) &&	/* not implemented yet */
	    (attr == &sensor_dev_attr_power1_average.dev_attr.attr))
		return 0;

	if (!is_support_sw_smu(adev)) {
		/* hide max/min values if we can't both query and manage the fan */
		if ((!adev->powerplay.pp_funcs->set_fan_speed_percent &&
		     !adev->powerplay.pp_funcs->get_fan_speed_percent) &&
		     (!adev->powerplay.pp_funcs->set_fan_speed_rpm &&
		     !adev->powerplay.pp_funcs->get_fan_speed_rpm) &&
		    (attr == &sensor_dev_attr_pwm1_max.dev_attr.attr ||
		     attr == &sensor_dev_attr_pwm1_min.dev_attr.attr))
			return 0;

		if ((!adev->powerplay.pp_funcs->set_fan_speed_rpm &&
		     !adev->powerplay.pp_funcs->get_fan_speed_rpm) &&
		    (attr == &sensor_dev_attr_fan1_max.dev_attr.attr ||
		     attr == &sensor_dev_attr_fan1_min.dev_attr.attr))
			return 0;
	}

	if ((adev->family == AMDGPU_FAMILY_SI ||	/* not implemented yet */
	     adev->family == AMDGPU_FAMILY_KV) &&	/* not implemented yet */
	    (attr == &sensor_dev_attr_in0_input.dev_attr.attr ||
	     attr == &sensor_dev_attr_in0_label.dev_attr.attr))
		return 0;

	/* only APUs have vddnb */
	if (!(adev->flags & AMD_IS_APU) &&
	    (attr == &sensor_dev_attr_in1_input.dev_attr.attr ||
	     attr == &sensor_dev_attr_in1_label.dev_attr.attr))
		return 0;

	/* no mclk on APUs */
	if ((adev->flags & AMD_IS_APU) &&
	    (attr == &sensor_dev_attr_freq2_input.dev_attr.attr ||
	     attr == &sensor_dev_attr_freq2_label.dev_attr.attr))
		return 0;

	/* only SOC15 dGPUs support hotspot and mem temperatures */
	if (((adev->flags & AMD_IS_APU) ||
	     adev->asic_type < CHIP_VEGA10) &&
	    (attr == &sensor_dev_attr_temp2_crit.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp2_crit_hyst.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp3_crit.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp3_crit_hyst.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp1_emergency.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp2_emergency.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp3_emergency.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp2_input.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp3_input.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp2_label.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp3_label.dev_attr.attr))
		return 0;

	/* only Vangogh has fast PPT limit and power labels */
	if (!(adev->asic_type == CHIP_VANGOGH) &&
	    (attr == &sensor_dev_attr_power2_average.dev_attr.attr ||
		 attr == &sensor_dev_attr_power2_cap_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_power2_cap_min.dev_attr.attr ||
		 attr == &sensor_dev_attr_power2_cap.dev_attr.attr ||
		 attr == &sensor_dev_attr_power2_cap_default.dev_attr.attr ||
		 attr == &sensor_dev_attr_power2_label.dev_attr.attr ||
		 attr == &sensor_dev_attr_power1_label.dev_attr.attr))
		return 0;

	return effective_mode;
}

static const struct attribute_group hwmon_attrgroup = {
	.attrs = hwmon_attributes,
	.is_visible = hwmon_attributes_visible,
};

static const struct attribute_group *hwmon_groups[] = {
	&hwmon_attrgroup,
	NULL
};

int amdgpu_pm_sysfs_init(struct amdgpu_device *adev)
{
	int ret;
	uint32_t mask = 0;

	if (adev->pm.sysfs_initialized)
		return 0;

	if (adev->pm.dpm_enabled == 0)
		return 0;

	INIT_LIST_HEAD(&adev->pm.pm_attr_list);

	adev->pm.int_hwmon_dev = hwmon_device_register_with_groups(adev->dev,
								   DRIVER_NAME, adev,
								   hwmon_groups);
	if (IS_ERR(adev->pm.int_hwmon_dev)) {
		ret = PTR_ERR(adev->pm.int_hwmon_dev);
		dev_err(adev->dev,
			"Unable to register hwmon device: %d\n", ret);
		return ret;
	}

	switch (amdgpu_virt_get_sriov_vf_mode(adev)) {
	case SRIOV_VF_MODE_ONE_VF:
		mask = ATTR_FLAG_ONEVF;
		break;
	case SRIOV_VF_MODE_MULTI_VF:
		mask = 0;
		break;
	case SRIOV_VF_MODE_BARE_METAL:
	default:
		mask = ATTR_FLAG_MASK_ALL;
		break;
	}

	ret = amdgpu_device_attr_create_groups(adev,
					       amdgpu_device_attrs,
					       ARRAY_SIZE(amdgpu_device_attrs),
					       mask,
					       &adev->pm.pm_attr_list);
	if (ret)
		return ret;

	adev->pm.sysfs_initialized = true;

	return 0;
}

void amdgpu_pm_sysfs_fini(struct amdgpu_device *adev)
{
	if (adev->pm.dpm_enabled == 0)
		return;

	if (adev->pm.int_hwmon_dev)
		hwmon_device_unregister(adev->pm.int_hwmon_dev);

	amdgpu_device_attr_remove_groups(adev, &adev->pm.pm_attr_list);
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)

static void amdgpu_debugfs_prints_cpu_info(struct seq_file *m,
					   struct amdgpu_device *adev) {
	uint16_t *p_val;
	uint32_t size;
	int i;

	if (is_support_cclk_dpm(adev)) {
		p_val = kcalloc(adev->smu.cpu_core_num, sizeof(uint16_t),
				GFP_KERNEL);

		if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_CPU_CLK,
					    (void *)p_val, &size)) {
			for (i = 0; i < adev->smu.cpu_core_num; i++)
				seq_printf(m, "\t%u MHz (CPU%d)\n",
					   *(p_val + i), i);
		}

		kfree(p_val);
	}
}

static int amdgpu_debugfs_pm_info_pp(struct seq_file *m, struct amdgpu_device *adev)
{
	uint32_t value;
	uint64_t value64 = 0;
	uint32_t query = 0;
	int size;

	/* GPU Clocks */
	size = sizeof(value);
	seq_printf(m, "GFX Clocks and Power:\n");

	amdgpu_debugfs_prints_cpu_info(m, adev);

	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GFX_MCLK, (void *)&value, &size))
		seq_printf(m, "\t%u MHz (MCLK)\n", value/100);
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GFX_SCLK, (void *)&value, &size))
		seq_printf(m, "\t%u MHz (SCLK)\n", value/100);
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_STABLE_PSTATE_SCLK, (void *)&value, &size))
		seq_printf(m, "\t%u MHz (PSTATE_SCLK)\n", value/100);
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_STABLE_PSTATE_MCLK, (void *)&value, &size))
		seq_printf(m, "\t%u MHz (PSTATE_MCLK)\n", value/100);
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_VDDGFX, (void *)&value, &size))
		seq_printf(m, "\t%u mV (VDDGFX)\n", value);
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_VDDNB, (void *)&value, &size))
		seq_printf(m, "\t%u mV (VDDNB)\n", value);
	size = sizeof(uint32_t);
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GPU_POWER, (void *)&query, &size))
		seq_printf(m, "\t%u.%u W (average GPU)\n", query >> 8, query & 0xff);
	size = sizeof(value);
	seq_printf(m, "\n");

	/* GPU Temp */
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GPU_TEMP, (void *)&value, &size))
		seq_printf(m, "GPU Temperature: %u C\n", value/1000);

	/* GPU Load */
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GPU_LOAD, (void *)&value, &size))
		seq_printf(m, "GPU Load: %u %%\n", value);
	/* MEM Load */
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_MEM_LOAD, (void *)&value, &size))
		seq_printf(m, "MEM Load: %u %%\n", value);

	seq_printf(m, "\n");

	/* SMC feature mask */
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_ENABLED_SMC_FEATURES_MASK, (void *)&value64, &size))
		seq_printf(m, "SMC Feature Mask: 0x%016llx\n", value64);

	if (adev->asic_type > CHIP_VEGA20) {
		/* VCN clocks */
		if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_VCN_POWER_STATE, (void *)&value, &size)) {
			if (!value) {
				seq_printf(m, "VCN: Disabled\n");
			} else {
				seq_printf(m, "VCN: Enabled\n");
				if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_UVD_DCLK, (void *)&value, &size))
					seq_printf(m, "\t%u MHz (DCLK)\n", value/100);
				if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_UVD_VCLK, (void *)&value, &size))
					seq_printf(m, "\t%u MHz (VCLK)\n", value/100);
			}
		}
		seq_printf(m, "\n");
	} else {
		/* UVD clocks */
		if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_UVD_POWER, (void *)&value, &size)) {
			if (!value) {
				seq_printf(m, "UVD: Disabled\n");
			} else {
				seq_printf(m, "UVD: Enabled\n");
				if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_UVD_DCLK, (void *)&value, &size))
					seq_printf(m, "\t%u MHz (DCLK)\n", value/100);
				if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_UVD_VCLK, (void *)&value, &size))
					seq_printf(m, "\t%u MHz (VCLK)\n", value/100);
			}
		}
		seq_printf(m, "\n");

		/* VCE clocks */
		if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_VCE_POWER, (void *)&value, &size)) {
			if (!value) {
				seq_printf(m, "VCE: Disabled\n");
			} else {
				seq_printf(m, "VCE: Enabled\n");
				if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_VCE_ECCLK, (void *)&value, &size))
					seq_printf(m, "\t%u MHz (ECCLK)\n", value/100);
			}
		}
	}

	return 0;
}

static void amdgpu_parse_cg_state(struct seq_file *m, u32 flags)
{
	int i;

	for (i = 0; clocks[i].flag; i++)
		seq_printf(m, "\t%s: %s\n", clocks[i].name,
			   (flags & clocks[i].flag) ? "On" : "Off");
}

static int amdgpu_debugfs_pm_info_show(struct seq_file *m, void *unused)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)m->private;
	struct drm_device *dev = adev_to_drm(adev);
	u32 flags = 0;
	int r;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	r = pm_runtime_get_sync(dev->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(dev->dev);
		return r;
	}

	if (!adev->pm.dpm_enabled) {
		seq_printf(m, "dpm not enabled\n");
		pm_runtime_mark_last_busy(dev->dev);
		pm_runtime_put_autosuspend(dev->dev);
		return 0;
	}

	if (!is_support_sw_smu(adev) &&
	    adev->powerplay.pp_funcs->debugfs_print_current_performance_level) {
		mutex_lock(&adev->pm.mutex);
		if (adev->powerplay.pp_funcs->debugfs_print_current_performance_level)
			adev->powerplay.pp_funcs->debugfs_print_current_performance_level(adev, m);
		else
			seq_printf(m, "Debugfs support not implemented for this asic\n");
		mutex_unlock(&adev->pm.mutex);
		r = 0;
	} else {
		r = amdgpu_debugfs_pm_info_pp(m, adev);
	}
	if (r)
		goto out;

	amdgpu_device_ip_get_clockgating_state(adev, &flags);

	seq_printf(m, "Clock Gating Flags Mask: 0x%x\n", flags);
	amdgpu_parse_cg_state(m, flags);
	seq_printf(m, "\n");

out:
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return r;
}

DEFINE_SHOW_ATTRIBUTE(amdgpu_debugfs_pm_info);

#endif

void amdgpu_debugfs_pm_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *root = minor->debugfs_root;

	debugfs_create_file("amdgpu_pm_info", 0444, root, adev,
			    &amdgpu_debugfs_pm_info_fops);

#endif
}

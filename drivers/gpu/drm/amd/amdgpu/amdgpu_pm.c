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
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_drv.h"
#include "amdgpu_pm.h"
#include "amdgpu_dpm.h"
#include "atom.h"
#include <linux/power_supply.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/nospec.h>

static int amdgpu_debugfs_pm_init(struct amdgpu_device *adev);

static const struct cg_flag_name clocks[] = {
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
	{0, NULL},
};

void amdgpu_pm_acpi_event_handler(struct amdgpu_device *adev)
{
	if (adev->pm.dpm_enabled) {
		mutex_lock(&adev->pm.mutex);
		if (power_supply_is_system_supplied() > 0)
			adev->pm.ac_power = true;
		else
			adev->pm.ac_power = false;
		if (adev->powerplay.pp_funcs->enable_bapm)
			amdgpu_dpm_enable_bapm(adev, adev->pm.ac_power);
		mutex_unlock(&adev->pm.mutex);
	}
}

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

static ssize_t amdgpu_get_dpm_state(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	enum amd_pm_state_type pm;

	if (adev->powerplay.pp_funcs->get_current_power_state)
		pm = amdgpu_dpm_get_current_power_state(adev);
	else
		pm = adev->pm.dpm.user_state;

	return snprintf(buf, PAGE_SIZE, "%s\n",
			(pm == POWER_STATE_TYPE_BATTERY) ? "battery" :
			(pm == POWER_STATE_TYPE_BALANCED) ? "balanced" : "performance");
}

static ssize_t amdgpu_set_dpm_state(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	enum amd_pm_state_type  state;

	if (strncmp("battery", buf, strlen("battery")) == 0)
		state = POWER_STATE_TYPE_BATTERY;
	else if (strncmp("balanced", buf, strlen("balanced")) == 0)
		state = POWER_STATE_TYPE_BALANCED;
	else if (strncmp("performance", buf, strlen("performance")) == 0)
		state = POWER_STATE_TYPE_PERFORMANCE;
	else {
		count = -EINVAL;
		goto fail;
	}

	if (adev->powerplay.pp_funcs->dispatch_tasks) {
		amdgpu_dpm_dispatch_task(adev, AMD_PP_TASK_ENABLE_USER_STATE, &state);
	} else {
		mutex_lock(&adev->pm.mutex);
		adev->pm.dpm.user_state = state;
		mutex_unlock(&adev->pm.mutex);

		/* Can't set dpm state when the card is off */
		if (!(adev->flags & AMD_IS_PX) ||
		    (ddev->switch_power_state == DRM_SWITCH_POWER_ON))
			amdgpu_pm_compute_clocks(adev);
	}
fail:
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

static ssize_t amdgpu_get_dpm_forced_performance_level(struct device *dev,
						struct device_attribute *attr,
								char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	enum amd_dpm_forced_level level = 0xff;

	if  ((adev->flags & AMD_IS_PX) &&
	     (ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return snprintf(buf, PAGE_SIZE, "off\n");

	if (adev->powerplay.pp_funcs->get_performance_level)
		level = amdgpu_dpm_get_performance_level(adev);
	else
		level = adev->pm.dpm.forced_level;

	return snprintf(buf, PAGE_SIZE, "%s\n",
			(level == AMD_DPM_FORCED_LEVEL_AUTO) ? "auto" :
			(level == AMD_DPM_FORCED_LEVEL_LOW) ? "low" :
			(level == AMD_DPM_FORCED_LEVEL_HIGH) ? "high" :
			(level == AMD_DPM_FORCED_LEVEL_MANUAL) ? "manual" :
			(level == AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD) ? "profile_standard" :
			(level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK) ? "profile_min_sclk" :
			(level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK) ? "profile_min_mclk" :
			(level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) ? "profile_peak" :
			"unknown");
}

static ssize_t amdgpu_set_dpm_forced_performance_level(struct device *dev,
						       struct device_attribute *attr,
						       const char *buf,
						       size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	enum amd_dpm_forced_level level;
	enum amd_dpm_forced_level current_level = 0xff;
	int ret = 0;

	/* Can't force performance level when the card is off */
	if  ((adev->flags & AMD_IS_PX) &&
	     (ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return -EINVAL;

	if (adev->powerplay.pp_funcs->get_performance_level)
		current_level = amdgpu_dpm_get_performance_level(adev);

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
	}  else {
		count = -EINVAL;
		goto fail;
	}

	if (current_level == level)
		return count;

	if (adev->powerplay.pp_funcs->force_performance_level) {
		mutex_lock(&adev->pm.mutex);
		if (adev->pm.dpm.thermal_active) {
			count = -EINVAL;
			mutex_unlock(&adev->pm.mutex);
			goto fail;
		}
		ret = amdgpu_dpm_force_performance_level(adev, level);
		if (ret)
			count = -EINVAL;
		else
			adev->pm.dpm.forced_level = level;
		mutex_unlock(&adev->pm.mutex);
	}

fail:
	return count;
}

static ssize_t amdgpu_get_pp_num_states(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	struct pp_states_info data;
	int i, buf_len;

	if (adev->powerplay.pp_funcs->get_pp_num_states)
		amdgpu_dpm_get_pp_num_states(adev, &data);

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
	struct amdgpu_device *adev = ddev->dev_private;
	struct pp_states_info data;
	enum amd_pm_state_type pm = 0;
	int i = 0;

	if (adev->powerplay.pp_funcs->get_current_power_state
		 && adev->powerplay.pp_funcs->get_pp_num_states) {
		pm = amdgpu_dpm_get_current_power_state(adev);
		amdgpu_dpm_get_pp_num_states(adev, &data);

		for (i = 0; i < data.nums; i++) {
			if (pm == data.states[i])
				break;
		}

		if (i == data.nums)
			i = -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", i);
}

static ssize_t amdgpu_get_pp_force_state(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;

	if (adev->pp_force_state_enabled)
		return amdgpu_get_pp_cur_state(dev, attr, buf);
	else
		return snprintf(buf, PAGE_SIZE, "\n");
}

static ssize_t amdgpu_set_pp_force_state(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	enum amd_pm_state_type state = 0;
	unsigned long idx;
	int ret;

	if (strlen(buf) == 1)
		adev->pp_force_state_enabled = false;
	else if (adev->powerplay.pp_funcs->dispatch_tasks &&
			adev->powerplay.pp_funcs->get_pp_num_states) {
		struct pp_states_info data;

		ret = kstrtoul(buf, 0, &idx);
		if (ret || idx >= ARRAY_SIZE(data.states)) {
			count = -EINVAL;
			goto fail;
		}
		idx = array_index_nospec(idx, ARRAY_SIZE(data.states));

		amdgpu_dpm_get_pp_num_states(adev, &data);
		state = data.states[idx];
		/* only set user selected power states */
		if (state != POWER_STATE_TYPE_INTERNAL_BOOT &&
		    state != POWER_STATE_TYPE_DEFAULT) {
			amdgpu_dpm_dispatch_task(adev,
					AMD_PP_TASK_ENABLE_USER_STATE, &state);
			adev->pp_force_state_enabled = true;
		}
	}
fail:
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
	struct amdgpu_device *adev = ddev->dev_private;
	char *table = NULL;
	int size;

	if (adev->powerplay.pp_funcs->get_pp_table)
		size = amdgpu_dpm_get_pp_table(adev, &table);
	else
		return 0;

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
	struct amdgpu_device *adev = ddev->dev_private;

	if (adev->powerplay.pp_funcs->set_pp_table)
		amdgpu_dpm_set_pp_table(adev, buf, count);

	return count;
}

/**
 * DOC: pp_od_clk_voltage
 *
 * The amdgpu driver provides a sysfs API for adjusting the clocks and voltages
 * in each power level within a power state.  The pp_od_clk_voltage is used for
 * this.
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
 */

static ssize_t amdgpu_set_pp_od_clk_voltage(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	int ret;
	uint32_t parameter_size = 0;
	long parameter[64];
	char buf_cpy[128];
	char *tmp_str;
	char *sub_str;
	const char delimiter[3] = {' ', '\n', '\0'};
	uint32_t type;

	if (count > 127)
		return -EINVAL;

	if (*buf == 's')
		type = PP_OD_EDIT_SCLK_VDDC_TABLE;
	else if (*buf == 'm')
		type = PP_OD_EDIT_MCLK_VDDC_TABLE;
	else if(*buf == 'r')
		type = PP_OD_RESTORE_DEFAULT_TABLE;
	else if (*buf == 'c')
		type = PP_OD_COMMIT_DPM_TABLE;
	else
		return -EINVAL;

	memcpy(buf_cpy, buf, count+1);

	tmp_str = buf_cpy;

	while (isspace(*++tmp_str));

	while (tmp_str[0]) {
		sub_str = strsep(&tmp_str, delimiter);
		ret = kstrtol(sub_str, 0, &parameter[parameter_size]);
		if (ret)
			return -EINVAL;
		parameter_size++;

		while (isspace(*tmp_str))
			tmp_str++;
	}

	if (adev->powerplay.pp_funcs->odn_edit_dpm_table)
		ret = amdgpu_dpm_odn_edit_dpm_table(adev, type,
						parameter, parameter_size);

	if (ret)
		return -EINVAL;

	if (type == PP_OD_COMMIT_DPM_TABLE) {
		if (adev->powerplay.pp_funcs->dispatch_tasks) {
			amdgpu_dpm_dispatch_task(adev, AMD_PP_TASK_READJUST_POWER_STATE, NULL);
			return count;
		} else {
			return -EINVAL;
		}
	}

	return count;
}

static ssize_t amdgpu_get_pp_od_clk_voltage(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	uint32_t size = 0;

	if (adev->powerplay.pp_funcs->print_clock_levels) {
		size = amdgpu_dpm_print_clock_levels(adev, OD_SCLK, buf);
		size += amdgpu_dpm_print_clock_levels(adev, OD_MCLK, buf+size);
		size += amdgpu_dpm_print_clock_levels(adev, OD_RANGE, buf+size);
		return size;
	} else {
		return snprintf(buf, PAGE_SIZE, "\n");
	}

}

/**
 * DOC: pp_dpm_sclk pp_dpm_mclk pp_dpm_pcie
 *
 * The amdgpu driver provides a sysfs API for adjusting what power levels
 * are enabled for a given power state.  The files pp_dpm_sclk, pp_dpm_mclk,
 * and pp_dpm_pcie are used for this.
 *
 * Reading back the files will show you the available power levels within
 * the power state and the clock information for those levels.
 *
 * To manually adjust these states, first select manual using
 * power_dpm_force_performance_level.
 * Secondly,Enter a new value for each level by inputing a string that
 * contains " echo xx xx xx > pp_dpm_sclk/mclk/pcie"
 * E.g., echo 4 5 6 to > pp_dpm_sclk will enable sclk levels 4, 5, and 6.
 */

static ssize_t amdgpu_get_pp_dpm_sclk(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;

	if (adev->powerplay.pp_funcs->print_clock_levels)
		return amdgpu_dpm_print_clock_levels(adev, PP_SCLK, buf);
	else
		return snprintf(buf, PAGE_SIZE, "\n");
}

/*
 * Worst case: 32 bits individually specified, in octal at 12 characters
 * per line (+1 for \n).
 */
#define AMDGPU_MASK_BUF_MAX	(32 * 13)

static ssize_t amdgpu_read_mask(const char *buf, size_t count, uint32_t *mask)
{
	int ret;
	long level;
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
	while (tmp[0]) {
		sub_str = strsep(&tmp, delimiter);
		if (strlen(sub_str)) {
			ret = kstrtol(sub_str, 0, &level);
			if (ret)
				return -EINVAL;
			*mask |= 1 << level;
		} else
			break;
	}

	return 0;
}

static ssize_t amdgpu_set_pp_dpm_sclk(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	int ret;
	uint32_t mask = 0;

	ret = amdgpu_read_mask(buf, count, &mask);
	if (ret)
		return ret;

	if (adev->powerplay.pp_funcs->force_clock_level)
		amdgpu_dpm_force_clock_level(adev, PP_SCLK, mask);

	return count;
}

static ssize_t amdgpu_get_pp_dpm_mclk(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;

	if (adev->powerplay.pp_funcs->print_clock_levels)
		return amdgpu_dpm_print_clock_levels(adev, PP_MCLK, buf);
	else
		return snprintf(buf, PAGE_SIZE, "\n");
}

static ssize_t amdgpu_set_pp_dpm_mclk(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	int ret;
	uint32_t mask = 0;

	ret = amdgpu_read_mask(buf, count, &mask);
	if (ret)
		return ret;

	if (adev->powerplay.pp_funcs->force_clock_level)
		amdgpu_dpm_force_clock_level(adev, PP_MCLK, mask);

	return count;
}

static ssize_t amdgpu_get_pp_dpm_pcie(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;

	if (adev->powerplay.pp_funcs->print_clock_levels)
		return amdgpu_dpm_print_clock_levels(adev, PP_PCIE, buf);
	else
		return snprintf(buf, PAGE_SIZE, "\n");
}

static ssize_t amdgpu_set_pp_dpm_pcie(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	int ret;
	uint32_t mask = 0;

	ret = amdgpu_read_mask(buf, count, &mask);
	if (ret)
		return ret;

	if (adev->powerplay.pp_funcs->force_clock_level)
		amdgpu_dpm_force_clock_level(adev, PP_PCIE, mask);

	return count;
}

static ssize_t amdgpu_get_pp_sclk_od(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	uint32_t value = 0;

	if (adev->powerplay.pp_funcs->get_sclk_od)
		value = amdgpu_dpm_get_sclk_od(adev);

	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t amdgpu_set_pp_sclk_od(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	int ret;
	long int value;

	ret = kstrtol(buf, 0, &value);

	if (ret) {
		count = -EINVAL;
		goto fail;
	}
	if (adev->powerplay.pp_funcs->set_sclk_od)
		amdgpu_dpm_set_sclk_od(adev, (uint32_t)value);

	if (adev->powerplay.pp_funcs->dispatch_tasks) {
		amdgpu_dpm_dispatch_task(adev, AMD_PP_TASK_READJUST_POWER_STATE, NULL);
	} else {
		adev->pm.dpm.current_ps = adev->pm.dpm.boot_ps;
		amdgpu_pm_compute_clocks(adev);
	}

fail:
	return count;
}

static ssize_t amdgpu_get_pp_mclk_od(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	uint32_t value = 0;

	if (adev->powerplay.pp_funcs->get_mclk_od)
		value = amdgpu_dpm_get_mclk_od(adev);

	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t amdgpu_set_pp_mclk_od(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	int ret;
	long int value;

	ret = kstrtol(buf, 0, &value);

	if (ret) {
		count = -EINVAL;
		goto fail;
	}
	if (adev->powerplay.pp_funcs->set_mclk_od)
		amdgpu_dpm_set_mclk_od(adev, (uint32_t)value);

	if (adev->powerplay.pp_funcs->dispatch_tasks) {
		amdgpu_dpm_dispatch_task(adev, AMD_PP_TASK_READJUST_POWER_STATE, NULL);
	} else {
		adev->pm.dpm.current_ps = adev->pm.dpm.boot_ps;
		amdgpu_pm_compute_clocks(adev);
	}

fail:
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
	struct amdgpu_device *adev = ddev->dev_private;

	if (adev->powerplay.pp_funcs->get_power_profile_mode)
		return amdgpu_dpm_get_power_profile_mode(adev, buf);

	return snprintf(buf, PAGE_SIZE, "\n");
}


static ssize_t amdgpu_set_pp_power_profile_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	int ret = 0xff;
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	uint32_t parameter_size = 0;
	long parameter[64];
	char *sub_str, buf_cpy[128];
	char *tmp_str;
	uint32_t i = 0;
	char tmp[2];
	long int profile_mode = 0;
	const char delimiter[3] = {' ', '\n', '\0'};

	tmp[0] = *(buf);
	tmp[1] = '\0';
	ret = kstrtol(tmp, 0, &profile_mode);
	if (ret)
		goto fail;

	if (profile_mode == PP_SMC_POWER_PROFILE_CUSTOM) {
		if (count < 2 || count > 127)
			return -EINVAL;
		while (isspace(*++buf))
			i++;
		memcpy(buf_cpy, buf, count-i);
		tmp_str = buf_cpy;
		while (tmp_str[0]) {
			sub_str = strsep(&tmp_str, delimiter);
			ret = kstrtol(sub_str, 0, &parameter[parameter_size]);
			if (ret) {
				count = -EINVAL;
				goto fail;
			}
			parameter_size++;
			while (isspace(*tmp_str))
				tmp_str++;
		}
	}
	parameter[parameter_size] = profile_mode;
	if (adev->powerplay.pp_funcs->set_power_profile_mode)
		ret = amdgpu_dpm_set_power_profile_mode(adev, parameter, parameter_size);

	if (!ret)
		return count;
fail:
	return -EINVAL;
}

/**
 * DOC: busy_percent
 *
 * The amdgpu driver provides a sysfs API for reading how busy the GPU
 * is as a percentage.  The file gpu_busy_percent is used for this.
 * The SMU firmware computes a percentage of load based on the
 * aggregate activity level in the IP cores.
 */
static ssize_t amdgpu_get_busy_percent(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	int r, value, size = sizeof(value);

	/* sanity check PP is enabled */
	if (!(adev->powerplay.pp_funcs &&
	      adev->powerplay.pp_funcs->read_sensor))
		return -EINVAL;

	/* read the IP busy sensor */
	r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GPU_LOAD,
				   (void *)&value, &size);
	if (r)
		return r;

	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}

static DEVICE_ATTR(power_dpm_state, S_IRUGO | S_IWUSR, amdgpu_get_dpm_state, amdgpu_set_dpm_state);
static DEVICE_ATTR(power_dpm_force_performance_level, S_IRUGO | S_IWUSR,
		   amdgpu_get_dpm_forced_performance_level,
		   amdgpu_set_dpm_forced_performance_level);
static DEVICE_ATTR(pp_num_states, S_IRUGO, amdgpu_get_pp_num_states, NULL);
static DEVICE_ATTR(pp_cur_state, S_IRUGO, amdgpu_get_pp_cur_state, NULL);
static DEVICE_ATTR(pp_force_state, S_IRUGO | S_IWUSR,
		amdgpu_get_pp_force_state,
		amdgpu_set_pp_force_state);
static DEVICE_ATTR(pp_table, S_IRUGO | S_IWUSR,
		amdgpu_get_pp_table,
		amdgpu_set_pp_table);
static DEVICE_ATTR(pp_dpm_sclk, S_IRUGO | S_IWUSR,
		amdgpu_get_pp_dpm_sclk,
		amdgpu_set_pp_dpm_sclk);
static DEVICE_ATTR(pp_dpm_mclk, S_IRUGO | S_IWUSR,
		amdgpu_get_pp_dpm_mclk,
		amdgpu_set_pp_dpm_mclk);
static DEVICE_ATTR(pp_dpm_pcie, S_IRUGO | S_IWUSR,
		amdgpu_get_pp_dpm_pcie,
		amdgpu_set_pp_dpm_pcie);
static DEVICE_ATTR(pp_sclk_od, S_IRUGO | S_IWUSR,
		amdgpu_get_pp_sclk_od,
		amdgpu_set_pp_sclk_od);
static DEVICE_ATTR(pp_mclk_od, S_IRUGO | S_IWUSR,
		amdgpu_get_pp_mclk_od,
		amdgpu_set_pp_mclk_od);
static DEVICE_ATTR(pp_power_profile_mode, S_IRUGO | S_IWUSR,
		amdgpu_get_pp_power_profile_mode,
		amdgpu_set_pp_power_profile_mode);
static DEVICE_ATTR(pp_od_clk_voltage, S_IRUGO | S_IWUSR,
		amdgpu_get_pp_od_clk_voltage,
		amdgpu_set_pp_od_clk_voltage);
static DEVICE_ATTR(gpu_busy_percent, S_IRUGO,
		amdgpu_get_busy_percent, NULL);

static ssize_t amdgpu_hwmon_show_temp(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	struct drm_device *ddev = adev->ddev;
	int r, temp, size = sizeof(temp);

	/* Can't get temperature when the card is off */
	if  ((adev->flags & AMD_IS_PX) &&
	     (ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return -EINVAL;

	/* sanity check PP is enabled */
	if (!(adev->powerplay.pp_funcs &&
	      adev->powerplay.pp_funcs->read_sensor))
		return -EINVAL;

	/* get the temperature */
	r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GPU_TEMP,
				   (void *)&temp, &size);
	if (r)
		return r;

	return snprintf(buf, PAGE_SIZE, "%d\n", temp);
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

	return snprintf(buf, PAGE_SIZE, "%d\n", temp);
}

static ssize_t amdgpu_hwmon_get_pwm1_enable(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	u32 pwm_mode = 0;

	if (!adev->powerplay.pp_funcs->get_fan_control_mode)
		return -EINVAL;

	pwm_mode = amdgpu_dpm_get_fan_control_mode(adev);

	return sprintf(buf, "%i\n", pwm_mode);
}

static ssize_t amdgpu_hwmon_set_pwm1_enable(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t count)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int err;
	int value;

	/* Can't adjust fan when the card is off */
	if  ((adev->flags & AMD_IS_PX) &&
	     (adev->ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return -EINVAL;

	if (!adev->powerplay.pp_funcs->set_fan_control_mode)
		return -EINVAL;

	err = kstrtoint(buf, 10, &value);
	if (err)
		return err;

	amdgpu_dpm_set_fan_control_mode(adev, value);

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

	/* Can't adjust fan when the card is off */
	if  ((adev->flags & AMD_IS_PX) &&
	     (adev->ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return -EINVAL;

	err = kstrtou32(buf, 10, &value);
	if (err)
		return err;

	value = (value * 100) / 255;

	if (adev->powerplay.pp_funcs->set_fan_speed_percent) {
		err = amdgpu_dpm_set_fan_speed_percent(adev, value);
		if (err)
			return err;
	}

	return count;
}

static ssize_t amdgpu_hwmon_get_pwm1(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int err;
	u32 speed = 0;

	/* Can't adjust fan when the card is off */
	if  ((adev->flags & AMD_IS_PX) &&
	     (adev->ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return -EINVAL;

	if (adev->powerplay.pp_funcs->get_fan_speed_percent) {
		err = amdgpu_dpm_get_fan_speed_percent(adev, &speed);
		if (err)
			return err;
	}

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

	/* Can't adjust fan when the card is off */
	if  ((adev->flags & AMD_IS_PX) &&
	     (adev->ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return -EINVAL;

	if (adev->powerplay.pp_funcs->get_fan_speed_rpm) {
		err = amdgpu_dpm_get_fan_speed_rpm(adev, &speed);
		if (err)
			return err;
	}

	return sprintf(buf, "%i\n", speed);
}

static ssize_t amdgpu_hwmon_show_vddgfx(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	struct drm_device *ddev = adev->ddev;
	u32 vddgfx;
	int r, size = sizeof(vddgfx);

	/* Can't get voltage when the card is off */
	if  ((adev->flags & AMD_IS_PX) &&
	     (ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return -EINVAL;

	/* sanity check PP is enabled */
	if (!(adev->powerplay.pp_funcs &&
	      adev->powerplay.pp_funcs->read_sensor))
	      return -EINVAL;

	/* get the voltage */
	r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_VDDGFX,
				   (void *)&vddgfx, &size);
	if (r)
		return r;

	return snprintf(buf, PAGE_SIZE, "%d\n", vddgfx);
}

static ssize_t amdgpu_hwmon_show_vddgfx_label(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	return snprintf(buf, PAGE_SIZE, "vddgfx\n");
}

static ssize_t amdgpu_hwmon_show_vddnb(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	struct drm_device *ddev = adev->ddev;
	u32 vddnb;
	int r, size = sizeof(vddnb);

	/* only APUs have vddnb */
	if  (!(adev->flags & AMD_IS_APU))
		return -EINVAL;

	/* Can't get voltage when the card is off */
	if  ((adev->flags & AMD_IS_PX) &&
	     (ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return -EINVAL;

	/* sanity check PP is enabled */
	if (!(adev->powerplay.pp_funcs &&
	      adev->powerplay.pp_funcs->read_sensor))
	      return -EINVAL;

	/* get the voltage */
	r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_VDDNB,
				   (void *)&vddnb, &size);
	if (r)
		return r;

	return snprintf(buf, PAGE_SIZE, "%d\n", vddnb);
}

static ssize_t amdgpu_hwmon_show_vddnb_label(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	return snprintf(buf, PAGE_SIZE, "vddnb\n");
}

static ssize_t amdgpu_hwmon_show_power_avg(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	struct drm_device *ddev = adev->ddev;
	u32 query = 0;
	int r, size = sizeof(u32);
	unsigned uw;

	/* Can't get power when the card is off */
	if  ((adev->flags & AMD_IS_PX) &&
	     (ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return -EINVAL;

	/* sanity check PP is enabled */
	if (!(adev->powerplay.pp_funcs &&
	      adev->powerplay.pp_funcs->read_sensor))
	      return -EINVAL;

	/* get the voltage */
	r = amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GPU_POWER,
				   (void *)&query, &size);
	if (r)
		return r;

	/* convert to microwatts */
	uw = (query >> 8) * 1000000 + (query & 0xff) * 1000;

	return snprintf(buf, PAGE_SIZE, "%u\n", uw);
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
	uint32_t limit = 0;

	if (adev->powerplay.pp_funcs && adev->powerplay.pp_funcs->get_power_limit) {
		adev->powerplay.pp_funcs->get_power_limit(adev->powerplay.pp_handle, &limit, true);
		return snprintf(buf, PAGE_SIZE, "%u\n", limit * 1000000);
	} else {
		return snprintf(buf, PAGE_SIZE, "\n");
	}
}

static ssize_t amdgpu_hwmon_show_power_cap(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	uint32_t limit = 0;

	if (adev->powerplay.pp_funcs && adev->powerplay.pp_funcs->get_power_limit) {
		adev->powerplay.pp_funcs->get_power_limit(adev->powerplay.pp_handle, &limit, false);
		return snprintf(buf, PAGE_SIZE, "%u\n", limit * 1000000);
	} else {
		return snprintf(buf, PAGE_SIZE, "\n");
	}
}


static ssize_t amdgpu_hwmon_set_power_cap(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int err;
	u32 value;

	err = kstrtou32(buf, 10, &value);
	if (err)
		return err;

	value = value / 1000000; /* convert to Watt */
	if (adev->powerplay.pp_funcs && adev->powerplay.pp_funcs->set_power_limit) {
		err = adev->powerplay.pp_funcs->set_power_limit(adev->powerplay.pp_handle, value);
		if (err)
			return err;
	} else {
		return -EINVAL;
	}

	return count;
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
 * hwmon interfaces for GPU temperature:
 *
 * - temp1_input: the on die GPU temperature in millidegrees Celsius
 *
 * - temp1_crit: temperature critical max value in millidegrees Celsius
 *
 * - temp1_crit_hyst: temperature hysteresis for critical limit in millidegrees Celsius
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
 * - fan1_input: fan speed in RPM
 *
 * You can use hwmon tools like sensors to view this information on your system.
 *
 */

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, amdgpu_hwmon_show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, amdgpu_hwmon_show_temp_thresh, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IRUGO, amdgpu_hwmon_show_temp_thresh, NULL, 1);
static SENSOR_DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR, amdgpu_hwmon_get_pwm1, amdgpu_hwmon_set_pwm1, 0);
static SENSOR_DEVICE_ATTR(pwm1_enable, S_IRUGO | S_IWUSR, amdgpu_hwmon_get_pwm1_enable, amdgpu_hwmon_set_pwm1_enable, 0);
static SENSOR_DEVICE_ATTR(pwm1_min, S_IRUGO, amdgpu_hwmon_get_pwm1_min, NULL, 0);
static SENSOR_DEVICE_ATTR(pwm1_max, S_IRUGO, amdgpu_hwmon_get_pwm1_max, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, amdgpu_hwmon_get_fan1_input, NULL, 0);
static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, amdgpu_hwmon_show_vddgfx, NULL, 0);
static SENSOR_DEVICE_ATTR(in0_label, S_IRUGO, amdgpu_hwmon_show_vddgfx_label, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, amdgpu_hwmon_show_vddnb, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_label, S_IRUGO, amdgpu_hwmon_show_vddnb_label, NULL, 0);
static SENSOR_DEVICE_ATTR(power1_average, S_IRUGO, amdgpu_hwmon_show_power_avg, NULL, 0);
static SENSOR_DEVICE_ATTR(power1_cap_max, S_IRUGO, amdgpu_hwmon_show_power_cap_max, NULL, 0);
static SENSOR_DEVICE_ATTR(power1_cap_min, S_IRUGO, amdgpu_hwmon_show_power_cap_min, NULL, 0);
static SENSOR_DEVICE_ATTR(power1_cap, S_IRUGO | S_IWUSR, amdgpu_hwmon_show_power_cap, amdgpu_hwmon_set_power_cap, 0);

static struct attribute *hwmon_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1_min.dev_attr.attr,
	&sensor_dev_attr_pwm1_max.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_label.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_label.dev_attr.attr,
	&sensor_dev_attr_power1_average.dev_attr.attr,
	&sensor_dev_attr_power1_cap_max.dev_attr.attr,
	&sensor_dev_attr_power1_cap_min.dev_attr.attr,
	&sensor_dev_attr_power1_cap.dev_attr.attr,
	NULL
};

static umode_t hwmon_attributes_visible(struct kobject *kobj,
					struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	umode_t effective_mode = attr->mode;


	/* Skip fan attributes if fan is not present */
	if (adev->pm.no_fan && (attr == &sensor_dev_attr_pwm1.dev_attr.attr ||
	    attr == &sensor_dev_attr_pwm1_enable.dev_attr.attr ||
	    attr == &sensor_dev_attr_pwm1_max.dev_attr.attr ||
	    attr == &sensor_dev_attr_pwm1_min.dev_attr.attr ||
	    attr == &sensor_dev_attr_fan1_input.dev_attr.attr))
		return 0;

	/* Skip limit attributes if DPM is not enabled */
	if (!adev->pm.dpm_enabled &&
	    (attr == &sensor_dev_attr_temp1_crit.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp1_crit_hyst.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_enable.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_min.dev_attr.attr))
		return 0;

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

	if ((adev->flags & AMD_IS_APU) &&
	    (attr == &sensor_dev_attr_power1_average.dev_attr.attr ||
	     attr == &sensor_dev_attr_power1_cap_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_power1_cap_min.dev_attr.attr||
	     attr == &sensor_dev_attr_power1_cap.dev_attr.attr))
		return 0;

	/* hide max/min values if we can't both query and manage the fan */
	if ((!adev->powerplay.pp_funcs->set_fan_speed_percent &&
	     !adev->powerplay.pp_funcs->get_fan_speed_percent) &&
	    (attr == &sensor_dev_attr_pwm1_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_min.dev_attr.attr))
		return 0;

	/* only APUs have vddnb */
	if (!(adev->flags & AMD_IS_APU) &&
	    (attr == &sensor_dev_attr_in1_input.dev_attr.attr ||
	     attr == &sensor_dev_attr_in1_label.dev_attr.attr))
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

void amdgpu_dpm_thermal_work_handler(struct work_struct *work)
{
	struct amdgpu_device *adev =
		container_of(work, struct amdgpu_device,
			     pm.dpm.thermal.work);
	/* switch to the thermal state */
	enum amd_pm_state_type dpm_state = POWER_STATE_TYPE_INTERNAL_THERMAL;
	int temp, size = sizeof(temp);

	if (!adev->pm.dpm_enabled)
		return;

	if (adev->powerplay.pp_funcs &&
	    adev->powerplay.pp_funcs->read_sensor &&
	    !amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GPU_TEMP,
				    (void *)&temp, &size)) {
		if (temp < adev->pm.dpm.thermal.min_temp)
			/* switch back the user state */
			dpm_state = adev->pm.dpm.user_state;
	} else {
		if (adev->pm.dpm.thermal.high_to_low)
			/* switch back the user state */
			dpm_state = adev->pm.dpm.user_state;
	}
	mutex_lock(&adev->pm.mutex);
	if (dpm_state == POWER_STATE_TYPE_INTERNAL_THERMAL)
		adev->pm.dpm.thermal_active = true;
	else
		adev->pm.dpm.thermal_active = false;
	adev->pm.dpm.state = dpm_state;
	mutex_unlock(&adev->pm.mutex);

	amdgpu_pm_compute_clocks(adev);
}

static struct amdgpu_ps *amdgpu_dpm_pick_power_state(struct amdgpu_device *adev,
						     enum amd_pm_state_type dpm_state)
{
	int i;
	struct amdgpu_ps *ps;
	u32 ui_class;
	bool single_display = (adev->pm.dpm.new_active_crtc_count < 2) ?
		true : false;

	/* check if the vblank period is too short to adjust the mclk */
	if (single_display && adev->powerplay.pp_funcs->vblank_too_short) {
		if (amdgpu_dpm_vblank_too_short(adev))
			single_display = false;
	}

	/* certain older asics have a separare 3D performance state,
	 * so try that first if the user selected performance
	 */
	if (dpm_state == POWER_STATE_TYPE_PERFORMANCE)
		dpm_state = POWER_STATE_TYPE_INTERNAL_3DPERF;
	/* balanced states don't exist at the moment */
	if (dpm_state == POWER_STATE_TYPE_BALANCED)
		dpm_state = POWER_STATE_TYPE_PERFORMANCE;

restart_search:
	/* Pick the best power state based on current conditions */
	for (i = 0; i < adev->pm.dpm.num_ps; i++) {
		ps = &adev->pm.dpm.ps[i];
		ui_class = ps->class & ATOM_PPLIB_CLASSIFICATION_UI_MASK;
		switch (dpm_state) {
		/* user states */
		case POWER_STATE_TYPE_BATTERY:
			if (ui_class == ATOM_PPLIB_CLASSIFICATION_UI_BATTERY) {
				if (ps->caps & ATOM_PPLIB_SINGLE_DISPLAY_ONLY) {
					if (single_display)
						return ps;
				} else
					return ps;
			}
			break;
		case POWER_STATE_TYPE_BALANCED:
			if (ui_class == ATOM_PPLIB_CLASSIFICATION_UI_BALANCED) {
				if (ps->caps & ATOM_PPLIB_SINGLE_DISPLAY_ONLY) {
					if (single_display)
						return ps;
				} else
					return ps;
			}
			break;
		case POWER_STATE_TYPE_PERFORMANCE:
			if (ui_class == ATOM_PPLIB_CLASSIFICATION_UI_PERFORMANCE) {
				if (ps->caps & ATOM_PPLIB_SINGLE_DISPLAY_ONLY) {
					if (single_display)
						return ps;
				} else
					return ps;
			}
			break;
		/* internal states */
		case POWER_STATE_TYPE_INTERNAL_UVD:
			if (adev->pm.dpm.uvd_ps)
				return adev->pm.dpm.uvd_ps;
			else
				break;
		case POWER_STATE_TYPE_INTERNAL_UVD_SD:
			if (ps->class & ATOM_PPLIB_CLASSIFICATION_SDSTATE)
				return ps;
			break;
		case POWER_STATE_TYPE_INTERNAL_UVD_HD:
			if (ps->class & ATOM_PPLIB_CLASSIFICATION_HDSTATE)
				return ps;
			break;
		case POWER_STATE_TYPE_INTERNAL_UVD_HD2:
			if (ps->class & ATOM_PPLIB_CLASSIFICATION_HD2STATE)
				return ps;
			break;
		case POWER_STATE_TYPE_INTERNAL_UVD_MVC:
			if (ps->class2 & ATOM_PPLIB_CLASSIFICATION2_MVC)
				return ps;
			break;
		case POWER_STATE_TYPE_INTERNAL_BOOT:
			return adev->pm.dpm.boot_ps;
		case POWER_STATE_TYPE_INTERNAL_THERMAL:
			if (ps->class & ATOM_PPLIB_CLASSIFICATION_THERMAL)
				return ps;
			break;
		case POWER_STATE_TYPE_INTERNAL_ACPI:
			if (ps->class & ATOM_PPLIB_CLASSIFICATION_ACPI)
				return ps;
			break;
		case POWER_STATE_TYPE_INTERNAL_ULV:
			if (ps->class2 & ATOM_PPLIB_CLASSIFICATION2_ULV)
				return ps;
			break;
		case POWER_STATE_TYPE_INTERNAL_3DPERF:
			if (ps->class & ATOM_PPLIB_CLASSIFICATION_3DPERFORMANCE)
				return ps;
			break;
		default:
			break;
		}
	}
	/* use a fallback state if we didn't match */
	switch (dpm_state) {
	case POWER_STATE_TYPE_INTERNAL_UVD_SD:
		dpm_state = POWER_STATE_TYPE_INTERNAL_UVD_HD;
		goto restart_search;
	case POWER_STATE_TYPE_INTERNAL_UVD_HD:
	case POWER_STATE_TYPE_INTERNAL_UVD_HD2:
	case POWER_STATE_TYPE_INTERNAL_UVD_MVC:
		if (adev->pm.dpm.uvd_ps) {
			return adev->pm.dpm.uvd_ps;
		} else {
			dpm_state = POWER_STATE_TYPE_PERFORMANCE;
			goto restart_search;
		}
	case POWER_STATE_TYPE_INTERNAL_THERMAL:
		dpm_state = POWER_STATE_TYPE_INTERNAL_ACPI;
		goto restart_search;
	case POWER_STATE_TYPE_INTERNAL_ACPI:
		dpm_state = POWER_STATE_TYPE_BATTERY;
		goto restart_search;
	case POWER_STATE_TYPE_BATTERY:
	case POWER_STATE_TYPE_BALANCED:
	case POWER_STATE_TYPE_INTERNAL_3DPERF:
		dpm_state = POWER_STATE_TYPE_PERFORMANCE;
		goto restart_search;
	default:
		break;
	}

	return NULL;
}

static void amdgpu_dpm_change_power_state_locked(struct amdgpu_device *adev)
{
	struct amdgpu_ps *ps;
	enum amd_pm_state_type dpm_state;
	int ret;
	bool equal = false;

	/* if dpm init failed */
	if (!adev->pm.dpm_enabled)
		return;

	if (adev->pm.dpm.user_state != adev->pm.dpm.state) {
		/* add other state override checks here */
		if ((!adev->pm.dpm.thermal_active) &&
		    (!adev->pm.dpm.uvd_active))
			adev->pm.dpm.state = adev->pm.dpm.user_state;
	}
	dpm_state = adev->pm.dpm.state;

	ps = amdgpu_dpm_pick_power_state(adev, dpm_state);
	if (ps)
		adev->pm.dpm.requested_ps = ps;
	else
		return;

	if (amdgpu_dpm == 1 && adev->powerplay.pp_funcs->print_power_state) {
		printk("switching from power state:\n");
		amdgpu_dpm_print_power_state(adev, adev->pm.dpm.current_ps);
		printk("switching to power state:\n");
		amdgpu_dpm_print_power_state(adev, adev->pm.dpm.requested_ps);
	}

	/* update whether vce is active */
	ps->vce_active = adev->pm.dpm.vce_active;
	if (adev->powerplay.pp_funcs->display_configuration_changed)
		amdgpu_dpm_display_configuration_changed(adev);

	ret = amdgpu_dpm_pre_set_power_state(adev);
	if (ret)
		return;

	if (adev->powerplay.pp_funcs->check_state_equal) {
		if (0 != amdgpu_dpm_check_state_equal(adev, adev->pm.dpm.current_ps, adev->pm.dpm.requested_ps, &equal))
			equal = false;
	}

	if (equal)
		return;

	amdgpu_dpm_set_power_state(adev);
	amdgpu_dpm_post_set_power_state(adev);

	adev->pm.dpm.current_active_crtcs = adev->pm.dpm.new_active_crtcs;
	adev->pm.dpm.current_active_crtc_count = adev->pm.dpm.new_active_crtc_count;

	if (adev->powerplay.pp_funcs->force_performance_level) {
		if (adev->pm.dpm.thermal_active) {
			enum amd_dpm_forced_level level = adev->pm.dpm.forced_level;
			/* force low perf level for thermal */
			amdgpu_dpm_force_performance_level(adev, AMD_DPM_FORCED_LEVEL_LOW);
			/* save the user's level */
			adev->pm.dpm.forced_level = level;
		} else {
			/* otherwise, user selected level */
			amdgpu_dpm_force_performance_level(adev, adev->pm.dpm.forced_level);
		}
	}
}

void amdgpu_dpm_enable_uvd(struct amdgpu_device *adev, bool enable)
{
	if (adev->powerplay.pp_funcs->set_powergating_by_smu) {
		/* enable/disable UVD */
		mutex_lock(&adev->pm.mutex);
		amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_UVD, !enable);
		mutex_unlock(&adev->pm.mutex);
	} else {
		if (enable) {
			mutex_lock(&adev->pm.mutex);
			adev->pm.dpm.uvd_active = true;
			adev->pm.dpm.state = POWER_STATE_TYPE_INTERNAL_UVD;
			mutex_unlock(&adev->pm.mutex);
		} else {
			mutex_lock(&adev->pm.mutex);
			adev->pm.dpm.uvd_active = false;
			mutex_unlock(&adev->pm.mutex);
		}
		amdgpu_pm_compute_clocks(adev);
	}
}

void amdgpu_dpm_enable_vce(struct amdgpu_device *adev, bool enable)
{
	if (adev->powerplay.pp_funcs->set_powergating_by_smu) {
		/* enable/disable VCE */
		mutex_lock(&adev->pm.mutex);
		amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_VCE, !enable);
		mutex_unlock(&adev->pm.mutex);
	} else {
		if (enable) {
			mutex_lock(&adev->pm.mutex);
			adev->pm.dpm.vce_active = true;
			/* XXX select vce level based on ring/task */
			adev->pm.dpm.vce_level = AMD_VCE_LEVEL_AC_ALL;
			mutex_unlock(&adev->pm.mutex);
			amdgpu_device_ip_set_clockgating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
							       AMD_CG_STATE_UNGATE);
			amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
							       AMD_PG_STATE_UNGATE);
			amdgpu_pm_compute_clocks(adev);
		} else {
			amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
							       AMD_PG_STATE_GATE);
			amdgpu_device_ip_set_clockgating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
							       AMD_CG_STATE_GATE);
			mutex_lock(&adev->pm.mutex);
			adev->pm.dpm.vce_active = false;
			mutex_unlock(&adev->pm.mutex);
			amdgpu_pm_compute_clocks(adev);
		}

	}
}

void amdgpu_pm_print_power_states(struct amdgpu_device *adev)
{
	int i;

	if (adev->powerplay.pp_funcs->print_power_state == NULL)
		return;

	for (i = 0; i < adev->pm.dpm.num_ps; i++)
		amdgpu_dpm_print_power_state(adev, &adev->pm.dpm.ps[i]);

}

int amdgpu_pm_sysfs_init(struct amdgpu_device *adev)
{
	int ret;

	if (adev->pm.sysfs_initialized)
		return 0;

	if (adev->pm.dpm_enabled == 0)
		return 0;

	adev->pm.int_hwmon_dev = hwmon_device_register_with_groups(adev->dev,
								   DRIVER_NAME, adev,
								   hwmon_groups);
	if (IS_ERR(adev->pm.int_hwmon_dev)) {
		ret = PTR_ERR(adev->pm.int_hwmon_dev);
		dev_err(adev->dev,
			"Unable to register hwmon device: %d\n", ret);
		return ret;
	}

	ret = device_create_file(adev->dev, &dev_attr_power_dpm_state);
	if (ret) {
		DRM_ERROR("failed to create device file for dpm state\n");
		return ret;
	}
	ret = device_create_file(adev->dev, &dev_attr_power_dpm_force_performance_level);
	if (ret) {
		DRM_ERROR("failed to create device file for dpm state\n");
		return ret;
	}


	ret = device_create_file(adev->dev, &dev_attr_pp_num_states);
	if (ret) {
		DRM_ERROR("failed to create device file pp_num_states\n");
		return ret;
	}
	ret = device_create_file(adev->dev, &dev_attr_pp_cur_state);
	if (ret) {
		DRM_ERROR("failed to create device file pp_cur_state\n");
		return ret;
	}
	ret = device_create_file(adev->dev, &dev_attr_pp_force_state);
	if (ret) {
		DRM_ERROR("failed to create device file pp_force_state\n");
		return ret;
	}
	ret = device_create_file(adev->dev, &dev_attr_pp_table);
	if (ret) {
		DRM_ERROR("failed to create device file pp_table\n");
		return ret;
	}

	ret = device_create_file(adev->dev, &dev_attr_pp_dpm_sclk);
	if (ret) {
		DRM_ERROR("failed to create device file pp_dpm_sclk\n");
		return ret;
	}
	ret = device_create_file(adev->dev, &dev_attr_pp_dpm_mclk);
	if (ret) {
		DRM_ERROR("failed to create device file pp_dpm_mclk\n");
		return ret;
	}
	ret = device_create_file(adev->dev, &dev_attr_pp_dpm_pcie);
	if (ret) {
		DRM_ERROR("failed to create device file pp_dpm_pcie\n");
		return ret;
	}
	ret = device_create_file(adev->dev, &dev_attr_pp_sclk_od);
	if (ret) {
		DRM_ERROR("failed to create device file pp_sclk_od\n");
		return ret;
	}
	ret = device_create_file(adev->dev, &dev_attr_pp_mclk_od);
	if (ret) {
		DRM_ERROR("failed to create device file pp_mclk_od\n");
		return ret;
	}
	ret = device_create_file(adev->dev,
			&dev_attr_pp_power_profile_mode);
	if (ret) {
		DRM_ERROR("failed to create device file	"
				"pp_power_profile_mode\n");
		return ret;
	}
	ret = device_create_file(adev->dev,
			&dev_attr_pp_od_clk_voltage);
	if (ret) {
		DRM_ERROR("failed to create device file	"
				"pp_od_clk_voltage\n");
		return ret;
	}
	ret = device_create_file(adev->dev,
			&dev_attr_gpu_busy_percent);
	if (ret) {
		DRM_ERROR("failed to create device file	"
				"gpu_busy_level\n");
		return ret;
	}
	ret = amdgpu_debugfs_pm_init(adev);
	if (ret) {
		DRM_ERROR("Failed to register debugfs file for dpm!\n");
		return ret;
	}

	adev->pm.sysfs_initialized = true;

	return 0;
}

void amdgpu_pm_sysfs_fini(struct amdgpu_device *adev)
{
	if (adev->pm.dpm_enabled == 0)
		return;

	if (adev->pm.int_hwmon_dev)
		hwmon_device_unregister(adev->pm.int_hwmon_dev);
	device_remove_file(adev->dev, &dev_attr_power_dpm_state);
	device_remove_file(adev->dev, &dev_attr_power_dpm_force_performance_level);

	device_remove_file(adev->dev, &dev_attr_pp_num_states);
	device_remove_file(adev->dev, &dev_attr_pp_cur_state);
	device_remove_file(adev->dev, &dev_attr_pp_force_state);
	device_remove_file(adev->dev, &dev_attr_pp_table);

	device_remove_file(adev->dev, &dev_attr_pp_dpm_sclk);
	device_remove_file(adev->dev, &dev_attr_pp_dpm_mclk);
	device_remove_file(adev->dev, &dev_attr_pp_dpm_pcie);
	device_remove_file(adev->dev, &dev_attr_pp_sclk_od);
	device_remove_file(adev->dev, &dev_attr_pp_mclk_od);
	device_remove_file(adev->dev,
			&dev_attr_pp_power_profile_mode);
	device_remove_file(adev->dev,
			&dev_attr_pp_od_clk_voltage);
	device_remove_file(adev->dev, &dev_attr_gpu_busy_percent);
}

void amdgpu_pm_compute_clocks(struct amdgpu_device *adev)
{
	int i = 0;

	if (!adev->pm.dpm_enabled)
		return;

	if (adev->mode_info.num_crtc)
		amdgpu_display_bandwidth_update(adev);

	for (i = 0; i < AMDGPU_MAX_RINGS; i++) {
		struct amdgpu_ring *ring = adev->rings[i];
		if (ring && ring->ready)
			amdgpu_fence_wait_empty(ring);
	}

	if (adev->powerplay.pp_funcs->dispatch_tasks) {
		if (!amdgpu_device_has_dc_support(adev)) {
			mutex_lock(&adev->pm.mutex);
			amdgpu_dpm_get_active_displays(adev);
			adev->pm.pm_display_cfg.num_display = adev->pm.dpm.new_active_crtc_count;
			adev->pm.pm_display_cfg.vrefresh = amdgpu_dpm_get_vrefresh(adev);
			adev->pm.pm_display_cfg.min_vblank_time = amdgpu_dpm_get_vblank_time(adev);
			/* we have issues with mclk switching with refresh rates over 120 hz on the non-DC code. */
			if (adev->pm.pm_display_cfg.vrefresh > 120)
				adev->pm.pm_display_cfg.min_vblank_time = 0;
			if (adev->powerplay.pp_funcs->display_configuration_change)
				adev->powerplay.pp_funcs->display_configuration_change(
								adev->powerplay.pp_handle,
								&adev->pm.pm_display_cfg);
			mutex_unlock(&adev->pm.mutex);
		}
		amdgpu_dpm_dispatch_task(adev, AMD_PP_TASK_DISPLAY_CONFIG_CHANGE, NULL);
	} else {
		mutex_lock(&adev->pm.mutex);
		amdgpu_dpm_get_active_displays(adev);
		amdgpu_dpm_change_power_state_locked(adev);
		mutex_unlock(&adev->pm.mutex);
	}
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)

static int amdgpu_debugfs_pm_info_pp(struct seq_file *m, struct amdgpu_device *adev)
{
	uint32_t value;
	uint32_t query = 0;
	int size;

	/* sanity check PP is enabled */
	if (!(adev->powerplay.pp_funcs &&
	      adev->powerplay.pp_funcs->read_sensor))
	      return -EINVAL;

	/* GPU Clocks */
	size = sizeof(value);
	seq_printf(m, "GFX Clocks and Power:\n");
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
	seq_printf(m, "\n");

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

	return 0;
}

static void amdgpu_parse_cg_state(struct seq_file *m, u32 flags)
{
	int i;

	for (i = 0; clocks[i].flag; i++)
		seq_printf(m, "\t%s: %s\n", clocks[i].name,
			   (flags & clocks[i].flag) ? "On" : "Off");
}

static int amdgpu_debugfs_pm_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct drm_device *ddev = adev->ddev;
	u32 flags = 0;

	amdgpu_device_ip_get_clockgating_state(adev, &flags);
	seq_printf(m, "Clock Gating Flags Mask: 0x%x\n", flags);
	amdgpu_parse_cg_state(m, flags);
	seq_printf(m, "\n");

	if (!adev->pm.dpm_enabled) {
		seq_printf(m, "dpm not enabled\n");
		return 0;
	}
	if  ((adev->flags & AMD_IS_PX) &&
	     (ddev->switch_power_state != DRM_SWITCH_POWER_ON)) {
		seq_printf(m, "PX asic powered off\n");
	} else if (adev->powerplay.pp_funcs->debugfs_print_current_performance_level) {
		mutex_lock(&adev->pm.mutex);
		if (adev->powerplay.pp_funcs->debugfs_print_current_performance_level)
			adev->powerplay.pp_funcs->debugfs_print_current_performance_level(adev, m);
		else
			seq_printf(m, "Debugfs support not implemented for this asic\n");
		mutex_unlock(&adev->pm.mutex);
	} else {
		return amdgpu_debugfs_pm_info_pp(m, adev);
	}

	return 0;
}

static const struct drm_info_list amdgpu_pm_info_list[] = {
	{"amdgpu_pm_info", amdgpu_debugfs_pm_info, 0, NULL},
};
#endif

static int amdgpu_debugfs_pm_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	return amdgpu_debugfs_add_files(adev, amdgpu_pm_info_list, ARRAY_SIZE(amdgpu_pm_info_list));
#else
	return 0;
#endif
}

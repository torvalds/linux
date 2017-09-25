/*
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

#include "amd_powerplay.h"

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
			adev->pm.dpm.ac_power = true;
		else
			adev->pm.dpm.ac_power = false;
		if (adev->powerplay.pp_funcs->enable_bapm)
			amdgpu_dpm_enable_bapm(adev, adev->pm.dpm.ac_power);
		mutex_unlock(&adev->pm.mutex);
	}
}

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
		amdgpu_dpm_dispatch_task(adev, AMD_PP_TASK_ENABLE_USER_STATE, &state, NULL);
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

		amdgpu_dpm_get_pp_num_states(adev, &data);
		state = data.states[idx];
		/* only set user selected power states */
		if (state != POWER_STATE_TYPE_INTERNAL_BOOT &&
		    state != POWER_STATE_TYPE_DEFAULT) {
			amdgpu_dpm_dispatch_task(adev,
					AMD_PP_TASK_ENABLE_USER_STATE, &state, NULL);
			adev->pp_force_state_enabled = true;
		}
	}
fail:
	return count;
}

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

static ssize_t amdgpu_set_pp_dpm_sclk(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	int ret;
	long level;
	uint32_t i, mask = 0;
	char sub_str[2];

	for (i = 0; i < strlen(buf); i++) {
		if (*(buf + i) == '\n')
			continue;
		sub_str[0] = *(buf + i);
		sub_str[1] = '\0';
		ret = kstrtol(sub_str, 0, &level);

		if (ret) {
			count = -EINVAL;
			goto fail;
		}
		mask |= 1 << level;
	}

	if (adev->powerplay.pp_funcs->force_clock_level)
		amdgpu_dpm_force_clock_level(adev, PP_SCLK, mask);

fail:
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
	long level;
	uint32_t i, mask = 0;
	char sub_str[2];

	for (i = 0; i < strlen(buf); i++) {
		if (*(buf + i) == '\n')
			continue;
		sub_str[0] = *(buf + i);
		sub_str[1] = '\0';
		ret = kstrtol(sub_str, 0, &level);

		if (ret) {
			count = -EINVAL;
			goto fail;
		}
		mask |= 1 << level;
	}
	if (adev->powerplay.pp_funcs->force_clock_level)
		amdgpu_dpm_force_clock_level(adev, PP_MCLK, mask);

fail:
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
	long level;
	uint32_t i, mask = 0;
	char sub_str[2];

	for (i = 0; i < strlen(buf); i++) {
		if (*(buf + i) == '\n')
			continue;
		sub_str[0] = *(buf + i);
		sub_str[1] = '\0';
		ret = kstrtol(sub_str, 0, &level);

		if (ret) {
			count = -EINVAL;
			goto fail;
		}
		mask |= 1 << level;
	}
	if (adev->powerplay.pp_funcs->force_clock_level)
		amdgpu_dpm_force_clock_level(adev, PP_PCIE, mask);

fail:
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
		amdgpu_dpm_dispatch_task(adev, AMD_PP_TASK_READJUST_POWER_STATE, NULL, NULL);
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
		amdgpu_dpm_dispatch_task(adev, AMD_PP_TASK_READJUST_POWER_STATE, NULL, NULL);
	} else {
		adev->pm.dpm.current_ps = adev->pm.dpm.boot_ps;
		amdgpu_pm_compute_clocks(adev);
	}

fail:
	return count;
}

static ssize_t amdgpu_get_pp_power_profile(struct device *dev,
		char *buf, struct amd_pp_profile *query)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	int ret = 0xff;

	if (adev->powerplay.pp_funcs->get_power_profile_state)
		ret = amdgpu_dpm_get_power_profile_state(
				adev, query);

	if (ret)
		return ret;

	return snprintf(buf, PAGE_SIZE,
			"%d %d %d %d %d\n",
			query->min_sclk / 100,
			query->min_mclk / 100,
			query->activity_threshold,
			query->up_hyst,
			query->down_hyst);
}

static ssize_t amdgpu_get_pp_gfx_power_profile(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct amd_pp_profile query = {0};

	query.type = AMD_PP_GFX_PROFILE;

	return amdgpu_get_pp_power_profile(dev, buf, &query);
}

static ssize_t amdgpu_get_pp_compute_power_profile(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct amd_pp_profile query = {0};

	query.type = AMD_PP_COMPUTE_PROFILE;

	return amdgpu_get_pp_power_profile(dev, buf, &query);
}

static ssize_t amdgpu_set_pp_power_profile(struct device *dev,
		const char *buf,
		size_t count,
		struct amd_pp_profile *request)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;
	uint32_t loop = 0;
	char *sub_str, buf_cpy[128], *tmp_str;
	const char delimiter[3] = {' ', '\n', '\0'};
	long int value;
	int ret = 0xff;

	if (strncmp("reset", buf, strlen("reset")) == 0) {
		if (adev->powerplay.pp_funcs->reset_power_profile_state)
			ret = amdgpu_dpm_reset_power_profile_state(
					adev, request);
		if (ret) {
			count = -EINVAL;
			goto fail;
		}
		return count;
	}

	if (strncmp("set", buf, strlen("set")) == 0) {
		if (adev->powerplay.pp_funcs->set_power_profile_state)
			ret = amdgpu_dpm_set_power_profile_state(
					adev, request);

		if (ret) {
			count = -EINVAL;
			goto fail;
		}
		return count;
	}

	if (count + 1 >= 128) {
		count = -EINVAL;
		goto fail;
	}

	memcpy(buf_cpy, buf, count + 1);
	tmp_str = buf_cpy;

	while (tmp_str[0]) {
		sub_str = strsep(&tmp_str, delimiter);
		ret = kstrtol(sub_str, 0, &value);
		if (ret) {
			count = -EINVAL;
			goto fail;
		}

		switch (loop) {
		case 0:
			/* input unit MHz convert to dpm table unit 10KHz*/
			request->min_sclk = (uint32_t)value * 100;
			break;
		case 1:
			/* input unit MHz convert to dpm table unit 10KHz*/
			request->min_mclk = (uint32_t)value * 100;
			break;
		case 2:
			request->activity_threshold = (uint16_t)value;
			break;
		case 3:
			request->up_hyst = (uint8_t)value;
			break;
		case 4:
			request->down_hyst = (uint8_t)value;
			break;
		default:
			break;
		}

		loop++;
	}
	if (adev->powerplay.pp_funcs->set_power_profile_state)
		ret = amdgpu_dpm_set_power_profile_state(adev, request);

	if (ret)
		count = -EINVAL;

fail:
	return count;
}

static ssize_t amdgpu_set_pp_gfx_power_profile(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct amd_pp_profile request = {0};

	request.type = AMD_PP_GFX_PROFILE;

	return amdgpu_set_pp_power_profile(dev, buf, count, &request);
}

static ssize_t amdgpu_set_pp_compute_power_profile(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct amd_pp_profile request = {0};

	request.type = AMD_PP_COMPUTE_PROFILE;

	return amdgpu_set_pp_power_profile(dev, buf, count, &request);
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
static DEVICE_ATTR(pp_gfx_power_profile, S_IRUGO | S_IWUSR,
		amdgpu_get_pp_gfx_power_profile,
		amdgpu_set_pp_gfx_power_profile);
static DEVICE_ATTR(pp_compute_power_profile, S_IRUGO | S_IWUSR,
		amdgpu_get_pp_compute_power_profile,
		amdgpu_set_pp_compute_power_profile);

static ssize_t amdgpu_hwmon_show_temp(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	struct drm_device *ddev = adev->ddev;
	int temp;

	/* Can't get temperature when the card is off */
	if  ((adev->flags & AMD_IS_PX) &&
	     (ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return -EINVAL;

	if (!adev->powerplay.pp_funcs->get_temperature)
		temp = 0;
	else
		temp = amdgpu_dpm_get_temperature(adev);

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

	if (adev->powerplay.pp_funcs->get_fan_speed_rpm) {
		err = amdgpu_dpm_get_fan_speed_rpm(adev, &speed);
		if (err)
			return err;
	}

	return sprintf(buf, "%i\n", speed);
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, amdgpu_hwmon_show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, amdgpu_hwmon_show_temp_thresh, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IRUGO, amdgpu_hwmon_show_temp_thresh, NULL, 1);
static SENSOR_DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR, amdgpu_hwmon_get_pwm1, amdgpu_hwmon_set_pwm1, 0);
static SENSOR_DEVICE_ATTR(pwm1_enable, S_IRUGO | S_IWUSR, amdgpu_hwmon_get_pwm1_enable, amdgpu_hwmon_set_pwm1_enable, 0);
static SENSOR_DEVICE_ATTR(pwm1_min, S_IRUGO, amdgpu_hwmon_get_pwm1_min, NULL, 0);
static SENSOR_DEVICE_ATTR(pwm1_max, S_IRUGO, amdgpu_hwmon_get_pwm1_max, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, amdgpu_hwmon_get_fan1_input, NULL, 0);

static struct attribute *hwmon_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1_min.dev_attr.attr,
	&sensor_dev_attr_pwm1_max.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	NULL
};

static umode_t hwmon_attributes_visible(struct kobject *kobj,
					struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	umode_t effective_mode = attr->mode;

	/* Skip limit attributes if DPM is not enabled */
	if (!adev->pm.dpm_enabled &&
	    (attr == &sensor_dev_attr_temp1_crit.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp1_crit_hyst.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_enable.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_min.dev_attr.attr))
		return 0;

	/* Skip fan attributes if fan is not present */
	if (adev->pm.no_fan &&
	    (attr == &sensor_dev_attr_pwm1.dev_attr.attr ||
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

	/* hide max/min values if we can't both query and manage the fan */
	if ((!adev->powerplay.pp_funcs->set_fan_speed_percent &&
	     !adev->powerplay.pp_funcs->get_fan_speed_percent) &&
	    (attr == &sensor_dev_attr_pwm1_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_min.dev_attr.attr))
		return 0;

	/* requires powerplay */
	if (attr == &sensor_dev_attr_fan1_input.dev_attr.attr)
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

	if (!adev->pm.dpm_enabled)
		return;

	if (adev->powerplay.pp_funcs->get_temperature) {
		int temp = amdgpu_dpm_get_temperature(adev);

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
	if (adev->powerplay.pp_funcs->powergate_uvd) {
		/* enable/disable UVD */
		mutex_lock(&adev->pm.mutex);
		amdgpu_dpm_powergate_uvd(adev, !enable);
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
	if (adev->powerplay.pp_funcs->powergate_vce) {
		/* enable/disable VCE */
		mutex_lock(&adev->pm.mutex);
		amdgpu_dpm_powergate_vce(adev, !enable);
		mutex_unlock(&adev->pm.mutex);
	} else {
		if (enable) {
			mutex_lock(&adev->pm.mutex);
			adev->pm.dpm.vce_active = true;
			/* XXX select vce level based on ring/task */
			adev->pm.dpm.vce_level = AMD_VCE_LEVEL_AC_ALL;
			mutex_unlock(&adev->pm.mutex);
			amdgpu_set_clockgating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
							AMD_CG_STATE_UNGATE);
			amdgpu_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
							AMD_PG_STATE_UNGATE);
			amdgpu_pm_compute_clocks(adev);
		} else {
			amdgpu_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
							AMD_PG_STATE_GATE);
			amdgpu_set_clockgating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
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

	if (adev->powerplay.pp_funcs->get_temperature == NULL)
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
			&dev_attr_pp_gfx_power_profile);
	if (ret) {
		DRM_ERROR("failed to create device file	"
				"pp_gfx_power_profile\n");
		return ret;
	}
	ret = device_create_file(adev->dev,
			&dev_attr_pp_compute_power_profile);
	if (ret) {
		DRM_ERROR("failed to create device file	"
				"pp_compute_power_profile\n");
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
			&dev_attr_pp_gfx_power_profile);
	device_remove_file(adev->dev,
			&dev_attr_pp_compute_power_profile);
}

void amdgpu_pm_compute_clocks(struct amdgpu_device *adev)
{
	struct drm_device *ddev = adev->ddev;
	struct drm_crtc *crtc;
	struct amdgpu_crtc *amdgpu_crtc;
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
		amdgpu_dpm_dispatch_task(adev, AMD_PP_TASK_DISPLAY_CONFIG_CHANGE, NULL, NULL);
	} else {
		mutex_lock(&adev->pm.mutex);
		adev->pm.dpm.new_active_crtcs = 0;
		adev->pm.dpm.new_active_crtc_count = 0;
		if (adev->mode_info.num_crtc && adev->mode_info.mode_config_initialized) {
			list_for_each_entry(crtc,
					    &ddev->mode_config.crtc_list, head) {
				amdgpu_crtc = to_amdgpu_crtc(crtc);
				if (crtc->enabled) {
					adev->pm.dpm.new_active_crtcs |= (1 << amdgpu_crtc->crtc_id);
					adev->pm.dpm.new_active_crtc_count++;
				}
			}
		}
		/* update battery/ac status */
		if (power_supply_is_system_supplied() > 0)
			adev->pm.dpm.ac_power = true;
		else
			adev->pm.dpm.ac_power = false;

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
	struct pp_gpu_power query = {0};
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
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_VDDGFX, (void *)&value, &size))
		seq_printf(m, "\t%u mV (VDDGFX)\n", value);
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_VDDNB, (void *)&value, &size))
		seq_printf(m, "\t%u mV (VDDNB)\n", value);
	size = sizeof(query);
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GPU_POWER, (void *)&query, &size)) {
		seq_printf(m, "\t%u.%u W (VDDC)\n", query.vddc_power >> 8,
				query.vddc_power & 0xff);
		seq_printf(m, "\t%u.%u W (VDDCI)\n", query.vddci_power >> 8,
				query.vddci_power & 0xff);
		seq_printf(m, "\t%u.%u W (max GPU)\n", query.max_gpu_power >> 8,
				query.max_gpu_power & 0xff);
		seq_printf(m, "\t%u.%u W (average GPU)\n", query.average_gpu_power >> 8,
				query.average_gpu_power & 0xff);
	}
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

	amdgpu_get_clockgating_state(adev, &flags);
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

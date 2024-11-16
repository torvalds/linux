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

#define MAX_NUM_OF_FEATURES_PER_SUBSET		8
#define MAX_NUM_OF_SUBSETS			8

#define DEVICE_ATTR_IS(_name)		(attr_id == device_attr_id__##_name)

struct od_attribute {
	struct kobj_attribute	attribute;
	struct list_head	entry;
};

struct od_kobj {
	struct kobject		kobj;
	struct list_head	entry;
	struct list_head	attribute;
	void			*priv;
};

struct od_feature_ops {
	umode_t (*is_visible)(struct amdgpu_device *adev);
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count);
};

struct od_feature_item {
	const char		*name;
	struct od_feature_ops	ops;
};

struct od_feature_container {
	char				*name;
	struct od_feature_ops		ops;
	struct od_feature_item		sub_feature[MAX_NUM_OF_FEATURES_PER_SUBSET];
};

struct od_feature_set {
	struct od_feature_container	containers[MAX_NUM_OF_SUBSETS];
};

static const struct hwmon_temp_label {
	enum PP_HWMON_TEMP channel;
	const char *label;
} temp_label[] = {
	{PP_TEMP_EDGE, "edge"},
	{PP_TEMP_JUNCTION, "junction"},
	{PP_TEMP_MEM, "mem"},
};

const char * const amdgpu_pp_profile_name[] = {
	"BOOTUP_DEFAULT",
	"3D_FULL_SCREEN",
	"POWER_SAVING",
	"VIDEO",
	"VR",
	"COMPUTE",
	"CUSTOM",
	"WINDOW_3D",
	"CAPPED",
	"UNCAPPED",
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

	amdgpu_dpm_get_current_power_state(adev, &pm);

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

	amdgpu_dpm_set_power_state(adev, state);

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

	level = amdgpu_dpm_get_performance_level(adev);

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
	enum amd_dpm_forced_level level;
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

	mutex_lock(&adev->pm.stable_pstate_ctx_lock);
	if (amdgpu_dpm_force_performance_level(adev, level)) {
		pm_runtime_mark_last_busy(ddev->dev);
		pm_runtime_put_autosuspend(ddev->dev);
		mutex_unlock(&adev->pm.stable_pstate_ctx_lock);
		return -EINVAL;
	}
	/* override whatever a user ctx may have set */
	adev->pm.stable_pstate_ctx = NULL;
	mutex_unlock(&adev->pm.stable_pstate_ctx_lock);

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
	struct pp_states_info data;
	uint32_t i;
	int buf_len, ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (amdgpu_dpm_get_pp_num_states(adev, &data))
		memset(&data, 0, sizeof(data));

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	buf_len = sysfs_emit(buf, "states: %d\n", data.nums);
	for (i = 0; i < data.nums; i++)
		buf_len += sysfs_emit_at(buf, buf_len, "%d %s\n", i,
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

	amdgpu_dpm_get_current_power_state(adev, &pm);

	ret = amdgpu_dpm_get_pp_num_states(adev, &data);

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	if (ret)
		return ret;

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

	if (adev->pm.pp_force_state_enabled)
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
	struct pp_states_info data;
	unsigned long idx;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	adev->pm.pp_force_state_enabled = false;

	if (strlen(buf) == 1)
		return count;

	ret = kstrtoul(buf, 0, &idx);
	if (ret || idx >= ARRAY_SIZE(data.states))
		return -EINVAL;

	idx = array_index_nospec(idx, ARRAY_SIZE(data.states));

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	ret = amdgpu_dpm_get_pp_num_states(adev, &data);
	if (ret)
		goto err_out;

	state = data.states[idx];

	/* only set user selected power states */
	if (state != POWER_STATE_TYPE_INTERNAL_BOOT &&
	    state != POWER_STATE_TYPE_DEFAULT) {
		ret = amdgpu_dpm_dispatch_task(adev,
				AMD_PP_TASK_ENABLE_USER_STATE, &state);
		if (ret)
			goto err_out;

		adev->pm.pp_force_state_enabled = true;
	}

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return count;

err_out:
	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);
	return ret;
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

	size = amdgpu_dpm_get_pp_table(adev, &table);

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	if (size <= 0)
		return size;

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

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	if (ret)
		return ret;

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
 *   They can be used to calibrate the sclk voltage curve. This is
 *   available for Vega20 and NV1X.
 *
 * - voltage offset(in mV) applied on target voltage calculation.
 *   This is available for Sienna Cichlid, Navy Flounder, Dimgrey
 *   Cavefish and some later SMU13 ASICs. For these ASICs, the target
 *   voltage calculation can be illustrated by "voltage = voltage
 *   calculated from v/f curve + overdrive vddgfx offset"
 *
 * - a list of valid ranges for sclk, mclk, voltage curve points
 *   or voltage offset labeled OD_RANGE
 *
 * < For APUs >
 *
 * Reading the file will display:
 *
 * - minimum and maximum engine clock labeled OD_SCLK
 *
 * - a list of valid ranges for sclk labeled OD_RANGE
 *
 * < For VanGogh >
 *
 * Reading the file will display:
 *
 * - minimum and maximum engine clock labeled OD_SCLK
 * - minimum and maximum core clocks labeled OD_CCLK
 *
 * - a list of valid ranges for sclk and cclk labeled OD_RANGE
 *
 * To manually adjust these settings:
 *
 * - First select manual using power_dpm_force_performance_level
 *
 * - For clock frequency setting, enter a new value by writing a
 *   string that contains "s/m index clock" to the file. The index
 *   should be 0 if to set minimum clock. And 1 if to set maximum
 *   clock. E.g., "s 0 500" will update minimum sclk to be 500 MHz.
 *   "m 1 800" will update maximum mclk to be 800Mhz. For core
 *   clocks on VanGogh, the string contains "p core index clock".
 *   E.g., "p 2 0 800" would set the minimum core clock on core
 *   2 to 800Mhz.
 *
 *   For sclk voltage curve supported by Vega20 and NV1X, enter the new
 *   values by writing a string that contains "vc point clock voltage"
 *   to the file. The points are indexed by 0, 1 and 2. E.g., "vc 0 300
 *   600" will update point1 with clock set as 300Mhz and voltage as 600mV.
 *   "vc 2 1000 1000" will update point3 with clock set as 1000Mhz and
 *   voltage 1000mV.
 *
 *   For voltage offset supported by Sienna Cichlid, Navy Flounder, Dimgrey
 *   Cavefish and some later SMU13 ASICs, enter the new value by writing a
 *   string that contains "vo offset". E.g., "vo -10" will update the extra
 *   voltage offset applied to the whole v/f curve line as -10mv.
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

	if (count > 127 || count == 0)
		return -EINVAL;

	if (*buf == 's')
		type = PP_OD_EDIT_SCLK_VDDC_TABLE;
	else if (*buf == 'p')
		type = PP_OD_EDIT_CCLK_VDDC_TABLE;
	else if (*buf == 'm')
		type = PP_OD_EDIT_MCLK_VDDC_TABLE;
	else if (*buf == 'r')
		type = PP_OD_RESTORE_DEFAULT_TABLE;
	else if (*buf == 'c')
		type = PP_OD_COMMIT_DPM_TABLE;
	else if (!strncmp(buf, "vc", 2))
		type = PP_OD_EDIT_VDDC_CURVE;
	else if (!strncmp(buf, "vo", 2))
		type = PP_OD_EDIT_VDDGFX_OFFSET;
	else
		return -EINVAL;

	memcpy(buf_cpy, buf, count);
	buf_cpy[count] = 0;

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

		if (!tmp_str)
			break;

		while (isspace(*tmp_str))
			tmp_str++;
	}

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	if (amdgpu_dpm_set_fine_grain_clk_vol(adev,
					      type,
					      parameter,
					      parameter_size))
		goto err_out;

	if (amdgpu_dpm_odn_edit_dpm_table(adev, type,
					  parameter, parameter_size))
		goto err_out;

	if (type == PP_OD_COMMIT_DPM_TABLE) {
		if (amdgpu_dpm_dispatch_task(adev,
					     AMD_PP_TASK_READJUST_POWER_STATE,
					     NULL))
			goto err_out;
	}

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return count;

err_out:
	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);
	return -EINVAL;
}

static ssize_t amdgpu_get_pp_od_clk_voltage(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int size = 0;
	int ret;
	enum pp_clock_type od_clocks[6] = {
		OD_SCLK,
		OD_MCLK,
		OD_VDDC_CURVE,
		OD_RANGE,
		OD_VDDGFX_OFFSET,
		OD_CCLK,
	};
	uint clk_index;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	for (clk_index = 0 ; clk_index < 6 ; clk_index++) {
		ret = amdgpu_dpm_emit_clock_levels(adev, od_clocks[clk_index], buf, &size);
		if (ret)
			break;
	}
	if (ret == -ENOENT) {
		size = amdgpu_dpm_print_clock_levels(adev, OD_SCLK, buf);
		size += amdgpu_dpm_print_clock_levels(adev, OD_MCLK, buf + size);
		size += amdgpu_dpm_print_clock_levels(adev, OD_VDDC_CURVE, buf + size);
		size += amdgpu_dpm_print_clock_levels(adev, OD_VDDGFX_OFFSET, buf + size);
		size += amdgpu_dpm_print_clock_levels(adev, OD_RANGE, buf + size);
		size += amdgpu_dpm_print_clock_levels(adev, OD_CCLK, buf + size);
	}

	if (size == 0)
		size = sysfs_emit(buf, "\n");

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

	ret = amdgpu_dpm_set_ppfeature_status(adev, featuremask);

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	if (ret)
		return -EINVAL;

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

	size = amdgpu_dpm_get_ppfeature_status(adev, buf);
	if (size <= 0)
		size = sysfs_emit(buf, "\n");

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
 * the power state and the clock information for those levels. If deep sleep is
 * applied to a clock, the level will be denoted by a special level 'S:'
 * E.g., ::
 *
 *  S: 19Mhz *
 *  0: 615Mhz
 *  1: 800Mhz
 *  2: 888Mhz
 *  3: 1000Mhz
 *
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
	int size = 0;
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

	ret = amdgpu_dpm_emit_clock_levels(adev, type, buf, &size);
	if (ret == -ENOENT)
		size = amdgpu_dpm_print_clock_levels(adev, type, buf);

	if (size == 0)
		size = sysfs_emit(buf, "\n");

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

	ret = amdgpu_dpm_force_clock_level(adev, type, mask);

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

static ssize_t amdgpu_get_pp_dpm_vclk1(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return amdgpu_get_pp_dpm_clock(dev, PP_VCLK1, buf);
}

static ssize_t amdgpu_set_pp_dpm_vclk1(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	return amdgpu_set_pp_dpm_clock(dev, PP_VCLK1, buf, count);
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

static ssize_t amdgpu_get_pp_dpm_dclk1(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return amdgpu_get_pp_dpm_clock(dev, PP_DCLK1, buf);
}

static ssize_t amdgpu_set_pp_dpm_dclk1(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	return amdgpu_set_pp_dpm_clock(dev, PP_DCLK1, buf, count);
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

	amdgpu_dpm_set_sclk_od(adev, (uint32_t)value);

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

	amdgpu_dpm_set_mclk_od(adev, (uint32_t)value);

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
 * the heuristic parameters vary from family to family. Additionally,
 * you can apply the custom heuristics to different clock domains.  Each
 * clock domain is considered a distinct operation so if you modify the
 * gfxclk heuristics and then the memclk heuristics, the all of the
 * custom heuristics will be retained until you switch to another profile.
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

	size = amdgpu_dpm_get_power_profile_mode(adev, buf);
	if (size <= 0)
		size = sysfs_emit(buf, "\n");

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

	ret = amdgpu_dpm_set_power_profile_mode(adev, parameter, parameter_size);

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	if (!ret)
		return count;

	return -EINVAL;
}

static int amdgpu_hwmon_get_sensor_generic(struct amdgpu_device *adev,
					   enum amd_pp_sensors sensor,
					   void *query)
{
	int r, size = sizeof(uint32_t);

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	/* get the sensor value */
	r = amdgpu_dpm_read_sensor(adev, sensor, query, &size);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	return r;
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
	unsigned int value;
	int r;

	r = amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_GPU_LOAD, &value);
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
	unsigned int value;
	int r;

	r = amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_MEM_LOAD, &value);
	if (r)
		return r;

	return sysfs_emit(buf, "%d\n", value);
}

/**
 * DOC: vcn_busy_percent
 *
 * The amdgpu driver provides a sysfs API for reading how busy the VCN
 * is as a percentage.  The file vcn_busy_percent is used for this.
 * The SMU firmware computes a percentage of load based on the
 * aggregate activity level in the IP cores.
 */
static ssize_t amdgpu_get_vcn_busy_percent(struct device *dev,
						  struct device_attribute *attr,
						  char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	unsigned int value;
	int r;

	r = amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_VCN_LOAD, &value);
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
 * DOC: apu_thermal_cap
 *
 * The amdgpu driver provides a sysfs API for retrieving/updating thermal
 * limit temperature in millidegrees Celsius
 *
 * Reading back the file shows you core limit value
 *
 * Writing an integer to the file, sets a new thermal limit. The value
 * should be between 0 and 100. If the value is less than 0 or greater
 * than 100, then the write request will be ignored.
 */
static ssize_t amdgpu_get_apu_thermal_cap(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int ret, size;
	u32 limit;
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	ret = amdgpu_dpm_get_apu_thermal_limit(adev, &limit);
	if (!ret)
		size = sysfs_emit(buf, "%u\n", limit);
	else
		size = sysfs_emit(buf, "failed to get thermal limit\n");

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return size;
}

static ssize_t amdgpu_set_apu_thermal_cap(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t count)
{
	int ret;
	u32 value;
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (value > 100) {
		dev_err(dev, "Invalid argument !\n");
		return -EINVAL;
	}

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	ret = amdgpu_dpm_set_apu_thermal_limit(adev, value);
	if (ret) {
		dev_err(dev, "failed to update thermal limit\n");
		return ret;
	}

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return count;
}

static int amdgpu_pm_metrics_attr_update(struct amdgpu_device *adev,
					 struct amdgpu_device_attr *attr,
					 uint32_t mask,
					 enum amdgpu_device_attr_states *states)
{
	if (amdgpu_dpm_get_pm_metrics(adev, NULL, 0) == -EOPNOTSUPP)
		*states = ATTR_STATE_UNSUPPORTED;

	return 0;
}

static ssize_t amdgpu_get_pm_metrics(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
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

	size = amdgpu_dpm_get_pm_metrics(adev, buf, PAGE_SIZE);

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return size;
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

static int amdgpu_show_powershift_percent(struct device *dev,
					char *buf, enum amd_pp_sensors sensor)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	uint32_t ss_power;
	int r = 0, i;

	r = amdgpu_hwmon_get_sensor_generic(adev, sensor, (void *)&ss_power);
	if (r == -EOPNOTSUPP) {
		/* sensor not available on dGPU, try to read from APU */
		adev = NULL;
		mutex_lock(&mgpu_info.mutex);
		for (i = 0; i < mgpu_info.num_gpu; i++) {
			if (mgpu_info.gpu_ins[i].adev->flags & AMD_IS_APU) {
				adev = mgpu_info.gpu_ins[i].adev;
				break;
			}
		}
		mutex_unlock(&mgpu_info.mutex);
		if (adev)
			r = amdgpu_hwmon_get_sensor_generic(adev, sensor, (void *)&ss_power);
	}

	if (r)
		return r;

	return sysfs_emit(buf, "%u%%\n", ss_power);
}

/**
 * DOC: smartshift_apu_power
 *
 * The amdgpu driver provides a sysfs API for reporting APU power
 * shift in percentage if platform supports smartshift. Value 0 means that
 * there is no powershift and values between [1-100] means that the power
 * is shifted to APU, the percentage of boost is with respect to APU power
 * limit on the platform.
 */

static ssize_t amdgpu_get_smartshift_apu_power(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	return amdgpu_show_powershift_percent(dev, buf, AMDGPU_PP_SENSOR_SS_APU_SHARE);
}

/**
 * DOC: smartshift_dgpu_power
 *
 * The amdgpu driver provides a sysfs API for reporting dGPU power
 * shift in percentage if platform supports smartshift. Value 0 means that
 * there is no powershift and values between [1-100] means that the power is
 * shifted to dGPU, the percentage of boost is with respect to dGPU power
 * limit on the platform.
 */

static ssize_t amdgpu_get_smartshift_dgpu_power(struct device *dev, struct device_attribute *attr,
						char *buf)
{
	return amdgpu_show_powershift_percent(dev, buf, AMDGPU_PP_SENSOR_SS_DGPU_SHARE);
}

/**
 * DOC: smartshift_bias
 *
 * The amdgpu driver provides a sysfs API for reporting the
 * smartshift(SS2.0) bias level. The value ranges from -100 to 100
 * and the default is 0. -100 sets maximum preference to APU
 * and 100 sets max perference to dGPU.
 */

static ssize_t amdgpu_get_smartshift_bias(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	int r = 0;

	r = sysfs_emit(buf, "%d\n", amdgpu_smartshift_bias);

	return r;
}

static ssize_t amdgpu_set_smartshift_bias(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int r = 0;
	int bias = 0;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	r = pm_runtime_get_sync(ddev->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return r;
	}

	r = kstrtoint(buf, 10, &bias);
	if (r)
		goto out;

	if (bias > AMDGPU_SMARTSHIFT_MAX_BIAS)
		bias = AMDGPU_SMARTSHIFT_MAX_BIAS;
	else if (bias < AMDGPU_SMARTSHIFT_MIN_BIAS)
		bias = AMDGPU_SMARTSHIFT_MIN_BIAS;

	amdgpu_smartshift_bias = bias;
	r = count;

	/* TODO: update bias level with SMU message */

out:
	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);
	return r;
}

static int ss_power_attr_update(struct amdgpu_device *adev, struct amdgpu_device_attr *attr,
				uint32_t mask, enum amdgpu_device_attr_states *states)
{
	if (!amdgpu_device_supports_smart_shift(adev_to_drm(adev)))
		*states = ATTR_STATE_UNSUPPORTED;

	return 0;
}

static int ss_bias_attr_update(struct amdgpu_device *adev, struct amdgpu_device_attr *attr,
			       uint32_t mask, enum amdgpu_device_attr_states *states)
{
	uint32_t ss_power;

	if (!amdgpu_device_supports_smart_shift(adev_to_drm(adev)))
		*states = ATTR_STATE_UNSUPPORTED;
	else if (amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_SS_APU_SHARE,
		 (void *)&ss_power))
		*states = ATTR_STATE_UNSUPPORTED;
	else if (amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_SS_DGPU_SHARE,
		 (void *)&ss_power))
		*states = ATTR_STATE_UNSUPPORTED;

	return 0;
}

static int pp_od_clk_voltage_attr_update(struct amdgpu_device *adev, struct amdgpu_device_attr *attr,
					 uint32_t mask, enum amdgpu_device_attr_states *states)
{
	uint32_t gc_ver = amdgpu_ip_version(adev, GC_HWIP, 0);

	*states = ATTR_STATE_SUPPORTED;

	if (!amdgpu_dpm_is_overdrive_supported(adev)) {
		*states = ATTR_STATE_UNSUPPORTED;
		return 0;
	}

	/* Enable pp_od_clk_voltage node for gc 9.4.3 SRIOV/BM support */
	if (gc_ver == IP_VERSION(9, 4, 3) ||
	    gc_ver == IP_VERSION(9, 4, 4)) {
		if (amdgpu_sriov_vf(adev) && !amdgpu_sriov_is_pp_one_vf(adev))
			*states = ATTR_STATE_UNSUPPORTED;
		return 0;
	}

	if (!(attr->flags & mask))
		*states = ATTR_STATE_UNSUPPORTED;

	return 0;
}

static int pp_dpm_dcefclk_attr_update(struct amdgpu_device *adev, struct amdgpu_device_attr *attr,
				      uint32_t mask, enum amdgpu_device_attr_states *states)
{
	struct device_attribute *dev_attr = &attr->dev_attr;
	uint32_t gc_ver;

	*states = ATTR_STATE_SUPPORTED;

	if (!(attr->flags & mask)) {
		*states = ATTR_STATE_UNSUPPORTED;
		return 0;
	}

	gc_ver = amdgpu_ip_version(adev, GC_HWIP, 0);
	/* dcefclk node is not available on gfx 11.0.3 sriov */
	if ((gc_ver == IP_VERSION(11, 0, 3) && amdgpu_sriov_is_pp_one_vf(adev)) ||
	    gc_ver < IP_VERSION(9, 0, 0) ||
	    !amdgpu_device_has_display_hardware(adev))
		*states = ATTR_STATE_UNSUPPORTED;

	/* SMU MP1 does not support dcefclk level setting,
	 * setting should not be allowed from VF if not in one VF mode.
	 */
	if (gc_ver >= IP_VERSION(10, 0, 0) ||
	    (amdgpu_sriov_vf(adev) && !amdgpu_sriov_is_pp_one_vf(adev))) {
		dev_attr->attr.mode &= ~S_IWUGO;
		dev_attr->store = NULL;
	}

	return 0;
}

static int pp_dpm_clk_default_attr_update(struct amdgpu_device *adev, struct amdgpu_device_attr *attr,
					  uint32_t mask, enum amdgpu_device_attr_states *states)
{
	struct device_attribute *dev_attr = &attr->dev_attr;
	enum amdgpu_device_attr_id attr_id = attr->attr_id;
	uint32_t mp1_ver = amdgpu_ip_version(adev, MP1_HWIP, 0);
	uint32_t gc_ver = amdgpu_ip_version(adev, GC_HWIP, 0);

	*states = ATTR_STATE_SUPPORTED;

	if (!(attr->flags & mask)) {
		*states = ATTR_STATE_UNSUPPORTED;
		return 0;
	}

	if (DEVICE_ATTR_IS(pp_dpm_socclk)) {
		if (gc_ver < IP_VERSION(9, 0, 0))
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_dpm_fclk)) {
		if (mp1_ver < IP_VERSION(10, 0, 0))
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_dpm_vclk)) {
		if (!(gc_ver == IP_VERSION(10, 3, 1) ||
		      gc_ver == IP_VERSION(10, 3, 3) ||
		      gc_ver == IP_VERSION(10, 3, 6) ||
		      gc_ver == IP_VERSION(10, 3, 7) ||
		      gc_ver == IP_VERSION(10, 3, 0) ||
		      gc_ver == IP_VERSION(10, 1, 2) ||
		      gc_ver == IP_VERSION(11, 0, 0) ||
		      gc_ver == IP_VERSION(11, 0, 1) ||
		      gc_ver == IP_VERSION(11, 0, 4) ||
		      gc_ver == IP_VERSION(11, 5, 0) ||
		      gc_ver == IP_VERSION(11, 0, 2) ||
		      gc_ver == IP_VERSION(11, 0, 3) ||
		      gc_ver == IP_VERSION(9, 4, 3) ||
		      gc_ver == IP_VERSION(9, 4, 4)))
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_dpm_vclk1)) {
		if (!((gc_ver == IP_VERSION(10, 3, 1) ||
		       gc_ver == IP_VERSION(10, 3, 0) ||
		       gc_ver == IP_VERSION(11, 0, 2) ||
		       gc_ver == IP_VERSION(11, 0, 3)) && adev->vcn.num_vcn_inst >= 2))
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_dpm_dclk)) {
		if (!(gc_ver == IP_VERSION(10, 3, 1) ||
		      gc_ver == IP_VERSION(10, 3, 3) ||
		      gc_ver == IP_VERSION(10, 3, 6) ||
		      gc_ver == IP_VERSION(10, 3, 7) ||
		      gc_ver == IP_VERSION(10, 3, 0) ||
		      gc_ver == IP_VERSION(10, 1, 2) ||
		      gc_ver == IP_VERSION(11, 0, 0) ||
		      gc_ver == IP_VERSION(11, 0, 1) ||
		      gc_ver == IP_VERSION(11, 0, 4) ||
		      gc_ver == IP_VERSION(11, 5, 0) ||
		      gc_ver == IP_VERSION(11, 0, 2) ||
		      gc_ver == IP_VERSION(11, 0, 3) ||
		      gc_ver == IP_VERSION(9, 4, 3) ||
		      gc_ver == IP_VERSION(9, 4, 4)))
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_dpm_dclk1)) {
		if (!((gc_ver == IP_VERSION(10, 3, 1) ||
		       gc_ver == IP_VERSION(10, 3, 0) ||
		       gc_ver == IP_VERSION(11, 0, 2) ||
		       gc_ver == IP_VERSION(11, 0, 3)) && adev->vcn.num_vcn_inst >= 2))
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_dpm_pcie)) {
		if (gc_ver == IP_VERSION(9, 4, 2) ||
		    gc_ver == IP_VERSION(9, 4, 3) ||
		    gc_ver == IP_VERSION(9, 4, 4))
			*states = ATTR_STATE_UNSUPPORTED;
	}

	switch (gc_ver) {
	case IP_VERSION(9, 4, 1):
	case IP_VERSION(9, 4, 2):
		/* the Mi series card does not support standalone mclk/socclk/fclk level setting */
		if (DEVICE_ATTR_IS(pp_dpm_mclk) ||
		    DEVICE_ATTR_IS(pp_dpm_socclk) ||
		    DEVICE_ATTR_IS(pp_dpm_fclk)) {
			dev_attr->attr.mode &= ~S_IWUGO;
			dev_attr->store = NULL;
		}
		break;
	default:
		break;
	}

	/* setting should not be allowed from VF if not in one VF mode */
	if (amdgpu_sriov_vf(adev) && amdgpu_sriov_is_pp_one_vf(adev)) {
		dev_attr->attr.mode &= ~S_IWUGO;
		dev_attr->store = NULL;
	}

	return 0;
}

/* pm policy attributes */
struct amdgpu_pm_policy_attr {
	struct device_attribute dev_attr;
	enum pp_pm_policy id;
};

/**
 * DOC: pm_policy
 *
 * Certain SOCs can support different power policies to optimize application
 * performance. However, this policy is provided only at SOC level and not at a
 * per-process level. This is useful especially when entire SOC is utilized for
 * dedicated workload.
 *
 * The amdgpu driver provides a sysfs API for selecting the policy. Presently,
 * only two types of policies are supported through this interface.
 *
 *  Pstate Policy Selection - This is to select different Pstate profiles which
 *  decides clock/throttling preferences.
 *
 *  XGMI PLPD Policy Selection - When multiple devices are connected over XGMI,
 *  this helps to select policy to be applied for per link power down.
 *
 * The list of available policies and policy levels vary between SOCs. They can
 * be viewed under pm_policy node directory. If SOC doesn't support any policy,
 * this node won't be available. The different policies supported will be
 * available as separate nodes under pm_policy.
 *
 *	cat /sys/bus/pci/devices/.../pm_policy/<policy_type>
 *
 * Reading the policy file shows the different levels supported. The level which
 * is applied presently is denoted by * (asterisk). E.g.,
 *
 * .. code-block:: console
 *
 *	cat /sys/bus/pci/devices/.../pm_policy/soc_pstate
 *	0 : soc_pstate_default
 *	1 : soc_pstate_0
 *	2 : soc_pstate_1*
 *	3 : soc_pstate_2
 *
 *	cat /sys/bus/pci/devices/.../pm_policy/xgmi_plpd
 *	0 : plpd_disallow
 *	1 : plpd_default
 *	2 : plpd_optimized*
 *
 * To apply a specific policy
 *
 * "echo  <level> > /sys/bus/pci/devices/.../pm_policy/<policy_type>"
 *
 * For the levels listed in the example above, to select "plpd_optimized" for
 * XGMI and "soc_pstate_2" for soc pstate policy -
 *
 * .. code-block:: console
 *
 *	echo "2" > /sys/bus/pci/devices/.../pm_policy/xgmi_plpd
 *	echo "3" > /sys/bus/pci/devices/.../pm_policy/soc_pstate
 *
 */
static ssize_t amdgpu_get_pm_policy_attr(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct amdgpu_pm_policy_attr *policy_attr;

	policy_attr =
		container_of(attr, struct amdgpu_pm_policy_attr, dev_attr);

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	return amdgpu_dpm_get_pm_policy_info(adev, policy_attr->id, buf);
}

static ssize_t amdgpu_set_pm_policy_attr(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct amdgpu_pm_policy_attr *policy_attr;
	int ret, num_params = 0;
	char delimiter[] = " \n\t";
	char tmp_buf[128];
	char *tmp, *param;
	long val;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	count = min(count, sizeof(tmp_buf));
	memcpy(tmp_buf, buf, count);
	tmp_buf[count - 1] = '\0';
	tmp = tmp_buf;

	tmp = skip_spaces(tmp);
	while ((param = strsep(&tmp, delimiter))) {
		if (!strlen(param)) {
			tmp = skip_spaces(tmp);
			continue;
		}
		ret = kstrtol(param, 0, &val);
		if (ret)
			return -EINVAL;
		num_params++;
		if (num_params > 1)
			return -EINVAL;
	}

	if (num_params != 1)
		return -EINVAL;

	policy_attr =
		container_of(attr, struct amdgpu_pm_policy_attr, dev_attr);

	ret = pm_runtime_get_sync(ddev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(ddev->dev);
		return ret;
	}

	ret = amdgpu_dpm_set_pm_policy(adev, policy_attr->id, val);

	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	if (ret)
		return ret;

	return count;
}

#define AMDGPU_PM_POLICY_ATTR(_name, _id)                                  \
	static struct amdgpu_pm_policy_attr pm_policy_attr_##_name = {     \
		.dev_attr = __ATTR(_name, 0644, amdgpu_get_pm_policy_attr, \
				   amdgpu_set_pm_policy_attr),             \
		.id = PP_PM_POLICY_##_id,                                  \
	};

#define AMDGPU_PM_POLICY_ATTR_VAR(_name) pm_policy_attr_##_name.dev_attr.attr

AMDGPU_PM_POLICY_ATTR(soc_pstate, SOC_PSTATE)
AMDGPU_PM_POLICY_ATTR(xgmi_plpd, XGMI_PLPD)

static struct attribute *pm_policy_attrs[] = {
	&AMDGPU_PM_POLICY_ATTR_VAR(soc_pstate),
	&AMDGPU_PM_POLICY_ATTR_VAR(xgmi_plpd),
	NULL
};

static umode_t amdgpu_pm_policy_attr_visible(struct kobject *kobj,
					     struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct amdgpu_pm_policy_attr *policy_attr;

	policy_attr =
		container_of(attr, struct amdgpu_pm_policy_attr, dev_attr.attr);

	if (amdgpu_dpm_get_pm_policy_info(adev, policy_attr->id, NULL) ==
	    -ENOENT)
		return 0;

	return attr->mode;
}

const struct attribute_group amdgpu_pm_policy_attr_group = {
	.name = "pm_policy",
	.attrs = pm_policy_attrs,
	.is_visible = amdgpu_pm_policy_attr_visible,
};

static struct amdgpu_device_attr amdgpu_device_attrs[] = {
	AMDGPU_DEVICE_ATTR_RW(power_dpm_state,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RW(power_dpm_force_performance_level,	ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RO(pp_num_states,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RO(pp_cur_state,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RW(pp_force_state,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RW(pp_table,					ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_sclk,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF,
			      .attr_update = pp_dpm_clk_default_attr_update),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_mclk,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF,
			      .attr_update = pp_dpm_clk_default_attr_update),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_socclk,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF,
			      .attr_update = pp_dpm_clk_default_attr_update),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_fclk,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF,
			      .attr_update = pp_dpm_clk_default_attr_update),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_vclk,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF,
			      .attr_update = pp_dpm_clk_default_attr_update),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_vclk1,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF,
			      .attr_update = pp_dpm_clk_default_attr_update),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_dclk,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF,
			      .attr_update = pp_dpm_clk_default_attr_update),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_dclk1,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF,
			      .attr_update = pp_dpm_clk_default_attr_update),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_dcefclk,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF,
			      .attr_update = pp_dpm_dcefclk_attr_update),
	AMDGPU_DEVICE_ATTR_RW(pp_dpm_pcie,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF,
			      .attr_update = pp_dpm_clk_default_attr_update),
	AMDGPU_DEVICE_ATTR_RW(pp_sclk_od,				ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RW(pp_mclk_od,				ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RW(pp_power_profile_mode,			ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RW(pp_od_clk_voltage,			ATTR_FLAG_BASIC,
			      .attr_update = pp_od_clk_voltage_attr_update),
	AMDGPU_DEVICE_ATTR_RO(gpu_busy_percent,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RO(mem_busy_percent,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RO(vcn_busy_percent,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RO(pcie_bw,					ATTR_FLAG_BASIC),
	AMDGPU_DEVICE_ATTR_RW(pp_features,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RO(unique_id,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RW(thermal_throttling_logging,		ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RW(apu_thermal_cap,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RO(gpu_metrics,				ATTR_FLAG_BASIC|ATTR_FLAG_ONEVF),
	AMDGPU_DEVICE_ATTR_RO(smartshift_apu_power,			ATTR_FLAG_BASIC,
			      .attr_update = ss_power_attr_update),
	AMDGPU_DEVICE_ATTR_RO(smartshift_dgpu_power,			ATTR_FLAG_BASIC,
			      .attr_update = ss_power_attr_update),
	AMDGPU_DEVICE_ATTR_RW(smartshift_bias,				ATTR_FLAG_BASIC,
			      .attr_update = ss_bias_attr_update),
	AMDGPU_DEVICE_ATTR_RO(pm_metrics,				ATTR_FLAG_BASIC,
			      .attr_update = amdgpu_pm_metrics_attr_update),
};

static int default_attr_update(struct amdgpu_device *adev, struct amdgpu_device_attr *attr,
			       uint32_t mask, enum amdgpu_device_attr_states *states)
{
	struct device_attribute *dev_attr = &attr->dev_attr;
	enum amdgpu_device_attr_id attr_id = attr->attr_id;
	uint32_t gc_ver = amdgpu_ip_version(adev, GC_HWIP, 0);

	if (!(attr->flags & mask)) {
		*states = ATTR_STATE_UNSUPPORTED;
		return 0;
	}

	if (DEVICE_ATTR_IS(mem_busy_percent)) {
		if ((adev->flags & AMD_IS_APU &&
		     gc_ver != IP_VERSION(9, 4, 3)) ||
		    gc_ver == IP_VERSION(9, 0, 1))
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(vcn_busy_percent)) {
		if (!(gc_ver == IP_VERSION(10, 3, 1) ||
			  gc_ver == IP_VERSION(10, 3, 3) ||
			  gc_ver == IP_VERSION(10, 3, 6) ||
			  gc_ver == IP_VERSION(10, 3, 7) ||
			  gc_ver == IP_VERSION(11, 0, 1) ||
			  gc_ver == IP_VERSION(11, 0, 4) ||
			  gc_ver == IP_VERSION(11, 5, 0)))
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pcie_bw)) {
		/* PCIe Perf counters won't work on APU nodes */
		if (adev->flags & AMD_IS_APU ||
		    !adev->asic_funcs->get_pcie_usage)
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(unique_id)) {
		switch (gc_ver) {
		case IP_VERSION(9, 0, 1):
		case IP_VERSION(9, 4, 0):
		case IP_VERSION(9, 4, 1):
		case IP_VERSION(9, 4, 2):
		case IP_VERSION(9, 4, 3):
		case IP_VERSION(9, 4, 4):
		case IP_VERSION(10, 3, 0):
		case IP_VERSION(11, 0, 0):
		case IP_VERSION(11, 0, 1):
		case IP_VERSION(11, 0, 2):
		case IP_VERSION(11, 0, 3):
			*states = ATTR_STATE_SUPPORTED;
			break;
		default:
			*states = ATTR_STATE_UNSUPPORTED;
		}
	} else if (DEVICE_ATTR_IS(pp_features)) {
		if ((adev->flags & AMD_IS_APU &&
		     gc_ver != IP_VERSION(9, 4, 3)) ||
		    gc_ver < IP_VERSION(9, 0, 0))
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(gpu_metrics)) {
		if (gc_ver < IP_VERSION(9, 1, 0))
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_power_profile_mode)) {
		if (amdgpu_dpm_get_power_profile_mode(adev, NULL) == -EOPNOTSUPP)
			*states = ATTR_STATE_UNSUPPORTED;
		else if ((gc_ver == IP_VERSION(10, 3, 0) ||
			  gc_ver == IP_VERSION(11, 0, 3)) && amdgpu_sriov_vf(adev))
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_mclk_od)) {
		if (amdgpu_dpm_get_mclk_od(adev) == -EOPNOTSUPP)
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(pp_sclk_od)) {
		if (amdgpu_dpm_get_sclk_od(adev) == -EOPNOTSUPP)
			*states = ATTR_STATE_UNSUPPORTED;
	} else if (DEVICE_ATTR_IS(apu_thermal_cap)) {
		u32 limit;

		if (amdgpu_dpm_get_apu_thermal_limit(adev, &limit) ==
		    -EOPNOTSUPP)
			*states = ATTR_STATE_UNSUPPORTED;
	}

	switch (gc_ver) {
	case IP_VERSION(10, 3, 0):
		if (DEVICE_ATTR_IS(power_dpm_force_performance_level) &&
		    amdgpu_sriov_vf(adev)) {
			dev_attr->attr.mode &= ~0222;
			dev_attr->store = NULL;
		}
		break;
	default:
		break;
	}

	return 0;
}


static int amdgpu_device_attr_create(struct amdgpu_device *adev,
				     struct amdgpu_device_attr *attr,
				     uint32_t mask, struct list_head *attr_list)
{
	int ret = 0;
	enum amdgpu_device_attr_states attr_states = ATTR_STATE_SUPPORTED;
	struct amdgpu_device_attr_entry *attr_entry;
	struct device_attribute *dev_attr;
	const char *name;

	int (*attr_update)(struct amdgpu_device *adev, struct amdgpu_device_attr *attr,
			   uint32_t mask, enum amdgpu_device_attr_states *states) = default_attr_update;

	if (!attr)
		return -EINVAL;

	dev_attr = &attr->dev_attr;
	name = dev_attr->attr.name;

	attr_update = attr->attr_update ? attr->attr_update : default_attr_update;

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
	int r, temp = 0;

	if (channel >= PP_TEMP_MAX)
		return -EINVAL;

	switch (channel) {
	case PP_TEMP_JUNCTION:
		/* get current junction temperature */
		r = amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_HOTSPOT_TEMP,
					   (void *)&temp);
		break;
	case PP_TEMP_EDGE:
		/* get current edge temperature */
		r = amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_EDGE_TEMP,
					   (void *)&temp);
		break;
	case PP_TEMP_MEM:
		/* get current memory temperature */
		r = amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_MEM_TEMP,
					   (void *)&temp);
		break;
	default:
		r = -EINVAL;
		break;
	}

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

	ret = amdgpu_dpm_get_fan_control_mode(adev, &pwm_mode);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (ret)
		return -EINVAL;

	return sysfs_emit(buf, "%u\n", pwm_mode);
}

static ssize_t amdgpu_hwmon_set_pwm1_enable(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t count)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	int err, ret;
	u32 pwm_mode;
	int value;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	err = kstrtoint(buf, 10, &value);
	if (err)
		return err;

	if (value == 0)
		pwm_mode = AMD_FAN_CTRL_NONE;
	else if (value == 1)
		pwm_mode = AMD_FAN_CTRL_MANUAL;
	else if (value == 2)
		pwm_mode = AMD_FAN_CTRL_AUTO;
	else
		return -EINVAL;

	ret = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return ret;
	}

	ret = amdgpu_dpm_set_fan_control_mode(adev, pwm_mode);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (ret)
		return -EINVAL;

	return count;
}

static ssize_t amdgpu_hwmon_get_pwm1_min(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return sysfs_emit(buf, "%i\n", 0);
}

static ssize_t amdgpu_hwmon_get_pwm1_max(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return sysfs_emit(buf, "%i\n", 255);
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

	err = kstrtou32(buf, 10, &value);
	if (err)
		return err;

	err = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (err < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return err;
	}

	err = amdgpu_dpm_get_fan_control_mode(adev, &pwm_mode);
	if (err)
		goto out;

	if (pwm_mode != AMD_FAN_CTRL_MANUAL) {
		pr_info("manual fan speed control should be enabled first\n");
		err = -EINVAL;
		goto out;
	}

	err = amdgpu_dpm_set_fan_speed_pwm(adev, value);

out:
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

	err = amdgpu_dpm_get_fan_speed_pwm(adev, &speed);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (err)
		return err;

	return sysfs_emit(buf, "%i\n", speed);
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

	err = amdgpu_dpm_get_fan_speed_rpm(adev, &speed);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (err)
		return err;

	return sysfs_emit(buf, "%i\n", speed);
}

static ssize_t amdgpu_hwmon_get_fan1_min(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	u32 min_rpm = 0;
	int r;

	r = amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_MIN_FAN_RPM,
				   (void *)&min_rpm);

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
	int r;

	r = amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_MAX_FAN_RPM,
				   (void *)&max_rpm);

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

	err = amdgpu_dpm_get_fan_speed_rpm(adev, &rpm);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (err)
		return err;

	return sysfs_emit(buf, "%i\n", rpm);
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

	err = kstrtou32(buf, 10, &value);
	if (err)
		return err;

	err = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (err < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return err;
	}

	err = amdgpu_dpm_get_fan_control_mode(adev, &pwm_mode);
	if (err)
		goto out;

	if (pwm_mode != AMD_FAN_CTRL_MANUAL) {
		err = -ENODATA;
		goto out;
	}

	err = amdgpu_dpm_set_fan_speed_rpm(adev, value);

out:
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

	ret = amdgpu_dpm_get_fan_control_mode(adev, &pwm_mode);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (ret)
		return -EINVAL;

	return sysfs_emit(buf, "%i\n", pwm_mode == AMD_FAN_CTRL_AUTO ? 0 : 1);
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

	err = amdgpu_dpm_set_fan_control_mode(adev, pwm_mode);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (err)
		return -EINVAL;

	return count;
}

static ssize_t amdgpu_hwmon_show_vddgfx(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	u32 vddgfx;
	int r;

	/* get the voltage */
	r = amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_VDDGFX,
				   (void *)&vddgfx);
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
	int r;

	/* only APUs have vddnb */
	if  (!(adev->flags & AMD_IS_APU))
		return -EINVAL;

	/* get the voltage */
	r = amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_VDDNB,
				   (void *)&vddnb);
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

static int amdgpu_hwmon_get_power(struct device *dev,
				  enum amd_pp_sensors sensor)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	unsigned int uw;
	u32 query = 0;
	int r;

	r = amdgpu_hwmon_get_sensor_generic(adev, sensor, (void *)&query);
	if (r)
		return r;

	/* convert to microwatts */
	uw = (query >> 8) * 1000000 + (query & 0xff) * 1000;

	return uw;
}

static ssize_t amdgpu_hwmon_show_power_avg(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	ssize_t val;

	val = amdgpu_hwmon_get_power(dev, AMDGPU_PP_SENSOR_GPU_AVG_POWER);
	if (val < 0)
		return val;

	return sysfs_emit(buf, "%zd\n", val);
}

static ssize_t amdgpu_hwmon_show_power_input(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	ssize_t val;

	val = amdgpu_hwmon_get_power(dev, AMDGPU_PP_SENSOR_GPU_INPUT_POWER);
	if (val < 0)
		return val;

	return sysfs_emit(buf, "%zd\n", val);
}

static ssize_t amdgpu_hwmon_show_power_cap_generic(struct device *dev,
					struct device_attribute *attr,
					char *buf,
					enum pp_power_limit_level pp_limit_level)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	enum pp_power_type power_type = to_sensor_dev_attr(attr)->index;
	uint32_t limit;
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

	r = amdgpu_dpm_get_power_limit(adev, &limit,
				      pp_limit_level, power_type);

	if (!r)
		size = sysfs_emit(buf, "%u\n", limit * 1000000);
	else
		size = sysfs_emit(buf, "\n");

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	return size;
}

static ssize_t amdgpu_hwmon_show_power_cap_min(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return amdgpu_hwmon_show_power_cap_generic(dev, attr, buf, PP_PWR_LIMIT_MIN);
}

static ssize_t amdgpu_hwmon_show_power_cap_max(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return amdgpu_hwmon_show_power_cap_generic(dev, attr, buf, PP_PWR_LIMIT_MAX);

}

static ssize_t amdgpu_hwmon_show_power_cap(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return amdgpu_hwmon_show_power_cap_generic(dev, attr, buf, PP_PWR_LIMIT_CURRENT);

}

static ssize_t amdgpu_hwmon_show_power_cap_default(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return amdgpu_hwmon_show_power_cap_generic(dev, attr, buf, PP_PWR_LIMIT_DEFAULT);

}

static ssize_t amdgpu_hwmon_show_power_label(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
	uint32_t gc_ver = amdgpu_ip_version(adev, GC_HWIP, 0);

	if (gc_ver == IP_VERSION(10, 3, 1))
		return sysfs_emit(buf, "%s\n",
				  to_sensor_dev_attr(attr)->index == PP_PWR_TYPE_FAST ?
				  "fastPPT" : "slowPPT");
	else
		return sysfs_emit(buf, "PPT\n");
}

static ssize_t amdgpu_hwmon_set_power_cap(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct amdgpu_device *adev = dev_get_drvdata(dev);
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

	err = amdgpu_dpm_set_power_limit(adev, value);

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
	int r;

	/* get the sclk */
	r = amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_GFX_SCLK,
				   (void *)&sclk);
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
	int r;

	/* get the sclk */
	r = amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_GFX_MCLK,
				   (void *)&mclk);
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
 * - power1_average: average power used by the SoC in microWatts.  On APUs this includes the CPU.
 *
 * - power1_input: instantaneous power used by the SoC in microWatts.  On APUs this includes the CPU.
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
 * NOTE: DO NOT set the fan speed via "pwm1" and "fan[1-\*]_target" interfaces at the same time.
 *       That will get the former one overridden.
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
static SENSOR_DEVICE_ATTR(power1_input, S_IRUGO, amdgpu_hwmon_show_power_input, NULL, 0);
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
	&sensor_dev_attr_power1_input.dev_attr.attr,
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
	uint32_t gc_ver = amdgpu_ip_version(adev, GC_HWIP, 0);
	uint32_t tmp;

	/* under pp one vf mode manage of hwmon attributes is not supported */
	if (amdgpu_sriov_is_pp_one_vf(adev))
		effective_mode &= ~S_IWUSR;

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
	if ((((adev->flags & AMD_IS_APU) && (adev->family >= AMDGPU_FAMILY_CZ)) ||
	    (gc_ver == IP_VERSION(9, 4, 3) || gc_ver == IP_VERSION(9, 4, 4))) &&
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

	/* mask fan attributes if we have no bindings for this asic to expose */
	if (((amdgpu_dpm_get_fan_speed_pwm(adev, NULL) == -EOPNOTSUPP) &&
	      attr == &sensor_dev_attr_pwm1.dev_attr.attr) || /* can't query fan */
	    ((amdgpu_dpm_get_fan_control_mode(adev, NULL) == -EOPNOTSUPP) &&
	     attr == &sensor_dev_attr_pwm1_enable.dev_attr.attr)) /* can't query state */
		effective_mode &= ~S_IRUGO;

	if (((amdgpu_dpm_set_fan_speed_pwm(adev, U32_MAX) == -EOPNOTSUPP) &&
	      attr == &sensor_dev_attr_pwm1.dev_attr.attr) || /* can't manage fan */
	      ((amdgpu_dpm_set_fan_control_mode(adev, U32_MAX) == -EOPNOTSUPP) &&
	      attr == &sensor_dev_attr_pwm1_enable.dev_attr.attr)) /* can't manage state */
		effective_mode &= ~S_IWUSR;

	/* not implemented yet for APUs other than GC 10.3.1 (vangogh) and 9.4.3 */
	if (((adev->family == AMDGPU_FAMILY_SI) ||
	     ((adev->flags & AMD_IS_APU) && (gc_ver != IP_VERSION(10, 3, 1)) &&
	      (gc_ver != IP_VERSION(9, 4, 3) && gc_ver != IP_VERSION(9, 4, 4)))) &&
	    (attr == &sensor_dev_attr_power1_cap_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_power1_cap_min.dev_attr.attr ||
	     attr == &sensor_dev_attr_power1_cap.dev_attr.attr ||
	     attr == &sensor_dev_attr_power1_cap_default.dev_attr.attr))
		return 0;

	/* not implemented yet for APUs having < GC 9.3.0 (Renoir) */
	if (((adev->family == AMDGPU_FAMILY_SI) ||
	     ((adev->flags & AMD_IS_APU) && (gc_ver < IP_VERSION(9, 3, 0)))) &&
	    (attr == &sensor_dev_attr_power1_average.dev_attr.attr))
		return 0;

	/* not all products support both average and instantaneous */
	if (attr == &sensor_dev_attr_power1_average.dev_attr.attr &&
	    amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_GPU_AVG_POWER, (void *)&tmp) == -EOPNOTSUPP)
		return 0;
	if (attr == &sensor_dev_attr_power1_input.dev_attr.attr &&
	    amdgpu_hwmon_get_sensor_generic(adev, AMDGPU_PP_SENSOR_GPU_INPUT_POWER, (void *)&tmp) == -EOPNOTSUPP)
		return 0;

	/* hide max/min values if we can't both query and manage the fan */
	if (((amdgpu_dpm_set_fan_speed_pwm(adev, U32_MAX) == -EOPNOTSUPP) &&
	      (amdgpu_dpm_get_fan_speed_pwm(adev, NULL) == -EOPNOTSUPP) &&
	      (amdgpu_dpm_set_fan_speed_rpm(adev, U32_MAX) == -EOPNOTSUPP) &&
	      (amdgpu_dpm_get_fan_speed_rpm(adev, NULL) == -EOPNOTSUPP)) &&
	    (attr == &sensor_dev_attr_pwm1_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_pwm1_min.dev_attr.attr))
		return 0;

	if ((amdgpu_dpm_set_fan_speed_rpm(adev, U32_MAX) == -EOPNOTSUPP) &&
	     (amdgpu_dpm_get_fan_speed_rpm(adev, NULL) == -EOPNOTSUPP) &&
	     (attr == &sensor_dev_attr_fan1_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_fan1_min.dev_attr.attr))
		return 0;

	if ((adev->family == AMDGPU_FAMILY_SI ||	/* not implemented yet */
	     adev->family == AMDGPU_FAMILY_KV ||	/* not implemented yet */
	     (gc_ver == IP_VERSION(9, 4, 3) ||
	      gc_ver == IP_VERSION(9, 4, 4))) &&
	    (attr == &sensor_dev_attr_in0_input.dev_attr.attr ||
	     attr == &sensor_dev_attr_in0_label.dev_attr.attr))
		return 0;

	/* only APUs other than gc 9,4,3 have vddnb */
	if ((!(adev->flags & AMD_IS_APU) ||
	     (gc_ver == IP_VERSION(9, 4, 3) ||
	      gc_ver == IP_VERSION(9, 4, 4))) &&
	    (attr == &sensor_dev_attr_in1_input.dev_attr.attr ||
	     attr == &sensor_dev_attr_in1_label.dev_attr.attr))
		return 0;

	/* no mclk on APUs other than gc 9,4,3*/
	if (((adev->flags & AMD_IS_APU) && (gc_ver != IP_VERSION(9, 4, 3))) &&
	    (attr == &sensor_dev_attr_freq2_input.dev_attr.attr ||
	     attr == &sensor_dev_attr_freq2_label.dev_attr.attr))
		return 0;

	if (((adev->flags & AMD_IS_APU) || gc_ver < IP_VERSION(9, 0, 0)) &&
	    (gc_ver != IP_VERSION(9, 4, 3) && gc_ver != IP_VERSION(9, 4, 4)) &&
	    (attr == &sensor_dev_attr_temp2_input.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp2_label.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp2_crit.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp3_input.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp3_label.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp3_crit.dev_attr.attr))
		return 0;

	/* hotspot temperature for gc 9,4,3*/
	if (gc_ver == IP_VERSION(9, 4, 3) ||
	    gc_ver == IP_VERSION(9, 4, 4)) {
		if (attr == &sensor_dev_attr_temp1_input.dev_attr.attr ||
		    attr == &sensor_dev_attr_temp1_emergency.dev_attr.attr ||
		    attr == &sensor_dev_attr_temp1_label.dev_attr.attr)
			return 0;

		if (attr == &sensor_dev_attr_temp2_emergency.dev_attr.attr ||
		    attr == &sensor_dev_attr_temp3_emergency.dev_attr.attr)
			return attr->mode;
	}

	/* only SOC15 dGPUs support hotspot and mem temperatures */
	if (((adev->flags & AMD_IS_APU) || gc_ver < IP_VERSION(9, 0, 0)) &&
	    (attr == &sensor_dev_attr_temp2_crit_hyst.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp3_crit_hyst.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp1_emergency.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp2_emergency.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp3_emergency.dev_attr.attr))
		return 0;

	/* only Vangogh has fast PPT limit and power labels */
	if (!(gc_ver == IP_VERSION(10, 3, 1)) &&
	    (attr == &sensor_dev_attr_power2_average.dev_attr.attr ||
	     attr == &sensor_dev_attr_power2_cap_max.dev_attr.attr ||
	     attr == &sensor_dev_attr_power2_cap_min.dev_attr.attr ||
	     attr == &sensor_dev_attr_power2_cap.dev_attr.attr ||
	     attr == &sensor_dev_attr_power2_cap_default.dev_attr.attr ||
	     attr == &sensor_dev_attr_power2_label.dev_attr.attr))
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

static int amdgpu_retrieve_od_settings(struct amdgpu_device *adev,
				       enum pp_clock_type od_type,
				       char *buf)
{
	int size = 0;
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = pm_runtime_get_sync(adev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(adev->dev);
		return ret;
	}

	size = amdgpu_dpm_print_clock_levels(adev, od_type, buf);
	if (size == 0)
		size = sysfs_emit(buf, "\n");

	pm_runtime_mark_last_busy(adev->dev);
	pm_runtime_put_autosuspend(adev->dev);

	return size;
}

static int parse_input_od_command_lines(const char *buf,
					size_t count,
					u32 *type,
					long *params,
					uint32_t *num_of_params)
{
	const char delimiter[3] = {' ', '\n', '\0'};
	uint32_t parameter_size = 0;
	char buf_cpy[128] = {0};
	char *tmp_str, *sub_str;
	int ret;

	if (count > sizeof(buf_cpy) - 1)
		return -EINVAL;

	memcpy(buf_cpy, buf, count);
	tmp_str = buf_cpy;

	/* skip heading spaces */
	while (isspace(*tmp_str))
		tmp_str++;

	switch (*tmp_str) {
	case 'c':
		*type = PP_OD_COMMIT_DPM_TABLE;
		return 0;
	case 'r':
		params[parameter_size] = *type;
		*num_of_params = 1;
		*type = PP_OD_RESTORE_DEFAULT_TABLE;
		return 0;
	default:
		break;
	}

	while ((sub_str = strsep(&tmp_str, delimiter)) != NULL) {
		if (strlen(sub_str) == 0)
			continue;

		ret = kstrtol(sub_str, 0, &params[parameter_size]);
		if (ret)
			return -EINVAL;
		parameter_size++;

		while (isspace(*tmp_str))
			tmp_str++;
	}

	*num_of_params = parameter_size;

	return 0;
}

static int
amdgpu_distribute_custom_od_settings(struct amdgpu_device *adev,
				     enum PP_OD_DPM_TABLE_COMMAND cmd_type,
				     const char *in_buf,
				     size_t count)
{
	uint32_t parameter_size = 0;
	long parameter[64];
	int ret;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = parse_input_od_command_lines(in_buf,
					   count,
					   &cmd_type,
					   parameter,
					   &parameter_size);
	if (ret)
		return ret;

	ret = pm_runtime_get_sync(adev->dev);
	if (ret < 0)
		goto err_out0;

	ret = amdgpu_dpm_odn_edit_dpm_table(adev,
					    cmd_type,
					    parameter,
					    parameter_size);
	if (ret)
		goto err_out1;

	if (cmd_type == PP_OD_COMMIT_DPM_TABLE) {
		ret = amdgpu_dpm_dispatch_task(adev,
					       AMD_PP_TASK_READJUST_POWER_STATE,
					       NULL);
		if (ret)
			goto err_out1;
	}

	pm_runtime_mark_last_busy(adev->dev);
	pm_runtime_put_autosuspend(adev->dev);

	return count;

err_out1:
	pm_runtime_mark_last_busy(adev->dev);
err_out0:
	pm_runtime_put_autosuspend(adev->dev);

	return ret;
}

/**
 * DOC: fan_curve
 *
 * The amdgpu driver provides a sysfs API for checking and adjusting the fan
 * control curve line.
 *
 * Reading back the file shows you the current settings(temperature in Celsius
 * degree and fan speed in pwm) applied to every anchor point of the curve line
 * and their permitted ranges if changable.
 *
 * Writing a desired string(with the format like "anchor_point_index temperature
 * fan_speed_in_pwm") to the file, change the settings for the specific anchor
 * point accordingly.
 *
 * When you have finished the editing, write "c" (commit) to the file to commit
 * your changes.
 *
 * If you want to reset to the default value, write "r" (reset) to the file to
 * reset them
 *
 * There are two fan control modes supported: auto and manual. With auto mode,
 * PMFW handles the fan speed control(how fan speed reacts to ASIC temperature).
 * While with manual mode, users can set their own fan curve line as what
 * described here. Normally the ASIC is booted up with auto mode. Any
 * settings via this interface will switch the fan control to manual mode
 * implicitly.
 */
static ssize_t fan_curve_show(struct kobject *kobj,
			      struct kobj_attribute *attr,
			      char *buf)
{
	struct od_kobj *container = container_of(kobj, struct od_kobj, kobj);
	struct amdgpu_device *adev = (struct amdgpu_device *)container->priv;

	return (ssize_t)amdgpu_retrieve_od_settings(adev, OD_FAN_CURVE, buf);
}

static ssize_t fan_curve_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf,
			       size_t count)
{
	struct od_kobj *container = container_of(kobj, struct od_kobj, kobj);
	struct amdgpu_device *adev = (struct amdgpu_device *)container->priv;

	return (ssize_t)amdgpu_distribute_custom_od_settings(adev,
							     PP_OD_EDIT_FAN_CURVE,
							     buf,
							     count);
}

static umode_t fan_curve_visible(struct amdgpu_device *adev)
{
	umode_t umode = 0000;

	if (adev->pm.od_feature_mask & OD_OPS_SUPPORT_FAN_CURVE_RETRIEVE)
		umode |= S_IRUSR | S_IRGRP | S_IROTH;

	if (adev->pm.od_feature_mask & OD_OPS_SUPPORT_FAN_CURVE_SET)
		umode |= S_IWUSR;

	return umode;
}

/**
 * DOC: acoustic_limit_rpm_threshold
 *
 * The amdgpu driver provides a sysfs API for checking and adjusting the
 * acoustic limit in RPM for fan control.
 *
 * Reading back the file shows you the current setting and the permitted
 * ranges if changable.
 *
 * Writing an integer to the file, change the setting accordingly.
 *
 * When you have finished the editing, write "c" (commit) to the file to commit
 * your changes.
 *
 * If you want to reset to the default value, write "r" (reset) to the file to
 * reset them
 *
 * This setting works under auto fan control mode only. It adjusts the PMFW's
 * behavior about the maximum speed in RPM the fan can spin. Setting via this
 * interface will switch the fan control to auto mode implicitly.
 */
static ssize_t acoustic_limit_threshold_show(struct kobject *kobj,
					     struct kobj_attribute *attr,
					     char *buf)
{
	struct od_kobj *container = container_of(kobj, struct od_kobj, kobj);
	struct amdgpu_device *adev = (struct amdgpu_device *)container->priv;

	return (ssize_t)amdgpu_retrieve_od_settings(adev, OD_ACOUSTIC_LIMIT, buf);
}

static ssize_t acoustic_limit_threshold_store(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      const char *buf,
					      size_t count)
{
	struct od_kobj *container = container_of(kobj, struct od_kobj, kobj);
	struct amdgpu_device *adev = (struct amdgpu_device *)container->priv;

	return (ssize_t)amdgpu_distribute_custom_od_settings(adev,
							     PP_OD_EDIT_ACOUSTIC_LIMIT,
							     buf,
							     count);
}

static umode_t acoustic_limit_threshold_visible(struct amdgpu_device *adev)
{
	umode_t umode = 0000;

	if (adev->pm.od_feature_mask & OD_OPS_SUPPORT_ACOUSTIC_LIMIT_THRESHOLD_RETRIEVE)
		umode |= S_IRUSR | S_IRGRP | S_IROTH;

	if (adev->pm.od_feature_mask & OD_OPS_SUPPORT_ACOUSTIC_LIMIT_THRESHOLD_SET)
		umode |= S_IWUSR;

	return umode;
}

/**
 * DOC: acoustic_target_rpm_threshold
 *
 * The amdgpu driver provides a sysfs API for checking and adjusting the
 * acoustic target in RPM for fan control.
 *
 * Reading back the file shows you the current setting and the permitted
 * ranges if changable.
 *
 * Writing an integer to the file, change the setting accordingly.
 *
 * When you have finished the editing, write "c" (commit) to the file to commit
 * your changes.
 *
 * If you want to reset to the default value, write "r" (reset) to the file to
 * reset them
 *
 * This setting works under auto fan control mode only. It can co-exist with
 * other settings which can work also under auto mode. It adjusts the PMFW's
 * behavior about the maximum speed in RPM the fan can spin when ASIC
 * temperature is not greater than target temperature. Setting via this
 * interface will switch the fan control to auto mode implicitly.
 */
static ssize_t acoustic_target_threshold_show(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      char *buf)
{
	struct od_kobj *container = container_of(kobj, struct od_kobj, kobj);
	struct amdgpu_device *adev = (struct amdgpu_device *)container->priv;

	return (ssize_t)amdgpu_retrieve_od_settings(adev, OD_ACOUSTIC_TARGET, buf);
}

static ssize_t acoustic_target_threshold_store(struct kobject *kobj,
					       struct kobj_attribute *attr,
					       const char *buf,
					       size_t count)
{
	struct od_kobj *container = container_of(kobj, struct od_kobj, kobj);
	struct amdgpu_device *adev = (struct amdgpu_device *)container->priv;

	return (ssize_t)amdgpu_distribute_custom_od_settings(adev,
							     PP_OD_EDIT_ACOUSTIC_TARGET,
							     buf,
							     count);
}

static umode_t acoustic_target_threshold_visible(struct amdgpu_device *adev)
{
	umode_t umode = 0000;

	if (adev->pm.od_feature_mask & OD_OPS_SUPPORT_ACOUSTIC_TARGET_THRESHOLD_RETRIEVE)
		umode |= S_IRUSR | S_IRGRP | S_IROTH;

	if (adev->pm.od_feature_mask & OD_OPS_SUPPORT_ACOUSTIC_TARGET_THRESHOLD_SET)
		umode |= S_IWUSR;

	return umode;
}

/**
 * DOC: fan_target_temperature
 *
 * The amdgpu driver provides a sysfs API for checking and adjusting the
 * target tempeature in Celsius degree for fan control.
 *
 * Reading back the file shows you the current setting and the permitted
 * ranges if changable.
 *
 * Writing an integer to the file, change the setting accordingly.
 *
 * When you have finished the editing, write "c" (commit) to the file to commit
 * your changes.
 *
 * If you want to reset to the default value, write "r" (reset) to the file to
 * reset them
 *
 * This setting works under auto fan control mode only. It can co-exist with
 * other settings which can work also under auto mode. Paring with the
 * acoustic_target_rpm_threshold setting, they define the maximum speed in
 * RPM the fan can spin when ASIC temperature is not greater than target
 * temperature. Setting via this interface will switch the fan control to
 * auto mode implicitly.
 */
static ssize_t fan_target_temperature_show(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   char *buf)
{
	struct od_kobj *container = container_of(kobj, struct od_kobj, kobj);
	struct amdgpu_device *adev = (struct amdgpu_device *)container->priv;

	return (ssize_t)amdgpu_retrieve_od_settings(adev, OD_FAN_TARGET_TEMPERATURE, buf);
}

static ssize_t fan_target_temperature_store(struct kobject *kobj,
					    struct kobj_attribute *attr,
					    const char *buf,
					    size_t count)
{
	struct od_kobj *container = container_of(kobj, struct od_kobj, kobj);
	struct amdgpu_device *adev = (struct amdgpu_device *)container->priv;

	return (ssize_t)amdgpu_distribute_custom_od_settings(adev,
							     PP_OD_EDIT_FAN_TARGET_TEMPERATURE,
							     buf,
							     count);
}

static umode_t fan_target_temperature_visible(struct amdgpu_device *adev)
{
	umode_t umode = 0000;

	if (adev->pm.od_feature_mask & OD_OPS_SUPPORT_FAN_TARGET_TEMPERATURE_RETRIEVE)
		umode |= S_IRUSR | S_IRGRP | S_IROTH;

	if (adev->pm.od_feature_mask & OD_OPS_SUPPORT_FAN_TARGET_TEMPERATURE_SET)
		umode |= S_IWUSR;

	return umode;
}

/**
 * DOC: fan_minimum_pwm
 *
 * The amdgpu driver provides a sysfs API for checking and adjusting the
 * minimum fan speed in PWM.
 *
 * Reading back the file shows you the current setting and the permitted
 * ranges if changable.
 *
 * Writing an integer to the file, change the setting accordingly.
 *
 * When you have finished the editing, write "c" (commit) to the file to commit
 * your changes.
 *
 * If you want to reset to the default value, write "r" (reset) to the file to
 * reset them
 *
 * This setting works under auto fan control mode only. It can co-exist with
 * other settings which can work also under auto mode. It adjusts the PMFW's
 * behavior about the minimum fan speed in PWM the fan should spin. Setting
 * via this interface will switch the fan control to auto mode implicitly.
 */
static ssize_t fan_minimum_pwm_show(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    char *buf)
{
	struct od_kobj *container = container_of(kobj, struct od_kobj, kobj);
	struct amdgpu_device *adev = (struct amdgpu_device *)container->priv;

	return (ssize_t)amdgpu_retrieve_od_settings(adev, OD_FAN_MINIMUM_PWM, buf);
}

static ssize_t fan_minimum_pwm_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf,
				     size_t count)
{
	struct od_kobj *container = container_of(kobj, struct od_kobj, kobj);
	struct amdgpu_device *adev = (struct amdgpu_device *)container->priv;

	return (ssize_t)amdgpu_distribute_custom_od_settings(adev,
							     PP_OD_EDIT_FAN_MINIMUM_PWM,
							     buf,
							     count);
}

static umode_t fan_minimum_pwm_visible(struct amdgpu_device *adev)
{
	umode_t umode = 0000;

	if (adev->pm.od_feature_mask & OD_OPS_SUPPORT_FAN_MINIMUM_PWM_RETRIEVE)
		umode |= S_IRUSR | S_IRGRP | S_IROTH;

	if (adev->pm.od_feature_mask & OD_OPS_SUPPORT_FAN_MINIMUM_PWM_SET)
		umode |= S_IWUSR;

	return umode;
}

static struct od_feature_set amdgpu_od_set = {
	.containers = {
		[0] = {
			.name = "fan_ctrl",
			.sub_feature = {
				[0] = {
					.name = "fan_curve",
					.ops = {
						.is_visible = fan_curve_visible,
						.show = fan_curve_show,
						.store = fan_curve_store,
					},
				},
				[1] = {
					.name = "acoustic_limit_rpm_threshold",
					.ops = {
						.is_visible = acoustic_limit_threshold_visible,
						.show = acoustic_limit_threshold_show,
						.store = acoustic_limit_threshold_store,
					},
				},
				[2] = {
					.name = "acoustic_target_rpm_threshold",
					.ops = {
						.is_visible = acoustic_target_threshold_visible,
						.show = acoustic_target_threshold_show,
						.store = acoustic_target_threshold_store,
					},
				},
				[3] = {
					.name = "fan_target_temperature",
					.ops = {
						.is_visible = fan_target_temperature_visible,
						.show = fan_target_temperature_show,
						.store = fan_target_temperature_store,
					},
				},
				[4] = {
					.name = "fan_minimum_pwm",
					.ops = {
						.is_visible = fan_minimum_pwm_visible,
						.show = fan_minimum_pwm_show,
						.store = fan_minimum_pwm_store,
					},
				},
			},
		},
	},
};

static void od_kobj_release(struct kobject *kobj)
{
	struct od_kobj *od_kobj = container_of(kobj, struct od_kobj, kobj);

	kfree(od_kobj);
}

static const struct kobj_type od_ktype = {
	.release	= od_kobj_release,
	.sysfs_ops	= &kobj_sysfs_ops,
};

static void amdgpu_od_set_fini(struct amdgpu_device *adev)
{
	struct od_kobj *container, *container_next;
	struct od_attribute *attribute, *attribute_next;

	if (list_empty(&adev->pm.od_kobj_list))
		return;

	list_for_each_entry_safe(container, container_next,
				 &adev->pm.od_kobj_list, entry) {
		list_del(&container->entry);

		list_for_each_entry_safe(attribute, attribute_next,
					 &container->attribute, entry) {
			list_del(&attribute->entry);
			sysfs_remove_file(&container->kobj,
					  &attribute->attribute.attr);
			kfree(attribute);
		}

		kobject_put(&container->kobj);
	}
}

static bool amdgpu_is_od_feature_supported(struct amdgpu_device *adev,
					   struct od_feature_ops *feature_ops)
{
	umode_t mode;

	if (!feature_ops->is_visible)
		return false;

	/*
	 * If the feature has no user read and write mode set,
	 * we can assume the feature is actually not supported.(?)
	 * And the revelant sysfs interface should not be exposed.
	 */
	mode = feature_ops->is_visible(adev);
	if (mode & (S_IRUSR | S_IWUSR))
		return true;

	return false;
}

static bool amdgpu_od_is_self_contained(struct amdgpu_device *adev,
					struct od_feature_container *container)
{
	int i;

	/*
	 * If there is no valid entry within the container, the container
	 * is recognized as a self contained container. And the valid entry
	 * here means it has a valid naming and it is visible/supported by
	 * the ASIC.
	 */
	for (i = 0; i < ARRAY_SIZE(container->sub_feature); i++) {
		if (container->sub_feature[i].name &&
		    amdgpu_is_od_feature_supported(adev,
			&container->sub_feature[i].ops))
			return false;
	}

	return true;
}

static int amdgpu_od_set_init(struct amdgpu_device *adev)
{
	struct od_kobj *top_set, *sub_set;
	struct od_attribute *attribute;
	struct od_feature_container *container;
	struct od_feature_item *feature;
	int i, j;
	int ret;

	/* Setup the top `gpu_od` directory which holds all other OD interfaces */
	top_set = kzalloc(sizeof(*top_set), GFP_KERNEL);
	if (!top_set)
		return -ENOMEM;
	list_add(&top_set->entry, &adev->pm.od_kobj_list);

	ret = kobject_init_and_add(&top_set->kobj,
				   &od_ktype,
				   &adev->dev->kobj,
				   "%s",
				   "gpu_od");
	if (ret)
		goto err_out;
	INIT_LIST_HEAD(&top_set->attribute);
	top_set->priv = adev;

	for (i = 0; i < ARRAY_SIZE(amdgpu_od_set.containers); i++) {
		container = &amdgpu_od_set.containers[i];

		if (!container->name)
			continue;

		/*
		 * If there is valid entries within the container, the container
		 * will be presented as a sub directory and all its holding entries
		 * will be presented as plain files under it.
		 * While if there is no valid entry within the container, the container
		 * itself will be presented as a plain file under top `gpu_od` directory.
		 */
		if (amdgpu_od_is_self_contained(adev, container)) {
			if (!amdgpu_is_od_feature_supported(adev,
			     &container->ops))
				continue;

			/*
			 * The container is presented as a plain file under top `gpu_od`
			 * directory.
			 */
			attribute = kzalloc(sizeof(*attribute), GFP_KERNEL);
			if (!attribute) {
				ret = -ENOMEM;
				goto err_out;
			}
			list_add(&attribute->entry, &top_set->attribute);

			attribute->attribute.attr.mode =
					container->ops.is_visible(adev);
			attribute->attribute.attr.name = container->name;
			attribute->attribute.show =
					container->ops.show;
			attribute->attribute.store =
					container->ops.store;
			ret = sysfs_create_file(&top_set->kobj,
						&attribute->attribute.attr);
			if (ret)
				goto err_out;
		} else {
			/* The container is presented as a sub directory. */
			sub_set = kzalloc(sizeof(*sub_set), GFP_KERNEL);
			if (!sub_set) {
				ret = -ENOMEM;
				goto err_out;
			}
			list_add(&sub_set->entry, &adev->pm.od_kobj_list);

			ret = kobject_init_and_add(&sub_set->kobj,
						   &od_ktype,
						   &top_set->kobj,
						   "%s",
						   container->name);
			if (ret)
				goto err_out;
			INIT_LIST_HEAD(&sub_set->attribute);
			sub_set->priv = adev;

			for (j = 0; j < ARRAY_SIZE(container->sub_feature); j++) {
				feature = &container->sub_feature[j];
				if (!feature->name)
					continue;

				if (!amdgpu_is_od_feature_supported(adev,
				     &feature->ops))
					continue;

				/*
				 * With the container presented as a sub directory, the entry within
				 * it is presented as a plain file under the sub directory.
				 */
				attribute = kzalloc(sizeof(*attribute), GFP_KERNEL);
				if (!attribute) {
					ret = -ENOMEM;
					goto err_out;
				}
				list_add(&attribute->entry, &sub_set->attribute);

				attribute->attribute.attr.mode =
						feature->ops.is_visible(adev);
				attribute->attribute.attr.name = feature->name;
				attribute->attribute.show =
						feature->ops.show;
				attribute->attribute.store =
						feature->ops.store;
				ret = sysfs_create_file(&sub_set->kobj,
							&attribute->attribute.attr);
				if (ret)
					goto err_out;
			}
		}
	}

	/*
	 * If gpu_od is the only member in the list, that means gpu_od is an
	 * empty directory, so remove it.
	 */
	if (list_is_singular(&adev->pm.od_kobj_list))
		goto err_out;

	return 0;

err_out:
	amdgpu_od_set_fini(adev);

	return ret;
}

int amdgpu_pm_sysfs_init(struct amdgpu_device *adev)
{
	enum amdgpu_sriov_vf_mode mode;
	uint32_t mask = 0;
	int ret;

	if (adev->pm.sysfs_initialized)
		return 0;

	INIT_LIST_HEAD(&adev->pm.pm_attr_list);

	if (adev->pm.dpm_enabled == 0)
		return 0;

	mode = amdgpu_virt_get_sriov_vf_mode(adev);

	/* under multi-vf mode, the hwmon attributes are all not supported */
	if (mode != SRIOV_VF_MODE_MULTI_VF) {
		adev->pm.int_hwmon_dev = hwmon_device_register_with_groups(adev->dev,
									DRIVER_NAME, adev,
									hwmon_groups);
		if (IS_ERR(adev->pm.int_hwmon_dev)) {
			ret = PTR_ERR(adev->pm.int_hwmon_dev);
			dev_err(adev->dev, "Unable to register hwmon device: %d\n", ret);
			return ret;
		}
	}

	switch (mode) {
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
		goto err_out0;

	if (amdgpu_dpm_is_overdrive_supported(adev)) {
		ret = amdgpu_od_set_init(adev);
		if (ret)
			goto err_out1;
	} else if (adev->pm.pp_feature & PP_OVERDRIVE_MASK) {
		dev_info(adev->dev, "overdrive feature is not supported\n");
	}

	if (amdgpu_dpm_get_pm_policy_info(adev, PP_PM_POLICY_NONE, NULL) !=
	    -EOPNOTSUPP) {
		ret = devm_device_add_group(adev->dev,
					    &amdgpu_pm_policy_attr_group);
		if (ret)
			goto err_out0;
	}

	adev->pm.sysfs_initialized = true;

	return 0;

err_out1:
	amdgpu_device_attr_remove_groups(adev, &adev->pm.pm_attr_list);
err_out0:
	if (adev->pm.int_hwmon_dev)
		hwmon_device_unregister(adev->pm.int_hwmon_dev);

	return ret;
}

void amdgpu_pm_sysfs_fini(struct amdgpu_device *adev)
{
	amdgpu_od_set_fini(adev);

	if (adev->pm.int_hwmon_dev)
		hwmon_device_unregister(adev->pm.int_hwmon_dev);

	amdgpu_device_attr_remove_groups(adev, &adev->pm.pm_attr_list);
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)

static void amdgpu_debugfs_prints_cpu_info(struct seq_file *m,
					   struct amdgpu_device *adev)
{
	uint16_t *p_val;
	uint32_t size;
	int i;
	uint32_t num_cpu_cores = amdgpu_dpm_get_num_cpu_cores(adev);

	if (amdgpu_dpm_is_cclk_dpm_supported(adev)) {
		p_val = kcalloc(num_cpu_cores, sizeof(uint16_t),
				GFP_KERNEL);

		if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_CPU_CLK,
					    (void *)p_val, &size)) {
			for (i = 0; i < num_cpu_cores; i++)
				seq_printf(m, "\t%u MHz (CPU%d)\n",
					   *(p_val + i), i);
		}

		kfree(p_val);
	}
}

static int amdgpu_debugfs_pm_info_pp(struct seq_file *m, struct amdgpu_device *adev)
{
	uint32_t mp1_ver = amdgpu_ip_version(adev, MP1_HWIP, 0);
	uint32_t gc_ver = amdgpu_ip_version(adev, GC_HWIP, 0);
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
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GPU_AVG_POWER, (void *)&query, &size)) {
		if (adev->flags & AMD_IS_APU)
			seq_printf(m, "\t%u.%02u W (average SoC including CPU)\n", query >> 8, query & 0xff);
		else
			seq_printf(m, "\t%u.%02u W (average SoC)\n", query >> 8, query & 0xff);
	}
	size = sizeof(uint32_t);
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_GPU_INPUT_POWER, (void *)&query, &size)) {
		if (adev->flags & AMD_IS_APU)
			seq_printf(m, "\t%u.%02u W (current SoC including CPU)\n", query >> 8, query & 0xff);
		else
			seq_printf(m, "\t%u.%02u W (current SoC)\n", query >> 8, query & 0xff);
	}
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
	/* VCN Load */
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_VCN_LOAD, (void *)&value, &size))
		seq_printf(m, "VCN Load: %u %%\n", value);

	seq_printf(m, "\n");

	/* SMC feature mask */
	if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_ENABLED_SMC_FEATURES_MASK, (void *)&value64, &size))
		seq_printf(m, "SMC Feature Mask: 0x%016llx\n", value64);

	/* ASICs greater than CHIP_VEGA20 supports these sensors */
	if (gc_ver != IP_VERSION(9, 4, 0) && mp1_ver > IP_VERSION(9, 0, 0)) {
		/* VCN clocks */
		if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_VCN_POWER_STATE, (void *)&value, &size)) {
			if (!value) {
				seq_printf(m, "VCN: Powered down\n");
			} else {
				seq_printf(m, "VCN: Powered up\n");
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
				seq_printf(m, "UVD: Powered down\n");
			} else {
				seq_printf(m, "UVD: Powered up\n");
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
				seq_printf(m, "VCE: Powered down\n");
			} else {
				seq_printf(m, "VCE: Powered up\n");
				if (!amdgpu_dpm_read_sensor(adev, AMDGPU_PP_SENSOR_VCE_ECCLK, (void *)&value, &size))
					seq_printf(m, "\t%u MHz (ECCLK)\n", value/100);
			}
		}
	}

	return 0;
}

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
	{AMD_CG_SUPPORT_REPEATER_FGCG, "Repeater Fine Grain Clock Gating"},
	{AMD_CG_SUPPORT_GFX_PERF_CLK, "Perfmon Clock Gating"},
	{AMD_CG_SUPPORT_ATHUB_MGCG, "Address Translation Hub Medium Grain Clock Gating"},
	{AMD_CG_SUPPORT_ATHUB_LS, "Address Translation Hub Light Sleep"},
	{0, NULL},
};

static void amdgpu_parse_cg_state(struct seq_file *m, u64 flags)
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
	u64 flags = 0;
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

	if (amdgpu_dpm_debugfs_print_current_performance_level(adev, m)) {
		r = amdgpu_debugfs_pm_info_pp(m, adev);
		if (r)
			goto out;
	}

	amdgpu_device_ip_get_clockgating_state(adev, &flags);

	seq_printf(m, "Clock Gating Flags Mask: 0x%llx\n", flags);
	amdgpu_parse_cg_state(m, flags);
	seq_printf(m, "\n");

out:
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return r;
}

DEFINE_SHOW_ATTRIBUTE(amdgpu_debugfs_pm_info);

/*
 * amdgpu_pm_priv_buffer_read - Read memory region allocated to FW
 *
 * Reads debug memory region allocated to PMFW
 */
static ssize_t amdgpu_pm_prv_buffer_read(struct file *f, char __user *buf,
					 size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	size_t smu_prv_buf_size;
	void *smu_prv_buf;
	int ret = 0;

	if (amdgpu_in_reset(adev))
		return -EPERM;
	if (adev->in_suspend && !adev->in_runpm)
		return -EPERM;

	ret = amdgpu_dpm_get_smu_prv_buf_details(adev, &smu_prv_buf, &smu_prv_buf_size);
	if (ret)
		return ret;

	if (!smu_prv_buf || !smu_prv_buf_size)
		return -EINVAL;

	return simple_read_from_buffer(buf, size, pos, smu_prv_buf,
				       smu_prv_buf_size);
}

static const struct file_operations amdgpu_debugfs_pm_prv_buffer_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = amdgpu_pm_prv_buffer_read,
	.llseek = default_llseek,
};

#endif

void amdgpu_debugfs_pm_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *root = minor->debugfs_root;

	if (!adev->pm.dpm_enabled)
		return;

	debugfs_create_file("amdgpu_pm_info", 0444, root, adev,
			    &amdgpu_debugfs_pm_info_fops);

	if (adev->pm.smu_prv_buffer_size > 0)
		debugfs_create_file_size("amdgpu_pm_prv_buffer", 0444, root,
					 adev,
					 &amdgpu_debugfs_pm_prv_buffer_fops,
					 adev->pm.smu_prv_buffer_size);

	amdgpu_dpm_stb_debug_fs_init(adev);
#endif
}

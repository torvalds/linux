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
#include "radeon.h"
#include "avivod.h"
#include "atom.h"
#include <linux/power_supply.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#define RADEON_IDLE_LOOP_MS 100
#define RADEON_RECLOCK_DELAY_MS 200
#define RADEON_WAIT_VBLANK_TIMEOUT 200

static const char *radeon_pm_state_type_name[5] = {
	"",
	"Powersave",
	"Battery",
	"Balanced",
	"Performance",
};

static void radeon_dynpm_idle_work_handler(struct work_struct *work);
static int radeon_debugfs_pm_init(struct radeon_device *rdev);
static bool radeon_pm_in_vbl(struct radeon_device *rdev);
static bool radeon_pm_debug_check_in_vbl(struct radeon_device *rdev, bool finish);
static void radeon_pm_update_profile(struct radeon_device *rdev);
static void radeon_pm_set_clocks(struct radeon_device *rdev);

int radeon_pm_get_type_index(struct radeon_device *rdev,
			     enum radeon_pm_state_type ps_type,
			     int instance)
{
	int i;
	int found_instance = -1;

	for (i = 0; i < rdev->pm.num_power_states; i++) {
		if (rdev->pm.power_state[i].type == ps_type) {
			found_instance++;
			if (found_instance == instance)
				return i;
		}
	}
	/* return default if no match */
	return rdev->pm.default_power_state_index;
}

void radeon_pm_acpi_event_handler(struct radeon_device *rdev)
{
	if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled) {
		mutex_lock(&rdev->pm.mutex);
		if (power_supply_is_system_supplied() > 0)
			rdev->pm.dpm.ac_power = true;
		else
			rdev->pm.dpm.ac_power = false;
		if (rdev->family == CHIP_ARUBA) {
			if (rdev->asic->dpm.enable_bapm)
				radeon_dpm_enable_bapm(rdev, rdev->pm.dpm.ac_power);
		}
		mutex_unlock(&rdev->pm.mutex);
        } else if (rdev->pm.pm_method == PM_METHOD_PROFILE) {
		if (rdev->pm.profile == PM_PROFILE_AUTO) {
			mutex_lock(&rdev->pm.mutex);
			radeon_pm_update_profile(rdev);
			radeon_pm_set_clocks(rdev);
			mutex_unlock(&rdev->pm.mutex);
		}
	}
}

static void radeon_pm_update_profile(struct radeon_device *rdev)
{
	switch (rdev->pm.profile) {
	case PM_PROFILE_DEFAULT:
		rdev->pm.profile_index = PM_PROFILE_DEFAULT_IDX;
		break;
	case PM_PROFILE_AUTO:
		if (power_supply_is_system_supplied() > 0) {
			if (rdev->pm.active_crtc_count > 1)
				rdev->pm.profile_index = PM_PROFILE_HIGH_MH_IDX;
			else
				rdev->pm.profile_index = PM_PROFILE_HIGH_SH_IDX;
		} else {
			if (rdev->pm.active_crtc_count > 1)
				rdev->pm.profile_index = PM_PROFILE_MID_MH_IDX;
			else
				rdev->pm.profile_index = PM_PROFILE_MID_SH_IDX;
		}
		break;
	case PM_PROFILE_LOW:
		if (rdev->pm.active_crtc_count > 1)
			rdev->pm.profile_index = PM_PROFILE_LOW_MH_IDX;
		else
			rdev->pm.profile_index = PM_PROFILE_LOW_SH_IDX;
		break;
	case PM_PROFILE_MID:
		if (rdev->pm.active_crtc_count > 1)
			rdev->pm.profile_index = PM_PROFILE_MID_MH_IDX;
		else
			rdev->pm.profile_index = PM_PROFILE_MID_SH_IDX;
		break;
	case PM_PROFILE_HIGH:
		if (rdev->pm.active_crtc_count > 1)
			rdev->pm.profile_index = PM_PROFILE_HIGH_MH_IDX;
		else
			rdev->pm.profile_index = PM_PROFILE_HIGH_SH_IDX;
		break;
	}

	if (rdev->pm.active_crtc_count == 0) {
		rdev->pm.requested_power_state_index =
			rdev->pm.profiles[rdev->pm.profile_index].dpms_off_ps_idx;
		rdev->pm.requested_clock_mode_index =
			rdev->pm.profiles[rdev->pm.profile_index].dpms_off_cm_idx;
	} else {
		rdev->pm.requested_power_state_index =
			rdev->pm.profiles[rdev->pm.profile_index].dpms_on_ps_idx;
		rdev->pm.requested_clock_mode_index =
			rdev->pm.profiles[rdev->pm.profile_index].dpms_on_cm_idx;
	}
}

static void radeon_unmap_vram_bos(struct radeon_device *rdev)
{
	struct radeon_bo *bo, *n;

	if (list_empty(&rdev->gem.objects))
		return;

	list_for_each_entry_safe(bo, n, &rdev->gem.objects, list) {
		if (bo->tbo.mem.mem_type == TTM_PL_VRAM)
			ttm_bo_unmap_virtual(&bo->tbo);
	}
}

static void radeon_sync_with_vblank(struct radeon_device *rdev)
{
	if (rdev->pm.active_crtcs) {
		rdev->pm.vblank_sync = false;
		wait_event_timeout(
			rdev->irq.vblank_queue, rdev->pm.vblank_sync,
			msecs_to_jiffies(RADEON_WAIT_VBLANK_TIMEOUT));
	}
}

static void radeon_set_power_state(struct radeon_device *rdev)
{
	u32 sclk, mclk;
	bool misc_after = false;

	if ((rdev->pm.requested_clock_mode_index == rdev->pm.current_clock_mode_index) &&
	    (rdev->pm.requested_power_state_index == rdev->pm.current_power_state_index))
		return;

	if (radeon_gui_idle(rdev)) {
		sclk = rdev->pm.power_state[rdev->pm.requested_power_state_index].
			clock_info[rdev->pm.requested_clock_mode_index].sclk;
		if (sclk > rdev->pm.default_sclk)
			sclk = rdev->pm.default_sclk;

		/* starting with BTC, there is one state that is used for both
		 * MH and SH.  Difference is that we always use the high clock index for
		 * mclk and vddci.
		 */
		if ((rdev->pm.pm_method == PM_METHOD_PROFILE) &&
		    (rdev->family >= CHIP_BARTS) &&
		    rdev->pm.active_crtc_count &&
		    ((rdev->pm.profile_index == PM_PROFILE_MID_MH_IDX) ||
		     (rdev->pm.profile_index == PM_PROFILE_LOW_MH_IDX)))
			mclk = rdev->pm.power_state[rdev->pm.requested_power_state_index].
				clock_info[rdev->pm.profiles[PM_PROFILE_HIGH_MH_IDX].dpms_on_cm_idx].mclk;
		else
			mclk = rdev->pm.power_state[rdev->pm.requested_power_state_index].
				clock_info[rdev->pm.requested_clock_mode_index].mclk;

		if (mclk > rdev->pm.default_mclk)
			mclk = rdev->pm.default_mclk;

		/* upvolt before raising clocks, downvolt after lowering clocks */
		if (sclk < rdev->pm.current_sclk)
			misc_after = true;

		radeon_sync_with_vblank(rdev);

		if (rdev->pm.pm_method == PM_METHOD_DYNPM) {
			if (!radeon_pm_in_vbl(rdev))
				return;
		}

		radeon_pm_prepare(rdev);

		if (!misc_after)
			/* voltage, pcie lanes, etc.*/
			radeon_pm_misc(rdev);

		/* set engine clock */
		if (sclk != rdev->pm.current_sclk) {
			radeon_pm_debug_check_in_vbl(rdev, false);
			radeon_set_engine_clock(rdev, sclk);
			radeon_pm_debug_check_in_vbl(rdev, true);
			rdev->pm.current_sclk = sclk;
			DRM_DEBUG_DRIVER("Setting: e: %d\n", sclk);
		}

		/* set memory clock */
		if (rdev->asic->pm.set_memory_clock && (mclk != rdev->pm.current_mclk)) {
			radeon_pm_debug_check_in_vbl(rdev, false);
			radeon_set_memory_clock(rdev, mclk);
			radeon_pm_debug_check_in_vbl(rdev, true);
			rdev->pm.current_mclk = mclk;
			DRM_DEBUG_DRIVER("Setting: m: %d\n", mclk);
		}

		if (misc_after)
			/* voltage, pcie lanes, etc.*/
			radeon_pm_misc(rdev);

		radeon_pm_finish(rdev);

		rdev->pm.current_power_state_index = rdev->pm.requested_power_state_index;
		rdev->pm.current_clock_mode_index = rdev->pm.requested_clock_mode_index;
	} else
		DRM_DEBUG_DRIVER("pm: GUI not idle!!!\n");
}

static void radeon_pm_set_clocks(struct radeon_device *rdev)
{
	int i, r;

	/* no need to take locks, etc. if nothing's going to change */
	if ((rdev->pm.requested_clock_mode_index == rdev->pm.current_clock_mode_index) &&
	    (rdev->pm.requested_power_state_index == rdev->pm.current_power_state_index))
		return;

	mutex_lock(&rdev->ddev->struct_mutex);
	down_write(&rdev->pm.mclk_lock);
	mutex_lock(&rdev->ring_lock);

	/* wait for the rings to drain */
	for (i = 0; i < RADEON_NUM_RINGS; i++) {
		struct radeon_ring *ring = &rdev->ring[i];
		if (!ring->ready) {
			continue;
		}
		r = radeon_fence_wait_empty(rdev, i);
		if (r) {
			/* needs a GPU reset dont reset here */
			mutex_unlock(&rdev->ring_lock);
			up_write(&rdev->pm.mclk_lock);
			mutex_unlock(&rdev->ddev->struct_mutex);
			return;
		}
	}

	radeon_unmap_vram_bos(rdev);

	if (rdev->irq.installed) {
		for (i = 0; i < rdev->num_crtc; i++) {
			if (rdev->pm.active_crtcs & (1 << i)) {
				rdev->pm.req_vblank |= (1 << i);
				drm_vblank_get(rdev->ddev, i);
			}
		}
	}

	radeon_set_power_state(rdev);

	if (rdev->irq.installed) {
		for (i = 0; i < rdev->num_crtc; i++) {
			if (rdev->pm.req_vblank & (1 << i)) {
				rdev->pm.req_vblank &= ~(1 << i);
				drm_vblank_put(rdev->ddev, i);
			}
		}
	}

	/* update display watermarks based on new power state */
	radeon_update_bandwidth_info(rdev);
	if (rdev->pm.active_crtc_count)
		radeon_bandwidth_update(rdev);

	rdev->pm.dynpm_planned_action = DYNPM_ACTION_NONE;

	mutex_unlock(&rdev->ring_lock);
	up_write(&rdev->pm.mclk_lock);
	mutex_unlock(&rdev->ddev->struct_mutex);
}

static void radeon_pm_print_states(struct radeon_device *rdev)
{
	int i, j;
	struct radeon_power_state *power_state;
	struct radeon_pm_clock_info *clock_info;

	DRM_DEBUG_DRIVER("%d Power State(s)\n", rdev->pm.num_power_states);
	for (i = 0; i < rdev->pm.num_power_states; i++) {
		power_state = &rdev->pm.power_state[i];
		DRM_DEBUG_DRIVER("State %d: %s\n", i,
			radeon_pm_state_type_name[power_state->type]);
		if (i == rdev->pm.default_power_state_index)
			DRM_DEBUG_DRIVER("\tDefault");
		if ((rdev->flags & RADEON_IS_PCIE) && !(rdev->flags & RADEON_IS_IGP))
			DRM_DEBUG_DRIVER("\t%d PCIE Lanes\n", power_state->pcie_lanes);
		if (power_state->flags & RADEON_PM_STATE_SINGLE_DISPLAY_ONLY)
			DRM_DEBUG_DRIVER("\tSingle display only\n");
		DRM_DEBUG_DRIVER("\t%d Clock Mode(s)\n", power_state->num_clock_modes);
		for (j = 0; j < power_state->num_clock_modes; j++) {
			clock_info = &(power_state->clock_info[j]);
			if (rdev->flags & RADEON_IS_IGP)
				DRM_DEBUG_DRIVER("\t\t%d e: %d\n",
						 j,
						 clock_info->sclk * 10);
			else
				DRM_DEBUG_DRIVER("\t\t%d e: %d\tm: %d\tv: %d\n",
						 j,
						 clock_info->sclk * 10,
						 clock_info->mclk * 10,
						 clock_info->voltage.voltage);
		}
	}
}

static ssize_t radeon_get_pm_profile(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct radeon_device *rdev = ddev->dev_private;
	int cp = rdev->pm.profile;

	return snprintf(buf, PAGE_SIZE, "%s\n",
			(cp == PM_PROFILE_AUTO) ? "auto" :
			(cp == PM_PROFILE_LOW) ? "low" :
			(cp == PM_PROFILE_MID) ? "mid" :
			(cp == PM_PROFILE_HIGH) ? "high" : "default");
}

static ssize_t radeon_set_pm_profile(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct radeon_device *rdev = ddev->dev_private;

	/* Can't set profile when the card is off */
	if  ((rdev->flags & RADEON_IS_PX) &&
	     (ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return -EINVAL;

	mutex_lock(&rdev->pm.mutex);
	if (rdev->pm.pm_method == PM_METHOD_PROFILE) {
		if (strncmp("default", buf, strlen("default")) == 0)
			rdev->pm.profile = PM_PROFILE_DEFAULT;
		else if (strncmp("auto", buf, strlen("auto")) == 0)
			rdev->pm.profile = PM_PROFILE_AUTO;
		else if (strncmp("low", buf, strlen("low")) == 0)
			rdev->pm.profile = PM_PROFILE_LOW;
		else if (strncmp("mid", buf, strlen("mid")) == 0)
			rdev->pm.profile = PM_PROFILE_MID;
		else if (strncmp("high", buf, strlen("high")) == 0)
			rdev->pm.profile = PM_PROFILE_HIGH;
		else {
			count = -EINVAL;
			goto fail;
		}
		radeon_pm_update_profile(rdev);
		radeon_pm_set_clocks(rdev);
	} else
		count = -EINVAL;

fail:
	mutex_unlock(&rdev->pm.mutex);

	return count;
}

static ssize_t radeon_get_pm_method(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct radeon_device *rdev = ddev->dev_private;
	int pm = rdev->pm.pm_method;

	return snprintf(buf, PAGE_SIZE, "%s\n",
			(pm == PM_METHOD_DYNPM) ? "dynpm" :
			(pm == PM_METHOD_PROFILE) ? "profile" : "dpm");
}

static ssize_t radeon_set_pm_method(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct radeon_device *rdev = ddev->dev_private;

	/* Can't set method when the card is off */
	if  ((rdev->flags & RADEON_IS_PX) &&
	     (ddev->switch_power_state != DRM_SWITCH_POWER_ON)) {
		count = -EINVAL;
		goto fail;
	}

	/* we don't support the legacy modes with dpm */
	if (rdev->pm.pm_method == PM_METHOD_DPM) {
		count = -EINVAL;
		goto fail;
	}

	if (strncmp("dynpm", buf, strlen("dynpm")) == 0) {
		mutex_lock(&rdev->pm.mutex);
		rdev->pm.pm_method = PM_METHOD_DYNPM;
		rdev->pm.dynpm_state = DYNPM_STATE_PAUSED;
		rdev->pm.dynpm_planned_action = DYNPM_ACTION_DEFAULT;
		mutex_unlock(&rdev->pm.mutex);
	} else if (strncmp("profile", buf, strlen("profile")) == 0) {
		mutex_lock(&rdev->pm.mutex);
		/* disable dynpm */
		rdev->pm.dynpm_state = DYNPM_STATE_DISABLED;
		rdev->pm.dynpm_planned_action = DYNPM_ACTION_NONE;
		rdev->pm.pm_method = PM_METHOD_PROFILE;
		mutex_unlock(&rdev->pm.mutex);
		cancel_delayed_work_sync(&rdev->pm.dynpm_idle_work);
	} else {
		count = -EINVAL;
		goto fail;
	}
	radeon_pm_compute_clocks(rdev);
fail:
	return count;
}

static ssize_t radeon_get_dpm_state(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct radeon_device *rdev = ddev->dev_private;
	enum radeon_pm_state_type pm = rdev->pm.dpm.user_state;

	return snprintf(buf, PAGE_SIZE, "%s\n",
			(pm == POWER_STATE_TYPE_BATTERY) ? "battery" :
			(pm == POWER_STATE_TYPE_BALANCED) ? "balanced" : "performance");
}

static ssize_t radeon_set_dpm_state(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct radeon_device *rdev = ddev->dev_private;

	mutex_lock(&rdev->pm.mutex);
	if (strncmp("battery", buf, strlen("battery")) == 0)
		rdev->pm.dpm.user_state = POWER_STATE_TYPE_BATTERY;
	else if (strncmp("balanced", buf, strlen("balanced")) == 0)
		rdev->pm.dpm.user_state = POWER_STATE_TYPE_BALANCED;
	else if (strncmp("performance", buf, strlen("performance")) == 0)
		rdev->pm.dpm.user_state = POWER_STATE_TYPE_PERFORMANCE;
	else {
		mutex_unlock(&rdev->pm.mutex);
		count = -EINVAL;
		goto fail;
	}
	mutex_unlock(&rdev->pm.mutex);

	/* Can't set dpm state when the card is off */
	if (!(rdev->flags & RADEON_IS_PX) ||
	    (ddev->switch_power_state == DRM_SWITCH_POWER_ON))
		radeon_pm_compute_clocks(rdev);

fail:
	return count;
}

static ssize_t radeon_get_dpm_forced_performance_level(struct device *dev,
						       struct device_attribute *attr,
						       char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct radeon_device *rdev = ddev->dev_private;
	enum radeon_dpm_forced_level level = rdev->pm.dpm.forced_level;

	if  ((rdev->flags & RADEON_IS_PX) &&
	     (ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return snprintf(buf, PAGE_SIZE, "off\n");

	return snprintf(buf, PAGE_SIZE, "%s\n",
			(level == RADEON_DPM_FORCED_LEVEL_AUTO) ? "auto" :
			(level == RADEON_DPM_FORCED_LEVEL_LOW) ? "low" : "high");
}

static ssize_t radeon_set_dpm_forced_performance_level(struct device *dev,
						       struct device_attribute *attr,
						       const char *buf,
						       size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct radeon_device *rdev = ddev->dev_private;
	enum radeon_dpm_forced_level level;
	int ret = 0;

	/* Can't force performance level when the card is off */
	if  ((rdev->flags & RADEON_IS_PX) &&
	     (ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return -EINVAL;

	mutex_lock(&rdev->pm.mutex);
	if (strncmp("low", buf, strlen("low")) == 0) {
		level = RADEON_DPM_FORCED_LEVEL_LOW;
	} else if (strncmp("high", buf, strlen("high")) == 0) {
		level = RADEON_DPM_FORCED_LEVEL_HIGH;
	} else if (strncmp("auto", buf, strlen("auto")) == 0) {
		level = RADEON_DPM_FORCED_LEVEL_AUTO;
	} else {
		count = -EINVAL;
		goto fail;
	}
	if (rdev->asic->dpm.force_performance_level) {
		if (rdev->pm.dpm.thermal_active) {
			count = -EINVAL;
			goto fail;
		}
		ret = radeon_dpm_force_performance_level(rdev, level);
		if (ret)
			count = -EINVAL;
	}
fail:
	mutex_unlock(&rdev->pm.mutex);

	return count;
}

static DEVICE_ATTR(power_profile, S_IRUGO | S_IWUSR, radeon_get_pm_profile, radeon_set_pm_profile);
static DEVICE_ATTR(power_method, S_IRUGO | S_IWUSR, radeon_get_pm_method, radeon_set_pm_method);
static DEVICE_ATTR(power_dpm_state, S_IRUGO | S_IWUSR, radeon_get_dpm_state, radeon_set_dpm_state);
static DEVICE_ATTR(power_dpm_force_performance_level, S_IRUGO | S_IWUSR,
		   radeon_get_dpm_forced_performance_level,
		   radeon_set_dpm_forced_performance_level);

static ssize_t radeon_hwmon_show_temp(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct radeon_device *rdev = dev_get_drvdata(dev);
	struct drm_device *ddev = rdev->ddev;
	int temp;

	/* Can't get temperature when the card is off */
	if  ((rdev->flags & RADEON_IS_PX) &&
	     (ddev->switch_power_state != DRM_SWITCH_POWER_ON))
		return -EINVAL;

	if (rdev->asic->pm.get_temperature)
		temp = radeon_get_temperature(rdev);
	else
		temp = 0;

	return snprintf(buf, PAGE_SIZE, "%d\n", temp);
}

static ssize_t radeon_hwmon_show_temp_thresh(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct radeon_device *rdev = dev_get_drvdata(dev);
	int hyst = to_sensor_dev_attr(attr)->index;
	int temp;

	if (hyst)
		temp = rdev->pm.dpm.thermal.min_temp;
	else
		temp = rdev->pm.dpm.thermal.max_temp;

	return snprintf(buf, PAGE_SIZE, "%d\n", temp);
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, radeon_hwmon_show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, radeon_hwmon_show_temp_thresh, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IRUGO, radeon_hwmon_show_temp_thresh, NULL, 1);

static struct attribute *hwmon_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
	NULL
};

static umode_t hwmon_attributes_visible(struct kobject *kobj,
					struct attribute *attr, int index)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct radeon_device *rdev = dev_get_drvdata(dev);

	/* Skip limit attributes if DPM is not enabled */
	if (rdev->pm.pm_method != PM_METHOD_DPM &&
	    (attr == &sensor_dev_attr_temp1_crit.dev_attr.attr ||
	     attr == &sensor_dev_attr_temp1_crit_hyst.dev_attr.attr))
		return 0;

	return attr->mode;
}

static const struct attribute_group hwmon_attrgroup = {
	.attrs = hwmon_attributes,
	.is_visible = hwmon_attributes_visible,
};

static const struct attribute_group *hwmon_groups[] = {
	&hwmon_attrgroup,
	NULL
};

static int radeon_hwmon_init(struct radeon_device *rdev)
{
	int err = 0;

	switch (rdev->pm.int_thermal_type) {
	case THERMAL_TYPE_RV6XX:
	case THERMAL_TYPE_RV770:
	case THERMAL_TYPE_EVERGREEN:
	case THERMAL_TYPE_NI:
	case THERMAL_TYPE_SUMO:
	case THERMAL_TYPE_SI:
	case THERMAL_TYPE_CI:
	case THERMAL_TYPE_KV:
		if (rdev->asic->pm.get_temperature == NULL)
			return err;
		rdev->pm.int_hwmon_dev = hwmon_device_register_with_groups(rdev->dev,
									   "radeon", rdev,
									   hwmon_groups);
		if (IS_ERR(rdev->pm.int_hwmon_dev)) {
			err = PTR_ERR(rdev->pm.int_hwmon_dev);
			dev_err(rdev->dev,
				"Unable to register hwmon device: %d\n", err);
		}
		break;
	default:
		break;
	}

	return err;
}

static void radeon_hwmon_fini(struct radeon_device *rdev)
{
	if (rdev->pm.int_hwmon_dev)
		hwmon_device_unregister(rdev->pm.int_hwmon_dev);
}

static void radeon_dpm_thermal_work_handler(struct work_struct *work)
{
	struct radeon_device *rdev =
		container_of(work, struct radeon_device,
			     pm.dpm.thermal.work);
	/* switch to the thermal state */
	enum radeon_pm_state_type dpm_state = POWER_STATE_TYPE_INTERNAL_THERMAL;

	if (!rdev->pm.dpm_enabled)
		return;

	if (rdev->asic->pm.get_temperature) {
		int temp = radeon_get_temperature(rdev);

		if (temp < rdev->pm.dpm.thermal.min_temp)
			/* switch back the user state */
			dpm_state = rdev->pm.dpm.user_state;
	} else {
		if (rdev->pm.dpm.thermal.high_to_low)
			/* switch back the user state */
			dpm_state = rdev->pm.dpm.user_state;
	}
	mutex_lock(&rdev->pm.mutex);
	if (dpm_state == POWER_STATE_TYPE_INTERNAL_THERMAL)
		rdev->pm.dpm.thermal_active = true;
	else
		rdev->pm.dpm.thermal_active = false;
	rdev->pm.dpm.state = dpm_state;
	mutex_unlock(&rdev->pm.mutex);

	radeon_pm_compute_clocks(rdev);
}

static struct radeon_ps *radeon_dpm_pick_power_state(struct radeon_device *rdev,
						     enum radeon_pm_state_type dpm_state)
{
	int i;
	struct radeon_ps *ps;
	u32 ui_class;
	bool single_display = (rdev->pm.dpm.new_active_crtc_count < 2) ?
		true : false;

	/* check if the vblank period is too short to adjust the mclk */
	if (single_display && rdev->asic->dpm.vblank_too_short) {
		if (radeon_dpm_vblank_too_short(rdev))
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
	for (i = 0; i < rdev->pm.dpm.num_ps; i++) {
		ps = &rdev->pm.dpm.ps[i];
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
			if (rdev->pm.dpm.uvd_ps)
				return rdev->pm.dpm.uvd_ps;
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
			return rdev->pm.dpm.boot_ps;
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
		if (rdev->pm.dpm.uvd_ps) {
			return rdev->pm.dpm.uvd_ps;
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

static void radeon_dpm_change_power_state_locked(struct radeon_device *rdev)
{
	int i;
	struct radeon_ps *ps;
	enum radeon_pm_state_type dpm_state;
	int ret;

	/* if dpm init failed */
	if (!rdev->pm.dpm_enabled)
		return;

	if (rdev->pm.dpm.user_state != rdev->pm.dpm.state) {
		/* add other state override checks here */
		if ((!rdev->pm.dpm.thermal_active) &&
		    (!rdev->pm.dpm.uvd_active))
			rdev->pm.dpm.state = rdev->pm.dpm.user_state;
	}
	dpm_state = rdev->pm.dpm.state;

	ps = radeon_dpm_pick_power_state(rdev, dpm_state);
	if (ps)
		rdev->pm.dpm.requested_ps = ps;
	else
		return;

	/* no need to reprogram if nothing changed unless we are on BTC+ */
	if (rdev->pm.dpm.current_ps == rdev->pm.dpm.requested_ps) {
		/* vce just modifies an existing state so force a change */
		if (ps->vce_active != rdev->pm.dpm.vce_active)
			goto force;
		if ((rdev->family < CHIP_BARTS) || (rdev->flags & RADEON_IS_IGP)) {
			/* for pre-BTC and APUs if the num crtcs changed but state is the same,
			 * all we need to do is update the display configuration.
			 */
			if (rdev->pm.dpm.new_active_crtcs != rdev->pm.dpm.current_active_crtcs) {
				/* update display watermarks based on new power state */
				radeon_bandwidth_update(rdev);
				/* update displays */
				radeon_dpm_display_configuration_changed(rdev);
				rdev->pm.dpm.current_active_crtcs = rdev->pm.dpm.new_active_crtcs;
				rdev->pm.dpm.current_active_crtc_count = rdev->pm.dpm.new_active_crtc_count;
			}
			return;
		} else {
			/* for BTC+ if the num crtcs hasn't changed and state is the same,
			 * nothing to do, if the num crtcs is > 1 and state is the same,
			 * update display configuration.
			 */
			if (rdev->pm.dpm.new_active_crtcs ==
			    rdev->pm.dpm.current_active_crtcs) {
				return;
			} else {
				if ((rdev->pm.dpm.current_active_crtc_count > 1) &&
				    (rdev->pm.dpm.new_active_crtc_count > 1)) {
					/* update display watermarks based on new power state */
					radeon_bandwidth_update(rdev);
					/* update displays */
					radeon_dpm_display_configuration_changed(rdev);
					rdev->pm.dpm.current_active_crtcs = rdev->pm.dpm.new_active_crtcs;
					rdev->pm.dpm.current_active_crtc_count = rdev->pm.dpm.new_active_crtc_count;
					return;
				}
			}
		}
	}

force:
	if (radeon_dpm == 1) {
		printk("switching from power state:\n");
		radeon_dpm_print_power_state(rdev, rdev->pm.dpm.current_ps);
		printk("switching to power state:\n");
		radeon_dpm_print_power_state(rdev, rdev->pm.dpm.requested_ps);
	}

	mutex_lock(&rdev->ddev->struct_mutex);
	down_write(&rdev->pm.mclk_lock);
	mutex_lock(&rdev->ring_lock);

	/* update whether vce is active */
	ps->vce_active = rdev->pm.dpm.vce_active;

	ret = radeon_dpm_pre_set_power_state(rdev);
	if (ret)
		goto done;

	/* update display watermarks based on new power state */
	radeon_bandwidth_update(rdev);
	/* update displays */
	radeon_dpm_display_configuration_changed(rdev);

	rdev->pm.dpm.current_active_crtcs = rdev->pm.dpm.new_active_crtcs;
	rdev->pm.dpm.current_active_crtc_count = rdev->pm.dpm.new_active_crtc_count;

	/* wait for the rings to drain */
	for (i = 0; i < RADEON_NUM_RINGS; i++) {
		struct radeon_ring *ring = &rdev->ring[i];
		if (ring->ready)
			radeon_fence_wait_empty(rdev, i);
	}

	/* program the new power state */
	radeon_dpm_set_power_state(rdev);

	/* update current power state */
	rdev->pm.dpm.current_ps = rdev->pm.dpm.requested_ps;

	radeon_dpm_post_set_power_state(rdev);

	if (rdev->asic->dpm.force_performance_level) {
		if (rdev->pm.dpm.thermal_active) {
			enum radeon_dpm_forced_level level = rdev->pm.dpm.forced_level;
			/* force low perf level for thermal */
			radeon_dpm_force_performance_level(rdev, RADEON_DPM_FORCED_LEVEL_LOW);
			/* save the user's level */
			rdev->pm.dpm.forced_level = level;
		} else {
			/* otherwise, user selected level */
			radeon_dpm_force_performance_level(rdev, rdev->pm.dpm.forced_level);
		}
	}

done:
	mutex_unlock(&rdev->ring_lock);
	up_write(&rdev->pm.mclk_lock);
	mutex_unlock(&rdev->ddev->struct_mutex);
}

void radeon_dpm_enable_uvd(struct radeon_device *rdev, bool enable)
{
	enum radeon_pm_state_type dpm_state;

	if (rdev->asic->dpm.powergate_uvd) {
		mutex_lock(&rdev->pm.mutex);
		/* don't powergate anything if we
		   have active but pause streams */
		enable |= rdev->pm.dpm.sd > 0;
		enable |= rdev->pm.dpm.hd > 0;
		/* enable/disable UVD */
		radeon_dpm_powergate_uvd(rdev, !enable);
		mutex_unlock(&rdev->pm.mutex);
	} else {
		if (enable) {
			mutex_lock(&rdev->pm.mutex);
			rdev->pm.dpm.uvd_active = true;
			/* disable this for now */
#if 0
			if ((rdev->pm.dpm.sd == 1) && (rdev->pm.dpm.hd == 0))
				dpm_state = POWER_STATE_TYPE_INTERNAL_UVD_SD;
			else if ((rdev->pm.dpm.sd == 2) && (rdev->pm.dpm.hd == 0))
				dpm_state = POWER_STATE_TYPE_INTERNAL_UVD_HD;
			else if ((rdev->pm.dpm.sd == 0) && (rdev->pm.dpm.hd == 1))
				dpm_state = POWER_STATE_TYPE_INTERNAL_UVD_HD;
			else if ((rdev->pm.dpm.sd == 0) && (rdev->pm.dpm.hd == 2))
				dpm_state = POWER_STATE_TYPE_INTERNAL_UVD_HD2;
			else
#endif
				dpm_state = POWER_STATE_TYPE_INTERNAL_UVD;
			rdev->pm.dpm.state = dpm_state;
			mutex_unlock(&rdev->pm.mutex);
		} else {
			mutex_lock(&rdev->pm.mutex);
			rdev->pm.dpm.uvd_active = false;
			mutex_unlock(&rdev->pm.mutex);
		}

		radeon_pm_compute_clocks(rdev);
	}
}

void radeon_dpm_enable_vce(struct radeon_device *rdev, bool enable)
{
	if (enable) {
		mutex_lock(&rdev->pm.mutex);
		rdev->pm.dpm.vce_active = true;
		/* XXX select vce level based on ring/task */
		rdev->pm.dpm.vce_level = RADEON_VCE_LEVEL_AC_ALL;
		mutex_unlock(&rdev->pm.mutex);
	} else {
		mutex_lock(&rdev->pm.mutex);
		rdev->pm.dpm.vce_active = false;
		mutex_unlock(&rdev->pm.mutex);
	}

	radeon_pm_compute_clocks(rdev);
}

static void radeon_pm_suspend_old(struct radeon_device *rdev)
{
	mutex_lock(&rdev->pm.mutex);
	if (rdev->pm.pm_method == PM_METHOD_DYNPM) {
		if (rdev->pm.dynpm_state == DYNPM_STATE_ACTIVE)
			rdev->pm.dynpm_state = DYNPM_STATE_SUSPENDED;
	}
	mutex_unlock(&rdev->pm.mutex);

	cancel_delayed_work_sync(&rdev->pm.dynpm_idle_work);
}

static void radeon_pm_suspend_dpm(struct radeon_device *rdev)
{
	mutex_lock(&rdev->pm.mutex);
	/* disable dpm */
	radeon_dpm_disable(rdev);
	/* reset the power state */
	rdev->pm.dpm.current_ps = rdev->pm.dpm.requested_ps = rdev->pm.dpm.boot_ps;
	rdev->pm.dpm_enabled = false;
	mutex_unlock(&rdev->pm.mutex);
}

void radeon_pm_suspend(struct radeon_device *rdev)
{
	if (rdev->pm.pm_method == PM_METHOD_DPM)
		radeon_pm_suspend_dpm(rdev);
	else
		radeon_pm_suspend_old(rdev);
}

static void radeon_pm_resume_old(struct radeon_device *rdev)
{
	/* set up the default clocks if the MC ucode is loaded */
	if ((rdev->family >= CHIP_BARTS) &&
	    (rdev->family <= CHIP_CAYMAN) &&
	    rdev->mc_fw) {
		if (rdev->pm.default_vddc)
			radeon_atom_set_voltage(rdev, rdev->pm.default_vddc,
						SET_VOLTAGE_TYPE_ASIC_VDDC);
		if (rdev->pm.default_vddci)
			radeon_atom_set_voltage(rdev, rdev->pm.default_vddci,
						SET_VOLTAGE_TYPE_ASIC_VDDCI);
		if (rdev->pm.default_sclk)
			radeon_set_engine_clock(rdev, rdev->pm.default_sclk);
		if (rdev->pm.default_mclk)
			radeon_set_memory_clock(rdev, rdev->pm.default_mclk);
	}
	/* asic init will reset the default power state */
	mutex_lock(&rdev->pm.mutex);
	rdev->pm.current_power_state_index = rdev->pm.default_power_state_index;
	rdev->pm.current_clock_mode_index = 0;
	rdev->pm.current_sclk = rdev->pm.default_sclk;
	rdev->pm.current_mclk = rdev->pm.default_mclk;
	if (rdev->pm.power_state) {
		rdev->pm.current_vddc = rdev->pm.power_state[rdev->pm.default_power_state_index].clock_info[0].voltage.voltage;
		rdev->pm.current_vddci = rdev->pm.power_state[rdev->pm.default_power_state_index].clock_info[0].voltage.vddci;
	}
	if (rdev->pm.pm_method == PM_METHOD_DYNPM
	    && rdev->pm.dynpm_state == DYNPM_STATE_SUSPENDED) {
		rdev->pm.dynpm_state = DYNPM_STATE_ACTIVE;
		schedule_delayed_work(&rdev->pm.dynpm_idle_work,
				      msecs_to_jiffies(RADEON_IDLE_LOOP_MS));
	}
	mutex_unlock(&rdev->pm.mutex);
	radeon_pm_compute_clocks(rdev);
}

static void radeon_pm_resume_dpm(struct radeon_device *rdev)
{
	int ret;

	/* asic init will reset to the boot state */
	mutex_lock(&rdev->pm.mutex);
	rdev->pm.dpm.current_ps = rdev->pm.dpm.requested_ps = rdev->pm.dpm.boot_ps;
	radeon_dpm_setup_asic(rdev);
	ret = radeon_dpm_enable(rdev);
	mutex_unlock(&rdev->pm.mutex);
	if (ret)
		goto dpm_resume_fail;
	rdev->pm.dpm_enabled = true;
	return;

dpm_resume_fail:
	DRM_ERROR("radeon: dpm resume failed\n");
	if ((rdev->family >= CHIP_BARTS) &&
	    (rdev->family <= CHIP_CAYMAN) &&
	    rdev->mc_fw) {
		if (rdev->pm.default_vddc)
			radeon_atom_set_voltage(rdev, rdev->pm.default_vddc,
						SET_VOLTAGE_TYPE_ASIC_VDDC);
		if (rdev->pm.default_vddci)
			radeon_atom_set_voltage(rdev, rdev->pm.default_vddci,
						SET_VOLTAGE_TYPE_ASIC_VDDCI);
		if (rdev->pm.default_sclk)
			radeon_set_engine_clock(rdev, rdev->pm.default_sclk);
		if (rdev->pm.default_mclk)
			radeon_set_memory_clock(rdev, rdev->pm.default_mclk);
	}
}

void radeon_pm_resume(struct radeon_device *rdev)
{
	if (rdev->pm.pm_method == PM_METHOD_DPM)
		radeon_pm_resume_dpm(rdev);
	else
		radeon_pm_resume_old(rdev);
}

static int radeon_pm_init_old(struct radeon_device *rdev)
{
	int ret;

	rdev->pm.profile = PM_PROFILE_DEFAULT;
	rdev->pm.dynpm_state = DYNPM_STATE_DISABLED;
	rdev->pm.dynpm_planned_action = DYNPM_ACTION_NONE;
	rdev->pm.dynpm_can_upclock = true;
	rdev->pm.dynpm_can_downclock = true;
	rdev->pm.default_sclk = rdev->clock.default_sclk;
	rdev->pm.default_mclk = rdev->clock.default_mclk;
	rdev->pm.current_sclk = rdev->clock.default_sclk;
	rdev->pm.current_mclk = rdev->clock.default_mclk;
	rdev->pm.int_thermal_type = THERMAL_TYPE_NONE;

	if (rdev->bios) {
		if (rdev->is_atom_bios)
			radeon_atombios_get_power_modes(rdev);
		else
			radeon_combios_get_power_modes(rdev);
		radeon_pm_print_states(rdev);
		radeon_pm_init_profile(rdev);
		/* set up the default clocks if the MC ucode is loaded */
		if ((rdev->family >= CHIP_BARTS) &&
		    (rdev->family <= CHIP_CAYMAN) &&
		    rdev->mc_fw) {
			if (rdev->pm.default_vddc)
				radeon_atom_set_voltage(rdev, rdev->pm.default_vddc,
							SET_VOLTAGE_TYPE_ASIC_VDDC);
			if (rdev->pm.default_vddci)
				radeon_atom_set_voltage(rdev, rdev->pm.default_vddci,
							SET_VOLTAGE_TYPE_ASIC_VDDCI);
			if (rdev->pm.default_sclk)
				radeon_set_engine_clock(rdev, rdev->pm.default_sclk);
			if (rdev->pm.default_mclk)
				radeon_set_memory_clock(rdev, rdev->pm.default_mclk);
		}
	}

	/* set up the internal thermal sensor if applicable */
	ret = radeon_hwmon_init(rdev);
	if (ret)
		return ret;

	INIT_DELAYED_WORK(&rdev->pm.dynpm_idle_work, radeon_dynpm_idle_work_handler);

	if (rdev->pm.num_power_states > 1) {
		/* where's the best place to put these? */
		ret = device_create_file(rdev->dev, &dev_attr_power_profile);
		if (ret)
			DRM_ERROR("failed to create device file for power profile\n");
		ret = device_create_file(rdev->dev, &dev_attr_power_method);
		if (ret)
			DRM_ERROR("failed to create device file for power method\n");

		if (radeon_debugfs_pm_init(rdev)) {
			DRM_ERROR("Failed to register debugfs file for PM!\n");
		}

		DRM_INFO("radeon: power management initialized\n");
	}

	return 0;
}

static void radeon_dpm_print_power_states(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->pm.dpm.num_ps; i++) {
		printk("== power state %d ==\n", i);
		radeon_dpm_print_power_state(rdev, &rdev->pm.dpm.ps[i]);
	}
}

static int radeon_pm_init_dpm(struct radeon_device *rdev)
{
	int ret;

	/* default to balanced state */
	rdev->pm.dpm.state = POWER_STATE_TYPE_BALANCED;
	rdev->pm.dpm.user_state = POWER_STATE_TYPE_BALANCED;
	rdev->pm.dpm.forced_level = RADEON_DPM_FORCED_LEVEL_AUTO;
	rdev->pm.default_sclk = rdev->clock.default_sclk;
	rdev->pm.default_mclk = rdev->clock.default_mclk;
	rdev->pm.current_sclk = rdev->clock.default_sclk;
	rdev->pm.current_mclk = rdev->clock.default_mclk;
	rdev->pm.int_thermal_type = THERMAL_TYPE_NONE;

	if (rdev->bios && rdev->is_atom_bios)
		radeon_atombios_get_power_modes(rdev);
	else
		return -EINVAL;

	/* set up the internal thermal sensor if applicable */
	ret = radeon_hwmon_init(rdev);
	if (ret)
		return ret;

	INIT_WORK(&rdev->pm.dpm.thermal.work, radeon_dpm_thermal_work_handler);
	mutex_lock(&rdev->pm.mutex);
	radeon_dpm_init(rdev);
	rdev->pm.dpm.current_ps = rdev->pm.dpm.requested_ps = rdev->pm.dpm.boot_ps;
	if (radeon_dpm == 1)
		radeon_dpm_print_power_states(rdev);
	radeon_dpm_setup_asic(rdev);
	ret = radeon_dpm_enable(rdev);
	mutex_unlock(&rdev->pm.mutex);
	if (ret)
		goto dpm_failed;
	rdev->pm.dpm_enabled = true;

	ret = device_create_file(rdev->dev, &dev_attr_power_dpm_state);
	if (ret)
		DRM_ERROR("failed to create device file for dpm state\n");
	ret = device_create_file(rdev->dev, &dev_attr_power_dpm_force_performance_level);
	if (ret)
		DRM_ERROR("failed to create device file for dpm state\n");
	/* XXX: these are noops for dpm but are here for backwards compat */
	ret = device_create_file(rdev->dev, &dev_attr_power_profile);
	if (ret)
		DRM_ERROR("failed to create device file for power profile\n");
	ret = device_create_file(rdev->dev, &dev_attr_power_method);
	if (ret)
		DRM_ERROR("failed to create device file for power method\n");

	if (radeon_debugfs_pm_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for dpm!\n");
	}

	DRM_INFO("radeon: dpm initialized\n");

	return 0;

dpm_failed:
	rdev->pm.dpm_enabled = false;
	if ((rdev->family >= CHIP_BARTS) &&
	    (rdev->family <= CHIP_CAYMAN) &&
	    rdev->mc_fw) {
		if (rdev->pm.default_vddc)
			radeon_atom_set_voltage(rdev, rdev->pm.default_vddc,
						SET_VOLTAGE_TYPE_ASIC_VDDC);
		if (rdev->pm.default_vddci)
			radeon_atom_set_voltage(rdev, rdev->pm.default_vddci,
						SET_VOLTAGE_TYPE_ASIC_VDDCI);
		if (rdev->pm.default_sclk)
			radeon_set_engine_clock(rdev, rdev->pm.default_sclk);
		if (rdev->pm.default_mclk)
			radeon_set_memory_clock(rdev, rdev->pm.default_mclk);
	}
	DRM_ERROR("radeon: dpm initialization failed\n");
	return ret;
}

int radeon_pm_init(struct radeon_device *rdev)
{
	/* enable dpm on rv6xx+ */
	switch (rdev->family) {
	case CHIP_RV610:
	case CHIP_RV630:
	case CHIP_RV620:
	case CHIP_RV635:
	case CHIP_RV670:
	case CHIP_RS780:
	case CHIP_RS880:
	case CHIP_RV770:
		/* DPM requires the RLC, RV770+ dGPU requires SMC */
		if (!rdev->rlc_fw)
			rdev->pm.pm_method = PM_METHOD_PROFILE;
		else if ((rdev->family >= CHIP_RV770) &&
			 (!(rdev->flags & RADEON_IS_IGP)) &&
			 (!rdev->smc_fw))
			rdev->pm.pm_method = PM_METHOD_PROFILE;
		else if (radeon_dpm == 1)
			rdev->pm.pm_method = PM_METHOD_DPM;
		else
			rdev->pm.pm_method = PM_METHOD_PROFILE;
		break;
	case CHIP_RV730:
	case CHIP_RV710:
	case CHIP_RV740:
	case CHIP_CEDAR:
	case CHIP_REDWOOD:
	case CHIP_JUNIPER:
	case CHIP_CYPRESS:
	case CHIP_HEMLOCK:
	case CHIP_PALM:
	case CHIP_SUMO:
	case CHIP_SUMO2:
	case CHIP_BARTS:
	case CHIP_TURKS:
	case CHIP_CAICOS:
	case CHIP_CAYMAN:
	case CHIP_ARUBA:
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_OLAND:
	case CHIP_HAINAN:
	case CHIP_BONAIRE:
	case CHIP_KABINI:
	case CHIP_KAVERI:
	case CHIP_HAWAII:
	case CHIP_MULLINS:
		/* DPM requires the RLC, RV770+ dGPU requires SMC */
		if (!rdev->rlc_fw)
			rdev->pm.pm_method = PM_METHOD_PROFILE;
		else if ((rdev->family >= CHIP_RV770) &&
			 (!(rdev->flags & RADEON_IS_IGP)) &&
			 (!rdev->smc_fw))
			rdev->pm.pm_method = PM_METHOD_PROFILE;
		else if (radeon_dpm == 0)
			rdev->pm.pm_method = PM_METHOD_PROFILE;
		else
			rdev->pm.pm_method = PM_METHOD_DPM;
		break;
	default:
		/* default to profile method */
		rdev->pm.pm_method = PM_METHOD_PROFILE;
		break;
	}

	if (rdev->pm.pm_method == PM_METHOD_DPM)
		return radeon_pm_init_dpm(rdev);
	else
		return radeon_pm_init_old(rdev);
}

int radeon_pm_late_init(struct radeon_device *rdev)
{
	int ret = 0;

	if (rdev->pm.pm_method == PM_METHOD_DPM) {
		mutex_lock(&rdev->pm.mutex);
		ret = radeon_dpm_late_enable(rdev);
		mutex_unlock(&rdev->pm.mutex);
	}
	return ret;
}

static void radeon_pm_fini_old(struct radeon_device *rdev)
{
	if (rdev->pm.num_power_states > 1) {
		mutex_lock(&rdev->pm.mutex);
		if (rdev->pm.pm_method == PM_METHOD_PROFILE) {
			rdev->pm.profile = PM_PROFILE_DEFAULT;
			radeon_pm_update_profile(rdev);
			radeon_pm_set_clocks(rdev);
		} else if (rdev->pm.pm_method == PM_METHOD_DYNPM) {
			/* reset default clocks */
			rdev->pm.dynpm_state = DYNPM_STATE_DISABLED;
			rdev->pm.dynpm_planned_action = DYNPM_ACTION_DEFAULT;
			radeon_pm_set_clocks(rdev);
		}
		mutex_unlock(&rdev->pm.mutex);

		cancel_delayed_work_sync(&rdev->pm.dynpm_idle_work);

		device_remove_file(rdev->dev, &dev_attr_power_profile);
		device_remove_file(rdev->dev, &dev_attr_power_method);
	}

	radeon_hwmon_fini(rdev);
	kfree(rdev->pm.power_state);
}

static void radeon_pm_fini_dpm(struct radeon_device *rdev)
{
	if (rdev->pm.num_power_states > 1) {
		mutex_lock(&rdev->pm.mutex);
		radeon_dpm_disable(rdev);
		mutex_unlock(&rdev->pm.mutex);

		device_remove_file(rdev->dev, &dev_attr_power_dpm_state);
		device_remove_file(rdev->dev, &dev_attr_power_dpm_force_performance_level);
		/* XXX backwards compat */
		device_remove_file(rdev->dev, &dev_attr_power_profile);
		device_remove_file(rdev->dev, &dev_attr_power_method);
	}
	radeon_dpm_fini(rdev);

	radeon_hwmon_fini(rdev);
	kfree(rdev->pm.power_state);
}

void radeon_pm_fini(struct radeon_device *rdev)
{
	if (rdev->pm.pm_method == PM_METHOD_DPM)
		radeon_pm_fini_dpm(rdev);
	else
		radeon_pm_fini_old(rdev);
}

static void radeon_pm_compute_clocks_old(struct radeon_device *rdev)
{
	struct drm_device *ddev = rdev->ddev;
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;

	if (rdev->pm.num_power_states < 2)
		return;

	mutex_lock(&rdev->pm.mutex);

	rdev->pm.active_crtcs = 0;
	rdev->pm.active_crtc_count = 0;
	if (rdev->num_crtc && rdev->mode_info.mode_config_initialized) {
		list_for_each_entry(crtc,
				    &ddev->mode_config.crtc_list, head) {
			radeon_crtc = to_radeon_crtc(crtc);
			if (radeon_crtc->enabled) {
				rdev->pm.active_crtcs |= (1 << radeon_crtc->crtc_id);
				rdev->pm.active_crtc_count++;
			}
		}
	}

	if (rdev->pm.pm_method == PM_METHOD_PROFILE) {
		radeon_pm_update_profile(rdev);
		radeon_pm_set_clocks(rdev);
	} else if (rdev->pm.pm_method == PM_METHOD_DYNPM) {
		if (rdev->pm.dynpm_state != DYNPM_STATE_DISABLED) {
			if (rdev->pm.active_crtc_count > 1) {
				if (rdev->pm.dynpm_state == DYNPM_STATE_ACTIVE) {
					cancel_delayed_work(&rdev->pm.dynpm_idle_work);

					rdev->pm.dynpm_state = DYNPM_STATE_PAUSED;
					rdev->pm.dynpm_planned_action = DYNPM_ACTION_DEFAULT;
					radeon_pm_get_dynpm_state(rdev);
					radeon_pm_set_clocks(rdev);

					DRM_DEBUG_DRIVER("radeon: dynamic power management deactivated\n");
				}
			} else if (rdev->pm.active_crtc_count == 1) {
				/* TODO: Increase clocks if needed for current mode */

				if (rdev->pm.dynpm_state == DYNPM_STATE_MINIMUM) {
					rdev->pm.dynpm_state = DYNPM_STATE_ACTIVE;
					rdev->pm.dynpm_planned_action = DYNPM_ACTION_UPCLOCK;
					radeon_pm_get_dynpm_state(rdev);
					radeon_pm_set_clocks(rdev);

					schedule_delayed_work(&rdev->pm.dynpm_idle_work,
							      msecs_to_jiffies(RADEON_IDLE_LOOP_MS));
				} else if (rdev->pm.dynpm_state == DYNPM_STATE_PAUSED) {
					rdev->pm.dynpm_state = DYNPM_STATE_ACTIVE;
					schedule_delayed_work(&rdev->pm.dynpm_idle_work,
							      msecs_to_jiffies(RADEON_IDLE_LOOP_MS));
					DRM_DEBUG_DRIVER("radeon: dynamic power management activated\n");
				}
			} else { /* count == 0 */
				if (rdev->pm.dynpm_state != DYNPM_STATE_MINIMUM) {
					cancel_delayed_work(&rdev->pm.dynpm_idle_work);

					rdev->pm.dynpm_state = DYNPM_STATE_MINIMUM;
					rdev->pm.dynpm_planned_action = DYNPM_ACTION_MINIMUM;
					radeon_pm_get_dynpm_state(rdev);
					radeon_pm_set_clocks(rdev);
				}
			}
		}
	}

	mutex_unlock(&rdev->pm.mutex);
}

static void radeon_pm_compute_clocks_dpm(struct radeon_device *rdev)
{
	struct drm_device *ddev = rdev->ddev;
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;

	if (!rdev->pm.dpm_enabled)
		return;

	mutex_lock(&rdev->pm.mutex);

	/* update active crtc counts */
	rdev->pm.dpm.new_active_crtcs = 0;
	rdev->pm.dpm.new_active_crtc_count = 0;
	if (rdev->num_crtc && rdev->mode_info.mode_config_initialized) {
		list_for_each_entry(crtc,
				    &ddev->mode_config.crtc_list, head) {
			radeon_crtc = to_radeon_crtc(crtc);
			if (crtc->enabled) {
				rdev->pm.dpm.new_active_crtcs |= (1 << radeon_crtc->crtc_id);
				rdev->pm.dpm.new_active_crtc_count++;
			}
		}
	}

	/* update battery/ac status */
	if (power_supply_is_system_supplied() > 0)
		rdev->pm.dpm.ac_power = true;
	else
		rdev->pm.dpm.ac_power = false;

	radeon_dpm_change_power_state_locked(rdev);

	mutex_unlock(&rdev->pm.mutex);

}

void radeon_pm_compute_clocks(struct radeon_device *rdev)
{
	if (rdev->pm.pm_method == PM_METHOD_DPM)
		radeon_pm_compute_clocks_dpm(rdev);
	else
		radeon_pm_compute_clocks_old(rdev);
}

static bool radeon_pm_in_vbl(struct radeon_device *rdev)
{
	int  crtc, vpos, hpos, vbl_status;
	bool in_vbl = true;

	/* Iterate over all active crtc's. All crtc's must be in vblank,
	 * otherwise return in_vbl == false.
	 */
	for (crtc = 0; (crtc < rdev->num_crtc) && in_vbl; crtc++) {
		if (rdev->pm.active_crtcs & (1 << crtc)) {
			vbl_status = radeon_get_crtc_scanoutpos(rdev->ddev, crtc, 0, &vpos, &hpos, NULL, NULL);
			if ((vbl_status & DRM_SCANOUTPOS_VALID) &&
			    !(vbl_status & DRM_SCANOUTPOS_IN_VBLANK))
				in_vbl = false;
		}
	}

	return in_vbl;
}

static bool radeon_pm_debug_check_in_vbl(struct radeon_device *rdev, bool finish)
{
	u32 stat_crtc = 0;
	bool in_vbl = radeon_pm_in_vbl(rdev);

	if (in_vbl == false)
		DRM_DEBUG_DRIVER("not in vbl for pm change %08x at %s\n", stat_crtc,
			 finish ? "exit" : "entry");
	return in_vbl;
}

static void radeon_dynpm_idle_work_handler(struct work_struct *work)
{
	struct radeon_device *rdev;
	int resched;
	rdev = container_of(work, struct radeon_device,
				pm.dynpm_idle_work.work);

	resched = ttm_bo_lock_delayed_workqueue(&rdev->mman.bdev);
	mutex_lock(&rdev->pm.mutex);
	if (rdev->pm.dynpm_state == DYNPM_STATE_ACTIVE) {
		int not_processed = 0;
		int i;

		for (i = 0; i < RADEON_NUM_RINGS; ++i) {
			struct radeon_ring *ring = &rdev->ring[i];

			if (ring->ready) {
				not_processed += radeon_fence_count_emitted(rdev, i);
				if (not_processed >= 3)
					break;
			}
		}

		if (not_processed >= 3) { /* should upclock */
			if (rdev->pm.dynpm_planned_action == DYNPM_ACTION_DOWNCLOCK) {
				rdev->pm.dynpm_planned_action = DYNPM_ACTION_NONE;
			} else if (rdev->pm.dynpm_planned_action == DYNPM_ACTION_NONE &&
				   rdev->pm.dynpm_can_upclock) {
				rdev->pm.dynpm_planned_action =
					DYNPM_ACTION_UPCLOCK;
				rdev->pm.dynpm_action_timeout = jiffies +
				msecs_to_jiffies(RADEON_RECLOCK_DELAY_MS);
			}
		} else if (not_processed == 0) { /* should downclock */
			if (rdev->pm.dynpm_planned_action == DYNPM_ACTION_UPCLOCK) {
				rdev->pm.dynpm_planned_action = DYNPM_ACTION_NONE;
			} else if (rdev->pm.dynpm_planned_action == DYNPM_ACTION_NONE &&
				   rdev->pm.dynpm_can_downclock) {
				rdev->pm.dynpm_planned_action =
					DYNPM_ACTION_DOWNCLOCK;
				rdev->pm.dynpm_action_timeout = jiffies +
				msecs_to_jiffies(RADEON_RECLOCK_DELAY_MS);
			}
		}

		/* Note, radeon_pm_set_clocks is called with static_switch set
		 * to false since we want to wait for vbl to avoid flicker.
		 */
		if (rdev->pm.dynpm_planned_action != DYNPM_ACTION_NONE &&
		    jiffies > rdev->pm.dynpm_action_timeout) {
			radeon_pm_get_dynpm_state(rdev);
			radeon_pm_set_clocks(rdev);
		}

		schedule_delayed_work(&rdev->pm.dynpm_idle_work,
				      msecs_to_jiffies(RADEON_IDLE_LOOP_MS));
	}
	mutex_unlock(&rdev->pm.mutex);
	ttm_bo_unlock_delayed_workqueue(&rdev->mman.bdev, resched);
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)

static int radeon_debugfs_pm_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct drm_device *ddev = rdev->ddev;

	if  ((rdev->flags & RADEON_IS_PX) &&
	     (ddev->switch_power_state != DRM_SWITCH_POWER_ON)) {
		seq_printf(m, "PX asic powered off\n");
	} else if (rdev->pm.dpm_enabled) {
		mutex_lock(&rdev->pm.mutex);
		if (rdev->asic->dpm.debugfs_print_current_performance_level)
			radeon_dpm_debugfs_print_current_performance_level(rdev, m);
		else
			seq_printf(m, "Debugfs support not implemented for this asic\n");
		mutex_unlock(&rdev->pm.mutex);
	} else {
		seq_printf(m, "default engine clock: %u0 kHz\n", rdev->pm.default_sclk);
		/* radeon_get_engine_clock is not reliable on APUs so just print the current clock */
		if ((rdev->family >= CHIP_PALM) && (rdev->flags & RADEON_IS_IGP))
			seq_printf(m, "current engine clock: %u0 kHz\n", rdev->pm.current_sclk);
		else
			seq_printf(m, "current engine clock: %u0 kHz\n", radeon_get_engine_clock(rdev));
		seq_printf(m, "default memory clock: %u0 kHz\n", rdev->pm.default_mclk);
		if (rdev->asic->pm.get_memory_clock)
			seq_printf(m, "current memory clock: %u0 kHz\n", radeon_get_memory_clock(rdev));
		if (rdev->pm.current_vddc)
			seq_printf(m, "voltage: %u mV\n", rdev->pm.current_vddc);
		if (rdev->asic->pm.get_pcie_lanes)
			seq_printf(m, "PCIE lanes: %d\n", radeon_get_pcie_lanes(rdev));
	}

	return 0;
}

static struct drm_info_list radeon_pm_info_list[] = {
	{"radeon_pm_info", radeon_debugfs_pm_info, 0, NULL},
};
#endif

static int radeon_debugfs_pm_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, radeon_pm_info_list, ARRAY_SIZE(radeon_pm_info_list));
#else
	return 0;
#endif
}

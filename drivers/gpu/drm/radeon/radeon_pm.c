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
#include "drmP.h"
#include "radeon.h"
#include "avivod.h"
#include "atom.h"
#ifdef CONFIG_ACPI
#include <linux/acpi.h>
#endif
#include <linux/power_supply.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#define RADEON_IDLE_LOOP_MS 100
#define RADEON_RECLOCK_DELAY_MS 200
#define RADEON_WAIT_VBLANK_TIMEOUT 200
#define RADEON_WAIT_IDLE_TIMEOUT 200

static const char *radeon_pm_state_type_name[5] = {
	"Default",
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

#define ACPI_AC_CLASS           "ac_adapter"

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

#ifdef CONFIG_ACPI
static int radeon_acpi_event(struct notifier_block *nb,
			     unsigned long val,
			     void *data)
{
	struct radeon_device *rdev = container_of(nb, struct radeon_device, acpi_nb);
	struct acpi_bus_event *entry = (struct acpi_bus_event *)data;

	if (strcmp(entry->device_class, ACPI_AC_CLASS) == 0) {
		if (power_supply_is_system_supplied() > 0)
			DRM_DEBUG_DRIVER("pm: AC\n");
		else
			DRM_DEBUG_DRIVER("pm: DC\n");

		if (rdev->pm.pm_method == PM_METHOD_PROFILE) {
			if (rdev->pm.profile == PM_PROFILE_AUTO) {
				mutex_lock(&rdev->pm.mutex);
				radeon_pm_update_profile(rdev);
				radeon_pm_set_clocks(rdev);
				mutex_unlock(&rdev->pm.mutex);
			}
		}
	}

	return NOTIFY_OK;
}
#endif

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
		if (rdev->asic->set_memory_clock && (mclk != rdev->pm.current_mclk)) {
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
	int i;

	/* no need to take locks, etc. if nothing's going to change */
	if ((rdev->pm.requested_clock_mode_index == rdev->pm.current_clock_mode_index) &&
	    (rdev->pm.requested_power_state_index == rdev->pm.current_power_state_index))
		return;

	mutex_lock(&rdev->ddev->struct_mutex);
	mutex_lock(&rdev->vram_mutex);
	mutex_lock(&rdev->cp.mutex);

	/* gui idle int has issues on older chips it seems */
	if (rdev->family >= CHIP_R600) {
		if (rdev->irq.installed) {
			/* wait for GPU idle */
			rdev->pm.gui_idle = false;
			rdev->irq.gui_idle = true;
			radeon_irq_set(rdev);
			wait_event_interruptible_timeout(
				rdev->irq.idle_queue, rdev->pm.gui_idle,
				msecs_to_jiffies(RADEON_WAIT_IDLE_TIMEOUT));
			rdev->irq.gui_idle = false;
			radeon_irq_set(rdev);
		}
	} else {
		if (rdev->cp.ready) {
			struct radeon_fence *fence;
			radeon_ring_alloc(rdev, 64);
			radeon_fence_create(rdev, &fence);
			radeon_fence_emit(rdev, fence);
			radeon_ring_commit(rdev);
			radeon_fence_wait(fence, false);
			radeon_fence_unref(&fence);
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

	mutex_unlock(&rdev->cp.mutex);
	mutex_unlock(&rdev->vram_mutex);
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
				DRM_DEBUG_DRIVER("\t\t%d e: %d%s\n",
					j,
					clock_info->sclk * 10,
					clock_info->flags & RADEON_PM_MODE_NO_DISPLAY ? "\tNo display only" : "");
			else
				DRM_DEBUG_DRIVER("\t\t%d e: %d\tm: %d\tv: %d%s\n",
					j,
					clock_info->sclk * 10,
					clock_info->mclk * 10,
					clock_info->voltage.voltage,
					clock_info->flags & RADEON_PM_MODE_NO_DISPLAY ? "\tNo display only" : "");
		}
	}
}

static ssize_t radeon_get_pm_profile(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct drm_device *ddev = pci_get_drvdata(to_pci_dev(dev));
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
	struct drm_device *ddev = pci_get_drvdata(to_pci_dev(dev));
	struct radeon_device *rdev = ddev->dev_private;

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
	struct drm_device *ddev = pci_get_drvdata(to_pci_dev(dev));
	struct radeon_device *rdev = ddev->dev_private;
	int pm = rdev->pm.pm_method;

	return snprintf(buf, PAGE_SIZE, "%s\n",
			(pm == PM_METHOD_DYNPM) ? "dynpm" : "profile");
}

static ssize_t radeon_set_pm_method(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count)
{
	struct drm_device *ddev = pci_get_drvdata(to_pci_dev(dev));
	struct radeon_device *rdev = ddev->dev_private;


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

static DEVICE_ATTR(power_profile, S_IRUGO | S_IWUSR, radeon_get_pm_profile, radeon_set_pm_profile);
static DEVICE_ATTR(power_method, S_IRUGO | S_IWUSR, radeon_get_pm_method, radeon_set_pm_method);

static ssize_t radeon_hwmon_show_temp(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct drm_device *ddev = pci_get_drvdata(to_pci_dev(dev));
	struct radeon_device *rdev = ddev->dev_private;
	int temp;

	switch (rdev->pm.int_thermal_type) {
	case THERMAL_TYPE_RV6XX:
		temp = rv6xx_get_temp(rdev);
		break;
	case THERMAL_TYPE_RV770:
		temp = rv770_get_temp(rdev);
		break;
	case THERMAL_TYPE_EVERGREEN:
	case THERMAL_TYPE_NI:
		temp = evergreen_get_temp(rdev);
		break;
	case THERMAL_TYPE_SUMO:
		temp = sumo_get_temp(rdev);
		break;
	default:
		temp = 0;
		break;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", temp);
}

static ssize_t radeon_hwmon_show_name(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return sprintf(buf, "radeon\n");
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, radeon_hwmon_show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(name, S_IRUGO, radeon_hwmon_show_name, NULL, 0);

static struct attribute *hwmon_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_name.dev_attr.attr,
	NULL
};

static const struct attribute_group hwmon_attrgroup = {
	.attrs = hwmon_attributes,
};

static int radeon_hwmon_init(struct radeon_device *rdev)
{
	int err = 0;

	rdev->pm.int_hwmon_dev = NULL;

	switch (rdev->pm.int_thermal_type) {
	case THERMAL_TYPE_RV6XX:
	case THERMAL_TYPE_RV770:
	case THERMAL_TYPE_EVERGREEN:
	case THERMAL_TYPE_NI:
	case THERMAL_TYPE_SUMO:
		rdev->pm.int_hwmon_dev = hwmon_device_register(rdev->dev);
		if (IS_ERR(rdev->pm.int_hwmon_dev)) {
			err = PTR_ERR(rdev->pm.int_hwmon_dev);
			dev_err(rdev->dev,
				"Unable to register hwmon device: %d\n", err);
			break;
		}
		dev_set_drvdata(rdev->pm.int_hwmon_dev, rdev->ddev);
		err = sysfs_create_group(&rdev->pm.int_hwmon_dev->kobj,
					 &hwmon_attrgroup);
		if (err) {
			dev_err(rdev->dev,
				"Unable to create hwmon sysfs file: %d\n", err);
			hwmon_device_unregister(rdev->dev);
		}
		break;
	default:
		break;
	}

	return err;
}

static void radeon_hwmon_fini(struct radeon_device *rdev)
{
	if (rdev->pm.int_hwmon_dev) {
		sysfs_remove_group(&rdev->pm.int_hwmon_dev->kobj, &hwmon_attrgroup);
		hwmon_device_unregister(rdev->pm.int_hwmon_dev);
	}
}

void radeon_pm_suspend(struct radeon_device *rdev)
{
	mutex_lock(&rdev->pm.mutex);
	if (rdev->pm.pm_method == PM_METHOD_DYNPM) {
		if (rdev->pm.dynpm_state == DYNPM_STATE_ACTIVE)
			rdev->pm.dynpm_state = DYNPM_STATE_SUSPENDED;
	}
	mutex_unlock(&rdev->pm.mutex);

	cancel_delayed_work_sync(&rdev->pm.dynpm_idle_work);
}

void radeon_pm_resume(struct radeon_device *rdev)
{
	/* set up the default clocks if the MC ucode is loaded */
	if (ASIC_IS_DCE5(rdev) && rdev->mc_fw) {
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
	rdev->pm.current_vddc = rdev->pm.power_state[rdev->pm.default_power_state_index].clock_info[0].voltage.voltage;
	rdev->pm.current_vddci = rdev->pm.power_state[rdev->pm.default_power_state_index].clock_info[0].voltage.vddci;
	if (rdev->pm.pm_method == PM_METHOD_DYNPM
	    && rdev->pm.dynpm_state == DYNPM_STATE_SUSPENDED) {
		rdev->pm.dynpm_state = DYNPM_STATE_ACTIVE;
		schedule_delayed_work(&rdev->pm.dynpm_idle_work,
				      msecs_to_jiffies(RADEON_IDLE_LOOP_MS));
	}
	mutex_unlock(&rdev->pm.mutex);
	radeon_pm_compute_clocks(rdev);
}

int radeon_pm_init(struct radeon_device *rdev)
{
	int ret;

	/* default to profile method */
	rdev->pm.pm_method = PM_METHOD_PROFILE;
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
		if (ASIC_IS_DCE5(rdev) && rdev->mc_fw) {
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

#ifdef CONFIG_ACPI
		rdev->acpi_nb.notifier_call = radeon_acpi_event;
		register_acpi_notifier(&rdev->acpi_nb);
#endif
		if (radeon_debugfs_pm_init(rdev)) {
			DRM_ERROR("Failed to register debugfs file for PM!\n");
		}

		DRM_INFO("radeon: power management initialized\n");
	}

	return 0;
}

void radeon_pm_fini(struct radeon_device *rdev)
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
#ifdef CONFIG_ACPI
		unregister_acpi_notifier(&rdev->acpi_nb);
#endif
	}

	if (rdev->pm.power_state)
		kfree(rdev->pm.power_state);

	radeon_hwmon_fini(rdev);
}

void radeon_pm_compute_clocks(struct radeon_device *rdev)
{
	struct drm_device *ddev = rdev->ddev;
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;

	if (rdev->pm.num_power_states < 2)
		return;

	mutex_lock(&rdev->pm.mutex);

	rdev->pm.active_crtcs = 0;
	rdev->pm.active_crtc_count = 0;
	list_for_each_entry(crtc,
		&ddev->mode_config.crtc_list, head) {
		radeon_crtc = to_radeon_crtc(crtc);
		if (radeon_crtc->enabled) {
			rdev->pm.active_crtcs |= (1 << radeon_crtc->crtc_id);
			rdev->pm.active_crtc_count++;
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

static bool radeon_pm_in_vbl(struct radeon_device *rdev)
{
	int  crtc, vpos, hpos, vbl_status;
	bool in_vbl = true;

	/* Iterate over all active crtc's. All crtc's must be in vblank,
	 * otherwise return in_vbl == false.
	 */
	for (crtc = 0; (crtc < rdev->num_crtc) && in_vbl; crtc++) {
		if (rdev->pm.active_crtcs & (1 << crtc)) {
			vbl_status = radeon_get_crtc_scanoutpos(rdev->ddev, crtc, &vpos, &hpos);
			if ((vbl_status & DRM_SCANOUTPOS_VALID) &&
			    !(vbl_status & DRM_SCANOUTPOS_INVBL))
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
		unsigned long irq_flags;
		int not_processed = 0;

		read_lock_irqsave(&rdev->fence_drv.lock, irq_flags);
		if (!list_empty(&rdev->fence_drv.emited)) {
			struct list_head *ptr;
			list_for_each(ptr, &rdev->fence_drv.emited) {
				/* count up to 3, that's enought info */
				if (++not_processed >= 3)
					break;
			}
		}
		read_unlock_irqrestore(&rdev->fence_drv.lock, irq_flags);

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

	seq_printf(m, "default engine clock: %u0 kHz\n", rdev->pm.default_sclk);
	seq_printf(m, "current engine clock: %u0 kHz\n", radeon_get_engine_clock(rdev));
	seq_printf(m, "default memory clock: %u0 kHz\n", rdev->pm.default_mclk);
	if (rdev->asic->get_memory_clock)
		seq_printf(m, "current memory clock: %u0 kHz\n", radeon_get_memory_clock(rdev));
	if (rdev->pm.current_vddc)
		seq_printf(m, "voltage: %u mV\n", rdev->pm.current_vddc);
	if (rdev->asic->get_pcie_lanes)
		seq_printf(m, "PCIE lanes: %d\n", radeon_get_pcie_lanes(rdev));

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

// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 */

#include "dc.h"
#include "dc/dc_dmub_srv.h"
#include "dc/dc_state.h"
#include "dc/dc_stat.h"

#include "amdgpu.h"
#include "amdgpu_display.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_backlight.h"
#include "amdgpu_dm_psr.h"
#include "amdgpu_dm_replay.h"
#include "amdgpu_atombios.h"

#include "modules/inc/mod_power.h"

#include <linux/backlight.h>
#include <linux/power_supply.h>
#include <drm/drm_edid.h>
#include <drm/drm_utils.h>

#include <acpi/video.h>

#include "amdgpu_dm_trace.h"
#include "amd_shared.h"
#include "amdgpu_dm_kunit_helpers.h"

void amdgpu_dm_update_backlight_caps(struct amdgpu_display_manager *dm,
				     int bl_idx)
{
	struct amdgpu_dm_backlight_caps *caps = &dm->backlight_caps[bl_idx];

	if (caps->caps_valid)
		return;

#if defined(CONFIG_ACPI)
	amdgpu_acpi_get_backlight_caps(caps);

	/* validate the firmware value is sane */
	if (caps->caps_valid) {
		int spread = caps->max_input_signal - caps->min_input_signal;

		if (caps->max_input_signal > AMDGPU_DM_DEFAULT_MAX_BACKLIGHT ||
		    caps->min_input_signal < 0 ||
		    spread > AMDGPU_DM_DEFAULT_MAX_BACKLIGHT ||
		    spread < AMDGPU_DM_MIN_SPREAD) {
			drm_dbg_kms(adev_to_drm(dm->adev), "DM: Invalid backlight caps: min=%d, max=%d\n",
				      caps->min_input_signal, caps->max_input_signal);
			caps->caps_valid = false;
		}
	}
#else
	if (caps->aux_support)
		return;
#endif
	if (!caps->caps_valid) {
		caps->min_input_signal = AMDGPU_DM_DEFAULT_MIN_BACKLIGHT;
		caps->max_input_signal = AMDGPU_DM_DEFAULT_MAX_BACKLIGHT;
		caps->ac_level = caps->dc_level = 50;
		caps->caps_valid = true;
	}
}
EXPORT_IF_KUNIT(amdgpu_dm_update_backlight_caps);

STATIC_IFN_KUNIT
int get_brightness_range(const struct amdgpu_dm_backlight_caps *caps,
			 unsigned int *min, unsigned int *max)
{
	if (!caps)
		return 0;

	if (caps->aux_support) {
		/* Firmware limits are in nits, DC API wants millinits. */
		*max = 1000 * caps->aux_max_input_signal;
		*min = 1000 * caps->aux_min_input_signal;
	} else {
		/* Firmware limits are 8-bit, PWM control is 16-bit. */
		*max = 0x101 * caps->max_input_signal;
		*min = 0x101 * caps->min_input_signal;
	}
	return 1;
}
EXPORT_IF_KUNIT(get_brightness_range);

/* Rescale from [min..max] to [0..AMDGPU_MAX_BL_LEVEL] */
static inline u32 scale_input_to_fw(int min, int max, u64 input)
{
	return DIV_ROUND_CLOSEST_ULL(input * AMDGPU_MAX_BL_LEVEL, max - min);
}

/* Rescale from [0..AMDGPU_MAX_BL_LEVEL] to [min..max] */
static inline u32 scale_fw_to_input(int min, int max, u64 input)
{
	return min + DIV_ROUND_CLOSEST_ULL(input * (max - min), AMDGPU_MAX_BL_LEVEL);
}

STATIC_IFN_KUNIT
void convert_custom_brightness(const struct amdgpu_dm_backlight_caps *caps,
			       unsigned int min, unsigned int max,
			       uint32_t *user_brightness)
{
	u32 brightness = scale_input_to_fw(min, max, *user_brightness);
	u8 lower_signal, upper_signal, upper_lum, lower_lum, lum;
	int left, right;

	if (amdgpu_dc_debug_mask & DC_DISABLE_CUSTOM_BRIGHTNESS_CURVE)
		return;

	if (!caps->data_points)
		return;

	/*
	 * Handle the case where brightness is below the first data point
	 * Interpolate between (0,0) and (first_signal, first_lum)
	 */
	if (brightness < caps->luminance_data[0].input_signal) {
		lum = DIV_ROUND_CLOSEST(caps->luminance_data[0].luminance * brightness,
					caps->luminance_data[0].input_signal);
		goto scale;
	}

	left = 0;
	right = caps->data_points - 1;
	while (left <= right) {
		int mid = left + (right - left) / 2;
		u8 signal = caps->luminance_data[mid].input_signal;

		/* Exact match found */
		if (signal == brightness) {
			lum = caps->luminance_data[mid].luminance;
			goto scale;
		}

		if (signal < brightness)
			left = mid + 1;
		else
			right = mid - 1;
	}

	/* verify bound */
	if (left >= caps->data_points)
		left = caps->data_points - 1;

	/* At this point, left > right */
	lower_signal = caps->luminance_data[right].input_signal;
	upper_signal = caps->luminance_data[left].input_signal;
	lower_lum = caps->luminance_data[right].luminance;
	upper_lum = caps->luminance_data[left].luminance;

	/* interpolate */
	if (right == left || !lower_lum)
		lum = upper_lum;
	else
		lum = lower_lum + DIV_ROUND_CLOSEST((upper_lum - lower_lum) *
						    (brightness - lower_signal),
						    upper_signal - lower_signal);
scale:
	*user_brightness = scale_fw_to_input(min, max,
					     DIV_ROUND_CLOSEST(lum * brightness, 101));
}

EXPORT_IF_KUNIT(convert_custom_brightness);

STATIC_IFN_KUNIT
u32 convert_brightness_from_user(const struct amdgpu_dm_backlight_caps *caps,
				uint32_t brightness)
{
	unsigned int min, max;

	if (!get_brightness_range(caps, &min, &max))
		return brightness;

	convert_custom_brightness(caps, min, max, &brightness);

	/* Rescale 0..max to min..max */
	return min + DIV_ROUND_CLOSEST_ULL((u64)(max - min) * brightness, max);
}

EXPORT_IF_KUNIT(convert_brightness_from_user);

STATIC_IFN_KUNIT
u32 convert_brightness_to_user(const struct amdgpu_dm_backlight_caps *caps,
			      uint32_t brightness)
{
	unsigned int min, max;

	if (!get_brightness_range(caps, &min, &max))
		return brightness;

	if (brightness < min)
		return 0;
	/* Rescale min..max to 0..max */
	return DIV_ROUND_CLOSEST_ULL((u64)max * (brightness - min),
				 max - min);
}
EXPORT_IF_KUNIT(convert_brightness_to_user);

STATIC_IFN_KUNIT
struct dc_stream_state *dm_find_stream_with_link(
	struct amdgpu_display_manager *dm,
	struct dc_link *link)
{
	struct dc_state *cur_dc_state = dm->dc->current_state;
	struct dc_stream_state *stream = NULL;
	int i;

	for (i = 0; i < cur_dc_state->stream_count; i++) {
		stream = cur_dc_state->streams[i];
		if (stream->link == link)
			return stream;
	}

	return NULL;
}
EXPORT_IF_KUNIT(dm_find_stream_with_link);

STATIC_IFN_KUNIT
int amdgpu_dm_backlight_get_device_index(struct amdgpu_display_manager *dm,
					 struct backlight_device *bd)
{
	int i;

	for (i = 0; i < dm->num_of_edps; i++) {
		if (bd == dm->backlight_dev[i])
			return i;
	}

	return 0;
}
EXPORT_IF_KUNIT(amdgpu_dm_backlight_get_device_index);

void amdgpu_dm_backlight_set_level(struct amdgpu_display_manager *dm,
				   int bl_idx,
				   u32 user_brightness)
{
	struct amdgpu_dm_backlight_caps *caps;
	struct dc_link *link;
	u32 brightness = 0;
	bool rc = false, reallow_idle = false;
	struct drm_connector *connector;
	struct dc_stream_state *stream;
	unsigned int min, max;

	list_for_each_entry(connector, &dm->ddev->mode_config.connector_list, head) {
		struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);

		if (aconnector->bl_idx != bl_idx)
			continue;

		/* if connector is off, save the brightness for next time it's on */
		if (!aconnector->base.encoder) {
			dm->brightness[bl_idx] = user_brightness;
			dm->actual_brightness[bl_idx] = 0;
			return;
		}
	}

	amdgpu_dm_update_backlight_caps(dm, bl_idx);
	caps = &dm->backlight_caps[bl_idx];

	dm->brightness[bl_idx] = user_brightness;
	/* update scratch register */
	if (bl_idx == 0)
		amdgpu_atombios_scratch_regs_set_backlight_level(dm->adev, dm->brightness[bl_idx]);
	brightness = convert_brightness_from_user(caps, dm->brightness[bl_idx]);
	link = (struct dc_link *)dm->backlight_link[bl_idx];

	/* Apply brightness quirk */
	if (caps->brightness_mask)
		brightness |= caps->brightness_mask;

	if (trace_amdgpu_dm_brightness_enabled()) {
		trace_amdgpu_dm_brightness(__builtin_return_address(0),
					   user_brightness,
					   brightness,
					   caps->aux_support,
					   power_supply_is_system_supplied() > 0);
	}

	stream = dm_find_stream_with_link(dm, link);
	if (!stream)
		return;

	mutex_lock(&dm->dc_lock);
	if (dm->dc->caps.ips_support && dm->dc->ctx->dmub_srv->idle_allowed) {
		dc_allow_idle_optimizations(dm->dc, false);
		reallow_idle = true;
	}

	if (caps->aux_support) {
		rc = mod_power_set_backlight_nits(dm->power_module, stream, brightness,
			AUX_BL_DEFAULT_TRANSITION_TIME_MS, false, true);
	} else {
		/* power module uses millipercent */
		get_brightness_range(caps, &min, &max);
		brightness = DIV_ROUND_CLOSEST(brightness * 100, (max - min)) * 1000;
		rc = mod_power_set_backlight_percent(dm->power_module, stream,
						     brightness, 0, false);
	}

	/*
	 * Some kms clients create a ramped backlight transition effect
	 * by rapidly changing the backlight. Yet we must wait on dmcub
	 * fw to exit psr/replay before programming backlight. To
	 * prevent lag, keep disable psr/replay and let the next atomic
	 * flip clear the event.
	 *
	 * ToDo: use ISM to handle rapidly backlight change
	 *
	 * Rapidly backlight change is similar to rapidly cursor events,
	 * which is now handled by ISM. ISM can delay the event until system
	 * is really idle, so we may use ISM to handle backlight change as well.
	 */
	amdgpu_dm_psr_set_event(dm, stream, true,
		psr_event_hw_programming, true);
	amdgpu_dm_replay_set_event(dm, stream, true,
		replay_event_hw_programming, true);

	if (dm->dc->caps.ips_support && reallow_idle)
		dc_allow_idle_optimizations(dm->dc, true);

	mutex_unlock(&dm->dc_lock);

	if (rc)
		dm->actual_brightness[bl_idx] = user_brightness;
}
EXPORT_IF_KUNIT(amdgpu_dm_backlight_set_level);

STATIC_IFN_KUNIT
int amdgpu_dm_backlight_update_status(struct backlight_device *bd)
{
	struct amdgpu_display_manager *dm = bl_get_data(bd);
	int i = amdgpu_dm_backlight_get_device_index(dm, bd);

	amdgpu_dm_backlight_set_level(dm, i, bd->props.brightness);

	return 0;
}
EXPORT_IF_KUNIT(amdgpu_dm_backlight_update_status);

STATIC_IFN_KUNIT
u32 amdgpu_dm_backlight_get_level(struct amdgpu_display_manager *dm, int bl_idx)
{
	int ret;
	struct amdgpu_dm_backlight_caps caps;
	struct dc_link *link = (struct dc_link *)dm->backlight_link[bl_idx];

	amdgpu_dm_update_backlight_caps(dm, bl_idx);
	caps = dm->backlight_caps[bl_idx];

	if (caps.aux_support) {
		u32 avg, peak;

		if (!dc_link_get_backlight_level_nits(link, &avg, &peak))
			return dm->brightness[bl_idx];
		return convert_brightness_to_user(&caps, avg);
	}

	ret = dc_link_get_backlight_level(link);

	if (ret == DC_ERROR_UNEXPECTED)
		return dm->brightness[bl_idx];

	return convert_brightness_to_user(&caps, ret);
}
EXPORT_IF_KUNIT(amdgpu_dm_backlight_get_level);

STATIC_IFN_KUNIT
int amdgpu_dm_backlight_get_brightness(struct backlight_device *bd)
{
	struct amdgpu_display_manager *dm = bl_get_data(bd);
	int i = amdgpu_dm_backlight_get_device_index(dm, bd);

	return amdgpu_dm_backlight_get_level(dm, i);
}
EXPORT_IF_KUNIT(amdgpu_dm_backlight_get_brightness);

static const struct backlight_ops amdgpu_dm_backlight_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = amdgpu_dm_backlight_get_brightness,
	.update_status	= amdgpu_dm_backlight_update_status,
};

STATIC_IFN_KUNIT
void amdgpu_dm_backlight_fill_props(const struct amdgpu_dm_backlight_caps *caps,
				    bool is_system_supplied,
				    bool custom_curve_enabled,
				    struct backlight_properties *props)
{
	unsigned int min, max;

	if (get_brightness_range(caps, &min, &max)) {
		if (is_system_supplied)
			props->brightness = DIV_ROUND_CLOSEST(max * caps->ac_level,
							       100);
		else
			props->brightness = DIV_ROUND_CLOSEST(max * caps->dc_level,
							       100);
		props->max_brightness = max;
	} else {
		props->brightness = MAX_BACKLIGHT_LEVEL;
		props->max_brightness = MAX_BACKLIGHT_LEVEL;
	}

	if (caps && caps->data_points && custom_curve_enabled)
		props->scale = BACKLIGHT_SCALE_NON_LINEAR;
	else
		props->scale = BACKLIGHT_SCALE_LINEAR;
	props->type = BACKLIGHT_RAW;
}
EXPORT_IF_KUNIT(amdgpu_dm_backlight_fill_props);

void
amdgpu_dm_register_backlight_device(struct amdgpu_dm_connector *aconnector)
{
	struct drm_device *drm = aconnector->base.dev;
	struct amdgpu_display_manager *dm = &drm_to_adev(drm)->dm;
	struct backlight_properties props = { 0 };
	struct amdgpu_dm_backlight_caps *caps;
	char bl_name[16];
	int real_brightness;
	int init_brightness;

	if (aconnector->bl_idx == -1)
		return;

	if (!acpi_video_backlight_use_native()) {
		drm_info(drm, "Skipping amdgpu DM backlight registration\n");
		/* Try registering an ACPI video backlight device instead. */
		acpi_video_register_backlight();
		return;
	}

	caps = &dm->backlight_caps[aconnector->bl_idx];
	amdgpu_dm_backlight_fill_props(caps, power_supply_is_system_supplied() > 0,
				       !(amdgpu_dc_debug_mask &
					 DC_DISABLE_CUSTOM_BRIGHTNESS_CURVE),
				       &props);
	drm_dbg(drm, "Backlight caps: max_brightness: %d, ac %d, dc %d\n",
		props.max_brightness, caps->ac_level, caps->dc_level);

	init_brightness = props.brightness;

	if (props.scale == BACKLIGHT_SCALE_NON_LINEAR)
		drm_info(drm, "Using custom brightness curve\n");

	snprintf(bl_name, sizeof(bl_name), "amdgpu_bl%d",
		 drm->primary->index + aconnector->bl_idx);

	dm->backlight_dev[aconnector->bl_idx] =
		backlight_device_register(bl_name, aconnector->base.kdev, dm,
					  &amdgpu_dm_backlight_ops, &props);
	dm->brightness[aconnector->bl_idx] = props.brightness;

	if (IS_ERR(dm->backlight_dev[aconnector->bl_idx])) {
		drm_err(drm, "DM: Backlight registration failed!\n");
		dm->backlight_dev[aconnector->bl_idx] = NULL;
	} else {
		/*
		 * dm->brightness[x] can be inconsistent just after startup until
		 * ops.get_brightness is called.
		 */
		real_brightness =
			amdgpu_dm_backlight_ops.get_brightness(dm->backlight_dev[aconnector->bl_idx]);

		if (real_brightness != init_brightness) {
			dm->actual_brightness[aconnector->bl_idx] = real_brightness;
			dm->brightness[aconnector->bl_idx] = real_brightness;
		}
		drm_dbg_driver(drm, "DM: Registered Backlight device: %s\n", bl_name);
	}
}
EXPORT_IF_KUNIT(amdgpu_dm_register_backlight_device);

void amdgpu_dm_update_connector_ext_caps(struct amdgpu_dm_connector *aconnector)
{
	const struct drm_panel_backlight_quirk *panel_backlight_quirk;
	struct amdgpu_dm_backlight_caps *caps;
	struct drm_connector *conn_base;
	struct amdgpu_device *adev;
	struct drm_luminance_range_info *luminance_range;
	struct drm_device *drm;

	if (aconnector->bl_idx == -1 ||
	    aconnector->dc_link->connector_signal != SIGNAL_TYPE_EDP)
		return;

	conn_base = &aconnector->base;
	drm = conn_base->dev;
	adev = drm_to_adev(drm);

	caps = &adev->dm.backlight_caps[aconnector->bl_idx];
	caps->ext_caps = &aconnector->dc_link->dpcd_sink_ext_caps;
	caps->aux_support = false;

	panel_backlight_quirk = drm_get_panel_backlight_quirk(aconnector->drm_edid);

	if (caps->ext_caps->bits.oled == 1
	    /*
	     * ||
	     * caps->ext_caps->bits.sdr_aux_backlight_control == 1 ||
	     * caps->ext_caps->bits.hdr_aux_backlight_control == 1
	     */)
		caps->aux_support = true;

	if (amdgpu_backlight == 0)
		caps->aux_support = false;
	else if (amdgpu_backlight == 1)
		caps->aux_support = true;
	else if (!IS_ERR_OR_NULL(panel_backlight_quirk) &&
		 panel_backlight_quirk->force_pwm)
		caps->aux_support = false;
	if (caps->aux_support)
		aconnector->dc_link->backlight_control_type = BACKLIGHT_CONTROL_AMD_AUX;

	luminance_range = &conn_base->display_info.luminance_range;

	if (luminance_range->max_luminance)
		caps->aux_max_input_signal = luminance_range->max_luminance;
	else
		caps->aux_max_input_signal = 512;

	if (luminance_range->min_luminance)
		caps->aux_min_input_signal = luminance_range->min_luminance;
	else
		caps->aux_min_input_signal = 1;

	if (!IS_ERR_OR_NULL(panel_backlight_quirk)) {
		if (panel_backlight_quirk->min_brightness) {
			caps->min_input_signal =
				panel_backlight_quirk->min_brightness - 1;
			drm_info(drm,
				 "Applying panel backlight quirk, min_brightness: %d\n",
				 caps->min_input_signal);
		}
		if (panel_backlight_quirk->brightness_mask) {
			drm_info(drm,
				 "Applying panel backlight quirk, brightness_mask: 0x%X\n",
				 panel_backlight_quirk->brightness_mask);
			caps->brightness_mask =
				panel_backlight_quirk->brightness_mask;
		}
	}
}
EXPORT_IF_KUNIT(amdgpu_dm_update_connector_ext_caps);

void amdgpu_dm_setup_backlight_device(struct amdgpu_display_manager *dm,
			    struct amdgpu_dm_connector *aconnector)
{
	struct dc_link *link = aconnector->dc_link;
	int bl_idx = dm->num_of_edps;

	if (!(link->connector_signal & (SIGNAL_TYPE_EDP | SIGNAL_TYPE_LVDS)) ||
	    link->type == dc_connection_none)
		return;

	if (dm->num_of_edps >= AMDGPU_DM_MAX_NUM_EDP) {
		drm_warn(adev_to_drm(dm->adev), "Too much eDP connections, skipping backlight setup for additional eDPs\n");
		return;
	}

	aconnector->bl_idx = bl_idx;

	amdgpu_dm_update_backlight_caps(dm, bl_idx);
	dm->backlight_link[bl_idx] = link;
	dm->num_of_edps++;

	amdgpu_dm_update_connector_ext_caps(aconnector);

	/* Offer ABM property when user didn't turn off by module parameter.
	 * OLED panels are included to support CACP (Content Adaptive
	 * Contrast and Power) feature via set_abm_level.
	 */
	if (amdgpu_dm_abm_level < 0)
		drm_object_attach_property(&aconnector->base.base,
					   dm->adev->mode_info.abm_level_property,
					   ABM_SYSFS_CONTROL);
}
EXPORT_IF_KUNIT(amdgpu_dm_setup_backlight_device);

/**
 * DOC: panel power savings
 *
 * The display manager allows you to set your desired **panel power savings**
 * level (between 0-4, with 0 representing off), e.g. using the following::
 *
 *   # echo 3 > /sys/class/drm/card0-eDP-1/amdgpu/panel_power_savings
 *
 * Modifying this value can have implications on color accuracy, so tread
 * carefully.
 */

STATIC_IFN_KUNIT
ssize_t panel_power_savings_show(struct device *device,
				 struct device_attribute *attr,
				 char *buf)
{
	struct drm_connector *connector = dev_get_drvdata(device);
	struct drm_device *dev = connector->dev;
	u8 val;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	val = to_dm_connector_state(connector->state)->abm_level ==
		ABM_LEVEL_IMMEDIATE_DISABLE ? 0 :
		to_dm_connector_state(connector->state)->abm_level;
	drm_modeset_unlock(&dev->mode_config.connection_mutex);

	return sysfs_emit(buf, "%u\n", val);
}
EXPORT_IF_KUNIT(panel_power_savings_show);

STATIC_IFN_KUNIT
ssize_t panel_power_savings_store(struct device *device,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct drm_connector *connector = dev_get_drvdata(device);
	struct drm_device *dev = connector->dev;
	long val;
	int ret;

	ret = kstrtol(buf, 0, &val);

	if (ret)
		return ret;

	if (val < 0 || val > 4)
		return -EINVAL;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	if (to_dm_connector_state(connector->state)->abm_sysfs_forbidden)
		ret = -EBUSY;
	else
		to_dm_connector_state(connector->state)->abm_level = val ?:
			ABM_LEVEL_IMMEDIATE_DISABLE;
	drm_modeset_unlock(&dev->mode_config.connection_mutex);

	if (ret)
		return ret;

	drm_kms_helper_hotplug_event(dev);

	return count;
}
EXPORT_IF_KUNIT(panel_power_savings_store);

static DEVICE_ATTR_RW(panel_power_savings);

static struct attribute *amdgpu_attrs[] = {
	&dev_attr_panel_power_savings.attr,
	NULL
};

const struct attribute_group amdgpu_group = {
	.name = "amdgpu",
	.attrs = amdgpu_attrs
};

bool
amdgpu_dm_should_create_sysfs(struct amdgpu_dm_connector *amdgpu_dm_connector)
{
	struct dc_link *link = amdgpu_dm_connector->dc_link;

	if (amdgpu_dm_abm_level >= 0)
		return false;

	if (amdgpu_dm_connector->base.connector_type != DRM_MODE_CONNECTOR_eDP)
		return false;

	if (link->panel_type != PANEL_TYPE_LCD && !link->panel_config.cacp.cacp_supported)
		return false;

	return true;
}
EXPORT_IF_KUNIT(amdgpu_dm_should_create_sysfs);

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
uint amdgpu_dm_get_dc_debug_mask(void)
{
	return amdgpu_dc_debug_mask;
}
EXPORT_IF_KUNIT(amdgpu_dm_get_dc_debug_mask);

void amdgpu_dm_set_dc_debug_mask(uint val)
{
	amdgpu_dc_debug_mask = val;
}
EXPORT_IF_KUNIT(amdgpu_dm_set_dc_debug_mask);

int amdgpu_dm_get_abm_level_param(void)
{
	return amdgpu_dm_abm_level;
}
EXPORT_IF_KUNIT(amdgpu_dm_get_abm_level_param);

void amdgpu_dm_set_abm_level_param(int val)
{
	amdgpu_dm_abm_level = val;
}
EXPORT_IF_KUNIT(amdgpu_dm_set_abm_level_param);

int amdgpu_dm_get_backlight_param(void)
{
	return amdgpu_backlight;
}
EXPORT_IF_KUNIT(amdgpu_dm_get_backlight_param);

void amdgpu_dm_set_backlight_param(int val)
{
	amdgpu_backlight = val;
}
EXPORT_IF_KUNIT(amdgpu_dm_set_backlight_param);
#endif

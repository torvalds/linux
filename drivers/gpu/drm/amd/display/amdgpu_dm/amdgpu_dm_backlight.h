/* SPDX-License-Identifier: MIT */
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
 */

#ifndef __AMDGPU_DM_BACKLIGHT_H__
#define __AMDGPU_DM_BACKLIGHT_H__

struct amdgpu_display_manager;
struct amdgpu_dm_connector;
struct backlight_device;
struct backlight_properties;
struct dc_link;
struct dc_stream_state;
struct device;
struct device_attribute;
struct drm_connector;
struct attribute_group;

#define AMDGPU_DM_DEFAULT_MIN_BACKLIGHT 12
#define AMDGPU_DM_DEFAULT_MAX_BACKLIGHT 255
#define AMDGPU_DM_MIN_SPREAD ((AMDGPU_DM_DEFAULT_MAX_BACKLIGHT - AMDGPU_DM_DEFAULT_MIN_BACKLIGHT) / 2)
#define AUX_BL_DEFAULT_TRANSITION_TIME_MS 50

void amdgpu_dm_update_backlight_caps(struct amdgpu_display_manager *dm,
				     int bl_idx);
void amdgpu_dm_backlight_set_level(struct amdgpu_display_manager *dm,
				   int bl_idx, u32 user_brightness);
void amdgpu_dm_register_backlight_device(struct amdgpu_dm_connector *aconnector);
void amdgpu_dm_setup_backlight_device(struct amdgpu_display_manager *dm,
			    struct amdgpu_dm_connector *aconnector);
void amdgpu_dm_update_connector_ext_caps(struct amdgpu_dm_connector *aconnector);
bool amdgpu_dm_should_create_sysfs(struct amdgpu_dm_connector *aconnector);

extern const struct attribute_group amdgpu_group;

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
struct dc_stream_state *dm_find_stream_with_link(struct amdgpu_display_manager *dm,
						 struct dc_link *link);
int amdgpu_dm_backlight_update_status(struct backlight_device *bd);
u32 amdgpu_dm_backlight_get_level(struct amdgpu_display_manager *dm, int bl_idx);
int amdgpu_dm_backlight_get_brightness(struct backlight_device *bd);
ssize_t panel_power_savings_show(struct device *device,
				 struct device_attribute *attr,
				 char *buf);
ssize_t panel_power_savings_store(struct device *device,
				 struct device_attribute *attr,
				 const char *buf, size_t count);
int get_brightness_range(const struct amdgpu_dm_backlight_caps *caps,
			 unsigned int *min, unsigned int *max);
void convert_custom_brightness(const struct amdgpu_dm_backlight_caps *caps,
			       unsigned int min, unsigned int max,
			       uint32_t *user_brightness);
u32 convert_brightness_from_user(const struct amdgpu_dm_backlight_caps *caps,
				 uint32_t brightness);
u32 convert_brightness_to_user(const struct amdgpu_dm_backlight_caps *caps,
			       uint32_t brightness);
int amdgpu_dm_backlight_get_device_index(struct amdgpu_display_manager *dm,
					 struct backlight_device *bd);
void amdgpu_dm_backlight_fill_props(const struct amdgpu_dm_backlight_caps *caps,
				    bool is_system_supplied,
				    bool custom_curve_enabled,
				    struct backlight_properties *props);
uint amdgpu_dm_get_dc_debug_mask(void);
void amdgpu_dm_set_dc_debug_mask(uint val);
int amdgpu_dm_get_abm_level_param(void);
void amdgpu_dm_set_abm_level_param(int val);
int amdgpu_dm_get_backlight_param(void);
void amdgpu_dm_set_backlight_param(int val);
#endif

#endif /* __AMDGPU_DM_BACKLIGHT_H__ */

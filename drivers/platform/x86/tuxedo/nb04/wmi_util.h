/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * This code gives functions to avoid code duplication while interacting with
 * the TUXEDO NB04 wmi interfaces.
 *
 * Copyright (C) 2024-2025 Werner Sembach <wse@tuxedocomputers.com>
 */

#ifndef TUXEDO_NB04_WMI_UTIL_H
#define TUXEDO_NB04_WMI_UTIL_H

#include <linux/wmi.h>

#define TUX_GET_DEVICE_STATUS_DEVICE_ID_TOUCHPAD	1
#define TUX_GET_DEVICE_STATUS_DEVICE_ID_KEYBOARD	2
#define TUX_GET_DEVICE_STATUS_DEVICE_ID_APP_PAGES	3

#define TUX_GET_DEVICE_STATUS_KBL_TYPE_NONE		0
#define TUX_GET_DEVICE_STATUS_KBL_TYPE_PER_KEY		1
#define TUX_GET_DEVICE_STATUS_KBL_TYPE_FOUR_ZONE	2
#define TUX_GET_DEVICE_STATUS_KBL_TYPE_WHITE_ONLY	3

#define TUX_GET_DEVICE_STATUS_KEYBOARD_LAYOUT_ANSII	0
#define TUX_GET_DEVICE_STATUS_KEYBOARD_LAYOUT_ISO	1

#define TUX_GET_DEVICE_STATUS_COLOR_ID_RED		1
#define TUX_GET_DEVICE_STATUS_COLOR_ID_GREEN		2
#define TUX_GET_DEVICE_STATUS_COLOR_ID_YELLOW		3
#define TUX_GET_DEVICE_STATUS_COLOR_ID_BLUE		4
#define TUX_GET_DEVICE_STATUS_COLOR_ID_PURPLE		5
#define TUX_GET_DEVICE_STATUS_COLOR_ID_INDIGO		6
#define TUX_GET_DEVICE_STATUS_COLOR_ID_WHITE		7

#define TUX_GET_DEVICE_STATUS_APP_PAGES_DASHBOARD	BIT(0)
#define TUX_GET_DEVICE_STATUS_APP_PAGES_SYSTEMINFOS	BIT(1)
#define TUX_GET_DEVICE_STATUS_APP_PAGES_KBL		BIT(2)
#define TUX_GET_DEVICE_STATUS_APP_PAGES_HOTKEYS		BIT(3)

union tux_wmi_xx_8in_80out_in_t {
	u8 raw[8];
	struct __packed {
		u8 device_type;
		u8 reserved[7];
	} get_device_status_in;
};

union tux_wmi_xx_8in_80out_out_t {
	u8 raw[80];
	struct __packed {
		u16 return_status;
		u8 device_enabled;
		u8 kbl_type;
		u8 kbl_side_bar_supported;
		u8 keyboard_physical_layout;
		u8 app_pages;
		u8 per_key_kbl_default_color;
		u8 four_zone_kbl_default_color_1;
		u8 four_zone_kbl_default_color_2;
		u8 four_zone_kbl_default_color_3;
		u8 four_zone_kbl_default_color_4;
		u8 light_bar_kbl_default_color;
		u8 reserved_0[1];
		u16 dedicated_gpu_id;
		u8 reserved_1[64];
	} get_device_status_out;
};

enum tux_wmi_xx_8in_80out_methods {
	TUX_GET_DEVICE_STATUS	= 2,
};

#define TUX_KBL_SET_MULTIPLE_KEYS_LIGHTING_SETTINGS_COUNT_MAX	120

union tux_wmi_xx_496in_80out_in_t {
	u8 raw[496];
	struct __packed {
		u8 reserved[15];
		u8 rgb_configs_cnt;
		struct tux_kbl_set_multiple_keys_in_rgb_config_t {
			u8 key_id;
			u8 red;
			u8 green;
			u8 blue;
		} rgb_configs[TUX_KBL_SET_MULTIPLE_KEYS_LIGHTING_SETTINGS_COUNT_MAX];
	}  kbl_set_multiple_keys_in;
};

union tux_wmi_xx_496in_80out_out_t {
	u8 raw[80];
	struct __packed {
		u8 return_value;
		u8 reserved[79];
	} kbl_set_multiple_keys_out;
};

enum tux_wmi_xx_496in_80out_methods {
	TUX_KBL_SET_MULTIPLE_KEYS	= 6,
};

int tux_wmi_xx_8in_80out(struct wmi_device *wdev,
			 enum tux_wmi_xx_8in_80out_methods method,
			 union tux_wmi_xx_8in_80out_in_t *in,
			 union tux_wmi_xx_8in_80out_out_t *out);
int tux_wmi_xx_496in_80out(struct wmi_device *wdev,
			   enum tux_wmi_xx_496in_80out_methods method,
			   union tux_wmi_xx_496in_80out_in_t *in,
			   union tux_wmi_xx_496in_80out_out_t *out);

#endif

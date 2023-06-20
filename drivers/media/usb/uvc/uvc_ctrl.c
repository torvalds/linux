// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      uvc_ctrl.c  --  USB Video Class driver - Controls
 *
 *      Copyright (C) 2005-2010
 *          Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/atomic.h>
#include <media/v4l2-ctrls.h>

#include "uvcvideo.h"

#define UVC_CTRL_DATA_CURRENT	0
#define UVC_CTRL_DATA_BACKUP	1
#define UVC_CTRL_DATA_MIN	2
#define UVC_CTRL_DATA_MAX	3
#define UVC_CTRL_DATA_RES	4
#define UVC_CTRL_DATA_DEF	5
#define UVC_CTRL_DATA_LAST	6

/* ------------------------------------------------------------------------
 * Controls
 */

static const struct uvc_control_info uvc_ctrls[] = {
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_BRIGHTNESS_CONTROL,
		.index		= 0,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_CONTRAST_CONTROL,
		.index		= 1,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_HUE_CONTROL,
		.index		= 2,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_SATURATION_CONTROL,
		.index		= 3,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_SHARPNESS_CONTROL,
		.index		= 4,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_GAMMA_CONTROL,
		.index		= 5,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_WHITE_BALANCE_TEMPERATURE_CONTROL,
		.index		= 6,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_WHITE_BALANCE_COMPONENT_CONTROL,
		.index		= 7,
		.size		= 4,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_BACKLIGHT_COMPENSATION_CONTROL,
		.index		= 8,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_GAIN_CONTROL,
		.index		= 9,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_POWER_LINE_FREQUENCY_CONTROL,
		.index		= 10,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_SET_CUR | UVC_CTRL_FLAG_GET_CUR
				| UVC_CTRL_FLAG_GET_DEF | UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_HUE_AUTO_CONTROL,
		.index		= 11,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_SET_CUR | UVC_CTRL_FLAG_GET_CUR
				| UVC_CTRL_FLAG_GET_DEF | UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL,
		.index		= 12,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_SET_CUR | UVC_CTRL_FLAG_GET_CUR
				| UVC_CTRL_FLAG_GET_DEF | UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL,
		.index		= 13,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_SET_CUR | UVC_CTRL_FLAG_GET_CUR
				| UVC_CTRL_FLAG_GET_DEF | UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_DIGITAL_MULTIPLIER_CONTROL,
		.index		= 14,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_DIGITAL_MULTIPLIER_LIMIT_CONTROL,
		.index		= 15,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_ANALOG_VIDEO_STANDARD_CONTROL,
		.index		= 16,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_GET_CUR,
	},
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_ANALOG_LOCK_STATUS_CONTROL,
		.index		= 17,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_GET_CUR,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_SCANNING_MODE_CONTROL,
		.index		= 0,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_SET_CUR | UVC_CTRL_FLAG_GET_CUR
				| UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_AE_MODE_CONTROL,
		.index		= 1,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_SET_CUR | UVC_CTRL_FLAG_GET_CUR
				| UVC_CTRL_FLAG_GET_DEF | UVC_CTRL_FLAG_GET_RES
				| UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_AE_PRIORITY_CONTROL,
		.index		= 2,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_SET_CUR | UVC_CTRL_FLAG_GET_CUR
				| UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL,
		.index		= 3,
		.size		= 4,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_EXPOSURE_TIME_RELATIVE_CONTROL,
		.index		= 4,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_SET_CUR | UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_FOCUS_ABSOLUTE_CONTROL,
		.index		= 5,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_FOCUS_RELATIVE_CONTROL,
		.index		= 6,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR | UVC_CTRL_FLAG_GET_MIN
				| UVC_CTRL_FLAG_GET_MAX | UVC_CTRL_FLAG_GET_RES
				| UVC_CTRL_FLAG_GET_DEF
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_IRIS_ABSOLUTE_CONTROL,
		.index		= 7,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_IRIS_RELATIVE_CONTROL,
		.index		= 8,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_ZOOM_ABSOLUTE_CONTROL,
		.index		= 9,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_ZOOM_RELATIVE_CONTROL,
		.index		= 10,
		.size		= 3,
		.flags		= UVC_CTRL_FLAG_SET_CUR | UVC_CTRL_FLAG_GET_MIN
				| UVC_CTRL_FLAG_GET_MAX | UVC_CTRL_FLAG_GET_RES
				| UVC_CTRL_FLAG_GET_DEF
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_PANTILT_ABSOLUTE_CONTROL,
		.index		= 11,
		.size		= 8,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_PANTILT_RELATIVE_CONTROL,
		.index		= 12,
		.size		= 4,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_ROLL_ABSOLUTE_CONTROL,
		.index		= 13,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_ROLL_RELATIVE_CONTROL,
		.index		= 14,
		.size		= 2,
		.flags		= UVC_CTRL_FLAG_SET_CUR | UVC_CTRL_FLAG_GET_MIN
				| UVC_CTRL_FLAG_GET_MAX | UVC_CTRL_FLAG_GET_RES
				| UVC_CTRL_FLAG_GET_DEF
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_FOCUS_AUTO_CONTROL,
		.index		= 17,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_SET_CUR | UVC_CTRL_FLAG_GET_CUR
				| UVC_CTRL_FLAG_GET_DEF | UVC_CTRL_FLAG_RESTORE,
	},
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_PRIVACY_CONTROL,
		.index		= 18,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_SET_CUR | UVC_CTRL_FLAG_GET_CUR
				| UVC_CTRL_FLAG_RESTORE
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_EXT_GPIO_CONTROLLER,
		.selector	= UVC_CT_PRIVACY_CONTROL,
		.index		= 0,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_GET_CUR
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
};

static const u32 uvc_control_classes[] = {
	V4L2_CID_CAMERA_CLASS,
	V4L2_CID_USER_CLASS,
};

static const struct uvc_menu_info power_line_frequency_controls[] = {
	{ 0, "Disabled" },
	{ 1, "50 Hz" },
	{ 2, "60 Hz" },
};

static const struct uvc_menu_info exposure_auto_controls[] = {
	{ 2, "Auto Mode" },
	{ 1, "Manual Mode" },
	{ 4, "Shutter Priority Mode" },
	{ 8, "Aperture Priority Mode" },
};

static s32 uvc_ctrl_get_zoom(struct uvc_control_mapping *mapping,
	u8 query, const u8 *data)
{
	s8 zoom = (s8)data[0];

	switch (query) {
	case UVC_GET_CUR:
		return (zoom == 0) ? 0 : (zoom > 0 ? data[2] : -data[2]);

	case UVC_GET_MIN:
	case UVC_GET_MAX:
	case UVC_GET_RES:
	case UVC_GET_DEF:
	default:
		return data[2];
	}
}

static void uvc_ctrl_set_zoom(struct uvc_control_mapping *mapping,
	s32 value, u8 *data)
{
	data[0] = value == 0 ? 0 : (value > 0) ? 1 : 0xff;
	data[2] = min((int)abs(value), 0xff);
}

static s32 uvc_ctrl_get_rel_speed(struct uvc_control_mapping *mapping,
	u8 query, const u8 *data)
{
	unsigned int first = mapping->offset / 8;
	s8 rel = (s8)data[first];

	switch (query) {
	case UVC_GET_CUR:
		return (rel == 0) ? 0 : (rel > 0 ? data[first+1]
						 : -data[first+1]);
	case UVC_GET_MIN:
		return -data[first+1];
	case UVC_GET_MAX:
	case UVC_GET_RES:
	case UVC_GET_DEF:
	default:
		return data[first+1];
	}
}

static void uvc_ctrl_set_rel_speed(struct uvc_control_mapping *mapping,
	s32 value, u8 *data)
{
	unsigned int first = mapping->offset / 8;

	data[first] = value == 0 ? 0 : (value > 0) ? 1 : 0xff;
	data[first+1] = min_t(int, abs(value), 0xff);
}

static const struct uvc_control_mapping uvc_ctrl_mappings[] = {
	{
		.id		= V4L2_CID_BRIGHTNESS,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_BRIGHTNESS_CONTROL,
		.size		= 16,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_SIGNED,
	},
	{
		.id		= V4L2_CID_CONTRAST,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_CONTRAST_CONTROL,
		.size		= 16,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_UNSIGNED,
	},
	{
		.id		= V4L2_CID_HUE,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_HUE_CONTROL,
		.size		= 16,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_SIGNED,
		.master_id	= V4L2_CID_HUE_AUTO,
		.master_manual	= 0,
	},
	{
		.id		= V4L2_CID_SATURATION,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_SATURATION_CONTROL,
		.size		= 16,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_UNSIGNED,
	},
	{
		.id		= V4L2_CID_SHARPNESS,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_SHARPNESS_CONTROL,
		.size		= 16,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_UNSIGNED,
	},
	{
		.id		= V4L2_CID_GAMMA,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_GAMMA_CONTROL,
		.size		= 16,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_UNSIGNED,
	},
	{
		.id		= V4L2_CID_BACKLIGHT_COMPENSATION,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_BACKLIGHT_COMPENSATION_CONTROL,
		.size		= 16,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_UNSIGNED,
	},
	{
		.id		= V4L2_CID_GAIN,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_GAIN_CONTROL,
		.size		= 16,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_UNSIGNED,
	},
	{
		.id		= V4L2_CID_POWER_LINE_FREQUENCY,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_POWER_LINE_FREQUENCY_CONTROL,
		.size		= 2,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_MENU,
		.data_type	= UVC_CTRL_DATA_TYPE_ENUM,
		.menu_info	= power_line_frequency_controls,
		.menu_count	= ARRAY_SIZE(power_line_frequency_controls),
	},
	{
		.id		= V4L2_CID_HUE_AUTO,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_HUE_AUTO_CONTROL,
		.size		= 1,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_BOOLEAN,
		.data_type	= UVC_CTRL_DATA_TYPE_BOOLEAN,
		.slave_ids	= { V4L2_CID_HUE, },
	},
	{
		.id		= V4L2_CID_EXPOSURE_AUTO,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_AE_MODE_CONTROL,
		.size		= 4,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_MENU,
		.data_type	= UVC_CTRL_DATA_TYPE_BITMASK,
		.menu_info	= exposure_auto_controls,
		.menu_count	= ARRAY_SIZE(exposure_auto_controls),
		.slave_ids	= { V4L2_CID_EXPOSURE_ABSOLUTE, },
	},
	{
		.id		= V4L2_CID_EXPOSURE_AUTO_PRIORITY,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_AE_PRIORITY_CONTROL,
		.size		= 1,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_BOOLEAN,
		.data_type	= UVC_CTRL_DATA_TYPE_BOOLEAN,
	},
	{
		.id		= V4L2_CID_EXPOSURE_ABSOLUTE,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL,
		.size		= 32,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_UNSIGNED,
		.master_id	= V4L2_CID_EXPOSURE_AUTO,
		.master_manual	= V4L2_EXPOSURE_MANUAL,
	},
	{
		.id		= V4L2_CID_AUTO_WHITE_BALANCE,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL,
		.size		= 1,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_BOOLEAN,
		.data_type	= UVC_CTRL_DATA_TYPE_BOOLEAN,
		.slave_ids	= { V4L2_CID_WHITE_BALANCE_TEMPERATURE, },
	},
	{
		.id		= V4L2_CID_WHITE_BALANCE_TEMPERATURE,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_WHITE_BALANCE_TEMPERATURE_CONTROL,
		.size		= 16,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_UNSIGNED,
		.master_id	= V4L2_CID_AUTO_WHITE_BALANCE,
		.master_manual	= 0,
	},
	{
		.id		= V4L2_CID_AUTO_WHITE_BALANCE,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL,
		.size		= 1,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_BOOLEAN,
		.data_type	= UVC_CTRL_DATA_TYPE_BOOLEAN,
		.slave_ids	= { V4L2_CID_BLUE_BALANCE,
				    V4L2_CID_RED_BALANCE },
	},
	{
		.id		= V4L2_CID_BLUE_BALANCE,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_WHITE_BALANCE_COMPONENT_CONTROL,
		.size		= 16,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_SIGNED,
		.master_id	= V4L2_CID_AUTO_WHITE_BALANCE,
		.master_manual	= 0,
	},
	{
		.id		= V4L2_CID_RED_BALANCE,
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_WHITE_BALANCE_COMPONENT_CONTROL,
		.size		= 16,
		.offset		= 16,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_SIGNED,
		.master_id	= V4L2_CID_AUTO_WHITE_BALANCE,
		.master_manual	= 0,
	},
	{
		.id		= V4L2_CID_FOCUS_ABSOLUTE,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_FOCUS_ABSOLUTE_CONTROL,
		.size		= 16,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_UNSIGNED,
		.master_id	= V4L2_CID_FOCUS_AUTO,
		.master_manual	= 0,
	},
	{
		.id		= V4L2_CID_FOCUS_AUTO,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_FOCUS_AUTO_CONTROL,
		.size		= 1,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_BOOLEAN,
		.data_type	= UVC_CTRL_DATA_TYPE_BOOLEAN,
		.slave_ids	= { V4L2_CID_FOCUS_ABSOLUTE, },
	},
	{
		.id		= V4L2_CID_IRIS_ABSOLUTE,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_IRIS_ABSOLUTE_CONTROL,
		.size		= 16,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_UNSIGNED,
	},
	{
		.id		= V4L2_CID_IRIS_RELATIVE,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_IRIS_RELATIVE_CONTROL,
		.size		= 8,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_SIGNED,
	},
	{
		.id		= V4L2_CID_ZOOM_ABSOLUTE,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_ZOOM_ABSOLUTE_CONTROL,
		.size		= 16,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_UNSIGNED,
	},
	{
		.id		= V4L2_CID_ZOOM_CONTINUOUS,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_ZOOM_RELATIVE_CONTROL,
		.size		= 0,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_SIGNED,
		.get		= uvc_ctrl_get_zoom,
		.set		= uvc_ctrl_set_zoom,
	},
	{
		.id		= V4L2_CID_PAN_ABSOLUTE,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_PANTILT_ABSOLUTE_CONTROL,
		.size		= 32,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_SIGNED,
	},
	{
		.id		= V4L2_CID_TILT_ABSOLUTE,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_PANTILT_ABSOLUTE_CONTROL,
		.size		= 32,
		.offset		= 32,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_SIGNED,
	},
	{
		.id		= V4L2_CID_PAN_SPEED,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_PANTILT_RELATIVE_CONTROL,
		.size		= 16,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_SIGNED,
		.get		= uvc_ctrl_get_rel_speed,
		.set		= uvc_ctrl_set_rel_speed,
	},
	{
		.id		= V4L2_CID_TILT_SPEED,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_PANTILT_RELATIVE_CONTROL,
		.size		= 16,
		.offset		= 16,
		.v4l2_type	= V4L2_CTRL_TYPE_INTEGER,
		.data_type	= UVC_CTRL_DATA_TYPE_SIGNED,
		.get		= uvc_ctrl_get_rel_speed,
		.set		= uvc_ctrl_set_rel_speed,
	},
	{
		.id		= V4L2_CID_PRIVACY,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_PRIVACY_CONTROL,
		.size		= 1,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_BOOLEAN,
		.data_type	= UVC_CTRL_DATA_TYPE_BOOLEAN,
	},
	{
		.id		= V4L2_CID_PRIVACY,
		.entity		= UVC_GUID_EXT_GPIO_CONTROLLER,
		.selector	= UVC_CT_PRIVACY_CONTROL,
		.size		= 1,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_BOOLEAN,
		.data_type	= UVC_CTRL_DATA_TYPE_BOOLEAN,
	},
};

/* ------------------------------------------------------------------------
 * Utility functions
 */

static inline u8 *uvc_ctrl_data(struct uvc_control *ctrl, int id)
{
	return ctrl->uvc_data + id * ctrl->info.size;
}

static inline int uvc_test_bit(const u8 *data, int bit)
{
	return (data[bit >> 3] >> (bit & 7)) & 1;
}

static inline void uvc_clear_bit(u8 *data, int bit)
{
	data[bit >> 3] &= ~(1 << (bit & 7));
}

/* Extract the bit string specified by mapping->offset and mapping->size
 * from the little-endian data stored at 'data' and return the result as
 * a signed 32bit integer. Sign extension will be performed if the mapping
 * references a signed data type.
 */
static s32 uvc_get_le_value(struct uvc_control_mapping *mapping,
	u8 query, const u8 *data)
{
	int bits = mapping->size;
	int offset = mapping->offset;
	s32 value = 0;
	u8 mask;

	data += offset / 8;
	offset &= 7;
	mask = ((1LL << bits) - 1) << offset;

	while (1) {
		u8 byte = *data & mask;
		value |= offset > 0 ? (byte >> offset) : (byte << (-offset));
		bits -= 8 - (offset > 0 ? offset : 0);
		if (bits <= 0)
			break;

		offset -= 8;
		mask = (1 << bits) - 1;
		data++;
	}

	/* Sign-extend the value if needed. */
	if (mapping->data_type == UVC_CTRL_DATA_TYPE_SIGNED)
		value |= -(value & (1 << (mapping->size - 1)));

	return value;
}

/* Set the bit string specified by mapping->offset and mapping->size
 * in the little-endian data stored at 'data' to the value 'value'.
 */
static void uvc_set_le_value(struct uvc_control_mapping *mapping,
	s32 value, u8 *data)
{
	int bits = mapping->size;
	int offset = mapping->offset;
	u8 mask;

	/* According to the v4l2 spec, writing any value to a button control
	 * should result in the action belonging to the button control being
	 * triggered. UVC devices however want to see a 1 written -> override
	 * value.
	 */
	if (mapping->v4l2_type == V4L2_CTRL_TYPE_BUTTON)
		value = -1;

	data += offset / 8;
	offset &= 7;

	for (; bits > 0; data++) {
		mask = ((1LL << bits) - 1) << offset;
		*data = (*data & ~mask) | ((value << offset) & mask);
		value >>= offset ? offset : 8;
		bits -= 8 - offset;
		offset = 0;
	}
}

/* ------------------------------------------------------------------------
 * Terminal and unit management
 */

static int uvc_entity_match_guid(const struct uvc_entity *entity,
				 const u8 guid[16])
{
	return memcmp(entity->guid, guid, sizeof(entity->guid)) == 0;
}

/* ------------------------------------------------------------------------
 * UVC Controls
 */

static void __uvc_find_control(struct uvc_entity *entity, u32 v4l2_id,
	struct uvc_control_mapping **mapping, struct uvc_control **control,
	int next)
{
	struct uvc_control *ctrl;
	struct uvc_control_mapping *map;
	unsigned int i;

	if (entity == NULL)
		return;

	for (i = 0; i < entity->ncontrols; ++i) {
		ctrl = &entity->controls[i];
		if (!ctrl->initialized)
			continue;

		list_for_each_entry(map, &ctrl->info.mappings, list) {
			if ((map->id == v4l2_id) && !next) {
				*control = ctrl;
				*mapping = map;
				return;
			}

			if ((*mapping == NULL || (*mapping)->id > map->id) &&
			    (map->id > v4l2_id) && next) {
				*control = ctrl;
				*mapping = map;
			}
		}
	}
}

static struct uvc_control *uvc_find_control(struct uvc_video_chain *chain,
	u32 v4l2_id, struct uvc_control_mapping **mapping)
{
	struct uvc_control *ctrl = NULL;
	struct uvc_entity *entity;
	int next = v4l2_id & V4L2_CTRL_FLAG_NEXT_CTRL;

	*mapping = NULL;

	/* Mask the query flags. */
	v4l2_id &= V4L2_CTRL_ID_MASK;

	/* Find the control. */
	list_for_each_entry(entity, &chain->entities, chain) {
		__uvc_find_control(entity, v4l2_id, mapping, &ctrl, next);
		if (ctrl && !next)
			return ctrl;
	}

	if (ctrl == NULL && !next)
		uvc_dbg(chain->dev, CONTROL, "Control 0x%08x not found\n",
			v4l2_id);

	return ctrl;
}

static int uvc_ctrl_populate_cache(struct uvc_video_chain *chain,
	struct uvc_control *ctrl)
{
	int ret;

	if (ctrl->info.flags & UVC_CTRL_FLAG_GET_DEF) {
		ret = uvc_query_ctrl(chain->dev, UVC_GET_DEF, ctrl->entity->id,
				     chain->dev->intfnum, ctrl->info.selector,
				     uvc_ctrl_data(ctrl, UVC_CTRL_DATA_DEF),
				     ctrl->info.size);
		if (ret < 0)
			return ret;
	}

	if (ctrl->info.flags & UVC_CTRL_FLAG_GET_MIN) {
		ret = uvc_query_ctrl(chain->dev, UVC_GET_MIN, ctrl->entity->id,
				     chain->dev->intfnum, ctrl->info.selector,
				     uvc_ctrl_data(ctrl, UVC_CTRL_DATA_MIN),
				     ctrl->info.size);
		if (ret < 0)
			return ret;
	}
	if (ctrl->info.flags & UVC_CTRL_FLAG_GET_MAX) {
		ret = uvc_query_ctrl(chain->dev, UVC_GET_MAX, ctrl->entity->id,
				     chain->dev->intfnum, ctrl->info.selector,
				     uvc_ctrl_data(ctrl, UVC_CTRL_DATA_MAX),
				     ctrl->info.size);
		if (ret < 0)
			return ret;
	}
	if (ctrl->info.flags & UVC_CTRL_FLAG_GET_RES) {
		ret = uvc_query_ctrl(chain->dev, UVC_GET_RES, ctrl->entity->id,
				     chain->dev->intfnum, ctrl->info.selector,
				     uvc_ctrl_data(ctrl, UVC_CTRL_DATA_RES),
				     ctrl->info.size);
		if (ret < 0) {
			if (UVC_ENTITY_TYPE(ctrl->entity) !=
			    UVC_VC_EXTENSION_UNIT)
				return ret;

			/* GET_RES is mandatory for XU controls, but some
			 * cameras still choke on it. Ignore errors and set the
			 * resolution value to zero.
			 */
			uvc_warn_once(chain->dev, UVC_WARN_XU_GET_RES,
				      "UVC non compliance - GET_RES failed on "
				      "an XU control. Enabling workaround.\n");
			memset(uvc_ctrl_data(ctrl, UVC_CTRL_DATA_RES), 0,
			       ctrl->info.size);
		}
	}

	ctrl->cached = 1;
	return 0;
}

static s32 __uvc_ctrl_get_value(struct uvc_control_mapping *mapping,
				const u8 *data)
{
	s32 value = mapping->get(mapping, UVC_GET_CUR, data);

	if (mapping->v4l2_type == V4L2_CTRL_TYPE_MENU) {
		const struct uvc_menu_info *menu = mapping->menu_info;
		unsigned int i;

		for (i = 0; i < mapping->menu_count; ++i, ++menu) {
			if (menu->value == value) {
				value = i;
				break;
			}
		}
	}

	return value;
}

static int __uvc_ctrl_get(struct uvc_video_chain *chain,
	struct uvc_control *ctrl, struct uvc_control_mapping *mapping,
	s32 *value)
{
	int ret;

	if ((ctrl->info.flags & UVC_CTRL_FLAG_GET_CUR) == 0)
		return -EACCES;

	if (!ctrl->loaded) {
		if (ctrl->entity->get_cur) {
			ret = ctrl->entity->get_cur(chain->dev,
				ctrl->entity,
				ctrl->info.selector,
				uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT),
				ctrl->info.size);
		} else {
			ret = uvc_query_ctrl(chain->dev, UVC_GET_CUR,
				ctrl->entity->id,
				chain->dev->intfnum,
				ctrl->info.selector,
				uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT),
				ctrl->info.size);
		}
		if (ret < 0)
			return ret;

		ctrl->loaded = 1;
	}

	*value = __uvc_ctrl_get_value(mapping,
				uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT));

	return 0;
}

static int __uvc_query_v4l2_class(struct uvc_video_chain *chain, u32 req_id,
				  u32 found_id)
{
	bool find_next = req_id & V4L2_CTRL_FLAG_NEXT_CTRL;
	unsigned int i;

	req_id &= V4L2_CTRL_ID_MASK;

	for (i = 0; i < ARRAY_SIZE(uvc_control_classes); i++) {
		if (!(chain->ctrl_class_bitmap & BIT(i)))
			continue;
		if (!find_next) {
			if (uvc_control_classes[i] == req_id)
				return i;
			continue;
		}
		if (uvc_control_classes[i] > req_id &&
		    uvc_control_classes[i] < found_id)
			return i;
	}

	return -ENODEV;
}

static int uvc_query_v4l2_class(struct uvc_video_chain *chain, u32 req_id,
				u32 found_id, struct v4l2_queryctrl *v4l2_ctrl)
{
	int idx;

	idx = __uvc_query_v4l2_class(chain, req_id, found_id);
	if (idx < 0)
		return -ENODEV;

	memset(v4l2_ctrl, 0, sizeof(*v4l2_ctrl));
	v4l2_ctrl->id = uvc_control_classes[idx];
	strscpy(v4l2_ctrl->name, v4l2_ctrl_get_name(v4l2_ctrl->id),
		sizeof(v4l2_ctrl->name));
	v4l2_ctrl->type = V4L2_CTRL_TYPE_CTRL_CLASS;
	v4l2_ctrl->flags = V4L2_CTRL_FLAG_WRITE_ONLY
			 | V4L2_CTRL_FLAG_READ_ONLY;
	return 0;
}

int uvc_ctrl_is_accessible(struct uvc_video_chain *chain, u32 v4l2_id,
			   bool read)
{
	struct uvc_control_mapping *mapping;
	struct uvc_control *ctrl;

	if (__uvc_query_v4l2_class(chain, v4l2_id, 0) >= 0)
		return -EACCES;

	ctrl = uvc_find_control(chain, v4l2_id, &mapping);
	if (!ctrl)
		return -EINVAL;

	if (!(ctrl->info.flags & UVC_CTRL_FLAG_GET_CUR) && read)
		return -EACCES;

	if (!(ctrl->info.flags & UVC_CTRL_FLAG_SET_CUR) && !read)
		return -EACCES;

	return 0;
}

static const char *uvc_map_get_name(const struct uvc_control_mapping *map)
{
	const char *name;

	if (map->name)
		return map->name;

	name = v4l2_ctrl_get_name(map->id);
	if (name)
		return name;

	return "Unknown Control";
}

static int __uvc_query_v4l2_ctrl(struct uvc_video_chain *chain,
	struct uvc_control *ctrl,
	struct uvc_control_mapping *mapping,
	struct v4l2_queryctrl *v4l2_ctrl)
{
	struct uvc_control_mapping *master_map = NULL;
	struct uvc_control *master_ctrl = NULL;
	const struct uvc_menu_info *menu;
	unsigned int i;

	memset(v4l2_ctrl, 0, sizeof(*v4l2_ctrl));
	v4l2_ctrl->id = mapping->id;
	v4l2_ctrl->type = mapping->v4l2_type;
	strscpy(v4l2_ctrl->name, uvc_map_get_name(mapping),
		sizeof(v4l2_ctrl->name));
	v4l2_ctrl->flags = 0;

	if (!(ctrl->info.flags & UVC_CTRL_FLAG_GET_CUR))
		v4l2_ctrl->flags |= V4L2_CTRL_FLAG_WRITE_ONLY;
	if (!(ctrl->info.flags & UVC_CTRL_FLAG_SET_CUR))
		v4l2_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	if (mapping->master_id)
		__uvc_find_control(ctrl->entity, mapping->master_id,
				   &master_map, &master_ctrl, 0);
	if (master_ctrl && (master_ctrl->info.flags & UVC_CTRL_FLAG_GET_CUR)) {
		s32 val;
		int ret = __uvc_ctrl_get(chain, master_ctrl, master_map, &val);
		if (ret < 0)
			return ret;

		if (val != mapping->master_manual)
				v4l2_ctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
	}

	if (!ctrl->cached) {
		int ret = uvc_ctrl_populate_cache(chain, ctrl);
		if (ret < 0)
			return ret;
	}

	if (ctrl->info.flags & UVC_CTRL_FLAG_GET_DEF) {
		v4l2_ctrl->default_value = mapping->get(mapping, UVC_GET_DEF,
				uvc_ctrl_data(ctrl, UVC_CTRL_DATA_DEF));
	}

	switch (mapping->v4l2_type) {
	case V4L2_CTRL_TYPE_MENU:
		v4l2_ctrl->minimum = 0;
		v4l2_ctrl->maximum = mapping->menu_count - 1;
		v4l2_ctrl->step = 1;

		menu = mapping->menu_info;
		for (i = 0; i < mapping->menu_count; ++i, ++menu) {
			if (menu->value == v4l2_ctrl->default_value) {
				v4l2_ctrl->default_value = i;
				break;
			}
		}

		return 0;

	case V4L2_CTRL_TYPE_BOOLEAN:
		v4l2_ctrl->minimum = 0;
		v4l2_ctrl->maximum = 1;
		v4l2_ctrl->step = 1;
		return 0;

	case V4L2_CTRL_TYPE_BUTTON:
		v4l2_ctrl->minimum = 0;
		v4l2_ctrl->maximum = 0;
		v4l2_ctrl->step = 0;
		return 0;

	default:
		break;
	}

	if (ctrl->info.flags & UVC_CTRL_FLAG_GET_MIN)
		v4l2_ctrl->minimum = mapping->get(mapping, UVC_GET_MIN,
				     uvc_ctrl_data(ctrl, UVC_CTRL_DATA_MIN));

	if (ctrl->info.flags & UVC_CTRL_FLAG_GET_MAX)
		v4l2_ctrl->maximum = mapping->get(mapping, UVC_GET_MAX,
				     uvc_ctrl_data(ctrl, UVC_CTRL_DATA_MAX));

	if (ctrl->info.flags & UVC_CTRL_FLAG_GET_RES)
		v4l2_ctrl->step = mapping->get(mapping, UVC_GET_RES,
				  uvc_ctrl_data(ctrl, UVC_CTRL_DATA_RES));

	return 0;
}

int uvc_query_v4l2_ctrl(struct uvc_video_chain *chain,
	struct v4l2_queryctrl *v4l2_ctrl)
{
	struct uvc_control *ctrl;
	struct uvc_control_mapping *mapping;
	int ret;

	ret = mutex_lock_interruptible(&chain->ctrl_mutex);
	if (ret < 0)
		return -ERESTARTSYS;

	/* Check if the ctrl is a know class */
	if (!(v4l2_ctrl->id & V4L2_CTRL_FLAG_NEXT_CTRL)) {
		ret = uvc_query_v4l2_class(chain, v4l2_ctrl->id, 0, v4l2_ctrl);
		if (!ret)
			goto done;
	}

	ctrl = uvc_find_control(chain, v4l2_ctrl->id, &mapping);
	if (ctrl == NULL) {
		ret = -EINVAL;
		goto done;
	}

	/*
	 * If we're enumerating control with V4L2_CTRL_FLAG_NEXT_CTRL, check if
	 * a class should be inserted between the previous control and the one
	 * we have just found.
	 */
	if (v4l2_ctrl->id & V4L2_CTRL_FLAG_NEXT_CTRL) {
		ret = uvc_query_v4l2_class(chain, v4l2_ctrl->id, mapping->id,
					   v4l2_ctrl);
		if (!ret)
			goto done;
	}

	ret = __uvc_query_v4l2_ctrl(chain, ctrl, mapping, v4l2_ctrl);
done:
	mutex_unlock(&chain->ctrl_mutex);
	return ret;
}

/*
 * Mapping V4L2 controls to UVC controls can be straightforward if done well.
 * Most of the UVC controls exist in V4L2, and can be mapped directly. Some
 * must be grouped (for instance the Red Balance, Blue Balance and Do White
 * Balance V4L2 controls use the White Balance Component UVC control) or
 * otherwise translated. The approach we take here is to use a translation
 * table for the controls that can be mapped directly, and handle the others
 * manually.
 */
int uvc_query_v4l2_menu(struct uvc_video_chain *chain,
	struct v4l2_querymenu *query_menu)
{
	const struct uvc_menu_info *menu_info;
	struct uvc_control_mapping *mapping;
	struct uvc_control *ctrl;
	u32 index = query_menu->index;
	u32 id = query_menu->id;
	int ret;

	memset(query_menu, 0, sizeof(*query_menu));
	query_menu->id = id;
	query_menu->index = index;

	ret = mutex_lock_interruptible(&chain->ctrl_mutex);
	if (ret < 0)
		return -ERESTARTSYS;

	ctrl = uvc_find_control(chain, query_menu->id, &mapping);
	if (ctrl == NULL || mapping->v4l2_type != V4L2_CTRL_TYPE_MENU) {
		ret = -EINVAL;
		goto done;
	}

	if (query_menu->index >= mapping->menu_count) {
		ret = -EINVAL;
		goto done;
	}

	menu_info = &mapping->menu_info[query_menu->index];

	if (mapping->data_type == UVC_CTRL_DATA_TYPE_BITMASK &&
	    (ctrl->info.flags & UVC_CTRL_FLAG_GET_RES)) {
		s32 bitmap;

		if (!ctrl->cached) {
			ret = uvc_ctrl_populate_cache(chain, ctrl);
			if (ret < 0)
				goto done;
		}

		bitmap = mapping->get(mapping, UVC_GET_RES,
				      uvc_ctrl_data(ctrl, UVC_CTRL_DATA_RES));
		if (!(bitmap & menu_info->value)) {
			ret = -EINVAL;
			goto done;
		}
	}

	strscpy(query_menu->name, menu_info->name, sizeof(query_menu->name));

done:
	mutex_unlock(&chain->ctrl_mutex);
	return ret;
}

/* --------------------------------------------------------------------------
 * Ctrl event handling
 */

static void uvc_ctrl_fill_event(struct uvc_video_chain *chain,
	struct v4l2_event *ev,
	struct uvc_control *ctrl,
	struct uvc_control_mapping *mapping,
	s32 value, u32 changes)
{
	struct v4l2_queryctrl v4l2_ctrl;

	__uvc_query_v4l2_ctrl(chain, ctrl, mapping, &v4l2_ctrl);

	memset(ev, 0, sizeof(*ev));
	ev->type = V4L2_EVENT_CTRL;
	ev->id = v4l2_ctrl.id;
	ev->u.ctrl.value = value;
	ev->u.ctrl.changes = changes;
	ev->u.ctrl.type = v4l2_ctrl.type;
	ev->u.ctrl.flags = v4l2_ctrl.flags;
	ev->u.ctrl.minimum = v4l2_ctrl.minimum;
	ev->u.ctrl.maximum = v4l2_ctrl.maximum;
	ev->u.ctrl.step = v4l2_ctrl.step;
	ev->u.ctrl.default_value = v4l2_ctrl.default_value;
}

/*
 * Send control change events to all subscribers for the @ctrl control. By
 * default the subscriber that generated the event, as identified by @handle,
 * is not notified unless it has set the V4L2_EVENT_SUB_FL_ALLOW_FEEDBACK flag.
 * @handle can be NULL for asynchronous events related to auto-update controls,
 * in which case all subscribers are notified.
 */
static void uvc_ctrl_send_event(struct uvc_video_chain *chain,
	struct uvc_fh *handle, struct uvc_control *ctrl,
	struct uvc_control_mapping *mapping, s32 value, u32 changes)
{
	struct v4l2_fh *originator = handle ? &handle->vfh : NULL;
	struct v4l2_subscribed_event *sev;
	struct v4l2_event ev;

	if (list_empty(&mapping->ev_subs))
		return;

	uvc_ctrl_fill_event(chain, &ev, ctrl, mapping, value, changes);

	list_for_each_entry(sev, &mapping->ev_subs, node) {
		if (sev->fh != originator ||
		    (sev->flags & V4L2_EVENT_SUB_FL_ALLOW_FEEDBACK) ||
		    (changes & V4L2_EVENT_CTRL_CH_FLAGS))
			v4l2_event_queue_fh(sev->fh, &ev);
	}
}

/*
 * Send control change events for the slave of the @master control identified
 * by the V4L2 ID @slave_id. The @handle identifies the event subscriber that
 * generated the event and may be NULL for auto-update events.
 */
static void uvc_ctrl_send_slave_event(struct uvc_video_chain *chain,
	struct uvc_fh *handle, struct uvc_control *master, u32 slave_id)
{
	struct uvc_control_mapping *mapping = NULL;
	struct uvc_control *ctrl = NULL;
	u32 changes = V4L2_EVENT_CTRL_CH_FLAGS;
	s32 val = 0;

	__uvc_find_control(master->entity, slave_id, &mapping, &ctrl, 0);
	if (ctrl == NULL)
		return;

	if (__uvc_ctrl_get(chain, ctrl, mapping, &val) == 0)
		changes |= V4L2_EVENT_CTRL_CH_VALUE;

	uvc_ctrl_send_event(chain, handle, ctrl, mapping, val, changes);
}

void uvc_ctrl_status_event(struct uvc_video_chain *chain,
			   struct uvc_control *ctrl, const u8 *data)
{
	struct uvc_control_mapping *mapping;
	struct uvc_fh *handle;
	unsigned int i;

	mutex_lock(&chain->ctrl_mutex);

	handle = ctrl->handle;
	ctrl->handle = NULL;

	list_for_each_entry(mapping, &ctrl->info.mappings, list) {
		s32 value = __uvc_ctrl_get_value(mapping, data);

		/*
		 * handle may be NULL here if the device sends auto-update
		 * events without a prior related control set from userspace.
		 */
		for (i = 0; i < ARRAY_SIZE(mapping->slave_ids); ++i) {
			if (!mapping->slave_ids[i])
				break;

			uvc_ctrl_send_slave_event(chain, handle, ctrl,
						  mapping->slave_ids[i]);
		}

		uvc_ctrl_send_event(chain, handle, ctrl, mapping, value,
				    V4L2_EVENT_CTRL_CH_VALUE);
	}

	mutex_unlock(&chain->ctrl_mutex);
}

static void uvc_ctrl_status_event_work(struct work_struct *work)
{
	struct uvc_device *dev = container_of(work, struct uvc_device,
					      async_ctrl.work);
	struct uvc_ctrl_work *w = &dev->async_ctrl;
	int ret;

	uvc_ctrl_status_event(w->chain, w->ctrl, w->data);

	/* Resubmit the URB. */
	w->urb->interval = dev->int_ep->desc.bInterval;
	ret = usb_submit_urb(w->urb, GFP_KERNEL);
	if (ret < 0)
		dev_err(&dev->udev->dev,
			"Failed to resubmit status URB (%d).\n", ret);
}

bool uvc_ctrl_status_event_async(struct urb *urb, struct uvc_video_chain *chain,
				 struct uvc_control *ctrl, const u8 *data)
{
	struct uvc_device *dev = chain->dev;
	struct uvc_ctrl_work *w = &dev->async_ctrl;

	if (list_empty(&ctrl->info.mappings)) {
		ctrl->handle = NULL;
		return false;
	}

	w->data = data;
	w->urb = urb;
	w->chain = chain;
	w->ctrl = ctrl;

	schedule_work(&w->work);

	return true;
}

static bool uvc_ctrl_xctrls_has_control(const struct v4l2_ext_control *xctrls,
					unsigned int xctrls_count, u32 id)
{
	unsigned int i;

	for (i = 0; i < xctrls_count; ++i) {
		if (xctrls[i].id == id)
			return true;
	}

	return false;
}

static void uvc_ctrl_send_events(struct uvc_fh *handle,
	const struct v4l2_ext_control *xctrls, unsigned int xctrls_count)
{
	struct uvc_control_mapping *mapping;
	struct uvc_control *ctrl;
	u32 changes = V4L2_EVENT_CTRL_CH_VALUE;
	unsigned int i;
	unsigned int j;

	for (i = 0; i < xctrls_count; ++i) {
		ctrl = uvc_find_control(handle->chain, xctrls[i].id, &mapping);

		if (ctrl->info.flags & UVC_CTRL_FLAG_ASYNCHRONOUS)
			/* Notification will be sent from an Interrupt event. */
			continue;

		for (j = 0; j < ARRAY_SIZE(mapping->slave_ids); ++j) {
			u32 slave_id = mapping->slave_ids[j];

			if (!slave_id)
				break;

			/*
			 * We can skip sending an event for the slave if the
			 * slave is being modified in the same transaction.
			 */
			if (uvc_ctrl_xctrls_has_control(xctrls, xctrls_count,
							slave_id))
				continue;

			uvc_ctrl_send_slave_event(handle->chain, handle, ctrl,
						  slave_id);
		}

		/*
		 * If the master is being modified in the same transaction
		 * flags may change too.
		 */
		if (mapping->master_id &&
		    uvc_ctrl_xctrls_has_control(xctrls, xctrls_count,
						mapping->master_id))
			changes |= V4L2_EVENT_CTRL_CH_FLAGS;

		uvc_ctrl_send_event(handle->chain, handle, ctrl, mapping,
				    xctrls[i].value, changes);
	}
}

static int uvc_ctrl_add_event(struct v4l2_subscribed_event *sev, unsigned elems)
{
	struct uvc_fh *handle = container_of(sev->fh, struct uvc_fh, vfh);
	struct uvc_control_mapping *mapping;
	struct uvc_control *ctrl;
	int ret;

	ret = mutex_lock_interruptible(&handle->chain->ctrl_mutex);
	if (ret < 0)
		return -ERESTARTSYS;

	if (__uvc_query_v4l2_class(handle->chain, sev->id, 0) >= 0) {
		ret = 0;
		goto done;
	}

	ctrl = uvc_find_control(handle->chain, sev->id, &mapping);
	if (ctrl == NULL) {
		ret = -EINVAL;
		goto done;
	}

	list_add_tail(&sev->node, &mapping->ev_subs);
	if (sev->flags & V4L2_EVENT_SUB_FL_SEND_INITIAL) {
		struct v4l2_event ev;
		u32 changes = V4L2_EVENT_CTRL_CH_FLAGS;
		s32 val = 0;

		if (__uvc_ctrl_get(handle->chain, ctrl, mapping, &val) == 0)
			changes |= V4L2_EVENT_CTRL_CH_VALUE;

		uvc_ctrl_fill_event(handle->chain, &ev, ctrl, mapping, val,
				    changes);
		/* Mark the queue as active, allowing this initial
		   event to be accepted. */
		sev->elems = elems;
		v4l2_event_queue_fh(sev->fh, &ev);
	}

done:
	mutex_unlock(&handle->chain->ctrl_mutex);
	return ret;
}

static void uvc_ctrl_del_event(struct v4l2_subscribed_event *sev)
{
	struct uvc_fh *handle = container_of(sev->fh, struct uvc_fh, vfh);

	mutex_lock(&handle->chain->ctrl_mutex);
	if (__uvc_query_v4l2_class(handle->chain, sev->id, 0) >= 0)
		goto done;
	list_del(&sev->node);
done:
	mutex_unlock(&handle->chain->ctrl_mutex);
}

const struct v4l2_subscribed_event_ops uvc_ctrl_sub_ev_ops = {
	.add = uvc_ctrl_add_event,
	.del = uvc_ctrl_del_event,
	.replace = v4l2_ctrl_replace,
	.merge = v4l2_ctrl_merge,
};

/* --------------------------------------------------------------------------
 * Control transactions
 *
 * To make extended set operations as atomic as the hardware allows, controls
 * are handled using begin/commit/rollback operations.
 *
 * At the beginning of a set request, uvc_ctrl_begin should be called to
 * initialize the request. This function acquires the control lock.
 *
 * When setting a control, the new value is stored in the control data field
 * at position UVC_CTRL_DATA_CURRENT. The control is then marked as dirty for
 * later processing. If the UVC and V4L2 control sizes differ, the current
 * value is loaded from the hardware before storing the new value in the data
 * field.
 *
 * After processing all controls in the transaction, uvc_ctrl_commit or
 * uvc_ctrl_rollback must be called to apply the pending changes to the
 * hardware or revert them. When applying changes, all controls marked as
 * dirty will be modified in the UVC device, and the dirty flag will be
 * cleared. When reverting controls, the control data field
 * UVC_CTRL_DATA_CURRENT is reverted to its previous value
 * (UVC_CTRL_DATA_BACKUP) for all dirty controls. Both functions release the
 * control lock.
 */
int uvc_ctrl_begin(struct uvc_video_chain *chain)
{
	return mutex_lock_interruptible(&chain->ctrl_mutex) ? -ERESTARTSYS : 0;
}

static int uvc_ctrl_commit_entity(struct uvc_device *dev,
	struct uvc_entity *entity, int rollback, struct uvc_control **err_ctrl)
{
	struct uvc_control *ctrl;
	unsigned int i;
	int ret;

	if (entity == NULL)
		return 0;

	for (i = 0; i < entity->ncontrols; ++i) {
		ctrl = &entity->controls[i];
		if (!ctrl->initialized)
			continue;

		/* Reset the loaded flag for auto-update controls that were
		 * marked as loaded in uvc_ctrl_get/uvc_ctrl_set to prevent
		 * uvc_ctrl_get from using the cached value, and for write-only
		 * controls to prevent uvc_ctrl_set from setting bits not
		 * explicitly set by the user.
		 */
		if (ctrl->info.flags & UVC_CTRL_FLAG_AUTO_UPDATE ||
		    !(ctrl->info.flags & UVC_CTRL_FLAG_GET_CUR))
			ctrl->loaded = 0;

		if (!ctrl->dirty)
			continue;

		if (!rollback)
			ret = uvc_query_ctrl(dev, UVC_SET_CUR, ctrl->entity->id,
				dev->intfnum, ctrl->info.selector,
				uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT),
				ctrl->info.size);
		else
			ret = 0;

		if (rollback || ret < 0)
			memcpy(uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT),
			       uvc_ctrl_data(ctrl, UVC_CTRL_DATA_BACKUP),
			       ctrl->info.size);

		ctrl->dirty = 0;

		if (ret < 0) {
			if (err_ctrl)
				*err_ctrl = ctrl;
			return ret;
		}
	}

	return 0;
}

static int uvc_ctrl_find_ctrl_idx(struct uvc_entity *entity,
				  struct v4l2_ext_controls *ctrls,
				  struct uvc_control *uvc_control)
{
	struct uvc_control_mapping *mapping = NULL;
	struct uvc_control *ctrl_found = NULL;
	unsigned int i;

	if (!entity)
		return ctrls->count;

	for (i = 0; i < ctrls->count; i++) {
		__uvc_find_control(entity, ctrls->controls[i].id, &mapping,
				   &ctrl_found, 0);
		if (uvc_control == ctrl_found)
			return i;
	}

	return ctrls->count;
}

int __uvc_ctrl_commit(struct uvc_fh *handle, int rollback,
		      struct v4l2_ext_controls *ctrls)
{
	struct uvc_video_chain *chain = handle->chain;
	struct uvc_control *err_ctrl;
	struct uvc_entity *entity;
	int ret = 0;

	/* Find the control. */
	list_for_each_entry(entity, &chain->entities, chain) {
		ret = uvc_ctrl_commit_entity(chain->dev, entity, rollback,
					     &err_ctrl);
		if (ret < 0)
			goto done;
	}

	if (!rollback)
		uvc_ctrl_send_events(handle, ctrls->controls, ctrls->count);
done:
	if (ret < 0 && ctrls)
		ctrls->error_idx = uvc_ctrl_find_ctrl_idx(entity, ctrls,
							  err_ctrl);
	mutex_unlock(&chain->ctrl_mutex);
	return ret;
}

int uvc_ctrl_get(struct uvc_video_chain *chain,
	struct v4l2_ext_control *xctrl)
{
	struct uvc_control *ctrl;
	struct uvc_control_mapping *mapping;

	if (__uvc_query_v4l2_class(chain, xctrl->id, 0) >= 0)
		return -EACCES;

	ctrl = uvc_find_control(chain, xctrl->id, &mapping);
	if (ctrl == NULL)
		return -EINVAL;

	return __uvc_ctrl_get(chain, ctrl, mapping, &xctrl->value);
}

int uvc_ctrl_set(struct uvc_fh *handle,
	struct v4l2_ext_control *xctrl)
{
	struct uvc_video_chain *chain = handle->chain;
	struct uvc_control *ctrl;
	struct uvc_control_mapping *mapping;
	s32 value;
	u32 step;
	s32 min;
	s32 max;
	int ret;

	if (__uvc_query_v4l2_class(chain, xctrl->id, 0) >= 0)
		return -EACCES;

	ctrl = uvc_find_control(chain, xctrl->id, &mapping);
	if (ctrl == NULL)
		return -EINVAL;
	if (!(ctrl->info.flags & UVC_CTRL_FLAG_SET_CUR))
		return -EACCES;

	/* Clamp out of range values. */
	switch (mapping->v4l2_type) {
	case V4L2_CTRL_TYPE_INTEGER:
		if (!ctrl->cached) {
			ret = uvc_ctrl_populate_cache(chain, ctrl);
			if (ret < 0)
				return ret;
		}

		min = mapping->get(mapping, UVC_GET_MIN,
				   uvc_ctrl_data(ctrl, UVC_CTRL_DATA_MIN));
		max = mapping->get(mapping, UVC_GET_MAX,
				   uvc_ctrl_data(ctrl, UVC_CTRL_DATA_MAX));
		step = mapping->get(mapping, UVC_GET_RES,
				    uvc_ctrl_data(ctrl, UVC_CTRL_DATA_RES));
		if (step == 0)
			step = 1;

		xctrl->value = min + DIV_ROUND_CLOSEST((u32)(xctrl->value - min),
							step) * step;
		if (mapping->data_type == UVC_CTRL_DATA_TYPE_SIGNED)
			xctrl->value = clamp(xctrl->value, min, max);
		else
			xctrl->value = clamp_t(u32, xctrl->value, min, max);
		value = xctrl->value;
		break;

	case V4L2_CTRL_TYPE_BOOLEAN:
		xctrl->value = clamp(xctrl->value, 0, 1);
		value = xctrl->value;
		break;

	case V4L2_CTRL_TYPE_MENU:
		if (xctrl->value < 0 || xctrl->value >= mapping->menu_count)
			return -ERANGE;
		value = mapping->menu_info[xctrl->value].value;

		/* Valid menu indices are reported by the GET_RES request for
		 * UVC controls that support it.
		 */
		if (mapping->data_type == UVC_CTRL_DATA_TYPE_BITMASK &&
		    (ctrl->info.flags & UVC_CTRL_FLAG_GET_RES)) {
			if (!ctrl->cached) {
				ret = uvc_ctrl_populate_cache(chain, ctrl);
				if (ret < 0)
					return ret;
			}

			step = mapping->get(mapping, UVC_GET_RES,
					uvc_ctrl_data(ctrl, UVC_CTRL_DATA_RES));
			if (!(step & value))
				return -EINVAL;
		}

		break;

	default:
		value = xctrl->value;
		break;
	}

	/* If the mapping doesn't span the whole UVC control, the current value
	 * needs to be loaded from the device to perform the read-modify-write
	 * operation.
	 */
	if (!ctrl->loaded && (ctrl->info.size * 8) != mapping->size) {
		if ((ctrl->info.flags & UVC_CTRL_FLAG_GET_CUR) == 0) {
			memset(uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT),
				0, ctrl->info.size);
		} else {
			ret = uvc_query_ctrl(chain->dev, UVC_GET_CUR,
				ctrl->entity->id, chain->dev->intfnum,
				ctrl->info.selector,
				uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT),
				ctrl->info.size);
			if (ret < 0)
				return ret;
		}

		ctrl->loaded = 1;
	}

	/* Backup the current value in case we need to rollback later. */
	if (!ctrl->dirty) {
		memcpy(uvc_ctrl_data(ctrl, UVC_CTRL_DATA_BACKUP),
		       uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT),
		       ctrl->info.size);
	}

	mapping->set(mapping, value,
		uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT));

	if (ctrl->info.flags & UVC_CTRL_FLAG_ASYNCHRONOUS)
		ctrl->handle = handle;

	ctrl->dirty = 1;
	ctrl->modified = 1;
	return 0;
}

/* --------------------------------------------------------------------------
 * Dynamic controls
 */

/*
 * Retrieve flags for a given control
 */
static int uvc_ctrl_get_flags(struct uvc_device *dev,
			      const struct uvc_control *ctrl,
			      struct uvc_control_info *info)
{
	u8 *data;
	int ret;

	data = kmalloc(1, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	if (ctrl->entity->get_info)
		ret = ctrl->entity->get_info(dev, ctrl->entity,
					     ctrl->info.selector, data);
	else
		ret = uvc_query_ctrl(dev, UVC_GET_INFO, ctrl->entity->id,
				     dev->intfnum, info->selector, data, 1);
	if (!ret)
		info->flags |= (data[0] & UVC_CONTROL_CAP_GET ?
				UVC_CTRL_FLAG_GET_CUR : 0)
			    |  (data[0] & UVC_CONTROL_CAP_SET ?
				UVC_CTRL_FLAG_SET_CUR : 0)
			    |  (data[0] & UVC_CONTROL_CAP_AUTOUPDATE ?
				UVC_CTRL_FLAG_AUTO_UPDATE : 0)
			    |  (data[0] & UVC_CONTROL_CAP_ASYNCHRONOUS ?
				UVC_CTRL_FLAG_ASYNCHRONOUS : 0);

	kfree(data);
	return ret;
}

static void uvc_ctrl_fixup_xu_info(struct uvc_device *dev,
	const struct uvc_control *ctrl, struct uvc_control_info *info)
{
	struct uvc_ctrl_fixup {
		struct usb_device_id id;
		u8 entity;
		u8 selector;
		u8 flags;
	};

	static const struct uvc_ctrl_fixup fixups[] = {
		{ { USB_DEVICE(0x046d, 0x08c2) }, 9, 1,
			UVC_CTRL_FLAG_GET_MIN | UVC_CTRL_FLAG_GET_MAX |
			UVC_CTRL_FLAG_GET_DEF | UVC_CTRL_FLAG_SET_CUR |
			UVC_CTRL_FLAG_AUTO_UPDATE },
		{ { USB_DEVICE(0x046d, 0x08cc) }, 9, 1,
			UVC_CTRL_FLAG_GET_MIN | UVC_CTRL_FLAG_GET_MAX |
			UVC_CTRL_FLAG_GET_DEF | UVC_CTRL_FLAG_SET_CUR |
			UVC_CTRL_FLAG_AUTO_UPDATE },
		{ { USB_DEVICE(0x046d, 0x0994) }, 9, 1,
			UVC_CTRL_FLAG_GET_MIN | UVC_CTRL_FLAG_GET_MAX |
			UVC_CTRL_FLAG_GET_DEF | UVC_CTRL_FLAG_SET_CUR |
			UVC_CTRL_FLAG_AUTO_UPDATE },
	};

	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(fixups); ++i) {
		if (!usb_match_one_id(dev->intf, &fixups[i].id))
			continue;

		if (fixups[i].entity == ctrl->entity->id &&
		    fixups[i].selector == info->selector) {
			info->flags = fixups[i].flags;
			return;
		}
	}
}

/*
 * Query control information (size and flags) for XU controls.
 */
static int uvc_ctrl_fill_xu_info(struct uvc_device *dev,
	const struct uvc_control *ctrl, struct uvc_control_info *info)
{
	u8 *data;
	int ret;

	data = kmalloc(2, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	memcpy(info->entity, ctrl->entity->guid, sizeof(info->entity));
	info->index = ctrl->index;
	info->selector = ctrl->index + 1;

	/* Query and verify the control length (GET_LEN) */
	ret = uvc_query_ctrl(dev, UVC_GET_LEN, ctrl->entity->id, dev->intfnum,
			     info->selector, data, 2);
	if (ret < 0) {
		uvc_dbg(dev, CONTROL,
			"GET_LEN failed on control %pUl/%u (%d)\n",
			info->entity, info->selector, ret);
		goto done;
	}

	info->size = le16_to_cpup((__le16 *)data);

	info->flags = UVC_CTRL_FLAG_GET_MIN | UVC_CTRL_FLAG_GET_MAX
		    | UVC_CTRL_FLAG_GET_RES | UVC_CTRL_FLAG_GET_DEF;

	ret = uvc_ctrl_get_flags(dev, ctrl, info);
	if (ret < 0) {
		uvc_dbg(dev, CONTROL,
			"Failed to get flags for control %pUl/%u (%d)\n",
			info->entity, info->selector, ret);
		goto done;
	}

	uvc_ctrl_fixup_xu_info(dev, ctrl, info);

	uvc_dbg(dev, CONTROL,
		"XU control %pUl/%u queried: len %u, flags { get %u set %u auto %u }\n",
		info->entity, info->selector, info->size,
		(info->flags & UVC_CTRL_FLAG_GET_CUR) ? 1 : 0,
		(info->flags & UVC_CTRL_FLAG_SET_CUR) ? 1 : 0,
		(info->flags & UVC_CTRL_FLAG_AUTO_UPDATE) ? 1 : 0);

done:
	kfree(data);
	return ret;
}

static int uvc_ctrl_add_info(struct uvc_device *dev, struct uvc_control *ctrl,
	const struct uvc_control_info *info);

static int uvc_ctrl_init_xu_ctrl(struct uvc_device *dev,
	struct uvc_control *ctrl)
{
	struct uvc_control_info info;
	int ret;

	if (ctrl->initialized)
		return 0;

	ret = uvc_ctrl_fill_xu_info(dev, ctrl, &info);
	if (ret < 0)
		return ret;

	ret = uvc_ctrl_add_info(dev, ctrl, &info);
	if (ret < 0)
		uvc_dbg(dev, CONTROL,
			"Failed to initialize control %pUl/%u on device %s entity %u\n",
			info.entity, info.selector, dev->udev->devpath,
			ctrl->entity->id);

	return ret;
}

int uvc_xu_ctrl_query(struct uvc_video_chain *chain,
	struct uvc_xu_control_query *xqry)
{
	struct uvc_entity *entity;
	struct uvc_control *ctrl;
	unsigned int i;
	bool found;
	u32 reqflags;
	u16 size;
	u8 *data = NULL;
	int ret;

	/* Find the extension unit. */
	found = false;
	list_for_each_entry(entity, &chain->entities, chain) {
		if (UVC_ENTITY_TYPE(entity) == UVC_VC_EXTENSION_UNIT &&
		    entity->id == xqry->unit) {
			found = true;
			break;
		}
	}

	if (!found) {
		uvc_dbg(chain->dev, CONTROL, "Extension unit %u not found\n",
			xqry->unit);
		return -ENOENT;
	}

	/* Find the control and perform delayed initialization if needed. */
	found = false;
	for (i = 0; i < entity->ncontrols; ++i) {
		ctrl = &entity->controls[i];
		if (ctrl->index == xqry->selector - 1) {
			found = true;
			break;
		}
	}

	if (!found) {
		uvc_dbg(chain->dev, CONTROL, "Control %pUl/%u not found\n",
			entity->guid, xqry->selector);
		return -ENOENT;
	}

	if (mutex_lock_interruptible(&chain->ctrl_mutex))
		return -ERESTARTSYS;

	ret = uvc_ctrl_init_xu_ctrl(chain->dev, ctrl);
	if (ret < 0) {
		ret = -ENOENT;
		goto done;
	}

	/* Validate the required buffer size and flags for the request */
	reqflags = 0;
	size = ctrl->info.size;

	switch (xqry->query) {
	case UVC_GET_CUR:
		reqflags = UVC_CTRL_FLAG_GET_CUR;
		break;
	case UVC_GET_MIN:
		reqflags = UVC_CTRL_FLAG_GET_MIN;
		break;
	case UVC_GET_MAX:
		reqflags = UVC_CTRL_FLAG_GET_MAX;
		break;
	case UVC_GET_DEF:
		reqflags = UVC_CTRL_FLAG_GET_DEF;
		break;
	case UVC_GET_RES:
		reqflags = UVC_CTRL_FLAG_GET_RES;
		break;
	case UVC_SET_CUR:
		reqflags = UVC_CTRL_FLAG_SET_CUR;
		break;
	case UVC_GET_LEN:
		size = 2;
		break;
	case UVC_GET_INFO:
		size = 1;
		break;
	default:
		ret = -EINVAL;
		goto done;
	}

	if (size != xqry->size) {
		ret = -ENOBUFS;
		goto done;
	}

	if (reqflags && !(ctrl->info.flags & reqflags)) {
		ret = -EBADRQC;
		goto done;
	}

	data = kmalloc(size, GFP_KERNEL);
	if (data == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	if (xqry->query == UVC_SET_CUR &&
	    copy_from_user(data, xqry->data, size)) {
		ret = -EFAULT;
		goto done;
	}

	ret = uvc_query_ctrl(chain->dev, xqry->query, xqry->unit,
			     chain->dev->intfnum, xqry->selector, data, size);
	if (ret < 0)
		goto done;

	if (xqry->query != UVC_SET_CUR &&
	    copy_to_user(xqry->data, data, size))
		ret = -EFAULT;
done:
	kfree(data);
	mutex_unlock(&chain->ctrl_mutex);
	return ret;
}

/* --------------------------------------------------------------------------
 * Suspend/resume
 */

/*
 * Restore control values after resume, skipping controls that haven't been
 * changed.
 *
 * TODO
 * - Don't restore modified controls that are back to their default value.
 * - Handle restore order (Auto-Exposure Mode should be restored before
 *   Exposure Time).
 */
int uvc_ctrl_restore_values(struct uvc_device *dev)
{
	struct uvc_control *ctrl;
	struct uvc_entity *entity;
	unsigned int i;
	int ret;

	/* Walk the entities list and restore controls when possible. */
	list_for_each_entry(entity, &dev->entities, list) {

		for (i = 0; i < entity->ncontrols; ++i) {
			ctrl = &entity->controls[i];

			if (!ctrl->initialized || !ctrl->modified ||
			    (ctrl->info.flags & UVC_CTRL_FLAG_RESTORE) == 0)
				continue;
			dev_dbg(&dev->udev->dev,
				"restoring control %pUl/%u/%u\n",
				ctrl->info.entity, ctrl->info.index,
				ctrl->info.selector);
			ctrl->dirty = 1;
		}

		ret = uvc_ctrl_commit_entity(dev, entity, 0, NULL);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* --------------------------------------------------------------------------
 * Control and mapping handling
 */

/*
 * Add control information to a given control.
 */
static int uvc_ctrl_add_info(struct uvc_device *dev, struct uvc_control *ctrl,
	const struct uvc_control_info *info)
{
	ctrl->info = *info;
	INIT_LIST_HEAD(&ctrl->info.mappings);

	/* Allocate an array to save control values (cur, def, max, etc.) */
	ctrl->uvc_data = kzalloc(ctrl->info.size * UVC_CTRL_DATA_LAST + 1,
				 GFP_KERNEL);
	if (!ctrl->uvc_data)
		return -ENOMEM;

	ctrl->initialized = 1;

	uvc_dbg(dev, CONTROL, "Added control %pUl/%u to device %s entity %u\n",
		ctrl->info.entity, ctrl->info.selector, dev->udev->devpath,
		ctrl->entity->id);

	return 0;
}

/*
 * Add a control mapping to a given control.
 */
static int __uvc_ctrl_add_mapping(struct uvc_video_chain *chain,
	struct uvc_control *ctrl, const struct uvc_control_mapping *mapping)
{
	struct uvc_control_mapping *map;
	unsigned int size;
	unsigned int i;

	/* Most mappings come from static kernel data and need to be duplicated.
	 * Mappings that come from userspace will be unnecessarily duplicated,
	 * this could be optimized.
	 */
	map = kmemdup(mapping, sizeof(*mapping), GFP_KERNEL);
	if (map == NULL)
		return -ENOMEM;

	/* For UVCIOC_CTRL_MAP custom control */
	if (mapping->name) {
		map->name = kstrdup(mapping->name, GFP_KERNEL);
		if (!map->name) {
			kfree(map);
			return -ENOMEM;
		}
	}

	INIT_LIST_HEAD(&map->ev_subs);

	size = sizeof(*mapping->menu_info) * mapping->menu_count;
	map->menu_info = kmemdup(mapping->menu_info, size, GFP_KERNEL);
	if (map->menu_info == NULL) {
		kfree(map->name);
		kfree(map);
		return -ENOMEM;
	}

	if (map->get == NULL)
		map->get = uvc_get_le_value;
	if (map->set == NULL)
		map->set = uvc_set_le_value;

	for (i = 0; i < ARRAY_SIZE(uvc_control_classes); i++) {
		if (V4L2_CTRL_ID2WHICH(uvc_control_classes[i]) ==
						V4L2_CTRL_ID2WHICH(map->id)) {
			chain->ctrl_class_bitmap |= BIT(i);
			break;
		}
	}

	list_add_tail(&map->list, &ctrl->info.mappings);
	uvc_dbg(chain->dev, CONTROL, "Adding mapping '%s' to control %pUl/%u\n",
		uvc_map_get_name(map), ctrl->info.entity,
		ctrl->info.selector);

	return 0;
}

int uvc_ctrl_add_mapping(struct uvc_video_chain *chain,
	const struct uvc_control_mapping *mapping)
{
	struct uvc_device *dev = chain->dev;
	struct uvc_control_mapping *map;
	struct uvc_entity *entity;
	struct uvc_control *ctrl;
	int found = 0;
	int ret;

	if (mapping->id & ~V4L2_CTRL_ID_MASK) {
		uvc_dbg(dev, CONTROL,
			"Can't add mapping '%s', control id 0x%08x is invalid\n",
			uvc_map_get_name(mapping), mapping->id);
		return -EINVAL;
	}

	/* Search for the matching (GUID/CS) control on the current chain */
	list_for_each_entry(entity, &chain->entities, chain) {
		unsigned int i;

		if (UVC_ENTITY_TYPE(entity) != UVC_VC_EXTENSION_UNIT ||
		    !uvc_entity_match_guid(entity, mapping->entity))
			continue;

		for (i = 0; i < entity->ncontrols; ++i) {
			ctrl = &entity->controls[i];
			if (ctrl->index == mapping->selector - 1) {
				found = 1;
				break;
			}
		}

		if (found)
			break;
	}
	if (!found)
		return -ENOENT;

	if (mutex_lock_interruptible(&chain->ctrl_mutex))
		return -ERESTARTSYS;

	/* Perform delayed initialization of XU controls */
	ret = uvc_ctrl_init_xu_ctrl(dev, ctrl);
	if (ret < 0) {
		ret = -ENOENT;
		goto done;
	}

	/* Validate the user-provided bit-size and offset */
	if (mapping->size > 32 ||
	    mapping->offset + mapping->size > ctrl->info.size * 8) {
		ret = -EINVAL;
		goto done;
	}

	list_for_each_entry(map, &ctrl->info.mappings, list) {
		if (mapping->id == map->id) {
			uvc_dbg(dev, CONTROL,
				"Can't add mapping '%s', control id 0x%08x already exists\n",
				uvc_map_get_name(mapping), mapping->id);
			ret = -EEXIST;
			goto done;
		}
	}

	/* Prevent excess memory consumption */
	if (atomic_inc_return(&dev->nmappings) > UVC_MAX_CONTROL_MAPPINGS) {
		atomic_dec(&dev->nmappings);
		uvc_dbg(dev, CONTROL,
			"Can't add mapping '%s', maximum mappings count (%u) exceeded\n",
			uvc_map_get_name(mapping), UVC_MAX_CONTROL_MAPPINGS);
		ret = -ENOMEM;
		goto done;
	}

	ret = __uvc_ctrl_add_mapping(chain, ctrl, mapping);
	if (ret < 0)
		atomic_dec(&dev->nmappings);

done:
	mutex_unlock(&chain->ctrl_mutex);
	return ret;
}

/*
 * Prune an entity of its bogus controls using a blacklist. Bogus controls
 * are currently the ones that crash the camera or unconditionally return an
 * error when queried.
 */
static void uvc_ctrl_prune_entity(struct uvc_device *dev,
	struct uvc_entity *entity)
{
	struct uvc_ctrl_blacklist {
		struct usb_device_id id;
		u8 index;
	};

	static const struct uvc_ctrl_blacklist processing_blacklist[] = {
		{ { USB_DEVICE(0x13d3, 0x509b) }, 9 }, /* Gain */
		{ { USB_DEVICE(0x1c4f, 0x3000) }, 6 }, /* WB Temperature */
		{ { USB_DEVICE(0x5986, 0x0241) }, 2 }, /* Hue */
	};
	static const struct uvc_ctrl_blacklist camera_blacklist[] = {
		{ { USB_DEVICE(0x06f8, 0x3005) }, 9 }, /* Zoom, Absolute */
	};

	const struct uvc_ctrl_blacklist *blacklist;
	unsigned int size;
	unsigned int count;
	unsigned int i;
	u8 *controls;

	switch (UVC_ENTITY_TYPE(entity)) {
	case UVC_VC_PROCESSING_UNIT:
		blacklist = processing_blacklist;
		count = ARRAY_SIZE(processing_blacklist);
		controls = entity->processing.bmControls;
		size = entity->processing.bControlSize;
		break;

	case UVC_ITT_CAMERA:
		blacklist = camera_blacklist;
		count = ARRAY_SIZE(camera_blacklist);
		controls = entity->camera.bmControls;
		size = entity->camera.bControlSize;
		break;

	default:
		return;
	}

	for (i = 0; i < count; ++i) {
		if (!usb_match_one_id(dev->intf, &blacklist[i].id))
			continue;

		if (blacklist[i].index >= 8 * size ||
		    !uvc_test_bit(controls, blacklist[i].index))
			continue;

		uvc_dbg(dev, CONTROL,
			"%u/%u control is black listed, removing it\n",
			entity->id, blacklist[i].index);

		uvc_clear_bit(controls, blacklist[i].index);
	}
}

/*
 * Add control information and hardcoded stock control mappings to the given
 * device.
 */
static void uvc_ctrl_init_ctrl(struct uvc_video_chain *chain,
			       struct uvc_control *ctrl)
{
	const struct uvc_control_info *info = uvc_ctrls;
	const struct uvc_control_info *iend = info + ARRAY_SIZE(uvc_ctrls);
	const struct uvc_control_mapping *mapping = uvc_ctrl_mappings;
	const struct uvc_control_mapping *mend =
		mapping + ARRAY_SIZE(uvc_ctrl_mappings);

	/* XU controls initialization requires querying the device for control
	 * information. As some buggy UVC devices will crash when queried
	 * repeatedly in a tight loop, delay XU controls initialization until
	 * first use.
	 */
	if (UVC_ENTITY_TYPE(ctrl->entity) == UVC_VC_EXTENSION_UNIT)
		return;

	for (; info < iend; ++info) {
		if (uvc_entity_match_guid(ctrl->entity, info->entity) &&
		    ctrl->index == info->index) {
			uvc_ctrl_add_info(chain->dev, ctrl, info);
			/*
			 * Retrieve control flags from the device. Ignore errors
			 * and work with default flag values from the uvc_ctrl
			 * array when the device doesn't properly implement
			 * GET_INFO on standard controls.
			 */
			uvc_ctrl_get_flags(chain->dev, ctrl, &ctrl->info);
			break;
		 }
	}

	if (!ctrl->initialized)
		return;

	for (; mapping < mend; ++mapping) {
		if (uvc_entity_match_guid(ctrl->entity, mapping->entity) &&
		    ctrl->info.selector == mapping->selector)
			__uvc_ctrl_add_mapping(chain, ctrl, mapping);
	}
}

/*
 * Initialize device controls.
 */
static int uvc_ctrl_init_chain(struct uvc_video_chain *chain)
{
	struct uvc_entity *entity;
	unsigned int i;

	/* Walk the entities list and instantiate controls */
	list_for_each_entry(entity, &chain->entities, chain) {
		struct uvc_control *ctrl;
		unsigned int bControlSize = 0, ncontrols;
		u8 *bmControls = NULL;

		if (UVC_ENTITY_TYPE(entity) == UVC_VC_EXTENSION_UNIT) {
			bmControls = entity->extension.bmControls;
			bControlSize = entity->extension.bControlSize;
		} else if (UVC_ENTITY_TYPE(entity) == UVC_VC_PROCESSING_UNIT) {
			bmControls = entity->processing.bmControls;
			bControlSize = entity->processing.bControlSize;
		} else if (UVC_ENTITY_TYPE(entity) == UVC_ITT_CAMERA) {
			bmControls = entity->camera.bmControls;
			bControlSize = entity->camera.bControlSize;
		} else if (UVC_ENTITY_TYPE(entity) == UVC_EXT_GPIO_UNIT) {
			bmControls = entity->gpio.bmControls;
			bControlSize = entity->gpio.bControlSize;
		}

		/* Remove bogus/blacklisted controls */
		uvc_ctrl_prune_entity(chain->dev, entity);

		/* Count supported controls and allocate the controls array */
		ncontrols = memweight(bmControls, bControlSize);
		if (ncontrols == 0)
			continue;

		entity->controls = kcalloc(ncontrols, sizeof(*ctrl),
					   GFP_KERNEL);
		if (entity->controls == NULL)
			return -ENOMEM;
		entity->ncontrols = ncontrols;

		/* Initialize all supported controls */
		ctrl = entity->controls;
		for (i = 0; i < bControlSize * 8; ++i) {
			if (uvc_test_bit(bmControls, i) == 0)
				continue;

			ctrl->entity = entity;
			ctrl->index = i;

			uvc_ctrl_init_ctrl(chain, ctrl);
			ctrl++;
		}
	}

	return 0;
}

int uvc_ctrl_init_device(struct uvc_device *dev)
{
	struct uvc_video_chain *chain;
	int ret;

	INIT_WORK(&dev->async_ctrl.work, uvc_ctrl_status_event_work);

	list_for_each_entry(chain, &dev->chains, list) {
		ret = uvc_ctrl_init_chain(chain);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Cleanup device controls.
 */
static void uvc_ctrl_cleanup_mappings(struct uvc_device *dev,
	struct uvc_control *ctrl)
{
	struct uvc_control_mapping *mapping, *nm;

	list_for_each_entry_safe(mapping, nm, &ctrl->info.mappings, list) {
		list_del(&mapping->list);
		kfree(mapping->menu_info);
		kfree(mapping->name);
		kfree(mapping);
	}
}

void uvc_ctrl_cleanup_device(struct uvc_device *dev)
{
	struct uvc_entity *entity;
	unsigned int i;

	/* Can be uninitialized if we are aborting on probe error. */
	if (dev->async_ctrl.work.func)
		cancel_work_sync(&dev->async_ctrl.work);

	/* Free controls and control mappings for all entities. */
	list_for_each_entry(entity, &dev->entities, list) {
		for (i = 0; i < entity->ncontrols; ++i) {
			struct uvc_control *ctrl = &entity->controls[i];

			if (!ctrl->initialized)
				continue;

			uvc_ctrl_cleanup_mappings(dev, ctrl);
			kfree(ctrl->uvc_data);
		}

		kfree(entity->controls);
	}
}

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      uvc_ctrl.c  --  USB Video Class driver - Controls
 *
 *      Copyright (C) 2005-2010
 *          Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <asm/barrier.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/uvc.h>
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
	/*
	 * UVC_CTRL_FLAG_AUTO_UPDATE is needed because the RoI may get updated
	 * by sensors.
	 * "This RoI should be the same as specified in most recent SET_CUR
	 * except in the case where the ‘Auto Detect and Track’ and/or
	 * ‘Image Stabilization’ bit have been set."
	 * 4.2.2.1.20 Digital Region of Interest (ROI) Control
	 */
	{
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_REGION_OF_INTEREST_CONTROL,
		.index		= 21,
		.size		= 10,
		.flags		= UVC_CTRL_FLAG_SET_CUR | UVC_CTRL_FLAG_GET_CUR
				| UVC_CTRL_FLAG_GET_MIN | UVC_CTRL_FLAG_GET_MAX
				| UVC_CTRL_FLAG_GET_DEF
				| UVC_CTRL_FLAG_AUTO_UPDATE,
	},
	{
		.entity		= UVC_GUID_CHROMEOS_XU,
		.selector	= UVC_CROSXU_CONTROL_IQ_PROFILE,
		.index		= 3,
		.size		= 1,
		.flags		= UVC_CTRL_FLAG_SET_CUR
				| UVC_CTRL_FLAG_GET_RANGE
				| UVC_CTRL_FLAG_RESTORE,
	},
};

static const u32 uvc_control_classes[] = {
	V4L2_CID_CAMERA_CLASS,
	V4L2_CID_USER_CLASS,
};

static const int exposure_auto_mapping[] = { 2, 1, 4, 8 };
static const int cros_colorfx_mapping[] = {
	1,	/* V4L2_COLORFX_NONE */
	-1,	/* V4L2_COLORFX_BW */
	-1,	/* V4L2_COLORFX_SEPIA */
	-1,	/* V4L2_COLORFX_NEGATIVE */
	-1,	/* V4L2_COLORFX_EMBOSS */
	-1,	/* V4L2_COLORFX_SKETCH */
	-1,	/* V4L2_COLORFX_SKY_BLUE */
	-1,	/* V4L2_COLORFX_GRASS_GREEN */
	-1,	/* V4L2_COLORFX_SKIN_WHITEN */
	0,	/* V4L2_COLORFX_VIVID */
};


static bool uvc_ctrl_mapping_is_compound(struct uvc_control_mapping *mapping)
{
	return mapping->v4l2_type >= V4L2_CTRL_COMPOUND_TYPES;
}

static s32 uvc_mapping_get_s32(struct uvc_control_mapping *mapping,
			       u8 query, const void *data_in)
{
	s32 data_out = 0;

	mapping->get(mapping, query, data_in, sizeof(data_out), &data_out);

	return data_out;
}

static void uvc_mapping_set_s32(struct uvc_control_mapping *mapping,
				s32 data_in, void *data_out)
{
	mapping->set(mapping, sizeof(data_in), &data_in, data_out);
}

/*
 * This function translates the V4L2 menu index @idx, as exposed to userspace as
 * the V4L2 control value, to the corresponding UVC control value used by the
 * device. The custom menu_mapping in the control @mapping is used when
 * available, otherwise the function assumes that the V4L2 and UVC values are
 * identical.
 *
 * For controls of type UVC_CTRL_DATA_TYPE_BITMASK, the UVC control value is
 * expressed as a bitmask and is thus guaranteed to have a single bit set.
 *
 * The function returns -EINVAL if the V4L2 menu index @idx isn't valid for the
 * control, which includes all controls whose type isn't UVC_CTRL_DATA_TYPE_ENUM
 * or UVC_CTRL_DATA_TYPE_BITMASK.
 */
static int uvc_mapping_get_menu_value(const struct uvc_control_mapping *mapping,
				      u32 idx)
{
	if (!test_bit(idx, &mapping->menu_mask))
		return -EINVAL;

	if (mapping->menu_mapping)
		return mapping->menu_mapping[idx];

	return idx;
}

static const char *
uvc_mapping_get_menu_name(const struct uvc_control_mapping *mapping, u32 idx)
{
	if (!test_bit(idx, &mapping->menu_mask))
		return NULL;

	if (mapping->menu_names)
		return mapping->menu_names[idx];

	return v4l2_ctrl_get_menu(mapping->id)[idx];
}

static int uvc_ctrl_get_zoom(struct uvc_control_mapping *mapping, u8 query,
			     const void *uvc_in, size_t v4l2_size,
			     void *v4l2_out)
{
	u8 value = ((u8 *)uvc_in)[2];
	s8 sign = ((s8 *)uvc_in)[0];
	s32 *out = v4l2_out;

	if (WARN_ON(v4l2_size != sizeof(s32)))
		return -EINVAL;

	switch (query) {
	case UVC_GET_CUR:
		*out = (sign == 0) ? 0 : (sign > 0 ? value : -value);
		return 0;

	case UVC_GET_MIN:
	case UVC_GET_MAX:
	case UVC_GET_RES:
	case UVC_GET_DEF:
	default:
		*out = value;
		return 0;
	}
}

static int uvc_ctrl_set_zoom(struct uvc_control_mapping *mapping,
			     size_t v4l2_size, const void *v4l2_in,
			     void *uvc_out)
{
	u8 *out = uvc_out;
	s32 value;

	if (WARN_ON(v4l2_size != sizeof(s32)))
		return -EINVAL;

	value = *(u32 *)v4l2_in;
	out[0] = value == 0 ? 0 : (value > 0) ? 1 : 0xff;
	out[2] = min_t(int, abs(value), 0xff);

	return 0;
}

static int uvc_ctrl_get_rel_speed(struct uvc_control_mapping *mapping,
				  u8 query, const void *uvc_in,
				  size_t v4l2_size, void *v4l2_out)
{
	unsigned int first = mapping->offset / 8;
	u8 value = ((u8 *)uvc_in)[first + 1];
	s8 sign = ((s8 *)uvc_in)[first];
	s32 *out = v4l2_out;

	if (WARN_ON(v4l2_size != sizeof(s32)))
		return -EINVAL;

	switch (query) {
	case UVC_GET_CUR:
		*out = (sign == 0) ? 0 : (sign > 0 ? value : -value);
		return 0;
	case UVC_GET_MIN:
		*out = -value;
		return 0;
	case UVC_GET_MAX:
	case UVC_GET_RES:
	case UVC_GET_DEF:
	default:
		*out = value;
		return 0;
	}
}

static int uvc_ctrl_set_rel_speed(struct uvc_control_mapping *mapping,
				  size_t v4l2_size, const void *v4l2_in,
				  void *uvc_out)
{
	unsigned int first = mapping->offset / 8;
	u8 *out = uvc_out;
	s32 value;

	if (WARN_ON(v4l2_size != sizeof(s32)))
		return -EINVAL;

	value = *(u32 *)v4l2_in;
	out[first] = value == 0 ? 0 : (value > 0) ? 1 : 0xff;
	out[first + 1] = min_t(int, abs(value), 0xff);

	return 0;
}

static const struct uvc_control_mapping uvc_ctrl_power_line_mapping_limited = {
	.id		= V4L2_CID_POWER_LINE_FREQUENCY,
	.entity		= UVC_GUID_UVC_PROCESSING,
	.selector	= UVC_PU_POWER_LINE_FREQUENCY_CONTROL,
	.size		= 2,
	.offset		= 0,
	.v4l2_type	= V4L2_CTRL_TYPE_MENU,
	.data_type	= UVC_CTRL_DATA_TYPE_ENUM,
	.menu_mask	= GENMASK(V4L2_CID_POWER_LINE_FREQUENCY_60HZ,
				  V4L2_CID_POWER_LINE_FREQUENCY_50HZ),
};

static const struct uvc_control_mapping uvc_ctrl_power_line_mapping_uvc11 = {
	.id		= V4L2_CID_POWER_LINE_FREQUENCY,
	.entity		= UVC_GUID_UVC_PROCESSING,
	.selector	= UVC_PU_POWER_LINE_FREQUENCY_CONTROL,
	.size		= 2,
	.offset		= 0,
	.v4l2_type	= V4L2_CTRL_TYPE_MENU,
	.data_type	= UVC_CTRL_DATA_TYPE_ENUM,
	.menu_mask	= GENMASK(V4L2_CID_POWER_LINE_FREQUENCY_60HZ,
				  V4L2_CID_POWER_LINE_FREQUENCY_DISABLED),
};

static const struct uvc_control_mapping uvc_ctrl_power_line_mapping_uvc15 = {
	.id		= V4L2_CID_POWER_LINE_FREQUENCY,
	.entity		= UVC_GUID_UVC_PROCESSING,
	.selector	= UVC_PU_POWER_LINE_FREQUENCY_CONTROL,
	.size		= 2,
	.offset		= 0,
	.v4l2_type	= V4L2_CTRL_TYPE_MENU,
	.data_type	= UVC_CTRL_DATA_TYPE_ENUM,
	.menu_mask	= GENMASK(V4L2_CID_POWER_LINE_FREQUENCY_AUTO,
				  V4L2_CID_POWER_LINE_FREQUENCY_DISABLED),
};

static const struct uvc_control_mapping *uvc_ctrl_filter_plf_mapping(
	struct uvc_video_chain *chain, struct uvc_control *ctrl)
{
	const struct uvc_control_mapping *out_mapping =
					&uvc_ctrl_power_line_mapping_uvc11;
	u8 *buf __free(kfree) = NULL;
	u8 init_val;
	int ret;

	buf = kmalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return NULL;

	/* Save the current PLF value, so we can restore it. */
	ret = uvc_query_ctrl(chain->dev, UVC_GET_CUR, ctrl->entity->id,
			     chain->dev->intfnum, ctrl->info.selector,
			     buf, sizeof(*buf));
	/* If we cannot read the control skip it. */
	if (ret)
		return NULL;
	init_val = *buf;

	/* If PLF value cannot be set to off, it is limited. */
	*buf = V4L2_CID_POWER_LINE_FREQUENCY_DISABLED;
	ret = uvc_query_ctrl(chain->dev, UVC_SET_CUR, ctrl->entity->id,
			     chain->dev->intfnum, ctrl->info.selector,
			     buf, sizeof(*buf));
	if (ret)
		return &uvc_ctrl_power_line_mapping_limited;

	/* UVC 1.1 does not define auto, we can exit. */
	if (chain->dev->uvc_version < 0x150)
		goto end;

	/* Check if the device supports auto. */
	*buf = V4L2_CID_POWER_LINE_FREQUENCY_AUTO;
	ret = uvc_query_ctrl(chain->dev, UVC_SET_CUR, ctrl->entity->id,
			     chain->dev->intfnum, ctrl->info.selector,
			     buf, sizeof(*buf));
	if (!ret)
		out_mapping = &uvc_ctrl_power_line_mapping_uvc15;

end:
	/* Restore initial value and add mapping. */
	*buf = init_val;
	uvc_query_ctrl(chain->dev, UVC_SET_CUR, ctrl->entity->id,
		       chain->dev->intfnum, ctrl->info.selector,
		       buf, sizeof(*buf));

	return out_mapping;
}

static int uvc_get_rect(struct uvc_control_mapping *mapping, u8 query,
			const void *uvc_in, size_t v4l2_size, void *v4l2_out)
{
	const struct uvc_rect *uvc_rect = uvc_in;
	struct v4l2_rect *v4l2_rect = v4l2_out;

	if (WARN_ON(v4l2_size != sizeof(struct v4l2_rect)))
		return -EINVAL;

	if (uvc_rect->left > uvc_rect->right ||
	    uvc_rect->top > uvc_rect->bottom)
		return -EIO;

	v4l2_rect->top = uvc_rect->top;
	v4l2_rect->left = uvc_rect->left;
	v4l2_rect->height = uvc_rect->bottom - uvc_rect->top + 1;
	v4l2_rect->width = uvc_rect->right - uvc_rect->left + 1;

	return 0;
}

static int uvc_set_rect(struct uvc_control_mapping *mapping, size_t v4l2_size,
			const void *v4l2_in, void *uvc_out)
{
	struct uvc_rect *uvc_rect = uvc_out;
	const struct v4l2_rect *v4l2_rect = v4l2_in;

	if (WARN_ON(v4l2_size != sizeof(struct v4l2_rect)))
		return -EINVAL;

	uvc_rect->top = min(0xffff, v4l2_rect->top);
	uvc_rect->left = min(0xffff, v4l2_rect->left);
	uvc_rect->bottom = min(0xffff, v4l2_rect->top + v4l2_rect->height - 1);
	uvc_rect->right = min(0xffff, v4l2_rect->left + v4l2_rect->width - 1);

	return 0;
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
		.menu_mapping	= exposure_auto_mapping,
		.menu_mask	= GENMASK(V4L2_EXPOSURE_APERTURE_PRIORITY,
					  V4L2_EXPOSURE_AUTO),
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
	{
		.entity		= UVC_GUID_UVC_PROCESSING,
		.selector	= UVC_PU_POWER_LINE_FREQUENCY_CONTROL,
		.filter_mapping	= uvc_ctrl_filter_plf_mapping,
	},
	{
		.id		= V4L2_CID_UVC_REGION_OF_INTEREST_RECT,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_REGION_OF_INTEREST_CONTROL,
		.size		= sizeof(struct uvc_rect) * 8,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_RECT,
		.data_type	= UVC_CTRL_DATA_TYPE_RECT,
		.get		= uvc_get_rect,
		.set		= uvc_set_rect,
		.name		= "Region of Interest Rectangle",
	},
	{
		.id		= V4L2_CID_UVC_REGION_OF_INTEREST_AUTO,
		.entity		= UVC_GUID_UVC_CAMERA,
		.selector	= UVC_CT_REGION_OF_INTEREST_CONTROL,
		.size		= 16,
		.offset		= 64,
		.v4l2_type	= V4L2_CTRL_TYPE_BITMASK,
		.data_type	= UVC_CTRL_DATA_TYPE_BITMASK,
		.name		= "Region of Interest Auto Ctrls",
	},
	{
		.id		= V4L2_CID_COLORFX,
		.entity		= UVC_GUID_CHROMEOS_XU,
		.selector	= UVC_CROSXU_CONTROL_IQ_PROFILE,
		.size		= 8,
		.offset		= 0,
		.v4l2_type	= V4L2_CTRL_TYPE_MENU,
		.data_type	= UVC_CTRL_DATA_TYPE_ENUM,
		.menu_mapping	= cros_colorfx_mapping,
		.menu_mask	= BIT(V4L2_COLORFX_VIVID) |
				  BIT(V4L2_COLORFX_NONE),
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

static s32 uvc_menu_to_v4l2_menu(struct uvc_control_mapping *mapping, s32 val)
{
	unsigned int i;

	for (i = 0; BIT(i) <= mapping->menu_mask; ++i) {
		u32 menu_value;

		if (!test_bit(i, &mapping->menu_mask))
			continue;

		menu_value = uvc_mapping_get_menu_value(mapping, i);

		if (menu_value == val)
			return i;
	}

	return val;
}

/*
 * Extract the bit string specified by mapping->offset and mapping->size
 * from the little-endian data stored at 'data' and return the result as
 * a signed 32bit integer. Sign extension will be performed if the mapping
 * references a signed data type.
 */
static int uvc_get_le_value(struct uvc_control_mapping *mapping,
			    u8 query, const void *uvc_in, size_t v4l2_size,
			    void *v4l2_out)
{
	int offset = mapping->offset;
	int bits = mapping->size;
	const u8 *data = uvc_in;
	s32 *out = v4l2_out;
	s32 value = 0;
	u8 mask;

	if (WARN_ON(v4l2_size != sizeof(s32)))
		return -EINVAL;

	data += offset / 8;
	offset &= 7;
	mask = ((1LL << bits) - 1) << offset;

	while (1) {
		u8 byte = *data & mask;
		value |= offset > 0 ? (byte >> offset) : (byte << (-offset));
		bits -= 8 - max(offset, 0);
		if (bits <= 0)
			break;

		offset -= 8;
		mask = (1 << bits) - 1;
		data++;
	}

	/* Sign-extend the value if needed. */
	if (mapping->data_type == UVC_CTRL_DATA_TYPE_SIGNED)
		value |= -(value & (1 << (mapping->size - 1)));

	/* If it is a menu, convert from uvc to v4l2. */
	if (mapping->v4l2_type != V4L2_CTRL_TYPE_MENU) {
		*out = value;
		return 0;
	}

	switch (query) {
	case UVC_GET_CUR:
	case UVC_GET_DEF:
		*out = uvc_menu_to_v4l2_menu(mapping, value);
		return 0;
	}

	*out = value;
	return 0;
}

/*
 * Set the bit string specified by mapping->offset and mapping->size
 * in the little-endian data stored at 'data' to the value 'value'.
 */
static int uvc_set_le_value(struct uvc_control_mapping *mapping,
			    size_t v4l2_size, const void *v4l2_in,
			    void *uvc_out)
{
	int offset = mapping->offset;
	int bits = mapping->size;
	u8 *data = uvc_out;
	s32 value;
	u8 mask;

	if (WARN_ON(v4l2_size != sizeof(s32)))
		return -EINVAL;

	value = *(s32 *)v4l2_in;

	switch (mapping->v4l2_type) {
	case V4L2_CTRL_TYPE_MENU:
		value = uvc_mapping_get_menu_value(mapping, value);
		break;
	case V4L2_CTRL_TYPE_BUTTON:
		/*
		 * According to the v4l2 spec, writing any value to a button
		 * control should result in the action belonging to the button
		 * control being triggered. UVC devices however want to see a 1
		 * written -> override value.
		 */
		value = -1;
		break;
	default:
		break;
	}

	data += offset / 8;
	offset &= 7;

	for (; bits > 0; data++) {
		mask = ((1LL << bits) - 1) << offset;
		*data = (*data & ~mask) | ((value << offset) & mask);
		value >>= offset ? offset : 8;
		bits -= 8 - offset;
		offset = 0;
	}

	return 0;
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
	int next, int next_compound)
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
			if (map->id == v4l2_id && !next && !next_compound) {
				*control = ctrl;
				*mapping = map;
				return;
			}

			if ((*mapping == NULL || (*mapping)->id > map->id) &&
			    (map->id > v4l2_id) &&
			    (uvc_ctrl_mapping_is_compound(map) ?
			     next_compound : next)) {
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
	int next_compound = v4l2_id & V4L2_CTRL_FLAG_NEXT_COMPOUND;

	*mapping = NULL;

	/* Mask the query flags. */
	v4l2_id &= V4L2_CTRL_ID_MASK;

	/* Find the control. */
	list_for_each_entry(entity, &chain->entities, chain) {
		__uvc_find_control(entity, v4l2_id, mapping, &ctrl, next,
				   next_compound);
		if (ctrl && !next && !next_compound)
			return ctrl;
	}

	if (!ctrl && !next && !next_compound)
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

			/*
			 * GET_RES is mandatory for XU controls, but some
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

static int __uvc_ctrl_load_cur(struct uvc_video_chain *chain,
			       struct uvc_control *ctrl)
{
	u8 *data;
	int ret;

	if (ctrl->loaded)
		return 0;

	data = uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT);

	if ((ctrl->info.flags & UVC_CTRL_FLAG_GET_CUR) == 0) {
		memset(data, 0, ctrl->info.size);
		ctrl->loaded = 1;

		return 0;
	}

	if (ctrl->entity->get_cur)
		ret = ctrl->entity->get_cur(chain->dev, ctrl->entity,
					    ctrl->info.selector, data,
					    ctrl->info.size);
	else
		ret = uvc_query_ctrl(chain->dev, UVC_GET_CUR,
				     ctrl->entity->id, chain->dev->intfnum,
				     ctrl->info.selector, data,
				     ctrl->info.size);

	if (ret < 0)
		return ret;

	ctrl->loaded = 1;

	return ret;
}

static int __uvc_ctrl_get(struct uvc_video_chain *chain,
			  struct uvc_control *ctrl,
			  struct uvc_control_mapping *mapping,
			  s32 *value)
{
	int ret;

	if ((ctrl->info.flags & UVC_CTRL_FLAG_GET_CUR) == 0)
		return -EACCES;

	ret = __uvc_ctrl_load_cur(chain, ctrl);
	if (ret < 0)
		return ret;

	*value = uvc_mapping_get_s32(mapping, UVC_GET_CUR,
				     uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT));

	return 0;
}

static int __uvc_query_v4l2_class(struct uvc_video_chain *chain, u32 req_id,
				  u32 found_id)
{
	bool find_next = req_id &
		(V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND);
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
				u32 found_id,
				struct v4l2_query_ext_ctrl *v4l2_ctrl)
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

static bool uvc_ctrl_is_readable(u32 which, struct uvc_control *ctrl,
				 struct uvc_control_mapping *mapping)
{
	if (which == V4L2_CTRL_WHICH_CUR_VAL)
		return !!(ctrl->info.flags & UVC_CTRL_FLAG_GET_CUR);

	if (which == V4L2_CTRL_WHICH_DEF_VAL)
		return !!(ctrl->info.flags & UVC_CTRL_FLAG_GET_DEF);

	/* Types with implicit boundaries. */
	switch (mapping->v4l2_type) {
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_BOOLEAN:
	case V4L2_CTRL_TYPE_BUTTON:
		return true;
	case V4L2_CTRL_TYPE_BITMASK:
		return (ctrl->info.flags & UVC_CTRL_FLAG_GET_RES) ||
			(ctrl->info.flags & UVC_CTRL_FLAG_GET_MAX);
	default:
		break;
	}

	if (which == V4L2_CTRL_WHICH_MIN_VAL)
		return !!(ctrl->info.flags & UVC_CTRL_FLAG_GET_MIN);

	if (which == V4L2_CTRL_WHICH_MAX_VAL)
		return !!(ctrl->info.flags & UVC_CTRL_FLAG_GET_MAX);

	return false;
}

/*
 * Check if control @v4l2_id can be accessed by the given control @ioctl
 * (VIDIOC_G_EXT_CTRLS, VIDIOC_TRY_EXT_CTRLS or VIDIOC_S_EXT_CTRLS).
 *
 * For set operations on slave controls, check if the master's value is set to
 * manual, either in the others controls set in the same ioctl call, or from
 * the master's current value. This catches VIDIOC_S_EXT_CTRLS calls that set
 * both the master and slave control, such as for instance setting
 * auto_exposure=1, exposure_time_absolute=251.
 */
int uvc_ctrl_is_accessible(struct uvc_video_chain *chain, u32 v4l2_id,
			   const struct v4l2_ext_controls *ctrls,
			   unsigned long ioctl)
{
	struct uvc_control_mapping *master_map = NULL;
	struct uvc_control *master_ctrl = NULL;
	struct uvc_control_mapping *mapping;
	struct uvc_control *ctrl;
	s32 val;
	int ret;
	int i;

	if (__uvc_query_v4l2_class(chain, v4l2_id, 0) >= 0)
		return -EACCES;

	ctrl = uvc_find_control(chain, v4l2_id, &mapping);
	if (!ctrl)
		return -EINVAL;

	if (ioctl == VIDIOC_G_EXT_CTRLS)
		return uvc_ctrl_is_readable(ctrls->which, ctrl, mapping);

	if (!(ctrl->info.flags & UVC_CTRL_FLAG_SET_CUR))
		return -EACCES;

	if (ioctl != VIDIOC_S_EXT_CTRLS || !mapping->master_id)
		return 0;

	/*
	 * Iterate backwards in cases where the master control is accessed
	 * multiple times in the same ioctl. We want the last value.
	 */
	for (i = ctrls->count - 1; i >= 0; i--) {
		if (ctrls->controls[i].id == mapping->master_id)
			return ctrls->controls[i].value ==
					mapping->master_manual ? 0 : -EACCES;
	}

	__uvc_find_control(ctrl->entity, mapping->master_id, &master_map,
			   &master_ctrl, 0, 0);

	if (!master_ctrl || !(master_ctrl->info.flags & UVC_CTRL_FLAG_GET_CUR))
		return 0;
	if (WARN_ON(uvc_ctrl_mapping_is_compound(master_map)))
		return -EIO;

	ret = __uvc_ctrl_get(chain, master_ctrl, master_map, &val);
	if (ret >= 0 && val != mapping->master_manual)
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

static u32 uvc_get_ctrl_bitmap(struct uvc_control *ctrl,
			       struct uvc_control_mapping *mapping)
{
	/*
	 * Some controls, like CT_AE_MODE_CONTROL, use GET_RES to represent
	 * the number of bits supported. Those controls do not list GET_MAX
	 * as supported.
	 */
	if (ctrl->info.flags & UVC_CTRL_FLAG_GET_RES)
		return uvc_mapping_get_s32(mapping, UVC_GET_RES,
					   uvc_ctrl_data(ctrl, UVC_CTRL_DATA_RES));

	if (ctrl->info.flags & UVC_CTRL_FLAG_GET_MAX)
		return uvc_mapping_get_s32(mapping, UVC_GET_MAX,
					   uvc_ctrl_data(ctrl, UVC_CTRL_DATA_MAX));

	return ~0;
}

/*
 * Maximum retry count to avoid spurious errors with controls. Increasing this
 * value does no seem to produce better results in the tested hardware.
 */
#define MAX_QUERY_RETRIES 2

static int __uvc_queryctrl_boundaries(struct uvc_video_chain *chain,
				      struct uvc_control *ctrl,
				      struct uvc_control_mapping *mapping,
				      struct v4l2_query_ext_ctrl *v4l2_ctrl)
{
	if (!ctrl->cached) {
		unsigned int retries;
		int ret;

		for (retries = 0; retries < MAX_QUERY_RETRIES; retries++) {
			ret = uvc_ctrl_populate_cache(chain, ctrl);
			if (ret != -EIO)
				break;
		}

		if (ret)
			return ret;
	}

	if (ctrl->info.flags & UVC_CTRL_FLAG_GET_DEF) {
		v4l2_ctrl->default_value = uvc_mapping_get_s32(mapping,
				UVC_GET_DEF, uvc_ctrl_data(ctrl, UVC_CTRL_DATA_DEF));
	}

	switch (mapping->v4l2_type) {
	case V4L2_CTRL_TYPE_MENU:
		v4l2_ctrl->minimum = ffs(mapping->menu_mask) - 1;
		v4l2_ctrl->maximum = fls(mapping->menu_mask) - 1;
		v4l2_ctrl->step = 1;
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

	case V4L2_CTRL_TYPE_BITMASK:
		v4l2_ctrl->minimum = 0;
		v4l2_ctrl->maximum = uvc_get_ctrl_bitmap(ctrl, mapping);
		v4l2_ctrl->step = 0;
		return 0;

	default:
		break;
	}

	if (ctrl->info.flags & UVC_CTRL_FLAG_GET_MIN)
		v4l2_ctrl->minimum = uvc_mapping_get_s32(mapping, UVC_GET_MIN,
				uvc_ctrl_data(ctrl, UVC_CTRL_DATA_MIN));
	else
		v4l2_ctrl->minimum = 0;

	if (ctrl->info.flags & UVC_CTRL_FLAG_GET_MAX)
		v4l2_ctrl->maximum = uvc_mapping_get_s32(mapping, UVC_GET_MAX,
				uvc_ctrl_data(ctrl, UVC_CTRL_DATA_MAX));
	else
		v4l2_ctrl->maximum = 0;

	if (ctrl->info.flags & UVC_CTRL_FLAG_GET_RES)
		v4l2_ctrl->step = uvc_mapping_get_s32(mapping, UVC_GET_RES,
				uvc_ctrl_data(ctrl, UVC_CTRL_DATA_RES));
	else
		v4l2_ctrl->step = 0;

	return 0;
}

static size_t uvc_mapping_v4l2_size(struct uvc_control_mapping *mapping)
{
	if (mapping->v4l2_type == V4L2_CTRL_TYPE_RECT)
		return sizeof(struct v4l2_rect);

	if (uvc_ctrl_mapping_is_compound(mapping))
		return DIV_ROUND_UP(mapping->size, 8);

	return sizeof(s32);
}

static int __uvc_query_v4l2_ctrl(struct uvc_video_chain *chain,
				 struct uvc_control *ctrl,
				 struct uvc_control_mapping *mapping,
				 struct v4l2_query_ext_ctrl *v4l2_ctrl)
{
	struct uvc_control_mapping *master_map = NULL;
	struct uvc_control *master_ctrl = NULL;
	int ret;

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
	if ((ctrl->info.flags & UVC_CTRL_FLAG_GET_MAX) &&
	    (ctrl->info.flags & UVC_CTRL_FLAG_GET_MIN))
		v4l2_ctrl->flags |= V4L2_CTRL_FLAG_HAS_WHICH_MIN_MAX;

	if (mapping->master_id)
		__uvc_find_control(ctrl->entity, mapping->master_id,
				   &master_map, &master_ctrl, 0, 0);
	if (master_ctrl && (master_ctrl->info.flags & UVC_CTRL_FLAG_GET_CUR)) {
		unsigned int retries;
		s32 val;
		int ret;

		if (WARN_ON(uvc_ctrl_mapping_is_compound(master_map)))
			return -EIO;

		for (retries = 0; retries < MAX_QUERY_RETRIES; retries++) {
			ret = __uvc_ctrl_get(chain, master_ctrl, master_map,
					     &val);
			if (!ret)
				break;
			if (ret < 0 && ret != -EIO)
				return ret;
		}

		if (ret == -EIO) {
			dev_warn_ratelimited(&chain->dev->intf->dev,
					     "UVC non compliance: Error %d querying master control %x (%s)\n",
					     ret, master_map->id,
					     uvc_map_get_name(master_map));
		} else {
			if (val != mapping->master_manual)
				v4l2_ctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		}
	}

	v4l2_ctrl->elem_size = uvc_mapping_v4l2_size(mapping);
	v4l2_ctrl->elems = 1;

	if (v4l2_ctrl->type >= V4L2_CTRL_COMPOUND_TYPES) {
		v4l2_ctrl->flags |= V4L2_CTRL_FLAG_HAS_PAYLOAD;
		v4l2_ctrl->default_value = 0;
		v4l2_ctrl->minimum = 0;
		v4l2_ctrl->maximum = 0;
		v4l2_ctrl->step = 0;
		return 0;
	}

	ret = __uvc_queryctrl_boundaries(chain, ctrl, mapping, v4l2_ctrl);
	if (ret && !mapping->disabled) {
		dev_warn(&chain->dev->intf->dev,
			 "UVC non compliance: permanently disabling control %x (%s), due to error %d\n",
			 mapping->id, uvc_map_get_name(mapping), ret);
		mapping->disabled = true;
	}

	if (mapping->disabled)
		v4l2_ctrl->flags |= V4L2_CTRL_FLAG_DISABLED;

	return 0;
}

int uvc_query_v4l2_ctrl(struct uvc_video_chain *chain,
			struct v4l2_query_ext_ctrl *v4l2_ctrl)
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
	struct uvc_control_mapping *mapping;
	struct uvc_control *ctrl;
	u32 index = query_menu->index;
	u32 id = query_menu->id;
	const char *name;
	int ret;

	memset(query_menu, 0, sizeof(*query_menu));
	query_menu->id = id;
	query_menu->index = index;

	if (index >= BITS_PER_TYPE(mapping->menu_mask))
		return -EINVAL;

	ret = mutex_lock_interruptible(&chain->ctrl_mutex);
	if (ret < 0)
		return -ERESTARTSYS;

	ctrl = uvc_find_control(chain, query_menu->id, &mapping);
	if (ctrl == NULL || mapping->v4l2_type != V4L2_CTRL_TYPE_MENU) {
		ret = -EINVAL;
		goto done;
	}

	if (!test_bit(query_menu->index, &mapping->menu_mask)) {
		ret = -EINVAL;
		goto done;
	}

	if (mapping->data_type == UVC_CTRL_DATA_TYPE_BITMASK) {
		int mask;

		if (!ctrl->cached) {
			ret = uvc_ctrl_populate_cache(chain, ctrl);
			if (ret < 0)
				goto done;
		}

		mask = uvc_mapping_get_menu_value(mapping, query_menu->index);
		if (mask < 0) {
			ret = mask;
			goto done;
		}

		if (!(uvc_get_ctrl_bitmap(ctrl, mapping) & mask)) {
			ret = -EINVAL;
			goto done;
		}
	}

	name = uvc_mapping_get_menu_name(mapping, query_menu->index);
	if (!name) {
		ret = -EINVAL;
		goto done;
	}

	strscpy(query_menu->name, name, sizeof(query_menu->name));

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
	struct v4l2_query_ext_ctrl v4l2_ctrl;

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

	__uvc_find_control(master->entity, slave_id, &mapping, &ctrl, 0, 0);
	if (ctrl == NULL)
		return;

	if (uvc_ctrl_mapping_is_compound(mapping) ||
	    __uvc_ctrl_get(chain, ctrl, mapping, &val) == 0)
		changes |= V4L2_EVENT_CTRL_CH_VALUE;

	uvc_ctrl_send_event(chain, handle, ctrl, mapping, val, changes);
}

static int uvc_ctrl_set_handle(struct uvc_control *ctrl, struct uvc_fh *handle)
{
	int ret;

	lockdep_assert_held(&handle->chain->ctrl_mutex);

	if (ctrl->handle) {
		dev_warn_ratelimited(&handle->stream->dev->intf->dev,
				     "UVC non compliance: Setting an async control with a pending operation.");

		if (ctrl->handle == handle)
			return 0;

		WARN_ON(!ctrl->handle->pending_async_ctrls);
		if (ctrl->handle->pending_async_ctrls)
			ctrl->handle->pending_async_ctrls--;
		ctrl->handle = handle;
		ctrl->handle->pending_async_ctrls++;
		return 0;
	}

	ret = uvc_pm_get(handle->chain->dev);
	if (ret)
		return ret;

	ctrl->handle = handle;
	ctrl->handle->pending_async_ctrls++;
	return 0;
}

static int uvc_ctrl_clear_handle(struct uvc_control *ctrl)
{
	lockdep_assert_held(&ctrl->handle->chain->ctrl_mutex);

	if (WARN_ON(!ctrl->handle->pending_async_ctrls)) {
		ctrl->handle = NULL;
		return -EINVAL;
	}

	ctrl->handle->pending_async_ctrls--;
	uvc_pm_put(ctrl->handle->chain->dev);
	ctrl->handle = NULL;
	return 0;
}

void uvc_ctrl_status_event(struct uvc_video_chain *chain,
			   struct uvc_control *ctrl, const u8 *data)
{
	struct uvc_control_mapping *mapping;
	struct uvc_fh *handle;
	unsigned int i;

	mutex_lock(&chain->ctrl_mutex);

	/* Flush the control cache, the data might have changed. */
	ctrl->loaded = 0;

	handle = ctrl->handle;
	if (handle)
		uvc_ctrl_clear_handle(ctrl);

	list_for_each_entry(mapping, &ctrl->info.mappings, list) {
		s32 value;

		if (uvc_ctrl_mapping_is_compound(mapping))
			value = 0;
		else
			value = uvc_mapping_get_s32(mapping, UVC_GET_CUR, data);

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

	/* The barrier is needed to synchronize with uvc_status_stop(). */
	if (smp_load_acquire(&dev->flush_status))
		return;

	/* Resubmit the URB. */
	w->urb->interval = dev->int_ep->desc.bInterval;
	ret = usb_submit_urb(w->urb, GFP_KERNEL);
	if (ret < 0)
		dev_err(&dev->intf->dev,
			"Failed to resubmit status URB (%d).\n", ret);
}

bool uvc_ctrl_status_event_async(struct urb *urb, struct uvc_video_chain *chain,
				 struct uvc_control *ctrl, const u8 *data)
{
	struct uvc_device *dev = chain->dev;
	struct uvc_ctrl_work *w = &dev->async_ctrl;

	if (list_empty(&ctrl->info.mappings))
		return false;

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
				 struct uvc_entity *entity,
				 const struct v4l2_ext_control *xctrls,
				 unsigned int xctrls_count)
{
	struct uvc_control_mapping *mapping;
	struct uvc_control *ctrl;
	unsigned int i;
	unsigned int j;

	for (i = 0; i < xctrls_count; ++i) {
		u32 changes = V4L2_EVENT_CTRL_CH_VALUE;
		s32 value;

		ctrl = uvc_find_control(handle->chain, xctrls[i].id, &mapping);
		if (ctrl->entity != entity)
			continue;

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

		if (uvc_ctrl_mapping_is_compound(mapping))
			value = 0;
		else
			value = xctrls[i].value;
		/*
		 * If the master is being modified in the same transaction
		 * flags may change too.
		 */
		if (mapping->master_id &&
		    uvc_ctrl_xctrls_has_control(xctrls, xctrls_count,
						mapping->master_id))
			changes |= V4L2_EVENT_CTRL_CH_FLAGS;

		uvc_ctrl_send_event(handle->chain, handle, ctrl, mapping,
				    value, changes);
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

	if (sev->flags & V4L2_EVENT_SUB_FL_SEND_INITIAL) {
		struct v4l2_event ev;
		u32 changes = V4L2_EVENT_CTRL_CH_FLAGS;
		s32 val = 0;

		ret = uvc_pm_get(handle->chain->dev);
		if (ret)
			goto done;

		if (uvc_ctrl_mapping_is_compound(mapping) ||
		    __uvc_ctrl_get(handle->chain, ctrl, mapping, &val) == 0)
			changes |= V4L2_EVENT_CTRL_CH_VALUE;

		uvc_ctrl_fill_event(handle->chain, &ev, ctrl, mapping, val,
				    changes);

		uvc_pm_put(handle->chain->dev);

		/*
		 * Mark the queue as active, allowing this initial event to be
		 * accepted.
		 */
		sev->elems = elems;
		v4l2_event_queue_fh(sev->fh, &ev);
	}

	list_add_tail(&sev->node, &mapping->ev_subs);

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

/*
 * Returns the number of uvc controls that have been correctly set, or a
 * negative number if there has been an error.
 */
static int uvc_ctrl_commit_entity(struct uvc_device *dev,
				  struct uvc_fh *handle,
				  struct uvc_entity *entity,
				  int rollback,
				  struct uvc_control **err_ctrl)
{
	unsigned int processed_ctrls = 0;
	struct uvc_control *ctrl;
	unsigned int i;
	int ret = 0;

	if (entity == NULL)
		return 0;

	for (i = 0; i < entity->ncontrols; ++i) {
		ctrl = &entity->controls[i];
		if (!ctrl->initialized)
			continue;

		/*
		 * Reset the loaded flag for auto-update controls that were
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

		if (!ret)
			processed_ctrls++;

		if (rollback || ret < 0)
			memcpy(uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT),
			       uvc_ctrl_data(ctrl, UVC_CTRL_DATA_BACKUP),
			       ctrl->info.size);

		ctrl->dirty = 0;

		if (!rollback && handle && !ret &&
		    ctrl->info.flags & UVC_CTRL_FLAG_ASYNCHRONOUS)
			ret = uvc_ctrl_set_handle(ctrl, handle);

		if (ret < 0 && !rollback) {
			if (err_ctrl)
				*err_ctrl = ctrl;
			/*
			 * If we fail to set a control, we need to rollback
			 * the next ones.
			 */
			rollback = 1;
		}
	}

	if (ret)
		return ret;

	return processed_ctrls;
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
				   &ctrl_found, 0, 0);
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
	int ret_out = 0;
	int ret;

	/* Find the control. */
	list_for_each_entry(entity, &chain->entities, chain) {
		ret = uvc_ctrl_commit_entity(chain->dev, handle, entity,
					     rollback, &err_ctrl);
		if (ret < 0) {
			if (ctrls)
				ctrls->error_idx =
					uvc_ctrl_find_ctrl_idx(entity, ctrls,
							       err_ctrl);
			/*
			 * When we fail to commit an entity, we need to
			 * restore the UVC_CTRL_DATA_BACKUP for all the
			 * controls in the other entities, otherwise our cache
			 * and the hardware will be out of sync.
			 */
			rollback = 1;

			ret_out = ret;
		} else if (ret > 0 && !rollback) {
			uvc_ctrl_send_events(handle, entity,
					     ctrls->controls, ctrls->count);
		}
	}

	mutex_unlock(&chain->ctrl_mutex);
	return ret_out;
}

static int uvc_mapping_get_xctrl_compound(struct uvc_video_chain *chain,
					  struct uvc_control *ctrl,
					  struct uvc_control_mapping *mapping,
					  u32 which,
					  struct v4l2_ext_control *xctrl)
{
	u8 *data __free(kfree) = NULL;
	size_t size;
	u8 query;
	int ret;
	int id;

	switch (which) {
	case V4L2_CTRL_WHICH_CUR_VAL:
		id = UVC_CTRL_DATA_CURRENT;
		query = UVC_GET_CUR;
		break;
	case V4L2_CTRL_WHICH_MIN_VAL:
		id = UVC_CTRL_DATA_MIN;
		query = UVC_GET_MIN;
		break;
	case V4L2_CTRL_WHICH_MAX_VAL:
		id = UVC_CTRL_DATA_MAX;
		query = UVC_GET_MAX;
		break;
	case V4L2_CTRL_WHICH_DEF_VAL:
		id = UVC_CTRL_DATA_DEF;
		query = UVC_GET_DEF;
		break;
	default:
		return -EINVAL;
	}

	size = uvc_mapping_v4l2_size(mapping);
	if (xctrl->size < size) {
		xctrl->size = size;
		return -ENOSPC;
	}

	data = kmalloc(size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (which == V4L2_CTRL_WHICH_CUR_VAL)
		ret = __uvc_ctrl_load_cur(chain, ctrl);
	else
		ret = uvc_ctrl_populate_cache(chain, ctrl);

	if (ret < 0)
		return ret;

	ret = mapping->get(mapping, query, uvc_ctrl_data(ctrl, id), size, data);
	if (ret < 0)
		return ret;

	/*
	 * v4l2_ext_control does not have enough room to fit a compound control.
	 * Instead, the value is in the user memory at xctrl->ptr. The v4l2
	 * ioctl helper does not copy it for us.
	 */
	return copy_to_user(xctrl->ptr, data, size) ? -EFAULT : 0;
}

static int uvc_mapping_get_xctrl_std(struct uvc_video_chain *chain,
				     struct uvc_control *ctrl,
				     struct uvc_control_mapping *mapping,
				     u32 which, struct v4l2_ext_control *xctrl)
{
	struct v4l2_query_ext_ctrl qec;
	int ret;

	switch (which) {
	case V4L2_CTRL_WHICH_CUR_VAL:
		return __uvc_ctrl_get(chain, ctrl, mapping, &xctrl->value);
	case V4L2_CTRL_WHICH_DEF_VAL:
	case V4L2_CTRL_WHICH_MIN_VAL:
	case V4L2_CTRL_WHICH_MAX_VAL:
		break;
	default:
		return -EINVAL;
	}

	ret = __uvc_queryctrl_boundaries(chain, ctrl, mapping, &qec);
	if (ret < 0)
		return ret;

	switch (which) {
	case V4L2_CTRL_WHICH_DEF_VAL:
		xctrl->value = qec.default_value;
		break;
	case V4L2_CTRL_WHICH_MIN_VAL:
		xctrl->value = qec.minimum;
		break;
	case V4L2_CTRL_WHICH_MAX_VAL:
		xctrl->value = qec.maximum;
		break;
	}

	return 0;
}

static int uvc_mapping_get_xctrl(struct uvc_video_chain *chain,
				 struct uvc_control *ctrl,
				 struct uvc_control_mapping *mapping,
				 u32 which, struct v4l2_ext_control *xctrl)
{
	if (uvc_ctrl_mapping_is_compound(mapping))
		return uvc_mapping_get_xctrl_compound(chain, ctrl, mapping,
						      which, xctrl);
	return uvc_mapping_get_xctrl_std(chain, ctrl, mapping, which, xctrl);
}

int uvc_ctrl_get(struct uvc_video_chain *chain, u32 which,
		 struct v4l2_ext_control *xctrl)
{
	struct uvc_control *ctrl;
	struct uvc_control_mapping *mapping;

	if (__uvc_query_v4l2_class(chain, xctrl->id, 0) >= 0)
		return -EACCES;

	ctrl = uvc_find_control(chain, xctrl->id, &mapping);
	if (!ctrl)
		return -EINVAL;

	return uvc_mapping_get_xctrl(chain, ctrl, mapping, which, xctrl);
}

static int uvc_ctrl_clamp(struct uvc_video_chain *chain,
			  struct uvc_control *ctrl,
			  struct uvc_control_mapping *mapping,
			  s32 *value_in_out)
{
	s32 value = *value_in_out;
	u32 step;
	s32 min;
	s32 max;
	int ret;

	switch (mapping->v4l2_type) {
	case V4L2_CTRL_TYPE_INTEGER:
		if (!ctrl->cached) {
			ret = uvc_ctrl_populate_cache(chain, ctrl);
			if (ret < 0)
				return ret;
		}

		min = uvc_mapping_get_s32(mapping, UVC_GET_MIN,
					  uvc_ctrl_data(ctrl, UVC_CTRL_DATA_MIN));
		max = uvc_mapping_get_s32(mapping, UVC_GET_MAX,
					  uvc_ctrl_data(ctrl, UVC_CTRL_DATA_MAX));
		step = uvc_mapping_get_s32(mapping, UVC_GET_RES,
					   uvc_ctrl_data(ctrl, UVC_CTRL_DATA_RES));
		if (step == 0)
			step = 1;

		value = min + DIV_ROUND_CLOSEST((u32)(value - min), step) * step;
		if (mapping->data_type == UVC_CTRL_DATA_TYPE_SIGNED)
			value = clamp(value, min, max);
		else
			value = clamp_t(u32, value, min, max);
		*value_in_out = value;
		return 0;

	case V4L2_CTRL_TYPE_BITMASK:
		if (!ctrl->cached) {
			ret = uvc_ctrl_populate_cache(chain, ctrl);
			if (ret < 0)
				return ret;
		}

		value &= uvc_get_ctrl_bitmap(ctrl, mapping);
		*value_in_out = value;
		return 0;

	case V4L2_CTRL_TYPE_BOOLEAN:
		*value_in_out = clamp(value, 0, 1);
		return 0;

	case V4L2_CTRL_TYPE_MENU:
		if (value < (ffs(mapping->menu_mask) - 1) ||
		    value > (fls(mapping->menu_mask) - 1))
			return -ERANGE;

		if (!test_bit(value, &mapping->menu_mask))
			return -EINVAL;

		/*
		 * Valid menu indices are reported by the GET_RES request for
		 * UVC controls that support it.
		 */
		if (mapping->data_type == UVC_CTRL_DATA_TYPE_BITMASK) {
			int val = uvc_mapping_get_menu_value(mapping, value);
			if (!ctrl->cached) {
				ret = uvc_ctrl_populate_cache(chain, ctrl);
				if (ret < 0)
					return ret;
			}

			if (!(uvc_get_ctrl_bitmap(ctrl, mapping) & val))
				return -EINVAL;
		}
		return 0;

	default:
		return 0;
	}

	return 0;
}

static int uvc_mapping_set_xctrl_compound(struct uvc_control *ctrl,
					  struct uvc_control_mapping *mapping,
					  struct v4l2_ext_control *xctrl)
{
	u8 *data __free(kfree) = NULL;
	size_t size = uvc_mapping_v4l2_size(mapping);

	if (xctrl->size != size)
		return -EINVAL;

	/*
	 * v4l2_ext_control does not have enough room to fit a compound control.
	 * Instead, the value is in the user memory at xctrl->ptr. The v4l2
	 * ioctl helper does not copy it for us.
	 */
	data = memdup_user(xctrl->ptr, size);
	if (IS_ERR(data))
		return PTR_ERR(data);

	return mapping->set(mapping, size, data,
			    uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT));
}

static int uvc_mapping_set_xctrl(struct uvc_control *ctrl,
				 struct uvc_control_mapping *mapping,
				 struct v4l2_ext_control *xctrl)
{
	if (uvc_ctrl_mapping_is_compound(mapping))
		return uvc_mapping_set_xctrl_compound(ctrl, mapping, xctrl);

	uvc_mapping_set_s32(mapping, xctrl->value,
			    uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT));
	return 0;
}

int uvc_ctrl_set(struct uvc_fh *handle, struct v4l2_ext_control *xctrl)
{
	struct uvc_video_chain *chain = handle->chain;
	struct uvc_control_mapping *mapping;
	struct uvc_control *ctrl;
	int ret;

	lockdep_assert_held(&chain->ctrl_mutex);

	if (__uvc_query_v4l2_class(chain, xctrl->id, 0) >= 0)
		return -EACCES;

	ctrl = uvc_find_control(chain, xctrl->id, &mapping);
	if (!ctrl)
		return -EINVAL;
	if (!(ctrl->info.flags & UVC_CTRL_FLAG_SET_CUR))
		return -EACCES;

	ret = uvc_ctrl_clamp(chain, ctrl, mapping, &xctrl->value);
	if (ret)
		return ret;
	/*
	 * If the mapping doesn't span the whole UVC control, the current value
	 * needs to be loaded from the device to perform the read-modify-write
	 * operation.
	 */
	if ((ctrl->info.size * 8) != mapping->size) {
		ret = __uvc_ctrl_load_cur(chain, ctrl);
		if (ret < 0)
			return ret;
	}

	/* Backup the current value in case we need to rollback later. */
	if (!ctrl->dirty) {
		memcpy(uvc_ctrl_data(ctrl, UVC_CTRL_DATA_BACKUP),
		       uvc_ctrl_data(ctrl, UVC_CTRL_DATA_CURRENT),
		       ctrl->info.size);
	}

	ret = uvc_mapping_set_xctrl(ctrl, mapping, xctrl);
	if (ret)
		return ret;

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

	if (!ret) {
		info->flags &= ~(UVC_CTRL_FLAG_GET_CUR |
				 UVC_CTRL_FLAG_SET_CUR |
				 UVC_CTRL_FLAG_AUTO_UPDATE |
				 UVC_CTRL_FLAG_ASYNCHRONOUS);

		info->flags |= (data[0] & UVC_CONTROL_CAP_GET ?
				UVC_CTRL_FLAG_GET_CUR : 0)
			    |  (data[0] & UVC_CONTROL_CAP_SET ?
				UVC_CTRL_FLAG_SET_CUR : 0)
			    |  (data[0] & UVC_CONTROL_CAP_AUTOUPDATE ?
				UVC_CTRL_FLAG_AUTO_UPDATE : 0)
			    |  (data[0] & UVC_CONTROL_CAP_ASYNCHRONOUS ?
				UVC_CTRL_FLAG_ASYNCHRONOUS : 0);
	}

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
	struct uvc_entity *entity, *iter;
	struct uvc_control *ctrl;
	unsigned int i;
	bool found;
	u32 reqflags;
	u16 size;
	u8 *data = NULL;
	int ret;

	/* Find the extension unit. */
	entity = NULL;
	list_for_each_entry(iter, &chain->entities, chain) {
		if (UVC_ENTITY_TYPE(iter) == UVC_VC_EXTENSION_UNIT &&
		    iter->id == xqry->unit) {
			entity = iter;
			break;
		}
	}

	if (!entity) {
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
			dev_dbg(&dev->intf->dev,
				"restoring control %pUl/%u/%u\n",
				ctrl->info.entity, ctrl->info.index,
				ctrl->info.selector);
			ctrl->dirty = 1;
		}

		ret = uvc_ctrl_commit_entity(dev, NULL, entity, 0, NULL);
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
	int ret;

	/*
	 * Most mappings come from static kernel data, and need to be duplicated.
	 * Mappings that come from userspace will be unnecessarily duplicated,
	 * this could be optimized.
	 */
	map = kmemdup(mapping, sizeof(*mapping), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	map->name = NULL;
	map->menu_names = NULL;
	map->menu_mapping = NULL;

	/* For UVCIOC_CTRL_MAP custom control */
	if (mapping->name) {
		map->name = kstrdup(mapping->name, GFP_KERNEL);
		if (!map->name)
			goto err_nomem;
	}

	INIT_LIST_HEAD(&map->ev_subs);

	if (mapping->menu_mapping && mapping->menu_mask) {
		size = sizeof(mapping->menu_mapping[0])
		       * fls(mapping->menu_mask);
		map->menu_mapping = kmemdup(mapping->menu_mapping, size,
					    GFP_KERNEL);
		if (!map->menu_mapping)
			goto err_nomem;
	}
	if (mapping->menu_names && mapping->menu_mask) {
		size = sizeof(mapping->menu_names[0])
		       * fls(mapping->menu_mask);
		map->menu_names = kmemdup(mapping->menu_names, size,
					  GFP_KERNEL);
		if (!map->menu_names)
			goto err_nomem;
	}

	if (uvc_ctrl_mapping_is_compound(map))
		if (WARN_ON(!map->set || !map->get)) {
			ret = -EIO;
			goto free_mem;
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

err_nomem:
	ret = -ENOMEM;
free_mem:
	kfree(map->menu_names);
	kfree(map->menu_mapping);
	kfree(map->name);
	kfree(map);
	return ret;
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
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(uvc_ctrls); ++i) {
		const struct uvc_control_info *info = &uvc_ctrls[i];

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

	/* Process common mappings. */
	for (i = 0; i < ARRAY_SIZE(uvc_ctrl_mappings); ++i) {
		const struct uvc_control_mapping *mapping = &uvc_ctrl_mappings[i];

		if (!uvc_entity_match_guid(ctrl->entity, mapping->entity) ||
		    ctrl->info.selector != mapping->selector)
			continue;

		/* Let the device provide a custom mapping. */
		if (mapping->filter_mapping) {
			mapping = mapping->filter_mapping(chain, ctrl);
			if (!mapping)
				continue;
		}

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

void uvc_ctrl_cleanup_fh(struct uvc_fh *handle)
{
	struct uvc_entity *entity;

	guard(mutex)(&handle->chain->ctrl_mutex);

	if (!handle->pending_async_ctrls)
		return;

	list_for_each_entry(entity, &handle->chain->dev->entities, list) {
		for (unsigned int i = 0; i < entity->ncontrols; ++i) {
			if (entity->controls[i].handle != handle)
				continue;
			uvc_ctrl_clear_handle(&entity->controls[i]);
		}
	}

	if (!WARN_ON(handle->pending_async_ctrls))
		return;

	for (unsigned int i = 0; i < handle->pending_async_ctrls; i++)
		uvc_pm_put(handle->stream->dev);
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
		kfree(mapping->menu_names);
		kfree(mapping->menu_mapping);
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

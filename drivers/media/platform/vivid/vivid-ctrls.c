// SPDX-License-Identifier: GPL-2.0-only
/*
 * vivid-ctrls.c - control support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/videodev2.h>
#include <media/v4l2-event.h>
#include <media/v4l2-common.h>

#include "vivid-core.h"
#include "vivid-vid-cap.h"
#include "vivid-vid-out.h"
#include "vivid-vid-common.h"
#include "vivid-radio-common.h"
#include "vivid-osd.h"
#include "vivid-ctrls.h"

#define VIVID_CID_CUSTOM_BASE		(V4L2_CID_USER_BASE | 0xf000)
#define VIVID_CID_BUTTON		(VIVID_CID_CUSTOM_BASE + 0)
#define VIVID_CID_BOOLEAN		(VIVID_CID_CUSTOM_BASE + 1)
#define VIVID_CID_INTEGER		(VIVID_CID_CUSTOM_BASE + 2)
#define VIVID_CID_INTEGER64		(VIVID_CID_CUSTOM_BASE + 3)
#define VIVID_CID_MENU			(VIVID_CID_CUSTOM_BASE + 4)
#define VIVID_CID_STRING		(VIVID_CID_CUSTOM_BASE + 5)
#define VIVID_CID_BITMASK		(VIVID_CID_CUSTOM_BASE + 6)
#define VIVID_CID_INTMENU		(VIVID_CID_CUSTOM_BASE + 7)
#define VIVID_CID_U32_ARRAY		(VIVID_CID_CUSTOM_BASE + 8)
#define VIVID_CID_U16_MATRIX		(VIVID_CID_CUSTOM_BASE + 9)
#define VIVID_CID_U8_4D_ARRAY		(VIVID_CID_CUSTOM_BASE + 10)

#define VIVID_CID_VIVID_BASE		(0x00f00000 | 0xf000)
#define VIVID_CID_VIVID_CLASS		(0x00f00000 | 1)
#define VIVID_CID_TEST_PATTERN		(VIVID_CID_VIVID_BASE + 0)
#define VIVID_CID_OSD_TEXT_MODE		(VIVID_CID_VIVID_BASE + 1)
#define VIVID_CID_HOR_MOVEMENT		(VIVID_CID_VIVID_BASE + 2)
#define VIVID_CID_VERT_MOVEMENT		(VIVID_CID_VIVID_BASE + 3)
#define VIVID_CID_SHOW_BORDER		(VIVID_CID_VIVID_BASE + 4)
#define VIVID_CID_SHOW_SQUARE		(VIVID_CID_VIVID_BASE + 5)
#define VIVID_CID_INSERT_SAV		(VIVID_CID_VIVID_BASE + 6)
#define VIVID_CID_INSERT_EAV		(VIVID_CID_VIVID_BASE + 7)
#define VIVID_CID_VBI_CAP_INTERLACED	(VIVID_CID_VIVID_BASE + 8)

#define VIVID_CID_HFLIP			(VIVID_CID_VIVID_BASE + 20)
#define VIVID_CID_VFLIP			(VIVID_CID_VIVID_BASE + 21)
#define VIVID_CID_STD_ASPECT_RATIO	(VIVID_CID_VIVID_BASE + 22)
#define VIVID_CID_DV_TIMINGS_ASPECT_RATIO	(VIVID_CID_VIVID_BASE + 23)
#define VIVID_CID_TSTAMP_SRC		(VIVID_CID_VIVID_BASE + 24)
#define VIVID_CID_COLORSPACE		(VIVID_CID_VIVID_BASE + 25)
#define VIVID_CID_XFER_FUNC		(VIVID_CID_VIVID_BASE + 26)
#define VIVID_CID_YCBCR_ENC		(VIVID_CID_VIVID_BASE + 27)
#define VIVID_CID_QUANTIZATION		(VIVID_CID_VIVID_BASE + 28)
#define VIVID_CID_LIMITED_RGB_RANGE	(VIVID_CID_VIVID_BASE + 29)
#define VIVID_CID_ALPHA_MODE		(VIVID_CID_VIVID_BASE + 30)
#define VIVID_CID_HAS_CROP_CAP		(VIVID_CID_VIVID_BASE + 31)
#define VIVID_CID_HAS_COMPOSE_CAP	(VIVID_CID_VIVID_BASE + 32)
#define VIVID_CID_HAS_SCALER_CAP	(VIVID_CID_VIVID_BASE + 33)
#define VIVID_CID_HAS_CROP_OUT		(VIVID_CID_VIVID_BASE + 34)
#define VIVID_CID_HAS_COMPOSE_OUT	(VIVID_CID_VIVID_BASE + 35)
#define VIVID_CID_HAS_SCALER_OUT	(VIVID_CID_VIVID_BASE + 36)
#define VIVID_CID_LOOP_VIDEO		(VIVID_CID_VIVID_BASE + 37)
#define VIVID_CID_SEQ_WRAP		(VIVID_CID_VIVID_BASE + 38)
#define VIVID_CID_TIME_WRAP		(VIVID_CID_VIVID_BASE + 39)
#define VIVID_CID_MAX_EDID_BLOCKS	(VIVID_CID_VIVID_BASE + 40)
#define VIVID_CID_PERCENTAGE_FILL	(VIVID_CID_VIVID_BASE + 41)
#define VIVID_CID_REDUCED_FPS		(VIVID_CID_VIVID_BASE + 42)
#define VIVID_CID_HSV_ENC		(VIVID_CID_VIVID_BASE + 43)

#define VIVID_CID_STD_SIGNAL_MODE	(VIVID_CID_VIVID_BASE + 60)
#define VIVID_CID_STANDARD		(VIVID_CID_VIVID_BASE + 61)
#define VIVID_CID_DV_TIMINGS_SIGNAL_MODE	(VIVID_CID_VIVID_BASE + 62)
#define VIVID_CID_DV_TIMINGS		(VIVID_CID_VIVID_BASE + 63)
#define VIVID_CID_PERC_DROPPED		(VIVID_CID_VIVID_BASE + 64)
#define VIVID_CID_DISCONNECT		(VIVID_CID_VIVID_BASE + 65)
#define VIVID_CID_DQBUF_ERROR		(VIVID_CID_VIVID_BASE + 66)
#define VIVID_CID_QUEUE_SETUP_ERROR	(VIVID_CID_VIVID_BASE + 67)
#define VIVID_CID_BUF_PREPARE_ERROR	(VIVID_CID_VIVID_BASE + 68)
#define VIVID_CID_START_STR_ERROR	(VIVID_CID_VIVID_BASE + 69)
#define VIVID_CID_QUEUE_ERROR		(VIVID_CID_VIVID_BASE + 70)
#define VIVID_CID_CLEAR_FB		(VIVID_CID_VIVID_BASE + 71)
#define VIVID_CID_REQ_VALIDATE_ERROR	(VIVID_CID_VIVID_BASE + 72)

#define VIVID_CID_RADIO_SEEK_MODE	(VIVID_CID_VIVID_BASE + 90)
#define VIVID_CID_RADIO_SEEK_PROG_LIM	(VIVID_CID_VIVID_BASE + 91)
#define VIVID_CID_RADIO_RX_RDS_RBDS	(VIVID_CID_VIVID_BASE + 92)
#define VIVID_CID_RADIO_RX_RDS_BLOCKIO	(VIVID_CID_VIVID_BASE + 93)

#define VIVID_CID_RADIO_TX_RDS_BLOCKIO	(VIVID_CID_VIVID_BASE + 94)

#define VIVID_CID_SDR_CAP_FM_DEVIATION	(VIVID_CID_VIVID_BASE + 110)

/* General User Controls */

static int vivid_user_gen_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vivid_dev *dev = container_of(ctrl->handler, struct vivid_dev, ctrl_hdl_user_gen);

	switch (ctrl->id) {
	case VIVID_CID_DISCONNECT:
		v4l2_info(&dev->v4l2_dev, "disconnect\n");
		clear_bit(V4L2_FL_REGISTERED, &dev->vid_cap_dev.flags);
		clear_bit(V4L2_FL_REGISTERED, &dev->vid_out_dev.flags);
		clear_bit(V4L2_FL_REGISTERED, &dev->vbi_cap_dev.flags);
		clear_bit(V4L2_FL_REGISTERED, &dev->vbi_out_dev.flags);
		clear_bit(V4L2_FL_REGISTERED, &dev->sdr_cap_dev.flags);
		clear_bit(V4L2_FL_REGISTERED, &dev->radio_rx_dev.flags);
		clear_bit(V4L2_FL_REGISTERED, &dev->radio_tx_dev.flags);
		break;
	case VIVID_CID_BUTTON:
		dev->button_pressed = 30;
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vivid_user_gen_ctrl_ops = {
	.s_ctrl = vivid_user_gen_s_ctrl,
};

static const struct v4l2_ctrl_config vivid_ctrl_button = {
	.ops = &vivid_user_gen_ctrl_ops,
	.id = VIVID_CID_BUTTON,
	.name = "Button",
	.type = V4L2_CTRL_TYPE_BUTTON,
};

static const struct v4l2_ctrl_config vivid_ctrl_boolean = {
	.ops = &vivid_user_gen_ctrl_ops,
	.id = VIVID_CID_BOOLEAN,
	.name = "Boolean",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_int32 = {
	.ops = &vivid_user_gen_ctrl_ops,
	.id = VIVID_CID_INTEGER,
	.name = "Integer 32 Bits",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0xffffffff80000000ULL,
	.max = 0x7fffffff,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_int64 = {
	.ops = &vivid_user_gen_ctrl_ops,
	.id = VIVID_CID_INTEGER64,
	.name = "Integer 64 Bits",
	.type = V4L2_CTRL_TYPE_INTEGER64,
	.min = 0x8000000000000000ULL,
	.max = 0x7fffffffffffffffLL,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_u32_array = {
	.ops = &vivid_user_gen_ctrl_ops,
	.id = VIVID_CID_U32_ARRAY,
	.name = "U32 1 Element Array",
	.type = V4L2_CTRL_TYPE_U32,
	.def = 0x18,
	.min = 0x10,
	.max = 0x20000,
	.step = 1,
	.dims = { 1 },
};

static const struct v4l2_ctrl_config vivid_ctrl_u16_matrix = {
	.ops = &vivid_user_gen_ctrl_ops,
	.id = VIVID_CID_U16_MATRIX,
	.name = "U16 8x16 Matrix",
	.type = V4L2_CTRL_TYPE_U16,
	.def = 0x18,
	.min = 0x10,
	.max = 0x2000,
	.step = 1,
	.dims = { 8, 16 },
};

static const struct v4l2_ctrl_config vivid_ctrl_u8_4d_array = {
	.ops = &vivid_user_gen_ctrl_ops,
	.id = VIVID_CID_U8_4D_ARRAY,
	.name = "U8 2x3x4x5 Array",
	.type = V4L2_CTRL_TYPE_U8,
	.def = 0x18,
	.min = 0x10,
	.max = 0x20,
	.step = 1,
	.dims = { 2, 3, 4, 5 },
};

static const char * const vivid_ctrl_menu_strings[] = {
	"Menu Item 0 (Skipped)",
	"Menu Item 1",
	"Menu Item 2 (Skipped)",
	"Menu Item 3",
	"Menu Item 4",
	"Menu Item 5 (Skipped)",
	NULL,
};

static const struct v4l2_ctrl_config vivid_ctrl_menu = {
	.ops = &vivid_user_gen_ctrl_ops,
	.id = VIVID_CID_MENU,
	.name = "Menu",
	.type = V4L2_CTRL_TYPE_MENU,
	.min = 1,
	.max = 4,
	.def = 3,
	.menu_skip_mask = 0x04,
	.qmenu = vivid_ctrl_menu_strings,
};

static const struct v4l2_ctrl_config vivid_ctrl_string = {
	.ops = &vivid_user_gen_ctrl_ops,
	.id = VIVID_CID_STRING,
	.name = "String",
	.type = V4L2_CTRL_TYPE_STRING,
	.min = 2,
	.max = 4,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_bitmask = {
	.ops = &vivid_user_gen_ctrl_ops,
	.id = VIVID_CID_BITMASK,
	.name = "Bitmask",
	.type = V4L2_CTRL_TYPE_BITMASK,
	.def = 0x80002000,
	.min = 0,
	.max = 0x80402010,
	.step = 0,
};

static const s64 vivid_ctrl_int_menu_values[] = {
	1, 1, 2, 3, 5, 8, 13, 21, 42,
};

static const struct v4l2_ctrl_config vivid_ctrl_int_menu = {
	.ops = &vivid_user_gen_ctrl_ops,
	.id = VIVID_CID_INTMENU,
	.name = "Integer Menu",
	.type = V4L2_CTRL_TYPE_INTEGER_MENU,
	.min = 1,
	.max = 8,
	.def = 4,
	.menu_skip_mask = 0x02,
	.qmenu_int = vivid_ctrl_int_menu_values,
};

static const struct v4l2_ctrl_config vivid_ctrl_disconnect = {
	.ops = &vivid_user_gen_ctrl_ops,
	.id = VIVID_CID_DISCONNECT,
	.name = "Disconnect",
	.type = V4L2_CTRL_TYPE_BUTTON,
};


/* Framebuffer Controls */

static int vivid_fb_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vivid_dev *dev = container_of(ctrl->handler,
					     struct vivid_dev, ctrl_hdl_fb);

	switch (ctrl->id) {
	case VIVID_CID_CLEAR_FB:
		vivid_clear_fb(dev);
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vivid_fb_ctrl_ops = {
	.s_ctrl = vivid_fb_s_ctrl,
};

static const struct v4l2_ctrl_config vivid_ctrl_clear_fb = {
	.ops = &vivid_fb_ctrl_ops,
	.id = VIVID_CID_CLEAR_FB,
	.name = "Clear Framebuffer",
	.type = V4L2_CTRL_TYPE_BUTTON,
};


/* Video User Controls */

static int vivid_user_vid_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vivid_dev *dev = container_of(ctrl->handler, struct vivid_dev, ctrl_hdl_user_vid);

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		dev->gain->val = (jiffies_to_msecs(jiffies) / 1000) & 0xff;
		break;
	}
	return 0;
}

static int vivid_user_vid_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vivid_dev *dev = container_of(ctrl->handler, struct vivid_dev, ctrl_hdl_user_vid);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		dev->input_brightness[dev->input] = ctrl->val - dev->input * 128;
		tpg_s_brightness(&dev->tpg, dev->input_brightness[dev->input]);
		break;
	case V4L2_CID_CONTRAST:
		tpg_s_contrast(&dev->tpg, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		tpg_s_saturation(&dev->tpg, ctrl->val);
		break;
	case V4L2_CID_HUE:
		tpg_s_hue(&dev->tpg, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		dev->hflip = ctrl->val;
		tpg_s_hflip(&dev->tpg, dev->sensor_hflip ^ dev->hflip);
		break;
	case V4L2_CID_VFLIP:
		dev->vflip = ctrl->val;
		tpg_s_vflip(&dev->tpg, dev->sensor_vflip ^ dev->vflip);
		break;
	case V4L2_CID_ALPHA_COMPONENT:
		tpg_s_alpha_component(&dev->tpg, ctrl->val);
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vivid_user_vid_ctrl_ops = {
	.g_volatile_ctrl = vivid_user_vid_g_volatile_ctrl,
	.s_ctrl = vivid_user_vid_s_ctrl,
};


/* Video Capture Controls */

static int vivid_vid_cap_s_ctrl(struct v4l2_ctrl *ctrl)
{
	static const u32 colorspaces[] = {
		V4L2_COLORSPACE_SMPTE170M,
		V4L2_COLORSPACE_REC709,
		V4L2_COLORSPACE_SRGB,
		V4L2_COLORSPACE_OPRGB,
		V4L2_COLORSPACE_BT2020,
		V4L2_COLORSPACE_DCI_P3,
		V4L2_COLORSPACE_SMPTE240M,
		V4L2_COLORSPACE_470_SYSTEM_M,
		V4L2_COLORSPACE_470_SYSTEM_BG,
	};
	struct vivid_dev *dev = container_of(ctrl->handler, struct vivid_dev, ctrl_hdl_vid_cap);
	unsigned i;

	switch (ctrl->id) {
	case VIVID_CID_TEST_PATTERN:
		vivid_update_quality(dev);
		tpg_s_pattern(&dev->tpg, ctrl->val);
		break;
	case VIVID_CID_COLORSPACE:
		tpg_s_colorspace(&dev->tpg, colorspaces[ctrl->val]);
		vivid_send_source_change(dev, TV);
		vivid_send_source_change(dev, SVID);
		vivid_send_source_change(dev, HDMI);
		vivid_send_source_change(dev, WEBCAM);
		break;
	case VIVID_CID_XFER_FUNC:
		tpg_s_xfer_func(&dev->tpg, ctrl->val);
		vivid_send_source_change(dev, TV);
		vivid_send_source_change(dev, SVID);
		vivid_send_source_change(dev, HDMI);
		vivid_send_source_change(dev, WEBCAM);
		break;
	case VIVID_CID_YCBCR_ENC:
		tpg_s_ycbcr_enc(&dev->tpg, ctrl->val);
		vivid_send_source_change(dev, TV);
		vivid_send_source_change(dev, SVID);
		vivid_send_source_change(dev, HDMI);
		vivid_send_source_change(dev, WEBCAM);
		break;
	case VIVID_CID_HSV_ENC:
		tpg_s_hsv_enc(&dev->tpg, ctrl->val ? V4L2_HSV_ENC_256 :
						     V4L2_HSV_ENC_180);
		vivid_send_source_change(dev, TV);
		vivid_send_source_change(dev, SVID);
		vivid_send_source_change(dev, HDMI);
		vivid_send_source_change(dev, WEBCAM);
		break;
	case VIVID_CID_QUANTIZATION:
		tpg_s_quantization(&dev->tpg, ctrl->val);
		vivid_send_source_change(dev, TV);
		vivid_send_source_change(dev, SVID);
		vivid_send_source_change(dev, HDMI);
		vivid_send_source_change(dev, WEBCAM);
		break;
	case V4L2_CID_DV_RX_RGB_RANGE:
		if (!vivid_is_hdmi_cap(dev))
			break;
		tpg_s_rgb_range(&dev->tpg, ctrl->val);
		break;
	case VIVID_CID_LIMITED_RGB_RANGE:
		tpg_s_real_rgb_range(&dev->tpg, ctrl->val ?
				V4L2_DV_RGB_RANGE_LIMITED : V4L2_DV_RGB_RANGE_FULL);
		break;
	case VIVID_CID_ALPHA_MODE:
		tpg_s_alpha_mode(&dev->tpg, ctrl->val);
		break;
	case VIVID_CID_HOR_MOVEMENT:
		tpg_s_mv_hor_mode(&dev->tpg, ctrl->val);
		break;
	case VIVID_CID_VERT_MOVEMENT:
		tpg_s_mv_vert_mode(&dev->tpg, ctrl->val);
		break;
	case VIVID_CID_OSD_TEXT_MODE:
		dev->osd_mode = ctrl->val;
		break;
	case VIVID_CID_PERCENTAGE_FILL:
		tpg_s_perc_fill(&dev->tpg, ctrl->val);
		for (i = 0; i < VIDEO_MAX_FRAME; i++)
			dev->must_blank[i] = ctrl->val < 100;
		break;
	case VIVID_CID_INSERT_SAV:
		tpg_s_insert_sav(&dev->tpg, ctrl->val);
		break;
	case VIVID_CID_INSERT_EAV:
		tpg_s_insert_eav(&dev->tpg, ctrl->val);
		break;
	case VIVID_CID_HFLIP:
		dev->sensor_hflip = ctrl->val;
		tpg_s_hflip(&dev->tpg, dev->sensor_hflip ^ dev->hflip);
		break;
	case VIVID_CID_VFLIP:
		dev->sensor_vflip = ctrl->val;
		tpg_s_vflip(&dev->tpg, dev->sensor_vflip ^ dev->vflip);
		break;
	case VIVID_CID_REDUCED_FPS:
		dev->reduced_fps = ctrl->val;
		vivid_update_format_cap(dev, true);
		break;
	case VIVID_CID_HAS_CROP_CAP:
		dev->has_crop_cap = ctrl->val;
		vivid_update_format_cap(dev, true);
		break;
	case VIVID_CID_HAS_COMPOSE_CAP:
		dev->has_compose_cap = ctrl->val;
		vivid_update_format_cap(dev, true);
		break;
	case VIVID_CID_HAS_SCALER_CAP:
		dev->has_scaler_cap = ctrl->val;
		vivid_update_format_cap(dev, true);
		break;
	case VIVID_CID_SHOW_BORDER:
		tpg_s_show_border(&dev->tpg, ctrl->val);
		break;
	case VIVID_CID_SHOW_SQUARE:
		tpg_s_show_square(&dev->tpg, ctrl->val);
		break;
	case VIVID_CID_STD_ASPECT_RATIO:
		dev->std_aspect_ratio = ctrl->val;
		tpg_s_video_aspect(&dev->tpg, vivid_get_video_aspect(dev));
		break;
	case VIVID_CID_DV_TIMINGS_SIGNAL_MODE:
		dev->dv_timings_signal_mode = dev->ctrl_dv_timings_signal_mode->val;
		if (dev->dv_timings_signal_mode == SELECTED_DV_TIMINGS)
			dev->query_dv_timings = dev->ctrl_dv_timings->val;
		v4l2_ctrl_activate(dev->ctrl_dv_timings,
				dev->dv_timings_signal_mode == SELECTED_DV_TIMINGS);
		vivid_update_quality(dev);
		vivid_send_source_change(dev, HDMI);
		break;
	case VIVID_CID_DV_TIMINGS_ASPECT_RATIO:
		dev->dv_timings_aspect_ratio = ctrl->val;
		tpg_s_video_aspect(&dev->tpg, vivid_get_video_aspect(dev));
		break;
	case VIVID_CID_TSTAMP_SRC:
		dev->tstamp_src_is_soe = ctrl->val;
		dev->vb_vid_cap_q.timestamp_flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
		if (dev->tstamp_src_is_soe)
			dev->vb_vid_cap_q.timestamp_flags |= V4L2_BUF_FLAG_TSTAMP_SRC_SOE;
		break;
	case VIVID_CID_MAX_EDID_BLOCKS:
		dev->edid_max_blocks = ctrl->val;
		if (dev->edid_blocks > dev->edid_max_blocks)
			dev->edid_blocks = dev->edid_max_blocks;
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vivid_vid_cap_ctrl_ops = {
	.s_ctrl = vivid_vid_cap_s_ctrl,
};

static const char * const vivid_ctrl_hor_movement_strings[] = {
	"Move Left Fast",
	"Move Left",
	"Move Left Slow",
	"No Movement",
	"Move Right Slow",
	"Move Right",
	"Move Right Fast",
	NULL,
};

static const struct v4l2_ctrl_config vivid_ctrl_hor_movement = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_HOR_MOVEMENT,
	.name = "Horizontal Movement",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = TPG_MOVE_POS_FAST,
	.def = TPG_MOVE_NONE,
	.qmenu = vivid_ctrl_hor_movement_strings,
};

static const char * const vivid_ctrl_vert_movement_strings[] = {
	"Move Up Fast",
	"Move Up",
	"Move Up Slow",
	"No Movement",
	"Move Down Slow",
	"Move Down",
	"Move Down Fast",
	NULL,
};

static const struct v4l2_ctrl_config vivid_ctrl_vert_movement = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_VERT_MOVEMENT,
	.name = "Vertical Movement",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = TPG_MOVE_POS_FAST,
	.def = TPG_MOVE_NONE,
	.qmenu = vivid_ctrl_vert_movement_strings,
};

static const struct v4l2_ctrl_config vivid_ctrl_show_border = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_SHOW_BORDER,
	.name = "Show Border",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_show_square = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_SHOW_SQUARE,
	.name = "Show Square",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};

static const char * const vivid_ctrl_osd_mode_strings[] = {
	"All",
	"Counters Only",
	"None",
	NULL,
};

static const struct v4l2_ctrl_config vivid_ctrl_osd_mode = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_OSD_TEXT_MODE,
	.name = "OSD Text Mode",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = ARRAY_SIZE(vivid_ctrl_osd_mode_strings) - 2,
	.qmenu = vivid_ctrl_osd_mode_strings,
};

static const struct v4l2_ctrl_config vivid_ctrl_perc_fill = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_PERCENTAGE_FILL,
	.name = "Fill Percentage of Frame",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 100,
	.def = 100,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_insert_sav = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_INSERT_SAV,
	.name = "Insert SAV Code in Image",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_insert_eav = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_INSERT_EAV,
	.name = "Insert EAV Code in Image",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_hflip = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_HFLIP,
	.name = "Sensor Flipped Horizontally",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_vflip = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_VFLIP,
	.name = "Sensor Flipped Vertically",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_reduced_fps = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_REDUCED_FPS,
	.name = "Reduced Framerate",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_has_crop_cap = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_HAS_CROP_CAP,
	.name = "Enable Capture Cropping",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.def = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_has_compose_cap = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_HAS_COMPOSE_CAP,
	.name = "Enable Capture Composing",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.def = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_has_scaler_cap = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_HAS_SCALER_CAP,
	.name = "Enable Capture Scaler",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.def = 1,
	.step = 1,
};

static const char * const vivid_ctrl_tstamp_src_strings[] = {
	"End of Frame",
	"Start of Exposure",
	NULL,
};

static const struct v4l2_ctrl_config vivid_ctrl_tstamp_src = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_TSTAMP_SRC,
	.name = "Timestamp Source",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = ARRAY_SIZE(vivid_ctrl_tstamp_src_strings) - 2,
	.qmenu = vivid_ctrl_tstamp_src_strings,
};

static const struct v4l2_ctrl_config vivid_ctrl_std_aspect_ratio = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_STD_ASPECT_RATIO,
	.name = "Standard Aspect Ratio",
	.type = V4L2_CTRL_TYPE_MENU,
	.min = 1,
	.max = 4,
	.def = 1,
	.qmenu = tpg_aspect_strings,
};

static const char * const vivid_ctrl_dv_timings_signal_mode_strings[] = {
	"Current DV Timings",
	"No Signal",
	"No Lock",
	"Out of Range",
	"Selected DV Timings",
	"Cycle Through All DV Timings",
	"Custom DV Timings",
	NULL,
};

static const struct v4l2_ctrl_config vivid_ctrl_dv_timings_signal_mode = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_DV_TIMINGS_SIGNAL_MODE,
	.name = "DV Timings Signal Mode",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = 5,
	.qmenu = vivid_ctrl_dv_timings_signal_mode_strings,
};

static const struct v4l2_ctrl_config vivid_ctrl_dv_timings_aspect_ratio = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_DV_TIMINGS_ASPECT_RATIO,
	.name = "DV Timings Aspect Ratio",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = 3,
	.qmenu = tpg_aspect_strings,
};

static const struct v4l2_ctrl_config vivid_ctrl_max_edid_blocks = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_MAX_EDID_BLOCKS,
	.name = "Maximum EDID Blocks",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 1,
	.max = 256,
	.def = 2,
	.step = 1,
};

static const char * const vivid_ctrl_colorspace_strings[] = {
	"SMPTE 170M",
	"Rec. 709",
	"sRGB",
	"opRGB",
	"BT.2020",
	"DCI-P3",
	"SMPTE 240M",
	"470 System M",
	"470 System BG",
	NULL,
};

static const struct v4l2_ctrl_config vivid_ctrl_colorspace = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_COLORSPACE,
	.name = "Colorspace",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = ARRAY_SIZE(vivid_ctrl_colorspace_strings) - 2,
	.def = 2,
	.qmenu = vivid_ctrl_colorspace_strings,
};

static const char * const vivid_ctrl_xfer_func_strings[] = {
	"Default",
	"Rec. 709",
	"sRGB",
	"opRGB",
	"SMPTE 240M",
	"None",
	"DCI-P3",
	"SMPTE 2084",
	NULL,
};

static const struct v4l2_ctrl_config vivid_ctrl_xfer_func = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_XFER_FUNC,
	.name = "Transfer Function",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = ARRAY_SIZE(vivid_ctrl_xfer_func_strings) - 2,
	.qmenu = vivid_ctrl_xfer_func_strings,
};

static const char * const vivid_ctrl_ycbcr_enc_strings[] = {
	"Default",
	"ITU-R 601",
	"Rec. 709",
	"xvYCC 601",
	"xvYCC 709",
	"",
	"BT.2020",
	"BT.2020 Constant Luminance",
	"SMPTE 240M",
	NULL,
};

static const struct v4l2_ctrl_config vivid_ctrl_ycbcr_enc = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_YCBCR_ENC,
	.name = "Y'CbCr Encoding",
	.type = V4L2_CTRL_TYPE_MENU,
	.menu_skip_mask = 1 << 5,
	.max = ARRAY_SIZE(vivid_ctrl_ycbcr_enc_strings) - 2,
	.qmenu = vivid_ctrl_ycbcr_enc_strings,
};

static const char * const vivid_ctrl_hsv_enc_strings[] = {
	"Hue 0-179",
	"Hue 0-256",
	NULL,
};

static const struct v4l2_ctrl_config vivid_ctrl_hsv_enc = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_HSV_ENC,
	.name = "HSV Encoding",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = ARRAY_SIZE(vivid_ctrl_hsv_enc_strings) - 2,
	.qmenu = vivid_ctrl_hsv_enc_strings,
};

static const char * const vivid_ctrl_quantization_strings[] = {
	"Default",
	"Full Range",
	"Limited Range",
	NULL,
};

static const struct v4l2_ctrl_config vivid_ctrl_quantization = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_QUANTIZATION,
	.name = "Quantization",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = ARRAY_SIZE(vivid_ctrl_quantization_strings) - 2,
	.qmenu = vivid_ctrl_quantization_strings,
};

static const struct v4l2_ctrl_config vivid_ctrl_alpha_mode = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_ALPHA_MODE,
	.name = "Apply Alpha To Red Only",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_limited_rgb_range = {
	.ops = &vivid_vid_cap_ctrl_ops,
	.id = VIVID_CID_LIMITED_RGB_RANGE,
	.name = "Limited RGB Range (16-235)",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};


/* Video Loop Control */

static int vivid_loop_cap_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vivid_dev *dev = container_of(ctrl->handler, struct vivid_dev, ctrl_hdl_loop_cap);

	switch (ctrl->id) {
	case VIVID_CID_LOOP_VIDEO:
		dev->loop_video = ctrl->val;
		vivid_update_quality(dev);
		vivid_send_source_change(dev, SVID);
		vivid_send_source_change(dev, HDMI);
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vivid_loop_cap_ctrl_ops = {
	.s_ctrl = vivid_loop_cap_s_ctrl,
};

static const struct v4l2_ctrl_config vivid_ctrl_loop_video = {
	.ops = &vivid_loop_cap_ctrl_ops,
	.id = VIVID_CID_LOOP_VIDEO,
	.name = "Loop Video",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};


/* VBI Capture Control */

static int vivid_vbi_cap_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vivid_dev *dev = container_of(ctrl->handler, struct vivid_dev, ctrl_hdl_vbi_cap);

	switch (ctrl->id) {
	case VIVID_CID_VBI_CAP_INTERLACED:
		dev->vbi_cap_interlaced = ctrl->val;
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vivid_vbi_cap_ctrl_ops = {
	.s_ctrl = vivid_vbi_cap_s_ctrl,
};

static const struct v4l2_ctrl_config vivid_ctrl_vbi_cap_interlaced = {
	.ops = &vivid_vbi_cap_ctrl_ops,
	.id = VIVID_CID_VBI_CAP_INTERLACED,
	.name = "Interlaced VBI Format",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};


/* Video Output Controls */

static int vivid_vid_out_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vivid_dev *dev = container_of(ctrl->handler, struct vivid_dev, ctrl_hdl_vid_out);
	struct v4l2_bt_timings *bt = &dev->dv_timings_out.bt;

	switch (ctrl->id) {
	case VIVID_CID_HAS_CROP_OUT:
		dev->has_crop_out = ctrl->val;
		vivid_update_format_out(dev);
		break;
	case VIVID_CID_HAS_COMPOSE_OUT:
		dev->has_compose_out = ctrl->val;
		vivid_update_format_out(dev);
		break;
	case VIVID_CID_HAS_SCALER_OUT:
		dev->has_scaler_out = ctrl->val;
		vivid_update_format_out(dev);
		break;
	case V4L2_CID_DV_TX_MODE:
		dev->dvi_d_out = ctrl->val == V4L2_DV_TX_MODE_DVI_D;
		if (!vivid_is_hdmi_out(dev))
			break;
		if (!dev->dvi_d_out && (bt->flags & V4L2_DV_FL_IS_CE_VIDEO)) {
			if (bt->width == 720 && bt->height <= 576)
				dev->colorspace_out = V4L2_COLORSPACE_SMPTE170M;
			else
				dev->colorspace_out = V4L2_COLORSPACE_REC709;
			dev->quantization_out = V4L2_QUANTIZATION_DEFAULT;
		} else {
			dev->colorspace_out = V4L2_COLORSPACE_SRGB;
			dev->quantization_out = dev->dvi_d_out ?
					V4L2_QUANTIZATION_LIM_RANGE :
					V4L2_QUANTIZATION_DEFAULT;
		}
		if (dev->loop_video)
			vivid_send_source_change(dev, HDMI);
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vivid_vid_out_ctrl_ops = {
	.s_ctrl = vivid_vid_out_s_ctrl,
};

static const struct v4l2_ctrl_config vivid_ctrl_has_crop_out = {
	.ops = &vivid_vid_out_ctrl_ops,
	.id = VIVID_CID_HAS_CROP_OUT,
	.name = "Enable Output Cropping",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.def = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_has_compose_out = {
	.ops = &vivid_vid_out_ctrl_ops,
	.id = VIVID_CID_HAS_COMPOSE_OUT,
	.name = "Enable Output Composing",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.def = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_has_scaler_out = {
	.ops = &vivid_vid_out_ctrl_ops,
	.id = VIVID_CID_HAS_SCALER_OUT,
	.name = "Enable Output Scaler",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.def = 1,
	.step = 1,
};


/* Streaming Controls */

static int vivid_streaming_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vivid_dev *dev = container_of(ctrl->handler, struct vivid_dev, ctrl_hdl_streaming);
	u64 rem;

	switch (ctrl->id) {
	case VIVID_CID_DQBUF_ERROR:
		dev->dqbuf_error = true;
		break;
	case VIVID_CID_PERC_DROPPED:
		dev->perc_dropped_buffers = ctrl->val;
		break;
	case VIVID_CID_QUEUE_SETUP_ERROR:
		dev->queue_setup_error = true;
		break;
	case VIVID_CID_BUF_PREPARE_ERROR:
		dev->buf_prepare_error = true;
		break;
	case VIVID_CID_START_STR_ERROR:
		dev->start_streaming_error = true;
		break;
	case VIVID_CID_REQ_VALIDATE_ERROR:
		dev->req_validate_error = true;
		break;
	case VIVID_CID_QUEUE_ERROR:
		if (vb2_start_streaming_called(&dev->vb_vid_cap_q))
			vb2_queue_error(&dev->vb_vid_cap_q);
		if (vb2_start_streaming_called(&dev->vb_vbi_cap_q))
			vb2_queue_error(&dev->vb_vbi_cap_q);
		if (vb2_start_streaming_called(&dev->vb_vid_out_q))
			vb2_queue_error(&dev->vb_vid_out_q);
		if (vb2_start_streaming_called(&dev->vb_vbi_out_q))
			vb2_queue_error(&dev->vb_vbi_out_q);
		if (vb2_start_streaming_called(&dev->vb_sdr_cap_q))
			vb2_queue_error(&dev->vb_sdr_cap_q);
		break;
	case VIVID_CID_SEQ_WRAP:
		dev->seq_wrap = ctrl->val;
		break;
	case VIVID_CID_TIME_WRAP:
		dev->time_wrap = ctrl->val;
		if (ctrl->val == 0) {
			dev->time_wrap_offset = 0;
			break;
		}
		/*
		 * We want to set the time 16 seconds before the 32 bit tv_sec
		 * value of struct timeval would wrap around. So first we
		 * calculate ktime_get_ns() % ((1 << 32) * NSEC_PER_SEC), and
		 * then we set the offset to ((1 << 32) - 16) * NSEC_PER_SEC).
		 */
		div64_u64_rem(ktime_get_ns(),
			0x100000000ULL * NSEC_PER_SEC, &rem);
		dev->time_wrap_offset =
			(0x100000000ULL - 16) * NSEC_PER_SEC - rem;
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vivid_streaming_ctrl_ops = {
	.s_ctrl = vivid_streaming_s_ctrl,
};

static const struct v4l2_ctrl_config vivid_ctrl_dqbuf_error = {
	.ops = &vivid_streaming_ctrl_ops,
	.id = VIVID_CID_DQBUF_ERROR,
	.name = "Inject V4L2_BUF_FLAG_ERROR",
	.type = V4L2_CTRL_TYPE_BUTTON,
};

static const struct v4l2_ctrl_config vivid_ctrl_perc_dropped = {
	.ops = &vivid_streaming_ctrl_ops,
	.id = VIVID_CID_PERC_DROPPED,
	.name = "Percentage of Dropped Buffers",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 100,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_queue_setup_error = {
	.ops = &vivid_streaming_ctrl_ops,
	.id = VIVID_CID_QUEUE_SETUP_ERROR,
	.name = "Inject VIDIOC_REQBUFS Error",
	.type = V4L2_CTRL_TYPE_BUTTON,
};

static const struct v4l2_ctrl_config vivid_ctrl_buf_prepare_error = {
	.ops = &vivid_streaming_ctrl_ops,
	.id = VIVID_CID_BUF_PREPARE_ERROR,
	.name = "Inject VIDIOC_QBUF Error",
	.type = V4L2_CTRL_TYPE_BUTTON,
};

static const struct v4l2_ctrl_config vivid_ctrl_start_streaming_error = {
	.ops = &vivid_streaming_ctrl_ops,
	.id = VIVID_CID_START_STR_ERROR,
	.name = "Inject VIDIOC_STREAMON Error",
	.type = V4L2_CTRL_TYPE_BUTTON,
};

static const struct v4l2_ctrl_config vivid_ctrl_queue_error = {
	.ops = &vivid_streaming_ctrl_ops,
	.id = VIVID_CID_QUEUE_ERROR,
	.name = "Inject Fatal Streaming Error",
	.type = V4L2_CTRL_TYPE_BUTTON,
};

#ifdef CONFIG_MEDIA_CONTROLLER
static const struct v4l2_ctrl_config vivid_ctrl_req_validate_error = {
	.ops = &vivid_streaming_ctrl_ops,
	.id = VIVID_CID_REQ_VALIDATE_ERROR,
	.name = "Inject req_validate() Error",
	.type = V4L2_CTRL_TYPE_BUTTON,
};
#endif

static const struct v4l2_ctrl_config vivid_ctrl_seq_wrap = {
	.ops = &vivid_streaming_ctrl_ops,
	.id = VIVID_CID_SEQ_WRAP,
	.name = "Wrap Sequence Number",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_time_wrap = {
	.ops = &vivid_streaming_ctrl_ops,
	.id = VIVID_CID_TIME_WRAP,
	.name = "Wrap Timestamp",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};


/* SDTV Capture Controls */

static int vivid_sdtv_cap_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vivid_dev *dev = container_of(ctrl->handler, struct vivid_dev, ctrl_hdl_sdtv_cap);

	switch (ctrl->id) {
	case VIVID_CID_STD_SIGNAL_MODE:
		dev->std_signal_mode = dev->ctrl_std_signal_mode->val;
		if (dev->std_signal_mode == SELECTED_STD)
			dev->query_std = vivid_standard[dev->ctrl_standard->val];
		v4l2_ctrl_activate(dev->ctrl_standard, dev->std_signal_mode == SELECTED_STD);
		vivid_update_quality(dev);
		vivid_send_source_change(dev, TV);
		vivid_send_source_change(dev, SVID);
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vivid_sdtv_cap_ctrl_ops = {
	.s_ctrl = vivid_sdtv_cap_s_ctrl,
};

static const char * const vivid_ctrl_std_signal_mode_strings[] = {
	"Current Standard",
	"No Signal",
	"No Lock",
	"",
	"Selected Standard",
	"Cycle Through All Standards",
	NULL,
};

static const struct v4l2_ctrl_config vivid_ctrl_std_signal_mode = {
	.ops = &vivid_sdtv_cap_ctrl_ops,
	.id = VIVID_CID_STD_SIGNAL_MODE,
	.name = "Standard Signal Mode",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = ARRAY_SIZE(vivid_ctrl_std_signal_mode_strings) - 2,
	.menu_skip_mask = 1 << 3,
	.qmenu = vivid_ctrl_std_signal_mode_strings,
};

static const struct v4l2_ctrl_config vivid_ctrl_standard = {
	.ops = &vivid_sdtv_cap_ctrl_ops,
	.id = VIVID_CID_STANDARD,
	.name = "Standard",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = 14,
	.qmenu = vivid_ctrl_standard_strings,
};



/* Radio Receiver Controls */

static int vivid_radio_rx_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vivid_dev *dev = container_of(ctrl->handler, struct vivid_dev, ctrl_hdl_radio_rx);

	switch (ctrl->id) {
	case VIVID_CID_RADIO_SEEK_MODE:
		dev->radio_rx_hw_seek_mode = ctrl->val;
		break;
	case VIVID_CID_RADIO_SEEK_PROG_LIM:
		dev->radio_rx_hw_seek_prog_lim = ctrl->val;
		break;
	case VIVID_CID_RADIO_RX_RDS_RBDS:
		dev->rds_gen.use_rbds = ctrl->val;
		break;
	case VIVID_CID_RADIO_RX_RDS_BLOCKIO:
		dev->radio_rx_rds_controls = ctrl->val;
		dev->radio_rx_caps &= ~V4L2_CAP_READWRITE;
		dev->radio_rx_rds_use_alternates = false;
		if (!dev->radio_rx_rds_controls) {
			dev->radio_rx_caps |= V4L2_CAP_READWRITE;
			__v4l2_ctrl_s_ctrl(dev->radio_rx_rds_pty, 0);
			__v4l2_ctrl_s_ctrl(dev->radio_rx_rds_ta, 0);
			__v4l2_ctrl_s_ctrl(dev->radio_rx_rds_tp, 0);
			__v4l2_ctrl_s_ctrl(dev->radio_rx_rds_ms, 0);
			__v4l2_ctrl_s_ctrl_string(dev->radio_rx_rds_psname, "");
			__v4l2_ctrl_s_ctrl_string(dev->radio_rx_rds_radiotext, "");
		}
		v4l2_ctrl_activate(dev->radio_rx_rds_pty, dev->radio_rx_rds_controls);
		v4l2_ctrl_activate(dev->radio_rx_rds_psname, dev->radio_rx_rds_controls);
		v4l2_ctrl_activate(dev->radio_rx_rds_radiotext, dev->radio_rx_rds_controls);
		v4l2_ctrl_activate(dev->radio_rx_rds_ta, dev->radio_rx_rds_controls);
		v4l2_ctrl_activate(dev->radio_rx_rds_tp, dev->radio_rx_rds_controls);
		v4l2_ctrl_activate(dev->radio_rx_rds_ms, dev->radio_rx_rds_controls);
		dev->radio_rx_dev.device_caps = dev->radio_rx_caps;
		break;
	case V4L2_CID_RDS_RECEPTION:
		dev->radio_rx_rds_enabled = ctrl->val;
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vivid_radio_rx_ctrl_ops = {
	.s_ctrl = vivid_radio_rx_s_ctrl,
};

static const char * const vivid_ctrl_radio_rds_mode_strings[] = {
	"Block I/O",
	"Controls",
	NULL,
};

static const struct v4l2_ctrl_config vivid_ctrl_radio_rx_rds_blockio = {
	.ops = &vivid_radio_rx_ctrl_ops,
	.id = VIVID_CID_RADIO_RX_RDS_BLOCKIO,
	.name = "RDS Rx I/O Mode",
	.type = V4L2_CTRL_TYPE_MENU,
	.qmenu = vivid_ctrl_radio_rds_mode_strings,
	.max = 1,
};

static const struct v4l2_ctrl_config vivid_ctrl_radio_rx_rds_rbds = {
	.ops = &vivid_radio_rx_ctrl_ops,
	.id = VIVID_CID_RADIO_RX_RDS_RBDS,
	.name = "Generate RBDS Instead of RDS",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};

static const char * const vivid_ctrl_radio_hw_seek_mode_strings[] = {
	"Bounded",
	"Wrap Around",
	"Both",
	NULL,
};

static const struct v4l2_ctrl_config vivid_ctrl_radio_hw_seek_mode = {
	.ops = &vivid_radio_rx_ctrl_ops,
	.id = VIVID_CID_RADIO_SEEK_MODE,
	.name = "Radio HW Seek Mode",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = 2,
	.qmenu = vivid_ctrl_radio_hw_seek_mode_strings,
};

static const struct v4l2_ctrl_config vivid_ctrl_radio_hw_seek_prog_lim = {
	.ops = &vivid_radio_rx_ctrl_ops,
	.id = VIVID_CID_RADIO_SEEK_PROG_LIM,
	.name = "Radio Programmable HW Seek",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};


/* Radio Transmitter Controls */

static int vivid_radio_tx_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vivid_dev *dev = container_of(ctrl->handler, struct vivid_dev, ctrl_hdl_radio_tx);

	switch (ctrl->id) {
	case VIVID_CID_RADIO_TX_RDS_BLOCKIO:
		dev->radio_tx_rds_controls = ctrl->val;
		dev->radio_tx_caps &= ~V4L2_CAP_READWRITE;
		if (!dev->radio_tx_rds_controls)
			dev->radio_tx_caps |= V4L2_CAP_READWRITE;
		dev->radio_tx_dev.device_caps = dev->radio_tx_caps;
		break;
	case V4L2_CID_RDS_TX_PTY:
		if (dev->radio_rx_rds_controls)
			v4l2_ctrl_s_ctrl(dev->radio_rx_rds_pty, ctrl->val);
		break;
	case V4L2_CID_RDS_TX_PS_NAME:
		if (dev->radio_rx_rds_controls)
			v4l2_ctrl_s_ctrl_string(dev->radio_rx_rds_psname, ctrl->p_new.p_char);
		break;
	case V4L2_CID_RDS_TX_RADIO_TEXT:
		if (dev->radio_rx_rds_controls)
			v4l2_ctrl_s_ctrl_string(dev->radio_rx_rds_radiotext, ctrl->p_new.p_char);
		break;
	case V4L2_CID_RDS_TX_TRAFFIC_ANNOUNCEMENT:
		if (dev->radio_rx_rds_controls)
			v4l2_ctrl_s_ctrl(dev->radio_rx_rds_ta, ctrl->val);
		break;
	case V4L2_CID_RDS_TX_TRAFFIC_PROGRAM:
		if (dev->radio_rx_rds_controls)
			v4l2_ctrl_s_ctrl(dev->radio_rx_rds_tp, ctrl->val);
		break;
	case V4L2_CID_RDS_TX_MUSIC_SPEECH:
		if (dev->radio_rx_rds_controls)
			v4l2_ctrl_s_ctrl(dev->radio_rx_rds_ms, ctrl->val);
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vivid_radio_tx_ctrl_ops = {
	.s_ctrl = vivid_radio_tx_s_ctrl,
};

static const struct v4l2_ctrl_config vivid_ctrl_radio_tx_rds_blockio = {
	.ops = &vivid_radio_tx_ctrl_ops,
	.id = VIVID_CID_RADIO_TX_RDS_BLOCKIO,
	.name = "RDS Tx I/O Mode",
	.type = V4L2_CTRL_TYPE_MENU,
	.qmenu = vivid_ctrl_radio_rds_mode_strings,
	.max = 1,
	.def = 1,
};


/* SDR Capture Controls */

static int vivid_sdr_cap_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vivid_dev *dev = container_of(ctrl->handler, struct vivid_dev, ctrl_hdl_sdr_cap);

	switch (ctrl->id) {
	case VIVID_CID_SDR_CAP_FM_DEVIATION:
		dev->sdr_fm_deviation = ctrl->val;
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vivid_sdr_cap_ctrl_ops = {
	.s_ctrl = vivid_sdr_cap_s_ctrl,
};

static const struct v4l2_ctrl_config vivid_ctrl_sdr_cap_fm_deviation = {
	.ops = &vivid_sdr_cap_ctrl_ops,
	.id = VIVID_CID_SDR_CAP_FM_DEVIATION,
	.name = "FM Deviation",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min =    100,
	.max = 200000,
	.def =  75000,
	.step =     1,
};


static const struct v4l2_ctrl_config vivid_ctrl_class = {
	.ops = &vivid_user_gen_ctrl_ops,
	.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_WRITE_ONLY,
	.id = VIVID_CID_VIVID_CLASS,
	.name = "Vivid Controls",
	.type = V4L2_CTRL_TYPE_CTRL_CLASS,
};

int vivid_create_controls(struct vivid_dev *dev, bool show_ccs_cap,
		bool show_ccs_out, bool no_error_inj,
		bool has_sdtv, bool has_hdmi)
{
	struct v4l2_ctrl_handler *hdl_user_gen = &dev->ctrl_hdl_user_gen;
	struct v4l2_ctrl_handler *hdl_user_vid = &dev->ctrl_hdl_user_vid;
	struct v4l2_ctrl_handler *hdl_user_aud = &dev->ctrl_hdl_user_aud;
	struct v4l2_ctrl_handler *hdl_streaming = &dev->ctrl_hdl_streaming;
	struct v4l2_ctrl_handler *hdl_sdtv_cap = &dev->ctrl_hdl_sdtv_cap;
	struct v4l2_ctrl_handler *hdl_loop_cap = &dev->ctrl_hdl_loop_cap;
	struct v4l2_ctrl_handler *hdl_fb = &dev->ctrl_hdl_fb;
	struct v4l2_ctrl_handler *hdl_vid_cap = &dev->ctrl_hdl_vid_cap;
	struct v4l2_ctrl_handler *hdl_vid_out = &dev->ctrl_hdl_vid_out;
	struct v4l2_ctrl_handler *hdl_vbi_cap = &dev->ctrl_hdl_vbi_cap;
	struct v4l2_ctrl_handler *hdl_vbi_out = &dev->ctrl_hdl_vbi_out;
	struct v4l2_ctrl_handler *hdl_radio_rx = &dev->ctrl_hdl_radio_rx;
	struct v4l2_ctrl_handler *hdl_radio_tx = &dev->ctrl_hdl_radio_tx;
	struct v4l2_ctrl_handler *hdl_sdr_cap = &dev->ctrl_hdl_sdr_cap;
	struct v4l2_ctrl_config vivid_ctrl_dv_timings = {
		.ops = &vivid_vid_cap_ctrl_ops,
		.id = VIVID_CID_DV_TIMINGS,
		.name = "DV Timings",
		.type = V4L2_CTRL_TYPE_MENU,
	};
	int i;

	v4l2_ctrl_handler_init(hdl_user_gen, 10);
	v4l2_ctrl_new_custom(hdl_user_gen, &vivid_ctrl_class, NULL);
	v4l2_ctrl_handler_init(hdl_user_vid, 9);
	v4l2_ctrl_new_custom(hdl_user_vid, &vivid_ctrl_class, NULL);
	v4l2_ctrl_handler_init(hdl_user_aud, 2);
	v4l2_ctrl_new_custom(hdl_user_aud, &vivid_ctrl_class, NULL);
	v4l2_ctrl_handler_init(hdl_streaming, 8);
	v4l2_ctrl_new_custom(hdl_streaming, &vivid_ctrl_class, NULL);
	v4l2_ctrl_handler_init(hdl_sdtv_cap, 2);
	v4l2_ctrl_new_custom(hdl_sdtv_cap, &vivid_ctrl_class, NULL);
	v4l2_ctrl_handler_init(hdl_loop_cap, 1);
	v4l2_ctrl_new_custom(hdl_loop_cap, &vivid_ctrl_class, NULL);
	v4l2_ctrl_handler_init(hdl_fb, 1);
	v4l2_ctrl_new_custom(hdl_fb, &vivid_ctrl_class, NULL);
	v4l2_ctrl_handler_init(hdl_vid_cap, 55);
	v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_class, NULL);
	v4l2_ctrl_handler_init(hdl_vid_out, 26);
	if (!no_error_inj || dev->has_fb)
		v4l2_ctrl_new_custom(hdl_vid_out, &vivid_ctrl_class, NULL);
	v4l2_ctrl_handler_init(hdl_vbi_cap, 21);
	v4l2_ctrl_new_custom(hdl_vbi_cap, &vivid_ctrl_class, NULL);
	v4l2_ctrl_handler_init(hdl_vbi_out, 19);
	if (!no_error_inj)
		v4l2_ctrl_new_custom(hdl_vbi_out, &vivid_ctrl_class, NULL);
	v4l2_ctrl_handler_init(hdl_radio_rx, 17);
	v4l2_ctrl_new_custom(hdl_radio_rx, &vivid_ctrl_class, NULL);
	v4l2_ctrl_handler_init(hdl_radio_tx, 17);
	v4l2_ctrl_new_custom(hdl_radio_tx, &vivid_ctrl_class, NULL);
	v4l2_ctrl_handler_init(hdl_sdr_cap, 19);
	v4l2_ctrl_new_custom(hdl_sdr_cap, &vivid_ctrl_class, NULL);

	/* User Controls */
	dev->volume = v4l2_ctrl_new_std(hdl_user_aud, NULL,
		V4L2_CID_AUDIO_VOLUME, 0, 255, 1, 200);
	dev->mute = v4l2_ctrl_new_std(hdl_user_aud, NULL,
		V4L2_CID_AUDIO_MUTE, 0, 1, 1, 0);
	if (dev->has_vid_cap) {
		dev->brightness = v4l2_ctrl_new_std(hdl_user_vid, &vivid_user_vid_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
		for (i = 0; i < MAX_INPUTS; i++)
			dev->input_brightness[i] = 128;
		dev->contrast = v4l2_ctrl_new_std(hdl_user_vid, &vivid_user_vid_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 255, 1, 128);
		dev->saturation = v4l2_ctrl_new_std(hdl_user_vid, &vivid_user_vid_ctrl_ops,
			V4L2_CID_SATURATION, 0, 255, 1, 128);
		dev->hue = v4l2_ctrl_new_std(hdl_user_vid, &vivid_user_vid_ctrl_ops,
			V4L2_CID_HUE, -128, 128, 1, 0);
		v4l2_ctrl_new_std(hdl_user_vid, &vivid_user_vid_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
		v4l2_ctrl_new_std(hdl_user_vid, &vivid_user_vid_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
		dev->autogain = v4l2_ctrl_new_std(hdl_user_vid, &vivid_user_vid_ctrl_ops,
			V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
		dev->gain = v4l2_ctrl_new_std(hdl_user_vid, &vivid_user_vid_ctrl_ops,
			V4L2_CID_GAIN, 0, 255, 1, 100);
		dev->alpha = v4l2_ctrl_new_std(hdl_user_vid, &vivid_user_vid_ctrl_ops,
			V4L2_CID_ALPHA_COMPONENT, 0, 255, 1, 0);
	}
	dev->button = v4l2_ctrl_new_custom(hdl_user_gen, &vivid_ctrl_button, NULL);
	dev->int32 = v4l2_ctrl_new_custom(hdl_user_gen, &vivid_ctrl_int32, NULL);
	dev->int64 = v4l2_ctrl_new_custom(hdl_user_gen, &vivid_ctrl_int64, NULL);
	dev->boolean = v4l2_ctrl_new_custom(hdl_user_gen, &vivid_ctrl_boolean, NULL);
	dev->menu = v4l2_ctrl_new_custom(hdl_user_gen, &vivid_ctrl_menu, NULL);
	dev->string = v4l2_ctrl_new_custom(hdl_user_gen, &vivid_ctrl_string, NULL);
	dev->bitmask = v4l2_ctrl_new_custom(hdl_user_gen, &vivid_ctrl_bitmask, NULL);
	dev->int_menu = v4l2_ctrl_new_custom(hdl_user_gen, &vivid_ctrl_int_menu, NULL);
	v4l2_ctrl_new_custom(hdl_user_gen, &vivid_ctrl_u32_array, NULL);
	v4l2_ctrl_new_custom(hdl_user_gen, &vivid_ctrl_u16_matrix, NULL);
	v4l2_ctrl_new_custom(hdl_user_gen, &vivid_ctrl_u8_4d_array, NULL);

	if (dev->has_vid_cap) {
		/* Image Processing Controls */
		struct v4l2_ctrl_config vivid_ctrl_test_pattern = {
			.ops = &vivid_vid_cap_ctrl_ops,
			.id = VIVID_CID_TEST_PATTERN,
			.name = "Test Pattern",
			.type = V4L2_CTRL_TYPE_MENU,
			.max = TPG_PAT_NOISE,
			.qmenu = tpg_pattern_strings,
		};

		dev->test_pattern = v4l2_ctrl_new_custom(hdl_vid_cap,
				&vivid_ctrl_test_pattern, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_perc_fill, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_hor_movement, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_vert_movement, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_osd_mode, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_show_border, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_show_square, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_hflip, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_vflip, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_insert_sav, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_insert_eav, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_reduced_fps, NULL);
		if (show_ccs_cap) {
			dev->ctrl_has_crop_cap = v4l2_ctrl_new_custom(hdl_vid_cap,
				&vivid_ctrl_has_crop_cap, NULL);
			dev->ctrl_has_compose_cap = v4l2_ctrl_new_custom(hdl_vid_cap,
				&vivid_ctrl_has_compose_cap, NULL);
			dev->ctrl_has_scaler_cap = v4l2_ctrl_new_custom(hdl_vid_cap,
				&vivid_ctrl_has_scaler_cap, NULL);
		}

		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_tstamp_src, NULL);
		dev->colorspace = v4l2_ctrl_new_custom(hdl_vid_cap,
			&vivid_ctrl_colorspace, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_xfer_func, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_ycbcr_enc, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_hsv_enc, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_quantization, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_alpha_mode, NULL);
	}

	if (dev->has_vid_out && show_ccs_out) {
		dev->ctrl_has_crop_out = v4l2_ctrl_new_custom(hdl_vid_out,
			&vivid_ctrl_has_crop_out, NULL);
		dev->ctrl_has_compose_out = v4l2_ctrl_new_custom(hdl_vid_out,
			&vivid_ctrl_has_compose_out, NULL);
		dev->ctrl_has_scaler_out = v4l2_ctrl_new_custom(hdl_vid_out,
			&vivid_ctrl_has_scaler_out, NULL);
	}

	/*
	 * Testing this driver with v4l2-compliance will trigger the error
	 * injection controls, and after that nothing will work as expected.
	 * So we have a module option to drop these error injecting controls
	 * allowing us to run v4l2_compliance again.
	 */
	if (!no_error_inj) {
		v4l2_ctrl_new_custom(hdl_user_gen, &vivid_ctrl_disconnect, NULL);
		v4l2_ctrl_new_custom(hdl_streaming, &vivid_ctrl_dqbuf_error, NULL);
		v4l2_ctrl_new_custom(hdl_streaming, &vivid_ctrl_perc_dropped, NULL);
		v4l2_ctrl_new_custom(hdl_streaming, &vivid_ctrl_queue_setup_error, NULL);
		v4l2_ctrl_new_custom(hdl_streaming, &vivid_ctrl_buf_prepare_error, NULL);
		v4l2_ctrl_new_custom(hdl_streaming, &vivid_ctrl_start_streaming_error, NULL);
		v4l2_ctrl_new_custom(hdl_streaming, &vivid_ctrl_queue_error, NULL);
#ifdef CONFIG_MEDIA_CONTROLLER
		v4l2_ctrl_new_custom(hdl_streaming, &vivid_ctrl_req_validate_error, NULL);
#endif
		v4l2_ctrl_new_custom(hdl_streaming, &vivid_ctrl_seq_wrap, NULL);
		v4l2_ctrl_new_custom(hdl_streaming, &vivid_ctrl_time_wrap, NULL);
	}

	if (has_sdtv && (dev->has_vid_cap || dev->has_vbi_cap)) {
		if (dev->has_vid_cap)
			v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_std_aspect_ratio, NULL);
		dev->ctrl_std_signal_mode = v4l2_ctrl_new_custom(hdl_sdtv_cap,
			&vivid_ctrl_std_signal_mode, NULL);
		dev->ctrl_standard = v4l2_ctrl_new_custom(hdl_sdtv_cap,
			&vivid_ctrl_standard, NULL);
		if (dev->ctrl_std_signal_mode)
			v4l2_ctrl_cluster(2, &dev->ctrl_std_signal_mode);
		if (dev->has_raw_vbi_cap)
			v4l2_ctrl_new_custom(hdl_vbi_cap, &vivid_ctrl_vbi_cap_interlaced, NULL);
	}

	if (has_hdmi && dev->has_vid_cap) {
		dev->ctrl_dv_timings_signal_mode = v4l2_ctrl_new_custom(hdl_vid_cap,
					&vivid_ctrl_dv_timings_signal_mode, NULL);

		vivid_ctrl_dv_timings.max = dev->query_dv_timings_size - 1;
		vivid_ctrl_dv_timings.qmenu =
			(const char * const *)dev->query_dv_timings_qmenu;
		dev->ctrl_dv_timings = v4l2_ctrl_new_custom(hdl_vid_cap,
			&vivid_ctrl_dv_timings, NULL);
		if (dev->ctrl_dv_timings_signal_mode)
			v4l2_ctrl_cluster(2, &dev->ctrl_dv_timings_signal_mode);

		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_dv_timings_aspect_ratio, NULL);
		v4l2_ctrl_new_custom(hdl_vid_cap, &vivid_ctrl_max_edid_blocks, NULL);
		dev->real_rgb_range_cap = v4l2_ctrl_new_custom(hdl_vid_cap,
			&vivid_ctrl_limited_rgb_range, NULL);
		dev->rgb_range_cap = v4l2_ctrl_new_std_menu(hdl_vid_cap,
			&vivid_vid_cap_ctrl_ops,
			V4L2_CID_DV_RX_RGB_RANGE, V4L2_DV_RGB_RANGE_FULL,
			0, V4L2_DV_RGB_RANGE_AUTO);
	}
	if (has_hdmi && dev->has_vid_out) {
		/*
		 * We aren't doing anything with this at the moment, but
		 * HDMI outputs typically have this controls.
		 */
		dev->ctrl_tx_rgb_range = v4l2_ctrl_new_std_menu(hdl_vid_out, NULL,
			V4L2_CID_DV_TX_RGB_RANGE, V4L2_DV_RGB_RANGE_FULL,
			0, V4L2_DV_RGB_RANGE_AUTO);
		dev->ctrl_tx_mode = v4l2_ctrl_new_std_menu(hdl_vid_out, NULL,
			V4L2_CID_DV_TX_MODE, V4L2_DV_TX_MODE_HDMI,
			0, V4L2_DV_TX_MODE_HDMI);
	}
	if ((dev->has_vid_cap && dev->has_vid_out) ||
	    (dev->has_vbi_cap && dev->has_vbi_out))
		v4l2_ctrl_new_custom(hdl_loop_cap, &vivid_ctrl_loop_video, NULL);

	if (dev->has_fb)
		v4l2_ctrl_new_custom(hdl_fb, &vivid_ctrl_clear_fb, NULL);

	if (dev->has_radio_rx) {
		v4l2_ctrl_new_custom(hdl_radio_rx, &vivid_ctrl_radio_hw_seek_mode, NULL);
		v4l2_ctrl_new_custom(hdl_radio_rx, &vivid_ctrl_radio_hw_seek_prog_lim, NULL);
		v4l2_ctrl_new_custom(hdl_radio_rx, &vivid_ctrl_radio_rx_rds_blockio, NULL);
		v4l2_ctrl_new_custom(hdl_radio_rx, &vivid_ctrl_radio_rx_rds_rbds, NULL);
		v4l2_ctrl_new_std(hdl_radio_rx, &vivid_radio_rx_ctrl_ops,
			V4L2_CID_RDS_RECEPTION, 0, 1, 1, 1);
		dev->radio_rx_rds_pty = v4l2_ctrl_new_std(hdl_radio_rx,
			&vivid_radio_rx_ctrl_ops,
			V4L2_CID_RDS_RX_PTY, 0, 31, 1, 0);
		dev->radio_rx_rds_psname = v4l2_ctrl_new_std(hdl_radio_rx,
			&vivid_radio_rx_ctrl_ops,
			V4L2_CID_RDS_RX_PS_NAME, 0, 8, 8, 0);
		dev->radio_rx_rds_radiotext = v4l2_ctrl_new_std(hdl_radio_rx,
			&vivid_radio_rx_ctrl_ops,
			V4L2_CID_RDS_RX_RADIO_TEXT, 0, 64, 64, 0);
		dev->radio_rx_rds_ta = v4l2_ctrl_new_std(hdl_radio_rx,
			&vivid_radio_rx_ctrl_ops,
			V4L2_CID_RDS_RX_TRAFFIC_ANNOUNCEMENT, 0, 1, 1, 0);
		dev->radio_rx_rds_tp = v4l2_ctrl_new_std(hdl_radio_rx,
			&vivid_radio_rx_ctrl_ops,
			V4L2_CID_RDS_RX_TRAFFIC_PROGRAM, 0, 1, 1, 0);
		dev->radio_rx_rds_ms = v4l2_ctrl_new_std(hdl_radio_rx,
			&vivid_radio_rx_ctrl_ops,
			V4L2_CID_RDS_RX_MUSIC_SPEECH, 0, 1, 1, 1);
	}
	if (dev->has_radio_tx) {
		v4l2_ctrl_new_custom(hdl_radio_tx,
			&vivid_ctrl_radio_tx_rds_blockio, NULL);
		dev->radio_tx_rds_pi = v4l2_ctrl_new_std(hdl_radio_tx,
			&vivid_radio_tx_ctrl_ops,
			V4L2_CID_RDS_TX_PI, 0, 0xffff, 1, 0x8088);
		dev->radio_tx_rds_pty = v4l2_ctrl_new_std(hdl_radio_tx,
			&vivid_radio_tx_ctrl_ops,
			V4L2_CID_RDS_TX_PTY, 0, 31, 1, 3);
		dev->radio_tx_rds_psname = v4l2_ctrl_new_std(hdl_radio_tx,
			&vivid_radio_tx_ctrl_ops,
			V4L2_CID_RDS_TX_PS_NAME, 0, 8, 8, 0);
		if (dev->radio_tx_rds_psname)
			v4l2_ctrl_s_ctrl_string(dev->radio_tx_rds_psname, "VIVID-TX");
		dev->radio_tx_rds_radiotext = v4l2_ctrl_new_std(hdl_radio_tx,
			&vivid_radio_tx_ctrl_ops,
			V4L2_CID_RDS_TX_RADIO_TEXT, 0, 64 * 2, 64, 0);
		if (dev->radio_tx_rds_radiotext)
			v4l2_ctrl_s_ctrl_string(dev->radio_tx_rds_radiotext,
			       "This is a VIVID default Radio Text template text, change at will");
		dev->radio_tx_rds_mono_stereo = v4l2_ctrl_new_std(hdl_radio_tx,
			&vivid_radio_tx_ctrl_ops,
			V4L2_CID_RDS_TX_MONO_STEREO, 0, 1, 1, 1);
		dev->radio_tx_rds_art_head = v4l2_ctrl_new_std(hdl_radio_tx,
			&vivid_radio_tx_ctrl_ops,
			V4L2_CID_RDS_TX_ARTIFICIAL_HEAD, 0, 1, 1, 0);
		dev->radio_tx_rds_compressed = v4l2_ctrl_new_std(hdl_radio_tx,
			&vivid_radio_tx_ctrl_ops,
			V4L2_CID_RDS_TX_COMPRESSED, 0, 1, 1, 0);
		dev->radio_tx_rds_dyn_pty = v4l2_ctrl_new_std(hdl_radio_tx,
			&vivid_radio_tx_ctrl_ops,
			V4L2_CID_RDS_TX_DYNAMIC_PTY, 0, 1, 1, 0);
		dev->radio_tx_rds_ta = v4l2_ctrl_new_std(hdl_radio_tx,
			&vivid_radio_tx_ctrl_ops,
			V4L2_CID_RDS_TX_TRAFFIC_ANNOUNCEMENT, 0, 1, 1, 0);
		dev->radio_tx_rds_tp = v4l2_ctrl_new_std(hdl_radio_tx,
			&vivid_radio_tx_ctrl_ops,
			V4L2_CID_RDS_TX_TRAFFIC_PROGRAM, 0, 1, 1, 1);
		dev->radio_tx_rds_ms = v4l2_ctrl_new_std(hdl_radio_tx,
			&vivid_radio_tx_ctrl_ops,
			V4L2_CID_RDS_TX_MUSIC_SPEECH, 0, 1, 1, 1);
	}
	if (dev->has_sdr_cap) {
		v4l2_ctrl_new_custom(hdl_sdr_cap,
			&vivid_ctrl_sdr_cap_fm_deviation, NULL);
	}
	if (hdl_user_gen->error)
		return hdl_user_gen->error;
	if (hdl_user_vid->error)
		return hdl_user_vid->error;
	if (hdl_user_aud->error)
		return hdl_user_aud->error;
	if (hdl_streaming->error)
		return hdl_streaming->error;
	if (hdl_sdr_cap->error)
		return hdl_sdr_cap->error;
	if (hdl_loop_cap->error)
		return hdl_loop_cap->error;

	if (dev->autogain)
		v4l2_ctrl_auto_cluster(2, &dev->autogain, 0, true);

	if (dev->has_vid_cap) {
		v4l2_ctrl_add_handler(hdl_vid_cap, hdl_user_gen, NULL, false);
		v4l2_ctrl_add_handler(hdl_vid_cap, hdl_user_vid, NULL, false);
		v4l2_ctrl_add_handler(hdl_vid_cap, hdl_user_aud, NULL, false);
		v4l2_ctrl_add_handler(hdl_vid_cap, hdl_streaming, NULL, false);
		v4l2_ctrl_add_handler(hdl_vid_cap, hdl_sdtv_cap, NULL, false);
		v4l2_ctrl_add_handler(hdl_vid_cap, hdl_loop_cap, NULL, false);
		v4l2_ctrl_add_handler(hdl_vid_cap, hdl_fb, NULL, false);
		if (hdl_vid_cap->error)
			return hdl_vid_cap->error;
		dev->vid_cap_dev.ctrl_handler = hdl_vid_cap;
	}
	if (dev->has_vid_out) {
		v4l2_ctrl_add_handler(hdl_vid_out, hdl_user_gen, NULL, false);
		v4l2_ctrl_add_handler(hdl_vid_out, hdl_user_aud, NULL, false);
		v4l2_ctrl_add_handler(hdl_vid_out, hdl_streaming, NULL, false);
		v4l2_ctrl_add_handler(hdl_vid_out, hdl_fb, NULL, false);
		if (hdl_vid_out->error)
			return hdl_vid_out->error;
		dev->vid_out_dev.ctrl_handler = hdl_vid_out;
	}
	if (dev->has_vbi_cap) {
		v4l2_ctrl_add_handler(hdl_vbi_cap, hdl_user_gen, NULL, false);
		v4l2_ctrl_add_handler(hdl_vbi_cap, hdl_streaming, NULL, false);
		v4l2_ctrl_add_handler(hdl_vbi_cap, hdl_sdtv_cap, NULL, false);
		v4l2_ctrl_add_handler(hdl_vbi_cap, hdl_loop_cap, NULL, false);
		if (hdl_vbi_cap->error)
			return hdl_vbi_cap->error;
		dev->vbi_cap_dev.ctrl_handler = hdl_vbi_cap;
	}
	if (dev->has_vbi_out) {
		v4l2_ctrl_add_handler(hdl_vbi_out, hdl_user_gen, NULL, false);
		v4l2_ctrl_add_handler(hdl_vbi_out, hdl_streaming, NULL, false);
		if (hdl_vbi_out->error)
			return hdl_vbi_out->error;
		dev->vbi_out_dev.ctrl_handler = hdl_vbi_out;
	}
	if (dev->has_radio_rx) {
		v4l2_ctrl_add_handler(hdl_radio_rx, hdl_user_gen, NULL, false);
		v4l2_ctrl_add_handler(hdl_radio_rx, hdl_user_aud, NULL, false);
		if (hdl_radio_rx->error)
			return hdl_radio_rx->error;
		dev->radio_rx_dev.ctrl_handler = hdl_radio_rx;
	}
	if (dev->has_radio_tx) {
		v4l2_ctrl_add_handler(hdl_radio_tx, hdl_user_gen, NULL, false);
		v4l2_ctrl_add_handler(hdl_radio_tx, hdl_user_aud, NULL, false);
		if (hdl_radio_tx->error)
			return hdl_radio_tx->error;
		dev->radio_tx_dev.ctrl_handler = hdl_radio_tx;
	}
	if (dev->has_sdr_cap) {
		v4l2_ctrl_add_handler(hdl_sdr_cap, hdl_user_gen, NULL, false);
		v4l2_ctrl_add_handler(hdl_sdr_cap, hdl_streaming, NULL, false);
		if (hdl_sdr_cap->error)
			return hdl_sdr_cap->error;
		dev->sdr_cap_dev.ctrl_handler = hdl_sdr_cap;
	}
	return 0;
}

void vivid_free_controls(struct vivid_dev *dev)
{
	v4l2_ctrl_handler_free(&dev->ctrl_hdl_vid_cap);
	v4l2_ctrl_handler_free(&dev->ctrl_hdl_vid_out);
	v4l2_ctrl_handler_free(&dev->ctrl_hdl_vbi_cap);
	v4l2_ctrl_handler_free(&dev->ctrl_hdl_vbi_out);
	v4l2_ctrl_handler_free(&dev->ctrl_hdl_radio_rx);
	v4l2_ctrl_handler_free(&dev->ctrl_hdl_radio_tx);
	v4l2_ctrl_handler_free(&dev->ctrl_hdl_sdr_cap);
	v4l2_ctrl_handler_free(&dev->ctrl_hdl_user_gen);
	v4l2_ctrl_handler_free(&dev->ctrl_hdl_user_vid);
	v4l2_ctrl_handler_free(&dev->ctrl_hdl_user_aud);
	v4l2_ctrl_handler_free(&dev->ctrl_hdl_streaming);
	v4l2_ctrl_handler_free(&dev->ctrl_hdl_sdtv_cap);
	v4l2_ctrl_handler_free(&dev->ctrl_hdl_loop_cap);
	v4l2_ctrl_handler_free(&dev->ctrl_hdl_fb);
}

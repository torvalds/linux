// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Test Pattern Generator
 *
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * Contacts: Hyun Kwon <hyun.kwon@xilinx.com>
 *           Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/xilinx-v4l2-controls.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"
#include "xilinx-vtc.h"

#define XTPG_CTRL_STATUS_SLAVE_ERROR		(1 << 16)
#define XTPG_CTRL_IRQ_SLAVE_ERROR		(1 << 16)

#define XTPG_PATTERN_CONTROL			0x0100
#define XTPG_PATTERN_MASK			(0xf << 0)
#define XTPG_PATTERN_CONTROL_CROSS_HAIRS	(1 << 4)
#define XTPG_PATTERN_CONTROL_MOVING_BOX		(1 << 5)
#define XTPG_PATTERN_CONTROL_COLOR_MASK_SHIFT	6
#define XTPG_PATTERN_CONTROL_COLOR_MASK_MASK	(0xf << 6)
#define XTPG_PATTERN_CONTROL_STUCK_PIXEL	(1 << 9)
#define XTPG_PATTERN_CONTROL_NOISE		(1 << 10)
#define XTPG_PATTERN_CONTROL_MOTION		(1 << 12)
#define XTPG_MOTION_SPEED			0x0104
#define XTPG_CROSS_HAIRS			0x0108
#define XTPG_CROSS_HAIRS_ROW_SHIFT		0
#define XTPG_CROSS_HAIRS_ROW_MASK		(0xfff << 0)
#define XTPG_CROSS_HAIRS_COLUMN_SHIFT		16
#define XTPG_CROSS_HAIRS_COLUMN_MASK		(0xfff << 16)
#define XTPG_ZPLATE_HOR_CONTROL			0x010c
#define XTPG_ZPLATE_VER_CONTROL			0x0110
#define XTPG_ZPLATE_START_SHIFT			0
#define XTPG_ZPLATE_START_MASK			(0xffff << 0)
#define XTPG_ZPLATE_SPEED_SHIFT			16
#define XTPG_ZPLATE_SPEED_MASK			(0xffff << 16)
#define XTPG_BOX_SIZE				0x0114
#define XTPG_BOX_COLOR				0x0118
#define XTPG_STUCK_PIXEL_THRESH			0x011c
#define XTPG_NOISE_GAIN				0x0120
#define XTPG_BAYER_PHASE			0x0124
#define XTPG_BAYER_PHASE_RGGB			0
#define XTPG_BAYER_PHASE_GRBG			1
#define XTPG_BAYER_PHASE_GBRG			2
#define XTPG_BAYER_PHASE_BGGR			3
#define XTPG_BAYER_PHASE_OFF			4

/*
 * The minimum blanking value is one clock cycle for the front porch, one clock
 * cycle for the sync pulse and one clock cycle for the back porch.
 */
#define XTPG_MIN_HBLANK			3
#define XTPG_MAX_HBLANK			(XVTC_MAX_HSIZE - XVIP_MIN_WIDTH)
#define XTPG_MIN_VBLANK			3
#define XTPG_MAX_VBLANK			(XVTC_MAX_VSIZE - XVIP_MIN_HEIGHT)

/**
 * struct xtpg_device - Xilinx Test Pattern Generator device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @npads: number of pads (1 or 2)
 * @has_input: whether an input is connected to the sink pad
 * @formats: active V4L2 media bus format for each pad
 * @default_format: default V4L2 media bus format
 * @vip_format: format information corresponding to the active format
 * @bayer: boolean flag if TPG is set to any bayer format
 * @ctrl_handler: control handler
 * @hblank: horizontal blanking control
 * @vblank: vertical blanking control
 * @pattern: test pattern control
 * @streaming: is the video stream active
 * @vtc: video timing controller
 * @vtmux_gpio: video timing mux GPIO
 */
struct xtpg_device {
	struct xvip_device xvip;

	struct media_pad pads[2];
	unsigned int npads;
	bool has_input;

	struct v4l2_mbus_framefmt formats[2];
	struct v4l2_mbus_framefmt default_format;
	const struct xvip_video_format *vip_format;
	bool bayer;

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *pattern;
	bool streaming;

	struct xvtc_device *vtc;
	struct gpio_desc *vtmux_gpio;
};

static inline struct xtpg_device *to_tpg(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xtpg_device, xvip.subdev);
}

static u32 xtpg_get_bayer_phase(unsigned int code)
{
	switch (code) {
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		return XTPG_BAYER_PHASE_RGGB;
	case MEDIA_BUS_FMT_SGRBG8_1X8:
		return XTPG_BAYER_PHASE_GRBG;
	case MEDIA_BUS_FMT_SGBRG8_1X8:
		return XTPG_BAYER_PHASE_GBRG;
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		return XTPG_BAYER_PHASE_BGGR;
	default:
		return XTPG_BAYER_PHASE_OFF;
	}
}

static void __xtpg_update_pattern_control(struct xtpg_device *xtpg,
					  bool passthrough, bool pattern)
{
	u32 pattern_mask = (1 << (xtpg->pattern->maximum + 1)) - 1;

	/*
	 * If the TPG has no sink pad or no input connected to its sink pad
	 * passthrough mode can't be enabled.
	 */
	if (xtpg->npads == 1 || !xtpg->has_input)
		passthrough = false;

	/* If passthrough mode is allowed unmask bit 0. */
	if (passthrough)
		pattern_mask &= ~1;

	/* If test pattern mode is allowed unmask all other bits. */
	if (pattern)
		pattern_mask &= 1;

	__v4l2_ctrl_modify_range(xtpg->pattern, 0, xtpg->pattern->maximum,
				 pattern_mask, pattern ? 9 : 0);
}

static void xtpg_update_pattern_control(struct xtpg_device *xtpg,
					bool passthrough, bool pattern)
{
	mutex_lock(xtpg->ctrl_handler.lock);
	__xtpg_update_pattern_control(xtpg, passthrough, pattern);
	mutex_unlock(xtpg->ctrl_handler.lock);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Video Operations
 */

static int xtpg_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xtpg_device *xtpg = to_tpg(subdev);
	unsigned int width = xtpg->formats[0].width;
	unsigned int height = xtpg->formats[0].height;
	bool passthrough;
	u32 bayer_phase;

	if (!enable) {
		xvip_stop(&xtpg->xvip);
		if (xtpg->vtc)
			xvtc_generator_stop(xtpg->vtc);

		xtpg_update_pattern_control(xtpg, true, true);
		xtpg->streaming = false;
		return 0;
	}

	xvip_set_frame_size(&xtpg->xvip, &xtpg->formats[0]);

	if (xtpg->vtc) {
		struct xvtc_config config = {
			.hblank_start = width,
			.hsync_start = width + 1,
			.vblank_start = height,
			.vsync_start = height + 1,
		};
		unsigned int htotal;
		unsigned int vtotal;

		htotal = min_t(unsigned int, XVTC_MAX_HSIZE,
			       v4l2_ctrl_g_ctrl(xtpg->hblank) + width);
		vtotal = min_t(unsigned int, XVTC_MAX_VSIZE,
			       v4l2_ctrl_g_ctrl(xtpg->vblank) + height);

		config.hsync_end = htotal - 1;
		config.hsize = htotal;
		config.vsync_end = vtotal - 1;
		config.vsize = vtotal;

		xvtc_generator_start(xtpg->vtc, &config);
	}

	/*
	 * Configure the bayer phase and video timing mux based on the
	 * operation mode (passthrough or test pattern generation). The test
	 * pattern can be modified by the control set handler, we thus need to
	 * take the control lock here to avoid races.
	 */
	mutex_lock(xtpg->ctrl_handler.lock);

	xvip_clr_and_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
			 XTPG_PATTERN_MASK, xtpg->pattern->cur.val);

	/*
	 * Switching between passthrough and test pattern generation modes isn't
	 * allowed during streaming, update the control range accordingly.
	 */
	passthrough = xtpg->pattern->cur.val == 0;
	__xtpg_update_pattern_control(xtpg, passthrough, !passthrough);

	xtpg->streaming = true;

	mutex_unlock(xtpg->ctrl_handler.lock);

	/*
	 * For TPG v5.0, the bayer phase needs to be off for the pass through
	 * mode, otherwise the external input would be subsampled.
	 */
	bayer_phase = passthrough ? XTPG_BAYER_PHASE_OFF
		    : xtpg_get_bayer_phase(xtpg->formats[0].code);
	xvip_write(&xtpg->xvip, XTPG_BAYER_PHASE, bayer_phase);

	if (xtpg->vtmux_gpio)
		gpiod_set_value_cansleep(xtpg->vtmux_gpio, !passthrough);

	xvip_start(&xtpg->xvip);

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static struct v4l2_mbus_framefmt *
__xtpg_get_pad_format(struct xtpg_device *xtpg,
		      struct v4l2_subdev_pad_config *cfg,
		      unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&xtpg->xvip.subdev, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xtpg->formats[pad];
	default:
		return NULL;
	}
}

static int xtpg_get_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct xtpg_device *xtpg = to_tpg(subdev);

	fmt->format = *__xtpg_get_pad_format(xtpg, cfg, fmt->pad, fmt->which);

	return 0;
}

static int xtpg_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct xtpg_device *xtpg = to_tpg(subdev);
	struct v4l2_mbus_framefmt *__format;
	u32 bayer_phase;

	__format = __xtpg_get_pad_format(xtpg, cfg, fmt->pad, fmt->which);

	/* In two pads mode the source pad format is always identical to the
	 * sink pad format.
	 */
	if (xtpg->npads == 2 && fmt->pad == 1) {
		fmt->format = *__format;
		return 0;
	}

	/* Bayer phase is configurable at runtime */
	if (xtpg->bayer) {
		bayer_phase = xtpg_get_bayer_phase(fmt->format.code);
		if (bayer_phase != XTPG_BAYER_PHASE_OFF)
			__format->code = fmt->format.code;
	}

	xvip_set_format_size(__format, fmt);

	fmt->format = *__format;

	/* Propagate the format to the source pad. */
	if (xtpg->npads == 2) {
		__format = __xtpg_get_pad_format(xtpg, cfg, 1, fmt->which);
		*__format = fmt->format;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static int xtpg_enum_frame_size(struct v4l2_subdev *subdev,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(subdev, cfg, fse->pad);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	/* Min / max values for pad 0 is always fixed in both one and two pads
	 * modes. In two pads mode, the source pad(= 1) size is identical to
	 * the sink pad size */
	if (fse->pad == 0) {
		fse->min_width = XVIP_MIN_WIDTH;
		fse->max_width = XVIP_MAX_WIDTH;
		fse->min_height = XVIP_MIN_HEIGHT;
		fse->max_height = XVIP_MAX_HEIGHT;
	} else {
		fse->min_width = format->width;
		fse->max_width = format->width;
		fse->min_height = format->height;
		fse->max_height = format->height;
	}

	return 0;
}

static int xtpg_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct xtpg_device *xtpg = to_tpg(subdev);
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(subdev, fh->pad, 0);
	*format = xtpg->default_format;

	if (xtpg->npads == 2) {
		format = v4l2_subdev_get_try_format(subdev, fh->pad, 1);
		*format = xtpg->default_format;
	}

	return 0;
}

static int xtpg_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int xtpg_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xtpg_device *xtpg = container_of(ctrl->handler,
						struct xtpg_device,
						ctrl_handler);
	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		xvip_clr_and_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
				 XTPG_PATTERN_MASK, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_CROSS_HAIRS:
		xvip_clr_or_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
				XTPG_PATTERN_CONTROL_CROSS_HAIRS, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_MOVING_BOX:
		xvip_clr_or_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
				XTPG_PATTERN_CONTROL_MOVING_BOX, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_COLOR_MASK:
		xvip_clr_and_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
				 XTPG_PATTERN_CONTROL_COLOR_MASK_MASK,
				 ctrl->val <<
				 XTPG_PATTERN_CONTROL_COLOR_MASK_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_STUCK_PIXEL:
		xvip_clr_or_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
				XTPG_PATTERN_CONTROL_STUCK_PIXEL, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_NOISE:
		xvip_clr_or_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
				XTPG_PATTERN_CONTROL_NOISE, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_MOTION:
		xvip_clr_or_set(&xtpg->xvip, XTPG_PATTERN_CONTROL,
				XTPG_PATTERN_CONTROL_MOTION, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_MOTION_SPEED:
		xvip_write(&xtpg->xvip, XTPG_MOTION_SPEED, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_CROSS_HAIR_ROW:
		xvip_clr_and_set(&xtpg->xvip, XTPG_CROSS_HAIRS,
				 XTPG_CROSS_HAIRS_ROW_MASK,
				 ctrl->val << XTPG_CROSS_HAIRS_ROW_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_CROSS_HAIR_COLUMN:
		xvip_clr_and_set(&xtpg->xvip, XTPG_CROSS_HAIRS,
				 XTPG_CROSS_HAIRS_COLUMN_MASK,
				 ctrl->val << XTPG_CROSS_HAIRS_COLUMN_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_ZPLATE_HOR_START:
		xvip_clr_and_set(&xtpg->xvip, XTPG_ZPLATE_HOR_CONTROL,
				 XTPG_ZPLATE_START_MASK,
				 ctrl->val << XTPG_ZPLATE_START_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_ZPLATE_HOR_SPEED:
		xvip_clr_and_set(&xtpg->xvip, XTPG_ZPLATE_HOR_CONTROL,
				 XTPG_ZPLATE_SPEED_MASK,
				 ctrl->val << XTPG_ZPLATE_SPEED_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_ZPLATE_VER_START:
		xvip_clr_and_set(&xtpg->xvip, XTPG_ZPLATE_VER_CONTROL,
				 XTPG_ZPLATE_START_MASK,
				 ctrl->val << XTPG_ZPLATE_START_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_ZPLATE_VER_SPEED:
		xvip_clr_and_set(&xtpg->xvip, XTPG_ZPLATE_VER_CONTROL,
				 XTPG_ZPLATE_SPEED_MASK,
				 ctrl->val << XTPG_ZPLATE_SPEED_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_BOX_SIZE:
		xvip_write(&xtpg->xvip, XTPG_BOX_SIZE, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_BOX_COLOR:
		xvip_write(&xtpg->xvip, XTPG_BOX_COLOR, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_STUCK_PIXEL_THRESH:
		xvip_write(&xtpg->xvip, XTPG_STUCK_PIXEL_THRESH, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_NOISE_GAIN:
		xvip_write(&xtpg->xvip, XTPG_NOISE_GAIN, ctrl->val);
		return 0;
	}

	return 0;
}

static const struct v4l2_ctrl_ops xtpg_ctrl_ops = {
	.s_ctrl	= xtpg_s_ctrl,
};

static const struct v4l2_subdev_core_ops xtpg_core_ops = {
};

static const struct v4l2_subdev_video_ops xtpg_video_ops = {
	.s_stream = xtpg_s_stream,
};

static const struct v4l2_subdev_pad_ops xtpg_pad_ops = {
	.enum_mbus_code		= xvip_enum_mbus_code,
	.enum_frame_size	= xtpg_enum_frame_size,
	.get_fmt		= xtpg_get_format,
	.set_fmt		= xtpg_set_format,
};

static const struct v4l2_subdev_ops xtpg_ops = {
	.core   = &xtpg_core_ops,
	.video  = &xtpg_video_ops,
	.pad    = &xtpg_pad_ops,
};

static const struct v4l2_subdev_internal_ops xtpg_internal_ops = {
	.open	= xtpg_open,
	.close	= xtpg_close,
};

/*
 * Control Config
 */

static const char *const xtpg_pattern_strings[] = {
	"Passthrough",
	"Horizontal Ramp",
	"Vertical Ramp",
	"Temporal Ramp",
	"Solid Red",
	"Solid Green",
	"Solid Blue",
	"Solid Black",
	"Solid White",
	"Color Bars",
	"Zone Plate",
	"Tartan Color Bars",
	"Cross Hatch",
	"None",
	"Vertical/Horizontal Ramps",
	"Black/White Checker Board",
};

static struct v4l2_ctrl_config xtpg_ctrls[] = {
	{
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_CROSS_HAIRS,
		.name	= "Test Pattern: Cross Hairs",
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.min	= false,
		.max	= true,
		.step	= 1,
		.def	= 0,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_MOVING_BOX,
		.name	= "Test Pattern: Moving Box",
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.min	= false,
		.max	= true,
		.step	= 1,
		.def	= 0,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_COLOR_MASK,
		.name	= "Test Pattern: Color Mask",
		.type	= V4L2_CTRL_TYPE_BITMASK,
		.min	= 0,
		.max	= 0xf,
		.def	= 0,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_STUCK_PIXEL,
		.name	= "Test Pattern: Stuck Pixel",
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.min	= false,
		.max	= true,
		.step	= 1,
		.def	= 0,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_NOISE,
		.name	= "Test Pattern: Noise",
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.min	= false,
		.max	= true,
		.step	= 1,
		.def	= 0,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_MOTION,
		.name	= "Test Pattern: Motion",
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.min	= false,
		.max	= true,
		.step	= 1,
		.def	= 0,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_MOTION_SPEED,
		.name	= "Test Pattern: Motion Speed",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 8) - 1,
		.step	= 1,
		.def	= 4,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_CROSS_HAIR_ROW,
		.name	= "Test Pattern: Cross Hairs Row",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 12) - 1,
		.step	= 1,
		.def	= 0x64,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_CROSS_HAIR_COLUMN,
		.name	= "Test Pattern: Cross Hairs Column",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 12) - 1,
		.step	= 1,
		.def	= 0x64,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_ZPLATE_HOR_START,
		.name	= "Test Pattern: Zplate Horizontal Start Pos",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 16) - 1,
		.step	= 1,
		.def	= 0x1e,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_ZPLATE_HOR_SPEED,
		.name	= "Test Pattern: Zplate Horizontal Speed",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 16) - 1,
		.step	= 1,
		.def	= 0,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_ZPLATE_VER_START,
		.name	= "Test Pattern: Zplate Vertical Start Pos",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 16) - 1,
		.step	= 1,
		.def	= 1,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_ZPLATE_VER_SPEED,
		.name	= "Test Pattern: Zplate Vertical Speed",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 16) - 1,
		.step	= 1,
		.def	= 0,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_BOX_SIZE,
		.name	= "Test Pattern: Box Size",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 12) - 1,
		.step	= 1,
		.def	= 0x32,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_BOX_COLOR,
		.name	= "Test Pattern: Box Color(RGB)",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 24) - 1,
		.step	= 1,
		.def	= 0,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_STUCK_PIXEL_THRESH,
		.name	= "Test Pattern: Stuck Pixel threshold",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 16) - 1,
		.step	= 1,
		.def	= 0,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.ops	= &xtpg_ctrl_ops,
		.id	= V4L2_CID_XILINX_TPG_NOISE_GAIN,
		.name	= "Test Pattern: Noise Gain",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= 0,
		.max	= (1 << 8) - 1,
		.step	= 1,
		.def	= 0,
		.flags	= V4L2_CTRL_FLAG_SLIDER,
	},
};

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations xtpg_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * Power Management
 */

static int __maybe_unused xtpg_pm_suspend(struct device *dev)
{
	struct xtpg_device *xtpg = dev_get_drvdata(dev);

	xvip_suspend(&xtpg->xvip);

	return 0;
}

static int __maybe_unused xtpg_pm_resume(struct device *dev)
{
	struct xtpg_device *xtpg = dev_get_drvdata(dev);

	xvip_resume(&xtpg->xvip);

	return 0;
}

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static int xtpg_parse_of(struct xtpg_device *xtpg)
{
	struct device *dev = xtpg->xvip.dev;
	struct device_node *node = xtpg->xvip.dev->of_node;
	struct device_node *ports;
	struct device_node *port;
	unsigned int nports = 0;
	bool has_endpoint = false;

	ports = of_get_child_by_name(node, "ports");
	if (ports == NULL)
		ports = node;

	for_each_child_of_node(ports, port) {
		const struct xvip_video_format *format;
		struct device_node *endpoint;

		if (!of_node_name_eq(port, "port"))
			continue;

		format = xvip_of_get_format(port);
		if (IS_ERR(format)) {
			dev_err(dev, "invalid format in DT");
			of_node_put(port);
			return PTR_ERR(format);
		}

		/* Get and check the format description */
		if (!xtpg->vip_format) {
			xtpg->vip_format = format;
		} else if (xtpg->vip_format != format) {
			dev_err(dev, "in/out format mismatch in DT");
			of_node_put(port);
			return -EINVAL;
		}

		if (nports == 0) {
			endpoint = of_get_next_child(port, NULL);
			if (endpoint)
				has_endpoint = true;
			of_node_put(endpoint);
		}

		/* Count the number of ports. */
		nports++;
	}

	if (nports != 1 && nports != 2) {
		dev_err(dev, "invalid number of ports %u\n", nports);
		return -EINVAL;
	}

	xtpg->npads = nports;
	if (nports == 2 && has_endpoint)
		xtpg->has_input = true;

	return 0;
}

static int xtpg_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xtpg_device *xtpg;
	u32 i, bayer_phase;
	int ret;

	xtpg = devm_kzalloc(&pdev->dev, sizeof(*xtpg), GFP_KERNEL);
	if (!xtpg)
		return -ENOMEM;

	xtpg->xvip.dev = &pdev->dev;

	ret = xtpg_parse_of(xtpg);
	if (ret < 0)
		return ret;

	ret = xvip_init_resources(&xtpg->xvip);
	if (ret < 0)
		return ret;

	xtpg->vtmux_gpio = devm_gpiod_get_optional(&pdev->dev, "timing",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(xtpg->vtmux_gpio)) {
		ret = PTR_ERR(xtpg->vtmux_gpio);
		goto error_resource;
	}

	xtpg->vtc = xvtc_of_get(pdev->dev.of_node);
	if (IS_ERR(xtpg->vtc)) {
		ret = PTR_ERR(xtpg->vtc);
		goto error_resource;
	}

	/* Reset and initialize the core */
	xvip_reset(&xtpg->xvip);

	/* Initialize V4L2 subdevice and media entity. Pad numbers depend on the
	 * number of pads.
	 */
	if (xtpg->npads == 2) {
		xtpg->pads[0].flags = MEDIA_PAD_FL_SINK;
		xtpg->pads[1].flags = MEDIA_PAD_FL_SOURCE;
	} else {
		xtpg->pads[0].flags = MEDIA_PAD_FL_SOURCE;
	}

	/* Initialize the default format */
	xtpg->default_format.code = xtpg->vip_format->code;
	xtpg->default_format.field = V4L2_FIELD_NONE;
	xtpg->default_format.colorspace = V4L2_COLORSPACE_SRGB;
	xvip_get_frame_size(&xtpg->xvip, &xtpg->default_format);

	bayer_phase = xtpg_get_bayer_phase(xtpg->vip_format->code);
	if (bayer_phase != XTPG_BAYER_PHASE_OFF)
		xtpg->bayer = true;

	xtpg->formats[0] = xtpg->default_format;
	if (xtpg->npads == 2)
		xtpg->formats[1] = xtpg->default_format;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xtpg->xvip.subdev;
	v4l2_subdev_init(subdev, &xtpg_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xtpg_internal_ops;
	strscpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xtpg);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	subdev->entity.ops = &xtpg_media_ops;

	ret = media_entity_pads_init(&subdev->entity, xtpg->npads, xtpg->pads);
	if (ret < 0)
		goto error;

	v4l2_ctrl_handler_init(&xtpg->ctrl_handler, 3 + ARRAY_SIZE(xtpg_ctrls));

	xtpg->vblank = v4l2_ctrl_new_std(&xtpg->ctrl_handler, &xtpg_ctrl_ops,
					 V4L2_CID_VBLANK, XTPG_MIN_VBLANK,
					 XTPG_MAX_VBLANK, 1, 100);
	xtpg->hblank = v4l2_ctrl_new_std(&xtpg->ctrl_handler, &xtpg_ctrl_ops,
					 V4L2_CID_HBLANK, XTPG_MIN_HBLANK,
					 XTPG_MAX_HBLANK, 1, 100);
	xtpg->pattern = v4l2_ctrl_new_std_menu_items(&xtpg->ctrl_handler,
					&xtpg_ctrl_ops, V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(xtpg_pattern_strings) - 1,
					1, 9, xtpg_pattern_strings);

	for (i = 0; i < ARRAY_SIZE(xtpg_ctrls); i++)
		v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_ctrls[i], NULL);

	if (xtpg->ctrl_handler.error) {
		dev_err(&pdev->dev, "failed to add controls\n");
		ret = xtpg->ctrl_handler.error;
		goto error;
	}
	subdev->ctrl_handler = &xtpg->ctrl_handler;

	xtpg_update_pattern_control(xtpg, true, true);

	ret = v4l2_ctrl_handler_setup(&xtpg->ctrl_handler);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to set controls\n");
		goto error;
	}

	platform_set_drvdata(pdev, xtpg);

	xvip_print_version(&xtpg->xvip);

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto error;
	}

	return 0;

error:
	v4l2_ctrl_handler_free(&xtpg->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
	xvtc_put(xtpg->vtc);
error_resource:
	xvip_cleanup_resources(&xtpg->xvip);
	return ret;
}

static int xtpg_remove(struct platform_device *pdev)
{
	struct xtpg_device *xtpg = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xtpg->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xtpg->ctrl_handler);
	media_entity_cleanup(&subdev->entity);

	xvip_cleanup_resources(&xtpg->xvip);

	return 0;
}

static SIMPLE_DEV_PM_OPS(xtpg_pm_ops, xtpg_pm_suspend, xtpg_pm_resume);

static const struct of_device_id xtpg_of_id_table[] = {
	{ .compatible = "xlnx,v-tpg-5.0" },
	{ }
};
MODULE_DEVICE_TABLE(of, xtpg_of_id_table);

static struct platform_driver xtpg_driver = {
	.driver = {
		.name		= "xilinx-tpg",
		.pm		= &xtpg_pm_ops,
		.of_match_table	= xtpg_of_id_table,
	},
	.probe			= xtpg_probe,
	.remove			= xtpg_remove,
};

module_platform_driver(xtpg_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Xilinx Test Pattern Generator Driver");
MODULE_LICENSE("GPL v2");

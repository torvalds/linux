// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Mali-C55 ISP Driver - Image signal processor
 *
 * Copyright (C) 2025 Ideas on Board Oy
 */

#include <linux/media/arm/mali-c55-config.h>

#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/property.h>
#include <linux/string.h>

#include <uapi/linux/media/arm/mali-c55-config.h>

#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>

#include "mali-c55-common.h"
#include "mali-c55-registers.h"

static const struct mali_c55_isp_format_info mali_c55_isp_fmts[] = {
	{
		.code = MEDIA_BUS_FMT_SRGGB20_1X20,
		.shifted_code = MEDIA_BUS_FMT_SRGGB16_1X16,
		.order = MALI_C55_BAYER_ORDER_RGGB,
		.bypass = false,
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG20_1X20,
		.shifted_code = MEDIA_BUS_FMT_SGRBG16_1X16,
		.order = MALI_C55_BAYER_ORDER_GRBG,
		.bypass = false,
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG20_1X20,
		.shifted_code = MEDIA_BUS_FMT_SGBRG16_1X16,
		.order = MALI_C55_BAYER_ORDER_GBRG,
		.bypass = false,
	},
	{
		.code = MEDIA_BUS_FMT_SBGGR20_1X20,
		.shifted_code = MEDIA_BUS_FMT_SBGGR16_1X16,
		.order = MALI_C55_BAYER_ORDER_BGGR,
		.bypass = false,
	},
	{
		.code = MEDIA_BUS_FMT_RGB202020_1X60,
		.shifted_code = 0, /* Not relevant for this format */
		.order = 0, /* Not relevant for this format */
		.bypass = true,
	}
	/*
	 * TODO: Support MEDIA_BUS_FMT_YUV20_1X60 here. This is so that we can
	 * also support YUV input from a sensor passed-through to the output. At
	 * present we have no mechanism to test that though so it may have to
	 * wait a while...
	 */
};

const struct mali_c55_isp_format_info *
mali_c55_isp_get_mbus_config_by_index(u32 index)
{
	if (index < ARRAY_SIZE(mali_c55_isp_fmts))
		return &mali_c55_isp_fmts[index];

	return NULL;
}

const struct mali_c55_isp_format_info *
mali_c55_isp_get_mbus_config_by_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mali_c55_isp_fmts); i++) {
		if (mali_c55_isp_fmts[i].code == code)
			return &mali_c55_isp_fmts[i];
	}

	return NULL;
}

const struct mali_c55_isp_format_info *
mali_c55_isp_get_mbus_config_by_shifted_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mali_c55_isp_fmts); i++) {
		if (mali_c55_isp_fmts[i].shifted_code == code)
			return &mali_c55_isp_fmts[i];
	}

	return NULL;
}

static void mali_c55_isp_stop(struct mali_c55 *mali_c55)
{
	u32 val;

	mali_c55_write(mali_c55, MALI_C55_REG_INPUT_MODE_REQUEST,
		       MALI_C55_INPUT_SAFE_STOP);
	readl_poll_timeout(mali_c55->base + MALI_C55_REG_MODE_STATUS,
			   val, !val, 10 * USEC_PER_MSEC, 250 * USEC_PER_MSEC);
}

static int mali_c55_isp_start(struct mali_c55 *mali_c55,
			      const struct v4l2_subdev_state *state)
{
	struct mali_c55_context *ctx = mali_c55_get_active_context(mali_c55);
	const struct mali_c55_isp_format_info *cfg;
	const struct v4l2_mbus_framefmt *format;
	const struct v4l2_rect *crop;
	u32 val;
	int ret;

	mali_c55_update_bits(mali_c55, MALI_C55_REG_MCU_CONFIG,
			     MALI_C55_REG_MCU_CONFIG_WRITE_MASK,
			     MALI_C55_REG_MCU_CONFIG_WRITE_PING);

	/* Apply input windowing */
	crop = v4l2_subdev_state_get_crop(state, MALI_C55_ISP_PAD_SINK_VIDEO);
	format = v4l2_subdev_state_get_format(state,
					      MALI_C55_ISP_PAD_SINK_VIDEO);
	cfg = mali_c55_isp_get_mbus_config_by_code(format->code);

	mali_c55_write(mali_c55, MALI_C55_REG_HC_START,
		       MALI_C55_HC_START(crop->left));
	mali_c55_write(mali_c55, MALI_C55_REG_HC_SIZE,
		       MALI_C55_HC_SIZE(crop->width));
	mali_c55_write(mali_c55, MALI_C55_REG_VC_START_SIZE,
		       MALI_C55_VC_START(crop->top) |
		       MALI_C55_VC_SIZE(crop->height));
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_BASE_ADDR,
				 MALI_C55_REG_ACTIVE_WIDTH_MASK, format->width);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_BASE_ADDR,
				 MALI_C55_REG_ACTIVE_HEIGHT_MASK,
				 format->height << 16);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_BAYER_ORDER,
				 MALI_C55_BAYER_ORDER_MASK, cfg->order);
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_INPUT_WIDTH,
				 MALI_C55_INPUT_WIDTH_MASK,
				 MALI_C55_INPUT_WIDTH_20BIT);

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_ISP_RAW_BYPASS,
				 MALI_C55_ISP_RAW_BYPASS_BYPASS_MASK,
				 cfg->bypass ? MALI_C55_ISP_RAW_BYPASS_BYPASS_MASK :
					     0x00);

	mali_c55_params_write_config(mali_c55);
	ret = mali_c55_config_write(ctx, MALI_C55_CONFIG_PING, true);
	if (ret) {
		dev_err(mali_c55->dev, "failed to write ISP config\n");
		return ret;
	}

	mali_c55_write(mali_c55, MALI_C55_REG_INPUT_MODE_REQUEST,
		       MALI_C55_INPUT_SAFE_START);

	ret = readl_poll_timeout(mali_c55->base + MALI_C55_REG_MODE_STATUS, val,
				 val, 10 * USEC_PER_MSEC, 250 * USEC_PER_MSEC);
	if (ret) {
		mali_c55_isp_stop(mali_c55);
		dev_err(mali_c55->dev, "timeout starting ISP\n");
		return ret;
	}

	return 0;
}

static int mali_c55_isp_enum_mbus_code(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       struct v4l2_subdev_mbus_code_enum *code)
{
	/*
	 * Only the internal RGB processed format is allowed on the regular
	 * processing source pad.
	 */
	if (code->pad == MALI_C55_ISP_PAD_SOURCE_VIDEO) {
		if (code->index)
			return -EINVAL;

		code->code = MEDIA_BUS_FMT_RGB121212_1X36;
		return 0;
	}

	/* On the sink and bypass pads all the supported formats are allowed. */
	if (code->index >= ARRAY_SIZE(mali_c55_isp_fmts))
		return -EINVAL;

	code->code = mali_c55_isp_fmts[code->index].code;

	return 0;
}

static int mali_c55_isp_enum_frame_size(struct v4l2_subdev *sd,
					struct v4l2_subdev_state *state,
					struct v4l2_subdev_frame_size_enum *fse)
{
	const struct mali_c55_isp_format_info *cfg;

	if (fse->index > 0)
		return -EINVAL;

	/*
	 * Only the internal RGB processed format is allowed on the regular
	 * processing source pad.
	 *
	 * On the sink and bypass pads all the supported formats are allowed.
	 */
	if (fse->pad == MALI_C55_ISP_PAD_SOURCE_VIDEO) {
		if (fse->code != MEDIA_BUS_FMT_RGB121212_1X36)
			return -EINVAL;
	} else {
		cfg = mali_c55_isp_get_mbus_config_by_code(fse->code);
		if (!cfg)
			return -EINVAL;
	}

	fse->min_width = MALI_C55_MIN_WIDTH;
	fse->min_height = MALI_C55_MIN_HEIGHT;
	fse->max_width = MALI_C55_MAX_WIDTH;
	fse->max_height = MALI_C55_MAX_HEIGHT;

	return 0;
}

static int mali_c55_isp_set_fmt(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct v4l2_mbus_framefmt *src_fmt, *sink_fmt;
	const struct mali_c55_isp_format_info *cfg;
	struct v4l2_rect *crop;

	/*
	 * Disallow set_fmt on the source pads; format is fixed and the sizes
	 * are the result of applying the sink crop rectangle to the sink
	 * format.
	 */
	if (format->pad != MALI_C55_ISP_PAD_SINK_VIDEO)
		return v4l2_subdev_get_fmt(sd, state, format);

	sink_fmt = v4l2_subdev_state_get_format(state,
						MALI_C55_ISP_PAD_SINK_VIDEO);

	cfg = mali_c55_isp_get_mbus_config_by_code(fmt->code);
	sink_fmt->code = cfg ? fmt->code : MEDIA_BUS_FMT_SRGGB20_1X20;

	/*
	 * Clamp sizes in the accepted limits and clamp the crop rectangle in
	 * the new sizes.
	 */
	sink_fmt->width = clamp(fmt->width, MALI_C55_MIN_WIDTH,
				MALI_C55_MAX_WIDTH);
	sink_fmt->height = clamp(fmt->height, MALI_C55_MIN_HEIGHT,
				 MALI_C55_MAX_HEIGHT);

	*fmt = *sink_fmt;

	crop = v4l2_subdev_state_get_crop(state, MALI_C55_ISP_PAD_SINK_VIDEO);
	crop->left = 0;
	crop->top = 0;
	crop->width = sink_fmt->width;
	crop->height = sink_fmt->height;

	/*
	 * Propagate format to source pads. On the 'regular' output pad use
	 * the internal RGB processed format, while on the bypass pad simply
	 * replicate the ISP sink format. The sizes on both pads are the same as
	 * the ISP sink crop rectangle. The "field" and "colorspace" fields are
	 * set in .init_state() and fixed for both source pads, as is the "code"
	 * field for the processed data source pad.
	 */
	src_fmt = v4l2_subdev_state_get_format(state,
					       MALI_C55_ISP_PAD_SOURCE_VIDEO);
	src_fmt->width = crop->width;
	src_fmt->height = crop->height;

	src_fmt = v4l2_subdev_state_get_format(state,
					       MALI_C55_ISP_PAD_SOURCE_BYPASS);
	src_fmt->code = sink_fmt->code;
	src_fmt->width = crop->width;
	src_fmt->height = crop->height;

	return 0;
}

static int mali_c55_isp_get_selection(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_selection *sel)
{
	if (sel->pad != MALI_C55_ISP_PAD_SINK_VIDEO ||
	    sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	sel->r = *v4l2_subdev_state_get_crop(state, MALI_C55_ISP_PAD_SINK_VIDEO);

	return 0;
}

static int mali_c55_isp_set_selection(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_selection *sel)
{
	struct v4l2_mbus_framefmt *src_fmt;
	const struct v4l2_mbus_framefmt *fmt;
	struct v4l2_rect *crop;

	if (sel->pad != MALI_C55_ISP_PAD_SINK_VIDEO ||
	    sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	fmt = v4l2_subdev_state_get_format(state, MALI_C55_ISP_PAD_SINK_VIDEO);

	sel->r.left = clamp_t(unsigned int, sel->r.left, 0, fmt->width);
	sel->r.top = clamp_t(unsigned int, sel->r.top, 0, fmt->height);
	sel->r.width = clamp_t(unsigned int, sel->r.width, MALI_C55_MIN_WIDTH,
			       fmt->width - sel->r.left);
	sel->r.height = clamp_t(unsigned int, sel->r.height,
				MALI_C55_MIN_HEIGHT,
				fmt->height - sel->r.top);

	crop = v4l2_subdev_state_get_crop(state, MALI_C55_ISP_PAD_SINK_VIDEO);
	*crop = sel->r;

	/*
	 * Propagate the crop rectangle sizes to the source pad format. The crop
	 * isn't propagated to the bypass source pad, because the bypassed data
	 * cannot be cropped.
	 */
	src_fmt = v4l2_subdev_state_get_format(state,
					       MALI_C55_ISP_PAD_SOURCE_VIDEO);
	src_fmt->width = crop->width;
	src_fmt->height = crop->height;

	return 0;
}

static int mali_c55_isp_enable_streams(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state, u32 pad,
				       u64 streams_mask)
{
	struct mali_c55_isp *isp = container_of(sd, struct mali_c55_isp, sd);
	struct mali_c55 *mali_c55 = isp->mali_c55;
	struct v4l2_subdev *src_sd;
	struct media_pad *sink_pad;
	int ret;

	/*
	 * We have two source pads, both of which have only a single stream. The
	 * core v4l2 code already validated those parameters so we can just get
	 * on with starting the ISP.
	 */

	sink_pad = &isp->pads[MALI_C55_ISP_PAD_SINK_VIDEO];
	isp->remote_src = media_pad_remote_pad_unique(sink_pad);
	src_sd = media_entity_to_v4l2_subdev(isp->remote_src->entity);

	isp->frame_sequence = 0;
	ret = mali_c55_isp_start(mali_c55, state);
	if (ret) {
		dev_err(mali_c55->dev, "Failed to start ISP\n");
		isp->remote_src = NULL;
		return ret;
	}

	/*
	 * We only support a single input stream, so we can just enable the 1st
	 * entry in the streams mask.
	 */
	ret = v4l2_subdev_enable_streams(src_sd, isp->remote_src->index, BIT(0));
	if (ret) {
		dev_err(mali_c55->dev, "Failed to start ISP source\n");
		mali_c55_isp_stop(mali_c55);
		return ret;
	}

	return 0;
}

static int mali_c55_isp_disable_streams(struct v4l2_subdev *sd,
					struct v4l2_subdev_state *state, u32 pad,
					u64 streams_mask)
{
	struct mali_c55_isp *isp = container_of(sd, struct mali_c55_isp, sd);
	struct mali_c55 *mali_c55 = isp->mali_c55;
	struct v4l2_subdev *src_sd;

	if (isp->remote_src) {
		src_sd = media_entity_to_v4l2_subdev(isp->remote_src->entity);
		v4l2_subdev_disable_streams(src_sd, isp->remote_src->index,
					    BIT(0));
	}
	isp->remote_src = NULL;

	mali_c55_isp_stop(mali_c55);

	return 0;
}

static const struct v4l2_subdev_pad_ops mali_c55_isp_pad_ops = {
	.enum_mbus_code		= mali_c55_isp_enum_mbus_code,
	.enum_frame_size	= mali_c55_isp_enum_frame_size,
	.get_fmt		= v4l2_subdev_get_fmt,
	.set_fmt		= mali_c55_isp_set_fmt,
	.get_selection		= mali_c55_isp_get_selection,
	.set_selection		= mali_c55_isp_set_selection,
	.link_validate		= v4l2_subdev_link_validate_default,
	.enable_streams		= mali_c55_isp_enable_streams,
	.disable_streams	= mali_c55_isp_disable_streams,
};

void mali_c55_isp_queue_event_sof(struct mali_c55 *mali_c55)
{
	struct v4l2_event event = {
		.type = V4L2_EVENT_FRAME_SYNC,
	};

	event.u.frame_sync.frame_sequence = mali_c55->isp.frame_sequence;
	v4l2_event_queue(mali_c55->isp.sd.devnode, &event);
}

static int
mali_c55_isp_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
			     struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_FRAME_SYNC:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_subdev_core_ops mali_c55_isp_core_ops = {
	.subscribe_event = mali_c55_isp_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_ops mali_c55_isp_ops = {
	.pad	= &mali_c55_isp_pad_ops,
	.core	= &mali_c55_isp_core_ops,
};

static int mali_c55_isp_init_state(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state)
{
	struct v4l2_mbus_framefmt *sink_fmt, *src_fmt;
	struct v4l2_rect *in_crop;

	sink_fmt = v4l2_subdev_state_get_format(state,
						MALI_C55_ISP_PAD_SINK_VIDEO);
	src_fmt = v4l2_subdev_state_get_format(state,
					       MALI_C55_ISP_PAD_SOURCE_VIDEO);
	in_crop = v4l2_subdev_state_get_crop(state,
					     MALI_C55_ISP_PAD_SINK_VIDEO);

	sink_fmt->width = MALI_C55_DEFAULT_WIDTH;
	sink_fmt->height = MALI_C55_DEFAULT_HEIGHT;
	sink_fmt->field = V4L2_FIELD_NONE;
	sink_fmt->code = MEDIA_BUS_FMT_SRGGB20_1X20;
	sink_fmt->colorspace = V4L2_COLORSPACE_RAW;
	sink_fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(sink_fmt->colorspace);
	sink_fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(sink_fmt->colorspace);
	sink_fmt->quantization =
		V4L2_MAP_QUANTIZATION_DEFAULT(false, sink_fmt->colorspace,
					      sink_fmt->ycbcr_enc);

	*v4l2_subdev_state_get_format(state,
			      MALI_C55_ISP_PAD_SOURCE_BYPASS) = *sink_fmt;

	src_fmt->width = MALI_C55_DEFAULT_WIDTH;
	src_fmt->height = MALI_C55_DEFAULT_HEIGHT;
	src_fmt->field = V4L2_FIELD_NONE;
	src_fmt->code = MEDIA_BUS_FMT_RGB121212_1X36;
	src_fmt->colorspace = V4L2_COLORSPACE_SRGB;
	src_fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(sink_fmt->colorspace);
	src_fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(sink_fmt->colorspace);
	src_fmt->quantization =
		V4L2_MAP_QUANTIZATION_DEFAULT(false, sink_fmt->colorspace,
					      sink_fmt->ycbcr_enc);

	in_crop->top = 0;
	in_crop->left = 0;
	in_crop->width = MALI_C55_DEFAULT_WIDTH;
	in_crop->height = MALI_C55_DEFAULT_HEIGHT;

	src_fmt = v4l2_subdev_state_get_format(state,
					       MALI_C55_ISP_PAD_SOURCE_STATS);
	sink_fmt = v4l2_subdev_state_get_format(state,
						MALI_C55_ISP_PAD_SINK_PARAMS);

	src_fmt->width = 0;
	src_fmt->height = 0;
	src_fmt->field = V4L2_FIELD_NONE;
	src_fmt->code = MEDIA_BUS_FMT_METADATA_FIXED;

	sink_fmt->width = 0;
	sink_fmt->height = 0;
	sink_fmt->field = V4L2_FIELD_NONE;
	sink_fmt->code = MEDIA_BUS_FMT_METADATA_FIXED;

	return 0;
}

static const struct v4l2_subdev_internal_ops mali_c55_isp_internal_ops = {
	.init_state = mali_c55_isp_init_state,
};

static int mali_c55_subdev_link_validate(struct media_link *link)
{
	/*
	 * Skip validation for the parameters sink pad, as the source is not
	 * a subdevice.
	 */
	if (link->sink->index == MALI_C55_ISP_PAD_SINK_PARAMS)
		return 0;

	return v4l2_subdev_link_validate(link);
}

static const struct media_entity_operations mali_c55_isp_media_ops = {
	.link_validate		= mali_c55_subdev_link_validate,
};

static int mali_c55_isp_s_ctrl(struct v4l2_ctrl *ctrl)
{
	/*
	 * .s_ctrl() is a mandatory operation, but the driver has only a single
	 * read only control. If we got here, something went badly wrong.
	 */
	return -EINVAL;
}

static const struct v4l2_ctrl_ops mali_c55_isp_ctrl_ops = {
	.s_ctrl = mali_c55_isp_s_ctrl,
};

/* NOT const because the default needs to be filled in at runtime */
static struct v4l2_ctrl_config mali_c55_isp_v4l2_custom_ctrls[] = {
	{
		.ops = &mali_c55_isp_ctrl_ops,
		.id = V4L2_CID_MALI_C55_CAPABILITIES,
		.name = "Mali-C55 ISP Capabilities",
		.type = V4L2_CTRL_TYPE_BITMASK,
		.min = 0,
		.max = MALI_C55_GPS_PONG_FITTED |
		       MALI_C55_GPS_WDR_FITTED |
		       MALI_C55_GPS_COMPRESSION_FITTED |
		       MALI_C55_GPS_TEMPER_FITTED |
		       MALI_C55_GPS_SINTER_LITE_FITTED |
		       MALI_C55_GPS_SINTER_FITTED |
		       MALI_C55_GPS_IRIDIX_LTM_FITTED |
		       MALI_C55_GPS_IRIDIX_GTM_FITTED |
		       MALI_C55_GPS_CNR_FITTED |
		       MALI_C55_GPS_FRSCALER_FITTED |
		       MALI_C55_GPS_DS_PIPE_FITTED,
		.def = 0,
	},
};

static int mali_c55_isp_init_controls(struct mali_c55 *mali_c55)
{
	struct v4l2_ctrl_handler *handler = &mali_c55->isp.handler;
	struct v4l2_ctrl *capabilities;
	int ret;

	ret = v4l2_ctrl_handler_init(handler, 1);
	if (ret)
		return ret;

	mali_c55_isp_v4l2_custom_ctrls[0].def = mali_c55->capabilities;

	capabilities = v4l2_ctrl_new_custom(handler,
					    &mali_c55_isp_v4l2_custom_ctrls[0],
					    NULL);
	if (capabilities)
		capabilities->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	if (handler->error) {
		dev_err(mali_c55->dev, "failed to register capabilities control\n");
		ret = handler->error;
		v4l2_ctrl_handler_free(handler);
		return ret;
	}

	mali_c55->isp.sd.ctrl_handler = handler;

	return 0;
}

int mali_c55_register_isp(struct mali_c55 *mali_c55)
{
	struct mali_c55_isp *isp = &mali_c55->isp;
	struct v4l2_subdev *sd = &isp->sd;
	int ret;

	isp->mali_c55 = mali_c55;

	v4l2_subdev_init(sd, &mali_c55_isp_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sd->entity.ops = &mali_c55_isp_media_ops;
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_ISP;
	sd->internal_ops = &mali_c55_isp_internal_ops;
	strscpy(sd->name, MALI_C55_DRIVER_NAME " isp", sizeof(sd->name));

	isp->pads[MALI_C55_ISP_PAD_SINK_VIDEO].flags = MEDIA_PAD_FL_SINK |
						       MEDIA_PAD_FL_MUST_CONNECT;
	isp->pads[MALI_C55_ISP_PAD_SOURCE_VIDEO].flags = MEDIA_PAD_FL_SOURCE;
	isp->pads[MALI_C55_ISP_PAD_SOURCE_BYPASS].flags = MEDIA_PAD_FL_SOURCE;
	isp->pads[MALI_C55_ISP_PAD_SOURCE_STATS].flags = MEDIA_PAD_FL_SOURCE;
	isp->pads[MALI_C55_ISP_PAD_SINK_PARAMS].flags = MEDIA_PAD_FL_SINK;

	ret = media_entity_pads_init(&sd->entity, MALI_C55_ISP_NUM_PADS,
				     isp->pads);
	if (ret)
		return ret;

	ret = mali_c55_isp_init_controls(mali_c55);
	if (ret)
		goto err_cleanup_media_entity;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret)
		goto err_free_ctrl_handler;

	ret = v4l2_device_register_subdev(&mali_c55->v4l2_dev, sd);
	if (ret)
		goto err_cleanup_subdev;

	mutex_init(&isp->capture_lock);

	return 0;

err_cleanup_subdev:
	v4l2_subdev_cleanup(sd);
err_free_ctrl_handler:
	v4l2_ctrl_handler_free(&isp->handler);
err_cleanup_media_entity:
	media_entity_cleanup(&sd->entity);
	isp->mali_c55 = NULL;

	return ret;
}

void mali_c55_unregister_isp(struct mali_c55 *mali_c55)
{
	struct mali_c55_isp *isp = &mali_c55->isp;

	if (!isp->mali_c55)
		return;

	mutex_destroy(&isp->capture_lock);
	v4l2_device_unregister_subdev(&isp->sd);
	v4l2_subdev_cleanup(&isp->sd);
	media_entity_cleanup(&isp->sd.entity);
}

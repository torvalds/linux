// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Intel Corporation
 * CVS driver - CSI/V4L2 subdev support
 */

#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>

#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>

#include "icvs.h"

/*
 * Helpers
 */
static inline struct icvs *notifier_to_csi(struct v4l2_async_notifier *n)
{
	return container_of(n, struct icvs, notifier);
}

static inline struct icvs *sd_to_csi(struct v4l2_subdev *sd)
{
	return container_of(sd, struct icvs, subdev);
}

/*
 * Default formats
 */
static const struct v4l2_mbus_framefmt cvs_csi_format_mbus_default = {
	.width = 1,
	.height = 1,
	.code = MEDIA_BUS_FMT_Y8_1X8,
	.field = V4L2_FIELD_NONE,
};

/**
 * csi_set_link_cfg - Program default CSI-2 link parameters
 * @ctx: CVS device context
 *
 * Populates a HOST_SET_MIPI_CONFIG command using current lane count and
 * link frequency, then submits it to the device.
 * Rest of the link parameters are left at firmware defaults.
 *
 * Return: 0 on success or negative errno.
 */
static int csi_set_link_cfg(struct icvs *ctx)
{
	struct icvs_cmd cmd = {
		.cmd_id = cpu_to_be16(ICVS_HOST_SET_MIPI_CONFIG),
		.param.conf.nr_of_lanes = ctx->nr_of_lanes,
		.param.conf.link_freq = ctx->link_freq,
	};
	size_t cmd_size = sizeof(cmd.cmd_id) + sizeof(cmd.param.conf);

	guard(mutex)(&ctx->lock);
	return cvs_send(ctx, &cmd, cmd_size);
}

/*
 * Streaming
 */

/**
 * cvs_csi_enable_streams - Start streaming through the bridge
 * @sd: Sub-device pointer
 * @state: Active state
 * @pad: Pad identifier (must be ICVS_CSI_PAD_SOURCE)
 * @streams_mask: Streams to enable (bit 0 supported)
 *
 * Runtime-resumes the bridge (triggering cvs_runtime_resume() to claim CSI-2
 * link ownership), fetches the link frequency, programs the MIPI configuration,
 * and forwards the enable request downstream.
 *
 * Return: 0 on success or negative errno.
 */
static int cvs_csi_enable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  u32 pad, u64 streams_mask)
{
	struct icvs *ctx = sd_to_csi(sd);
	struct v4l2_subdev *remote_sd =
			    media_entity_to_v4l2_subdev(ctx->remote->entity);
	struct device *dev = cvs_dev(ctx);
	s64 freq;
	int ret;

	/* cvs_set_link_owner(ICVS_CSI_LINK_HOST) */
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	freq = v4l2_get_link_freq(ctx->remote, 0, 0);
	if (freq < 0) {
		ret = freq;
		goto err_rpm_put;
	}
	ctx->link_freq = freq;

	if (ctx->i2c_client) {
		ret = csi_set_link_cfg(ctx);
		if (ret < 0)
			goto err_rpm_put_sync;
	}

	ret = v4l2_subdev_enable_streams(remote_sd,
					 ctx->remote->index,
					 streams_mask);
	if (ret)
		goto err_rpm_put_sync;

	return 0;

err_rpm_put_sync:
	/* Bypass autosuspend to immediately release ownership on error */
	pm_runtime_put_sync(dev);
	return ret;
err_rpm_put:
	pm_runtime_put_autosuspend(dev);
	return ret;
}

/**
 * cvs_csi_disable_streams - Stop streaming through the bridge
 * @sd: Sub-device pointer
 * @state: Active state
 * @pad: Pad identifier (must be ICVS_CSI_PAD_SOURCE)
 * @streams_mask: Streams to disable (bit 0 supported)
 *
 * Disables the remote sensor stream then drops the PM reference acquired
 * during enable. After the autosuspend delay, cvs_runtime_suspend() will
 * return CSI-2 link ownership to CVS firmware.
 *
 * Return: 0 on success or negative errno.
 */
static int cvs_csi_disable_streams(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   u32 pad, u64 streams_mask)
{
	struct icvs *ctx = sd_to_csi(sd);
	struct v4l2_subdev *remote_sd =
			    media_entity_to_v4l2_subdev(ctx->remote->entity);
	struct device *dev = cvs_dev(ctx);
	int ret;

	ret = v4l2_subdev_disable_streams(remote_sd,
					  ctx->remote->index,
					  streams_mask);
	if (ret)
		dev_err(dev, "disable streams failed: %d\n", ret);

	/* cvs_set_link_owner(ICVS_CSI_LINK_CVS) */
	pm_runtime_put_autosuspend(dev);

	return ret;
}

/*
 * Pad operations / formats
 */
/**
 * cvs_csi_init_state - Initialize pad formats in subdev state
 * @sd: Sub-device
 * @state: State container
 *
 * Sets all pad formats to a minimal 1x1 default.
 *
 * Return: 0.
 */
static int cvs_csi_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state)
{
	for (unsigned int i = 0; i < sd->entity.num_pads; i++)
		*v4l2_subdev_state_get_format(state, i) =
			cvs_csi_format_mbus_default;

	return 0;
}

/**
 * cvs_csi_set_fmt - Negotiate pad format
 * @sd: Sub-device
 * @state: State
 * @format: Desired / returned format
 *
 * Mirrors sink format onto source pad. Accepts many media bus codes, falling
 * back to Y8 if unsupported. Normalizes field setting.
 *
 * Return: 0.
 */
static int cvs_csi_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *src =
		v4l2_subdev_state_get_format(state, ICVS_CSI_PAD_SOURCE);
	struct v4l2_mbus_framefmt *sink =
		v4l2_subdev_state_get_format(state, ICVS_CSI_PAD_SINK);

	if (format->pad == ICVS_CSI_PAD_SOURCE) { /* source pad mirrors sink */
		*src = *sink;
		return 0;
	}

	v4l_bound_align_image(&format->format.width, 1, 65536, 0,
			      &format->format.height, 1, 65536, 0, 0);

	switch (format->format.code) {
	/* Accept a large list; default fallback to Y8 */
	case MEDIA_BUS_FMT_RGB444_1X12:
	case MEDIA_BUS_FMT_RGB444_2X8_PADHI_BE:
	case MEDIA_BUS_FMT_RGB444_2X8_PADHI_LE:
	case MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE:
	case MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE:
	case MEDIA_BUS_FMT_RGB565_1X16:
	case MEDIA_BUS_FMT_BGR565_2X8_BE:
	case MEDIA_BUS_FMT_BGR565_2X8_LE:
	case MEDIA_BUS_FMT_RGB565_2X8_BE:
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_RBG888_1X24:
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
	case MEDIA_BUS_FMT_BGR888_1X24:
	case MEDIA_BUS_FMT_GBR888_1X24:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB888_2X12_BE:
	case MEDIA_BUS_FMT_RGB888_2X12_LE:
	case MEDIA_BUS_FMT_ARGB8888_1X32:
	case MEDIA_BUS_FMT_RGB888_1X32_PADHI:
	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_RGB121212_1X36:
	case MEDIA_BUS_FMT_RGB161616_1X48:
	case MEDIA_BUS_FMT_Y8_1X8:
	case MEDIA_BUS_FMT_UV8_1X8:
	case MEDIA_BUS_FMT_UYVY8_1_5X8:
	case MEDIA_BUS_FMT_VYUY8_1_5X8:
	case MEDIA_BUS_FMT_YUYV8_1_5X8:
	case MEDIA_BUS_FMT_YVYU8_1_5X8:
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_VYUY8_2X8:
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
	case MEDIA_BUS_FMT_Y10_1X10:
	case MEDIA_BUS_FMT_UYVY10_2X10:
	case MEDIA_BUS_FMT_VYUY10_2X10:
	case MEDIA_BUS_FMT_YUYV10_2X10:
	case MEDIA_BUS_FMT_YVYU10_2X10:
	case MEDIA_BUS_FMT_Y12_1X12:
	case MEDIA_BUS_FMT_UYVY12_2X12:
	case MEDIA_BUS_FMT_VYUY12_2X12:
	case MEDIA_BUS_FMT_YUYV12_2X12:
	case MEDIA_BUS_FMT_YVYU12_2X12:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_VYUY8_1X16:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YVYU8_1X16:
	case MEDIA_BUS_FMT_YDYUYDYV8_1X16:
	case MEDIA_BUS_FMT_UYVY10_1X20:
	case MEDIA_BUS_FMT_VYUY10_1X20:
	case MEDIA_BUS_FMT_YUYV10_1X20:
	case MEDIA_BUS_FMT_YVYU10_1X20:
	case MEDIA_BUS_FMT_VUY8_1X24:
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
	case MEDIA_BUS_FMT_UYVY12_1X24:
	case MEDIA_BUS_FMT_VYUY12_1X24:
	case MEDIA_BUS_FMT_YUYV12_1X24:
	case MEDIA_BUS_FMT_YVYU12_1X24:
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
	case MEDIA_BUS_FMT_AYUV8_1X32:
	case MEDIA_BUS_FMT_UYYVYY12_0_5X36:
	case MEDIA_BUS_FMT_YUV12_1X36:
	case MEDIA_BUS_FMT_YUV16_1X48:
	case MEDIA_BUS_FMT_UYYVYY16_0_5X48:
	case MEDIA_BUS_FMT_JPEG_1X8:
	case MEDIA_BUS_FMT_AHSV8888_1X32:
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
	case MEDIA_BUS_FMT_SBGGR14_1X14:
	case MEDIA_BUS_FMT_SGBRG14_1X14:
	case MEDIA_BUS_FMT_SGRBG14_1X14:
	case MEDIA_BUS_FMT_SRGGB14_1X14:
	case MEDIA_BUS_FMT_SBGGR16_1X16:
	case MEDIA_BUS_FMT_SGBRG16_1X16:
	case MEDIA_BUS_FMT_SGRBG16_1X16:
	case MEDIA_BUS_FMT_SRGGB16_1X16:
		break;
	default:
		format->format.code = MEDIA_BUS_FMT_Y8_1X8;
		break;
	}

	if (format->format.field == V4L2_FIELD_ANY)
		format->format.field = V4L2_FIELD_NONE;

	*sink = format->format;
	*src = *sink;

	return 0;
}

/**
 * cvs_csi_get_mbus_config - Provide current CSI-2 bus configuration
 * @sd: Sub-device
 * @pad: Pad index
 * @cfg: Returned bus config
 *
 * Fills lane ordering and number of lanes; retrieves link frequency from
 * remote entity.
 *
 * Return: 0 on success or negative errno.
 */
static int cvs_csi_get_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				   struct v4l2_mbus_config *cfg)
{
	struct icvs *ctx = sd_to_csi(sd);
	s64 freq;

	cfg->type = V4L2_MBUS_CSI2_DPHY;
	for (unsigned int i = 0; i < V4L2_MBUS_CSI2_MAX_DATA_LANES; i++)
		cfg->bus.mipi_csi2.data_lanes[i] = i + 1;
	cfg->bus.mipi_csi2.num_data_lanes = ctx->nr_of_lanes;

	freq = v4l2_get_link_freq(ctx->remote, 0, 0);
	if (freq < 0)
		return -EINVAL;

	ctx->link_freq = freq;
	cfg->link_freq = freq;

	return 0;
}

static const struct v4l2_subdev_core_ops cvs_csi_subdev_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops cvs_csi_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops cvs_csi_pad_ops = {
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = cvs_csi_set_fmt,
	.get_mbus_config = cvs_csi_get_mbus_config,
	.enable_streams = cvs_csi_enable_streams,
	.disable_streams = cvs_csi_disable_streams,
};

static const struct v4l2_subdev_ops cvs_csi_subdev_ops = {
	.core = &cvs_csi_subdev_core_ops,
	.video = &cvs_csi_video_ops,
	.pad = &cvs_csi_pad_ops,
};

static const struct v4l2_subdev_internal_ops cvs_csi_internal_ops = {
	.init_state = cvs_csi_init_state,
};

static const struct media_entity_operations cvs_csi_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * Async notifier
 */
/**
 * cvs_csi_notify_bound - Remote sensor bound callback
 * @notifier: Async notifier
 * @sd: Remote subdev
 * @asc: Async match connection
 *
 * Locates the source pad of the remote sensor and creates a media link to
 * the CVS bridge sink pad enabling it by default.
 *
 * Return: 0 on success or negative errno.
 */
static int cvs_csi_notify_bound(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *sd,
				struct v4l2_async_connection *asc)
{
	struct icvs *ctx = notifier_to_csi(notifier);
	int pad;

	pad = media_entity_get_fwnode_pad(&sd->entity, asc->match.fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (pad < 0)
		return pad;

	ctx->remote = &sd->entity.pads[pad];

	return media_create_pad_link(&sd->entity, pad, &ctx->subdev.entity,
				     ICVS_CSI_PAD_SINK, MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

/**
 * cvs_csi_notify_unbind - Remote sensor unbind callback
 * @notifier: Notifier
 * @sd: Remote subdev
 * @asc: Connection
 */
static void cvs_csi_notify_unbind(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *sd,
				  struct v4l2_async_connection *asc)
{
	struct icvs *ctx = notifier_to_csi(notifier);

	ctx->remote = NULL;
}

static const struct v4l2_async_notifier_operations cvs_csi_notify_ops = {
	.bound = cvs_csi_notify_bound,
	.unbind = cvs_csi_notify_unbind,
};

/*
 * Controls
 */
/**
 * cvs_csi_init_controls - Initialize V4L2 controls
 * @ctx: CVS context
 *
 * Currently sets up a read-only privacy control placeholder.
 *
 * Return: 0 on success or negative errno.
 */
static int cvs_csi_init_controls(struct icvs *ctx)
{
	struct v4l2_ctrl *privacy_ctrl;

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, 1);

	privacy_ctrl = v4l2_ctrl_new_std(&ctx->ctrl_handler, NULL,
					 V4L2_CID_PRIVACY, 0, 1, 1, 0);
	if (privacy_ctrl)
		privacy_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	if (ctx->ctrl_handler.error) {
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		return ctx->ctrl_handler.error;
	}

	ctx->subdev.ctrl_handler = &ctx->ctrl_handler;

	return 0;
}

/*
 * Firmware (graph) parsing
 */
/**
 * cvs_csi_parse_firmware - Parse firmware (ACPI graph) endpoints
 * @ctx: CVS context
 *
 * Discovers sink and remote source endpoints, validates lane counts and
 * registers an async notifier for the remote sensor.
 *
 * Return: 0 on success or negative errno.
 */
static int cvs_csi_parse_firmware(struct icvs *ctx)
{
	struct v4l2_fwnode_endpoint ep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	struct device *dev = cvs_dev(ctx);
	struct fwnode_handle *sink_ep, *source_ep;
	struct v4l2_async_connection *asc;
	int ret;

	sink_ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 0, 0);
	v4l2_async_subdev_nf_init(&ctx->notifier, &ctx->subdev);
	ctx->notifier.ops = &cvs_csi_notify_ops;

	ret = v4l2_fwnode_endpoint_parse(sink_ep, &ep);
	if (ret)
		goto err_nf_cleanup;

	ctx->nr_of_lanes = ep.bus.mipi_csi2.num_data_lanes;
	source_ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 1, 0, 0);
	ret = v4l2_fwnode_endpoint_parse(source_ep, &ep);
	fwnode_handle_put(source_ep);
	if (ret)
		goto err_nf_cleanup;

	if (ctx->nr_of_lanes != ep.bus.mipi_csi2.num_data_lanes) {
		ret = -EINVAL;
		goto err_nf_cleanup;
	}

	asc = v4l2_async_nf_add_fwnode_remote(&ctx->notifier, sink_ep,
					      struct v4l2_async_connection);
	if (IS_ERR(asc)) {
		ret = PTR_ERR(asc);
		goto err_nf_cleanup;
	}

	ret = v4l2_async_nf_register(&ctx->notifier);
	if (ret)
		goto err_nf_cleanup;

	fwnode_handle_put(sink_ep);

	return 0;

err_nf_cleanup:
	v4l2_async_nf_cleanup(&ctx->notifier);
	fwnode_handle_put(sink_ep);

	return ret;
}

/*
 * Public CSI init/cleanup used by core probe/remove
 */
/**
 * cvs_csi_init - Initialize CSI/V4L2 sub-device side of bridge
 * @ctx: CVS context
 * @dev: Parent device
 * @i2c: I2C client (may be NULL for platform mode)
 *
 * Sets up sub-device, media entity pads, async notifier, controls and
 * registers with the V4L2 framework.
 *
 * Return: 0 on success or negative errno.
 */
int cvs_csi_init(struct icvs *ctx, struct device *dev, struct i2c_client *i2c)
{
	int ret;

	if (i2c) {
		v4l2_i2c_subdev_init(&ctx->subdev, i2c, &cvs_csi_subdev_ops);
	} else {
		v4l2_subdev_init(&ctx->subdev, &cvs_csi_subdev_ops);
		ctx->subdev.dev = dev;
	}

	ctx->subdev.internal_ops = &cvs_csi_internal_ops;
	v4l2_set_subdevdata(&ctx->subdev, ctx);
	ctx->subdev.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	ctx->subdev.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	ctx->subdev.entity.ops = &cvs_csi_entity_ops;
	snprintf(ctx->subdev.name, sizeof(ctx->subdev.name), "Intel CVS");

	ret = cvs_csi_parse_firmware(ctx);
	if (ret)
		return ret;

	ret = cvs_csi_init_controls(ctx);
	if (ret)
		goto err_nf_unreg;

	ctx->pads[ICVS_CSI_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	ctx->pads[ICVS_CSI_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&ctx->subdev.entity, ICVS_CSI_NUM_PADS,
				     ctx->pads);
	if (ret)
		goto err_ctrl_cleanup;

	ctx->subdev.state_lock = ctx->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&ctx->subdev);
	if (ret)
		goto err_entity_cleanup;

	ret = v4l2_async_register_subdev(&ctx->subdev);
	if (ret)
		goto err_entity_cleanup;

	return 0;

err_entity_cleanup:
	media_entity_cleanup(&ctx->subdev.entity);

err_ctrl_cleanup:
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);

err_nf_unreg:
	v4l2_async_nf_unregister(&ctx->notifier);
	v4l2_async_nf_cleanup(&ctx->notifier);

	return ret;
}

/**
 * cvs_csi_remove - Cleanup CSI/V4L2 sub-device
 * @ctx: CVS context
 */
void cvs_csi_remove(struct icvs *ctx)
{
	v4l2_async_nf_unregister(&ctx->notifier);
	v4l2_async_nf_cleanup(&ctx->notifier);
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_async_unregister_subdev(&ctx->subdev);
	v4l2_subdev_cleanup(&ctx->subdev);
	media_entity_cleanup(&ctx->subdev.entity);
}

MODULE_AUTHOR("Miguel Vadillo <miguel.vadillo@intel.com>");
MODULE_DESCRIPTION("CSI/V4L2 support for Intel Vision Sensing Controller");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * video stream multiplexer controlled via mux control
 *
 * Copyright (C) 2013 Pengutronix, Sascha Hauer <kernel@pengutronix.de>
 * Copyright (C) 2016-2017 Pengutronix, Philipp Zabel <kernel@pengutronix.de>
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mux/consumer.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>

struct video_mux {
	struct v4l2_subdev subdev;
	struct v4l2_async_notifier notifier;
	struct media_pad *pads;
	struct v4l2_mbus_framefmt *format_mbus;
	struct mux_control *mux;
	struct mutex lock;
	int active;
};

static const struct v4l2_mbus_framefmt video_mux_format_mbus_default = {
	.width = 1,
	.height = 1,
	.code = MEDIA_BUS_FMT_Y8_1X8,
	.field = V4L2_FIELD_NONE,
};

static inline struct video_mux *
notifier_to_video_mux(struct v4l2_async_notifier *n)
{
	return container_of(n, struct video_mux, notifier);
}

static inline struct video_mux *v4l2_subdev_to_video_mux(struct v4l2_subdev *sd)
{
	return container_of(sd, struct video_mux, subdev);
}

static int video_mux_link_setup(struct media_entity *entity,
				const struct media_pad *local,
				const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct video_mux *vmux = v4l2_subdev_to_video_mux(sd);
	u16 source_pad = entity->num_pads - 1;
	int ret = 0;

	/*
	 * The mux state is determined by the enabled sink pad link.
	 * Enabling or disabling the source pad link has no effect.
	 */
	if (local->flags & MEDIA_PAD_FL_SOURCE)
		return 0;

	dev_dbg(sd->dev, "link setup '%s':%d->'%s':%d[%d]",
		remote->entity->name, remote->index, local->entity->name,
		local->index, flags & MEDIA_LNK_FL_ENABLED);

	mutex_lock(&vmux->lock);

	if (flags & MEDIA_LNK_FL_ENABLED) {
		if (vmux->active == local->index)
			goto out;

		if (vmux->active >= 0) {
			ret = -EBUSY;
			goto out;
		}

		dev_dbg(sd->dev, "setting %d active\n", local->index);
		ret = mux_control_try_select(vmux->mux, local->index);
		if (ret < 0)
			goto out;
		vmux->active = local->index;

		/* Propagate the active format to the source */
		vmux->format_mbus[source_pad] = vmux->format_mbus[vmux->active];
	} else {
		if (vmux->active != local->index)
			goto out;

		dev_dbg(sd->dev, "going inactive\n");
		mux_control_deselect(vmux->mux);
		vmux->active = -1;
	}

out:
	mutex_unlock(&vmux->lock);
	return ret;
}

static const struct media_entity_operations video_mux_ops = {
	.link_setup = video_mux_link_setup,
	.link_validate = v4l2_subdev_link_validate,
	.get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,
};

static int video_mux_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct video_mux *vmux = v4l2_subdev_to_video_mux(sd);
	struct v4l2_subdev *upstream_sd;
	struct media_pad *pad;

	if (vmux->active == -1) {
		dev_err(sd->dev, "Can not start streaming on inactive mux\n");
		return -EINVAL;
	}

	pad = media_entity_remote_pad(&sd->entity.pads[vmux->active]);
	if (!pad) {
		dev_err(sd->dev, "Failed to find remote source pad\n");
		return -ENOLINK;
	}

	if (!is_media_entity_v4l2_subdev(pad->entity)) {
		dev_err(sd->dev, "Upstream entity is not a v4l2 subdev\n");
		return -ENODEV;
	}

	upstream_sd = media_entity_to_v4l2_subdev(pad->entity);

	return v4l2_subdev_call(upstream_sd, video, s_stream, enable);
}

static const struct v4l2_subdev_video_ops video_mux_subdev_video_ops = {
	.s_stream = video_mux_s_stream,
};

static struct v4l2_mbus_framefmt *
__video_mux_get_pad_format(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   unsigned int pad, u32 which)
{
	struct video_mux *vmux = v4l2_subdev_to_video_mux(sd);

	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(sd, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &vmux->format_mbus[pad];
	default:
		return NULL;
	}
}

static int video_mux_get_format(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *sdformat)
{
	struct video_mux *vmux = v4l2_subdev_to_video_mux(sd);

	mutex_lock(&vmux->lock);

	sdformat->format = *__video_mux_get_pad_format(sd, cfg, sdformat->pad,
						       sdformat->which);

	mutex_unlock(&vmux->lock);

	return 0;
}

static int video_mux_set_format(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *sdformat)
{
	struct video_mux *vmux = v4l2_subdev_to_video_mux(sd);
	struct v4l2_mbus_framefmt *mbusformat, *source_mbusformat;
	struct media_pad *pad = &vmux->pads[sdformat->pad];
	u16 source_pad = sd->entity.num_pads - 1;

	mbusformat = __video_mux_get_pad_format(sd, cfg, sdformat->pad,
					    sdformat->which);
	if (!mbusformat)
		return -EINVAL;

	source_mbusformat = __video_mux_get_pad_format(sd, cfg, source_pad,
						       sdformat->which);
	if (!source_mbusformat)
		return -EINVAL;

	/* No size limitations except V4L2 compliance requirements */
	v4l_bound_align_image(&sdformat->format.width, 1, 65536, 0,
			      &sdformat->format.height, 1, 65536, 0, 0);

	/* All formats except LVDS and vendor specific formats are acceptable */
	switch (sdformat->format.code) {
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
		sdformat->format.code = MEDIA_BUS_FMT_Y8_1X8;
		break;
	}
	if (sdformat->format.field == V4L2_FIELD_ANY)
		sdformat->format.field = V4L2_FIELD_NONE;

	mutex_lock(&vmux->lock);

	/* Source pad mirrors active sink pad, no limitations on sink pads */
	if ((pad->flags & MEDIA_PAD_FL_SOURCE) && vmux->active >= 0)
		sdformat->format = vmux->format_mbus[vmux->active];

	*mbusformat = sdformat->format;

	/* Propagate the format from an active sink to source */
	if ((pad->flags & MEDIA_PAD_FL_SINK) && (pad->index == vmux->active))
		*source_mbusformat = sdformat->format;

	mutex_unlock(&vmux->lock);

	return 0;
}

static int video_mux_init_cfg(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg)
{
	struct video_mux *vmux = v4l2_subdev_to_video_mux(sd);
	struct v4l2_mbus_framefmt *mbusformat;
	unsigned int i;

	mutex_lock(&vmux->lock);

	for (i = 0; i < sd->entity.num_pads; i++) {
		mbusformat = v4l2_subdev_get_try_format(sd, cfg, i);
		*mbusformat = video_mux_format_mbus_default;
	}

	mutex_unlock(&vmux->lock);

	return 0;
}

static const struct v4l2_subdev_pad_ops video_mux_pad_ops = {
	.init_cfg = video_mux_init_cfg,
	.get_fmt = video_mux_get_format,
	.set_fmt = video_mux_set_format,
};

static const struct v4l2_subdev_ops video_mux_subdev_ops = {
	.pad = &video_mux_pad_ops,
	.video = &video_mux_subdev_video_ops,
};

static int video_mux_notify_bound(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *sd,
				  struct v4l2_async_subdev *asd)
{
	struct video_mux *vmux = notifier_to_video_mux(notifier);

	return v4l2_create_fwnode_links(sd, &vmux->subdev);
}

static const struct v4l2_async_notifier_operations video_mux_notify_ops = {
	.bound = video_mux_notify_bound,
};

static int video_mux_async_register(struct video_mux *vmux,
				    unsigned int num_input_pads)
{
	unsigned int i;
	int ret;

	v4l2_async_notifier_init(&vmux->notifier);

	for (i = 0; i < num_input_pads; i++) {
		struct v4l2_async_subdev *asd;
		struct fwnode_handle *ep;

		ep = fwnode_graph_get_endpoint_by_id(
			dev_fwnode(vmux->subdev.dev), i, 0,
			FWNODE_GRAPH_ENDPOINT_NEXT);
		if (!ep)
			continue;

		asd = kzalloc(sizeof(*asd), GFP_KERNEL);
		if (!asd) {
			fwnode_handle_put(ep);
			return -ENOMEM;
		}

		ret = v4l2_async_notifier_add_fwnode_remote_subdev(
			&vmux->notifier, ep, asd);

		fwnode_handle_put(ep);

		if (ret) {
			kfree(asd);
			/* OK if asd already exists */
			if (ret != -EEXIST)
				return ret;
		}
	}

	vmux->notifier.ops = &video_mux_notify_ops;

	ret = v4l2_async_subdev_notifier_register(&vmux->subdev,
						  &vmux->notifier);
	if (ret)
		return ret;

	return v4l2_async_register_subdev(&vmux->subdev);
}

static int video_mux_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct device_node *ep;
	struct video_mux *vmux;
	unsigned int num_pads = 0;
	unsigned int i;
	int ret;

	vmux = devm_kzalloc(dev, sizeof(*vmux), GFP_KERNEL);
	if (!vmux)
		return -ENOMEM;

	platform_set_drvdata(pdev, vmux);

	v4l2_subdev_init(&vmux->subdev, &video_mux_subdev_ops);
	snprintf(vmux->subdev.name, sizeof(vmux->subdev.name), "%pOFn", np);
	vmux->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	vmux->subdev.dev = dev;

	/*
	 * The largest numbered port is the output port. It determines
	 * total number of pads.
	 */
	for_each_endpoint_of_node(np, ep) {
		struct of_endpoint endpoint;

		of_graph_parse_endpoint(ep, &endpoint);
		num_pads = max(num_pads, endpoint.port + 1);
	}

	if (num_pads < 2) {
		dev_err(dev, "Not enough ports %d\n", num_pads);
		return -EINVAL;
	}

	vmux->mux = devm_mux_control_get(dev, NULL);
	if (IS_ERR(vmux->mux)) {
		ret = PTR_ERR(vmux->mux);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get mux: %d\n", ret);
		return ret;
	}

	mutex_init(&vmux->lock);
	vmux->active = -1;
	vmux->pads = devm_kcalloc(dev, num_pads, sizeof(*vmux->pads),
				  GFP_KERNEL);
	if (!vmux->pads)
		return -ENOMEM;

	vmux->format_mbus = devm_kcalloc(dev, num_pads,
					 sizeof(*vmux->format_mbus),
					 GFP_KERNEL);
	if (!vmux->format_mbus)
		return -ENOMEM;

	for (i = 0; i < num_pads; i++) {
		vmux->pads[i].flags = (i < num_pads - 1) ? MEDIA_PAD_FL_SINK
							 : MEDIA_PAD_FL_SOURCE;
		vmux->format_mbus[i] = video_mux_format_mbus_default;
	}

	vmux->subdev.entity.function = MEDIA_ENT_F_VID_MUX;
	ret = media_entity_pads_init(&vmux->subdev.entity, num_pads,
				     vmux->pads);
	if (ret < 0)
		return ret;

	vmux->subdev.entity.ops = &video_mux_ops;

	ret = video_mux_async_register(vmux, num_pads - 1);
	if (ret) {
		v4l2_async_notifier_unregister(&vmux->notifier);
		v4l2_async_notifier_cleanup(&vmux->notifier);
	}

	return ret;
}

static int video_mux_remove(struct platform_device *pdev)
{
	struct video_mux *vmux = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd = &vmux->subdev;

	v4l2_async_notifier_unregister(&vmux->notifier);
	v4l2_async_notifier_cleanup(&vmux->notifier);
	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);

	return 0;
}

static const struct of_device_id video_mux_dt_ids[] = {
	{ .compatible = "video-mux", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, video_mux_dt_ids);

static struct platform_driver video_mux_driver = {
	.probe		= video_mux_probe,
	.remove		= video_mux_remove,
	.driver		= {
		.of_match_table = video_mux_dt_ids,
		.name = "video-mux",
	},
};

module_platform_driver(video_mux_driver);

MODULE_DESCRIPTION("video stream multiplexer");
MODULE_AUTHOR("Sascha Hauer, Pengutronix");
MODULE_AUTHOR("Philipp Zabel, Pengutronix");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#include "sun6i_csi.h"
#include "sun6i_csi_bridge.h"

/* Helpers */

void sun6i_csi_bridge_dimensions(struct sun6i_csi_device *csi_dev,
				 unsigned int *width, unsigned int *height)
{
	if (width)
		*width = csi_dev->bridge.mbus_format.width;
	if (height)
		*height = csi_dev->bridge.mbus_format.height;
}

void sun6i_csi_bridge_format(struct sun6i_csi_device *csi_dev,
			     u32 *mbus_code, u32 *field)
{
	if (mbus_code)
		*mbus_code = csi_dev->bridge.mbus_format.code;
	if (field)
		*field = csi_dev->bridge.mbus_format.field;
}

/* Format */

static const u32 sun6i_csi_bridge_mbus_codes[] = {
	/* Bayer */
	MEDIA_BUS_FMT_SBGGR8_1X8,
	MEDIA_BUS_FMT_SGBRG8_1X8,
	MEDIA_BUS_FMT_SGRBG8_1X8,
	MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SBGGR12_1X12,
	MEDIA_BUS_FMT_SGBRG12_1X12,
	MEDIA_BUS_FMT_SGRBG12_1X12,
	MEDIA_BUS_FMT_SRGGB12_1X12,
	/* RGB */
	MEDIA_BUS_FMT_RGB565_2X8_LE,
	MEDIA_BUS_FMT_RGB565_2X8_BE,
	/* YUV422 */
	MEDIA_BUS_FMT_YUYV8_2X8,
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_YVYU8_2X8,
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_VYUY8_2X8,
	MEDIA_BUS_FMT_YUYV8_1X16,
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_YVYU8_1X16,
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_VYUY8_1X16,
	/* Compressed */
	MEDIA_BUS_FMT_JPEG_1X8,
};

static bool sun6i_csi_bridge_mbus_code_check(u32 mbus_code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sun6i_csi_bridge_mbus_codes); i++)
		if (sun6i_csi_bridge_mbus_codes[i] == mbus_code)
			return true;

	return false;
}

/* V4L2 Subdev */

static int sun6i_csi_bridge_s_stream(struct v4l2_subdev *subdev, int on)
{
	struct sun6i_csi_device *csi_dev = v4l2_get_subdevdata(subdev);
	struct sun6i_csi_bridge *bridge = &csi_dev->bridge;
	struct media_pad *local_pad = &bridge->pads[SUN6I_CSI_BRIDGE_PAD_SINK];
	struct device *dev = csi_dev->dev;
	struct v4l2_subdev *source_subdev;
	struct media_pad *remote_pad;
	/* Initialize to 0 to use both in disable label (ret != 0) and off. */
	int ret = 0;

	/* Source */

	remote_pad = media_pad_remote_pad_unique(local_pad);
	if (IS_ERR(remote_pad)) {
		dev_err(dev,
			"zero or more than a single source connected to the bridge\n");
		return PTR_ERR(remote_pad);
	}

	source_subdev = media_entity_to_v4l2_subdev(remote_pad->entity);

	if (!on) {
		v4l2_subdev_call(source_subdev, video, s_stream, 0);
		goto disable;
	}

	ret = v4l2_subdev_call(source_subdev, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD)
		goto disable;

	return 0;

disable:

	return ret;
}

static const struct v4l2_subdev_video_ops sun6i_csi_bridge_video_ops = {
	.s_stream	= sun6i_csi_bridge_s_stream,
};

static void
sun6i_csi_bridge_mbus_format_prepare(struct v4l2_mbus_framefmt *mbus_format)
{
	if (!sun6i_csi_bridge_mbus_code_check(mbus_format->code))
		mbus_format->code = sun6i_csi_bridge_mbus_codes[0];

	mbus_format->field = V4L2_FIELD_NONE;
	mbus_format->colorspace = V4L2_COLORSPACE_RAW;
	mbus_format->quantization = V4L2_QUANTIZATION_DEFAULT;
	mbus_format->xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static int sun6i_csi_bridge_init_cfg(struct v4l2_subdev *subdev,
				     struct v4l2_subdev_state *state)
{
	struct sun6i_csi_device *csi_dev = v4l2_get_subdevdata(subdev);
	unsigned int pad = SUN6I_CSI_BRIDGE_PAD_SINK;
	struct v4l2_mbus_framefmt *mbus_format =
		v4l2_subdev_get_try_format(subdev, state, pad);
	struct mutex *lock = &csi_dev->bridge.lock;

	mutex_lock(lock);

	mbus_format->code = sun6i_csi_bridge_mbus_codes[0];
	mbus_format->width = 1280;
	mbus_format->height = 720;

	sun6i_csi_bridge_mbus_format_prepare(mbus_format);

	mutex_unlock(lock);

	return 0;
}

static int
sun6i_csi_bridge_enum_mbus_code(struct v4l2_subdev *subdev,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_mbus_code_enum *code_enum)
{
	if (code_enum->index >= ARRAY_SIZE(sun6i_csi_bridge_mbus_codes))
		return -EINVAL;

	code_enum->code = sun6i_csi_bridge_mbus_codes[code_enum->index];

	return 0;
}

static int sun6i_csi_bridge_get_fmt(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_format *format)
{
	struct sun6i_csi_device *csi_dev = v4l2_get_subdevdata(subdev);
	struct v4l2_mbus_framefmt *mbus_format = &format->format;
	struct mutex *lock = &csi_dev->bridge.lock;

	mutex_lock(lock);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		*mbus_format = *v4l2_subdev_get_try_format(subdev, state,
							   format->pad);
	else
		*mbus_format = csi_dev->bridge.mbus_format;

	mutex_unlock(lock);

	return 0;
}

static int sun6i_csi_bridge_set_fmt(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_format *format)
{
	struct sun6i_csi_device *csi_dev = v4l2_get_subdevdata(subdev);
	struct v4l2_mbus_framefmt *mbus_format = &format->format;
	struct mutex *lock = &csi_dev->bridge.lock;

	mutex_lock(lock);

	sun6i_csi_bridge_mbus_format_prepare(mbus_format);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		*v4l2_subdev_get_try_format(subdev, state, format->pad) =
			*mbus_format;
	else
		csi_dev->bridge.mbus_format = *mbus_format;

	mutex_unlock(lock);

	return 0;
}

static const struct v4l2_subdev_pad_ops sun6i_csi_bridge_pad_ops = {
	.init_cfg	= sun6i_csi_bridge_init_cfg,
	.enum_mbus_code	= sun6i_csi_bridge_enum_mbus_code,
	.get_fmt	= sun6i_csi_bridge_get_fmt,
	.set_fmt	= sun6i_csi_bridge_set_fmt,
};

const struct v4l2_subdev_ops sun6i_csi_bridge_subdev_ops = {
	.video	= &sun6i_csi_bridge_video_ops,
	.pad	= &sun6i_csi_bridge_pad_ops,
};

/* Media Entity */

static const struct media_entity_operations sun6i_csi_bridge_entity_ops = {
	.link_validate	= v4l2_subdev_link_validate,
};

/* V4L2 Async */

static int sun6i_csi_bridge_link(struct sun6i_csi_device *csi_dev,
				 int sink_pad_index,
				 struct v4l2_subdev *remote_subdev,
				 bool enabled)
{
	struct device *dev = csi_dev->dev;
	struct v4l2_subdev *subdev = &csi_dev->bridge.subdev;
	struct media_entity *sink_entity = &subdev->entity;
	struct media_entity *source_entity = &remote_subdev->entity;
	int source_pad_index;
	int ret;

	/* Get the first remote source pad. */
	ret = media_entity_get_fwnode_pad(source_entity, remote_subdev->fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (ret < 0) {
		dev_err(dev, "missing source pad in external entity %s\n",
			source_entity->name);
		return -EINVAL;
	}

	source_pad_index = ret;

	dev_dbg(dev, "creating %s:%u -> %s:%u link\n", source_entity->name,
		source_pad_index, sink_entity->name, sink_pad_index);

	ret = media_create_pad_link(source_entity, source_pad_index,
				    sink_entity, sink_pad_index,
				    enabled ? MEDIA_LNK_FL_ENABLED : 0);
	if (ret < 0) {
		dev_err(dev, "failed to create %s:%u -> %s:%u link\n",
			source_entity->name, source_pad_index,
			sink_entity->name, sink_pad_index);
		return ret;
	}

	return 0;
}

static int
sun6i_csi_bridge_notifier_bound(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *remote_subdev,
				struct v4l2_async_subdev *async_subdev)
{
	struct sun6i_csi_device *csi_dev =
		container_of(notifier, struct sun6i_csi_device,
			     bridge.notifier);
	struct sun6i_csi_bridge_async_subdev *bridge_async_subdev =
		container_of(async_subdev, struct sun6i_csi_bridge_async_subdev,
			     async_subdev);
	struct sun6i_csi_bridge_source *source = bridge_async_subdev->source;
	bool enabled;

	switch (source->endpoint.base.port) {
	case SUN6I_CSI_PORT_PARALLEL:
		enabled = true;
		break;
	default:
		break;
	}

	source->subdev = remote_subdev;

	return sun6i_csi_bridge_link(csi_dev, SUN6I_CSI_BRIDGE_PAD_SINK,
				     remote_subdev, enabled);
}

static int
sun6i_csi_bridge_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct sun6i_csi_device *csi_dev =
		container_of(notifier, struct sun6i_csi_device,
			     bridge.notifier);
	struct v4l2_device *v4l2_dev = &csi_dev->v4l2.v4l2_dev;

	return v4l2_device_register_subdev_nodes(v4l2_dev);
}

static const struct v4l2_async_notifier_operations
sun6i_csi_bridge_notifier_ops = {
	.bound		= sun6i_csi_bridge_notifier_bound,
	.complete	= sun6i_csi_bridge_notifier_complete,
};

/* Bridge */

static int sun6i_csi_bridge_source_setup(struct sun6i_csi_device *csi_dev,
					 struct sun6i_csi_bridge_source *source,
					 u32 port,
					 enum v4l2_mbus_type *bus_types)
{
	struct device *dev = csi_dev->dev;
	struct v4l2_async_notifier *notifier = &csi_dev->bridge.notifier;
	struct v4l2_fwnode_endpoint *endpoint = &source->endpoint;
	struct sun6i_csi_bridge_async_subdev *bridge_async_subdev;
	struct fwnode_handle *handle;
	int ret;

	handle = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), port, 0, 0);
	if (!handle)
		return -ENODEV;

	ret = v4l2_fwnode_endpoint_parse(handle, endpoint);
	if (ret)
		goto complete;

	if (bus_types) {
		bool valid = false;
		unsigned int i;

		for (i = 0; bus_types[i] != V4L2_MBUS_INVALID; i++) {
			if (endpoint->bus_type == bus_types[i]) {
				valid = true;
				break;
			}
		}

		if (!valid) {
			dev_err(dev, "unsupported bus type for port %d\n",
				port);
			ret = -EINVAL;
			goto complete;
		}
	}

	bridge_async_subdev =
		v4l2_async_nf_add_fwnode_remote(notifier, handle,
						struct
						sun6i_csi_bridge_async_subdev);
	if (IS_ERR(bridge_async_subdev)) {
		ret = PTR_ERR(bridge_async_subdev);
		goto complete;
	}

	bridge_async_subdev->source = source;

	source->expected = true;

complete:
	fwnode_handle_put(handle);

	return ret;
}

int sun6i_csi_bridge_setup(struct sun6i_csi_device *csi_dev)
{
	struct device *dev = csi_dev->dev;
	struct sun6i_csi_bridge *bridge = &csi_dev->bridge;
	struct v4l2_device *v4l2_dev = &csi_dev->v4l2.v4l2_dev;
	struct v4l2_subdev *subdev = &bridge->subdev;
	struct v4l2_async_notifier *notifier = &bridge->notifier;
	struct media_pad *pads = bridge->pads;
	enum v4l2_mbus_type parallel_mbus_types[] = {
		V4L2_MBUS_PARALLEL,
		V4L2_MBUS_BT656,
		V4L2_MBUS_INVALID
	};
	int ret;

	mutex_init(&bridge->lock);

	/* V4L2 Subdev */

	v4l2_subdev_init(subdev, &sun6i_csi_bridge_subdev_ops);
	strscpy(subdev->name, SUN6I_CSI_BRIDGE_NAME, sizeof(subdev->name));
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	subdev->owner = THIS_MODULE;
	subdev->dev = dev;

	v4l2_set_subdevdata(subdev, csi_dev);

	/* Media Entity */

	subdev->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	subdev->entity.ops = &sun6i_csi_bridge_entity_ops;

	/* Media Pads */

	pads[SUN6I_CSI_BRIDGE_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[SUN6I_CSI_BRIDGE_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE |
						  MEDIA_PAD_FL_MUST_CONNECT;

	ret = media_entity_pads_init(&subdev->entity,
				     SUN6I_CSI_BRIDGE_PAD_COUNT, pads);
	if (ret < 0)
		return ret;

	/* V4L2 Subdev */

	ret = v4l2_device_register_subdev(v4l2_dev, subdev);
	if (ret) {
		dev_err(dev, "failed to register v4l2 subdev: %d\n", ret);
		goto error_media_entity;
	}

	/* V4L2 Async */

	v4l2_async_nf_init(notifier);
	notifier->ops = &sun6i_csi_bridge_notifier_ops;

	sun6i_csi_bridge_source_setup(csi_dev, &bridge->source_parallel,
				      SUN6I_CSI_PORT_PARALLEL,
				      parallel_mbus_types);

	ret = v4l2_async_nf_register(v4l2_dev, notifier);
	if (ret) {
		dev_err(dev, "failed to register v4l2 async notifier: %d\n",
			ret);
		goto error_v4l2_async_notifier;
	}

	return 0;

error_v4l2_async_notifier:
	v4l2_async_nf_cleanup(notifier);

	v4l2_device_unregister_subdev(subdev);

error_media_entity:
	media_entity_cleanup(&subdev->entity);

	return ret;
}

void sun6i_csi_bridge_cleanup(struct sun6i_csi_device *csi_dev)
{
	struct v4l2_subdev *subdev = &csi_dev->bridge.subdev;
	struct v4l2_async_notifier *notifier = &csi_dev->bridge.notifier;

	v4l2_async_nf_unregister(notifier);
	v4l2_async_nf_cleanup(notifier);

	v4l2_device_unregister_subdev(subdev);

	media_entity_cleanup(&subdev->entity);
}

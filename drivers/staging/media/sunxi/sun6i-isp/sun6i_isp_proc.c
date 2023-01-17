// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#include "sun6i_isp.h"
#include "sun6i_isp_capture.h"
#include "sun6i_isp_params.h"
#include "sun6i_isp_proc.h"
#include "sun6i_isp_reg.h"

/* Helpers */

void sun6i_isp_proc_dimensions(struct sun6i_isp_device *isp_dev,
			       unsigned int *width, unsigned int *height)
{
	if (width)
		*width = isp_dev->proc.mbus_format.width;
	if (height)
		*height = isp_dev->proc.mbus_format.height;
}

/* Format */

static const struct sun6i_isp_proc_format sun6i_isp_proc_formats[] = {
	{
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
		.input_format	= SUN6I_ISP_INPUT_FMT_RAW_BGGR,
	},
	{
		.mbus_code	= MEDIA_BUS_FMT_SGBRG8_1X8,
		.input_format	= SUN6I_ISP_INPUT_FMT_RAW_GBRG,
	},
	{
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.input_format	= SUN6I_ISP_INPUT_FMT_RAW_GRBG,
	},
	{
		.mbus_code	= MEDIA_BUS_FMT_SRGGB8_1X8,
		.input_format	= SUN6I_ISP_INPUT_FMT_RAW_RGGB,
	},

	{
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.input_format	= SUN6I_ISP_INPUT_FMT_RAW_BGGR,
	},
	{
		.mbus_code	= MEDIA_BUS_FMT_SGBRG10_1X10,
		.input_format	= SUN6I_ISP_INPUT_FMT_RAW_GBRG,
	},
	{
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
		.input_format	= SUN6I_ISP_INPUT_FMT_RAW_GRBG,
	},
	{
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.input_format	= SUN6I_ISP_INPUT_FMT_RAW_RGGB,
	},
};

const struct sun6i_isp_proc_format *sun6i_isp_proc_format_find(u32 mbus_code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sun6i_isp_proc_formats); i++)
		if (sun6i_isp_proc_formats[i].mbus_code == mbus_code)
			return &sun6i_isp_proc_formats[i];

	return NULL;
}

/* Processor */

static void sun6i_isp_proc_irq_enable(struct sun6i_isp_device *isp_dev)
{
	struct regmap *regmap = isp_dev->regmap;

	regmap_write(regmap, SUN6I_ISP_FE_INT_EN_REG,
		     SUN6I_ISP_FE_INT_EN_FINISH |
		     SUN6I_ISP_FE_INT_EN_START |
		     SUN6I_ISP_FE_INT_EN_PARA_SAVE |
		     SUN6I_ISP_FE_INT_EN_PARA_LOAD |
		     SUN6I_ISP_FE_INT_EN_SRC0_FIFO |
		     SUN6I_ISP_FE_INT_EN_ROT_FINISH);
}

static void sun6i_isp_proc_irq_disable(struct sun6i_isp_device *isp_dev)
{
	struct regmap *regmap = isp_dev->regmap;

	regmap_write(regmap, SUN6I_ISP_FE_INT_EN_REG, 0);
}

static void sun6i_isp_proc_irq_clear(struct sun6i_isp_device *isp_dev)
{
	struct regmap *regmap = isp_dev->regmap;

	regmap_write(regmap, SUN6I_ISP_FE_INT_EN_REG, 0);
	regmap_write(regmap, SUN6I_ISP_FE_INT_STA_REG,
		     SUN6I_ISP_FE_INT_STA_CLEAR);
}

static void sun6i_isp_proc_enable(struct sun6i_isp_device *isp_dev,
				  struct sun6i_isp_proc_source *source)
{
	struct sun6i_isp_proc *proc = &isp_dev->proc;
	struct regmap *regmap = isp_dev->regmap;
	u8 mode;

	/* Frontend */

	if (source == &proc->source_csi0)
		mode = SUN6I_ISP_SRC_MODE_CSI(0);
	else
		mode = SUN6I_ISP_SRC_MODE_CSI(1);

	regmap_write(regmap, SUN6I_ISP_FE_CFG_REG,
		     SUN6I_ISP_FE_CFG_EN | SUN6I_ISP_FE_CFG_SRC0_MODE(mode));

	regmap_write(regmap, SUN6I_ISP_FE_CTRL_REG,
		     SUN6I_ISP_FE_CTRL_VCAP_EN | SUN6I_ISP_FE_CTRL_PARA_READY);
}

static void sun6i_isp_proc_disable(struct sun6i_isp_device *isp_dev)
{
	struct regmap *regmap = isp_dev->regmap;

	/* Frontend */

	regmap_write(regmap, SUN6I_ISP_FE_CTRL_REG, 0);
	regmap_write(regmap, SUN6I_ISP_FE_CFG_REG, 0);
}

static void sun6i_isp_proc_configure(struct sun6i_isp_device *isp_dev)
{
	struct v4l2_mbus_framefmt *mbus_format = &isp_dev->proc.mbus_format;
	const struct sun6i_isp_proc_format *format;
	u32 value;

	/* Module */

	value = sun6i_isp_load_read(isp_dev, SUN6I_ISP_MODULE_EN_REG);
	value |= SUN6I_ISP_MODULE_EN_SRC0;
	sun6i_isp_load_write(isp_dev, SUN6I_ISP_MODULE_EN_REG, value);

	/* Input */

	format = sun6i_isp_proc_format_find(mbus_format->code);
	if (WARN_ON(!format))
		return;

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_MODE_REG,
			     SUN6I_ISP_MODE_INPUT_FMT(format->input_format) |
			     SUN6I_ISP_MODE_INPUT_YUV_SEQ(format->input_yuv_seq) |
			     SUN6I_ISP_MODE_SHARP(1) |
			     SUN6I_ISP_MODE_HIST(2));
}

/* V4L2 Subdev */

static int sun6i_isp_proc_s_stream(struct v4l2_subdev *subdev, int on)
{
	struct sun6i_isp_device *isp_dev = v4l2_get_subdevdata(subdev);
	struct sun6i_isp_proc *proc = &isp_dev->proc;
	struct media_pad *local_pad = &proc->pads[SUN6I_ISP_PROC_PAD_SINK_CSI];
	struct device *dev = isp_dev->dev;
	struct sun6i_isp_proc_source *source;
	struct v4l2_subdev *source_subdev;
	struct media_pad *remote_pad;
	int ret;

	/* Source */

	remote_pad = media_pad_remote_pad_unique(local_pad);
	if (IS_ERR(remote_pad)) {
		dev_err(dev,
			"zero or more than a single source connected to the bridge\n");
		return PTR_ERR(remote_pad);
	}

	source_subdev = media_entity_to_v4l2_subdev(remote_pad->entity);

	if (source_subdev == proc->source_csi0.subdev)
		source = &proc->source_csi0;
	else
		source = &proc->source_csi1;

	if (!on) {
		sun6i_isp_proc_irq_disable(isp_dev);
		v4l2_subdev_call(source_subdev, video, s_stream, 0);
		ret = 0;
		goto disable;
	}

	/* PM */

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	/* Clear */

	sun6i_isp_proc_irq_clear(isp_dev);

	/* Configure */

	sun6i_isp_tables_configure(isp_dev);
	sun6i_isp_params_configure(isp_dev);
	sun6i_isp_proc_configure(isp_dev);
	sun6i_isp_capture_configure(isp_dev);

	/* State Update */

	sun6i_isp_state_update(isp_dev, true);

	/* Enable */

	sun6i_isp_proc_irq_enable(isp_dev);
	sun6i_isp_proc_enable(isp_dev, source);

	ret = v4l2_subdev_call(source_subdev, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD) {
		sun6i_isp_proc_irq_disable(isp_dev);
		goto disable;
	}

	return 0;

disable:
	sun6i_isp_proc_disable(isp_dev);

	pm_runtime_put(dev);

	return ret;
}

static const struct v4l2_subdev_video_ops sun6i_isp_proc_video_ops = {
	.s_stream	= sun6i_isp_proc_s_stream,
};

static void
sun6i_isp_proc_mbus_format_prepare(struct v4l2_mbus_framefmt *mbus_format)
{
	if (!sun6i_isp_proc_format_find(mbus_format->code))
		mbus_format->code = sun6i_isp_proc_formats[0].mbus_code;

	mbus_format->field = V4L2_FIELD_NONE;
	mbus_format->colorspace = V4L2_COLORSPACE_RAW;
	mbus_format->quantization = V4L2_QUANTIZATION_DEFAULT;
	mbus_format->xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static int sun6i_isp_proc_init_cfg(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_state *state)
{
	struct sun6i_isp_device *isp_dev = v4l2_get_subdevdata(subdev);
	unsigned int pad = SUN6I_ISP_PROC_PAD_SINK_CSI;
	struct v4l2_mbus_framefmt *mbus_format =
		v4l2_subdev_get_try_format(subdev, state, pad);
	struct mutex *lock = &isp_dev->proc.lock;

	mutex_lock(lock);

	mbus_format->code = sun6i_isp_proc_formats[0].mbus_code;
	mbus_format->width = 1280;
	mbus_format->height = 720;

	sun6i_isp_proc_mbus_format_prepare(mbus_format);

	mutex_unlock(lock);

	return 0;
}

static int
sun6i_isp_proc_enum_mbus_code(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_mbus_code_enum *code_enum)
{
	if (code_enum->index >= ARRAY_SIZE(sun6i_isp_proc_formats))
		return -EINVAL;

	code_enum->code = sun6i_isp_proc_formats[code_enum->index].mbus_code;

	return 0;
}

static int sun6i_isp_proc_get_fmt(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_format *format)
{
	struct sun6i_isp_device *isp_dev = v4l2_get_subdevdata(subdev);
	struct v4l2_mbus_framefmt *mbus_format = &format->format;
	struct mutex *lock = &isp_dev->proc.lock;

	mutex_lock(lock);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		*mbus_format = *v4l2_subdev_get_try_format(subdev, state,
							   format->pad);
	else
		*mbus_format = isp_dev->proc.mbus_format;

	mutex_unlock(lock);

	return 0;
}

static int sun6i_isp_proc_set_fmt(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_format *format)
{
	struct sun6i_isp_device *isp_dev = v4l2_get_subdevdata(subdev);
	struct v4l2_mbus_framefmt *mbus_format = &format->format;
	struct mutex *lock = &isp_dev->proc.lock;

	mutex_lock(lock);

	sun6i_isp_proc_mbus_format_prepare(mbus_format);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		*v4l2_subdev_get_try_format(subdev, state, format->pad) =
			*mbus_format;
	else
		isp_dev->proc.mbus_format = *mbus_format;

	mutex_unlock(lock);

	return 0;
}

static const struct v4l2_subdev_pad_ops sun6i_isp_proc_pad_ops = {
	.init_cfg	= sun6i_isp_proc_init_cfg,
	.enum_mbus_code	= sun6i_isp_proc_enum_mbus_code,
	.get_fmt	= sun6i_isp_proc_get_fmt,
	.set_fmt	= sun6i_isp_proc_set_fmt,
};

static const struct v4l2_subdev_ops sun6i_isp_proc_subdev_ops = {
	.video	= &sun6i_isp_proc_video_ops,
	.pad	= &sun6i_isp_proc_pad_ops,
};

/* Media Entity */

static const struct media_entity_operations sun6i_isp_proc_entity_ops = {
	.link_validate	= v4l2_subdev_link_validate,
};

/* V4L2 Async */

static int sun6i_isp_proc_link(struct sun6i_isp_device *isp_dev,
			       int sink_pad_index,
			       struct v4l2_subdev *remote_subdev, bool enabled)
{
	struct device *dev = isp_dev->dev;
	struct v4l2_subdev *subdev = &isp_dev->proc.subdev;
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

static int sun6i_isp_proc_notifier_bound(struct v4l2_async_notifier *notifier,
					 struct v4l2_subdev *remote_subdev,
					 struct v4l2_async_subdev *async_subdev)
{
	struct sun6i_isp_device *isp_dev =
		container_of(notifier, struct sun6i_isp_device, proc.notifier);
	struct sun6i_isp_proc_async_subdev *proc_async_subdev =
		container_of(async_subdev, struct sun6i_isp_proc_async_subdev,
			     async_subdev);
	struct sun6i_isp_proc *proc = &isp_dev->proc;
	struct sun6i_isp_proc_source *source = proc_async_subdev->source;
	bool enabled;

	switch (source->endpoint.base.port) {
	case SUN6I_ISP_PORT_CSI0:
		source = &proc->source_csi0;
		enabled = true;
		break;
	case SUN6I_ISP_PORT_CSI1:
		source = &proc->source_csi1;
		enabled = !proc->source_csi0.expected;
		break;
	default:
		return -EINVAL;
	}

	source->subdev = remote_subdev;

	return sun6i_isp_proc_link(isp_dev, SUN6I_ISP_PROC_PAD_SINK_CSI,
				   remote_subdev, enabled);
}

static int
sun6i_isp_proc_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct sun6i_isp_device *isp_dev =
		container_of(notifier, struct sun6i_isp_device, proc.notifier);
	struct v4l2_device *v4l2_dev = &isp_dev->v4l2.v4l2_dev;
	int ret;

	ret = v4l2_device_register_subdev_nodes(v4l2_dev);
	if (ret)
		return ret;

	return 0;
}

static const struct v4l2_async_notifier_operations
sun6i_isp_proc_notifier_ops = {
	.bound		= sun6i_isp_proc_notifier_bound,
	.complete	= sun6i_isp_proc_notifier_complete,
};

/* Processor */

static int sun6i_isp_proc_source_setup(struct sun6i_isp_device *isp_dev,
				       struct sun6i_isp_proc_source *source,
				       u32 port)
{
	struct device *dev = isp_dev->dev;
	struct v4l2_async_notifier *notifier = &isp_dev->proc.notifier;
	struct v4l2_fwnode_endpoint *endpoint = &source->endpoint;
	struct sun6i_isp_proc_async_subdev *proc_async_subdev;
	struct fwnode_handle *handle = NULL;
	int ret;

	handle = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), port, 0, 0);
	if (!handle)
		return -ENODEV;

	ret = v4l2_fwnode_endpoint_parse(handle, endpoint);
	if (ret)
		goto complete;

	proc_async_subdev =
		v4l2_async_nf_add_fwnode_remote(notifier, handle,
						struct
						sun6i_isp_proc_async_subdev);
	if (IS_ERR(proc_async_subdev)) {
		ret = PTR_ERR(proc_async_subdev);
		goto complete;
	}

	proc_async_subdev->source = source;

	source->expected = true;

complete:
	fwnode_handle_put(handle);

	return ret;
}

int sun6i_isp_proc_setup(struct sun6i_isp_device *isp_dev)
{
	struct device *dev = isp_dev->dev;
	struct sun6i_isp_proc *proc = &isp_dev->proc;
	struct v4l2_device *v4l2_dev = &isp_dev->v4l2.v4l2_dev;
	struct v4l2_async_notifier *notifier = &proc->notifier;
	struct v4l2_subdev *subdev = &proc->subdev;
	struct media_pad *pads = proc->pads;
	int ret;

	mutex_init(&proc->lock);

	/* V4L2 Subdev */

	v4l2_subdev_init(subdev, &sun6i_isp_proc_subdev_ops);
	strscpy(subdev->name, SUN6I_ISP_PROC_NAME, sizeof(subdev->name));
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	subdev->owner = THIS_MODULE;
	subdev->dev = dev;

	v4l2_set_subdevdata(subdev, isp_dev);

	/* Media Entity */

	subdev->entity.function = MEDIA_ENT_F_PROC_VIDEO_ISP;
	subdev->entity.ops = &sun6i_isp_proc_entity_ops;

	/* Media Pads */

	pads[SUN6I_ISP_PROC_PAD_SINK_CSI].flags = MEDIA_PAD_FL_SINK |
						  MEDIA_PAD_FL_MUST_CONNECT;
	pads[SUN6I_ISP_PROC_PAD_SINK_PARAMS].flags = MEDIA_PAD_FL_SINK |
						     MEDIA_PAD_FL_MUST_CONNECT;
	pads[SUN6I_ISP_PROC_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&subdev->entity, SUN6I_ISP_PROC_PAD_COUNT,
				     pads);
	if (ret)
		return ret;

	/* V4L2 Subdev */

	ret = v4l2_device_register_subdev(v4l2_dev, subdev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "failed to register v4l2 subdev: %d\n", ret);
		goto error_media_entity;
	}

	/* V4L2 Async */

	v4l2_async_nf_init(notifier);
	notifier->ops = &sun6i_isp_proc_notifier_ops;

	sun6i_isp_proc_source_setup(isp_dev, &proc->source_csi0,
				    SUN6I_ISP_PORT_CSI0);
	sun6i_isp_proc_source_setup(isp_dev, &proc->source_csi1,
				    SUN6I_ISP_PORT_CSI1);

	ret = v4l2_async_nf_register(v4l2_dev, notifier);
	if (ret) {
		v4l2_err(v4l2_dev,
			 "failed to register v4l2 async notifier: %d\n", ret);
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

void sun6i_isp_proc_cleanup(struct sun6i_isp_device *isp_dev)
{
	struct v4l2_async_notifier *notifier = &isp_dev->proc.notifier;
	struct v4l2_subdev *subdev = &isp_dev->proc.subdev;

	v4l2_async_nf_unregister(notifier);
	v4l2_async_nf_cleanup(notifier);

	v4l2_device_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);
}

// SPDX-License-Identifier: GPL-2.0
/*
 * virtual camera driver
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd.
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define PROP_WIDTH	"width"
#define PROP_HEIGHT	"height"
#define PROP_BUSFMT	"bus-format"
#define VCAM_VTS_MAX	0x7fff

struct output_mode {
	u32 width;
	u32 height;
	u32 hts_def;
	u32 vts_def;
};

struct output_pixfmt {
	u32 code;
	u32 reserved;
};

struct virtual_camera {
	struct i2c_client *client;
	bool streaming;
	struct mutex mutex; /* lock for updating format protection */
	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_mbus_framefmt def_fmt;

	const struct output_mode *cur_mode;
	int fmt_code;
	s64 link_frequency;
};

#define to_virtual_camera(sd) container_of(sd, struct virtual_camera, subdev)

static const s64 link_freq_menu_items[] = {
	40000000,	/* minimum support frequency */
	55000000,
	75000000,
	100000000,
	125000000,
	150000000,
	200000000,
	250000000,
	300000000,
	350000000,
	400000000,
	500000000,
	600000000,
	700000000,
	752000000,
	800000000,
	900000000,
	1000000000,
	1100000000,
	1200000000,
	1250000000	/* maximum support frequency */
};

static const struct output_pixfmt supported_formats[] = {
	{
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG8_1X8,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB8_1X8,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
	}, {
		.code = MEDIA_BUS_FMT_RGB888_1X24,
	},
};

static const struct output_mode supported_modes[] = {
	{
		.width = 1280,
		.height = 720,
		.hts_def = 1500,
		.vts_def = 900,
	}, {
		.width = 1920,
		.height = 1080,
		.hts_def = 2400,
		.vts_def = 1200,
	}, {
		.width = 3840,
		.height = 720,
		.hts_def = 4300,
		.vts_def = 900,
	}, {
		.width = 3840,
		.height = 2160,
		.hts_def = 4300,
		.vts_def = 2400,
	}, {
		.width = 4096,
		.height = 2048,
		.hts_def = 4300,
		.vts_def = 2400,
	}, {
		.width = 5120,
		.height = 2880,
		.hts_def = 5800,
		.vts_def = 3100,
	}, {
		.width = 5760,
		.height = 1080,
		.hts_def = 6400,
		.vts_def = 1300,
	},
};

static int vcamera_get_reso_dist(const struct output_mode *mode,
				 struct v4l2_mbus_framefmt *fmt)
{
	return abs(mode->width - fmt->width) +
	       abs(mode->height - fmt->height);
}

static const struct output_mode *
vcamera_get_best_mode(struct v4l2_mbus_framefmt *fmt)
{
	int dist = 0, min_dist = -1;
	int best = 0;
	unsigned int i = 0;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = vcamera_get_reso_dist(&supported_modes[i], fmt);
		if (min_dist == -1 || dist < min_dist) {
			min_dist = dist;
			best = i;
		}
	}

	return &supported_modes[best];
}

static void vcamera_fill_fmt(struct virtual_camera *vcam,
			     struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = vcam->fmt_code;
	fmt->width = vcam->cur_mode->width;
	fmt->height = vcam->cur_mode->height;
	fmt->field = V4L2_FIELD_NONE;
}

static void vcamera_get_default_fmt(struct virtual_camera *vcam)
{
	struct device *dev = &vcam->client->dev;
	struct v4l2_mbus_framefmt *def_fmt = &vcam->def_fmt;
	int index = ARRAY_SIZE(supported_formats);

	vcam->cur_mode = vcamera_get_best_mode(def_fmt);
	if (vcam->cur_mode->width != def_fmt->width ||
	    vcam->cur_mode->height != def_fmt->height)
		dev_warn(dev, "get dts res: %dx%d, select best res: %dx%d\n",
			 def_fmt->width, def_fmt->height,
			 vcam->cur_mode->width, vcam->cur_mode->height);

	while (--index >= 0)
		if (supported_formats[index].code == def_fmt->code)
			break;

	if (index < 0) {
		vcam->fmt_code = MEDIA_BUS_FMT_SBGGR8_1X8;
		dev_warn(dev, "get dts fmt: 0x%x, select default fmt: 0x%x\n",
			 def_fmt->code, vcam->fmt_code);
	} else {
		vcam->fmt_code = def_fmt->code;
	}

	/* if not found link-frequencies in dts, set a default value */
	if (!vcam->link_frequency)
		vcam->link_frequency = 500000000;

	vcamera_fill_fmt(vcam, def_fmt);
}

static int vcamera_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct virtual_camera *vcam = to_virtual_camera(sd);
	struct v4l2_mbus_framefmt *mbus_fmt = &fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		struct v4l2_mbus_framefmt *mf;

		mf = v4l2_subdev_get_try_format(sd, cfg, 0);
		mutex_lock(&vcam->mutex);
		fmt->format = *mf;
		mutex_unlock(&vcam->mutex);
		return 0;
#else
		return -ENOTTY;
#endif
	}

	mutex_lock(&vcam->mutex);
	vcamera_fill_fmt(vcam, mbus_fmt);
	mutex_unlock(&vcam->mutex);

	return 0;
}

static int vcamera_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct virtual_camera *vcam = to_virtual_camera(sd);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	int index = ARRAY_SIZE(supported_formats);

	vcam->cur_mode = vcamera_get_best_mode(mf);

	while (--index >= 0)
		if (supported_formats[index].code == mf->code)
			break;

	if (index < 0)
		return -EINVAL;

	vcam->fmt_code = supported_formats[index].code;

	mutex_lock(&vcam->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		*mf = fmt->format;
#else
		mutex_unlock(&vcam->mutex);
		return -ENOTTY;
#endif
	} else {
		if (vcam->streaming) {
			mutex_unlock(&vcam->mutex);
			return -EBUSY;
		}

		vcamera_fill_fmt(vcam, mf);
	}

	mutex_unlock(&vcam->mutex);

	return 0;
}

static int vcamera_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(supported_formats))
		return -EINVAL;

	code->code = supported_formats[code->index].code;

	return 0;
}

static int vcamera_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	unsigned int index = fse->index;
	int i = ARRAY_SIZE(supported_formats);

	if (index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	while (--i >= 0)
		if (fse->code == supported_formats[i].code)
			break;

	fse->code = supported_formats[i].code;

	fse->min_width  = supported_modes[index].width;
	fse->max_width  = fse->min_width;
	fse->max_height = supported_modes[index].height;
	fse->min_height = fse->max_height;

	return 0;
}

static inline void vcamera_set_vts(struct virtual_camera *vcam, s32 val)
{
	/* TODO */
}

static int vcamera_s_stream(struct v4l2_subdev *sd, int on)
{
	struct virtual_camera *vcam = to_virtual_camera(sd);

	mutex_lock(&vcam->mutex);

	on = !!on;
	if (on == vcam->streaming)
		goto unlock_and_return;

	/* TODO */
	vcam->streaming = on;

unlock_and_return:
	mutex_unlock(&vcam->mutex);
	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int vcamera_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct virtual_camera *vcam = to_virtual_camera(sd);
	struct v4l2_mbus_framefmt *try_fmt;

	mutex_lock(&vcam->mutex);

	try_fmt = v4l2_subdev_get_try_format(sd, fh->pad, 0);
	/* Initialize try_fmt */
	vcamera_fill_fmt(vcam, try_fmt);

	mutex_unlock(&vcam->mutex);

	return 0;
}
#endif

static int vcamera_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct virtual_camera *vcam = container_of(ctrl->handler,
						   struct virtual_camera,
						   ctrl_handler);
	struct i2c_client *client = vcam->client;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		vcamera_set_vts(vcam, ctrl->val);
		break;
	case V4L2_CID_LINK_FREQ:
		vcam->link_frequency = link_freq_menu_items[ctrl->val];
		dev_info(&client->dev, "link freq ctrl->val: %d freq: %lld\n",
			 ctrl->val, vcam->link_frequency);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	return 0;
}

static struct v4l2_subdev_core_ops vcamera_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
};

static struct v4l2_subdev_video_ops vcamera_video_ops = {
	.s_stream = vcamera_s_stream,
};

static struct v4l2_subdev_pad_ops vcamera_pad_ops = {
	.enum_mbus_code = vcamera_enum_mbus_code,
	.enum_frame_size = vcamera_enum_frame_sizes,
	.get_fmt = vcamera_get_fmt,
	.set_fmt = vcamera_set_fmt,
};

static struct v4l2_subdev_ops vcamera_subdev_ops = {
	.core	= &vcamera_core_ops,
	.video	= &vcamera_video_ops,
	.pad	= &vcamera_pad_ops,
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops vcamera_internal_ops = {
	.open = vcamera_open,
};
#endif

static const struct v4l2_ctrl_ops vcamera_ctrl_ops = {
	.s_ctrl = vcamera_s_ctrl,
};

static int vcamera_initialize_controls(struct virtual_camera *vcam)
{
	const struct output_mode *mode;
	struct v4l2_ctrl_handler *handler;
	u32 h_blank, i;
	int ret;

	handler = &vcam->ctrl_handler;
	mode = vcam->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 3);
	if (ret)
		return ret;

	handler->lock = &vcam->mutex;

	vcam->link_freq = v4l2_ctrl_new_int_menu(handler, &vcamera_ctrl_ops,
					V4L2_CID_LINK_FREQ,
					ARRAY_SIZE(link_freq_menu_items) - 1, 0,
					link_freq_menu_items);

	h_blank = mode->hts_def - mode->width;
	vcam->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (vcam->hblank)
		vcam->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vcam->vblank = v4l2_ctrl_new_std(handler, &vcamera_ctrl_ops,
				V4L2_CID_VBLANK, mode->vts_def - mode->height,
				VCAM_VTS_MAX - mode->height, 1,
				mode->vts_def - mode->height);

	if (handler->error) {
		v4l2_ctrl_handler_free(handler);
		return handler->error;
	}

	vcam->subdev.ctrl_handler = handler;

	for (i = 0; i < ARRAY_SIZE(link_freq_menu_items); i++) {
		if (link_freq_menu_items[i] > vcam->link_frequency && i >= 1) {
			v4l2_ctrl_s_ctrl(vcam->link_freq, i - 1);
			break;
		}
	}

	if (i == ARRAY_SIZE(link_freq_menu_items)) {
		dev_warn(&vcam->client->dev,
			 "vcam->link_frequency: %lld, max support clock: %lld\n",
			 vcam->link_frequency, link_freq_menu_items[i - 1]);
		v4l2_ctrl_s_ctrl(vcam->link_freq, i - 1);
	}

	return 0;
}

static int vcamera_check_sensor_id(struct virtual_camera *vcam,
				   struct i2c_client *client)
{
	/* TODO */
	return 0;
}

static int vcamera_get_pdata(struct i2c_client *client,
			     struct virtual_camera *vcam)
{
	struct v4l2_fwnode_endpoint *bus_cfg;
	struct device_node *np = client->dev.of_node;
	struct device_node *endpoint;
	u32 val;

	if (!IS_ENABLED(CONFIG_OF) || !np)
		return 0;

	if (!of_property_read_u32(np, PROP_WIDTH, &val))
		vcam->def_fmt.width = val;

	if (!of_property_read_u32(np, PROP_HEIGHT, &val))
		vcam->def_fmt.height = val;

	if (!of_property_read_u32(np, PROP_BUSFMT, &val))
		vcam->def_fmt.code = val;

	endpoint = of_graph_get_next_endpoint(np, NULL);
	if (!endpoint)
		return -ENODEV;

	bus_cfg = v4l2_fwnode_endpoint_alloc_parse(of_fwnode_handle(endpoint));
	if (IS_ERR(bus_cfg))
		goto done;

	if (!bus_cfg->nr_of_link_frequencies) {
		dev_info(&client->dev,
			 "link-frequencies property not found or too many\n");
		goto done;
	}

	vcam->link_frequency = bus_cfg->link_frequencies[0];

done:
	v4l2_fwnode_endpoint_free(bus_cfg);
	of_node_put(endpoint);
	return 0;
}

static int vcamera_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct virtual_camera *vcam;
	int ret;

	vcam = devm_kzalloc(dev, sizeof(*vcam), GFP_KERNEL);
	if (!vcam)
		return -ENOMEM;

	vcam->client = client;
	vcamera_get_pdata(client, vcam);
	vcamera_get_default_fmt(vcam);

	mutex_init(&vcam->mutex);
	v4l2_i2c_subdev_init(&vcam->subdev, client, &vcamera_subdev_ops);
	ret = vcamera_initialize_controls(vcam);
	if (ret)
		goto destroy_mutex;

	ret = vcamera_check_sensor_id(vcam, client);
	if (ret)
		return ret;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	vcam->subdev.internal_ops = &vcamera_internal_ops;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	vcam->pad.flags = MEDIA_PAD_FL_SOURCE;
	vcam->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	vcam->subdev.entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ret = media_entity_init(&vcam->subdev.entity, 1, &vcam->pad, 0);
	if (ret < 0)
		goto free_ctrl_handler;
#endif

	ret = v4l2_async_register_subdev(&vcam->subdev);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto clean_entity;
	}

	dev_info(dev, "virtual camera register success\n");

	return 0;

clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&vcam->subdev.entity);
#endif
free_ctrl_handler:
	v4l2_ctrl_handler_free(&vcam->ctrl_handler);
destroy_mutex:
	mutex_destroy(&vcam->mutex);

	return ret;
}

static int vcamera_remove(struct i2c_client *client)
{
	struct virtual_camera *vcam = i2c_get_clientdata(client);

	v4l2_async_unregister_subdev(&vcam->subdev);
	media_entity_cleanup(&vcam->subdev.entity);
	v4l2_ctrl_handler_free(&vcam->ctrl_handler);
	mutex_destroy(&vcam->mutex);

	return 0;
}

static const struct i2c_device_id vcamera_id[] = {
	{ "virtual-camera", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i2c, vcamera_id);

static const struct of_device_id vcamera_of_match[] = {
	{ .compatible = "rockchip,virtual-camera" },
	{},
};

static struct i2c_driver vcamera_i2c_driver = {
	.driver = {
		.name = "virtual-camera",
		.of_match_table = vcamera_of_match
	},
	.probe		= vcamera_probe,
	.remove		= vcamera_remove,
	.id_table	= vcamera_id,
};

module_i2c_driver(vcamera_i2c_driver);

MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip virtual camera sensor driver");
MODULE_LICENSE("GPL v2");

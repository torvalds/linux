// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Texas Instruments DS90UB913 video serializer
 *
 * Based on a driver from Luca Ceresoli <luca@lucaceresoli.net>
 *
 * Copyright (c) 2019 Luca Ceresoli <luca@lucaceresoli.net>
 * Copyright (c) 2023 Tomi Valkeinen <tomi.valkeinen@ideasonboard.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/fwnode.h>
#include <linux/gpio/driver.h>
#include <linux/i2c-atr.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>

#include <media/i2c/ds90ub9xx.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#define UB913_PAD_SINK			0
#define UB913_PAD_SOURCE		1

/*
 * UB913 has 4 gpios, but gpios 3 and 4 are reserved for external oscillator
 * mode. Thus we only support 2 gpios for now.
 */
#define UB913_NUM_GPIOS			2

#define UB913_REG_RESET_CTL			0x01
#define UB913_REG_RESET_CTL_DIGITAL_RESET_1	BIT(1)
#define UB913_REG_RESET_CTL_DIGITAL_RESET_0	BIT(0)

#define UB913_REG_GENERAL_CFG			0x03
#define UB913_REG_GENERAL_CFG_CRC_ERR_RESET	BIT(5)
#define UB913_REG_GENERAL_CFG_PCLK_RISING	BIT(0)

#define UB913_REG_MODE_SEL			0x05
#define UB913_REG_MODE_SEL_MODE_OVERRIDE	BIT(5)
#define UB913_REG_MODE_SEL_MODE_UP_TO_DATE	BIT(4)
#define UB913_REG_MODE_SEL_MODE_MASK		GENMASK(3, 0)

#define UB913_REG_CRC_ERRORS_LSB		0x0a
#define UB913_REG_CRC_ERRORS_MSB		0x0b

#define UB913_REG_GENERAL_STATUS		0x0c

#define UB913_REG_GPIO_CFG(n)			(0x0d + (n))
#define UB913_REG_GPIO_CFG_ENABLE(n)		BIT(0 + (n) * 4)
#define UB913_REG_GPIO_CFG_DIR_INPUT(n)		BIT(1 + (n) * 4)
#define UB913_REG_GPIO_CFG_REMOTE_EN(n)		BIT(2 + (n) * 4)
#define UB913_REG_GPIO_CFG_OUT_VAL(n)		BIT(3 + (n) * 4)
#define UB913_REG_GPIO_CFG_MASK(n)		(0xf << ((n) * 4))

#define UB913_REG_SCL_HIGH_TIME			0x11
#define UB913_REG_SCL_LOW_TIME			0x12

#define UB913_REG_PLL_OVR			0x35

struct ub913_data {
	struct i2c_client	*client;
	struct regmap		*regmap;
	struct clk		*clkin;

	struct gpio_chip	gpio_chip;

	struct v4l2_subdev	sd;
	struct media_pad	pads[2];

	struct v4l2_async_notifier	notifier;

	struct v4l2_subdev	*source_sd;
	u16			source_sd_pad;

	u64			enabled_source_streams;

	struct clk_hw		*clkout_clk_hw;

	struct ds90ub9xx_platform_data *plat_data;

	bool			pclk_polarity_rising;
};

static inline struct ub913_data *sd_to_ub913(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ub913_data, sd);
}

struct ub913_format_info {
	u32 incode;
	u32 outcode;
};

static const struct ub913_format_info ub913_formats[] = {
	/* Only RAW10 with 8-bit payload is supported at the moment */
	{ .incode = MEDIA_BUS_FMT_YUYV8_2X8, .outcode = MEDIA_BUS_FMT_YUYV8_1X16 },
	{ .incode = MEDIA_BUS_FMT_UYVY8_2X8, .outcode = MEDIA_BUS_FMT_UYVY8_1X16 },
	{ .incode = MEDIA_BUS_FMT_VYUY8_2X8, .outcode = MEDIA_BUS_FMT_VYUY8_1X16 },
	{ .incode = MEDIA_BUS_FMT_YVYU8_2X8, .outcode = MEDIA_BUS_FMT_YVYU8_1X16 },
};

static const struct ub913_format_info *ub913_find_format(u32 incode)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ub913_formats); i++) {
		if (ub913_formats[i].incode == incode)
			return &ub913_formats[i];
	}

	return NULL;
}

static int ub913_read(const struct ub913_data *priv, u8 reg, u8 *val)
{
	unsigned int v;
	int ret;

	ret = regmap_read(priv->regmap, reg, &v);
	if (ret < 0) {
		dev_err(&priv->client->dev,
			"Cannot read register 0x%02x: %d!\n", reg, ret);
		return ret;
	}

	*val = v;
	return 0;
}

static int ub913_write(const struct ub913_data *priv, u8 reg, u8 val)
{
	int ret;

	ret = regmap_write(priv->regmap, reg, val);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"Cannot write register 0x%02x: %d!\n", reg, ret);

	return ret;
}

/*
 * GPIO chip
 */
static int ub913_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	return GPIO_LINE_DIRECTION_OUT;
}

static int ub913_gpio_direction_out(struct gpio_chip *gc, unsigned int offset,
				    int value)
{
	struct ub913_data *priv = gpiochip_get_data(gc);
	unsigned int reg_idx = offset / 2;
	unsigned int field_idx = offset % 2;

	return regmap_update_bits(priv->regmap, UB913_REG_GPIO_CFG(reg_idx),
				  UB913_REG_GPIO_CFG_MASK(field_idx),
				  UB913_REG_GPIO_CFG_ENABLE(field_idx) |
					  (value ? UB913_REG_GPIO_CFG_OUT_VAL(field_idx) :
						   0));
}

static void ub913_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	ub913_gpio_direction_out(gc, offset, value);
}

static int ub913_gpio_of_xlate(struct gpio_chip *gc,
			       const struct of_phandle_args *gpiospec,
			       u32 *flags)
{
	if (flags)
		*flags = gpiospec->args[1];

	return gpiospec->args[0];
}

static int ub913_gpiochip_probe(struct ub913_data *priv)
{
	struct device *dev = &priv->client->dev;
	struct gpio_chip *gc = &priv->gpio_chip;
	int ret;

	/* Initialize GPIOs 0 and 1 to local control, tri-state */
	ub913_write(priv, UB913_REG_GPIO_CFG(0), 0);

	gc->label = dev_name(dev);
	gc->parent = dev;
	gc->owner = THIS_MODULE;
	gc->base = -1;
	gc->can_sleep = true;
	gc->ngpio = UB913_NUM_GPIOS;
	gc->get_direction = ub913_gpio_get_direction;
	gc->direction_output = ub913_gpio_direction_out;
	gc->set = ub913_gpio_set;
	gc->of_xlate = ub913_gpio_of_xlate;
	gc->of_gpio_n_cells = 2;

	ret = gpiochip_add_data(gc, priv);
	if (ret) {
		dev_err(dev, "Failed to add GPIOs: %d\n", ret);
		return ret;
	}

	return 0;
}

static void ub913_gpiochip_remove(struct ub913_data *priv)
{
	gpiochip_remove(&priv->gpio_chip);
}

static const struct regmap_config ub913_regmap_config = {
	.name = "ds90ub913",
	.reg_bits = 8,
	.val_bits = 8,
	.reg_format_endian = REGMAP_ENDIAN_DEFAULT,
	.val_format_endian = REGMAP_ENDIAN_DEFAULT,
};

/*
 * V4L2
 */

static int ub913_enable_streams(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state, u32 pad,
				u64 streams_mask)
{
	struct ub913_data *priv = sd_to_ub913(sd);
	u64 sink_streams;
	int ret;

	sink_streams = v4l2_subdev_state_xlate_streams(state, UB913_PAD_SOURCE,
						       UB913_PAD_SINK,
						       &streams_mask);

	ret = v4l2_subdev_enable_streams(priv->source_sd, priv->source_sd_pad,
					 sink_streams);
	if (ret)
		return ret;

	priv->enabled_source_streams |= streams_mask;

	return 0;
}

static int ub913_disable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct ub913_data *priv = sd_to_ub913(sd);
	u64 sink_streams;
	int ret;

	sink_streams = v4l2_subdev_state_xlate_streams(state, UB913_PAD_SOURCE,
						       UB913_PAD_SINK,
						       &streams_mask);

	ret = v4l2_subdev_disable_streams(priv->source_sd, priv->source_sd_pad,
					  sink_streams);
	if (ret)
		return ret;

	priv->enabled_source_streams &= ~streams_mask;

	return 0;
}

static int _ub913_set_routing(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_krouting *routing)
{
	static const struct v4l2_mbus_framefmt in_format = {
		.width = 640,
		.height = 480,
		.code = MEDIA_BUS_FMT_UYVY8_2X8,
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.ycbcr_enc = V4L2_YCBCR_ENC_601,
		.quantization = V4L2_QUANTIZATION_LIM_RANGE,
		.xfer_func = V4L2_XFER_FUNC_SRGB,
	};
	static const struct v4l2_mbus_framefmt out_format = {
		.width = 640,
		.height = 480,
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.ycbcr_enc = V4L2_YCBCR_ENC_601,
		.quantization = V4L2_QUANTIZATION_LIM_RANGE,
		.xfer_func = V4L2_XFER_FUNC_SRGB,
	};
	struct v4l2_subdev_stream_configs *stream_configs;
	unsigned int i;
	int ret;

	/*
	 * Note: we can only support up to V4L2_FRAME_DESC_ENTRY_MAX, until
	 * frame desc is made dynamically allocated.
	 */

	if (routing->num_routes > V4L2_FRAME_DESC_ENTRY_MAX)
		return -EINVAL;

	ret = v4l2_subdev_routing_validate(sd, routing,
					   V4L2_SUBDEV_ROUTING_ONLY_1_TO_1);
	if (ret)
		return ret;

	ret = v4l2_subdev_set_routing(sd, state, routing);
	if (ret)
		return ret;

	stream_configs = &state->stream_configs;

	for (i = 0; i < stream_configs->num_configs; i++) {
		if (stream_configs->configs[i].pad == UB913_PAD_SINK)
			stream_configs->configs[i].fmt = in_format;
		else
			stream_configs->configs[i].fmt = out_format;
	}

	return 0;
}

static int ub913_set_routing(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     enum v4l2_subdev_format_whence which,
			     struct v4l2_subdev_krouting *routing)
{
	struct ub913_data *priv = sd_to_ub913(sd);

	if (which == V4L2_SUBDEV_FORMAT_ACTIVE && priv->enabled_source_streams)
		return -EBUSY;

	return _ub913_set_routing(sd, state, routing);
}

static int ub913_get_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_frame_desc *fd)
{
	struct ub913_data *priv = sd_to_ub913(sd);
	const struct v4l2_subdev_krouting *routing;
	struct v4l2_mbus_frame_desc source_fd;
	struct v4l2_subdev_route *route;
	struct v4l2_subdev_state *state;
	int ret;

	if (pad != UB913_PAD_SOURCE)
		return -EINVAL;

	ret = v4l2_subdev_call(priv->source_sd, pad, get_frame_desc,
			       priv->source_sd_pad, &source_fd);
	if (ret)
		return ret;

	fd->type = V4L2_MBUS_FRAME_DESC_TYPE_PARALLEL;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	routing = &state->routing;

	for_each_active_route(routing, route) {
		unsigned int i;

		if (route->source_pad != pad)
			continue;

		for (i = 0; i < source_fd.num_entries; i++) {
			if (source_fd.entry[i].stream == route->sink_stream)
				break;
		}

		if (i == source_fd.num_entries) {
			dev_err(&priv->client->dev,
				"Failed to find stream from source frame desc\n");
			ret = -EPIPE;
			goto out_unlock;
		}

		fd->entry[fd->num_entries].stream = route->source_stream;
		fd->entry[fd->num_entries].flags = source_fd.entry[i].flags;
		fd->entry[fd->num_entries].length = source_fd.entry[i].length;
		fd->entry[fd->num_entries].pixelcode =
			source_fd.entry[i].pixelcode;

		fd->num_entries++;
	}

out_unlock:
	v4l2_subdev_unlock_state(state);

	return ret;
}

static int ub913_set_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_state *state,
			 struct v4l2_subdev_format *format)
{
	struct ub913_data *priv = sd_to_ub913(sd);
	struct v4l2_mbus_framefmt *fmt;
	const struct ub913_format_info *finfo;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE &&
	    priv->enabled_source_streams)
		return -EBUSY;

	/* Source format is fully defined by the sink format, so not settable */
	if (format->pad == UB913_PAD_SOURCE)
		return v4l2_subdev_get_fmt(sd, state, format);

	finfo = ub913_find_format(format->format.code);
	if (!finfo) {
		finfo = &ub913_formats[0];
		format->format.code = finfo->incode;
	}

	/* Set sink format */
	fmt = v4l2_subdev_state_get_format(state, format->pad, format->stream);
	if (!fmt)
		return -EINVAL;

	*fmt = format->format;

	/* Propagate to source format, and adjust the mbus code */
	fmt = v4l2_subdev_state_get_opposite_stream_format(state, format->pad,
							   format->stream);
	if (!fmt)
		return -EINVAL;

	format->format.code = finfo->outcode;

	*fmt = format->format;

	return 0;
}

static int ub913_init_state(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_route routes[] = {
		{
			.sink_pad = UB913_PAD_SINK,
			.sink_stream = 0,
			.source_pad = UB913_PAD_SOURCE,
			.source_stream = 0,
			.flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE,
		},
	};

	struct v4l2_subdev_krouting routing = {
		.num_routes = ARRAY_SIZE(routes),
		.routes = routes,
	};

	return _ub913_set_routing(sd, state, &routing);
}

static int ub913_log_status(struct v4l2_subdev *sd)
{
	struct ub913_data *priv = sd_to_ub913(sd);
	struct device *dev = &priv->client->dev;
	u8 v = 0, v1 = 0, v2 = 0;

	ub913_read(priv, UB913_REG_MODE_SEL, &v);
	dev_info(dev, "MODE_SEL %#02x\n", v);

	ub913_read(priv, UB913_REG_CRC_ERRORS_LSB, &v1);
	ub913_read(priv, UB913_REG_CRC_ERRORS_MSB, &v2);
	dev_info(dev, "CRC errors %u\n", v1 | (v2 << 8));

	/* clear CRC errors */
	ub913_read(priv, UB913_REG_GENERAL_CFG, &v);
	ub913_write(priv, UB913_REG_GENERAL_CFG,
		    v | UB913_REG_GENERAL_CFG_CRC_ERR_RESET);
	ub913_write(priv, UB913_REG_GENERAL_CFG, v);

	ub913_read(priv, UB913_REG_GENERAL_STATUS, &v);
	dev_info(dev, "GENERAL_STATUS %#02x\n", v);

	ub913_read(priv, UB913_REG_PLL_OVR, &v);
	dev_info(dev, "PLL_OVR %#02x\n", v);

	return 0;
}

static const struct v4l2_subdev_core_ops ub913_subdev_core_ops = {
	.log_status = ub913_log_status,
};

static const struct v4l2_subdev_pad_ops ub913_pad_ops = {
	.enable_streams = ub913_enable_streams,
	.disable_streams = ub913_disable_streams,
	.set_routing = ub913_set_routing,
	.get_frame_desc = ub913_get_frame_desc,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ub913_set_fmt,
};

static const struct v4l2_subdev_ops ub913_subdev_ops = {
	.core = &ub913_subdev_core_ops,
	.pad = &ub913_pad_ops,
};

static const struct v4l2_subdev_internal_ops ub913_internal_ops = {
	.init_state = ub913_init_state,
};

static const struct media_entity_operations ub913_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int ub913_notify_bound(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *source_subdev,
			      struct v4l2_async_connection *asd)
{
	struct ub913_data *priv = sd_to_ub913(notifier->sd);
	struct device *dev = &priv->client->dev;
	int ret;

	ret = media_entity_get_fwnode_pad(&source_subdev->entity,
					  source_subdev->fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (ret < 0) {
		dev_err(dev, "Failed to find pad for %s\n",
			source_subdev->name);
		return ret;
	}

	priv->source_sd = source_subdev;
	priv->source_sd_pad = ret;

	ret = media_create_pad_link(&source_subdev->entity, priv->source_sd_pad,
				    &priv->sd.entity, UB913_PAD_SINK,
				    MEDIA_LNK_FL_ENABLED |
					    MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_err(dev, "Unable to link %s:%u -> %s:0\n",
			source_subdev->name, priv->source_sd_pad,
			priv->sd.name);
		return ret;
	}

	return 0;
}

static const struct v4l2_async_notifier_operations ub913_notify_ops = {
	.bound = ub913_notify_bound,
};

static int ub913_v4l2_notifier_register(struct ub913_data *priv)
{
	struct device *dev = &priv->client->dev;
	struct v4l2_async_connection *asd;
	struct fwnode_handle *ep_fwnode;
	int ret;

	ep_fwnode = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev),
						    UB913_PAD_SINK, 0, 0);
	if (!ep_fwnode) {
		dev_err(dev, "No graph endpoint\n");
		return -ENODEV;
	}

	v4l2_async_subdev_nf_init(&priv->notifier, &priv->sd);

	asd = v4l2_async_nf_add_fwnode_remote(&priv->notifier, ep_fwnode,
					      struct v4l2_async_connection);

	fwnode_handle_put(ep_fwnode);

	if (IS_ERR(asd)) {
		dev_err(dev, "Failed to add subdev: %ld", PTR_ERR(asd));
		v4l2_async_nf_cleanup(&priv->notifier);
		return PTR_ERR(asd);
	}

	priv->notifier.ops = &ub913_notify_ops;

	ret = v4l2_async_nf_register(&priv->notifier);
	if (ret) {
		dev_err(dev, "Failed to register subdev_notifier");
		v4l2_async_nf_cleanup(&priv->notifier);
		return ret;
	}

	return 0;
}

static void ub913_v4l2_nf_unregister(struct ub913_data *priv)
{
	v4l2_async_nf_unregister(&priv->notifier);
	v4l2_async_nf_cleanup(&priv->notifier);
}

static int ub913_register_clkout(struct ub913_data *priv)
{
	struct device *dev = &priv->client->dev;
	const char *name;
	int ret;

	name = kasprintf(GFP_KERNEL, "ds90ub913.%s.clk_out", dev_name(dev));
	if (!name)
		return -ENOMEM;

	priv->clkout_clk_hw = devm_clk_hw_register_fixed_factor(dev, name,
		__clk_get_name(priv->clkin), 0, 1, 2);

	kfree(name);

	if (IS_ERR(priv->clkout_clk_hw))
		return dev_err_probe(dev, PTR_ERR(priv->clkout_clk_hw),
				     "Cannot register clkout hw\n");

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					  priv->clkout_clk_hw);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Cannot add OF clock provider\n");

	return 0;
}

static int ub913_i2c_master_init(struct ub913_data *priv)
{
	/* i2c fast mode */
	u32 scl_high = 600 + 300; /* high period + rise time, ns */
	u32 scl_low = 1300 + 300; /* low period + fall time, ns */
	unsigned long ref;
	int ret;

	ref = clk_get_rate(priv->clkin) / 2;

	scl_high = div64_u64((u64)scl_high * ref, 1000000000);
	scl_low = div64_u64((u64)scl_low * ref, 1000000000);

	ret = ub913_write(priv, UB913_REG_SCL_HIGH_TIME, scl_high);
	if (ret)
		return ret;

	ret = ub913_write(priv, UB913_REG_SCL_LOW_TIME, scl_low);
	if (ret)
		return ret;

	return 0;
}

static int ub913_add_i2c_adapter(struct ub913_data *priv)
{
	struct device *dev = &priv->client->dev;
	struct fwnode_handle *i2c_handle;
	int ret;

	i2c_handle = device_get_named_child_node(dev, "i2c");
	if (!i2c_handle)
		return 0;

	ret = i2c_atr_add_adapter(priv->plat_data->atr, priv->plat_data->port,
				  dev, i2c_handle);

	fwnode_handle_put(i2c_handle);

	if (ret)
		return ret;

	return 0;
}

static int ub913_parse_dt(struct ub913_data *priv)
{
	struct device *dev = &priv->client->dev;
	struct v4l2_fwnode_endpoint vep = {
		.bus_type = V4L2_MBUS_PARALLEL,
	};
	struct fwnode_handle *ep_fwnode;
	int ret;

	ep_fwnode = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev),
						    UB913_PAD_SINK, 0, 0);
	if (!ep_fwnode)
		return dev_err_probe(dev, -ENOENT, "No sink endpoint\n");

	ret = v4l2_fwnode_endpoint_parse(ep_fwnode, &vep);

	fwnode_handle_put(ep_fwnode);

	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to parse sink endpoint data\n");

	if (vep.bus.parallel.flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
		priv->pclk_polarity_rising = true;
	else if (vep.bus.parallel.flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
		priv->pclk_polarity_rising = false;
	else
		return dev_err_probe(dev, -EINVAL,
				     "bad value for 'pclk-sample'\n");

	return 0;
}

static int ub913_hw_init(struct ub913_data *priv)
{
	struct device *dev = &priv->client->dev;
	bool mode_override;
	u8 mode;
	int ret;
	u8 v;

	ret = ub913_read(priv, UB913_REG_MODE_SEL, &v);
	if (ret)
		return ret;

	if (!(v & UB913_REG_MODE_SEL_MODE_UP_TO_DATE))
		return dev_err_probe(dev, -ENODEV,
				     "Mode value not stabilized\n");

	mode_override = v & UB913_REG_MODE_SEL_MODE_OVERRIDE;
	mode = v & UB913_REG_MODE_SEL_MODE_MASK;

	dev_dbg(dev, "mode from %s: %#x\n",
		mode_override ? "reg" : "deserializer", mode);

	ret = ub913_i2c_master_init(priv);
	if (ret)
		return dev_err_probe(dev, ret, "i2c master init failed\n");

	ub913_read(priv, UB913_REG_GENERAL_CFG, &v);
	v &= ~UB913_REG_GENERAL_CFG_PCLK_RISING;
	v |= priv->pclk_polarity_rising ? UB913_REG_GENERAL_CFG_PCLK_RISING : 0;
	ub913_write(priv, UB913_REG_GENERAL_CFG, v);

	return 0;
}

static int ub913_subdev_init(struct ub913_data *priv)
{
	struct device *dev = &priv->client->dev;
	int ret;

	v4l2_i2c_subdev_init(&priv->sd, priv->client, &ub913_subdev_ops);
	priv->sd.internal_ops = &ub913_internal_ops;
	priv->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_STREAMS;
	priv->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	priv->sd.entity.ops = &ub913_entity_ops;

	priv->pads[0].flags = MEDIA_PAD_FL_SINK;
	priv->pads[1].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&priv->sd.entity, 2, priv->pads);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init pads\n");

	ret = v4l2_subdev_init_finalize(&priv->sd);
	if (ret)
		goto err_entity_cleanup;

	ret = ub913_v4l2_notifier_register(priv);
	if (ret) {
		dev_err_probe(dev, ret,
			      "v4l2 subdev notifier register failed\n");
		goto err_subdev_cleanup;
	}

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret) {
		dev_err_probe(dev, ret, "v4l2_async_register_subdev error\n");
		goto err_unreg_notif;
	}

	return 0;

err_unreg_notif:
	ub913_v4l2_nf_unregister(priv);
err_subdev_cleanup:
	v4l2_subdev_cleanup(&priv->sd);
err_entity_cleanup:
	media_entity_cleanup(&priv->sd.entity);

	return ret;
}

static void ub913_subdev_uninit(struct ub913_data *priv)
{
	v4l2_async_unregister_subdev(&priv->sd);
	ub913_v4l2_nf_unregister(priv);
	v4l2_subdev_cleanup(&priv->sd);
	fwnode_handle_put(priv->sd.fwnode);
	media_entity_cleanup(&priv->sd.entity);
}

static int ub913_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ub913_data *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;

	priv->plat_data = dev_get_platdata(&client->dev);
	if (!priv->plat_data)
		return dev_err_probe(dev, -ENODEV, "Platform data missing\n");

	priv->regmap = devm_regmap_init_i2c(client, &ub913_regmap_config);
	if (IS_ERR(priv->regmap))
		return dev_err_probe(dev, PTR_ERR(priv->regmap),
				     "Failed to init regmap\n");

	/*
	 * ub913 can also work without ext clock, but that is not supported by
	 * the driver yet.
	 */
	priv->clkin = devm_clk_get(dev, "clkin");
	if (IS_ERR(priv->clkin))
		return dev_err_probe(dev, PTR_ERR(priv->clkin),
				     "Cannot get CLKIN\n");

	ret = ub913_parse_dt(priv);
	if (ret)
		return ret;

	ret = ub913_hw_init(priv);
	if (ret)
		return ret;

	ret = ub913_gpiochip_probe(priv);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init gpiochip\n");

	ret = ub913_register_clkout(priv);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to register clkout\n");
		goto err_gpiochip_remove;
	}

	ret = ub913_subdev_init(priv);
	if (ret)
		goto err_gpiochip_remove;

	ret = ub913_add_i2c_adapter(priv);
	if (ret) {
		dev_err_probe(dev, ret, "failed to add remote i2c adapter\n");
		goto err_subdev_uninit;
	}

	return 0;

err_subdev_uninit:
	ub913_subdev_uninit(priv);
err_gpiochip_remove:
	ub913_gpiochip_remove(priv);

	return ret;
}

static void ub913_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ub913_data *priv = sd_to_ub913(sd);

	i2c_atr_del_adapter(priv->plat_data->atr, priv->plat_data->port);

	ub913_subdev_uninit(priv);

	ub913_gpiochip_remove(priv);
}

static const struct i2c_device_id ub913_id[] = { { "ds90ub913a-q1", 0 }, {} };
MODULE_DEVICE_TABLE(i2c, ub913_id);

static const struct of_device_id ub913_dt_ids[] = {
	{ .compatible = "ti,ds90ub913a-q1" },
	{}
};
MODULE_DEVICE_TABLE(of, ub913_dt_ids);

static struct i2c_driver ds90ub913_driver = {
	.probe		= ub913_probe,
	.remove		= ub913_remove,
	.id_table	= ub913_id,
	.driver = {
		.name	= "ds90ub913a",
		.of_match_table = ub913_dt_ids,
	},
};
module_i2c_driver(ds90ub913_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Texas Instruments DS90UB913 FPD-Link III Serializer Driver");
MODULE_AUTHOR("Luca Ceresoli <luca@lucaceresoli.net>");
MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ideasonboard.com>");
MODULE_IMPORT_NS(I2C_ATR);

// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Texas Instruments DS90UB953 video serializer
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
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/rational.h>
#include <linux/regmap.h>

#include <media/i2c/ds90ub9xx.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#define UB953_PAD_SINK			0
#define UB953_PAD_SOURCE		1

#define UB953_NUM_GPIOS			4

#define UB953_DEFAULT_CLKOUT_RATE	25000000UL

#define UB953_REG_RESET_CTL			0x01
#define UB953_REG_RESET_CTL_DIGITAL_RESET_1	BIT(1)
#define UB953_REG_RESET_CTL_DIGITAL_RESET_0	BIT(0)

#define UB953_REG_GENERAL_CFG			0x02
#define UB953_REG_GENERAL_CFG_CONT_CLK		BIT(6)
#define UB953_REG_GENERAL_CFG_CSI_LANE_SEL_SHIFT	4
#define UB953_REG_GENERAL_CFG_CSI_LANE_SEL_MASK	GENMASK(5, 4)
#define UB953_REG_GENERAL_CFG_CRC_TX_GEN_ENABLE	BIT(1)
#define UB953_REG_GENERAL_CFG_I2C_STRAP_MODE	BIT(0)

#define UB953_REG_MODE_SEL			0x03
#define UB953_REG_MODE_SEL_MODE_DONE		BIT(3)
#define UB953_REG_MODE_SEL_MODE_OVERRIDE	BIT(4)
#define UB953_REG_MODE_SEL_MODE_MASK		GENMASK(2, 0)

#define UB953_REG_CLKOUT_CTRL0			0x06
#define UB953_REG_CLKOUT_CTRL1			0x07

#define UB953_REG_SCL_HIGH_TIME			0x0b
#define UB953_REG_SCL_LOW_TIME			0x0c

#define UB953_REG_LOCAL_GPIO_DATA		0x0d
#define UB953_REG_LOCAL_GPIO_DATA_GPIO_RMTEN(n)		BIT(4 + (n))
#define UB953_REG_LOCAL_GPIO_DATA_GPIO_OUT_SRC(n)	BIT(0 + (n))

#define UB953_REG_GPIO_INPUT_CTRL		0x0e
#define UB953_REG_GPIO_INPUT_CTRL_OUT_EN(n)	BIT(4 + (n))
#define UB953_REG_GPIO_INPUT_CTRL_INPUT_EN(n)	BIT(0 + (n))

#define UB953_REG_REV_MASK_ID			0x50
#define UB953_REG_GENERAL_STATUS		0x52

#define UB953_REG_GPIO_PIN_STS			0x53
#define UB953_REG_GPIO_PIN_STS_GPIO_STS(n)	BIT(0 + (n))

#define UB953_REG_BIST_ERR_CNT			0x54
#define UB953_REG_CRC_ERR_CNT1			0x55
#define UB953_REG_CRC_ERR_CNT2			0x56

#define UB953_REG_CSI_ERR_CNT			0x5c
#define UB953_REG_CSI_ERR_STATUS		0x5d
#define UB953_REG_CSI_ERR_DLANE01		0x5e
#define UB953_REG_CSI_ERR_DLANE23		0x5f
#define UB953_REG_CSI_ERR_CLK_LANE		0x60
#define UB953_REG_CSI_PKT_HDR_VC_ID		0x61
#define UB953_REG_PKT_HDR_WC_LSB		0x62
#define UB953_REG_PKT_HDR_WC_MSB		0x63
#define UB953_REG_CSI_ECC			0x64

#define UB953_REG_IND_ACC_CTL			0xb0
#define UB953_REG_IND_ACC_ADDR			0xb1
#define UB953_REG_IND_ACC_DATA			0xb2

#define UB953_REG_FPD3_RX_ID(n)			(0xf0 + (n))
#define UB953_REG_FPD3_RX_ID_LEN		6

/* Indirect register blocks */
#define UB953_IND_TARGET_PAT_GEN		0x00
#define UB953_IND_TARGET_FPD3_TX		0x01
#define UB953_IND_TARGET_DIE_ID			0x02

#define UB953_IND_PGEN_CTL			0x01
#define UB953_IND_PGEN_CTL_PGEN_ENABLE		BIT(0)
#define UB953_IND_PGEN_CFG			0x02
#define UB953_IND_PGEN_CSI_DI			0x03
#define UB953_IND_PGEN_LINE_SIZE1		0x04
#define UB953_IND_PGEN_LINE_SIZE0		0x05
#define UB953_IND_PGEN_BAR_SIZE1		0x06
#define UB953_IND_PGEN_BAR_SIZE0		0x07
#define UB953_IND_PGEN_ACT_LPF1			0x08
#define UB953_IND_PGEN_ACT_LPF0			0x09
#define UB953_IND_PGEN_TOT_LPF1			0x0a
#define UB953_IND_PGEN_TOT_LPF0			0x0b
#define UB953_IND_PGEN_LINE_PD1			0x0c
#define UB953_IND_PGEN_LINE_PD0			0x0d
#define UB953_IND_PGEN_VBP			0x0e
#define UB953_IND_PGEN_VFP			0x0f
#define UB953_IND_PGEN_COLOR(n)			(0x10 + (n)) /* n <= 15 */

/* Note: Only sync mode supported for now */
enum ub953_mode {
	/* FPD-Link III CSI-2 synchronous mode */
	UB953_MODE_SYNC,
	/* FPD-Link III CSI-2 non-synchronous mode, external ref clock */
	UB953_MODE_NONSYNC_EXT,
	/* FPD-Link III CSI-2 non-synchronous mode, internal ref clock */
	UB953_MODE_NONSYNC_INT,
	/* FPD-Link III DVP mode */
	UB953_MODE_DVP,
};

struct ub953_hw_data {
	const char *model;
	bool is_ub971;
};

struct ub953_clkout_data {
	u32 hs_div;
	u32 m;
	u32 n;
	unsigned long rate;
};

struct ub953_data {
	const struct ub953_hw_data	*hw_data;

	struct i2c_client	*client;
	struct regmap		*regmap;
	struct clk		*clkin;

	u32			num_data_lanes;
	bool			non_continous_clk;

	struct gpio_chip	gpio_chip;

	struct v4l2_subdev	sd;
	struct media_pad	pads[2];

	struct v4l2_async_notifier	notifier;

	struct v4l2_subdev	*source_sd;
	u16			source_sd_pad;

	u64			enabled_source_streams;

	/* lock for register access */
	struct mutex		reg_lock;

	u8			current_indirect_target;

	struct clk_hw		clkout_clk_hw;

	enum ub953_mode		mode;

	const struct ds90ub9xx_platform_data	*plat_data;
};

static inline struct ub953_data *sd_to_ub953(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ub953_data, sd);
}

/*
 * HW Access
 */

static int ub953_read(struct ub953_data *priv, u8 reg, u8 *val)
{
	unsigned int v;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = regmap_read(priv->regmap, reg, &v);
	if (ret) {
		dev_err(&priv->client->dev, "Cannot read register 0x%02x: %d\n",
			reg, ret);
		goto out_unlock;
	}

	*val = v;

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub953_write(struct ub953_data *priv, u8 reg, u8 val)
{
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = regmap_write(priv->regmap, reg, val);
	if (ret)
		dev_err(&priv->client->dev,
			"Cannot write register 0x%02x: %d\n", reg, ret);

	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub953_select_ind_reg_block(struct ub953_data *priv, u8 block)
{
	struct device *dev = &priv->client->dev;
	int ret;

	if (priv->current_indirect_target == block)
		return 0;

	ret = regmap_write(priv->regmap, UB953_REG_IND_ACC_CTL, block << 2);
	if (ret) {
		dev_err(dev, "%s: cannot select indirect target %u (%d)\n",
			__func__, block, ret);
		return ret;
	}

	priv->current_indirect_target = block;

	return 0;
}

__maybe_unused
static int ub953_read_ind(struct ub953_data *priv, u8 block, u8 reg, u8 *val)
{
	unsigned int v;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = ub953_select_ind_reg_block(priv, block);
	if (ret)
		goto out_unlock;

	ret = regmap_write(priv->regmap, UB953_REG_IND_ACC_ADDR, reg);
	if (ret) {
		dev_err(&priv->client->dev,
			"Write to IND_ACC_ADDR failed when reading %u:%x02x: %d\n",
			block, reg, ret);
		goto out_unlock;
	}

	ret = regmap_read(priv->regmap, UB953_REG_IND_ACC_DATA, &v);
	if (ret) {
		dev_err(&priv->client->dev,
			"Write to IND_ACC_DATA failed when reading %u:%x02x: %d\n",
			block, reg, ret);
		goto out_unlock;
	}

	*val = v;

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

__maybe_unused
static int ub953_write_ind(struct ub953_data *priv, u8 block, u8 reg, u8 val)
{
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = ub953_select_ind_reg_block(priv, block);
	if (ret)
		goto out_unlock;

	ret = regmap_write(priv->regmap, UB953_REG_IND_ACC_ADDR, reg);
	if (ret) {
		dev_err(&priv->client->dev,
			"Write to IND_ACC_ADDR failed when writing %u:%x02x: %d\n",
			block, reg, ret);
		goto out_unlock;
	}

	ret = regmap_write(priv->regmap, UB953_REG_IND_ACC_DATA, val);
	if (ret) {
		dev_err(&priv->client->dev,
			"Write to IND_ACC_DATA failed when writing %u:%x02x\n: %d\n",
			block, reg, ret);
	}

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

/*
 * GPIO chip
 */
static int ub953_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct ub953_data *priv = gpiochip_get_data(gc);
	int ret;
	u8 v;

	ret = ub953_read(priv, UB953_REG_GPIO_INPUT_CTRL, &v);
	if (ret)
		return ret;

	if (v & UB953_REG_GPIO_INPUT_CTRL_INPUT_EN(offset))
		return GPIO_LINE_DIRECTION_IN;
	else
		return GPIO_LINE_DIRECTION_OUT;
}

static int ub953_gpio_direction_in(struct gpio_chip *gc, unsigned int offset)
{
	struct ub953_data *priv = gpiochip_get_data(gc);

	return regmap_update_bits(priv->regmap, UB953_REG_GPIO_INPUT_CTRL,
				  UB953_REG_GPIO_INPUT_CTRL_INPUT_EN(offset) |
					  UB953_REG_GPIO_INPUT_CTRL_OUT_EN(offset),
				  UB953_REG_GPIO_INPUT_CTRL_INPUT_EN(offset));
}

static int ub953_gpio_direction_out(struct gpio_chip *gc, unsigned int offset,
				    int value)
{
	struct ub953_data *priv = gpiochip_get_data(gc);
	int ret;

	ret = regmap_update_bits(priv->regmap, UB953_REG_LOCAL_GPIO_DATA,
				 UB953_REG_LOCAL_GPIO_DATA_GPIO_OUT_SRC(offset),
				 value ? UB953_REG_LOCAL_GPIO_DATA_GPIO_OUT_SRC(offset) :
					 0);

	if (ret)
		return ret;

	return regmap_update_bits(priv->regmap, UB953_REG_GPIO_INPUT_CTRL,
				  UB953_REG_GPIO_INPUT_CTRL_INPUT_EN(offset) |
					  UB953_REG_GPIO_INPUT_CTRL_OUT_EN(offset),
				  UB953_REG_GPIO_INPUT_CTRL_OUT_EN(offset));
}

static int ub953_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct ub953_data *priv = gpiochip_get_data(gc);
	int ret;
	u8 v;

	ret = ub953_read(priv, UB953_REG_GPIO_PIN_STS, &v);
	if (ret)
		return ret;

	return !!(v & UB953_REG_GPIO_PIN_STS_GPIO_STS(offset));
}

static void ub953_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct ub953_data *priv = gpiochip_get_data(gc);

	regmap_update_bits(priv->regmap, UB953_REG_LOCAL_GPIO_DATA,
			   UB953_REG_LOCAL_GPIO_DATA_GPIO_OUT_SRC(offset),
			   value ? UB953_REG_LOCAL_GPIO_DATA_GPIO_OUT_SRC(offset) :
				   0);
}

static int ub953_gpio_of_xlate(struct gpio_chip *gc,
			       const struct of_phandle_args *gpiospec,
			       u32 *flags)
{
	if (flags)
		*flags = gpiospec->args[1];

	return gpiospec->args[0];
}

static int ub953_gpiochip_probe(struct ub953_data *priv)
{
	struct device *dev = &priv->client->dev;
	struct gpio_chip *gc = &priv->gpio_chip;
	int ret;

	/* Set all GPIOs to local input mode */
	ub953_write(priv, UB953_REG_LOCAL_GPIO_DATA, 0);
	ub953_write(priv, UB953_REG_GPIO_INPUT_CTRL, 0xf);

	gc->label = dev_name(dev);
	gc->parent = dev;
	gc->owner = THIS_MODULE;
	gc->base = -1;
	gc->can_sleep = true;
	gc->ngpio = UB953_NUM_GPIOS;
	gc->get_direction = ub953_gpio_get_direction;
	gc->direction_input = ub953_gpio_direction_in;
	gc->direction_output = ub953_gpio_direction_out;
	gc->get = ub953_gpio_get;
	gc->set = ub953_gpio_set;
	gc->of_xlate = ub953_gpio_of_xlate;
	gc->of_gpio_n_cells = 2;

	ret = gpiochip_add_data(gc, priv);
	if (ret) {
		dev_err(dev, "Failed to add GPIOs: %d\n", ret);
		return ret;
	}

	return 0;
}

static void ub953_gpiochip_remove(struct ub953_data *priv)
{
	gpiochip_remove(&priv->gpio_chip);
}

/*
 * V4L2
 */

static int _ub953_set_routing(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_krouting *routing)
{
	static const struct v4l2_mbus_framefmt format = {
		.width = 640,
		.height = 480,
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.ycbcr_enc = V4L2_YCBCR_ENC_601,
		.quantization = V4L2_QUANTIZATION_LIM_RANGE,
		.xfer_func = V4L2_XFER_FUNC_SRGB,
	};
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

	ret = v4l2_subdev_set_routing_with_fmt(sd, state, routing, &format);
	if (ret)
		return ret;

	return 0;
}

static int ub953_set_routing(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     enum v4l2_subdev_format_whence which,
			     struct v4l2_subdev_krouting *routing)
{
	struct ub953_data *priv = sd_to_ub953(sd);

	if (which == V4L2_SUBDEV_FORMAT_ACTIVE && priv->enabled_source_streams)
		return -EBUSY;

	return _ub953_set_routing(sd, state, routing);
}

static int ub953_get_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_frame_desc *fd)
{
	struct ub953_data *priv = sd_to_ub953(sd);
	struct v4l2_mbus_frame_desc source_fd;
	struct v4l2_subdev_route *route;
	struct v4l2_subdev_state *state;
	int ret;

	if (pad != UB953_PAD_SOURCE)
		return -EINVAL;

	ret = v4l2_subdev_call(priv->source_sd, pad, get_frame_desc,
			       priv->source_sd_pad, &source_fd);
	if (ret)
		return ret;

	fd->type = V4L2_MBUS_FRAME_DESC_TYPE_CSI2;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	for_each_active_route(&state->routing, route) {
		struct v4l2_mbus_frame_desc_entry *source_entry = NULL;
		unsigned int i;

		if (route->source_pad != pad)
			continue;

		for (i = 0; i < source_fd.num_entries; i++) {
			if (source_fd.entry[i].stream == route->sink_stream) {
				source_entry = &source_fd.entry[i];
				break;
			}
		}

		if (!source_entry) {
			dev_err(&priv->client->dev,
				"Failed to find stream from source frame desc\n");
			ret = -EPIPE;
			goto out_unlock;
		}

		fd->entry[fd->num_entries].stream = route->source_stream;
		fd->entry[fd->num_entries].flags = source_entry->flags;
		fd->entry[fd->num_entries].length = source_entry->length;
		fd->entry[fd->num_entries].pixelcode = source_entry->pixelcode;
		fd->entry[fd->num_entries].bus.csi2.vc =
			source_entry->bus.csi2.vc;
		fd->entry[fd->num_entries].bus.csi2.dt =
			source_entry->bus.csi2.dt;

		fd->num_entries++;
	}

out_unlock:
	v4l2_subdev_unlock_state(state);

	return ret;
}

static int ub953_set_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_state *state,
			 struct v4l2_subdev_format *format)
{
	struct ub953_data *priv = sd_to_ub953(sd);
	struct v4l2_mbus_framefmt *fmt;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE &&
	    priv->enabled_source_streams)
		return -EBUSY;

	/* No transcoding, source and sink formats must match. */
	if (format->pad == UB953_PAD_SOURCE)
		return v4l2_subdev_get_fmt(sd, state, format);

	/* Set sink format */
	fmt = v4l2_subdev_state_get_format(state, format->pad, format->stream);
	if (!fmt)
		return -EINVAL;

	*fmt = format->format;

	/* Propagate to source format */
	fmt = v4l2_subdev_state_get_opposite_stream_format(state, format->pad,
							   format->stream);
	if (!fmt)
		return -EINVAL;

	*fmt = format->format;

	return 0;
}

static int ub953_init_state(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_route routes[] = {
		{
			.sink_pad = UB953_PAD_SINK,
			.sink_stream = 0,
			.source_pad = UB953_PAD_SOURCE,
			.source_stream = 0,
			.flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE,
		},
	};

	struct v4l2_subdev_krouting routing = {
		.num_routes = ARRAY_SIZE(routes),
		.routes = routes,
	};

	return _ub953_set_routing(sd, state, &routing);
}

static int ub953_log_status(struct v4l2_subdev *sd)
{
	struct ub953_data *priv = sd_to_ub953(sd);
	struct device *dev = &priv->client->dev;
	u8 v = 0, v1 = 0, v2 = 0;
	unsigned int i;
	char id[UB953_REG_FPD3_RX_ID_LEN];
	u8 gpio_local_data = 0;
	u8 gpio_input_ctrl = 0;
	u8 gpio_pin_sts = 0;

	for (i = 0; i < sizeof(id); i++)
		ub953_read(priv, UB953_REG_FPD3_RX_ID(i), &id[i]);

	dev_info(dev, "ID '%.*s'\n", (int)sizeof(id), id);

	ub953_read(priv, UB953_REG_GENERAL_STATUS, &v);
	dev_info(dev, "GENERAL_STATUS %#02x\n", v);

	ub953_read(priv, UB953_REG_CRC_ERR_CNT1, &v1);
	ub953_read(priv, UB953_REG_CRC_ERR_CNT2, &v2);
	dev_info(dev, "CRC error count %u\n", v1 | (v2 << 8));

	ub953_read(priv, UB953_REG_CSI_ERR_CNT, &v);
	dev_info(dev, "CSI error count %u\n", v);

	ub953_read(priv, UB953_REG_CSI_ERR_STATUS, &v);
	dev_info(dev, "CSI_ERR_STATUS %#02x\n", v);

	ub953_read(priv, UB953_REG_CSI_ERR_DLANE01, &v);
	dev_info(dev, "CSI_ERR_DLANE01 %#02x\n", v);

	ub953_read(priv, UB953_REG_CSI_ERR_DLANE23, &v);
	dev_info(dev, "CSI_ERR_DLANE23 %#02x\n", v);

	ub953_read(priv, UB953_REG_CSI_ERR_CLK_LANE, &v);
	dev_info(dev, "CSI_ERR_CLK_LANE %#02x\n", v);

	ub953_read(priv, UB953_REG_CSI_PKT_HDR_VC_ID, &v);
	dev_info(dev, "CSI packet header VC %u ID %u\n", v >> 6, v & 0x3f);

	ub953_read(priv, UB953_REG_PKT_HDR_WC_LSB, &v1);
	ub953_read(priv, UB953_REG_PKT_HDR_WC_MSB, &v2);
	dev_info(dev, "CSI packet header WC %u\n", (v2 << 8) | v1);

	ub953_read(priv, UB953_REG_CSI_ECC, &v);
	dev_info(dev, "CSI ECC %#02x\n", v);

	ub953_read(priv, UB953_REG_LOCAL_GPIO_DATA, &gpio_local_data);
	ub953_read(priv, UB953_REG_GPIO_INPUT_CTRL, &gpio_input_ctrl);
	ub953_read(priv, UB953_REG_GPIO_PIN_STS, &gpio_pin_sts);

	for (i = 0; i < UB953_NUM_GPIOS; i++) {
		dev_info(dev,
			 "GPIO%u: remote: %u is_input: %u is_output: %u val: %u sts: %u\n",
			 i,
			 !!(gpio_local_data & UB953_REG_LOCAL_GPIO_DATA_GPIO_RMTEN(i)),
			 !!(gpio_input_ctrl & UB953_REG_GPIO_INPUT_CTRL_INPUT_EN(i)),
			 !!(gpio_input_ctrl & UB953_REG_GPIO_INPUT_CTRL_OUT_EN(i)),
			 !!(gpio_local_data & UB953_REG_LOCAL_GPIO_DATA_GPIO_OUT_SRC(i)),
			 !!(gpio_pin_sts & UB953_REG_GPIO_PIN_STS_GPIO_STS(i)));
	}

	return 0;
}

static int ub953_enable_streams(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state, u32 pad,
				u64 streams_mask)
{
	struct ub953_data *priv = sd_to_ub953(sd);
	u64 sink_streams;
	int ret;

	sink_streams = v4l2_subdev_state_xlate_streams(state, UB953_PAD_SOURCE,
						       UB953_PAD_SINK,
						       &streams_mask);

	ret = v4l2_subdev_enable_streams(priv->source_sd, priv->source_sd_pad,
					 sink_streams);
	if (ret)
		return ret;

	priv->enabled_source_streams |= streams_mask;

	return 0;
}

static int ub953_disable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct ub953_data *priv = sd_to_ub953(sd);
	u64 sink_streams;
	int ret;

	sink_streams = v4l2_subdev_state_xlate_streams(state, UB953_PAD_SOURCE,
						       UB953_PAD_SINK,
						       &streams_mask);

	ret = v4l2_subdev_disable_streams(priv->source_sd, priv->source_sd_pad,
					  sink_streams);
	if (ret)
		return ret;

	priv->enabled_source_streams &= ~streams_mask;

	return 0;
}

static const struct v4l2_subdev_pad_ops ub953_pad_ops = {
	.enable_streams = ub953_enable_streams,
	.disable_streams = ub953_disable_streams,
	.set_routing = ub953_set_routing,
	.get_frame_desc = ub953_get_frame_desc,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ub953_set_fmt,
};

static const struct v4l2_subdev_core_ops ub953_subdev_core_ops = {
	.log_status = ub953_log_status,
};

static const struct v4l2_subdev_ops ub953_subdev_ops = {
	.core = &ub953_subdev_core_ops,
	.pad = &ub953_pad_ops,
};

static const struct v4l2_subdev_internal_ops ub953_internal_ops = {
	.init_state = ub953_init_state,
};

static const struct media_entity_operations ub953_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int ub953_notify_bound(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *source_subdev,
			      struct v4l2_async_connection *asd)
{
	struct ub953_data *priv = sd_to_ub953(notifier->sd);
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
				    &priv->sd.entity, 0,
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

static const struct v4l2_async_notifier_operations ub953_notify_ops = {
	.bound = ub953_notify_bound,
};

static int ub953_v4l2_notifier_register(struct ub953_data *priv)
{
	struct device *dev = &priv->client->dev;
	struct v4l2_async_connection *asd;
	struct fwnode_handle *ep_fwnode;
	int ret;

	ep_fwnode = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev),
						    UB953_PAD_SINK, 0, 0);
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

	priv->notifier.ops = &ub953_notify_ops;

	ret = v4l2_async_nf_register(&priv->notifier);
	if (ret) {
		dev_err(dev, "Failed to register subdev_notifier");
		v4l2_async_nf_cleanup(&priv->notifier);
		return ret;
	}

	return 0;
}

static void ub953_v4l2_notifier_unregister(struct ub953_data *priv)
{
	v4l2_async_nf_unregister(&priv->notifier);
	v4l2_async_nf_cleanup(&priv->notifier);
}

/*
 * Probing
 */

static int ub953_i2c_master_init(struct ub953_data *priv)
{
	/* i2c fast mode */
	u32 ref = 26250000;
	u32 scl_high = 915; /* ns */
	u32 scl_low = 1641; /* ns */
	int ret;

	scl_high = div64_u64((u64)scl_high * ref, 1000000000) - 5;
	scl_low = div64_u64((u64)scl_low * ref, 1000000000) - 5;

	ret = ub953_write(priv, UB953_REG_SCL_HIGH_TIME, scl_high);
	if (ret)
		return ret;

	ret = ub953_write(priv, UB953_REG_SCL_LOW_TIME, scl_low);
	if (ret)
		return ret;

	return 0;
}

static u64 ub953_get_fc_rate(struct ub953_data *priv)
{
	switch (priv->mode) {
	case UB953_MODE_SYNC:
		if (priv->hw_data->is_ub971)
			return priv->plat_data->bc_rate * 160ull;
		else
			return priv->plat_data->bc_rate / 2 * 160ull;

	case UB953_MODE_NONSYNC_EXT:
		/* CLKIN_DIV = 1 always */
		return clk_get_rate(priv->clkin) * 80ull;

	default:
		/* Not supported */
		return 0;
	}
}

static unsigned long ub953_calc_clkout_ub953(struct ub953_data *priv,
					     unsigned long target, u64 fc,
					     u8 *hs_div, u8 *m, u8 *n)
{
	/*
	 * We always use 4 as a pre-divider (HS_CLK_DIV = 2).
	 *
	 * According to the datasheet:
	 * - "HS_CLK_DIV typically should be set to either 16, 8, or 4 (default)."
	 * - "if it is not possible to have an integer ratio of N/M, it is best to
	 *    select a smaller value for HS_CLK_DIV.
	 *
	 * For above reasons the default HS_CLK_DIV seems the best in the average
	 * case. Use always that value to keep the code simple.
	 */
	static const unsigned long hs_clk_div = 4;

	u64 fc_divided;
	unsigned long mul, div;
	unsigned long res;

	/* clkout = fc / hs_clk_div * m / n */

	fc_divided = div_u64(fc, hs_clk_div);

	rational_best_approximation(target, fc_divided, (1 << 5) - 1,
				    (1 << 8) - 1, &mul, &div);

	res = div_u64(fc_divided * mul, div);

	*hs_div = hs_clk_div;
	*m = mul;
	*n = div;

	return res;
}

static unsigned long ub953_calc_clkout_ub971(struct ub953_data *priv,
					     unsigned long target, u64 fc,
					     u8 *m, u8 *n)
{
	u64 fc_divided;
	unsigned long mul, div;
	unsigned long res;

	/* clkout = fc * m / (8 * n) */

	fc_divided = div_u64(fc, 8);

	rational_best_approximation(target, fc_divided, (1 << 5) - 1,
				    (1 << 8) - 1, &mul, &div);

	res = div_u64(fc_divided * mul, div);

	*m = mul;
	*n = div;

	return res;
}

static void ub953_calc_clkout_params(struct ub953_data *priv,
				     unsigned long target_rate,
				     struct ub953_clkout_data *clkout_data)
{
	struct device *dev = &priv->client->dev;
	unsigned long clkout_rate;
	u64 fc_rate;

	fc_rate = ub953_get_fc_rate(priv);

	if (priv->hw_data->is_ub971) {
		u8 m, n;

		clkout_rate = ub953_calc_clkout_ub971(priv, target_rate,
						      fc_rate, &m, &n);

		clkout_data->m = m;
		clkout_data->n = n;

		dev_dbg(dev, "%s %llu * %u / (8 * %u) = %lu (requested %lu)",
			__func__, fc_rate, m, n, clkout_rate, target_rate);
	} else {
		u8 hs_div, m, n;

		clkout_rate = ub953_calc_clkout_ub953(priv, target_rate,
						      fc_rate, &hs_div, &m, &n);

		clkout_data->hs_div = hs_div;
		clkout_data->m = m;
		clkout_data->n = n;

		dev_dbg(dev, "%s %llu / %u * %u / %u = %lu (requested %lu)",
			__func__, fc_rate, hs_div, m, n, clkout_rate,
			target_rate);
	}

	clkout_data->rate = clkout_rate;
}

static void ub953_write_clkout_regs(struct ub953_data *priv,
				    const struct ub953_clkout_data *clkout_data)
{
	u8 clkout_ctrl0, clkout_ctrl1;

	if (priv->hw_data->is_ub971)
		clkout_ctrl0 = clkout_data->m;
	else
		clkout_ctrl0 = (__ffs(clkout_data->hs_div) << 5) |
			       clkout_data->m;

	clkout_ctrl1 = clkout_data->n;

	ub953_write(priv, UB953_REG_CLKOUT_CTRL0, clkout_ctrl0);
	ub953_write(priv, UB953_REG_CLKOUT_CTRL1, clkout_ctrl1);
}

static unsigned long ub953_clkout_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct ub953_data *priv = container_of(hw, struct ub953_data, clkout_clk_hw);
	struct device *dev = &priv->client->dev;
	u8 ctrl0, ctrl1;
	u32 mul, div;
	u64 fc_rate;
	u32 hs_clk_div;
	u64 rate;
	int ret;

	ret = ub953_read(priv, UB953_REG_CLKOUT_CTRL0, &ctrl0);
	if (ret) {
		dev_err(dev, "Failed to read CLKOUT_CTRL0: %d\n", ret);
		return 0;
	}

	ret = ub953_read(priv, UB953_REG_CLKOUT_CTRL1, &ctrl1);
	if (ret) {
		dev_err(dev, "Failed to read CLKOUT_CTRL1: %d\n", ret);
		return 0;
	}

	fc_rate = ub953_get_fc_rate(priv);

	if (priv->hw_data->is_ub971) {
		mul = ctrl0 & 0x1f;
		div = ctrl1;

		if (div == 0)
			return 0;

		rate = div_u64(fc_rate * mul, 8 * div);

		dev_dbg(dev, "clkout: fc rate %llu, mul %u, div %u = %llu\n",
			fc_rate, mul, div, rate);
	} else {
		mul = ctrl0 & 0x1f;
		hs_clk_div = 1 << (ctrl0 >> 5);
		div = ctrl1;

		if (div == 0)
			return 0;

		rate = div_u64(div_u64(fc_rate, hs_clk_div) * mul, div);

		dev_dbg(dev,
			"clkout: fc rate %llu, hs_clk_div %u, mul %u, div %u = %llu\n",
			fc_rate, hs_clk_div, mul, div, rate);
	}

	return rate;
}

static long ub953_clkout_round_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *parent_rate)
{
	struct ub953_data *priv = container_of(hw, struct ub953_data, clkout_clk_hw);
	struct ub953_clkout_data clkout_data;

	ub953_calc_clkout_params(priv, rate, &clkout_data);

	return clkout_data.rate;
}

static int ub953_clkout_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct ub953_data *priv = container_of(hw, struct ub953_data, clkout_clk_hw);
	struct ub953_clkout_data clkout_data;

	ub953_calc_clkout_params(priv, rate, &clkout_data);

	dev_dbg(&priv->client->dev, "%s %lu (requested %lu)\n", __func__,
		clkout_data.rate, rate);

	ub953_write_clkout_regs(priv, &clkout_data);

	return 0;
}

static const struct clk_ops ub953_clkout_ops = {
	.recalc_rate	= ub953_clkout_recalc_rate,
	.round_rate	= ub953_clkout_round_rate,
	.set_rate	= ub953_clkout_set_rate,
};

static int ub953_register_clkout(struct ub953_data *priv)
{
	struct device *dev = &priv->client->dev;
	const struct clk_init_data init = {
		.name = kasprintf(GFP_KERNEL, "ds90%s.%s.clk_out",
				  priv->hw_data->model, dev_name(dev)),
		.ops = &ub953_clkout_ops,
	};
	struct ub953_clkout_data clkout_data;
	int ret;

	if (!init.name)
		return -ENOMEM;

	/* Initialize clkout to 25MHz by default */
	ub953_calc_clkout_params(priv, UB953_DEFAULT_CLKOUT_RATE, &clkout_data);
	ub953_write_clkout_regs(priv, &clkout_data);

	priv->clkout_clk_hw.init = &init;

	ret = devm_clk_hw_register(dev, &priv->clkout_clk_hw);
	kfree(init.name);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot register clock HW\n");

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					  &priv->clkout_clk_hw);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Cannot add OF clock provider\n");

	return 0;
}

static int ub953_add_i2c_adapter(struct ub953_data *priv)
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

static const struct regmap_config ub953_regmap_config = {
	.name = "ds90ub953",
	.reg_bits = 8,
	.val_bits = 8,
	.reg_format_endian = REGMAP_ENDIAN_DEFAULT,
	.val_format_endian = REGMAP_ENDIAN_DEFAULT,
};

static int ub953_parse_dt(struct ub953_data *priv)
{
	struct device *dev = &priv->client->dev;
	struct v4l2_fwnode_endpoint vep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *ep_fwnode;
	unsigned char nlanes;
	int ret;

	ep_fwnode = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev),
						    UB953_PAD_SINK, 0, 0);
	if (!ep_fwnode)
		return dev_err_probe(dev, -ENOENT, "no endpoint found\n");

	ret = v4l2_fwnode_endpoint_parse(ep_fwnode, &vep);

	fwnode_handle_put(ep_fwnode);

	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to parse sink endpoint data\n");

	nlanes = vep.bus.mipi_csi2.num_data_lanes;
	if (nlanes != 1 && nlanes != 2 && nlanes != 4)
		return dev_err_probe(dev, -EINVAL,
				     "bad number of data-lanes: %u\n", nlanes);

	priv->num_data_lanes = nlanes;

	priv->non_continous_clk = vep.bus.mipi_csi2.flags &
				  V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK;

	return 0;
}

static int ub953_hw_init(struct ub953_data *priv)
{
	struct device *dev = &priv->client->dev;
	bool mode_override;
	int ret;
	u8 v;

	ret = ub953_read(priv, UB953_REG_MODE_SEL, &v);
	if (ret)
		return ret;

	if (!(v & UB953_REG_MODE_SEL_MODE_DONE))
		return dev_err_probe(dev, -EIO, "Mode value not stabilized\n");

	mode_override = v & UB953_REG_MODE_SEL_MODE_OVERRIDE;

	switch (v & UB953_REG_MODE_SEL_MODE_MASK) {
	case 0:
		priv->mode = UB953_MODE_SYNC;
		break;
	case 2:
		priv->mode = UB953_MODE_NONSYNC_EXT;
		break;
	case 3:
		priv->mode = UB953_MODE_NONSYNC_INT;
		break;
	case 5:
		priv->mode = UB953_MODE_DVP;
		break;
	default:
		return dev_err_probe(dev, -EIO,
				     "Invalid mode in mode register\n");
	}

	dev_dbg(dev, "mode from %s: %#x\n", mode_override ? "reg" : "strap",
		priv->mode);

	if (priv->mode != UB953_MODE_SYNC &&
	    priv->mode != UB953_MODE_NONSYNC_EXT)
		return dev_err_probe(dev, -ENODEV,
				     "Unsupported mode selected: %u\n",
				     priv->mode);

	if (priv->mode == UB953_MODE_NONSYNC_EXT && !priv->clkin)
		return dev_err_probe(dev, -EINVAL,
				     "clkin required for non-sync ext mode\n");

	ret = ub953_read(priv, UB953_REG_REV_MASK_ID, &v);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read revision");

	dev_info(dev, "Found %s rev/mask %#04x\n", priv->hw_data->model, v);

	ret = ub953_read(priv, UB953_REG_GENERAL_CFG, &v);
	if (ret)
		return ret;

	dev_dbg(dev, "i2c strap setting %s V\n",
		(v & UB953_REG_GENERAL_CFG_I2C_STRAP_MODE) ? "1.8" : "3.3");

	ret = ub953_i2c_master_init(priv);
	if (ret)
		return dev_err_probe(dev, ret, "i2c init failed\n");

	ub953_write(priv, UB953_REG_GENERAL_CFG,
		    (priv->non_continous_clk ? 0 : UB953_REG_GENERAL_CFG_CONT_CLK) |
		    ((priv->num_data_lanes - 1) << UB953_REG_GENERAL_CFG_CSI_LANE_SEL_SHIFT) |
		    UB953_REG_GENERAL_CFG_CRC_TX_GEN_ENABLE);

	return 0;
}

static int ub953_subdev_init(struct ub953_data *priv)
{
	struct device *dev = &priv->client->dev;
	int ret;

	v4l2_i2c_subdev_init(&priv->sd, priv->client, &ub953_subdev_ops);
	priv->sd.internal_ops = &ub953_internal_ops;

	priv->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			  V4L2_SUBDEV_FL_STREAMS;
	priv->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	priv->sd.entity.ops = &ub953_entity_ops;

	priv->pads[0].flags = MEDIA_PAD_FL_SINK;
	priv->pads[1].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&priv->sd.entity, 2, priv->pads);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init pads\n");

	ret = v4l2_subdev_init_finalize(&priv->sd);
	if (ret)
		goto err_entity_cleanup;

	ret = ub953_v4l2_notifier_register(priv);
	if (ret) {
		dev_err_probe(dev, ret,
			      "v4l2 subdev notifier register failed\n");
		goto err_free_state;
	}

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret) {
		dev_err_probe(dev, ret, "v4l2_async_register_subdev error\n");
		goto err_unreg_notif;
	}

	return 0;

err_unreg_notif:
	ub953_v4l2_notifier_unregister(priv);
err_free_state:
	v4l2_subdev_cleanup(&priv->sd);
err_entity_cleanup:
	media_entity_cleanup(&priv->sd.entity);

	return ret;
}

static void ub953_subdev_uninit(struct ub953_data *priv)
{
	v4l2_async_unregister_subdev(&priv->sd);
	ub953_v4l2_notifier_unregister(priv);
	v4l2_subdev_cleanup(&priv->sd);
	fwnode_handle_put(priv->sd.fwnode);
	media_entity_cleanup(&priv->sd.entity);
}

static int ub953_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ub953_data *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;

	priv->hw_data = device_get_match_data(dev);

	priv->plat_data = dev_get_platdata(&client->dev);
	if (!priv->plat_data)
		return dev_err_probe(dev, -ENODEV, "Platform data missing\n");

	mutex_init(&priv->reg_lock);

	/*
	 * Initialize to invalid values so that the first reg writes will
	 * configure the target.
	 */
	priv->current_indirect_target = 0xff;

	priv->regmap = devm_regmap_init_i2c(client, &ub953_regmap_config);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err_probe(dev, ret, "Failed to init regmap\n");
		goto err_mutex_destroy;
	}

	priv->clkin = devm_clk_get_optional(dev, "clkin");
	if (IS_ERR(priv->clkin)) {
		ret = PTR_ERR(priv->clkin);
		dev_err_probe(dev, ret, "failed to parse 'clkin'\n");
		goto err_mutex_destroy;
	}

	ret = ub953_parse_dt(priv);
	if (ret)
		goto err_mutex_destroy;

	ret = ub953_hw_init(priv);
	if (ret)
		goto err_mutex_destroy;

	ret = ub953_gpiochip_probe(priv);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to init gpiochip\n");
		goto err_mutex_destroy;
	}

	ret = ub953_register_clkout(priv);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to register clkout\n");
		goto err_gpiochip_remove;
	}

	ret = ub953_subdev_init(priv);
	if (ret)
		goto err_gpiochip_remove;

	ret = ub953_add_i2c_adapter(priv);
	if (ret) {
		dev_err_probe(dev, ret, "failed to add remote i2c adapter\n");
		goto err_subdev_uninit;
	}

	return 0;

err_subdev_uninit:
	ub953_subdev_uninit(priv);
err_gpiochip_remove:
	ub953_gpiochip_remove(priv);
err_mutex_destroy:
	mutex_destroy(&priv->reg_lock);

	return ret;
}

static void ub953_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ub953_data *priv = sd_to_ub953(sd);

	i2c_atr_del_adapter(priv->plat_data->atr, priv->plat_data->port);

	ub953_subdev_uninit(priv);

	ub953_gpiochip_remove(priv);
	mutex_destroy(&priv->reg_lock);
}

static const struct ub953_hw_data ds90ub953_hw = {
	.model = "ub953",
};

static const struct ub953_hw_data ds90ub971_hw = {
	.model = "ub971",
	.is_ub971 = true,
};

static const struct i2c_device_id ub953_id[] = {
	{ "ds90ub953-q1", (kernel_ulong_t)&ds90ub953_hw },
	{ "ds90ub971-q1", (kernel_ulong_t)&ds90ub971_hw },
	{}
};
MODULE_DEVICE_TABLE(i2c, ub953_id);

static const struct of_device_id ub953_dt_ids[] = {
	{ .compatible = "ti,ds90ub953-q1", .data = &ds90ub953_hw },
	{ .compatible = "ti,ds90ub971-q1", .data = &ds90ub971_hw },
	{}
};
MODULE_DEVICE_TABLE(of, ub953_dt_ids);

static struct i2c_driver ds90ub953_driver = {
	.probe		= ub953_probe,
	.remove		= ub953_remove,
	.id_table	= ub953_id,
	.driver = {
		.name	= "ds90ub953",
		.of_match_table = ub953_dt_ids,
	},
};
module_i2c_driver(ds90ub953_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Texas Instruments FPD-Link III/IV CSI-2 Serializers Driver");
MODULE_AUTHOR("Luca Ceresoli <luca@lucaceresoli.net>");
MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ideasonboard.com>");
MODULE_IMPORT_NS(I2C_ATR);

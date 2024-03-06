// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for ST MIPID02 CSI-2 to PARALLEL bridge
 *
 * Copyright (C) STMicroelectronics SA 2019
 * Authors: Mickael Guene <mickael.guene@st.com>
 *          for STMicroelectronics.
 *
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <media/mipi-csi2.h>
#include <media/v4l2-async.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define MIPID02_CLK_LANE_WR_REG1	CCI_REG8(0x01)
#define MIPID02_CLK_LANE_REG1		CCI_REG8(0x02)
#define MIPID02_CLK_LANE_REG3		CCI_REG8(0x04)
#define MIPID02_DATA_LANE0_REG1		CCI_REG8(0x05)
#define MIPID02_DATA_LANE0_REG2		CCI_REG8(0x06)
#define MIPID02_DATA_LANE1_REG1		CCI_REG8(0x09)
#define MIPID02_DATA_LANE1_REG2		CCI_REG8(0x0a)
#define MIPID02_MODE_REG1		CCI_REG8(0x14)
#define MIPID02_MODE_REG2		CCI_REG8(0x15)
#define MIPID02_DATA_ID_RREG		CCI_REG8(0x17)
#define MIPID02_DATA_SELECTION_CTRL	CCI_REG8(0x19)
#define MIPID02_PIX_WIDTH_CTRL		CCI_REG8(0x1e)
#define MIPID02_PIX_WIDTH_CTRL_EMB	CCI_REG8(0x1f)

/* Bits definition for MIPID02_CLK_LANE_REG1 */
#define CLK_ENABLE					BIT(0)
/* Bits definition for MIPID02_CLK_LANE_REG3 */
#define CLK_MIPI_CSI					BIT(1)
/* Bits definition for MIPID02_DATA_LANE0_REG1 */
#define DATA_ENABLE					BIT(0)
/* Bits definition for MIPID02_DATA_LANEx_REG2 */
#define DATA_MIPI_CSI					BIT(0)
/* Bits definition for MIPID02_MODE_REG1 */
#define MODE_DATA_SWAP					BIT(2)
#define MODE_NO_BYPASS					BIT(6)
/* Bits definition for MIPID02_MODE_REG2 */
#define MODE_HSYNC_ACTIVE_HIGH				BIT(1)
#define MODE_VSYNC_ACTIVE_HIGH				BIT(2)
#define MODE_PCLK_SAMPLE_RISING				BIT(3)
/* Bits definition for MIPID02_DATA_SELECTION_CTRL */
#define SELECTION_MANUAL_DATA				BIT(2)
#define SELECTION_MANUAL_WIDTH				BIT(3)

static const u32 mipid02_supported_fmt_codes[] = {
	MEDIA_BUS_FMT_SBGGR8_1X8, MEDIA_BUS_FMT_SGBRG8_1X8,
	MEDIA_BUS_FMT_SGRBG8_1X8, MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_SBGGR10_1X10, MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10, MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SBGGR12_1X12, MEDIA_BUS_FMT_SGBRG12_1X12,
	MEDIA_BUS_FMT_SGRBG12_1X12, MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_YUYV8_1X16, MEDIA_BUS_FMT_YVYU8_1X16,
	MEDIA_BUS_FMT_UYVY8_1X16, MEDIA_BUS_FMT_VYUY8_1X16,
	MEDIA_BUS_FMT_RGB565_1X16, MEDIA_BUS_FMT_BGR888_1X24,
	MEDIA_BUS_FMT_RGB565_2X8_LE, MEDIA_BUS_FMT_RGB565_2X8_BE,
	MEDIA_BUS_FMT_YUYV8_2X8, MEDIA_BUS_FMT_YVYU8_2X8,
	MEDIA_BUS_FMT_UYVY8_2X8, MEDIA_BUS_FMT_VYUY8_2X8,
	MEDIA_BUS_FMT_Y8_1X8, MEDIA_BUS_FMT_JPEG_1X8
};

/* regulator supplies */
static const char * const mipid02_supply_name[] = {
	"VDDE", /* 1.8V digital I/O supply */
	"VDDIN", /* 1V8 voltage regulator supply */
};

#define MIPID02_NUM_SUPPLIES		ARRAY_SIZE(mipid02_supply_name)

#define MIPID02_SINK_0			0
#define MIPID02_SINK_1			1
#define MIPID02_SOURCE			2
#define MIPID02_PAD_NB			3

struct mipid02_dev {
	struct i2c_client *i2c_client;
	struct regulator_bulk_data supplies[MIPID02_NUM_SUPPLIES];
	struct v4l2_subdev sd;
	struct regmap *regmap;
	struct media_pad pad[MIPID02_PAD_NB];
	struct clk *xclk;
	struct gpio_desc *reset_gpio;
	/* endpoints info */
	struct v4l2_fwnode_endpoint rx;
	struct v4l2_fwnode_endpoint tx;
	/* remote source */
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *s_subdev;
	/* registers */
	struct {
		u8 clk_lane_reg1;
		u8 data_lane0_reg1;
		u8 data_lane1_reg1;
		u8 mode_reg1;
		u8 mode_reg2;
		u8 data_selection_ctrl;
		u8 data_id_rreg;
		u8 pix_width_ctrl;
		u8 pix_width_ctrl_emb;
	} r;
};

static int bpp_from_code(__u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_Y8_1X8:
		return 8;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		return 10;
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		return 12;
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YVYU8_1X16:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_VYUY8_1X16:
	case MEDIA_BUS_FMT_RGB565_1X16:
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_VYUY8_2X8:
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
	case MEDIA_BUS_FMT_RGB565_2X8_BE:
		return 16;
	case MEDIA_BUS_FMT_BGR888_1X24:
		return 24;
	default:
		return 0;
	}
}

static u8 data_type_from_code(__u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_Y8_1X8:
		return MIPI_CSI2_DT_RAW8;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		return MIPI_CSI2_DT_RAW10;
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		return MIPI_CSI2_DT_RAW12;
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YVYU8_1X16:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_VYUY8_1X16:
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_VYUY8_2X8:
		return MIPI_CSI2_DT_YUV422_8B;
	case MEDIA_BUS_FMT_BGR888_1X24:
		return MIPI_CSI2_DT_RGB888;
	case MEDIA_BUS_FMT_RGB565_1X16:
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
	case MEDIA_BUS_FMT_RGB565_2X8_BE:
		return MIPI_CSI2_DT_RGB565;
	default:
		return 0;
	}
}

static __u32 get_fmt_code(__u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mipid02_supported_fmt_codes); i++) {
		if (code == mipid02_supported_fmt_codes[i])
			return code;
	}

	return mipid02_supported_fmt_codes[0];
}

static __u32 serial_to_parallel_code(__u32 serial)
{
	if (serial == MEDIA_BUS_FMT_RGB565_1X16)
		return MEDIA_BUS_FMT_RGB565_2X8_LE;
	if (serial == MEDIA_BUS_FMT_YUYV8_1X16)
		return MEDIA_BUS_FMT_YUYV8_2X8;
	if (serial == MEDIA_BUS_FMT_YVYU8_1X16)
		return MEDIA_BUS_FMT_YVYU8_2X8;
	if (serial == MEDIA_BUS_FMT_UYVY8_1X16)
		return MEDIA_BUS_FMT_UYVY8_2X8;
	if (serial == MEDIA_BUS_FMT_VYUY8_1X16)
		return MEDIA_BUS_FMT_VYUY8_2X8;
	if (serial == MEDIA_BUS_FMT_BGR888_1X24)
		return MEDIA_BUS_FMT_BGR888_3X8;

	return serial;
}

static inline struct mipid02_dev *to_mipid02_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mipid02_dev, sd);
}

static int mipid02_get_regulators(struct mipid02_dev *bridge)
{
	unsigned int i;

	for (i = 0; i < MIPID02_NUM_SUPPLIES; i++)
		bridge->supplies[i].supply = mipid02_supply_name[i];

	return devm_regulator_bulk_get(&bridge->i2c_client->dev,
				       MIPID02_NUM_SUPPLIES,
				       bridge->supplies);
}

static void mipid02_apply_reset(struct mipid02_dev *bridge)
{
	gpiod_set_value_cansleep(bridge->reset_gpio, 0);
	usleep_range(5000, 10000);
	gpiod_set_value_cansleep(bridge->reset_gpio, 1);
	usleep_range(5000, 10000);
	gpiod_set_value_cansleep(bridge->reset_gpio, 0);
	usleep_range(5000, 10000);
}

static int mipid02_set_power_on(struct mipid02_dev *bridge)
{
	struct i2c_client *client = bridge->i2c_client;
	int ret;

	ret = clk_prepare_enable(bridge->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n", __func__);
		return ret;
	}

	ret = regulator_bulk_enable(MIPID02_NUM_SUPPLIES,
				    bridge->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			    __func__);
		goto xclk_off;
	}

	if (bridge->reset_gpio) {
		dev_dbg(&client->dev, "apply reset");
		mipid02_apply_reset(bridge);
	} else {
		dev_dbg(&client->dev, "don't apply reset");
		usleep_range(5000, 10000);
	}

	return 0;

xclk_off:
	clk_disable_unprepare(bridge->xclk);
	return ret;
}

static void mipid02_set_power_off(struct mipid02_dev *bridge)
{
	regulator_bulk_disable(MIPID02_NUM_SUPPLIES, bridge->supplies);
	clk_disable_unprepare(bridge->xclk);
}

static int mipid02_detect(struct mipid02_dev *bridge)
{
	u64 reg;

	/*
	 * There is no version registers. Just try to read register
	 * MIPID02_CLK_LANE_WR_REG1.
	 */
	return cci_read(bridge->regmap, MIPID02_CLK_LANE_WR_REG1, &reg, NULL);
}

/*
 * We need to know link frequency to setup clk_lane_reg1 timings. Link frequency
 * will be retrieve from connected device via v4l2_get_link_freq, bit per pixel
 * and number of lanes.
 */
static int mipid02_configure_from_rx_speed(struct mipid02_dev *bridge,
					   struct v4l2_mbus_framefmt *fmt)
{
	struct i2c_client *client = bridge->i2c_client;
	struct v4l2_subdev *subdev = bridge->s_subdev;
	struct v4l2_fwnode_endpoint *ep = &bridge->rx;
	u32 bpp = bpp_from_code(fmt->code);
	/*
	 * clk_lane_reg1 requires 4 times the unit interval time, and bitrate
	 * is twice the link frequency, hence ui_4 = 1000000000 * 4 / 2
	 */
	u64 ui_4 = 2000000000;
	s64 link_freq;

	link_freq = v4l2_get_link_freq(subdev->ctrl_handler, bpp,
				       2 * ep->bus.mipi_csi2.num_data_lanes);
	if (link_freq < 0) {
		dev_err(&client->dev, "Failed to get link frequency");
		return -EINVAL;
	}

	dev_dbg(&client->dev, "detect link_freq = %lld Hz", link_freq);
	do_div(ui_4, link_freq);
	bridge->r.clk_lane_reg1 |= ui_4 << 2;

	return 0;
}

static int mipid02_configure_clk_lane(struct mipid02_dev *bridge)
{
	struct i2c_client *client = bridge->i2c_client;
	struct v4l2_fwnode_endpoint *ep = &bridge->rx;
	bool *polarities = ep->bus.mipi_csi2.lane_polarities;

	/* midid02 doesn't support clock lane remapping */
	if (ep->bus.mipi_csi2.clock_lane != 0) {
		dev_err(&client->dev, "clk lane must be map to lane 0\n");
		return -EINVAL;
	}
	bridge->r.clk_lane_reg1 |= (polarities[0] << 1) | CLK_ENABLE;

	return 0;
}

static int mipid02_configure_data0_lane(struct mipid02_dev *bridge, int nb,
					bool are_lanes_swap, bool *polarities)
{
	bool are_pin_swap = are_lanes_swap ? polarities[2] : polarities[1];

	if (nb == 1 && are_lanes_swap)
		return 0;

	/*
	 * data lane 0 as pin swap polarity reversed compared to clock and
	 * data lane 1
	 */
	if (!are_pin_swap)
		bridge->r.data_lane0_reg1 = 1 << 1;
	bridge->r.data_lane0_reg1 |= DATA_ENABLE;

	return 0;
}

static int mipid02_configure_data1_lane(struct mipid02_dev *bridge, int nb,
					bool are_lanes_swap, bool *polarities)
{
	bool are_pin_swap = are_lanes_swap ? polarities[1] : polarities[2];

	if (nb == 1 && !are_lanes_swap)
		return 0;

	if (are_pin_swap)
		bridge->r.data_lane1_reg1 = 1 << 1;
	bridge->r.data_lane1_reg1 |= DATA_ENABLE;

	return 0;
}

static int mipid02_configure_from_rx(struct mipid02_dev *bridge,
				     struct v4l2_mbus_framefmt *fmt)
{
	struct v4l2_fwnode_endpoint *ep = &bridge->rx;
	bool are_lanes_swap = ep->bus.mipi_csi2.data_lanes[0] == 2;
	bool *polarities = ep->bus.mipi_csi2.lane_polarities;
	int nb = ep->bus.mipi_csi2.num_data_lanes;
	int ret;

	ret = mipid02_configure_clk_lane(bridge);
	if (ret)
		return ret;

	ret = mipid02_configure_data0_lane(bridge, nb, are_lanes_swap,
					   polarities);
	if (ret)
		return ret;

	ret = mipid02_configure_data1_lane(bridge, nb, are_lanes_swap,
					   polarities);
	if (ret)
		return ret;

	bridge->r.mode_reg1 |= are_lanes_swap ? MODE_DATA_SWAP : 0;
	bridge->r.mode_reg1 |= (nb - 1) << 1;

	return mipid02_configure_from_rx_speed(bridge, fmt);
}

static int mipid02_configure_from_tx(struct mipid02_dev *bridge)
{
	struct v4l2_fwnode_endpoint *ep = &bridge->tx;

	bridge->r.data_selection_ctrl = SELECTION_MANUAL_WIDTH;
	bridge->r.pix_width_ctrl = ep->bus.parallel.bus_width;
	bridge->r.pix_width_ctrl_emb = ep->bus.parallel.bus_width;
	if (ep->bus.parallel.flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH)
		bridge->r.mode_reg2 |= MODE_HSYNC_ACTIVE_HIGH;
	if (ep->bus.parallel.flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH)
		bridge->r.mode_reg2 |= MODE_VSYNC_ACTIVE_HIGH;
	if (ep->bus.parallel.flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
		bridge->r.mode_reg2 |= MODE_PCLK_SAMPLE_RISING;

	return 0;
}

static int mipid02_configure_from_code(struct mipid02_dev *bridge,
				       struct v4l2_mbus_framefmt *fmt)
{
	u8 data_type;

	bridge->r.data_id_rreg = 0;

	if (fmt->code != MEDIA_BUS_FMT_JPEG_1X8) {
		bridge->r.data_selection_ctrl |= SELECTION_MANUAL_DATA;

		data_type = data_type_from_code(fmt->code);
		if (!data_type)
			return -EINVAL;
		bridge->r.data_id_rreg = data_type;
	}

	return 0;
}

static int mipid02_stream_disable(struct mipid02_dev *bridge)
{
	struct i2c_client *client = bridge->i2c_client;
	int ret = -EINVAL;

	if (!bridge->s_subdev)
		goto error;

	ret = v4l2_subdev_call(bridge->s_subdev, video, s_stream, 0);
	if (ret)
		goto error;

	/* Disable all lanes */
	cci_write(bridge->regmap, MIPID02_CLK_LANE_REG1, 0, &ret);
	cci_write(bridge->regmap, MIPID02_DATA_LANE0_REG1, 0, &ret);
	cci_write(bridge->regmap, MIPID02_DATA_LANE1_REG1, 0, &ret);
	if (ret)
		goto error;
error:
	if (ret)
		dev_err(&client->dev, "failed to stream off %d", ret);

	return ret;
}

static int mipid02_stream_enable(struct mipid02_dev *bridge)
{
	struct i2c_client *client = bridge->i2c_client;
	struct v4l2_subdev_state *state;
	struct v4l2_mbus_framefmt *fmt;
	int ret = -EINVAL;

	if (!bridge->s_subdev)
		goto error;

	memset(&bridge->r, 0, sizeof(bridge->r));

	state = v4l2_subdev_lock_and_get_active_state(&bridge->sd);
	fmt = v4l2_subdev_state_get_format(state, MIPID02_SINK_0);

	/* build registers content */
	ret = mipid02_configure_from_rx(bridge, fmt);
	if (ret)
		goto error;
	ret = mipid02_configure_from_tx(bridge);
	if (ret)
		goto error;
	ret = mipid02_configure_from_code(bridge, fmt);
	if (ret)
		goto error;

	v4l2_subdev_unlock_state(state);

	/* write mipi registers */
	cci_write(bridge->regmap, MIPID02_CLK_LANE_REG1,
		  bridge->r.clk_lane_reg1, &ret);
	cci_write(bridge->regmap, MIPID02_CLK_LANE_REG3, CLK_MIPI_CSI, &ret);
	cci_write(bridge->regmap, MIPID02_DATA_LANE0_REG1,
		  bridge->r.data_lane0_reg1, &ret);
	cci_write(bridge->regmap, MIPID02_DATA_LANE0_REG2, DATA_MIPI_CSI, &ret);
	cci_write(bridge->regmap, MIPID02_DATA_LANE1_REG1,
		  bridge->r.data_lane1_reg1, &ret);
	cci_write(bridge->regmap, MIPID02_DATA_LANE1_REG2, DATA_MIPI_CSI, &ret);
	cci_write(bridge->regmap, MIPID02_MODE_REG1,
		  MODE_NO_BYPASS | bridge->r.mode_reg1, &ret);
	cci_write(bridge->regmap, MIPID02_MODE_REG2, bridge->r.mode_reg2, &ret);
	cci_write(bridge->regmap, MIPID02_DATA_ID_RREG, bridge->r.data_id_rreg,
		  &ret);
	cci_write(bridge->regmap, MIPID02_DATA_SELECTION_CTRL,
		  bridge->r.data_selection_ctrl, &ret);
	cci_write(bridge->regmap, MIPID02_PIX_WIDTH_CTRL,
		  bridge->r.pix_width_ctrl, &ret);
	cci_write(bridge->regmap, MIPID02_PIX_WIDTH_CTRL_EMB,
		  bridge->r.pix_width_ctrl_emb, &ret);
	if (ret)
		goto error;

	ret = v4l2_subdev_call(bridge->s_subdev, video, s_stream, 1);
	if (ret)
		goto error;

	return 0;

error:
	dev_err(&client->dev, "failed to stream on %d", ret);
	mipid02_stream_disable(bridge);

	return ret;
}

static int mipid02_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct mipid02_dev *bridge = to_mipid02_dev(sd);
	struct i2c_client *client = bridge->i2c_client;
	int ret = 0;

	dev_dbg(&client->dev, "%s : requested %d\n", __func__, enable);

	ret = enable ? mipid02_stream_enable(bridge) :
		       mipid02_stream_disable(bridge);
	if (ret)
		dev_err(&client->dev, "failed to stream %s (%d)\n",
			enable ? "enable" : "disable", ret);

	return ret;
}

static const struct v4l2_mbus_framefmt default_fmt = {
	.code = MEDIA_BUS_FMT_SBGGR8_1X8,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_SRGB,
	.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
	.quantization = V4L2_QUANTIZATION_FULL_RANGE,
	.xfer_func = V4L2_XFER_FUNC_DEFAULT,
	.width = 640,
	.height = 480,
};

static int mipid02_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state)
{
	*v4l2_subdev_state_get_format(state, MIPID02_SINK_0) = default_fmt;
	/* MIPID02_SINK_1 isn't supported yet */
	*v4l2_subdev_state_get_format(state, MIPID02_SOURCE) = default_fmt;

	return 0;
}

static int mipid02_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct v4l2_mbus_framefmt *sink_fmt;
	int ret = 0;

	switch (code->pad) {
	case MIPID02_SINK_0:
		if (code->index >= ARRAY_SIZE(mipid02_supported_fmt_codes))
			ret = -EINVAL;
		else
			code->code = mipid02_supported_fmt_codes[code->index];
		break;
	case MIPID02_SOURCE:
		if (code->index == 0) {
			sink_fmt = v4l2_subdev_state_get_format(sd_state,
								MIPID02_SINK_0);
			code->code = serial_to_parallel_code(sink_fmt->code);
		} else {
			ret = -EINVAL;
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int mipid02_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *sd_state,
			   struct v4l2_subdev_format *fmt)
{
	struct mipid02_dev *bridge = to_mipid02_dev(sd);
	struct i2c_client *client = bridge->i2c_client;
	struct v4l2_mbus_framefmt *pad_fmt;

	dev_dbg(&client->dev, "%s for %d", __func__, fmt->pad);

	/* second CSI-2 pad not yet supported */
	if (fmt->pad == MIPID02_SINK_1)
		return -EINVAL;

	pad_fmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
	fmt->format.code = get_fmt_code(fmt->format.code);

	/* code may need to be converted */
	if (fmt->pad == MIPID02_SOURCE)
		fmt->format.code = serial_to_parallel_code(fmt->format.code);

	*pad_fmt = fmt->format;

	/* Propagate the format to the source pad in case of sink pad update */
	if (fmt->pad == MIPID02_SINK_0) {
		pad_fmt = v4l2_subdev_state_get_format(sd_state,
						       MIPID02_SOURCE);
		*pad_fmt = fmt->format;
		pad_fmt->code = serial_to_parallel_code(fmt->format.code);
	}

	return 0;
}

static const struct v4l2_subdev_video_ops mipid02_video_ops = {
	.s_stream = mipid02_s_stream,
};

static const struct v4l2_subdev_pad_ops mipid02_pad_ops = {
	.enum_mbus_code = mipid02_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = mipid02_set_fmt,
};

static const struct v4l2_subdev_ops mipid02_subdev_ops = {
	.video = &mipid02_video_ops,
	.pad = &mipid02_pad_ops,
};

static const struct v4l2_subdev_internal_ops mipid02_subdev_internal_ops = {
	.init_state = mipid02_init_state,
};

static const struct media_entity_operations mipid02_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int mipid02_async_bound(struct v4l2_async_notifier *notifier,
			       struct v4l2_subdev *s_subdev,
			       struct v4l2_async_connection *asd)
{
	struct mipid02_dev *bridge = to_mipid02_dev(notifier->sd);
	struct i2c_client *client = bridge->i2c_client;
	int source_pad;
	int ret;

	dev_dbg(&client->dev, "sensor_async_bound call %p", s_subdev);

	source_pad = media_entity_get_fwnode_pad(&s_subdev->entity,
						 s_subdev->fwnode,
						 MEDIA_PAD_FL_SOURCE);
	if (source_pad < 0) {
		dev_err(&client->dev, "Couldn't find output pad for subdev %s\n",
			s_subdev->name);
		return source_pad;
	}

	ret = media_create_pad_link(&s_subdev->entity, source_pad,
				    &bridge->sd.entity, 0,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_err(&client->dev, "Couldn't create media link %d", ret);
		return ret;
	}

	bridge->s_subdev = s_subdev;

	return 0;
}

static void mipid02_async_unbind(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *s_subdev,
				 struct v4l2_async_connection *asd)
{
	struct mipid02_dev *bridge = to_mipid02_dev(notifier->sd);

	bridge->s_subdev = NULL;
}

static const struct v4l2_async_notifier_operations mipid02_notifier_ops = {
	.bound		= mipid02_async_bound,
	.unbind		= mipid02_async_unbind,
};

static int mipid02_parse_rx_ep(struct mipid02_dev *bridge)
{
	struct v4l2_fwnode_endpoint ep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	struct i2c_client *client = bridge->i2c_client;
	struct v4l2_async_connection *asd;
	struct device_node *ep_node;
	int ret;

	/* parse rx (endpoint 0) */
	ep_node = of_graph_get_endpoint_by_regs(bridge->i2c_client->dev.of_node,
						0, 0);
	if (!ep_node) {
		dev_err(&client->dev, "unable to find port0 ep");
		ret = -EINVAL;
		goto error;
	}

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep_node), &ep);
	if (ret) {
		dev_err(&client->dev, "Could not parse v4l2 endpoint %d\n",
			ret);
		goto error_of_node_put;
	}

	/* do some sanity checks */
	if (ep.bus.mipi_csi2.num_data_lanes > 2) {
		dev_err(&client->dev, "max supported data lanes is 2 / got %d",
			ep.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto error_of_node_put;
	}

	/* register it for later use */
	bridge->rx = ep;

	/* register async notifier so we get noticed when sensor is connected */
	v4l2_async_subdev_nf_init(&bridge->notifier, &bridge->sd);
	asd = v4l2_async_nf_add_fwnode_remote(&bridge->notifier,
					      of_fwnode_handle(ep_node),
					      struct v4l2_async_connection);
	of_node_put(ep_node);

	if (IS_ERR(asd)) {
		dev_err(&client->dev, "fail to register asd to notifier %ld",
			PTR_ERR(asd));
		return PTR_ERR(asd);
	}
	bridge->notifier.ops = &mipid02_notifier_ops;

	ret = v4l2_async_nf_register(&bridge->notifier);
	if (ret)
		v4l2_async_nf_cleanup(&bridge->notifier);

	return ret;

error_of_node_put:
	of_node_put(ep_node);
error:

	return ret;
}

static int mipid02_parse_tx_ep(struct mipid02_dev *bridge)
{
	struct v4l2_fwnode_endpoint ep = { .bus_type = V4L2_MBUS_PARALLEL };
	struct i2c_client *client = bridge->i2c_client;
	struct device_node *ep_node;
	int ret;

	/* parse tx (endpoint 2) */
	ep_node = of_graph_get_endpoint_by_regs(bridge->i2c_client->dev.of_node,
						2, 0);
	if (!ep_node) {
		dev_err(&client->dev, "unable to find port1 ep");
		ret = -EINVAL;
		goto error;
	}

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep_node), &ep);
	if (ret) {
		dev_err(&client->dev, "Could not parse v4l2 endpoint\n");
		goto error_of_node_put;
	}

	of_node_put(ep_node);
	bridge->tx = ep;

	return 0;

error_of_node_put:
	of_node_put(ep_node);
error:

	return -EINVAL;
}

static int mipid02_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct mipid02_dev *bridge;
	u32 clk_freq;
	int ret;

	bridge = devm_kzalloc(dev, sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return -ENOMEM;

	bridge->i2c_client = client;
	v4l2_i2c_subdev_init(&bridge->sd, client, &mipid02_subdev_ops);

	/* got and check clock */
	bridge->xclk = devm_clk_get(dev, "xclk");
	if (IS_ERR(bridge->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(bridge->xclk);
	}

	clk_freq = clk_get_rate(bridge->xclk);
	if (clk_freq < 6000000 || clk_freq > 27000000) {
		dev_err(dev, "xclk freq must be in 6-27 Mhz range. got %d Hz\n",
			clk_freq);
		return -EINVAL;
	}

	bridge->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);

	if (IS_ERR(bridge->reset_gpio)) {
		dev_err(dev, "failed to get reset GPIO\n");
		return PTR_ERR(bridge->reset_gpio);
	}

	ret = mipid02_get_regulators(bridge);
	if (ret) {
		dev_err(dev, "failed to get regulators %d", ret);
		return ret;
	}

	/* Initialise the regmap for further cci access */
	bridge->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(bridge->regmap))
		return dev_err_probe(dev, PTR_ERR(bridge->regmap),
				     "failed to get cci regmap\n");

	bridge->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	bridge->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	bridge->sd.internal_ops = &mipid02_subdev_internal_ops;
	bridge->sd.entity.ops = &mipid02_subdev_entity_ops;
	bridge->pad[0].flags = MEDIA_PAD_FL_SINK;
	bridge->pad[1].flags = MEDIA_PAD_FL_SINK;
	bridge->pad[2].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&bridge->sd.entity, MIPID02_PAD_NB,
				     bridge->pad);
	if (ret) {
		dev_err(&client->dev, "pads init failed %d", ret);
		return ret;
	}

	ret = v4l2_subdev_init_finalize(&bridge->sd);
	if (ret < 0) {
		dev_err(dev, "subdev init error: %d\n", ret);
		goto entity_cleanup;
	}

	/* enable clock, power and reset device if available */
	ret = mipid02_set_power_on(bridge);
	if (ret)
		goto entity_cleanup;

	ret = mipid02_detect(bridge);
	if (ret) {
		dev_err(&client->dev, "failed to detect mipid02 %d", ret);
		goto power_off;
	}

	ret = mipid02_parse_tx_ep(bridge);
	if (ret) {
		dev_err(&client->dev, "failed to parse tx %d", ret);
		goto power_off;
	}

	ret = mipid02_parse_rx_ep(bridge);
	if (ret) {
		dev_err(&client->dev, "failed to parse rx %d", ret);
		goto power_off;
	}

	ret = v4l2_async_register_subdev(&bridge->sd);
	if (ret < 0) {
		dev_err(&client->dev, "v4l2_async_register_subdev failed %d",
			    ret);
		goto unregister_notifier;
	}

	dev_info(&client->dev, "mipid02 device probe successfully");

	return 0;

unregister_notifier:
	v4l2_async_nf_unregister(&bridge->notifier);
	v4l2_async_nf_cleanup(&bridge->notifier);
power_off:
	mipid02_set_power_off(bridge);
entity_cleanup:
	media_entity_cleanup(&bridge->sd.entity);

	return ret;
}

static void mipid02_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct mipid02_dev *bridge = to_mipid02_dev(sd);

	v4l2_async_nf_unregister(&bridge->notifier);
	v4l2_async_nf_cleanup(&bridge->notifier);
	v4l2_async_unregister_subdev(&bridge->sd);
	mipid02_set_power_off(bridge);
	media_entity_cleanup(&bridge->sd.entity);
}

static const struct of_device_id mipid02_dt_ids[] = {
	{ .compatible = "st,st-mipid02" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mipid02_dt_ids);

static struct i2c_driver mipid02_i2c_driver = {
	.driver = {
		.name  = "st-mipid02",
		.of_match_table = mipid02_dt_ids,
	},
	.probe = mipid02_probe,
	.remove = mipid02_remove,
};

module_i2c_driver(mipid02_i2c_driver);

MODULE_AUTHOR("Mickael Guene <mickael.guene@st.com>");
MODULE_DESCRIPTION("STMicroelectronics MIPID02 CSI-2 bridge driver");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Sieć Badawcza Łukasiewicz
 * - Przemysłowy Instytut Automatyki i Pomiarów PIAP
 * Written by Krzysztof Hałasa
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/* External clock (extclk) frequencies */
#define AR0521_EXTCLK_MIN		(10 * 1000 * 1000)
#define AR0521_EXTCLK_MAX		(48 * 1000 * 1000)

/* PLL and PLL2 */
#define AR0521_PLL_MIN			(320 * 1000 * 1000)
#define AR0521_PLL_MAX			(1280 * 1000 * 1000)

/* Effective pixel sample rate on the pixel array. */
#define AR0521_PIXEL_CLOCK_RATE		(184 * 1000 * 1000)
#define AR0521_PIXEL_CLOCK_MIN		(168 * 1000 * 1000)
#define AR0521_PIXEL_CLOCK_MAX		(414 * 1000 * 1000)

#define AR0521_NATIVE_WIDTH		2604u
#define AR0521_NATIVE_HEIGHT		1964u
#define AR0521_MIN_X_ADDR_START		0u
#define AR0521_MIN_Y_ADDR_START		0u
#define AR0521_MAX_X_ADDR_END		2603u
#define AR0521_MAX_Y_ADDR_END		1955u

#define AR0521_WIDTH_MIN		8u
#define AR0521_WIDTH_MAX		2592u
#define AR0521_HEIGHT_MIN		8u
#define AR0521_HEIGHT_MAX		1944u

#define AR0521_WIDTH_BLANKING_MIN	572u
#define AR0521_HEIGHT_BLANKING_MIN	38u /* must be even */
#define AR0521_TOTAL_HEIGHT_MAX		65535u /* max_frame_length_lines */
#define AR0521_TOTAL_WIDTH_MAX		65532u /* max_line_length_pck */

#define AR0521_ANA_GAIN_MIN		0x00
#define AR0521_ANA_GAIN_MAX		0x3f
#define AR0521_ANA_GAIN_STEP		0x01
#define AR0521_ANA_GAIN_DEFAULT		0x00

/* AR0521 registers */
#define AR0521_REG_VT_PIX_CLK_DIV		0x0300
#define AR0521_REG_FRAME_LENGTH_LINES		0x0340

#define AR0521_REG_CHIP_ID			0x3000
#define AR0521_REG_COARSE_INTEGRATION_TIME	0x3012
#define AR0521_REG_ROW_SPEED			0x3016
#define AR0521_REG_EXTRA_DELAY			0x3018
#define AR0521_REG_RESET			0x301A
#define   AR0521_REG_RESET_DEFAULTS		  0x0238
#define   AR0521_REG_RESET_GROUP_PARAM_HOLD	  0x8000
#define   AR0521_REG_RESET_STREAM		  BIT(2)
#define   AR0521_REG_RESET_RESTART		  BIT(1)
#define   AR0521_REG_RESET_INIT			  BIT(0)

#define AR0521_REG_ANA_GAIN_CODE_GLOBAL		0x3028

#define AR0521_REG_GREEN1_GAIN			0x3056
#define AR0521_REG_BLUE_GAIN			0x3058
#define AR0521_REG_RED_GAIN			0x305A
#define AR0521_REG_GREEN2_GAIN			0x305C
#define AR0521_REG_GLOBAL_GAIN			0x305E

#define AR0521_REG_HISPI_TEST_MODE		0x3066
#define AR0521_REG_HISPI_TEST_MODE_LP11		  0x0004

#define AR0521_REG_TEST_PATTERN_MODE		0x3070

#define AR0521_REG_SERIAL_FORMAT		0x31AE
#define AR0521_REG_SERIAL_FORMAT_MIPI		  0x0200

#define AR0521_REG_HISPI_CONTROL_STATUS		0x31C6
#define AR0521_REG_HISPI_CONTROL_STATUS_FRAMER_TEST_MODE_ENABLE 0x80

#define be		cpu_to_be16

static const char * const ar0521_supply_names[] = {
	"vdd_io",	/* I/O (1.8V) supply */
	"vdd",		/* Core, PLL and MIPI (1.2V) supply */
	"vaa",		/* Analog (2.7V) supply */
};

static const s64 ar0521_link_frequencies[] = {
	184000000,
};

struct ar0521_ctrls {
	struct v4l2_ctrl_handler handler;
	struct {
		struct v4l2_ctrl *gain;
		struct v4l2_ctrl *red_balance;
		struct v4l2_ctrl *blue_balance;
	};
	struct {
		struct v4l2_ctrl *hblank;
		struct v4l2_ctrl *vblank;
	};
	struct v4l2_ctrl *pixrate;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *test_pattern;
};

struct ar0521_dev {
	struct i2c_client *i2c_client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct clk *extclk;
	u32 extclk_freq;

	struct regulator *supplies[ARRAY_SIZE(ar0521_supply_names)];
	struct gpio_desc *reset_gpio;

	/* lock to protect all members below */
	struct mutex lock;

	struct v4l2_mbus_framefmt fmt;
	struct ar0521_ctrls ctrls;
	unsigned int lane_count;
	struct {
		u16 pre;
		u16 mult;
		u16 pre2;
		u16 mult2;
		u16 vt_pix;
	} pll;
};

static inline struct ar0521_dev *to_ar0521_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ar0521_dev, sd);
}

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ar0521_dev,
			     ctrls.handler)->sd;
}

static u32 div64_round(u64 v, u32 d)
{
	return div_u64(v + (d >> 1), d);
}

static u32 div64_round_up(u64 v, u32 d)
{
	return div_u64(v + d - 1, d);
}

static int ar0521_code_to_bpp(struct ar0521_dev *sensor)
{
	switch (sensor->fmt.code) {
	case MEDIA_BUS_FMT_SGRBG8_1X8:
		return 8;
	}

	return -EINVAL;
}

/* Data must be BE16, the first value is the register address */
static int ar0521_write_regs(struct ar0521_dev *sensor, const __be16 *data,
			     unsigned int count)
{
	struct i2c_client *client = sensor->i2c_client;
	struct i2c_msg msg;
	int ret;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = (u8 *)data;
	msg.len = count * sizeof(*data);

	ret = i2c_transfer(client->adapter, &msg, 1);

	if (ret < 0) {
		v4l2_err(&sensor->sd, "%s: I2C write error\n", __func__);
		return ret;
	}

	return 0;
}

static int ar0521_write_reg(struct ar0521_dev *sensor, u16 reg, u16 val)
{
	__be16 buf[2] = {be(reg), be(val)};

	return ar0521_write_regs(sensor, buf, 2);
}

static int ar0521_set_geometry(struct ar0521_dev *sensor)
{
	/* Center the image in the visible output window. */
	u16 x = clamp((AR0521_WIDTH_MAX - sensor->fmt.width) / 2,
		       AR0521_MIN_X_ADDR_START, AR0521_MAX_X_ADDR_END);
	u16 y = clamp(((AR0521_HEIGHT_MAX - sensor->fmt.height) / 2) & ~1,
		       AR0521_MIN_Y_ADDR_START, AR0521_MAX_Y_ADDR_END);

	/* All dimensions are unsigned 12-bit integers */
	__be16 regs[] = {
		be(AR0521_REG_FRAME_LENGTH_LINES),
		be(sensor->fmt.height + sensor->ctrls.vblank->val),
		be(sensor->fmt.width + sensor->ctrls.hblank->val),
		be(x),
		be(y),
		be(x + sensor->fmt.width - 1),
		be(y + sensor->fmt.height - 1),
		be(sensor->fmt.width),
		be(sensor->fmt.height)
	};

	return ar0521_write_regs(sensor, regs, ARRAY_SIZE(regs));
}

static int ar0521_set_gains(struct ar0521_dev *sensor)
{
	int green = sensor->ctrls.gain->val;
	int red = max(green + sensor->ctrls.red_balance->val, 0);
	int blue = max(green + sensor->ctrls.blue_balance->val, 0);
	unsigned int gain = min(red, min(green, blue));
	unsigned int analog = min(gain, 64u); /* range is 0 - 127 */
	__be16 regs[5];

	red   = min(red   - analog + 64, 511u);
	green = min(green - analog + 64, 511u);
	blue  = min(blue  - analog + 64, 511u);
	regs[0] = be(AR0521_REG_GREEN1_GAIN);
	regs[1] = be(green << 7 | analog);
	regs[2] = be(blue  << 7 | analog);
	regs[3] = be(red   << 7 | analog);
	regs[4] = be(green << 7 | analog);

	return ar0521_write_regs(sensor, regs, ARRAY_SIZE(regs));
}

static u32 calc_pll(struct ar0521_dev *sensor, u32 freq, u16 *pre_ptr, u16 *mult_ptr)
{
	u16 pre = 1, mult = 1, new_pre;
	u32 pll = AR0521_PLL_MAX + 1;

	for (new_pre = 1; new_pre < 64; new_pre++) {
		u32 new_pll;
		u32 new_mult = div64_round_up((u64)freq * new_pre,
					      sensor->extclk_freq);

		if (new_mult < 32)
			continue; /* Minimum value */
		if (new_mult > 254)
			break; /* Maximum, larger pre won't work either */
		if (sensor->extclk_freq * (u64)new_mult < AR0521_PLL_MIN *
		    new_pre)
			continue;
		if (sensor->extclk_freq * (u64)new_mult > AR0521_PLL_MAX *
		    new_pre)
			break; /* Larger pre won't work either */
		new_pll = div64_round_up(sensor->extclk_freq * (u64)new_mult,
					 new_pre);
		if (new_pll < pll) {
			pll = new_pll;
			pre = new_pre;
			mult = new_mult;
		}
	}

	pll = div64_round(sensor->extclk_freq * (u64)mult, pre);
	*pre_ptr = pre;
	*mult_ptr = mult;
	return pll;
}

static void ar0521_calc_pll(struct ar0521_dev *sensor)
{
	unsigned int pixel_clock;
	u16 pre, mult;
	u32 vco;
	int bpp;

	/*
	 * PLL1 and PLL2 are computed equally even if the application note
	 * suggests a slower PLL1 clock. Maintain pll1 and pll2 divider and
	 * multiplier separated to later specialize the calculation procedure.
	 *
	 * PLL1:
	 * - mclk -> / pre_div1 * pre_mul1 = VCO1 = COUNTER_CLOCK
	 *
	 * PLL2:
	 * - mclk -> / pre_div * pre_mul = VCO
	 *
	 *   VCO -> / vt_pix = PIXEL_CLOCK
	 *   VCO -> / vt_pix / 2 = WORD_CLOCK
	 *   VCO -> / op_sys = SERIAL_CLOCK
	 *
	 * With:
	 * - vt_pix = bpp / 2
	 * - WORD_CLOCK = PIXEL_CLOCK / 2
	 * - SERIAL_CLOCK = MIPI data rate (Mbps / lane) = WORD_CLOCK * bpp
	 *   NOTE: this implies the MIPI clock is divided internally by 2
	 *         to account for DDR.
	 *
	 * As op_sys_div is fixed to 1:
	 *
	 * SERIAL_CLOCK = VCO
	 * VCO = 2 * MIPI_CLK
	 * VCO = PIXEL_CLOCK * bpp / 2
	 *
	 * In the clock tree:
	 * MIPI_CLK = PIXEL_CLOCK * bpp / 2 / 2
	 *
	 * Generic pixel_rate to bus clock frequencey equation:
	 * MIPI_CLK = V4L2_CID_PIXEL_RATE * bpp / lanes / 2
	 *
	 * From which we derive the PIXEL_CLOCK to use in the clock tree:
	 * PIXEL_CLOCK = V4L2_CID_PIXEL_RATE * 2 / lanes
	 *
	 * Documented clock ranges:
	 *   WORD_CLOCK = (35MHz - 120 MHz)
	 *   PIXEL_CLOCK = (84MHz - 207MHz)
	 *   VCO = (320MHz - 1280MHz)
	 *
	 * TODO: in case we have less data lanes we have to reduce the desired
	 * VCO not to exceed the limits specified by the datasheet and
	 * consequentially reduce the obtained pixel clock.
	 */
	pixel_clock = AR0521_PIXEL_CLOCK_RATE * 2 / sensor->lane_count;
	bpp = ar0521_code_to_bpp(sensor);
	sensor->pll.vt_pix = bpp / 2;
	vco = pixel_clock * sensor->pll.vt_pix;

	calc_pll(sensor, vco, &pre, &mult);

	sensor->pll.pre = sensor->pll.pre2 = pre;
	sensor->pll.mult = sensor->pll.mult2 = mult;
}

static int ar0521_pll_config(struct ar0521_dev *sensor)
{
	__be16 pll_regs[] = {
		be(AR0521_REG_VT_PIX_CLK_DIV),
		/* 0x300 */ be(sensor->pll.vt_pix), /* vt_pix_clk_div = bpp / 2 */
		/* 0x302 */ be(1), /* vt_sys_clk_div */
		/* 0x304 */ be((sensor->pll.pre2 << 8) | sensor->pll.pre),
		/* 0x306 */ be((sensor->pll.mult2 << 8) | sensor->pll.mult),
		/* 0x308 */ be(sensor->pll.vt_pix * 2), /* op_pix_clk_div = 2 * vt_pix_clk_div */
		/* 0x30A */ be(1)  /* op_sys_clk_div */
	};

	ar0521_calc_pll(sensor);
	return ar0521_write_regs(sensor, pll_regs, ARRAY_SIZE(pll_regs));
}

static int ar0521_set_stream(struct ar0521_dev *sensor, bool on)
{
	int ret;

	if (on) {
		ret = pm_runtime_resume_and_get(&sensor->i2c_client->dev);
		if (ret < 0)
			return ret;

		/* Stop streaming for just a moment */
		ret = ar0521_write_reg(sensor, AR0521_REG_RESET,
				       AR0521_REG_RESET_DEFAULTS);
		if (ret)
			return ret;

		ret = ar0521_set_geometry(sensor);
		if (ret)
			return ret;

		ret = ar0521_pll_config(sensor);
		if (ret)
			goto err;

		ret =  __v4l2_ctrl_handler_setup(&sensor->ctrls.handler);
		if (ret)
			goto err;

		/* Exit LP-11 mode on clock and data lanes */
		ret = ar0521_write_reg(sensor, AR0521_REG_HISPI_CONTROL_STATUS,
				       0);
		if (ret)
			goto err;

		/* Start streaming */
		ret = ar0521_write_reg(sensor, AR0521_REG_RESET,
				       AR0521_REG_RESET_DEFAULTS |
				       AR0521_REG_RESET_STREAM);
		if (ret)
			goto err;

		return 0;

err:
		pm_runtime_put(&sensor->i2c_client->dev);
		return ret;

	} else {
		/*
		 * Reset gain, the sensor may produce all white pixels without
		 * this
		 */
		ret = ar0521_write_reg(sensor, AR0521_REG_GLOBAL_GAIN, 0x2000);
		if (ret)
			return ret;

		/* Stop streaming */
		ret = ar0521_write_reg(sensor, AR0521_REG_RESET,
				       AR0521_REG_RESET_DEFAULTS);
		if (ret)
			return ret;

		pm_runtime_put(&sensor->i2c_client->dev);
		return 0;
	}
}

static void ar0521_adj_fmt(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = clamp(ALIGN(fmt->width, 4), AR0521_WIDTH_MIN,
			   AR0521_WIDTH_MAX);
	fmt->height = clamp(ALIGN(fmt->height, 4), AR0521_HEIGHT_MIN,
			    AR0521_HEIGHT_MAX);
	fmt->code = MEDIA_BUS_FMT_SGRBG8_1X8;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static int ar0521_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct ar0521_dev *sensor = to_ar0521_dev(sd);
	struct v4l2_mbus_framefmt *fmt;

	mutex_lock(&sensor->lock);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt = v4l2_subdev_state_get_format(sd_state, 0);
	else
		fmt = &sensor->fmt;

	format->format = *fmt;

	mutex_unlock(&sensor->lock);
	return 0;
}

static int ar0521_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct ar0521_dev *sensor = to_ar0521_dev(sd);
	int max_vblank, max_hblank, exposure_max;
	int ret;

	ar0521_adj_fmt(&format->format);

	mutex_lock(&sensor->lock);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_state_get_format(sd_state, 0);
		*fmt = format->format;

		mutex_unlock(&sensor->lock);

		return 0;
	}

	sensor->fmt = format->format;
	ar0521_calc_pll(sensor);

	/*
	 * Update the exposure and blankings limits. Blankings are also reset
	 * to the minimum.
	 */
	max_hblank = AR0521_TOTAL_WIDTH_MAX - sensor->fmt.width;
	ret = __v4l2_ctrl_modify_range(sensor->ctrls.hblank,
				       sensor->ctrls.hblank->minimum,
				       max_hblank, sensor->ctrls.hblank->step,
				       sensor->ctrls.hblank->minimum);
	if (ret)
		goto unlock;

	ret = __v4l2_ctrl_s_ctrl(sensor->ctrls.hblank,
				 sensor->ctrls.hblank->minimum);
	if (ret)
		goto unlock;

	max_vblank = AR0521_TOTAL_HEIGHT_MAX - sensor->fmt.height;
	ret = __v4l2_ctrl_modify_range(sensor->ctrls.vblank,
				       sensor->ctrls.vblank->minimum,
				       max_vblank, sensor->ctrls.vblank->step,
				       sensor->ctrls.vblank->minimum);
	if (ret)
		goto unlock;

	ret = __v4l2_ctrl_s_ctrl(sensor->ctrls.vblank,
				 sensor->ctrls.vblank->minimum);
	if (ret)
		goto unlock;

	exposure_max = sensor->fmt.height + AR0521_HEIGHT_BLANKING_MIN - 4;
	ret = __v4l2_ctrl_modify_range(sensor->ctrls.exposure,
				       sensor->ctrls.exposure->minimum,
				       exposure_max,
				       sensor->ctrls.exposure->step,
				       sensor->ctrls.exposure->default_value);
unlock:
	mutex_unlock(&sensor->lock);

	return ret;
}

static int ar0521_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct ar0521_dev *sensor = to_ar0521_dev(sd);
	int exp_max;
	int ret;

	/* v4l2_ctrl_lock() locks our own mutex */

	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		exp_max = sensor->fmt.height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(sensor->ctrls.exposure,
					 sensor->ctrls.exposure->minimum,
					 exp_max, sensor->ctrls.exposure->step,
					 sensor->ctrls.exposure->default_value);
		break;
	}

	/* access the sensor only if it's powered up */
	if (!pm_runtime_get_if_in_use(&sensor->i2c_client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_HBLANK:
	case V4L2_CID_VBLANK:
		ret = ar0521_set_geometry(sensor);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ar0521_write_reg(sensor, AR0521_REG_ANA_GAIN_CODE_GLOBAL,
				       ctrl->val);
		break;
	case V4L2_CID_GAIN:
	case V4L2_CID_RED_BALANCE:
	case V4L2_CID_BLUE_BALANCE:
		ret = ar0521_set_gains(sensor);
		break;
	case V4L2_CID_EXPOSURE:
		ret = ar0521_write_reg(sensor,
				       AR0521_REG_COARSE_INTEGRATION_TIME,
				       ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ar0521_write_reg(sensor, AR0521_REG_TEST_PATTERN_MODE,
				       ctrl->val);
		break;
	default:
		dev_err(&sensor->i2c_client->dev,
			"Unsupported control %x\n", ctrl->id);
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&sensor->i2c_client->dev);
	return ret;
}

static const struct v4l2_ctrl_ops ar0521_ctrl_ops = {
	.s_ctrl = ar0521_s_ctrl,
};

static const char * const test_pattern_menu[] = {
	"Disabled",
	"Solid color",
	"Color bars",
	"Faded color bars"
};

static int ar0521_init_controls(struct ar0521_dev *sensor)
{
	const struct v4l2_ctrl_ops *ops = &ar0521_ctrl_ops;
	struct ar0521_ctrls *ctrls = &sensor->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int max_vblank, max_hblank, exposure_max;
	struct v4l2_ctrl *link_freq;
	int ret;

	v4l2_ctrl_handler_init(hdl, 32);

	/* We can use our own mutex for the ctrl lock */
	hdl->lock = &sensor->lock;

	/* Analog gain */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_ANALOGUE_GAIN,
			  AR0521_ANA_GAIN_MIN, AR0521_ANA_GAIN_MAX,
			  AR0521_ANA_GAIN_STEP, AR0521_ANA_GAIN_DEFAULT);

	/* Manual gain */
	ctrls->gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAIN, 0, 511, 1, 0);
	ctrls->red_balance = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_RED_BALANCE,
					       -512, 511, 1, 0);
	ctrls->blue_balance = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_BLUE_BALANCE,
						-512, 511, 1, 0);
	v4l2_ctrl_cluster(3, &ctrls->gain);

	/* Initialize blanking limits using the default 2592x1944 format. */
	max_hblank = AR0521_TOTAL_WIDTH_MAX - AR0521_WIDTH_MAX;
	ctrls->hblank = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HBLANK,
					  AR0521_WIDTH_BLANKING_MIN,
					  max_hblank, 1,
					  AR0521_WIDTH_BLANKING_MIN);

	max_vblank = AR0521_TOTAL_HEIGHT_MAX - AR0521_HEIGHT_MAX;
	ctrls->vblank = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VBLANK,
					  AR0521_HEIGHT_BLANKING_MIN,
					  max_vblank, 2,
					  AR0521_HEIGHT_BLANKING_MIN);
	v4l2_ctrl_cluster(2, &ctrls->hblank);

	/* Read-only */
	ctrls->pixrate = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_PIXEL_RATE,
					   AR0521_PIXEL_CLOCK_MIN,
					   AR0521_PIXEL_CLOCK_MAX, 1,
					   AR0521_PIXEL_CLOCK_RATE);

	/* Manual exposure time: max exposure time = visible + blank - 4 */
	exposure_max = AR0521_HEIGHT_MAX + AR0521_HEIGHT_BLANKING_MIN - 4;
	ctrls->exposure = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE, 0,
					    exposure_max, 1, 0x70);

	link_freq = v4l2_ctrl_new_int_menu(hdl, ops, V4L2_CID_LINK_FREQ,
					ARRAY_SIZE(ar0521_link_frequencies) - 1,
					0, ar0521_link_frequencies);
	if (link_freq)
		link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctrls->test_pattern = v4l2_ctrl_new_std_menu_items(hdl, ops,
					V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(test_pattern_menu) - 1,
					0, 0, test_pattern_menu);

	if (hdl->error) {
		ret = hdl->error;
		goto free_ctrls;
	}

	sensor->sd.ctrl_handler = hdl;
	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(hdl);
	return ret;
}

#define REGS_ENTRY(a)	{(a), ARRAY_SIZE(a)}
#define REGS(...)	REGS_ENTRY(((const __be16[]){__VA_ARGS__}))

static const struct initial_reg {
	const __be16 *data; /* data[0] is register address */
	unsigned int count;
} initial_regs[] = {
	REGS(be(0x0112), be(0x0808)), /* 8-bit/8-bit mode */

	/* PEDESTAL+2 :+2 is a workaround for 10bit mode +0.5 rounding */
	REGS(be(0x301E), be(0x00AA)),

	/* corrections_recommended_bayer */
	REGS(be(0x3042),
	     be(0x0004),  /* 3042: RNC: enable b/w rnc mode */
	     be(0x4580)), /* 3044: RNC: enable row noise correction */

	REGS(be(0x30D2),
	     be(0x0000),  /* 30D2: CRM/CC: enable crm on Visible and CC rows */
	     be(0x0000),  /* 30D4: CC: CC enabled with 16 samples per column */
	     /* 30D6: CC: bw mode enabled/12 bit data resolution/bw mode */
	     be(0x2FFF)),

	REGS(be(0x30DA),
	     be(0x0FFF),  /* 30DA: CC: column correction clip level 2 is 0 */
	     be(0x0FFF),  /* 30DC: CC: column correction clip level 3 is 0 */
	     be(0x0000)), /* 30DE: CC: Group FPN correction */

	/* RNC: rnc scaling factor = * 54 / 64 (32 / 38 * 64 = 53.9) */
	REGS(be(0x30EE), be(0x1136)),
	REGS(be(0x30FA), be(0xFD00)), /* GPIO0 = flash, GPIO1 = shutter */
	REGS(be(0x3120), be(0x0005)), /* p1 dither enabled for 10bit mode */
	REGS(be(0x3172), be(0x0206)), /* txlo clk divider options */
	/* FDOC:fdoc settings with fdoc every frame turned of */
	REGS(be(0x3180), be(0x9434)),

	REGS(be(0x31B0),
	     be(0x008B),  /* 31B0: frame_preamble - FIXME check WRT lanes# */
	     be(0x0050)), /* 31B2: line_preamble - FIXME check WRT lanes# */

	/* don't use continuous clock mode while shut down */
	REGS(be(0x31BC), be(0x068C)),
	REGS(be(0x31E0), be(0x0781)), /* Fuse/2DDC: enable 2ddc */

	/* analog_setup_recommended_10bit */
	REGS(be(0x341A), be(0x4735)), /* Samp&Hold pulse in ADC */
	REGS(be(0x3420), be(0x4735)), /* Samp&Hold pulse in ADC */
	REGS(be(0x3426), be(0x8A1A)), /* ADC offset distribution pulse */
	REGS(be(0x342A), be(0x0018)), /* pulse_config */

	/* pixel_timing_recommended */
	REGS(be(0x3D00),
	     /* 3D00 */ be(0x043E), be(0x4760), be(0xFFFF), be(0xFFFF),
	     /* 3D08 */ be(0x8000), be(0x0510), be(0xAF08), be(0x0252),
	     /* 3D10 */ be(0x486F), be(0x5D5D), be(0x8056), be(0x8313),
	     /* 3D18 */ be(0x0087), be(0x6A48), be(0x6982), be(0x0280),
	     /* 3D20 */ be(0x8359), be(0x8D02), be(0x8020), be(0x4882),
	     /* 3D28 */ be(0x4269), be(0x6A95), be(0x5988), be(0x5A83),
	     /* 3D30 */ be(0x5885), be(0x6280), be(0x6289), be(0x6097),
	     /* 3D38 */ be(0x5782), be(0x605C), be(0xBF18), be(0x0961),
	     /* 3D40 */ be(0x5080), be(0x2090), be(0x4390), be(0x4382),
	     /* 3D48 */ be(0x5F8A), be(0x5D5D), be(0x9C63), be(0x8063),
	     /* 3D50 */ be(0xA960), be(0x9757), be(0x8260), be(0x5CFF),
	     /* 3D58 */ be(0xBF10), be(0x1681), be(0x0802), be(0x8000),
	     /* 3D60 */ be(0x141C), be(0x6000), be(0x6022), be(0x4D80),
	     /* 3D68 */ be(0x5C97), be(0x6A69), be(0xAC6F), be(0x4645),
	     /* 3D70 */ be(0x4400), be(0x0513), be(0x8069), be(0x6AC6),
	     /* 3D78 */ be(0x5F95), be(0x5F70), be(0x8040), be(0x4A81),
	     /* 3D80 */ be(0x0300), be(0xE703), be(0x0088), be(0x4A83),
	     /* 3D88 */ be(0x40FF), be(0xFFFF), be(0xFD70), be(0x8040),
	     /* 3D90 */ be(0x4A85), be(0x4FA8), be(0x4F8C), be(0x0070),
	     /* 3D98 */ be(0xBE47), be(0x8847), be(0xBC78), be(0x6B89),
	     /* 3DA0 */ be(0x6A80), be(0x6986), be(0x6B8E), be(0x6B80),
	     /* 3DA8 */ be(0x6980), be(0x6A88), be(0x7C9F), be(0x866B),
	     /* 3DB0 */ be(0x8765), be(0x46FF), be(0xE365), be(0xA679),
	     /* 3DB8 */ be(0x4A40), be(0x4580), be(0x44BC), be(0x7000),
	     /* 3DC0 */ be(0x8040), be(0x0802), be(0x10EF), be(0x0104),
	     /* 3DC8 */ be(0x3860), be(0x5D5D), be(0x5682), be(0x1300),
	     /* 3DD0 */ be(0x8648), be(0x8202), be(0x8082), be(0x598A),
	     /* 3DD8 */ be(0x0280), be(0x2048), be(0x3060), be(0x8042),
	     /* 3DE0 */ be(0x9259), be(0x865A), be(0x8258), be(0x8562),
	     /* 3DE8 */ be(0x8062), be(0x8560), be(0x9257), be(0x8221),
	     /* 3DF0 */ be(0x10FF), be(0xB757), be(0x9361), be(0x1019),
	     /* 3DF8 */ be(0x8020), be(0x9043), be(0x8E43), be(0x845F),
	     /* 3E00 */ be(0x835D), be(0x805D), be(0x8163), be(0x8063),
	     /* 3E08 */ be(0xA060), be(0x9157), be(0x8260), be(0x5CFF),
	     /* 3E10 */ be(0xFFFF), be(0xFFE5), be(0x1016), be(0x2048),
	     /* 3E18 */ be(0x0802), be(0x1C60), be(0x0014), be(0x0060),
	     /* 3E20 */ be(0x2205), be(0x8120), be(0x908F), be(0x6A80),
	     /* 3E28 */ be(0x6982), be(0x5F9F), be(0x6F46), be(0x4544),
	     /* 3E30 */ be(0x0005), be(0x8013), be(0x8069), be(0x6A80),
	     /* 3E38 */ be(0x7000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3E40 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3E48 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3E50 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3E58 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3E60 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3E68 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3E70 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3E78 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3E80 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3E88 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3E90 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3E98 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3EA0 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3EA8 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	     /* 3EB0 */ be(0x0000), be(0x0000), be(0x0000)),

	REGS(be(0x3EB6), be(0x004C)), /* ECL */

	REGS(be(0x3EBA),
	     be(0xAAAD),  /* 3EBA */
	     be(0x0086)), /* 3EBC: Bias currents for FSC/ECL */

	REGS(be(0x3EC0),
	     be(0x1E00),  /* 3EC0: SFbin/SH mode settings */
	     be(0x100A),  /* 3EC2: CLK divider for ramp for 10 bit 400MH */
	     /* 3EC4: FSC clamps for HDR mode and adc comp power down co */
	     be(0x3300),
	     be(0xEA44),  /* 3EC6: VLN and clk gating controls */
	     be(0x6F6F),  /* 3EC8: Txl0 and Txlo1 settings for normal mode */
	     be(0x2F4A),  /* 3ECA: CDAC/Txlo2/RSTGHI/RSTGLO settings */
	     be(0x0506),  /* 3ECC: RSTDHI/RSTDLO/CDAC/TXHI settings */
	     /* 3ECE: Ramp buffer settings and Booster enable (bits 0-5) */
	     be(0x203B),
	     be(0x13F0),  /* 3ED0: TXLO from atest/sf bin settings */
	     be(0xA53D),  /* 3ED2: Ramp offset */
	     be(0x862F),  /* 3ED4: TXLO open loop/row driver settings */
	     be(0x4081),  /* 3ED6: Txlatch fr cfpn rows/vln bias */
	     be(0x8003),  /* 3ED8: Ramp step setting for 10 bit 400 Mhz */
	     be(0xA580),  /* 3EDA: Ramp Offset */
	     be(0xC000),  /* 3EDC: over range for rst and under range for sig */
	     be(0xC103)), /* 3EDE: over range for sig and col dec clk settings */

	/* corrections_recommended_bayer */
	REGS(be(0x3F00),
	     be(0x0017),  /* 3F00: BM_T0 */
	     be(0x02DD),  /* 3F02: BM_T1 */
	     /* 3F04: if Ana_gain less than 2, use noise_floor0, multipl */
	     be(0x0020),
	     /* 3F06: if Ana_gain between 4 and 7, use noise_floor2 and */
	     be(0x0040),
	     /* 3F08: if Ana_gain between 4 and 7, use noise_floor2 and */
	     be(0x0070),
	     /* 3F0A: Define noise_floor0(low address) and noise_floor1 */
	     be(0x0101),
	     be(0x0302)), /* 3F0C: Define noise_floor2 and noise_floor3 */

	REGS(be(0x3F10),
	     be(0x0505),  /* 3F10: single k factor 0 */
	     be(0x0505),  /* 3F12: single k factor 1 */
	     be(0x0505),  /* 3F14: single k factor 2 */
	     be(0x01FF),  /* 3F16: cross factor 0 */
	     be(0x01FF),  /* 3F18: cross factor 1 */
	     be(0x01FF),  /* 3F1A: cross factor 2 */
	     be(0x0022)), /* 3F1E */

	/* GTH_THRES_RTN: 4max,4min filtered out of every 46 samples and */
	REGS(be(0x3F2C), be(0x442E)),

	REGS(be(0x3F3E),
	     be(0x0000),  /* 3F3E: Switch ADC from 12 bit to 10 bit mode */
	     be(0x1511),  /* 3F40: couple k factor 0 */
	     be(0x1511),  /* 3F42: couple k factor 1 */
	     be(0x0707)), /* 3F44: couple k factor 2 */
};

static int ar0521_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ar0521_dev *sensor = to_ar0521_dev(sd);
	int i;

	clk_disable_unprepare(sensor->extclk);

	if (sensor->reset_gpio)
		gpiod_set_value(sensor->reset_gpio, 1); /* assert RESET signal */

	for (i = ARRAY_SIZE(ar0521_supply_names) - 1; i >= 0; i--) {
		if (sensor->supplies[i])
			regulator_disable(sensor->supplies[i]);
	}
	return 0;
}

static int ar0521_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ar0521_dev *sensor = to_ar0521_dev(sd);
	unsigned int cnt;
	int ret;

	for (cnt = 0; cnt < ARRAY_SIZE(ar0521_supply_names); cnt++)
		if (sensor->supplies[cnt]) {
			ret = regulator_enable(sensor->supplies[cnt]);
			if (ret < 0)
				goto off;

			usleep_range(1000, 1500); /* min 1 ms */
		}

	ret = clk_prepare_enable(sensor->extclk);
	if (ret < 0) {
		v4l2_err(&sensor->sd, "error enabling sensor clock\n");
		goto off;
	}
	usleep_range(1000, 1500); /* min 1 ms */

	if (sensor->reset_gpio)
		/* deassert RESET signal */
		gpiod_set_value(sensor->reset_gpio, 0);
	usleep_range(4500, 5000); /* min 45000 clocks */

	for (cnt = 0; cnt < ARRAY_SIZE(initial_regs); cnt++) {
		ret = ar0521_write_regs(sensor, initial_regs[cnt].data,
					initial_regs[cnt].count);
		if (ret)
			goto off;
	}

	ret = ar0521_write_reg(sensor, AR0521_REG_SERIAL_FORMAT,
			       AR0521_REG_SERIAL_FORMAT_MIPI |
			       sensor->lane_count);
	if (ret)
		goto off;

	/* set MIPI test mode - disabled for now */
	ret = ar0521_write_reg(sensor, AR0521_REG_HISPI_TEST_MODE,
			       ((0x40 << sensor->lane_count) - 0x40) |
			       AR0521_REG_HISPI_TEST_MODE_LP11);
	if (ret)
		goto off;

	ret = ar0521_write_reg(sensor, AR0521_REG_ROW_SPEED, 0x110 |
			       4 / sensor->lane_count);
	if (ret)
		goto off;

	return 0;
off:
	ar0521_power_off(dev);
	return ret;
}

static int ar0521_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct ar0521_dev *sensor = to_ar0521_dev(sd);

	if (code->index)
		return -EINVAL;

	code->code = sensor->fmt.code;
	return 0;
}

static int ar0521_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SGRBG8_1X8)
		return -EINVAL;

	fse->min_width = AR0521_WIDTH_MIN;
	fse->max_width = AR0521_WIDTH_MAX;
	fse->min_height = AR0521_HEIGHT_MIN;
	fse->max_height = AR0521_HEIGHT_MAX;

	return 0;
}

static int ar0521_pre_streamon(struct v4l2_subdev *sd, u32 flags)
{
	struct ar0521_dev *sensor = to_ar0521_dev(sd);
	int ret;

	if (!(flags & V4L2_SUBDEV_PRE_STREAMON_FL_MANUAL_LP))
		return -EACCES;

	ret = pm_runtime_resume_and_get(&sensor->i2c_client->dev);
	if (ret < 0)
		return ret;

	/* Set LP-11 on clock and data lanes */
	ret = ar0521_write_reg(sensor, AR0521_REG_HISPI_CONTROL_STATUS,
			AR0521_REG_HISPI_CONTROL_STATUS_FRAMER_TEST_MODE_ENABLE);
	if (ret)
		goto err;

	/* Start streaming LP-11 */
	ret = ar0521_write_reg(sensor, AR0521_REG_RESET,
			       AR0521_REG_RESET_DEFAULTS |
			       AR0521_REG_RESET_STREAM);
	if (ret)
		goto err;
	return 0;

err:
	pm_runtime_put(&sensor->i2c_client->dev);
	return ret;
}

static int ar0521_post_streamoff(struct v4l2_subdev *sd)
{
	struct ar0521_dev *sensor = to_ar0521_dev(sd);

	pm_runtime_put(&sensor->i2c_client->dev);
	return 0;
}

static int ar0521_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ar0521_dev *sensor = to_ar0521_dev(sd);
	int ret;

	mutex_lock(&sensor->lock);
	ret = ar0521_set_stream(sensor, enable);
	mutex_unlock(&sensor->lock);

	return ret;
}

static const struct v4l2_subdev_core_ops ar0521_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
};

static const struct v4l2_subdev_video_ops ar0521_video_ops = {
	.s_stream = ar0521_s_stream,
	.pre_streamon = ar0521_pre_streamon,
	.post_streamoff = ar0521_post_streamoff,
};

static const struct v4l2_subdev_pad_ops ar0521_pad_ops = {
	.enum_mbus_code = ar0521_enum_mbus_code,
	.enum_frame_size = ar0521_enum_frame_size,
	.get_fmt = ar0521_get_fmt,
	.set_fmt = ar0521_set_fmt,
};

static const struct v4l2_subdev_ops ar0521_subdev_ops = {
	.core = &ar0521_core_ops,
	.video = &ar0521_video_ops,
	.pad = &ar0521_pad_ops,
};

static int ar0521_probe(struct i2c_client *client)
{
	struct v4l2_fwnode_endpoint ep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct device *dev = &client->dev;
	struct fwnode_handle *endpoint;
	struct ar0521_dev *sensor;
	unsigned int cnt;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->i2c_client = client;
	sensor->fmt.width = AR0521_WIDTH_MAX;
	sensor->fmt.height = AR0521_HEIGHT_MAX;

	endpoint = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 0,
						   FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(endpoint, &ep);
	fwnode_handle_put(endpoint);
	if (ret) {
		dev_err(dev, "could not parse endpoint\n");
		return ret;
	}

	if (ep.bus_type != V4L2_MBUS_CSI2_DPHY) {
		dev_err(dev, "invalid bus type, must be MIPI CSI2\n");
		return -EINVAL;
	}

	sensor->lane_count = ep.bus.mipi_csi2.num_data_lanes;
	switch (sensor->lane_count) {
	case 1:
	case 2:
	case 4:
		break;
	default:
		dev_err(dev, "invalid number of MIPI data lanes\n");
		return -EINVAL;
	}

	/* Get master clock (extclk) */
	sensor->extclk = devm_clk_get(dev, "extclk");
	if (IS_ERR(sensor->extclk)) {
		dev_err(dev, "failed to get extclk\n");
		return PTR_ERR(sensor->extclk);
	}

	sensor->extclk_freq = clk_get_rate(sensor->extclk);

	if (sensor->extclk_freq < AR0521_EXTCLK_MIN ||
	    sensor->extclk_freq > AR0521_EXTCLK_MAX) {
		dev_err(dev, "extclk frequency out of range: %u Hz\n",
			sensor->extclk_freq);
		return -EINVAL;
	}

	/* Request optional reset pin (usually active low) and assert it */
	sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);

	v4l2_i2c_subdev_init(&sensor->sd, client, &ar0521_subdev_ops);

	sensor->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;

	for (cnt = 0; cnt < ARRAY_SIZE(ar0521_supply_names); cnt++) {
		struct regulator *supply = devm_regulator_get(dev,
						ar0521_supply_names[cnt]);

		if (IS_ERR(supply)) {
			dev_info(dev, "no %s regulator found: %li\n",
				 ar0521_supply_names[cnt], PTR_ERR(supply));
			return PTR_ERR(supply);
		}
		sensor->supplies[cnt] = supply;
	}

	mutex_init(&sensor->lock);

	ret = ar0521_init_controls(sensor);
	if (ret)
		goto entity_cleanup;

	ar0521_adj_fmt(&sensor->fmt);

	ret = v4l2_async_register_subdev(&sensor->sd);
	if (ret)
		goto free_ctrls;

	/* Turn on the device and enable runtime PM */
	ret = ar0521_power_on(&client->dev);
	if (ret)
		goto disable;
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);
	return 0;

disable:
	v4l2_async_unregister_subdev(&sensor->sd);
	media_entity_cleanup(&sensor->sd.entity);
free_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);
entity_cleanup:
	media_entity_cleanup(&sensor->sd.entity);
	mutex_destroy(&sensor->lock);
	return ret;
}

static void ar0521_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0521_dev *sensor = to_ar0521_dev(sd);

	v4l2_async_unregister_subdev(&sensor->sd);
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);
	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		ar0521_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	mutex_destroy(&sensor->lock);
}

static const struct dev_pm_ops ar0521_pm_ops = {
	SET_RUNTIME_PM_OPS(ar0521_power_off, ar0521_power_on, NULL)
};
static const struct of_device_id ar0521_dt_ids[] = {
	{.compatible = "onnn,ar0521"},
	{}
};
MODULE_DEVICE_TABLE(of, ar0521_dt_ids);

static struct i2c_driver ar0521_i2c_driver = {
	.driver = {
		.name  = "ar0521",
		.pm = &ar0521_pm_ops,
		.of_match_table = ar0521_dt_ids,
	},
	.probe = ar0521_probe,
	.remove = ar0521_remove,
};

module_i2c_driver(ar0521_i2c_driver);

MODULE_DESCRIPTION("AR0521 MIPI Camera subdev driver");
MODULE_AUTHOR("Krzysztof Hałasa <khalasa@piap.pl>");
MODULE_LICENSE("GPL");

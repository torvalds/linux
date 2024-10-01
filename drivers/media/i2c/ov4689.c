// SPDX-License-Identifier: GPL-2.0
/*
 * ov4689 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 * Copyright (C) 2022, 2024 Mikhail Rudenko
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>

#define OV4689_REG_CTRL_MODE		CCI_REG8(0x0100)
#define OV4689_MODE_SW_STANDBY		0x0
#define OV4689_MODE_STREAMING		BIT(0)

#define OV4689_REG_CHIP_ID		CCI_REG16(0x300a)
#define CHIP_ID				0x004688

#define OV4689_REG_EXPOSURE		CCI_REG24(0x3500)
#define OV4689_EXPOSURE_MIN		4
#define OV4689_EXPOSURE_STEP		1

#define OV4689_REG_GAIN			CCI_REG16(0x3508)
#define OV4689_GAIN_STEP		1
#define OV4689_GAIN_DEFAULT		0x80

#define OV4689_REG_DIG_GAIN		CCI_REG16(0x352a)
#define OV4689_DIG_GAIN_MIN		1
#define OV4689_DIG_GAIN_MAX		0x7fff
#define OV4689_DIG_GAIN_STEP		1
#define OV4689_DIG_GAIN_DEFAULT		0x800

#define OV4689_REG_H_CROP_START		CCI_REG16(0x3800)
#define OV4689_REG_V_CROP_START		CCI_REG16(0x3802)
#define OV4689_REG_H_CROP_END		CCI_REG16(0x3804)
#define OV4689_REG_V_CROP_END		CCI_REG16(0x3806)
#define OV4689_REG_H_OUTPUT_SIZE	CCI_REG16(0x3808)
#define OV4689_REG_V_OUTPUT_SIZE	CCI_REG16(0x380a)

#define OV4689_REG_HTS			CCI_REG16(0x380c)
#define OV4689_HTS_DIVIDER		4
#define OV4689_HTS_MAX			0x7fff

#define OV4689_REG_VTS			CCI_REG16(0x380e)
#define OV4689_VTS_MAX			0x7fff

#define OV4689_REG_H_WIN_OFF		CCI_REG16(0x3810)
#define OV4689_REG_V_WIN_OFF		CCI_REG16(0x3812)

#define OV4689_REG_TIMING_FORMAT1	CCI_REG8(0x3820) /* Vertical */
#define OV4689_REG_TIMING_FORMAT2	CCI_REG8(0x3821) /* Horizontal */
#define OV4689_TIMING_FLIP_MASK		GENMASK(2, 1)
#define OV4689_TIMING_FLIP_ARRAY	BIT(1)
#define OV4689_TIMING_FLIP_DIGITAL	BIT(2)
#define OV4689_TIMING_FLIP_BOTH		(OV4689_TIMING_FLIP_ARRAY |\
					 OV4689_TIMING_FLIP_DIGITAL)

#define OV4689_REG_ANCHOR_LEFT_START	CCI_REG16(0x4020)
#define OV4689_ANCHOR_LEFT_START_DEF	576
#define OV4689_REG_ANCHOR_LEFT_END	CCI_REG16(0x4022)
#define OV4689_ANCHOR_LEFT_END_DEF	831
#define OV4689_REG_ANCHOR_RIGHT_START	CCI_REG16(0x4024)
#define OV4689_ANCHOR_RIGHT_START_DEF	1984
#define OV4689_REG_ANCHOR_RIGHT_END	CCI_REG16(0x4026)
#define OV4689_ANCHOR_RIGHT_END_DEF	2239

#define OV4689_REG_VFIFO_CTRL_01	CCI_REG8(0x4601)

#define OV4689_REG_WB_GAIN_RED		CCI_REG16(0x500c)
#define OV4689_REG_WB_GAIN_BLUE		CCI_REG16(0x5010)
#define OV4689_WB_GAIN_MIN		1
#define OV4689_WB_GAIN_MAX		0xfff
#define OV4689_WB_GAIN_STEP		1
#define OV4689_WB_GAIN_DEFAULT		0x400

#define OV4689_REG_TEST_PATTERN		CCI_REG8(0x5040)
#define OV4689_TEST_PATTERN_ENABLE	0x80
#define OV4689_TEST_PATTERN_DISABLE	0x0

#define OV4689_LANES			4
#define OV4689_XVCLK_FREQ		24000000

#define OV4689_PIXEL_ARRAY_WIDTH	2720
#define OV4689_PIXEL_ARRAY_HEIGHT	1536
#define OV4689_DUMMY_ROWS		8	/* 8 dummy rows on each side */
#define OV4689_DUMMY_COLUMNS		16	/* 16 dummy columns on each side */

static const char *const ov4689_supply_names[] = {
	"avdd", /* Analog power */
	"dovdd", /* Digital I/O power */
	"dvdd", /* Digital core power */
};

enum ov4689_mode_id {
	OV4689_MODE_2688_1520 = 0,
	OV4689_NUM_MODES,
};

struct ov4689_mode {
	enum ov4689_mode_id id;
	u32 width;
	u32 height;
	u32 hts_def;
	u32 hts_min;
	u32 vts_def;
	u32 exp_def;
	u32 pixel_rate;
	const struct cci_reg_sequence *reg_list;
	unsigned int num_regs;
};

struct ov4689 {
	struct device *dev;
	struct regmap *regmap;
	struct clk *xvclk;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	struct regulator_bulk_data supplies[ARRAY_SIZE(ov4689_supply_names)];

	struct v4l2_subdev subdev;
	struct media_pad pad;

	u32 clock_rate;

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *exposure;

	const struct ov4689_mode *cur_mode;
};

#define to_ov4689(sd) container_of(sd, struct ov4689, subdev)

struct ov4689_gain_range {
	u32 logical_min;
	u32 logical_max;
	u32 offset;
	u32 divider;
	u32 physical_min;
	u32 physical_max;
};

/*
 * Xclk 24Mhz
 * max_framerate 90fps
 * mipi_datarate per lane 1008Mbps
 */
static const struct cci_reg_sequence ov4689_2688x1520_regs[] = {
	/* System control*/
	{ CCI_REG8(0x0103), 0x01 }, /* SC_CTRL0103 software_reset = 1 */
	{ CCI_REG8(0x3000), 0x20 }, /* SC_CMMN_PAD_OEN0 FSIN_output_enable = 1 */
	{ CCI_REG8(0x3021), 0x03 }, /*
				     * SC_CMMN_MISC_CTRL fst_stby_ctr = 0,
				     * sleep_no_latch_enable = 0
				     */

	/* AEC PK */
	{ CCI_REG8(0x3503), 0x04 }, /* AEC_MANUAL gain_input_as_sensor_gain_format = 1 */

	/* ADC and analog control*/
	{ CCI_REG8(0x3603), 0x40 },
	{ CCI_REG8(0x3604), 0x02 },
	{ CCI_REG8(0x3609), 0x12 },
	{ CCI_REG8(0x360c), 0x08 },
	{ CCI_REG8(0x360f), 0xe5 },
	{ CCI_REG8(0x3608), 0x8f },
	{ CCI_REG8(0x3611), 0x00 },
	{ CCI_REG8(0x3613), 0xf7 },
	{ CCI_REG8(0x3616), 0x58 },
	{ CCI_REG8(0x3619), 0x99 },
	{ CCI_REG8(0x361b), 0x60 },
	{ CCI_REG8(0x361e), 0x79 },
	{ CCI_REG8(0x3634), 0x10 },
	{ CCI_REG8(0x3635), 0x10 },
	{ CCI_REG8(0x3636), 0x15 },
	{ CCI_REG8(0x3646), 0x86 },
	{ CCI_REG8(0x364a), 0x0b },

	/* Sensor control */
	{ CCI_REG8(0x3700), 0x17 },
	{ CCI_REG8(0x3701), 0x22 },
	{ CCI_REG8(0x3703), 0x10 },
	{ CCI_REG8(0x370a), 0x37 },
	{ CCI_REG8(0x3706), 0x63 },
	{ CCI_REG8(0x3709), 0x3c },
	{ CCI_REG8(0x370c), 0x30 },
	{ CCI_REG8(0x3710), 0x24 },
	{ CCI_REG8(0x3720), 0x28 },
	{ CCI_REG8(0x3729), 0x7b },
	{ CCI_REG8(0x372b), 0xbd },
	{ CCI_REG8(0x372c), 0xbc },
	{ CCI_REG8(0x372e), 0x52 },
	{ CCI_REG8(0x373c), 0x0e },
	{ CCI_REG8(0x373e), 0x33 },
	{ CCI_REG8(0x3743), 0x10 },
	{ CCI_REG8(0x3744), 0x88 },
	{ CCI_REG8(0x3745), 0xc0 },
	{ CCI_REG8(0x374c), 0x00 },
	{ CCI_REG8(0x374e), 0x23 },
	{ CCI_REG8(0x3751), 0x7b },
	{ CCI_REG8(0x3753), 0xbd },
	{ CCI_REG8(0x3754), 0xbc },
	{ CCI_REG8(0x3756), 0x52 },
	{ CCI_REG8(0x376b), 0x20 },
	{ CCI_REG8(0x3774), 0x51 },
	{ CCI_REG8(0x3776), 0xbd },
	{ CCI_REG8(0x3777), 0xbd },
	{ CCI_REG8(0x3781), 0x18 },
	{ CCI_REG8(0x3783), 0x25 },
	{ CCI_REG8(0x3798), 0x1b },

	/* Timing control */
	{ CCI_REG8(0x3819), 0x01 }, /* VSYNC_END_L vsync_end_point[7:0] = 0x01 */

	/* OTP control */
	{ CCI_REG8(0x3d85), 0x36 }, /* OTP_REG85 OTP_power_up_load_setting_enable = 1,
				     * OTP_power_up_load_data_enable = 1,
				     * OTP_bist_select = 1 (compare with zero)
				     */
	{ CCI_REG8(0x3d8c), 0x71 }, /* OTP_SETTING_STT_ADDRESS_H */
	{ CCI_REG8(0x3d8d), 0xcb }, /* OTP_SETTING_STT_ADDRESS_L */

	/* BLC registers*/
	{ CCI_REG8(0x4001), 0x40 }, /* DEBUG_MODE */
	{ CCI_REG8(0x401b), 0x00 }, /* DEBUG_MODE */
	{ CCI_REG8(0x401d), 0x00 }, /* DEBUG_MODE */
	{ CCI_REG8(0x401f), 0x00 }, /* DEBUG_MODE */

	/* ADC sync control */
	{ CCI_REG8(0x4500), 0x6c }, /* ADC_SYNC_CTRL */
	{ CCI_REG8(0x4503), 0x01 }, /* ADC_SYNC_CTRL */

	/* Temperature monitor */
	{ CCI_REG8(0x4d00), 0x04 }, /* TPM_CTRL_00 tmp_slope[15:8] = 0x04 */
	{ CCI_REG8(0x4d01), 0x42 }, /* TPM_CTRL_01 tmp_slope[7:0] = 0x42 */
	{ CCI_REG8(0x4d02), 0xd1 }, /* TPM_CTRL_02 tpm_offset[31:24] = 0xd1 */
	{ CCI_REG8(0x4d03), 0x93 }, /* TPM_CTRL_03 tpm_offset[23:16] = 0x93 */
	{ CCI_REG8(0x4d04), 0xf5 }, /* TPM_CTRL_04 tpm_offset[15:8]  = 0xf5 */
	{ CCI_REG8(0x4d05), 0xc1 }, /* TPM_CTRL_05 tpm_offset[7:0]   = 0xc1 */

	/* pre-ISP control */
	{ CCI_REG8(0x5050), 0x0c }, /* DEBUG_MODE */

	/* OTP-DPC control */
	{ CCI_REG8(0x5501), 0x10 }, /* OTP_DPC_START_L otp_start_address[7:0] = 0x10 */
	{ CCI_REG8(0x5503), 0x0f }, /* OTP_DPC_END_L otp_end_address[7:0] = 0x0f */
};

static const struct ov4689_mode supported_modes[] = {
	{
		.id = OV4689_MODE_2688_1520,
		.width = 2688,
		.height = 1520,
		.exp_def = 1536,
		.hts_def = 10296,
		.hts_min = 3432,
		.vts_def = 1554,
		.pixel_rate = 480000000,
		.reg_list = ov4689_2688x1520_regs,
		.num_regs = ARRAY_SIZE(ov4689_2688x1520_regs),
	},
};

static const u64 link_freq_menu_items[] = { 504000000 };

static const char *const ov4689_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/*
 * These coefficients are based on those used in Rockchip's camera
 * engine, with minor tweaks for continuity.
 */
static const struct ov4689_gain_range ov4689_gain_ranges[] = {
	{
		.logical_min = 0,
		.logical_max = 255,
		.offset = 0,
		.divider = 1,
		.physical_min = 0,
		.physical_max = 255,
	},
	{
		.logical_min = 256,
		.logical_max = 511,
		.offset = 252,
		.divider = 2,
		.physical_min = 376,
		.physical_max = 504,
	},
	{
		.logical_min = 512,
		.logical_max = 1023,
		.offset = 758,
		.divider = 4,
		.physical_min = 884,
		.physical_max = 1012,
	},
	{
		.logical_min = 1024,
		.logical_max = 2047,
		.offset = 1788,
		.divider = 8,
		.physical_min = 1912,
		.physical_max = 2047,
	},
};

static void ov4689_fill_fmt(const struct ov4689_mode *mode,
			    struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = V4L2_FIELD_NONE;
}

static int ov4689_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *mbus_fmt = &fmt->format;
	struct ov4689 *ov4689 = to_ov4689(sd);

	/* only one mode supported for now */
	ov4689_fill_fmt(ov4689->cur_mode, mbus_fmt);

	return 0;
}

static int ov4689_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov4689_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int ov4689_enable_test_pattern(struct ov4689 *ov4689, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | OV4689_TEST_PATTERN_ENABLE;
	else
		val = OV4689_TEST_PATTERN_DISABLE;

	return cci_write(ov4689->regmap, OV4689_REG_TEST_PATTERN,
			 val, NULL);
}

static int ov4689_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = OV4689_PIXEL_ARRAY_WIDTH;
		sel->r.height = OV4689_PIXEL_ARRAY_HEIGHT;
		return 0;
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.top = OV4689_DUMMY_ROWS;
		sel->r.left = OV4689_DUMMY_COLUMNS;
		sel->r.width =
			OV4689_PIXEL_ARRAY_WIDTH - 2 * OV4689_DUMMY_COLUMNS;
		sel->r.height =
			OV4689_PIXEL_ARRAY_HEIGHT - 2 * OV4689_DUMMY_ROWS;
		return 0;
	}

	return -EINVAL;
}

static int ov4689_setup_timings(struct ov4689 *ov4689)
{
	const struct ov4689_mode *mode = ov4689->cur_mode;
	struct regmap *rm = ov4689->regmap;
	int ret = 0;

	cci_write(rm, OV4689_REG_H_CROP_START, 8, &ret);
	cci_write(rm, OV4689_REG_V_CROP_START, 8, &ret);
	cci_write(rm, OV4689_REG_H_CROP_END, 2711, &ret);
	cci_write(rm, OV4689_REG_V_CROP_END, 1531, &ret);

	cci_write(rm, OV4689_REG_H_OUTPUT_SIZE, mode->width, &ret);
	cci_write(rm, OV4689_REG_V_OUTPUT_SIZE, mode->height, &ret);

	cci_write(rm, OV4689_REG_H_WIN_OFF, 8, &ret);
	cci_write(rm, OV4689_REG_V_WIN_OFF, 4, &ret);

	cci_write(rm, OV4689_REG_VFIFO_CTRL_01, 167, &ret);

	return ret;
}

static int ov4689_setup_blc_anchors(struct ov4689 *ov4689)
{
	struct regmap *rm = ov4689->regmap;
	int ret = 0;

	cci_write(rm, OV4689_REG_ANCHOR_LEFT_START, 16, &ret);
	cci_write(rm, OV4689_REG_ANCHOR_LEFT_END, 1999, &ret);
	cci_write(rm, OV4689_REG_ANCHOR_RIGHT_START, 2400, &ret);
	cci_write(rm, OV4689_REG_ANCHOR_RIGHT_END, 2415, &ret);

	return ret;
}

static int ov4689_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov4689 *ov4689 = to_ov4689(sd);
	struct v4l2_subdev_state *sd_state;
	struct device *dev = ov4689->dev;
	int ret = 0;

	sd_state = v4l2_subdev_lock_and_get_active_state(&ov4689->subdev);

	if (on) {
		ret = pm_runtime_resume_and_get(dev);
		if (ret < 0)
			goto unlock_and_return;

		ret = cci_multi_reg_write(ov4689->regmap,
					  ov4689->cur_mode->reg_list,
					  ov4689->cur_mode->num_regs,
					  NULL);
		if (ret) {
			pm_runtime_put(dev);
			goto unlock_and_return;
		}

		ret = ov4689_setup_timings(ov4689);
		if (ret) {
			pm_runtime_put(dev);
			goto unlock_and_return;
		}

		ret = ov4689_setup_blc_anchors(ov4689);
		if (ret) {
			pm_runtime_put(dev);
			goto unlock_and_return;
		}

		ret = __v4l2_ctrl_handler_setup(&ov4689->ctrl_handler);
		if (ret) {
			pm_runtime_put(dev);
			goto unlock_and_return;
		}

		ret = cci_write(ov4689->regmap, OV4689_REG_CTRL_MODE,
				OV4689_MODE_STREAMING, NULL);
		if (ret) {
			pm_runtime_put(dev);
			goto unlock_and_return;
		}
	} else {
		cci_write(ov4689->regmap, OV4689_REG_CTRL_MODE,
			  OV4689_MODE_SW_STANDBY, NULL);
		pm_runtime_mark_last_busy(dev);
		pm_runtime_put_autosuspend(dev);
	}

unlock_and_return:
	v4l2_subdev_unlock_state(sd_state);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov4689_cal_delay(struct ov4689 *ov4689, u32 cycles)
{
	return DIV_ROUND_UP(cycles * 1000,
			    DIV_ROUND_UP(ov4689->clock_rate, 1000));
}

static int __maybe_unused ov4689_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov4689 *ov4689 = to_ov4689(sd);
	u32 delay_us;
	int ret;

	ret = clk_prepare_enable(ov4689->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	gpiod_set_value_cansleep(ov4689->reset_gpio, 1);

	ret = regulator_bulk_enable(ARRAY_SIZE(ov4689_supply_names),
				    ov4689->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	gpiod_set_value_cansleep(ov4689->reset_gpio, 0);
	usleep_range(500, 1000);
	gpiod_set_value_cansleep(ov4689->pwdn_gpio, 0);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ov4689_cal_delay(ov4689, 8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(ov4689->xvclk);

	return ret;
}

static int __maybe_unused ov4689_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov4689 *ov4689 = to_ov4689(sd);

	gpiod_set_value_cansleep(ov4689->pwdn_gpio, 1);
	clk_disable_unprepare(ov4689->xvclk);
	gpiod_set_value_cansleep(ov4689->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ov4689_supply_names),
			       ov4689->supplies);
	return 0;
}

static int ov4689_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct v4l2_mbus_framefmt *fmt =
		v4l2_subdev_state_get_format(sd_state, 0);

	ov4689_fill_fmt(&supported_modes[OV4689_MODE_2688_1520], fmt);

	return 0;
}

static const struct dev_pm_ops ov4689_pm_ops = {
	SET_RUNTIME_PM_OPS(ov4689_power_off, ov4689_power_on, NULL)
};

static const struct v4l2_subdev_video_ops ov4689_video_ops = {
	.s_stream = ov4689_s_stream,
};

static const struct v4l2_subdev_pad_ops ov4689_pad_ops = {
	.enum_mbus_code = ov4689_enum_mbus_code,
	.enum_frame_size = ov4689_enum_frame_sizes,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ov4689_set_fmt,
	.get_selection = ov4689_get_selection,
};

static const struct v4l2_subdev_internal_ops ov4689_internal_ops = {
	.init_state = ov4689_init_state,
};

static const struct v4l2_subdev_ops ov4689_subdev_ops = {
	.video = &ov4689_video_ops,
	.pad = &ov4689_pad_ops,
};

/*
 * Map userspace (logical) gain to sensor (physical) gain using
 * ov4689_gain_ranges table.
 */
static int ov4689_map_gain(struct ov4689 *ov4689, int logical_gain, int *result)
{
	const struct ov4689_gain_range *range;
	unsigned int n;

	for (n = 0; n < ARRAY_SIZE(ov4689_gain_ranges); n++) {
		if (logical_gain >= ov4689_gain_ranges[n].logical_min &&
		    logical_gain <= ov4689_gain_ranges[n].logical_max)
			break;
	}

	if (n == ARRAY_SIZE(ov4689_gain_ranges)) {
		dev_warn_ratelimited(ov4689->dev,
				     "no mapping found for gain %d\n",
				     logical_gain);
		return -EINVAL;
	}

	range = &ov4689_gain_ranges[n];

	*result = clamp(range->offset + (logical_gain) / range->divider,
			range->physical_min, range->physical_max);
	return 0;
}

static int ov4689_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov4689 *ov4689 =
		container_of(ctrl->handler, struct ov4689, ctrl_handler);
	struct regmap *regmap = ov4689->regmap;
	struct device *dev = ov4689->dev;
	int sensor_gain = 0;
	s64 max_expo;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max_expo = ov4689->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(ov4689->exposure,
					 ov4689->exposure->minimum, max_expo,
					 ov4689->exposure->step,
					 ov4689->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of exposure are fractional part */
		cci_write(regmap, OV4689_REG_EXPOSURE, ctrl->val << 4, &ret);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov4689_map_gain(ov4689, ctrl->val, &sensor_gain);
		cci_write(regmap, OV4689_REG_GAIN, sensor_gain, &ret);
		break;
	case V4L2_CID_VBLANK:
		cci_write(regmap, OV4689_REG_VTS,
			  ctrl->val + ov4689->cur_mode->height, &ret);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov4689_enable_test_pattern(ov4689, ctrl->val);
		break;
	case V4L2_CID_HBLANK:
		cci_write(regmap, OV4689_REG_HTS,
			  (ctrl->val + ov4689->cur_mode->width) /
			  OV4689_HTS_DIVIDER, &ret);
		break;
	case V4L2_CID_VFLIP:
		cci_update_bits(regmap, OV4689_REG_TIMING_FORMAT1,
				OV4689_TIMING_FLIP_MASK,
				ctrl->val ? OV4689_TIMING_FLIP_BOTH : 0, &ret);
		break;
	case V4L2_CID_HFLIP:
		cci_update_bits(regmap, OV4689_REG_TIMING_FORMAT2,
				OV4689_TIMING_FLIP_MASK,
				ctrl->val ? 0 : OV4689_TIMING_FLIP_BOTH, &ret);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		cci_write(regmap, OV4689_REG_DIG_GAIN, ctrl->val, &ret);
		break;
	case V4L2_CID_RED_BALANCE:
		cci_write(regmap, OV4689_REG_WB_GAIN_RED, ctrl->val, &ret);
		break;
	case V4L2_CID_BLUE_BALANCE:
		cci_write(regmap, OV4689_REG_WB_GAIN_BLUE, ctrl->val, &ret);
		break;
	default:
		dev_warn(dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		ret = -EINVAL;
		break;
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov4689_ctrl_ops = {
	.s_ctrl = ov4689_set_ctrl,
};

static int ov4689_initialize_controls(struct ov4689 *ov4689)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov4689->subdev);
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *handler;
	const struct ov4689_mode *mode;
	s64 exposure_max, vblank_def;
	s64 hblank_def, hblank_min;
	struct v4l2_ctrl *ctrl;
	int ret;

	handler = &ov4689->ctrl_handler;
	mode = ov4689->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 15);
	if (ret)
		return ret;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ, 0, 0,
				      link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE, 0,
			  mode->pixel_rate, 1, mode->pixel_rate);

	hblank_def = mode->hts_def - mode->width;
	hblank_min = mode->hts_min - mode->width;
	v4l2_ctrl_new_std(handler, &ov4689_ctrl_ops, V4L2_CID_HBLANK,
			  hblank_min, OV4689_HTS_MAX - mode->width,
			  OV4689_HTS_DIVIDER, hblank_def);

	vblank_def = mode->vts_def - mode->height;
	v4l2_ctrl_new_std(handler, &ov4689_ctrl_ops, V4L2_CID_VBLANK,
			  vblank_def, OV4689_VTS_MAX - mode->height, 1,
			  vblank_def);

	exposure_max = mode->vts_def - 4;
	ov4689->exposure =
		v4l2_ctrl_new_std(handler, &ov4689_ctrl_ops, V4L2_CID_EXPOSURE,
				  OV4689_EXPOSURE_MIN, exposure_max,
				  OV4689_EXPOSURE_STEP, mode->exp_def);

	v4l2_ctrl_new_std(handler, &ov4689_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  ov4689_gain_ranges[0].logical_min,
			  ov4689_gain_ranges[ARRAY_SIZE(ov4689_gain_ranges) - 1]
				  .logical_max,
			  OV4689_GAIN_STEP, OV4689_GAIN_DEFAULT);

	v4l2_ctrl_new_std_menu_items(handler, &ov4689_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov4689_test_pattern_menu) - 1,
				     0, 0, ov4689_test_pattern_menu);

	v4l2_ctrl_new_std(handler, &ov4689_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &ov4689_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(handler, &ov4689_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV4689_DIG_GAIN_MIN, OV4689_DIG_GAIN_MAX,
			  OV4689_DIG_GAIN_STEP, OV4689_DIG_GAIN_DEFAULT);

	v4l2_ctrl_new_std(handler, &ov4689_ctrl_ops, V4L2_CID_RED_BALANCE,
			  OV4689_WB_GAIN_MIN, OV4689_WB_GAIN_MAX,
			  OV4689_WB_GAIN_STEP, OV4689_WB_GAIN_DEFAULT);

	v4l2_ctrl_new_std(handler, &ov4689_ctrl_ops, V4L2_CID_BLUE_BALANCE,
			  OV4689_WB_GAIN_MIN, OV4689_WB_GAIN_MAX,
			  OV4689_WB_GAIN_STEP, OV4689_WB_GAIN_DEFAULT);

	if (handler->error) {
		ret = handler->error;
		dev_err(ov4689->dev, "Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto err_free_handler;

	ret = v4l2_ctrl_new_fwnode_properties(handler, &ov4689_ctrl_ops,
					      &props);
	if (ret)
		goto err_free_handler;

	ov4689->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov4689_check_sensor_id(struct ov4689 *ov4689,
				  struct i2c_client *client)
{
	struct device *dev = ov4689->dev;
	u64 id = 0;
	int ret;

	ret = cci_read(ov4689->regmap, OV4689_REG_CHIP_ID, &id, NULL);
	if (ret) {
		dev_err(dev, "Cannot read sensor ID\n");
		return ret;
	}

	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor ID %06llx, expected %06x\n",
			id, CHIP_ID);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int ov4689_configure_regulators(struct ov4689 *ov4689)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ov4689_supply_names); i++)
		ov4689->supplies[i].supply = ov4689_supply_names[i];

	return devm_regulator_bulk_get(ov4689->dev,
				       ARRAY_SIZE(ov4689_supply_names),
				       ov4689->supplies);
}

static u64 ov4689_check_link_frequency(struct v4l2_fwnode_endpoint *ep)
{
	const u64 *freqs = link_freq_menu_items;
	unsigned int i, j;

	for (i = 0; i < ARRAY_SIZE(link_freq_menu_items); i++) {
		for (j = 0; j < ep->nr_of_link_frequencies; j++)
			if (freqs[i] == ep->link_frequencies[j])
				return freqs[i];
	}

	return 0;
}

static int ov4689_check_hwcfg(struct device *dev)
{
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *endpoint;
	int ret;

	endpoint = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!endpoint)
		return -EINVAL;

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &bus_cfg);
	fwnode_handle_put(endpoint);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != OV4689_LANES) {
		dev_err(dev, "Only a 4-lane CSI2 config is supported");
		ret = -EINVAL;
		goto out_free_bus_cfg;
	}

	if (!ov4689_check_link_frequency(&bus_cfg)) {
		dev_err(dev, "No supported link frequency found\n");
		ret = -EINVAL;
	}

out_free_bus_cfg:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int ov4689_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct v4l2_subdev *sd;
	struct ov4689 *ov4689;
	int ret;

	ret = ov4689_check_hwcfg(dev);
	if (ret)
		return ret;

	ov4689 = devm_kzalloc(dev, sizeof(*ov4689), GFP_KERNEL);
	if (!ov4689)
		return -ENOMEM;

	ov4689->dev = dev;

	ov4689->cur_mode = &supported_modes[OV4689_MODE_2688_1520];

	ov4689->xvclk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(ov4689->xvclk))
		return dev_err_probe(dev, PTR_ERR(ov4689->xvclk),
				     "Failed to get external clock\n");

	if (!ov4689->xvclk) {
		dev_dbg(dev,
			"No clock provided, using clock-frequency property\n");
		device_property_read_u32(dev, "clock-frequency",
					 &ov4689->clock_rate);
	} else {
		ov4689->clock_rate = clk_get_rate(ov4689->xvclk);
	}

	if (ov4689->clock_rate != OV4689_XVCLK_FREQ) {
		dev_err(dev,
			"External clock rate mismatch: got %d Hz, expected %d Hz\n",
			ov4689->clock_rate, OV4689_XVCLK_FREQ);
		return -EINVAL;
	}

	ov4689->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(ov4689->regmap)) {
		ret = PTR_ERR(ov4689->regmap);
		dev_err(dev, "failed to initialize CCI: %d\n", ret);
		return ret;
	}

	ov4689->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(ov4689->reset_gpio)) {
		dev_err(dev, "Failed to get reset-gpios\n");
		return PTR_ERR(ov4689->reset_gpio);
	}

	ov4689->pwdn_gpio = devm_gpiod_get_optional(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov4689->pwdn_gpio)) {
		dev_err(dev, "Failed to get pwdn-gpios\n");
		return PTR_ERR(ov4689->pwdn_gpio);
	}

	ret = ov4689_configure_regulators(ov4689);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to get power regulators\n");

	sd = &ov4689->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov4689_subdev_ops);
	sd->internal_ops = &ov4689_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ret = ov4689_initialize_controls(ov4689);
	if (ret) {
		dev_err(dev, "Failed to initialize controls\n");
		return ret;
	}

	ret = ov4689_power_on(dev);
	if (ret)
		goto err_free_handler;

	ret = ov4689_check_sensor_id(ov4689, client);
	if (ret)
		goto err_power_off;


	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ov4689->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, 1, &ov4689->pad);
	if (ret < 0)
		goto err_power_off;

	sd->state_lock = ov4689->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(sd);
	if (ret) {
		dev_err(dev, "Could not register v4l2 device\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	ret = v4l2_async_register_subdev_sensor(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_subdev_pm;
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

err_clean_subdev_pm:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	v4l2_subdev_cleanup(sd);
err_clean_entity:
	media_entity_cleanup(&sd->entity);
err_power_off:
	ov4689_power_off(dev);
err_free_handler:
	v4l2_ctrl_handler_free(&ov4689->ctrl_handler);

	return ret;
}

static void ov4689_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov4689 *ov4689 = to_ov4689(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_subdev_cleanup(sd);
	v4l2_ctrl_handler_free(&ov4689->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		ov4689_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static const struct of_device_id ov4689_of_match[] = {
	{ .compatible = "ovti,ov4689" },
	{},
};
MODULE_DEVICE_TABLE(of, ov4689_of_match);

static struct i2c_driver ov4689_i2c_driver = {
	.driver = {
		.name = "ov4689",
		.pm = &ov4689_pm_ops,
		.of_match_table = ov4689_of_match,
	},
	.probe = ov4689_probe,
	.remove	= ov4689_remove,
};

module_i2c_driver(ov4689_i2c_driver);

MODULE_DESCRIPTION("OmniVision ov4689 sensor driver");
MODULE_LICENSE("GPL");

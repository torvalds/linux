// SPDX-License-Identifier: GPL-2.0
/*
 * Support for GalaxyCore GC0310 VGA camera sensor.
 *
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 * Copyright (c) 2023-2025 Hans de Goede <hansg@kernel.org>
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/string.h>
#include <linux/types.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define GC0310_NATIVE_WIDTH			656
#define GC0310_NATIVE_HEIGHT			496

/*
 * The actual PLL output rate is unknown, the datasheet
 * says that the formula for the frame-time in pixels is:
 * rowtime = win-width + hblank + sh-delay + 4
 * frametime = rowtime * (win-height + vblank)
 * Filling this in and multiplying by 30 fps gives:
 * pixelrate = (660 + 178 + 42 + 4) * (498 + 27) * 30 = 13923000
 */
#define GC0310_PIXELRATE			13923000
/* single lane, bus-format is 8 bpp, CSI-2 is double data rate */
#define GC0310_LINK_FREQ			(GC0310_PIXELRATE * 8 / 2)
#define GC0310_MCLK_FREQ			19200000
#define GC0310_FPS				30
#define GC0310_SKIP_FRAMES			3

#define GC0310_ID				0xa310

#define GC0310_RESET_RELATED_REG		CCI_REG8(0xfe)
#define GC0310_REGISTER_PAGE_0			0x0
#define GC0310_REGISTER_PAGE_3			0x3

/*
 * GC0310 System control registers
 */
#define GC0310_SW_STREAM_REG			CCI_REG8(0x10)

#define GC0310_START_STREAMING			0x94 /* 8-bit enable */
#define GC0310_STOP_STREAMING			0x0 /* 8-bit disable */

#define GC0310_SC_CMMN_CHIP_ID_REG		CCI_REG16(0xf0)

#define GC0310_AEC_PK_EXPO_REG			CCI_REG16(0x03)
#define GC0310_AGC_ADJ_REG			CCI_REG8(0x48)
#define GC0310_DGC_ADJ_REG			CCI_REG8(0x71)

#define GC0310_H_CROP_START_REG			CCI_REG16(0x09)
#define GC0310_V_CROP_START_REG			CCI_REG16(0x0b)
#define GC0310_H_OUTSIZE_REG			CCI_REG16(0x0f)
#define GC0310_V_OUTSIZE_REG			CCI_REG16(0x0d)

#define GC0310_H_BLANKING_REG			CCI_REG16(0x05)
/* Hblank-register + sh-delay + H-crop + 4 (from hw) */
#define GC0310_H_BLANK_DEFAULT			(178 + 42 + 4 + 4)

#define GC0310_V_BLANKING_REG			CCI_REG16(0x07)
/* Vblank needs an offset compensate for the small V-crop done */
#define GC0310_V_BLANK_OFFSET			2
/* Vsync start time + 1 row vsync + vsync end time + offset */
#define GC0310_V_BLANK_MIN			(9 + 1 + 4 + GC0310_V_BLANK_OFFSET)
#define GC0310_V_BLANK_DEFAULT			(27 + GC0310_V_BLANK_OFFSET)
#define GC0310_V_BLANK_MAX			(4095 - GC0310_NATIVE_HEIGHT)

#define GC0310_SH_DELAY_REG			CCI_REG8(0x11)
#define GC0310_VS_START_TIME_REG		CCI_REG8(0x12)
#define GC0310_VS_END_TIME_REG			CCI_REG8(0x13)

#define to_gc0310_sensor(x) container_of(x, struct gc0310_device, sd)

struct gc0310_device {
	struct v4l2_subdev sd;
	struct media_pad pad;

	struct regmap *regmap;
	struct gpio_desc *reset;
	struct gpio_desc *powerdown;

	struct gc0310_ctrls {
		struct v4l2_ctrl_handler handler;
		struct v4l2_ctrl *exposure;
		struct v4l2_ctrl *gain;
		struct v4l2_ctrl *link_freq;
		struct v4l2_ctrl *pixel_rate;
		struct v4l2_ctrl *vblank;
		struct v4l2_ctrl *hblank;
	} ctrls;
};

struct gc0310_reg {
	u8 reg;
	u8 val;
};

static const struct reg_sequence gc0310_reset_register[] = {
	/* System registers */
	{ 0xfe, 0xf0 },
	{ 0xfe, 0xf0 },
	{ 0xfe, 0x00 },

	{ 0xfc, 0x0e }, /* 4e */
	{ 0xfc, 0x0e }, /* 16//4e // [0]apwd [6]regf_clk_gate */
	{ 0xf2, 0x80 }, /* sync output */
	{ 0xf3, 0x00 }, /* 1f//01 data output */
	{ 0xf7, 0x33 }, /* f9 */
	{ 0xf8, 0x05 }, /* 00 */
	{ 0xf9, 0x0e }, /* 0x8e //0f */
	{ 0xfa, 0x11 },

	/* MIPI */
	{ 0xfe, 0x03 },
	{ 0x01, 0x03 }, /* mipi 1lane */
	{ 0x02, 0x22 }, /* 0x33 */
	{ 0x03, 0x94 },
	{ 0x04, 0x01 }, /* fifo_prog */
	{ 0x05, 0x00 }, /* fifo_prog */
	{ 0x06, 0x80 }, /* b0  //YUV ISP data */
	{ 0x11, 0x2a }, /* 1e //LDI set YUV422 */
	{ 0x12, 0x90 }, /* 00 //04 //00 //04//00 //LWC[7:0] */
	{ 0x13, 0x02 }, /* 05 //05 //LWC[15:8] */
	{ 0x15, 0x12 }, /* 0x10 //DPHYY_MODE read_ready */
	{ 0x17, 0x01 },
	{ 0x40, 0x08 },
	{ 0x41, 0x00 },
	{ 0x42, 0x00 },
	{ 0x43, 0x00 },
	{ 0x21, 0x02 }, /* 0x01 */
	{ 0x22, 0x02 }, /* 0x01 */
	{ 0x23, 0x01 }, /* 0x05 //Nor:0x05 DOU:0x06 */
	{ 0x29, 0x00 },
	{ 0x2A, 0x25 }, /* 0x05 //data zero 0x7a de */
	{ 0x2B, 0x02 },

	{ 0xfe, 0x00 },

	/* CISCTL */
	{ 0x00, 0x2f }, /* 2f//0f//02//01 */
	{ 0x01, 0x0f }, /* 06 */
	{ 0x02, 0x04 },
	{ 0x4f, 0x00 }, /* AEC 0FF */
	{ 0x03, 0x01 }, /* 0x03 //04 */
	{ 0x04, 0xc0 }, /* 0xe8 //58 */
	{ 0x05, 0x00 },
	{ 0x06, 0xb2 }, /* 0x0a //HB */
	/* Vblank (reg 0x07 + 0x08) gets set by the vblank ctrl */
	{ 0x09, 0x00 }, /* row start */
	{ 0x0a, 0x00 },
	{ 0x0b, 0x00 }, /* col start */
	{ 0x0c, 0x00 },
	{ 0x0d, 0x01 }, /* height */
	{ 0x0e, 0xf2 }, /* 0xf7 //height */
	{ 0x0f, 0x02 }, /* width */
	{ 0x10, 0x94 }, /* 0xa0 //height */
	{ 0x17, 0x14 },
	{ 0x18, 0x1a }, /* 0a//[4]double reset */
	{ 0x19, 0x14 }, /* AD pipeline */
	{ 0x1b, 0x48 },
	{ 0x1e, 0x6b }, /* 3b//col bias */
	{ 0x1f, 0x28 }, /* 20//00//08//txlow */
	{ 0x20, 0x89 }, /* 88//0c//[3:2]DA15 */
	{ 0x21, 0x49 }, /* 48//[3] txhigh */
	{ 0x22, 0xb0 },
	{ 0x23, 0x04 }, /* [1:0]vcm_r */
	{ 0x24, 0x16 }, /* 15 */
	{ 0x34, 0x20 }, /* [6:4] rsg high//range */

	/* BLK */
	{ 0x26, 0x23 }, /* [1]dark_current_en [0]offset_en */
	{ 0x28, 0xff }, /* BLK_limie_value */
	{ 0x29, 0x00 }, /* global offset */
	{ 0x33, 0x18 }, /* offset_ratio */
	{ 0x37, 0x20 }, /* dark_current_ratio */
	{ 0x2a, 0x00 },
	{ 0x2b, 0x00 },
	{ 0x2c, 0x00 },
	{ 0x2d, 0x00 },
	{ 0x2e, 0x00 },
	{ 0x2f, 0x00 },
	{ 0x30, 0x00 },
	{ 0x31, 0x00 },
	{ 0x47, 0x80 }, /* a7 */
	{ 0x4e, 0x66 }, /* select_row */
	{ 0xa8, 0x02 }, /* win_width_dark, same with crop_win_width */
	{ 0xa9, 0x80 },

	/* ISP */
	{ 0x40, 0x06 }, /* 0xff //ff //48 */
	{ 0x41, 0x00 }, /* 0x21 //00//[0]curve_en */
	{ 0x42, 0x04 }, /* 0xcf //0a//[1]awn_en */
	{ 0x44, 0x18 }, /* 0x18 //02 */
	{ 0x46, 0x02 }, /* 0x03 //sync */
	{ 0x49, 0x03 },
	{ 0x4c, 0x20 }, /* 00[5]pretect exp */
	{ 0x50, 0x01 }, /* crop enable */
	{ 0x51, 0x00 },
	{ 0x52, 0x00 },
	{ 0x53, 0x00 },
	{ 0x54, 0x01 },
	{ 0x55, 0x01 }, /* crop window height */
	{ 0x56, 0xf0 },
	{ 0x57, 0x02 }, /* crop window width */
	{ 0x58, 0x90 },

	/* Gain */
	{ 0x70, 0x70 }, /* 70 //80//global gain */
	{ 0x71, 0x20 }, /* pregain gain */
	{ 0x72, 0x40 }, /* post gain */
	{ 0x5a, 0x84 }, /* 84//analog gain 0  */
	{ 0x5b, 0xc9 }, /* c9 */
	{ 0x5c, 0xed }, /* ed//not use pga gain highest level */
	{ 0x77, 0x40 }, /* R gain 0x74 //awb gain */
	{ 0x78, 0x40 }, /* G gain */
	{ 0x79, 0x40 }, /* B gain 0x5f */

	{ 0x48, 0x00 },
	{ 0xfe, 0x01 },
	{ 0x0a, 0x45 }, /* [7]col gain mode */

	{ 0x3e, 0x40 },
	{ 0x3f, 0x5c },
	{ 0x40, 0x7b },
	{ 0x41, 0xbd },
	{ 0x42, 0xf6 },
	{ 0x43, 0x63 },
	{ 0x03, 0x60 },
	{ 0x44, 0x03 },

	/* Dark / Sun mode related */
	{ 0xfe, 0x01 },
	{ 0x45, 0xa4 }, /* 0xf7 */
	{ 0x46, 0xf0 }, /* 0xff //f0//sun value th */
	{ 0x48, 0x03 }, /* sun mode */
	{ 0x4f, 0x60 }, /* sun_clamp */
	{ 0xfe, 0x00 },
};

static const struct reg_sequence gc0310_VGA_30fps[] = {
	{ 0xfe, 0x00 },
	{ 0x0d, 0x01 }, /* height */
	{ 0x0e, 0xf2 }, /* 0xf7 //height */
	{ 0x0f, 0x02 }, /* width */
	{ 0x10, 0x94 }, /* 0xa0 //height */

	{ 0x50, 0x01 }, /* crop enable */
	{ 0x51, 0x00 },
	{ 0x52, 0x00 },
	{ 0x53, 0x00 },
	{ 0x54, 0x01 },
	{ 0x55, 0x01 }, /* crop window height */
	{ 0x56, 0xf0 },
	{ 0x57, 0x02 }, /* crop window width */
	{ 0x58, 0x90 },

	{ 0xfe, 0x03 },
	{ 0x12, 0x90 }, /* 00 //04 //00 //04//00 //LWC[7:0]  */
	{ 0x13, 0x02 }, /* 05 //05 //LWC[15:8] */

	{ 0xfe, 0x00 },
};

static const s64 link_freq_menu_items[] = {
	GC0310_LINK_FREQ,
};

static int gc0310_gain_set(struct gc0310_device *sensor, u32 gain)
{
	u8 again, dgain;
	int ret = 0;

	/* Taken from original driver, this never sets dgain lower then 32? */

	/* Change 0 - 95 to 32 - 127 */
	gain += 32;

	if (gain < 64) {
		again = 0x0; /* sqrt(2) */
		dgain = gain;
	} else {
		again = 0x2; /* 2 * sqrt(2) */
		dgain = gain / 2;
	}

	cci_write(sensor->regmap, GC0310_AGC_ADJ_REG, again, &ret);
	cci_write(sensor->regmap, GC0310_DGC_ADJ_REG, dgain, &ret);
	return ret;
}

static int gc0310_exposure_update_range(struct gc0310_device *sensor)
{
	int exp_max = GC0310_NATIVE_HEIGHT + sensor->ctrls.vblank->val;

	return __v4l2_ctrl_modify_range(sensor->ctrls.exposure, 0, exp_max,
					1, exp_max);
}

static int gc0310_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc0310_device *sensor =
		container_of(ctrl->handler, struct gc0310_device, ctrls.handler);
	int ret;

	/* Update exposure range on vblank changes */
	if (ctrl->id == V4L2_CID_VBLANK) {
		ret = gc0310_exposure_update_range(sensor);
		if (ret)
			return ret;
	}

	/* Only apply changes to the controls if the device is powered up */
	if (!pm_runtime_get_if_in_use(sensor->sd.dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = cci_write(sensor->regmap, GC0310_AEC_PK_EXPO_REG,
				ctrl->val, NULL);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = gc0310_gain_set(sensor, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(sensor->regmap, GC0310_V_BLANKING_REG,
				ctrl->val - GC0310_V_BLANK_OFFSET,
				NULL);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(sensor->sd.dev);
	return ret;
}

static const struct v4l2_ctrl_ops ctrl_ops = {
	.s_ctrl = gc0310_s_ctrl,
};

/* The GC0310 currently only supports 1 fixed fmt */
static void gc0310_fill_format(struct v4l2_mbus_framefmt *fmt)
{
	memset(fmt, 0, sizeof(*fmt));
	fmt->width = GC0310_NATIVE_WIDTH;
	fmt->height = GC0310_NATIVE_HEIGHT;
	fmt->field = V4L2_FIELD_NONE;
	fmt->code = MEDIA_BUS_FMT_SGRBG8_1X8;
}

static int gc0310_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	/* Only the single fixed 656x496 mode is supported, without croping */
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = GC0310_NATIVE_WIDTH;
		sel->r.height = GC0310_NATIVE_HEIGHT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int gc0310_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct gc0310_device *sensor = to_gc0310_sensor(sd);

	gpiod_set_value_cansleep(sensor->powerdown, 1);
	gpiod_set_value_cansleep(sensor->reset, 1);
	return 0;
}

static int gc0310_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct gc0310_device *sensor = to_gc0310_sensor(sd);

	fsleep(10 * USEC_PER_MSEC);
	gpiod_set_value_cansleep(sensor->reset, 0);
	fsleep(10 * USEC_PER_MSEC);
	gpiod_set_value_cansleep(sensor->powerdown, 0);
	fsleep(10 * USEC_PER_MSEC);

	return 0;
}

static int gc0310_detect(struct gc0310_device *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->sd);
	u64 val;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	ret = cci_read(sensor->regmap, GC0310_SC_CMMN_CHIP_ID_REG, &val, NULL);
	if (ret < 0) {
		dev_err(&client->dev, "read sensor_id failed: %d\n", ret);
		return -ENODEV;
	}

	dev_dbg(&client->dev, "sensor ID = 0x%llx\n", val);

	if (val != GC0310_ID) {
		dev_err(&client->dev, "sensor ID error, read id = 0x%llx, target id = 0x%x\n",
			val, GC0310_ID);
		return -ENODEV;
	}

	dev_dbg(&client->dev, "detect gc0310 success\n");

	return 0;
}

static int gc0310_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 u32 pad, u64 streams_mask)
{
	struct gc0310_device *sensor = to_gc0310_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = pm_runtime_resume_and_get(&client->dev);
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(sensor->regmap,
				     gc0310_reset_register,
				     ARRAY_SIZE(gc0310_reset_register));
	if (ret)
		goto error_power_down;

	ret = regmap_multi_reg_write(sensor->regmap,
				     gc0310_VGA_30fps,
				     ARRAY_SIZE(gc0310_VGA_30fps));
	if (ret)
		goto error_power_down;

	/* restore value of all ctrls */
	ret = __v4l2_ctrl_handler_setup(&sensor->ctrls.handler);

	/* enable per frame MIPI and sensor ctrl reset  */
	cci_write(sensor->regmap, GC0310_RESET_RELATED_REG, 0x30, &ret);

	cci_write(sensor->regmap, GC0310_RESET_RELATED_REG,
		  GC0310_REGISTER_PAGE_3, &ret);
	cci_write(sensor->regmap, GC0310_SW_STREAM_REG,
		  GC0310_START_STREAMING, &ret);
	cci_write(sensor->regmap, GC0310_RESET_RELATED_REG,
		  GC0310_REGISTER_PAGE_0, &ret);

error_power_down:
	if (ret)
		pm_runtime_put(&client->dev);

	return ret;
}

static int gc0310_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  u32 pad, u64 streams_mask)
{
	struct gc0310_device *sensor = to_gc0310_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	cci_write(sensor->regmap, GC0310_RESET_RELATED_REG,
		  GC0310_REGISTER_PAGE_3, &ret);
	cci_write(sensor->regmap, GC0310_SW_STREAM_REG,
		  GC0310_STOP_STREAMING, &ret);
	cci_write(sensor->regmap, GC0310_RESET_RELATED_REG,
		  GC0310_REGISTER_PAGE_0, &ret);

	pm_runtime_put(&client->dev);
	return ret;
}

static int gc0310_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	/* We support only a single format */
	if (code->index)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG8_1X8;
	return 0;
}

static int gc0310_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	/* We support only a single resolution */
	if (fse->index)
		return -EINVAL;

	fse->min_width = GC0310_NATIVE_WIDTH;
	fse->max_width = GC0310_NATIVE_WIDTH;
	fse->min_height = GC0310_NATIVE_HEIGHT;
	fse->max_height = GC0310_NATIVE_HEIGHT;

	return 0;
}

static const struct v4l2_subdev_video_ops gc0310_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops gc0310_pad_ops = {
	.enum_mbus_code = gc0310_enum_mbus_code,
	.enum_frame_size = gc0310_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = v4l2_subdev_get_fmt, /* Only 1 fixed mode supported */
	.get_selection = gc0310_get_selection,
	.set_selection = gc0310_get_selection,
	.enable_streams = gc0310_enable_streams,
	.disable_streams = gc0310_disable_streams,
};

static const struct v4l2_subdev_ops gc0310_ops = {
	.video = &gc0310_video_ops,
	.pad = &gc0310_pad_ops,
};

static int gc0310_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	gc0310_fill_format(v4l2_subdev_state_get_format(sd_state, 0));
	return 0;
}

static const struct v4l2_subdev_internal_ops gc0310_internal_ops = {
	.init_state = gc0310_init_state,
};

static int gc0310_init_controls(struct gc0310_device *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->sd);
	struct v4l2_ctrl_handler *hdl = &sensor->ctrls.handler;
	struct v4l2_fwnode_device_properties props;
	int exp_max, ret;

	v4l2_ctrl_handler_init(hdl, 8);

	/* Use the same lock for controls as for everything else */
	sensor->sd.ctrl_handler = hdl;

	exp_max = GC0310_NATIVE_HEIGHT + GC0310_V_BLANK_DEFAULT;
	sensor->ctrls.exposure =
		v4l2_ctrl_new_std(hdl, &ctrl_ops, V4L2_CID_EXPOSURE, 0,
				  exp_max, 1, exp_max);

	/* 32 steps at base gain 1 + 64 half steps at base gain 2 */
	sensor->ctrls.gain =
		v4l2_ctrl_new_std(hdl, &ctrl_ops, V4L2_CID_ANALOGUE_GAIN, 0, 95, 1, 31);

	sensor->ctrls.link_freq =
		v4l2_ctrl_new_int_menu(hdl, NULL, V4L2_CID_LINK_FREQ,
				       0, 0, link_freq_menu_items);
	sensor->ctrls.pixel_rate =
		v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_PIXEL_RATE, 0,
				  GC0310_PIXELRATE, 1, GC0310_PIXELRATE);

	sensor->ctrls.vblank =
		v4l2_ctrl_new_std(hdl, &ctrl_ops, V4L2_CID_VBLANK,
				  GC0310_V_BLANK_MIN,
				  GC0310_V_BLANK_MAX, 1,
				  GC0310_V_BLANK_DEFAULT);

	sensor->ctrls.hblank =
		v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_HBLANK,
				  GC0310_H_BLANK_DEFAULT,
				  GC0310_H_BLANK_DEFAULT, 1,
				  GC0310_H_BLANK_DEFAULT);

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		return ret;

	v4l2_ctrl_new_fwnode_properties(hdl, &ctrl_ops, &props);

	if (hdl->error)
		return hdl->error;

	sensor->ctrls.pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	sensor->ctrls.link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	sensor->ctrls.hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	return 0;
}

static void gc0310_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc0310_device *sensor = to_gc0310_sensor(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);
	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev)) {
		gc0310_power_off(&client->dev);
		pm_runtime_set_suspended(&client->dev);
	}
}

static int gc0310_check_hwcfg(struct device *dev)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *ep_fwnode;
	unsigned long link_freq_bitmap;
	u32 mclk;
	int ret;

	/*
	 * Sometimes the fwnode graph is initialized by the bridge driver.
	 * Bridge drivers doing this may also add GPIO mappings, wait for this.
	 */
	ep_fwnode = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 0, 0);
	if (!ep_fwnode)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "waiting for fwnode graph endpoint\n");

	ret = fwnode_property_read_u32(dev_fwnode(dev), "clock-frequency",
				       &mclk);
	if (ret) {
		fwnode_handle_put(ep_fwnode);
		return dev_err_probe(dev, ret,
				     "reading clock-frequency property\n");
	}

	if (mclk != GC0310_MCLK_FREQ) {
		fwnode_handle_put(ep_fwnode);
		return dev_err_probe(dev, -EINVAL,
				     "external clock %u is not supported\n",
				     mclk);
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(ep_fwnode, &bus_cfg);
	fwnode_handle_put(ep_fwnode);
	if (ret)
		return dev_err_probe(dev, ret, "parsing endpoint failed\n");

	ret = v4l2_link_freq_to_bitmap(dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       link_freq_menu_items,
				       ARRAY_SIZE(link_freq_menu_items),
				       &link_freq_bitmap);

	if (ret == 0 && bus_cfg.bus.mipi_csi2.num_data_lanes != 1)
		ret = dev_err_probe(dev, -EINVAL,
				    "number of CSI2 data lanes %u is not supported\n",
				    bus_cfg.bus.mipi_csi2.num_data_lanes);

	v4l2_fwnode_endpoint_free(&bus_cfg);
	return ret;
}

static int gc0310_probe(struct i2c_client *client)
{
	struct gc0310_device *sensor;
	int ret;

	ret = gc0310_check_hwcfg(&client->dev);
	if (ret)
		return ret;

	sensor = devm_kzalloc(&client->dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->reset = devm_gpiod_get(&client->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset)) {
		return dev_err_probe(&client->dev, PTR_ERR(sensor->reset),
				     "getting reset GPIO\n");
	}

	sensor->powerdown = devm_gpiod_get(&client->dev, "powerdown", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->powerdown)) {
		return dev_err_probe(&client->dev, PTR_ERR(sensor->powerdown),
				     "getting powerdown GPIO\n");
	}

	v4l2_i2c_subdev_init(&sensor->sd, client, &gc0310_ops);

	sensor->regmap = devm_cci_regmap_init_i2c(client, 8);
	if (IS_ERR(sensor->regmap))
		return PTR_ERR(sensor->regmap);

	gc0310_power_on(&client->dev);

	pm_runtime_set_active(&client->dev);
	pm_runtime_get_noresume(&client->dev);
	pm_runtime_enable(&client->dev);

	ret = gc0310_detect(sensor);
	if (ret)
		goto err_power_down;

	sensor->sd.internal_ops = &gc0310_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = gc0310_init_controls(sensor);
	if (ret)
		goto err_power_down;

	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		goto err_power_down;

	sensor->sd.state_lock = sensor->ctrls.handler.lock;
	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto err_power_down;

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto err_power_down;

	pm_runtime_set_autosuspend_delay(&client->dev, 1000);
	pm_runtime_use_autosuspend(&client->dev);
	pm_runtime_put_autosuspend(&client->dev);

	return 0;

err_power_down:
	pm_runtime_put_noidle(&client->dev);
	gc0310_remove(client);
	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(gc0310_pm_ops,
				 gc0310_power_off, gc0310_power_on, NULL);

static const struct acpi_device_id gc0310_acpi_match[] = {
	{"INT0310"},
	{},
};
MODULE_DEVICE_TABLE(acpi, gc0310_acpi_match);

static struct i2c_driver gc0310_driver = {
	.driver = {
		.name = "gc0310",
		.pm = pm_sleep_ptr(&gc0310_pm_ops),
		.acpi_match_table = gc0310_acpi_match,
	},
	.probe = gc0310_probe,
	.remove = gc0310_remove,
};
module_i2c_driver(gc0310_driver);

MODULE_AUTHOR("Lai, Angie <angie.lai@intel.com>");
MODULE_AUTHOR("Hans de Goede <hansg@kernel.org>");
MODULE_DESCRIPTION("A low-level driver for GalaxyCore GC0310 sensors");
MODULE_LICENSE("GPL");

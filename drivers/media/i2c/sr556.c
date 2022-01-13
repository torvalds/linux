// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Intel Corporation.

#include <linux/unaligned.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define SR556_REG_VALUE_08BIT		1
#define SR556_REG_VALUE_16BIT		2
#define SR556_REG_VALUE_24BIT		3

#define SR556_LINK_FREQ_437MHZ		437000000ULL
#define SR556_MCLK			24000000
#define SR556_DATA_LANES		2
#define SR556_RGB_DEPTH			10

#define SR556_REG_CHIP_ID		0x0f16
#define SR556_CHIP_ID			0x0556

#define SR556_REG_MODE_SELECT		0x0114
#define SR556_MODE_STANDBY		0x0000
#define SR556_MODE_STREAMING		0x0100

/* vertical-timings from sensor */
#define SR556_REG_FLL			0x0006
#define SR556_FLL_30FPS			0x0814
#define SR556_FLL_30FPS_MIN		0x0814
#define SR556_FLL_MAX			0x7fff

/* horizontal-timings from sensor */
#define SR556_REG_LLP			0x0008

/* Exposure controls from sensor */
#define SR556_REG_EXPOSURE		0x0074
#define SR556_EXPOSURE_MIN		6
#define SR556_EXPOSURE_MAX_MARGIN	2
#define SR556_EXPOSURE_STEP		1

/* Analog gain controls from sensor */
#define SR556_REG_ANALOG_GAIN		0x0077
#define SR556_ANAL_GAIN_MIN		0
#define SR556_ANAL_GAIN_MAX		240
#define SR556_ANAL_GAIN_STEP		1

/* Digital gain controls from sensor */
#define SR556_REG_MWB_GR_GAIN		0x0078
#define SR556_REG_MWB_GB_GAIN		0x007a
#define SR556_REG_MWB_R_GAIN		0x007c
#define SR556_REG_MWB_B_GAIN		0x007e
#define SR556_DGTL_GAIN_MIN		0
#define SR556_DGTL_GAIN_MAX		2048
#define SR556_DGTL_GAIN_STEP		1
#define SR556_DGTL_GAIN_DEFAULT		256

/* Test Pattern Control */
#define SR556_REG_ISP			0X0a05
#define SR556_REG_ISP_TPG_EN		0x01
#define SR556_REG_TEST_PATTERN		0x0201

enum {
	SR556_LINK_FREQ_437MHZ_INDEX,
};

static const char * const sr556_supply_names[] = { "avdd", "dvdd", "vio" };

#define SR556_NUM_SUPPLIES		ARRAY_SIZE(sr556_supply_names)

struct sr556_reg {
	u16 address;
	u16 val;
};

struct sr556_reg_list {
	u32 num_of_regs;
	const struct sr556_reg *regs;
};

struct sr556_link_freq_config {
	const struct sr556_reg_list reg_list;
};

struct sr556_mode {
	/* Frame width in pixels */
	u32 width;

	/* Frame height in pixels */
	u32 height;

	/* Horizontal timining size */
	u32 llp;

	/* Default vertical timining size */
	u32 fll_def;

	/* Min vertical timining size */
	u32 fll_min;

	/* Link frequency needed for this resolution */
	u32 link_freq_index;

	/* Sensor register settings for this resolution */
	const struct sr556_reg_list reg_list;
};

#define to_sr556(_sd) container_of(_sd, struct sr556, sd)

//SENSOR_INITIALIZATION
static const struct sr556_reg mipi_data_rate_874mbps[] = {
	{ 0x0e00, 0x0102 }, { 0x0e02, 0x0102 }, { 0x0e0c, 0x0100 }, { 0x2000, 0x4031 },
	{ 0x2002, 0x8400 }, { 0x2004, 0x12b0 }, { 0x2006, 0xe104 }, { 0x2008, 0x12b0 },
	{ 0x200a, 0xe12c }, { 0x200c, 0x12b0 }, { 0x200e, 0xe142 }, { 0x2010, 0x12b0 },
	{ 0x2012, 0xe254 }, { 0x2014, 0x12b0 }, { 0x2016, 0xe150 }, { 0x2018, 0x12b0 },
	{ 0x201a, 0xed9e }, { 0x201c, 0x12b0 }, { 0x201e, 0xe16c }, { 0x2020, 0x12b0 },
	{ 0x2022, 0xe67e }, { 0x2024, 0x12b0 }, { 0x2026, 0xe182 }, { 0x2028, 0x12b0 },
	{ 0x202a, 0xe198 }, { 0x202c, 0x12b0 }, { 0x202e, 0xe1ba }, { 0x2030, 0x12b0 },
	{ 0x2032, 0xf422 }, { 0x2034, 0x12b0 }, { 0x2036, 0xe1c4 }, { 0x2038, 0x12b0 },
	{ 0x203a, 0xf3c2 }, { 0x203c, 0x9392 }, { 0x203e, 0x7114 }, { 0x2040, 0x2003 },
	{ 0x2042, 0x12b0 }, { 0x2044, 0xe1fa }, { 0x2046, 0x3ffa }, { 0x2048, 0x0b00 },
	{ 0x204a, 0x7302 }, { 0x204c, 0x0036 }, { 0x204e, 0x4392 }, { 0x2050, 0x7902 },
	{ 0x2052, 0x4292 }, { 0x2054, 0x7100 }, { 0x2056, 0x82be }, { 0x2058, 0x9382 },
	{ 0x205a, 0x7114 }, { 0x205c, 0x2403 }, { 0x205e, 0x40b2 }, { 0x2060, 0xd081 },
	{ 0x2062, 0x0b88 }, { 0x2064, 0x12b0 }, { 0x2066, 0xe6d8 }, { 0x2068, 0x12b0 },
	{ 0x206a, 0xea1c }, { 0x206c, 0x12b0 }, { 0x206e, 0xe1e4 }, { 0x2070, 0x12b0 },
	{ 0x2072, 0xe370 }, { 0x2074, 0x930f }, { 0x2076, 0x27e2 }, { 0x2078, 0x12b0 },
	{ 0x207a, 0xf3da }, { 0x207c, 0x3fd5 }, { 0x207e, 0x4030 }, { 0x2080, 0xf750 },
	{ 0x27fe, 0xe000 }, { 0x3000, 0x70f8 }, { 0x3002, 0x187f }, { 0x3004, 0x7070 },
	{ 0x3006, 0x0114 }, { 0x3008, 0x70b0 }, { 0x300a, 0x1473 }, { 0x300c, 0x0013 },
	{ 0x300e, 0x140f }, { 0x3010, 0x0040 }, { 0x3012, 0x100f }, { 0x3014, 0x70f8 },
	{ 0x3016, 0x187f }, { 0x3018, 0x7070 }, { 0x301a, 0x0114 }, { 0x301c, 0x70b0 },
	{ 0x301e, 0x1473 }, { 0x3020, 0x0013 }, { 0x3022, 0x140f }, { 0x3024, 0x0040 },
	{ 0x3026, 0x000f }, { 0x0b00, 0x0000 }, { 0x0b02, 0x0045 }, { 0x0b04, 0xb405 },
	{ 0x0b06, 0xc403 }, { 0x0b08, 0x0081 }, { 0x0b0a, 0x8252 }, { 0x0b0c, 0xf814 },
	{ 0x0b0e, 0xc618 }, { 0x0b10, 0xa828 }, { 0x0b12, 0x002c }, { 0x0b14, 0x4068 },
	{ 0x0b16, 0x0000 }, { 0x0f30, 0x6a25 }, { 0x0f32, 0x7067 }, { 0x0954, 0x0009 },
	{ 0x0956, 0x0000 }, { 0x0958, 0xbb80 }, { 0x095a, 0x5140 }, { 0x0c00, 0x1110 },
	{ 0x0c02, 0x0011 }, { 0x0c04, 0x0000 }, { 0x0c06, 0x0200 }, { 0x0c10, 0x0040 },
	{ 0x0c12, 0x0040 }, { 0x0c14, 0x0040 }, { 0x0c16, 0x0040 }, { 0x0a10, 0x4000 },
	{ 0x3068, 0xffff }, { 0x306a, 0xffff }, { 0x006c, 0x0300 }, { 0x005e, 0x0200 },
	{ 0x000e, 0x0100 }, { 0x0e0a, 0x0001 }, { 0x004a, 0x0100 }, { 0x004c, 0x0000 },
	{ 0x004e, 0x0100 }, { 0x000c, 0x0022 }, { 0x0008, 0x0b00 }, { 0x005a, 0x0202 },
	{ 0x0012, 0x000e }, { 0x0018, 0x0a31 }, { 0x0022, 0x0008 }, { 0x0028, 0x0017 },
	{ 0x0024, 0x0028 }, { 0x002a, 0x002d }, { 0x0026, 0x0030 }, { 0x002c, 0x07c7 },
	{ 0x002e, 0x1111 }, { 0x0030, 0x1111 }, { 0x0032, 0x1111 }, { 0x0006, 0x0823 },
	{ 0x0116, 0x07b6 }, { 0x0a22, 0x0000 }, { 0x0a12, 0x0a20 }, { 0x0a14, 0x0798 },
	{ 0x003e, 0x0000 }, { 0x0074, 0x080e }, { 0x0070, 0x0407 }, { 0x0002, 0x0000 },
	{ 0x0a02, 0x0000 }, { 0x0a24, 0x0100 }, { 0x0046, 0x0000 }, { 0x0076, 0x0000 },
	{ 0x0060, 0x0000 }, { 0x0062, 0x0530 }, { 0x0064, 0x0500 }, { 0x0066, 0x0530 },
	{ 0x0068, 0x0500 }, { 0x0122, 0x0300 }, { 0x015a, 0xff08 }, { 0x0126, 0x00f9 },
	{ 0x0804, 0x0200 }, { 0x005c, 0x0100 }, { 0x0a1a, 0x0800 },
};

static const struct sr556_reg mode_2592x1944_regs[] = {
	{ 0x0b0a, 0x8252 }, { 0x0f30, 0x6a25 }, { 0x0f32, 0x7067 }, { 0x004a, 0x0100 },
	{ 0x004c, 0x0000 }, { 0x004e, 0x0100 }, { 0x000c, 0x0022 }, { 0x0008, 0x0b00 },
	{ 0x005a, 0x0202 }, { 0x0012, 0x000e }, { 0x0018, 0x0a31 }, { 0x0022, 0x0008 },
	{ 0x0028, 0x0017 }, { 0x0024, 0x0028 }, { 0x002a, 0x002d }, { 0x0026, 0x0030 },
	{ 0x002c, 0x07c7 }, { 0x002e, 0x1111 }, { 0x0030, 0x1111 }, { 0x0032, 0x1111 },
	{ 0x003c, 0x0001 }, { 0x0006, 0x07d4 }, { 0x0116, 0x07b6 }, { 0x0a22, 0x0000 },
	{ 0x0a12, 0x0a20 }, { 0x0a14, 0x0798 }, { 0x0074, 0x07d2 }, { 0x0070, 0x0411 },
	{ 0x0804, 0x0200 }, { 0x0a04, 0x014a }, { 0x090c, 0x0fdc }, { 0x090e, 0x002d },
	{ 0x0902, 0x4319 }, { 0x0914, 0xc10a }, { 0x0916, 0x071f }, { 0x0918, 0x0408 },
	{ 0x091a, 0x0c0d }, { 0x091c, 0x0f09 }, { 0x091e, 0x0a00 },
};

static const char * const sr556_test_pattern_menu[] = {
	"Disabled",
	"Solid Colour",
	"100% Colour Bars",
	"Fade To Grey Colour Bars",
	"PN9",
	"Gradient Horizontal",
	"Gradient Vertical",
	"Check Board",
	"Slant Pattern",
};

static const s64 link_freq_menu_items[] = {
	SR556_LINK_FREQ_437MHZ,
};

static const struct sr556_link_freq_config link_freq_configs[] = {
	[SR556_LINK_FREQ_437MHZ_INDEX] = {
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_874mbps),
			.regs = mipi_data_rate_874mbps,
		}
	}
};

static const struct sr556_mode supported_modes[] = {
	{
		.width = 2592,
		.height = 1944,
		.fll_def = SR556_FLL_30FPS,
		.fll_min = SR556_FLL_30FPS_MIN,
		.llp = 0x0b00,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2592x1944_regs),
			.regs = mode_2592x1944_regs,
		},
		.link_freq_index = SR556_LINK_FREQ_437MHZ_INDEX,
	},
};

struct sr556 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;

	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[SR556_NUM_SUPPLIES];
	struct clk	*mclk;
	int enabled;

	/* Current mode */
	const struct sr556_mode *cur_mode;

	/* To serialize asynchronus callbacks */
	struct mutex mutex;

	/* Streaming on/off */
	bool streaming;
};

static u64 to_pixel_rate(u32 f_index)
{
	u64 pixel_rate = link_freq_menu_items[f_index] * 2 * SR556_DATA_LANES;

	do_div(pixel_rate, SR556_RGB_DEPTH);

	return pixel_rate;
}

static int sr556_read_reg(struct sr556 *sr556, u16 reg, u16 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sr556->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2];
	u8 data_buf[4] = {0};
	int ret;

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, addr_buf);
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(addr_buf);
	msgs[0].buf = addr_buf;
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = get_unaligned_be32(data_buf);

	return 0;
}

static int sr556_write_reg(struct sr556 *sr556, u16 reg, u16 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sr556->sd);
	u8 buf[6];

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << 8 * (4 - len), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int sr556_write_reg_list(struct sr556 *sr556,
				const struct sr556_reg_list *r_list)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sr556->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < r_list->num_of_regs; i++) {
		ret = sr556_write_reg(sr556, r_list->regs[i].address,
				      SR556_REG_VALUE_16BIT,
				      r_list->regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "failed to write reg 0x%4.4x. error = %d",
					    r_list->regs[i].address, ret);
			return ret;
		}
	}

	return 0;
}

static int sr556_update_digital_gain(struct sr556 *sr556, u32 d_gain)
{
	int ret;

	ret = sr556_write_reg(sr556, SR556_REG_MWB_GR_GAIN,
			      SR556_REG_VALUE_16BIT, d_gain);
	if (ret)
		return ret;

	ret = sr556_write_reg(sr556, SR556_REG_MWB_GB_GAIN,
			      SR556_REG_VALUE_16BIT, d_gain);
	if (ret)
		return ret;

	ret = sr556_write_reg(sr556, SR556_REG_MWB_R_GAIN,
			      SR556_REG_VALUE_16BIT, d_gain);
	if (ret)
		return ret;

	return sr556_write_reg(sr556, SR556_REG_MWB_B_GAIN,
			       SR556_REG_VALUE_16BIT, d_gain);
}

static int sr556_test_pattern(struct sr556 *sr556, u32 pattern)
{
	int ret;
	u32 val;

	if (pattern) {
		ret = sr556_read_reg(sr556, SR556_REG_ISP,
				     SR556_REG_VALUE_08BIT, &val);
		if (ret)
			return ret;

		ret = sr556_write_reg(sr556, SR556_REG_ISP,
				      SR556_REG_VALUE_08BIT,
				      val | SR556_REG_ISP_TPG_EN);
		if (ret)
			return ret;
	}

	return sr556_write_reg(sr556, SR556_REG_TEST_PATTERN,
			       SR556_REG_VALUE_08BIT, pattern);
}

static int sr556_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sr556 *sr556 = container_of(ctrl->handler,
					     struct sr556, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&sr556->sd);
	s64 exposure_max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Update max exposure while meeting expected vblanking */
		exposure_max = sr556->cur_mode->height + ctrl->val -
			       SR556_EXPOSURE_MAX_MARGIN;
		__v4l2_ctrl_modify_range(sr556->exposure,
					 sr556->exposure->minimum,
					 exposure_max, sr556->exposure->step,
					 exposure_max);
	}

	/* V4L2 controls values will be applied only when power is already up */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = sr556_write_reg(sr556, SR556_REG_ANALOG_GAIN,
				      SR556_REG_VALUE_16BIT, ctrl->val);
		break;

	case V4L2_CID_DIGITAL_GAIN:
		ret = sr556_update_digital_gain(sr556, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE:
		ret = sr556_write_reg(sr556, SR556_REG_EXPOSURE,
				      SR556_REG_VALUE_16BIT, ctrl->val);
		break;

	case V4L2_CID_VBLANK:
		/* Update FLL that meets expected vertical blanking */
		ret = sr556_write_reg(sr556, SR556_REG_FLL,
				      SR556_REG_VALUE_16BIT,
				      sr556->cur_mode->height + ctrl->val);
		break;

	case V4L2_CID_TEST_PATTERN:
		ret = sr556_test_pattern(sr556, ctrl->val);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sr556_ctrl_ops = {
	.s_ctrl = sr556_set_ctrl,
};

static int sr556_init_controls(struct sr556 *sr556)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sr556->sd);
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct v4l2_fwnode_device_properties props;
	s64 exposure_max, h_blank;
	int ret;

	ctrl_hdlr = &sr556->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	ctrl_hdlr->lock = &sr556->mutex;
	sr556->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &sr556_ctrl_ops,
						  V4L2_CID_LINK_FREQ,
					ARRAY_SIZE(link_freq_menu_items) - 1,
					0, link_freq_menu_items);
	if (sr556->link_freq)
		sr556->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	sr556->pixel_rate = v4l2_ctrl_new_std
			    (ctrl_hdlr, &sr556_ctrl_ops,
			     V4L2_CID_PIXEL_RATE, 0,
			     to_pixel_rate(SR556_LINK_FREQ_437MHZ_INDEX),
			     1,
			     to_pixel_rate(SR556_LINK_FREQ_437MHZ_INDEX));
	sr556->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &sr556_ctrl_ops,
					  V4L2_CID_VBLANK,
					  sr556->cur_mode->fll_min -
					  sr556->cur_mode->height,
					  SR556_FLL_MAX -
					  sr556->cur_mode->height, 1,
					  sr556->cur_mode->fll_def -
					  sr556->cur_mode->height);

	h_blank = sr556->cur_mode->llp - sr556->cur_mode->width;

	sr556->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &sr556_ctrl_ops,
					  V4L2_CID_HBLANK, h_blank, h_blank, 1,
					  h_blank);
	if (sr556->hblank)
		sr556->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &sr556_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  SR556_ANAL_GAIN_MIN, SR556_ANAL_GAIN_MAX,
			  SR556_ANAL_GAIN_STEP, SR556_ANAL_GAIN_MIN);
	v4l2_ctrl_new_std(ctrl_hdlr, &sr556_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  SR556_DGTL_GAIN_MIN, SR556_DGTL_GAIN_MAX,
			  SR556_DGTL_GAIN_STEP, SR556_DGTL_GAIN_DEFAULT);
	exposure_max = sr556->cur_mode->fll_def - SR556_EXPOSURE_MAX_MARGIN;
	sr556->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &sr556_ctrl_ops,
					    V4L2_CID_EXPOSURE,
					    SR556_EXPOSURE_MIN, exposure_max,
					    SR556_EXPOSURE_STEP,
					    exposure_max);
	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &sr556_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(sr556_test_pattern_menu) - 1,
				     0, 0, sr556_test_pattern_menu);
	if (ctrl_hdlr->error)
		return ctrl_hdlr->error;

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		return ret;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &sr556_ctrl_ops,
					      &props);
	if (ret)
		return ret;

	sr556->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

static void sr556_assign_pad_format(const struct sr556_mode *mode,
				    struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = MEDIA_BUS_FMT_SGBRG10_1X10;
	fmt->field = V4L2_FIELD_NONE;
}

static int sr556_start_streaming(struct sr556 *sr556)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sr556->sd);
	const struct sr556_reg_list *reg_list;
	int link_freq_index, ret;

	link_freq_index = sr556->cur_mode->link_freq_index;
	reg_list = &link_freq_configs[link_freq_index].reg_list;
	ret = sr556_write_reg_list(sr556, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set plls");
		return ret;
	}

	reg_list = &sr556->cur_mode->reg_list;
	ret = sr556_write_reg_list(sr556, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set mode");
		return ret;
	}

	ret = __v4l2_ctrl_handler_setup(sr556->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = sr556_write_reg(sr556, 0x40, SR556_REG_VALUE_16BIT, 0x0000);
	ret = sr556_write_reg(sr556, 0x42, SR556_REG_VALUE_16BIT, 0x0100);
	ret = sr556_write_reg(sr556, 0x3e, SR556_REG_VALUE_16BIT, 0x0000);

	ret = sr556_write_reg(sr556, SR556_REG_MODE_SELECT,
			      SR556_REG_VALUE_16BIT, SR556_MODE_STREAMING);

	if (ret) {
		dev_err(&client->dev, "failed to set stream");
		return ret;
	}

	return 0;
}

static void sr556_stop_streaming(struct sr556 *sr556)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sr556->sd);

	if (sr556_write_reg(sr556, SR556_REG_MODE_SELECT,
			    SR556_REG_VALUE_16BIT, SR556_MODE_STANDBY))
		dev_err(&client->dev, "failed to set stream");
}

static int sr556_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct sr556 *sr556 = to_sr556(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (sr556->streaming == enable)
		return 0;

	mutex_lock(&sr556->mutex);
	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			mutex_unlock(&sr556->mutex);
			return ret;
		}

		ret = sr556_start_streaming(sr556);
		if (ret) {
			enable = 0;
			sr556_stop_streaming(sr556);
			pm_runtime_put(&client->dev);
		}
	} else {
		sr556_stop_streaming(sr556);
		pm_runtime_put(&client->dev);
	}

	sr556->streaming = enable;
	mutex_unlock(&sr556->mutex);

	return ret;
}

static int __maybe_unused sr556_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sr556 *sr556 = to_sr556(sd);

	mutex_lock(&sr556->mutex);
	if (sr556->streaming)
		sr556_stop_streaming(sr556);

	mutex_unlock(&sr556->mutex);

	return 0;
}

static int __maybe_unused sr556_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sr556 *sr556 = to_sr556(sd);
	int ret;

	mutex_lock(&sr556->mutex);
	if (sr556->streaming) {
		ret = sr556_start_streaming(sr556);
		if (ret)
			goto error;
	}

	mutex_unlock(&sr556->mutex);

	return 0;

error:
	sr556_stop_streaming(sr556);
	sr556->streaming = 0;
	mutex_unlock(&sr556->mutex);
	return ret;
}

static int sr556_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sr556 *sr556 = to_sr556(sd);
	int ret;

	if ((sr556->enabled)++)
		return 0;

	ret = regulator_bulk_enable(SR556_NUM_SUPPLIES,
				    sr556->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	if (sr556->mclk) {
		ret = clk_set_rate(sr556->mclk, SR556_MCLK);
		if (ret) {
			dev_err(dev, "can't set clock frequency");
			return ret;
		}
	}

	ret = clk_prepare_enable(sr556->mclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(sr556->reset_gpio, 0);

	usleep_range(10000, 11000);

	return 0;

reg_off:
	regulator_bulk_disable(SR556_NUM_SUPPLIES, sr556->supplies);

	return ret;
}

static int sr556_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sr556 *sr556 = to_sr556(sd);

	if (--(sr556->enabled) > 0)
		return 0;

	gpiod_set_value_cansleep(sr556->reset_gpio, 1);
	regulator_bulk_disable(SR556_NUM_SUPPLIES, sr556->supplies);
	clk_disable_unprepare(sr556->mclk);

	return 0;
}

static int sr556_set_format(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *fmt)
{
	struct sr556 *sr556 = to_sr556(sd);
	const struct sr556_mode *mode;
	s32 vblank_def, h_blank;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes), width,
				      height, fmt->format.width,
				      fmt->format.height);

	mutex_lock(&sr556->mutex);
	sr556_assign_pad_format(mode, &fmt->format);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_state_get_format(sd_state, fmt->pad) = fmt->format;
	} else {
		sr556->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(sr556->link_freq, mode->link_freq_index);
		__v4l2_ctrl_s_ctrl_int64(sr556->pixel_rate,
					 to_pixel_rate(mode->link_freq_index));

		/* Update limits and set FPS to default */
		vblank_def = mode->fll_def - mode->height;
		__v4l2_ctrl_modify_range(sr556->vblank,
					 mode->fll_min - mode->height,
					 SR556_FLL_MAX - mode->height, 1,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(sr556->vblank, vblank_def);

		h_blank = sr556->cur_mode->llp - sr556->cur_mode->width;

		__v4l2_ctrl_modify_range(sr556->hblank, h_blank, h_blank, 1,
					 h_blank);
	}

	mutex_unlock(&sr556->mutex);

	return 0;
}

static int sr556_get_format(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *fmt)
{
	struct sr556 *sr556 = to_sr556(sd);

	mutex_lock(&sr556->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt->format = *v4l2_subdev_state_get_format(sd_state,
							  fmt->pad);
	else
		sr556_assign_pad_format(sr556->cur_mode, &fmt->format);

	mutex_unlock(&sr556->mutex);

	return 0;
}

static int sr556_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGBRG10_1X10;

	return 0;
}

static int sr556_enum_frame_size(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SGBRG10_1X10)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int sr556_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sr556 *sr556 = to_sr556(sd);

	mutex_lock(&sr556->mutex);
	sr556_assign_pad_format(&supported_modes[0],
				v4l2_subdev_state_get_format(fh->state, 0));
	mutex_unlock(&sr556->mutex);

	return 0;
}

static const struct v4l2_subdev_video_ops sr556_video_ops = {
	.s_stream = sr556_set_stream,
};

static const struct v4l2_subdev_pad_ops sr556_pad_ops = {
	.set_fmt = sr556_set_format,
	.get_fmt = sr556_get_format,
	.enum_mbus_code = sr556_enum_mbus_code,
	.enum_frame_size = sr556_enum_frame_size,
};

static const struct v4l2_subdev_ops sr556_subdev_ops = {
	.video = &sr556_video_ops,
	.pad = &sr556_pad_ops,
};

static const struct media_entity_operations sr556_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops sr556_internal_ops = {
	.open = sr556_open,
};

static int sr556_identify_module(struct sr556 *sr556)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sr556->sd);
	int ret;
	u32 val;

	ret = sr556_read_reg(sr556, SR556_REG_CHIP_ID,
			     SR556_REG_VALUE_16BIT, &val);
	if (ret)
		return ret;

	if (val != SR556_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x",
			SR556_CHIP_ID, val);
		return -ENXIO;
	}

	return 0;
}

static int sr556_check_hwcfg(struct sr556 *sr556, struct device *dev)
{
	struct fwnode_handle *ep;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	u32 mclk;
	int ret = 0;
	unsigned int i, j;

	if (!fwnode)
		return -ENXIO;

	ret = fwnode_property_read_u32(fwnode, "clock-frequency", &mclk);
	if (ret) {
		dev_err(dev, "can't get clock frequency");
		return ret;
	}

	if (mclk != SR556_MCLK) {
		dev_err(dev, "external clock %d is not supported", mclk);
		return -EINVAL;
	}

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != 2) {
		dev_err(dev, "number of CSI2 data lanes %d is not supported",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto check_hwcfg_error;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(dev, "no link frequencies defined");
		ret = -EINVAL;
		goto check_hwcfg_error;
	}

	for (i = 0; i < ARRAY_SIZE(link_freq_menu_items); i++) {
		for (j = 0; j < bus_cfg.nr_of_link_frequencies; j++) {
			if (link_freq_menu_items[i] ==
				bus_cfg.link_frequencies[j])
				break;
		}

		if (j == bus_cfg.nr_of_link_frequencies) {
			dev_err(dev, "no link frequency %lld supported",
				link_freq_menu_items[i]);
			ret = -EINVAL;
			goto check_hwcfg_error;
		}
	}

check_hwcfg_error:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static void sr556_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sr556 *sr556 = to_sr556(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(&client->dev);
	mutex_destroy(&sr556->mutex);
}

static int sr556_of_init(struct sr556 *sr556, struct device *dev)
{
	int i, ret;

	for (i = 0; i < SR556_NUM_SUPPLIES; i++)
		sr556->supplies[i].supply = sr556_supply_names[i];

	ret = devm_regulator_bulk_get(dev, SR556_NUM_SUPPLIES, sr556->supplies);
	if (ret < 0)
		return ret;

	sr556->mclk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(sr556->mclk))
		return PTR_ERR(sr556->mclk);

	/* Request optional enable pin */
	sr556->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(sr556->reset_gpio))
		return PTR_ERR(sr556->reset_gpio);

	return 0;
}

static int sr556_probe(struct i2c_client *client)
{
	struct sr556 *sr556;
	int ret;

	sr556 = devm_kzalloc(&client->dev, sizeof(*sr556), GFP_KERNEL);
	if (!sr556)
		return -ENOMEM;

	ret = sr556_of_init(sr556, &client->dev);
	if (ret)
		return ret;

	ret = sr556_check_hwcfg(sr556, &client->dev);
	if (ret) {
		dev_err(&client->dev, "failed to check HW configuration: %d",
			ret);
		return ret;
	}

	v4l2_i2c_subdev_init(&sr556->sd, client, &sr556_subdev_ops);

	sr556_power_on(&client->dev);

	ret = sr556_identify_module(sr556);
	if (ret) {
		dev_err(&client->dev, "failed to find sensor: %d", ret);
		goto power_off;
	}

	mutex_init(&sr556->mutex);
	sr556->cur_mode = &supported_modes[0];
	ret = sr556_init_controls(sr556);
	if (ret) {
		dev_err(&client->dev, "failed to init controls: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	sr556->sd.internal_ops = &sr556_internal_ops;
	sr556->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sr556->sd.entity.ops = &sr556_subdev_entity_ops;
	sr556->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sr556->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sr556->sd.entity, 1, &sr556->pad);
	if (ret) {
		dev_err(&client->dev, "failed to init entity pads: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&sr556->sd);
	if (ret < 0) {
		dev_err(&client->dev, "failed to register V4L2 subdev: %d",
			ret);
		goto probe_error_media_entity_cleanup;
	}

	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

probe_error_media_entity_cleanup:
	media_entity_cleanup(&sr556->sd.entity);

probe_error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(sr556->sd.ctrl_handler);
	mutex_destroy(&sr556->mutex);

power_off:
	sr556_power_off(&client->dev);

	return ret;
}

static const struct dev_pm_ops sr556_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sr556_suspend, sr556_resume)
	SET_RUNTIME_PM_OPS(sr556_power_off, sr556_power_on, NULL)
};

static const struct of_device_id sr556_of_match[] = {
	{ .compatible = "hynix,sr556" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sr556_of_match);

static struct i2c_driver sr556_i2c_driver = {
	.driver = {
		.name = "sr556",
		.pm = &sr556_pm_ops,
		.of_match_table	= of_match_ptr(sr556_of_match),
	},
	.probe = sr556_probe,
	.remove = sr556_remove,
};

module_i2c_driver(sr556_i2c_driver);

MODULE_AUTHOR("Shawn Tu <shawnx.tu@intel.com>");
MODULE_DESCRIPTION("Hynix SR556 sensor driver");
MODULE_LICENSE("GPL v2");

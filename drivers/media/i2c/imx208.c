// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2021 Intel Corporation

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <linux/unaligned.h>

#define IMX208_REG_MODE_SELECT		0x0100
#define IMX208_MODE_STANDBY		0x00
#define IMX208_MODE_STREAMING		0x01

/* Chip ID */
#define IMX208_REG_CHIP_ID		0x0000
#define IMX208_CHIP_ID			0x0208

/* V_TIMING internal */
#define IMX208_REG_VTS			0x0340
#define IMX208_VTS_60FPS		0x0472
#define IMX208_VTS_BINNING		0x0239
#define IMX208_VTS_60FPS_MIN		0x0458
#define IMX208_VTS_BINNING_MIN		0x0230
#define IMX208_VTS_MAX			0xffff

/* HBLANK control - read only */
#define IMX208_PPL_384MHZ		2248
#define IMX208_PPL_96MHZ		2248

/* Exposure control */
#define IMX208_REG_EXPOSURE		0x0202
#define IMX208_EXPOSURE_MIN		4
#define IMX208_EXPOSURE_STEP		1
#define IMX208_EXPOSURE_DEFAULT		0x190
#define IMX208_EXPOSURE_MAX		65535

/* Analog gain control */
#define IMX208_REG_ANALOG_GAIN		0x0204
#define IMX208_ANA_GAIN_MIN		0
#define IMX208_ANA_GAIN_MAX		0x00e0
#define IMX208_ANA_GAIN_STEP		1
#define IMX208_ANA_GAIN_DEFAULT		0x0

/* Digital gain control */
#define IMX208_REG_GR_DIGITAL_GAIN	0x020e
#define IMX208_REG_R_DIGITAL_GAIN	0x0210
#define IMX208_REG_B_DIGITAL_GAIN	0x0212
#define IMX208_REG_GB_DIGITAL_GAIN	0x0214
#define IMX208_DIGITAL_GAIN_SHIFT	8

/* Orientation */
#define IMX208_REG_ORIENTATION_CONTROL	0x0101

/* Test Pattern Control */
#define IMX208_REG_TEST_PATTERN_MODE	0x0600
#define IMX208_TEST_PATTERN_DISABLE	0x0
#define IMX208_TEST_PATTERN_SOLID_COLOR	0x1
#define IMX208_TEST_PATTERN_COLOR_BARS	0x2
#define IMX208_TEST_PATTERN_GREY_COLOR	0x3
#define IMX208_TEST_PATTERN_PN9		0x4
#define IMX208_TEST_PATTERN_FIX_1	0x100
#define IMX208_TEST_PATTERN_FIX_2	0x101
#define IMX208_TEST_PATTERN_FIX_3	0x102
#define IMX208_TEST_PATTERN_FIX_4	0x103
#define IMX208_TEST_PATTERN_FIX_5	0x104
#define IMX208_TEST_PATTERN_FIX_6	0x105

/* OTP Access */
#define IMX208_OTP_BASE			0x3500
#define IMX208_OTP_SIZE			40

struct imx208_reg {
	u16 address;
	u8 val;
};

struct imx208_reg_list {
	u32 num_of_regs;
	const struct imx208_reg *regs;
};

/* Link frequency config */
struct imx208_link_freq_config {
	u32 pixels_per_line;

	/* PLL registers for this link frequency */
	struct imx208_reg_list reg_list;
};

/* Mode : resolution and related config&values */
struct imx208_mode {
	/* Frame width */
	u32 width;
	/* Frame height */
	u32 height;

	/* V-timing */
	u32 vts_def;
	u32 vts_min;

	/* Index of Link frequency config to be used */
	u32 link_freq_index;
	/* Default register values */
	struct imx208_reg_list reg_list;
};

static const struct imx208_reg pll_ctrl_reg[] = {
	{0x0305, 0x02},
	{0x0307, 0x50},
	{0x303C, 0x3C},
};

static const struct imx208_reg mode_1936x1096_60fps_regs[] = {
	{0x0340, 0x04},
	{0x0341, 0x72},
	{0x0342, 0x04},
	{0x0343, 0x64},
	{0x034C, 0x07},
	{0x034D, 0x90},
	{0x034E, 0x04},
	{0x034F, 0x48},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x3048, 0x00},
	{0x3050, 0x01},
	{0x30D5, 0x00},
	{0x3301, 0x00},
	{0x3318, 0x62},
	{0x0202, 0x01},
	{0x0203, 0x90},
	{0x0205, 0x00},
};

static const struct imx208_reg mode_968_548_60fps_regs[] = {
	{0x0340, 0x02},
	{0x0341, 0x39},
	{0x0342, 0x08},
	{0x0343, 0xC8},
	{0x034C, 0x03},
	{0x034D, 0xC8},
	{0x034E, 0x02},
	{0x034F, 0x24},
	{0x0381, 0x01},
	{0x0383, 0x03},
	{0x0385, 0x01},
	{0x0387, 0x03},
	{0x3048, 0x01},
	{0x3050, 0x02},
	{0x30D5, 0x03},
	{0x3301, 0x10},
	{0x3318, 0x75},
	{0x0202, 0x01},
	{0x0203, 0x90},
	{0x0205, 0x00},
};

static const s64 imx208_discrete_digital_gain[] = {
	1, 2, 4, 8, 16,
};

static const char * const imx208_test_pattern_menu[] = {
	"Disabled",
	"Solid Color",
	"100% Color Bar",
	"Fade to Grey Color Bar",
	"PN9",
	"Fixed Pattern1",
	"Fixed Pattern2",
	"Fixed Pattern3",
	"Fixed Pattern4",
	"Fixed Pattern5",
	"Fixed Pattern6"
};

static const int imx208_test_pattern_val[] = {
	IMX208_TEST_PATTERN_DISABLE,
	IMX208_TEST_PATTERN_SOLID_COLOR,
	IMX208_TEST_PATTERN_COLOR_BARS,
	IMX208_TEST_PATTERN_GREY_COLOR,
	IMX208_TEST_PATTERN_PN9,
	IMX208_TEST_PATTERN_FIX_1,
	IMX208_TEST_PATTERN_FIX_2,
	IMX208_TEST_PATTERN_FIX_3,
	IMX208_TEST_PATTERN_FIX_4,
	IMX208_TEST_PATTERN_FIX_5,
	IMX208_TEST_PATTERN_FIX_6,
};

/* Configurations for supported link frequencies */
#define IMX208_MHZ			(1000 * 1000ULL)
#define IMX208_LINK_FREQ_384MHZ		(384ULL * IMX208_MHZ)
#define IMX208_LINK_FREQ_96MHZ		(96ULL * IMX208_MHZ)

#define IMX208_DATA_RATE_DOUBLE		2
#define IMX208_NUM_OF_LANES		2
#define IMX208_PIXEL_BITS		10

enum {
	IMX208_LINK_FREQ_384MHZ_INDEX,
	IMX208_LINK_FREQ_96MHZ_INDEX,
};

/*
 * pixel_rate = link_freq * data-rate * nr_of_lanes / bits_per_sample
 * data rate => double data rate; number of lanes => 2; bits per pixel => 10
 */
static u64 link_freq_to_pixel_rate(u64 f)
{
	f *= IMX208_DATA_RATE_DOUBLE * IMX208_NUM_OF_LANES;
	do_div(f, IMX208_PIXEL_BITS);

	return f;
}

/* Menu items for LINK_FREQ V4L2 control */
static const s64 link_freq_menu_items[] = {
	[IMX208_LINK_FREQ_384MHZ_INDEX] = IMX208_LINK_FREQ_384MHZ,
	[IMX208_LINK_FREQ_96MHZ_INDEX] = IMX208_LINK_FREQ_96MHZ,
};

/* Link frequency configs */
static const struct imx208_link_freq_config link_freq_configs[] = {
	[IMX208_LINK_FREQ_384MHZ_INDEX] = {
		.pixels_per_line = IMX208_PPL_384MHZ,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(pll_ctrl_reg),
			.regs = pll_ctrl_reg,
		}
	},
	[IMX208_LINK_FREQ_96MHZ_INDEX] = {
		.pixels_per_line = IMX208_PPL_96MHZ,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(pll_ctrl_reg),
			.regs = pll_ctrl_reg,
		}
	},
};

/* Mode configs */
static const struct imx208_mode supported_modes[] = {
	{
		.width = 1936,
		.height = 1096,
		.vts_def = IMX208_VTS_60FPS,
		.vts_min = IMX208_VTS_60FPS_MIN,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1936x1096_60fps_regs),
			.regs = mode_1936x1096_60fps_regs,
		},
		.link_freq_index = IMX208_LINK_FREQ_384MHZ_INDEX,
	},
	{
		.width = 968,
		.height = 548,
		.vts_def = IMX208_VTS_BINNING,
		.vts_min = IMX208_VTS_BINNING_MIN,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_968_548_60fps_regs),
			.regs = mode_968_548_60fps_regs,
		},
		.link_freq_index = IMX208_LINK_FREQ_96MHZ_INDEX,
	},
};

struct imx208 {
	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;

	/* Current mode */
	const struct imx208_mode *cur_mode;

	/*
	 * Mutex for serialized access:
	 * Protect sensor set pad format and start/stop streaming safely.
	 * Protect access to sensor v4l2 controls.
	 */
	struct mutex imx208_mx;

	/* OTP data */
	bool otp_read;
	char otp_data[IMX208_OTP_SIZE];

	/* True if the device has been identified */
	bool identified;
};

static inline struct imx208 *to_imx208(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx208, sd);
}

/* Get bayer order based on flip setting. */
static u32 imx208_get_format_code(struct imx208 *imx208)
{
	/*
	 * Only one bayer order is supported.
	 * It depends on the flip settings.
	 */
	static const u32 codes[2][2] = {
		{ MEDIA_BUS_FMT_SRGGB10_1X10, MEDIA_BUS_FMT_SGRBG10_1X10, },
		{ MEDIA_BUS_FMT_SGBRG10_1X10, MEDIA_BUS_FMT_SBGGR10_1X10, },
	};

	return codes[imx208->vflip->val][imx208->hflip->val];
}

/* Read registers up to 4 at a time */
static int imx208_read_reg(struct imx208 *imx208, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx208->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2] = { reg >> 8, reg & 0xff };
	u8 data_buf[4] = { 0, };
	int ret;

	if (len > 4)
		return -EINVAL;

	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	/* Read data from register */
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

/* Write registers up to 4 at a time */
static int imx208_write_reg(struct imx208 *imx208, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx208->sd);
	u8 buf[6];

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

/* Write a list of registers */
static int imx208_write_regs(struct imx208 *imx208,
			     const struct imx208_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx208->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imx208_write_reg(imx208, regs[i].address, 1,
				       regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "Failed to write reg 0x%4.4x. error = %d\n",
					    regs[i].address, ret);

			return ret;
		}
	}

	return 0;
}

/* Open sub-device */
static int imx208_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_state_get_format(fh->state, 0);

	/* Initialize try_fmt */
	try_fmt->width = supported_modes[0].width;
	try_fmt->height = supported_modes[0].height;
	try_fmt->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	return 0;
}

static int imx208_update_digital_gain(struct imx208 *imx208, u32 len, u32 val)
{
	int ret;

	val = imx208_discrete_digital_gain[val] << IMX208_DIGITAL_GAIN_SHIFT;

	ret = imx208_write_reg(imx208, IMX208_REG_GR_DIGITAL_GAIN, 2, val);
	if (ret)
		return ret;

	ret = imx208_write_reg(imx208, IMX208_REG_GB_DIGITAL_GAIN, 2, val);
	if (ret)
		return ret;

	ret = imx208_write_reg(imx208, IMX208_REG_R_DIGITAL_GAIN, 2, val);
	if (ret)
		return ret;

	return imx208_write_reg(imx208, IMX208_REG_B_DIGITAL_GAIN, 2, val);
}

static int imx208_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx208 *imx208 =
		container_of(ctrl->handler, struct imx208, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx208->sd);
	int ret;

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = imx208_write_reg(imx208, IMX208_REG_ANALOG_GAIN,
				       2, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		ret = imx208_write_reg(imx208, IMX208_REG_EXPOSURE,
				       2, ctrl->val);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = imx208_update_digital_gain(imx208, 2, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		/* Update VTS that meets expected vertical blanking */
		ret = imx208_write_reg(imx208, IMX208_REG_VTS, 2,
				       imx208->cur_mode->height + ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = imx208_write_reg(imx208, IMX208_REG_TEST_PATTERN_MODE,
				       2, imx208_test_pattern_val[ctrl->val]);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		ret = imx208_write_reg(imx208, IMX208_REG_ORIENTATION_CONTROL,
				       1,
				       imx208->hflip->val |
				       imx208->vflip->val << 1);
		break;
	default:
		ret = -EINVAL;
		dev_err(&client->dev,
			"ctrl(id:0x%x,val:0x%x) is not handled\n",
			ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx208_ctrl_ops = {
	.s_ctrl = imx208_set_ctrl,
};

static const struct v4l2_ctrl_config imx208_digital_gain_control = {
	.ops = &imx208_ctrl_ops,
	.id = V4L2_CID_DIGITAL_GAIN,
	.name = "Digital Gain",
	.type = V4L2_CTRL_TYPE_INTEGER_MENU,
	.min = 0,
	.max = ARRAY_SIZE(imx208_discrete_digital_gain) - 1,
	.step = 0,
	.def = 0,
	.menu_skip_mask = 0,
	.qmenu_int = imx208_discrete_digital_gain,
};

static int imx208_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx208 *imx208 = to_imx208(sd);

	if (code->index > 0)
		return -EINVAL;

	code->code = imx208_get_format_code(imx208);

	return 0;
}

static int imx208_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx208 *imx208 = to_imx208(sd);

	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != imx208_get_format_code(imx208))
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static void imx208_mode_to_pad_format(struct imx208 *imx208,
				      const struct imx208_mode *mode,
				      struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = imx208_get_format_code(imx208);
	fmt->format.field = V4L2_FIELD_NONE;
}

static int __imx208_get_pad_format(struct imx208 *imx208,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_format *fmt)
{
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt->format = *v4l2_subdev_state_get_format(sd_state,
							    fmt->pad);
	else
		imx208_mode_to_pad_format(imx208, imx208->cur_mode, fmt);

	return 0;
}

static int imx208_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx208 *imx208 = to_imx208(sd);
	int ret;

	mutex_lock(&imx208->imx208_mx);
	ret = __imx208_get_pad_format(imx208, sd_state, fmt);
	mutex_unlock(&imx208->imx208_mx);

	return ret;
}

static int imx208_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx208 *imx208 = to_imx208(sd);
	const struct imx208_mode *mode;
	s32 vblank_def;
	s32 vblank_min;
	s64 h_blank;
	s64 pixel_rate;
	s64 link_freq;

	mutex_lock(&imx208->imx208_mx);

	fmt->format.code = imx208_get_format_code(imx208);
	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes), width, height,
				      fmt->format.width, fmt->format.height);
	imx208_mode_to_pad_format(imx208, mode, fmt);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_state_get_format(sd_state, fmt->pad) = fmt->format;
	} else {
		imx208->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(imx208->link_freq, mode->link_freq_index);
		link_freq = link_freq_menu_items[mode->link_freq_index];
		pixel_rate = link_freq_to_pixel_rate(link_freq);
		__v4l2_ctrl_s_ctrl_int64(imx208->pixel_rate, pixel_rate);
		/* Update limits and set FPS to default */
		vblank_def = imx208->cur_mode->vts_def -
			     imx208->cur_mode->height;
		vblank_min = imx208->cur_mode->vts_min -
			     imx208->cur_mode->height;
		__v4l2_ctrl_modify_range(imx208->vblank, vblank_min,
					 IMX208_VTS_MAX - imx208->cur_mode->height,
					 1, vblank_def);
		__v4l2_ctrl_s_ctrl(imx208->vblank, vblank_def);
		h_blank =
			link_freq_configs[mode->link_freq_index].pixels_per_line
			 - imx208->cur_mode->width;
		__v4l2_ctrl_modify_range(imx208->hblank, h_blank,
					 h_blank, 1, h_blank);
	}

	mutex_unlock(&imx208->imx208_mx);

	return 0;
}

static int imx208_identify_module(struct imx208 *imx208)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx208->sd);
	int ret;
	u32 val;

	if (imx208->identified)
		return 0;

	ret = imx208_read_reg(imx208, IMX208_REG_CHIP_ID,
			      2, &val);
	if (ret) {
		dev_err(&client->dev, "failed to read chip id %x\n",
			IMX208_CHIP_ID);
		return ret;
	}

	if (val != IMX208_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x\n",
			IMX208_CHIP_ID, val);
		return -EIO;
	}

	imx208->identified = true;

	return 0;
}

/* Start streaming */
static int imx208_start_streaming(struct imx208 *imx208)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx208->sd);
	const struct imx208_reg_list *reg_list;
	int ret, link_freq_index;

	ret = imx208_identify_module(imx208);
	if (ret)
		return ret;

	/* Setup PLL */
	link_freq_index = imx208->cur_mode->link_freq_index;
	reg_list = &link_freq_configs[link_freq_index].reg_list;
	ret = imx208_write_regs(imx208, reg_list->regs, reg_list->num_of_regs);
	if (ret) {
		dev_err(&client->dev, "%s failed to set plls\n", __func__);
		return ret;
	}

	/* Apply default values of current mode */
	reg_list = &imx208->cur_mode->reg_list;
	ret = imx208_write_regs(imx208, reg_list->regs, reg_list->num_of_regs);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(imx208->sd.ctrl_handler);
	if (ret)
		return ret;

	/* set stream on register */
	return imx208_write_reg(imx208, IMX208_REG_MODE_SELECT,
				1, IMX208_MODE_STREAMING);
}

/* Stop streaming */
static int imx208_stop_streaming(struct imx208 *imx208)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx208->sd);
	int ret;

	/* set stream off register */
	ret = imx208_write_reg(imx208, IMX208_REG_MODE_SELECT,
			       1, IMX208_MODE_STANDBY);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);

	/*
	 * Return success even if it was an error, as there is nothing the
	 * caller can do about it.
	 */
	return 0;
}

static int imx208_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx208 *imx208 = to_imx208(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&imx208->imx208_mx);

	if (enable) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret) {
			mutex_unlock(&imx208->imx208_mx);
			return ret;
		}

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = imx208_start_streaming(imx208);
		if (ret)
			goto err_rpm_put;
	} else {
		imx208_stop_streaming(imx208);
		pm_runtime_put(&client->dev);
	}

	mutex_unlock(&imx208->imx208_mx);

	/* vflip and hflip cannot change during streaming */
	v4l2_ctrl_grab(imx208->vflip, enable);
	v4l2_ctrl_grab(imx208->hflip, enable);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
	mutex_unlock(&imx208->imx208_mx);

	return ret;
}

/* Verify chip ID */
static const struct v4l2_subdev_video_ops imx208_video_ops = {
	.s_stream = imx208_set_stream,
};

static const struct v4l2_subdev_pad_ops imx208_pad_ops = {
	.enum_mbus_code = imx208_enum_mbus_code,
	.get_fmt = imx208_get_pad_format,
	.set_fmt = imx208_set_pad_format,
	.enum_frame_size = imx208_enum_frame_size,
};

static const struct v4l2_subdev_ops imx208_subdev_ops = {
	.video = &imx208_video_ops,
	.pad = &imx208_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx208_internal_ops = {
	.open = imx208_open,
};

static int imx208_read_otp(struct imx208 *imx208)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx208->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2] = { IMX208_OTP_BASE >> 8, IMX208_OTP_BASE & 0xff };
	int ret = 0;

	mutex_lock(&imx208->imx208_mx);

	if (imx208->otp_read)
		goto out_unlock;

	ret = pm_runtime_resume_and_get(&client->dev);
	if (ret)
		goto out_unlock;

	ret = imx208_identify_module(imx208);
	if (ret)
		goto out_pm_put;

	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	/* Read data from registers */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = sizeof(imx208->otp_data);
	msgs[1].buf = imx208->otp_data;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret == ARRAY_SIZE(msgs)) {
		imx208->otp_read = true;
		ret = 0;
	}

out_pm_put:
	pm_runtime_put(&client->dev);

out_unlock:
	mutex_unlock(&imx208->imx208_mx);

	return ret;
}

static ssize_t otp_read(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr,
			char *buf, loff_t off, size_t count)
{
	struct i2c_client *client = to_i2c_client(kobj_to_dev(kobj));
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx208 *imx208 = to_imx208(sd);
	int ret;

	ret = imx208_read_otp(imx208);
	if (ret)
		return ret;

	memcpy(buf, &imx208->otp_data[off], count);
	return count;
}

static const BIN_ATTR_RO(otp, IMX208_OTP_SIZE);

/* Initialize control handlers */
static int imx208_init_controls(struct imx208 *imx208)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx208->sd);
	struct v4l2_ctrl_handler *ctrl_hdlr = &imx208->ctrl_handler;
	s64 exposure_max;
	s64 vblank_def;
	s64 vblank_min;
	s64 pixel_rate_min;
	s64 pixel_rate_max;
	int ret;

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	mutex_init(&imx208->imx208_mx);
	ctrl_hdlr->lock = &imx208->imx208_mx;
	imx208->link_freq =
		v4l2_ctrl_new_int_menu(ctrl_hdlr,
				       &imx208_ctrl_ops,
				       V4L2_CID_LINK_FREQ,
				       ARRAY_SIZE(link_freq_menu_items) - 1,
				       0, link_freq_menu_items);

	if (imx208->link_freq)
		imx208->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate_max = link_freq_to_pixel_rate(link_freq_menu_items[0]);
	pixel_rate_min =
		link_freq_to_pixel_rate(link_freq_menu_items[ARRAY_SIZE(link_freq_menu_items) - 1]);
	/* By default, PIXEL_RATE is read only */
	imx208->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx208_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       pixel_rate_min, pixel_rate_max,
					       1, pixel_rate_max);

	vblank_def = imx208->cur_mode->vts_def - imx208->cur_mode->height;
	vblank_min = imx208->cur_mode->vts_min - imx208->cur_mode->height;
	imx208->vblank =
		v4l2_ctrl_new_std(ctrl_hdlr, &imx208_ctrl_ops, V4L2_CID_VBLANK,
				  vblank_min,
				  IMX208_VTS_MAX - imx208->cur_mode->height, 1,
				  vblank_def);

	imx208->hblank =
		v4l2_ctrl_new_std(ctrl_hdlr, &imx208_ctrl_ops, V4L2_CID_HBLANK,
				  IMX208_PPL_384MHZ - imx208->cur_mode->width,
				  IMX208_PPL_384MHZ - imx208->cur_mode->width,
				  1,
				  IMX208_PPL_384MHZ - imx208->cur_mode->width);

	if (imx208->hblank)
		imx208->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	exposure_max = imx208->cur_mode->vts_def - 8;
	v4l2_ctrl_new_std(ctrl_hdlr, &imx208_ctrl_ops, V4L2_CID_EXPOSURE,
			  IMX208_EXPOSURE_MIN, exposure_max,
			  IMX208_EXPOSURE_STEP, IMX208_EXPOSURE_DEFAULT);

	imx208->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx208_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	if (imx208->hflip)
		imx208->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;
	imx208->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx208_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (imx208->vflip)
		imx208->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	v4l2_ctrl_new_std(ctrl_hdlr, &imx208_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX208_ANA_GAIN_MIN, IMX208_ANA_GAIN_MAX,
			  IMX208_ANA_GAIN_STEP, IMX208_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_custom(ctrl_hdlr, &imx208_digital_gain_control, NULL);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx208_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx208_test_pattern_menu) - 1,
				     0, 0, imx208_test_pattern_menu);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	imx208->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&imx208->imx208_mx);

	return ret;
}

static void imx208_free_controls(struct imx208 *imx208)
{
	v4l2_ctrl_handler_free(imx208->sd.ctrl_handler);
}

static int imx208_probe(struct i2c_client *client)
{
	struct imx208 *imx208;
	int ret;
	bool full_power;
	u32 val = 0;

	device_property_read_u32(&client->dev, "clock-frequency", &val);
	if (val != 19200000) {
		dev_err(&client->dev,
			"Unsupported clock-frequency %u. Expected 19200000.\n",
			val);
		return -EINVAL;
	}

	imx208 = devm_kzalloc(&client->dev, sizeof(*imx208), GFP_KERNEL);
	if (!imx208)
		return -ENOMEM;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&imx208->sd, client, &imx208_subdev_ops);

	full_power = acpi_dev_state_d0(&client->dev);
	if (full_power) {
		/* Check module identity */
		ret = imx208_identify_module(imx208);
		if (ret) {
			dev_err(&client->dev, "failed to find sensor: %d", ret);
			goto error_probe;
		}
	}

	/* Set default mode to max resolution */
	imx208->cur_mode = &supported_modes[0];

	ret = imx208_init_controls(imx208);
	if (ret) {
		dev_err(&client->dev, "failed to init controls: %d", ret);
		goto error_probe;
	}

	/* Initialize subdev */
	imx208->sd.internal_ops = &imx208_internal_ops;
	imx208->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx208->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx208->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&imx208->sd.entity, 1, &imx208->pad);
	if (ret) {
		dev_err(&client->dev, "%s failed:%d\n", __func__, ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx208->sd);
	if (ret < 0)
		goto error_media_entity;

	ret = device_create_bin_file(&client->dev, &bin_attr_otp);
	if (ret) {
		dev_err(&client->dev, "sysfs otp creation failed\n");
		goto error_async_subdev;
	}

	/* Set the device's state to active if it's in D0 state. */
	if (full_power)
		pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

error_async_subdev:
	v4l2_async_unregister_subdev(&imx208->sd);

error_media_entity:
	media_entity_cleanup(&imx208->sd.entity);

error_handler_free:
	imx208_free_controls(imx208);

error_probe:
	mutex_destroy(&imx208->imx208_mx);

	return ret;
}

static void imx208_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx208 *imx208 = to_imx208(sd);

	device_remove_bin_file(&client->dev, &bin_attr_otp);
	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	imx208_free_controls(imx208);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	mutex_destroy(&imx208->imx208_mx);
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id imx208_acpi_ids[] = {
	{ "INT3478" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(acpi, imx208_acpi_ids);
#endif

static struct i2c_driver imx208_i2c_driver = {
	.driver = {
		.name = "imx208",
		.acpi_match_table = ACPI_PTR(imx208_acpi_ids),
	},
	.probe = imx208_probe,
	.remove = imx208_remove,
	.flags = I2C_DRV_ACPI_WAIVE_D0_PROBE,
};

module_i2c_driver(imx208_i2c_driver);

MODULE_AUTHOR("Yeh, Andy <andy.yeh@intel.com>");
MODULE_AUTHOR("Chen, Ping-chung <ping-chung.chen@intel.com>");
MODULE_AUTHOR("Shawn Tu");
MODULE_DESCRIPTION("Sony IMX208 sensor driver");
MODULE_LICENSE("GPL v2");

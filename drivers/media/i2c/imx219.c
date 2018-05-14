/*
 * Driver for IMX219 CMOS Image Sensor from Sony
 *
 * Copyright (C) 2014, Andrew Chew <achew@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>

/* IMX219 supported geometry */
#define IMX219_TABLE_END		0xffff
#define IMX219_ANALOGUE_GAIN_MULTIPLIER	256
#define IMX219_ANALOGUE_GAIN_MIN	(1 * IMX219_ANALOGUE_GAIN_MULTIPLIER)
#define IMX219_ANALOGUE_GAIN_MAX	(11 * IMX219_ANALOGUE_GAIN_MULTIPLIER)
#define IMX219_ANALOGUE_GAIN_DEFAULT	(2 * IMX219_ANALOGUE_GAIN_MULTIPLIER)

/* In dB*256 */
#define IMX219_DIGITAL_GAIN_MIN		256
#define IMX219_DIGITAL_GAIN_MAX		43663
#define IMX219_DIGITAL_GAIN_DEFAULT	256

#define IMX219_DIGITAL_EXPOSURE_MIN	0
#define IMX219_DIGITAL_EXPOSURE_MAX	4095
#define IMX219_DIGITAL_EXPOSURE_DEFAULT	1575

#define IMX219_EXP_LINES_MARGIN	4

static const s64 link_freq_menu_items[] = {
	456000000,
};

struct imx219_reg {
	u16 addr;
	u8 val;
};

struct imx219_mode {
	u32 width;
	u32 height;
	u32 max_fps;
	u32 hts_def;
	u32 vts_def;
	const struct imx219_reg *reg_list;
};

/* MCLK:24MHz  3280x2464  21.2fps   MIPI LANE2 */
static const struct imx219_reg imx219_init_tab_3280_2464_21fps[] = {
	{0x30EB, 0x05},		/* Access Code for address over 0x3000 */
	{0x30EB, 0x0C},		/* Access Code for address over 0x3000 */
	{0x300A, 0xFF},		/* Access Code for address over 0x3000 */
	{0x300B, 0xFF},		/* Access Code for address over 0x3000 */
	{0x30EB, 0x05},		/* Access Code for address over 0x3000 */
	{0x30EB, 0x09},		/* Access Code for address over 0x3000 */
	{0x0114, 0x01},		/* CSI_LANE_MODE[1:0} */
	{0x0128, 0x00},		/* DPHY_CNTRL */
	{0x012A, 0x18},		/* EXCK_FREQ[15:8] */
	{0x012B, 0x00},		/* EXCK_FREQ[7:0] */
	{0x015A, 0x01},		/* INTEG TIME[15:8] */
	{0x015B, 0xF4},		/* INTEG TIME[7:0] */
	{0x0160, 0x09},		/* FRM_LENGTH_A[15:8] */
	{0x0161, 0xC4},		/* FRM_LENGTH_A[7:0] */
	{0x0162, 0x0D},		/* LINE_LENGTH_A[15:8] */
	{0x0163, 0x78},		/* LINE_LENGTH_A[7:0] */
	{0x0260, 0x09},		/* FRM_LENGTH_B[15:8] */
	{0x0261, 0xC4},		/* FRM_LENGTH_B[7:0] */
	{0x0262, 0x0D},		/* LINE_LENGTH_B[15:8] */
	{0x0263, 0x78},		/* LINE_LENGTH_B[7:0] */
	{0x0170, 0x01},		/* X_ODD_INC_A[2:0] */
	{0x0171, 0x01},		/* Y_ODD_INC_A[2:0] */
	{0x0270, 0x01},		/* X_ODD_INC_B[2:0] */
	{0x0271, 0x01},		/* Y_ODD_INC_B[2:0] */
	{0x0174, 0x00},		/* BINNING_MODE_H_A */
	{0x0175, 0x00},		/* BINNING_MODE_V_A */
	{0x0274, 0x00},		/* BINNING_MODE_H_B */
	{0x0275, 0x00},		/* BINNING_MODE_V_B */
	{0x018C, 0x0A},		/* CSI_DATA_FORMAT_A[15:8] */
	{0x018D, 0x0A},		/* CSI_DATA_FORMAT_A[7:0] */
	{0x028C, 0x0A},		/* CSI_DATA_FORMAT_B[15:8] */
	{0x028D, 0x0A},		/* CSI_DATA_FORMAT_B[7:0] */
	{0x0301, 0x05},		/* VTPXCK_DIV */
	{0x0303, 0x01},		/* VTSYCK_DIV */
	{0x0304, 0x03},		/* PREPLLCK_VT_DIV[3:0] */
	{0x0305, 0x03},		/* PREPLLCK_OP_DIV[3:0] */
	{0x0306, 0x00},		/* PLL_VT_MPY[10:8] */
	{0x0307, 0x39},		/* PLL_VT_MPY[7:0] */
	{0x0309, 0x0A},		/* OPPXCK_DIV[4:0] */
	{0x030B, 0x01},		/* OPSYCK_DIV */
	{0x030C, 0x00},		/* PLL_OP_MPY[10:8] */
	{0x030D, 0x72},		/* PLL_OP_MPY[7:0] */
	{0x455E, 0x00},		/* CIS Tuning */
	{0x471E, 0x4B},		/* CIS Tuning */
	{0x4767, 0x0F},		/* CIS Tuning */
	{0x4750, 0x14},		/* CIS Tuning */
	{0x47B4, 0x14},		/* CIS Tuning */
	{IMX219_TABLE_END, 0x00}
};

/* MCLK:24MHz  1920x1080  30fps   MIPI LANE2 */
static const struct imx219_reg imx219_init_tab_1920_1080_30fps[] = {
	{0x30EB, 0x05},
	{0x30EB, 0x0C},
	{0x300A, 0xFF},
	{0x300B, 0xFF},
	{0x30EB, 0x05},
	{0x30EB, 0x09},
	{0x0114, 0x01},
	{0x0128, 0x00},
	{0x012A, 0x18},
	{0x012B, 0x00},
	{0x0160, 0x06},
	{0x0161, 0xE6},
	{0x0162, 0x0D},
	{0x0163, 0x78},
	{0x0164, 0x02},
	{0x0165, 0xA8},
	{0x0166, 0x0A},
	{0x0167, 0x27},
	{0x0168, 0x02},
	{0x0169, 0xB4},
	{0x016A, 0x06},
	{0x016B, 0xEB},
	{0x016C, 0x07},
	{0x016D, 0x80},
	{0x016E, 0x04},
	{0x016F, 0x38},
	{0x0170, 0x01},
	{0x0171, 0x01},
	{0x0174, 0x00},
	{0x0175, 0x00},
	{0x018C, 0x0A},
	{0x018D, 0x0A},
	{0x0301, 0x05},
	{0x0303, 0x01},
	{0x0304, 0x03},
	{0x0305, 0x03},
	{0x0306, 0x00},
	{0x0307, 0x39},
	{0x0309, 0x0A},
	{0x030B, 0x01},
	{0x030C, 0x00},
	{0x030D, 0x72},
	{0x455E, 0x00},
	{0x471E, 0x4B},
	{0x4767, 0x0F},
	{0x4750, 0x14},
	{0x4540, 0x00},
	{0x47B4, 0x14},
	{IMX219_TABLE_END, 0x00}
};

static const struct imx219_reg start[] = {
	{0x0100, 0x01},		/* mode select streaming on */
	{IMX219_TABLE_END, 0x00}
};

static const struct imx219_reg stop[] = {
	{0x0100, 0x00},		/* mode select streaming off */
	{IMX219_TABLE_END, 0x00}
};

enum {
	TEST_PATTERN_DISABLED,
	TEST_PATTERN_SOLID_BLACK,
	TEST_PATTERN_SOLID_WHITE,
	TEST_PATTERN_SOLID_RED,
	TEST_PATTERN_SOLID_GREEN,
	TEST_PATTERN_SOLID_BLUE,
	TEST_PATTERN_COLOR_BAR,
	TEST_PATTERN_FADE_TO_GREY_COLOR_BAR,
	TEST_PATTERN_PN9,
	TEST_PATTERN_16_SPLIT_COLOR_BAR,
	TEST_PATTERN_16_SPLIT_INVERTED_COLOR_BAR,
	TEST_PATTERN_COLUMN_COUNTER,
	TEST_PATTERN_INVERTED_COLUMN_COUNTER,
	TEST_PATTERN_PN31,
	TEST_PATTERN_MAX
};

static const char *const tp_qmenu[] = {
	"Disabled",
	"Solid Black",
	"Solid White",
	"Solid Red",
	"Solid Green",
	"Solid Blue",
	"Color Bar",
	"Fade to Grey Color Bar",
	"PN9",
	"16 Split Color Bar",
	"16 Split Inverted Color Bar",
	"Column Counter",
	"Inverted Column Counter",
	"PN31",
};

#define SIZEOF_I2C_TRANSBUF 32

struct imx219 {
	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct clk *clk;
	struct v4l2_rect crop_rect;
	int hflip;
	int vflip;
	u8 analogue_gain;
	u16 digital_gain;	/* bits 11:0 */
	u16 exposure_time;
	u16 test_pattern;
	u16 test_pattern_solid_color_r;
	u16 test_pattern_solid_color_gr;
	u16 test_pattern_solid_color_b;
	u16 test_pattern_solid_color_gb;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *pixel_rate;
	const struct imx219_mode *cur_mode;
	u16 cur_vts;
};

static const struct imx219_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.max_fps = 30,
		.hts_def = 0x0d78 - IMX219_EXP_LINES_MARGIN,
		.vts_def = 0x06E6,
		.reg_list = imx219_init_tab_1920_1080_30fps,
	},
	{
		.width = 3280,
		.height = 2464,
		.max_fps = 21,
		.hts_def = 0x0d78 - IMX219_EXP_LINES_MARGIN,
		.vts_def = 0x09c4,
		.reg_list = imx219_init_tab_3280_2464_21fps,
	},
};

static struct imx219 *to_imx219(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct imx219, subdev);
}

static int reg_write(struct i2c_client *client, const u16 addr, const u8 data)
{
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	u8 tx[3];
	int ret;

	msg.addr = client->addr;
	msg.buf = tx;
	msg.len = 3;
	msg.flags = 0;
	tx[0] = addr >> 8;
	tx[1] = addr & 0xff;
	tx[2] = data;
	ret = i2c_transfer(adap, &msg, 1);
	mdelay(2);

	return ret == 1 ? 0 : -EIO;
}

static int reg_read(struct i2c_client *client, const u16 addr)
{
	u8 buf[2] = {addr >> 8, addr & 0xff};
	int ret;
	struct i2c_msg msgs[] = {
		{
			.addr  = client->addr,
			.flags = 0,
			.len   = 2,
			.buf   = buf,
		}, {
			.addr  = client->addr,
			.flags = I2C_M_RD,
			.len   = 1,
			.buf   = buf,
		},
	};

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0) {
		dev_warn(&client->dev, "Reading register %x from %x failed\n",
			 addr, client->addr);
		return ret;
	}

	return buf[0];
}

static int reg_write_table(struct i2c_client *client,
			   const struct imx219_reg table[])
{
	const struct imx219_reg *reg;
	int ret;

	for (reg = table; reg->addr != IMX219_TABLE_END; reg++) {
		ret = reg_write(client, reg->addr, reg->val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* V4L2 subdev video operations */
static int imx219_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx219 *priv = to_imx219(client);
	u8 reg = 0x00;
	int ret;

	if (!enable)
		return reg_write_table(client, stop);

	ret = reg_write_table(client, priv->cur_mode->reg_list);
	if (ret)
		return ret;

	/* Handle crop */
	ret = reg_write(client, 0x0164, priv->crop_rect.left >> 8);
	ret |= reg_write(client, 0x0165, priv->crop_rect.left & 0xff);
	ret |= reg_write(client, 0x0166, (priv->crop_rect.left + priv->crop_rect.width - 1) >> 8);
	ret |= reg_write(client, 0x0167, (priv->crop_rect.left + priv->crop_rect.width - 1) & 0xff);
	ret |= reg_write(client, 0x0168, priv->crop_rect.top >> 8);
	ret |= reg_write(client, 0x0169, priv->crop_rect.top & 0xff);
	ret |= reg_write(client, 0x016A, (priv->crop_rect.top + priv->crop_rect.height - 1) >> 8);
	ret |= reg_write(client, 0x016B, (priv->crop_rect.top + priv->crop_rect.height - 1) & 0xff);
	ret |= reg_write(client, 0x016C, priv->crop_rect.width >> 8);
	ret |= reg_write(client, 0x016D, priv->crop_rect.width & 0xff);
	ret |= reg_write(client, 0x016E, priv->crop_rect.height >> 8);
	ret |= reg_write(client, 0x016F, priv->crop_rect.height & 0xff);

	if (ret)
		return ret;

	/* Handle flip/mirror */
	if (priv->hflip)
		reg |= 0x1;
	if (priv->vflip)
		reg |= 0x2;

	ret = reg_write(client, 0x0172, reg);
	if (ret)
		return ret;

	/* Handle test pattern */
	if (priv->test_pattern) {
		ret = reg_write(client, 0x0600, priv->test_pattern >> 8);
		ret |= reg_write(client, 0x0601, priv->test_pattern & 0xff);
		ret |= reg_write(client, 0x0602,
				 priv->test_pattern_solid_color_r >> 8);
		ret |= reg_write(client, 0x0603,
				 priv->test_pattern_solid_color_r & 0xff);
		ret |= reg_write(client, 0x0604,
				 priv->test_pattern_solid_color_gr >> 8);
		ret |= reg_write(client, 0x0605,
				 priv->test_pattern_solid_color_gr & 0xff);
		ret |= reg_write(client, 0x0606,
				 priv->test_pattern_solid_color_b >> 8);
		ret |= reg_write(client, 0x0607,
				 priv->test_pattern_solid_color_b & 0xff);
		ret |= reg_write(client, 0x0608,
				 priv->test_pattern_solid_color_gb >> 8);
		ret |= reg_write(client, 0x0609,
				 priv->test_pattern_solid_color_gb & 0xff);
		ret |= reg_write(client, 0x0620, priv->crop_rect.left >> 8);
		ret |= reg_write(client, 0x0621, priv->crop_rect.left & 0xff);
		ret |= reg_write(client, 0x0622, priv->crop_rect.top >> 8);
		ret |= reg_write(client, 0x0623, priv->crop_rect.top & 0xff);
		ret |= reg_write(client, 0x0624, priv->crop_rect.width >> 8);
		ret |= reg_write(client, 0x0625, priv->crop_rect.width & 0xff);
		ret |= reg_write(client, 0x0626, priv->crop_rect.height >> 8);
		ret |= reg_write(client, 0x0627, priv->crop_rect.height & 0xff);
	} else {
		ret = reg_write(client, 0x0600, 0x00);
		ret |= reg_write(client, 0x0601, 0x00);
	}

	priv->cur_vts = priv->cur_mode->vts_def - IMX219_EXP_LINES_MARGIN;
	if (ret)
		return ret;

	return reg_write_table(client, start);
}

/* V4L2 subdev core operations */
static int imx219_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx219 *priv = to_imx219(client);

	if (on)	{
		dev_dbg(&client->dev, "imx219 power on\n");
		clk_prepare_enable(priv->clk);
	} else if (!on) {
		dev_dbg(&client->dev, "imx219 power off\n");
		clk_disable_unprepare(priv->clk);
	}

	return 0;
}

/* V4L2 ctrl operations */
static int imx219_s_ctrl_test_pattern(struct v4l2_ctrl *ctrl)
{
	struct imx219 *priv =
	    container_of(ctrl->handler, struct imx219, ctrl_handler);

	switch (ctrl->val) {
	case TEST_PATTERN_DISABLED:
		priv->test_pattern = 0x0000;
		break;
	case TEST_PATTERN_SOLID_BLACK:
		priv->test_pattern = 0x0001;
		priv->test_pattern_solid_color_r = 0x0000;
		priv->test_pattern_solid_color_gr = 0x0000;
		priv->test_pattern_solid_color_b = 0x0000;
		priv->test_pattern_solid_color_gb = 0x0000;
		break;
	case TEST_PATTERN_SOLID_WHITE:
		priv->test_pattern = 0x0001;
		priv->test_pattern_solid_color_r = 0x0fff;
		priv->test_pattern_solid_color_gr = 0x0fff;
		priv->test_pattern_solid_color_b = 0x0fff;
		priv->test_pattern_solid_color_gb = 0x0fff;
		break;
	case TEST_PATTERN_SOLID_RED:
		priv->test_pattern = 0x0001;
		priv->test_pattern_solid_color_r = 0x0fff;
		priv->test_pattern_solid_color_gr = 0x0000;
		priv->test_pattern_solid_color_b = 0x0000;
		priv->test_pattern_solid_color_gb = 0x0000;
		break;
	case TEST_PATTERN_SOLID_GREEN:
		priv->test_pattern = 0x0001;
		priv->test_pattern_solid_color_r = 0x0000;
		priv->test_pattern_solid_color_gr = 0x0fff;
		priv->test_pattern_solid_color_b = 0x0000;
		priv->test_pattern_solid_color_gb = 0x0fff;
		break;
	case TEST_PATTERN_SOLID_BLUE:
		priv->test_pattern = 0x0001;
		priv->test_pattern_solid_color_r = 0x0000;
		priv->test_pattern_solid_color_gr = 0x0000;
		priv->test_pattern_solid_color_b = 0x0fff;
		priv->test_pattern_solid_color_gb = 0x0000;
		break;
	case TEST_PATTERN_COLOR_BAR:
		priv->test_pattern = 0x0002;
		break;
	case TEST_PATTERN_FADE_TO_GREY_COLOR_BAR:
		priv->test_pattern = 0x0003;
		break;
	case TEST_PATTERN_PN9:
		priv->test_pattern = 0x0004;
		break;
	case TEST_PATTERN_16_SPLIT_COLOR_BAR:
		priv->test_pattern = 0x0005;
		break;
	case TEST_PATTERN_16_SPLIT_INVERTED_COLOR_BAR:
		priv->test_pattern = 0x0006;
		break;
	case TEST_PATTERN_COLUMN_COUNTER:
		priv->test_pattern = 0x0007;
		break;
	case TEST_PATTERN_INVERTED_COLUMN_COUNTER:
		priv->test_pattern = 0x0008;
		break;
	case TEST_PATTERN_PN31:
		priv->test_pattern = 0x0009;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int imx219_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx219 *priv =
	    container_of(ctrl->handler, struct imx219, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&priv->subdev);
	u8 reg;
	int ret;
	u16 gain = 256;
	u16 a_gain = 256;
	u16 d_gain = 1;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		priv->hflip = ctrl->val;
		break;

	case V4L2_CID_VFLIP:
		priv->vflip = ctrl->val;
		break;

	case V4L2_CID_ANALOGUE_GAIN:
	case V4L2_CID_GAIN:
		/*
		 * hal transfer (gain * 256)  to kernel
		 * than divide into analog gain & digital gain in kernel
		 */

		gain = ctrl->val;
		if (gain < 256)
			gain = 256;
		if (gain > 43663)
			gain = 43663;
		if (gain >= 256 && gain <= 2728) {
			a_gain = gain;
			d_gain = 1 * 256;
		} else {
			a_gain = 2728;
			d_gain = (gain * 256) / a_gain;
		}

		/*
		 * Analog gain, reg range[0, 232], gain value[1, 10.66]
		 * reg = 256 - 256 / again
		 * a_gain here is 256 multify
		 * so the reg = 256 - 256 * 256 / a_gain
		 */
		priv->analogue_gain = (256 - (256 * 256) / a_gain);
		if (a_gain < 256)
			priv->analogue_gain = 0;
		if (priv->analogue_gain > 232)
			priv->analogue_gain = 232;

		/*
		 * Digital gain, reg range[256, 4095], gain rage[1, 16]
		 * reg = dgain * 256
		 */
		priv->digital_gain = d_gain;
		if (priv->digital_gain < 256)
			priv->digital_gain = 256;
		if (priv->digital_gain > 4095)
			priv->digital_gain = 4095;

		/*
		 * for bank A and bank B switch
		 * exposure time , gain, vts must change at the same time
		 * so the exposure & gain can reflect at the same frame
		 */

		ret = reg_write(client, 0x0157, priv->analogue_gain);
		ret |= reg_write(client, 0x0158, priv->digital_gain >> 8);
		ret |= reg_write(client, 0x0159, priv->digital_gain & 0xff);

		return ret;

	case V4L2_CID_EXPOSURE:
		priv->exposure_time = ctrl->val;

		ret = reg_write(client, 0x015a, priv->exposure_time >> 8);
		ret |= reg_write(client, 0x015b, priv->exposure_time & 0xff);
		return ret;

	case V4L2_CID_TEST_PATTERN:
		return imx219_s_ctrl_test_pattern(ctrl);

	case V4L2_CID_VBLANK:
		if (ctrl->val < priv->cur_mode->vts_def)
			ctrl->val = priv->cur_mode->vts_def;
		if ((ctrl->val - IMX219_EXP_LINES_MARGIN) != priv->cur_vts)
			priv->cur_vts = ctrl->val - IMX219_EXP_LINES_MARGIN;
		ret = reg_write(client, 0x0160, ((priv->cur_vts >> 8) & 0xff));
		ret |= reg_write(client, 0x0161, (priv->cur_vts & 0xff));
		return ret;

	default:
		return -EINVAL;
	}
	/* If enabled, apply settings immediately */
	reg = reg_read(client, 0x0100);
	if ((reg & 0x1f) == 0x01)
		imx219_s_stream(&priv->subdev, 1);

	return 0;
}

static int imx219_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int imx219_get_reso_dist(const struct imx219_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct imx219_mode *imx219_find_best_fit(
					struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = imx219_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int imx219_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx219 *priv = to_imx219(client);
	const struct imx219_mode *mode;
	s64 h_blank, v_blank, pixel_rate;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	mode = imx219_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	priv->cur_mode = mode;
	h_blank = mode->hts_def - mode->width;
	__v4l2_ctrl_modify_range(priv->hblank, h_blank,
					h_blank, 1, h_blank);
	v_blank = mode->vts_def - mode->height;
	__v4l2_ctrl_modify_range(priv->vblank, v_blank,
					v_blank,
					1, v_blank);
	pixel_rate = mode->vts_def * mode->hts_def * mode->max_fps;
	__v4l2_ctrl_modify_range(priv->pixel_rate, pixel_rate,
					pixel_rate, 1, pixel_rate);

	/* reset crop window */
	priv->crop_rect.left = 1640 - (mode->width / 2);
	if (priv->crop_rect.left < 0)
		priv->crop_rect.left = 0;
	priv->crop_rect.top = 1232 - (mode->height / 2);
	if (priv->crop_rect.top < 0)
		priv->crop_rect.top = 0;
	priv->crop_rect.width = mode->width;
	priv->crop_rect.height = mode->height;

	return 0;
}

static int imx219_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx219 *priv = to_imx219(client);
	const struct imx219_mode *mode = priv->cur_mode;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	fmt->format.field = V4L2_FIELD_NONE;

	return 0;
}

/* Various V4L2 operations tables */
static struct v4l2_subdev_video_ops imx219_subdev_video_ops = {
	.s_stream = imx219_s_stream,
};

static struct v4l2_subdev_core_ops imx219_subdev_core_ops = {
	.s_power = imx219_s_power,
};

static const struct v4l2_subdev_pad_ops imx219_subdev_pad_ops = {
	.enum_mbus_code = imx219_enum_mbus_code,
	.set_fmt = imx219_set_fmt,
	.get_fmt = imx219_get_fmt,
};

static struct v4l2_subdev_ops imx219_subdev_ops = {
	.core = &imx219_subdev_core_ops,
	.video = &imx219_subdev_video_ops,
	.pad = &imx219_subdev_pad_ops,
};

static const struct v4l2_ctrl_ops imx219_ctrl_ops = {
	.s_ctrl = imx219_s_ctrl,
};

static int imx219_video_probe(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	u16 model_id;
	u32 lot_id;
	u16 chip_id;
	int ret;

	ret = imx219_s_power(subdev, 1);
	if (ret < 0)
		return ret;

	/* Check and show model, lot, and chip ID. */
	ret = reg_read(client, 0x0000);
	if (ret < 0) {
		dev_err(&client->dev, "Failure to read Model ID (high byte)\n");
		goto done;
	}
	model_id = ret << 8;

	ret = reg_read(client, 0x0001);
	if (ret < 0) {
		dev_err(&client->dev, "Failure to read Model ID (low byte)\n");
		goto done;
	}
	model_id |= ret;

	ret = reg_read(client, 0x0004);
	if (ret < 0) {
		dev_err(&client->dev, "Failure to read Lot ID (high byte)\n");
		goto done;
	}
	lot_id = ret << 16;

	ret = reg_read(client, 0x0005);
	if (ret < 0) {
		dev_err(&client->dev, "Failure to read Lot ID (mid byte)\n");
		goto done;
	}
	lot_id |= ret << 8;

	ret = reg_read(client, 0x0006);
	if (ret < 0) {
		dev_err(&client->dev, "Failure to read Lot ID (low byte)\n");
		goto done;
	}
	lot_id |= ret;

	ret = reg_read(client, 0x000D);
	if (ret < 0) {
		dev_err(&client->dev, "Failure to read Chip ID (high byte)\n");
		goto done;
	}
	chip_id = ret << 8;

	ret = reg_read(client, 0x000E);
	if (ret < 0) {
		dev_err(&client->dev, "Failure to read Chip ID (low byte)\n");
		goto done;
	}
	chip_id |= ret;

	if (model_id != 0x0219) {
		dev_err(&client->dev, "Model ID: %x not supported!\n",
			model_id);
		ret = -ENODEV;
		goto done;
	}
	dev_info(&client->dev,
		 "Model ID 0x%04x, Lot ID 0x%06x, Chip ID 0x%04x\n",
		 model_id, lot_id, chip_id);
done:
	imx219_s_power(subdev, 0);
	return ret;
}

static int imx219_ctrls_init(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx219 *priv = to_imx219(client);
	const struct imx219_mode *mode = priv->cur_mode;
	s64 pixel_rate, h_blank, v_blank;
	int ret;

	v4l2_ctrl_handler_init(&priv->ctrl_handler, 10);
	v4l2_ctrl_new_std(&priv->ctrl_handler, &imx219_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->ctrl_handler, &imx219_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);

	/* exposure */
	v4l2_ctrl_new_std(&priv->ctrl_handler, &imx219_ctrl_ops,
			  V4L2_CID_ANALOGUE_GAIN,
			  IMX219_ANALOGUE_GAIN_MIN,
			  IMX219_ANALOGUE_GAIN_MAX,
			  1, IMX219_ANALOGUE_GAIN_DEFAULT);
	v4l2_ctrl_new_std(&priv->ctrl_handler, &imx219_ctrl_ops,
			  V4L2_CID_GAIN,
			  IMX219_DIGITAL_GAIN_MIN,
			  IMX219_DIGITAL_GAIN_MAX, 1,
			  IMX219_DIGITAL_GAIN_DEFAULT);
	v4l2_ctrl_new_std(&priv->ctrl_handler, &imx219_ctrl_ops,
			  V4L2_CID_EXPOSURE,
			  IMX219_DIGITAL_EXPOSURE_MIN,
			  IMX219_DIGITAL_EXPOSURE_MAX, 1,
			  IMX219_DIGITAL_EXPOSURE_DEFAULT);

	/* blank */
	h_blank = mode->hts_def - mode->width;
	priv->hblank = v4l2_ctrl_new_std(&priv->ctrl_handler, NULL, V4L2_CID_HBLANK,
			  h_blank, h_blank, 1, h_blank);
	v_blank = mode->vts_def - mode->height;
	priv->vblank = v4l2_ctrl_new_std(&priv->ctrl_handler, NULL, V4L2_CID_VBLANK,
			  v_blank, v_blank, 1, v_blank);

	/* freq */
	v4l2_ctrl_new_int_menu(&priv->ctrl_handler, NULL, V4L2_CID_LINK_FREQ,
			       0, 0, link_freq_menu_items);
	pixel_rate = mode->vts_def * mode->hts_def * mode->max_fps;
	priv->pixel_rate = v4l2_ctrl_new_std(&priv->ctrl_handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, pixel_rate, 1, pixel_rate);

	v4l2_ctrl_new_std_menu_items(&priv->ctrl_handler, &imx219_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(tp_qmenu) - 1, 0, 0, tp_qmenu);

	priv->subdev.ctrl_handler = &priv->ctrl_handler;
	if (priv->ctrl_handler.error) {
		dev_err(&client->dev, "Error %d adding controls\n",
			priv->ctrl_handler.error);
		ret = priv->ctrl_handler.error;
		goto error;
	}

	ret = v4l2_ctrl_handler_setup(&priv->ctrl_handler);
	if (ret < 0) {
		dev_err(&client->dev, "Error %d setting default controls\n",
			ret);
		goto error;
	}

	return 0;
error:
	v4l2_ctrl_handler_free(&priv->ctrl_handler);
	return ret;
}

static int imx219_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct imx219 *priv;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_SMBUS_BYTE\n");
		return -EIO;
	}
	priv = devm_kzalloc(&client->dev, sizeof(struct imx219), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get(&client->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_info(&client->dev, "Error %ld getting clock\n",
			 PTR_ERR(priv->clk));
		return -EPROBE_DEFER;
	}

	/* 1920 * 1080 by default */
	priv->cur_mode = &supported_modes[0];

	priv->crop_rect.left = 680;
	priv->crop_rect.top = 692;
	priv->crop_rect.width = priv->cur_mode->width;
	priv->crop_rect.height = priv->cur_mode->height;

	v4l2_i2c_subdev_init(&priv->subdev, client, &imx219_subdev_ops);
	ret = imx219_ctrls_init(&priv->subdev);
	if (ret < 0)
		return ret;
	ret = imx219_video_probe(client);
	if (ret < 0)
		return ret;

	priv->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	priv->pad.flags = MEDIA_PAD_FL_SOURCE;
	priv->subdev.entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ret = media_entity_init(&priv->subdev.entity, 1, &priv->pad, 0);
	if (ret < 0)
		return ret;

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0)
		return ret;

	return ret;
}

static int imx219_remove(struct i2c_client *client)
{
	struct imx219 *priv = to_imx219(client);

	v4l2_async_unregister_subdev(&priv->subdev);
	media_entity_cleanup(&priv->subdev.entity);
	v4l2_ctrl_handler_free(&priv->ctrl_handler);

	return 0;
}

static const struct i2c_device_id imx219_id[] = {
	{"imx219", 0},
	{}
};

static const struct of_device_id imx219_of_match[] = {
	{ .compatible = "sony,imx219" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, imx219_of_match);

MODULE_DEVICE_TABLE(i2c, imx219_id);
static struct i2c_driver imx219_i2c_driver = {
	.driver = {
		.of_match_table = of_match_ptr(imx219_of_match),
		.name = "imx219",
	},
	.probe = imx219_probe,
	.remove = imx219_remove,
	.id_table = imx219_id,
};

module_i2c_driver(imx219_i2c_driver);
MODULE_DESCRIPTION("Sony IMX219 Camera driver");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_LICENSE("GPL v2");

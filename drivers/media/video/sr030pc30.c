/*
 * Driver for SiliconFile SR030PC30 VGA (1/10-Inch) Image Sensor with ISP
 *
 * Copyright (C) 2010 Samsung Electronics Co., Ltd
 * Author: Sylwester Nawrocki, s.nawrocki@samsung.com
 *
 * Based on original driver authored by Dongsoo Nathaniel Kim
 * and HeungJun Kim <riverful.kim@samsung.com>.
 *
 * Based on mt9v011 Micron Digital Image Sensor driver
 * Copyright (c) 2009 Mauro Carvalho Chehab (mchehab@redhat.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-mediabus.h>
#include <media/sr030pc30.h>

static int debug;
module_param(debug, int, 0644);

#define MODULE_NAME	"SR030PC30"

/*
 * Register offsets within a page
 * b15..b8 - page id, b7..b0 - register address
 */
#define POWER_CTRL_REG		0x0001
#define PAGEMODE_REG		0x03
#define DEVICE_ID_REG		0x0004
#define NOON010PC30_ID		0x86
#define SR030PC30_ID		0x8C
#define VDO_CTL1_REG		0x0010
#define SUBSAMPL_NONE_VGA	0
#define SUBSAMPL_QVGA		0x10
#define SUBSAMPL_QQVGA		0x20
#define VDO_CTL2_REG		0x0011
#define SYNC_CTL_REG		0x0012
#define WIN_ROWH_REG		0x0020
#define WIN_ROWL_REG		0x0021
#define WIN_COLH_REG		0x0022
#define WIN_COLL_REG		0x0023
#define WIN_HEIGHTH_REG		0x0024
#define WIN_HEIGHTL_REG		0x0025
#define WIN_WIDTHH_REG		0x0026
#define WIN_WIDTHL_REG		0x0027
#define HBLANKH_REG		0x0040
#define HBLANKL_REG		0x0041
#define VSYNCH_REG		0x0042
#define VSYNCL_REG		0x0043
/* page 10 */
#define ISP_CTL_REG(n)		(0x1010 + (n))
#define YOFS_REG		0x1040
#define DARK_YOFS_REG		0x1041
#define AG_ABRTH_REG		0x1050
#define SAT_CTL_REG		0x1060
#define BSAT_REG		0x1061
#define RSAT_REG		0x1062
#define AG_SAT_TH_REG		0x1063
/* page 11 */
#define ZLPF_CTRL_REG		0x1110
#define ZLPF_CTRL2_REG		0x1112
#define ZLPF_AGH_THR_REG	0x1121
#define ZLPF_THR_REG		0x1160
#define ZLPF_DYN_THR_REG	0x1160
/* page 12 */
#define YCLPF_CTL1_REG		0x1240
#define YCLPF_CTL2_REG		0x1241
#define YCLPF_THR_REG		0x1250
#define BLPF_CTL_REG		0x1270
#define BLPF_THR1_REG		0x1274
#define BLPF_THR2_REG		0x1275
/* page 14 - Lens Shading Compensation */
#define LENS_CTRL_REG		0x1410
#define LENS_XCEN_REG		0x1420
#define LENS_YCEN_REG		0x1421
#define LENS_R_COMP_REG		0x1422
#define LENS_G_COMP_REG		0x1423
#define LENS_B_COMP_REG		0x1424
/* page 15 - Color correction */
#define CMC_CTL_REG		0x1510
#define CMC_OFSGH_REG		0x1514
#define CMC_OFSGL_REG		0x1516
#define CMC_SIGN_REG		0x1517
/* Color correction coefficients */
#define CMC_COEF_REG(n)		(0x1530 + (n))
/* Color correction offset coefficients */
#define CMC_OFS_REG(n)		(0x1540 + (n))
/* page 16 - Gamma correction */
#define GMA_CTL_REG		0x1610
/* Gamma correction coefficients 0.14 */
#define GMA_COEF_REG(n)		(0x1630 + (n))
/* page 20 - Auto Exposure */
#define AE_CTL1_REG		0x2010
#define AE_CTL2_REG		0x2011
#define AE_FRM_CTL_REG		0x2020
#define AE_FINE_CTL_REG(n)	(0x2028 + (n))
#define EXP_TIMEH_REG		0x2083
#define EXP_TIMEM_REG		0x2084
#define EXP_TIMEL_REG		0x2085
#define EXP_MMINH_REG		0x2086
#define EXP_MMINL_REG		0x2087
#define EXP_MMAXH_REG		0x2088
#define EXP_MMAXM_REG		0x2089
#define EXP_MMAXL_REG		0x208A
/* page 22 - Auto White Balance */
#define AWB_CTL1_REG		0x2210
#define AWB_ENABLE		0x80
#define AWB_CTL2_REG		0x2211
#define MWB_ENABLE		0x01
/* RGB gain control (manual WB) when AWB_CTL1[7]=0 */
#define AWB_RGAIN_REG		0x2280
#define AWB_GGAIN_REG		0x2281
#define AWB_BGAIN_REG		0x2282
#define AWB_RMAX_REG		0x2283
#define AWB_RMIN_REG		0x2284
#define AWB_BMAX_REG		0x2285
#define AWB_BMIN_REG		0x2286
/* R, B gain range in bright light conditions */
#define AWB_RMAXB_REG		0x2287
#define AWB_RMINB_REG		0x2288
#define AWB_BMAXB_REG		0x2289
#define AWB_BMINB_REG		0x228A
/* manual white balance, when AWB_CTL2[0]=1 */
#define MWB_RGAIN_REG		0x22B2
#define MWB_BGAIN_REG		0x22B3
/* the token to mark an array end */
#define REG_TERM		0xFFFF

/* Minimum and maximum exposure time in ms */
#define EXPOS_MIN_MS		1
#define EXPOS_MAX_MS		125

struct sr030pc30_info {
	struct v4l2_subdev sd;
	const struct sr030pc30_platform_data *pdata;
	const struct sr030pc30_format *curr_fmt;
	const struct sr030pc30_frmsize *curr_win;
	unsigned int auto_wb:1;
	unsigned int auto_exp:1;
	unsigned int hflip:1;
	unsigned int vflip:1;
	unsigned int sleep:1;
	unsigned int exposure;
	u8 blue_balance;
	u8 red_balance;
	u8 i2c_reg_page;
};

struct sr030pc30_format {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
	u16 ispctl1_reg;
};

struct sr030pc30_frmsize {
	u16 width;
	u16 height;
	int vid_ctl1;
};

struct i2c_regval {
	u16 addr;
	u16 val;
};

static const struct v4l2_queryctrl sr030pc30_ctrl[] = {
	{
		.id		= V4L2_CID_AUTO_WHITE_BALANCE,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Auto White Balance",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 1,
	}, {
		.id		= V4L2_CID_RED_BALANCE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Red Balance",
		.minimum	= 0,
		.maximum	= 127,
		.step		= 1,
		.default_value	= 64,
		.flags		= 0,
	}, {
		.id		= V4L2_CID_BLUE_BALANCE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Blue Balance",
		.minimum	= 0,
		.maximum	= 127,
		.step		= 1,
		.default_value	= 64,
	}, {
		.id		= V4L2_CID_EXPOSURE_AUTO,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Auto Exposure",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 1,
	}, {
		.id		= V4L2_CID_EXPOSURE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Exposure",
		.minimum	= EXPOS_MIN_MS,
		.maximum	= EXPOS_MAX_MS,
		.step		= 1,
		.default_value	= 1,
	}, {
	}
};

/* supported resolutions */
static const struct sr030pc30_frmsize sr030pc30_sizes[] = {
	{
		.width		= 640,
		.height		= 480,
		.vid_ctl1	= SUBSAMPL_NONE_VGA,
	}, {
		.width		= 320,
		.height		= 240,
		.vid_ctl1	= SUBSAMPL_QVGA,
	}, {
		.width		= 160,
		.height		= 120,
		.vid_ctl1	= SUBSAMPL_QQVGA,
	},
};

/* supported pixel formats */
static const struct sr030pc30_format sr030pc30_formats[] = {
	{
		.code		= V4L2_MBUS_FMT_YUYV8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.ispctl1_reg	= 0x03,
	}, {
		.code		= V4L2_MBUS_FMT_YVYU8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.ispctl1_reg	= 0x02,
	}, {
		.code		= V4L2_MBUS_FMT_VYUY8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.ispctl1_reg	= 0,
	}, {
		.code		= V4L2_MBUS_FMT_UYVY8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.ispctl1_reg	= 0x01,
	}, {
		.code		= V4L2_MBUS_FMT_RGB565_2X8_BE,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.ispctl1_reg	= 0x40,
	},
};

static const struct i2c_regval sr030pc30_base_regs[] = {
	/* Window size and position within pixel matrix */
	{ WIN_ROWH_REG,		0x00 }, { WIN_ROWL_REG,		0x06 },
	{ WIN_COLH_REG,		0x00 },	{ WIN_COLL_REG,		0x06 },
	{ WIN_HEIGHTH_REG,	0x01 }, { WIN_HEIGHTL_REG,	0xE0 },
	{ WIN_WIDTHH_REG,	0x02 }, { WIN_WIDTHL_REG,	0x80 },
	{ HBLANKH_REG,		0x01 }, { HBLANKL_REG,		0x50 },
	{ VSYNCH_REG,		0x00 }, { VSYNCL_REG,		0x14 },
	{ SYNC_CTL_REG,		0 },
	/* Color corection and saturation */
	{ ISP_CTL_REG(0),	0x30 }, { YOFS_REG,		0x80 },
	{ DARK_YOFS_REG,	0x04 }, { AG_ABRTH_REG,		0x78 },
	{ SAT_CTL_REG,		0x1F }, { BSAT_REG,		0x90 },
	{ AG_SAT_TH_REG,	0xF0 }, { 0x1064,		0x80 },
	{ CMC_CTL_REG,		0x03 }, { CMC_OFSGH_REG,	0x3C },
	{ CMC_OFSGL_REG,	0x2C }, { CMC_SIGN_REG,		0x2F },
	{ CMC_COEF_REG(0),	0xCB }, { CMC_OFS_REG(0),	0x87 },
	{ CMC_COEF_REG(1),	0x61 }, { CMC_OFS_REG(1),	0x18 },
	{ CMC_COEF_REG(2),	0x16 }, { CMC_OFS_REG(2),	0x91 },
	{ CMC_COEF_REG(3),	0x23 }, { CMC_OFS_REG(3),	0x94 },
	{ CMC_COEF_REG(4),	0xCE }, { CMC_OFS_REG(4),	0x9f },
	{ CMC_COEF_REG(5),	0x2B }, { CMC_OFS_REG(5),	0x33 },
	{ CMC_COEF_REG(6),	0x01 }, { CMC_OFS_REG(6),	0x00 },
	{ CMC_COEF_REG(7),	0x34 }, { CMC_OFS_REG(7),	0x94 },
	{ CMC_COEF_REG(8),	0x75 }, { CMC_OFS_REG(8),	0x14 },
	/* Color corection coefficients */
	{ GMA_CTL_REG,		0x03 },	{ GMA_COEF_REG(0),	0x00 },
	{ GMA_COEF_REG(1),	0x19 },	{ GMA_COEF_REG(2),	0x26 },
	{ GMA_COEF_REG(3),	0x3B },	{ GMA_COEF_REG(4),	0x5D },
	{ GMA_COEF_REG(5),	0x79 }, { GMA_COEF_REG(6),	0x8E },
	{ GMA_COEF_REG(7),	0x9F },	{ GMA_COEF_REG(8),	0xAF },
	{ GMA_COEF_REG(9),	0xBD },	{ GMA_COEF_REG(10),	0xCA },
	{ GMA_COEF_REG(11),	0xDD }, { GMA_COEF_REG(12),	0xEC },
	{ GMA_COEF_REG(13),	0xF7 },	{ GMA_COEF_REG(14),	0xFF },
	/* Noise reduction, Z-LPF, YC-LPF and BLPF filters setup */
	{ ZLPF_CTRL_REG,	0x99 }, { ZLPF_CTRL2_REG,	0x0E },
	{ ZLPF_AGH_THR_REG,	0x29 }, { ZLPF_THR_REG,		0x0F },
	{ ZLPF_DYN_THR_REG,	0x63 }, { YCLPF_CTL1_REG,	0x23 },
	{ YCLPF_CTL2_REG,	0x3B }, { YCLPF_THR_REG,	0x05 },
	{ BLPF_CTL_REG,		0x1D }, { BLPF_THR1_REG,	0x05 },
	{ BLPF_THR2_REG,	0x04 },
	/* Automatic white balance */
	{ AWB_CTL1_REG,		0xFB }, { AWB_CTL2_REG,		0x26 },
	{ AWB_RMAX_REG,		0x54 }, { AWB_RMIN_REG,		0x2B },
	{ AWB_BMAX_REG,		0x57 }, { AWB_BMIN_REG,		0x29 },
	{ AWB_RMAXB_REG,	0x50 }, { AWB_RMINB_REG,	0x43 },
	{ AWB_BMAXB_REG,	0x30 }, { AWB_BMINB_REG,	0x22 },
	/* Auto exposure */
	{ AE_CTL1_REG,		0x8C }, { AE_CTL2_REG,		0x04 },
	{ AE_FRM_CTL_REG,	0x01 }, { AE_FINE_CTL_REG(0),	0x3F },
	{ AE_FINE_CTL_REG(1),	0xA3 }, { AE_FINE_CTL_REG(3),	0x34 },
	/* Lens shading compensation */
	{ LENS_CTRL_REG,	0x01 }, { LENS_XCEN_REG,	0x80 },
	{ LENS_YCEN_REG,	0x70 }, { LENS_R_COMP_REG,	0x53 },
	{ LENS_G_COMP_REG,	0x40 }, { LENS_B_COMP_REG,	0x3e },
	{ REG_TERM,		0 },
};

static inline struct sr030pc30_info *to_sr030pc30(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sr030pc30_info, sd);
}

static inline int set_i2c_page(struct sr030pc30_info *info,
			       struct i2c_client *client, unsigned int reg)
{
	int ret = 0;
	u32 page = reg >> 8 & 0xFF;

	if (info->i2c_reg_page != page && (reg & 0xFF) != 0x03) {
		ret = i2c_smbus_write_byte_data(client, PAGEMODE_REG, page);
		if (!ret)
			info->i2c_reg_page = page;
	}
	return ret;
}

static int cam_i2c_read(struct v4l2_subdev *sd, u32 reg_addr)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sr030pc30_info *info = to_sr030pc30(sd);

	int ret = set_i2c_page(info, client, reg_addr);
	if (!ret)
		ret = i2c_smbus_read_byte_data(client, reg_addr & 0xFF);
	return ret;
}

static int cam_i2c_write(struct v4l2_subdev *sd, u32 reg_addr, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sr030pc30_info *info = to_sr030pc30(sd);

	int ret = set_i2c_page(info, client, reg_addr);
	if (!ret)
		ret = i2c_smbus_write_byte_data(
			client, reg_addr & 0xFF, val);
	return ret;
}

static inline int sr030pc30_bulk_write_reg(struct v4l2_subdev *sd,
				const struct i2c_regval *msg)
{
	while (msg->addr != REG_TERM) {
		int ret = cam_i2c_write(sd, msg->addr, msg->val);
		if (ret)
			return ret;
		msg++;
	}
	return 0;
}

/* Device reset and sleep mode control */
static int sr030pc30_pwr_ctrl(struct v4l2_subdev *sd,
				     bool reset, bool sleep)
{
	struct sr030pc30_info *info = to_sr030pc30(sd);
	u8 reg = sleep ? 0xF1 : 0xF0;
	int ret = 0;

	if (reset)
		ret = cam_i2c_write(sd, POWER_CTRL_REG, reg | 0x02);
	if (!ret) {
		ret = cam_i2c_write(sd, POWER_CTRL_REG, reg);
		if (!ret) {
			info->sleep = sleep;
			if (reset)
				info->i2c_reg_page = -1;
		}
	}
	return ret;
}

static inline int sr030pc30_enable_autoexposure(struct v4l2_subdev *sd, int on)
{
	struct sr030pc30_info *info = to_sr030pc30(sd);
	/* auto anti-flicker is also enabled here */
	int ret = cam_i2c_write(sd, AE_CTL1_REG, on ? 0xDC : 0x0C);
	if (!ret)
		info->auto_exp = on;
	return ret;
}

static int sr030pc30_set_exposure(struct v4l2_subdev *sd, int value)
{
	struct sr030pc30_info *info = to_sr030pc30(sd);

	unsigned long expos = value * info->pdata->clk_rate / (8 * 1000);

	int ret = cam_i2c_write(sd, EXP_TIMEH_REG, expos >> 16 & 0xFF);
	if (!ret)
		ret = cam_i2c_write(sd, EXP_TIMEM_REG, expos >> 8 & 0xFF);
	if (!ret)
		ret = cam_i2c_write(sd, EXP_TIMEL_REG, expos & 0xFF);
	if (!ret) { /* Turn off AE */
		info->exposure = value;
		ret = sr030pc30_enable_autoexposure(sd, 0);
	}
	return ret;
}

/* Automatic white balance control */
static int sr030pc30_enable_autowhitebalance(struct v4l2_subdev *sd, int on)
{
	struct sr030pc30_info *info = to_sr030pc30(sd);

	int ret = cam_i2c_write(sd, AWB_CTL2_REG, on ? 0x2E : 0x2F);
	if (!ret)
		ret = cam_i2c_write(sd, AWB_CTL1_REG, on ? 0xFB : 0x7B);
	if (!ret)
		info->auto_wb = on;

	return ret;
}

static int sr030pc30_set_flip(struct v4l2_subdev *sd)
{
	struct sr030pc30_info *info = to_sr030pc30(sd);

	s32 reg = cam_i2c_read(sd, VDO_CTL2_REG);
	if (reg < 0)
		return reg;

	reg &= 0x7C;
	if (info->hflip)
		reg |= 0x01;
	if (info->vflip)
		reg |= 0x02;
	return cam_i2c_write(sd, VDO_CTL2_REG, reg | 0x80);
}

/* Configure resolution, color format and image flip */
static int sr030pc30_set_params(struct v4l2_subdev *sd)
{
	struct sr030pc30_info *info = to_sr030pc30(sd);
	int ret;

	if (!info->curr_win)
		return -EINVAL;

	/* Configure the resolution through subsampling */
	ret = cam_i2c_write(sd, VDO_CTL1_REG,
			    info->curr_win->vid_ctl1);

	if (!ret && info->curr_fmt)
		ret = cam_i2c_write(sd, ISP_CTL_REG(0),
				info->curr_fmt->ispctl1_reg);
	if (!ret)
		ret = sr030pc30_set_flip(sd);

	return ret;
}

/* Find nearest matching image pixel size. */
static int sr030pc30_try_frame_size(struct v4l2_mbus_framefmt *mf)
{
	unsigned int min_err = ~0;
	int i = ARRAY_SIZE(sr030pc30_sizes);
	const struct sr030pc30_frmsize *fsize = &sr030pc30_sizes[0],
					*match = NULL;
	while (i--) {
		int err = abs(fsize->width - mf->width)
				+ abs(fsize->height - mf->height);
		if (err < min_err) {
			min_err = err;
			match = fsize;
		}
		fsize++;
	}
	if (match) {
		mf->width  = match->width;
		mf->height = match->height;
		return 0;
	}
	return -EINVAL;
}

static int sr030pc30_queryctrl(struct v4l2_subdev *sd,
			       struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sr030pc30_ctrl); i++)
		if (qc->id == sr030pc30_ctrl[i].id) {
			*qc = sr030pc30_ctrl[i];
			v4l2_dbg(1, debug, sd, "%s id: %d\n",
				 __func__, qc->id);
			return 0;
		}

	return -EINVAL;
}

static inline int sr030pc30_set_bluebalance(struct v4l2_subdev *sd, int value)
{
	int ret = cam_i2c_write(sd, MWB_BGAIN_REG, value);
	if (!ret)
		to_sr030pc30(sd)->blue_balance = value;
	return ret;
}

static inline int sr030pc30_set_redbalance(struct v4l2_subdev *sd, int value)
{
	int ret = cam_i2c_write(sd, MWB_RGAIN_REG, value);
	if (!ret)
		to_sr030pc30(sd)->red_balance = value;
	return ret;
}

static int sr030pc30_s_ctrl(struct v4l2_subdev *sd,
			    struct v4l2_control *ctrl)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(sr030pc30_ctrl); i++)
		if (ctrl->id == sr030pc30_ctrl[i].id)
			break;

	if (i == ARRAY_SIZE(sr030pc30_ctrl))
		return -EINVAL;

	if (ctrl->value < sr030pc30_ctrl[i].minimum ||
		ctrl->value > sr030pc30_ctrl[i].maximum)
			return -ERANGE;

	v4l2_dbg(1, debug, sd, "%s: ctrl_id: %d, value: %d\n",
			 __func__, ctrl->id, ctrl->value);

	switch (ctrl->id) {
	case V4L2_CID_AUTO_WHITE_BALANCE:
		sr030pc30_enable_autowhitebalance(sd, ctrl->value);
		break;
	case V4L2_CID_BLUE_BALANCE:
		ret = sr030pc30_set_bluebalance(sd, ctrl->value);
		break;
	case V4L2_CID_RED_BALANCE:
		ret = sr030pc30_set_redbalance(sd, ctrl->value);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		sr030pc30_enable_autoexposure(sd,
			ctrl->value == V4L2_EXPOSURE_AUTO);
		break;
	case V4L2_CID_EXPOSURE:
		ret = sr030pc30_set_exposure(sd, ctrl->value);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int sr030pc30_g_ctrl(struct v4l2_subdev *sd,
			    struct v4l2_control *ctrl)
{
	struct sr030pc30_info *info = to_sr030pc30(sd);

	v4l2_dbg(1, debug, sd, "%s: id: %d\n", __func__, ctrl->id);

	switch (ctrl->id) {
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ctrl->value = info->auto_wb;
		break;
	case V4L2_CID_BLUE_BALANCE:
		ctrl->value = info->blue_balance;
		break;
	case V4L2_CID_RED_BALANCE:
		ctrl->value = info->red_balance;
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		ctrl->value = info->auto_exp;
		break;
	case V4L2_CID_EXPOSURE:
		ctrl->value = info->exposure;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sr030pc30_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			      enum v4l2_mbus_pixelcode *code)
{
	if (!code || index >= ARRAY_SIZE(sr030pc30_formats))
		return -EINVAL;

	*code = sr030pc30_formats[index].code;
	return 0;
}

static int sr030pc30_g_fmt(struct v4l2_subdev *sd,
			   struct v4l2_mbus_framefmt *mf)
{
	struct sr030pc30_info *info = to_sr030pc30(sd);
	int ret;

	if (!mf)
		return -EINVAL;

	if (!info->curr_win || !info->curr_fmt) {
		ret = sr030pc30_set_params(sd);
		if (ret)
			return ret;
	}

	mf->width	= info->curr_win->width;
	mf->height	= info->curr_win->height;
	mf->code	= info->curr_fmt->code;
	mf->colorspace	= info->curr_fmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

/* Return nearest media bus frame format. */
static const struct sr030pc30_format *try_fmt(struct v4l2_subdev *sd,
					      struct v4l2_mbus_framefmt *mf)
{
	int i = ARRAY_SIZE(sr030pc30_formats);

	sr030pc30_try_frame_size(mf);

	while (i--)
		if (mf->code == sr030pc30_formats[i].code)
			break;

	mf->code = sr030pc30_formats[i].code;

	return &sr030pc30_formats[i];
}

/* Return nearest media bus frame format. */
static int sr030pc30_try_fmt(struct v4l2_subdev *sd,
			     struct v4l2_mbus_framefmt *mf)
{
	if (!sd || !mf)
		return -EINVAL;

	try_fmt(sd, mf);
	return 0;
}

static int sr030pc30_s_fmt(struct v4l2_subdev *sd,
			   struct v4l2_mbus_framefmt *mf)
{
	struct sr030pc30_info *info = to_sr030pc30(sd);

	if (!sd || !mf)
		return -EINVAL;

	info->curr_fmt = try_fmt(sd, mf);

	return sr030pc30_set_params(sd);
}

static int sr030pc30_base_config(struct v4l2_subdev *sd)
{
	struct sr030pc30_info *info = to_sr030pc30(sd);
	int ret;
	unsigned long expmin, expmax;

	ret = sr030pc30_bulk_write_reg(sd, sr030pc30_base_regs);
	if (!ret) {
		info->curr_fmt = &sr030pc30_formats[0];
		info->curr_win = &sr030pc30_sizes[0];
		ret = sr030pc30_set_params(sd);
	}
	if (!ret)
		ret = sr030pc30_pwr_ctrl(sd, false, false);

	if (!ret && !info->pdata)
		return ret;

	expmin = EXPOS_MIN_MS * info->pdata->clk_rate / (8 * 1000);
	expmax = EXPOS_MAX_MS * info->pdata->clk_rate / (8 * 1000);

	v4l2_dbg(1, debug, sd, "%s: expmin= %lx, expmax= %lx", __func__,
		 expmin, expmax);

	/* Setting up manual exposure time range */
	ret = cam_i2c_write(sd, EXP_MMINH_REG, expmin >> 8 & 0xFF);
	if (!ret)
		ret = cam_i2c_write(sd, EXP_MMINL_REG, expmin & 0xFF);
	if (!ret)
		ret = cam_i2c_write(sd, EXP_MMAXH_REG, expmax >> 16 & 0xFF);
	if (!ret)
		ret = cam_i2c_write(sd, EXP_MMAXM_REG, expmax >> 8 & 0xFF);
	if (!ret)
		ret = cam_i2c_write(sd, EXP_MMAXL_REG, expmax & 0xFF);

	return ret;
}

static int sr030pc30_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sr030pc30_info *info = to_sr030pc30(sd);
	const struct sr030pc30_platform_data *pdata = info->pdata;
	int ret;

	if (pdata == NULL) {
		WARN(1, "No platform data!\n");
		return -EINVAL;
	}

	/*
	 * Put sensor into power sleep mode before switching off
	 * power and disabling MCLK.
	 */
	if (!on)
		sr030pc30_pwr_ctrl(sd, false, true);

	/* set_power controls sensor's power and clock */
	if (pdata->set_power) {
		ret = pdata->set_power(&client->dev, on);
		if (ret)
			return ret;
	}

	if (on) {
		ret = sr030pc30_base_config(sd);
	} else {
		ret = 0;
		info->curr_win = NULL;
		info->curr_fmt = NULL;
	}

	return ret;
}

static const struct v4l2_subdev_core_ops sr030pc30_core_ops = {
	.s_power	= sr030pc30_s_power,
	.queryctrl	= sr030pc30_queryctrl,
	.s_ctrl		= sr030pc30_s_ctrl,
	.g_ctrl		= sr030pc30_g_ctrl,
};

static const struct v4l2_subdev_video_ops sr030pc30_video_ops = {
	.g_mbus_fmt	= sr030pc30_g_fmt,
	.s_mbus_fmt	= sr030pc30_s_fmt,
	.try_mbus_fmt	= sr030pc30_try_fmt,
	.enum_mbus_fmt	= sr030pc30_enum_fmt,
};

static const struct v4l2_subdev_ops sr030pc30_ops = {
	.core	= &sr030pc30_core_ops,
	.video	= &sr030pc30_video_ops,
};

/*
 * Detect sensor type. Return 0 if SR030PC30 was detected
 * or -ENODEV otherwise.
 */
static int sr030pc30_detect(struct i2c_client *client)
{
	const struct sr030pc30_platform_data *pdata
		= client->dev.platform_data;
	int ret;

	/* Enable sensor's power and clock */
	if (pdata->set_power) {
		ret = pdata->set_power(&client->dev, 1);
		if (ret)
			return ret;
	}

	ret = i2c_smbus_read_byte_data(client, DEVICE_ID_REG);

	if (pdata->set_power)
		pdata->set_power(&client->dev, 0);

	if (ret < 0) {
		dev_err(&client->dev, "%s: I2C read failed\n", __func__);
		return ret;
	}

	return ret == SR030PC30_ID ? 0 : -ENODEV;
}


static int sr030pc30_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct sr030pc30_info *info;
	struct v4l2_subdev *sd;
	const struct sr030pc30_platform_data *pdata
		= client->dev.platform_data;
	int ret;

	if (!pdata) {
		dev_err(&client->dev, "No platform data!");
		return -EIO;
	}

	ret = sr030pc30_detect(client);
	if (ret)
		return ret;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	sd = &info->sd;
	strcpy(sd->name, MODULE_NAME);
	info->pdata = client->dev.platform_data;

	v4l2_i2c_subdev_init(sd, client, &sr030pc30_ops);

	info->i2c_reg_page	= -1;
	info->hflip		= 1;
	info->auto_exp		= 1;
	info->exposure		= 30;

	return 0;
}

static int sr030pc30_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sr030pc30_info *info = to_sr030pc30(sd);

	v4l2_device_unregister_subdev(sd);
	kfree(info);
	return 0;
}

static const struct i2c_device_id sr030pc30_id[] = {
	{ MODULE_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, sr030pc30_id);


static struct i2c_driver sr030pc30_i2c_driver = {
	.driver = {
		.name = MODULE_NAME
	},
	.probe		= sr030pc30_probe,
	.remove		= sr030pc30_remove,
	.id_table	= sr030pc30_id,
};

module_i2c_driver(sr030pc30_i2c_driver);

MODULE_DESCRIPTION("Siliconfile SR030PC30 camera driver");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_LICENSE("GPL");

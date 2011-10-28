/*
 * Driver for SiliconFile NOON010PC30 CIF (1/11") Image Sensor with ISP
 *
 * Copyright (C) 2010 Samsung Electronics
 * Contact: Sylwester Nawrocki, <s.nawrocki@samsung.com>
 *
 * Initial register configuration based on a driver authored by
 * HeungJun Kim <riverful.kim@samsung.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later vergsion.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <media/noon010pc30.h>
#include <media/v4l2-chip-ident.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable module debug trace. Set to 1 to enable.");

#define MODULE_NAME		"NOON010PC30"

/*
 * Register offsets within a page
 * b15..b8 - page id, b7..b0 - register address
 */
#define POWER_CTRL_REG		0x0001
#define PAGEMODE_REG		0x03
#define DEVICE_ID_REG		0x0004
#define NOON010PC30_ID		0x86
#define VDO_CTL_REG(n)		(0x0010 + (n))
#define SYNC_CTL_REG		0x0012
/* Window size and position */
#define WIN_ROWH_REG		0x0013
#define WIN_ROWL_REG		0x0014
#define WIN_COLH_REG		0x0015
#define WIN_COLL_REG		0x0016
#define WIN_HEIGHTH_REG		0x0017
#define WIN_HEIGHTL_REG		0x0018
#define WIN_WIDTHH_REG		0x0019
#define WIN_WIDTHL_REG		0x001A
#define HBLANKH_REG		0x001B
#define HBLANKL_REG		0x001C
#define VSYNCH_REG		0x001D
#define VSYNCL_REG		0x001E
/* VSYNC control */
#define VS_CTL_REG(n)		(0x00A1 + (n))
/* page 1 */
#define ISP_CTL_REG(n)		(0x0110 + (n))
#define YOFS_REG		0x0119
#define DARK_YOFS_REG		0x011A
#define SAT_CTL_REG		0x0120
#define BSAT_REG		0x0121
#define RSAT_REG		0x0122
/* Color correction */
#define CMC_CTL_REG		0x0130
#define CMC_OFSGH_REG		0x0133
#define CMC_OFSGL_REG		0x0135
#define CMC_SIGN_REG		0x0136
#define CMC_GOFS_REG		0x0137
#define CMC_COEF_REG(n)		(0x0138 + (n))
#define CMC_OFS_REG(n)		(0x0141 + (n))
/* Gamma correction */
#define GMA_CTL_REG		0x0160
#define GMA_COEF_REG(n)		(0x0161 + (n))
/* Lens Shading */
#define LENS_CTRL_REG		0x01D0
#define LENS_XCEN_REG		0x01D1
#define LENS_YCEN_REG		0x01D2
#define LENS_RC_REG		0x01D3
#define LENS_GC_REG		0x01D4
#define LENS_BC_REG		0x01D5
#define L_AGON_REG		0x01D6
#define L_AGOFF_REG		0x01D7
/* Page 3 - Auto Exposure */
#define AE_CTL_REG(n)		(0x0310 + (n))
#define AE_CTL9_REG		0x032C
#define AE_CTL10_REG		0x032D
#define AE_YLVL_REG		0x031C
#define AE_YTH_REG(n)		(0x031D + (n))
#define AE_WGT_REG		0x0326
#define EXP_TIMEH_REG		0x0333
#define EXP_TIMEM_REG		0x0334
#define EXP_TIMEL_REG		0x0335
#define EXP_MMINH_REG		0x0336
#define EXP_MMINL_REG		0x0337
#define EXP_MMAXH_REG		0x0338
#define EXP_MMAXM_REG		0x0339
#define EXP_MMAXL_REG		0x033A
/* Page 4 - Auto White Balance */
#define AWB_CTL_REG(n)		(0x0410 + (n))
#define AWB_ENABE		0x80
#define AWB_WGHT_REG		0x0419
#define BGAIN_PAR_REG(n)	(0x044F + (n))
/* Manual white balance, when AWB_CTL2[0]=1 */
#define MWB_RGAIN_REG		0x0466
#define MWB_BGAIN_REG		0x0467

/* The token to mark an array end */
#define REG_TERM		0xFFFF

struct noon010_format {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
	u16 ispctl1_reg;
};

struct noon010_frmsize {
	u16 width;
	u16 height;
	int vid_ctl1;
};

static const char * const noon010_supply_name[] = {
	"vdd_core", "vddio", "vdda"
};

#define NOON010_NUM_SUPPLIES ARRAY_SIZE(noon010_supply_name)

struct noon010_info {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	const struct noon010pc30_platform_data *pdata;
	const struct noon010_format *curr_fmt;
	const struct noon010_frmsize *curr_win;
	unsigned int hflip:1;
	unsigned int vflip:1;
	unsigned int power:1;
	u8 i2c_reg_page;
	struct regulator_bulk_data supply[NOON010_NUM_SUPPLIES];
	u32 gpio_nreset;
	u32 gpio_nstby;
};

struct i2c_regval {
	u16 addr;
	u16 val;
};

/* Supported resolutions. */
static const struct noon010_frmsize noon010_sizes[] = {
	{
		.width		= 352,
		.height		= 288,
		.vid_ctl1	= 0,
	}, {
		.width		= 176,
		.height		= 144,
		.vid_ctl1	= 0x10,
	}, {
		.width		= 88,
		.height		= 72,
		.vid_ctl1	= 0x20,
	},
};

/* Supported pixel formats. */
static const struct noon010_format noon010_formats[] = {
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

static const struct i2c_regval noon010_base_regs[] = {
	{ WIN_COLL_REG,		0x06 },	{ HBLANKL_REG,		0x7C },
	/* Color corection and saturation */
	{ ISP_CTL_REG(0),	0x30 }, { ISP_CTL_REG(2),	0x30 },
	{ YOFS_REG,		0x80 }, { DARK_YOFS_REG,	0x04 },
	{ SAT_CTL_REG,		0x1F }, { BSAT_REG,		0x90 },
	{ CMC_CTL_REG,		0x0F }, { CMC_OFSGH_REG,	0x3C },
	{ CMC_OFSGL_REG,	0x2C }, { CMC_SIGN_REG,		0x3F },
	{ CMC_COEF_REG(0),	0x79 }, { CMC_OFS_REG(0),	0x00 },
	{ CMC_COEF_REG(1),	0x39 }, { CMC_OFS_REG(1),	0x00 },
	{ CMC_COEF_REG(2),	0x00 }, { CMC_OFS_REG(2),	0x00 },
	{ CMC_COEF_REG(3),	0x11 }, { CMC_OFS_REG(3),	0x8B },
	{ CMC_COEF_REG(4),	0x65 }, { CMC_OFS_REG(4),	0x07 },
	{ CMC_COEF_REG(5),	0x14 }, { CMC_OFS_REG(5),	0x04 },
	{ CMC_COEF_REG(6),	0x01 }, { CMC_OFS_REG(6),	0x9C },
	{ CMC_COEF_REG(7),	0x33 }, { CMC_OFS_REG(7),	0x89 },
	{ CMC_COEF_REG(8),	0x74 }, { CMC_OFS_REG(8),	0x25 },
	/* Automatic white balance */
	{ AWB_CTL_REG(0),	0x78 }, { AWB_CTL_REG(1),	0x2E },
	{ AWB_CTL_REG(2),	0x20 }, { AWB_CTL_REG(3),	0x85 },
	/* Auto exposure */
	{ AE_CTL_REG(0),	0xDC }, { AE_CTL_REG(1),	0x81 },
	{ AE_CTL_REG(2),	0x30 }, { AE_CTL_REG(3),	0xA5 },
	{ AE_CTL_REG(4),	0x40 }, { AE_CTL_REG(5),	0x51 },
	{ AE_CTL_REG(6),	0x33 }, { AE_CTL_REG(7),	0x7E },
	{ AE_CTL9_REG,		0x00 }, { AE_CTL10_REG,		0x02 },
	{ AE_YLVL_REG,		0x44 },	{ AE_YTH_REG(0),	0x34 },
	{ AE_YTH_REG(1),	0x30 },	{ AE_WGT_REG,		0xD5 },
	/* Lens shading compensation */
	{ LENS_CTRL_REG,	0x01 }, { LENS_XCEN_REG,	0x80 },
	{ LENS_YCEN_REG,	0x70 }, { LENS_RC_REG,		0x53 },
	{ LENS_GC_REG,		0x40 }, { LENS_BC_REG,		0x3E },
	{ REG_TERM,		0 },
};

static inline struct noon010_info *to_noon010(struct v4l2_subdev *sd)
{
	return container_of(sd, struct noon010_info, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct noon010_info, hdl)->sd;
}

static inline int set_i2c_page(struct noon010_info *info,
			       struct i2c_client *client, unsigned int reg)
{
	u32 page = reg >> 8 & 0xFF;
	int ret = 0;

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
	struct noon010_info *info = to_noon010(sd);
	int ret = set_i2c_page(info, client, reg_addr);

	if (ret)
		return ret;
	return i2c_smbus_read_byte_data(client, reg_addr & 0xFF);
}

static int cam_i2c_write(struct v4l2_subdev *sd, u32 reg_addr, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct noon010_info *info = to_noon010(sd);
	int ret = set_i2c_page(info, client, reg_addr);

	if (ret)
		return ret;
	return i2c_smbus_write_byte_data(client, reg_addr & 0xFF, val);
}

static inline int noon010_bulk_write_reg(struct v4l2_subdev *sd,
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
static int noon010_power_ctrl(struct v4l2_subdev *sd, bool reset, bool sleep)
{
	struct noon010_info *info = to_noon010(sd);
	u8 reg = sleep ? 0xF1 : 0xF0;
	int ret = 0;

	if (reset)
		ret = cam_i2c_write(sd, POWER_CTRL_REG, reg | 0x02);
	if (!ret) {
		ret = cam_i2c_write(sd, POWER_CTRL_REG, reg);
		if (reset && !ret)
			info->i2c_reg_page = -1;
	}
	return ret;
}

/* Automatic white balance control */
static int noon010_enable_autowhitebalance(struct v4l2_subdev *sd, int on)
{
	int ret;

	ret = cam_i2c_write(sd, AWB_CTL_REG(1), on ? 0x2E : 0x2F);
	if (!ret)
		ret = cam_i2c_write(sd, AWB_CTL_REG(0), on ? 0xFB : 0x7B);
	return ret;
}

static int noon010_set_flip(struct v4l2_subdev *sd, int hflip, int vflip)
{
	struct noon010_info *info = to_noon010(sd);
	int reg, ret;

	reg = cam_i2c_read(sd, VDO_CTL_REG(1));
	if (reg < 0)
		return reg;

	reg &= 0x7C;
	if (hflip)
		reg |= 0x01;
	if (vflip)
		reg |= 0x02;

	ret = cam_i2c_write(sd, VDO_CTL_REG(1), reg | 0x80);
	if (!ret) {
		info->hflip = hflip;
		info->vflip = vflip;
	}
	return ret;
}

/* Configure resolution and color format */
static int noon010_set_params(struct v4l2_subdev *sd)
{
	struct noon010_info *info = to_noon010(sd);
	int ret;

	if (!info->curr_win)
		return -EINVAL;

	ret = cam_i2c_write(sd, VDO_CTL_REG(0), info->curr_win->vid_ctl1);

	if (!ret && info->curr_fmt)
		ret = cam_i2c_write(sd, ISP_CTL_REG(0),
				info->curr_fmt->ispctl1_reg);
	return ret;
}

/* Find nearest matching image pixel size. */
static int noon010_try_frame_size(struct v4l2_mbus_framefmt *mf)
{
	unsigned int min_err = ~0;
	int i = ARRAY_SIZE(noon010_sizes);
	const struct noon010_frmsize *fsize = &noon010_sizes[0],
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

static int power_enable(struct noon010_info *info)
{
	int ret;

	if (info->power) {
		v4l2_info(&info->sd, "%s: sensor is already on\n", __func__);
		return 0;
	}

	if (gpio_is_valid(info->gpio_nstby))
		gpio_set_value(info->gpio_nstby, 0);

	if (gpio_is_valid(info->gpio_nreset))
		gpio_set_value(info->gpio_nreset, 0);

	ret = regulator_bulk_enable(NOON010_NUM_SUPPLIES, info->supply);
	if (ret)
		return ret;

	if (gpio_is_valid(info->gpio_nreset)) {
		msleep(50);
		gpio_set_value(info->gpio_nreset, 1);
	}
	if (gpio_is_valid(info->gpio_nstby)) {
		udelay(1000);
		gpio_set_value(info->gpio_nstby, 1);
	}
	if (gpio_is_valid(info->gpio_nreset)) {
		udelay(1000);
		gpio_set_value(info->gpio_nreset, 0);
		msleep(100);
		gpio_set_value(info->gpio_nreset, 1);
		msleep(20);
	}
	info->power = 1;

	v4l2_dbg(1, debug, &info->sd,  "%s: sensor is on\n", __func__);
	return 0;
}

static int power_disable(struct noon010_info *info)
{
	int ret;

	if (!info->power) {
		v4l2_info(&info->sd, "%s: sensor is already off\n", __func__);
		return 0;
	}

	ret = regulator_bulk_disable(NOON010_NUM_SUPPLIES, info->supply);
	if (ret)
		return ret;

	if (gpio_is_valid(info->gpio_nstby))
		gpio_set_value(info->gpio_nstby, 0);

	if (gpio_is_valid(info->gpio_nreset))
		gpio_set_value(info->gpio_nreset, 0);

	info->power = 0;

	v4l2_dbg(1, debug, &info->sd,  "%s: sensor is off\n", __func__);

	return 0;
}

static int noon010_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);

	v4l2_dbg(1, debug, sd, "%s: ctrl_id: %d, value: %d\n",
		 __func__, ctrl->id, ctrl->val);

	switch (ctrl->id) {
	case V4L2_CID_AUTO_WHITE_BALANCE:
		return noon010_enable_autowhitebalance(sd, ctrl->val);
	case V4L2_CID_BLUE_BALANCE:
		return cam_i2c_write(sd, MWB_BGAIN_REG, ctrl->val);
	case V4L2_CID_RED_BALANCE:
		return cam_i2c_write(sd, MWB_RGAIN_REG, ctrl->val);
	default:
		return -EINVAL;
	}
}

static int noon010_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			    enum v4l2_mbus_pixelcode *code)
{
	if (!code || index >= ARRAY_SIZE(noon010_formats))
		return -EINVAL;

	*code = noon010_formats[index].code;
	return 0;
}

static int noon010_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct noon010_info *info = to_noon010(sd);
	int ret;

	if (!mf)
		return -EINVAL;

	if (!info->curr_win || !info->curr_fmt) {
		ret = noon010_set_params(sd);
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
static const struct noon010_format *try_fmt(struct v4l2_subdev *sd,
					    struct v4l2_mbus_framefmt *mf)
{
	int i = ARRAY_SIZE(noon010_formats);

	noon010_try_frame_size(mf);

	while (i--)
		if (mf->code == noon010_formats[i].code)
			break;

	mf->code = noon010_formats[i].code;

	return &noon010_formats[i];
}

static int noon010_try_fmt(struct v4l2_subdev *sd,
			   struct v4l2_mbus_framefmt *mf)
{
	if (!sd || !mf)
		return -EINVAL;

	try_fmt(sd, mf);
	return 0;
}

static int noon010_s_fmt(struct v4l2_subdev *sd,
			 struct v4l2_mbus_framefmt *mf)
{
	struct noon010_info *info = to_noon010(sd);

	if (!sd || !mf)
		return -EINVAL;

	info->curr_fmt = try_fmt(sd, mf);

	return noon010_set_params(sd);
}

static int noon010_base_config(struct v4l2_subdev *sd)
{
	struct noon010_info *info = to_noon010(sd);
	int ret;

	ret = noon010_bulk_write_reg(sd, noon010_base_regs);
	if (!ret) {
		info->curr_fmt = &noon010_formats[0];
		info->curr_win = &noon010_sizes[0];
		ret = noon010_set_params(sd);
	}
	if (!ret)
		ret = noon010_set_flip(sd, 1, 0);
	if (!ret)
		ret = noon010_power_ctrl(sd, false, false);

	/* sync the handler and the registers state */
	v4l2_ctrl_handler_setup(&to_noon010(sd)->hdl);
	return ret;
}

static int noon010_s_power(struct v4l2_subdev *sd, int on)
{
	struct noon010_info *info = to_noon010(sd);
	const struct noon010pc30_platform_data *pdata = info->pdata;
	int ret = 0;

	if (WARN(pdata == NULL, "No platform data!\n"))
		return -ENOMEM;

	if (on) {
		ret = power_enable(info);
		if (ret)
			return ret;
		ret = noon010_base_config(sd);
	} else {
		noon010_power_ctrl(sd, false, true);
		ret = power_disable(info);
		info->curr_win = NULL;
		info->curr_fmt = NULL;
	}

	return ret;
}

static int noon010_g_chip_ident(struct v4l2_subdev *sd,
				struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip,
					  V4L2_IDENT_NOON010PC30, 0);
}

static int noon010_log_status(struct v4l2_subdev *sd)
{
	struct noon010_info *info = to_noon010(sd);

	v4l2_ctrl_handler_log_status(&info->hdl, sd->name);
	return 0;
}

static const struct v4l2_ctrl_ops noon010_ctrl_ops = {
	.s_ctrl = noon010_s_ctrl,
};

static const struct v4l2_subdev_core_ops noon010_core_ops = {
	.g_chip_ident	= noon010_g_chip_ident,
	.s_power	= noon010_s_power,
	.g_ctrl		= v4l2_subdev_g_ctrl,
	.s_ctrl		= v4l2_subdev_s_ctrl,
	.queryctrl	= v4l2_subdev_queryctrl,
	.querymenu	= v4l2_subdev_querymenu,
	.g_ext_ctrls	= v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls	= v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls	= v4l2_subdev_s_ext_ctrls,
	.log_status	= noon010_log_status,
};

static const struct v4l2_subdev_video_ops noon010_video_ops = {
	.g_mbus_fmt	= noon010_g_fmt,
	.s_mbus_fmt	= noon010_s_fmt,
	.try_mbus_fmt	= noon010_try_fmt,
	.enum_mbus_fmt	= noon010_enum_fmt,
};

static const struct v4l2_subdev_ops noon010_ops = {
	.core	= &noon010_core_ops,
	.video	= &noon010_video_ops,
};

/* Return 0 if NOON010PC30L sensor type was detected or -ENODEV otherwise. */
static int noon010_detect(struct i2c_client *client, struct noon010_info *info)
{
	int ret;

	ret = power_enable(info);
	if (ret)
		return ret;

	ret = i2c_smbus_read_byte_data(client, DEVICE_ID_REG);
	if (ret < 0)
		dev_err(&client->dev, "I2C read failed: 0x%X\n", ret);

	power_disable(info);

	return ret == NOON010PC30_ID ? 0 : -ENODEV;
}

static int noon010_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct noon010_info *info;
	struct v4l2_subdev *sd;
	const struct noon010pc30_platform_data *pdata
		= client->dev.platform_data;
	int ret;
	int i;

	if (!pdata) {
		dev_err(&client->dev, "No platform data!\n");
		return -EIO;
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	sd = &info->sd;
	strlcpy(sd->name, MODULE_NAME, sizeof(sd->name));
	v4l2_i2c_subdev_init(sd, client, &noon010_ops);

	v4l2_ctrl_handler_init(&info->hdl, 3);

	v4l2_ctrl_new_std(&info->hdl, &noon010_ctrl_ops,
			  V4L2_CID_AUTO_WHITE_BALANCE, 0, 1, 1, 1);
	v4l2_ctrl_new_std(&info->hdl, &noon010_ctrl_ops,
			  V4L2_CID_RED_BALANCE, 0, 127, 1, 64);
	v4l2_ctrl_new_std(&info->hdl, &noon010_ctrl_ops,
			  V4L2_CID_BLUE_BALANCE, 0, 127, 1, 64);

	sd->ctrl_handler = &info->hdl;

	ret = info->hdl.error;
	if (ret)
		goto np_err;

	info->pdata		= client->dev.platform_data;
	info->i2c_reg_page	= -1;
	info->gpio_nreset	= -EINVAL;
	info->gpio_nstby	= -EINVAL;

	if (gpio_is_valid(pdata->gpio_nreset)) {
		ret = gpio_request(pdata->gpio_nreset, "NOON010PC30 NRST");
		if (ret) {
			dev_err(&client->dev, "GPIO request error: %d\n", ret);
			goto np_err;
		}
		info->gpio_nreset = pdata->gpio_nreset;
		gpio_direction_output(info->gpio_nreset, 0);
		gpio_export(info->gpio_nreset, 0);
	}

	if (gpio_is_valid(pdata->gpio_nstby)) {
		ret = gpio_request(pdata->gpio_nstby, "NOON010PC30 NSTBY");
		if (ret) {
			dev_err(&client->dev, "GPIO request error: %d\n", ret);
			goto np_gpio_err;
		}
		info->gpio_nstby = pdata->gpio_nstby;
		gpio_direction_output(info->gpio_nstby, 0);
		gpio_export(info->gpio_nstby, 0);
	}

	for (i = 0; i < NOON010_NUM_SUPPLIES; i++)
		info->supply[i].supply = noon010_supply_name[i];

	ret = regulator_bulk_get(&client->dev, NOON010_NUM_SUPPLIES,
				 info->supply);
	if (ret)
		goto np_reg_err;

	ret = noon010_detect(client, info);
	if (!ret)
		return 0;

	/* the sensor detection failed */
	regulator_bulk_free(NOON010_NUM_SUPPLIES, info->supply);
np_reg_err:
	if (gpio_is_valid(info->gpio_nstby))
		gpio_free(info->gpio_nstby);
np_gpio_err:
	if (gpio_is_valid(info->gpio_nreset))
		gpio_free(info->gpio_nreset);
np_err:
	v4l2_ctrl_handler_free(&info->hdl);
	v4l2_device_unregister_subdev(sd);
	kfree(info);
	return ret;
}

static int noon010_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct noon010_info *info = to_noon010(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&info->hdl);

	regulator_bulk_free(NOON010_NUM_SUPPLIES, info->supply);

	if (gpio_is_valid(info->gpio_nreset))
		gpio_free(info->gpio_nreset);

	if (gpio_is_valid(info->gpio_nstby))
		gpio_free(info->gpio_nstby);

	kfree(info);
	return 0;
}

static const struct i2c_device_id noon010_id[] = {
	{ MODULE_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, noon010_id);


static struct i2c_driver noon010_i2c_driver = {
	.driver = {
		.name = MODULE_NAME
	},
	.probe		= noon010_probe,
	.remove		= noon010_remove,
	.id_table	= noon010_id,
};

static int __init noon010_init(void)
{
	return i2c_add_driver(&noon010_i2c_driver);
}

static void __exit noon010_exit(void)
{
	i2c_del_driver(&noon010_i2c_driver);
}

module_init(noon010_init);
module_exit(noon010_exit);

MODULE_DESCRIPTION("Siliconfile NOON010PC30 camera driver");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_LICENSE("GPL");

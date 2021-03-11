// SPDX-License-Identifier: GPL-2.0
/*
 * gc5035 driver
 *
 * Copyright (C) 2019 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 init driver.
 * TODO: add OTP function.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 add enum_frame_interval function.
 * V0.0X01.0X04 fix vb and gain set issues.
 * V0.0X01.0X05 add quick stream on/off
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_gpio.h>

#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x05)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define GC5035_LANES			2
#define GC5035_BITS_PER_SAMPLE		10
#define GC5035_LINK_FREQ_MHZ		438000000LL
#define MIPI_FREQ		438000000LL

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define GC5035_PIXEL_RATE		(MIPI_FREQ * 2LL * 2LL / 10)
#define GC5035_XVCLK_FREQ		24000000

#define CHIP_ID				0x5035
#define GC5035_REG_CHIP_ID_H		0xf0
#define GC5035_REG_CHIP_ID_L		0xf1

#define GC5035_REG_SET_PAGE		0xfe
#define GC5035_SET_PAGE_ONE		0x00

#define GC5035_REG_CTRL_MODE		0x3e
#define GC5035_MODE_SW_STANDBY		0x01
#define GC5035_MODE_STREAMING		0x91

#define GC5035_REG_EXPOSURE_H		0x03
#define GC5035_REG_EXPOSURE_L		0x04
#define GC5035_FETCH_HIGH_BYTE_EXP(VAL) (((VAL) >> 8) & 0x0F)	/* 4 Bits */
#define GC5035_FETCH_LOW_BYTE_EXP(VAL) ((VAL) & 0xFF)	/* 8 Bits */
#define	GC5035_EXPOSURE_MIN		4
#define	GC5035_EXPOSURE_STEP		1
#define GC5035_VTS_MAX			0x1fff

#define GC5035_REG_AGAIN		0xb6
#define GC5035_REG_DGAIN_INT		0xb1
#define GC5035_REG_DGAIN_FRAC		0xb2
#define GC5035_GAIN_MIN			64
#define GC5035_GAIN_MAX			1024
#define GC5035_GAIN_STEP		1
#define GC5035_GAIN_DEFAULT		64

#define GC5035_REG_VTS_H		0x41
#define GC5035_REG_VTS_L		0x42

#define REG_NULL			0xFF

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define GC5035_NAME			"gc5035"

static const char * const gc5035_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define GC5035_NUM_SUPPLIES ARRAY_SIZE(gc5035_supply_names)

#define IMAGE_NORMAL_MIRROR
#define DD_PARAM_QTY_5035	200
#define INFO_ROM_START_5035	0x08
#define INFO_WIDTH_5035		0x08
#define WB_ROM_START_5035	0x88
#define WB_WIDTH_5035		0x05
#define GOLDEN_ROM_START_5035	0xe0
#define GOLDEN_WIDTH_5035	0x05
#define WINDOW_WIDTH		0x0a30
#define WINDOW_HEIGHT		0x079c

/* SENSOR MIRROR FLIP INFO */
#define GC5035_MIRROR_FLIP_ENABLE         0
#if GC5035_MIRROR_FLIP_ENABLE
#define GC5035_MIRROR                     0x83
#define GC5035_RSTDUMMY1                  0x03
#define GC5035_RSTDUMMY2                  0xfc
#else
#define GC5035_MIRROR                     0x80
#define GC5035_RSTDUMMY1                  0x02
#define GC5035_RSTDUMMY2                  0x7c
#endif

struct gc5035_otp_info {
	u32 flag; //bit[7]: info bit[6]:wb bit[3]:dd
	u32 module_id;
	u32 lens_id;
	u16 vcm_id;
	u16 vcm_driver_id;
	u32 year;
	u32 month;
	u32 day;
	u32 rg_ratio;
	u32 bg_ratio;
	u32 golden_rg;
	u32 golden_bg;
	u16 dd_param_x[DD_PARAM_QTY_5035];
	u16 dd_param_y[DD_PARAM_QTY_5035];
	u16 dd_param_type[DD_PARAM_QTY_5035];
	u16 dd_cnt;
};

struct gc5035_id_name {
	u32 id;
	char name[RKMODULE_NAME_LEN];
};

struct regval {
	u8 addr;
	u8 val;
};

struct gc5035_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct gc5035 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[GC5035_NUM_SUPPLIES];

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct gc5035_mode *cur_mode;
	unsigned int lane_num;
	unsigned int cfg_num;
	unsigned int pixel_rate;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32 Dgain_ratio;
	struct gc5035_otp_info *otp;
	struct rkmodule_inf	module_inf;
	struct rkmodule_awb_cfg	awb_cfg;
};

#define to_gc5035(sd) container_of(sd, struct gc5035, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval gc5035_global_regs[] = {
	/* SYSTEM */
	{0xfc, 0x01},
	{0xf4, 0x40},
	{0xf5, 0xe9},
	{0xf6, 0x14},
	{0xf8, 0x49},
	{0xf9, 0x82},
	{0xfa, 0x00},
	{0xfc, 0x81},
	{0xfe, 0x00},
	{0x36, 0x01},
	{0xd3, 0x87},
	{0x36, 0x00},
	{0x33, 0x00},
	{0xfe, 0x03},
	{0x01, 0xe7},
	{0xf7, 0x01},
	{0xfc, 0x8f},
	{0xfc, 0x8f},
	{0xfc, 0x8e},
	{0xfe, 0x00},
	{0xee, 0x30},
	{0x87, 0x18},
	{0xfe, 0x01},
	{0x8c, 0x90},
	{0xfe, 0x00},

	/* Analog & CISCTL */
	{0xfe, 0x00},
	{0x05, 0x02},
	{0x06, 0xda},
	{0x9d, 0x0c},
	{0x09, 0x00},
	{0x0a, 0x04},
	{0x0b, 0x00},
	{0x0c, 0x03},
	{0x0d, 0x07},
	{0x0e, 0xa8},
	{0x0f, 0x0a},
	{0x10, 0x30},
	{0x11, 0x02},
	{0x17, GC5035_MIRROR},
	{0x19, 0x05},
	{0xfe, 0x02},
	{0x30, 0x03},
	{0x31, 0x03},
	{0xfe, 0x00},
	{0xd9, 0xc0},
	{0x1b, 0x20},
	{0x21, 0x48},
	{0x28, 0x22},
	{0x29, 0x58},
	{0x44, 0x20},
	{0x4b, 0x10},
	{0x4e, 0x1a},
	{0x50, 0x11},
	{0x52, 0x33},
	{0x53, 0x44},
	{0x55, 0x10},
	{0x5b, 0x11},
	{0xc5, 0x02},
	{0x8c, 0x1a},
	{0xfe, 0x02},
	{0x33, 0x05},
	{0x32, 0x38},
	{0xfe, 0x00},
	{0x91, 0x80},
	{0x92, 0x28},
	{0x93, 0x20},
	{0x95, 0xa0},
	{0x96, 0xe0},
	{0xd5, 0xfc},
	{0x97, 0x28},
	{0x16, 0x0c},
	{0x1a, 0x1a},
	{0x1f, 0x11},
	{0x20, 0x10},
	{0x46, 0xe3},
	{0x4a, 0x04},
	{0x54, GC5035_RSTDUMMY1},
	{0x62, 0x00},
	{0x72, 0xcf},
	{0x73, 0xc9},
	{0x7a, 0x05},
	{0x7d, 0xcc},
	{0x90, 0x00},
	{0xce, 0x98},
	{0xd0, 0xb2},
	{0xd2, 0x40},
	{0xe6, 0xe0},
	{0xfe, 0x02},
	{0x12, 0x01},
	{0x13, 0x01},
	{0x14, 0x01},
	{0x15, 0x02},
	{0x22, GC5035_RSTDUMMY2},
	{0x91, 0x00},
	{0x92, 0x00},
	{0x93, 0x00},
	{0x94, 0x00},
	{0xfe, 0x00},
	{0xfc, 0x88},
	{0xfe, 0x10},
	{0xfe, 0x00},
	{0xfc, 0x8e},
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xfc, 0x88},
	{0xfe, 0x10},
	{0xfe, 0x00},
	{0xfc, 0x8e},

	/* Gain */
	{0xfe, 0x00},
	{0xb0, 0x6e},
	{0xb1, 0x01},
	{0xb2, 0x00},
	{0xb3, 0x00},
	{0xb4, 0x00},
	{0xb6, 0x00},

	/* ISP */
	{0xfe, 0x01},
	{0x53, 0x00},
	{0x89, 0x03},
	{0x60, 0x40},
	{0x87, 0x50},

	/* BLK */
	{0xfe, 0x01},
	{0x42, 0x21},
	{0x49, 0x03},
	{0x4a, 0xff},
	{0x4b, 0xc0},
	{0x55, 0x00},

	/* Anti_blooming */
	{0xfe, 0x01},
	{0x41, 0x28},
	{0x4c, 0x00},
	{0x4d, 0x00},
	{0x4e, 0x3c},
	{0x44, 0x08},
	{0x48, 0x01},

	/* Crop */
	{0xfe, 0x01},
	{0x91, 0x00},
	{0x92, 0x08},
	{0x93, 0x00},
	{0x94, 0x07},
	{0x95, 0x07},
	{0x96, 0x98},
	{0x97, 0x0a},
	{0x98, 0x20},
	{0x99, 0x00},

	/* MIPI */
	{0xfe, 0x03},
	{0x02, 0x57},
	{0x03, 0xb7},
	{0x15, 0x14},
	{0x18, 0x0f},
	{0x21, 0x22},
	{0x22, 0x06},
	{0x23, 0x48},
	{0x24, 0x12},
	{0x25, 0x28},
	{0x26, 0x08},
	{0x29, 0x06},
	{0x2a, 0x58},
	{0x2b, 0x08},
	{0xfe, 0x01},
	{0x8c, 0x10},

	{0xfe, 0x00},
	{0x3e, 0x01},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 876Mbps
 */
static const struct regval gc5035_2592x1944_regs[] = {
	/* lane snap */
	{REG_NULL, 0x00},
};

static const struct gc5035_mode supported_modes_2lane[] = {
	{
		.width = 2592,
		.height = 1944,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x07C0,
		.hts_def = 0x0B68,
		.vts_def = 0x07D0,
		.reg_list = gc5035_2592x1944_regs,
	},
};

static const struct gc5035_mode *supported_modes;

static const s64 link_freq_menu_items[] = {
	GC5035_LINK_FREQ_MHZ
};

/* Write registers up to 4 at a time */
static int gc5035_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	dev_dbg(&client->dev, "write reg(0x%x val:0x%x)!\n", reg, val);
	buf[0] = reg & 0xFF;
	buf[1] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0)
		return 0;

	dev_err(&client->dev,
		"gc5035 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int gc5035_write_array(struct i2c_client *client,
				const struct regval *regs)
{
	u32 i = 0;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = gc5035_write_reg(client, regs[i].addr, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int gc5035_read_reg(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[1];
	int ret;

	buf[0] = reg & 0xFF;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret >= 0) {
		*val = buf[0];
		return 0;
	}

	dev_err(&client->dev,
		"gc5035 read reg:0x%x failed !\n", reg);

	return ret;
}

static int gc5035_get_reso_dist(const struct gc5035_mode *mode,
	struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
		abs(mode->height - framefmt->height);
}

static const struct gc5035_mode *
gc5035_find_best_fit(struct gc5035 *gc5035,
			struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < gc5035->cfg_num; i++) {
		dist = gc5035_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int gc5035_set_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *fmt)
{
	struct gc5035 *gc5035 = to_gc5035(sd);
	const struct gc5035_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&gc5035->mutex);

	mode = gc5035_find_best_fit(gc5035, fmt);
	fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc5035->mutex);
		return -ENOTTY;
#endif
	} else {
		gc5035->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(gc5035->hblank, h_blank,
			h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(gc5035->vblank, vblank_def,
			GC5035_VTS_MAX - mode->height,
			1, vblank_def);
	}

	mutex_unlock(&gc5035->mutex);

	return 0;
}

static int gc5035_get_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *fmt)
{
	struct gc5035 *gc5035 = to_gc5035(sd);
	const struct gc5035_mode *mode = gc5035->cur_mode;

	mutex_lock(&gc5035->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc5035->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&gc5035->mutex);

	return 0;
}

static int gc5035_enum_mbus_code(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SRGGB10_1X10;

	return 0;
}

static int gc5035_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct gc5035 *gc5035 = to_gc5035(sd);

	if (fse->index >= gc5035->cfg_num)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SRGGB10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int gc5035_g_frame_interval(struct v4l2_subdev *sd,
	struct v4l2_subdev_frame_interval *fi)
{
	struct gc5035 *gc5035 = to_gc5035(sd);
	const struct gc5035_mode *mode = gc5035->cur_mode;

	mutex_lock(&gc5035->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&gc5035->mutex);

	return 0;
}

static void gc5035_get_module_inf(struct gc5035 *gc5035,
				  struct rkmodule_inf *inf)
{
	strlcpy(inf->base.sensor,
		GC5035_NAME,
		sizeof(inf->base.sensor));
	strlcpy(inf->base.module,
		gc5035->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens,
		gc5035->len_name,
		sizeof(inf->base.lens));
}

static void gc5035_set_module_inf(struct gc5035 *gc5035,
				  struct rkmodule_awb_cfg *cfg)
{
	mutex_lock(&gc5035->mutex);
	memcpy(&gc5035->awb_cfg, cfg, sizeof(*cfg));
	mutex_unlock(&gc5035->mutex);
}

static long gc5035_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc5035 *gc5035 = to_gc5035(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc5035_get_module_inf(gc5035, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_AWB_CFG:
		gc5035_set_module_inf(gc5035, (struct rkmodule_awb_cfg *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream) {
			ret = gc5035_write_reg(gc5035->client,
					       GC5035_REG_SET_PAGE,
					       GC5035_SET_PAGE_ONE);
			ret |= gc5035_write_reg(gc5035->client,
						GC5035_REG_CTRL_MODE,
						GC5035_MODE_STREAMING);
		} else {
			ret = gc5035_write_reg(gc5035->client,
					       GC5035_REG_SET_PAGE,
					       GC5035_SET_PAGE_ONE);
			ret |= gc5035_write_reg(gc5035->client,
						GC5035_REG_CTRL_MODE,
						GC5035_MODE_SW_STANDBY);
		}
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc5035_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc5035_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = gc5035_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc5035_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}
#endif

static int __gc5035_start_stream(struct gc5035 *gc5035)
{
	int ret;

	ret = gc5035_write_array(gc5035->client, gc5035->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&gc5035->mutex);
	ret = v4l2_ctrl_handler_setup(&gc5035->ctrl_handler);
	mutex_lock(&gc5035->mutex);
	if (ret)
		return ret;
	ret = gc5035_write_reg(gc5035->client,
		GC5035_REG_SET_PAGE,
		GC5035_SET_PAGE_ONE);
	ret |= gc5035_write_reg(gc5035->client,
		GC5035_REG_CTRL_MODE,
		GC5035_MODE_STREAMING);
	return ret;
}

static int __gc5035_stop_stream(struct gc5035 *gc5035)
{
	int ret;

	ret = gc5035_write_reg(gc5035->client,
		GC5035_REG_SET_PAGE,
		GC5035_SET_PAGE_ONE);
	ret |= gc5035_write_reg(gc5035->client,
		GC5035_REG_CTRL_MODE,
		GC5035_MODE_SW_STANDBY);
	return ret;
}

static int gc5035_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc5035 *gc5035 = to_gc5035(sd);
	struct i2c_client *client = gc5035->client;
	int ret = 0;

	mutex_lock(&gc5035->mutex);
	on = !!on;
	if (on == gc5035->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __gc5035_start_stream(gc5035);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__gc5035_stop_stream(gc5035);
		pm_runtime_put(&client->dev);
	}

	gc5035->streaming = on;

unlock_and_return:
	mutex_unlock(&gc5035->mutex);

	return ret;
}

static int gc5035_s_power(struct v4l2_subdev *sd, int on)
{
	struct gc5035 *gc5035 = to_gc5035(sd);
	struct i2c_client *client = gc5035->client;
	int ret = 0;

	mutex_lock(&gc5035->mutex);

	/* If the power state is not modified - no work to do. */
	if (gc5035->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = gc5035_write_array(gc5035->client, gc5035_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		gc5035->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		gc5035->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&gc5035->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 gc5035_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, GC5035_XVCLK_FREQ / 1000 / 1000);
}

static int __gc5035_power_on(struct gc5035 *gc5035)
{
	int ret;
	u32 delay_us;
	struct device *dev = &gc5035->client->dev;

	if (!IS_ERR_OR_NULL(gc5035->pins_default)) {
		ret = pinctrl_select_state(gc5035->pinctrl,
					   gc5035->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(gc5035->xvclk, GC5035_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(gc5035->xvclk) != GC5035_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(gc5035->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(gc5035->reset_gpio))
		gpiod_set_value_cansleep(gc5035->reset_gpio, 0);

	ret = regulator_bulk_enable(GC5035_NUM_SUPPLIES, gc5035->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	usleep_range(1000, 1100);
	if (!IS_ERR(gc5035->reset_gpio))
		gpiod_set_value_cansleep(gc5035->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(gc5035->pwdn_gpio))
		gpiod_set_value_cansleep(gc5035->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = gc5035_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(gc5035->xvclk);

	return ret;
}

static void __gc5035_power_off(struct gc5035 *gc5035)
{
	int ret;

	if (!IS_ERR(gc5035->pwdn_gpio))
		gpiod_set_value_cansleep(gc5035->pwdn_gpio, 0);
	clk_disable_unprepare(gc5035->xvclk);
	if (!IS_ERR(gc5035->reset_gpio))
		gpiod_set_value_cansleep(gc5035->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(gc5035->pins_sleep)) {
		ret = pinctrl_select_state(gc5035->pinctrl,
			gc5035->pins_sleep);
		if (ret < 0)
			dev_dbg(&gc5035->client->dev, "could not set pins\n");
	}
	regulator_bulk_disable(GC5035_NUM_SUPPLIES, gc5035->supplies);
}

static int gc5035_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5035 *gc5035 = to_gc5035(sd);

	return __gc5035_power_on(gc5035);
}

static int gc5035_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5035 *gc5035 = to_gc5035(sd);

	__gc5035_power_off(gc5035);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc5035_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc5035 *gc5035 = to_gc5035(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc5035_mode *def_mode = &supported_modes[0];

	mutex_lock(&gc5035->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&gc5035->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sensor_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	struct gc5035 *sensor = to_gc5035(sd);
	struct device *dev = &sensor->client->dev;

	dev_info(dev, "%s(%d) enter!\n", __func__, __LINE__);

	if (2 == sensor->lane_num) {
		config->type = V4L2_MBUS_CSI2;
		config->flags = V4L2_MBUS_CSI2_2_LANE |
						V4L2_MBUS_CSI2_CHANNEL_0 |
						V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	} else {
		dev_err(&sensor->client->dev,
				"unsupported lane_num(%d)\n", sensor->lane_num);
	}
	return 0;
}

static int gc5035_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct gc5035 *gc5035 = to_gc5035(sd);

	if (fie->index >= gc5035->cfg_num)
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_SRGGB10_1X10)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static const struct dev_pm_ops gc5035_pm_ops = {
	SET_RUNTIME_PM_OPS(gc5035_runtime_suspend,
			   gc5035_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc5035_internal_ops = {
	.open = gc5035_open,
};
#endif

static const struct v4l2_subdev_core_ops gc5035_core_ops = {
	.s_power = gc5035_s_power,
	.ioctl = gc5035_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc5035_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc5035_video_ops = {
	.g_mbus_config = sensor_g_mbus_config,
	.s_stream = gc5035_s_stream,
	.g_frame_interval = gc5035_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc5035_pad_ops = {
	.enum_mbus_code = gc5035_enum_mbus_code,
	.enum_frame_size = gc5035_enum_frame_sizes,
	.enum_frame_interval = gc5035_enum_frame_interval,
	.get_fmt = gc5035_get_fmt,
	.set_fmt = gc5035_set_fmt,
};

static const struct v4l2_subdev_ops gc5035_subdev_ops = {
	.core	= &gc5035_core_ops,
	.video	= &gc5035_video_ops,
	.pad	= &gc5035_pad_ops,
};

static int gc5035_set_test_pattern(struct gc5035 *gc5035, int value)
{
	int ret = 0;

	dev_info(&gc5035->client->dev, "Test Pattern!!\n");
	ret = gc5035_write_reg(gc5035->client, 0xfe, 0x01);
	ret = gc5035_write_reg(gc5035->client, 0x8c, value);
	ret = gc5035_write_reg(gc5035->client, 0xfe, 0x00);
	return ret;
}

static const char * const gc5035_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

static int gc5035_set_exposure_reg(struct gc5035 *gc5035, u32 exposure)
{
	u32 caltime = 0;
	int ret = 0;

	caltime = exposure / 2;
	caltime = caltime * 2;
	gc5035->Dgain_ratio = 64 * exposure / caltime;
	ret = gc5035_write_reg(gc5035->client,
		GC5035_REG_SET_PAGE,
		GC5035_SET_PAGE_ONE);

	ret |= gc5035_write_reg(gc5035->client,
		GC5035_REG_EXPOSURE_H,
		(caltime >> 8) & 0x3F);
	ret |= gc5035_write_reg(gc5035->client,
		GC5035_REG_EXPOSURE_L,
		caltime & 0xFF);

	return ret;
}

static u32 GC5035_AGC_Param[17][2] = {
	{64, 0},
	{76, 1},
	{90, 2},
	{106, 3},
	{126, 8},
	{150, 9},
	{179, 10},
	{211, 11},
	{250, 12},
	{301, 13},
	{358, 14},
	{427, 15},
	{499, 16},
	{589, 17},
	{704, 18},
	{830, 19},
	{998, 20},
};

static int gc5035_set_gain_reg(struct gc5035 *gc5035, u32 a_gain)
{
	struct device *dev = &gc5035->client->dev;
	int ret = 0, i = 0;
	u32 temp_gain = 0;

	dev_info(dev, "%s(%d) a_gain(0x%08x)!\n", __func__, __LINE__, a_gain);
	if (a_gain < 0x40)
		a_gain = 0x40;
	else if (a_gain > 0x400)
		a_gain = 0x400;
	for (i = 16; i >= 0; i--) {
		if (a_gain >= GC5035_AGC_Param[i][0])
			break;
	}

	ret = gc5035_write_reg(gc5035->client,
		GC5035_REG_SET_PAGE,
		GC5035_SET_PAGE_ONE);
	ret |= gc5035_write_reg(gc5035->client,
		GC5035_REG_AGAIN, GC5035_AGC_Param[i][1]);
	temp_gain = a_gain;
	temp_gain = temp_gain * gc5035->Dgain_ratio / GC5035_AGC_Param[i][0];

	dev_info(dev, "AGC_Param[%d][0](%d) temp_gain is(0x%08x)!\n",
				i, GC5035_AGC_Param[i][0], temp_gain);
	ret |= gc5035_write_reg(gc5035->client,
		GC5035_REG_DGAIN_INT,
		temp_gain >> 6);
	ret |= gc5035_write_reg(gc5035->client,
		GC5035_REG_DGAIN_FRAC,
		(temp_gain << 2) & 0xfc);
	return ret;
}

static int gc5035_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc5035 *gc5035 = container_of(ctrl->handler,
					     struct gc5035, ctrl_handler);
	struct i2c_client *client = gc5035->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = gc5035->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(gc5035->exposure,
			gc5035->exposure->minimum, max,
			gc5035->exposure->step,
			gc5035->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = gc5035_set_exposure_reg(gc5035, ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = gc5035_set_gain_reg(gc5035, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = gc5035_write_reg(gc5035->client,
			GC5035_REG_SET_PAGE,
			GC5035_SET_PAGE_ONE);
		ret |= gc5035_write_reg(gc5035->client,
			GC5035_REG_VTS_H,
			((ctrl->val + gc5035->cur_mode->height) >> 8) & 0xff);
		ret |= gc5035_write_reg(gc5035->client,
			GC5035_REG_VTS_L,
			(ctrl->val + gc5035->cur_mode->height) & 0xff);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = gc5035_set_test_pattern(gc5035, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			__func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc5035_ctrl_ops = {
	.s_ctrl = gc5035_set_ctrl,
};

static int gc5035_initialize_controls(struct gc5035 *gc5035)
{
	const struct gc5035_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &gc5035->ctrl_handler;
	mode = gc5035->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &gc5035->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
		0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
		0, GC5035_PIXEL_RATE, 1, GC5035_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	gc5035->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
		h_blank, h_blank, 1, h_blank);
	if (gc5035->hblank)
		gc5035->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc5035->vblank = v4l2_ctrl_new_std(handler, &gc5035_ctrl_ops,
		V4L2_CID_VBLANK, vblank_def,
		GC5035_VTS_MAX - mode->height,
		1, vblank_def);

	exposure_max = mode->vts_def - 4;
	gc5035->exposure = v4l2_ctrl_new_std(handler, &gc5035_ctrl_ops,
		V4L2_CID_EXPOSURE, GC5035_EXPOSURE_MIN,
		exposure_max, GC5035_EXPOSURE_STEP,
		mode->exp_def);

	gc5035->anal_gain = v4l2_ctrl_new_std(handler, &gc5035_ctrl_ops,
		V4L2_CID_ANALOGUE_GAIN, GC5035_GAIN_MIN,
		GC5035_GAIN_MAX, GC5035_GAIN_STEP,
		GC5035_GAIN_DEFAULT);

	gc5035->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&gc5035_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(gc5035_test_pattern_menu) - 1,
				0, 0, gc5035_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&gc5035->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc5035->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int gc5035_check_sensor_id(struct gc5035 *gc5035,
	struct i2c_client *client)
{
	struct device *dev = &gc5035->client->dev;
	u16 id = 0;
	u8 reg_H = 0;
	u8 reg_L = 0;
	int ret;

	ret = gc5035_read_reg(client, GC5035_REG_CHIP_ID_H, &reg_H);
	ret |= gc5035_read_reg(client, GC5035_REG_CHIP_ID_L, &reg_L);
	id = ((reg_H << 8) & 0xff00) | (reg_L & 0xff);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	dev_info(dev, "detected gc%04x sensor\n", id);
	return ret;
}

static int gc5035_configure_regulators(struct gc5035 *gc5035)
{
	unsigned int i;

	for (i = 0; i < GC5035_NUM_SUPPLIES; i++)
		gc5035->supplies[i].supply = gc5035_supply_names[i];

	return devm_regulator_bulk_get(&gc5035->client->dev,
		GC5035_NUM_SUPPLIES,
		gc5035->supplies);
}

static void free_gpio(struct gc5035 *sensor)
{
	struct device *dev = &sensor->client->dev;
	unsigned int temp_gpio = -1;

	dev_info(dev, "%s(%d) enter!\n", __func__, __LINE__);
	if (!IS_ERR(sensor->reset_gpio)) {
		temp_gpio = desc_to_gpio(sensor->reset_gpio);
		dev_info(dev, "free gpio(%d)!\n", temp_gpio);
		gpio_free(temp_gpio);
	}

	if (!IS_ERR(sensor->pwdn_gpio)) {
		temp_gpio = desc_to_gpio(sensor->pwdn_gpio);
		dev_info(dev, "free gpio(%d)!\n", temp_gpio);
		gpio_free(temp_gpio);
	}
}

static int gc5035_parse_of(struct gc5035 *gc5035)
{
	struct device *dev = &gc5035->client->dev;
	struct device_node *endpoint;
	struct fwnode_handle *fwnode;
	int rval;

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}
	fwnode = of_fwnode_handle(endpoint);
	rval = fwnode_property_read_u32_array(fwnode, "data-lanes", NULL, 0);
	if (rval <= 0) {
		dev_warn(dev, " Get mipi lane num failed!\n");
		return -1;
	}

	gc5035->lane_num = rval;
	if (2 == gc5035->lane_num) {
		gc5035->cur_mode = &supported_modes_2lane[0];
		supported_modes = supported_modes_2lane;
		gc5035->cfg_num = ARRAY_SIZE(supported_modes_2lane);

		/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
		gc5035->pixel_rate = MIPI_FREQ * 2U * gc5035->lane_num / 10U;
		dev_info(dev, "lane_num(%d)  pixel_rate(%u)\n",
				 gc5035->lane_num, gc5035->pixel_rate);
	} else {
		dev_err(dev, "unsupported lane_num(%d)\n", gc5035->lane_num);
		return -1;
	}
	return 0;
}

static int gc5035_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gc5035 *gc5035;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	gc5035 = devm_kzalloc(dev, sizeof(*gc5035), GFP_KERNEL);
	if (!gc5035)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
		&gc5035->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
		&gc5035->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
		&gc5035->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
		&gc5035->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}
	gc5035->client = client;

	gc5035->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(gc5035->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc5035->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc5035->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc5035->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc5035->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = gc5035_configure_regulators(gc5035);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	ret = gc5035_parse_of(gc5035);
	if (ret != 0)
		return -EINVAL;

	gc5035->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(gc5035->pinctrl)) {
		gc5035->pins_default =
			pinctrl_lookup_state(gc5035->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(gc5035->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		gc5035->pins_sleep =
			pinctrl_lookup_state(gc5035->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(gc5035->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&gc5035->mutex);

	sd = &gc5035->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc5035_subdev_ops);
	ret = gc5035_initialize_controls(gc5035);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc5035_power_on(gc5035);
	if (ret)
		goto err_free_handler;

	ret = gc5035_check_sensor_id(gc5035, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc5035_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	gc5035->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc5035->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc5035->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc5035->module_index, facing,
		 GC5035_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__gc5035_power_off(gc5035);
	free_gpio(gc5035);
err_free_handler:
	v4l2_ctrl_handler_free(&gc5035->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc5035->mutex);

	return ret;
}

static int gc5035_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5035 *gc5035 = to_gc5035(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc5035->ctrl_handler);
	mutex_destroy(&gc5035->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc5035_power_off(gc5035);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc5035_of_match[] = {
	{ .compatible = "galaxycore,gc5035" },
	{},
};
MODULE_DEVICE_TABLE(of, gc5035_of_match);
#endif

static const struct i2c_device_id gc5035_match_id[] = {
	{ "galaxycore,gc5035", 0 },
	{ },
};

static struct i2c_driver gc5035_i2c_driver = {
	.driver = {
		.name = GC5035_NAME,
		.pm = &gc5035_pm_ops,
		.of_match_table = of_match_ptr(gc5035_of_match),
	},
	.probe		= &gc5035_probe,
	.remove		= &gc5035_remove,
	.id_table	= gc5035_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc5035_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc5035_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("GalaxyCore gc5035 sensor driver");
MODULE_LICENSE("GPL v2");

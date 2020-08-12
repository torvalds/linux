// SPDX-License-Identifier: GPL-2.0
/*
 * gc2093 sensor driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>

#include <media/v4l2-async.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define DRIVER_VERSION		KERNEL_VERSION(0, 0x01, 0x00)
#define GC2093_NAME		"gc2093"
#define GC2093_MEDIA_BUS_FMT	MEDIA_BUS_FMT_SRGGB10_1X10

#define MIPI_FREQ_297M		297000000
/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define GC2093_PIXEL_RATE	(MIPI_FREQ_297M * 2 * 2 / 10)
#define GC2093_XVCLK_FREQ	27000000

#define GC2093_REG_CHIP_ID_H	0x03F0
#define GC2093_REG_CHIP_ID_L	0x03F1

#define GC2093_REG_EXP_H	0x0003
#define GC2093_REG_EXP_L	0x0004

#define GC2093_MIRROR_FLIP_REG	0x0017
#define MIRROR_MASK		BIT(0)
#define FLIP_MASK		BIT(1)

#define GC2093_REG_CTRL_MODE	0x003E
#define GC2093_MODE_SW_STANDBY	0x11
#define GC2093_MODE_STREAMING	0x91

#define REG_NULL		0xFFFF

#define GC2093_CHIP_ID		0x2093

#define GC2093_VTS_MAX		0x3FFF
#define GC2093_HTS_MAX		0xFFF

#define GC2093_EXPOSURE_MAX	0x3FFF
#define GC2093_EXPOSURE_MIN	1
#define GC2093_EXPOSURE_STEP	1

#define GC2093_GAIN_MIN		0x40
#define GC2093_GAIN_MAX		0x2000
#define GC2093_GAIN_STEP	1
#define GC2093_GAIN_DEFAULT	64

#define GC2093_LANES		2

static const char * const gc2093_supply_names[] = {
	"dovdd",    /* Digital I/O power */
	"avdd",     /* Analog power */
	"dvdd",     /* Digital power */
};

#define GC2093_NUM_SUPPLIES ARRAY_SIZE(gc2093_supply_names)

#define to_gc2093(sd) container_of(sd, struct gc2093, subdev)

enum gc2093_max_pad {
	PAD0,
	PAD_MAX,
};

struct gain_reg_config {
	u32  value;
	u16  analog_gain;
	u16  col_gain;
	u8   dacin;
};

struct gc2093_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct reg_sequence *reg_list;
	u32 reg_num;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct gc2093 {
	struct device	*dev;
	struct clk	*xvclk;
	struct regmap	*regmap;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	struct regulator_bulk_data supplies[GC2093_NUM_SUPPLIES];

	struct v4l2_subdev  subdev;
	struct media_pad    pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl    *exposure;
	struct v4l2_ctrl    *anal_gain;
	struct v4l2_ctrl    *hblank;
	struct v4l2_ctrl    *vblank;
	struct v4l2_ctrl    *h_flip;
	struct v4l2_ctrl    *v_flip;
	struct v4l2_ctrl    *pixel_rate;

	struct mutex        lock;
	bool		    streaming;
	unsigned int        cfg_num;
	const struct gc2093_mode *cur_mode;

	u8		flip;
	u32		module_index;
	const char      *module_facing;
	const char      *module_name;
	const char      *len_name;
};

static const struct regmap_config gc2093_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x0420,
};

/*
 * window_size=1920*1080 mipi@2lane
 * mclk=24mhz mipi_clk=594Mbps
 * pixel_line_total=2200 line_frame_total=1125
 * row_time=29.62us frame_rate=30fps
 */
static const struct reg_sequence gc2093_1080p_liner_settings[] = {
	/* System */
	{0x03fe, 0x80},
	{0x03fe, 0x80},
	{0x03fe, 0x80},
	{0x03fe, 0x00},
	{0x03f2, 0x00},
	{0x03f3, 0x00},
	{0x03f4, 0x36},
	{0x03f5, 0xc0},
	{0x03f6, 0x0A},
	{0x03f7, 0x01},
	{0x03f8, 0x2C},
	{0x03f9, 0x10},
	{0x03fc, 0x8e},
	/* Cisctl & analog */
	{0x0087, 0x18},
	{0x00ee, 0x30},
	{0x00d0, 0xb7},
	{0x01a0, 0x00},
	{0x01a4, 0x40},
	{0x01a5, 0x40},
	{0x01a6, 0x40},
	{0x01af, 0x09},
	{0x0001, 0x00},
	{0x0002, 0x02},
	{0x0003, 0x00},
	{0x0004, 0x02},
	{0x0005, 0x04},
	{0x0006, 0x4c},
	{0x0007, 0x00},
	{0x0008, 0x11},
	{0x0009, 0x00},
	{0x000a, 0x02},
	{0x000b, 0x00},
	{0x000c, 0x04},
	{0x000d, 0x04},
	{0x000e, 0x40},
	{0x000f, 0x07},
	{0x0010, 0x8c},
	{0x0013, 0x15},
	{0x0019, 0x0c},
	{0x0041, 0x04},
	{0x0042, 0x65},
	{0x0053, 0x60},
	{0x008d, 0x92},
	{0x0090, 0x00},
	{0x00c7, 0xe1},
	{0x001b, 0x73},
	{0x0028, 0x0d},
	{0x0029, 0x40},
	{0x002b, 0x04},
	{0x002e, 0x23},
	{0x0037, 0x03},
	{0x0038, 0x88},
	{0x0044, 0x20},
	{0x004b, 0x14},
	{0x0055, 0x20},
	{0x0068, 0x20},
	{0x0069, 0x20},
	{0x0077, 0x00},
	{0x0078, 0x04},
	{0x007c, 0x91},
	{0x00ce, 0x7c},
	{0x00d3, 0xdc},
	{0x00e6, 0x50},
	/* Gain */
	{0x00b6, 0xc0},
	{0x00b0, 0x60},
	/* Isp */
	{0x0102, 0x89},
	{0x0104, 0x01},
	{0x0158, 0x00},
	/* Blk */
	{0x0026, 0x20},
	{0x0142, 0x00},
	{0x0149, 0x1e},
	{0x014a, 0x07},
	{0x014b, 0x80},
	{0x0155, 0x07},
	{0x0414, 0x7e},
	{0x0415, 0x7e},
	{0x0416, 0x7e},
	{0x0417, 0x7e},
	/* Window */
	{0x0192, 0x02},
	{0x0194, 0x03},
	{0x0195, 0x04},
	{0x0196, 0x38},
	{0x0197, 0x07},
	{0x0198, 0x80},
	/* MIPI */
	{0x019a, 0x06},
	{0x007b, 0x2a},
	{0x0023, 0x2d},
	{0x0201, 0x27},
	{0x0202, 0x56},
	{0x0203, 0xce},
	{0x0212, 0x80},
	{0x0213, 0x07},
	{0x003e, 0x91},
};

static const struct gc2093_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x460,
		.hts_def = 0x898,
		.vts_def = 0x465,
		.reg_list = gc2093_1080p_liner_settings,
		.reg_num = ARRAY_SIZE(gc2093_1080p_liner_settings),
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ_297M
};

static inline int gc2093_read_reg(struct gc2093 *gc2093, u16 addr, u8 *value)
{
	unsigned int val;
	int ret;

	ret = regmap_read(gc2093->regmap, addr, &val);
	if (ret) {
		dev_err(gc2093->dev, "i2c read failed at addr: %x\n", addr);
		return ret;
	}

	*value = val & 0xff;

	return 0;
}

static inline int gc2093_write_reg(struct gc2093 *gc2093, u16 addr, u8 value)
{
	int ret;

	ret = regmap_write(gc2093->regmap, addr, value);
	if (ret) {
		dev_err(gc2093->dev, "i2c write failed at addr: %x\n", addr);
		return ret;
	}

	return ret;
}

static const struct gain_reg_config gain_reg_configs[] = {
	{  64, 0x0000, 0x0100, 0x04},
	{  74, 0x0010, 0x010c, 0x04},
	{  89, 0x0020, 0x011b, 0x04},
	{ 104, 0x0030, 0x012c, 0x04},
	{ 126, 0x0040, 0x013f, 0x2c},
	{ 146, 0x0050, 0x0216, 0x2c},
	{ 179, 0x0060, 0x0235, 0x2c},
	{ 209, 0x0070, 0x0316, 0x2c},
	{ 254, 0x0080, 0x0402, 0x28},
	{ 295, 0x0090, 0x0431, 0x28},
	{ 339, 0x00a0, 0x0532, 0x28},
	{ 394, 0x00b0, 0x0635, 0x28},
	{ 479, 0x00c0, 0x0804, 0x24},
	{ 566, 0x005a, 0x0919, 0x24},
	{ 682, 0x0083, 0x0b0f, 0x24},
	{ 793, 0x0093, 0x0d12, 0x24},
	{ 963, 0x0084, 0x1000, 0x20},
	{1119, 0x0094, 0x123a, 0x20},
	{1407, 0x012c, 0x1a02, 0x20},
	{1634, 0x013c, 0x1b20, 0x20},
	{1927, 0x008c, 0x200f, 0x14},
	{2239, 0x009c, 0x2607, 0x14},
	{2619, 0x0264, 0x3621, 0x14},
	{2966, 0x0274, 0x373a, 0x14},
	{3438, 0x00c6, 0x3d02, 0x08},
	{3994, 0x00dc, 0x3f3f, 0x08},
	{4828, 0x0285, 0x3f3f, 0x08},
	{4678, 0x0295, 0x3f3f, 0x04},
	{6875, 0x00ce, 0x3f3f, 0x04},
	{8192, 0x00ce, 0x3f3f, 0x04},
};

static int gc2093_set_gain(struct gc2093 *gc2093, u32 gain)
{
	int ret, i = 0;
	u16 pre_gain = 0;

	for (i = 0; i < ARRAY_SIZE(gain_reg_configs) - 1; i++)
		if ((gain_reg_configs[i].value <= gain) && (gain < gain_reg_configs[i+1].value))
			break;

	ret = gc2093_write_reg(gc2093, 0x00b4, gain_reg_configs[i].analog_gain >> 8);
	ret |= gc2093_write_reg(gc2093, 0x00b3, gain_reg_configs[i].analog_gain & 0xff);
	ret |= gc2093_write_reg(gc2093, 0x00b8, gain_reg_configs[i].col_gain >> 8);
	ret |= gc2093_write_reg(gc2093, 0x00b9, gain_reg_configs[i].col_gain & 0xff);
	ret |= gc2093_write_reg(gc2093, 0x0078, gain_reg_configs[i].dacin);

	pre_gain = 64 * gain / gain_reg_configs[i].value;

	ret |= gc2093_write_reg(gc2093, 0x00b1, (pre_gain >> 6));
	ret |= gc2093_write_reg(gc2093, 0x00b2, ((pre_gain & 0x3f) << 2));

	return ret;
}

static int gc2093_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc2093 *gc2093 = container_of(ctrl->handler,
					     struct gc2093, ctrl_handler);
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = gc2093->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(gc2093->exposure,
					 gc2093->exposure->minimum, max,
					 gc2093->exposure->step,
					 gc2093->exposure->default_value);
		break;
	}
	if (pm_runtime_get(gc2093->dev) <= 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = gc2093_write_reg(gc2093, GC2093_REG_EXP_H,
					   (ctrl->val >> 8) & 0x3f);
		ret |= gc2093_write_reg(gc2093, GC2093_REG_EXP_L,
					   ctrl->val & 0xff);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		gc2093_set_gain(gc2093, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		/* The exposure goes up and reduces the frame rate, no need to write vb */
		break;
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			regmap_update_bits(gc2093->regmap, GC2093_MIRROR_FLIP_REG,
					   MIRROR_MASK, MIRROR_MASK);
		else
			regmap_update_bits(gc2093->regmap, GC2093_MIRROR_FLIP_REG,
					   MIRROR_MASK, 0);
		break;
	case V4L2_CID_VFLIP:
		if (ctrl->val)
			regmap_update_bits(gc2093->regmap, GC2093_MIRROR_FLIP_REG,
					   FLIP_MASK, FLIP_MASK);
		else
			regmap_update_bits(gc2093->regmap, GC2093_MIRROR_FLIP_REG,
					   FLIP_MASK, 0);
		break;
	default:
		dev_warn(gc2093->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(gc2093->dev);
	return ret;
}

static const struct v4l2_ctrl_ops gc2093_ctrl_ops = {
	.s_ctrl = gc2093_set_ctrl,
};

static int gc2093_get_regulators(struct gc2093 *gc2093)
{
	unsigned int i;

	for (i = 0; i < GC2093_NUM_SUPPLIES; i++)
		gc2093->supplies[i].supply = gc2093_supply_names[i];

	return devm_regulator_bulk_get(gc2093->dev,
					   GC2093_NUM_SUPPLIES,
					   gc2093->supplies);
}

static int gc2093_initialize_controls(struct gc2093 *gc2093)
{
	const struct gc2093_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &gc2093->ctrl_handler;
	mode = gc2093->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &gc2093->lock;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
					  0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	gc2093->pixel_rate = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, GC2093_PIXEL_RATE, 1, GC2093_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	gc2093->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (gc2093->hblank)
		gc2093->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc2093->vblank = v4l2_ctrl_new_std(handler, &gc2093_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				GC2093_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	gc2093->exposure = v4l2_ctrl_new_std(handler, &gc2093_ctrl_ops,
				V4L2_CID_EXPOSURE, GC2093_EXPOSURE_MIN,
				exposure_max, GC2093_EXPOSURE_STEP,
				mode->exp_def);

	gc2093->anal_gain = v4l2_ctrl_new_std(handler, &gc2093_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, GC2093_GAIN_MIN,
				GC2093_GAIN_MAX, GC2093_GAIN_STEP,
				GC2093_GAIN_DEFAULT);

	gc2093->h_flip = v4l2_ctrl_new_std(handler, &gc2093_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	gc2093->v_flip = v4l2_ctrl_new_std(handler, &gc2093_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	gc2093->flip = 0;

	if (handler->error) {
		ret = handler->error;
		dev_err(gc2093->dev, "Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc2093->subdev.ctrl_handler = handler;
	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);
	return ret;
}

static int __gc2093_power_on(struct gc2093 *gc2093)
{
	int ret;
	struct device *dev = gc2093->dev;

	ret = clk_set_rate(gc2093->xvclk, GC2093_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate\n");

	if (clk_get_rate(gc2093->xvclk) != GC2093_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 27MHz\n");

	ret = clk_prepare_enable(gc2093->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	ret = regulator_bulk_enable(GC2093_NUM_SUPPLIES, gc2093->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(gc2093->reset_gpio))
		gpiod_set_value_cansleep(gc2093->reset_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR(gc2093->pwdn_gpio))
		gpiod_set_value_cansleep(gc2093->pwdn_gpio, 1);
	if (!IS_ERR(gc2093->reset_gpio))
		gpiod_set_value_cansleep(gc2093->reset_gpio, 0);

	usleep_range(10000, 20000);

	return 0;

disable_clk:
	clk_disable_unprepare(gc2093->xvclk);
	return ret;
}

static void __gc2093_power_off(struct gc2093 *gc2093)
{
	if (!IS_ERR(gc2093->reset_gpio))
		gpiod_set_value_cansleep(gc2093->reset_gpio, 1);
	if (!IS_ERR(gc2093->pwdn_gpio))
		gpiod_set_value_cansleep(gc2093->pwdn_gpio, 0);

	regulator_bulk_disable(GC2093_NUM_SUPPLIES, gc2093->supplies);
	clk_disable_unprepare(gc2093->xvclk);
}

static int gc2093_check_sensor_id(struct gc2093 *gc2093)
{
	u8 id_h = 0, id_l = 0;
	u16 id = 0;
	int ret = 0;

	ret = gc2093_read_reg(gc2093, GC2093_REG_CHIP_ID_H, &id_h);
	ret |= gc2093_read_reg(gc2093, GC2093_REG_CHIP_ID_L, &id_l);
	if (ret) {
		dev_err(gc2093->dev, "Failed to read sensor id, (%d)\n", ret);
		return ret;
	}

	id = id_h << 8 | id_l;
	if (id != GC2093_CHIP_ID) {
		dev_err(gc2093->dev, "sensor id: %04X mismatched\n", id);
		return -ENODEV;
	}

	dev_info(gc2093->dev, "Detected GC2093 sensor\n");
	return 0;
}

static int __gc2093_start_stream(struct gc2093 *gc2093)
{
	int ret;

	ret = regmap_multi_reg_write(gc2093->regmap,
				     gc2093->cur_mode->reg_list,
				     gc2093->cur_mode->reg_num);
	if (ret)
		return ret;

	/* Apply customized control from user */
	mutex_unlock(&gc2093->lock);
	v4l2_ctrl_handler_setup(&gc2093->ctrl_handler);
	mutex_lock(&gc2093->lock);

	return gc2093_write_reg(gc2093, GC2093_REG_CTRL_MODE,
				GC2093_MODE_STREAMING);
}

static int __gc2093_stop_stream(struct gc2093 *gc2093)
{
	return gc2093_write_reg(gc2093, GC2093_REG_CTRL_MODE,
				GC2093_MODE_SW_STANDBY);
}

static void gc2093_get_module_inf(struct gc2093 *gc2093,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.lens, gc2093->len_name, sizeof(inf->base.lens));
	strlcpy(inf->base.sensor, GC2093_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, gc2093->module_name, sizeof(inf->base.module));
}

static long gc2093_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc2093 *gc2093 = to_gc2093(sd);
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_HDR_CFG:
	case RKMODULE_SET_HDR_CFG:
	case RKMODULE_SET_CONVERSION_GAIN:
		break;
	case RKMODULE_GET_MODULE_INFO:
		gc2093_get_module_inf(gc2093, (struct rkmodule_inf *)arg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

#ifdef CONFIG_COMPAT
static long gc2093_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_hdr_cfg *hdr;
	long ret = 0;
	u32 cg = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc2093_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
		kfree(inf);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc2093_ioctl(sd, cmd, hdr);
		if (!ret)
			ret = copy_to_user(up, hdr, sizeof(*hdr));
		kfree(hdr);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(hdr, up, sizeof(*hdr));
		if (!ret)
			ret = gc2093_ioctl(sd, cmd, hdr);
		kfree(hdr);
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		ret = copy_from_user(&cg, up, sizeof(cg));
		if (!ret)
			ret = gc2093_ioctl(sd, cmd, &cg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}
#endif

static int gc2093_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc2093 *gc2093 = to_gc2093(sd);
	int ret = 0;

	mutex_lock(&gc2093->lock);
	on = !!on;
	if (on == gc2093->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(gc2093->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(gc2093->dev);
			goto unlock_and_return;
		}

		ret = __gc2093_start_stream(gc2093);
		if (ret) {
			dev_err(gc2093->dev, "Failed to start gc2093 stream\n");
			pm_runtime_put(gc2093->dev);
			goto unlock_and_return;
		}
	} else {
		__gc2093_stop_stream(gc2093);
		pm_runtime_put(gc2093->dev);
	}

	gc2093->streaming = on;

unlock_and_return:
	mutex_unlock(&gc2093->lock);
	return 0;
}

static int gc2093_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct gc2093 *gc2093 = to_gc2093(sd);
	const struct gc2093_mode *mode = gc2093->cur_mode;

	mutex_lock(&gc2093->lock);
	fi->interval = mode->max_fps;
	mutex_unlock(&gc2093->lock);

	return 0;
}

static int gc2093_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	struct gc2093 *gc2093 = to_gc2093(sd);
	const struct gc2093_mode *mode = gc2093->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (GC2093_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	config->type = V4L2_MBUS_CSI2;
	config->flags = val;
	return 0;
}
static int gc2093_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = GC2093_MEDIA_BUS_FMT;
	return 0;
}

static int gc2093_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct gc2093 *gc2093 = to_gc2093(sd);

	if (fse->index >= gc2093->cfg_num)
		return -EINVAL;

	if (fse->code != GC2093_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;
	return 0;
}

static int gc2093_enum_frame_interval(struct v4l2_subdev *sd,
						  struct v4l2_subdev_pad_config *cfg,
						  struct v4l2_subdev_frame_interval_enum *fie)
{
	struct gc2093 *gc2093 = to_gc2093(sd);

	if (fie->index >= gc2093->cfg_num)
		return -EINVAL;

	fie->code = GC2093_MEDIA_BUS_FMT;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static int gc2093_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc2093 *gc2093 = to_gc2093(sd);
	const struct gc2093_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&gc2093->lock);

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	fmt->format.code = GC2093_MEDIA_BUS_FMT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc2093->lock);
		return -ENOTTY;
#endif
	} else {
		gc2093->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(gc2093->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(gc2093->vblank, vblank_def,
					 GC2093_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&gc2093->lock);
	return 0;
}

static int gc2093_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc2093 *gc2093 = to_gc2093(sd);
	const struct gc2093_mode *mode = gc2093->cur_mode;

	mutex_lock(&gc2093->lock);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc2093->lock);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = GC2093_MEDIA_BUS_FMT;
		fmt->format.field = V4L2_FIELD_NONE;

		/* format info: width/height/data type/virctual channel */
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];

	}
	mutex_unlock(&gc2093->lock);
	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc2093_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc2093 *gc2093 = to_gc2093(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc2093_mode *def_mode = &supported_modes[0];

	mutex_lock(&gc2093->lock);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = GC2093_MEDIA_BUS_FMT;
	try_fmt->field = V4L2_FIELD_NONE;
	mutex_unlock(&gc2093->lock);

	return 0;
}
#endif

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc2093_internal_ops = {
	.open = gc2093_open,
};
#endif

static const struct v4l2_subdev_core_ops gc2093_core_ops = {
	.ioctl = gc2093_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc2093_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc2093_video_ops = {
	.s_stream = gc2093_s_stream,
	.g_frame_interval = gc2093_g_frame_interval,
	.g_mbus_config = gc2093_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops gc2093_pad_ops = {
	.enum_mbus_code = gc2093_enum_mbus_code,
	.enum_frame_size = gc2093_enum_frame_sizes,
	.enum_frame_interval = gc2093_enum_frame_interval,
	.get_fmt = gc2093_get_fmt,
	.set_fmt = gc2093_set_fmt,
};

static const struct v4l2_subdev_ops gc2093_subdev_ops = {
	.core   = &gc2093_core_ops,
	.video  = &gc2093_video_ops,
	.pad    = &gc2093_pad_ops,
};

static int gc2093_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2093 *gc2093 = to_gc2093(sd);

	__gc2093_power_on(gc2093);
	return 0;
}

static int gc2093_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2093 *gc2093 = to_gc2093(sd);

	__gc2093_power_off(gc2093);
	return 0;
}

static const struct dev_pm_ops gc2093_pm_ops = {
	SET_RUNTIME_PM_OPS(gc2093_runtime_suspend,
			   gc2093_runtime_resume, NULL)
};

static int gc2093_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gc2093 *gc2093;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	gc2093 = devm_kzalloc(dev, sizeof(*gc2093), GFP_KERNEL);
	if (!gc2093)
		return -ENOMEM;

	gc2093->dev = dev;
	gc2093->regmap = devm_regmap_init_i2c(client, &gc2093_regmap_config);
	if (IS_ERR(gc2093->regmap)) {
		dev_err(dev, "Failed to initialize I2C\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &gc2093->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &gc2093->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &gc2093->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &gc2093->len_name);
	if (ret) {
		dev_err(dev, "Failed to get module information\n");
		return -EINVAL;
	}

	gc2093->xvclk = devm_clk_get(gc2093->dev, "xvclk");
	if (IS_ERR(gc2093->xvclk)) {
		dev_err(gc2093->dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc2093->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc2093->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc2093->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_HIGH);
	if (IS_ERR(gc2093->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = gc2093_get_regulators(gc2093);
	if (ret) {
		dev_err(dev, "Failed to get regulators\n");
		return ret;
	}

	mutex_init(&gc2093->lock);

	/* set default mode */
	gc2093->cur_mode = &supported_modes[0];
	gc2093->cfg_num = ARRAY_SIZE(supported_modes);

	sd = &gc2093->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc2093_subdev_ops);
	ret = gc2093_initialize_controls(gc2093);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc2093_power_on(gc2093);
	if (ret)
		goto err_free_handler;

	ret = gc2093_check_sensor_id(gc2093);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc2093_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif

#ifdef CONFIG_MEDIA_CONTROLLER
	gc2093->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc2093->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc2093->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc2093->module_index, facing,
		 GC2093_NAME, dev_name(sd->dev));

	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "Failed to register v4l2 async subdev\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#ifdef CONFIG_MEDIA_CONTROLLER
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__gc2093_power_off(gc2093);
err_free_handler:
	v4l2_ctrl_handler_free(&gc2093->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc2093->lock);

	return ret;
}

static int gc2093_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2093 *gc2093 = to_gc2093(sd);

	v4l2_async_unregister_subdev(sd);
#ifdef CONFIG_MEDIA_CONTROLLER
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc2093->ctrl_handler);
	mutex_destroy(&gc2093->lock);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc2093_power_off(gc2093);
	pm_runtime_set_suspended(&client->dev);
	return 0;
}

static const struct i2c_device_id gc2093_match_id[] = {
	{ "gc2093", 0 },
	{ },
};

static const struct of_device_id gc2093_of_match[] = {
	{ .compatible = "galaxycore,gc2093" },
	{},
};
MODULE_DEVICE_TABLE(of, gc2093_of_match);

static struct i2c_driver gc2093_i2c_driver = {
	.driver = {
		.name = GC2093_NAME,
		.pm = &gc2093_pm_ops,
		.of_match_table = of_match_ptr(gc2093_of_match),
	},
	.probe      = &gc2093_probe,
	.remove     = &gc2093_remove,
	.id_table   = gc2093_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc2093_i2c_driver);
}
static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc2093_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Galaxycore GC2093 Image Sensor driver");
MODULE_LICENSE("GPL v2");

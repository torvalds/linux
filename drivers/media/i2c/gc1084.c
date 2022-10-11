// SPDX-License-Identifier: GPL-2.0
/*
 * gc1084 sensor driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 Add HDR support.
 * V0.0X01.0X02 update sensor driver
 * 1. fix linear mode ae flicker issue.
 * 2. add hdr mode exposure limit issue.
 * 3. fix hdr mode highlighting pink issue.
 * 4. add some debug info.
 */
//#define DEBUG
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
#include <linux/rk-preisp.h>

#include <media/v4l2-async.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define DRIVER_VERSION		KERNEL_VERSION(0, 0x01, 0x02)
#define GC1084_NAME		"gc1084"
#define GC1084_MEDIA_BUS_FMT	MEDIA_BUS_FMT_SGRBG10_1X10

#define MIPI_FREQ_400M		400000000

#define GC1084_XVCLK_FREQ	27000000

#define GC1084_REG_CHIP_ID_H	0x03F0
#define GC1084_REG_CHIP_ID_L	0x03F1

#define GC1084_REG_EXP_H	0x0d03
#define GC1084_REG_EXP_L	0x0d04

#define GC1084_REG_VTS_H	0x0000
#define GC1084_REG_VTS_L	0x0001

#define GC1084_REG_CTRL_MODE	0x003E
#define GC1084_MODE_SW_STANDBY	0x11
#define GC1084_MODE_STREAMING	0x91

#define GC1084_CHIP_ID		0x1084

#define GC1084_VTS_MAX		0x3FFF
#define GC1084_HTS_MAX		0xFFF

#define GC1084_EXPOSURE_MAX	0x3FFF
#define GC1084_EXPOSURE_MIN	1
#define GC1084_EXPOSURE_STEP	1

#define GC1084_GAIN_MIN		0x40
#define GC1084_GAIN_MAX		0x2000
#define GC1084_GAIN_STEP	1
#define GC1084_GAIN_DEFAULT	64
#define REG_NULL		0xFFFF

#define GC1084_LANES		1

static const char * const gc1084_supply_names[] = {
	"dovdd",    /* Digital I/O power */
	"avdd",     /* Analog power */
	"dvdd",     /* Digital power */
};

#define GC1084_NUM_SUPPLIES ARRAY_SIZE(gc1084_supply_names)

#define to_gc1084(sd) container_of(sd, struct gc1084, subdev)

enum {
	LINK_FREQ_400M_INDEX,
};

struct gain_reg_config {
	u32 value;
	u16 analog_gain;
	u16 col_gain;
	u16 reserved;
};

struct gc1084_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 link_freq_index;
	const struct reg_sequence *reg_list;
	u32 reg_num;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct gc1084 {
	struct device	*dev;
	struct clk	*xvclk;
	struct regmap	*regmap;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	struct regulator_bulk_data supplies[GC1084_NUM_SUPPLIES];

	struct v4l2_subdev  subdev;
	struct media_pad    pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl    *exposure;
	struct v4l2_ctrl    *anal_gain;
	struct v4l2_ctrl    *hblank;
	struct v4l2_ctrl    *vblank;
	struct v4l2_ctrl    *h_flip;
	struct v4l2_ctrl    *v_flip;
	struct v4l2_ctrl    *link_freq;
	struct v4l2_ctrl    *pixel_rate;

	struct mutex        lock;
	bool		    streaming;
	bool		    power_on;
	unsigned int        cfg_num;
	const struct gc1084_mode *cur_mode;

	u32		module_index;
	const char      *module_facing;
	const char      *module_name;
	const char      *len_name;
	u32		cur_vts;

	bool			  has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
};

static const struct regmap_config gc1084_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x1000,
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ_400M,
};

/*
 * window size=1280*720 mipi@1lane
 * mclk=27M mipi_clk=400Mbps
 * pixel_line_total=2200 line_frame_total=1125
 * row_time=44.4444us frame_rate=30fps
 */
static const struct reg_sequence gc1084_1280x720_liner_settings[] = {
	{0x03fe, 0xf0},
	{0x03fe, 0xf0},
	{0x03fe, 0xf0},
	{0x03fe, 0x00},
	{0x03f2, 0x00},
	{0x03f3, 0x00},
	{0x03f4, 0x36},
	{0x03f5, 0xc0},
	{0x03f6, 0x13},
	{0x03f7, 0x01},
	{0x03f8, 0x32},
	{0x03f9, 0x21},
	{0x03fc, 0xae},
	{0x0d05, 0x08},
	{0x0d06, 0xae},
	{0x0d08, 0x10},
	{0x0d0a, 0x02},
	{0x000c, 0x03},
	{0x0d0d, 0x02},
	{0x0d0e, 0xd4},
	{0x000f, 0x05},
	{0x0010, 0x08},
	{0x0017, 0x08},
	{0x0d73, 0x92},
	{0x0076, 0x00},
	{0x0d76, 0x00},
	{0x0d41, 0x02},
	{0x0d42, 0xee},
	{0x0d7a, 0x0a},
	{0x006b, 0x18},
	{0x0db0, 0x9d},
	{0x0db1, 0x00},
	{0x0db2, 0xac},
	{0x0db3, 0xd5},
	{0x0db4, 0x00},
	{0x0db5, 0x97},
	{0x0db6, 0x09},
	{0x00d2, 0xfc},
	{0x0d19, 0x31},
	{0x0d20, 0x40},
	{0x0d25, 0xcb},
	{0x0d27, 0x03},
	{0x0d29, 0x40},
	{0x0d43, 0x20},
	{0x0058, 0x60},
	{0x00d6, 0x66},
	{0x00d7, 0x19},
	{0x0093, 0x02},
	{0x00d9, 0x14},
	{0x00da, 0xc1},
	{0x0d2a, 0x00},
	{0x0d28, 0x04},
	{0x0dc2, 0x84},
	{0x0050, 0x30},
	{0x0080, 0x07},
	{0x008c, 0x05},
	{0x008d, 0xa8},
	{0x0077, 0x01},
	{0x0078, 0xee},
	{0x0079, 0x02},
	{0x0067, 0xc0},
	{0x0054, 0xff},
	{0x0055, 0x02},
	{0x0056, 0x00},
	{0x0057, 0x04},
	{0x005a, 0xff},
	{0x005b, 0x07},
	{0x00d5, 0x03},
	{0x0102, 0xa9},
	{0x0d03, 0x02},
	{0x0d04, 0xd0},
	{0x007a, 0x60},
	{0x04e0, 0xff},
	{0x0414, 0x75},
	{0x0415, 0x75},
	{0x0416, 0x75},
	{0x0417, 0x75},
	{0x0122, 0x00},
	{0x0121, 0x80},
	{0x0428, 0x10},
	{0x0429, 0x10},
	{0x042a, 0x10},
	{0x042b, 0x10},
	{0x042c, 0x14},
	{0x042d, 0x14},
	{0x042e, 0x18},
	{0x042f, 0x18},
	{0x0430, 0x05},
	{0x0431, 0x05},
	{0x0432, 0x05},
	{0x0433, 0x05},
	{0x0434, 0x05},
	{0x0435, 0x05},
	{0x0436, 0x05},
	{0x0437, 0x05},
	{0x0153, 0x00},
	{0x0190, 0x01},
	{0x0192, 0x02},
	{0x0194, 0x04},
	{0x0195, 0x02},
	{0x0196, 0xd0},
	{0x0197, 0x05},
	{0x0198, 0x00},
	{0x0201, 0x23},
	{0x0202, 0x53},
	{0x0203, 0xce},
	{0x0208, 0x39},
	{0x0212, 0x06},
	{0x0213, 0x40},
	{0x0215, 0x12},
	{0x0229, 0x05},
	{0x023e, 0x98},
	{0x031e, 0x3e},
};

static const struct gc1084_mode supported_modes[] = {
	{
		.width = 1280,
		.height = 720,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x460,
		.hts_def = 0x898,
		.vts_def = 0x465,
		.link_freq_index = LINK_FREQ_400M_INDEX,
		.reg_list = gc1084_1280x720_liner_settings,
		.reg_num = ARRAY_SIZE(gc1084_1280x720_liner_settings),
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
/* * 2, to match suitable isp freq */
static u64 to_pixel_rate(u32 index)
{
	u64 pixel_rate = link_freq_menu_items[index] * 2 * GC1084_LANES * 2;

	do_div(pixel_rate, 10);

	return pixel_rate;
}

static inline int gc1084_read_reg(struct gc1084 *gc1084, u16 addr, u8 *value)
{
	unsigned int val;
	int ret;

	ret = regmap_read(gc1084->regmap, addr, &val);
	if (ret) {
		dev_err(gc1084->dev, "i2c read failed at addr: %x\n", addr);
		return ret;
	}

	*value = val & 0xff;

	return 0;
}

static inline int gc1084_write_reg(struct gc1084 *gc1084, u16 addr, u8 value)
{
	int ret;

	ret = regmap_write(gc1084->regmap, addr, value);
	if (ret) {
		dev_err(gc1084->dev, "i2c write failed at addr: %x\n", addr);
		return ret;
	}

	return ret;
}

static const struct gain_reg_config gain_reg_configs[] = {
	{  64, 0x0000, 0x0100, 0x0080},
	{  76, 0x0a00, 0x010b, 0x0080},
	{  90, 0x0001, 0x0119, 0x0080},
	{ 106, 0x0a01, 0x012a, 0x0080},
	{ 128, 0x0002, 0x0200, 0x0080},
	{ 152, 0x0a02, 0x0217, 0x0080},
	{ 179, 0x0003, 0x0233, 0x0080},
	{ 212, 0x0a03, 0x0314, 0x0080},
	{ 256, 0x0004, 0x0400, 0x0090},
	{ 303, 0x0a04, 0x042f, 0x0090},
	{ 358, 0x0005, 0x0526, 0x0090},
	{ 425, 0x0a05, 0x0628, 0x0090},
	{ 512, 0x0006, 0x0800, 0x00a0},
	{ 607, 0x0a06, 0x091e, 0x00a0},
	{ 716, 0x1246, 0x0b0c, 0x00a0},
	{ 848, 0x1966, 0x0d10, 0x00a0},
	{1024, 0x4004, 0x1000, 0x00a0},
	{1214, 0x4a04, 0x123d, 0x00a0},
	{1434, 0x4005, 0x1619, 0x00b0},
	{1699, 0x4a05, 0x1a23, 0x00c0},
	{2048, 0x4006, 0x2000, 0x00c0},
	{2427, 0x4a06, 0x253b, 0x00c0},
	{2865, 0x5246, 0x2c30, 0x00c0},
	{3393, 0x5946, 0x3501, 0x00d0},
	{4096, 0x6006, 0x3f3f, 0x00e0},
};

static int gc1084_set_gain(struct gc1084 *gc1084, u32 gain)
{
	int ret, i = 0;
	u16 pre_gain = 0;

	for (i = 0; i < ARRAY_SIZE(gain_reg_configs) - 1; i++)
		if ((gain_reg_configs[i].value <= gain) && (gain < gain_reg_configs[i+1].value))
			break;

	ret = gc1084_write_reg(gc1084, 0x00d1, (gain_reg_configs[i].analog_gain >> 8) & 0x3f);
	ret |= gc1084_write_reg(gc1084, 0x00d0, gain_reg_configs[i].analog_gain & 0xff);

	ret |= gc1084_write_reg(gc1084, 0x031d, 0x2e);

	ret |= gc1084_write_reg(gc1084, 0x0dc1, (gain_reg_configs[i].analog_gain >> 14) & 1);

	ret |= gc1084_write_reg(gc1084, 0x031d, 0x28);

	ret |= gc1084_write_reg(gc1084, 0x0155, gain_reg_configs[i].reserved & 0xff);

	ret |= gc1084_write_reg(gc1084, 0x00b8, gain_reg_configs[i].col_gain >> 8);
	ret |= gc1084_write_reg(gc1084, 0x00b9, gain_reg_configs[i].col_gain & 0xff);

	pre_gain = 64 * gain / gain_reg_configs[i].value;

	ret |= gc1084_write_reg(gc1084, 0x00b1, (pre_gain >> 6));
	ret |= gc1084_write_reg(gc1084, 0x00b2, ((pre_gain & 0x3f) << 2));

	return ret;
}

static int gc1084_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc1084 *gc1084 = container_of(ctrl->handler,
					     struct gc1084, ctrl_handler);
	s64 max;
	int ret = 0;
	u32 vts = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = gc1084->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(gc1084->exposure,
					 gc1084->exposure->minimum, max,
					 gc1084->exposure->step,
					 gc1084->exposure->default_value);
		break;
	}
	if (!pm_runtime_get_if_in_use(gc1084->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (gc1084->cur_mode->hdr_mode != NO_HDR)
			goto ctrl_end;
		dev_dbg(gc1084->dev, "set exposure value 0x%x\n", ctrl->val);
		ret = gc1084_write_reg(gc1084, GC1084_REG_EXP_H,
				       (ctrl->val >> 8) & 0x3f);
		ret |= gc1084_write_reg(gc1084, GC1084_REG_EXP_L,
					ctrl->val & 0xff);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (gc1084->cur_mode->hdr_mode != NO_HDR)
			goto ctrl_end;
		dev_dbg(gc1084->dev, "set gain value 0x%x\n", ctrl->val);
		gc1084_set_gain(gc1084, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		vts = gc1084->cur_mode->height + ctrl->val;
		gc1084->cur_vts = vts;
		ret = gc1084_write_reg(gc1084, GC1084_REG_VTS_H,
				       (vts >> 8) & 0x3f);
		ret |= gc1084_write_reg(gc1084, GC1084_REG_VTS_L,
					vts & 0xff);
		dev_dbg(gc1084->dev, " set blank value 0x%x\n", ctrl->val);
		break;
	default:
		dev_warn(gc1084->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

ctrl_end:
	pm_runtime_put(gc1084->dev);
	return ret;
}

static const struct v4l2_ctrl_ops gc1084_ctrl_ops = {
	.s_ctrl = gc1084_set_ctrl,
};

static int gc1084_get_regulators(struct gc1084 *gc1084)
{
	unsigned int i;

	for (i = 0; i < GC1084_NUM_SUPPLIES; i++)
		gc1084->supplies[i].supply = gc1084_supply_names[i];

	return devm_regulator_bulk_get(gc1084->dev,
				       GC1084_NUM_SUPPLIES,
				       gc1084->supplies);
}

static int gc1084_initialize_controls(struct gc1084 *gc1084)
{
	const struct gc1084_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &gc1084->ctrl_handler;
	mode = gc1084->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &gc1084->lock;

	gc1084->link_freq = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
						   ARRAY_SIZE(link_freq_menu_items) - 1, 0,
						   link_freq_menu_items);

	gc1084->pixel_rate = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
					       0, to_pixel_rate(LINK_FREQ_400M_INDEX),
					       1, to_pixel_rate(LINK_FREQ_400M_INDEX));

	h_blank = mode->hts_def - mode->width;
	gc1084->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					   h_blank, h_blank, 1, h_blank);
	if (gc1084->hblank)
		gc1084->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc1084->cur_vts = mode->vts_def;
	gc1084->vblank = v4l2_ctrl_new_std(handler, &gc1084_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_def,
					   GC1084_VTS_MAX - mode->height,
					   1, vblank_def);

	exposure_max = mode->vts_def - 4;
	gc1084->exposure = v4l2_ctrl_new_std(handler, &gc1084_ctrl_ops,
					     V4L2_CID_EXPOSURE, GC1084_EXPOSURE_MIN,
					     exposure_max, GC1084_EXPOSURE_STEP,
					     mode->exp_def);

	gc1084->anal_gain = v4l2_ctrl_new_std(handler, &gc1084_ctrl_ops,
					      V4L2_CID_ANALOGUE_GAIN, GC1084_GAIN_MIN,
					      GC1084_GAIN_MAX, GC1084_GAIN_STEP,
					      GC1084_GAIN_DEFAULT);

	gc1084->h_flip = v4l2_ctrl_new_std(handler, &gc1084_ctrl_ops,
					   V4L2_CID_HFLIP, 0, 1, 1, 0);

	gc1084->v_flip = v4l2_ctrl_new_std(handler, &gc1084_ctrl_ops,
					   V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(gc1084->dev, "Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc1084->subdev.ctrl_handler = handler;
	gc1084->has_init_exp = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);
	return ret;
}

static int __gc1084_power_on(struct gc1084 *gc1084)
{
	int ret;
	struct device *dev = gc1084->dev;

	ret = clk_set_rate(gc1084->xvclk, GC1084_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate\n");

	if (clk_get_rate(gc1084->xvclk) != GC1084_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 27MHz\n");

	ret = clk_prepare_enable(gc1084->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	ret = regulator_bulk_enable(GC1084_NUM_SUPPLIES, gc1084->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(gc1084->reset_gpio))
		gpiod_set_value_cansleep(gc1084->reset_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR(gc1084->pwdn_gpio))
		gpiod_set_value_cansleep(gc1084->pwdn_gpio, 1);
	if (!IS_ERR(gc1084->reset_gpio))
		gpiod_set_value_cansleep(gc1084->reset_gpio, 0);

	usleep_range(10000, 20000);

	return 0;

disable_clk:
	clk_disable_unprepare(gc1084->xvclk);
	return ret;
}

static void __gc1084_power_off(struct gc1084 *gc1084)
{
	if (!IS_ERR(gc1084->reset_gpio))
		gpiod_set_value_cansleep(gc1084->reset_gpio, 1);
	if (!IS_ERR(gc1084->pwdn_gpio))
		gpiod_set_value_cansleep(gc1084->pwdn_gpio, 0);

	regulator_bulk_disable(GC1084_NUM_SUPPLIES, gc1084->supplies);
	clk_disable_unprepare(gc1084->xvclk);
}

static int gc1084_check_sensor_id(struct gc1084 *gc1084)
{
	u8 id_h = 0, id_l = 0;
	u16 id = 0;
	int ret = 0;

	ret = gc1084_read_reg(gc1084, GC1084_REG_CHIP_ID_H, &id_h);
	ret |= gc1084_read_reg(gc1084, GC1084_REG_CHIP_ID_L, &id_l);
	if (ret) {
		dev_err(gc1084->dev, "Failed to read sensor id, (%d)\n", ret);
		return ret;
	}

	id = id_h << 8 | id_l;
	if (id != GC1084_CHIP_ID) {
		dev_err(gc1084->dev, "sensor id: %04X mismatched\n", id);
		return -ENODEV;
	}

	dev_info(gc1084->dev, "Detected GC1084 sensor\n");
	return 0;
}

static void gc1084_get_module_inf(struct gc1084 *gc1084,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.lens, gc1084->len_name, sizeof(inf->base.lens));
	strlcpy(inf->base.sensor, GC1084_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, gc1084->module_name, sizeof(inf->base.module));
}

static long gc1084_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc1084 *gc1084 = to_gc1084(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	long ret = 0;
	u32 stream = 0;
	u64 delay_us = 0;
	u32 fps = 0;

	switch (cmd) {
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = gc1084->cur_mode->hdr_mode;
		break;
	case RKMODULE_GET_MODULE_INFO:
		gc1084_get_module_inf(gc1084, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream) {
			ret = gc1084_write_reg(gc1084, GC1084_REG_CTRL_MODE,
				GC1084_MODE_STREAMING);
		} else {
			ret = gc1084_write_reg(gc1084, GC1084_REG_CTRL_MODE,
				GC1084_MODE_SW_STANDBY);
			fps = gc1084->cur_mode->max_fps.denominator /
				  gc1084->cur_mode->max_fps.numerator;
			delay_us = 1000000 / (gc1084->cur_mode->vts_def * fps / gc1084->cur_vts);
			usleep_range(delay_us, delay_us + 2000);
		}
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

static int __gc1084_start_stream(struct gc1084 *gc1084)
{
	int ret;

	ret = regmap_multi_reg_write(gc1084->regmap,
				     gc1084->cur_mode->reg_list,
				     gc1084->cur_mode->reg_num);
	if (ret)
		return ret;

	/* Apply customized control from user */
	mutex_unlock(&gc1084->lock);
	v4l2_ctrl_handler_setup(&gc1084->ctrl_handler);
	mutex_lock(&gc1084->lock);

	if (gc1084->has_init_exp && gc1084->cur_mode->hdr_mode != NO_HDR) {
		ret = gc1084_ioctl(&gc1084->subdev, PREISP_CMD_SET_HDRAE_EXP,
				   &gc1084->init_hdrae_exp);
		if (ret) {
			dev_err(gc1084->dev, "init exp fail in hdr mode\n");
			return ret;
		}
	}

	return gc1084_write_reg(gc1084, GC1084_REG_CTRL_MODE,
				GC1084_MODE_STREAMING);
}

static int __gc1084_stop_stream(struct gc1084 *gc1084)
{
	gc1084->has_init_exp = false;
	return gc1084_write_reg(gc1084, GC1084_REG_CTRL_MODE,
				GC1084_MODE_SW_STANDBY);
}

#ifdef CONFIG_COMPAT
static long gc1084_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc1084_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc1084_ioctl(sd, cmd, hdr);
		if (!ret) {
			ret = copy_to_user(up, hdr, sizeof(*hdr));
			if (ret)
				ret = -EFAULT;
		}
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
			ret = gc1084_ioctl(sd, cmd, hdr);
		else
			ret = -EFAULT;
		kfree(hdr);
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		hdrae = kzalloc(sizeof(*hdrae), GFP_KERNEL);
		if (!hdrae) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(hdrae, up, sizeof(*hdrae));
		if (!ret)
			ret = gc1084_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc1084_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}
#endif

static int gc1084_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc1084 *gc1084 = to_gc1084(sd);
	int ret = 0;
	unsigned int fps;
	unsigned int delay_us;

	fps = DIV_ROUND_CLOSEST(gc1084->cur_mode->max_fps.denominator,
					gc1084->cur_mode->max_fps.numerator);

	dev_info(gc1084->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				gc1084->cur_mode->width,
				gc1084->cur_mode->height,
				fps);

	mutex_lock(&gc1084->lock);
	on = !!on;
	if (on == gc1084->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(gc1084->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(gc1084->dev);
			goto unlock_and_return;
		}

		ret = __gc1084_start_stream(gc1084);
		if (ret) {
			dev_err(gc1084->dev, "Failed to start gc1084 stream\n");
			pm_runtime_put(gc1084->dev);
			goto unlock_and_return;
		}
	} else {
		__gc1084_stop_stream(gc1084);
		/* delay to enable oneframe complete */
		delay_us = 1000 * 1000 / fps;
		usleep_range(delay_us, delay_us+10);
		dev_info(gc1084->dev, "%s: on: %d, sleep(%dus)\n",
				__func__, on, delay_us);

		pm_runtime_put(gc1084->dev);
	}

	gc1084->streaming = on;

unlock_and_return:
	mutex_unlock(&gc1084->lock);
	return 0;
}

static int gc1084_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct gc1084 *gc1084 = to_gc1084(sd);
	const struct gc1084_mode *mode = gc1084->cur_mode;

	mutex_lock(&gc1084->lock);
	fi->interval = mode->max_fps;
	mutex_unlock(&gc1084->lock);

	return 0;
}

static int gc1084_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct gc1084 *gc1084 = to_gc1084(sd);
	u32 val = 1 << (GC1084_LANES - 1) | V4L2_MBUS_CSI2_CHANNEL_0 |
		  V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = (gc1084->cur_mode->hdr_mode == NO_HDR) ?
			val : (val | V4L2_MBUS_CSI2_CHANNEL_1);

	return 0;
}

static int gc1084_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = GC1084_MEDIA_BUS_FMT;
	return 0;
}

static int gc1084_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct gc1084 *gc1084 = to_gc1084(sd);

	if (fse->index >= gc1084->cfg_num)
		return -EINVAL;

	if (fse->code != GC1084_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;
	return 0;
}

static int gc1084_enum_frame_interval(struct v4l2_subdev *sd,
						  struct v4l2_subdev_pad_config *cfg,
						  struct v4l2_subdev_frame_interval_enum *fie)
{
	struct gc1084 *gc1084 = to_gc1084(sd);

	if (fie->index >= gc1084->cfg_num)
		return -EINVAL;

	fie->code = GC1084_MEDIA_BUS_FMT;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static int gc1084_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc1084 *gc1084 = to_gc1084(sd);
	const struct gc1084_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&gc1084->lock);

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	fmt->format.code = GC1084_MEDIA_BUS_FMT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc1084->lock);
		return -ENOTTY;
#endif
	} else {
		gc1084->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(gc1084->link_freq, mode->link_freq_index);
		__v4l2_ctrl_s_ctrl_int64(gc1084->pixel_rate,
					 to_pixel_rate(mode->link_freq_index));
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(gc1084->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(gc1084->vblank, vblank_def,
					 GC1084_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&gc1084->lock);
	return 0;
}

static int gc1084_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc1084 *gc1084 = to_gc1084(sd);
	const struct gc1084_mode *mode = gc1084->cur_mode;

	mutex_lock(&gc1084->lock);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc1084->lock);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = GC1084_MEDIA_BUS_FMT;
		fmt->format.field = V4L2_FIELD_NONE;

		/* format info: width/height/data type/virctual channel */
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];

	}
	mutex_unlock(&gc1084->lock);
	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc1084_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc1084 *gc1084 = to_gc1084(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc1084_mode *def_mode = &supported_modes[0];

	mutex_lock(&gc1084->lock);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = GC1084_MEDIA_BUS_FMT;
	try_fmt->field = V4L2_FIELD_NONE;
	mutex_unlock(&gc1084->lock);

	return 0;
}
#endif

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc1084_internal_ops = {
	.open = gc1084_open,
};
#endif

static int gc1084_s_power(struct v4l2_subdev *sd, int on)
{
	struct gc1084 *gc1084 = to_gc1084(sd);
	int ret = 0;

	mutex_lock(&gc1084->lock);

	if (gc1084->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(gc1084->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(gc1084->dev);
			goto unlock_and_return;
		}
		gc1084->power_on = true;
	} else {
		pm_runtime_put(gc1084->dev);
		gc1084->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&gc1084->lock);

	return ret;
}

static const struct v4l2_subdev_core_ops gc1084_core_ops = {
	.s_power = gc1084_s_power,
	.ioctl = gc1084_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc1084_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc1084_video_ops = {
	.s_stream = gc1084_s_stream,
	.g_frame_interval = gc1084_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc1084_pad_ops = {
	.enum_mbus_code = gc1084_enum_mbus_code,
	.enum_frame_size = gc1084_enum_frame_sizes,
	.enum_frame_interval = gc1084_enum_frame_interval,
	.get_fmt = gc1084_get_fmt,
	.set_fmt = gc1084_set_fmt,
	.get_mbus_config = gc1084_g_mbus_config,
};

static const struct v4l2_subdev_ops gc1084_subdev_ops = {
	.core   = &gc1084_core_ops,
	.video  = &gc1084_video_ops,
	.pad    = &gc1084_pad_ops,
};

static int gc1084_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc1084 *gc1084 = to_gc1084(sd);

	__gc1084_power_on(gc1084);
	return 0;
}

static int gc1084_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc1084 *gc1084 = to_gc1084(sd);

	__gc1084_power_off(gc1084);
	return 0;
}

static const struct dev_pm_ops gc1084_pm_ops = {
	SET_RUNTIME_PM_OPS(gc1084_runtime_suspend,
			   gc1084_runtime_resume, NULL)
};

static int gc1084_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gc1084 *gc1084;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	gc1084 = devm_kzalloc(dev, sizeof(*gc1084), GFP_KERNEL);
	if (!gc1084)
		return -ENOMEM;

	gc1084->dev = dev;
	gc1084->regmap = devm_regmap_init_i2c(client, &gc1084_regmap_config);
	if (IS_ERR(gc1084->regmap)) {
		dev_err(dev, "Failed to initialize I2C\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &gc1084->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &gc1084->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &gc1084->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &gc1084->len_name);
	if (ret) {
		dev_err(dev, "Failed to get module information\n");
		return -EINVAL;
	}

	gc1084->xvclk = devm_clk_get(gc1084->dev, "xvclk");
	if (IS_ERR(gc1084->xvclk)) {
		dev_err(gc1084->dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc1084->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc1084->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc1084->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_HIGH);
	if (IS_ERR(gc1084->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = gc1084_get_regulators(gc1084);
	if (ret) {
		dev_err(dev, "Failed to get regulators\n");
		return ret;
	}

	mutex_init(&gc1084->lock);

	/* set default mode */
	gc1084->cur_mode = &supported_modes[0];
	gc1084->cfg_num = ARRAY_SIZE(supported_modes);
	gc1084->cur_vts = gc1084->cur_mode->vts_def;

	sd = &gc1084->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc1084_subdev_ops);
	ret = gc1084_initialize_controls(gc1084);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc1084_power_on(gc1084);
	if (ret)
		goto err_free_handler;

	ret = gc1084_check_sensor_id(gc1084);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc1084_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif

#ifdef CONFIG_MEDIA_CONTROLLER
	gc1084->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc1084->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc1084->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc1084->module_index, facing,
		 GC1084_NAME, dev_name(sd->dev));

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
	__gc1084_power_off(gc1084);
err_free_handler:
	v4l2_ctrl_handler_free(&gc1084->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc1084->lock);

	return ret;
}

static int gc1084_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc1084 *gc1084 = to_gc1084(sd);

	v4l2_async_unregister_subdev(sd);
#ifdef CONFIG_MEDIA_CONTROLLER
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc1084->ctrl_handler);
	mutex_destroy(&gc1084->lock);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc1084_power_off(gc1084);
	pm_runtime_set_suspended(&client->dev);
	return 0;
}

static const struct i2c_device_id gc1084_match_id[] = {
	{ "gc1084", 0 },
	{ },
};

static const struct of_device_id gc1084_of_match[] = {
	{ .compatible = "galaxycore,gc1084" },
	{},
};
MODULE_DEVICE_TABLE(of, gc1084_of_match);

static struct i2c_driver gc1084_i2c_driver = {
	.driver = {
		.name = GC1084_NAME,
		.pm = &gc1084_pm_ops,
		.of_match_table = of_match_ptr(gc1084_of_match),
	},
	.probe      = &gc1084_probe,
	.remove     = &gc1084_remove,
	.id_table   = gc1084_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc1084_i2c_driver);
}
static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc1084_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Galaxycore GC1084 Image Sensor driver");
MODULE_LICENSE("GPL v2");

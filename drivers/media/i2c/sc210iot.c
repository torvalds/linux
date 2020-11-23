// SPDX-License-Identifier: GPL-2.0
/*
 * sc210iot sensor driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 add quick stream on/off
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/pinctrl/consumer.h>
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
#include "../platform/rockchip/isp/rkisp_tb_helper.h"

#define DRIVER_VERSION		KERNEL_VERSION(0, 0x01, 0x01)

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define SC210IOT_NAME		"sc210iot"
#define SC210IOT_MEDIA_BUS_FMT	MEDIA_BUS_FMT_SBGGR10_1X10
#define MIPI_FREQ		371250000
#define SC210IOT_XVCLK_FREQ	27000000

#define SC210IOT_REG_CHIP_ID_H	0x3108
#define SC210IOT_REG_CHIP_ID_L	0x3107

#define SC210IOT_REG_EXP_LONG_H	0x3e00
#define SC210IOT_REG_EXP_LONG_M	0x3e01
#define SC210IOT_REG_EXP_LONG_L	0x3e02

#define SC210IOT_REG_GAIN_LONG_3	0x3e06
#define SC210IOT_REG_GAIN_LONG_2	0x3e07
#define SC210IOT_REG_GAIN_LONG_1	0x3e08
#define SC210IOT_REG_GAIN_LONG_0	0x3e09

#define SC210IOT_REG_MIRROR_FLIP	0x3221
#define MIRROR_MASK		0x6
#define FLIP_MASK		0x60

#define SC210IOT_REG_CTRL_MODE	0x0100
#define SC210IOT_MODE_SW_STANDBY	0x0
#define SC210IOT_MODE_STREAMING	BIT(0)

#define SC210IOT_CHIP_ID		0x17cb

#define SC210IOT_REG_VTS_H	0x320e
#define SC210IOT_REG_VTS_L	0x320f
#define SC210IOT_VTS_MAX		0x3FFF
#define SC210IOT_HTS_MAX		0xFFF

#define SC210IOT_EXPOSURE_MAX	0x3FFF
#define SC210IOT_EXPOSURE_MIN	1
#define SC210IOT_EXPOSURE_STEP	1

#define SC210IOT_GAIN_MIN		0x40
#define SC210IOT_GAIN_MAX		0x8000
#define SC210IOT_GAIN_STEP		1
#define SC210IOT_GAIN_DEFAULT	64

#define SC210IOT_SOFTWARE_RESET_REG	0x0103

#define SC210IOT_LANES		2

static const char * const sc210iot_supply_names[] = {
	"dovdd",    /* Digital I/O power */
	"avdd",     /* Analog power */
	"dvdd",     /* Digital power */
};

#define SC210IOT_NUM_SUPPLIES ARRAY_SIZE(sc210iot_supply_names)

#define to_sc210iot(sd) container_of(sd, struct sc210iot, subdev)

enum {
	PAD0,
	PAD1,
	PAD2,
	PAD3,
	PAD_MAX,
};

enum {
	LINK_FREQ_INDEX,
};

struct gain_section {
	u16 min_gain;
	u16 max_gain;
	u16 again_regs_start;
	u16 again_regs_stop;
	u16 again_deviation;
	u16 dgain_regs_start;
	u16 dgain_regs_stop;
	u16 dgain_deviation;
	u16 steps;
};

struct sc210iot_mode {
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

struct sc210iot {
	struct device	*dev;
	struct clk	*xvclk;
	struct regmap	*regmap;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	struct regulator_bulk_data supplies[SC210IOT_NUM_SUPPLIES];
	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;
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
	bool		streaming;
	bool		power_on;
	bool		is_thunderboot;
	bool		is_thunderboot_ng;
	bool		is_first_streamoff;
	unsigned int	cfg_num;
	const struct sc210iot_mode *cur_mode;
	u32		module_index;
	const char      *module_facing;
	const char      *module_name;
	const char      *len_name;
	bool		has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
};

static const struct regmap_config sc210iot_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x6f00,
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ,
};

/*
 * window size=1920*1080 mipi@2lane
 * mclk=27M mipi_clk=371.25Mbps
 * pixel_line_total=2200 line_frame_total=1125
 * row_time=29.62us frame_rate=30fps
 */
static const struct reg_sequence sc210iot_1080p_liner_30fps_settings[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x301c, 0x78},
	{0x3208, 0x07},
	{0x3209, 0x80},
	{0x320a, 0x04},
	{0x320b, 0x38},
	{0x320e, 0x04},
	{0x320f, 0x65},
	{0x3214, 0x11},
	{0x3215, 0x11},
	{0x3253, 0x0c},
	{0x3274, 0x09},
	{0x3301, 0x05},
	{0x3304, 0x68},
	{0x3306, 0x40},
	{0x330b, 0xcc},
	{0x331c, 0x01},
	{0x331e, 0x61},
	{0x3333, 0x10},
	{0x3364, 0x17},
	{0x3391, 0x18},
	{0x3392, 0x38},
	{0x3393, 0x08},
	{0x3394, 0x0b},
	{0x3395, 0x50},
	{0x3620, 0x88},
	{0x3622, 0x06},
	{0x3630, 0xf8},
	{0x3634, 0x44},
	{0x3637, 0x16},
	{0x363a, 0x1f},
	{0x3670, 0x1c},
	{0x3677, 0x84},
	{0x3678, 0x86},
	{0x3679, 0x8b},
	{0x367e, 0x18},
	{0x367f, 0x38},
	{0x3690, 0x53},
	{0x3691, 0x63},
	{0x3692, 0x63},
	{0x369c, 0x08},
	{0x369d, 0x38},
	{0x36a4, 0x08},
	{0x36a5, 0x18},
	{0x36a8, 0x08},
	{0x36a9, 0x28},
	{0x36aa, 0x2a},
	{0x36fc, 0x11},
	{0x36fd, 0x14},
	{0x3e01, 0x8c},
	{0x3e03, 0x0b},
	{0x3e08, 0x03},
	{0x3e09, 0x20},
	{0x3e1b, 0x15},
	{0x3f03, 0x01},
	{0x36e9, 0x20},
	{0x36f9, 0x24},
	{0x0100, 0x01},
};

static const struct sc210iot_mode supported_modes[] = {
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
		.link_freq_index = LINK_FREQ_INDEX,
		.reg_list = sc210iot_1080p_liner_30fps_settings,
		.reg_num = ARRAY_SIZE(sc210iot_1080p_liner_30fps_settings),
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
static u64 to_pixel_rate(u32 index)
{
	u64 pixel_rate = link_freq_menu_items[index] * 2 * SC210IOT_LANES;

	do_div(pixel_rate, 10);
	return pixel_rate;
}

static inline int sc210iot_read_reg(struct sc210iot *sc210iot, u16 addr, u8 *value)
{
	unsigned int val;
	int ret;

	ret = regmap_read(sc210iot->regmap, addr, &val);
	if (ret) {
		dev_err(sc210iot->dev, "i2c read failed at addr: %x\n", addr);
		return ret;
	}
	*value = val & 0xff;
	return 0;
}

static int __sc210iot_power_on(struct sc210iot *sc210iot);

static inline int sc210iot_write_reg(struct sc210iot *sc210iot, u16 addr, u8 value)
{
	int ret;

	ret = regmap_write(sc210iot->regmap, addr, value);
	if (ret) {
		dev_err(sc210iot->dev, "i2c write failed at addr: %x\n", addr);
		return ret;
	}
	return ret;
}

static const struct gain_section gain_sections[] = {
	{    64,   128, 0x0320, 0x033f,  64, 0x0080, 0x0080,     0, 32 },
	{   128,   256, 0x0720, 0x073f, 128, 0x0080, 0x0080,     0, 32 },
	{   256,   512, 0x0f20, 0x0f3f, 256, 0x0080, 0x0080,     0, 32 },
	{   512,  1024, 0x1f20, 0x1f3f, 512, 0x0080, 0x0080,     0, 32 },
	{  1024,  2048, 0x1f3f, 0x1f3f,   0, 0x0080, 0x00fc,  1024, 32 },
	{  2048,  4096, 0x1f3f, 0x1f3f,   0, 0x0180, 0x01fc,  2048, 32 },
	{  4096,  8192, 0x1f3f, 0x1f3f,   0, 0x0380, 0x03fc,  4096, 32 },
	{  8192, 16384, 0x1f3f, 0x1f3f,   0, 0x0780, 0x07fc,  8192, 32 },
	{ 16384, 32768, 0x1f3f, 0x1f3f,   0, 0x0f80, 0x0ffc, 16384, 32 }
};

static int sc210iot_set_gain(struct sc210iot *sc210iot, u32 gain)
{
	int ret, i = 0;
	int offset, step, step_len, reg_step_len;
	int a_gain = 0, d_gain = 0;

	dev_dbg(sc210iot->dev, "%s: gain : %d\n", __func__, gain);
	if (sc210iot->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
		sc210iot->is_thunderboot = false;
		sc210iot->is_thunderboot_ng = true;
		__sc210iot_power_on(sc210iot);
	}

	for (i = 0; i < ARRAY_SIZE(gain_sections) - 1; i++)
		if ((gain_sections[i].min_gain <= gain) && (gain < gain_sections[i].max_gain))
			break;
	if (gain_sections[i].again_deviation) {
		offset = gain - gain_sections[i].min_gain;
		step_len = gain_sections[i].again_deviation / gain_sections[i].steps;
		reg_step_len = 1;
		step = offset / step_len;
		a_gain = gain_sections[i].again_regs_start + step * reg_step_len;
		d_gain = gain_sections[i].dgain_regs_start;
	} else {
		offset = gain - gain_sections[i].min_gain;
		step_len = gain_sections[i].dgain_deviation / gain_sections[i].steps;
		step = offset / step_len;
		reg_step_len = 4;
		a_gain = gain_sections[i].again_regs_start;
		d_gain = gain_sections[i].dgain_regs_start + step * reg_step_len;
	}

	if (a_gain > gain_sections[i].again_regs_stop)
		a_gain = gain_sections[i].again_regs_stop;
	if (d_gain > gain_sections[i].dgain_regs_stop)
		d_gain = gain_sections[i].dgain_regs_stop;

	dev_dbg(sc210iot->dev, "%s: a_gain: 0x%x d_gain: 0x%x\n", __func__, a_gain, d_gain);

	ret  = sc210iot_write_reg(sc210iot, SC210IOT_REG_GAIN_LONG_1, a_gain >> 8);
	ret |= sc210iot_write_reg(sc210iot, SC210IOT_REG_GAIN_LONG_0, a_gain & 0xff);
	ret |= sc210iot_write_reg(sc210iot, SC210IOT_REG_GAIN_LONG_3, d_gain >> 8);
	ret |= sc210iot_write_reg(sc210iot, SC210IOT_REG_GAIN_LONG_2, d_gain & 0xff);
	return ret;
}

static int sc210iot_set_exp(struct sc210iot *sc210iot, u32 exp)
{
	int ret;

	dev_dbg(sc210iot->dev, "%s: exp : %d\n", __func__, exp);
	ret  = sc210iot_write_reg(sc210iot, SC210IOT_REG_EXP_LONG_H,
					(exp >> 12) & 0xf);
	ret |= sc210iot_write_reg(sc210iot, SC210IOT_REG_EXP_LONG_M,
					(exp >> 4) & 0xff);
	ret |= sc210iot_write_reg(sc210iot, SC210IOT_REG_EXP_LONG_L,
					(exp & 0xf) << 4);
	return ret;
}

static int sc210iot_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc210iot *sc210iot = container_of(ctrl->handler,
					     struct sc210iot, ctrl_handler);
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc210iot->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(sc210iot->exposure,
					 sc210iot->exposure->minimum, max,
					 sc210iot->exposure->step,
					 sc210iot->exposure->default_value);
		break;
	}
	if (!pm_runtime_get_if_in_use(sc210iot->dev))
		return 0;
	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = sc210iot_set_exp(sc210iot, ctrl->val << 1);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = sc210iot_set_gain(sc210iot, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		dev_dbg(sc210iot->dev, "set vblank 0x%x\n", ctrl->val);
		ret = sc210iot_write_reg(sc210iot, SC210IOT_REG_VTS_H,
					(ctrl->val + sc210iot->cur_mode->height) >> 8);
		ret = sc210iot_write_reg(sc210iot, SC210IOT_REG_VTS_L,
					(ctrl->val + sc210iot->cur_mode->height) & 0xff);
		break;
	case V4L2_CID_HFLIP:
		regmap_update_bits(sc210iot->regmap, SC210IOT_REG_MIRROR_FLIP,
				   MIRROR_MASK, ctrl->val ? MIRROR_MASK : 0);
		break;
	case V4L2_CID_VFLIP:
		regmap_update_bits(sc210iot->regmap, SC210IOT_REG_MIRROR_FLIP,
				   FLIP_MASK,  ctrl->val ? FLIP_MASK : 0);
		break;
	default:
		dev_warn(sc210iot->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}
	pm_runtime_put(sc210iot->dev);
	return ret;
}

static const struct v4l2_ctrl_ops sc210iot_ctrl_ops = {
	.s_ctrl = sc210iot_set_ctrl,
};

static int sc210iot_get_regulators(struct sc210iot *sc210iot)
{
	unsigned int i;

	for (i = 0; i < SC210IOT_NUM_SUPPLIES; i++)
		sc210iot->supplies[i].supply = sc210iot_supply_names[i];
	return devm_regulator_bulk_get(sc210iot->dev,
				       SC210IOT_NUM_SUPPLIES,
				       sc210iot->supplies);
}

static int sc210iot_initialize_controls(struct sc210iot *sc210iot)
{
	const struct sc210iot_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &sc210iot->ctrl_handler;
	mode = sc210iot->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &sc210iot->lock;
	sc210iot->link_freq = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
						  ARRAY_SIZE(link_freq_menu_items) - 1, 0,
						  link_freq_menu_items);
	sc210iot->pixel_rate = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
					      0, to_pixel_rate(LINK_FREQ_INDEX),
					      1, to_pixel_rate(LINK_FREQ_INDEX));
	h_blank = mode->hts_def - mode->width;
	sc210iot->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					  h_blank, h_blank, 1, h_blank);
	if (sc210iot->hblank)
		sc210iot->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	vblank_def = mode->vts_def - mode->height;
	sc210iot->vblank = v4l2_ctrl_new_std(handler, &sc210iot_ctrl_ops,
					  V4L2_CID_VBLANK, vblank_def,
					  SC210IOT_VTS_MAX - mode->height,
					  1, vblank_def);
	exposure_max = mode->vts_def - 4;
	sc210iot->exposure = v4l2_ctrl_new_std(handler, &sc210iot_ctrl_ops,
					    V4L2_CID_EXPOSURE, SC210IOT_EXPOSURE_MIN,
					    exposure_max, SC210IOT_EXPOSURE_STEP,
					    mode->exp_def);
	sc210iot->anal_gain = v4l2_ctrl_new_std(handler, &sc210iot_ctrl_ops,
					     V4L2_CID_ANALOGUE_GAIN, SC210IOT_GAIN_MIN,
					     SC210IOT_GAIN_MAX, SC210IOT_GAIN_STEP,
					     SC210IOT_GAIN_DEFAULT);
	sc210iot->h_flip = v4l2_ctrl_new_std(handler, &sc210iot_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	sc210iot->v_flip = v4l2_ctrl_new_std(handler, &sc210iot_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (handler->error) {
		ret = handler->error;
		dev_err(sc210iot->dev, "Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}
	sc210iot->subdev.ctrl_handler = handler;
	sc210iot->has_init_exp = false;
	return 0;
err_free_handler:
	v4l2_ctrl_handler_free(handler);
	return ret;
}

static int __sc210iot_power_on(struct sc210iot *sc210iot)
{
	int ret;
	struct device *dev = sc210iot->dev;

	if (sc210iot->is_thunderboot)
		return 0;

	if (!IS_ERR_OR_NULL(sc210iot->pins_default)) {
		ret = pinctrl_select_state(sc210iot->pinctrl,
					   sc210iot->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(sc210iot->xvclk, SC210IOT_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate\n");
	if (clk_get_rate(sc210iot->xvclk) != SC210IOT_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 27MHz\n");
	ret = clk_prepare_enable(sc210iot->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	ret = regulator_bulk_enable(SC210IOT_NUM_SUPPLIES, sc210iot->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}
	if (!IS_ERR(sc210iot->reset_gpio))
		gpiod_direction_output(sc210iot->reset_gpio, 1);
	usleep_range(1000, 2000);
	if (!IS_ERR(sc210iot->pwdn_gpio))
		gpiod_direction_output(sc210iot->pwdn_gpio, 1);
	if (!IS_ERR(sc210iot->reset_gpio))
		gpiod_direction_output(sc210iot->reset_gpio, 0);
	usleep_range(10000, 20000);
	return 0;
disable_clk:
	clk_disable_unprepare(sc210iot->xvclk);

	if (!IS_ERR_OR_NULL(sc210iot->pins_sleep))
		pinctrl_select_state(sc210iot->pinctrl, sc210iot->pins_sleep);

	return ret;
}

static void __sc210iot_power_off(struct sc210iot *sc210iot)
{
	int ret;
	struct device *dev = sc210iot->dev;

	if (sc210iot->is_thunderboot) {
		if (sc210iot->is_first_streamoff) {
			sc210iot->is_thunderboot = false;
			sc210iot->is_first_streamoff = false;
		} else {
			return;
		}
	}
	if (!IS_ERR_OR_NULL(sc210iot->pins_sleep)) {
		ret = pinctrl_select_state(sc210iot->pinctrl,
					   sc210iot->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	if (!IS_ERR(sc210iot->reset_gpio))
		gpiod_direction_output(sc210iot->reset_gpio, 1);
	if (!IS_ERR(sc210iot->pwdn_gpio))
		gpiod_direction_output(sc210iot->pwdn_gpio, 0);
	if (sc210iot->is_thunderboot_ng) {
		sc210iot->is_thunderboot_ng = false;
		regulator_bulk_disable(SC210IOT_NUM_SUPPLIES, sc210iot->supplies);
	}
	clk_disable_unprepare(sc210iot->xvclk);
}

static int sc210iot_check_sensor_id(struct sc210iot *sc210iot)
{
	u8 id_h = 0, id_l = 0;
	u16 id = 0;
	int ret = 0;

	if (sc210iot->is_thunderboot) {
		dev_info(sc210iot->dev, "Enable thunderboot mode, skip sensor id check\n");
		return 0;
	}

	ret = sc210iot_read_reg(sc210iot, SC210IOT_REG_CHIP_ID_H, &id_h);
	ret |= sc210iot_read_reg(sc210iot, SC210IOT_REG_CHIP_ID_L, &id_l);
	if (ret) {
		dev_err(sc210iot->dev, "Failed to read sensor id, (%d)\n", ret);
		return ret;
	}
	id = id_h << 8 | id_l;
	if (id != SC210IOT_CHIP_ID) {
		dev_err(sc210iot->dev, "sensor id: %04X mismatched\n", id);
		return -ENODEV;
	}
	dev_info(sc210iot->dev, "Detected SC210IOT sensor\n");
	return 0;
}

static void sc210iot_get_module_inf(struct sc210iot *sc210iot,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.lens, sc210iot->len_name, sizeof(inf->base.lens));
	strlcpy(inf->base.sensor, SC210IOT_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, sc210iot->module_name, sizeof(inf->base.module));
}

static long sc210iot_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc210iot *sc210iot = to_sc210iot(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = sc210iot->cur_mode->hdr_mode;
		break;
	case RKMODULE_GET_MODULE_INFO:
		sc210iot_get_module_inf(sc210iot, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_HDR_CFG:
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = sc210iot_write_reg(sc210iot,
						 SC210IOT_REG_CTRL_MODE,
						 SC210IOT_MODE_STREAMING);
		else
			ret = sc210iot_write_reg(sc210iot,
						 SC210IOT_REG_CTRL_MODE,
						 SC210IOT_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

static int __sc210iot_start_stream(struct sc210iot *sc210iot)
{
	int ret = 0;

	if (!sc210iot->is_thunderboot) {
		ret = regmap_multi_reg_write(sc210iot->regmap,
					     sc210iot->cur_mode->reg_list,
					     sc210iot->cur_mode->reg_num);
		if (ret)
			return ret;
	}
	__v4l2_ctrl_handler_setup(&sc210iot->ctrl_handler);
	return sc210iot_write_reg(sc210iot,
				  SC210IOT_REG_CTRL_MODE,
				  SC210IOT_MODE_STREAMING);
}

static int __sc210iot_stop_stream(struct sc210iot *sc210iot)
{
	sc210iot->has_init_exp = false;
	if (sc210iot->is_thunderboot)
		sc210iot->is_first_streamoff = true;
	return sc210iot_write_reg(sc210iot, SC210IOT_REG_CTRL_MODE,
				  SC210IOT_MODE_SW_STANDBY);
}

#ifdef CONFIG_COMPAT
static long sc210iot_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_hdr_cfg *hdr;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}
		ret = sc210iot_ioctl(sd, cmd, inf);
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
		ret = sc210iot_ioctl(sd, cmd, hdr);
		if (!ret)
			ret = copy_to_user(up, hdr, sizeof(*hdr));
		kfree(hdr);
		break;
	case RKMODULE_SET_HDR_CFG:
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = sc210iot_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}
#endif

static int sc210iot_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc210iot *sc210iot = to_sc210iot(sd);
	int ret = 0;

	mutex_lock(&sc210iot->lock);
	on = !!on;
	if (on == sc210iot->streaming)
		goto unlock_and_return;
	if (on) {
		if (sc210iot->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
			sc210iot->is_thunderboot = false;
			__sc210iot_power_on(sc210iot);
		}
		ret = pm_runtime_get_sync(sc210iot->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(sc210iot->dev);
			goto unlock_and_return;
		}
		ret = __sc210iot_start_stream(sc210iot);
		if (ret) {
			dev_err(sc210iot->dev, "Failed to start sc210iot stream\n");
			pm_runtime_put(sc210iot->dev);
			goto unlock_and_return;
		}
	} else {
		__sc210iot_stop_stream(sc210iot);
		pm_runtime_put(sc210iot->dev);
	}
	sc210iot->streaming = on;
unlock_and_return:
	mutex_unlock(&sc210iot->lock);
	return 0;
}

static int sc210iot_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct sc210iot *sc210iot = to_sc210iot(sd);
	const struct sc210iot_mode *mode = sc210iot->cur_mode;

	mutex_lock(&sc210iot->lock);
	fi->interval = mode->max_fps;
	mutex_unlock(&sc210iot->lock);
	return 0;
}

static int sc210iot_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	struct sc210iot *sc210iot = to_sc210iot(sd);

	u32 val = 1 << (SC210IOT_LANES - 1) | V4L2_MBUS_CSI2_CHANNEL_0 |
		  V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2;
	config->flags = (sc210iot->cur_mode->hdr_mode == NO_HDR) ?
			val : (val | V4L2_MBUS_CSI2_CHANNEL_1);
	return 0;
}

static int sc210iot_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = SC210IOT_MEDIA_BUS_FMT;
	return 0;
}

static int sc210iot_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct sc210iot *sc210iot = to_sc210iot(sd);

	if (fse->index >= sc210iot->cfg_num)
		return -EINVAL;
	if (fse->code != SC210IOT_MEDIA_BUS_FMT)
		return -EINVAL;
	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;
	return 0;
}

static int sc210iot_enum_frame_interval(struct v4l2_subdev *sd,
						  struct v4l2_subdev_pad_config *cfg,
						  struct v4l2_subdev_frame_interval_enum *fie)
{
	struct sc210iot *sc210iot = to_sc210iot(sd);

	if (fie->index >= sc210iot->cfg_num)
		return -EINVAL;
	fie->code = SC210IOT_MEDIA_BUS_FMT;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static int sc210iot_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sc210iot *sc210iot = to_sc210iot(sd);
	const struct sc210iot_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&sc210iot->lock);
	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);
	fmt->format.code = SC210IOT_MEDIA_BUS_FMT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc210iot->lock);
		return -ENOTTY;
#endif
	} else {
		sc210iot->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(sc210iot->link_freq, mode->link_freq_index);
		__v4l2_ctrl_s_ctrl_int64(sc210iot->pixel_rate,
					 to_pixel_rate(mode->link_freq_index));
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc210iot->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc210iot->vblank, vblank_def,
					 SC210IOT_VTS_MAX - mode->height,
					 1, vblank_def);
	}
	mutex_unlock(&sc210iot->lock);
	return 0;
}

static int sc210iot_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sc210iot *sc210iot = to_sc210iot(sd);
	const struct sc210iot_mode *mode = sc210iot->cur_mode;

	mutex_lock(&sc210iot->lock);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc210iot->lock);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = SC210IOT_MEDIA_BUS_FMT;
		fmt->format.field = V4L2_FIELD_NONE;
		fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&sc210iot->lock);
	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc210iot_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc210iot *sc210iot = to_sc210iot(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc210iot_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc210iot->lock);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = SC210IOT_MEDIA_BUS_FMT;
	try_fmt->field = V4L2_FIELD_NONE;
	mutex_unlock(&sc210iot->lock);
	return 0;
}
#endif
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc210iot_internal_ops = {
	.open = sc210iot_open,
};
#endif

static int sc210iot_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc210iot *sc210iot = to_sc210iot(sd);
	int ret = 0;

	mutex_lock(&sc210iot->lock);
	if (sc210iot->power_on == !!on)
		goto unlock_and_return;
	if (on) {
		ret = pm_runtime_get_sync(sc210iot->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(sc210iot->dev);
			goto unlock_and_return;
		}
		if (!sc210iot->is_thunderboot) {
			ret |= sc210iot_write_reg(sc210iot,
						  SC210IOT_SOFTWARE_RESET_REG, 0x01);
			usleep_range(100, 200);
		}
		sc210iot->power_on = true;
	} else {
		pm_runtime_put(sc210iot->dev);
		sc210iot->power_on = false;
	}
unlock_and_return:
	mutex_unlock(&sc210iot->lock);
	return ret;
}

static const struct v4l2_subdev_core_ops sc210iot_core_ops = {
	.s_power = sc210iot_s_power,
	.ioctl = sc210iot_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc210iot_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc210iot_video_ops = {
	.s_stream = sc210iot_s_stream,
	.g_frame_interval = sc210iot_g_frame_interval,
	.g_mbus_config = sc210iot_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops sc210iot_pad_ops = {
	.enum_mbus_code = sc210iot_enum_mbus_code,
	.enum_frame_size = sc210iot_enum_frame_sizes,
	.enum_frame_interval = sc210iot_enum_frame_interval,
	.get_fmt = sc210iot_get_fmt,
	.set_fmt = sc210iot_set_fmt,
};

static const struct v4l2_subdev_ops sc210iot_subdev_ops = {
	.core   = &sc210iot_core_ops,
	.video  = &sc210iot_video_ops,
	.pad    = &sc210iot_pad_ops,
};

static int sc210iot_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc210iot *sc210iot = to_sc210iot(sd);

	__sc210iot_power_on(sc210iot);
	return 0;
}

static int sc210iot_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc210iot *sc210iot = to_sc210iot(sd);

	__sc210iot_power_off(sc210iot);
	return 0;
}

static const struct dev_pm_ops sc210iot_pm_ops = {
	SET_RUNTIME_PM_OPS(sc210iot_runtime_suspend,
			   sc210iot_runtime_resume, NULL)
};

static int sc210iot_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc210iot *sc210iot;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);
	sc210iot = devm_kzalloc(dev, sizeof(*sc210iot), GFP_KERNEL);
	if (!sc210iot)
		return -ENOMEM;
	sc210iot->dev = dev;
	sc210iot->regmap = devm_regmap_init_i2c(client, &sc210iot_regmap_config);
	if (IS_ERR(sc210iot->regmap)) {
		dev_err(dev, "Failed to initialize I2C\n");
		return -ENODEV;
	}
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc210iot->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc210iot->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc210iot->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc210iot->len_name);
	if (ret) {
		dev_err(dev, "Failed to get module information\n");
		return -EINVAL;
	}

	sc210iot->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);

	sc210iot->xvclk = devm_clk_get(sc210iot->dev, "xvclk");
	if (IS_ERR(sc210iot->xvclk)) {
		dev_err(sc210iot->dev, "Failed to get xvclk\n");
		return -EINVAL;
	}
	sc210iot->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(sc210iot->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");
	sc210iot->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_ASIS);
	if (IS_ERR(sc210iot->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");
	ret = sc210iot_get_regulators(sc210iot);
	if (ret) {
		dev_err(dev, "Failed to get regulators\n");
		return ret;
	}
	sc210iot->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc210iot->pinctrl)) {
		sc210iot->pins_default =
			pinctrl_lookup_state(sc210iot->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc210iot->pins_default))
			dev_info(dev, "could not get default pinstate\n");

		sc210iot->pins_sleep =
			pinctrl_lookup_state(sc210iot->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc210iot->pins_sleep))
			dev_info(dev, "could not get sleep pinstate\n");
	} else {
		dev_info(dev, "no pinctrl\n");
	}
	mutex_init(&sc210iot->lock);
	/* set default mode */
	sc210iot->cur_mode = &supported_modes[0];
	sc210iot->cfg_num = ARRAY_SIZE(supported_modes);
	sd = &sc210iot->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc210iot_subdev_ops);
	ret = sc210iot_initialize_controls(sc210iot);
	if (ret)
		goto err_destroy_mutex;
	ret = __sc210iot_power_on(sc210iot);
	if (ret)
		goto err_free_handler;
	ret = sc210iot_check_sensor_id(sc210iot);
	if (ret)
		goto err_power_off;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc210iot_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#ifdef CONFIG_MEDIA_CONTROLLER
	sc210iot->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc210iot->pad);
	if (ret < 0)
		goto err_power_off;
#endif
	memset(facing, 0, sizeof(facing));
	if (strcmp(sc210iot->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';
	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sc210iot->module_index, facing,
		 SC210IOT_NAME, dev_name(sd->dev));
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
	__sc210iot_power_off(sc210iot);
err_free_handler:
	v4l2_ctrl_handler_free(&sc210iot->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc210iot->lock);
	return ret;
}

static int sc210iot_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc210iot *sc210iot = to_sc210iot(sd);

	v4l2_async_unregister_subdev(sd);
#ifdef CONFIG_MEDIA_CONTROLLER
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc210iot->ctrl_handler);
	mutex_destroy(&sc210iot->lock);
	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc210iot_power_off(sc210iot);
	pm_runtime_set_suspended(&client->dev);
	return 0;
}

static const struct i2c_device_id sc210iot_match_id[] = {
	{ "sc210iot", 0 },
	{ },
};

static const struct of_device_id sc210iot_of_match[] = {
	{ .compatible = "smartsens,sc210iot" },
	{},
};

MODULE_DEVICE_TABLE(of, sc210iot_of_match);

static struct i2c_driver sc210iot_i2c_driver = {
	.driver = {
		.name = SC210IOT_NAME,
		.pm = &sc210iot_pm_ops,
		.of_match_table = of_match_ptr(sc210iot_of_match),
	},
	.probe      = &sc210iot_probe,
	.remove     = &sc210iot_remove,
	.id_table   = sc210iot_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc210iot_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc210iot_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Smartsens sc210iot Image Sensor driver");
MODULE_LICENSE("GPL v2");
